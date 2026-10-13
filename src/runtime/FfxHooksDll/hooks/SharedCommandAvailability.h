#pragma once
// Jarvis-HOOK: learned-command admission runs before equipment-only grants.
// Both clients share one permanent native entry regardless of startup order.
#include "NativeUiHookSupport.h"
#include <array>
#include <atomic>
#include <mutex>

namespace FfxHooks::SharedCommandAvailability {
using Function=int(__cdecl*)(unsigned char,unsigned short);
inline constexpr auto Rva=ExecutableProfile::Rva<0x39AD40>();
inline constexpr std::array<unsigned char,16> expected={
    0x55,0x8B,0xEC,0x53,0x56,0x57,0xFF,0x75,0x08,0x33,0xF6,0xE8,0xE0,0x92,0xFF,0xFF};
inline std::atomic<Function> learned{nullptr},equipment{nullptr};
inline std::atomic<bool> installed{false};
inline std::mutex installation;
inline std::uintptr_t module=0;
inline void* original[1]{};
inline std::array<unsigned char,48> owned{};
inline Function Original() noexcept {return reinterpret_cast<Function>(original[0]);}
inline int __cdecl Learned(unsigned char actor,unsigned short command){
    const auto callback=learned.load(std::memory_order_acquire);
    return (callback?callback:Original())(actor,command);
}
inline int __cdecl Dispatch(unsigned char actor,unsigned short command){
    const auto callback=equipment.load(std::memory_order_acquire);
    return (callback?callback:&Learned)(actor,command);
}
inline bool Register(std::atomic<Function>& slot,Function callback) noexcept {
    if(!installed.load(std::memory_order_acquire)||!callback)return false;
    Function empty=nullptr;
    return slot.compare_exchange_strong(empty,callback,std::memory_order_release,std::memory_order_acquire)||empty==callback;
}
inline bool RegisterLearned(Function callback) noexcept {return Register(learned,callback);}
inline bool RegisterEquipment(Function callback) noexcept {return Register(equipment,callback);}
inline void UnregisterLearned(Function callback) noexcept {learned.compare_exchange_strong(callback,nullptr);}
inline void UnregisterEquipment(Function callback) noexcept {equipment.compare_exchange_strong(callback,nullptr);}
inline bool MatchesOwned(std::uintptr_t base,const void* bytes,std::size_t size) noexcept {
    return installed.load(std::memory_order_acquire)&&base==module&&bytes&&size&&size<=owned.size()&&
        std::memcmp(bytes,owned.data(),size)==0;
}
inline bool Profile(std::uintptr_t base) noexcept {
    unsigned char header[0x1000]{};F8Runtime::ExecutableIdentity identity{};
    std::array<unsigned char,16> bytes{};
    return NativeUiSupport::Copy(header,reinterpret_cast<const void*>(base),sizeof(header))&&
        F8Runtime::ParseExecutableIdentity(header,sizeof(header),&identity)==F8Runtime::ProfileResult::Supported&&
        F8Runtime::IsSupportedExecutable(identity)&&
        NativeUiSupport::Copy(bytes.data(),reinterpret_cast<const void*>(base+Rva),bytes.size())&&
        (bytes==expected||MatchesOwned(base,bytes.data(),bytes.size()));
}
inline bool Start(std::uintptr_t base){
    std::lock_guard<std::mutex> lock(installation);
    if(!Profile(base))return false;
    if(installed.load(std::memory_order_acquire))return base==module;
    const std::uint32_t rvas[]={Rva};void* replacements[]={reinterpret_cast<void*>(&Dispatch)};
    if(!NativeUiSupport::Install(base,rvas,replacements,original,MinHookBatch::Owner::SharedCommandAvailability,reinterpret_cast<const void*>(&Start)))return false;
    if(!NativeUiSupport::Copy(owned.data(),reinterpret_cast<const void*>(base+Rva),owned.size()))return false;
    module=base;installed.store(true,std::memory_order_release);return true;
}
static_assert(std::atomic<Function>::is_always_lock_free,"Availability detach closes only lock-free registrations");
}
