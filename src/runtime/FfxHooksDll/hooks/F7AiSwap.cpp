// Monster AI observe-only runtime adapter (legacy file name retained for build compatibility).
//
// The old implementation inferred an action and target from unrelated actor bytes, then wrote
// status state every frame. That path could not swap AI and is intentionally gone. This adapter
// observes the game-owned channel-1 registration/cleanup lifecycle and command-dispatch telemetry
// without granting any actor, script, command, target, file, or save-data write authority.

#include "F7AiSwap.h"

#include "F8RuntimeCore.h"
#include "MinHookBatchCoordinator.h"
#include "MonsterAiDispatchShadow.h"
#include "MonsterAiDispatchTelemetry.h"
#include "MonsterAiObserverCore.h"
#include "../shared/Config.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>

#ifdef FFXHOOKS_HAVE_POLYHOOK
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <MinHook.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <utility>
#include <intrin.h>
#endif

namespace FfxHooks {
namespace {

using MonsterAiObserver::DetourIo;
using MonsterAiObserver::DetourOwner;
using MonsterAiObserver::DrainIo;
using MonsterAiObserver::EventSink;
using MonsterAiObserver::LifecycleState;
using MonsterAiObserver::OriginalCall;
using MonsterAiObserver::ReadOnlyMemory;
using MonsterAiObserver::Snapshot;
using MonsterAiObserver::SnapshotPhase;
using MonsterAiObserver::TeardownResult;

constexpr uint32_t RVA_MONSTER_SCRIPT_REGISTRATION = 0x00384120u;
constexpr uint32_t RVA_MONSTER_SCRIPT_CLEANUP = 0x00381660u;
constexpr uint32_t RVA_MONSTER_SCRIPT_REGISTRATION_CALLER = 0x003839C4u;
constexpr uint32_t RVA_MONSTER_AI_DISPATCH = MonsterAiShadow::kDispatcherRva;
constexpr uint32_t RVA_MONSTER_ACTOR_LIST_POINTER = 0x00D34460u;
constexpr uint32_t kSupportedImageSize = 0x0237D000u;
constexpr uint32_t kPreferredImageBase = 0x00400000u;

// Offline evidence identity: FFX.exe PE32/I386, preferred image base 0x00400000,
// timestamp 0x55D2F3CC, SizeOfImage 0x0237D000, SHA-256
// 78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED. This adapter
// does not recompute the SHA: runtime admission combines the exact PE identity with the loaded,
// ASLR-adjusted signatures and caller cardinality below.
//
// Critical probes use the supported build's preferred VA/RVA contract:
// - registration is VA 0x00784120 / RVA 0x00384120, void __cdecl(), with the sole direct
//   call at VA 0x007839C4 / RVA 0x003839C4. The shim reads one u32 pointer from preferred
//   VA 0x01134460 / RVA 0x00D34460, then observes eight 0xF90 actor records read-only;
// - cleanup is VA 0x00781660 / RVA 0x00381660, void __cdecl(), and its prefix compares the
//   direct byte global at preferred VA 0x0112A8E0 / RVA 0x00D2A8E0 (no pointer dereference);
// - dispatch is VA 0x007AC9E0 / RVA 0x003AC9E0,
//   int32_t __cdecl(int32_t, int32_t, uint32_t, int32_t, int32_t), admitted only from the
//   three preferred return VAs 0x007A454E, 0x007A4A60, and 0x007A4B8C. Its four abs32
//   operands are direct ASLR-relocated globals, not pointer-chain guesses.

// These base-relocation entries cover only the absolute memory operands inside the two prefixes.
// The trailing E8 displacements are relative to the next instruction and therefore stay exact.
constexpr uint32_t RVA_MONSTER_SCRIPT_REGISTRATION_RELOCATION = 0x00384127u;
constexpr size_t kRegistrationInstructionOffset = 6u;
constexpr size_t kRegistrationOperandOffset = 1u;
constexpr uint32_t RVA_MONSTER_SCRIPT_REGISTRATION_TARGET = 0x008613D8u;
constexpr uint32_t RVA_MONSTER_SCRIPT_CLEANUP_RELOCATION = 0x00381662u;
constexpr size_t kCleanupInstructionOffset = 0u;
constexpr size_t kCleanupOperandOffset = 2u;
constexpr uint32_t RVA_MONSTER_SCRIPT_CLEANUP_TARGET = 0x00D2A8E0u;

// FFX_Battle_DispatchActionCommand has four absolute global operands in its exact 66-byte
// supported-executable prefix. The PE base-relocation table marks all four as HIGHLOW; every
// other byte in the prefix is invariant under ASLR.
constexpr uint32_t RVA_MONSTER_AI_DISPATCH_RELOCATION_0 = 0x003AC9E8u;
constexpr uint32_t RVA_MONSTER_AI_DISPATCH_RELOCATION_1 = 0x003AC9F6u;
constexpr uint32_t RVA_MONSTER_AI_DISPATCH_RELOCATION_2 = 0x003ACA06u;
constexpr uint32_t RVA_MONSTER_AI_DISPATCH_RELOCATION_3 = 0x003ACA1Eu;
constexpr uint32_t RVA_MONSTER_AI_DISPATCH_TARGET_0 = 0x00D36A58u;
constexpr uint32_t RVA_MONSTER_AI_DISPATCH_TARGET_1 = 0x00D36A68u;
constexpr uint32_t RVA_MONSTER_AI_DISPATCH_TARGET_2 = 0x00D36A6Bu;
constexpr uint32_t RVA_MONSTER_AI_DISPATCH_TARGET_3 = 0x00D36A70u;

constexpr std::array<uint8_t, 24> kRegistrationSignature = {
    0x55u, 0x8Bu, 0xECu, 0x83u, 0xECu, 0x0Cu, 0xA1u, 0xD8u,
    0x13u, 0xC6u, 0x00u, 0x33u, 0xC5u, 0x89u, 0x45u, 0xFCu,
    0x53u, 0x56u, 0x57u, 0xE8u, 0xF8u, 0x89u, 0x01u, 0x00u,
};
constexpr std::array<uint8_t, 18> kCleanupSignature = {
    0x80u, 0x3Du, 0xE0u, 0xA8u, 0x12u, 0x01u, 0x00u, 0x0Fu,
    0x84u, 0x91u, 0x00u, 0x00u, 0x00u, 0xE8u, 0x6Eu, 0xBBu,
    0xFEu, 0xFFu,
};
constexpr std::array<uint8_t, 5> kRegistrationCallerSignature = {
    0xE8u, 0x57u, 0x07u, 0x00u, 0x00u,
};
constexpr std::array<uint8_t, 66> kDispatchSignature = {
    0x55u, 0x8Bu, 0xECu, 0x83u, 0xECu, 0x10u, 0x83u, 0x3Du,
    0x58u, 0x6Au, 0x13u, 0x01u, 0xFFu, 0x0Fu, 0x84u, 0x20u,
    0x01u, 0x00u, 0x00u, 0x0Fu, 0xB6u, 0x05u, 0x68u, 0x6Au,
    0x13u, 0x01u, 0x8Bu, 0x55u, 0x08u, 0x3Bu, 0xD0u, 0x0Fu,
    0x85u, 0x0Eu, 0x01u, 0x00u, 0x00u, 0xA0u, 0x6Bu, 0x6Au,
    0x13u, 0x01u, 0x3Cu, 0x04u, 0x0Fu, 0x83u, 0x01u, 0x01u,
    0x00u, 0x00u, 0x0Fu, 0xB6u, 0xC8u, 0x66u, 0x8Bu, 0x45u,
    0x0Cu, 0xC1u, 0xE1u, 0x04u, 0x81u, 0xC1u, 0x70u, 0x6Au,
    0x13u, 0x01u,
};
constexpr std::array<uint32_t, 3> kDispatchCallerReturnRvas = {
    MonsterAiShadow::kNormalPerformReturnPreferredVa - kPreferredImageBase,
    MonsterAiShadow::kForceDispatchReturnPreferredVa - kPreferredImageBase,
    MonsterAiShadow::kDeathOverrideReturnPreferredVa - kPreferredImageBase,
};

static_assert(RVA_MONSTER_SCRIPT_REGISTRATION_RELOCATION ==
              RVA_MONSTER_SCRIPT_REGISTRATION + kRegistrationInstructionOffset +
                  kRegistrationOperandOffset,
              "registration relocation evidence must identify the abs32 operand");
static_assert(RVA_MONSTER_SCRIPT_CLEANUP_RELOCATION ==
              RVA_MONSTER_SCRIPT_CLEANUP + kCleanupInstructionOffset +
                  kCleanupOperandOffset,
              "cleanup relocation evidence must identify the abs32 operand");
static_assert(kPreferredImageBase + RVA_MONSTER_SCRIPT_REGISTRATION_TARGET == 0x00C613D8u,
              "registration preferred-base operand must match the evidenced prefix");
static_assert(kPreferredImageBase + RVA_MONSTER_SCRIPT_CLEANUP_TARGET == 0x0112A8E0u,
              "cleanup preferred-base operand must match the evidenced prefix");
static_assert(RVA_MONSTER_AI_DISPATCH_RELOCATION_0 == RVA_MONSTER_AI_DISPATCH + 0x08u &&
                  RVA_MONSTER_AI_DISPATCH_RELOCATION_1 == RVA_MONSTER_AI_DISPATCH + 0x16u &&
                  RVA_MONSTER_AI_DISPATCH_RELOCATION_2 == RVA_MONSTER_AI_DISPATCH + 0x26u &&
                  RVA_MONSTER_AI_DISPATCH_RELOCATION_3 == RVA_MONSTER_AI_DISPATCH + 0x3Eu,
              "dispatcher HIGHLOW operands must retain their exact 66-byte signature offsets");
static_assert(kPreferredImageBase + RVA_MONSTER_AI_DISPATCH_TARGET_0 == 0x01136A58u &&
                  kPreferredImageBase + RVA_MONSTER_AI_DISPATCH_TARGET_1 == 0x01136A68u &&
                  kPreferredImageBase + RVA_MONSTER_AI_DISPATCH_TARGET_2 == 0x01136A6Bu &&
                  kPreferredImageBase + RVA_MONSTER_AI_DISPATCH_TARGET_3 == 0x01136A70u,
              "dispatcher preferred-base operands must match the supported on-disk prefix");

struct RelocatedOperandSpec {
    uint32_t relocationRva;
    uint32_t targetRva;
};

struct RelocatedSignatureSpec {
    uint32_t signatureRva;
    const uint8_t* preferredBytes;
    size_t length;
    const RelocatedOperandSpec* relocations;
    size_t relocationCount;
};

enum class ObserverSignatureTarget : uint8_t {
    Registration = 0,
    Cleanup,
    Dispatch,
};

constexpr std::array<RelocatedOperandSpec, 1> kRegistrationRelocations = {{
    {RVA_MONSTER_SCRIPT_REGISTRATION_RELOCATION, RVA_MONSTER_SCRIPT_REGISTRATION_TARGET},
}};
constexpr std::array<RelocatedOperandSpec, 1> kCleanupRelocations = {{
    {RVA_MONSTER_SCRIPT_CLEANUP_RELOCATION, RVA_MONSTER_SCRIPT_CLEANUP_TARGET},
}};
constexpr std::array<RelocatedOperandSpec, 4> kDispatchRelocations = {{
    {RVA_MONSTER_AI_DISPATCH_RELOCATION_0, RVA_MONSTER_AI_DISPATCH_TARGET_0},
    {RVA_MONSTER_AI_DISPATCH_RELOCATION_1, RVA_MONSTER_AI_DISPATCH_TARGET_1},
    {RVA_MONSTER_AI_DISPATCH_RELOCATION_2, RVA_MONSTER_AI_DISPATCH_TARGET_2},
    {RVA_MONSTER_AI_DISPATCH_RELOCATION_3, RVA_MONSTER_AI_DISPATCH_TARGET_3},
}};

constexpr RelocatedSignatureSpec kRegistrationSignatureSpec = {
    RVA_MONSTER_SCRIPT_REGISTRATION,
    kRegistrationSignature.data(),
    kRegistrationSignature.size(),
    kRegistrationRelocations.data(),
    kRegistrationRelocations.size(),
};
constexpr RelocatedSignatureSpec kCleanupSignatureSpec = {
    RVA_MONSTER_SCRIPT_CLEANUP,
    kCleanupSignature.data(),
    kCleanupSignature.size(),
    kCleanupRelocations.data(),
    kCleanupRelocations.size(),
};
constexpr RelocatedSignatureSpec kDispatchSignatureSpec = {
    RVA_MONSTER_AI_DISPATCH,
    kDispatchSignature.data(),
    kDispatchSignature.size(),
    kDispatchRelocations.data(),
    kDispatchRelocations.size(),
};

const RelocatedSignatureSpec& LoadedSignatureSpec(ObserverSignatureTarget target) {
    switch (target) {
        case ObserverSignatureTarget::Registration: return kRegistrationSignatureSpec;
        case ObserverSignatureTarget::Cleanup: return kCleanupSignatureSpec;
        case ObserverSignatureTarget::Dispatch: return kDispatchSignatureSpec;
        default: return kRegistrationSignatureSpec;
    }
}

bool BuildExpectedLoadedSignature(ObserverSignatureTarget target, uintptr_t moduleBase,
                                  uint8_t* expectedOut, size_t expectedLength) {
    const RelocatedSignatureSpec& spec = LoadedSignatureSpec(target);
    if (!moduleBase || !expectedOut || expectedLength != spec.length ||
        !spec.relocations || spec.relocationCount == 0u) {
        return false;
    }

    std::memcpy(expectedOut, spec.preferredBytes, spec.length);
    for (size_t relocationIndex = 0u; relocationIndex < spec.relocationCount;
         ++relocationIndex) {
        const RelocatedOperandSpec& relocation = spec.relocations[relocationIndex];
        if (relocation.relocationRva < spec.signatureRva) return false;
        const size_t operandOffset =
            static_cast<size_t>(relocation.relocationRva - spec.signatureRva);
        if (operandOffset > spec.length || spec.length - operandOffset < sizeof(uint32_t)) {
            return false;
        }
        const uint64_t loadedTarget =
            static_cast<uint64_t>(moduleBase) + static_cast<uint64_t>(relocation.targetRva);
        if (loadedTarget > (std::numeric_limits<uint32_t>::max)()) return false;
        const uint32_t target32 = static_cast<uint32_t>(loadedTarget);
        expectedOut[operandOffset + 0u] = static_cast<uint8_t>(target32 & 0xFFu);
        expectedOut[operandOffset + 1u] = static_cast<uint8_t>((target32 >> 8u) & 0xFFu);
        expectedOut[operandOffset + 2u] = static_cast<uint8_t>((target32 >> 16u) & 0xFFu);
        expectedOut[operandOffset + 3u] = static_cast<uint8_t>((target32 >> 24u) & 0xFFu);
    }
    return true;
}

bool ValidateLoadedSignature(ObserverSignatureTarget target, uintptr_t moduleBase,
                             const uint8_t* observed, size_t observedLength) {
    if (!observed || observedLength > kDispatchSignature.size()) return false;
    std::array<uint8_t, kDispatchSignature.size()> expected{};
    if (!BuildExpectedLoadedSignature(
            target, moduleBase, expected.data(), observedLength)) {
        return false;
    }
    return std::memcmp(observed, expected.data(), observedLength) == 0;
}

bool ValidateDispatchCallerReturns(const uint32_t* callerReturnRvas, size_t count) {
    if (!callerReturnRvas || count != kDispatchCallerReturnRvas.size()) return false;
    for (uint32_t expected : kDispatchCallerReturnRvas) {
        size_t matches = 0u;
        for (size_t index = 0u; index < count; ++index) {
            if (callerReturnRvas[index] == expected) ++matches;
        }
        if (matches != 1u) return false;
    }
    return true;
}

F7AiObserverStatus ResolveRequestedStatus(F7AiObserverStatus runtimeStatus, bool requested) {
    return runtimeStatus == F7AiObserverStatus::Off && requested
        ? F7AiObserverStatus::ObservePendingRestart
        : runtimeStatus;
}

F7AiObserverStatus ResolveSetupFailureStatus(
    F7AiObserverStatus runtimeStatus, bool requested) {
    if (!requested) return runtimeStatus;
    // Setup failure is terminal for this process. Preserve any stronger live/teardown state, but
    // never let an untouched OFF status be reinterpreted as a pending-restart observer request.
    return runtimeStatus == F7AiObserverStatus::Off ||
                   runtimeStatus == F7AiObserverStatus::ObservePendingRestart
        ? F7AiObserverStatus::Unavailable
        : runtimeStatus;
}

F7AiObserverStatus ResolveInstallFailureStatus(
    MonsterAiObserver::InstallResult result, const DetourOwner& owner) {
    if (owner.retainedInert) return F7AiObserverStatus::RetainedInert;
    // UNAVAILABLE is safe only after ownership was fully rolled back. A created target or a
    // poisoned partial apply can still route vanilla calls through retained observer state, so
    // expose STOPPING until normal-context teardown proves an inert boundary.
    if (result == MonsterAiObserver::InstallResult::RollbackFailed ||
        owner.registrationCreated || owner.cleanupCreated || owner.dispatchCreated ||
        owner.active ||
        owner.coordinatorPoisoned) {
        return F7AiObserverStatus::Stopping;
    }
    return F7AiObserverStatus::Unavailable;
}

#ifdef FFXHOOKS_HAVE_POLYHOOK
const char* InitializationResultName(MinHookBatch::InitializationResult result) {
    switch (result) {
        case MinHookBatch::InitializationResult::Ready: return "Ready";
        case MinHookBatch::InitializationResult::InvalidArgument: return "InvalidArgument";
        case MinHookBatch::InitializationResult::Busy: return "Busy";
        case MinHookBatch::InitializationResult::FailedPoisoned: return "FailedPoisoned";
        case MinHookBatch::InitializationResult::Poisoned: return "Poisoned";
        default: return "Unknown";
    }
}

const char* InstallResultName(MonsterAiObserver::InstallResult result) {
    switch (result) {
        case MonsterAiObserver::InstallResult::Installed: return "Installed";
        case MonsterAiObserver::InstallResult::InvalidArgument: return "InvalidArgument";
        case MonsterAiObserver::InstallResult::AlreadyOwned: return "AlreadyOwned";
        case MonsterAiObserver::InstallResult::CreateFailed: return "CreateFailed";
        case MonsterAiObserver::InstallResult::QueueFailed: return "QueueFailed";
        case MonsterAiObserver::InstallResult::ApplyFailed: return "ApplyFailed";
        case MonsterAiObserver::InstallResult::RollbackFailed: return "RollbackFailed";
        case MonsterAiObserver::InstallResult::BatchBusy: return "BatchBusy";
        case MonsterAiObserver::InstallResult::CoordinatorNotInitialized:
            return "CoordinatorNotInitialized";
        case MonsterAiObserver::InstallResult::CoordinatorPoisoned:
            return "CoordinatorPoisoned";
        default: return "Unknown";
    }
}

const char* TeardownResultName(MonsterAiObserver::TeardownResult result) {
    switch (result) {
        case MonsterAiObserver::TeardownResult::Removed: return "Removed";
        case MonsterAiObserver::TeardownResult::RetainedInert: return "RetainedInert";
        case MonsterAiObserver::TeardownResult::InvalidArgument: return "InvalidArgument";
        case MonsterAiObserver::TeardownResult::RemoveFailed: return "RemoveFailed";
        case MonsterAiObserver::TeardownResult::BatchBusy: return "BatchBusy";
        case MonsterAiObserver::TeardownResult::CoordinatorPoisoned:
            return "CoordinatorPoisoned";
        default: return "Unknown";
    }
}
#endif

MonsterAiObserver::InstallResult InstallObserverAdapter(
    const DetourIo& detours, MinHookBatch::Coordinator* coordinator,
    const MinHookBatch::BatchIo& batchIo, const DrainIo& drain,
    LifecycleState* lifecycle,
    uintptr_t registrationTarget, uintptr_t registrationDetour,
    uintptr_t cleanupTarget, uintptr_t cleanupDetour,
    uintptr_t dispatchTarget, uintptr_t dispatchDetour, DetourOwner* owner) {
    return MonsterAiObserver::InstallDetourSet(
        detours, coordinator, batchIo, drain, lifecycle,
        registrationTarget, registrationDetour,
        cleanupTarget, cleanupDetour,
        dispatchTarget, dispatchDetour, owner);
}

TeardownResult RemoveObserverAdapter(
    const DetourIo& detours, MinHookBatch::Coordinator* coordinator,
    const MinHookBatch::BatchIo& batchIo, const DrainIo& drain,
    LifecycleState* lifecycle, DetourOwner* owner) {
    return MonsterAiObserver::StopDrainAndRetainDetourSet(
        detours, coordinator, batchIo, drain, lifecycle, owner);
}

int32_t ObserveDispatchAdapter(
    MonsterAiObserver::CallbackLease& callback,
    MonsterAiDispatchTelemetry::DispatchTelemetryState* telemetry,
    uint32_t threadId, uint32_t callerReturnPreferredVa,
    int32_t actorIndex, int32_t commandStack32, uint32_t targetMask,
    int32_t force, int32_t n64,
    const MonsterAiDispatchTelemetry::DispatchEventSink& sink,
    const MonsterAiDispatchTelemetry::DispatchOriginalCall& original) {
    return MonsterAiDispatchTelemetry::ObserveDispatch(
        callback, telemetry, threadId, callerReturnPreferredVa,
        actorIndex, commandStack32, targetMask, force, n64, sink, original);
}

uint32_t NormalizeCallerReturn(uintptr_t moduleBase, uintptr_t liveReturn) {
    if (!moduleBase || liveReturn < moduleBase) return 0u;
    const uintptr_t rva = liveReturn - moduleBase;
    if (rva >= kSupportedImageSize || rva > UINT32_MAX - kPreferredImageBase) return 0u;
    return kPreferredImageBase + static_cast<uint32_t>(rva);
}

#ifdef FFXHOOKS_HAVE_POLYHOOK

uintptr_t g_moduleBase = 0u;
void (*g_log)(const char*) = nullptr;
volatile LONG g_status = static_cast<LONG>(F7AiObserverStatus::Off);
LifecycleState g_lifecycle{};
DetourOwner g_detourOwner{};
MonsterAiDispatchTelemetry::DispatchTelemetryState g_dispatchTelemetry{};

using ScriptLifecycleFn = void(__cdecl*)();
using DispatchFn = int32_t(__cdecl*)(int32_t, int32_t, uint32_t, int32_t, int32_t);

void ObserverLog(const char* format, ...) {
    if (!g_log || !format) return;
    char line[512] = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, format, arguments);
    va_end(arguments);
    g_log(line);
}

void PublishStatus(F7AiObserverStatus status) {
    InterlockedExchange(&g_status, static_cast<LONG>(status));
}

F7AiObserverStatus RuntimeStatus() {
    return static_cast<F7AiObserverStatus>(
        InterlockedCompareExchange(&g_status, 0, 0));
}

bool ReadProcessBytes(void*, uintptr_t address, uint8_t* out, size_t length) {
    if (!address || !out || length == 0u) return false;
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), length);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadRuntimeU32(uintptr_t address, uint32_t* valueOut) {
    uint8_t bytes[4] = {};
    if (!valueOut || !ReadProcessBytes(nullptr, address, bytes, sizeof(bytes))) return false;
    *valueOut = static_cast<uint32_t>(bytes[0]) |
                (static_cast<uint32_t>(bytes[1]) << 8u) |
                (static_cast<uint32_t>(bytes[2]) << 16u) |
                (static_cast<uint32_t>(bytes[3]) << 24u);
    return true;
}

