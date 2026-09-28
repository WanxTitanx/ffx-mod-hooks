#pragma once
#include <array>
#include <cstdint>
#include <algorithm>
#include <limits>

namespace FfxHooks::Vanguard {
struct ActionSettlement {unsigned heal=0;std::uint8_t consume=0;bool accepted=false;};
class ActionLedger {
    struct Pending {
        std::uint64_t token=0,loss=0;
        std::uintptr_t actor=0;
        std::array<std::uint64_t,2> buffs{};
        std::uint8_t present=0,used=0;
        bool vampirism=false;
    };
    std::array<Pending,31> pending_{};
    std::array<std::array<std::uint64_t,2>,31> buffs_{};
public:
    void Reset() noexcept {pending_={};buffs_={};}
    bool Begin(unsigned owner,std::uint64_t token,std::uintptr_t actor,bool vampirism,std::uint8_t present) noexcept {
        if(owner>=pending_.size()||!token||!actor)return false;
        auto& p=pending_[owner];
        if(p.token)return p.token==token&&p.actor==actor;
        p.token=token;p.actor=actor;p.buffs=buffs_[owner];p.present=present&0x14;p.vampirism=vampirism;
        return true;
    }
    bool Active(unsigned owner,std::uint64_t token,std::uintptr_t actor) const noexcept {
        return owner<pending_.size()&&token&&pending_[owner].token==token&&pending_[owner].actor==actor;
    }
    void Used(unsigned owner,std::uint64_t token,std::uint8_t mask) noexcept {
        if(owner<pending_.size()&&token&&pending_[owner].token==token)
            pending_[owner].used|=mask&pending_[owner].present&0x14;
    }
    void Reapplied(unsigned owner,std::uint8_t mask) noexcept {
        if(owner>=buffs_.size())return;
        for(unsigned i=0;i<2;++i)if(mask&(i?0x10:4)){
            // Exhaustion fails closed rather than allowing an ancient instance
            // to compare equal after a generation wrap.
            if(buffs_[owner][i]==UINT64_MAX){pending_[owner]={};buffs_[owner][i]=0;}
            else ++buffs_[owner][i];
        }
    }
    void Lost(unsigned owner,std::uint64_t token,unsigned target,int before,int after,bool offensive) noexcept {
        if(owner>=18||target<18||target>=31||!offensive||before<=0||after<0||before<=after||
           !token||pending_[owner].token!=token||!pending_[owner].vampirism)return;
        auto& p=pending_[owner];const auto delta=static_cast<std::uint64_t>(before-after);
        p.loss+= (std::min)(delta,UINT64_MAX-p.loss);
    }
    ActionSettlement Finish(unsigned owner,std::uint64_t token,std::uintptr_t actor,bool complete,
                            bool alive,bool zombie,unsigned hp,unsigned maximum) noexcept {
        if(!Active(owner,token,actor))return {};
        const Pending old=pending_[owner];pending_[owner]={};
        ActionSettlement result{};result.accepted=true;
        if(!complete||!alive)return result;
        for(unsigned i=0;i<2;++i)if(old.buffs[i]==buffs_[owner][i])
            result.consume|=old.used&static_cast<std::uint8_t>(i?0x10:4);
        // Explicit rule: Zombie suppresses this passive restoration. It neither
        // clears Zombie nor manufactures a second offensive action/death chain.
        if(old.vampirism&&!zombie&&maximum>hp)
            result.heal=static_cast<unsigned>((std::min)(old.loss/50,std::uint64_t(maximum-hp)));
        return result;
    }
};
}
