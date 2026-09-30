#pragma once
#include <polyhook2/Detour/x86Detour.hpp>
#include "FahrenheitCoexistenceCore.h"

#ifndef FFXHOOKS_COEXISTENCE
namespace FfxHooks { using CompatibleDetour = PLH::x86Detour; }
#else
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace FfxHooks {
// Standalone installations retain the existing PolyHook implementation. When
// Fahrenheit owns the process, both families use the same MinHook registry.
class CompatibleDetour {
    std::uint64_t target_, replacement_;
    std::uint64_t* original_;
    PLH::x86Detour* standalone_=nullptr;
    const Coexistence::DetourApi* api_=nullptr;
    std::uintptr_t trampoline_=0;
    unsigned char* relay_=nullptr;
    volatile LONG* destination_=nullptr;
    bool attempted_=false, active_=false, peer_=false;

    bool PrepareRelay() noexcept {
        static_assert(sizeof(void*)==4, "The game detour ABI is x86");
        SYSTEM_INFO info{}; GetSystemInfo(&info);
        if(info.dwPageSize<6 || info.dwPageSize>65536)return false;
        auto* allocation=static_cast<unsigned char*>(VirtualAlloc(nullptr,
            static_cast<SIZE_T>(info.dwPageSize)*2,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        if(!allocation)return false;
        destination_=reinterpret_cast<volatile LONG*>(allocation+info.dwPageSize);
        // The relay performs an indirect JMP through a separately writable,
        // aligned cell. No stack, register or calling convention is changed.
        allocation[0]=0xFF; allocation[1]=0x25;
        const auto cell=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(destination_));
        std::memcpy(allocation+2,&cell,sizeof(cell));
        DWORD old=0;
        if(!VirtualProtect(allocation,info.dwPageSize,PAGE_EXECUTE_READ,&old) ||
           !FlushInstructionCache(GetCurrentProcess(),allocation,6)){
            VirtualFree(allocation,0,MEM_RELEASE);destination_=nullptr;return false;
        }
        relay_=allocation;return true;
    }
    void Route(std::uintptr_t address) noexcept {
        if(destination_)InterlockedExchange(destination_,static_cast<LONG>(address));
    }
public:
    CompatibleDetour(std::uint64_t target,std::uint64_t replacement,std::uint64_t* original)
        :target_(target),replacement_(replacement),original_(original){}
    CompatibleDetour(const CompatibleDetour&)=delete;
    CompatibleDetour& operator=(const CompatibleDetour&)=delete;
    ~CompatibleDetour(){
        if(standalone_)delete standalone_;
        // Once created, a relay/trampoline can be reached through another
        // provider's chain. Neutralize the relay, retain its storage to exit.
        if(peer_)Route(trampoline_);
    }
    bool hook(){
        if(active_)return true;
        if(attempted_)return false;
        attempted_=true;peer_=Coexistence::runtime.PeerPresent();
        if(!peer_){
            standalone_=new PLH::x86Detour(target_,replacement_,original_);
            active_=standalone_->hook();return active_;
        }
        api_=Coexistence::detourApi.load(std::memory_order_acquire);
        if(!original_ || !api_ || !api_->create || !api_->enable || !api_->disable ||
           !target_ || !replacement_ || !PrepareRelay())return false;
        const bool created=api_->create(static_cast<std::uintptr_t>(target_),
             reinterpret_cast<std::uintptr_t>(relay_),&trampoline_);
        // A created entry may be retained after post-create validation fails.
        // Publish its neutral destination before returning failure to a caller.
        if(trampoline_)Route(trampoline_);
        if(!created || !trampoline_)return false;
        *original_=trampoline_;
        // A partial enable failure must keep a callable native path even when
        // the existing caller deletes this object and clears its original.
        if(!api_->enable(static_cast<std::uintptr_t>(target_)))return false;
        Route(static_cast<std::uintptr_t>(replacement_));
        active_=true;return true;
    }
    bool unHook(){
        if(standalone_){const bool ok=standalone_->unHook();if(ok)active_=false;return ok;}
        if(!peer_ || !trampoline_)return true;
        Route(trampoline_);
        const bool ok=api_ && api_->disable(static_cast<std::uintptr_t>(target_));
        if(ok)active_=false;
        return ok;
    }
};
}
#endif
