#include "../hooks/CustomMixWindowsAdapter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using namespace FfxHooks::CustomMixUltra;
using namespace FfxHooks::CustomMixUltra::WindowsAdapter;

int g_passed = 0;
int g_failed = 0;

static_assert(sizeof(void*) == 4u,
              "the Windows carrier adapter RT1 must compile as x86");
static_assert(kSupportedPreferredImageBase == 0x00400000u,
              "the supported profile preferred base must remain exact");
static_assert(kCarrierSizeRva == 0x00D2A9A6u,
              "the u16 carrier-size global RVA must remain exact");
static_assert(kCarrierPointerRva == 0x00D2A9A8u,
              "the ptr32 carrier global RVA must remain exact");
static_assert(kCarrierNameRva == 0x00D2C25Au,
              "the ten-byte carrier-name global RVA must remain exact");
static_assert(kCarrierSizeReadWidth == 2u &&
                  kCarrierPointerReadWidth == 4u &&
                  kCarrierNameReadWidth == 10u,
              "the three closed probe widths must remain u16, ptr32, and char[10]");
static_assert(kExpectedCarrierSize == 0x4428u,
              "the adapter must bind the exact carrier allocation size");
static_assert(kExpectedEncounterToken == 0x02050000u,
              "the adapter must materialize the exact encounter token");
static_assert(kExpectedPatchBytes == 16u,
              "the portable core must remain the sole sixteen-byte actor patch owner");

void Expect(bool condition, const char* message) {
    if (condition) {
        ++g_passed;
        return;
    }
    ++g_failed;
    std::printf("FAIL: %s\n", message);
}

struct FakeMemoryProbe final : MemoryProbe {
    U16ProbeRead sizeRead{};
    Pointer32ProbeRead pointerRead{};
    Name10ProbeRead nameRead{};
    mutable std::uint32_t sizeAddress = 0u;
    mutable std::uint32_t pointerAddress = 0u;
    mutable std::uint32_t nameAddress = 0u;
    mutable int sizeCalls = 0;
    mutable int pointerCalls = 0;
    mutable int nameCalls = 0;

    FakeMemoryProbe() {
        sizeRead.succeeded = true;
        sizeRead.bytesRead = 2u;
        sizeRead.value = 0x4428u;
        pointerRead.succeeded = true;
        pointerRead.bytesRead = 4u;
        pointerRead.value = 0x10002000u;
        nameRead.succeeded = true;
        nameRead.bytesRead = 10u;
        nameRead.value = {{
            'd', 'o', 'm', 'e', '0', '2', '_', '0', '0', '\0',
        }};
    }

    U16ProbeRead ReadCarrierSizeU16(std::uint32_t address) const noexcept override {
        sizeAddress = address;
        ++sizeCalls;
        return sizeRead;
    }

    Pointer32ProbeRead ReadCarrierPointer32(
        std::uint32_t address) const noexcept override {
        pointerAddress = address;
        ++pointerCalls;
        return pointerRead;
    }

    Name10ProbeRead ReadCarrierName10(std::uint32_t address) const noexcept override {
        nameAddress = address;
        ++nameCalls;
        return nameRead;
    }
};

CarrierGlobals ValidGlobals(std::uint32_t address = 0x10002000u) {
    CarrierGlobals globals{};
    globals.carrierSize = 0x4428u;
    globals.carrierAddress = address;
    globals.carrierName = {{
        'd', 'o', 'm', 'e', '0', '2', '_', '0', '0', '\0',
    }};
    return globals;
}

RegionInfo ValidRegion(std::uint32_t address = 0x10002000u) {
    RegionInfo region{};
    region.baseAddress = address;
    region.regionSize = 0x4428u;
    region.state = RegionState::Commit;
    region.protection = kPageReadWrite;
    return region;
}

