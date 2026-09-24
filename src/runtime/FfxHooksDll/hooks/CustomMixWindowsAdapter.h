#pragma once

#include "CustomMixUltraCore.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace FfxHooks::CustomMixUltra::WindowsAdapter {

// Offline carrier evidence for the sole supported PE32/I386 executable profile.
// The production caller remains responsible for the executable hash/signature gate;
// this adapter accepts an actual relocated image base and applies only these RVAs.
constexpr std::uint32_t kSupportedPreferredImageBase = 0x00400000u;
constexpr std::uint32_t kCarrierSizeRva = 0x00D2A9A6u;
constexpr std::uint32_t kCarrierPointerRva = 0x00D2A9A8u;
constexpr std::uint32_t kCarrierNameRva = 0x00D2C25Au;
constexpr std::size_t kCarrierSizeReadWidth = sizeof(std::uint16_t);
constexpr std::size_t kCarrierPointerReadWidth = sizeof(std::uint32_t);
constexpr std::size_t kCarrierNameReadWidth = 10u;
constexpr std::size_t kExpectedCarrierNameLength = 9u;
constexpr std::size_t kExpectedCarrierSize = kCarrierSize;
constexpr std::uint32_t kExpectedEncounterToken = kCarrierEncounterToken;
constexpr std::size_t kExpectedPatchBytes = kFormationSlotBytes; // Formation-only base path.
constexpr std::size_t kMaximumPatchBytes = kFormationSlotBytes + kMonsterSlotCount * 8u;

// WHY: numeric values mirror the Win32 MEMORY_BASIC_INFORMATION contract without
// pulling query authority into this portable adapter. A future production shim
// gathers one RegionInfo value; this module only validates the closed value facts.
enum class RegionState : std::uint32_t {
    Unknown = 0u,
    Commit = 0x00001000u,
    Reserve = 0x00002000u,
    Free = 0x00010000u,
};

constexpr std::uint32_t kPageNoAccess = 0x00000001u;
constexpr std::uint32_t kPageReadOnly = 0x00000002u;
constexpr std::uint32_t kPageReadWrite = 0x00000004u;
constexpr std::uint32_t kPageWriteCopy = 0x00000008u;
constexpr std::uint32_t kPageExecute = 0x00000010u;
constexpr std::uint32_t kPageExecuteRead = 0x00000020u;
constexpr std::uint32_t kPageExecuteReadWrite = 0x00000040u;
constexpr std::uint32_t kPageExecuteWriteCopy = 0x00000080u;
constexpr std::uint32_t kPageGuard = 0x00000100u;
constexpr std::uint32_t kPageNoCache = 0x00000200u;
constexpr std::uint32_t kPageWriteCombine = 0x00000400u;

struct RegionInfo {
    std::uint32_t baseAddress = 0u;
    std::uint32_t regionSize = 0u;
    RegionState state = RegionState::Unknown;
    std::uint32_t protection = 0u;
};

struct U16ProbeRead {
    bool succeeded = false;
    std::size_t bytesRead = 0u;
    std::uint16_t value = 0u;
};

struct Pointer32ProbeRead {
    bool succeeded = false;
    std::size_t bytesRead = 0u;
    std::uint32_t value = 0u;
};

struct Name10ProbeRead {
    bool succeeded = false;
    std::size_t bytesRead = 0u;
    std::array<char, kCarrierNameReadWidth> value{};
};

// WHY: carrier-specific typed reads are the entire injected capability. There is no
// generic address/length callback, opaque context, writer, or ownership transfer for a
// test double or future production shim to expand into unrelated memory access.
class MemoryProbe {
public:
    virtual ~MemoryProbe() = default;
    virtual U16ProbeRead ReadCarrierSizeU16(
        std::uint32_t address) const noexcept = 0;
    virtual Pointer32ProbeRead ReadCarrierPointer32(
        std::uint32_t address) const noexcept = 0;
    virtual Name10ProbeRead ReadCarrierName10(
        std::uint32_t address) const noexcept = 0;
};

struct CarrierGlobals {
    std::uint16_t carrierSize = 0u;
    std::uint32_t carrierAddress = 0u;
    std::array<char, kCarrierNameReadWidth> carrierName{};
};

enum class AdapterCode : std::uint8_t {
    Invalid = 0,
    Ready,
    NullProbe,
    InvalidImageBase,
    GlobalAddressOverflow,
    SizeReadFailed,
    SizeReadShort,
    PointerReadFailed,
    PointerReadShort,
    NameReadFailed,
    NameReadShort,
    InvalidCarrierSize,
    NullCarrierPointer,
    InvalidCarrierName,
    InvalidRegion,
    RegionRangeOverflow,
    CarrierRangeOverflow,
    CarrierOutsideRegion,
    RegionNotCommitted,
    RegionGuarded,
    RegionNoAccess,
    RegionNotWritable,
};

struct GlobalReadOutcome {
    AdapterCode code = AdapterCode::Invalid;
    CarrierGlobals globals{};
};

struct CarrierViewOutcome {
    AdapterCode code = AdapterCode::Invalid;
    CarrierView carrier{};
};

GlobalReadOutcome ReadCarrierGlobals(
    std::uint64_t imageBase, const MemoryProbe* probe) noexcept;

// This function validates only value facts and pointer arithmetic. It does not query,
// dereference, call the original, patch, or restore carrier memory. ExecuteTransaction
// in CustomMixUltraCore remains the sole formation/optional-position transaction owner and
// revalidates the carrier header, chunk metadata, and vanilla formation slots.
CarrierViewOutcome MaterializeCarrierView(
    const CarrierGlobals& globals, const RegionInfo& region) noexcept;

#if defined(FFXHOOKS_TESTING)
namespace Testing {

// Closed test seams exercise each fixed-width address contract independently; they
// expose no read, callback, context, writer, or arbitrary RVA/width input.
bool CarrierSizeReadSpanFits32(std::uint64_t imageBase) noexcept;
bool CarrierPointerReadSpanFits32(std::uint64_t imageBase) noexcept;
bool CarrierNameReadSpanFits32(std::uint64_t imageBase) noexcept;

}  // namespace Testing
#endif

}  // namespace FfxHooks::CustomMixUltra::WindowsAdapter
