#include "../shared/ExecutableProfile.h"
#pragma once
// Jarvis-HOOK: Arcana adjusts the command/HP request around Vanguard's existing
// policy, which in turn calls the native operation once. Startup order never
// changes this order, the native RNG owner, or actual-HP-loss accounting.
#include "NativeUiHookSupport.h"
#include "ArcanaCombatEvidence.generated.h"
#include <array>
#include <atomic>
#include <mutex>
namespace FfxHooks::SharedCombat {
using Byte=unsigned char;
using MpFn=int(__cdecl*)(unsigned,const Byte*);
using CriticalFn=int(__cdecl*)(const Byte*,const Byte*,const Byte*,unsigned*,int);
using HpFn=int(__cdecl*)(unsigned,Byte*,int,int,int,int,int);
struct Callbacks {MpFn mp;CriticalFn critical;HpFn hp;};
inline std::atomic<const Callbacks*> vanguard{nullptr},arcana{nullptr};
inline std::atomic<bool> installed{false};
inline std::mutex installation;
inline std::uintptr_t module=0;
inline constexpr std::uint32_t rvas[]={(::FfxHooks::ExecutableProfile::Rva<0x38D030>()),(::FfxHooks::ExecutableProfile::Rva<0x389750>()),(::FfxHooks::ExecutableProfile::Rva<0x38E2F0>())};
inline void* originals[3]{};
inline std::array<std::array<Byte,32>,3> owned{};
inline bool Register(std::atomic<const Callbacks*>& slot,const Callbacks* value) noexcept {
    if(!value||!value->mp||!value->critical||!value->hp)return false;
    const Callbacks* empty=nullptr;return slot.compare_exchange_strong(empty,value)||empty==value;
}
inline void Unregister(std::atomic<const Callbacks*>& slot,const Callbacks* value) noexcept {slot.compare_exchange_strong(value,nullptr);}
inline MpFn OriginalMp() noexcept {return reinterpret_cast<MpFn>(originals[0]);}
inline CriticalFn OriginalCritical() noexcept {return reinterpret_cast<CriticalFn>(originals[1]);}
inline HpFn OriginalHp() noexcept {return reinterpret_cast<HpFn>(originals[2]);}
inline int __cdecl AfterArcanaMp(unsigned owner,const Byte* command){const auto* next=vanguard.load();return (next?next->mp:OriginalMp())(owner,command);}
inline int __cdecl AfterArcanaCritical(const Byte* source,const Byte* target,const Byte* command,unsigned* flags,int amount){
    const auto* next=vanguard.load();return (next?next->critical:OriginalCritical())(source,target,command,flags,amount);
}
inline int __cdecl AfterArcanaHp(unsigned target,Byte* actor,int amount,int display,int code,int flags,int other){
    const auto* next=vanguard.load();return (next?next->hp:OriginalHp())(target,actor,amount,display,code,flags,other);
}
inline int __cdecl Mp(unsigned owner,const Byte* command){const auto* next=arcana.load();return (next?next->mp:AfterArcanaMp)(owner,command);}
inline int __cdecl Critical(const Byte* source,const Byte* target,const Byte* command,unsigned* flags,int amount){
    const auto* next=arcana.load();return (next?next->critical:AfterArcanaCritical)(source,target,command,flags,amount);
}
inline int __cdecl Hp(unsigned target,Byte* actor,int amount,int display,int code,int flags,int other){
    const auto* next=arcana.load();return (next?next->hp:AfterArcanaHp)(target,actor,amount,display,code,flags,other);
}
inline bool MatchesOwned(std::uintptr_t base,std::uint32_t rva,const void* bytes,std::size_t size) noexcept {
    if(!installed.load()||base!=module||!bytes||!size||size>32)return false;
    for(unsigned i=0;i<3;++i)if(rva==rvas[i])return !std::memcmp(bytes,owned[i].data(),size);
    return false;
}
inline bool Start(std::uintptr_t base){
    std::lock_guard<std::mutex> lock(installation);
    // Both modules previously admitted these exact generated native spans.
    const EquipmentWorkshop::Evidence::Span spans[]={Arcana::Evidence::combat[1],Arcana::Evidence::combat[3],Arcana::Evidence::combat[5]};
    if(!NativeUiSupport::Profile(base,spans,MatchesOwned))return false;
    if(installed.load())return module==base;
    void* replacements[]={reinterpret_cast<void*>(&Mp),reinterpret_cast<void*>(&Critical),reinterpret_cast<void*>(&Hp)};
    if(!NativeUiSupport::Install(base,rvas,replacements,originals,MinHookBatch::Owner::SharedCombat,reinterpret_cast<const void*>(&Start)))return false;
    for(unsigned i=0;i<3;++i)if(!NativeUiSupport::Copy(owned[i].data(),reinterpret_cast<const void*>(base+rvas[i]),32))return false;
    module=base;installed=true;return true;
}
}
