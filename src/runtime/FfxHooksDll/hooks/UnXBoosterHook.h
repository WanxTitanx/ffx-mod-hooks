#pragma once

#include <cstdint>

namespace FfxHooks {

using BoosterLogFn = void (*)(const char*);

/// Validate and publish the adapter; mutation waits for a ready Present producer.
bool StartUnXBoosterHook(uintptr_t moduleBase, BoosterLogFn log);
/// Publish Present readiness or a sticky terminal failure to the runtime lifecycle.
void NotifyUnXBoosterPresentProducer(bool ready, bool terminalFailure);
/// Run admitted Present-thread work through the runtime's 33 ms tick gate.
void UnXBoosterFrameTick(uint32_t nowMs);
/// Close frame admission atomically without waiting, restoring, or removing hooks.
void RequestUnXBoosterStop();

} // namespace FfxHooks
