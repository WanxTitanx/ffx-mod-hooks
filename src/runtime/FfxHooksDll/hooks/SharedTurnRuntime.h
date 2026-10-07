#include "../shared/ExecutableProfile.h"
#pragma once
// Jarvis-HOOK: a single owner for the native CTB edge used by Phase Rotation and
// Vanguard. Subscribers are process-lifetime descriptors; teardown only disarms.
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
namespace FfxHooks::SharedTurn {
struct Observer {void (*after)(unsigned,void*,std::uint32_t) noexcept;};
enum class Consumer : unsigned {PhaseRotation,Vanguard,Spira,Count};
inline constexpr unsigned ConsumerCount=static_cast<unsigned>(Consumer::Count);
inline std::atomic<const Observer*> observers[ConsumerCount]{};
inline std::atomic<bool> installed{false};
inline std::atomic<std::uint32_t> sequence{0};
inline std::uintptr_t imageBase=0;
inline void* original=nullptr;
inline unsigned char admitted[16]{};
inline std::mutex installMutex;
inline thread_local bool dispatching=false;
inline bool Register(Consumer consumer,const Observer* observer) noexcept {
    const auto index=static_cast<unsigned>(consumer);if(index>=ConsumerCount||!observer||!observer->after)return false;
    const Observer* expected=nullptr;
    return observers[index].compare_exchange_strong(expected,observer)||expected==observer;
}
inline void Unregister(Consumer consumer,const Observer* observer) noexcept {
    const auto index=static_cast<unsigned>(consumer);if(index<ConsumerCount)(void)observers[index].compare_exchange_strong(observer,nullptr);
}
inline bool Copy(void* out,const void* in,std::size_t size) noexcept {
    __try{std::memcpy(out,in,size);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline void __cdecl Shim(unsigned slot,void* actor){
    reinterpret_cast<void(__cdecl*)(unsigned,void*)>(original)(slot,actor);
    unsigned char battle=0;
    if(dispatching||!Copy(&battle,reinterpret_cast<void*>(imageBase+::FfxHooks::ExecutableProfile::Rva<0xD2A8E0>()),1)||!battle)return;
    auto prior=sequence.load();
    // Saturating, bounded publication. A competing foreign caller loses this
    // notification instead of wrapping or spinning inside the game callback.
    if(prior==UINT32_MAX||!sequence.compare_exchange_strong(prior,prior+1))return;
    const auto next=prior+1;
    dispatching=true;
    __try {
        // Gameplay runs before the optional legacy sidecar can enqueue a command.
        for(unsigned index:{1u,2u,0u}){const auto* value=observers[index].load();if(value)value->after(slot,actor,next);}
    }__finally{dispatching=false;}
}
inline bool Start(std::uintptr_t base){
    std::lock_guard<std::mutex> lock(installMutex);
    unsigned char bytes[16]{};constexpr unsigned rva=(::FfxHooks::ExecutableProfile::Rva<0x3B13D0>());
    if(installed.load())return imageBase==base&&Copy(bytes,reinterpret_cast<void*>(base+rva),16)&&!std::memcmp(bytes,admitted,16);
    unsigned char header[0x1000]{};F8Runtime::ExecutableIdentity identity{};
    constexpr unsigned char expected[]={0x55,0x8B,0xEC,0x51,0x53,0x8B,0x5D,0x08,0x83,0xFB,0x06,0x0F,0x87,0x5C,0x01,0x00};
    if(!Copy(header,reinterpret_cast<void*>(base),sizeof(header))||
       F8Runtime::ParseExecutableIdentity(header,sizeof(header),&identity)!=F8Runtime::ProfileResult::Supported||
       !F8Runtime::IsSupportedExecutable(identity)||!Copy(bytes,reinterpret_cast<void*>(base+rva),16)||std::memcmp(bytes,expected,16))return false;
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if(MinHookBatch::EnsureProcessInitialized()!=MinHookBatch::InitializationResult::Ready)return false;
    HMODULE pin=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Start),&pin))return false;
    const std::uintptr_t target=base+rva;
    if(MH_CreateHook(reinterpret_cast<void*>(target),reinterpret_cast<void*>(&Shim),&original)!=MH_OK)return false;
    imageBase=base; // publish dependencies before a machine-code entrant is possible
    const auto result=MinHookBatch::EnableBatch(&MinHookBatch::ProcessCoordinator(),MinHookBatch::RuntimeBatchIo(),MinHookBatch::Owner::SharedTurn,&target,1);
    if(result.result!=MinHookBatch::BatchResult::Applied)return false;
    if(!Copy(admitted,reinterpret_cast<void*>(target),16))return false;
    installed=true;return true;
#else
    return false;
#endif
}
}
