// Jarvis-HOOK: an optional public Fahrenheit module owns the bootstrap and frame delivery.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "FahrenheitBridge.h"
#include "MinHookBatchCoordinator.h"
#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <MinHook.h>
#endif

namespace FfxHooks::Coexistence {
namespace {
std::atomic<const BridgeCallbacks*> callbacks{nullptr};
std::atomic<bool> scheduled{false};
std::atomic<DWORD> renderThread{0};
std::atomic<void*> renderSwapChain{nullptr};
std::atomic<std::uint32_t> frames{0};
std::atomic_flag inFrame=ATOMIC_FLAG_INIT;
std::atomic<std::uint32_t> resizeSerial{0},resizeTicket{0};
std::atomic<DWORD> resizeThread{0};
#ifdef FFXHOOKS_HAVE_POLYHOOK
bool Create(std::uintptr_t target,std::uintptr_t detour,std::uintptr_t* original) noexcept {
    return MH_CreateHook(reinterpret_cast<void*>(target),reinterpret_cast<void*>(detour),
        reinterpret_cast<void**>(original))==MH_OK;
}
bool Enable(std::uintptr_t target) noexcept {return MH_EnableHook(reinterpret_cast<void*>(target))==MH_OK;}
bool Disable(std::uintptr_t target) noexcept {
    const auto status=MH_DisableHook(reinterpret_cast<void*>(target));return status==MH_OK||status==MH_ERROR_DISABLED;
}
bool Remove(std::uintptr_t target) noexcept {return MH_RemoveHook(reinterpret_cast<void*>(target))==MH_OK;}
const DetourApi sharedApi{Create,Enable,Disable,Remove};
#endif
}
bool RegisterCallbacks(const BridgeCallbacks* value) noexcept {
    if(!value||!value->schedule||!value->frame||!value->resize)return false;
    const BridgeCallbacks* expected=nullptr;
    return callbacks.compare_exchange_strong(expected,value,std::memory_order_acq_rel)||expected==value;
}
void ObservePeer() noexcept {
    // Stage1's native module is observable before managed modules commit hooks.
    // MinHook alone could belong to an unrelated mod and is not a Fahrenheit signal.
    runtime.Observe(GetModuleHandleW(L"fhstage1.dll")!=nullptr);
}
bool PrepareProvider() noexcept {
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if(runtime.PeerPresent()){
        HMODULE release=GetModuleHandleW(L"minhook.x32.dll");
        HMODULE debug=GetModuleHandleW(L"minhook.x32d.dll");
        if((release&&debug)||(!release&&!debug))return false;
        if(MH_BindSharedProvider(release?release:debug)!=MH_OK)return false;
    }
    if(MinHookBatch::EnsureProcessInitialized()!=MinHookBatch::InitializationResult::Ready)return false;
    if(runtime.PeerPresent())detourApi.store(&sharedApi,std::memory_order_release);
    return true;
#else
    return !runtime.PeerPresent();
#endif
}
}

