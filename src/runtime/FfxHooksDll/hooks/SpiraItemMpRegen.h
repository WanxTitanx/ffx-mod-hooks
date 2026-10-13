#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
namespace FfxHooks::SpiraAbilities::ItemMpRegen {
// Spellspring is bit 2 in the item command's SpecialBuffFlags byte (+0x5A).
// Its native battle owner stores the composed special-buff byte at actor+0x640.
inline bool Convert(bool enabled,unsigned command,const unsigned char* row,unsigned width,
                    unsigned target,unsigned previous,int& nativeMask) noexcept {
    if(!enabled||(command>>12)!=2||!row||width!=96||!(row[0x5A]&4)||target>=18||nativeMask<0||!(nativeMask&4))return false;
    nativeMask=(nativeMask&~4)|static_cast<int>(previous&4);return true;
}
class State {
    struct Recipient {std::uintptr_t actor=0;std::uint64_t generation=0;std::uint32_t turn=0;};
    std::array<Recipient,18> recipients_{};
public:
    void Reset() noexcept {recipients_={};}
    void Forget(unsigned owner) noexcept {if(owner<recipients_.size())recipients_[owner]={};}
    bool Grant(unsigned owner,std::uintptr_t actor,std::uint64_t generation) noexcept {
        if(owner>=recipients_.size()||!actor||!generation)return false;
        auto& value=recipients_[owner];
        if(value.actor!=actor||value.generation!=generation)value={actor,generation,0};
        return true;
    }
    int Turn(unsigned owner,std::uintptr_t actor,std::uint64_t generation,std::uint32_t turn,int hp,int current,int maximum) noexcept {
        if(owner>=recipients_.size())return current;
        auto& value=recipients_[owner];
        if(!actor||value.actor!=actor||!generation||value.generation!=generation||hp<=0){value={};return current;}
        if(!turn||turn<=value.turn)return current;
        value.turn=turn;
        if(current<0||maximum<=0||current>=maximum)return current;
        const auto amount=(std::max)(1,maximum/100);
        return static_cast<int>((std::min)(std::int64_t(maximum),std::int64_t(current)+amount));
    }
};
}
