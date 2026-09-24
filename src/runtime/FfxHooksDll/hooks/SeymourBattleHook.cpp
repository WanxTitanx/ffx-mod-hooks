#include "SeymourBattleHook.h"

#include "F7InLive.h"
#include "F8FlagCatalog.h"
#include "F7DifficultyCore.h"
#include "MinHookBatchCoordinator.h"
#include "SharedBattleRuntime.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>

#include <MinHook.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>

#pragma intrinsic(_ReturnAddress)

namespace FfxHooks {
namespace {

using namespace SeymourBattle;
using F7Difficulty::kSupportedSizeOfImage;

constexpr char kSeymourKey[] = "boosters.playable_seymour";
constexpr std::array<uint8_t, 5> kEntryCall = {
    0xE8u, 0xB4u, 0x0Cu, 0x00u, 0x00u,
};
constexpr std::array<uint8_t, 5> kExitCall = {
    0xE8u, 0x79u, 0x51u, 0xFFu, 0xFFu,
};
constexpr std::array<uint8_t, 23> kAssignPrefix = {
    0x55u, 0x8Bu, 0xECu, 0x53u, 0x8Bu, 0x5Du, 0x08u, 0x81u,
    0xE3u, 0xFFu, 0x00u, 0x00u, 0x00u, 0x83u, 0x7Du, 0x0Cu,
    0x00u, 0x0Fu, 0x84u, 0x88u, 0x00u, 0x00u, 0x00u,
};
constexpr std::array<uint8_t, 20> kExitTargetFixedPrefix = {
    0x53u, 0x56u, 0x57u, 0xE8u, 0x48u, 0x8Eu, 0x02u, 0x00u,
    0xE8u, 0x63u, 0x09u, 0x00u, 0x00u, 0xE8u, 0xDEu, 0x00u,
    0x00u, 0x00u, 0x33u, 0xDBu,
};
constexpr uint32_t kExitTargetActorTableRva = 0x00F32078u;

enum class AdapterPublication : uint32_t {
    Unpublished = 0,
    Installing,
    Installed,
    Unsupported,
    RestorePending,
    Conflict,
    Stopped,
};

enum class MemoryAccess : uint8_t { ReadOnly = 0, ReadWrite, Execute };

using AssignRoutine = int(__cdecl*)(uint8_t slot, int active);
using ExitRoutine = void(__cdecl*)();

struct ValidatedAdapter {
    uintptr_t moduleBase = 0u;
    uintptr_t party = 0u;
    uintptr_t state = 0u;
    uintptr_t ability = 0u;
    uintptr_t discriminator = 0u;
    AssignRoutine assign = nullptr;
};

ValidatedAdapter g_adapter{};
std::atomic<const ValidatedAdapter*> g_adapterPublicationPtr{nullptr};
std::atomic<void*> g_exitOriginal{nullptr};
std::atomic<uint32_t> g_publication{
    static_cast<uint32_t>(AdapterPublication::Unpublished)};
std::atomic<uint32_t> g_producer{
    static_cast<uint32_t>(ProducerPublication::Unknown)};
AtomicCommandMailbox g_commands{};
AtomicAdmission g_admission{};
AtomicTelemetryMailbox g_telemetry{};
Ownership g_ownership{};
ExitDetourOwner g_exitOwner{};
TeardownMachine g_teardown{};
AdapterPublication g_teardownTerminal = AdapterPublication::Stopped;
ExitDetourResult g_lastExitRetirement = ExitDetourResult::InvalidArgument;
SharedBattleRuntime::ComposerSlotResult g_lastComposerUnregister =
    SharedBattleRuntime::ComposerSlotResult::InvalidArgument;
SeymourBattleLogFn g_log = nullptr;
const F8FlagSpec* g_flag = nullptr;
bool g_hasPresentEdge = false;
bool g_priorRequested = false;
Telemetry g_priorTelemetry{};

void SeymourEntryComposer(
    void*, uintptr_t returnRva, const SharedBattleRuntime::OriginalIo& original);
void __cdecl SeymourExitShim();

// Process-lifetime descriptor: F7 publishes only this stable address through its atomic slot.
const SharedBattleRuntime::ComposerIo g_entryComposer{
    nullptr, &SeymourEntryComposer};

void BattleLog(const char* format, ...) {
    if (!g_log || !format) return;
    char line[512] = {};
    va_list args;
    va_start(args, format);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
    va_end(args);
    g_log(line);
}

const char* ExitDetourResultName(ExitDetourResult result) {
    switch (result) {
        case ExitDetourResult::Installed: return "Installed";
        case ExitDetourResult::Removed: return "Removed";
        case ExitDetourResult::RetainedInert: return "RetainedInert";
        case ExitDetourResult::InvalidArgument: return "InvalidArgument";
        case ExitDetourResult::AlreadyOwned: return "AlreadyOwned";
        case ExitDetourResult::CreateFailed: return "CreateFailed";
        case ExitDetourResult::PublicationFailed: return "PublicationFailed";
        case ExitDetourResult::RollbackFailed: return "RollbackFailed";
        case ExitDetourResult::EnableFailedRetained: return "EnableFailedRetained";
        case ExitDetourResult::CoordinatorNotReady: return "CoordinatorNotReady";
        case ExitDetourResult::CoordinatorBusy: return "CoordinatorBusy";
        case ExitDetourResult::CoordinatorPoisoned: return "CoordinatorPoisoned";
        case ExitDetourResult::TeardownRetryRequired: return "TeardownRetryRequired";
        default: return "Unknown";
    }
}

const char* ComposerSlotResultName(SharedBattleRuntime::ComposerSlotResult result) {
    switch (result) {
        case SharedBattleRuntime::ComposerSlotResult::Registered: return "Registered";
        case SharedBattleRuntime::ComposerSlotResult::AlreadyRegistered:
            return "AlreadyRegistered";
        case SharedBattleRuntime::ComposerSlotResult::Conflict: return "Conflict";
        case SharedBattleRuntime::ComposerSlotResult::Unregistered: return "Unregistered";
        case SharedBattleRuntime::ComposerSlotResult::NotOwner: return "NotOwner";
        case SharedBattleRuntime::ComposerSlotResult::InvalidArgument: return "InvalidArgument";
        default: return "Unknown";
    }
}

const char* TeardownResultName(TeardownResult result) {
    switch (result) {
        case TeardownResult::InvalidArgument: return "InvalidArgument";
        case TeardownResult::Inactive: return "Inactive";
        case TeardownResult::OwnershipPending: return "OwnershipPending";
        case TeardownResult::ExitRetryRequired: return "ExitRetryRequired";
        case TeardownResult::ComposerRetryRequired: return "ComposerRetryRequired";
        case TeardownResult::Complete: return "Complete";
        default: return "Unknown";
    }
}

const char* AdapterPublicationName(AdapterPublication publication) {
    switch (publication) {
        case AdapterPublication::Unpublished: return "Unpublished";
        case AdapterPublication::Installing: return "Installing";
        case AdapterPublication::Installed: return "Installed";
        case AdapterPublication::Unsupported: return "Unsupported";
        case AdapterPublication::RestorePending: return "RestorePending";
        case AdapterPublication::Conflict: return "Conflict";
        case AdapterPublication::Stopped: return "Stopped";
        default: return "Unknown";
    }
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
                        MemoryAccess expected) {
    if (!moduleBase || !address || length == 0u ||
        address > (std::numeric_limits<uintptr_t>::max)() - length) {
        return false;
    }
    const uintptr_t end = address + length;
    for (uintptr_t cursor = address; cursor < end;) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &memory, sizeof(memory)) !=
            sizeof(memory)) {
            return false;
        }
        const uintptr_t regionBase = reinterpret_cast<uintptr_t>(memory.BaseAddress);
        if (memory.RegionSize == 0u ||
            regionBase > (std::numeric_limits<uintptr_t>::max)() - memory.RegionSize) {
            return false;
        }
        const uintptr_t regionEnd = regionBase + memory.RegionSize;
        if (cursor < regionBase || cursor >= regionEnd || memory.State != MEM_COMMIT ||
            memory.Type != MEM_IMAGE ||
            memory.AllocationBase != reinterpret_cast<void*>(moduleBase) ||
            (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0u) {
            return false;
        }
        const bool protectionMatches = expected == MemoryAccess::Execute
            ? IsExecutableProtection(memory.Protect)
            : expected == MemoryAccess::ReadWrite
                ? IsReadableProtection(memory.Protect) && IsWritableProtection(memory.Protect)
                : IsReadableProtection(memory.Protect);
        if (!protectionMatches) return false;
        cursor = regionEnd < end ? regionEnd : end;
    }
    return true;
}

