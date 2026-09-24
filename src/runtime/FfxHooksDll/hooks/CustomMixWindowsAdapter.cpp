#include "CustomMixWindowsAdapter.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace FfxHooks::CustomMixUltra::WindowsAdapter {
namespace {

constexpr std::uint64_t kAddress32Max = 0xFFFFFFFFull;
constexpr std::uint32_t kBaseProtectionMask = 0xFFu;
constexpr std::array<char, kCarrierNameReadWidth> kExpectedCarrierName = {{
    'd', 'o', 'm', 'e', '0', '2', '_', '0', '0', '\0',
}};

static_assert(kExpectedCarrierSize == 0x4428u,
              "offline carrier evidence requires an exact 0x4428-byte allocation");
static_assert(kExpectedEncounterToken == 0x02050000u,
              "offline carrier evidence requires the exact encounter token");
static_assert(kExpectedPatchBytes == 16u,
              "the portable core must remain the sole sixteen-byte patch owner");
static_assert(sizeof(void*) == 4u,
              "the production carrier adapter supports only the PE32/I386 profile");

bool ResolveReadSpan32(std::uint64_t imageBase,
                       std::uint32_t rva,
                       std::size_t width,
                       std::uint32_t* addressOut) noexcept {
    if (addressOut == nullptr || imageBase > kAddress32Max || width == 0u) {
        return false;
    }
    const std::uint64_t address = imageBase + static_cast<std::uint64_t>(rva);
    const std::uint64_t tail = static_cast<std::uint64_t>(width - 1u);
    if (address > kAddress32Max || tail > kAddress32Max - address) return false;
    *addressOut = static_cast<std::uint32_t>(address);
    return true;
}

bool InclusiveLast32(std::uint32_t first,
                     std::uint32_t length,
                     std::uint32_t* lastOut) noexcept {
    if (lastOut == nullptr || length == 0u) return false;
    const std::uint32_t tail = length - 1u;
    if (first > static_cast<std::uint32_t>(kAddress32Max) - tail) return false;
    *lastOut = first + tail;
    return true;
}

bool HasExactCarrierName(
    const std::array<char, kCarrierNameReadWidth>& actual) noexcept {
    for (std::size_t index = 0u; index < kExpectedCarrierName.size(); ++index) {
        if (actual[index] != kExpectedCarrierName[index]) return false;
    }
    return true;
}

bool IsWritableProtection(std::uint32_t protection) noexcept {
    switch (protection & kBaseProtectionMask) {
    case kPageReadWrite:
    case kPageWriteCopy:
    case kPageExecuteReadWrite:
    case kPageExecuteWriteCopy:
        return true;
    default:
        return false;
    }
}

GlobalReadOutcome RejectGlobal(AdapterCode code) noexcept {
    GlobalReadOutcome outcome{};
    outcome.code = code;
    return outcome;
}

CarrierViewOutcome RejectCarrier(AdapterCode code) noexcept {
    CarrierViewOutcome outcome{};
    outcome.code = code;
    return outcome;
}

}  // namespace

#if defined(FFXHOOKS_TESTING)
namespace Testing {

bool CarrierSizeReadSpanFits32(std::uint64_t imageBase) noexcept {
    std::uint32_t address = 0u;
    return ResolveReadSpan32(
        imageBase, kCarrierSizeRva, kCarrierSizeReadWidth, &address);
}

bool CarrierPointerReadSpanFits32(std::uint64_t imageBase) noexcept {
    std::uint32_t address = 0u;
    return ResolveReadSpan32(
        imageBase, kCarrierPointerRva, kCarrierPointerReadWidth, &address);
}

bool CarrierNameReadSpanFits32(std::uint64_t imageBase) noexcept {
    std::uint32_t address = 0u;
    return ResolveReadSpan32(
        imageBase, kCarrierNameRva, kCarrierNameReadWidth, &address);
}

}  // namespace Testing
#endif

