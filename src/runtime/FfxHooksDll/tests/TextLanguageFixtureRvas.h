#pragma once
#include "../shared/ExecutableProfile.h"

// Jarvis-HOOK: fixture-only stubs and observers. The production adapter uses the
// shared catalogue. These additional sites were matched in the exact legacy
// 78ce3439... and Steam 0537b2a1... PEs; no inferred uniform relocation delta.
template<std::uint32_t LegacyRva>
constexpr std::uint32_t TextFixtureRva() noexcept {
#ifdef FFXHOOKS_TARGET_STEAM_20261001
    if constexpr (LegacyRva==0x353F0) return 0x352F0;
    else if constexpr (LegacyRva==0x2082A0) return 0x2080E0;
    else if constexpr (LegacyRva==0x21BF70) return 0x21BDB0;
    else if constexpr (LegacyRva==0x21C0D0) return 0x21BF10;
    else if constexpr (LegacyRva==0x22F6B0) return 0x22F500;
    else if constexpr (LegacyRva==0x387430) return 0x387370;
    else if constexpr (LegacyRva==0x4B3EE3) return 0x4B3F33;
    else if constexpr (LegacyRva==0x54925C) return 0x549256;
    else return ::FfxHooks::ExecutableProfile::Rva<LegacyRva>();
#else
    return ::FfxHooks::ExecutableProfile::Rva<LegacyRva>();
#endif
}