bool AddressFromRva(uintptr_t moduleBase, uint32_t rva, size_t length,
                    uintptr_t* addressOut) {
    if (!addressOut || length == 0u || rva >= kSupportedSizeOfImage ||
        length > static_cast<size_t>(kSupportedSizeOfImage - rva) ||
        moduleBase > (std::numeric_limits<uintptr_t>::max)() - rva) {
        return false;
    }
    *addressOut = moduleBase + rva;
    return true;
}

bool GuardedRead(uintptr_t address, void* output, size_t length) {
    if (!address || !output || length == 0u) return false;
    __try {
        std::memcpy(output, reinterpret_cast<const void*>(address), length);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uint32_t ReadU32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8u) |
           (static_cast<uint32_t>(bytes[2]) << 16u) |
           (static_cast<uint32_t>(bytes[3]) << 24u);
}

bool DecodeRel32CallTarget(uintptr_t callsite, const std::array<uint8_t, 5>& bytes,
                           uintptr_t* targetOut) {
    if (!targetOut || bytes[0] != 0xE8u ||
        callsite > (std::numeric_limits<uintptr_t>::max)() - bytes.size()) {
        return false;
    }
    int32_t displacement = 0;
    std::memcpy(&displacement, bytes.data() + 1u, sizeof(displacement));
    const int64_t target = static_cast<int64_t>(callsite + bytes.size()) + displacement;
    if (target <= 0 || static_cast<uint64_t>(target) >
                           (std::numeric_limits<uintptr_t>::max)()) {
        return false;
    }
    *targetOut = static_cast<uintptr_t>(target);
    return true;
}

bool ValidateExitTarget(uintptr_t moduleBase, uintptr_t address) {
    std::array<uint8_t, 25> bytes{};
    if (!GuardedRead(address, bytes.data(), bytes.size()) ||
        !std::equal(kExitTargetFixedPrefix.begin(), kExitTargetFixedPrefix.end(),
                    bytes.begin()) || bytes[20] != 0xBEu) {
        return false;
    }
    uintptr_t actorTable = 0u;
    return AddressFromRva(moduleBase, kExitTargetActorTableRva, 1u, &actorTable) &&
           actorTable <= (std::numeric_limits<uint32_t>::max)() &&
           ReadU32(bytes.data() + 21u) == static_cast<uint32_t>(actorTable);
}

