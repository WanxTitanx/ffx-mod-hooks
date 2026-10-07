#include "../shared/ExecutableProfile.h"
#pragma once
// Jarvis-HOOK: one native Nul owner composes complete coverage before either
// native or external charges can be spent. Published callbacks are permanent.
#include "CombatExtensionBus.h"
#include "NativeUiHookSupport.h"
#include <array>
#include <atomic>
#include <mutex>

namespace FfxHooks::SharedNul {
using Function=int(__cdecl*)(unsigned,unsigned,void*);
using Resolve=bool(*)(unsigned,unsigned,void*,int&) noexcept;
struct Wards {
    Resolve resolve=nullptr;
    unsigned (*available)(const CombatExtensions::DamageCall&) noexcept=nullptr;
    bool (*reserve)(const CombatExtensions::DamageCall&,unsigned) noexcept=nullptr;
    bool (*reserved)(const CombatExtensions::DamageCall&) noexcept=nullptr;
    unsigned (*keyMask)(const char*) noexcept=nullptr;
};
inline std::atomic<Resolve> elemental{nullptr};
inline std::atomic<const Wards*> wards{nullptr};
inline std::atomic<bool> installed{false};
inline std::mutex installation;
inline std::uintptr_t imageBase=0;
inline void* original[1]{};
inline std::array<unsigned char,16> owned{};
inline constexpr unsigned Rva=(::FfxHooks::ExecutableProfile::Rva<0x38C070>());
inline Function Original() noexcept {return reinterpret_cast<Function>(original[0]);}
inline bool Register(Resolve callback) noexcept {
    if(!callback)return false;Resolve empty=nullptr;
    return elemental.compare_exchange_strong(empty,callback)||empty==callback;
}
inline void Unregister(Resolve callback) noexcept {elemental.compare_exchange_strong(callback,nullptr);}
inline bool RegisterWards(const Wards* callback) noexcept {
    if(!callback||!callback->resolve||!callback->available||!callback->reserve||!callback->reserved)return false;
    const Wards* empty=nullptr;return wards.compare_exchange_strong(empty,callback)||empty==callback;
}
inline void UnregisterWards(const Wards* callback) noexcept {wards.compare_exchange_strong(callback,nullptr);}
inline unsigned AvailableWards(const CombatExtensions::DamageCall& call) noexcept {
    const auto* callback=wards.load();return callback?callback->available(call):0;
}
inline unsigned ExternalWardMask(const char* key) noexcept {
    const auto* callback=wards.load();return callback&&callback->keyMask?callback->keyMask(key):0;
}
inline bool ReserveWards(const CombatExtensions::DamageCall& call,unsigned mask) noexcept {
    if(!mask)return true;const auto* callback=wards.load();return callback&&callback->reserve(call,mask);
}
inline bool ReservedWards(const CombatExtensions::DamageCall& call) noexcept {
    const auto* callback=wards.load();return callback&&callback->reserved(call);
}
inline int __cdecl Dispatch(unsigned argument,unsigned mask,void* info){
    int result=0;const auto primary=elemental.load();
    if(primary&&primary(argument,mask,info,result))return result;
    const auto* fallback=wards.load();
    if(fallback&&fallback->resolve(argument,mask,info,result))return result;
    return Original()(argument,mask,info);
}
inline bool Start(std::uintptr_t base){
    std::lock_guard<std::mutex> lock(installation);
    if(installed.load()){
        std::array<unsigned char,16> bytes{};
        return base==imageBase&&NativeUiSupport::Copy(bytes.data(),reinterpret_cast<const void*>(base+Rva),bytes.size())&&bytes==owned;
    }
    unsigned char header[0x1000]{};F8Runtime::ExecutableIdentity identity{};
    constexpr std::array<unsigned char,16> expected={0x55,0x8B,0xEC,0x83,0xEC,0x0C,0x8B,0x4D,0x10,0x53,0x56,0x57,0x33,0xDB,0x33,0xD2};
    std::array<unsigned char,16> bytes{};
    if(!NativeUiSupport::Copy(header,reinterpret_cast<const void*>(base),sizeof(header))||
       F8Runtime::ParseExecutableIdentity(header,sizeof(header),&identity)!=F8Runtime::ProfileResult::Supported||
       !F8Runtime::IsSupportedExecutable(identity)||
       !NativeUiSupport::Copy(bytes.data(),reinterpret_cast<const void*>(base+Rva),bytes.size())||bytes!=expected)return false;
    const std::uint32_t rvas[]={Rva};void* replacements[]={reinterpret_cast<void*>(&Dispatch)};
    if(!NativeUiSupport::Install(base,rvas,replacements,original,MinHookBatch::Owner::SharedNul,reinterpret_cast<const void*>(&Start)))return false;
    if(!NativeUiSupport::Copy(owned.data(),reinterpret_cast<const void*>(base+Rva),owned.size()))return false;
    imageBase=base;installed=true;return true;
}
} // namespace FfxHooks::SharedNul
