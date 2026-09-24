#pragma once

// Legacy API name retained so older menu/runtime call sites remain source-compatible. This
// component is now strictly a no-write Monster AI registration/dispatch observer; no swap pair is
// armed and the original dispatcher arguments are never changed.

#include <cstdint>

namespace FfxHooks {

enum class F7AiObserverStatus : uint8_t {
    Off = 0,
    ObservePendingRestart,
    Observing,
    RetainedInert,
    Unavailable,
    Stopping,
};

bool F7AiSwap_Install(uintptr_t moduleBase, void (*log)(const char*));
// The worker uses this status-only path when process-global MinHook setup failed before feature
// installation. It must never initialize MinHook or attempt a detour on its own.
void F7AiSwap_ReportSetupFailure(void (*log)(const char*));
void F7AiSwap_RequestStop();
void F7AiSwap_Remove();
bool F7AiSwap_IsEnabled();
F7AiObserverStatus F7AiSwap_Status();
const char* F7AiSwap_StatusName();
const char* F7AiSwap_DetailText();

} // namespace FfxHooks
