#pragma once
#include "CompatibleDetour.h"
#include "NativeUiHookSupport.h"
#include "NativeMenuHelpEvidence.h"
#include <atomic>
#include <cstdint>

namespace FfxHooks::NativeMenuHelp {
inline thread_local bool suppressed=false;
inline std::atomic<bool> enabled{false};
inline std::uint64_t original=0;
inline CompatibleDetour* detour=nullptr;
inline std::uintptr_t installedBase=0;

// This is the persistent native pool callback on draw layer 6, not the generic
// text renderer. Leave its cached message, previous message and timer untouched.
inline int __cdecl Draw(int object){
    if(enabled.load(std::memory_order_acquire)&&suppressed)return 0;
    return reinterpret_cast<int(__cdecl*)(int)>(original)(object);
}

inline bool Start(std::uintptr_t base){
    if(!base)return false;
    if(detour){
        if(base!=installedBase)return false;
        enabled.store(true,std::memory_order_release);return true;
    }
    if(!NativeUiSupport::Profile(base,Evidence::renderer))return false;
    // Another producer can already be entering the original renderer. Pin this
    // callback and retain the chain to exit; Stop only revokes its presentation.
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&Draw),&module))return false;
    CompatibleDetour* candidate=nullptr;
    try {
        candidate=new CompatibleDetour(base+Evidence::DrawRva,
            reinterpret_cast<std::uintptr_t>(&Draw),&original);
        // Its leading CALL queries menu flags; following it would hook that
        // shared getter instead of the help renderer we just verified.
        candidate->setIsFollowCallOnFnAddress(false);
        if(!candidate->hook()){delete candidate;return false;}
        installedBase=base;detour=candidate;
        enabled.store(true,std::memory_order_release);return true;
    } catch(...) {delete candidate;return false;}
}

inline void Stop() noexcept {enabled.store(false,std::memory_order_release);}

inline int Pump(std::uint64_t callback,unsigned int argument,bool customMenu){
    // Suppression belongs to one Pump invocation and its thread. Restore on
    // normal return, nested invocation and native SEH; other threads stay native.
    const bool previous=suppressed;
    int result=0;
    __try {
        suppressed=customMenu;
        result=reinterpret_cast<int(__cdecl*)(unsigned int)>(callback)(argument);
    } __finally {suppressed=previous;}
    return result;
}
}
