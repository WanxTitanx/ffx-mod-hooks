#include "UnXBoosterHook.h"

#include "F8FlagCatalog.h"
#include "F8RuntimeCore.h"
#include "../shared/Config.h"
#include "../shared/ffx_addresses.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

using FfxHooks::F8RuntimeAvailability;
using FfxHooks::F8Runtime::AtomicLifecycle;
using FfxHooks::F8Runtime::ByteIo;
using FfxHooks::F8Runtime::ByteTransition;
using FfxHooks::F8Runtime::ExecutableIdentity;
using FfxHooks::F8Runtime::OwnedByte;
using FfxHooks::F8Runtime::OwnershipState;
using FfxHooks::F8Runtime::ProducerState;
using FfxHooks::F8Runtime::ProfileResult;
using FfxHooks::F8Runtime::RewardBindingResult;
using FfxHooks::F8Runtime::RewardHookIo;
using FfxHooks::F8Runtime::RewardHookOwnership;
using FfxHooks::F8Runtime::RewardHookResult;
using FfxHooks::F8Runtime::RewardHookState;
using FfxHooks::F8Runtime::RewardKind;
using FfxHooks::F8Runtime::RewardScalarIo;
using FfxHooks::F8Runtime::RuntimeLifecycle;
using FfxHooks::F8Runtime::TickGate;
using FfxHooks::F8Runtime::WriteEffect;
using FfxHooks::F8Runtime::WriteResult;

constexpr size_t kPeHeaderProbeSize = 0x1000u;
constexpr size_t kOwnedDebugFieldCount = 7;
constexpr size_t kRewardBindingCount = 2;
using FfxHooks::F8Runtime::kRewardPatchSize;
using FfxHooks::F8Runtime::kRewardStubSize;
static_assert(kOwnedDebugFieldCount == 7,
              "AP and Gil are independent compound bindings outside the generic Debug loop");
static_assert(kRewardBindingCount == 2, "AP and Gil own exactly two reward hook sites");
static_assert(FFX_PARTY_SLOT_COUNT == FfxHooks::F8Runtime::kApSlotCount,
              "address ledger and AP transform must cover the same seven slots");

enum class AdapterState : uint32_t { Unpublished = 0, Validated, Unsupported };
enum class MemoryAccess : uint8_t { ReadOnly = 0, ReadWrite, Execute };
enum class FailureReason : uint8_t {
    None = 0,
    Read,
    Write,
    Readback,
    Conflict,
    BattleGate,
    MemorySpan,
    InvalidConfiguration,
    HookInstall,
    Protection,
};
enum class EdgeState : uint8_t { Applied = 0, Restored, Pending, Conflict };

struct DebugBinding {
    const char* key;
    uint32_t offset;
    uintptr_t address = 0;
    const FfxHooks::F8FlagSpec* flag = nullptr;
    OwnedByte owned{};
    FailureReason failure = FailureReason::None;
    bool hasEdge = false;
    bool edgeEffective = false;
    FfxHooks::Config::BoolSource edgeSource = FfxHooks::Config::BoolSource::DefaultValue;
    EdgeState edgeState = EdgeState::Pending;
    uint8_t edgeReadback = 0;
};

struct RewardBinding {
    const char* key;
    RewardKind kind;
    uint32_t debugOffset;
    uintptr_t gateAddress = 0;
    const FfxHooks::F8FlagSpec* flag = nullptr;
    // Ordinary edits change this aligned data cell only. The RX stub reads it indirectly, so no
    // executable byte is self-modified after the one-time safe-point installation.
    alignas(4) volatile LONG scalar = 100;
    RewardHookState hook{};
    OwnedByte gateOwner{};
    FailureReason failure = FailureReason::None;
    bool hasEdge = false;
    bool edgeEffective = false;
    FfxHooks::Config::BoolSource edgeSource = FfxHooks::Config::BoolSource::DefaultValue;
    EdgeState edgeState = EdgeState::Pending;
    uint8_t edgeGateReadback = 0;
    bool edgeHasScalarReadback = false;
    int32_t edgeScalarReadback = 0;
    bool prepared = false;
};

struct ValidatedAdapter {
    uintptr_t moduleBase = 0;
    uint32_t sizeOfImage = 0;
    uintptr_t debugBase = 0;
    uintptr_t battleActive = 0;
    std::array<uintptr_t, FfxHooks::F8Runtime::kApSlotCount> inParty{};
    uintptr_t participation = 0;
    uintptr_t earn = 0;
    std::array<uintptr_t, FfxHooks::F8Runtime::kApSlotCount> participationBytes{};
    std::array<uintptr_t, FfxHooks::F8Runtime::kApSlotCount> earnBytes{};
};

std::array<DebugBinding, kOwnedDebugFieldCount> g_debugBindings = {{
    {"boosters.permanent_sensor", FFX_DEBUG_PERMANENT_SENSOR_OFFSET},
    {"cheats.invincible_party", FFX_DEBUG_INVINCIBLE_PARTY_OFFSET},
    {"cheats.invincible_enemies", FFX_DEBUG_INVINCIBLE_ENEMIES_OFFSET},
    {"cheats.always_overdrive", FFX_DEBUG_ALWAYS_OVERDRIVE_OFFSET},
    {"cheats.always_critical", FFX_DEBUG_ALWAYS_CRITICAL_OFFSET},
    {"cheats.damage_value", FFX_DEBUG_ALWAYS_DEAL_99999_OFFSET},
    {"cheats.always_rare_drop", FFX_DEBUG_ALWAYS_RARE_REWARD_OFFSET},
}};

std::array<RewardBinding, kRewardBindingCount> g_rewardBindings = {{
    {"cheats.ap_100x", RewardKind::Ap, FFX_DEBUG_AP_100X_OFFSET},
    {"cheats.gil_100x", RewardKind::Gil, FFX_DEBUG_GIL_100X_OFFSET},
}};

ValidatedAdapter g_adapter{};
const FfxHooks::F8FlagSpec* g_apFlag = nullptr;
FfxHooks::BoosterLogFn g_log = nullptr;
std::atomic<uint32_t> g_adapterState{static_cast<uint32_t>(AdapterState::Unpublished)};
// Acquire-observed state 2 is the publication barrier before readers touch non-atomic g_tickGate;
// state 0 is unarmed and state 1 reserves its single-threaded initialization.
std::atomic<uint32_t> g_tickGatePublication{0};
AtomicLifecycle g_lifecycle{};
TickGate g_tickGate{};
SRWLOCK g_rewardPatchLock = SRWLOCK_INIT;
FailureReason g_apFailure = FailureReason::None;
bool g_apHasEdge = false;
bool g_apEdgeEffective = false;
FfxHooks::Config::BoolSource g_apEdgeSource = FfxHooks::Config::BoolSource::DefaultValue;
EdgeState g_apEdgeState = EdgeState::Pending;
uint8_t g_apEdgeReadback = 0;

