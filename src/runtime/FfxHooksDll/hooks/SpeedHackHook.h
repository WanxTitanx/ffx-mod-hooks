#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

/* SpeedHackHook.h — F8 booster "Speed Hack".
 *
 * FFX's native SpeedBooster owns 2x/4x. The clean-room fast-field-scene route owns only 8x: it
 * multiplies the field-scene service delta and composes DialogSkip's voice-event bypass for scene
 * and dialog acceleration. The optional movie bridge owns FMV frame/audio acceleration separately.
 * Native and clean-room ownership are mutually exclusive, and focus/stop epoch
 * changes neutralize both routes before stale Present work may publish. The fallback shortcut is
 * Ctrl+Shift+K (Alt rejected); F8 may supply a user-configured keyboard/gamepad reader. */
namespace FfxHooks {

enum class SpeedHackTransition : uint8_t { None = 0, FactorChanged, Restored1x };

struct SpeedHackControlState {
    bool keyWasDown = false;
    uint8_t factor = 1;
};

struct SpeedHackInputSample {
    // Present samples Ctrl+Shift+K only. F12 belongs to other UI paths and must never enter this
    // control state, including as an alias or fallback trigger.
    bool gateOn;
    bool foreground;
    bool ctrlDown;
    bool shiftDown;
    bool altDown;
    bool keyDown;
};

enum class SpeedHackBackend : uint8_t {
    None = 0,
    NativeStandard,
    FastFieldScenes,
    Movie, // observation only; never published into the scene-clock route word
};

enum class SpeedHackRuntimePhase : uint8_t {
    Idle = 0,
    Arming,
    Armed,
    Applied,
    Paused,
    Conflict,
    Unavailable,
};

enum class SpeedHackConflictReason : uint8_t {
    None = 0,
    NativeBusy,
    NativeDrift,
    UnXModuleLoaded,
    ExternalTargetDrift,
    UnXTargetDrift,
};

struct SpeedHackCapabilities {
    bool nativeStateReady = false;
    bool nativeAvailabilityReady = false;
    bool globalTickHookReady = false;
    bool dialogBypassReady = false;
};

struct SpeedHackRoute {
    uint8_t requestedFactor = 1;
    uint8_t routedFactor = 1;
    SpeedHackBackend backend = SpeedHackBackend::None;
    bool unavailable = false;
    bool paused = false;
};

struct SpeedHackPublishedRoute {
    uint8_t requestedFactor = 1;
    uint8_t routedFactor = 1;
    SpeedHackBackend backend = SpeedHackBackend::None;
    bool unavailable = false;
    bool paused = false;
    uint32_t generation = 0;
};

enum class SpeedHackIndicatorMode : uint8_t {
    Hidden = 0,
    Armed,
    Applied,
    Paused,
    Conflict,
    Unavailable,
};

struct SpeedHackRuntimeSnapshot {
    bool nativeStateReady = false;
    bool nativeAvailabilityReady = false;
    bool globalTickHookReady = false;
    bool dialogBypassReady = false;
    uint8_t requestedFactor = 1;
    uint8_t routedFactor = 1;
    uint8_t appliedFactor = 1;
    SpeedHackBackend backend = SpeedHackBackend::None;
    SpeedHackRuntimePhase phase = SpeedHackRuntimePhase::Unavailable;
    uint32_t callbackCount = 0;
    uint32_t inputDeltaBits = 0;
    uint32_t outputDeltaBits = 0;
    uint32_t nativeState = 0;
    bool nativeAvailable = false;
    bool globalTargetOwned = false;
    bool unxModuleLoaded = false;
    SpeedHackConflictReason conflict = SpeedHackConflictReason::None;
};
struct SpeedHackMovieBridge {
    bool(*ownsRate)()=nullptr;
    unsigned(*epoch)()=nullptr;
    void(*publish)(unsigned,bool,unsigned)=nullptr;
    void(*neutralize)()=nullptr;
    void(*decorate)(SpeedHackRuntimeSnapshot*)=nullptr;
};
void SpeedHackSetMovieBridge(const SpeedHackMovieBridge*) noexcept;

struct SpeedHackIndicatorState {
    SpeedHackIndicatorMode mode = SpeedHackIndicatorMode::Hidden;
    uint8_t factor = 1;
    SpeedHackBackend backend = SpeedHackBackend::None;
};

inline constexpr bool IsSupportedSpeedHackFactor(uint8_t factor) noexcept {
    return factor == 1 || factor == 2 || factor == 4 || factor == 8;
}

inline constexpr uint8_t NextSpeedHackFactor(uint8_t factor) noexcept {
    return factor == 1 ? 2 : factor == 2 ? 4 : factor == 4 ? 8 : 1;
}

inline constexpr uint8_t ClampSpeedHackFactor(
    uint8_t requestedFactor,
    float maxSpeed) noexcept {
    if (!IsSupportedSpeedHackFactor(requestedFactor) || !(maxSpeed > 1.0f)) return 1;
    uint8_t effectiveFactor = requestedFactor;
    while (effectiveFactor > 1 && static_cast<float>(effectiveFactor) > maxSpeed) {
        effectiveFactor = static_cast<uint8_t>(effectiveFactor / 2);
    }
    return effectiveFactor;
}

enum class SpeedHackNativeAction : uint8_t {
    None = 0,
    ClaimState1,
    ClaimState2,
    ChangeToState1,
    ChangeToState2,
    RestoreZero,
    DropAfterNativeUnavailable,
};

struct SpeedHackArbitrationInput {
    uint8_t requestedFactor = 1;
    float maxSpeed = 8.0f;
    bool nativeStateReady = false;
    bool nativeAvailabilityReady = false;
    bool globalTickHookReady = false;
    bool dialogBypassReady = false;
    bool globalTargetOwned = false;
    bool unxModuleLoaded = false;
    bool nativeAvailable = false;
    uint32_t observedNativeState = 0;
    bool nativeOwned = false;
    uint32_t lastWrittenNativeState = 0;
    bool nativeDriftLatched = false;
    bool movieOwnsRate = false;
};

struct SpeedHackArbitration {
    SpeedHackRoute route{};
    SpeedHackRuntimePhase phase = SpeedHackRuntimePhase::Unavailable;
    SpeedHackNativeAction nativeAction = SpeedHackNativeAction::None;
    SpeedHackConflictReason conflict = SpeedHackConflictReason::None;
    uint32_t expectedNativeState = 0;
    uint32_t desiredNativeState = 0;
};

inline constexpr SpeedHackArbitration ResolveSpeedHackArbitration(
    const SpeedHackArbitrationInput& input) noexcept {
    SpeedHackArbitration result{};
    result.route.requestedFactor = input.requestedFactor;
    if (!IsSupportedSpeedHackFactor(input.requestedFactor) ||
        !input.nativeStateReady || !input.nativeAvailabilityReady ||
        !input.globalTickHookReady) {
        result.route.unavailable = true;
        return result;
    }
    if (input.unxModuleLoaded) {
        result.route.unavailable = true;
        result.phase = SpeedHackRuntimePhase::Conflict;
        result.conflict = SpeedHackConflictReason::UnXModuleLoaded;
        return result;
    }
    if (!input.globalTargetOwned) {
        result.route.unavailable = true;
        result.phase = SpeedHackRuntimePhase::Conflict;
        result.conflict = SpeedHackConflictReason::ExternalTargetDrift;
        return result;
    }

    const uint8_t capped = input.movieOwnsRate?1:ClampSpeedHackFactor(input.requestedFactor, input.maxSpeed);
    if (input.nativeDriftLatched && capped != 1) {
        result.route.unavailable = true;
        result.phase = SpeedHackRuntimePhase::Conflict;
        result.conflict = SpeedHackConflictReason::NativeDrift;
        return result;
    }
    const bool nativeObservationMatches = input.nativeOwned &&
        input.observedNativeState == input.lastWrittenNativeState &&
        (input.lastWrittenNativeState == 1 || input.lastWrittenNativeState == 2);
    // The engine marks the native booster unavailable only after resetting its state. Retaining
    // our old nonzero value is drift, not evidence about the kind of scene currently running.
    if (input.nativeOwned && !input.nativeAvailable && input.observedNativeState != 0) {
        result.route.unavailable = true;
        result.phase = SpeedHackRuntimePhase::Conflict;
        result.conflict = SpeedHackConflictReason::NativeDrift;
        return result;
    }
    if (input.nativeOwned && !nativeObservationMatches &&
        !(input.observedNativeState == 0 && !input.nativeAvailable)) {
        result.route.unavailable = true;
        result.phase = SpeedHackRuntimePhase::Conflict;
        result.conflict = SpeedHackConflictReason::NativeDrift;
        return result;
    }
    if (!input.nativeOwned && input.observedNativeState != 0) {
        result.route.unavailable = true;
        result.phase = SpeedHackRuntimePhase::Conflict;
        result.conflict = SpeedHackConflictReason::NativeBusy;
        return result;
    }

    if (capped == 1) {
        result.phase = SpeedHackRuntimePhase::Idle;
        if (input.nativeOwned) {
            if (input.observedNativeState == 0 && !input.nativeAvailable) {
                result.nativeAction = SpeedHackNativeAction::DropAfterNativeUnavailable;
            } else {
                result.nativeAction = SpeedHackNativeAction::RestoreZero;
                result.expectedNativeState = input.lastWrittenNativeState;
            }
        }
        return result;
    }

    if (capped == 2 || capped == 4) {
        if (!input.nativeAvailable) {
            result.phase = SpeedHackRuntimePhase::Paused;
            result.route.paused = true;
            if (input.nativeOwned) {
                result.nativeAction = SpeedHackNativeAction::DropAfterNativeUnavailable;
            }
            return result;
        }

        const uint32_t desiredState = capped == 2 ? 1u : 2u;
        result.route.routedFactor = capped;
        result.route.backend = SpeedHackBackend::NativeStandard;
        result.phase = SpeedHackRuntimePhase::Arming;
        result.desiredNativeState = desiredState;
        if (!input.nativeOwned) {
            result.nativeAction = desiredState == 1
                ? SpeedHackNativeAction::ClaimState1
                : SpeedHackNativeAction::ClaimState2;
            return result;
        }
        result.expectedNativeState = input.lastWrittenNativeState;
        if (input.lastWrittenNativeState == desiredState) {
            result.nativeAction = SpeedHackNativeAction::None;
            result.phase = SpeedHackRuntimePhase::Armed;
        } else {
            result.nativeAction = desiredState == 1
                ? SpeedHackNativeAction::ChangeToState1
                : SpeedHackNativeAction::ChangeToState2;
        }
        return result;
    }

    if (!input.dialogBypassReady) {
        result.route.unavailable = true;
        return result;
    }
    result.route.routedFactor = 8;
    result.route.backend = SpeedHackBackend::FastFieldScenes;
    result.phase = SpeedHackRuntimePhase::Armed;
    if (input.nativeOwned) {
        result.nativeAction = SpeedHackNativeAction::RestoreZero;
        result.expectedNativeState = input.lastWrittenNativeState;
    }
    return result;
}

enum class SpeedHackPublishEpochDisposition : uint8_t {
    RejectBeforeSideEffects = 0,
    Commit,
    CompensateAfterSideEffects,
};

/* Focus and stop are asynchronous to Present. A side effect admitted by a current epoch is
 * committed only when a trailing sample is also current; otherwise its caller must neutralize. */
inline constexpr SpeedHackPublishEpochDisposition ResolveSpeedHackPublishEpochDisposition(
    bool epochCurrentBeforeSideEffects,
    bool epochCurrentAfterSideEffects) noexcept {
    if (!epochCurrentBeforeSideEffects) {
        return SpeedHackPublishEpochDisposition::RejectBeforeSideEffects;
    }
    return epochCurrentAfterSideEffects
        ? SpeedHackPublishEpochDisposition::Commit
        : SpeedHackPublishEpochDisposition::CompensateAfterSideEffects;
}

inline constexpr bool NextSpeedHackNativeDriftLatch(
    bool currentlyLatched,
    bool nativeDriftObserved,
    bool safeResetAt1x) noexcept {
    return nativeDriftObserved || (currentlyLatched && !safeResetAt1x);
}

/* Native drift is sticky across transient ownership conflicts. A deliberate, fully validated
 * 1x decision with observed zero is the sole in-process reset point before a later fresh cycle. */
inline constexpr bool SpeedHackCanResetNativeDriftLatch(
    const SpeedHackArbitrationInput& input,
    const SpeedHackArbitration& decision) noexcept {
    return input.nativeDriftLatched && input.requestedFactor == 1 &&
        input.nativeStateReady && input.nativeAvailabilityReady &&
        input.globalTickHookReady && input.globalTargetOwned && !input.unxModuleLoaded &&
        !input.nativeOwned && input.observedNativeState == 0 &&
        decision.phase == SpeedHackRuntimePhase::Idle &&
        decision.conflict == SpeedHackConflictReason::None &&
        decision.nativeAction == SpeedHackNativeAction::None;
}

inline constexpr uint32_t kSpeedHackPublishedModeMask = 0x00000003u;
inline constexpr uint32_t kSpeedHackPublishedRequestedMask = 0x0000000Cu;
inline constexpr uint32_t kSpeedHackPublishedUnavailableMask = 0x00000010u;
inline constexpr uint32_t kSpeedHackPublishedPausedMask = 0x00000020u;
inline constexpr uint32_t kSpeedHackPublishedGenerationMask = 0xFFFFFF00u;

inline constexpr uint8_t SpeedHackFactorCode(uint8_t factor) noexcept {
    return factor == 2 ? 1 : factor == 4 ? 2 : factor == 8 ? 3 : 0;
}

inline constexpr uint8_t SpeedHackFactorFromCode(uint8_t code) noexcept {
    return code == 1 ? 2 : code == 2 ? 4 : code == 3 ? 8 : 1;
}

inline constexpr uint8_t SpeedHackModeCode(const SpeedHackRoute& route) noexcept {
    return route.backend == SpeedHackBackend::NativeStandard && route.routedFactor == 2
        ? 1
        : route.backend == SpeedHackBackend::NativeStandard && route.routedFactor == 4
            ? 2
            : route.backend == SpeedHackBackend::FastFieldScenes && route.routedFactor == 8
                ? 3
                : 0;
}

inline constexpr uint32_t PackSpeedHackPublishedRoute(
    const SpeedHackRoute& route,
    uint32_t generation,
    bool paused) noexcept {
    return static_cast<uint32_t>(SpeedHackModeCode(route)) |
        (static_cast<uint32_t>(SpeedHackFactorCode(route.requestedFactor)) << 2u) |
        (route.unavailable ? kSpeedHackPublishedUnavailableMask : 0u) |
        (paused ? kSpeedHackPublishedPausedMask : 0u) |
        ((generation & 0x00FFFFFFu) << 8u);
}

inline constexpr SpeedHackPublishedRoute UnpackSpeedHackPublishedRoute(
    uint32_t word) noexcept {
    SpeedHackPublishedRoute published{};
    const uint8_t mode = static_cast<uint8_t>(word & kSpeedHackPublishedModeMask);
    published.requestedFactor = SpeedHackFactorFromCode(
        static_cast<uint8_t>((word & kSpeedHackPublishedRequestedMask) >> 2u));
    published.routedFactor = mode == 1 ? 2 : mode == 2 ? 4 : mode == 3 ? 8 : 1;
    published.backend = mode == 1 || mode == 2
        ? SpeedHackBackend::NativeStandard
        : mode == 3 ? SpeedHackBackend::FastFieldScenes : SpeedHackBackend::None;
    published.unavailable = (word & kSpeedHackPublishedUnavailableMask) != 0;
    published.paused = (word & kSpeedHackPublishedPausedMask) != 0;
    published.generation = (word & kSpeedHackPublishedGenerationMask) >> 8u;
    return published;
}

inline constexpr uint8_t SpeedHackGlobalFactorFromPublishedRoute(uint32_t word) noexcept {
    return (word & kSpeedHackPublishedModeMask) == 3 ? 8 : 1;
}

enum class SpeedHackBridgeAction : uint8_t {
    PassThrough = 0,
    Scale8,
    NativeConflict,
};

inline constexpr SpeedHackBridgeAction ResolveSpeedHackBridgeAction(
    uint32_t publishedRoute,
    bool nativeStateReadable,
    uint32_t nativeState) noexcept {
    if (SpeedHackGlobalFactorFromPublishedRoute(publishedRoute) != 8) {
        return SpeedHackBridgeAction::PassThrough;
    }
    if (!nativeStateReadable || nativeState != 0) {
        return SpeedHackBridgeAction::NativeConflict;
    }
    return SpeedHackBridgeAction::Scale8;
}

inline constexpr bool SpeedHackShouldReportBridgeNativeConflict(
    uint32_t currentRoute,
    bool callbackNativeConflict) noexcept {
    return callbackNativeConflict &&
        SpeedHackGlobalFactorFromPublishedRoute(currentRoute) == 8;
}

inline constexpr bool SpeedHackAcknowledgementMatches(
    uint32_t currentRoute,
    uint32_t acknowledgedRoute) noexcept {
    return currentRoute == acknowledgedRoute;
}

inline constexpr bool SpeedHackCallbackTelemetryMatches(
    uint32_t currentRoute,
    uint32_t acknowledgedRoute,
    uint32_t callbackBaseline,
    uint32_t callbackCount) noexcept {
    return SpeedHackAcknowledgementMatches(currentRoute, acknowledgedRoute) &&
        callbackCount != callbackBaseline &&
        SpeedHackGlobalFactorFromPublishedRoute(currentRoute) == 8;
}

inline constexpr bool SpeedHackShouldLogAppliedGeneration(
    uint32_t currentRoute,
    uint32_t lastLoggedRoute,
    uint32_t acknowledgedRoute,
    uint32_t callbackBaseline,
    uint32_t callbackCount) noexcept {
    return currentRoute != lastLoggedRoute &&
        SpeedHackCallbackTelemetryMatches(
            currentRoute, acknowledgedRoute, callbackBaseline, callbackCount);
}

inline constexpr SpeedHackIndicatorState ResolveSpeedHackIndicator(
    bool gateOn,
    const SpeedHackRuntimeSnapshot& snapshot) noexcept {
    if (!gateOn) return {};

    SpeedHackIndicatorState indicator{};
    indicator.factor = snapshot.requestedFactor;
    indicator.backend = snapshot.backend;
    const bool routeShapeValid =
        (snapshot.routedFactor == 1 && snapshot.backend == SpeedHackBackend::None) ||
        ((snapshot.routedFactor == 2 || snapshot.routedFactor == 4) &&
         snapshot.backend == SpeedHackBackend::NativeStandard &&
         snapshot.nativeStateReady && snapshot.nativeAvailabilityReady) ||
        (snapshot.routedFactor == 8 &&
         snapshot.backend == SpeedHackBackend::FastFieldScenes &&
         snapshot.globalTickHookReady && snapshot.dialogBypassReady) ||
        (snapshot.backend==SpeedHackBackend::Movie && snapshot.routedFactor>=1 && snapshot.routedFactor<=8);
    if (!IsSupportedSpeedHackFactor(snapshot.requestedFactor) || !routeShapeValid ||
        snapshot.phase == SpeedHackRuntimePhase::Unavailable) {
        indicator.mode = SpeedHackIndicatorMode::Unavailable;
        return indicator;
    }
    indicator.factor = snapshot.routedFactor > 1
        ? snapshot.routedFactor
        : snapshot.requestedFactor;
    if (snapshot.phase == SpeedHackRuntimePhase::Conflict) {
        indicator.mode = SpeedHackIndicatorMode::Conflict;
        return indicator;
    }
    if (snapshot.phase == SpeedHackRuntimePhase::Paused) {
        indicator.mode = SpeedHackIndicatorMode::Paused;
        return indicator;
    }
    indicator.mode = snapshot.phase == SpeedHackRuntimePhase::Applied
        ? SpeedHackIndicatorMode::Applied
        : SpeedHackIndicatorMode::Armed;
    return indicator;
}

inline SpeedHackTransition AdvanceSpeedHackControl(
    SpeedHackControlState* state,
    const SpeedHackInputSample& sample) noexcept {
    if (!state) return SpeedHackTransition::None;

    const bool wasDown = state->keyWasDown;
    // Keep the physical edge while disarmed. Re-enabling with K already held must not silently
    // advance the cycle before the player releases and deliberately presses the chord again.
    state->keyWasDown = sample.keyDown;

    if (!sample.gateOn || !sample.foreground) {
        if (state->factor == 1) return SpeedHackTransition::None;
        state->factor = 1;
        return SpeedHackTransition::Restored1x;
    }

    const bool validChord = sample.ctrlDown && sample.shiftDown && !sample.altDown;
    if (!sample.keyDown || wasDown || !validChord) return SpeedHackTransition::None;

    state->factor = NextSpeedHackFactor(state->factor);
    return state->factor == 1
        ? SpeedHackTransition::Restored1x
        : SpeedHackTransition::FactorChanged;
}

inline SpeedHackTransition AdvanceSpeedHackControlAfterForegroundEvent(
    SpeedHackControlState* state,
    SpeedHackInputSample sample,
    bool foregroundLossPending) noexcept {
    /* WM_ACTIVATEAPP/WM_KILLFOCUS can arrive while Present is paused. Treat the queued event as
     * one synthetic background frame even if FFX is foreground again by the time rendering
     * resumes. This both restores 1x and records a held K key, so returning focus cannot create
     * an artificial shortcut edge. */
    if (foregroundLossPending) sample.foreground = false;
    return AdvanceSpeedHackControl(state, sample);
}

inline constexpr size_t kSpeedHackTargetLength = 12;
inline constexpr size_t kSpeedHackOperandOffset = 2;
inline constexpr uint32_t kSpeedHackOperandRva = 0x008C9CF5u;
inline constexpr uint8_t kSpeedHackTargetSignature[kSpeedHackTargetLength] = {
    0x80, 0x3D, 0xF5, 0x9C, 0xCC, 0x00,
    0x00, 0x56, 0x8B, 0xF1, 0x74, 0x06,
};

inline constexpr size_t kSpeedHackGlobalTargetLength = 16;
inline constexpr size_t kSpeedHackGlobalOperandOffset = 10;
inline constexpr uint32_t kSpeedHackGlobalOperandRva = 0x008613D8u;
inline constexpr uint8_t kSpeedHackGlobalTargetSignature[kSpeedHackGlobalTargetLength] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xA4, 0x00, 0x00,
    0x00, 0xA1, 0xD8, 0x13, 0xC6, 0x00, 0x33, 0xC5,
};

