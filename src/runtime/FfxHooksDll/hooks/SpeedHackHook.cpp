#include "SpeedHackHook.h"
#include "DialogSkipHook.h"
#include "F8RuntimeCore.h"
#include "../shared/ffx_addresses.h"
#include "../shared/Config.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <atomic>

#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <polyhook2/Detour/x86Detour.hpp>
#endif

namespace FfxHooks {
namespace {

static bool g_installed = false;
static bool g_terminalInstallFailure = false;
static SpeedHackInstallStatus g_terminalInstallStatus =
    SpeedHackInstallStatus::PolyHookUnavailable;
static void (*g_logFn)(const char*) = nullptr;
static void* g_globalTickDetour = nullptr;  /* PLH::x86Detour* in PolyHook builds. */

/* PolyHook publishes the target patch only after writing the gateway. This durable storage is
 * callback-visible from the first possible detoured instruction. */
alignas(8) static uint64_t g_globalTickTrampoline = 0;

/* One packed route prevents native 2x/4x and the clean-room fast-field-scene 8x bridge from being
 * published independently. Telemetry is fixed-size and callback-only; it never allocates. */
alignas(4) static volatile LONG g_publishedRouteWord = 0;
alignas(4) static volatile LONG g_appliedRouteWord = 0;
alignas(4) static volatile LONG g_routeCallbackBaseline = 0;
alignas(4) static volatile LONG g_callbackCount = 0;
alignas(4) static volatile LONG g_callbackInputDeltaBits = 0;
alignas(4) static volatile LONG g_callbackOutputDeltaBits = 0;
alignas(4) static volatile LONG g_callbackNativeConflict = 0;

alignas(4) static volatile LONG g_nativeStateReady = 0;
alignas(4) static volatile LONG g_nativeAvailabilityReady = 0;
alignas(4) static volatile LONG g_globalTickHookReady = 0;
alignas(4) static volatile LONG g_dialogBypassReady = 0;
alignas(4) static volatile LONG g_globalTargetOwned = 0;
alignas(4) static volatile LONG g_nativeOwned = 0;
alignas(4) static volatile LONG g_nativeLastWrite = 0;
alignas(4) static volatile LONG g_runtimePhase =
    static_cast<LONG>(SpeedHackRuntimePhase::Unavailable);
alignas(4) static volatile LONG g_conflictReason =
    static_cast<LONG>(SpeedHackConflictReason::None);
/* The display reason may change as transient UnX/target conflicts come and go. Native drift is
 * a separate sticky ownership fact and can clear only at the validated 1x reset policy. */
alignas(4) static volatile LONG g_nativeDriftLatched = 0;

alignas(4) static volatile LONG g_stopRequested = 0;
alignas(4) static volatile LONG g_foregroundLost = 0;
/* Focus/stop increments this epoch before neutralization. An older Present frame may publish
 * only if the epoch captured when it acquired producer ownership is still current. */
alignas(4) static volatile LONG g_neutralEpoch = 0;
alignas(4) static volatile LONG g_presentProducerActive = 0;
alignas(4) static const float g_globalScale8 = 8.0f;

/* The state pointer is written once after supported-profile/range validation. The x86 bridge
 * reads it only to enforce the no-double-multiply invariant. */
alignas(4) static volatile LONG* g_nativeStateAddress = nullptr;
static const volatile uint8_t* g_nativeAvailabilityAddress = nullptr;
static uintptr_t g_globalTargetAddress = 0;
static uint8_t g_ownedGlobalTargetBytes[kSpeedHackGlobalTargetLength] = {};

static SpeedHackControlState g_control{};  /* Present-thread owner only. */
static std::atomic<SpeedHackShortcutReader> g_shortcutReader{nullptr};
static std::atomic<const SpeedHackMovieBridge*> g_movieBridge{nullptr};
static uint32_t g_lastAppliedLogWord = UINT32_MAX;
static SpeedHackConflictReason g_lastConflictLog = SpeedHackConflictReason::None;

static_assert(sizeof(LONG) == 4, "Speed Hack atomics require 32-bit LONG storage");

static uint32_t AtomicLoadRoute(const volatile LONG* word) noexcept {
    return static_cast<uint32_t>(InterlockedCompareExchange(
        const_cast<volatile LONG*>(word), 0, 0));
}

static LONG AtomicLoadLong(const volatile LONG* word) noexcept {
    return InterlockedCompareExchange(
        const_cast<volatile LONG*>(word), 0, 0);
}

static bool ProducerEpochStillCurrent(LONG producerEpoch) noexcept {
    return AtomicLoadLong(&g_stopRequested) == 0 &&
        AtomicLoadLong(&g_neutralEpoch) == producerEpoch;
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

static const char* SpeedHackBackendName(SpeedHackBackend backend) noexcept {
    switch (backend) {
    case SpeedHackBackend::NativeStandard: return "standard_boost";
    case SpeedHackBackend::FastFieldScenes: return "fast_field_scenes";
    case SpeedHackBackend::None: return "none";
    }
    return "unknown";
}

static const char* SpeedHackConflictName(SpeedHackConflictReason conflict) noexcept {
    switch (conflict) {
    case SpeedHackConflictReason::None: return "none";
    case SpeedHackConflictReason::NativeBusy: return "native_busy";
    case SpeedHackConflictReason::NativeDrift: return "native_drift";
    case SpeedHackConflictReason::UnXModuleLoaded: return "unx_module_loaded";
    case SpeedHackConflictReason::ExternalTargetDrift: return "external_target_drift";
    case SpeedHackConflictReason::UnXTargetDrift: return "unx_target_drift";
    }
    return "unknown";
}

static const char* SpeedHackInstallStatusName(SpeedHackInstallStatus status) noexcept {
    switch (status) {
    case SpeedHackInstallStatus::Installed: return "Installed";
    case SpeedHackInstallStatus::AlreadyInstalled: return "AlreadyInstalled";
    case SpeedHackInstallStatus::UnsupportedProfile: return "UnsupportedProfile";
    case SpeedHackInstallStatus::NativeStateOutOfRange: return "NativeStateOutOfRange";
    case SpeedHackInstallStatus::NativeAvailabilityOutOfRange:
        return "NativeAvailabilityOutOfRange";
    case SpeedHackInstallStatus::GlobalTargetOutOfRange: return "GlobalTargetOutOfRange";
    case SpeedHackInstallStatus::GlobalSignatureMismatch: return "GlobalSignatureMismatch";
    case SpeedHackInstallStatus::GlobalDetourLikePrefix: return "GlobalDetourLikePrefix";
    case SpeedHackInstallStatus::UnXModuleConflict: return "UnXModuleConflict";
    case SpeedHackInstallStatus::GlobalDetourFailed: return "GlobalDetourFailed";
    case SpeedHackInstallStatus::PolyHookUnavailable: return "PolyHookUnavailable";
    }
    return "Unknown";
}

static bool FinishSpeedHackInstall(
    SpeedHackInstallStatus status,
    const SpeedHackTargetValidation& validation,
    bool unxLoaded,
    SpeedHackInstallStatus* statusOut,
    bool result) {
    if (statusOut) *statusOut = status;
    HookLog(
        "[ffx-hooks] SpeedHackHook install status=%s "
        "global_mismatch=0x%02X global_actual=0x%08X global_expected=0x%08X "
        "unx_loaded=%u\n",
        SpeedHackInstallStatusName(status),
        static_cast<unsigned>(validation.mismatchOffset),
        static_cast<unsigned>(validation.actualOperand),
        static_cast<unsigned>(validation.expectedOperand),
        unxLoaded ? 1u : 0u);
    return result;
}

static bool IsFfxForeground() noexcept {
    const HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    DWORD foregroundProcessId = 0;
    return GetWindowThreadProcessId(foreground, &foregroundProcessId) != 0 &&
           foregroundProcessId == GetCurrentProcessId();
}

static bool IsUnXModuleLoaded() noexcept {
    return GetModuleHandleW(L"UnX.dll") != nullptr ||
        GetModuleHandleW(L"unx.dll") != nullptr;
}

static uint32_t NextRouteGeneration(uint32_t currentWord) noexcept {
    const uint32_t current = UnpackSpeedHackPublishedRoute(currentWord).generation;
    return (current + 1u) & 0x00FFFFFFu;
}

static bool PublishedRouteMatches(
    const SpeedHackPublishedRoute& current,
    const SpeedHackArbitration& desired) noexcept {
    return current.requestedFactor == desired.route.requestedFactor &&
        current.routedFactor == desired.route.routedFactor &&
        current.backend == desired.route.backend &&
        current.unavailable == desired.route.unavailable &&
        current.paused == (desired.phase == SpeedHackRuntimePhase::Paused);
}

static uint32_t PublishNeutralRouteAsync() noexcept {
    SpeedHackRoute neutral{};
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        const uint32_t oldWord = AtomicLoadRoute(&g_publishedRouteWord);
        const uint32_t newWord = PackSpeedHackPublishedRoute(
            neutral, NextRouteGeneration(oldWord), false);
        const LONG observed = InterlockedCompareExchange(
            &g_publishedRouteWord,
            static_cast<LONG>(newWord),
            static_cast<LONG>(oldWord));
        if (static_cast<uint32_t>(observed) == oldWord) {
            InterlockedExchange(&g_appliedRouteWord, static_cast<LONG>(newWord));
            InterlockedExchange(&g_routeCallbackBaseline, AtomicLoadLong(&g_callbackCount));
            return newWord;
        }
    }
    /* Window callbacks and loader-lock stop requests must remain bounded. A final atomic neutral
     * publication wins; the single Present producer reconciles any older in-flight decision. */
    const uint32_t oldWord = AtomicLoadRoute(&g_publishedRouteWord);
    const uint32_t neutralWord = PackSpeedHackPublishedRoute(
        neutral, NextRouteGeneration(oldWord), false);
    InterlockedExchange(&g_publishedRouteWord, static_cast<LONG>(neutralWord));
    InterlockedExchange(&g_appliedRouteWord, static_cast<LONG>(neutralWord));
    InterlockedExchange(&g_routeCallbackBaseline, AtomicLoadLong(&g_callbackCount));
    return neutralWord;
}

static bool ReadNativeState(uint32_t* valueOut) noexcept {
    if (!valueOut || !g_nativeStateAddress || AtomicLoadLong(&g_nativeStateReady) == 0) {
        return false;
    }
    *valueOut = static_cast<uint32_t>(InterlockedCompareExchange(
        g_nativeStateAddress, 0, 0));
    return true;
}

static bool ReadNativeAvailability(bool* availableOut) noexcept {
    if (!availableOut || !g_nativeAvailabilityAddress ||
        AtomicLoadLong(&g_nativeAvailabilityReady) == 0) {
        return false;
    }
    *availableOut = *g_nativeAvailabilityAddress != 0;
    return true;
}

static bool GlobalTargetStillOwned() noexcept {
    if (AtomicLoadLong(&g_globalTargetOwned) == 0 || !g_globalTargetAddress) return false;
    bool matches = false;
    __try {
        matches = std::memcmp(
            reinterpret_cast<const void*>(g_globalTargetAddress),
            g_ownedGlobalTargetBytes,
            sizeof(g_ownedGlobalTargetBytes)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        matches = false;
    }
    if (!matches) InterlockedExchange(&g_globalTargetOwned, 0);
    return matches;
}

static SpeedHackCapabilities ReadCapabilities() noexcept {
    SpeedHackCapabilities capabilities{};
    capabilities.nativeStateReady = AtomicLoadLong(&g_nativeStateReady) != 0;
    capabilities.nativeAvailabilityReady =
        AtomicLoadLong(&g_nativeAvailabilityReady) != 0;
    capabilities.globalTickHookReady = AtomicLoadLong(&g_globalTickHookReady) != 0;
    capabilities.dialogBypassReady = AtomicLoadLong(&g_dialogBypassReady) != 0;
    return capabilities;
}

static void DropNativeOwnershipMetadata() noexcept {
    InterlockedExchange(&g_nativeOwned, 0);
    InterlockedExchange(&g_nativeLastWrite, 0);
}

static void LatchNativeDrift() noexcept {
    const LONG latched = NextSpeedHackNativeDriftLatch(false, true, false) ? 1 : 0;
    InterlockedExchange(&g_nativeDriftLatched, latched);
    InterlockedExchange(
        &g_conflictReason, static_cast<LONG>(SpeedHackConflictReason::NativeDrift));
}

/* The game resets SpeedBooster before marking the booster unavailable. Observed zero in that
 * state therefore releases our ownership without a write; every other mismatch is drift. */
static bool CompareRestoreNativeOwned() noexcept {
    /* Focus, stop, and stale-Present compensation may converge concurrently. Exactly one caller
     * consumes ownership; later neutralizers then observe an already-idempotent restore. */
    if (InterlockedCompareExchange(&g_nativeOwned, 0, 1) != 1) return true;
    const LONG expected = InterlockedExchange(&g_nativeLastWrite, 0);
    if (!g_nativeStateAddress || (expected != 1 && expected != 2)) {
        LatchNativeDrift();
        return false;
    }

    const LONG observed = InterlockedCompareExchange(g_nativeStateAddress, 0, expected);
    bool nativeAvailable = true;
    (void)ReadNativeAvailability(&nativeAvailable);
    const bool restored = observed == expected;
    const bool engineResetWhileUnavailable = observed == 0 && !nativeAvailable;
    if (restored || engineResetWhileUnavailable) return true;
    LatchNativeDrift();
    return false;
}

static bool TryClearNativeDriftLatchAtSafe1x(
    const SpeedHackArbitrationInput& input,
    const SpeedHackArbitration& decision,
    LONG producerEpoch) noexcept {
    if (!SpeedHackCanResetNativeDriftLatch(input, decision) ||
        !ProducerEpochStillCurrent(producerEpoch)) {
        return false;
    }
    const LONG resetValue = NextSpeedHackNativeDriftLatch(true, false, true) ? 1 : 0;
    if (InterlockedCompareExchange(&g_nativeDriftLatched, resetValue, 1) != 1) return false;
    /* A focus/stop event racing the reset must keep the ownership failure sticky; otherwise a
     * later 2x/4x request could re-adopt native state after the neutral epoch already changed. */
    if (!ProducerEpochStillCurrent(producerEpoch)) {
        InterlockedExchange(&g_nativeDriftLatched, 1);
        return false;
    }
    return true;
}

static SpeedHackArbitration MakeConflictDecision(
    uint8_t requestedFactor,
    SpeedHackConflictReason conflict) noexcept {
    SpeedHackArbitration decision{};
    decision.route.requestedFactor = requestedFactor;
    decision.route.unavailable = true;
    decision.phase = SpeedHackRuntimePhase::Conflict;
    decision.conflict = conflict;
    return decision;
}

enum class SpeedHackNativeExecution : uint8_t {
    Applied = 0,
    Conflict,
    EpochChanged,
};

static SpeedHackNativeExecution ExecuteNativeAction(const SpeedHackArbitration& decision, LONG producerEpoch) {
    if (decision.nativeAction == SpeedHackNativeAction::None) {
        return ProducerEpochStillCurrent(producerEpoch)
            ? SpeedHackNativeExecution::Applied
            : SpeedHackNativeExecution::EpochChanged;
    }
    if (decision.nativeAction == SpeedHackNativeAction::DropAfterNativeUnavailable) {
        DropNativeOwnershipMetadata();
        return ProducerEpochStillCurrent(producerEpoch)
            ? SpeedHackNativeExecution::Applied
            : SpeedHackNativeExecution::EpochChanged;
    }
    if (!g_nativeStateAddress) return SpeedHackNativeExecution::Conflict;

    /* Focus and stop increment the epoch before neutralization. Checking on both sides of the
     * compare-write closes the only interval in which a stale Present frame can reclaim state. */
    if (!ProducerEpochStillCurrent(producerEpoch)) {
        return SpeedHackNativeExecution::EpochChanged;
    }
    const LONG observed = InterlockedCompareExchange(
        g_nativeStateAddress,
        static_cast<LONG>(decision.desiredNativeState),
        static_cast<LONG>(decision.expectedNativeState));
    if (static_cast<uint32_t>(observed) != decision.expectedNativeState) {
        DropNativeOwnershipMetadata();
        return SpeedHackNativeExecution::Conflict;
    }
    if (decision.desiredNativeState == 0) {
        DropNativeOwnershipMetadata();
    } else {
        InterlockedExchange(&g_nativeLastWrite, static_cast<LONG>(decision.desiredNativeState));
        InterlockedExchange(&g_nativeOwned, 1);
    }
    if (!ProducerEpochStillCurrent(producerEpoch)) {
        if (decision.desiredNativeState != 0 && !CompareRestoreNativeOwned()) {
            return SpeedHackNativeExecution::Conflict;
        }
        return SpeedHackNativeExecution::EpochChanged;
    }
    return SpeedHackNativeExecution::Applied;
}

#if defined(_MSC_VER) && defined(_M_IX86)
/* FFX_Scene_Field_ServiceTick @ preferred VA 0x820C00 receives float delta in [esp+4].
 * pushfd+pushad move it to [esp+40] and preserve flags/GPRs. Only a published custom-8 route
 * requires native zero; native 2x/4x passes through without being misreported as drift. The
 * callback increments its fixed-size count last so readers cannot see stale telemetry. */
__declspec(naked) void SpeedHackGlobalTickBridge() {
    __asm {
        pushfd
        pushad
        cmp dword ptr [g_stopRequested], 0
        jne passthrough
        mov eax, dword ptr [g_publishedRouteWord]
        mov edx, eax
        and edx, 3
        cmp edx, 3
        jne passthrough
        mov edx, dword ptr [g_nativeStateAddress]
        test edx, edx
        jz native_conflict
        cmp dword ptr [edx], 0
        jne native_conflict
        mov ecx, dword ptr [esp + 40]
        mov dword ptr [g_callbackInputDeltaBits], ecx
        fld dword ptr [esp + 40]
        fmul dword ptr [g_globalScale8]
        fstp dword ptr [esp + 40]
        mov ecx, dword ptr [esp + 40]
        mov dword ptr [g_callbackOutputDeltaBits], ecx
        mov dword ptr [g_appliedRouteWord], eax
        lock inc dword ptr [g_callbackCount]
        jmp passthrough
    native_conflict:
        mov dword ptr [g_callbackNativeConflict], 1
    passthrough:
        popad
        popfd
        jmp dword ptr [g_globalTickTrampoline]
    }
}
#else
static void SpeedHackGlobalTickBridge() {}
#endif

static void ResetRuntimeState() noexcept {
    SpeedHackRoute neutral{};
    const uint32_t neutralWord = PackSpeedHackPublishedRoute(neutral, 0, false);
    InterlockedExchange(&g_publishedRouteWord, static_cast<LONG>(neutralWord));
    InterlockedExchange(&g_appliedRouteWord, static_cast<LONG>(neutralWord));
    InterlockedExchange(&g_routeCallbackBaseline, AtomicLoadLong(&g_callbackCount));
    InterlockedExchange(&g_callbackInputDeltaBits, 0);
    InterlockedExchange(&g_callbackOutputDeltaBits, 0);
    InterlockedExchange(&g_callbackNativeConflict, 0);
    InterlockedExchange(&g_nativeOwned, 0);
    InterlockedExchange(&g_nativeLastWrite, 0);
    InterlockedExchange(&g_foregroundLost, 0);
    InterlockedExchange(&g_neutralEpoch, 0);
    InterlockedExchange(&g_presentProducerActive, 0);
    InterlockedExchange(&g_runtimePhase, static_cast<LONG>(SpeedHackRuntimePhase::Idle));
    InterlockedExchange(&g_conflictReason, static_cast<LONG>(SpeedHackConflictReason::None));
    InterlockedExchange(&g_nativeDriftLatched, 0);
    g_control = {};
    g_lastAppliedLogWord = UINT32_MAX;
    g_lastConflictLog = SpeedHackConflictReason::None;
}

static void NeutralizeAfterProducerEpochLoss() noexcept {
    const bool restored = CompareRestoreNativeOwned();
    PublishNeutralRouteAsync();
    DialogSkipNotifySpeedHack8Inactive();
    const bool nativeDrift = !restored || AtomicLoadLong(&g_nativeDriftLatched) != 0;
    InterlockedExchange(
        &g_runtimePhase,
        static_cast<LONG>(nativeDrift
            ? SpeedHackRuntimePhase::Conflict
            : AtomicLoadLong(&g_stopRequested) != 0
                ? SpeedHackRuntimePhase::Unavailable
                : SpeedHackRuntimePhase::Idle));
    InterlockedExchange(
        &g_conflictReason,
        static_cast<LONG>(nativeDrift
            ? SpeedHackConflictReason::NativeDrift
            : SpeedHackConflictReason::None));
}

static bool PublishDecision(
    const SpeedHackArbitration& decision,
    LONG producerEpoch,
    uint32_t* currentWordInOut) {
    if (!currentWordInOut) return false;
    uint32_t currentWord = *currentWordInOut;
    const SpeedHackPublishedRoute current = UnpackSpeedHackPublishedRoute(currentWord);
    const bool desiredGlobal = decision.route.backend == SpeedHackBackend::FastFieldScenes &&
        decision.route.routedFactor == 8 && !decision.route.unavailable;
    const bool routeChanged = !PublishedRouteMatches(current, decision);
    const bool epochCurrentBeforeSideEffects = ProducerEpochStillCurrent(producerEpoch);
    if (ResolveSpeedHackPublishEpochDisposition(
            epochCurrentBeforeSideEffects, epochCurrentBeforeSideEffects) ==
        SpeedHackPublishEpochDisposition::RejectBeforeSideEffects) {
        return false;
    }

    if (routeChanged) {
        if (desiredGlobal) {
            uint32_t nativeState = UINT32_MAX;
            if (!ReadNativeState(&nativeState) || nativeState != 0) return false;
            DialogSkipFrameTick(true);
            if (!ProducerEpochStillCurrent(producerEpoch)) {
                NeutralizeAfterProducerEpochLoss();
                return false;
            }
        }

        const uint32_t newWord = PackSpeedHackPublishedRoute(
            decision.route,
            NextRouteGeneration(currentWord),
            decision.phase == SpeedHackRuntimePhase::Paused);
        InterlockedExchange(&g_routeCallbackBaseline, AtomicLoadLong(&g_callbackCount));
        const LONG observed = InterlockedCompareExchange(
            &g_publishedRouteWord,
            static_cast<LONG>(newWord),
            static_cast<LONG>(currentWord));
        if (static_cast<uint32_t>(observed) != currentWord) {
            if (desiredGlobal) DialogSkipNotifySpeedHack8Inactive();
            *currentWordInOut = static_cast<uint32_t>(observed);
            return false;
        }
        currentWord = newWord;
        *currentWordInOut = newWord;
        if (!desiredGlobal) {
            /* Neutral/native publication closes global multiplication before the separately
             * owned voice request is disabled. */
            DialogSkipFrameTick(false);
        }
        if (decision.route.routedFactor == 1) {
            InterlockedExchange(&g_appliedRouteWord, static_cast<LONG>(newWord));
        } else {
            InterlockedExchange(&g_appliedRouteWord, 0);
        }
    } else {
        /* Even an unchanged 8x route is a new side effect attempt: focus may have disabled the
         * composed voice request after Present's last epoch sample. */
        DialogSkipFrameTick(desiredGlobal);
    }

    InterlockedExchange(&g_runtimePhase, static_cast<LONG>(decision.phase));
    InterlockedExchange(&g_conflictReason, static_cast<LONG>(decision.conflict));
    const SpeedHackPublishEpochDisposition disposition =
        ResolveSpeedHackPublishEpochDisposition(
            epochCurrentBeforeSideEffects,
            ProducerEpochStillCurrent(producerEpoch));
    if (disposition != SpeedHackPublishEpochDisposition::Commit) {
        NeutralizeAfterProducerEpochLoss();
        *currentWordInOut = AtomicLoadRoute(&g_publishedRouteWord);
        return false;
    }
    if (routeChanged) {
        HookLog(
            "[ffx-hooks] SpeedHack: Ctrl+Shift+K requested=%ux routed=%ux backend=%s\n",
            static_cast<unsigned>(decision.route.requestedFactor),
            static_cast<unsigned>(decision.route.routedFactor),
            SpeedHackBackendName(decision.route.backend));
    }
    return true;
}

} // namespace

bool InstallSpeedHackHook(uintptr_t moduleBase, bool dialogBypassReady, void* logFn, SpeedHackInstallStatus* statusOut) {
    g_logFn = reinterpret_cast<void (*)(const char*)>(logFn);
    if (statusOut) *statusOut = SpeedHackInstallStatus::PolyHookUnavailable;
    SpeedHackTargetValidation globalValidation{};
    const bool unxLoaded = IsUnXModuleLoaded();
    if (g_terminalInstallFailure) {
        return FinishSpeedHackInstall(
            g_terminalInstallStatus, globalValidation, unxLoaded, statusOut, false);
    }
    if (g_installed) {
        return FinishSpeedHackInstall(
            SpeedHackInstallStatus::AlreadyInstalled,
            globalValidation, unxLoaded, statusOut, true);
    }

#ifdef FFXHOOKS_HAVE_POLYHOOK
    constexpr size_t kImageHeaderProbeBytes = 0x1000u;
    F8Runtime::ExecutableIdentity identity{};
    const F8Runtime::ProfileResult profile = F8Runtime::ParseExecutableIdentity(
        reinterpret_cast<const uint8_t*>(moduleBase), kImageHeaderProbeBytes, &identity);
    if (profile != F8Runtime::ProfileResult::Supported ||
        !F8Runtime::IsSupportedExecutable(identity)) {
        return FinishSpeedHackInstall(
            SpeedHackInstallStatus::UnsupportedProfile,
            globalValidation, unxLoaded, statusOut, false);
    }
    if (F8Runtime::ValidateImageRange(
            RVA_FFX_NATIVE_SPEED_BOOSTER, sizeof(uint32_t),
            identity.sizeOfImage) != F8Runtime::ProfileResult::Supported) {
        return FinishSpeedHackInstall(
            SpeedHackInstallStatus::NativeStateOutOfRange,
            globalValidation, unxLoaded, statusOut, false);
    }
    if (F8Runtime::ValidateImageRange(
            RVA_FFX_NATIVE_SPEED_BOOSTER_AVAILABILITY, sizeof(uint8_t),
            identity.sizeOfImage) != F8Runtime::ProfileResult::Supported) {
        return FinishSpeedHackInstall(
            SpeedHackInstallStatus::NativeAvailabilityOutOfRange,
            globalValidation, unxLoaded, statusOut, false);
    }
    if (F8Runtime::ValidateImageRange(
            RVA_FFX_SCENE_FIELD_SERVICE_TICK, kSpeedHackGlobalTargetLength,
            identity.sizeOfImage) != F8Runtime::ProfileResult::Supported) {
        return FinishSpeedHackInstall(
            SpeedHackInstallStatus::GlobalTargetOutOfRange,
            globalValidation, unxLoaded, statusOut, false);
    }

    const uint8_t* const globalTarget = reinterpret_cast<const uint8_t*>(
        moduleBase + RVA_FFX_SCENE_FIELD_SERVICE_TICK);
    globalValidation = ValidateSpeedHackGlobalTargetSignature(
        globalTarget, kSpeedHackGlobalTargetLength, moduleBase);
    if (globalValidation.status == SpeedHackTargetStatus::BaseOverflow) {
        return FinishSpeedHackInstall(
            SpeedHackInstallStatus::GlobalTargetOutOfRange,
            globalValidation, unxLoaded, statusOut, false);
    }
    if (globalValidation.status == SpeedHackTargetStatus::DetourLikePrefix) {
        return FinishSpeedHackInstall(
            SpeedHackInstallStatus::GlobalDetourLikePrefix,
            globalValidation, unxLoaded, statusOut, false);
    }
    if (globalValidation.status != SpeedHackTargetStatus::Match) {
        return FinishSpeedHackInstall(
            SpeedHackInstallStatus::GlobalSignatureMismatch,
            globalValidation, unxLoaded, statusOut, false);
    }
    if (unxLoaded) {
        return FinishSpeedHackInstall(
            SpeedHackInstallStatus::UnXModuleConflict,
            globalValidation, true, statusOut, false);
    }

    g_nativeStateAddress = reinterpret_cast<volatile LONG*>(
        moduleBase + RVA_FFX_NATIVE_SPEED_BOOSTER);
    g_nativeAvailabilityAddress = reinterpret_cast<const volatile uint8_t*>(
        moduleBase + RVA_FFX_NATIVE_SPEED_BOOSTER_AVAILABILITY);
    g_globalTargetAddress = moduleBase + RVA_FFX_SCENE_FIELD_SERVICE_TICK;

    bool globalHooked = false;
    try {
        g_globalTickDetour = new PLH::x86Detour(
            static_cast<uint64_t>(g_globalTargetAddress),
            reinterpret_cast<uint64_t>(&SpeedHackGlobalTickBridge),
            &g_globalTickTrampoline);
        globalHooked = static_cast<PLH::x86Detour*>(g_globalTickDetour)->hook();
    } catch (...) {
        globalHooked = false;
    }
    if (!g_globalTickDetour || !globalHooked || g_globalTickTrampoline == 0) {
        ResetRuntimeState();
        InterlockedExchange(&g_stopRequested, 1);
        g_terminalInstallFailure = true;
        g_terminalInstallStatus = SpeedHackInstallStatus::GlobalDetourFailed;
        return FinishSpeedHackInstall(
            SpeedHackInstallStatus::GlobalDetourFailed,
            globalValidation, unxLoaded, statusOut, false);
    }

    /* Capture the complete post-hook target span. A future UnX/external rewrite cannot be
     * decoded or chained safely, so any byte drift permanently removes our route authority. */
    std::memcpy(
        g_ownedGlobalTargetBytes,
        reinterpret_cast<const void*>(g_globalTargetAddress),
        sizeof(g_ownedGlobalTargetBytes));
    ResetRuntimeState();
    InterlockedExchange(&g_stopRequested, 0);
    InterlockedExchange(&g_nativeStateReady, 1);
    InterlockedExchange(&g_nativeAvailabilityReady, 1);
    InterlockedExchange(&g_dialogBypassReady, dialogBypassReady ? 1 : 0);
    InterlockedExchange(&g_globalTickHookReady, 1);
    InterlockedExchange(&g_globalTargetOwned, 1);
    g_installed = true;
    g_terminalInstallFailure = false;
    g_terminalInstallStatus = SpeedHackInstallStatus::PolyHookUnavailable;
    return FinishSpeedHackInstall(
        SpeedHackInstallStatus::Installed,
        globalValidation, false, statusOut, true);
#else
    (void)moduleBase;
    (void)dialogBypassReady;
    return FinishSpeedHackInstall(
        SpeedHackInstallStatus::PolyHookUnavailable,
        globalValidation, unxLoaded, statusOut, false);
#endif
}

void SpeedHackFrameTick() {
    if (InterlockedCompareExchange(&g_presentProducerActive, 1, 0) != 0) return;

    if (AtomicLoadLong(&g_stopRequested) != 0) {
        DialogSkipFrameTick(false);
        InterlockedExchange(&g_presentProducerActive, 0);
        return;
    }

    const LONG producerEpoch = AtomicLoadLong(&g_neutralEpoch);
    const auto movie=g_movieBridge.load();
    const unsigned movieEpoch=movie&&movie->epoch?movie->epoch():0;
    const bool foregroundLossPending = InterlockedExchange(&g_foregroundLost, 0) != 0;
    const bool foreground = IsFfxForeground();
    // The native controller remains authoritative. The F8-configured reader
    // supplies an already matched keyboard/controller chord; the old chord is
    // the fallback when the settings adapter is not present.
    const auto reader=g_shortcutReader.load();
    const SpeedHackInputSample input = {
        Config::GetBool("boosters.speed_hack", false),
        foreground,
        reader?true:(GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0,
        reader?true:(GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0,
        reader?false:(GetAsyncKeyState(VK_MENU) & 0x8000) != 0,
        reader?reader():(GetAsyncKeyState('K') & 0x8000) != 0,
    };
    const SpeedHackTransition transition = AdvanceSpeedHackControlAfterForegroundEvent(
        &g_control, input, foregroundLossPending);

    bool nativeAvailable = false;
    uint32_t nativeState = UINT32_MAX;
    const bool availabilityReadable = ReadNativeAvailability(&nativeAvailable);
    const bool nativeReadable = ReadNativeState(&nativeState);
    const bool unxLoaded = IsUnXModuleLoaded();
    const bool targetOwned = GlobalTargetStillOwned();
    if ((unxLoaded || !targetOwned) && AtomicLoadLong(&g_nativeOwned) != 0) {
        (void)CompareRestoreNativeOwned();
        (void)ReadNativeState(&nativeState);
    }

    SpeedHackArbitrationInput arbitrationInput{};
    arbitrationInput.requestedFactor = g_control.factor;
    arbitrationInput.maxSpeed = Config::GetFloat("speed_hack.max_speed", 8.0f);
    arbitrationInput.nativeStateReady = nativeReadable;
    arbitrationInput.nativeAvailabilityReady = availabilityReadable;
    arbitrationInput.globalTickHookReady = AtomicLoadLong(&g_globalTickHookReady) != 0;
    arbitrationInput.dialogBypassReady = AtomicLoadLong(&g_dialogBypassReady) != 0;
    arbitrationInput.globalTargetOwned = targetOwned;
    arbitrationInput.unxModuleLoaded = unxLoaded;
    arbitrationInput.nativeAvailable = nativeAvailable;
    arbitrationInput.observedNativeState = nativeState;
    arbitrationInput.nativeOwned = AtomicLoadLong(&g_nativeOwned) != 0;
    arbitrationInput.lastWrittenNativeState =
        static_cast<uint32_t>(AtomicLoadLong(&g_nativeLastWrite));
    arbitrationInput.nativeDriftLatched = AtomicLoadLong(&g_nativeDriftLatched) != 0;
    arbitrationInput.movieOwnsRate=movie&&movie->ownsRate&&movie->ownsRate();

    SpeedHackArbitration decision = ResolveSpeedHackArbitration(arbitrationInput);
    if (TryClearNativeDriftLatchAtSafe1x(arbitrationInput, decision, producerEpoch)) {
        arbitrationInput.nativeDriftLatched = false;
    }
    uint32_t currentWord = AtomicLoadRoute(&g_publishedRouteWord);
    const bool currentGlobal = SpeedHackGlobalFactorFromPublishedRoute(currentWord) == 8;
    const bool desiredNativeAction =
        decision.nativeAction == SpeedHackNativeAction::ClaimState1 ||
        decision.nativeAction == SpeedHackNativeAction::ClaimState2 ||
        decision.nativeAction == SpeedHackNativeAction::ChangeToState1 ||
        decision.nativeAction == SpeedHackNativeAction::ChangeToState2;

    if (currentGlobal && desiredNativeAction) {
        /* Global-to-native transitions publish neutral and close voice ownership before the
         * first native compare-write, so one frame can never see both multipliers. */
        SpeedHackArbitration neutral{};
        neutral.route.requestedFactor = decision.route.requestedFactor;
        neutral.phase = SpeedHackRuntimePhase::Arming;
        if (!PublishDecision(neutral, producerEpoch, &currentWord)) {
            InterlockedExchange(&g_presentProducerActive, 0);
            return;
        }
    }

    if (decision.nativeAction != SpeedHackNativeAction::None) {
        const bool wasOwned = arbitrationInput.nativeOwned;
        const SpeedHackNativeExecution execution = ExecuteNativeAction(decision, producerEpoch);
        if (execution == SpeedHackNativeExecution::Conflict) {
            decision = MakeConflictDecision(
                g_control.factor,
                wasOwned ? SpeedHackConflictReason::NativeDrift
                         : SpeedHackConflictReason::NativeBusy);
        } else if (execution == SpeedHackNativeExecution::EpochChanged) {
            InterlockedExchange(&g_presentProducerActive, 0);
            return;
        } else {
            (void)ReadNativeAvailability(&arbitrationInput.nativeAvailable);
            (void)ReadNativeState(&arbitrationInput.observedNativeState);
            arbitrationInput.nativeOwned = AtomicLoadLong(&g_nativeOwned) != 0;
            arbitrationInput.lastWrittenNativeState =
                static_cast<uint32_t>(AtomicLoadLong(&g_nativeLastWrite));
            decision = ResolveSpeedHackArbitration(arbitrationInput);
        }
    }

    const bool callbackNativeConflict =
        InterlockedExchange(&g_callbackNativeConflict, 0) != 0;
    if (SpeedHackShouldReportBridgeNativeConflict(currentWord, callbackNativeConflict) &&
        decision.phase != SpeedHackRuntimePhase::Conflict) {
        decision = MakeConflictDecision(
            g_control.factor,
            arbitrationInput.nativeOwned
                ? SpeedHackConflictReason::NativeDrift
                : SpeedHackConflictReason::NativeBusy);
    }

    if (decision.conflict == SpeedHackConflictReason::NativeDrift) {
        /* The observed value is now externally owned. Forget only our ownership record; writing
         * or re-adopting it in this frame would erase the evidence that caused the conflict. */
        LatchNativeDrift();
        DropNativeOwnershipMetadata();
    }

    if (!ProducerEpochStillCurrent(producerEpoch)) {
        (void)CompareRestoreNativeOwned();
        InterlockedExchange(&g_presentProducerActive, 0);
        return;
    }
    if (!PublishDecision(decision, producerEpoch, &currentWord)) {
        InterlockedExchange(&g_presentProducerActive, 0);
        return;
    }

    if(movie && movie->publish)movie->publish(ClampSpeedHackFactor(g_control.factor,arbitrationInput.maxSpeed),
        arbitrationInput.movieOwnsRate && input.gateOn && input.foreground &&
        decision.conflict==SpeedHackConflictReason::None && !arbitrationInput.nativeDriftLatched &&
        arbitrationInput.observedNativeState==0 && ProducerEpochStillCurrent(producerEpoch),movieEpoch);

    if (transition == SpeedHackTransition::Restored1x) {
        const char* reason = !input.gateOn
            ? "gate_off"
            : (!input.foreground || foregroundLossPending)
                ? "foreground_lost"
                : "shortcut_cycle";
        HookLog("[ffx-hooks] SpeedHack: restored 1x reason=%s\n", reason);
    }

    if (decision.phase == SpeedHackRuntimePhase::Conflict &&
        decision.conflict != g_lastConflictLog) {
        g_lastConflictLog = decision.conflict;
        HookLog(
            "[ffx-hooks] SpeedHack: conflict reason=%s requested=%ux native=0x%08X\n",
            SpeedHackConflictName(decision.conflict),
            static_cast<unsigned>(decision.route.requestedFactor),
            static_cast<unsigned>(nativeState));
    } else if (decision.phase != SpeedHackRuntimePhase::Conflict) {
        g_lastConflictLog = SpeedHackConflictReason::None;
    }

    const uint32_t appliedWord = AtomicLoadRoute(&g_appliedRouteWord);
    const uint32_t callbackCount = AtomicLoadRoute(&g_callbackCount);
    const uint32_t callbackBaseline = AtomicLoadRoute(&g_routeCallbackBaseline);
    if (SpeedHackShouldLogAppliedGeneration(
            currentWord, g_lastAppliedLogWord, appliedWord,
            callbackBaseline, callbackCount)) {
        g_lastAppliedLogWord = currentWord;
        HookLog(
            "[ffx-hooks] SpeedHack: applied factor=%ux backend=%s callbacks=%u\n",
            8u,
            SpeedHackBackendName(SpeedHackBackend::FastFieldScenes),
            static_cast<unsigned>(callbackCount));
    } else if (SpeedHackGlobalFactorFromPublishedRoute(currentWord) != 8) {
        g_lastAppliedLogWord = UINT32_MAX;
    }

    InterlockedExchange(&g_presentProducerActive, 0);
}

SpeedHackRuntimeSnapshot GetSpeedHackRuntimeSnapshot() noexcept {
    SpeedHackRuntimeSnapshot snapshot{};
    const SpeedHackCapabilities capabilities = ReadCapabilities();
    snapshot.nativeStateReady = capabilities.nativeStateReady;
    snapshot.nativeAvailabilityReady = capabilities.nativeAvailabilityReady;
    snapshot.globalTickHookReady = capabilities.globalTickHookReady;
    snapshot.dialogBypassReady = capabilities.dialogBypassReady;

    const uint32_t routeWord = AtomicLoadRoute(&g_publishedRouteWord);
    const uint32_t appliedWord = AtomicLoadRoute(&g_appliedRouteWord);
    const uint32_t callbackBaseline = AtomicLoadRoute(&g_routeCallbackBaseline);
    const SpeedHackPublishedRoute route = UnpackSpeedHackPublishedRoute(routeWord);
    snapshot.requestedFactor = route.requestedFactor;
    snapshot.routedFactor = route.routedFactor;
    snapshot.backend = route.backend;
    snapshot.callbackCount = AtomicLoadRoute(&g_callbackCount);
    snapshot.inputDeltaBits = AtomicLoadRoute(&g_callbackInputDeltaBits);
    snapshot.outputDeltaBits = AtomicLoadRoute(&g_callbackOutputDeltaBits);
    snapshot.globalTargetOwned = AtomicLoadLong(&g_globalTargetOwned) != 0;
    snapshot.unxModuleLoaded = IsUnXModuleLoaded();
    (void)ReadNativeState(&snapshot.nativeState);
    (void)ReadNativeAvailability(&snapshot.nativeAvailable);
    snapshot.phase = static_cast<SpeedHackRuntimePhase>(AtomicLoadLong(&g_runtimePhase));
    snapshot.conflict = static_cast<SpeedHackConflictReason>(AtomicLoadLong(&g_conflictReason));
    snapshot.appliedFactor = 1;

    if (snapshot.phase == SpeedHackRuntimePhase::Armed &&
        SpeedHackCallbackTelemetryMatches(
            routeWord, appliedWord, callbackBaseline, snapshot.callbackCount)) {
        snapshot.phase = SpeedHackRuntimePhase::Applied;
        snapshot.appliedFactor = 8;
    }
    if (AtomicLoadLong(&g_stopRequested) != 0 &&
        snapshot.phase != SpeedHackRuntimePhase::Conflict) {
        snapshot.phase = SpeedHackRuntimePhase::Unavailable;
    }
    if(AtomicLoadLong(&g_stopRequested)==0)if(const auto movie=g_movieBridge.load())if(movie->decorate)movie->decorate(&snapshot);
    return snapshot;
}

void SpeedHackNotifyForegroundLost() noexcept {
    InterlockedExchange(&g_foregroundLost, 1);
    InterlockedIncrement(&g_neutralEpoch);
    if(const auto movie=g_movieBridge.load())if(movie->neutralize)movie->neutralize();
    const bool restored = CompareRestoreNativeOwned();
    PublishNeutralRouteAsync();
    DialogSkipNotifySpeedHack8Inactive();
    const bool nativeDrift = !restored || AtomicLoadLong(&g_nativeDriftLatched) != 0;
    InterlockedExchange(
        &g_runtimePhase,
        static_cast<LONG>(nativeDrift
            ? SpeedHackRuntimePhase::Conflict
            : SpeedHackRuntimePhase::Idle));
    InterlockedExchange(
        &g_conflictReason,
        static_cast<LONG>(nativeDrift
            ? SpeedHackConflictReason::NativeDrift
            : SpeedHackConflictReason::None));
}

void RequestSpeedHackStop() noexcept {
    InterlockedExchange(&g_stopRequested, 1);
    InterlockedIncrement(&g_neutralEpoch);
    const bool restored = CompareRestoreNativeOwned();
    PublishNeutralRouteAsync();
    DialogSkipNotifySpeedHack8Inactive();
    const bool nativeDrift = !restored || AtomicLoadLong(&g_nativeDriftLatched) != 0;
    InterlockedExchange(
        &g_runtimePhase,
        static_cast<LONG>(nativeDrift
            ? SpeedHackRuntimePhase::Conflict
            : SpeedHackRuntimePhase::Unavailable));
    InterlockedExchange(
        &g_conflictReason,
        static_cast<LONG>(nativeDrift
            ? SpeedHackConflictReason::NativeDrift
            : SpeedHackConflictReason::None));
}

void RemoveSpeedHackHook() {
    RequestSpeedHackStop();
    if(const auto movie=g_movieBridge.load())if(movie->neutralize)movie->neutralize();
    InterlockedExchange(&g_nativeStateReady, 0);
    InterlockedExchange(&g_nativeAvailabilityReady, 0);
    InterlockedExchange(&g_globalTickHookReady, 0);
    InterlockedExchange(&g_dialogBypassReady, 0);
    /* The patched target can execute concurrently with teardown. Admission is closed and both
     * routes are neutralized above, while detour, trampoline, and callback-address storage stay
     * valid for process lifetime. Dynamic DLL unload is intentionally unsupported. */
}

void SpeedHackSetShortcutReader(SpeedHackShortcutReader reader) noexcept {g_shortcutReader=reader;}
void SpeedHackSetMovieBridge(const SpeedHackMovieBridge* bridge) noexcept {g_movieBridge=bridge;}
} // namespace FfxHooks
