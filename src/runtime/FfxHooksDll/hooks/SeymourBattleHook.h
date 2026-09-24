#pragma once

#include "SeymourBattleCore.h"

#include <cstdint>

namespace FfxHooks {

using SeymourBattleLogFn = void (*)(const char*);

// Supported profile: FFX.exe PE32/I386, preferred image base 0x00400000, timestamp 0x55D2F3CC,
// SizeOfImage 0x0237D000, SHA-256
// 78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED.
// These are preferred RVAs; runtime addresses are loaded module base + RVA. Entry call
// VA 0x00783217 / RVA 0x00383217 targets int __cdecl InitScene() at VA 0x00783ED0 /
// RVA 0x00383ED0 and admits only return VA 0x0078321C / RVA 0x0038321C. Bootstrap return
// RVA 0x00381C76 and startup/Sphere Grid return RVA 0x003821CF remain vanilla-only. Exit call
// VA 0x00790F02 / RVA 0x00390F02 targets void __cdecl SyncPartyStatsFromActors() at
// VA 0x00786080 / RVA 0x00386080 and admits only return VA 0x00790F07 / RVA 0x00390F07.
// The canonical entry target belongs to F7's shared composer; Seymour owns only the unique exit.
inline constexpr uint32_t kSeymourBattleEntryCallRva = 0x00383217u;
inline constexpr uint32_t kSeymourBattleEntryReturnRva = 0x0038321Cu;
inline constexpr uint32_t kSeymourBattleExitCallRva = 0x00390F02u;
inline constexpr uint32_t kSeymourBattleExitReturnRva = 0x00390F07u;
inline constexpr uint32_t kExitTargetRva = 0x00386080u;
inline constexpr uint32_t kSeymourBattleAssignRva = 0x00386A70u;
inline constexpr uint32_t kSeymourBattlePersistentStateRva = 0x00D307E8u;
inline constexpr uint32_t kSeymourBattlePersistentAbilityRva = 0x00D307EBu;
inline constexpr uint32_t kSeymourBattlePartyByteRva = 0x00D32494u;
inline constexpr uint32_t kSeymourBattleDiscriminatorRva = 0x00D2A8E0u;
inline constexpr uint32_t kSeymourBattleLocalStateRva = 0x00D2C895u;
inline constexpr uint32_t kSeymourBattleLocalAbilityRva = 0x00D2C8A3u;

// The official formation routine is int __cdecl(uint8_t slot, int active) at preferred
// VA 0x00786A70 / RVA 0x00386A70. Slot 7 and discriminator byte 7 at VA 0x0112A8E0 /
// RVA 0x00D2A8E0 are exact build evidence, not masks or aliases. Owned persistent spans are
// party[1] at VA 0x01132494 / RVA 0x00D32494, state[3] at VA 0x011307E8 /
// RVA 0x00D307E8, and ability[17] at VA 0x011307EB / RVA 0x00D307EB. Local state[7] at
// VA 0x0112C895 / RVA 0x00D2C895 and ability[17] at VA 0x0112C8A3 / RVA 0x00D2C8A3 are
// evidence/protocol spans only and are never directly written by this adapter.

enum class SeymourBattleInstallStatus : uint8_t {
    Installed = 0,
    AlreadyInstalled,
    ValidateOnly,
    SharedRuntimeUnavailable,
    UnsupportedProfile,
    TargetOutOfRange,
    SignatureMismatch,
    ExitCreateFailed,
    ExitPublicationFailed,
    ExitEnableFailed,
    CoordinatorUnavailable,
    CoordinatorBusy,
    CoordinatorPoisoned,
    ComposerConflict,
    RestorePending,
};

struct SeymourBattleRuntimeSnapshot {
    SeymourBattle::State state = SeymourBattle::State::Off;
    SeymourBattle::ServiceOutcome outcome = SeymourBattle::ServiceOutcome::NoChange;
    uint32_t generation = 0;
    uint32_t threadId = 0;
    SeymourBattle::CallbackKind callback = SeymourBattle::CallbackKind::None;
    uint32_t returnAddress = 0;
    uint32_t battleDiscriminator = 0;
    uint32_t beforeHash = 0;
    uint32_t applyHash = 0;
    uint32_t restoreHash = 0;
    bool installed = false;
    bool producerReady = false;
};

bool StartSeymourBattleHook(
    uintptr_t moduleBase, SeymourBattleLogFn log,
    SeymourBattleInstallStatus* statusOut = nullptr);
void NotifySeymourBattlePresentProducer(bool ready, bool terminalFailure);
// Present publishes config commands and telemetry only; it never reads or writes game RAM.
void SeymourBattlePresentTick();
// Loader-lock safe: publish the absorbing stop bit and close admission, without waiting.
void RequestSeymourBattleStop();
// Normal-context retirement never removes a MinHook target or process-lifetime trampoline.
bool RemoveSeymourBattleHook();
SeymourBattleRuntimeSnapshot GetSeymourBattleRuntimeSnapshot();
const char* SeymourBattleInstallStatusName(SeymourBattleInstallStatus status);

} // namespace FfxHooks
