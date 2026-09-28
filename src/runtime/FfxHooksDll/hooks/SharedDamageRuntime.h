#pragma once
// Jarvis-HOOK: one owner for the three native damage/protection entries already
// used by Workshop. Registered callbacks have process lifetime; stop only disarms.
#include "EquipmentWorkshopEvidence.h"
#include "F8RuntimeCore.h"
#include "MinHookBatchCoordinator.h"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <windows.h>
#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <MinHook.h>
#endif

namespace FfxHooks::SharedDamage {
using ProtectionFn=int(__cdecl*)(const void*,unsigned*,int*,const void*,int);
using DamageFn=unsigned(__cdecl*)(unsigned,void*,unsigned,void*,const void*,unsigned,void*,unsigned,unsigned,unsigned,unsigned);
enum Entry : unsigned {Protect,Shell,Damage,Count};
inline constexpr std::uint32_t rvas[Count]={0x38AE00,0x38AE80,0x38E680};
struct WorkshopCallbacks {ProtectionFn protect,shell;DamageFn damage;};
struct CombatCallbacks {
    bool (*ignoreProtection)(const void*,const void*,int) noexcept;
    void (*beforeHit)(unsigned,void*,unsigned,void*,const void*,unsigned) noexcept=nullptr;
};
inline std::atomic<const WorkshopCallbacks*> workshop{nullptr};
inline std::atomic<const CombatCallbacks*> combat{nullptr};
inline std::atomic<bool> installed{false};
inline std::uintptr_t imageBase=0;
inline void* originals[Count]{};
inline unsigned char admitted[Count][32]{};
inline std::mutex installMutex;
inline bool Copy(void* out,const void* in,std::size_t bytes) noexcept {
    __try{std::memcpy(out,in,bytes);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool RegisterWorkshop(const WorkshopCallbacks* value) noexcept {
    if(!value||!value->protect||!value->shell||!value->damage)return false;
    const WorkshopCallbacks* empty=nullptr;
    return workshop.compare_exchange_strong(empty,value)||empty==value;
}
inline void UnregisterWorkshop(const WorkshopCallbacks* value) noexcept {
    const WorkshopCallbacks* expected=value;(void)workshop.compare_exchange_strong(expected,nullptr);
}
inline bool RegisterCombat(const CombatCallbacks* value) noexcept {
    if(!value||!value->ignoreProtection)return false;
    const CombatCallbacks* empty=nullptr;
    return combat.compare_exchange_strong(empty,value)||empty==value;
}
inline void UnregisterCombat(const CombatCallbacks* value) noexcept {
    const CombatCallbacks* expected=value;(void)combat.compare_exchange_strong(expected,nullptr);
}
inline int __cdecl ProtectShim(const void* command,unsigned* flags,int* divisor,const void* info,int amount){
    const auto* policy=combat.load();
    if(policy&&policy->ignoreProtection(command,info,amount))return amount;
    const auto* consumer=workshop.load();
    return consumer?consumer->protect(command,flags,divisor,info,amount):
        reinterpret_cast<ProtectionFn>(originals[Protect])(command,flags,divisor,info,amount);
}
inline int __cdecl ShellShim(const void* command,unsigned* flags,int* divisor,const void* info,int amount){
    const auto* policy=combat.load();
    if(policy&&policy->ignoreProtection(command,info,amount))return amount;
    const auto* consumer=workshop.load();
    return consumer?consumer->shell(command,flags,divisor,info,amount):
        reinterpret_cast<ProtectionFn>(originals[Shell])(command,flags,divisor,info,amount);
}
inline unsigned __cdecl DamageShim(unsigned user,void* source,unsigned target,void* destination,
    const void* command,unsigned commandId,void* info,unsigned a8,unsigned a9,unsigned a10,unsigned a11){
    const auto* observer=combat.load();
    if(observer&&observer->beforeHit)observer->beforeHit(user,source,target,destination,command,commandId);
    const auto* consumer=workshop.load();
    const auto call=consumer?consumer->damage:reinterpret_cast<DamageFn>(originals[Damage]);
    return call(user,source,target,destination,command,commandId,info,a8,a9,a10,a11);
}
inline void* Original(Entry entry) noexcept {return entry<Count?originals[entry]:nullptr;}
inline bool MatchesOwned(std::uintptr_t base,std::uint32_t rva,const unsigned char* bytes) noexcept {
    if(!installed.load()||base!=imageBase||!bytes)return false;
    for(unsigned i=0;i<Count;++i)if(rvas[i]==rva)return std::memcmp(bytes,admitted[i],32)==0;
    return false;
}
inline bool Start(std::uintptr_t base) {
    std::lock_guard<std::mutex> lock(installMutex);
    if(installed.load()){
        if(base!=imageBase)return false;
        for(unsigned i=0;i<Count;++i){unsigned char bytes[32]{};
            if(!Copy(bytes,reinterpret_cast<void*>(base+rvas[i]),32)||!MatchesOwned(base,rvas[i],bytes))return false;}
        return true;
    }
    unsigned char header[0x1000]{};F8Runtime::ExecutableIdentity identity{};
    if(!Copy(header,reinterpret_cast<void*>(base),sizeof(header))||
       F8Runtime::ParseExecutableIdentity(header,sizeof(header),&identity)!=F8Runtime::ProfileResult::Supported||
       !F8Runtime::IsSupportedExecutable(identity))return false;
    for(unsigned i=0;i<Count;++i){bool verified=false;unsigned char bytes[32]{};
        if(!Copy(bytes,reinterpret_cast<void*>(base+rvas[i]),32))return false;
        for(const auto& span:EquipmentWorkshop::Evidence::spans)if(span.rva==rvas[i])
            verified=EquipmentWorkshop::Evidence::Matches(span,bytes,base);
        if(!verified)return false;
    }
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if(MinHookBatch::EnsureProcessInitialized()!=MinHookBatch::InitializationResult::Ready)return false;
    HMODULE pin=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                          reinterpret_cast<LPCWSTR>(&Start),&pin))return false;
    void* replacements[Count]={reinterpret_cast<void*>(&ProtectShim),reinterpret_cast<void*>(&ShellShim),reinterpret_cast<void*>(&DamageShim)};
    std::uintptr_t targets[Count]{};unsigned created=0;
    for(;created<Count;++created){targets[created]=base+rvas[created];
        if(MH_CreateHook(reinterpret_cast<void*>(targets[created]),replacements[created],&originals[created])!=MH_OK)break;}
    if(created!=Count){while(created)MH_RemoveHook(reinterpret_cast<void*>(targets[--created]));return false;}
    const auto result=MinHookBatch::EnableBatch(&MinHookBatch::ProcessCoordinator(),MinHookBatch::RuntimeBatchIo(),
                                               MinHookBatch::Owner::SharedDamage,targets,Count);
    // After apply, entrants may hold any trampoline. Retain all such storage on
    // failure too; without consumers, every reachable dispatcher is native-only.
    if(result.result!=MinHookBatch::BatchResult::Applied)return false;
    for(unsigned i=0;i<Count;++i)if(!Copy(admitted[i],reinterpret_cast<void*>(targets[i]),32))return false;
    imageBase=base;installed=true;return true;
#else
    return false;
#endif
}
}
