#pragma once
#include "FahrenheitCoexistenceCore.h"
namespace FfxHooks::Coexistence {
// Menus must agree across WndProc, the native game pump and polled shortcuts.
// This gates input only: frame maintenance/cleanup must keep running while a
// peer owns capture, otherwise native modal state can never release itself.
inline bool NativeInputAllowed(const State& state,std::uint32_t capture) noexcept {
    return !state.PeerPresent()||(state.FrameAllowed()&&capture==0);
}
}
