#pragma once
#include "BattleDamagePolicy.h"
#include <array>
#include <atomic>
#include <cstdint>
#define FFXHOOKS_COMBAT_AMOUNT_POLICY_V1 1

namespace FfxHooks::CombatExtensions {

// Each published descriptor is immutable and remains alive for the process.
// Modules publish during startup and close their own admission before retiring.
enum class Slot : unsigned { Elemental, Aeon, NulWard, Reserved, Count };
enum class ResetReason : unsigned { NativeLoad, BattleStart, BattleExit, Stop };
inline constexpr unsigned SlotCount=static_cast<unsigned>(Slot::Count);
inline constexpr unsigned MaximumDepth=16;

struct DamageCall {
    unsigned user=0;
    void* userActor=nullptr;
    unsigned target=0;
    void* targetActor=nullptr;
    const void* command=nullptr;
    unsigned commandId=0;
    void* info=nullptr;
    unsigned buffer=0,counter=0,result=0,output=0;
};

struct CapRequests {
    bool magicEligible=false;
    bool aeonAuthorized=false;
    bool nonlethal=false;
    std::int32_t targetCurrentHp=0;
    void Nonlethal(std::int32_t hp) noexcept {
        if(!nonlethal || hp<targetCurrentHp)targetCurrentHp=hp;
        nonlethal=true;
    }
    void Merge(const CapRequests& other) noexcept {
        magicEligible=magicEligible||other.magicEligible;
        aeonAuthorized=aeonAuthorized||other.aeonAuthorized;
        if(other.nonlethal)Nonlethal(other.targetCurrentHp);
    }
};

struct Observer {
    void* (*enter)(const DamageCall&,const void*& commandToForward) noexcept=nullptr;
    void (*leave)(void* token,const DamageCall&,unsigned result,bool completed) noexcept=nullptr;
    void (*cap)(void* token,const DamageCall&,CapRequests&) noexcept=nullptr;
    void (*reset)(ResetReason) noexcept=nullptr;
    std::int32_t (*modifyHp)(void* token,const DamageCall&,std::int32_t amount) noexcept=nullptr;
};
inline std::array<std::atomic<const Observer*>,SlotCount> observers{};

inline bool Subscribe(Slot slot,const Observer* observer) noexcept {
    const unsigned index=static_cast<unsigned>(slot);
    if(index>=SlotCount||!observer)return false;
    const Observer* empty=nullptr;
    return observers[index].compare_exchange_strong(empty,observer,std::memory_order_release,
                                                    std::memory_order_acquire)||empty==observer;
}
inline bool Unsubscribe(Slot slot,const Observer* observer) noexcept {
    const unsigned index=static_cast<unsigned>(slot);
    if(index>=SlotCount||!observer)return false;
    const Observer* expected=observer;
    return observers[index].compare_exchange_strong(expected,nullptr,std::memory_order_acq_rel,
                                                    std::memory_order_acquire);
}
inline bool Required() noexcept {
    for(const auto& entry:observers)if(entry.load(std::memory_order_acquire))return true;
    return false;
}
inline void Reset(ResetReason reason) noexcept {
    for(const auto& slot:observers){
        const auto* observer=slot.load(std::memory_order_acquire);
        if(observer&&observer->reset)observer->reset(reason);
    }
}

struct DamageScope {
    DamageCall call{};
    const void* forwardCommand=nullptr;
    std::array<const Observer*,SlotCount> listeners{};
    std::array<void*,SlotCount> tokens{};
    std::array<bool,SlotCount> participating{};
    DamageScope* previous=nullptr;
    unsigned depth=0;
    bool entered=false;
};
inline thread_local DamageScope* currentDamage=nullptr;

// Even an overflow frame temporarily hides its parent. A nested unsupported
// native invocation must never inherit another target's element or paid cap.
inline bool EnterDamage(DamageScope& scope,const DamageCall& call,bool admit=true) noexcept {
    if(scope.entered)return false;
    scope.call=call;scope.forwardCommand=call.command;scope.previous=currentDamage;
    scope.depth=currentDamage?currentDamage->depth+1:1;scope.entered=true;
    currentDamage=&scope;
    if(!admit||scope.depth>MaximumDepth)return false;
    for(unsigned i=0;i<SlotCount;++i){
        const auto* observer=observers[i].load(std::memory_order_acquire);
        if(!observer)continue;
        scope.listeners[i]=observer;
        if(observer->enter){
            const void* proposed=scope.forwardCommand;
            scope.tokens[i]=observer->enter(call,proposed);
            scope.participating[i]=scope.tokens[i]!=nullptr;
            if(scope.participating[i]&&proposed)scope.forwardCommand=proposed;
        }else scope.participating[i]=true;
    }
    return true;
}

inline bool LeaveDamage(DamageScope& scope,unsigned result,bool completed) noexcept {
    if(!scope.entered||currentDamage!=&scope)return false;
    for(unsigned i=SlotCount;i>0;--i){
        const unsigned index=i-1;
        const auto* observer=scope.listeners[index];
        // Tokens still need cleanup after admission has closed. Their immutable
        // descriptors are retained, just like already published trampolines.
        if(observer&&scope.participating[index]&&observer->leave)
            observer->leave(scope.tokens[index],scope.call,result,completed);
    }
    currentDamage=scope.previous;scope.entered=false;
    return true;
}

inline std::int32_t UpperDamage(std::int32_t damage,std::int32_t nativeUpper,
                               unsigned actor,unsigned command,unsigned remaining,
                               bool legacyNova) noexcept {
    BattleDamage::Policy policy{};
    policy.nativeUpper=nativeUpper;policy.component=BattleDamage::FromNativePass(remaining);
    policy.legacyNovaBypass=legacyNova;
    const auto* scope=currentDamage;
    if(scope&&scope->entered&&scope->depth<=MaximumDepth&&
       scope->call.user==actor&&scope->call.commandId==command){
        CapRequests combined{};
        for(unsigned i=0;i<SlotCount;++i){
            const auto* observer=scope->listeners[i];
            if(!observer||!scope->participating[i]||
               observers[i].load(std::memory_order_acquire)!=observer)continue;
            CapRequests request{};
            if(observer->cap){observer->cap(scope->tokens[i],scope->call,request);combined.Merge(request);}
            // Single positive-HP stage after native affinity/protection, before
            // upper limits. Other components and signed restoration bypass it.
            if(damage>0&&remaining==3&&observer->modifyHp){
                const auto changed=observer->modifyHp(scope->tokens[i],scope->call,damage);
                if(changed>=0)damage=changed;
            }
        }
        policy.magicEnabled=combined.magicEligible;policy.offensiveSpell=combined.magicEligible;
        policy.aeonBreakAuthorized=combined.aeonAuthorized;
        policy.nonlethal=combined.nonlethal;policy.targetCurrentHp=combined.targetCurrentHp;
    }
    const auto result=BattleDamage::ClampUpper(damage,policy);
    // An invalid extension request cannot turn off the native upper clamp.
    return result.valid?result.damage:(damage>nativeUpper?nativeUpper:damage);
}

} // namespace FfxHooks::CombatExtensions