void TestExactTypedReadsAndCarrierMaterialization() {
    std::array<std::uint8_t, 0x4428u> storage{};
    storage.fill(0x5Au);
    const std::array<std::uint8_t, 0x4428u> before = storage;
    const std::uint32_t storageAddress = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(storage.data()));

    FakeMemoryProbe probe;
    probe.pointerRead.value = storageAddress;
    const GlobalReadOutcome read = ReadCarrierGlobals(0x00400000u, &probe);

    Expect(read.code == AdapterCode::Ready &&
               read.globals.carrierSize == 0x4428u &&
               read.globals.carrierAddress == storageAddress &&
               read.globals.carrierName[9] == '\0',
           "the typed probe must return the exact validated carrier globals");
    Expect(probe.sizeCalls == 1 && probe.pointerCalls == 1 && probe.nameCalls == 1 &&
               probe.sizeAddress == 0x0112A9A6u &&
               probe.pointerAddress == 0x0112A9A8u &&
               probe.nameAddress == 0x0112C25Au,
           "the preferred base must resolve the exact three global VAs once each");

    RegionInfo region = ValidRegion(storageAddress);
    const CarrierViewOutcome materialized =
        MaterializeCarrierView(read.globals, region);
    Expect(materialized.code == AdapterCode::Ready &&
               materialized.carrier.bytes == storage.data() &&
               materialized.carrier.size == 0x4428u &&
               materialized.carrier.encounterToken == 0x02050000u &&
               materialized.carrier.encounterNameLength == 9u &&
               materialized.carrier.encounterName != nullptr &&
               std::memcmp(materialized.carrier.encounterName,
                           "dome02_00", 10u) == 0 &&
               materialized.carrier.access == CarrierAccess::ReadWrite,
           "a committed writable exact carrier must materialize the portable core view");
    Expect(storage == before,
           "materializing a carrier view must not read or write the carrier allocation");
}

void TestModuleAddressArithmeticFailsClosed() {
    FakeMemoryProbe probe;
    const GlobalReadOutcome nullBase = ReadCarrierGlobals(0u, &probe);
    const GlobalReadOutcome above32 = ReadCarrierGlobals(0x100000000ull, &probe);
    Expect(nullBase.code == AdapterCode::InvalidImageBase &&
               above32.code == AdapterCode::InvalidImageBase &&
               probe.sizeCalls == 0 && probe.pointerCalls == 0 && probe.nameCalls == 0,
           "null and above-32-bit image bases must fail before any probe read");

    Expect(Testing::CarrierSizeReadSpanFits32(0xFF2D5658ull) &&
               !Testing::CarrierSizeReadSpanFits32(0xFF2D5659ull),
           "the u16 read span must accept only through its independent PE32 boundary");
    Expect(Testing::CarrierPointerReadSpanFits32(0xFF2D5654ull) &&
               !Testing::CarrierPointerReadSpanFits32(0xFF2D5655ull),
           "the ptr32 read span must accept only through its independent PE32 boundary");
    Expect(Testing::CarrierNameReadSpanFits32(0xFF2D3D9Cull) &&
               !Testing::CarrierNameReadSpanFits32(0xFF2D3D9Dull),
           "the name10 read span must accept only through its independent PE32 boundary");

    const GlobalReadOutcome highest =
        ReadCarrierGlobals(0xFF2D3D9Cull, &probe);
    Expect(highest.code == AdapterCode::Ready &&
               probe.sizeAddress == 0xFFFFE742u &&
               probe.pointerAddress == 0xFFFFE744u &&
               probe.nameAddress == 0xFFFFFFF6u,
           "the last image base whose full name10 span ends at UINT32_MAX must be valid");

    FakeMemoryProbe overflowProbe;
    const GlobalReadOutcome overflow =
        ReadCarrierGlobals(0xFF2D3D9Dull, &overflowProbe);
    Expect(overflow.code == AdapterCode::GlobalAddressOverflow &&
               overflowProbe.sizeCalls == 0 && overflowProbe.pointerCalls == 0 &&
               overflowProbe.nameCalls == 0,
           "an image base whose name10 span crosses UINT32_MAX must fail before reads");

    const std::array<std::uint64_t, 3u> firstInvalidBases = {{
        0xFF2D5659ull,
        0xFF2D5655ull,
        0xFF2D3D9Dull,
    }};
    for (const std::uint64_t imageBase : firstInvalidBases) {
        FakeMemoryProbe noPartialProbe;
        const GlobalReadOutcome rejected =
            ReadCarrierGlobals(imageBase, &noPartialProbe);
        Expect(rejected.code == AdapterCode::GlobalAddressOverflow &&
                   noPartialProbe.sizeCalls == 0 &&
                   noPartialProbe.pointerCalls == 0 &&
                   noPartialProbe.nameCalls == 0,
               "every typed read-span overflow must reject before the first probe");
    }
}

