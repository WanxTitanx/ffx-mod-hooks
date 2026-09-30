#pragma once
#include "FahrenheitCoexistenceCore.h"
#include <cstdint>

namespace FfxHooks::Coexistence {
struct BridgeCallbacks {
    void (*schedule)() noexcept;
    void (*frame)(void* swapChain,std::uint32_t inputCapture) noexcept;
    void (*resize)(void* swapChain) noexcept;
};
struct BridgeStatusV1 {
    std::uint32_t size,abi,phase,capabilities,managedSaveCompatible,frames;
};
static_assert(sizeof(BridgeStatusV1)==24,"Stable bridge ABI layout");
bool RegisterCallbacks(const BridgeCallbacks*) noexcept;
void ObservePeer() noexcept;
bool PrepareProvider() noexcept;
inline std::atomic<std::uint32_t> peerInputCapture{0};
inline bool PeerCapturesInput() noexcept {return peerInputCapture.load(std::memory_order_acquire)!=0;}
}

#ifdef _WIN32
#include <windows.h>
extern "C" {
__declspec(dllexport) int __cdecl FfxHooks_FahrenheitQueryV1(FfxHooks::Coexistence::BridgeStatusV1*,std::uint32_t);
__declspec(dllexport) int __cdecl FfxHooks_FahrenheitReadyV1(std::uint32_t abi,std::uint32_t capabilities);
__declspec(dllexport) int __cdecl FfxHooks_FahrenheitFrameV1(void* swapChain,std::uint32_t inputCapture);
__declspec(dllexport) std::uint32_t __cdecl FfxHooks_FahrenheitResizeBeginV1(void* swapChain);
__declspec(dllexport) int __cdecl FfxHooks_FahrenheitResizeEndV1(std::uint32_t ticket);
}
#endif
