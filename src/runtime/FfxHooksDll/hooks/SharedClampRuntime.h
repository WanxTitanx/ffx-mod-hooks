#include "../shared/ExecutableProfile.h"
#pragma once
// Jarvis-HOOK: one owner for the native integer clamp. Caller identity is captured
// before callback dispatch, so independent field/battle policies retain their ABI.
#include "NativeUiHookSupport.h"
#include <array>
#include <atomic>
#include <mutex>
#include <intrin.h>
namespace FfxHooks::SharedClamp {
enum class Slot : unsigned {Arcana,Spira,Count};
using Adjust=void(*)(std::uintptr_t,int&,int&,int&) noexcept;
inline std::array<std::atomic<Adjust>,static_cast<unsigned>(Slot::Count)> callbacks{};
inline std::atomic<bool> installed{false};
inline std::mutex installation;
inline std::uintptr_t module=0;
inline void* original[1]{};
inline std::array<unsigned char,32> owned{};
inline constexpr std::uint32_t Rva=(::FfxHooks::ExecutableProfile::Rva<0x39A0D0>());
inline bool Register(Slot slot,Adjust callback) noexcept {
    const auto index=static_cast<unsigned>(slot);if(index>=callbacks.size()||!callback)return false;
    Adjust empty=nullptr;return callbacks[index].compare_exchange_strong(empty,callback)||empty==callback;
}
inline void Unregister(Slot slot,Adjust callback) noexcept {
    const auto index=static_cast<unsigned>(slot);if(index<callbacks.size())callbacks[index].compare_exchange_strong(callback,nullptr);
}
inline bool MatchesOwned(std::uintptr_t base,const void* bytes,std::size_t size) noexcept {
    return installed.load()&&base==module&&bytes&&size<=owned.size()&&!std::memcmp(bytes,owned.data(),size);
}
inline bool Profile(std::uintptr_t base) noexcept {
    unsigned char header[0x1000]{};F8Runtime::ExecutableIdentity identity{};
    constexpr unsigned char expected[]={0x55,0x8B,0xEC,0x8B,0x45,0x08,0x8B,0x4D,0x0C,0x3B,0xC1,0x7D,0x02,
        0x8B,0xC1,0x8B,0x4D,0x10,0x3B,0xC1,0x7E,0x02,0x8B,0xC1,0x5D,0xC3};
    unsigned char bytes[sizeof(expected)]{};
    return NativeUiSupport::Copy(header,reinterpret_cast<const void*>(base),sizeof(header))&&
        F8Runtime::ParseExecutableIdentity(header,sizeof(header),&identity)==F8Runtime::ProfileResult::Supported&&
        F8Runtime::IsSupportedExecutable(identity)&&NativeUiSupport::Copy(bytes,reinterpret_cast<const void*>(base+Rva),sizeof(bytes))&&
        (!std::memcmp(bytes,expected,sizeof(bytes))||MatchesOwned(base,bytes,sizeof(bytes)));
}
inline int __cdecl Dispatch(int value,int minimum,int maximum){
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-module;
    for(const auto& slot:callbacks){const auto callback=slot.load();if(callback)callback(caller,value,minimum,maximum);}
    return reinterpret_cast<int(__cdecl*)(int,int,int)>(original[0])(value,minimum,maximum);
}
inline bool Start(std::uintptr_t base){
    std::lock_guard<std::mutex> lock(installation);
    if(!Profile(base))return false;if(installed.load())return module==base;
    const std::uint32_t rvas[]={Rva};void* replacements[]={reinterpret_cast<void*>(&Dispatch)};
    if(!NativeUiSupport::Install(base,rvas,replacements,original,MinHookBatch::Owner::SharedClamp,reinterpret_cast<const void*>(&Start)))return false;
    if(!NativeUiSupport::Copy(owned.data(),reinterpret_cast<const void*>(base+Rva),owned.size()))return false;
    module=base;installed=true;return true;
}
}