void TestNullFailedAndShortReadsFailClosed() {
    const GlobalReadOutcome nullProbe =
        ReadCarrierGlobals(0x00400000u, nullptr);
    Expect(nullProbe.code == AdapterCode::NullProbe,
           "a null typed memory probe must fail closed");

    FakeMemoryProbe failedSize;
    failedSize.sizeRead.succeeded = false;
    Expect(ReadCarrierGlobals(0x00400000u, &failedSize).code ==
               AdapterCode::SizeReadFailed &&
               failedSize.sizeCalls == 1 && failedSize.pointerCalls == 0 &&
               failedSize.nameCalls == 0,
           "a failed u16 size read must stop before later global reads");

    FakeMemoryProbe shortSize;
    shortSize.sizeRead.bytesRead = 1u;
    Expect(ReadCarrierGlobals(0x00400000u, &shortSize).code ==
               AdapterCode::SizeReadShort,
           "a one-byte carrier size read must not satisfy the u16 contract");

    FakeMemoryProbe failedPointer;
    failedPointer.pointerRead.succeeded = false;
    Expect(ReadCarrierGlobals(0x00400000u, &failedPointer).code ==
               AdapterCode::PointerReadFailed &&
               failedPointer.sizeCalls == 1 && failedPointer.pointerCalls == 1 &&
               failedPointer.nameCalls == 0,
           "a failed ptr32 read must stop before the name read");

    FakeMemoryProbe shortPointer;
    shortPointer.pointerRead.bytesRead = 3u;
    Expect(ReadCarrierGlobals(0x00400000u, &shortPointer).code ==
               AdapterCode::PointerReadShort,
           "a three-byte carrier pointer read must not satisfy the ptr32 contract");

    FakeMemoryProbe failedName;
    failedName.nameRead.succeeded = false;
    Expect(ReadCarrierGlobals(0x00400000u, &failedName).code ==
               AdapterCode::NameReadFailed &&
               failedName.sizeCalls == 1 && failedName.pointerCalls == 1 &&
               failedName.nameCalls == 1,
           "a failed fixed-name read must fail after the two typed scalar reads");

    FakeMemoryProbe shortName;
    shortName.nameRead.bytesRead = 9u;
    Expect(ReadCarrierGlobals(0x00400000u, &shortName).code ==
               AdapterCode::NameReadShort,
           "a nine-byte name read must not omit the required trailing NUL");
}

void TestExactNameSizeAndPointerIdentity() {
    FakeMemoryProbe nullPointer;
    nullPointer.pointerRead.value = 0u;
    Expect(ReadCarrierGlobals(0x00400000u, &nullPointer).code ==
               AdapterCode::NullCarrierPointer,
           "a null carrier ptr32 global must fail closed");

    FakeMemoryProbe shortSize;
    shortSize.sizeRead.value = 0x4427u;
    FakeMemoryProbe longSize;
    longSize.sizeRead.value = 0x4429u;
    Expect(ReadCarrierGlobals(0x00400000u, &shortSize).code ==
               AdapterCode::InvalidCarrierSize &&
               ReadCarrierGlobals(0x00400000u, &longSize).code ==
               AdapterCode::InvalidCarrierSize,
           "only the exact 0x4428 carrier size may be admitted");

    for (std::size_t index = 0u; index < 9u; ++index) {
        FakeMemoryProbe mismatch;
        mismatch.nameRead.value[index] =
            static_cast<char>(mismatch.nameRead.value[index] ^ 0x01);
        Expect(ReadCarrierGlobals(0x00400000u, &mismatch).code ==
                   AdapterCode::InvalidCarrierName,
               "every byte of dome02_00 must be validated exactly");
    }

    FakeMemoryProbe missingNul;
    missingNul.nameRead.value[9] = 'X';
    Expect(ReadCarrierGlobals(0x00400000u, &missingNul).code ==
               AdapterCode::InvalidCarrierName,
           "dome02_00 must be followed immediately by NUL");

    CarrierGlobals forged = ValidGlobals();
    forged.carrierSize = 0x4427u;
    Expect(MaterializeCarrierView(forged, ValidRegion()).code ==
               AdapterCode::InvalidCarrierSize,
           "materialization must revalidate the exact carrier size");
    forged = ValidGlobals();
    forged.carrierName[0] = 'x';
    Expect(MaterializeCarrierView(forged, ValidRegion()).code ==
               AdapterCode::InvalidCarrierName,
           "materialization must revalidate the exact carrier name");
}