extern "C" int __cdecl FfxHooks_FahrenheitQueryV1(
    FfxHooks::Coexistence::BridgeStatusV1* output,std::uint32_t size){
    using namespace FfxHooks::Coexistence;
    if(!output||size!=sizeof(BridgeStatusV1))return 0;
    const BridgeStatusV1 snapshot{sizeof(BridgeStatusV1),kBridgeAbi,
        static_cast<std::uint32_t>(runtime.Read()),kBridgeCapabilities,
        runtime.FrameAllowed()&&runtime.SaveServicesAllowed()?1u:0u,frames.load(std::memory_order_acquire)};
    __try{*output=snapshot;return 1;}__except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
extern "C" int __cdecl FfxHooks_FahrenheitReadyV1(std::uint32_t abi,std::uint32_t capabilities){
    using namespace FfxHooks::Coexistence;
    if(abi!=kBridgeAbi||capabilities!=kBridgeCapabilities)return 0;
    ObservePeer();
    if(!runtime.Ready(abi,capabilities))return 0;
    if(runtime.FrameAllowed())return 1; // Synchronous V2 bootstrap already completed.
    const auto* target=callbacks.load(std::memory_order_acquire);
    if(target&&!scheduled.exchange(true,std::memory_order_acq_rel))target->schedule();
    return 1;
}
extern "C" int __cdecl FfxHooks_FahrenheitFrameV1(void* swapChain,std::uint32_t inputCapture){
    using namespace FfxHooks::Coexistence;
    if(!swapChain||(inputCapture&~3u)||!runtime.FrameAllowed())return 0;
    const auto* target=callbacks.load(std::memory_order_acquire);
    if(!target)return 0;
    // A skipped frame must not claim the renderer while a resize owns this gate.
    if(inFrame.test_and_set(std::memory_order_acquire))return 0;
    const DWORD thread=GetCurrentThreadId();DWORD expected=0;
    if(!renderThread.compare_exchange_strong(expected,thread,std::memory_order_acq_rel)&&expected!=thread){
        inFrame.clear(std::memory_order_release);return 0;
    }
    void* previous=nullptr;
    if(!renderSwapChain.compare_exchange_strong(previous,swapChain,std::memory_order_acq_rel)&&previous!=swapChain){
        inFrame.clear(std::memory_order_release);return 0;
    }
    peerInputCapture.store(inputCapture,std::memory_order_release);
    bool complete=false;
    __try{if(runtime.FrameAllowed()){target->frame(swapChain,inputCapture);complete=true;}}
    __except(EXCEPTION_EXECUTE_HANDLER){runtime.Stop();}
    if(complete)frames.fetch_add(1,std::memory_order_relaxed);
    inFrame.clear(std::memory_order_release);
    return complete?1:0;
}
extern "C" std::uint32_t __cdecl FfxHooks_FahrenheitResizeBeginV1(void* swapChain){
    using namespace FfxHooks::Coexistence;
    if(!swapChain)return 0;
    const auto* target=callbacks.load(std::memory_order_acquire);
    // The gate spans both resource release and the caller's ResizeBuffers.
    // Never wait for a render callback on the message-pump thread. The caller
    // must preserve DXGI failure when a frame or another resize still owns it.
    if(inFrame.test_and_set(std::memory_order_acquire))return 0;
    // Read ownership after acquiring the gate: a first frame may have created
    // resources while this resize was approaching the gate.
    const auto current=renderSwapChain.load(std::memory_order_acquire);
    if(current&&current!=swapChain){inFrame.clear(std::memory_order_release);return 0;}
    const auto serial=resizeSerial.load(std::memory_order_relaxed);
    if(serial==UINT32_MAX){inFrame.clear(std::memory_order_release);return 0;}
    bool complete=current==nullptr;
    __try{if(current&&target){target->resize(swapChain);complete=true;}}
    __except(EXCEPTION_EXECUTE_HANDLER){runtime.Stop();}
    if(!complete){inFrame.clear(std::memory_order_release);return 0;}
    resizeSerial.store(serial+1,std::memory_order_relaxed);
    resizeThread.store(GetCurrentThreadId(),std::memory_order_relaxed);
    resizeTicket.store(serial+1,std::memory_order_release);
    return serial+1;
}
extern "C" int __cdecl FfxHooks_FahrenheitResizeEndV1(std::uint32_t ticket){
    using namespace FfxHooks::Coexistence;
    if(!ticket||resizeTicket.load(std::memory_order_acquire)!=ticket||
       resizeThread.load(std::memory_order_relaxed)!=GetCurrentThreadId())return 0;
    if(!resizeTicket.compare_exchange_strong(ticket,0,std::memory_order_acq_rel))return 0;
    resizeThread.store(0,std::memory_order_relaxed);
    inFrame.clear(std::memory_order_release);return 1;
}
