#include "../hooks/CustomMixUltraCore.h"

#include <array>
#include <cstring>
#include <limits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {

using namespace FfxHooks::CustomMixUltra;

int g_passed = 0;
int g_failed = 0;

// Hand-derived fixture values keep the test independent from the production constants.
// A wrong production offset must move the mutation away from this known carrier window.
constexpr std::size_t kFixtureSize = 0x4428u;
constexpr std::size_t kFixtureChunk2Offset = 0x3F6Cu;
constexpr std::size_t kFixtureChunk3Offset = 0x3F88u;
constexpr std::size_t kFixtureSlotOffset = 0x3F78u;
constexpr std::size_t kFixtureSlotBytes = 16u;
constexpr std::size_t kFixtureMonsterSlots = 8u;
constexpr std::size_t kNoMutation = kFixtureSlotBytes;

static_assert(std::is_same_v<
                  typename decltype(PendingRequest{}.selection.activations)::value_type,
                  MonsterChoice>,
              "the transaction request must expose symbolic choices, never raw monster IDs");

void Expect(bool condition, const char* message) {
    if (condition) {
        ++g_passed;
        return;
    }
    ++g_failed;
    std::printf("FAIL: %s\n", message);
}

void PutU16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

std::uint16_t ReadU16(const std::uint8_t* bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(bytes[offset + 1u]) << 8u);
}