void TestInclusiveRegionContainmentAnd32BitBoundaries() {
    const std::uint32_t carrierAddress = 0x10002000u;
    const CarrierGlobals globals = ValidGlobals(carrierAddress);

    RegionInfo exact = ValidRegion(carrierAddress);
    Expect(MaterializeCarrierView(globals, exact).code == AdapterCode::Ready,
           "a region ending on the carrier's inclusive last byte must be accepted");

    RegionInfo prefixed = exact;
    prefixed.baseAddress = carrierAddress - 0x100u;
    prefixed.regionSize = 0x100u + 0x4428u;
    Expect(MaterializeCarrierView(globals, prefixed).code == AdapterCode::Ready,
           "a carrier may start inside a region when its inclusive end is contained");

    RegionInfo oneByteShort = prefixed;
    --oneByteShort.regionSize;
    Expect(MaterializeCarrierView(globals, oneByteShort).code ==
               AdapterCode::CarrierOutsideRegion,
           "a carrier crossing the queried region by one byte must be rejected");

    RegionInfo pointerBelow = exact;
    pointerBelow.baseAddress = carrierAddress + 1u;
    Expect(MaterializeCarrierView(globals, pointerBelow).code ==
               AdapterCode::CarrierOutsideRegion,
           "a carrier pointer below the queried region base must be rejected");

    RegionInfo pointerAbove = exact;
    pointerAbove.baseAddress = carrierAddress - 0x100u;
    pointerAbove.regionSize = 0x80u;
    Expect(MaterializeCarrierView(globals, pointerAbove).code ==
               AdapterCode::CarrierOutsideRegion,
           "a carrier pointer above the queried region end must be rejected");

    const std::uint32_t highestCarrier = 0xFFFFBBD8u;
    Expect(MaterializeCarrierView(
               ValidGlobals(highestCarrier), ValidRegion(highestCarrier)).code ==
               AdapterCode::Ready,
           "a carrier whose inclusive last byte is UINT32_MAX must be accepted");
    RegionInfo broadNonOverflowingRegion = ValidRegion(0u);
    broadNonOverflowingRegion.regionSize = 0xFFFFFFFFu;
    Expect(MaterializeCarrierView(
               ValidGlobals(highestCarrier + 1u), broadNonOverflowingRegion).code ==
               AdapterCode::CarrierRangeOverflow,
           "a carrier whose inclusive last byte exceeds UINT32_MAX must be rejected");

    RegionInfo overflowingRegion = ValidRegion(0xFFFFFFF0u);
    overflowingRegion.regionSize = 0x20u;
    Expect(MaterializeCarrierView(
               ValidGlobals(0xFFFFFFF0u), overflowingRegion).code ==
               AdapterCode::RegionRangeOverflow,
           "an overflowing VirtualQuery-style region range must be rejected");

    RegionInfo emptyRegion = ValidRegion(carrierAddress);
    emptyRegion.regionSize = 0u;
    Expect(MaterializeCarrierView(globals, emptyRegion).code ==
               AdapterCode::InvalidRegion,
           "a zero-length region cannot authorize carrier access");
}

