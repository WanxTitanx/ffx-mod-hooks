#include "DialogSkipHook.h"
#include "F8RuntimeCore.h"
#include "../shared/ffx_addresses.h"
#include "../shared/Config.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdarg>
#include <cstdio>

#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <polyhook2/Detour/x86Detour.hpp>
#endif

namespace FfxHooks {

namespace {

static bool g_installed = false;
static bool g_terminalInstallFailure = false;
static void (*g_logFn)(const char*) = nullptr;
static void* g_detour = nullptr; /* PLH::x86Detour* when PolyHook is compiled in. */
alignas(8) static uint64_t g_trampoline = 0;
/* The callback, Present producer, focus callback, and detach path share exactly one word.
 * Separate request/effective/stop atomics allowed a stale Present frame to re-arm bypass after
 * focus loss or stop. Stop now dominates every interpretation of this packed publication. */
alignas(4) static volatile LONG g_dialogStateWord = 0;

static_assert(sizeof(LONG) == 4, "Dialog Skip atomics require 32-bit LONG storage");

static uint32_t AtomicLoadDialogState() noexcept {
    return static_cast<uint32_t>(InterlockedCompareExchange(
        &g_dialogStateWord, 0, 0));
}

static void HookLog(const char* fmt, ...) {
    if (!g_logFn) return;
    char line[512] = {};
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    g_logFn(line);
}

static const char* DialogSkipInstallStatusName(DialogSkipInstallStatus status) noexcept {
    switch (status) {
    case DialogSkipInstallStatus::Installed: return "Installed";
    case DialogSkipInstallStatus::AlreadyInstalled: return "AlreadyInstalled";
    case DialogSkipInstallStatus::UnsupportedProfile: return "UnsupportedProfile";
    case DialogSkipInstallStatus::TargetOutOfRange: return "TargetOutOfRange";
    case DialogSkipInstallStatus::SignatureMismatch: return "SignatureMismatch";
    case DialogSkipInstallStatus::DetourLikePrefix: return "DetourLikePrefix";
    case DialogSkipInstallStatus::DetourFailed: return "DetourFailed";
    case DialogSkipInstallStatus::PolyHookUnavailable: return "PolyHookUnavailable";
    }
    return "Unknown";
}

static bool FinishDialogSkipInstall(
    DialogSkipInstallStatus status,
    const DialogSkipTargetValidation& validation,
    DialogSkipInstallStatus* statusOut,
    bool result) {
    if (statusOut) *statusOut = status;
    HookLog(
        "[ffx-hooks] DialogSkipHook install status=%s target_rva=0x%08X "
        "mismatch_offset=0x%02X\n",
        DialogSkipInstallStatusName(status),
        static_cast<unsigned>(RVA_FFX_FMODVOICE_READ_EVENT_DATA),
        static_cast<unsigned>(validation.mismatchOffset));
    return result;
}

/* FFX_FmodVoice_ReadEventData @ preferred VA 0x70AEC0 (RVA 0x30AEC0).
 * The supported build passes `this` in ECX, two arguments on the stack, and returns with `ret 8`.
 * A __fastcall shim receives ECX/EDX explicitly while retaining the two original stack slots.
 * The callback reads one aligned atomic only; configuration and edge logging stay on Present. */
using ReadEventDataFn = int(__fastcall*)(
    void* this_, void* edx, unsigned int evtId, uintptr_t context);

static int __fastcall ReadEventData_DialogSkipHook(void* this_, void* edx, unsigned int evtId, uintptr_t context) {
    const uint32_t stateWord = AtomicLoadDialogState();
    if (DialogSkipPublishedStateEffective(stateWord)) {
        return 0;
    }
    const ReadEventDataFn original = reinterpret_cast<ReadEventDataFn>(
        static_cast<uintptr_t>(g_trampoline));
    return original ? original(this_, edx, evtId, context) : 0;
}

} // namespace

bool InstallDialogSkipHook(uintptr_t moduleBase, void* logFn, DialogSkipInstallStatus* statusOut) {
    g_logFn = reinterpret_cast<void (*)(const char*)>(logFn);
    if (statusOut) *statusOut = DialogSkipInstallStatus::PolyHookUnavailable;
    DialogSkipTargetValidation validation{};
    if (g_terminalInstallFailure) {
        return FinishDialogSkipInstall(
            DialogSkipInstallStatus::DetourFailed, validation, statusOut, false);
    }
    if (g_installed) {
        return FinishDialogSkipInstall(
            DialogSkipInstallStatus::AlreadyInstalled, validation, statusOut, true);
    }

#ifdef FFXHOOKS_HAVE_POLYHOOK
    constexpr size_t kImageHeaderProbeBytes = 0x1000u;
    F8Runtime::ExecutableIdentity identity{};
    const F8Runtime::ProfileResult profile = F8Runtime::ParseExecutableIdentity(
        reinterpret_cast<const uint8_t*>(moduleBase), kImageHeaderProbeBytes, &identity);
    if (profile != F8Runtime::ProfileResult::Supported ||
        !F8Runtime::IsSupportedExecutable(identity)) {
        return FinishDialogSkipInstall(
            DialogSkipInstallStatus::UnsupportedProfile, validation, statusOut, false);
    }
    if (F8Runtime::ValidateImageRange(
            RVA_FFX_FMODVOICE_READ_EVENT_DATA, kDialogSkipTargetLength,
            identity.sizeOfImage) != F8Runtime::ProfileResult::Supported) {
        return FinishDialogSkipInstall(
            DialogSkipInstallStatus::TargetOutOfRange, validation, statusOut, false);
    }
    const uint8_t* const target = reinterpret_cast<const uint8_t*>(
        moduleBase + RVA_FFX_FMODVOICE_READ_EVENT_DATA);
    validation = ValidateDialogSkipTargetSignature(target, kDialogSkipTargetLength);
    if (validation.status == DialogSkipTargetStatus::DetourLikePrefix) {
        return FinishDialogSkipInstall(
            DialogSkipInstallStatus::DetourLikePrefix, validation, statusOut, false);
    }
    if (validation.status != DialogSkipTargetStatus::Match) {
        return FinishDialogSkipInstall(
            DialogSkipInstallStatus::SignatureMismatch, validation, statusOut, false);
    }

    bool hooked = false;
    try {
        g_detour = new PLH::x86Detour(
            static_cast<uint64_t>(moduleBase + RVA_FFX_FMODVOICE_READ_EVENT_DATA),
            reinterpret_cast<uint64_t>(&ReadEventData_DialogSkipHook),
            &g_trampoline);
        hooked = static_cast<PLH::x86Detour*>(g_detour)->hook();
    } catch (...) {
        hooked = false;
    }
    if (!g_detour || !hooked || g_trampoline == 0) {
        /* PolyHook may have allocated a gateway or changed bytes before returning false/throwing.
         * Without callback quiescence it is unsafe to unhook or free that object here. Retain it
         * pass-through and reject retries for the process lifetime. */
        InterlockedExchange(
            &g_dialogStateWord,
            static_cast<LONG>(PackDialogSkipPublishedState(false, true, false, false)));
        g_terminalInstallFailure = true;
        return FinishDialogSkipInstall(
            DialogSkipInstallStatus::DetourFailed, validation, statusOut, false);
    }

    g_installed = true;
    InterlockedExchange(
        &g_dialogStateWord,
        static_cast<LONG>(PackDialogSkipPublishedState(true, false, false, false)));
    return FinishDialogSkipInstall(
        DialogSkipInstallStatus::Installed, validation, statusOut, true);
#else
    (void)moduleBase;
    return FinishDialogSkipInstall(
        DialogSkipInstallStatus::PolyHookUnavailable, validation, statusOut, false);
#endif
}

void DialogSkipFrameTick(bool speedHack8Active) {
    uint32_t oldWord = AtomicLoadDialogState();
    if (UnpackDialogSkipPublishedState(oldWord).stopped) return;

    const bool manual = Config::GetBool("input.dialog_skip", false);
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        const uint32_t newWord = ResolveDialogSkipPublishedRequests(
            oldWord, manual, speedHack8Active);
        if (newWord == oldWord) return;
        const LONG observed = InterlockedCompareExchange(
            &g_dialogStateWord,
            static_cast<LONG>(newWord),
            static_cast<LONG>(oldWord));
        if (static_cast<uint32_t>(observed) == oldWord) {
            const DialogSkipPublishedState published =
                UnpackDialogSkipPublishedState(newWord);
            HookLog(
                "[ffx-hooks] DialogSkip: manual=%d speed8=%d effective=%d\n",
                published.manualRequested ? 1 : 0,
                published.speedHack8Requested ? 1 : 0,
                DialogSkipPublishedStateEffective(newWord) ? 1 : 0);
            return;
        }
        oldWord = static_cast<uint32_t>(observed);
        if (UnpackDialogSkipPublishedState(oldWord).stopped) return;
    }
}