bool ExactBytes(uintptr_t address, const uint8_t* expected, size_t length) {
    if (!expected || length == 0u) return false;
    std::array<uint8_t, kDispatchSignature.size()> observed{};
    if (length > observed.size() || !ReadProcessBytes(nullptr, address, observed.data(), length)) {
        return false;
    }
    return std::memcmp(observed.data(), expected, length) == 0;
}

bool ExactLoadedSignature(uintptr_t moduleBase, ObserverSignatureTarget target) {
    const RelocatedSignatureSpec& spec = LoadedSignatureSpec(target);
    if (moduleBase > (std::numeric_limits<uintptr_t>::max)() - spec.signatureRva) return false;
    std::array<uint8_t, kDispatchSignature.size()> observed{};
    if (!ReadProcessBytes(nullptr, moduleBase + spec.signatureRva,
                          observed.data(), spec.length)) {
        return false;
    }
    return ValidateLoadedSignature(target, moduleBase, observed.data(), spec.length);
}

bool CheckedTargetFromCall(uintptr_t callAddress, int32_t displacement, uintptr_t* targetOut) {
    if (!targetOut || callAddress > UINTPTR_MAX - 5u) return false;
    const uintptr_t next = callAddress + 5u;
    if (displacement >= 0) {
        const uintptr_t distance = static_cast<uintptr_t>(displacement);
        if (distance > UINTPTR_MAX - next) return false;
        *targetOut = next + distance;
        return true;
    }
    const uint64_t magnitude = static_cast<uint64_t>(-
        static_cast<int64_t>(displacement));
    if (magnitude > static_cast<uint64_t>(next)) return false;
    *targetOut = next - static_cast<uintptr_t>(magnitude);
    return true;
}

