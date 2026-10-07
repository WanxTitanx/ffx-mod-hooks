#pragma once
#include <array>
#include <cstdint>

namespace FfxHooks::ExecutableProfile {
#ifdef FFXHOOKS_TARGET_STEAM_20261001
inline constexpr bool Steam20261001 = true;
inline constexpr std::uint32_t Timestamp = 0x6AA2219Cu;
// Native bytes, relocation geometry and isolated consumer/lifecycle matrices
// are validated for this exact PE. File SHA256 admission precedes all services.
// This is offline/isolated validation, not RT2 or Production promotion.
inline constexpr bool NativeStartupValidated = true;
#else
inline constexpr bool Steam20261001 = false;
inline constexpr std::uint32_t Timestamp = 0x55D2F3CCu;
inline constexpr bool NativeStartupValidated = true;
#endif

inline constexpr std::array<std::uint8_t, 32> LegacySha256 = {
    0x78,0xCE,0x34,0x39,0x7D,0xA5,0xE6,0xF4,0x9B,0x72,0xC2,0xAE,0xBA,0xDE,0xDA,0xF4,
    0xCD,0x3F,0x67,0x20,0xE1,0x94,0x9D,0x46,0xA1,0xB8,0xED,0x67,0xD3,0xDB,0x5C,0xED};
inline constexpr std::array<std::uint8_t, 32> SteamSha256 = {
    0x05,0x37,0xB2,0xA1,0x04,0x7F,0x32,0x66,0xE7,0x34,0x95,0xCD,0x4E,0x35,0xF6,0x3F,
    0x07,0x77,0xF4,0x23,0x1D,0x41,0x76,0x99,0xF9,0x79,0x95,0x46,0x86,0xDA,0x68,0x6D};
inline constexpr auto Sha256 = Steam20261001 ? SteamSha256 : LegacySha256;
inline constexpr std::uint32_t FileBytes = Steam20261001 ? 10687744u : 10675712u;
inline constexpr const char* Sha256Hex = Steam20261001
    ? "0537b2a1047f3266e73495cd4e35f63f0777f4231d417699f979954686da686d"
    : "78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced";

struct AddressPair { std::uint32_t legacy, updated; };
#ifdef FFXHOOKS_TARGET_STEAM_20261001
#include "Steam20261001Addresses.generated.inc"
constexpr std::uint32_t Lookup(std::uint32_t legacy) noexcept {
    std::size_t low = 0, high = sizeof(SteamAddresses) / sizeof(SteamAddresses[0]);
    while (low < high) {
        const auto middle = low + (high - low) / 2;
        if (SteamAddresses[middle].legacy < legacy) low = middle + 1;
        else high = middle;
    }
    return low < sizeof(SteamAddresses) / sizeof(SteamAddresses[0]) && SteamAddresses[low].legacy == legacy
        ? SteamAddresses[low].updated : 0;
}
#endif

template<std::uint32_t LegacyRva>
constexpr std::uint32_t Rva() noexcept {
#ifdef FFXHOOKS_TARGET_STEAM_20261001
    constexpr auto current = Lookup(LegacyRva);
    static_assert(current != 0, "Steam executable address correspondence is missing; do not reuse a legacy RVA");
    return current;
#else
    return LegacyRva;
#endif
}

template<std::uint32_t LegacyVa>
constexpr std::uint32_t Va() noexcept {
    static_assert(LegacyVa >= 0x00400000u, "FFX preferred VA must include the image base");
    return 0x00400000u + Rva<LegacyVa - 0x00400000u>();
}
}