bool ValidateAdapter(uintptr_t moduleBase, ValidatedAdapter* output,
                     SeymourBattleInstallStatus* statusOut) {
    if (!output || !moduleBase || !F7_SharedBattleRuntimeReady(moduleBase)) {
        if (statusOut) *statusOut = SeymourBattleInstallStatus::SharedRuntimeUnavailable;
        return false;
    }

    ValidatedAdapter candidate{};
    candidate.moduleBase = moduleBase;
    uintptr_t entryCallsite = 0u;
    uintptr_t exitCallsite = 0u;
    uintptr_t exitTarget = 0u;
    uintptr_t assign = 0u;
    if (!AddressFromRva(moduleBase, kSeymourBattleEntryCallRva, kEntryCall.size(),
                        &entryCallsite) ||
        !AddressFromRva(moduleBase, kSeymourBattleExitCallRva, kExitCall.size(),
                        &exitCallsite) ||
        !AddressFromRva(moduleBase, kExitTargetRva, 25u, &exitTarget) ||
        !AddressFromRva(moduleBase, kSeymourBattleAssignRva, kAssignPrefix.size(), &assign) ||
        !AddressFromRva(moduleBase, kSeymourBattlePartyByteRva, 1u, &candidate.party) ||
        !AddressFromRva(moduleBase, kSeymourBattlePersistentStateRva, kStateListSize,
                        &candidate.state) ||
        !AddressFromRva(moduleBase, kSeymourBattlePersistentAbilityRva, kAbilityListSize,
                        &candidate.ability) ||
        !AddressFromRva(moduleBase, kSeymourBattleDiscriminatorRva, 1u,
                        &candidate.discriminator)) {
        if (statusOut) *statusOut = SeymourBattleInstallStatus::TargetOutOfRange;
        return false;
    }

    if (!ValidateMappedSpan(moduleBase, entryCallsite, kEntryCall.size(),
                            MemoryAccess::Execute) ||
        !ValidateMappedSpan(moduleBase, exitCallsite, kExitCall.size(),
                            MemoryAccess::Execute) ||
        !ValidateMappedSpan(moduleBase, exitTarget, 25u, MemoryAccess::Execute) ||
        !ValidateMappedSpan(moduleBase, assign, kAssignPrefix.size(), MemoryAccess::Execute) ||
        !ValidateMappedSpan(moduleBase, candidate.party, 1u, MemoryAccess::ReadWrite) ||
        !ValidateMappedSpan(moduleBase, candidate.state, kStateListSize,
                            MemoryAccess::ReadWrite) ||
        !ValidateMappedSpan(moduleBase, candidate.ability, kAbilityListSize,
                            MemoryAccess::ReadWrite) ||
        !ValidateMappedSpan(moduleBase, candidate.discriminator, 1u,
                            MemoryAccess::ReadOnly)) {
        if (statusOut) *statusOut = SeymourBattleInstallStatus::TargetOutOfRange;
        return false;
    }

    std::array<uint8_t, 5> entryBytes{};
    std::array<uint8_t, 5> exitBytes{};
    std::array<uint8_t, kAssignPrefix.size()> assignBytes{};
    uintptr_t decodedEntryTarget = 0u;
    uintptr_t decodedExitTarget = 0u;
    if (!GuardedRead(entryCallsite, entryBytes.data(), entryBytes.size()) ||
        !GuardedRead(exitCallsite, exitBytes.data(), exitBytes.size()) ||
        !GuardedRead(assign, assignBytes.data(), assignBytes.size()) ||
        entryBytes != kEntryCall || exitBytes != kExitCall ||
        assignBytes != kAssignPrefix ||
        !DecodeRel32CallTarget(entryCallsite, entryBytes, &decodedEntryTarget) ||
        !DecodeRel32CallTarget(exitCallsite, exitBytes, &decodedExitTarget) ||
        decodedEntryTarget != F7_SharedBattleInitSceneTarget() ||
        decodedExitTarget != exitTarget || !ValidateExitTarget(moduleBase, exitTarget)) {
        if (statusOut) *statusOut = SeymourBattleInstallStatus::SignatureMismatch;
        return false;
    }

    // WHY: F7 readiness above proves the exact disk SHA-256, PE timestamp/image geometry, and
    // sole InitScene/ActorInit detour batch. These additional loaded-byte gates cover Seymour's
    // read-only caller evidence, unique exit target, official Assign ABI, and RAM widths.
    candidate.assign = reinterpret_cast<AssignRoutine>(assign);
    *output = candidate;
    return true;
}

bool RuntimeRead(void* context, RosterImage* image) {
    const ValidatedAdapter* adapter = static_cast<const ValidatedAdapter*>(context);
    return adapter && image &&
           GuardedRead(adapter->party, &image->party, sizeof(image->party)) &&
           GuardedRead(adapter->state, image->state.data(), image->state.size()) &&
           GuardedRead(adapter->ability, image->ability.data(), image->ability.size());
}

