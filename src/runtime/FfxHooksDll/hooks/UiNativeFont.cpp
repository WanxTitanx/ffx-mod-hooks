#include "UiNativeFont.h"
#include "UiNativeFontEvidence.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <set>
#include <setjmp.h>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif

namespace FfxHooks::UiNativeFont {
namespace Inflate {
#ifdef _MSC_VER
#pragma warning(push,0)
#endif
#include "../third_party/puff/puff.c"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
#undef local
#undef NIL
namespace {
using Bytes=std::vector<unsigned char>;
constexpr std::size_t MaxAsset=8u*1024u*1024u,MaxHeader=64u*1024u*1024u;
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::uint16_t U16(const unsigned char* p){return std::uint16_t(p[0]|(unsigned(p[1])<<8));}
std::uint32_t U32(const unsigned char* p){return std::uint32_t(U16(p))|(std::uint32_t(U16(p+2))<<16);}
std::uint64_t U64(const unsigned char* p){return std::uint64_t(U32(p))|(std::uint64_t(U32(p+4))<<32);}
Bytes Digest(const unsigned char* data,std::size_t length,bool sha){
    Bytes result(sha?32u:16u);
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    Require(BCryptOpenAlgorithmProvider(&algorithm,sha?BCRYPT_SHA256_ALGORITHM:BCRYPT_MD5_ALGORITHM,nullptr,0)>=0,"Font digest provider unavailable");
    const bool created=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    const bool ok=created&&length<=ULONG_MAX&&BCryptHashData(hash,const_cast<PUCHAR>(data),static_cast<ULONG>(length),0)>=0&&
        BCryptFinishHash(hash,result.data(),static_cast<ULONG>(result.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
    Require(ok,"Font digest failed");
#else
    unsigned size=0;Require(EVP_Digest(data,length,result.data(),&size,sha?EVP_sha256():EVP_md5(),nullptr)==1&&size==result.size(),"Font digest failed");
#endif
    return result;
}
bool Fingerprint(const Bytes& data,const char* expected){
    const auto hash=Digest(data.data(),data.size(),true);constexpr char digits[]="0123456789abcdef";
    for(std::size_t i=0;i<hash.size();++i)if(expected[2*i]!=digits[hash[i]>>4]||expected[2*i+1]!=digits[hash[i]&15])return false;
    return expected[64]==0;
}
std::uint32_t Adler(const Bytes& bytes){std::uint32_t a=1,b=0;for(auto c:bytes){a=(a+c)%65521;b=(b+a)%65521;}return (b<<16)|a;}
Bytes Unpack(const Bytes& packed,std::size_t expected){
    Require(expected>0&&expected<=65536,"Font block extent invalid");
    if(packed.size()==expected)return packed;
    Require(packed.size()>=6&&packed.size()<expected&&(packed[0]&15)==8&&(packed[0]>>4)<=7&&
        ((unsigned(packed[0])*256+packed[1])%31)==0&&!(packed[1]&32),"Font zlib block header invalid");
    Bytes out(expected);unsigned long source=static_cast<unsigned long>(packed.size()-6),destination=static_cast<unsigned long>(expected);
    int status=0;
    // puff's fixed-Huffman tables are initialized lazily. Serialize that native
    // boundary even for independent callers of this offline-capable loader.
    static std::mutex inflateMutex;
    {std::lock_guard<std::mutex> lock(inflateMutex);status=Inflate::puff(out.data(),&destination,packed.data()+2,&source);}
    Require(status==0&&destination==expected&&source==packed.size()-6,"Font deflate block rejected");
    const auto* tail=packed.data()+packed.size()-4;
    Require(Adler(out)==((std::uint32_t(tail[0])<<24)|(std::uint32_t(tail[1])<<16)|(std::uint32_t(tail[2])<<8)|tail[3]),"Font block checksum differs");
    return out;
}
class Archive {
    std::ifstream file_;std::uint64_t size_=0;Bytes header_;std::size_t count_=0,entries_=0,blocks_=0;
    Bytes Read(std::uint64_t at,std::size_t count){
        Require(count<=MaxHeader&&at<=size_&&count<=size_-at,"Font archive read outside bounds");
        Bytes out(count);file_.clear();file_.seekg(static_cast<std::streamoff>(at));
        Require(bool(file_)&&bool(file_.read(reinterpret_cast<char*>(out.data()),static_cast<std::streamsize>(count))),"Font archive read failed");return out;
    }
public:
    explicit Archive(const std::filesystem::path& path):file_(path,std::ios::binary){
        Require(bool(file_),"Original FFX font archive unavailable");file_.seekg(0,std::ios::end);const auto end=file_.tellg();
        Require(end>=32,"Font archive is truncated");size_=static_cast<std::uint64_t>(end);
        const auto first=Read(0,16);Require(!std::memcmp(first.data(),"SRYK",4),"Font archive signature differs");
        const auto count=U64(first.data()+8),bytes=std::uint64_t(U32(first.data()+4));
        Require(count>0&&count<=1000000&&bytes>=20+count*48&&bytes<=MaxHeader&&bytes<=size_-16,"Font archive header extent invalid");
        count_=static_cast<std::size_t>(count);header_=Read(0,static_cast<std::size_t>(bytes));
        Require(Digest(header_.data(),header_.size(),false)==Read(size_-16,16),"Font archive header checksum differs");
        entries_=16+count_*16;const auto names=entries_+count_*32;const auto nameBytes=U32(header_.data()+names);
        Require(nameBytes>=4&&nameBytes<=header_.size()-names,"Font archive names extent invalid");blocks_=names+nameBytes;
        Require((header_.size()-blocks_)%2==0,"Font archive block table invalid");
    }
    Bytes Asset(const Evidence::Asset& wanted){
        const auto key=Digest(reinterpret_cast<const unsigned char*>(wanted.path),std::strlen(wanted.path),false);
        std::size_t index=count_;
        for(std::size_t i=0;i<count_;++i)if(!std::memcmp(header_.data()+16+i*16,key.data(),16)){
            Require(index==count_,"Duplicate font archive member");index=i;}
        Require(index<count_,"Original font resource missing");const auto* entry=header_.data()+entries_+index*32;
        const auto first=std::uint64_t(U32(entry)),size=U64(entry+8);auto offset=U64(entry+16);
        Require(size>0&&size<=MaxAsset,"Font resource extent invalid");const auto blocks=(size+65535)/65536;
        Require(first+blocks<=(header_.size()-blocks_)/2&&offset>=header_.size(),"Font resource blocks invalid");
        Bytes result;result.reserve(static_cast<std::size_t>(size));
        for(std::uint64_t i=0;i<blocks;++i){
            const auto field=U16(header_.data()+blocks_+static_cast<std::size_t>(first+i)*2);
            const std::size_t stored=field?field:65536u,logical=static_cast<std::size_t>((std::min)(std::uint64_t(65536),size-i*65536));
            Require(offset<=size_-16&&stored<=size_-16-offset,"Font resource payload exceeds archive");
            auto decoded=Unpack(Read(offset,stored),logical);result.insert(result.end(),decoded.begin(),decoded.end());offset+=stored;
        }
        Require(result.size()==size&&Fingerprint(result,wanted.sha256),"Original font resource identity differs");return result;
    }
};
std::vector<std::uint32_t> Scalars(const unsigned char* data,std::size_t size){
    std::string bounded(reinterpret_cast<const char*>(data),size);bounded.push_back(0);std::vector<std::uint32_t> out;
    for(std::size_t at=0;at<size;){const auto* p=reinterpret_cast<const unsigned char*>(bounded.data()+at);
        if(!*p){out.push_back(0);++at;continue;}const auto bytes=UiLanguage::ScalarBytes(p);
        Require(bytes&&bytes<=size-at,"Invalid font Unicode mapping");std::uint32_t cp=*p;
        if(bytes>1){cp&=(1u<<(7-bytes))-1u;for(std::size_t i=1;i<bytes;++i)cp=(cp<<6)|(p[i]&63);}
        out.push_back(cp);at+=bytes;}
    return out;
}
struct Texture {
    Bytes bytes;unsigned width=0,height=0;std::size_t pixels=0;
    explicit Texture(Bytes value):bytes(std::move(value)){
        Require(bytes.size()>=96&&!std::memcmp(bytes.data(),"RYHPT",5),"Native font texture header differs");unsigned matches=0;
        for(std::size_t at=88;at+64<(std::min)(bytes.size(),std::size_t(16384));++at){
            if(std::memcmp(bytes.data()+at,"PTexture2D\0DXT5\0",16))continue;
            width=U32(bytes.data()+at-88);height=U32(bytes.data()+at-84);pixels=at+53;++matches;}
        Require(matches==1&&width==512&&(height==1024||height==4096)&&pixels+std::size_t(width)*height<=bytes.size(),"Native font BC3 geometry differs");
    }
    std::uint32_t Pixel(int x,int y) const {
        x=(std::clamp)(x,0,int(width)-1);y=(std::clamp)(y,0,int(height)-1);
        const auto* block=bytes.data()+pixels+(std::size_t(y/4)*(width/4)+unsigned(x/4))*16;
        const unsigned sample=unsigned(y%4)*4+unsigned(x%4);std::uint64_t alphaBits=0;
        for(unsigned i=0;i<6;++i)alphaBits|=std::uint64_t(block[2+i])<<(8*i);
        const unsigned ai=unsigned((alphaBits>>(3*sample))&7),a0=block[0],a1=block[1];unsigned a=0;
        if(ai<2)a=ai?a1:a0;else if(a0>a1)a=((8-ai)*a0+(ai-1)*a1)/7;
        else if(ai<6)a=((6-ai)*a0+(ai-1)*a1)/5;else a=ai==6?0:255;
        const unsigned c0=U16(block+8),c1=U16(block+10),ci=(U32(block+12)>>(2*sample))&3;
        auto channel=[&](unsigned shift,unsigned mask){
            const auto first=(c0>>shift)&mask,second=(c1>>shift)&mask;
            const unsigned p=mask==31?(first<<3)|(first>>2):(first<<2)|(first>>4);
            const unsigned q=mask==31?(second<<3)|(second>>2):(second<<2)|(second>>4);
            return ci==0?p:ci==1?q:ci==2?(2*p+q)/3:(p+2*q)/3;};
        return (a<<24)|(channel(11,31)<<16)|(channel(5,63)<<8)|channel(0,31);
    }
    std::uint32_t Sample(float x,float y) const {
        const int ix=static_cast<int>(std::floor(x)),iy=static_cast<int>(std::floor(y));const float fx=x-float(ix),fy=y-float(iy);
        const std::uint32_t p[]={Pixel(ix,iy),Pixel(ix+1,iy),Pixel(ix,iy+1),Pixel(ix+1,iy+1)};
        const float weights[]={(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};float a=0,colors[3]{};
        for(unsigned i=0;i<4;++i){const float alpha=float(p[i]>>24)*weights[i];a+=alpha;
            for(unsigned c=0;c<3;++c)colors[c]+=float((p[i]>>(8*c))&255)*alpha;}
        if(a<.5f)return 0;auto result=std::uint32_t(std::lround(a))<<24;
        for(unsigned c=0;c<3;++c)result|=std::uint32_t(std::lround(colors[c]/a))<<(8*c);return result;
    }
};
struct Face {
    Bytes metrics;std::vector<std::uint32_t> mapping;Texture pages[2];unsigned count=0,offset=0,pw=0,ph=0;
    Face(Archive& archive,unsigned family):metrics(archive.Asset(Evidence::Assets[family*4])),
        pages{Texture(archive.Asset(Evidence::Assets[family*4+2])),Texture(archive.Asset(Evidence::Assets[family*4+3]))}{
        const auto raw=archive.Asset(Evidence::Assets[family*4+1]);mapping=Scalars(raw.data(),raw.size());
        Require(metrics.size()>=64&&!std::memcmp(metrics.data(),"FTCX",4)&&U16(metrics.data()+4)==200,"Native font metrics header differs");
        count=U32(metrics.data()+16);offset=U32(metrics.data()+48);pw=U16(metrics.data()+40);ph=U16(metrics.data()+42);
        Require(count>0&&count<=2048&&offset>=64&&offset<=metrics.size()&&count<=metrics.size()-offset&&
            U16(metrics.data()+20)==14&&U16(metrics.data()+22)==18&&pw==128&&ph>0&&ph<=4096&&mapping.size()<=count+1,
            "Native font metrics geometry differs");
    }
    Glyph Make(unsigned index) const {
        Require(index<count,"Native glyph index exceeds metrics");Glyph glyph;glyph.advance=float(metrics[offset+index])*.25f;
        const auto& page=pages[index&1];
        const float left=float((index%18)/2)*14.f*float(page.width)/float(pw),top=float(index/18)*18.f*float(page.height)/float(ph);
        const float width=14.f*float(page.width)/float(pw),height=18.f*float(page.height)/float(ph);
        Require(left+width<=float(page.width)+.01f&&top+height<=float(page.height)+.01f,"Native glyph rectangle exceeds atlas");
        for(unsigned y=0;y<GlyphHeight;++y)for(unsigned x=0;x<GlyphWidth;++x)
            glyph.pixels[y*GlyphWidth+x]=page.Sample(left+(float(x)+.5f)*width/float(GlyphWidth)-.5f,
                float(page.height)-.5f-(top+(float(y)+.5f)*height/float(GlyphHeight)));
        return glyph;
    }
};
void AddScalars(std::set<std::uint32_t>& out,const char* text){const auto chars=Scalars(reinterpret_cast<const unsigned char*>(text),std::strlen(text));out.insert(chars.begin(),chars.end());}
Glyph WithTilde(const Glyph& base,const Glyph& donor){
    Glyph result=base;unsigned end=0;bool ink=false;
    for(unsigned y=0;y<GlyphHeight/2;++y){bool row=false;for(unsigned x=0;x<GlyphWidth;++x)row|=(donor.pixels[y*GlyphWidth+x]>>24)>24;
        if(ink&&!row){end=y;break;}ink|=row;}
    Require(end>0&&end<GlyphHeight/2,"Native tilde donor is not separated from its base");
    const int dx=static_cast<int>(std::lround((base.advance-donor.advance)*2.f));
    for(unsigned y=0;y<end;++y)for(unsigned x=0;x<GlyphWidth;++x){const int target=int(x)+dx;const auto p=donor.pixels[y*GlyphWidth+x];
        if(target>=0&&target<int(GlyphWidth)&&(p>>24))result.pixels[y*GlyphWidth+unsigned(target)]=p;}
    return result;
}
}

std::unique_ptr<Library> Library::Load(const std::filesystem::path& path,UiLanguage::Locale locale){
    Archive archive(path);Face latin(archive,0);auto result=std::make_unique<Library>();auto& map=result->glyphs_;
    for(unsigned i=0;i<latin.mapping.size()&&i<latin.count;++i)if(latin.mapping[i]>=32)map.emplace(latin.mapping[i],latin.Make(i));
    constexpr char ascii[]="0123456789 !\"#$%&'()*+,-./:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
    for(unsigned i=0;ascii[i];++i)map[static_cast<unsigned char>(ascii[i])]=latin.Make(i);
    const char16_t accents[]=u"\u00C0\u00C1\u00C2\u00C4\u00C7\u00C8\u00C9\u00CA\u00CB\u00CC\u00CD\u00CE\u00CF\u00D1\u00D2\u00D3\u00D4\u00D6\u00D9\u00DA\u00DB\u00DC\u00DF\u00E0\u00E1\u00E2\u00E4\u00E7\u00E8\u00E9\u00EA\u00EB\u00EC\u00ED\u00EE\u00EF\u00F1\u00F2\u00F3\u00F4\u00F6\u00F9\u00FA\u00FB\u00FC";
    for(unsigned i=0;accents[i];++i)map[accents[i]]=latin.Make(163-48+i);
    map[0x2026]=latin.Make(211-48);map[0x2019]=latin.Make(213-48);
    map[0x00E3]=WithTilde(map.at('a'),map.at(0x00F1));map[0x00F5]=WithTilde(map.at('o'),map.at(0x00F1));
    map[0x00C3]=WithTilde(map.at('A'),map.at(0x00D1));map[0x00D5]=WithTilde(map.at('O'),map.at(0x00D1));
    unsigned family=0;
    if(locale==UiLanguage::Locale::Japanese)family=1;else if(locale==UiLanguage::Locale::Korean)family=2;else if(locale==UiLanguage::Locale::Chinese)family=3;
    if(family){Face regional(archive,family);std::set<std::uint32_t> needed;
        for(std::size_t i=0;i<UiLanguage::EntryCount();++i)AddScalars(needed,UiLanguage::Text(UiLanguage::EntryKey(i),locale));
        for(auto name:UiLanguage::Names)AddScalars(needed,name);
        for(unsigned i=0;i<regional.mapping.size()&&i<regional.count;++i){const auto cp=regional.mapping[i];
            if(cp>=32&&needed.count(cp)&&!map.count(cp))map.emplace(cp,regional.Make(i));}}
    Require(map.size()<=1024,"UI native glyph cache exceeded its bound");return result;
}

#ifdef _WIN32
namespace {
std::filesystem::path configuredArchive;
LogFn fontLog=nullptr;
std::atomic<bool> configured{false},stopped{false},busy{false};
static_assert(std::atomic<bool>::is_always_lock_free,"font detach admission must remain lock-free");
std::atomic<const Library*> libraries[UiLanguage::LocaleCount]{};
std::atomic<bool> failed[UiLanguage::LocaleCount]{};
DWORD WINAPI LoadWorker(void* value){
    const auto index=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(value));
    try{
        if(!stopped.load()){
            auto library=Library::Load(configuredArchive,static_cast<UiLanguage::Locale>(index));
            if(!stopped.load()){
                const auto count=library->Size();libraries[index].store(library.release(),std::memory_order_release);
                if(fontLog){char line[192]{};std::snprintf(line,sizeof(line),"[ffx-hooks] UI native font ready locale=%s glyphs=%zu\n",UiLanguage::Codes[index],count);fontLog(line);}
            }
        }
    }catch(const std::exception& error){failed[index]=true;if(fontLog){char line[256]{};std::snprintf(line,sizeof(line),"[ffx-hooks] UI native font unavailable locale=%s reason=%s\n",UiLanguage::Codes[index],error.what());fontLog(line);}}
    catch(...){failed[index]=true;}
    busy=false;return 0;
}
}
void Configure(const std::filesystem::path& archive,LogFn log) noexcept {
    if(configured.load()||stopped.load())return;
    try{configuredArchive=archive;fontLog=log;configured.store(true,std::memory_order_release);}catch(...){}
}
void Request(UiLanguage::Locale locale) noexcept {
    const auto index=UiLanguage::Index(locale);
    if(!index||!configured.load(std::memory_order_acquire)||stopped.load()||libraries[index].load()||failed[index].load())return;
    bool expected=false;if(!busy.compare_exchange_strong(expected,true))return;
    HMODULE pin=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Request),&pin)||
       !QueueUserWorkItem(LoadWorker,reinterpret_cast<void*>(std::uintptr_t(index)),WT_EXECUTEDEFAULT)){busy=false;failed[index]=true;}
}
const Library* Current(UiLanguage::Locale locale) noexcept {return stopped.load()?nullptr:libraries[UiLanguage::Index(locale)].load(std::memory_order_acquire);}
bool Loading(UiLanguage::Locale locale) noexcept {
    const auto index=UiLanguage::Index(locale);return index&&configured.load()&&!stopped.load()&&!failed[index].load()&&!libraries[index].load();
}
void Stop() noexcept {
    // Published libraries remain immutable and pinned until process exit. A
    // worker may still be reading its private file; detach never joins it or
    // frees pixels that a Present callback may already be using.
    stopped=true;
}
#endif
}