const char* FailureReasonName(FailureReason reason) {
    switch (reason) {
        case FailureReason::Read: return "read-failed";
        case FailureReason::Write: return "write-failed";
        case FailureReason::Readback: return "readback-failed";
        case FailureReason::Conflict: return "ownership-conflict";
        case FailureReason::BattleGate: return "battle-gate-closed";
        case FailureReason::MemorySpan: return "memory-span-invalid";
        case FailureReason::InvalidConfiguration: return "invalid-configuration";
        case FailureReason::HookInstall: return "hook-install-failed";
        case FailureReason::Protection: return "protection-failed";
        default: return "none";
    }
}

const char* EdgeStateName(EdgeState state) {
    switch (state) {
        case EdgeState::Applied: return "applied";
        case EdgeState::Restored: return "restored";
        case EdgeState::Pending: return "pending";
        case EdgeState::Conflict: return "conflict";
        default: return "pending";
    }
}

void BoostLog(const char* format, ...) {
    if (!g_log || !format) return;
    char line[512] = {};
    va_list args;
    va_start(args, format);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
    va_end(args);
    g_log(line);
}

bool IsReadableProtection(DWORD protection) {
    switch (protection & 0xFFu) {
        case PAGE_READONLY:
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return true;
        default:
            return false;
    }
}

bool IsWritableProtection(DWORD protection) {
    switch (protection & 0xFFu) {
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return true;
        default:
            return false;
    }
}

bool IsExecutableProtection(DWORD protection) {
    switch (protection & 0xFFu) {
        case PAGE_EXECUTE:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return true;
        default:
            return false;
    }
}

bool ValidateMappedSpan(uintptr_t moduleBase, uintptr_t address, size_t length,
                        MemoryAccess expectedAccess) {
    if (!moduleBase || !address || length == 0 ||
        address > (std::numeric_limits<uintptr_t>::max)() - length) {
        return false;
    }
    const uintptr_t spanEnd = address + length;
    uintptr_t cursor = address;
    while (cursor < spanEnd) {
        MEMORY_BASIC_INFORMATION memory = {};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &memory, sizeof(memory)) !=
            sizeof(memory)) {
            return false;
        }
        const uintptr_t regionBase = reinterpret_cast<uintptr_t>(memory.BaseAddress);
        if (memory.RegionSize == 0 ||
            regionBase > (std::numeric_limits<uintptr_t>::max)() - memory.RegionSize) {
            return false;
        }
        const uintptr_t regionEnd = regionBase + memory.RegionSize;
        if (cursor < regionBase || cursor >= regionEnd ||
            memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE ||
            memory.AllocationBase != reinterpret_cast<void*>(moduleBase) ||
            (memory.Protect & PAGE_GUARD) != 0 ||
            (memory.Protect & PAGE_NOACCESS) != 0) {
            return false;
        }
        const bool protectionMatches =
            expectedAccess == MemoryAccess::ReadOnly
                ? IsReadableProtection(memory.Protect)
                : expectedAccess == MemoryAccess::ReadWrite
                    ? IsReadableProtection(memory.Protect) && IsWritableProtection(memory.Protect)
                    : IsExecutableProtection(memory.Protect);
        if (!protectionMatches) return false;
        cursor = regionEnd < spanEnd ? regionEnd : spanEnd;
    }
    return true;
}

bool AddressFromRva(uintptr_t moduleBase, uint32_t sizeOfImage, uint32_t rva,
                    size_t length, uintptr_t* addressOut) {
    if (!addressOut ||
        FfxHooks::F8Runtime::ValidateImageRange(rva, length, sizeOfImage) !=
            ProfileResult::Supported ||
        moduleBase > (std::numeric_limits<uintptr_t>::max)() - rva) {
        return false;
    }
    *addressOut = moduleBase + rva;
    return true;
}

bool CheckedRvaAdd(uint32_t baseRva, size_t offset, uint32_t* rvaOut) {
    if (!rvaOut || offset > (std::numeric_limits<uint32_t>::max)() ||
        baseRva > (std::numeric_limits<uint32_t>::max)() - static_cast<uint32_t>(offset)) {
        return false;
    }
    *rvaOut = baseRva + static_cast<uint32_t>(offset);
    return true;
}