enum class SpeedHackTargetStatus : uint8_t {
    Match = 0,
    NullInput,
    WrongLength,
    BaseOverflow,
    DetourLikePrefix,
    Mismatch,
};

struct SpeedHackTargetValidation {
    SpeedHackTargetStatus status = SpeedHackTargetStatus::NullInput;
    uint8_t mismatchOffset = 0xFF;
    uint32_t expectedOperand = 0;
    uint32_t actualOperand = 0;
};

inline SpeedHackTargetValidation ValidateRelocatedSpeedHackTargetSignature(
    const uint8_t* runtimeBytes,
    size_t length,
    uintptr_t loadedImageBase,
    const uint8_t* preferredSignature,
    size_t expectedLength,
    size_t operandOffset,
    uint32_t operandRva) noexcept {
    SpeedHackTargetValidation result{};
    if (!runtimeBytes || !preferredSignature) return result;
    if (length != expectedLength) {
        result.status = SpeedHackTargetStatus::WrongLength;
        return result;
    }

    const uint64_t relocatedOperand =
        static_cast<uint64_t>(loadedImageBase) + operandRva;
    if (relocatedOperand > UINT32_MAX) {
        result.status = SpeedHackTargetStatus::BaseOverflow;
        return result;
    }
    result.expectedOperand = static_cast<uint32_t>(relocatedOperand);
    memcpy(&result.actualOperand, runtimeBytes + operandOffset,
           sizeof(result.actualOperand));

    for (size_t index = 0; index < expectedLength; ++index) {
        uint8_t expectedByte = preferredSignature[index];
        if (index >= operandOffset && index < operandOffset + sizeof(uint32_t)) {
            expectedByte = reinterpret_cast<const uint8_t*>(&result.expectedOperand)[
                index - operandOffset];
        }
        if (runtimeBytes[index] == expectedByte) continue;
        result.mismatchOffset = static_cast<uint8_t>(index);
        result.status = runtimeBytes[0] == 0xE9u ||
                (runtimeBytes[0] == 0xFFu && runtimeBytes[1] == 0x25u)
            ? SpeedHackTargetStatus::DetourLikePrefix
            : SpeedHackTargetStatus::Mismatch;
        return result;
    }

    result.status = SpeedHackTargetStatus::Match;
    return result;
}

