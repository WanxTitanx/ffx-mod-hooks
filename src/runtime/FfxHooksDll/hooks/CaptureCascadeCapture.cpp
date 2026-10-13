#include "CaptureCascadeCapture.h"
#include "CaptureCascadeCaptureCore.h"
#include "CaptureCascadeRuntime.h"
#include "NativeUiHookSupport.h"
#include "../shared/ExecutableProfile.h"
#include <intrin.h>
#include <atomic>

namespace FfxHooks::CaptureCascade::Capture {
namespace {
static_assert(std::atomic<bool>::is_always_lock_free,"Native capture teardown must not wait under loader lock");
constexpr auto captureRva=ExecutableProfile::Rva<0x390B80>();
constexpr auto callerRva=ExecutableProfile::Rva<0x38C8F6>();
constexpr auto messageRva=ExecutableProfile::Rva<0x39CFF0>();
std::uintptr_t base=0;void* originals[1]{};
std::atomic<bool> accepting{false};
bool attempted=false;void(*logger)(const char*)=nullptr;
using Original=int(__cdecl*)(unsigned,unsigned);
template<class T> bool Read(std::uintptr_t at,T& out) noexcept {return at>=0x10000&&NativeUiSupport::Copy(&out,reinterpret_cast<const void*>(at),sizeof(out));}
bool Announce(unsigned slot) noexcept {
    __try{reinterpret_cast<int(__cdecl*)(int,unsigned,int,int,int)>(base+messageRva)(5,slot,0x300A,0x1B,0x23);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
int __cdecl Shim(unsigned slot,unsigned arena){
    const auto original=reinterpret_cast<Original>(originals[0]);
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    CaptureEvidence e;e.enabled=accepting.load();e.bound=Runtime::Ready();e.nativeCaller=caller==base+callerRva;e.slot=slot;e.arena=arena;
    std::uint32_t pool=0;unsigned char battle=0;
    if(e.enabled&&e.bound&&e.nativeCaller&&slot>=18&&slot<26&&arena==255&&
       Read(base+ExecutableProfile::Rva<0xD2A8E0>(),battle)&&battle&&Read(base+ExecutableProfile::Rva<0xD334CC>(),pool)&&
       pool>=0x10000&&pool<=UINT32_MAX-31*0xF90u){
        const auto actor=std::uintptr_t(pool)+slot*0xF90u;
        std::uint16_t identity=0,embedded=0,arenaId=0;unsigned char marker=0,petrify=0;std::int32_t hp=1;
        if(Read(actor+0xC,embedded)&&Read(actor+0xE,identity)&&Read(actor+0x6D6,arenaId)&&Read(actor+0xDD0,marker)&&
           Read(actor+0x5D0,hp)&&Read(actor+0x617,petrify)){
            e.battle=true;e.matchingActor=embedded==slot&&arenaId==arena;e.monster=identity;e.marker=marker;e.death=hp<=0||(petrify&1);
        }
    }
    if(CapturedFamily(e)<kFamilies&&Runtime::RecordCapture(e.monster-0x1000)){
        // The native death producer has already checked command capture support
        // and the attacker's Capture auto-ability. Return its ordinary success
        // result and use its captured-message queue without invoking either
        // native count reader/writer with a custom or out-of-range Arena slot.
        if(!Announce(slot)&&logger)logger("[ffx-hooks] Capture Cascade: capture recorded; native capture message unavailable\n");
        return 1;
    }
    return original?original(slot,arena):0;
}
bool Profile(std::uintptr_t module) noexcept {
    constexpr unsigned char prefix[]={0x55,0x8B,0xEC,0x53,0x56,0x8B,0x75,0x0C,0x33,0xDB,0x57,0x81,0xFE,0xFF,0x00,0x00,0x00};
    constexpr unsigned char weapon[]={0xF7,0x41,0x1C,0x00,0x00,0x04,0x00,0x74,0x21,0xF6,0x86,0xC0,0x06,0x00,0x00,0x04,0x74,0x18};
    constexpr unsigned char marker[]={0xC6,0x87,0xD0,0x0D,0x00,0x00,0x01};
    unsigned char bytes[32]{},call[5]{};std::int32_t displacement=0;
    if(!NativeUiSupport::Copy(bytes,reinterpret_cast<const void*>(module+captureRva),sizeof(prefix))||std::memcmp(bytes,prefix,sizeof(prefix))||
       !NativeUiSupport::Copy(bytes,reinterpret_cast<const void*>(module+ExecutableProfile::Rva<0x3895C0>()),sizeof(weapon))||std::memcmp(bytes,weapon,sizeof(weapon))||
       !NativeUiSupport::Copy(bytes,reinterpret_cast<const void*>(module+ExecutableProfile::Rva<0x3895E1>()),sizeof(marker))||std::memcmp(bytes,marker,sizeof(marker))||
       !NativeUiSupport::Copy(call,reinterpret_cast<const void*>(module+callerRva-5),5)||call[0]!=0xE8)return false;
    std::memcpy(&displacement,call+1,4);
    return callerRva+displacement==captureRva;
}
}
bool Start(std::uintptr_t module,void(*log)(const char*)) noexcept {
    if(!Runtime::Requested())return false;
    if(attempted)return accepting.load();
    attempted=true;base=module;logger=log;
    try{
        if(!Profile(module))return false;
        const std::uint32_t targets[]={captureRva};void* replacements[]={reinterpret_cast<void*>(Shim)};
        if(!NativeUiSupport::Install(base,targets,replacements,originals,MinHookBatch::Owner::CaptureCascade,reinterpret_cast<const void*>(&Start)))return false;
        accepting=true;return true;
    }catch(...){return false;}
}
void Stop() noexcept {accepting=false;}
}
