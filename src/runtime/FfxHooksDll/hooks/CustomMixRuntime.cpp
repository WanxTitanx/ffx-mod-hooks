#include "CustomMixRuntime.h"
#include "ArenaBattleProgram.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace FfxHooks::CustomMixUltra::Runtime {
namespace {

using namespace WindowsAdapter;

SRWLOCK g_requestLock = SRWLOCK_INIT;
RequestState g_requestState;
volatile LONG g_productionAccepting = 0;
std::uintptr_t g_productionImageBase = 0u;
struct ActivePositions {
    ArenaPositions::Layout layout{};
    ExpandedSelection selection{};
    std::uint32_t carrierAddress = 0u;
    DWORD threadId = 0u;
    std::uint8_t nativeOwnedSlots = 0u;
};
ActivePositions g_activePositions{};
ArenaBattleProgram::Frame* g_preparedFrame=nullptr;
struct ActiveBattlefield {
    BattlefieldLease lease{};
    std::uint32_t carrierAddress = 0u;
    DWORD threadId = 0u;
};
ActiveBattlefield g_activeBattlefield{};

// Native InitScene and its caller both consume the low uint16 at this address.
// The high uint16 is the native field-table index (71 for the exact dome carrier).
constexpr std::uint32_t kBattlefieldRva = 0x00D2C254u;
static std::uint16_t* ProbeBattlefield(std::uintptr_t image, std::uint32_t carrier) noexcept {
    if (!image || !carrier || image > UINT32_MAX - kBattlefieldRva - 6u) return nullptr;
    const auto address = image + kBattlefieldRva;
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region)) != sizeof(region) ||
        region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD) != 0 ||
        ((region.Protect & 0xFFu) != PAGE_READWRITE && (region.Protect & 0xFFu) != PAGE_WRITECOPY &&
         (region.Protect & 0xFFu) != PAGE_EXECUTE_READWRITE && (region.Protect & 0xFFu) != PAGE_EXECUTE_WRITECOPY))
        return nullptr;
    const auto first = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
    if (address < first || region.RegionSize < 6u || address - first > region.RegionSize - 6u)
        return nullptr;
    __try {
        if (*reinterpret_cast<const std::uint32_t*>(image + kCarrierPointerRva) != carrier ||
            *reinterpret_cast<const std::uint16_t*>(address + 2u) != 71u ||
            *reinterpret_cast<const std::uint8_t*>(address + 4u) != 0u ||
            *reinterpret_cast<const std::uint8_t*>(address + 5u) != 0u ||
            std::memcmp(reinterpret_cast<const void*>(image + kCarrierNameRva), "dome02_00", 10u) != 0)
            return nullptr;
        return reinterpret_cast<std::uint16_t*>(address);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static void RestoreOwnedBattlefield(bool afterDrain) noexcept {
    if (!g_activeBattlefield.lease.active) return;
    if (!afterDrain && g_activeBattlefield.threadId != GetCurrentThreadId()) return;
    auto* field = ProbeBattlefield(g_productionImageBase, g_activeBattlefield.carrierAddress);
    if (field) {
        __try { RestoreBattlefield(&g_activeBattlefield.lease, field); }
        __except (EXCEPTION_EXECUTE_HANDLER) { /* Native teardown may already own the page. */ }
    }
    // A changed native encounter/context owns the selector now; never restore over it.
    g_activeBattlefield = {};
}

StatusCode StatusForOutcome(const SelectionOutcome& outcome) noexcept {
    if (outcome.result == SelectionResult::Empty) return StatusCode::Empty;
    if (outcome.result != SelectionResult::Ready) return StatusCode::Failed;
    return outcome.expanded.monsterCount == kMonsterSlotCount
        ? StatusCode::Full
        : StatusCode::Ready;
}

SelectionEditResult DescribeSelection(
    const SelectionInput& selection, bool accepted) noexcept {
    SelectionEditResult result{};
    result.accepted = accepted;
    result.selection = selection;
    result.preview = BuildSelection(selection);
    result.status = StatusForOutcome(result.preview);
    return result;
}

struct NestedCallContext {
    NestedInitSceneIo io{};
    int originalResult = 0;
    bool originalResultAvailable = false;
};

bool InvokeNestedAsTransactionOriginal(void* rawContext) {
    NestedCallContext& context = *static_cast<NestedCallContext*>(rawContext);
    int result = 0;
    const bool accepted = context.io.invoke(context.io.context, &result);
    context.originalResult = result;
    context.originalResultAvailable = true;
    return accepted;
}

bool ReadExactU16(std::uint32_t address, U16ProbeRead* result) noexcept {
    if (!result || address == 0u) return false;
    __try {
        result->value = *reinterpret_cast<volatile const std::uint16_t*>(
            static_cast<std::uintptr_t>(address));
        result->bytesRead = sizeof(result->value);
        result->succeeded = true;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *result = {};
        return false;
    }
}

bool ReadExactPointer32(
    std::uint32_t address, Pointer32ProbeRead* result) noexcept {
    if (!result || address == 0u) return false;
    __try {
        result->value = *reinterpret_cast<volatile const std::uint32_t*>(
            static_cast<std::uintptr_t>(address));
        result->bytesRead = sizeof(result->value);
        result->succeeded = true;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *result = {};
        return false;
    }
}

bool ReadExactName10(std::uint32_t address, Name10ProbeRead* result) noexcept {
    if (!result || address == 0u) return false;
    __try {
        const volatile char* source = reinterpret_cast<volatile const char*>(
            static_cast<std::uintptr_t>(address));
        for (std::size_t index = 0u; index < result->value.size(); ++index) {
            result->value[index] = source[index];
        }
        result->bytesRead = result->value.size();
        result->succeeded = true;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *result = {};
        return false;
    }
}

bool ExactAddress(
    std::uint32_t imageBase,
    std::uint32_t rva,
    std::uint32_t actual) noexcept {
    const std::uint64_t expected =
        static_cast<std::uint64_t>(imageBase) + static_cast<std::uint64_t>(rva);
    return expected <= std::numeric_limits<std::uint32_t>::max() &&
           actual == static_cast<std::uint32_t>(expected);
}

class FixedProductionMemoryProbe final : public MemoryProbe {
public:
    explicit FixedProductionMemoryProbe(std::uint32_t imageBase) noexcept
        : imageBase_(imageBase) {}

    U16ProbeRead ReadCarrierSizeU16(
        std::uint32_t address) const noexcept override {
        U16ProbeRead result{};
        if (!ExactAddress(imageBase_, kCarrierSizeRva, address)) return result;
        (void)ReadExactU16(address, &result);
        return result;
    }

    Pointer32ProbeRead ReadCarrierPointer32(
        std::uint32_t address) const noexcept override {
        Pointer32ProbeRead result{};
        if (!ExactAddress(imageBase_, kCarrierPointerRva, address)) return result;
        (void)ReadExactPointer32(address, &result);
        return result;
    }

    Name10ProbeRead ReadCarrierName10(
        std::uint32_t address) const noexcept override {
        Name10ProbeRead result{};
        if (!ExactAddress(imageBase_, kCarrierNameRva, address)) return result;
        (void)ReadExactName10(address, &result);
        return result;
    }

private:
    std::uint32_t imageBase_ = 0u;
};

RegionState MapRegionState(DWORD state) noexcept {
    switch (state) {
    case MEM_COMMIT: return RegionState::Commit;
    case MEM_RESERVE: return RegionState::Reserve;
    case MEM_FREE: return RegionState::Free;
    default: return RegionState::Unknown;
    }
}

CarrierViewOutcome ProbeCarrierInternal(
    std::uint64_t imageBase, std::uint32_t* virtualQueryCalls) noexcept {
    if (virtualQueryCalls) *virtualQueryCalls = 0u;
    if (imageBase == 0u ||
        imageBase > std::numeric_limits<std::uint32_t>::max()) {
        CarrierViewOutcome rejected{};
        rejected.code = AdapterCode::InvalidImageBase;
        return rejected;
    }

    const std::uint32_t imageBase32 = static_cast<std::uint32_t>(imageBase);
    const FixedProductionMemoryProbe probe(imageBase32);
    const GlobalReadOutcome globals = ReadCarrierGlobals(imageBase, &probe);
    if (globals.code != AdapterCode::Ready) {
        CarrierViewOutcome rejected{};
        rejected.code = globals.code;
        return rejected;
    }

    MEMORY_BASIC_INFORMATION memory{};
    if (virtualQueryCalls) ++*virtualQueryCalls;
    const SIZE_T queried = VirtualQuery(
        reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(globals.globals.carrierAddress)),
        &memory,
        sizeof(memory));
    if (queried != sizeof(memory) ||
        reinterpret_cast<std::uintptr_t>(memory.BaseAddress) >
            std::numeric_limits<std::uint32_t>::max() ||
        memory.RegionSize > std::numeric_limits<std::uint32_t>::max()) {
        CarrierViewOutcome rejected{};
        rejected.code = AdapterCode::InvalidRegion;
        return rejected;
    }

    RegionInfo region{};
    region.baseAddress = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(memory.BaseAddress));
    region.regionSize = static_cast<std::uint32_t>(memory.RegionSize);
    region.state = MapRegionState(memory.State);
    region.protection = memory.Protect;
    return MaterializeCarrierView(globals.globals, region);
}

