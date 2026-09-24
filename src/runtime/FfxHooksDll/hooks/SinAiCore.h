#pragma once
#include "SinAiProfiles.generated.h"
#include "SinSpreadCore.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace FfxHooks::SinAi {
inline constexpr std::uint32_t kRegisterRva=0x00397A50u,kRegisterCallerRva=0x0038417Cu;
inline constexpr std::uint32_t kMonster2RootRva=0x00D2A934u;
inline constexpr std::size_t kMaxPack=4u*1024u*1024u,kMaxScript=65536u;
inline std::uint16_t U16(const std::uint8_t* p){return static_cast<std::uint16_t>(p[0]|(p[1]<<8));}
inline std::uint32_t U32(const std::uint8_t* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
inline std::uint64_t U64(const std::uint8_t* p){return U32(p)|(std::uint64_t(U32(p+4))<<32);}
inline std::uint64_t Hash(const std::uint8_t* data,std::size_t count){std::uint64_t hash=14695981039346656037ULL;for(std::size_t i=0;i<count;++i)hash=(hash^data[i])*1099511628211ULL;return hash;}
struct View {const Proof* proof=nullptr;const std::uint8_t* bytes=nullptr;};
enum class PackCode {Ok,InvalidSize,InvalidHeader,UnknownProfile,Duplicate,InvalidPayload,Incomplete};
struct Pack {
    std::array<View,kProofs.size()> views{};
    bool ready=false;
    PackCode Load(const std::uint8_t* bytes,std::size_t length){
        ready=false;views={};
        if(!bytes || length<12 || length>kMaxPack)return PackCode::InvalidSize;
        if(std::memcmp(bytes,"SINAI001",8)!=0 || U32(bytes+8)!=views.size())return PackCode::InvalidHeader;
        std::size_t at=12;
        for(std::size_t record=0;record<views.size();++record){
            if(length-at<24)return PackCode::Incomplete;
            const auto monster=U16(bytes+at);const auto curse=bytes[at+2];
            const auto original=U64(bytes+at+4);const auto size=U32(bytes+at+12);const auto hash=U64(bytes+at+16);
            if(bytes[at+3]!=0)return PackCode::InvalidHeader;
            at+=24;std::size_t index=views.size();
            for(std::size_t i=0;i<kProofs.size();++i)if(kProofs[i].monster==monster&&kProofs[i].curse==curse){index=i;break;}
            if(index==views.size())return PackCode::UnknownProfile;
            if(views[index].bytes)return PackCode::Duplicate;
            const auto& proof=kProofs[index];
            if(size!=proof.size || original!=proof.originalHash || hash!=proof.hash || size>length-at || size<0x38 || size>kMaxScript)return PackCode::InvalidPayload;
            const auto declared=U32(bytes+at+0x10);
            if(Hash(bytes+at,size)!=hash || declared<0x38 || declared>size || size-declared>64)return PackCode::InvalidPayload;
            for(std::size_t pad=declared;pad<size;++pad)if(bytes[at+pad]!=0)return PackCode::InvalidPayload;
            const auto codeSize=U32(bytes+at),codeStart=U32(bytes+at+0x30);const auto workers=U16(bytes+at+0x36);
            if(codeStart>declared || codeSize>declared-codeStart || workers==0 || workers>64 || 0x38u+workers*4u>codeStart)return PackCode::InvalidPayload;
            views[index]={&proof,bytes+at};at+=size;
        }
        if(at!=length)return PackCode::InvalidSize;
        ready=true;return PackCode::Ok;
    }
    const View* Find(unsigned monster,unsigned curse) const {
        if(!ready)return nullptr;
        for(const auto& view:views)if(view.proof->monster==monster&&view.proof->curse==curse)return &view;
        return nullptr;
    }
};
inline bool MonsterName(const char* text,unsigned* monster){
    if(!text || !monster || text[0]!='m')return false;
    unsigned value=0;
    for(unsigned i=1;i<=3;++i){if(text[i]<'0'||text[i]>'9')return false;value=value*10u+static_cast<unsigned>(text[i]-'0');}
    if(text[4])return false;
    *monster=value;return true;
}
struct ReadIo {void* context=nullptr;bool(*read)(void*,std::uintptr_t,void*,std::size_t)=nullptr;};
inline bool RegistrationSlot(const ReadIo& io,std::uintptr_t base,std::uintptr_t source,unsigned monster,unsigned* slot){
    if(!io.read || !slot || !base)return false;
    std::uint8_t bytes[4]{};
    if(!io.read(io.context,base+0x00D34460u,bytes,4))return false;
    const auto actors=static_cast<std::uintptr_t>(U32(bytes));
    if(actors<0x10000u || actors>0x7FFFF000u || !io.read(io.context,base+0x00D34468u+0x54u,bytes,4))return false;
    const auto ordinal=U32(bytes);if(ordinal>=8)return false;
    unsigned active=0;
    for(unsigned index=0;index<8;++index){
        const auto actor=actors+index*0xF90u;
        if(!io.read(io.context,actor+0x48u,bytes,4))return false;
        if(!U32(bytes))continue;
        if(active++!=ordinal)continue;
        if(!io.read(io.context,actor+0xF78u,bytes,4) || U32(bytes)!=source)return false;
        if(!io.read(io.context,actor+0x0Eu,bytes,2) || U16(bytes)!=SinSpread::NativeMonsterId(static_cast<std::uint16_t>(monster)))return false;
        *slot=index;return true;
    }
    return false;
}
inline bool CommandsReady(const ReadIo& io,std::uintptr_t base){
    if(!io.read || !base)return false;
    std::uint8_t rootBytes[4]{};
    if(!io.read(io.context,base+kMonster2RootRva,rootBytes,4))return false;
    const auto root=static_cast<std::uintptr_t>(U32(rootBytes));
    if(root<0x10000u || root>0x7FFFFFFFu)return false;
    std::uint8_t header[8]{};
    if(!io.read(io.context,root,header,sizeof(header)))return false;
    const auto segments=U16(header);if(!segments||segments>16)return false;
    std::array<std::uint8_t,92> record{};
    for(const auto& command:kCommands){
        bool matched=false;
        for(unsigned i=0;i<segments;++i){
            std::uint8_t descriptor[12]{};
            if(!io.read(io.context,root+8u+i*12u,descriptor,sizeof(descriptor)))return false;
            const auto first=U16(descriptor),last=U16(descriptor+2),size=U16(descriptor+4);const auto offset=U32(descriptor+8);
            if(first>last || last>4095 || size!=command.size || size>record.size() || offset<8u+segments*12u || offset>kMaxPack)return false;
            if(command.index<first || command.index>last)continue;
            const std::size_t entryOffset=offset+std::size_t(command.index-first)*size;
            if(entryOffset>kMaxPack-size || root+entryOffset<root || !io.read(io.context,root+entryOffset,record.data(),size))return false;
            if(Hash(record.data(),size)!=command.hash)return false;
            matched=true;break;
        }
        // The game's entry lookup silently returns row zero on a missing index.
        // Such a fallback is never an admitted S.I.N. command.
        if(!matched)return false;
    }
    return true;
}
}
