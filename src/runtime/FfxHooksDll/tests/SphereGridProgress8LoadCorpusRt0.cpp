// Jarvis-HOOK: read-only corpus through the actual load-binding core/CRC.
// File SHA-256 names are verified by the runner. Path identities are explicit
// unique fixtures, not a test of Windows canonicalization or a native loader.
#include "../hooks/SphereGridProgress8LoadCore.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
namespace L=FfxHooks::SphereGridProgress8Load;
namespace R=FfxHooks::RonsoPool;
static unsigned checks=0,failures=0;
static void Check(bool value,const char* reason){++checks;if(!value){++failures;std::printf("FAIL: %s\n",reason);}}
struct Fixture {R::SaveImage image{};L::Identity identity{};};
static int Hex(char value){
    if(value>='0'&&value<='9')return value-'0';
    if(value>='a'&&value<='f')return value-'a'+10;
    return -1;
}
int main(int argc,char** argv){
    if(argc<2)return 2;
    std::vector<Fixture> corpus;
    for(int i=1;i<argc;++i){
        const std::filesystem::path path(argv[i]);const auto name=path.filename().string();
        if(name.size()!=64||std::filesystem::file_size(path)!=R::kSaveSize)return 3;
        Fixture fixture;fixture.identity.path.fill(static_cast<unsigned char>(i));
        for(unsigned j=0;j<32;++j){
            const int a=Hex(name[j*2]),b=Hex(name[j*2+1]);if(a<0||b<0)return 4;
            fixture.identity.image[j]=static_cast<unsigned char>(a*16+b);
        }
        std::ifstream input(path,std::ios::binary);
        if(!input.read(reinterpret_cast<char*>(fixture.image.data()),fixture.image.size()))return 5;
        Check(R::IsValidSave(fixture.image),"each real corpus file satisfies the native checksum contract");
        corpus.push_back(std::move(fixture));
    }
    Check(corpus.size()==21,"complete pinned unique FFX corpus is present");
    if(corpus.size()!=21||failures)return 1;
    constexpr std::uintptr_t destination=0x112ca90,bufferA=0x1000,bufferB=0x2000;
    constexpr std::uint32_t thread=7;
    constexpr std::uint64_t request=3;
    const FfxHooks::SeymourSession::Token session{11,thread};
    unsigned pairs=0,cleared=0,sameBytes=0;
    for(std::size_t i=0;i<corpus.size();++i){
        const auto& f=corpus[i];
        for(std::size_t j=0;j<corpus.size();++j){
            L::Tracker tracker(destination);
            Check(tracker.ReadCompleted(bufferA,f.identity,f.image),"observed source record is accepted");
            tracker.Begin(1,destination,bufferA,thread,request,&corpus[j].image);
            const bool accepted=tracker.End(1,true,thread,session,corpus[j].image.data()+64,R::kSaveSize-64);
            Check(accepted==(i==j),"all ordered corpus pairs require the exact completed-read image");
            const auto claim=tracker.Capture(session,request);
            Check(claim.Valid()==(i==j),"a different save cannot supply companion identity to this load");
            if(i==j)Check(claim.save==f.identity,"accepted corpus claim retains its complete identity");
            ++pairs;
        }
        L::Tracker tracker(destination);auto normalized=f.image;
        Check(tracker.ReadCompleted(bufferA,f.identity,f.image),"CRC-clear fixture observed original disk image");
        for(unsigned j=0;j<4;++j)normalized[25844+j]=0;
        tracker.Begin(1,destination,bufferA,thread,request,&normalized);
        Check(tracker.End(1,true,thread,session,normalized.data()+64,R::kSaveSize-64),"legitimate native CRC clear works for every corpus save");
        Check(tracker.Capture(session,request).save==f.identity,"normalized load preserves original on-disk hash");++cleared;
        auto other=f.identity;other.path[31]^=0x80;
        Check(tracker.ReadCompleted(bufferB,other,f.image),"equal bytes from another path get independent provenance");
        tracker.ReadStarting(bufferA);tracker.Begin(2,destination,bufferA,thread,request,&f.image);
        Check(!tracker.End(2,true,thread,{12,thread},f.image.data()+64,R::kSaveSize-64),"another path's equal bytes never revive invalidated buffer A");
        tracker.Begin(3,destination,bufferB,thread,request,&f.image);
        Check(tracker.End(3,true,thread,{13,thread},f.image.data()+64,R::kSaveSize-64)&&
              tracker.Capture({13,thread},request).save==other,"buffer B retains its own path even with identical bytes");++sameBytes;
        for(const auto offset:{std::size_t(0),std::size_t(100),R::kSaveSize-65}){
            L::Tracker corrupt(destination);Check(corrupt.ReadCompleted(bufferA,f.identity,f.image),"corruption fixture has observed input");
            corrupt.Begin(1,destination,bufferA,thread,request,&f.image);auto actual=f.image;actual[64+offset]^=1;
            Check(!corrupt.End(1,true,thread,session,actual.data()+64,R::kSaveSize-64),"corrupted native-copy payload is rejected");
        }
    }
    std::printf("SphereGridProgress8LoadCorpusRt0: %u/%u passed; files=%zu pairs=%u cleared_crc=%u same_bytes_paths=%u\n",
        checks-failures,checks,corpus.size(),pairs,cleared,sameBytes);
    return failures?1:0;
}