inline SpeedHackTargetValidation ValidateSpeedHackTargetSignature(
    const uint8_t* runtimeBytes,
    size_t length,
    uintptr_t loadedImageBase) noexcept {
    return ValidateRelocatedSpeedHackTargetSignature(
        runtimeBytes, length, loadedImageBase,
        kSpeedHackTargetSignature, kSpeedHackTargetLength,
        kSpeedHackOperandOffset, kSpeedHackOperandRva);
}

inline SpeedHackTargetValidation ValidateSpeedHackGlobalTargetSignature(
    const uint8_t* runtimeBytes,
    size_t length,
    uintptr_t loadedImageBase) noexcept {
    return ValidateRelocatedSpeedHackTargetSignature(
        runtimeBytes, length, loadedImageBase,
        kSpeedHackGlobalTargetSignature, kSpeedHackGlobalTargetLength,
        kSpeedHackGlobalOperandOffset, kSpeedHackGlobalOperandRva);
}

enum class SpeedHackInstallStatus : uint8_t {
    Installed = 0,
    AlreadyInstalled,
    UnsupportedProfile,
    NativeStateOutOfRange,
    NativeAvailabilityOutOfRange,
    GlobalTargetOutOfRange,
    GlobalSignatureMismatch,
    GlobalDetourLikePrefix,
    UnXModuleConflict,
    GlobalDetourFailed,
    PolyHookUnavailable,
};

bool InstallSpeedHackHook(
    uintptr_t moduleBase,
    bool dialogBypassReady,
    void* logFn,
    SpeedHackInstallStatus* statusOut = nullptr);
void SpeedHackFrameTick();
using SpeedHackShortcutReader=bool(*)();
void SpeedHackSetShortcutReader(SpeedHackShortcutReader) noexcept;
SpeedHackRuntimeSnapshot GetSpeedHackRuntimeSnapshot() noexcept;
void SpeedHackNotifyForegroundLost() noexcept;
void RequestSpeedHackStop() noexcept;
void RemoveSpeedHackHook();

} // namespace FfxHooks
