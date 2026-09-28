#pragma once
#include "ElementRegistry.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace FfxHooks::F7Elements {
enum class Affinity : std::uint8_t {Unchanged,Weak,Resist,Absorb};
struct Extra {std::array<char,65> key{};Affinity affinity=Affinity::Unchanged;};
using Extras=std::array<Extra,2>;
struct Selection {std::uint8_t weak=0,resist=0,absorb=0;Extras extra{};};
// Owner-thread publication after the native Difficulty transaction succeeds.
// Editing or saving requested configuration alone cannot change a running battle.
class AppliedSelection {
    struct Actor {std::uintptr_t address=0;std::uint16_t formation=0xFFFF;};
    std::array<Actor,8> actors_{};
    Selection selection_{};
    std::uint64_t generation_=0;
public:
    void Clear() noexcept {generation_=0;actors_={};selection_={};}
    void Begin(std::uint64_t generation,const Selection& selection) noexcept {Clear();generation_=generation;selection_=selection;}
    void Admit(unsigned enemySlot,std::uintptr_t address,std::uint16_t formation) noexcept {
        if(enemySlot<actors_.size()&&address&&formation!=0xFFFF)actors_[enemySlot]={address,formation};
    }
    bool Read(std::uint64_t generation,unsigned nativeSlot,std::uintptr_t address,std::uint16_t formation,Selection& output) const noexcept {
        output={};if(!generation||generation!=generation_||nativeSlot<18||nativeSlot>=26)return false;
        const auto& actor=actors_[nativeSlot-18];if(!actor.address||actor.address!=address||actor.formation!=formation)return false;
        output=selection_;return true;
    }
};
inline bool Valid(const Extras& value) noexcept {
    for(unsigned i=0;i<value.size();++i){const auto& item=value[i];
        if(!std::memchr(item.key.data(),0,item.key.size())||item.affinity>Affinity::Absorb)return false;
        if(item.key[0]&&!ElementalDominion::ValidKey(item.key.data()))return false;
        if(!item.key[0]&&item.affinity!=Affinity::Unchanged)return false;
        if(i&&item.key[0]&&std::strcmp(item.key.data(),value[0].key.data())==0)return false;
    }return true;
}
inline bool Base(const Selection& selected,unsigned nativeBit,const char* key,std::int32_t& value) noexcept {
    Affinity mode=Affinity::Unchanged;
    if(nativeBit){if(nativeBit>0x80||(nativeBit&(nativeBit-1)))return false;
        if(selected.absorb&nativeBit)mode=Affinity::Absorb;
        else if(selected.resist&nativeBit)mode=Affinity::Resist;
        else if(selected.weak&nativeBit)mode=Affinity::Weak;
    }else if(key&&Valid(selected.extra))for(const auto& item:selected.extra)
        if(item.key[0]&&std::strcmp(key,item.key.data())==0){mode=item.affinity;break;}
    switch(mode){case Affinity::Weak:value=15000;return true;case Affinity::Resist:value=5000;return true;
        case Affinity::Absorb:value=-10000;return true;default:return false;}
}
struct Provider {bool (*read)(unsigned,std::uintptr_t,Selection&) noexcept=nullptr;};
inline std::atomic<const Provider*> provider{nullptr};
inline bool Register(const Provider* value) noexcept {if(!value||!value->read)return false;const Provider* empty=nullptr;return provider.compare_exchange_strong(empty,value)||empty==value;}
inline void Unregister(const Provider* value) noexcept {provider.compare_exchange_strong(value,nullptr);}
inline bool Read(unsigned actor,std::uintptr_t address,Selection& output) noexcept {
    output={};const auto* source=provider.load();Selection candidate{};
    if(actor<18||actor>=26||!source||!source->read||!source->read(actor,address,candidate)||provider.load()!=source||!Valid(candidate.extra))return false;
    output=candidate;return true;
}
}