bool CountExecutableDirectCalls(uintptr_t moduleBase, uintptr_t target,
                                size_t* countOut, uint32_t* onlyCallerRvaOut) {
    if (!moduleBase || !target || !countOut || !onlyCallerRvaOut) return false;
    *countOut = 0u;
    *onlyCallerRvaOut = 0u;
    __try {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
        const uintptr_t ntAddress = moduleBase + static_cast<uint32_t>(dos->e_lfanew);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(ntAddress);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
            nt->OptionalHeader.SizeOfImage != kSupportedImageSize) {
            return false;
        }
        const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
        for (uint16_t sectionIndex = 0u; sectionIndex < nt->FileHeader.NumberOfSections;
             ++sectionIndex, ++section) {
            if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0u) continue;
            const uint32_t length = (std::max)(section->Misc.VirtualSize, section->SizeOfRawData);
            if (length < 5u || F8Runtime::ValidateImageRange(
                    section->VirtualAddress, length, nt->OptionalHeader.SizeOfImage) !=
                    F8Runtime::ProfileResult::Supported) {
                continue;
            }
            const uintptr_t start = moduleBase + section->VirtualAddress;
            for (uint32_t offset = 0u; offset <= length - 5u; ++offset) {
                const uint8_t* instruction = reinterpret_cast<const uint8_t*>(start + offset);
                if (instruction[0] != 0xE8u) continue;
                int32_t displacement = 0;
                std::memcpy(&displacement, instruction + 1u, sizeof(displacement));
                uintptr_t resolved = 0u;
                if (!CheckedTargetFromCall(start + offset, displacement, &resolved) || resolved != target) {
                    continue;
                }
                ++*countOut;
                *onlyCallerRvaOut = section->VirtualAddress + offset;
                if (*countOut > 1u) return true;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CollectExecutableDirectCallReturns(uintptr_t moduleBase, uintptr_t target,
                                        uint32_t* returnRvasOut, size_t capacity,
                                        size_t* countOut) {
    if (!moduleBase || !target || !returnRvasOut || capacity == 0u || !countOut) return false;
    *countOut = 0u;
    __try {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
        const uintptr_t ntAddress = moduleBase + static_cast<uint32_t>(dos->e_lfanew);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(ntAddress);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
            nt->OptionalHeader.SizeOfImage != kSupportedImageSize) {
            return false;
        }
        const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
        for (uint16_t sectionIndex = 0u; sectionIndex < nt->FileHeader.NumberOfSections;
             ++sectionIndex, ++section) {
            if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0u) continue;
            const uint32_t length = (std::max)(section->Misc.VirtualSize, section->SizeOfRawData);
            if (length < 5u || F8Runtime::ValidateImageRange(
                    section->VirtualAddress, length, nt->OptionalHeader.SizeOfImage) !=
                    F8Runtime::ProfileResult::Supported) {
                continue;
            }
            const uintptr_t start = moduleBase + section->VirtualAddress;
            for (uint32_t offset = 0u; offset <= length - 5u; ++offset) {
                const uint8_t* instruction = reinterpret_cast<const uint8_t*>(start + offset);
                if (instruction[0] != 0xE8u) continue;
                int32_t displacement = 0;
                std::memcpy(&displacement, instruction + 1u, sizeof(displacement));
                uintptr_t resolved = 0u;
                if (!CheckedTargetFromCall(start + offset, displacement, &resolved) ||
                    resolved != target) {
                    continue;
                }
                if (*countOut < capacity) {
                    returnRvasOut[*countOut] = section->VirtualAddress + offset + 5u;
                }
                ++*countOut;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CountExecutableSignatureMatches(uintptr_t moduleBase, ObserverSignatureTarget target,
                                     size_t* countOut, uint32_t* onlyMatchRvaOut) {
    if (!moduleBase || !countOut || !onlyMatchRvaOut) return false;
    *countOut = 0u;
    *onlyMatchRvaOut = 0u;
    const RelocatedSignatureSpec& spec = LoadedSignatureSpec(target);
    std::array<uint8_t, kDispatchSignature.size()> expected{};
    if (!BuildExpectedLoadedSignature(target, moduleBase, expected.data(), spec.length)) {
        return false;
    }
    __try {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
        const uintptr_t ntAddress = moduleBase + static_cast<uint32_t>(dos->e_lfanew);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(ntAddress);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
            nt->OptionalHeader.SizeOfImage != kSupportedImageSize) {
            return false;
        }
        const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
        for (uint16_t sectionIndex = 0u; sectionIndex < nt->FileHeader.NumberOfSections;
             ++sectionIndex, ++section) {
            if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0u) continue;
            const uint32_t length = (std::max)(section->Misc.VirtualSize, section->SizeOfRawData);
            if (length < spec.length || F8Runtime::ValidateImageRange(
                    section->VirtualAddress, length, nt->OptionalHeader.SizeOfImage) !=
                    F8Runtime::ProfileResult::Supported) {
                continue;
            }
            const uintptr_t start = moduleBase + section->VirtualAddress;
            for (uint32_t offset = 0u; offset <= length - spec.length; ++offset) {
                if (std::memcmp(reinterpret_cast<const void*>(start + offset),
                                expected.data(), spec.length) != 0) {
                    continue;
                }
                ++*countOut;
                *onlyMatchRvaOut = section->VirtualAddress + offset;
                if (*countOut > 1u) return true;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

enum class ProfileValidation : uint8_t {
    Supported = 0,
    InvalidImage,
    OutOfRange,
    SignatureMismatch,
    SignatureNotUnique,
    CallerMismatch,
};

ProfileValidation ValidateObserverProfile(uintptr_t moduleBase) {
    F8Runtime::ExecutableIdentity identity{};
    F8Runtime::ProfileResult profile = F8Runtime::ProfileResult::BadDos;
    __try {
        profile = F8Runtime::ParseExecutableIdentity(
            reinterpret_cast<const uint8_t*>(moduleBase), 0x1000u, &identity);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return ProfileValidation::InvalidImage;
    }
    if (profile != F8Runtime::ProfileResult::Supported ||
        !F8Runtime::IsSupportedExecutable(identity)) {
        return ProfileValidation::InvalidImage;
    }
    for (const auto& range : {
             std::pair<uint32_t, size_t>{RVA_MONSTER_SCRIPT_REGISTRATION, kRegistrationSignature.size()},
             std::pair<uint32_t, size_t>{RVA_MONSTER_SCRIPT_CLEANUP, kCleanupSignature.size()},
             std::pair<uint32_t, size_t>{RVA_MONSTER_AI_DISPATCH, kDispatchSignature.size()},
             std::pair<uint32_t, size_t>{RVA_MONSTER_SCRIPT_REGISTRATION_CALLER, kRegistrationCallerSignature.size()},
             std::pair<uint32_t, size_t>{kDispatchCallerReturnRvas[0] - 5u, 5u},
             std::pair<uint32_t, size_t>{kDispatchCallerReturnRvas[1] - 5u, 5u},
             std::pair<uint32_t, size_t>{kDispatchCallerReturnRvas[2] - 5u, 5u},
             std::pair<uint32_t, size_t>{RVA_MONSTER_ACTOR_LIST_POINTER, sizeof(uint32_t)},
         }) {
        if (F8Runtime::ValidateImageRange(range.first, range.second, identity.sizeOfImage) !=
            F8Runtime::ProfileResult::Supported) {
            return ProfileValidation::OutOfRange;
        }
    }

    if (!ExactLoadedSignature(moduleBase, ObserverSignatureTarget::Registration) ||
        !ExactLoadedSignature(moduleBase, ObserverSignatureTarget::Cleanup) ||
        !ExactLoadedSignature(moduleBase, ObserverSignatureTarget::Dispatch) ||
        !ExactBytes(moduleBase + RVA_MONSTER_SCRIPT_REGISTRATION_CALLER,
                    kRegistrationCallerSignature.data(), kRegistrationCallerSignature.size())) {
        return ProfileValidation::SignatureMismatch;
    }
    size_t callerCount = 0u;
    uint32_t callerRva = 0u;
    if (!CountExecutableDirectCalls(
            moduleBase, moduleBase + RVA_MONSTER_SCRIPT_REGISTRATION,
            &callerCount, &callerRva) || callerCount != 1u ||
        callerRva != RVA_MONSTER_SCRIPT_REGISTRATION_CALLER) {
        return ProfileValidation::CallerMismatch;
    }
    size_t signatureCount = 0u;
    uint32_t signatureRva = 0u;
    if (!CountExecutableSignatureMatches(
            moduleBase, ObserverSignatureTarget::Dispatch,
            &signatureCount, &signatureRva) || signatureCount != 1u ||
        signatureRva != RVA_MONSTER_AI_DISPATCH) {
        return ProfileValidation::SignatureNotUnique;
    }
    std::array<uint32_t, kDispatchCallerReturnRvas.size()> dispatchCallerReturns{};
    size_t dispatchCallerCount = 0u;
    if (!CollectExecutableDirectCallReturns(
            moduleBase, moduleBase + RVA_MONSTER_AI_DISPATCH,
            dispatchCallerReturns.data(), dispatchCallerReturns.size(),
            &dispatchCallerCount) ||
        !ValidateDispatchCallerReturns(dispatchCallerReturns.data(), dispatchCallerCount)) {
        return ProfileValidation::CallerMismatch;
    }
    return ProfileValidation::Supported;
}

const char* ProfileValidationName(ProfileValidation validation) {
    switch (validation) {
        case ProfileValidation::Supported: return "supported";
        case ProfileValidation::InvalidImage: return "invalid-image";
        case ProfileValidation::OutOfRange: return "out-of-range";
        case ProfileValidation::SignatureMismatch: return "signature-mismatch";
        case ProfileValidation::SignatureNotUnique: return "signature-not-unique";
        case ProfileValidation::CallerMismatch: return "caller-mismatch";
        default: return "unknown";
    }
}

bool EnvironmentEnabled(const char* name) {
    char value[16] = {};
    const DWORD length = GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value)));
    return length > 0u && length < sizeof(value) &&
           (value[0] == '1' || value[0] == 'y' || value[0] == 'Y' ||
            value[0] == 't' || value[0] == 'T');
}

void EmitSnapshot(void*, SnapshotPhase phase, const Snapshot& snapshot) {
    const char* phaseName = phase == SnapshotPhase::BeforeRegistration ? "before" : "after";
    if (phase == SnapshotPhase::AfterRegistration) {
        // Only the game-owned post-registration image may feed dispatch correlation. The cache
        // publishes hashes/lengths as scalar identity and never retains actor or script pointers.
        MonsterAiDispatchTelemetry::PublishSnapshot(&g_dispatchTelemetry, snapshot);
    }
    for (size_t slot = 0u; slot < snapshot.slots.size(); ++slot) {
        char fields[256] = {};
        if (MonsterAiObserver::FormatSlotLine(snapshot, slot, fields, sizeof(fields))) {
            ObserverLog("[ffx-hooks] MonsterAiObserver registration-%s %s\n", phaseName, fields);
        }
    }
}

void EmitTeardown(void*, uint32_t generation, uint32_t threadId) {
    MonsterAiDispatchTelemetry::RetireGeneration(&g_dispatchTelemetry, generation);
    ObserverLog("[ffx-hooks] MonsterAiObserver teardown generation=%u thread=%u\n",
                generation, threadId);
}

void EmitDispatch(void*, const MonsterAiDispatchTelemetry::DispatchEvent& event) {
    ObserverLog(
        "[ffx-hooks] MonsterAiObserver dispatch generation=%u thread=%u serial=%u "
        "slot=%d monster_id=0x%04X command=0x%04X target=0x%08X force=%d "
        "queue=%d reason=%s proposal=%u proposed_command=0x%04X\n",
        event.generation, event.threadId, event.serial, event.actorSlot,
        static_cast<unsigned>(event.monsterRawId), static_cast<unsigned>(event.command),
        event.targetMask, event.force, event.queueReadback,
        MonsterAiShadow::DecisionReasonText(event.reason), event.proposalAvailable ? 1u : 0u,
        static_cast<unsigned>(event.proposedCommand));
}

void InvokeRegistrationOriginal(void*) {
    const uintptr_t original = g_detourOwner.registrationOriginal;
    if (original) reinterpret_cast<ScriptLifecycleFn>(original)();
}

void InvokeCleanupOriginal(void*) {
    const uintptr_t original = g_detourOwner.cleanupOriginal;
    if (original) reinterpret_cast<ScriptLifecycleFn>(original)();
}

int32_t InvokeDispatchOriginal(void*, int32_t actorIndex, int32_t commandStack32,
                               uint32_t targetMask, int32_t force, int32_t n64) {
    const uintptr_t original = g_detourOwner.dispatchOriginal;
    return original
        ? reinterpret_cast<DispatchFn>(original)(
              actorIndex, commandStack32, targetMask, force, n64)
        : 0;
}

void __cdecl RegistrationShim() {
    MonsterAiObserver::CallbackLease callback(&g_lifecycle);
    uint32_t actorList32 = 0u;
    (void)ReadRuntimeU32(g_moduleBase + RVA_MONSTER_ACTOR_LIST_POINTER, &actorList32);
    const ReadOnlyMemory memory{nullptr, &ReadProcessBytes};
    const EventSink sink{nullptr, &EmitSnapshot, &EmitTeardown};
    MonsterAiObserver::ObserveRegistration(
        callback, memory, static_cast<uintptr_t>(actorList32), GetCurrentThreadId(),
        sink, OriginalCall{nullptr, &InvokeRegistrationOriginal});
}

void __cdecl CleanupShim() {
    MonsterAiObserver::CallbackLease callback(&g_lifecycle);
    const EventSink sink{nullptr, &EmitSnapshot, &EmitTeardown};
    MonsterAiObserver::ObserveCleanup(
        callback, GetCurrentThreadId(), sink,
        OriginalCall{nullptr, &InvokeCleanupOriginal});
}

int32_t __cdecl DispatchShim(int32_t actorIndex, int32_t commandStack32,
                             uint32_t targetMask, int32_t force, int32_t n64) {
    MonsterAiObserver::CallbackLease callback(&g_lifecycle);
    // CallbackLease is deliberately the first C++ statement. A CPU already redirected into this
    // machine prologue during teardown must find both the retained trampoline and closed admission.
    const uint32_t callerReturnPreferredVa = NormalizeCallerReturn(
        g_moduleBase, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    const MonsterAiDispatchTelemetry::DispatchEventSink sink{nullptr, &EmitDispatch};
    const MonsterAiDispatchTelemetry::DispatchOriginalCall original{
        nullptr, &InvokeDispatchOriginal};
    return ObserveDispatchAdapter(
        callback, &g_dispatchTelemetry, GetCurrentThreadId(), callerReturnPreferredVa,
        actorIndex, commandStack32, targetMask, force, n64, sink, original);
}

bool DetourCreate(void*, uintptr_t target, uintptr_t detour, uintptr_t* originalOut) {
    if (!originalOut) return false;
    void* original = nullptr;
    const MH_STATUS status = MH_CreateHook(
        reinterpret_cast<void*>(target), reinterpret_cast<void*>(detour), &original);
    if (status != MH_OK) return false;
    *originalOut = reinterpret_cast<uintptr_t>(original);
    return true;
}

bool DetourRemove(void*, uintptr_t target) {
    const MH_STATUS status = MH_RemoveHook(reinterpret_cast<void*>(target));
    return status == MH_OK || status == MH_ERROR_NOT_CREATED;
}

DetourIo RuntimeDetours() {
    return {nullptr, &DetourCreate, &DetourRemove};
}

void DrainPause(void*, uint32_t milliseconds) {
    Sleep(milliseconds);
}

DrainIo RuntimeDrain() {
    // This bounded wait is owned only by normal-context install rollback/removal. DllMain never
    // calls it; detach closes admission through the lock-free request API and returns immediately.
    return {nullptr, &DrainPause, 1000u};
}

#endif // FFXHOOKS_HAVE_POLYHOOK

} // namespace

bool F7AiSwap_Install(uintptr_t moduleBase, void (*log)(const char*)) {
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if (F7AiSwap_Status() == F7AiObserverStatus::Observing) return true;
    if (g_detourOwner.registrationCreated || g_detourOwner.cleanupCreated ||
        g_detourOwner.dispatchCreated ||
        g_detourOwner.applyAttempted || g_detourOwner.retainedInert ||
        g_detourOwner.coordinatorPoisoned || g_detourOwner.active) {
        const bool retained = g_detourOwner.retainedInert;
        PublishStatus(retained
            ? F7AiObserverStatus::RetainedInert
            : F7AiObserverStatus::Stopping);
        ObserverLog("[ffx-hooks] MonsterAiObserver %s (unresolved detour ownership)\n",
                    retained ? "INERT (TRAMPOLINES RETAINED)" : "STOPPING");
        return false;
    }
    g_moduleBase = moduleBase;
    g_log = log;
    MonsterAiObserver::InitializeLifecycle(&g_lifecycle);
    MonsterAiDispatchTelemetry::Initialize(&g_dispatchTelemetry);
    g_detourOwner = DetourOwner{};

    const bool requested = Config::CheckEnabled(
        "f7.aiswap", "FFXHOOKS_ENABLE_F7_AISWAP", "f7_aiswap.flag", false);
    if (!requested) {
        PublishStatus(F7AiObserverStatus::Off);
        ObserverLog("[ffx-hooks] MonsterAiObserver OFF (f7.aiswap is disabled)\n");
        return false;
    }
    if (!moduleBase) {
        PublishStatus(F7AiObserverStatus::Unavailable);
        ObserverLog("[ffx-hooks] MonsterAiObserver UNAVAILABLE (module base is null)\n");
        return false;
    }

    const ProfileValidation validation = ValidateObserverProfile(moduleBase);
    if (validation != ProfileValidation::Supported) {
        PublishStatus(F7AiObserverStatus::Unavailable);
        ObserverLog("[ffx-hooks] MonsterAiObserver UNAVAILABLE validation=%s\n",
                    ProfileValidationName(validation));
        return false;
    }
    ObserverLog("[ffx-hooks] MonsterAiObserver profile/signatures/callers validated "
                "(dispatch signature unique; callers=3)\n");
    if (EnvironmentEnabled("FFXHOOKS_VALIDATE_ONLY")) {
        MonsterAiObserver::RequestStop(&g_lifecycle);
        PublishStatus(F7AiObserverStatus::ObservePendingRestart);
        ObserverLog("[ffx-hooks] MonsterAiObserver OBSERVE PENDING RESTART (validation-only)\n");
        return true;
    }

    const MinHookBatch::InitializationResult initialize =
        MinHookBatch::EnsureProcessInitialized();
    if (initialize != MinHookBatch::InitializationResult::Ready) {
        MonsterAiObserver::RequestStop(&g_lifecycle);
        PublishStatus(F7AiObserverStatus::Unavailable);
        ObserverLog("[ffx-hooks] MonsterAiObserver UNAVAILABLE "
                    "(shared MinHook init=%s(%u))\n",
                    InitializationResultName(initialize), static_cast<unsigned>(initialize));
        return false;
    }
    const MonsterAiObserver::InstallResult installed = InstallObserverAdapter(
        RuntimeDetours(), &MinHookBatch::ProcessCoordinator(),
        MinHookBatch::RuntimeBatchIo(), RuntimeDrain(), &g_lifecycle,
        moduleBase + RVA_MONSTER_SCRIPT_REGISTRATION,
        reinterpret_cast<uintptr_t>(&RegistrationShim),
        moduleBase + RVA_MONSTER_SCRIPT_CLEANUP,
        reinterpret_cast<uintptr_t>(&CleanupShim),
        moduleBase + RVA_MONSTER_AI_DISPATCH,
        reinterpret_cast<uintptr_t>(&DispatchShim),
        &g_detourOwner);
    if (installed != MonsterAiObserver::InstallResult::Installed) {
        MonsterAiObserver::RequestStop(&g_lifecycle);
        const F7AiObserverStatus failureStatus =
            ResolveInstallFailureStatus(installed, g_detourOwner);
        PublishStatus(failureStatus);
        ObserverLog("[ffx-hooks] MonsterAiObserver %s transactional-install=%s(%u)\n",
                    failureStatus == F7AiObserverStatus::RetainedInert
                        ? "INERT (TRAMPOLINES RETAINED)"
                        : (failureStatus == F7AiObserverStatus::Stopping
                               ? "STOPPING"
                               : "UNAVAILABLE"),
                    InstallResultName(installed),
                    static_cast<unsigned>(installed));
        return false;
    }
    PublishStatus(F7AiObserverStatus::Observing);
    ObserverLog("[ffx-hooks] MonsterAiObserver OBSERVING "
                "(registration/cleanup/dispatch telemetry; mutation whitelist=0)\n");
    return true;
#else
    (void)moduleBase;
    (void)log;
    return false;
#endif
}

void F7AiSwap_ReportSetupFailure(void (*log)(const char*)) {
#ifdef FFXHOOKS_HAVE_POLYHOOK
    const bool requested = Config::CheckEnabled(
        "f7.aiswap", "FFXHOOKS_ENABLE_F7_AISWAP", "f7_aiswap.flag", false);
    if (!requested) return;
    const F7AiObserverStatus setupStatus =
        ResolveSetupFailureStatus(RuntimeStatus(), requested);
    if (setupStatus != F7AiObserverStatus::Unavailable) return;

    // Global initialization has already failed in the worker. This path only publishes truth for
    // the requested observer; retrying or touching MinHook here would violate fail-closed startup.
    g_log = log;
    PublishStatus(F7AiObserverStatus::Unavailable);
    ObserverLog("[ffx-hooks] MonsterAiObserver UNAVAILABLE "
                "(shared MinHook setup failed before observer install)\n");
#else
    (void)log;
#endif
}

void F7AiSwap_RequestStop() {
#ifdef FFXHOOKS_HAVE_POLYHOOK
    MonsterAiObserver::RequestStop(&g_lifecycle);
    // DllMain calls this path. RuntimeStatus is an atomic read only; the public status resolver may
    // consult configuration and must remain outside loader lock.
    if (RuntimeStatus() == F7AiObserverStatus::Observing) {
        PublishStatus(F7AiObserverStatus::Stopping);
    }
#endif
}

void F7AiSwap_Remove() {
#ifdef FFXHOOKS_HAVE_POLYHOOK
    const TeardownResult removed = RemoveObserverAdapter(
        RuntimeDetours(), &MinHookBatch::ProcessCoordinator(),
        MinHookBatch::RuntimeBatchIo(), RuntimeDrain(), &g_lifecycle,
        &g_detourOwner);
    if (removed == TeardownResult::RetainedInert ||
        (removed == TeardownResult::CoordinatorPoisoned &&
         g_detourOwner.retainedInert)) {
        // The targets are restored and observation is closed, but an uncounted CPU may still be
        // paused before CallbackLease. Keep module/log/owner/trampolines alive until process exit.
        PublishStatus(F7AiObserverStatus::RetainedInert);
        ObserverLog("[ffx-hooks] MonsterAiObserver INERT "
                    "(hooks disabled; trampolines retained until process exit%s)\n",
                    removed == TeardownResult::CoordinatorPoisoned
                        ? "; MinHook coordinator poisoned"
                        : "");
        return;
    }
    if (removed != TeardownResult::Removed) {
        PublishStatus(F7AiObserverStatus::Stopping);
        ObserverLog("[ffx-hooks] MonsterAiObserver STOPPING teardown=%s(%u)\n",
                    TeardownResultName(removed), static_cast<unsigned>(removed));
        return;
    }
    ObserverLog("[ffx-hooks] MonsterAiObserver OFF (teardown complete)\n");
    PublishStatus(F7AiObserverStatus::Off);
    g_moduleBase = 0u;
    g_log = nullptr;
#endif
}

bool F7AiSwap_IsEnabled() {
    return F7AiSwap_Status() == F7AiObserverStatus::Observing;
}

F7AiObserverStatus F7AiSwap_Status() {
#ifdef FFXHOOKS_HAVE_POLYHOOK
    const F7AiObserverStatus runtimeStatus = RuntimeStatus();
    const bool requested = Config::CheckEnabled(
        "f7.aiswap", "FFXHOOKS_ENABLE_F7_AISWAP", "f7_aiswap.flag", false);
    return ResolveRequestedStatus(runtimeStatus, requested);
#else
    return F7AiObserverStatus::Unavailable;
#endif
}

const char* F7AiSwap_StatusName() {
    switch (F7AiSwap_Status()) {
        case F7AiObserverStatus::Off: return "OFF";
        case F7AiObserverStatus::ObservePendingRestart: return "OBSERVE PENDING RESTART";
        case F7AiObserverStatus::Observing: return "OBSERVING";
        case F7AiObserverStatus::RetainedInert: return "INERT (RESTART REQUIRED)";
        case F7AiObserverStatus::Unavailable: return "UNAVAILABLE";
        case F7AiObserverStatus::Stopping: return "STOPPING";
        default: return "UNAVAILABLE";
    }
}

const char* F7AiSwap_DetailText() {
    return "Registration and dispatch telemetry only; no compatible swap pair is validated.";
}

#ifdef FFXHOOKS_TESTING
MonsterAiObserver::InstallResult F7AiSwap_TestInstallAdapter(
    const MonsterAiObserver::DetourIo& detours,
    MinHookBatch::Coordinator* coordinator,
    const MinHookBatch::BatchIo& batchIo,
    const MonsterAiObserver::DrainIo& drain,
    MonsterAiObserver::LifecycleState* lifecycle,
    uintptr_t registrationTarget, uintptr_t registrationDetour,
    uintptr_t cleanupTarget, uintptr_t cleanupDetour,
    uintptr_t dispatchTarget, uintptr_t dispatchDetour,
    MonsterAiObserver::DetourOwner* owner) {
    return InstallObserverAdapter(
        detours, coordinator, batchIo, drain, lifecycle,
        registrationTarget, registrationDetour,
        cleanupTarget, cleanupDetour,
        dispatchTarget, dispatchDetour, owner);
}

MonsterAiObserver::TeardownResult F7AiSwap_TestRemoveAdapter(
    const MonsterAiObserver::DetourIo& detours,
    MinHookBatch::Coordinator* coordinator,
    const MinHookBatch::BatchIo& batchIo,
    const MonsterAiObserver::DrainIo& drain,
    MonsterAiObserver::LifecycleState* lifecycle,
    MonsterAiObserver::DetourOwner* owner) {
    return RemoveObserverAdapter(
        detours, coordinator, batchIo, drain, lifecycle, owner);
}

F7AiObserverStatus F7AiSwap_TestResolveStatus(
    F7AiObserverStatus runtimeStatus, bool requested) {
    return ResolveRequestedStatus(runtimeStatus, requested);
}

F7AiObserverStatus F7AiSwap_TestResolveSetupFailureStatus(
    F7AiObserverStatus runtimeStatus, bool requested) {
    return ResolveSetupFailureStatus(runtimeStatus, requested);
}

F7AiObserverStatus F7AiSwap_TestResolveInstallFailureStatus(
    MonsterAiObserver::InstallResult result,
    const MonsterAiObserver::DetourOwner& owner) {
    return ResolveInstallFailureStatus(result, owner);
}

bool F7AiSwap_TestBuildLoadedSignature(
    bool registration, uintptr_t moduleBase, uint8_t* expectedOut, size_t expectedLength) {
    return BuildExpectedLoadedSignature(
        registration ? ObserverSignatureTarget::Registration : ObserverSignatureTarget::Cleanup,
        moduleBase, expectedOut, expectedLength);
}

bool F7AiSwap_TestValidateLoadedSignature(
    bool registration, uintptr_t moduleBase, const uint8_t* observed, size_t observedLength) {
    return ValidateLoadedSignature(
        registration ? ObserverSignatureTarget::Registration : ObserverSignatureTarget::Cleanup,
        moduleBase, observed, observedLength);
}

bool F7AiSwap_TestBuildDispatchLoadedSignature(
    uintptr_t moduleBase, uint8_t* expectedOut, size_t expectedLength) {
    return BuildExpectedLoadedSignature(
        ObserverSignatureTarget::Dispatch, moduleBase, expectedOut, expectedLength);
}

bool F7AiSwap_TestValidateDispatchLoadedSignature(
    uintptr_t moduleBase, const uint8_t* observed, size_t observedLength) {
    return ValidateLoadedSignature(
        ObserverSignatureTarget::Dispatch, moduleBase, observed, observedLength);
}

bool F7AiSwap_TestValidateDispatchCallerReturns(
    const uint32_t* callerReturnRvas, size_t count) {
    return ValidateDispatchCallerReturns(callerReturnRvas, count);
}

uint32_t F7AiSwap_TestNormalizeCallerReturn(uintptr_t moduleBase, uintptr_t liveReturn) {
    return NormalizeCallerReturn(moduleBase, liveReturn);
}

int32_t F7AiSwap_TestObserveDispatchAdapter(
    MonsterAiObserver::LifecycleState* lifecycle,
    MonsterAiDispatchTelemetry::DispatchTelemetryState* telemetry,
    uint32_t threadId, uint32_t callerReturnPreferredVa,
    int32_t actorIndex, int32_t commandStack32, uint32_t targetMask,
    int32_t force, int32_t n64,
    const MonsterAiDispatchTelemetry::DispatchEventSink& sink,
    const MonsterAiDispatchTelemetry::DispatchOriginalCall& original) {
    MonsterAiObserver::CallbackLease callback(lifecycle);
    return ObserveDispatchAdapter(
        callback, telemetry, threadId, callerReturnPreferredVa,
        actorIndex, commandStack32, targetMask, force, n64, sink, original);
}
#endif

} // namespace FfxHooks