bool RuntimeAssign(void* context, uint8_t slot, int active) {
    const ValidatedAdapter* adapter = static_cast<const ValidatedAdapter*>(context);
    if (!adapter || !adapter->assign) return false;
    // ABI evidence: IDA decompiles VA 0x00786A70 as int __cdecl(uint8_t slot, int active).
    // The return value is not an ownership proof; exact RAM readback remains authoritative.
    __try {
        (void)adapter->assign(slot, active);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool RuntimeCommandCurrent(void*, const Command* expected) {
    if (!expected) return false;
    const Command current = ReadCommand(&g_commands);
    return current.generation == expected->generation &&
           current.requested == expected->requested &&
           current.stopping == expected->stopping;
}

ServiceIo RuntimeServiceIo(const ValidatedAdapter* adapter) {
    return {const_cast<ValidatedAdapter*>(adapter), kStateListSize, kAbilityListSize,
            &RuntimeRead, &RuntimeAssign, &RuntimeCommandCurrent};
}

struct SharedOriginalContext {
    const SharedBattleRuntime::OriginalIo* original = nullptr;
};

int CallSharedOriginal(void* context) {
    const SharedOriginalContext* shared = static_cast<const SharedOriginalContext*>(context);
    return shared && shared->original && shared->original->call
        ? shared->original->call(shared->original->context)
        : 0;
}

void CallExitOriginal(void*) {
    const ExitRoutine original = reinterpret_cast<ExitRoutine>(
        g_exitOriginal.load(std::memory_order_acquire));
    if (original) original();
}

void PublishInvalidTelemetry(const Command& command, CallbackKind callback,
                             uintptr_t returnRva) {
    Telemetry telemetry{};
    telemetry.state = State::Unavailable;
    telemetry.outcome = ServiceOutcome::InvalidInput;
    telemetry.generation = command.generation;
    telemetry.threadId = GetCurrentThreadId();
    telemetry.callback = callback;
    telemetry.returnAddress = static_cast<uint32_t>(returnRva);
    PublishTelemetry(&g_telemetry, telemetry);
}

void SeymourEntryComposer(
    void*, uintptr_t returnRva, const SharedBattleRuntime::OriginalIo& original) {
    // KEY: admission is the first C++ action. A callback rejected during retirement can use only
    // the shared guarded original and never observes adapter or ownership state.
    const AdmissionTicket ticket = EnterCallback(&g_admission);
    const ValidatedAdapter* adapter = g_adapterPublicationPtr.load(std::memory_order_acquire);
    if (!ticket.behaviorAdmitted || !adapter || !IsAdmittedEntryReturnRva(returnRva)) {
        if (original.call) (void)original.call(original.context);
        LeaveCallback(&g_admission, ticket);
        return;
    }

    const Command command = ReadCommand(&g_commands);
    uint8_t discriminator = 0u;
    if (!GuardedRead(adapter->discriminator, &discriminator, sizeof(discriminator))) {
        if (original.call) (void)original.call(original.context);
        PublishInvalidTelemetry(command, CallbackKind::Entry, returnRva);
        LeaveCallback(&g_admission, ticket);
        return;
    }

    SharedOriginalContext originalContext{&original};
    Telemetry telemetry{};
    const EntryServiceResult serviced = ServiceEntry(
        command, discriminator, RuntimeServiceIo(adapter),
        {&originalContext, &CallSharedOriginal}, &g_ownership, &telemetry);
    telemetry.threadId = GetCurrentThreadId();
    telemetry.callback = CallbackKind::Entry;
    telemetry.returnAddress = static_cast<uint32_t>(returnRva);
    telemetry.battleDiscriminator = discriminator;
    PublishTelemetry(&g_telemetry, telemetry);
    (void)serviced;
    LeaveCallback(&g_admission, ticket);
}

void __cdecl SeymourExitShim() {
    // KEY: count before reading even the trampoline. Apply publishes g_exitOriginal with release
    // semantics before MinHook can expose this entry; the acquire load below is therefore exact.
    const AdmissionTicket ticket = EnterCallback(&g_admission);
    const ExitRoutine original = reinterpret_cast<ExitRoutine>(
        g_exitOriginal.load(std::memory_order_acquire));
    if (!original) {
        LeaveCallback(&g_admission, ticket);
        return;
    }

    const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const ValidatedAdapter* adapter = g_adapterPublicationPtr.load(std::memory_order_acquire);
    const uintptr_t returnRva = adapter && returnAddress >= adapter->moduleBase
        ? returnAddress - adapter->moduleBase
        : 0u;
    if (!ticket.behaviorAdmitted || !adapter || !IsAdmittedExitReturnRva(returnRva)) {
        original();
        LeaveCallback(&g_admission, ticket);
        return;
    }

    const Command command = ReadCommand(&g_commands);
    Telemetry telemetry{};
    (void)ServiceExit(command, RuntimeServiceIo(adapter),
                      {nullptr, &CallExitOriginal}, &g_ownership, &telemetry);
    telemetry.threadId = GetCurrentThreadId();
    telemetry.callback = CallbackKind::Exit;
    telemetry.returnAddress = static_cast<uint32_t>(returnRva);
    PublishTelemetry(&g_telemetry, telemetry);
    LeaveCallback(&g_admission, ticket);
}

bool ExitHookCreate(void*, uintptr_t target, void* detour, void** originalOut) {
    return MH_CreateHook(reinterpret_cast<void*>(target), detour, originalOut) == MH_OK;
}

bool PublishExitOriginal(void*, void* original) {
    if (!original) return false;
    g_exitOriginal.store(original, std::memory_order_release);
    return true;
}

ExitDetourIo ExitCreateIo() {
    return {nullptr, &ExitHookCreate, &PublishExitOriginal};
}

bool DrainCallbacks(DWORD timeoutMs) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    while (ActiveCallbacks(&g_admission) != 0u) {
        if (GetTickCount64() >= deadline) return false;
        Sleep(1u);
    }
    return true;
}

bool CloseAndDrain(void*) {
    CloseAdmission(&g_admission);
    return DrainCallbacks(5000u);
}

bool DrainAfterDisable(void*) {
    return DrainCallbacks(5000u);
}

MinHookBatch::NeutralizationFence ExitFence() {
    return {nullptr, &CloseAndDrain, &DrainAfterDisable};
}

bool RosterOwnershipClear() {
    return !g_ownership.hasBaseline && !g_ownership.battleRosterOwned &&
           !g_ownership.persistentMayContainSeymour &&
           g_ownership.state != State::RestorePending;
}

bool RetireExitForTeardown(void*) {
    g_lastExitRetirement = RetireExitDetour(
        ExitCreateIo(), &MinHookBatch::ProcessCoordinator(),
        MinHookBatch::RuntimeBatchIo(), ExitFence(), &g_exitOwner);
    return g_lastExitRetirement == ExitDetourResult::RetainedInert ||
           g_lastExitRetirement == ExitDetourResult::Removed;
}

bool UnregisterComposerForTeardown(void*) {
    g_lastComposerUnregister = F7_UnregisterInitSceneComposer(&g_entryComposer);
    return g_lastComposerUnregister ==
        SharedBattleRuntime::ComposerSlotResult::Unregistered;
}

TeardownIo TeardownOperations() {
    return {nullptr, &RetireExitForTeardown, &UnregisterComposerForTeardown};
}

enum class TeardownPreparation : uint8_t {
    Ready = 0,
    OwnershipPending,
    CallbackBusy,
    DrainTimeout,
};

const char* TeardownPreparationName(TeardownPreparation preparation) {
    switch (preparation) {
        case TeardownPreparation::Ready: return "Ready";
        case TeardownPreparation::OwnershipPending: return "OwnershipPending";
        case TeardownPreparation::CallbackBusy: return "CallbackBusy";
        case TeardownPreparation::DrainTimeout: return "DrainTimeout";
        default: return "Unknown";
    }
}

static TeardownPreparation PrepareTeardownAdmission() {
    const AdmissionTicket ticket = EnterCallback(&g_admission);
    if (ticket.behaviorAdmitted) {
        if (!RosterOwnershipClear()) {
            // Keep admission open so the exact exit/next-entry callback can finish official
            // cleanup after a normal-context teardown request.
            LeaveCallback(&g_admission, ticket);
            return TeardownPreparation::OwnershipPending;
        }
        CloseAdmission(&g_admission);
        LeaveCallback(&g_admission, ticket);
    } else {
        const bool stillAccepting =
            g_admission.accepting.load(std::memory_order_acquire) != 0u;
        LeaveCallback(&g_admission, ticket);
        if (stillAccepting) return TeardownPreparation::CallbackBusy;
    }

    if (!DrainCallbacks(5000u)) return TeardownPreparation::DrainTimeout;
    return RosterOwnershipClear()
        ? TeardownPreparation::Ready
        : TeardownPreparation::OwnershipPending;
}

F8RuntimeAvailability AvailabilityFor(const Telemetry& telemetry) {
    switch (telemetry.state) {
        case State::Off:
        case State::AppliedBattleRoster:
            return F8RuntimeAvailability::Available;
        case State::PendingBattle: return F8RuntimeAvailability::Pending;
        case State::RestorePending:
            return F8RuntimeAvailability::RestorePending;
        case State::RejectedPreflight:
            return F8RuntimeAvailability::Conflict;
        case State::Unavailable:
        default:
            return F8RuntimeAvailability::ProducerUnavailable;
    }
}

bool SameTelemetry(const Telemetry& left, const Telemetry& right) {
    return left.state == right.state && left.outcome == right.outcome &&
           left.generation == right.generation && left.threadId == right.threadId &&
           left.callback == right.callback && left.returnAddress == right.returnAddress &&
           left.battleDiscriminator == right.battleDiscriminator &&
           left.beforeHash == right.beforeHash && left.applyHash == right.applyHash &&
           left.restoreHash == right.restoreHash;
}

const char* StateName(State state) {
    switch (state) {
        case State::Off: return "off";
        case State::PendingBattle: return "pending-battle";
        case State::AppliedBattleRoster: return "applied-battle-roster";
        case State::RestorePending: return "restore-pending";
        case State::Unavailable: return "unavailable";
        case State::RejectedPreflight: return "rejected-preflight";
        default: return "unknown";
    }
}

const char* OutcomeName(ServiceOutcome outcome) {
    switch (outcome) {
        case ServiceOutcome::NoChange: return "no-change";
        case ServiceOutcome::DeferredBattle: return "deferred-battle";
        case ServiceOutcome::Applied: return "applied";
        case ServiceOutcome::Restored: return "restored";
        case ServiceOutcome::RejectedPreflight: return "rejected-preflight";
        case ServiceOutcome::ApplyFailedRolledBack: return "apply-failed-rolled-back";
        case ServiceOutcome::RestorePending: return "restore-pending";
        case ServiceOutcome::CommandSuperseded: return "command-superseded";
        case ServiceOutcome::CommandSupersededRolledBack:
            return "command-superseded-rolled-back";
        case ServiceOutcome::InvalidInput: return "invalid-input";
        default: return "unknown";
    }
}

const char* FailureName(ServiceOutcome outcome) {
    switch (outcome) {
        case ServiceOutcome::RejectedPreflight:
        case ServiceOutcome::ApplyFailedRolledBack:
            return "ownership-conflict";
        case ServiceOutcome::RestorePending:
            return "restore-pending";
        case ServiceOutcome::InvalidInput:
            return "memory-span-invalid";
        default:
            return nullptr;
    }
}

const char* CallbackName(CallbackKind callback) {
    switch (callback) {
        case CallbackKind::Entry: return "entry";
        case CallbackKind::Exit: return "exit";
        default: return "none";
    }
}

SeymourBattleInstallStatus InstallStatusFor(ExitDetourResult result) {
    switch (result) {
        case ExitDetourResult::CreateFailed:
            return SeymourBattleInstallStatus::ExitCreateFailed;
        case ExitDetourResult::PublicationFailed:
            return SeymourBattleInstallStatus::ExitPublicationFailed;
        case ExitDetourResult::EnableFailedRetained:
        case ExitDetourResult::TeardownRetryRequired:
            return SeymourBattleInstallStatus::ExitEnableFailed;
        case ExitDetourResult::CoordinatorNotReady:
            return SeymourBattleInstallStatus::CoordinatorUnavailable;
        case ExitDetourResult::CoordinatorBusy:
            return SeymourBattleInstallStatus::CoordinatorBusy;
        case ExitDetourResult::CoordinatorPoisoned:
            return SeymourBattleInstallStatus::CoordinatorPoisoned;
        default:
            return SeymourBattleInstallStatus::ExitEnableFailed;
    }
}

F8RuntimeAvailability FailureAvailability(SeymourBattleInstallStatus status) {
    switch (status) {
        case SeymourBattleInstallStatus::UnsupportedProfile:
            return F8RuntimeAvailability::UnsupportedBuild;
        case SeymourBattleInstallStatus::SignatureMismatch:
            return F8RuntimeAvailability::SignatureMismatch;
        case SeymourBattleInstallStatus::ComposerConflict:
            return F8RuntimeAvailability::Conflict;
        case SeymourBattleInstallStatus::RestorePending:
            return F8RuntimeAvailability::RestorePending;
        default:
            return F8RuntimeAvailability::ProducerUnavailable;
    }
}

void PublishInstallFailure(SeymourBattleInstallStatus status) {
    if (g_flag) {
        PublishF8RuntimeStatus(kSeymourKey, FailureAvailability(status), false, false);
    }
}

} // namespace