GlobalReadOutcome ReadCarrierGlobals(
    std::uint64_t imageBase, const MemoryProbe* probe) noexcept {
    if (probe == nullptr) return RejectGlobal(AdapterCode::NullProbe);
    if (imageBase == 0u || imageBase > kAddress32Max) {
        return RejectGlobal(AdapterCode::InvalidImageBase);
    }

    // WHY: resolve every complete typed span before the first injected read. A
    // relocated module whose final u16, ptr32, or name10 byte crosses PE32 therefore
    // has no partial probe, even when the corresponding start address still fits.
    std::uint32_t sizeAddress = 0u;
    std::uint32_t pointerAddress = 0u;
    std::uint32_t nameAddress = 0u;
    const bool sizeSpanFits = ResolveReadSpan32(
        imageBase, kCarrierSizeRva, kCarrierSizeReadWidth, &sizeAddress);
    const bool pointerSpanFits = ResolveReadSpan32(
        imageBase, kCarrierPointerRva, kCarrierPointerReadWidth, &pointerAddress);
    const bool nameSpanFits = ResolveReadSpan32(
        imageBase, kCarrierNameRva, kCarrierNameReadWidth, &nameAddress);
    if (!sizeSpanFits || !pointerSpanFits || !nameSpanFits) {
        return RejectGlobal(AdapterCode::GlobalAddressOverflow);
    }

    const U16ProbeRead sizeRead = probe->ReadCarrierSizeU16(sizeAddress);
    if (!sizeRead.succeeded) return RejectGlobal(AdapterCode::SizeReadFailed);
    if (sizeRead.bytesRead != kCarrierSizeReadWidth) {
        return RejectGlobal(AdapterCode::SizeReadShort);
    }
    if (sizeRead.value != kExpectedCarrierSize) {
        return RejectGlobal(AdapterCode::InvalidCarrierSize);
    }

    const Pointer32ProbeRead pointerRead =
        probe->ReadCarrierPointer32(pointerAddress);
    if (!pointerRead.succeeded) return RejectGlobal(AdapterCode::PointerReadFailed);
    if (pointerRead.bytesRead != kCarrierPointerReadWidth) {
        return RejectGlobal(AdapterCode::PointerReadShort);
    }
    if (pointerRead.value == 0u) {
        return RejectGlobal(AdapterCode::NullCarrierPointer);
    }

    const Name10ProbeRead nameRead = probe->ReadCarrierName10(nameAddress);
    if (!nameRead.succeeded) return RejectGlobal(AdapterCode::NameReadFailed);
    if (nameRead.bytesRead != kCarrierNameReadWidth) {
        return RejectGlobal(AdapterCode::NameReadShort);
    }
    if (!HasExactCarrierName(nameRead.value)) {
        return RejectGlobal(AdapterCode::InvalidCarrierName);
    }

    GlobalReadOutcome outcome{};
    outcome.code = AdapterCode::Ready;
    outcome.globals.carrierSize = sizeRead.value;
    outcome.globals.carrierAddress = pointerRead.value;
    outcome.globals.carrierName = nameRead.value;
    return outcome;
}

CarrierViewOutcome MaterializeCarrierView(
    const CarrierGlobals& globals, const RegionInfo& region) noexcept {
    // Revalidation keeps constructed test values or a future shim bug from bypassing
    // the exact identity already checked at the global-read boundary.
    if (globals.carrierSize != kExpectedCarrierSize) {
        return RejectCarrier(AdapterCode::InvalidCarrierSize);
    }
    if (globals.carrierAddress == 0u) {
        return RejectCarrier(AdapterCode::NullCarrierPointer);
    }
    if (!HasExactCarrierName(globals.carrierName)) {
        return RejectCarrier(AdapterCode::InvalidCarrierName);
    }
    if (region.regionSize == 0u) {
        return RejectCarrier(AdapterCode::InvalidRegion);
    }

    std::uint32_t regionLast = 0u;
    if (!InclusiveLast32(region.baseAddress, region.regionSize, &regionLast)) {
        return RejectCarrier(AdapterCode::RegionRangeOverflow);
    }
    std::uint32_t carrierLast = 0u;
    if (!InclusiveLast32(
            globals.carrierAddress,
            static_cast<std::uint32_t>(kExpectedCarrierSize),
            &carrierLast)) {
        return RejectCarrier(AdapterCode::CarrierRangeOverflow);
    }

    // WHY: one value-only region must contain both inclusive endpoints. This rejects
    // a cross-region carrier even if its first page alone is committed and writable.
    if (globals.carrierAddress < region.baseAddress || carrierLast > regionLast) {
        return RejectCarrier(AdapterCode::CarrierOutsideRegion);
    }
    if (region.state != RegionState::Commit) {
        return RejectCarrier(AdapterCode::RegionNotCommitted);
    }
    if ((region.protection & kPageGuard) != 0u) {
        return RejectCarrier(AdapterCode::RegionGuarded);
    }
    if ((region.protection & kPageNoAccess) != 0u) {
        return RejectCarrier(AdapterCode::RegionNoAccess);
    }
    if (!IsWritableProtection(region.protection)) {
        return RejectCarrier(AdapterCode::RegionNotWritable);
    }

    CarrierViewOutcome outcome{};
    outcome.code = AdapterCode::Ready;
    outcome.carrier.bytes = reinterpret_cast<std::uint8_t*>(
        static_cast<std::uintptr_t>(globals.carrierAddress));
    outcome.carrier.size = kExpectedCarrierSize;
    outcome.carrier.encounterToken = kExpectedEncounterToken;
    outcome.carrier.encounterName = kExpectedCarrierName.data();
    outcome.carrier.encounterNameLength = kExpectedCarrierNameLength;
    outcome.carrier.access = CarrierAccess::ReadWrite;
    return outcome;
}

}  // namespace FfxHooks::CustomMixUltra::WindowsAdapter