void PutU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    bytes[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

struct CarrierFixture {
    std::vector<std::uint8_t> bytes;

    CarrierFixture() : bytes(kFixtureSize, 0xCCu) {
        const std::array<std::uint32_t, 9> header = {{
            8u, 0x30u, 0x3790u, 0x3F6Cu, 0x3F88u,
            0x42E8u, 0u, 0x4388u, 0x4428u,
        }};
        for (std::size_t index = 0u; index < header.size(); ++index) {
            PutU32(bytes, index * sizeof(std::uint32_t), header[index]);
        }

        const std::array<std::uint8_t, 12> chunk2Prefix = {{
            0x00u, 0x00u, 0x07u, 0x00u, 0x00u, 0x00u,
            0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        }};
        for (std::size_t index = 0u; index < chunk2Prefix.size(); ++index) {
            bytes[kFixtureChunk2Offset + index] = chunk2Prefix[index];
        }
        PutU16(bytes, kFixtureSlotOffset, 0x1081u);
        for (std::size_t slot = 1u; slot < kFixtureMonsterSlots; ++slot) {
            PutU16(bytes, kFixtureSlotOffset + slot * sizeof(std::uint16_t), 0x1092u);
        }

        const std::array<std::uint8_t, 16> chunk3Prefix = {{
            0x00u, 0x01u, 0x01u, 0x04u, 0x07u, 0x07u, 0x08u, 0x00u,
            0x01u, 0x04u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        }};
        for (std::size_t index = 0u; index < chunk3Prefix.size(); ++index) {
            bytes[kFixtureChunk3Offset + index] = chunk3Prefix[index];
        }
        const std::array<std::uint32_t, 8> chunk3Offsets = {{
            0x70u, 0xE0u, 0x150u, 0x1C0u,
            0x230u, 0x2B0u, 0x60u, 0x350u,
        }};
        for (std::size_t index = 0u; index < chunk3Offsets.size(); ++index) {
            PutU32(bytes, kFixtureChunk3Offset + 0x10u + index * sizeof(std::uint32_t),
                   chunk3Offsets[index]);
        }
    }
};

CarrierView ValidCarrier(CarrierFixture& fixture) {
    CarrierView carrier{};
    carrier.bytes = fixture.bytes.data();
    carrier.size = fixture.bytes.size();
    carrier.encounterToken = 0x02050000u;
    carrier.encounterName = "dome02_00";
    carrier.encounterNameLength = 9u;
    carrier.access = CarrierAccess::ReadWrite;
    return carrier;
}

PendingRequest ValidRequest(std::uint64_t generation = 41u) {
    PendingRequest request{};
    request.enabled = true;
    request.armed = true;
    request.generation = generation;
    request.deadlineTick = 1000u;
    request.selection.activationCount = 1u;
    request.selection.activations[0] = MonsterChoice::Valefor;
    return request;
}

Observation ValidObservation(std::uint64_t generation = 41u) {
    Observation observation{};
    observation.generation = generation;
    observation.nowTick = 500u;
    return observation;
}

struct CallbackContext {
    std::uint8_t* bytes = nullptr;
    const std::vector<std::uint8_t>* original = nullptr;
    std::array<std::uint8_t, kFixtureSlotBytes> expectedSlots{};
    int calls = 0;
    bool sawExpectedSlots = false;
    bool sawUntouchedOutsideSlots = false;
    bool returnValue = true;
    bool throwException = false;
    std::size_t mutationIndex = kNoMutation;
    std::array<std::uint16_t, kFixtureMonsterSlots> observedSlots{};
};

bool InspectPatchedCarrier(void* rawContext) {
    CallbackContext& context = *static_cast<CallbackContext*>(rawContext);
    ++context.calls;
    context.sawExpectedSlots = true;
    for (std::size_t index = 0u; index < kFixtureSlotBytes; ++index) {
        context.sawExpectedSlots =
            context.sawExpectedSlots &&
            context.bytes[kFixtureSlotOffset + index] == context.expectedSlots[index];
    }
    for (std::size_t slot = 0u; slot < kFixtureMonsterSlots; ++slot) {
        context.observedSlots[slot] = ReadU16(
            context.bytes, kFixtureSlotOffset + slot * sizeof(std::uint16_t));
    }

    context.sawUntouchedOutsideSlots = true;
    for (std::size_t index = 0u; index < context.original->size(); ++index) {
        if (index >= kFixtureSlotOffset &&
            index < kFixtureSlotOffset + kFixtureSlotBytes) {
            continue;
        }
        context.sawUntouchedOutsideSlots =
            context.sawUntouchedOutsideSlots &&
            context.bytes[index] == (*context.original)[index];
    }

    if (context.mutationIndex < kFixtureSlotBytes) {
        context.bytes[kFixtureSlotOffset + context.mutationIndex] ^= 0x5Au;
    }
    if (context.throwException) {
        throw std::runtime_error("isolated callback failure");
    }
    return context.returnValue;
}

std::array<std::uint8_t, kFixtureSlotBytes> OneMonsterPatch() {
    return {{
        0x4Eu, 0x11u, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
        0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
    }};
}

void TestClosedSelectionModelExpandsOnlySupportedAeons() {
    SelectionInput input{};
    input.activationCount = 8u;
    input.activations = {{
        MonsterChoice::Valefor,
        MonsterChoice::Ifrit,
        MonsterChoice::Ixion,
        MonsterChoice::Shiva,
        MonsterChoice::Bahamut,
        MonsterChoice::Yojimbo,
        MonsterChoice::Anima,
        MonsterChoice::Valefor,
    }};

    const SelectionOutcome outcome = BuildSelection(input);
    const std::array<std::uint16_t, kFixtureMonsterSlots> expected = {{
        0x114Eu, 0x114Eu, 0x114Fu, 0x1150u,
        0x1151u, 0x1152u, 0x1154u, 0x1153u,
    }};
    Expect(outcome.result == SelectionResult::Ready &&
               outcome.expanded.monsterCount == 8u &&
               outcome.expanded.monsterIds == expected,
           "the closed model must group repeats and preserve first-activation order");

    SelectionInput magus{};
    magus.activationCount = 2u;
    magus.activations[0] = MonsterChoice::Magus;
    magus.activations[1] = MonsterChoice::Valefor;
    const SelectionOutcome magusOutcome = BuildSelection(magus);
    Expect(magusOutcome.result == SelectionResult::Ready &&
               magusOutcome.expanded.monsterCount == 4u &&
               magusOutcome.expanded.monsterIds[0] == 0x1155u &&
               magusOutcome.expanded.monsterIds[1] == 0x1156u &&
               magusOutcome.expanded.monsterIds[2] == 0x1157u &&
               magusOutcome.expanded.monsterIds[3] == 0x114Eu,
           "Magus must expand to the exact three sisters before the next activation group");

    SelectionInput repeatedMagus{};
    repeatedMagus.activationCount = 3u;
    repeatedMagus.activations[0] = MonsterChoice::Magus;
    repeatedMagus.activations[1] = MonsterChoice::Valefor;
    repeatedMagus.activations[2] = MonsterChoice::Magus;
    const SelectionOutcome repeatedMagusOutcome = BuildSelection(repeatedMagus);
    const std::array<std::uint16_t, kFixtureMonsterSlots> expectedRepeatedMagus = {{
        0x1155u, 0x1156u, 0x1157u, 0x1155u,
        0x1156u, 0x1157u, 0x114Eu, 0xFFFFu,
    }};
    Expect(repeatedMagusOutcome.result == SelectionResult::Ready &&
               repeatedMagusOutcome.expanded.monsterCount == 7u &&
               repeatedMagusOutcome.expanded.monsterIds == expectedRepeatedMagus,
           "non-adjacent Magus activations must become contiguous expansion groups");
}

void TestClosedSelectionModelRejectsEmptyInvalidAndOverflowInputs() {
    SelectionInput empty{};
    const SelectionOutcome emptyOutcome = BuildSelection(empty);
    Expect(emptyOutcome.result == SelectionResult::Empty &&
               emptyOutcome.expanded.monsterCount == 0u,
           "an empty symbolic selection must fail closed");

    SelectionInput invalid{};
    invalid.activationCount = 1u;
    invalid.activations[0] = static_cast<MonsterChoice>(8u);
    const SelectionOutcome invalidOutcome = BuildSelection(invalid);
    Expect(invalidOutcome.result == SelectionResult::InvalidChoice &&
               invalidOutcome.expanded.monsterCount == 0u,
           "an out-of-domain symbolic value must not alias Penance or an arbitrary monster ID");

    SelectionInput tooManyActivations{};
    tooManyActivations.activationCount = 9u;
    const SelectionOutcome tooManyOutcome = BuildSelection(tooManyActivations);
    Expect(tooManyOutcome.result == SelectionResult::TooManyActivations &&
               tooManyOutcome.expanded.monsterCount == 0u,
           "more than eight activations must fail before reading the fixed input array");

    SelectionInput expandedOverflow{};
    expandedOverflow.activationCount = 3u;
    expandedOverflow.activations[0] = MonsterChoice::Magus;
    expandedOverflow.activations[1] = MonsterChoice::Magus;
    expandedOverflow.activations[2] = MonsterChoice::Magus;
    const SelectionOutcome overflowOutcome = BuildSelection(expandedOverflow);
    Expect(overflowOutcome.result == SelectionResult::ExpandedCapacityExceeded &&
               overflowOutcome.expanded.monsterCount == 0u,
           "a symbolic selection expanding past eight carrier slots must fail atomically");
}

// Each row is a hand-derived callback view for expanded count N=row+1. These literals
// make a wrong endian conversion or a missing 0xFFFF fill independently observable.
constexpr std::array<std::array<std::uint8_t, kFixtureSlotBytes>, 8>
    kExpectedSlotsByMonsterCount = {{
        {{0x4Eu, 0x11u, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
          0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu}},
        {{0x4Eu, 0x11u, 0x4Fu, 0x11u, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
          0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu}},
        {{0x55u, 0x11u, 0x56u, 0x11u, 0x57u, 0x11u, 0xFFu, 0xFFu,
          0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu}},
        {{0x55u, 0x11u, 0x56u, 0x11u, 0x57u, 0x11u, 0x4Eu, 0x11u,
          0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu}},
        {{0x55u, 0x11u, 0x56u, 0x11u, 0x57u, 0x11u, 0x4Eu, 0x11u,
          0x4Fu, 0x11u, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu}},
        {{0x55u, 0x11u, 0x56u, 0x11u, 0x57u, 0x11u, 0x55u, 0x11u,
          0x56u, 0x11u, 0x57u, 0x11u, 0xFFu, 0xFFu, 0xFFu, 0xFFu}},
        {{0x55u, 0x11u, 0x56u, 0x11u, 0x57u, 0x11u, 0x55u, 0x11u,
          0x56u, 0x11u, 0x57u, 0x11u, 0x4Eu, 0x11u, 0xFFu, 0xFFu}},
        {{0x55u, 0x11u, 0x56u, 0x11u, 0x57u, 0x11u, 0x55u, 0x11u,
          0x56u, 0x11u, 0x57u, 0x11u, 0x4Eu, 0x11u, 0x4Fu, 0x11u}},
    }};

SelectionInput SelectionForExpandedCount(std::size_t count) {
    SelectionInput selection{};
    switch (count) {
    case 1u:
        selection.activationCount = 1u;
        selection.activations[0] = MonsterChoice::Valefor;
        break;
    case 2u:
        selection.activationCount = 2u;
        selection.activations[0] = MonsterChoice::Valefor;
        selection.activations[1] = MonsterChoice::Ifrit;
        break;
    case 3u:
        selection.activationCount = 1u;
        selection.activations[0] = MonsterChoice::Magus;
        break;
    case 4u:
    case 5u:
        selection.activationCount = static_cast<std::uint8_t>(count - 2u);
        selection.activations[0] = MonsterChoice::Magus;
        selection.activations[1] = MonsterChoice::Valefor;
        selection.activations[2] = MonsterChoice::Ifrit;
        break;
    case 6u:
    case 7u:
    case 8u:
        selection.activationCount = static_cast<std::uint8_t>(count - 4u);
        selection.activations[0] = MonsterChoice::Magus;
        selection.activations[1] = MonsterChoice::Magus;
        selection.activations[2] = MonsterChoice::Valefor;
        selection.activations[3] = MonsterChoice::Ifrit;
        break;
    default:
        break;
    }
    return selection;
}

void TestEveryExpandedCountWritesIdsThenEmptySentinels() {
    for (std::size_t count = 1u; count <= kFixtureMonsterSlots; ++count) {
        CarrierFixture fixture;
        const std::vector<std::uint8_t> original = fixture.bytes;
        CarrierView carrier = ValidCarrier(fixture);
        PendingRequest request = ValidRequest(100u + count);
        request.selection = SelectionForExpandedCount(count);

        CallbackContext callback{};
        callback.bytes = fixture.bytes.data();
        callback.original = &original;
        callback.expectedSlots = kExpectedSlotsByMonsterCount[count - 1u];

        const TransactionOutcome outcome = ExecuteTransaction(
            &request, ValidObservation(100u + count), carrier,
            InspectPatchedCarrier, &callback);

        bool exactU16Slots = true;
        for (std::size_t slot = 0u; slot < kFixtureMonsterSlots; ++slot) {
            const std::uint16_t expected = ReadU16(
                kExpectedSlotsByMonsterCount[count - 1u].data(),
                slot * sizeof(std::uint16_t));
            exactU16Slots = exactU16Slots && callback.observedSlots[slot] == expected;
        }
        Expect(outcome.result == TransactionResult::Restored && outcome.restored,
               "each expanded count from one through eight must complete and restore");
        Expect(callback.calls == 1 && callback.sawExpectedSlots &&
                   callback.sawUntouchedOutsideSlots,
               "each original call must see its independent sixteen-byte literal");
        Expect(exactU16Slots,
               "each original call must see exact selected IDs then only 0xFFFF slots");
        Expect(fixture.bytes == original,
               "each expanded selection must restore the full carrier byte-for-byte");
    }
}

void TestDefaultRequestPassesThroughWithoutPatching() {
    CarrierFixture fixture;
    const std::vector<std::uint8_t> original = fixture.bytes;
    CarrierView carrier = ValidCarrier(fixture);
    std::uint8_t* const originalPointer = carrier.bytes;
    PendingRequest request{};
    CallbackContext callback{};
    callback.bytes = fixture.bytes.data();
    callback.original = &original;
    callback.expectedSlots = OneMonsterPatch();

    const TransactionOutcome outcome = ExecuteTransaction(
        &request, ValidObservation(), carrier, InspectPatchedCarrier, &callback);

    Expect(outcome.result == TransactionResult::NotArmed && !outcome.patchApplied &&
               outcome.originalInvoked && outcome.originalAccepted &&
               !outcome.originalThrew && !outcome.restored,
           "a default request must report not armed after exactly one passthrough");
    Expect(callback.calls == 1 && !callback.sawExpectedSlots &&
               callback.sawUntouchedOutsideSlots && fixture.bytes == original,
           "a default request must invoke vanilla once without exposing a carrier patch");
    Expect(carrier.bytes == originalPointer,
           "the transaction must never replace the caller's carrier pointer");
}

void TestOneAndEightMonsterTransactionsAreBoundedAndOneShot() {
    CarrierFixture oneFixture;
    const std::vector<std::uint8_t> oneOriginal = oneFixture.bytes;
    CarrierView oneCarrier = ValidCarrier(oneFixture);
    PendingRequest oneRequest = ValidRequest();
    CallbackContext oneCallback{};
    oneCallback.bytes = oneFixture.bytes.data();
    oneCallback.original = &oneOriginal;
    oneCallback.expectedSlots = OneMonsterPatch();

    const TransactionOutcome one = ExecuteTransaction(
        &oneRequest, ValidObservation(), oneCarrier, InspectPatchedCarrier, &oneCallback);
    Expect(one.result == TransactionResult::Restored && one.patchApplied &&
               one.originalInvoked && one.originalAccepted && !one.originalThrew &&
               one.restored,
           "one monster must execute inside one restored RAM transaction");
    Expect(oneCallback.calls == 1 && oneCallback.sawExpectedSlots &&
               oneCallback.sawUntouchedOutsideSlots,
           "the original call must see one ID followed by seven empty sentinels only");
    Expect(oneFixture.bytes == oneOriginal,
           "the one-monster transaction must restore the complete carrier byte-for-byte");

    const TransactionOutcome replay = ExecuteTransaction(
        &oneRequest, ValidObservation(), oneCarrier, InspectPatchedCarrier, &oneCallback);
    Expect(replay.result == TransactionResult::NotArmed && !replay.patchApplied &&
               replay.originalInvoked && replay.originalAccepted &&
               !replay.originalThrew && !replay.restored &&
               oneCallback.calls == 2 && !oneCallback.sawExpectedSlots,
           "a consumed request must pass through once without applying a second patch");

    CarrierFixture eightFixture;
    const std::vector<std::uint8_t> eightOriginal = eightFixture.bytes;
    CarrierView eightCarrier = ValidCarrier(eightFixture);
    PendingRequest eightRequest = ValidRequest(42u);
    eightRequest.selection = SelectionForExpandedCount(8u);
    CallbackContext eightCallback{};
    eightCallback.bytes = eightFixture.bytes.data();
    eightCallback.original = &eightOriginal;
    eightCallback.expectedSlots = kExpectedSlotsByMonsterCount[7u];

    const TransactionOutcome eight = ExecuteTransaction(
        &eightRequest, ValidObservation(42u), eightCarrier,
        InspectPatchedCarrier, &eightCallback);
    Expect(eight.result == TransactionResult::Restored && eight.restored &&
               eightCallback.sawExpectedSlots && eightCallback.sawUntouchedOutsideSlots,
           "eight IDs must occupy exactly all sixteen formation bytes in little-endian order");
    Expect(eightFixture.bytes == eightOriginal,
           "the eight-monster transaction must restore the complete carrier byte-for-byte");
}

void TestEveryOriginalExitComparesAndRestores() {
    CarrierFixture rejectedFixture;
    const std::vector<std::uint8_t> rejectedOriginal = rejectedFixture.bytes;
    CarrierView rejectedCarrier = ValidCarrier(rejectedFixture);
    PendingRequest rejectedRequest = ValidRequest();
    CallbackContext rejectedCallback{};
    rejectedCallback.bytes = rejectedFixture.bytes.data();
    rejectedCallback.original = &rejectedOriginal;
    rejectedCallback.expectedSlots = OneMonsterPatch();
    rejectedCallback.returnValue = false;

    const TransactionOutcome rejected = ExecuteTransaction(
        &rejectedRequest, ValidObservation(), rejectedCarrier,
        InspectPatchedCarrier, &rejectedCallback);
    Expect(rejected.result == TransactionResult::OriginalRejected &&
               rejected.originalInvoked && !rejected.originalAccepted &&
               !rejected.originalThrew && rejected.restored &&
               rejectedCallback.calls == 1 && rejectedFixture.bytes == rejectedOriginal,
           "an original rejection must still compare and restore all sixteen bytes");

    CarrierFixture throwingFixture;
    const std::vector<std::uint8_t> throwingOriginal = throwingFixture.bytes;
    CarrierView throwingCarrier = ValidCarrier(throwingFixture);
    PendingRequest throwingRequest = ValidRequest();
    CallbackContext throwingCallback{};
    throwingCallback.bytes = throwingFixture.bytes.data();
    throwingCallback.original = &throwingOriginal;
    throwingCallback.expectedSlots = OneMonsterPatch();
    throwingCallback.throwException = true;

    const TransactionOutcome throwing = ExecuteTransaction(
        &throwingRequest, ValidObservation(), throwingCarrier,
        InspectPatchedCarrier, &throwingCallback);
    Expect(throwing.result == TransactionResult::OriginalThrew &&
               throwing.originalInvoked && throwing.originalThrew &&
               throwingCallback.calls == 1 && throwing.restored &&
               throwingFixture.bytes == throwingOriginal,
           "a C++ original exception must still compare and restore all sixteen bytes");
}

void TestEveryFormationByteDetectsRestoreConflict() {
    for (std::size_t mutation = 0u; mutation < kFixtureSlotBytes; ++mutation) {
        CarrierFixture fixture;
        const std::vector<std::uint8_t> original = fixture.bytes;
        CarrierView carrier = ValidCarrier(fixture);
        PendingRequest request = ValidRequest(200u + mutation);
        CallbackContext callback{};
        callback.bytes = fixture.bytes.data();
        callback.original = &original;
        callback.expectedSlots = OneMonsterPatch();
        callback.mutationIndex = mutation;

        const TransactionOutcome outcome = ExecuteTransaction(
            &request, ValidObservation(200u + mutation), carrier,
            InspectPatchedCarrier, &callback);

        Expect(outcome.result == TransactionResult::RestoreConflict &&
                   outcome.patchApplied && outcome.originalInvoked &&
                   outcome.originalAccepted && callback.calls == 1 && !outcome.restored,
               "each of all sixteen changed candidate bytes must trigger restore conflict");
        Expect(fixture.bytes[kFixtureSlotOffset + mutation] ==
                   static_cast<std::uint8_t>(callback.expectedSlots[mutation] ^ 0x5Au),
               "compare-before-restore must preserve every conflicting third-party byte");
        bool outsideUntouched = true;
        for (std::size_t index = 0u; index < fixture.bytes.size(); ++index) {
            if (index >= kFixtureSlotOffset &&
                index < kFixtureSlotOffset + kFixtureSlotBytes) {
                continue;
            }
            outsideUntouched = outsideUntouched && fixture.bytes[index] == original[index];
        }
        Expect(outsideUntouched,
               "every restore conflict must leave bytes outside the sixteen-byte window untouched");
    }
}

void TestNullPreflightInputsFailClosed() {
    CarrierFixture fixture;
    const std::vector<std::uint8_t> original = fixture.bytes;
    CarrierView carrier = ValidCarrier(fixture);
    CallbackContext callback{};
    callback.bytes = fixture.bytes.data();
    callback.original = &original;
    callback.expectedSlots = OneMonsterPatch();

    const TransactionOutcome nullRequest = ExecuteTransaction(
        nullptr, ValidObservation(), carrier, InspectPatchedCarrier, &callback);
    Expect(nullRequest.result == TransactionResult::NotArmed &&
               !nullRequest.patchApplied && nullRequest.originalInvoked &&
               nullRequest.originalAccepted && !nullRequest.originalThrew &&
               callback.calls == 1 && !callback.sawExpectedSlots &&
               fixture.bytes == original,
           "a null request must pass through exactly once without reading request state");

    PendingRequest request = ValidRequest();
    CarrierView nullCarrier = carrier;
    nullCarrier.bytes = nullptr;
    const TransactionOutcome nullCarrierOutcome = ExecuteTransaction(
        &request, ValidObservation(), nullCarrier, InspectPatchedCarrier, &callback);
    Expect(nullCarrierOutcome.result == TransactionResult::InvalidCarrierPointer &&
               !request.armed && !nullCarrierOutcome.patchApplied &&
               nullCarrierOutcome.originalInvoked &&
               nullCarrierOutcome.originalAccepted &&
               !nullCarrierOutcome.originalThrew && callback.calls == 2 &&
               fixture.bytes == original,
           "a null carrier pointer must consume the request and pass through once");

    request = ValidRequest(42u);
    const TransactionOutcome nullCallback = ExecuteTransaction(
        &request, ValidObservation(42u), carrier, nullptr, &callback);
    Expect(nullCallback.result == TransactionResult::OriginalUnavailable &&
               !request.armed && !nullCallback.patchApplied &&
               !nullCallback.originalInvoked && callback.calls == 2 &&
               fixture.bytes == original,
           "a null original must consume the request and fail before patching");
    Expect(request.consumedGeneration == request.generation,
           "a null original must consume the armed generation fail-closed");

    PendingRequest unarmed{};
    const TransactionOutcome unarmedNullOriginal = ExecuteTransaction(
        &unarmed, ValidObservation(), carrier, nullptr, &callback);
    Expect(unarmedNullOriginal.result == TransactionResult::OriginalUnavailable &&
               !unarmedNullOriginal.patchApplied &&
               !unarmedNullOriginal.originalInvoked && callback.calls == 2 &&
               fixture.bytes == original,
           "a null original must remain the sole uncallable result even when unarmed");

    const TransactionOutcome nullRequestAndOriginal = ExecuteTransaction(
        nullptr, ValidObservation(), carrier, nullptr, &callback);
    Expect(nullRequestAndOriginal.result == TransactionResult::OriginalUnavailable &&
               !nullRequestAndOriginal.patchApplied &&
               !nullRequestAndOriginal.originalInvoked && callback.calls == 2 &&
               fixture.bytes == original,
           "a null original must fail closed even when no request exists");
}

void ExpectPreflightRejects(
    PendingRequest request,
    const Observation& observation,
    CarrierView carrier,
    TransactionResult expected,
    const char* message,
    InvokeOriginal original = InspectPatchedCarrier) {
    // The fixture allocation remains the exact carrier size even when a test corrupts
    // the advertised view size. Snapshot the allocation, not the intentionally bad view.
    const std::vector<std::uint8_t> snapshot(
        carrier.bytes, carrier.bytes + kFixtureSize);
    CallbackContext callbackContext{};
    callbackContext.bytes = carrier.bytes;
    callbackContext.original = &snapshot;
    callbackContext.expectedSlots = OneMonsterPatch();

    const TransactionOutcome outcome = ExecuteTransaction(
        &request, observation, carrier, original, &callbackContext);
    Expect(outcome.result == expected && !outcome.patchApplied &&
               outcome.originalInvoked && outcome.originalAccepted &&
               !outcome.originalThrew && !outcome.restored &&
               callbackContext.calls == 1 &&
               !callbackContext.sawExpectedSlots &&
               callbackContext.sawUntouchedOutsideSlots &&
               std::vector<std::uint8_t>(
                   carrier.bytes, carrier.bytes + kFixtureSize) == snapshot,
           message);
    Expect(!request.armed,
           "every armed preflight attempt must be consumed exactly once");
    Expect(request.consumedGeneration == request.generation,
           "every armed preflight attempt must record its consumed generation");
}

void TestRejectedPassthroughPreservesPrimaryResultAcrossOriginalFailures() {
    CarrierFixture rejectedFixture;
    const std::vector<std::uint8_t> rejectedOriginal = rejectedFixture.bytes;
    CarrierView rejectedCarrier = ValidCarrier(rejectedFixture);
    PendingRequest rejectedRequest = ValidRequest();
    rejectedRequest.enabled = false;
    CallbackContext rejectedContext{};
    rejectedContext.bytes = rejectedFixture.bytes.data();
    rejectedContext.original = &rejectedOriginal;
    rejectedContext.expectedSlots = OneMonsterPatch();
    rejectedContext.returnValue = false;

    const TransactionOutcome rejected = ExecuteTransaction(
        &rejectedRequest, ValidObservation(), rejectedCarrier,
        InspectPatchedCarrier, &rejectedContext);
    Expect(rejected.result == TransactionResult::Disabled &&
               !rejected.patchApplied && rejected.originalInvoked &&
               !rejected.originalAccepted && !rejected.originalThrew &&
               !rejected.restored && rejectedContext.calls == 1 &&
               !rejectedContext.sawExpectedSlots &&
               rejectedFixture.bytes == rejectedOriginal,
           "a rejecting passthrough must preserve the primary preflight reason");

    CarrierFixture throwingFixture;
    const std::vector<std::uint8_t> throwingOriginal = throwingFixture.bytes;
    CarrierView throwingCarrier = ValidCarrier(throwingFixture);
    PendingRequest throwingRequest = ValidRequest(42u);
    throwingRequest.deadlineTick = ValidObservation(42u).nowTick;
    CallbackContext throwingContext{};
    throwingContext.bytes = throwingFixture.bytes.data();
    throwingContext.original = &throwingOriginal;
    throwingContext.expectedSlots = OneMonsterPatch();
    throwingContext.throwException = true;

    const TransactionOutcome throwing = ExecuteTransaction(
        &throwingRequest, ValidObservation(42u), throwingCarrier,
        InspectPatchedCarrier, &throwingContext);
    Expect(throwing.result == TransactionResult::Expired &&
               !throwing.patchApplied && throwing.originalInvoked &&
               !throwing.originalAccepted && throwing.originalThrew &&
               !throwing.restored && throwingContext.calls == 1 &&
               !throwingContext.sawExpectedSlots &&
               throwingFixture.bytes == throwingOriginal,
           "a throwing passthrough must preserve the primary preflight reason");
}

void TestCarrierAccessContractFailsClosed() {
    auto runAccess = [](CarrierAccess access,
                        TransactionResult expected,
                        const char* message) {
        CarrierFixture fixture;
        CarrierView carrier = ValidCarrier(fixture);
        carrier.access = access;
        PendingRequest request = ValidRequest();
        ExpectPreflightRejects(request, ValidObservation(), carrier, expected, message);
    };

    runAccess(CarrierAccess::Invalid,
              TransactionResult::InvalidCarrierAccess,
              "an unclassified carrier page must pass through before core memory access");
    runAccess(CarrierAccess::Guarded,
              TransactionResult::CarrierGuarded,
              "a simulated guard-page carrier must pass through before core memory access");
    runAccess(CarrierAccess::ReadOnly,
              TransactionResult::CarrierReadOnly,
              "a simulated read-only carrier must pass through before carrier reads");
    runAccess(static_cast<CarrierAccess>(0xFFu),
              TransactionResult::InvalidCarrierAccess,
              "an out-of-domain carrier classification must pass through fail-closed");
}

void TestRequestAdmissionFailsClosed() {
    CarrierFixture fixture;
    CarrierView carrier = ValidCarrier(fixture);

    PendingRequest request = ValidRequest();
    request.enabled = false;
    ExpectPreflightRejects(request, ValidObservation(), carrier,
                           TransactionResult::Disabled,
                           "an armed but disabled request must fail closed");

    request = ValidRequest();
    request.cancelled = true;
    ExpectPreflightRejects(request, ValidObservation(), carrier,
                           TransactionResult::Cancelled,
                           "a cancelled request must fail closed before writing");

    request = ValidRequest();
    request.generation = 0u;
    ExpectPreflightRejects(request, ValidObservation(), carrier,
                           TransactionResult::InvalidGeneration,
                           "generation zero must never own a transaction");

    request = ValidRequest();
    ExpectPreflightRejects(request, ValidObservation(42u), carrier,
                           TransactionResult::GenerationMismatch,
                           "a request from another generation must fail closed");

    request = ValidRequest();
    request.consumedGeneration = request.generation;
    ExpectPreflightRejects(request, ValidObservation(), carrier,
                           TransactionResult::GenerationReplay,
                           "rearming an already consumed generation must fail closed");

    request = ValidRequest();
    request.deadlineTick = 0u;
    ExpectPreflightRejects(request, ValidObservation(), carrier,
                           TransactionResult::InvalidDeadline,
                           "an armed request without a deadline must fail closed");

    request = ValidRequest();
    request.deadlineTick = ValidObservation().nowTick;
    ExpectPreflightRejects(request, ValidObservation(), carrier,
                           TransactionResult::Expired,
                           "the exact deadline tick must already be expired");

    request = ValidRequest();
    request.selection.activationCount = 0u;
    ExpectPreflightRejects(request, ValidObservation(), carrier,
                           TransactionResult::EmptySelection,
                           "an empty symbolic selection must be rejected without writing");

    request = ValidRequest();
    request.selection.activationCount = 9u;
    ExpectPreflightRejects(request, ValidObservation(), carrier,
                           TransactionResult::TooManyActivations,
                           "more than eight activations must be rejected without writing");

    request = ValidRequest();
    request.selection.activations[0] = static_cast<MonsterChoice>(8u);
    ExpectPreflightRejects(request, ValidObservation(), carrier,
                           TransactionResult::InvalidSelectionChoice,
                           "an invalid symbolic choice must not alias Penance or an arbitrary ID");

    request = ValidRequest();
    request.selection.activationCount = 3u;
    request.selection.activations[0] = MonsterChoice::Magus;
    request.selection.activations[1] = MonsterChoice::Magus;
    request.selection.activations[2] = MonsterChoice::Magus;
    ExpectPreflightRejects(request, ValidObservation(), carrier,
                           TransactionResult::ExpandedSelectionOverflow,
                           "a selection expanding past eight carrier slots must be rejected");
}

void TestCarrierIdentityMetadataOffsetsAndLengthsFailClosed() {
    auto runMutation = [](
                           void (*mutate)(CarrierFixture&, CarrierView&),
                           TransactionResult expected,
                           const char* message) {
        CarrierFixture fixture;
        CarrierView carrier = ValidCarrier(fixture);
        mutate(fixture, carrier);
        PendingRequest request = ValidRequest();
        ExpectPreflightRejects(request, ValidObservation(), carrier, expected, message);
    };

    runMutation(
        [](CarrierFixture&, CarrierView& carrier) { carrier.encounterToken ^= 1u; },
        TransactionResult::InvalidCarrierIdentity,
        "a token other than the exact dome02_00 carrier must be rejected");
    runMutation(
        [](CarrierFixture&, CarrierView& carrier) { carrier.encounterName = "dome02_01"; },
        TransactionResult::InvalidCarrierIdentity,
        "a name other than the exact dome02_00 carrier must be rejected");
    runMutation(
        [](CarrierFixture&, CarrierView& carrier) { carrier.encounterNameLength = 8u; },
        TransactionResult::InvalidCarrierIdentity,
        "a truncated carrier name must be rejected without unbounded string reads");
    runMutation(
        [](CarrierFixture&, CarrierView& carrier) { --carrier.size; },
        TransactionResult::InvalidCarrierSize,
        "a truncated carrier buffer must be rejected before reading metadata");
    runMutation(
        [](CarrierFixture&, CarrierView& carrier) { ++carrier.size; },
        TransactionResult::InvalidCarrierSize,
        "an oversized buffer must not alias the exact carrier identity");
    runMutation(
        [](CarrierFixture& fixture, CarrierView&) { fixture.bytes[0] = 7u; },
        TransactionResult::InvalidCarrierMetadata,
        "an invalid carrier chunk marker must be rejected");
    runMutation(
        [](CarrierFixture& fixture, CarrierView&) {
            PutU32(fixture.bytes, 0x0Cu, 0x3F70u);
        },
        TransactionResult::InvalidCarrierMetadata,
        "an invalid chunk2 offset must be rejected");
    runMutation(
        [](CarrierFixture& fixture, CarrierView&) {
            PutU32(fixture.bytes, 0x10u, 0x3F8Cu);
        },
        TransactionResult::InvalidCarrierMetadata,
        "an invalid chunk2 length boundary must be rejected");
    runMutation(
        [](CarrierFixture& fixture, CarrierView&) {
            PutU32(fixture.bytes, 0x14u, 0x42E4u);
        },
        TransactionResult::InvalidCarrierMetadata,
        "an invalid chunk3 length boundary must be rejected");
    runMutation(
        [](CarrierFixture& fixture, CarrierView&) {
            fixture.bytes[kFixtureChunk2Offset + 2u] ^= 1u;
        },
        TransactionResult::InvalidCarrierMetadata,
        "a mismatched formation metadata prefix must be rejected");
    runMutation(
        [](CarrierFixture& fixture, CarrierView&) {
            fixture.bytes[kFixtureSlotOffset] ^= 1u;
        },
        TransactionResult::CarrierSlotConflict,
        "a carrier whose formation slots are already changed must fail closed");
    runMutation(
        [](CarrierFixture& fixture, CarrierView&) {
            fixture.bytes[kFixtureChunk3Offset + 6u] = 7u;
        },
        TransactionResult::InvalidCarrierMetadata,
        "a carrier without exactly eight live monster positions must be rejected");
    runMutation(
        [](CarrierFixture& fixture, CarrierView&) {
            PutU32(fixture.bytes, kFixtureChunk3Offset + 0x20u, 0x234u);
        },
        TransactionResult::InvalidCarrierMetadata,
        "a changed monster-position offset must be rejected");
}

// Position fixtures use literal byte locations, independent of production offsets.
static float PositionFloat(const std::vector<std::uint8_t>& b, std::size_t offset) {
    float f = 0; std::memcpy(&f, b.data() + offset, 4); return f;
}
static void PositionPut(std::vector<std::uint8_t>& b, std::size_t offset, float f) {
    std::memcpy(b.data() + offset, &f, 4);
}
static void PositionReferences(CarrierFixture& f) {
    const float party[7][2] = {{0,0},{0,72},{64,36},{64,-36},{0,-72},{-64,-36},{-64,36}};
    for (unsigned i=0;i<7;++i) {
        PositionPut(f.bytes,0x3FF8u+16u*i,party[i][0]);
        PositionPut(f.bytes,0x4000u+16u*i,party[i][1]);
    }
    for(unsigned i=0;i<8;++i) for(unsigned c=0;c<4;++c)
        PositionPut(f.bytes,0x41B8u+16u*i+4u*c,static_cast<float>(i*10u+c));
}
struct PositionCallback {
    CarrierFixture* fixture;
    const std::vector<std::uint8_t>* before;
    FfxHooks::ArenaPositions::Layout layout;
    int corruptByte = -1;
    bool throwAfter = false;
    bool result = true;
    int calls = 0;
};
static bool InspectPositions(void* raw) {
    auto& c=*static_cast<PositionCallback*>(raw); ++c.calls;
    for(unsigned i=0;i<c.layout.count;++i) {
        Expect(PositionFloat(c.fixture->bytes,0x41B8u+16u*i)==c.layout.points[i].x &&
               PositionFloat(c.fixture->bytes,0x41C0u+16u*i)==c.layout.points[i].z,
               "vanilla must see every selected slot's requested X/Z");
    }
    bool untouched=true;
    for(std::size_t i=0;i<c.before->size();++i) {
        const bool formation=i>=0x3F78u && i<0x3F88u;
        const bool position=i>=0x41B8u && i<0x41B8u+16u*c.layout.count &&
            ((i-0x41B8u)%16u<4u || ((i-0x41B8u)%16u>=8u && (i-0x41B8u)%16u<12u));
        if(!formation && !position && c.fixture->bytes[i]!=(*c.before)[i]) untouched=false;
    }
    Expect(untouched,"positions preserve Y/W, party, camera, unused slots and all other chunks");
    if(c.corruptByte>=0) {
        const unsigned byte=static_cast<unsigned>(c.corruptByte);
        c.fixture->bytes[0x41B8u+(byte/8u)*16u+(byte%8u<4u?byte%8u:byte%8u+4u)]^=0x5Au;
    }
    if(c.throwAfter) throw std::runtime_error("position callback exit");
    return c.result;
}
void TestPositionGenerationEditingAndTransactions() {
    namespace P=FfxHooks::ArenaPositions;
    Expect(!P::Generate(0).enabled && !P::Generate(9).enabled,"invalid counts cannot enable positioning");
    for(std::uint8_t n=1;n<=8;++n) {
        const auto layout=P::Generate(n);
        Expect(layout.enabled && layout.count==n && P::Validate(layout,n)==P::Issue::None,
               "auto arrangement produces exactly one distinct legal position per slot, including five/eight");
        CarrierFixture fixture; PositionReferences(fixture); const auto before=fixture.bytes;
        auto request=ValidRequest();request.selection.activationCount=n;
        request.selection.positions=layout;
        PositionCallback c{&fixture,&before,layout};
        const auto out=ExecuteTransaction(&request,ValidObservation(),ValidCarrier(fixture),&InspectPositions,&c);
        Expect(out.restored && out.positionSlotsApplied==n && fixture.bytes==before && c.calls==1,
               "position and ID transaction restores the complete original buffer after one vanilla call");
    }
    auto manual=P::Generate(5);
    const auto original=manual;
    Expect(P::Move(&manual,4,4,0) && !manual.automatic && manual.points[4].x==32.0f &&
               manual.points[0].x==original.points[0].x,"manual edits retain slot identity and leave peers alone");
    Expect(!P::Move(&manual,4,10000,0) && manual.points[4].x==32.0f,
           "out-of-bounds edits preserve the last valid draft");
    manual.points[0]=manual.points[1];
    Expect(P::Validate(manual,5)==P::Issue::Overlap,"coincident slots are rejected");
    manual=P::Generate(1);manual.points[0]={0,80};
    Expect(P::Validate(manual,1)==P::Issue::PartyOverlap,"manual positions cannot coincide with a party spawn");
    manual=P::Generate(1);manual.points[0].x=std::numeric_limits<float>::quiet_NaN();
    Expect(P::Validate(manual,1)==P::Issue::NonFinite,"NaN coordinates cannot reach the writer");
    for(int exit=0;exit<2;++exit) {
        CarrierFixture fixture;PositionReferences(fixture);const auto before=fixture.bytes;
        auto request=ValidRequest();request.selection.positions=P::Generate(1);
        PositionCallback c{&fixture,&before,request.selection.positions};c.throwAfter=exit==1;c.result=false;
        const auto out=ExecuteTransaction(&request,ValidObservation(),ValidCarrier(fixture),&InspectPositions,&c);
        Expect(out.restored && fixture.bytes==before && c.calls==1,"false/throw exits restore both owned windows");
    }
    for(int byte=0;byte<64;++byte) {
        CarrierFixture fixture;PositionReferences(fixture);const auto before=fixture.bytes;
        auto request=ValidRequest();request.selection.activationCount=8;request.selection.positions=P::Generate(8);
        PositionCallback c{&fixture,&before,request.selection.positions};c.corruptByte=byte;
        const auto out=ExecuteTransaction(&request,ValidObservation(),ValidCarrier(fixture),&InspectPositions,&c);
        const unsigned field=0x41B8u+static_cast<unsigned>(byte/8)*16u+(byte%8<4?0u:8u);
        bool restoredOwned=true;
        for(unsigned i=0;i<fixture.bytes.size();++i)
            if((i<field || i>=field+4u) && fixture.bytes[i]!=before[i]) restoredOwned=false;
        Expect(out.result==TransactionResult::RestoreConflict && !out.restored && restoredOwned,
               "every conflicting position byte is preserved while all still-owned fields are restored");
    }
    for(int bad=0;bad<3;++bad) {
        CarrierFixture fixture;PositionReferences(fixture);
        auto request=ValidRequest();request.selection.positions=P::Generate(1);
        if(bad==0) request.selection.positions.count=2;
        if(bad==1) request.selection.positions.points[0].z=std::numeric_limits<float>::infinity();
        if(bad==2) PositionPut(fixture.bytes,0x3FF8u,1.0f);
        const auto before=fixture.bytes;int calls=0;
        const auto out=ExecuteTransaction(&request,ValidObservation(),ValidCarrier(fixture),
            [](void* p){++*static_cast<int*>(p);return true;},&calls);
        Expect(!out.patchApplied && fixture.bytes==before && calls==1,
               "invalid layouts or an unexpected party reference pass through without mutation");
    }
}

}  // namespace