bool GuardedCopy(void* destination, const void* source, size_t length) {
    __try {
        memcpy(destination, source, length);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool GuardedReadByte(void*, uintptr_t address, uint8_t* valueOut) {
    if (!address || !valueOut) return false;
    __try {
        *valueOut = *reinterpret_cast<volatile const uint8_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

WriteResult GuardedWriteByte(void*, uintptr_t address, uint8_t value) {
    if (!address) return {WriteEffect::NotTouched};
    __try {
        *reinterpret_cast<volatile uint8_t*>(address) = value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return {WriteEffect::MayHaveChanged};
    }
    return {WriteEffect::MayHaveChanged};
}

bool GuardedStoreByte(uintptr_t address, uint8_t value) {
    if (!address) return false;
    __try {
        *reinterpret_cast<volatile uint8_t*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

ByteIo RuntimeByteIo() {
    return {nullptr, &GuardedReadByte, &GuardedWriteByte};
}

bool GuardedReadSpan(void*, uintptr_t address, uint8_t* bytes, size_t length) {
    return address && bytes && length != 0 &&
           GuardedCopy(bytes, reinterpret_cast<const void*>(address), length);
}

bool RewardAllocateWritable(void*, size_t length, uintptr_t* addressOut) {
    if (!addressOut || length != kRewardStubSize) return false;
    void* allocation = VirtualAlloc(nullptr, kRewardStubSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!allocation) return false;
    *addressOut = reinterpret_cast<uintptr_t>(allocation);
    return true;
}

FfxHooks::F8Runtime::MutationReport RewardWriteWritable(
    void*, uintptr_t address, const uint8_t* bytes, size_t length) {
    if (!address || !bytes || length != kRewardStubSize) {
        return {FfxHooks::F8Runtime::MutationEffect::NotTouched, false};
    }
    __try {
        memcpy(reinterpret_cast<void*>(address), bytes, length);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return {FfxHooks::F8Runtime::MutationEffect::MayHaveChanged, false};
    }
    return {FfxHooks::F8Runtime::MutationEffect::MayHaveChanged, false};
}

bool RewardProtectExecuteRead(void*, uintptr_t address, size_t length) {
    if (!address || length != kRewardStubSize) return false;
    DWORD prior = 0;
    // W^X is deliberate: the stub begins RW/NX and becomes RX/RO before any site can target it.
    return VirtualProtect(reinterpret_cast<void*>(address), length, PAGE_EXECUTE_READ, &prior) != FALSE;
}

bool RewardConfirmBattleInactive(void*) {
    uint8_t battleActive = 1;
    // The earlier frame sample admits stub preparation only. Re-reading here closes the window in
    // which battle code could become active before an install or restoration mutates its page.
    return GuardedReadByte(nullptr, g_adapter.battleActive, &battleActive) && battleActive == 0;
}

bool RewardBeginCodeWrite(
    void*, uintptr_t address, size_t length,
    FfxHooks::F8Runtime::PatchProtectionToken* token) {
    if (!address || length != kRewardPatchSize || !token || token->active) return false;
    DWORD prior = 0;
    if (!VirtualProtect(
            reinterpret_cast<void*>(address), length, PAGE_EXECUTE_READWRITE, &prior)) {
        return false;
    }
    *token = {address, length, prior, true};
    if (!FfxHooks::F8Runtime::IsRewardPriorProtectionAdmitted(
            IsExecutableProtection(prior), IsWritableProtection(prior))) {
        // AP and Gil share one image page. Adopting an already-writable or non-executable prior
        // state would make the peer's later restore ambiguous, so fail closed and immediately
        // discharge the token when possible. A failed discharge stays active for Present recovery.
        DWORD ignored = 0;
        if (VirtualProtect(reinterpret_cast<void*>(address), length, prior, &ignored)) {
            token->active = false;
        }
        return false;
    }
    return true;
}

FfxHooks::F8Runtime::MutationReport RewardWriteIfEqual(
    void*, uintptr_t address, const uint8_t* expected,
    const uint8_t* desired, size_t length) {
    if (!address || !expected || !desired || length != kRewardPatchSize) {
        return {FfxHooks::F8Runtime::MutationEffect::NotTouched, false};
    }
    FfxHooks::F8Runtime::MutationReport result = {
        FfxHooks::F8Runtime::MutationEffect::NotTouched, false};
    __try {
        // Battle-inactive admission proves the only game caller cannot enter this function. The
        // outer transaction lock serializes AP/Gil from protection acquisition through readback;
        // the complete compare still catches a third-party race without adopting its bytes.
        if (memcmp(reinterpret_cast<const void*>(address), expected, length) == 0) {
            memcpy(reinterpret_cast<void*>(address), desired, length);
            result.effect = FfxHooks::F8Runtime::MutationEffect::MayHaveChanged;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result.effect = FfxHooks::F8Runtime::MutationEffect::MayHaveChanged;
    }
    return result;
}

bool RewardFlush(void*, uintptr_t address, size_t length) {
    return address && length != 0 &&
           FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void*>(address), length) != FALSE;
}

bool RewardEndCodeWrite(
    void*, FfxHooks::F8Runtime::PatchProtectionToken* token) {
    if (!token || !token->active || !token->address || token->length != kRewardPatchSize) {
        return false;
    }
    DWORD ignored = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(token->address), token->length,
                        token->originalProtection, &ignored)) {
        return false;
    }
    token->active = false;
    return true;
}

bool RewardFreeAllocation(void*, uintptr_t address, size_t length) {
    return address && length == kRewardStubSize &&
           VirtualFree(reinterpret_cast<void*>(address), 0, MEM_RELEASE) != FALSE;
}

RewardHookIo RuntimeRewardHookIo(RewardBinding& binding) {
    return {&binding, &GuardedReadSpan, &RewardAllocateWritable, &RewardWriteWritable,
            &RewardProtectExecuteRead, &RewardConfirmBattleInactive,
            &RewardBeginCodeWrite, &RewardWriteIfEqual,
            &RewardFlush, &RewardEndCodeWrite, &RewardFreeAllocation};
}

bool RewardScalarExchange(void* context, int32_t value) {
    if (!context) return false;
    InterlockedExchange(static_cast<volatile LONG*>(context), static_cast<LONG>(value));
    return true;
}

bool RewardScalarRead(void* context, int32_t* valueOut) {
    if (!context || !valueOut) return false;
    *valueOut = static_cast<int32_t>(
        InterlockedCompareExchange(static_cast<volatile LONG*>(context), 0, 0));
    return true;
}

RewardScalarIo RuntimeRewardScalarIo(RewardBinding& binding) {
    return {const_cast<LONG*>(&binding.scalar), &RewardScalarExchange, &RewardScalarRead};
}

bool ResolveRuntimeCatalogRows() {
    for (DebugBinding& binding : g_debugBindings) {
        binding.flag = FfxHooks::FindF8Flag(binding.key);
        if (!binding.flag) return false;
    }
    for (RewardBinding& binding : g_rewardBindings) {
        binding.flag = FfxHooks::FindF8Flag(binding.key);
        if (!binding.flag || !binding.flag->scalar) return false;
    }
    g_apFlag = FfxHooks::FindF8Flag("boosters.entire_party_earns_ap");
    return g_apFlag != nullptr;
}

void PublishAllRuntimeRows(F8RuntimeAvailability availability, bool hasAppliedValue,
                           bool appliedValue) {
    for (const DebugBinding& binding : g_debugBindings) {
        FfxHooks::PublishF8RuntimeStatus(
            binding.key, availability, hasAppliedValue, appliedValue);
    }
    for (const RewardBinding& binding : g_rewardBindings) {
        FfxHooks::PublishF8RuntimeScalarStatus(
            binding.key, availability, hasAppliedValue, appliedValue, false, 0);
    }
    FfxHooks::PublishF8RuntimeStatus(
        "boosters.entire_party_earns_ap", availability, hasAppliedValue, appliedValue);
}

void LogPersistentFailure(const char* key, FailureReason reason, FailureReason* prior) {
    if (!prior || *prior == reason) return;
    *prior = reason;
    if (reason != FailureReason::None) {
        BoostLog("[f8-runtime] key=%s failure=%s\n", key, FailureReasonName(reason));
    }
}

void LogEdge(const char* key, bool effective, FfxHooks::Config::BoolSource source,
             EdgeState state, uint8_t readback, bool* hasPrior, bool* priorEffective,
             FfxHooks::Config::BoolSource* priorSource, EdgeState* priorState,
             uint8_t* priorReadback) {
    if (!hasPrior || !priorEffective || !priorSource || !priorState || !priorReadback) return;
    if (*hasPrior && *priorEffective == effective && *priorSource == source &&
        *priorState == state && *priorReadback == readback) {
        return;
    }
    *hasPrior = true;
    *priorEffective = effective;
    *priorSource = source;
    *priorState = state;
    *priorReadback = readback;
    BoostLog("[f8-runtime] key=%s effective=%d source=%s state=%s readback=%02X\n",
             key, effective ? 1 : 0, FfxHooks::Config::BoolSourceName(source),
             EdgeStateName(state), static_cast<unsigned>(readback));
}

bool ValidateAdapter(uintptr_t moduleBase, ProfileResult* failureOut) {
    if (failureOut) *failureOut = ProfileResult::BadDos;
    if (!moduleBase ||
        moduleBase > (std::numeric_limits<uintptr_t>::max)() - kPeHeaderProbeSize ||
        !ValidateMappedSpan(moduleBase, moduleBase, kPeHeaderProbeSize, MemoryAccess::ReadOnly)) {
        return false;
    }

    std::array<uint8_t, kPeHeaderProbeSize> header{};
    if (!GuardedCopy(header.data(), reinterpret_cast<const void*>(moduleBase), header.size())) {
        return false;
    }
    ExecutableIdentity identity{};
    const ProfileResult profile = FfxHooks::F8Runtime::ParseExecutableIdentity(
        header.data(), header.size(), &identity);
    if (failureOut) *failureOut = profile;
    if (profile != ProfileResult::Supported ||
        !FfxHooks::F8Runtime::IsSupportedExecutable(identity)) {
        return false;
    }

    ValidatedAdapter candidate{};
    candidate.moduleBase = moduleBase;
    candidate.sizeOfImage = identity.sizeOfImage;
    if (!AddressFromRva(moduleBase, identity.sizeOfImage, RVA_FFX_DEBUG_FLAGS,
                        FFX_DEBUG_STRUCT_SIZE, &candidate.debugBase) ||
        !ValidateMappedSpan(moduleBase, candidate.debugBase, FFX_DEBUG_STRUCT_SIZE,
                            MemoryAccess::ReadWrite) ||
        !AddressFromRva(moduleBase, identity.sizeOfImage, RVA_FFX_BATTLE_ACTIVE_FLAG,
                        sizeof(uint8_t), &candidate.battleActive) ||
        !ValidateMappedSpan(moduleBase, candidate.battleActive, sizeof(uint8_t),
                            MemoryAccess::ReadWrite) ||
        !AddressFromRva(moduleBase, identity.sizeOfImage, RVA_FFX_BATTLE_PARTICIPATION,
                        FfxHooks::F8Runtime::kApSlotCount, &candidate.participation) ||
        !ValidateMappedSpan(moduleBase, candidate.participation,
                            FfxHooks::F8Runtime::kApSlotCount, MemoryAccess::ReadWrite) ||
        !AddressFromRva(moduleBase, identity.sizeOfImage, RVA_FFX_AP_EARN,
                        FfxHooks::F8Runtime::kApSlotCount, &candidate.earn) ||
        !ValidateMappedSpan(moduleBase, candidate.earn, FfxHooks::F8Runtime::kApSlotCount,
                            MemoryAccess::ReadWrite)) {
        if (failureOut) *failureOut = ProfileResult::OutOfImage;
        return false;
    }
    for (DebugBinding& binding : g_debugBindings) {
        if (binding.offset >= FFX_DEBUG_STRUCT_SIZE ||
            candidate.debugBase > (std::numeric_limits<uintptr_t>::max)() - binding.offset) {
            if (failureOut) *failureOut = ProfileResult::RangeOverflow;
            return false;
        }
        binding.address = candidate.debugBase + binding.offset;
    }
    for (RewardBinding& binding : g_rewardBindings) {
        binding.prepared = false;
        if (binding.debugOffset >= FFX_DEBUG_STRUCT_SIZE ||
            candidate.debugBase > (std::numeric_limits<uintptr_t>::max)() - binding.debugOffset) {
            binding.failure = FailureReason::MemorySpan;
            continue;
        }
        binding.gateAddress = candidate.debugBase + binding.debugOffset;
        const FfxHooks::F8Runtime::RewardHookSpec& spec =
            FfxHooks::F8Runtime::RewardHookSpecFor(binding.kind);
        uintptr_t signatureAddress = 0;
        if (!AddressFromRva(moduleBase, identity.sizeOfImage, spec.signatureRva,
                            spec.signature.size(), &signatureAddress) ||
            !ValidateMappedSpan(moduleBase, signatureAddress, spec.signature.size(),
                                MemoryAccess::Execute) ||
            !FfxHooks::F8Runtime::PrepareRewardHookState(
                binding.kind, moduleBase,
                reinterpret_cast<uintptr_t>(&binding.scalar), &binding.hook)) {
            binding.failure = FailureReason::MemorySpan;
            continue;
        }
        binding.prepared = true;
    }
    for (size_t slot = 0; slot < FfxHooks::F8Runtime::kApSlotCount; ++slot) {
        if (candidate.participation > (std::numeric_limits<uintptr_t>::max)() - slot ||
            candidate.earn > (std::numeric_limits<uintptr_t>::max)() - slot) {
            if (failureOut) *failureOut = ProfileResult::RangeOverflow;
            return false;
        }
        candidate.participationBytes[slot] = candidate.participation + slot;
        candidate.earnBytes[slot] = candidate.earn + slot;
    }

    constexpr size_t partySpanLength =
        (FfxHooks::F8Runtime::kApSlotCount - 1u) * FFX_PARTY_SLOT_STRIDE +
        FFX_PARTY_IN_PARTY_OFFSET + sizeof(uint8_t);
    uintptr_t partySpan = 0;
    if (!AddressFromRva(moduleBase, identity.sizeOfImage, RVA_FFX_PARTY_STRUCT_BASE,
                        partySpanLength, &partySpan) ||
        !ValidateMappedSpan(moduleBase, partySpan, partySpanLength, MemoryAccess::ReadWrite)) {
        if (failureOut) *failureOut = ProfileResult::OutOfImage;
        return false;
    }
    for (size_t slot = 0; slot < FfxHooks::F8Runtime::kApSlotCount; ++slot) {
        if (slot > (std::numeric_limits<size_t>::max)() / FFX_PARTY_SLOT_STRIDE) {
            if (failureOut) *failureOut = ProfileResult::RangeOverflow;
            return false;
        }
        const size_t scaledSlot = slot * FFX_PARTY_SLOT_STRIDE;
        if (scaledSlot > (std::numeric_limits<size_t>::max)() - FFX_PARTY_IN_PARTY_OFFSET) {
            if (failureOut) *failureOut = ProfileResult::RangeOverflow;
            return false;
        }
        const size_t slotOffset = scaledSlot + FFX_PARTY_IN_PARTY_OFFSET;
        uint32_t slotRva = 0;
        if (!CheckedRvaAdd(RVA_FFX_PARTY_STRUCT_BASE, slotOffset, &slotRva) ||
            !AddressFromRva(moduleBase, identity.sizeOfImage, slotRva, sizeof(uint8_t),
                            &candidate.inParty[slot])) {
            if (failureOut) *failureOut = ProfileResult::RangeOverflow;
            return false;
        }
    }

    g_adapter = candidate;
    return true;
}

void ArmRuntimeIfReady() {
    if (g_adapterState.load(std::memory_order_acquire) !=
        static_cast<uint32_t>(AdapterState::Validated)) {
        return;
    }
    const ProducerState producer = FfxHooks::F8Runtime::ReadProducerState(&g_lifecycle);
    if (producer == ProducerState::TerminalFailure) {
        PublishAllRuntimeRows(F8RuntimeAvailability::ProducerUnavailable, false, false);
        return;
    }
    if (producer != ProducerState::Ready) {
        PublishAllRuntimeRows(F8RuntimeAvailability::Pending, false, false);
        return;
    }
    uint32_t tickPublication = g_tickGatePublication.load(std::memory_order_acquire);
    if (tickPublication == 0) {
        uint32_t expected = 0;
        if (g_tickGatePublication.compare_exchange_strong(
                expected, 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
            FfxHooks::F8Runtime::ArmTickGate(&g_tickGate);
            g_tickGatePublication.store(2, std::memory_order_release);
            tickPublication = 2;
        } else {
            tickPublication = expected;
        }
    }
    if (tickPublication != 2) return;
    if (FfxHooks::F8Runtime::StartLifecycle(&g_lifecycle)) {
        BoostLog("[f8-runtime] Present producer ready; frame lifecycle running\n");
    }
    if (g_lifecycle.state.load(std::memory_order_acquire) ==
        static_cast<uint32_t>(RuntimeLifecycle::Running)) {
        PublishAllRuntimeRows(F8RuntimeAvailability::Pending, false, false);
    } else {
        PublishAllRuntimeRows(F8RuntimeAvailability::ProducerUnavailable, false, false);
    }
}

FailureReason TransitionFailure(ByteTransition transition) {
    switch (transition) {
        case ByteTransition::ReadFailed: return FailureReason::Read;
        case ByteTransition::WriteFailed: return FailureReason::Write;
        case ByteTransition::ReadbackFailed: return FailureReason::Readback;
        case ByteTransition::Conflict: return FailureReason::Conflict;
        default: return FailureReason::None;
    }
}

// OFF restores only originals captured by this owner; an unowned byte is never normalized.
void ApplyDebugBindings() {
    const ByteIo io = RuntimeByteIo();
    for (DebugBinding& binding : g_debugBindings) {
        const FfxHooks::Config::BoolGateResult gate = FfxHooks::ResolveF8Flag(*binding.flag);
        const uintptr_t address = binding.address;
        const bool disabledAndUnowned = !gate.value && !binding.owned.captured;
        const ByteTransition transition = disabledAndUnowned
            ? ByteTransition::NoChange
            : FfxHooks::F8Runtime::UpdateOwnedByte(io, address, 1u, gate.value, &binding.owned);

        uint8_t readback = 0;
        const bool readOk = GuardedReadByte(nullptr, address, &readback);
        FailureReason failure = TransitionFailure(transition);
        if (!readOk && failure == FailureReason::None) failure = FailureReason::Readback;

        F8RuntimeAvailability availability = F8RuntimeAvailability::Available;
        EdgeState state = gate.value ? EdgeState::Applied : EdgeState::Restored;
        if (binding.owned.state == OwnershipState::Conflict ||
            transition == ByteTransition::Conflict) {
            availability = F8RuntimeAvailability::Conflict;
            state = EdgeState::Conflict;
        } else if (failure != FailureReason::None ||
                   binding.owned.state == OwnershipState::RestorePending) {
            availability = binding.owned.captured
                ? F8RuntimeAvailability::RestorePending
                : F8RuntimeAvailability::Pending;
            state = EdgeState::Pending;
        }

        FfxHooks::PublishF8RuntimeStatus(
            binding.key, availability, readOk, readOk && readback != 0);
        LogPersistentFailure(binding.key, failure, &binding.failure);
        LogEdge(binding.key, gate.value, gate.source, state, readback,
                &binding.hasEdge, &binding.edgeEffective, &binding.edgeSource,
                &binding.edgeState, &binding.edgeReadback);
    }
}

F8RuntimeAvailability RewardHookAvailability(RewardHookResult result) {
    switch (result) {
        case RewardHookResult::Installed:
        case RewardHookResult::AlreadyInstalled:
            return F8RuntimeAvailability::Available;
        case RewardHookResult::SignatureMismatch:
            return F8RuntimeAvailability::SignatureMismatch;
        case RewardHookResult::Conflict:
            return F8RuntimeAvailability::Conflict;
        case RewardHookResult::RestorePending:
            return F8RuntimeAvailability::RestorePending;
        case RewardHookResult::DeferredBattleActive:
            return F8RuntimeAvailability::Pending;
        default:
            return F8RuntimeAvailability::ProducerUnavailable;
    }
}

void PublishRewardEdge(RewardBinding& binding,
                       const FfxHooks::Config::BoolGateResult& gate,
                       F8RuntimeAvailability availability,
                       RewardBindingResult result,
                       int32_t scalarReadback) {
    uint8_t gateReadback = 0;
    const bool gateRead = GuardedReadByte(nullptr, binding.gateAddress, &gateReadback);
    const bool applied = result == RewardBindingResult::Applied;
    FfxHooks::PublishF8RuntimeScalarStatus(
        binding.key, availability, gateRead, gateRead && gateReadback != 0,
        applied, applied ? scalarReadback : 0);

    FailureReason failure = FailureReason::None;
    EdgeState state = gate.value ? EdgeState::Pending : EdgeState::Restored;
    switch (result) {
        case RewardBindingResult::Applied:
            state = EdgeState::Applied;
            break;
        case RewardBindingResult::Disabled:
            state = EdgeState::Restored;
            break;
        case RewardBindingResult::InvalidConfiguration:
            failure = FailureReason::InvalidConfiguration;
            state = EdgeState::Restored;
            break;
        case RewardBindingResult::HookDeferred:
            // Battle activity is an ordinary safe-point deferral, not an install failure.
            state = EdgeState::Pending;
            break;
        case RewardBindingResult::Conflict:
            failure = FailureReason::Conflict;
            state = EdgeState::Conflict;
            break;
        case RewardBindingResult::ScalarWriteFailed:
            failure = FailureReason::Write;
            break;
        case RewardBindingResult::ScalarReadbackFailed:
            failure = FailureReason::Readback;
            break;
        case RewardBindingResult::GateFailed:
            failure = binding.gateOwner.captured
                ? FailureReason::Protection : FailureReason::Readback;
            break;
        case RewardBindingResult::HookUnavailable:
            failure = FailureReason::HookInstall;
            break;
        default:
            break;
    }
    LogPersistentFailure(binding.key, failure, &binding.failure);
    const bool edgeChanged = !binding.hasEdge || binding.edgeEffective != gate.value ||
        binding.edgeSource != gate.source || binding.edgeState != state ||
        binding.edgeGateReadback != gateReadback ||
        binding.edgeHasScalarReadback != applied ||
        (applied && binding.edgeScalarReadback != scalarReadback);
    if (edgeChanged) {
        binding.hasEdge = true;
        binding.edgeEffective = gate.value;
        binding.edgeSource = gate.source;
        binding.edgeState = state;
        binding.edgeGateReadback = gateReadback;
        binding.edgeHasScalarReadback = applied;
        binding.edgeScalarReadback = scalarReadback;
        if (applied) {
            BoostLog("[f8-runtime] key=%s effective=%d source=%s state=%s gate=%02X scalar=%ld\n",
                     binding.key, gate.value ? 1 : 0,
                     FfxHooks::Config::BoolSourceName(gate.source), EdgeStateName(state),
                     static_cast<unsigned>(gateReadback), static_cast<long>(scalarReadback));
        } else {
            // OFF restores only the owned gate; scalar=none explicitly avoids claiming a scalar
            // memory readback or reset that this transaction does not perform.
            BoostLog("[f8-runtime] key=%s effective=%d source=%s state=%s gate=%02X scalar=none\n",
                     binding.key, gate.value ? 1 : 0,
                     FfxHooks::Config::BoolSourceName(gate.source), EdgeStateName(state),
                     static_cast<unsigned>(gateReadback));
        }
    }
}

void ApplyRewardMultipliers() {
    uint8_t battleActive = 0;
    if (!GuardedReadByte(nullptr, g_adapter.battleActive, &battleActive)) {
        for (RewardBinding& binding : g_rewardBindings) {
            const FfxHooks::Config::BoolGateResult gate =
                FfxHooks::ResolveF8Flag(*binding.flag);
            PublishRewardEdge(binding, gate, F8RuntimeAvailability::Pending,
                              RewardBindingResult::HookUnavailable, 0);
        }
        return;
    }

    // This lock spans the complete AP/Gil page transaction, including protection, compare/write,
    // flush, protection restoration, and readback. The two six-byte sites are logically
    // independent, but their shared image page makes an unresolved protection token a peer block.
    AcquireSRWLockExclusive(&g_rewardPatchLock);
    for (size_t bindingIndex = 0; bindingIndex < g_rewardBindings.size(); ++bindingIndex) {
        RewardBinding& binding = g_rewardBindings[bindingIndex];
        const FfxHooks::Config::BoolGateResult gate =
            FfxHooks::ResolveF8Flag(*binding.flag);
        if (!binding.prepared) {
            PublishRewardEdge(binding, gate, F8RuntimeAvailability::SignatureMismatch,
                              RewardBindingResult::HookUnavailable, 0);
            continue;
        }

        std::array<RewardHookState, kRewardBindingCount> transactionStates{};
        for (size_t index = 0; index < g_rewardBindings.size(); ++index) {
            transactionStates[index] = g_rewardBindings[index].hook;
        }
        if (!FfxHooks::F8Runtime::CanRunRewardHookTransaction(
                transactionStates.data(), transactionStates.size(), bindingIndex)) {
            PublishRewardEdge(binding, gate, F8RuntimeAvailability::Pending,
                              RewardBindingResult::HookDeferred, 0);
            continue;
        }

        if (binding.hook.state == RewardHookOwnership::RestorePending ||
            binding.hook.state == RewardHookOwnership::StubCleanupPending) {
            const RewardHookResult removal = FfxHooks::F8Runtime::RemoveRewardHook(
                RuntimeRewardHookIo(binding), battleActive != 0, &binding.hook);
            if (removal == RewardHookResult::Removed ||
                removal == RewardHookResult::AlreadyRemoved) {
                // Re-prepare only after removal has proved original site bytes and released the
                // old stub. Installation waits for a later frame, keeping recovery and publication
                // as distinct admitted transactions.
                binding.prepared = FfxHooks::F8Runtime::PrepareRewardHookState(
                    binding.kind, g_adapter.moduleBase,
                    reinterpret_cast<uintptr_t>(&binding.scalar), &binding.hook);
                PublishRewardEdge(
                    binding, gate,
                    binding.prepared ? F8RuntimeAvailability::Pending
                                     : F8RuntimeAvailability::SignatureMismatch,
                    binding.prepared ? RewardBindingResult::HookDeferred
                                     : RewardBindingResult::HookUnavailable,
                    0);
                continue;
            }
            PublishRewardEdge(binding, gate, RewardHookAvailability(removal),
                              FfxHooks::F8Runtime::RewardBindingResultFromHook(removal), 0);
            continue;
        }

        uint8_t gateBefore = 0;
        if (binding.hook.state != RewardHookOwnership::Installed &&
            !binding.gateOwner.captured) {
            if (!GuardedReadByte(nullptr, binding.gateAddress, &gateBefore)) {
                PublishRewardEdge(binding, gate, F8RuntimeAvailability::Pending,
                                  RewardBindingResult::GateFailed, 0);
                continue;
            }
            if (gateBefore != 0) {
                // Installing while an unowned gate is already ON would not be inert; leave both
                // code and byte untouched and surface external ownership instead.
                PublishRewardEdge(binding, gate, F8RuntimeAvailability::Conflict,
                                  RewardBindingResult::Conflict, 0);
                continue;
            }
        }

        RewardHookResult install = binding.hook.state == RewardHookOwnership::Installed
            ? RewardHookResult::AlreadyInstalled : RewardHookResult::DeferredBattleActive;
        if (battleActive == 0) {
            install = FfxHooks::F8Runtime::InstallRewardHook(
                RuntimeRewardHookIo(binding), false, &binding.hook);
        }
        const bool installed = binding.hook.state == RewardHookOwnership::Installed;
        if (!installed) {
            PublishRewardEdge(binding, gate, RewardHookAvailability(install),
                              FfxHooks::F8Runtime::RewardBindingResultFromHook(install),
                              0);
            continue;
        }

        const FfxHooks::F8ScalarResult scalar = FfxHooks::ResolveF8Scalar(*binding.flag);
        const bool scalarValid = scalar.state != FfxHooks::F8ScalarState::Invalid;
        int32_t scalarReadback = 0;
        const RewardBindingResult result = FfxHooks::F8Runtime::UpdateRewardBinding(
            RuntimeRewardScalarIo(binding), RuntimeByteIo(), binding.gateAddress,
            true, scalarValid, scalar.value, gate.value, &binding.gateOwner,
            &scalarReadback);
        F8RuntimeAvailability availability = F8RuntimeAvailability::Available;
        if (result == RewardBindingResult::Conflict) {
            availability = F8RuntimeAvailability::Conflict;
        } else if (result == RewardBindingResult::GateFailed) {
            availability = binding.gateOwner.captured
                ? F8RuntimeAvailability::RestorePending
                : F8RuntimeAvailability::Pending;
        } else if (result == RewardBindingResult::ScalarWriteFailed ||
                   result == RewardBindingResult::ScalarReadbackFailed) {
            availability = F8RuntimeAvailability::Pending;
        }
        PublishRewardEdge(binding, gate, availability, result, scalarReadback);
    }
    ReleaseSRWLockExclusive(&g_rewardPatchLock);
}

void PublishApResult(const FfxHooks::Config::BoolGateResult& gate,
                     F8RuntimeAvailability availability, bool applied,
                     FailureReason failure, EdgeState state, uint8_t readback) {
    FfxHooks::PublishF8RuntimeStatus(
        "boosters.entire_party_earns_ap", availability,
        availability == F8RuntimeAvailability::Available, applied);
    LogPersistentFailure("boosters.entire_party_earns_ap", failure, &g_apFailure);
    LogEdge("boosters.entire_party_earns_ap", gate.value, gate.source, state, readback,
            &g_apHasEdge, &g_apEdgeEffective, &g_apEdgeSource, &g_apEdgeState,
            &g_apEdgeReadback);
}

// AP mutation covers current-battle slots 0..6; Seymour's structural slot 7 is excluded.
// OFF and battle end never replay stale bytes captured from an earlier battle.
void ApplyEntirePartyAp() {
    const FfxHooks::Config::BoolGateResult gate = FfxHooks::ResolveF8Flag(*g_apFlag);
    if (!gate.value) {
        PublishApResult(gate, F8RuntimeAvailability::Available, false,
                        FailureReason::None, EdgeState::Restored, 0);
        return;
    }

    uint8_t battleActive = 0;
    if (!GuardedReadByte(nullptr, g_adapter.battleActive, &battleActive)) {
        PublishApResult(gate, F8RuntimeAvailability::Pending, false,
                        FailureReason::Read, EdgeState::Pending, 0);
        return;
    }
    if (battleActive == 0) {
        PublishApResult(gate, F8RuntimeAvailability::Available, false,
                        FailureReason::None, EdgeState::Pending, 0);
        return;
    }

    std::array<uint8_t, FfxHooks::F8Runtime::kApSlotCount> inParty{};
    std::array<uint8_t, FfxHooks::F8Runtime::kApSlotCount> participation{};
    for (size_t slot = 0; slot < FfxHooks::F8Runtime::kApSlotCount; ++slot) {
        if (!GuardedReadByte(nullptr, g_adapter.inParty[slot], &inParty[slot]) ||
            !GuardedReadByte(nullptr, g_adapter.participationBytes[slot], &participation[slot])) {
            PublishApResult(gate, F8RuntimeAvailability::Pending, false,
                            FailureReason::Read, EdgeState::Pending, 0);
            return;
        }
    }
    if (!FfxHooks::F8Runtime::HasSeededBattleParticipant(inParty, participation)) {
        PublishApResult(gate, F8RuntimeAvailability::Available, false,
                        FailureReason::None, EdgeState::Pending, 0);
        return;
    }

    const std::array<FfxHooks::F8Runtime::ApSlotUpdate, FfxHooks::F8Runtime::kApSlotCount>
        updates = FfxHooks::F8Runtime::ComputeApUpdates(inParty, participation);
    if (!ValidateMappedSpan(g_adapter.moduleBase, g_adapter.participation,
                            FfxHooks::F8Runtime::kApSlotCount, MemoryAccess::ReadWrite) ||
        !ValidateMappedSpan(g_adapter.moduleBase, g_adapter.earn,
                            FfxHooks::F8Runtime::kApSlotCount, MemoryAccess::ReadWrite)) {
        PublishApResult(gate, F8RuntimeAvailability::Pending, false,
                        FailureReason::MemorySpan, EdgeState::Pending, 0);
        return;
    }

    bool writesSucceeded = true;
    for (size_t slot = 0; slot < FfxHooks::F8Runtime::kApSlotCount; ++slot) {
        writesSucceeded = GuardedStoreByte(
            g_adapter.participationBytes[slot], updates[slot].participation) && writesSucceeded;
    }
    for (size_t slot = 0; slot < FfxHooks::F8Runtime::kApSlotCount; ++slot) {
        writesSucceeded = GuardedStoreByte(g_adapter.earnBytes[slot], updates[slot].earn) &&
                          writesSucceeded;
    }

    std::array<uint8_t, FfxHooks::F8Runtime::kApSlotCount> participationReadback{};
    std::array<uint8_t, FfxHooks::F8Runtime::kApSlotCount> earnReadback{};
    bool readbackSucceeded = true;
    bool readbackMatches = true;
    for (size_t slot = 0; slot < FfxHooks::F8Runtime::kApSlotCount; ++slot) {
        const bool participationRead = GuardedReadByte(
            nullptr, g_adapter.participationBytes[slot], &participationReadback[slot]);
        const bool earnRead = GuardedReadByte(
            nullptr, g_adapter.earnBytes[slot], &earnReadback[slot]);
        readbackSucceeded = participationRead && earnRead && readbackSucceeded;
        readbackMatches = participationRead && earnRead &&
                          participationReadback[slot] == updates[slot].participation &&
                          earnReadback[slot] == updates[slot].earn && readbackMatches;
    }

    if (!writesSucceeded) {
        PublishApResult(gate, F8RuntimeAvailability::Pending, false,
                        FailureReason::Write, EdgeState::Pending, 0);
    } else if (!readbackSucceeded || !readbackMatches) {
        PublishApResult(gate, F8RuntimeAvailability::Pending, false,
                        FailureReason::Readback, EdgeState::Pending, 0);
    } else {
        PublishApResult(gate, F8RuntimeAvailability::Available, true,
                        FailureReason::None, EdgeState::Applied, 1);
    }
}

class FrameAdmissionScope {
public:
    ~FrameAdmissionScope() { FfxHooks::F8Runtime::LeaveFrame(&g_lifecycle); }
    FrameAdmissionScope(const FrameAdmissionScope&) = delete;
    FrameAdmissionScope& operator=(const FrameAdmissionScope&) = delete;
    FrameAdmissionScope() = default;
};

class TickScope {
public:
    ~TickScope() { FfxHooks::F8Runtime::EndTick(&g_tickGate); }
    TickScope(const TickScope&) = delete;
    TickScope& operator=(const TickScope&) = delete;
    TickScope() = default;
};

} // namespace

bool FfxHooks::StartUnXBoosterHook(uintptr_t moduleBase, BoosterLogFn log) {
    const AdapterState existing = static_cast<AdapterState>(
        g_adapterState.load(std::memory_order_acquire));
    if (existing == AdapterState::Validated) return true;
    if (existing == AdapterState::Unsupported) return false;

    g_log = log;
    if (!ResolveRuntimeCatalogRows()) {
        g_adapterState.store(static_cast<uint32_t>(AdapterState::Unsupported),
                             std::memory_order_release);
        PublishAllRuntimeRows(F8RuntimeAvailability::ProducerUnavailable, false, false);
        BoostLog("[f8-runtime] catalog binding failed\n");
        return false;
    }

    ProfileResult profileFailure = ProfileResult::BadDos;
    if (!ValidateAdapter(moduleBase, &profileFailure)) {
        g_adapterState.store(static_cast<uint32_t>(AdapterState::Unsupported),
                             std::memory_order_release);
        const F8RuntimeAvailability availability =
            profileFailure == ProfileResult::WrongMachine ||
                    profileFailure == ProfileResult::WrongTimestamp ||
                    profileFailure == ProfileResult::WrongImageSize ||
                    profileFailure == ProfileResult::BadDos ||
                    profileFailure == ProfileResult::BadPe ||
                    profileFailure == ProfileResult::BadOptionalHeader
                ? F8RuntimeAvailability::UnsupportedBuild
                : F8RuntimeAvailability::SignatureMismatch;
        PublishAllRuntimeRows(availability, false, false);
        BoostLog("[f8-runtime] unsupported executable profile result=%u\n",
                 static_cast<unsigned>(profileFailure));
        return false;
    }

    // All adapter fields and catalog pointers become visible before either Notify or FrameTick
    // may consume them. Notify performs the matching acquire load, so Start/Notify order is free.
    g_adapterState.store(static_cast<uint32_t>(AdapterState::Validated),
                         std::memory_order_release);
    BoostLog("[f8-runtime] adapter validated base=0x%08X size=0x%08X\n",
             static_cast<unsigned>(moduleBase), g_adapter.sizeOfImage);
    ArmRuntimeIfReady();
    return true;
}

void FfxHooks::NotifyUnXBoosterPresentProducer(bool ready, bool terminalFailure) {
    if (terminalFailure) {
        F8Runtime::PublishProducerState(&g_lifecycle, ProducerState::TerminalFailure);
        RequestUnXBoosterStop();
        if (g_adapterState.load(std::memory_order_acquire) ==
            static_cast<uint32_t>(AdapterState::Validated)) {
            PublishAllRuntimeRows(F8RuntimeAvailability::ProducerUnavailable, false, false);
            BoostLog("[f8-runtime] Present producer terminal failure\n");
        }
        return;
    }
    if (!ready) return;
    F8Runtime::PublishProducerState(&g_lifecycle, ProducerState::Ready);
    ArmRuntimeIfReady();
}

void FfxHooks::UnXBoosterFrameTick(uint32_t nowMs) {
    if (g_adapterState.load(std::memory_order_acquire) !=
        static_cast<uint32_t>(AdapterState::Validated)) {
        return;
    }
    if (!F8Runtime::TryEnterFrame(&g_lifecycle)) return;
    FrameAdmissionScope frame;
    if (!F8Runtime::TryBeginTick(&g_tickGate, nowMs, 33u)) return;
    TickScope tick;
    ApplyDebugBindings();
    ApplyRewardMultipliers();
    ApplyEntirePartyAp();
}

// Detach may call this path: it only closes admission and performs no wait or teardown work.
void FfxHooks::RequestUnXBoosterStop() {
    uint32_t expected = static_cast<uint32_t>(RuntimeLifecycle::Running);
    if (g_lifecycle.state.compare_exchange_strong(
            expected, static_cast<uint32_t>(RuntimeLifecycle::Stopping),
            std::memory_order_release, std::memory_order_relaxed)) {
        return;
    }
    expected = static_cast<uint32_t>(RuntimeLifecycle::Stopped);
    g_lifecycle.state.compare_exchange_strong(
        expected, static_cast<uint32_t>(RuntimeLifecycle::Stopping),
        std::memory_order_release, std::memory_order_relaxed);
}