DialogSkipRuntimeSnapshot GetDialogSkipRuntimeSnapshot() noexcept {
    DialogSkipRuntimeSnapshot snapshot{};
    const uint32_t word = AtomicLoadDialogState();
    const DialogSkipPublishedState published = UnpackDialogSkipPublishedState(word);
    snapshot.hookReady = published.hookReady && !published.stopped;
    snapshot.manualRequested = published.manualRequested;
    snapshot.speedHack8Requested = published.speedHack8Requested;
    snapshot.effectiveBypass = DialogSkipPublishedStateEffective(word);
    return snapshot;
}

void DialogSkipNotifySpeedHack8Inactive() noexcept {
    /* A single atomic AND preserves manual/ready/stop while closing only Speed ownership. No
     * Present, configuration, or logging service is required from the focus callback. */
    InterlockedAnd(
        &g_dialogStateWord,
        static_cast<LONG>(~kDialogSkipSpeed8Mask));
}

void RequestDialogSkipStop() noexcept {
    /* Loader-lock safe: publish the dominating stop bit first, then clear stale request bits.
     * The callback observes bypass=false as soon as the first atomic completes. */
    InterlockedOr(
        &g_dialogStateWord,
        static_cast<LONG>(kDialogSkipStoppedMask));
    InterlockedAnd(
        &g_dialogStateWord,
        static_cast<LONG>(~(kDialogSkipManualMask | kDialogSkipSpeed8Mask)));
}

void RemoveDialogSkipHook() {
    RequestDialogSkipStop();
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if (g_detour && !g_terminalInstallFailure) {
        static_cast<PLH::x86Detour*>(g_detour)->unHook();
        delete static_cast<PLH::x86Detour*>(g_detour);
        g_detour = nullptr;
    }
#endif
    if (!g_terminalInstallFailure) {
        g_trampoline = 0;
        g_installed = false;
        InterlockedExchange(
            &g_dialogStateWord,
            static_cast<LONG>(PackDialogSkipPublishedState(false, true, false, false)));
    }
}

} // namespace FfxHooks
