#include "../shared/ExecutableProfile.h"
#pragma once
// Jarvis-HOOK: sole native affinity owner. Descriptors and published trampolines
// have process lifetime; an unhandled external policy delegates exactly once.
#include "F8RuntimeCore.h"
#include "MinHookBatchCoordinator.h"
#include <windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <MinHook.h>
#endif
namespace FfxHooks::SharedElement {
using NativeFn=int(__cdecl*)(const unsigned char*,const unsigned char*,unsigned,int);
struct Resolver {
    bool (*resolve)(const unsigned char*,const unsigned char*,unsigned,int,int*) noexcept=nullptr;
};
inline constexpr std::uint32_t kRva=(::FfxHooks::ExecutableProfile::Rva<0x38A420>());
inline constexpr unsigned char kExpected[16]={
    0x55,0x8B,0xEC,0x83,0xEC,0x20,0x57,0x8B,0x7D,0x10,0x85,0xFF,0x75,0x08,0x8B,0x45};
inline std::atomic<NativeFn> legacy{nullptr};
inline std::atomic<const Resolver*> resolver{nullptr};
inline std::atomic<bool> installed{false};
inline std::uintptr_t imageBase=0;
inline void* original=nullptr;
inline unsigned char admitted[sizeof(kExpected)]{};
inline std::mutex installMutex;
inline bool attempted=false;
inline bool Copy(void* out,const void* in,std::size_t size) noexcept {
    __try{std::memcpy(out,in,size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline NativeFn Original() noexcept {return reinterpret_cast<NativeFn>(original);}
inline bool RegisterLegacy(NativeFn value) noexcept {
    if(!value||!installed.load())return false;
    NativeFn expected=nullptr;return legacy.compare_exchange_strong(expected,value)||expected==value;
}
inline void UnregisterLegacy(NativeFn value) noexcept {
    NativeFn expected=value;(void)legacy.compare_exchange_strong(expected,nullptr);
}
inline bool RegisterResolver(const Resolver* value) noexcept {
    if(!value||!value->resolve||!installed.load())return false;
    const Resolver* expected=nullptr;return resolver.compare_exchange_strong(expected,value)||expected==value;
}
inline void UnregisterResolver(const Resolver* value) noexcept {
    const Resolver* expected=value;(void)resolver.compare_exchange_strong(expected,nullptr);
}
inline int __cdecl Dispatch(const unsigned char* target,const unsigned char* command,unsigned mask,int amount){
    int result=amount;const auto* selected=resolver.load();
    if(selected&&selected->resolve(target,command,mask,amount,&result))return result;
    const auto previous=legacy.load();
    return (previous?previous:Original())(target,command,mask,amount);
}
inline bool MatchesOwned(std::uintptr_t base,const void* bytes,std::size_t size) noexcept {
    return installed.load()&&base==imageBase&&bytes&&size==sizeof(admitted)&&
           std::memcmp(bytes,admitted,sizeof(admitted))==0;
}
inline bool Start(std::uintptr_t base){
    std::lock_guard<std::mutex> lock(installMutex);
    if(installed.load()){
        unsigned char bytes[sizeof(admitted)]{};
        return base==imageBase&&Copy(bytes,reinterpret_cast<void*>(base+kRva),sizeof(bytes))&&
               MatchesOwned(base,bytes,sizeof(bytes));
    }
    if(attempted||!base)return false;
    unsigned char header[0x1000]{},bytes[sizeof(kExpected)]{};
    F8Runtime::ExecutableIdentity identity{};
    if(!Copy(header,reinterpret_cast<void*>(base),sizeof(header))||
       F8Runtime::ParseExecutableIdentity(header,sizeof(header),&identity)!=F8Runtime::ProfileResult::Supported||
       !F8Runtime::IsSupportedExecutable(identity)||
       !Copy(bytes,reinterpret_cast<void*>(base+kRva),sizeof(bytes))||
       std::memcmp(bytes,kExpected,sizeof(bytes)))return false;
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if(MinHookBatch::EnsureProcessInitialized()!=MinHookBatch::InitializationResult::Ready)return false;
    HMODULE pin=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                          reinterpret_cast<LPCWSTR>(&Start),&pin))return false;
    const auto target=base+kRva;attempted=true;
    if(MH_CreateHook(reinterpret_cast<void*>(target),reinterpret_cast<void*>(&Dispatch),&original)!=MH_OK)return false;
    const auto report=MinHookBatch::EnableBatch(&MinHookBatch::ProcessCoordinator(),
        MinHookBatch::RuntimeBatchIo(),MinHookBatch::Owner::SharedElement,&target,1);
    // Published entrants can retain a trampoline even if apply later fails.
    // Failed startup remains native-only; never retry or free that storage.
    if(report.result!=MinHookBatch::BatchResult::Applied||
       !Copy(admitted,reinterpret_cast<void*>(target),sizeof(admitted)))return false;
    imageBase=base;installed.store(true);return true;
#else
    return false;
#endif
}
static_assert(std::atomic<NativeFn>::is_always_lock_free);
static_assert(std::atomic<const Resolver*>::is_always_lock_free);
}
