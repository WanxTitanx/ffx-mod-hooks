#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

/* DialogSkipHook.h — the sole owner of FFX_FmodVoice_ReadEventData.
 *
 * The legacy UnX patch replaced the real entry at RVA 0x30AEC0 with `ret 8`. This port keeps
 * one reversible detour installed and ORs two independent, pre-published reasons: the player's
 * input.dialog_skip gate and the composed Speed Hack 8x route. The latter accelerates dialog and
 * field-scene pacing by bypassing voice-event reads alongside the clean-room field tick; it does
 * not decode, seek, or accelerate pre-rendered FMV. No configuration or logging is performed on
 * the voice callback thread. Stop is absorbing and clears both owners before teardown. */
namespace FfxHooks {

inline constexpr size_t kDialogSkipTargetLength = 16;
inline constexpr uint8_t kDialogSkipTargetSignature[kDialogSkipTargetLength] = {
    0x55, 0x8B, 0xEC, 0x83, 0x7D, 0x0C, 0x00, 0x57,
    0x8B, 0xF9, 0x0F, 0x84, 0xBE, 0x02, 0x00, 0x00,
};

enum class DialogSkipTargetStatus : uint8_t {
    Match = 0,
    NullInput,
    WrongLength,
    DetourLikePrefix,
    Mismatch,
};

struct DialogSkipTargetValidation {
    DialogSkipTargetStatus status = DialogSkipTargetStatus::NullInput;
    uint8_t mismatchOffset = 0xFF;
};

inline DialogSkipTargetValidation ValidateDialogSkipTargetSignature(
    const uint8_t* runtimeBytes,
    size_t length) noexcept {
    DialogSkipTargetValidation result{};
    if (!runtimeBytes) return result;
    if (length != kDialogSkipTargetLength) {
        result.status = DialogSkipTargetStatus::WrongLength;
        return result;
    }
    for (size_t index = 0; index < kDialogSkipTargetLength; ++index) {
        if (runtimeBytes[index] == kDialogSkipTargetSignature[index]) continue;
        result.mismatchOffset = static_cast<uint8_t>(index);
        result.status = runtimeBytes[0] == 0xE9u ||
                (runtimeBytes[0] == 0xFFu && runtimeBytes[1] == 0x25u)
            ? DialogSkipTargetStatus::DetourLikePrefix
            : DialogSkipTargetStatus::Mismatch;
        return result;
    }
    result.status = DialogSkipTargetStatus::Match;
    return result;
}

inline constexpr bool ShouldBypassDialogVoice(
    bool manualDialogSkip,
    bool speedHack8Active) noexcept {
    return manualDialogSkip || speedHack8Active;
}

inline constexpr uint32_t kDialogSkipManualMask = 0x00000001u;
inline constexpr uint32_t kDialogSkipSpeed8Mask = 0x00000002u;
inline constexpr uint32_t kDialogSkipHookReadyMask = 0x00000004u;
inline constexpr uint32_t kDialogSkipStoppedMask = 0x00000008u;

struct DialogSkipPublishedState {
    bool hookReady = false;
    bool stopped = false;
    bool manualRequested = false;
    bool speedHack8Requested = false;
};

inline constexpr uint32_t PackDialogSkipPublishedState(
    bool hookReady,
    bool stopped,
    bool manualRequested,
    bool speedHack8Requested) noexcept {
    /* Stop is absorbing. Clearing request bits in the same word prevents a stale Present
     * producer from briefly re-enabling the callback after detach or terminal install failure. */
    return (hookReady ? kDialogSkipHookReadyMask : 0u) |
        (stopped ? kDialogSkipStoppedMask : 0u) |
        (!stopped && manualRequested ? kDialogSkipManualMask : 0u) |
        (!stopped && speedHack8Requested ? kDialogSkipSpeed8Mask : 0u);
}

inline constexpr DialogSkipPublishedState UnpackDialogSkipPublishedState(
    uint32_t word) noexcept {
    return {
        (word & kDialogSkipHookReadyMask) != 0,
        (word & kDialogSkipStoppedMask) != 0,
        (word & kDialogSkipManualMask) != 0,
        (word & kDialogSkipSpeed8Mask) != 0,
    };
}

inline constexpr bool DialogSkipPublishedStateEffective(uint32_t word) noexcept {
    return (word & kDialogSkipHookReadyMask) != 0 &&
        (word & kDialogSkipStoppedMask) == 0 &&
        (word & (kDialogSkipManualMask | kDialogSkipSpeed8Mask)) != 0;
}

inline constexpr uint32_t ResolveDialogSkipPublishedRequests(
    uint32_t currentWord,
    bool manualRequested,
    bool speedHack8Requested) noexcept {
    const DialogSkipPublishedState current =
        UnpackDialogSkipPublishedState(currentWord);
    return PackDialogSkipPublishedState(
        current.hookReady,
        current.stopped,
        manualRequested,
        speedHack8Requested);
}

inline constexpr uint32_t ClearDialogSkipPublishedSpeedRequest(
    uint32_t currentWord) noexcept {
    const DialogSkipPublishedState current =
        UnpackDialogSkipPublishedState(currentWord);
    return ResolveDialogSkipPublishedRequests(
        currentWord, current.manualRequested, false);
}

inline constexpr uint32_t StopDialogSkipPublishedState(uint32_t currentWord) noexcept {
    const DialogSkipPublishedState current =
        UnpackDialogSkipPublishedState(currentWord);
    return PackDialogSkipPublishedState(current.hookReady, true, false, false);
}

enum class DialogSkipInstallStatus : uint8_t {
    Installed = 0,
    AlreadyInstalled,
    UnsupportedProfile,
    TargetOutOfRange,
    SignatureMismatch,
    DetourLikePrefix,
    DetourFailed,
    PolyHookUnavailable,
};

struct DialogSkipRuntimeSnapshot {
    bool hookReady = false;
    bool manualRequested = false;
    bool speedHack8Requested = false;
    bool effectiveBypass = false;
};

bool InstallDialogSkipHook(
    uintptr_t moduleBase,
    void* logFn,
    DialogSkipInstallStatus* statusOut = nullptr);
void DialogSkipFrameTick(bool speedHack8Active);
DialogSkipRuntimeSnapshot GetDialogSkipRuntimeSnapshot() noexcept;
void DialogSkipNotifySpeedHack8Inactive() noexcept;
void RequestDialogSkipStop() noexcept;
void RemoveDialogSkipHook();

} // namespace FfxHooks
