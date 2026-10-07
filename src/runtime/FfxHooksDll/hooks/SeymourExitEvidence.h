#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "../shared/ExecutableProfile.h"
namespace FfxHooks::SeymourBattle::ExitEvidence {
// The actual exit body loads VA 0x1132078, hence RVA D32078. F32078
// would add an extra 2 MiB and reject the native body on both executables.
inline constexpr std::uint32_t ActorTableRva = ::FfxHooks::ExecutableProfile::Rva<0x00D32078u>();
inline bool ActorTableOperandMatches(const unsigned char* bytes, std::size_t count,
                                    std::uintptr_t base) noexcept {
    if (!bytes || count < 25 || bytes[20] != 0xBE) return false;
    std::uint32_t actual = 0;
    std::memcpy(&actual, bytes + 21, sizeof(actual));
    const auto expected = std::uint64_t(base) + ActorTableRva;
    return expected <= UINT32_MAX && actual == expected;
}
}