struct FixedCarrierContext {
    CarrierViewOutcome outcome{};
};

bool MaterializeFixedCarrier(void* rawContext, CarrierView* carrierOut) noexcept {
    if (!rawContext || !carrierOut) return false;
    const FixedCarrierContext& context =
        *static_cast<const FixedCarrierContext*>(rawContext);
    if (context.outcome.code != AdapterCode::Ready) return false;
    *carrierOut = context.outcome.carrier;
    return true;
}

struct NormalViewContext {
    NestedInitSceneIo original{};
    PendingRequest* request=nullptr;
    ArenaBattleProgram::Frame* frame=nullptr;
    std::uintptr_t image=0;
    std::uint32_t owner=0;
    bool used=false;
};

// Loan only the root pointer/size while native InitScene derives its references.
// Restore the engine-owned allocation afterwards. The derived references keep the
// normal frame, which is pinned for native ATEL/formation/position consumers.
static bool ExchangeRoot(const NormalViewContext& c, bool enter) noexcept {
    if(!c.frame||!c.image||!c.owner||c.frame->bytes.empty()||c.frame->bytes.size()>=65536u)return false;
    const auto address=c.image+kCarrierSizeRva;
    MEMORY_BASIC_INFORMATION region{};
    if(VirtualQuery(reinterpret_cast<void*>(address),&region,sizeof(region))!=sizeof(region)||region.State!=MEM_COMMIT||
       (region.Protect&PAGE_GUARD)!=0||((region.Protect&0xFFu)!=PAGE_READWRITE&&(region.Protect&0xFFu)!=PAGE_EXECUTE_READWRITE&&
       (region.Protect&0xFFu)!=PAGE_WRITECOPY&&(region.Protect&0xFFu)!=PAGE_EXECUTE_WRITECOPY))return false;
    const auto first=reinterpret_cast<std::uintptr_t>(region.BaseAddress);
    if(address<first||region.RegionSize<6u||address-first>region.RegionSize-6u)return false;
    const auto view=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(c.frame->bytes.data()));
    __try {
        auto* size=reinterpret_cast<volatile std::uint16_t*>(address);
        auto* root=reinterpret_cast<volatile std::uint32_t*>(c.image+kCarrierPointerRva);
        const auto expected=enter?c.owner:view;
        const auto expectedSize=enter?static_cast<std::uint16_t>(kCarrierSize):static_cast<std::uint16_t>(c.frame->bytes.size());
        const bool rootOwned=*root==expected,sizeOwned=*size==expectedSize;
        if(enter){
            if(!rootOwned||!sizeOwned)return false;
            *size=static_cast<std::uint16_t>(c.frame->bytes.size());*root=view;
        }else{
            // Restore each still-owned field independently, including a partial
            // conflict. Never leave our CRT-backed view as the engine's file owner.
            if(rootOwned)*root=c.owner;
            if(sizeOwned)*size=static_cast<std::uint16_t>(kCarrierSize);
        }
        return rootOwned&&sizeOwned;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool NativeReferencesMatch(const NormalViewContext& c) noexcept {
    const auto* b=c.frame->bytes.data();const auto start=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(b));
    __try {
        const auto expectedScript=start+*reinterpret_cast<const std::uint32_t*>(b+4);
        const auto expectedMap=start+*reinterpret_cast<const std::uint32_t*>(b+8);
        const auto expectedFormation=start+*reinterpret_cast<const std::uint32_t*>(b+12);
        const auto expectedArea=start+*reinterpret_cast<const std::uint32_t*>(b+16);
        return *reinterpret_cast<const std::uint32_t*>(c.image+0x00D2A9ACu)==expectedScript&&
            *reinterpret_cast<const std::uint32_t*>(c.image+0x00D2A9B4u)==expectedMap&&
            *reinterpret_cast<const std::uint32_t*>(c.image+0x00D2A9C0u)==expectedFormation&&
            *reinterpret_cast<const std::uint32_t*>(c.image+0x00D2A9B0u)==expectedArea&&
            *reinterpret_cast<const std::uint8_t*>(c.image+0x00D2A9A4u)==2u;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool InvokeNormalView(void* raw, int* result) {
    auto& c=*static_cast<NormalViewContext*>(raw);
    if(!c.original.invoke)return false;
    if(!c.request||!c.request->composing)return c.original.invoke(c.original.context,result);
    if(!ExchangeRoot(c,true)){
        c.original.invoke(c.original.context,result);
        return false;
    }
    c.frame->published=true; // No native borrower may outlive this storage.
    bool accepted=false, references=false;
    try {
        accepted=c.original.invoke(c.original.context,result);
        references=NativeReferencesMatch(c);
    } catch(...) {
        ExchangeRoot(c,false);
        throw;
    }
    const bool restored=ExchangeRoot(c,false);
    c.used=accepted&&references&&restored;
    return c.used;
}

}  // namespace

const char* StatusName(StatusCode code) noexcept {
    switch (code) {
    case StatusCode::Empty: return "EMPTY";
    case StatusCode::Full: return "FULL";
    case StatusCode::Ready: return "READY";
    case StatusCode::Queued: return "QUEUED";
    case StatusCode::Consumed: return "CONSUMED";
    case StatusCode::Failed: return "FAILED";
    case StatusCode::RestoreConflict: return "RESTORE CONFLICT";
    case StatusCode::Expired: return "EXPIRED";
    case StatusCode::Unavailable: return "UNAVAILABLE";
    default: return "UNAVAILABLE";
    }
}

StatusCode ClassifySelection(const SelectionInput& selection) noexcept {
    return StatusForOutcome(BuildSelection(selection));
}

SelectionEditResult TryAddChoice(
    const SelectionInput& selection, MonsterChoice choice) noexcept {
    if (selection.activationCount >= selection.activations.size()) {
        return DescribeSelection(selection, false);
    }
    SelectionInput candidate = selection;
    candidate.activations[candidate.activationCount++] = choice;
    if (BuildSelection(candidate).result != SelectionResult::Ready) {
        return DescribeSelection(selection, false);
    }
    if (selection.positions.enabled || selection.positions.automatic)
        candidate.positions = ArenaScenery::Generate(candidate.scenery, BuildSelection(candidate).expanded.monsterCount);
    return DescribeSelection(candidate, true);
}

SelectionEditResult RemoveLastChoice(const SelectionInput& selection) noexcept {
    if (selection.activationCount == 0u ||
        selection.activationCount > selection.activations.size()) {
        return DescribeSelection(selection, false);
    }
    SelectionInput candidate = selection;
    --candidate.activationCount;
    candidate.activations[candidate.activationCount] = MonsterChoice::Valefor;
    if (selection.positions.enabled || selection.positions.automatic)
        candidate.positions = ArenaScenery::Generate(candidate.scenery, BuildSelection(candidate).expanded.monsterCount);
    return DescribeSelection(candidate, true);
}

SelectionEditResult ClearSelection() noexcept {
    return DescribeSelection(SelectionInput{}, true);
}

std::uint64_t RequestState::NextGeneration(std::uint64_t current) noexcept {
    return current == std::numeric_limits<std::uint64_t>::max()
        ? 1u
        : current + 1u;
}

void RequestState::Publish(
    StatusCode code,
    std::uint64_t generation,
    TransactionResult result) noexcept {
    status_.code = code;
    status_.generation = generation;
    status_.transactionResult = result;
}

void RequestState::Start(bool exactProfileReady, bool validateOnly) noexcept {
    pending_ = {};
    ready_ = exactProfileReady && !validateOnly && !restoreConflictLatched_;
    if (restoreConflictLatched_) {
        Publish(StatusCode::RestoreConflict, status_.generation,
                TransactionResult::RestoreConflict);
    } else {
        Publish(ready_ ? StatusCode::Empty : StatusCode::Unavailable, 0u,
                TransactionResult::NotArmed);
    }
}

bool RequestState::IsReady() const noexcept { return ready_; }

bool RequestState::RestoreConflictLatched() const noexcept {
    return restoreConflictLatched_;
}

StatusSnapshot RequestState::Status() const noexcept { return status_; }

void RequestState::PublishSelection(const SelectionInput& selection) noexcept {
    if (!ready_ || restoreConflictLatched_ || pending_.armed) return;
    Publish(ClassifySelection(selection), 0u, TransactionResult::NotArmed);
}

bool RequestState::Arm(
    const SelectionInput& selection, std::uint64_t nowTick) noexcept {
    if (!ready_ || restoreConflictLatched_) return false;
    if (!ArenaScenery::Get(selection.scenery) || !ArenaScenery::ValidCamera(selection.camera)) {
        pending_ = {};
        Publish(StatusCode::Failed, status_.generation, TransactionResult::InvalidScenery);
        return false;
    }
    if (ArenaPositions::Validate(selection.positions, BuildSelection(selection).expanded.monsterCount) !=
        ArenaPositions::Issue::None) {
        pending_ = {};
        Publish(StatusCode::Failed, status_.generation, TransactionResult::InvalidPositions);
        return false;
    }
    if (BuildSelection(selection).result != SelectionResult::Ready ||
        nowTick > std::numeric_limits<std::uint64_t>::max() - kRequestTtlMs) {
        pending_ = {};
        Publish(StatusCode::Failed, status_.generation, TransactionResult::InvalidDeadline);
        return false;
    }

    // WHY: replacing a request is the only new-generation operation. The old
    // request becomes unreachable before the next nonzero generation is published.
    pending_ = {};
    generationCounter_ = NextGeneration(generationCounter_);
    pending_.enabled = true;
    pending_.armed = true;
    pending_.generation = generationCounter_;
    pending_.deadlineTick = nowTick + kRequestTtlMs;
    pending_.selection = selection;
    Publish(StatusCode::Queued, generationCounter_, TransactionResult::NotArmed);
    return true;
}

PendingClaim RequestState::Claim(std::uint64_t nowTick) noexcept {
    PendingClaim claim{};
    if (!ready_ || restoreConflictLatched_ || !pending_.armed) return claim;

    // WHY: copy and clear precede deadline, selection, carrier, and metadata
    // validation. No rejected battle can leave the one-shot generation armed.
    claim.claimed = true;
    claim.request = pending_;
    claim.observation.generation = pending_.generation;
    claim.observation.nowTick = nowTick;
    pending_ = {};
    Publish(StatusCode::Consumed, claim.request.generation,
            TransactionResult::NotArmed);
    return claim;
}

void RequestState::Cancel(CancelReason reason) noexcept {
    const std::uint64_t generation = pending_.armed
        ? pending_.generation
        : status_.generation;
    pending_ = {};
    if (restoreConflictLatched_) {
        Publish(StatusCode::RestoreConflict, generation,
                TransactionResult::RestoreConflict);
        return;
    }
    switch (reason) {
    case CancelReason::QueueFailure:
    case CancelReason::ArmFailure:
        Publish(StatusCode::Failed, generation, TransactionResult::NotArmed);
        break;
    case CancelReason::Timeout:
        Publish(StatusCode::Expired, generation, TransactionResult::Expired);
        break;
    case CancelReason::ValidateOnly:
    case CancelReason::Stop:
    case CancelReason::Unavailable:
        ready_ = false;
        Publish(StatusCode::Unavailable, generation, TransactionResult::NotArmed);
        break;
    default:
        Publish(StatusCode::Empty, 0u, TransactionResult::NotArmed);
        break;
    }
}

void RequestState::Tick(std::uint64_t nowTick) noexcept {
    if (pending_.armed && pending_.deadlineTick != 0u &&
        nowTick >= pending_.deadlineTick) {
        Cancel(CancelReason::Timeout);
    }
}

void RequestState::PublishTransaction(
    std::uint64_t generation, const TransactionOutcome& outcome) noexcept {
    if (outcome.result == TransactionResult::RestoreConflict) {
        restoreConflictLatched_ = true;
        ready_ = false;
        pending_ = {};
        Publish(StatusCode::RestoreConflict, generation, outcome.result);
        return;
    }
    // A newer queued generation owns the visible status. The completed older
    // transaction may not overwrite it after returning from vanilla.
    if (pending_.armed && pending_.generation != generation) return;
    if (status_.generation != generation) return;

    if (outcome.result == TransactionResult::Restored) {
        Publish(StatusCode::Consumed, generation, outcome.result);
    } else if (outcome.result == TransactionResult::Expired) {
        Publish(StatusCode::Expired, generation, outcome.result);
    } else {
        Publish(StatusCode::Failed, generation, outcome.result);
    }
}

void RequestState::Stop() noexcept {
    ready_ = false;
    pending_ = {};
    if (restoreConflictLatched_) {
        Publish(StatusCode::RestoreConflict, status_.generation,
                TransactionResult::RestoreConflict);
    } else {
        Publish(StatusCode::Unavailable, 0u, TransactionResult::NotArmed);
    }
}

void RequestState::ResetAfterDrain() noexcept { Stop(); }

LaunchOutcome QueueThenArm(
    const SelectionInput& selection,
    std::uint64_t nowTick,
    const LaunchIo& io) noexcept {
    LaunchOutcome outcome{};
    if (!ArenaScenery::Get(selection.scenery) || !ArenaScenery::ValidCamera(selection.camera) ||
        BuildSelection(selection).result != SelectionResult::Ready ||
        ArenaPositions::Validate(selection.positions, BuildSelection(selection).expanded.monsterCount) !=
            ArenaPositions::Issue::None) {
        outcome.code = LaunchCode::InvalidSelection;
        if (io.cancelRequest) io.cancelRequest(io.context, CancelReason::ArmFailure);
        return outcome;
    }
    if (!io.queueCarrier || !io.armRequest) {
        outcome.code = LaunchCode::InvalidIo;
        if (io.cancelRequest) io.cancelRequest(io.context, CancelReason::ArmFailure);
        return outcome;
    }
    if (io.prepareSelection && !io.prepareSelection(io.context, selection)) {
        outcome.code=LaunchCode::PreparationFailed;
        if(io.cancelRequest)io.cancelRequest(io.context,CancelReason::ArmFailure);
        return outcome;
    }

    // KEY: queueCarrier is the first side effect. Only the complete exact queue
    // evidence may reach the arm callback; no pre-queue request exists to race.
    outcome.queue = io.queueCarrier(io.context);
    outcome.carrierQueued = outcome.queue.callSucceeded &&
        outcome.queue.returnValue == -1 && outcome.queue.queueArmed;
    if (!outcome.carrierQueued) {
        outcome.code = LaunchCode::QueueFailed;
        if (io.cancelRequest) io.cancelRequest(io.context, CancelReason::QueueFailure);
        return outcome;
    }

    outcome.requestArmed = io.armRequest(io.context, selection, nowTick);
    if (!outcome.requestArmed) {
        outcome.code = LaunchCode::ArmFailed;
        if (io.cancelRequest) io.cancelRequest(io.context, CancelReason::ArmFailure);
        return outcome;
    }
    outcome.code = LaunchCode::QueuedAndArmed;
    return outcome;
}

EditorLaunchOutcome LaunchEditorSelection(
    const SelectionInput& selection,
    std::uint64_t nowTick,
    const LaunchIo& io) noexcept {
    EditorLaunchOutcome outcome{};
    outcome.selection = selection;
    outcome.launch = QueueThenArm(selection, nowTick, io);

    if (outcome.launch.code == LaunchCode::QueuedAndArmed) {
        outcome.selection = {};
        outcome.disposition = EditorLaunchDisposition::CloseWithArmedRequest;
    } else if (outcome.launch.carrierQueued) {
        // WHY: a queue-success/arm-failure carrier is already owned by vanilla.
        // Closing prevents a retry while preserving the cancellation published by QueueThenArm.
        outcome.selection = {};
        outcome.disposition = EditorLaunchDisposition::CloseWithVanillaCarrier;
    }
    return outcome;
}

BattleCompositionOutcome RunBattleComposition(
    PendingRequest* request,
    const Observation& observation,
    const BattleCompositionIo& io) noexcept {
    BattleCompositionOutcome outcome{};
    CarrierView carrier{};
    if (request && request->armed) {
        outcome.carrierProbeAttempted = true;
        outcome.carrierProbeSucceeded = io.carrier.materialize &&
            io.carrier.materialize(io.carrier.context, &carrier);
    }

    NestedCallContext nested{};
    nested.io = io.nested;
    const InvokeOriginal original = io.nested.invoke
        ? &InvokeNestedAsTransactionOriginal
        : nullptr;
    outcome.transaction = ExecuteTransaction(
        request, observation, carrier, original, &nested);
    outcome.originalResult = nested.originalResult;
    outcome.originalResultAvailable = nested.originalResultAvailable;
    return outcome;
}

void StartProduction(
    std::uintptr_t imageBase,
    bool exactF7ProfileReady,
    bool validateOnly) noexcept {
    InterlockedExchange(&g_productionAccepting, 0);
    AcquireSRWLockExclusive(&g_requestLock);
    const bool baseFits = imageBase != 0u &&
        imageBase <= std::numeric_limits<std::uint32_t>::max();
    g_productionImageBase = baseFits ? imageBase : 0u;
    g_activePositions = {};
    g_requestState.Start(exactF7ProfileReady && baseFits && ArenaBattleProgram::Ready(), validateOnly);
    const bool ready = g_requestState.IsReady();
    ReleaseSRWLockExclusive(&g_requestLock);
    InterlockedExchange(&g_productionAccepting, ready ? 1 : 0);
}

void ProductionClearPositionBattle() noexcept {
    AcquireSRWLockExclusive(&g_requestLock);
    g_activePositions = {};
    ReleaseSRWLockExclusive(&g_requestLock);
}

void ProductionLeaveBattle() noexcept {
    AcquireSRWLockExclusive(&g_requestLock);
    g_activePositions = {};
    RestoreOwnedBattlefield(false);
    ReleaseSRWLockExclusive(&g_requestLock);
}

static bool PositionReadFactsMatch(std::uintptr_t image, std::uint32_t carrier,
                                  std::uintptr_t actor, std::uint16_t expectedId) noexcept {
    __try {
        if (*reinterpret_cast<volatile const std::uint32_t*>(image + 0x00D2A9B0u) !=
            carrier + kCarrierChunk3Offset) return false;
        if (actor && (*reinterpret_cast<volatile const std::uint8_t*>(actor + 0x6D4u) != 0xFFu ||
                      *reinterpret_cast<volatile const std::uint16_t*>(actor + 0x0Eu) != expectedId))
            return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void ProductionPositionSet(std::uintptr_t actor, int slot) noexcept {
    if (slot < 0 || slot >= 8) return;
    AcquireSRWLockExclusive(&g_requestLock);
    if (g_activePositions.layout.enabled && slot < g_activePositions.layout.count &&
        g_activePositions.threadId == GetCurrentThreadId() &&
        PositionReadFactsMatch(g_productionImageBase, g_activePositions.carrierAddress, actor,
                               g_activePositions.selection.monsterIds[slot]))
        g_activePositions.nativeOwnedSlots |= static_cast<std::uint8_t>(1u << slot);
    ReleaseSRWLockExclusive(&g_requestLock);
}

static bool WritePositionResult(float* output, ArenaPositions::Point point) noexcept {
    __try {
        // The original has already produced this caller-owned vector on this thread.
        output[0] = point.x;
        output[2] = point.z;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ProductionPositionRead(int originalResult, int setMode, std::uintptr_t actor,
                            int area, int role, int slot, float* output) noexcept {
    if (originalResult != 0 || setMode != 0 || area != 0 || role != 5 || !output ||
        slot < 0 || slot >= 8 || InterlockedCompareExchange(&g_productionAccepting,0,0)==0) return false;
    AcquireSRWLockShared(&g_requestLock);
    const ActivePositions active = g_activePositions;
    const std::uintptr_t image = g_productionImageBase;
    ReleaseSRWLockShared(&g_requestLock);
    if (!active.layout.enabled || slot >= active.layout.count || active.threadId != GetCurrentThreadId() ||
        (active.nativeOwnedSlots & (1u << slot)) != 0u) return false;
    const std::uint64_t outputBegin = reinterpret_cast<std::uintptr_t>(output);
    const std::uint64_t carrierEnd = static_cast<std::uint64_t>(active.carrierAddress) + kCarrierSize;
    if (outputBegin + 16u > 0x100000000ull ||
        (outputBegin < carrierEnd && outputBegin + 16u > active.carrierAddress)) return false;
    const FixedProductionMemoryProbe probe(static_cast<std::uint32_t>(image));
    const auto globals = ReadCarrierGlobals(image, &probe);
    if (globals.code != AdapterCode::Ready || globals.globals.carrierAddress != active.carrierAddress ||
        !PositionReadFactsMatch(image, active.carrierAddress, actor, active.selection.monsterIds[slot]))
        return false;
    return WritePositionResult(output, active.layout.points[slot]);
}

bool ProductionOperational() noexcept {
    if (InterlockedCompareExchange(&g_productionAccepting, 0, 0) == 0) return false;
    AcquireSRWLockShared(&g_requestLock);
    const bool ready = g_requestState.IsReady();
    ReleaseSRWLockShared(&g_requestLock);
    return ready;
}

StatusSnapshot ProductionStatus() noexcept {
    AcquireSRWLockShared(&g_requestLock);
    const StatusSnapshot status = g_requestState.Status();
    ReleaseSRWLockShared(&g_requestLock);
    return status;
}

void ProductionPublishSelection(const SelectionInput& selection) noexcept {
    AcquireSRWLockExclusive(&g_requestLock);
    g_requestState.PublishSelection(selection);
    ReleaseSRWLockExclusive(&g_requestLock);
}

bool ProductionArmSelection(
    const SelectionInput& selection, std::uint64_t nowTick) noexcept {
    if(!ProductionPrepareSelection(selection))return false;
    if (InterlockedCompareExchange(&g_productionAccepting, 0, 0) == 0) return false;
    AcquireSRWLockExclusive(&g_requestLock);
    const bool accepting =
        InterlockedCompareExchange(&g_productionAccepting, 0, 0) != 0;
    const bool armed = accepting && g_requestState.Arm(selection, nowTick);
    ReleaseSRWLockExclusive(&g_requestLock);
    return armed;
}

bool ProductionPrepareSelection(const SelectionInput& selection) noexcept {
    if(InterlockedCompareExchange(&g_productionAccepting,0,0)==0)return false;
    AcquireSRWLockExclusive(&g_requestLock);
    bool ready=InterlockedCompareExchange(&g_productionAccepting,0,0)!=0;
    if(ready&&!ArenaBattleProgram::Matches(g_preparedFrame,selection)){
        ArenaBattleProgram::ReleaseUnpublished(g_preparedFrame);
        g_preparedFrame=ArenaBattleProgram::Reserve(selection);
    }
    ready=ready&&g_preparedFrame!=nullptr;
    ReleaseSRWLockExclusive(&g_requestLock);
    return ready;
}

void ProductionCancel(CancelReason reason) noexcept {
    AcquireSRWLockExclusive(&g_requestLock);
    g_requestState.Cancel(reason);
    ArenaBattleProgram::ReleaseUnpublished(g_preparedFrame);g_preparedFrame=nullptr;
    ReleaseSRWLockExclusive(&g_requestLock);
}

void ProductionTick(std::uint64_t nowTick) noexcept {
    AcquireSRWLockExclusive(&g_requestLock);
    g_requestState.Tick(nowTick);
    if(g_requestState.Status().code==StatusCode::Expired){
        ArenaBattleProgram::ReleaseUnpublished(g_preparedFrame);g_preparedFrame=nullptr;
    }
    ReleaseSRWLockExclusive(&g_requestLock);
}

void ProductionRequestStop() noexcept {
    // Loader-lock callers only close admission. The existing F7 callback fence
    // drains every possible transaction before normal-context state reset.
    InterlockedExchange(&g_productionAccepting, 0);
}

void ProductionResetAfterDrain() noexcept {
    AcquireSRWLockExclusive(&g_requestLock);
    RestoreOwnedBattlefield(true);
    ArenaBattleProgram::ReleaseUnpublished(g_preparedFrame);g_preparedFrame=nullptr;
    g_activePositions = {};
    g_requestState.ResetAfterDrain();
    ReleaseSRWLockExclusive(&g_requestLock);
}

BattleCompositionOutcome RunProductionBattle(
    const NestedInitSceneIo& nested, std::uint64_t nowTick) noexcept {
    ProductionClearPositionBattle(); // Every new battle invalidates the preceding formation.
    AcquireSRWLockExclusive(&g_requestLock);
    // The native queue has already selected the new encounter. Even if its scenery
    // equals ours, that new native selection must never be restored to the old ID.
    g_activeBattlefield = {};
    ReleaseSRWLockExclusive(&g_requestLock);
    PendingClaim claim{};
    ArenaBattleProgram::Frame* frame=nullptr;
    if (InterlockedCompareExchange(&g_productionAccepting, 0, 0) != 0) {
        AcquireSRWLockExclusive(&g_requestLock);
        if (InterlockedCompareExchange(&g_productionAccepting, 0, 0) != 0) {
            claim = g_requestState.Claim(nowTick);
            if(claim.claimed){frame=g_preparedFrame;g_preparedFrame=nullptr;}
        }
        ReleaseSRWLockExclusive(&g_requestLock);
    }

    FixedCarrierContext carrier{};
    if (claim.claimed) {
        carrier.outcome = ProbeCarrierInternal(g_productionImageBase, nullptr);
        if (carrier.outcome.code == AdapterCode::Ready &&
            claim.request.selection.scenery != ArenaScenery::Choice::Carrier)
            carrier.outcome.carrier.battlefieldId = ProbeBattlefield(g_productionImageBase,
                static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(carrier.outcome.carrier.bytes)));
    }
    NormalViewContext normal{nested, claim.claimed?&claim.request:nullptr, frame, g_productionImageBase,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(carrier.outcome.carrier.bytes)),false};
    const NestedInitSceneIo selectedOriginal=claim.claimed?NestedInitSceneIo{&normal,&InvokeNormalView}:nested;
    const BattleCompositionIo io{
        {&carrier, &MaterializeFixedCarrier},
        selectedOriginal,
    };
    BattleCompositionOutcome outcome = RunBattleComposition(
        claim.claimed ? &claim.request : nullptr,
        claim.claimed ? claim.observation : Observation{0u, nowTick},
        io);
    outcome.normalProgramUsed=normal.used;
    outcome.programSource=normal.used&&frame?frame->geometry.sourceName:nullptr;
    if(frame&&!frame->published)ArenaBattleProgram::ReleaseUnpublished(frame);

    if (claim.claimed) {
        AcquireSRWLockExclusive(&g_requestLock);
        g_requestState.PublishTransaction(claim.request.generation, outcome.transaction);
        if (outcome.transaction.battlefield.active) {
            g_activeBattlefield.lease = outcome.transaction.battlefield;
            g_activeBattlefield.carrierAddress = static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(carrier.outcome.carrier.bytes));
            g_activeBattlefield.threadId = GetCurrentThreadId();
        }
        if (!normal.used && outcome.transaction.result == TransactionResult::Restored &&
            claim.request.selection.positions.enabled &&
            InterlockedCompareExchange(&g_productionAccepting,0,0)!=0) {
            g_activePositions.layout = claim.request.selection.positions;
            g_activePositions.selection = BuildSelection(claim.request.selection).expanded;
            g_activePositions.carrierAddress = static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(carrier.outcome.carrier.bytes));
            g_activePositions.threadId = GetCurrentThreadId();
        }
        if (g_requestState.RestoreConflictLatched()) {
            InterlockedExchange(&g_productionAccepting, 0);
        }
        ReleaseSRWLockExclusive(&g_requestLock);
    }
    return outcome;
}

#if defined(FFXHOOKS_TESTING)
namespace Testing {

ProbeEvidence ProbeCarrier(std::uint64_t imageBase) noexcept {
    ProbeEvidence evidence{};
    evidence.outcome = ProbeCarrierInternal(
        imageBase, &evidence.virtualQueryCalls);
    return evidence;
}

}  // namespace Testing
#endif

}  // namespace FfxHooks::CustomMixUltra::Runtime