void TestCommittedAndWritableProtectionCategories() {
    struct ProtectionCase {
        std::uint32_t protection;
        AdapterCode expected;
        const char* message;
    };
    const std::array<ProtectionCase, 12u> cases = {{
        {kPageNoAccess, AdapterCode::RegionNoAccess,
         "PAGE_NOACCESS must be rejected"},
        {kPageReadOnly, AdapterCode::RegionNotWritable,
         "PAGE_READONLY must be rejected"},
        {kPageReadWrite, AdapterCode::Ready,
         "PAGE_READWRITE must be accepted"},
        {kPageWriteCopy, AdapterCode::Ready,
         "PAGE_WRITECOPY must be accepted"},
        {kPageExecute, AdapterCode::RegionNotWritable,
         "PAGE_EXECUTE must be rejected"},
        {kPageExecuteRead, AdapterCode::RegionNotWritable,
         "PAGE_EXECUTE_READ must be rejected"},
        {kPageExecuteReadWrite, AdapterCode::Ready,
         "PAGE_EXECUTE_READWRITE must be accepted as writable"},
        {kPageExecuteWriteCopy, AdapterCode::Ready,
         "PAGE_EXECUTE_WRITECOPY must be accepted as writable"},
        {0u, AdapterCode::RegionNotWritable,
         "an unknown protection must be rejected"},
        {kPageReadWrite | kPageNoCache, AdapterCode::Ready,
         "PAGE_NOCACHE must not erase writable base protection"},
        {kPageReadWrite | kPageWriteCombine, AdapterCode::Ready,
         "PAGE_WRITECOMBINE must not erase writable base protection"},
        {kPageReadWrite | kPageGuard, AdapterCode::RegionGuarded,
         "PAGE_GUARD must override writable base protection"},
    }};

    for (const ProtectionCase& test : cases) {
        RegionInfo region = ValidRegion();
        region.protection = test.protection;
        const CarrierViewOutcome outcome =
            MaterializeCarrierView(ValidGlobals(), region);
        const bool viewIsClosed =
            test.expected == AdapterCode::Ready
                ? outcome.carrier.bytes != nullptr &&
                      outcome.carrier.access == CarrierAccess::ReadWrite
                : outcome.carrier.bytes == nullptr &&
                      outcome.carrier.access == CarrierAccess::Invalid;
        Expect(outcome.code == test.expected && viewIsClosed,
               test.message);
    }

    RegionInfo noAccessWithModifier = ValidRegion();
    noAccessWithModifier.protection = kPageNoAccess | kPageNoCache;
    Expect(MaterializeCarrierView(ValidGlobals(), noAccessWithModifier).code ==
               AdapterCode::RegionNoAccess,
           "PAGE_NOACCESS must remain rejected when a modifier is present");

    RegionInfo reserved = ValidRegion();
    reserved.state = RegionState::Reserve;
    RegionInfo free = ValidRegion();
    free.state = RegionState::Free;
    RegionInfo unknown = ValidRegion();
    unknown.state = RegionState::Unknown;
    Expect(MaterializeCarrierView(ValidGlobals(), reserved).code ==
               AdapterCode::RegionNotCommitted &&
               MaterializeCarrierView(ValidGlobals(), free).code ==
               AdapterCode::RegionNotCommitted &&
               MaterializeCarrierView(ValidGlobals(), unknown).code ==
               AdapterCode::RegionNotCommitted,
           "reserve, free, and unknown region states must all fail the commit gate");
}

}  // namespace

int main() {
    TestExactTypedReadsAndCarrierMaterialization();
    TestModuleAddressArithmeticFailsClosed();
    TestNullFailedAndShortReadsFailClosed();
    TestExactNameSizeAndPointerIdentity();
    TestInclusiveRegionContainmentAnd32BitBoundaries();
    TestCommittedAndWritableProtectionCategories();

    std::printf("CustomMix Windows adapter RT1: %d/%d passed\n",
                g_passed, g_passed + g_failed);
    return g_failed == 0 ? 0 : 1;
}
