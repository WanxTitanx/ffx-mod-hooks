#include "CaptureCascadeBattleCore.h"
#include <cstring>
namespace FfxHooks::CaptureCascade {
namespace {
std::uint32_t Word(const unsigned char* bytes,unsigned count) noexcept {std::uint32_t value=0;for(unsigned i=0;i<count;++i)value|=std::uint32_t(bytes[i])<<(i*8);return value;}
}
std::uint64_t FormationHash(const unsigned char* bytes,std::size_t size) noexcept {
    if(!bytes)return 0;
    std::uint64_t hash=0xCBF29CE484222325ULL;
    for(std::size_t i=0;i<size;++i){hash^=bytes[i];hash*=0x100000001B3ULL;}return hash;
}
FormationCode BuildFormation(const FormationProof& proof,const unsigned char* source,std::size_t size,
    const Config& config,const State& state,Aeon region,const Candidate* pool,std::size_t count,std::uint32_t entropy,
    std::vector<unsigned char>& output,Selection& plan) noexcept {
    try{
        if(!source||size<0x34||size>65535||size!=proof.size||proof.slots<0x24||proof.slots>size-16||
           Word(source,4)!=8||Word(source+32,4)!=size||Word(source+12,4)+12u!=proof.slots||
           Word(source+16,4)<proof.slots+16||Word(source+16,4)>size||FormationHash(source,size)!=proof.hash)return FormationCode::Invalid;
        Slot slots[kMaximumActors]{};
        for(unsigned i=0;i<kMaximumActors;++i){
            slots[i].monster=static_cast<std::uint16_t>(Word(source+proof.slots+i*2,2));
            slots[i].replaceable=(proof.replaceable&(1u<<i))!=0;
            if(slots[i].replaceable&&(slots[i].monster<=0x1000||slots[i].monster>=0x1400))return FormationCode::Invalid;
        }
        const auto selected=Select(config,state,region,pool,count,slots,kMaximumActors,entropy);
        if(!selected.selected)return FormationCode::Unchanged;
        std::vector<unsigned char> replacement(source,source+size);
        for(unsigned i=0;i<kMaximumActors;++i)if(selected.changed&(1u<<i)){
            replacement[proof.slots+i*2]=static_cast<unsigned char>(selected.monsters[i]);
            replacement[proof.slots+i*2+1]=static_cast<unsigned char>(selected.monsters[i]>>8);
        }
        output=std::move(replacement);plan=selected;return FormationCode::Prepared;
    }catch(...){return FormationCode::Invalid;}
}
}