int main() {
    SelectionInput ordinary{};
    ordinary.activationCount=3;
    ordinary.activations[0]=static_cast<MonsterChoice>(0x109u); // Catalog Dingo, not native 0x1009.
    ordinary.activations[1]=MonsterChoice::Valefor;
    ordinary.activations[2]=static_cast<MonsterChoice>(0x109u);
    const auto expandedOrdinary=BuildSelection(ordinary);
    Expect(expandedOrdinary.result==SelectionResult::Ready,
           "Ultra admits an explicit catalog fiend choice");
    Expect(expandedOrdinary.expanded.monsterIds[0]==0x1009u &&
           expandedOrdinary.expanded.monsterIds[1]==0x1009u &&
           expandedOrdinary.expanded.monsterIds[2]==0x114Eu,
           "catalog choices retain stable repeated order and native monster identity");
    TestPositionGenerationEditingAndTransactions();
    TestClosedSelectionModelExpandsOnlySupportedAeons();
    TestClosedSelectionModelRejectsEmptyInvalidAndOverflowInputs();
    TestDefaultRequestPassesThroughWithoutPatching();
    TestOneAndEightMonsterTransactionsAreBoundedAndOneShot();
    TestEveryExpandedCountWritesIdsThenEmptySentinels();
    TestEveryOriginalExitComparesAndRestores();
    TestEveryFormationByteDetectsRestoreConflict();
    TestNullPreflightInputsFailClosed();
    TestRejectedPassthroughPreservesPrimaryResultAcrossOriginalFailures();
    TestCarrierAccessContractFailsClosed();
    TestRequestAdmissionFailsClosed();
    TestCarrierIdentityMetadataOffsetsAndLengthsFailClosed();

    std::printf("CustomMix Ultra core RT0/RT1: %d/%d passed\n",
                g_passed, g_passed + g_failed);
    return g_failed == 0 ? 0 : 1;
}
