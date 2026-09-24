#include "SinCurseHook.h"
#include "F7UnsafePrototypePolicy.h"

namespace FfxHooks {

namespace {

static bool g_installed = false;

} // namespace

SinCurseInstallResult InstallSinCurseHook(uintptr_t moduleBase, void* logFn) {
    (void)moduleBase;
    const auto status = ResolveF7UnsafePrototype(
        F7UnsafePrototype::LegacySinWriter, false, false);

    SinCurseInstallResult result = {};
    result.reason = status.reason;

    // WHY: the previous field-load hook launched a disk materializer from the
    // game thread. Flags, sidecars, and environment values must never revive
    // that path until a separate RAM-only implementation is reviewed.
    if (logFn) {
        reinterpret_cast<void (*)(const char*)>(logFn)(status.reason);
    }
    return result;
}

bool RemoveSinCurseHook() {
    // The quarantined implementation owns no detour, worker, file, or process.
    // Keeping teardown idempotent preserves the existing shutdown contract.
    g_installed = false;
    return true;
}

bool IsSinCurseHookInstalled() {
    return g_installed;
}

const char* GetCurrentRegion() {
    return "";
}

int GetCurrentThreatCap() {
    return 0;
}

const char* GetCurrentField() {
    return "";
}

} // namespace FfxHooks