const char* SeymourBattleInstallStatusName(SeymourBattleInstallStatus status) {
    switch (status) {
        case SeymourBattleInstallStatus::Installed: return "Installed";
        case SeymourBattleInstallStatus::AlreadyInstalled: return "AlreadyInstalled";
        case SeymourBattleInstallStatus::ValidateOnly: return "ValidateOnly";
        case SeymourBattleInstallStatus::SharedRuntimeUnavailable:
            return "SharedRuntimeUnavailable";
        case SeymourBattleInstallStatus::UnsupportedProfile: return "UnsupportedProfile";
        case SeymourBattleInstallStatus::TargetOutOfRange: return "TargetOutOfRange";
        case SeymourBattleInstallStatus::SignatureMismatch: return "SignatureMismatch";
        case SeymourBattleInstallStatus::ExitCreateFailed: return "ExitCreateFailed";
        case SeymourBattleInstallStatus::ExitPublicationFailed:
            return "ExitPublicationFailed";
        case SeymourBattleInstallStatus::ExitEnableFailed: return "ExitEnableFailed";
        case SeymourBattleInstallStatus::CoordinatorUnavailable:
            return "CoordinatorUnavailable";
        case SeymourBattleInstallStatus::CoordinatorBusy: return "CoordinatorBusy";
        case SeymourBattleInstallStatus::CoordinatorPoisoned: return "CoordinatorPoisoned";
        case SeymourBattleInstallStatus::ComposerConflict: return "ComposerConflict";
        case SeymourBattleInstallStatus::RestorePending: return "RestorePending";
        default: return "Unknown";
    }
}

bool StartSeymourBattleHook(
    uintptr_t moduleBase, SeymourBattleLogFn log,
    SeymourBattleInstallStatus* statusOut) {
    const AdapterPublication current = static_cast<AdapterPublication>(
        g_publication.load(std::memory_order_acquire));
    if (current == AdapterPublication::Installed) {
        if (statusOut) *statusOut = SeymourBattleInstallStatus::AlreadyInstalled;
        return true;
    }
    uint32_t expected = static_cast<uint32_t>(AdapterPublication::Unpublished);
    if (!g_publication.compare_exchange_strong(
            expected, static_cast<uint32_t>(AdapterPublication::Installing),
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        if (statusOut) *statusOut = SeymourBattleInstallStatus::RestorePending;
        return false;
    }

    g_log = log;
    g_flag = FindF8Flag(kSeymourKey);
    ValidatedAdapter adapter{};
    SeymourBattleInstallStatus status = SeymourBattleInstallStatus::UnsupportedProfile;
    if (!g_flag || !ValidateAdapter(moduleBase, &adapter, &status)) {
        g_publication.store(static_cast<uint32_t>(AdapterPublication::Unsupported),
                            std::memory_order_release);
        PublishInstallFailure(status);
        if (statusOut) *statusOut = status;
        BattleLog("[seymour-battle] adapter rejected status=%s\n",
                  SeymourBattleInstallStatusName(status));
        return false;
    }

    const MinHookBatch::InitializationResult initialized =
        MinHookBatch::EnsureProcessInitialized();
    if (initialized != MinHookBatch::InitializationResult::Ready) {
        status = initialized == MinHookBatch::InitializationResult::Busy
            ? SeymourBattleInstallStatus::CoordinatorBusy
            : initialized == MinHookBatch::InitializationResult::Poisoned ||
                      initialized == MinHookBatch::InitializationResult::FailedPoisoned
                ? SeymourBattleInstallStatus::CoordinatorPoisoned
                : SeymourBattleInstallStatus::CoordinatorUnavailable;
        g_publication.store(static_cast<uint32_t>(AdapterPublication::Unsupported),
                            std::memory_order_release);
        PublishInstallFailure(status);
        if (statusOut) *statusOut = status;
        return false;
    }

    uintptr_t exitTarget = 0u;
    if (!AddressFromRva(moduleBase, kExitTargetRva, 25u, &exitTarget)) {
        status = SeymourBattleInstallStatus::TargetOutOfRange;
        g_publication.store(static_cast<uint32_t>(AdapterPublication::Unsupported),
                            std::memory_order_release);
        PublishInstallFailure(status);
        if (statusOut) *statusOut = status;
        return false;
    }

    CloseAdmission(&g_admission);
    const ExitDetourResult exitInstalled = InstallExitDetour(
        ExitCreateIo(), &MinHookBatch::ProcessCoordinator(),
        MinHookBatch::RuntimeBatchIo(), ExitFence(), exitTarget,
        reinterpret_cast<void*>(&SeymourExitShim), &g_exitOwner);
    if (exitInstalled != ExitDetourResult::Installed) {
        status = InstallStatusFor(exitInstalled);
        if (g_exitOwner.active) {
            // An Apply failure can leave the detour reachable. Publish ownership, not a terminal
            // install failure, and let normal-context Remove retry exact neutralization.
            g_teardownTerminal = AdapterPublication::Unsupported;
            (void)ArmTeardown(&g_teardown, true, false);
            g_publication.store(static_cast<uint32_t>(AdapterPublication::RestorePending),
                                std::memory_order_release);
            PublishF8RuntimeStatus(
                kSeymourKey, F8RuntimeAvailability::RestorePending, false, false);
        } else {
            g_publication.store(static_cast<uint32_t>(AdapterPublication::Unsupported),
                                std::memory_order_release);
            PublishInstallFailure(status);
        }
        if (statusOut) *statusOut = status;
        BattleLog("[seymour-battle] exit detour unavailable result=%s(%u) status=%s\n",
                  ExitDetourResultName(exitInstalled), static_cast<unsigned>(exitInstalled),
                  SeymourBattleInstallStatusName(status));
        return false;
    }

    const SharedBattleRuntime::ComposerSlotResult registered =
        F7_RegisterInitSceneComposer(&g_entryComposer);
    if (registered != SharedBattleRuntime::ComposerSlotResult::Registered &&
        registered != SharedBattleRuntime::ComposerSlotResult::AlreadyRegistered) {
        g_teardownTerminal = AdapterPublication::Conflict;
        const bool armed = ArmTeardown(&g_teardown, true, false);
        const TeardownResult rollback = armed
            ? AdvanceTeardown(&g_teardown, true, TeardownOperations())
            : TeardownResult::InvalidArgument;
        const bool rollbackComplete = rollback == TeardownResult::Complete;
        status = rollbackComplete
            ? SeymourBattleInstallStatus::ComposerConflict
            : SeymourBattleInstallStatus::RestorePending;
        g_publication.store(
            static_cast<uint32_t>(rollbackComplete
                ? AdapterPublication::Conflict
                : AdapterPublication::RestorePending),
            std::memory_order_release);
        if (rollbackComplete) {
            PublishInstallFailure(SeymourBattleInstallStatus::ComposerConflict);
        } else {
            PublishF8RuntimeStatus(
                kSeymourKey, F8RuntimeAvailability::RestorePending, false, false);
        }
        if (statusOut) *statusOut = status;
        BattleLog(
            "[seymour-battle] composer registration rejected result=%s(%u) "
            "rollback=%s(%u) exit_retirement=%s(%u)\n",
            ComposerSlotResultName(registered), static_cast<unsigned>(registered),
            TeardownResultName(rollback), static_cast<unsigned>(rollback),
            ExitDetourResultName(g_lastExitRetirement),
            static_cast<unsigned>(g_lastExitRetirement));
        return false;
    }

    g_teardownTerminal = AdapterPublication::Stopped;
    if (!ArmTeardown(&g_teardown, true, true)) {
        status = SeymourBattleInstallStatus::RestorePending;
        g_publication.store(static_cast<uint32_t>(AdapterPublication::RestorePending),
                            std::memory_order_release);
        PublishF8RuntimeStatus(
            kSeymourKey, F8RuntimeAvailability::RestorePending, false, false);
        if (statusOut) *statusOut = status;
        return false;
    }

    g_adapter = adapter;
    g_adapterPublicationPtr.store(&g_adapter, std::memory_order_release);
    OpenAdmission(&g_admission);
    g_publication.store(static_cast<uint32_t>(AdapterPublication::Installed),
                        std::memory_order_release);
    const ProducerStartupDisposition producerAtInstall = ClassifyProducerAtInstall(
        static_cast<ProducerPublication>(
            g_producer.load(std::memory_order_acquire)));
    F8RuntimeAvailability producerAvailability = F8RuntimeAvailability::Pending;
    bool producerOperational = false;
    switch (producerAtInstall) {
        case ProducerStartupDisposition::Available:
            producerAvailability = F8RuntimeAvailability::Available;
            producerOperational = true;
            break;
        case ProducerStartupDisposition::ProducerUnavailable:
            // WHY: Present can terminate before the delayed worker reaches Start. Terminal is
            // absorbing, so startup must preserve the earlier failure instead of publishing the
            // less truthful PENDING state.
            producerAvailability = F8RuntimeAvailability::ProducerUnavailable;
            break;
        case ProducerStartupDisposition::Pending:
        default:
            break;
    }
    PublishF8RuntimeStatus(
        kSeymourKey, producerAvailability, producerOperational, false);
    if (g_producer.load(std::memory_order_acquire) ==
        static_cast<uint32_t>(ProducerPublication::Terminal)) {
        // Close the only race in which Terminal linearizes after classification but before the
        // initial status publish. If it linearizes later, the terminal notifier publishes again.
        PublishF8RuntimeStatus(
            kSeymourKey, F8RuntimeAvailability::ProducerUnavailable, false, false);
    }
    BattleLog(
        "[seymour-battle] installed profile=ffx-pe32-55D2F3CC base=0x%08X "
        "entry_return_rva=0x%08X exit_target_rva=0x%08X default=OFF\n",
        static_cast<unsigned>(moduleBase), kSeymourBattleEntryReturnRva,
        kExitTargetRva);
    if (statusOut) *statusOut = SeymourBattleInstallStatus::Installed;
    return true;
}

void NotifySeymourBattlePresentProducer(bool ready, bool terminalFailure) {
    const ProducerPublication producer =
        PublishProducerState(&g_producer, ready, terminalFailure);
    if (terminalFailure) {
        (void)PublishRequested(&g_commands, false);
        Telemetry telemetry{};
        const bool restorePending = ReadTelemetry(&g_telemetry, &telemetry) &&
            telemetry.state == State::RestorePending;
        PublishF8RuntimeStatus(
            kSeymourKey,
            restorePending ? F8RuntimeAvailability::RestorePending
                           : F8RuntimeAvailability::ProducerUnavailable,
            false, false);
        return;
    }
    if (!ready || producer != ProducerPublication::Ready) {
        return;
    }
    if (g_publication.load(std::memory_order_acquire) ==
        static_cast<uint32_t>(AdapterPublication::Installed)) {
        PublishF8RuntimeStatus(kSeymourKey, F8RuntimeAvailability::Available, true, false);
        if (g_producer.load(std::memory_order_acquire) ==
            static_cast<uint32_t>(ProducerPublication::Terminal)) {
            // A terminal edge that raced the Available publish is reasserted here; if it arrives
            // after this read, its own notifier performs the same unavailable publication.
            PublishF8RuntimeStatus(
                kSeymourKey, F8RuntimeAvailability::ProducerUnavailable, false, false);
        }
    }
}

void SeymourBattlePresentTick() {
    if (g_publication.load(std::memory_order_acquire) !=
            static_cast<uint32_t>(AdapterPublication::Installed) ||
        g_producer.load(std::memory_order_acquire) !=
            static_cast<uint32_t>(ProducerPublication::Ready) || !g_flag) {
        return;
    }
    const Config::BoolGateResult gate = ResolveF8Flag(*g_flag);
    if (!PublishRequested(&g_commands, gate.value)) return;
    const Command published = ReadCommand(&g_commands);

    Telemetry telemetry{};
    const bool hasTelemetry = ReadTelemetry(&g_telemetry, &telemetry);
    const bool telemetryCurrent = hasTelemetry &&
        telemetry.generation == published.generation;
    const F8RuntimeAvailability availability = telemetryCurrent
        ? AvailabilityFor(telemetry)
        : (!hasTelemetry && !gate.value)
            ? F8RuntimeAvailability::Available
            : F8RuntimeAvailability::Pending;
    const bool hasApplied = telemetryCurrent &&
        (telemetry.state == State::AppliedBattleRoster ||
         telemetry.state == State::Off);
    const bool applied = telemetryCurrent &&
        telemetry.state == State::AppliedBattleRoster;
    PublishF8RuntimeStatus(kSeymourKey, availability, hasApplied, applied);

    const bool telemetryChanged = hasTelemetry &&
        (!g_hasPresentEdge || !SameTelemetry(g_priorTelemetry, telemetry));
    if (!g_hasPresentEdge || g_priorRequested != gate.value || telemetryChanged) {
        g_hasPresentEdge = true;
        g_priorRequested = gate.value;
        if (hasTelemetry) g_priorTelemetry = telemetry;
        if (telemetryCurrent && telemetryChanged) {
            // KEY: the manual F8 protocol consumes these exact RuntimeAcknowledged edges. Generic
            // UI attestations never substitute for the separate raw-memory Seymour evidence.
            if (telemetry.outcome == ServiceOutcome::Applied && gate.value) {
                BattleLog("[f8-runtime] key=%s effective=%d source=%s state=applied readback=01\n",
                          kSeymourKey, 1, Config::BoolSourceName(gate.source));
            } else if (telemetry.outcome == ServiceOutcome::Restored && !gate.value) {
                BattleLog("[f8-runtime] key=%s effective=%d source=%s state=restored readback=00\n",
                          kSeymourKey, 0, Config::BoolSourceName(gate.source));
            }
            if (const char* failure = FailureName(telemetry.outcome)) {
                BattleLog("[f8-runtime] key=%s failure=%s\n", kSeymourKey, failure);
            }
        }
        BattleLog(
            "[seymour-battle] requested=%d source=%s generation=%lu callback=%s "
            "caller_rva=%08lX discriminator=%lu state=%s outcome=%s "
            "before=%08lX apply=%08lX restore=%08lX\n",
            gate.value ? 1 : 0, Config::BoolSourceName(gate.source),
            static_cast<unsigned long>(published.generation),
            CallbackName(hasTelemetry ? telemetry.callback : CallbackKind::None),
            static_cast<unsigned long>(hasTelemetry ? telemetry.returnAddress : 0u),
            static_cast<unsigned long>(hasTelemetry ? telemetry.battleDiscriminator : 0u),
            StateName(hasTelemetry ? telemetry.state : State::PendingBattle),
            OutcomeName(hasTelemetry ? telemetry.outcome : ServiceOutcome::NoChange),
            static_cast<unsigned long>(hasTelemetry ? telemetry.beforeHash : 0u),
            static_cast<unsigned long>(hasTelemetry ? telemetry.applyHash : 0u),
            static_cast<unsigned long>(hasTelemetry ? telemetry.restoreHash : 0u));
    }
}

void RequestSeymourBattleStop() {
    SeymourBattle::RequestStop(&g_commands);
    CloseAdmission(&g_admission);
}

bool RemoveSeymourBattleHook() {
    if (g_teardown.phase == TeardownPhase::Complete) return true;
    if (g_teardown.phase == TeardownPhase::Inactive) {
        const AdapterPublication publication = static_cast<AdapterPublication>(
            g_publication.load(std::memory_order_acquire));
        return publication == AdapterPublication::Unpublished ||
               publication == AdapterPublication::Unsupported ||
               publication == AdapterPublication::Conflict ||
               publication == AdapterPublication::Stopped;
    }

    // Absorbing stop prevents Present from republishing ON while admission remains open long
    // enough for an owned battle's exact exit/next-entry cleanup callback to run.
    SeymourBattle::RequestStop(&g_commands);
    const TeardownPreparation preparation = PrepareTeardownAdmission();
    if (preparation != TeardownPreparation::Ready) {
        PublishF8RuntimeStatus(
            kSeymourKey, F8RuntimeAvailability::RestorePending, false, false);
        BattleLog("[seymour-battle] teardown deferred preparation=%s(%u)\n",
                  TeardownPreparationName(preparation), static_cast<unsigned>(preparation));
        return false;
    }

    const TeardownResult retired = AdvanceTeardown(
        &g_teardown, true, TeardownOperations());
    if (retired != TeardownResult::Complete) {
        PublishF8RuntimeStatus(
            kSeymourKey, F8RuntimeAvailability::RestorePending, false, false);
        BattleLog(
            "[seymour-battle] teardown retry result=%s(%u) exit=%s(%u) "
            "composer=%s(%u)\n",
            TeardownResultName(retired), static_cast<unsigned>(retired),
            ExitDetourResultName(g_lastExitRetirement),
            static_cast<unsigned>(g_lastExitRetirement),
            ComposerSlotResultName(g_lastComposerUnregister),
            static_cast<unsigned>(g_lastComposerUnregister));
        return false;
    }

    g_adapterPublicationPtr.store(nullptr, std::memory_order_release);
    g_publication.store(static_cast<uint32_t>(g_teardownTerminal),
                        std::memory_order_release);
    PublishF8RuntimeStatus(
        kSeymourKey,
        g_teardownTerminal == AdapterPublication::Conflict
            ? F8RuntimeAvailability::Conflict
            : F8RuntimeAvailability::ProducerUnavailable,
        false, false);
    BattleLog(
        "[seymour-battle] teardown complete exit=%s(%u) composer=%s(%u) "
        "terminal=%s(%u)\n",
        ExitDetourResultName(g_lastExitRetirement),
        static_cast<unsigned>(g_lastExitRetirement),
        ComposerSlotResultName(g_lastComposerUnregister),
        static_cast<unsigned>(g_lastComposerUnregister),
        AdapterPublicationName(g_teardownTerminal),
        static_cast<unsigned>(g_teardownTerminal));
    return true;
}

SeymourBattleRuntimeSnapshot GetSeymourBattleRuntimeSnapshot() {
    SeymourBattleRuntimeSnapshot snapshot{};
    Telemetry telemetry{};
    if (ReadTelemetry(&g_telemetry, &telemetry)) {
        snapshot.state = telemetry.state;
        snapshot.outcome = telemetry.outcome;
        snapshot.generation = telemetry.generation;
        snapshot.threadId = telemetry.threadId;
        snapshot.callback = telemetry.callback;
        snapshot.returnAddress = telemetry.returnAddress;
        snapshot.battleDiscriminator = telemetry.battleDiscriminator;
        snapshot.beforeHash = telemetry.beforeHash;
        snapshot.applyHash = telemetry.applyHash;
        snapshot.restoreHash = telemetry.restoreHash;
    }
    snapshot.installed = g_publication.load(std::memory_order_acquire) ==
        static_cast<uint32_t>(AdapterPublication::Installed);
    snapshot.producerReady = g_producer.load(std::memory_order_acquire) ==
        static_cast<uint32_t>(ProducerPublication::Ready);
    return snapshot;
}

} // namespace FfxHooks
