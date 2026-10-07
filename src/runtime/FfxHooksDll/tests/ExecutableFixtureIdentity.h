#pragma once
#include "../shared/ExecutableProfile.h"

// Independently recorded input identities; fixture metadata must describe the
// actual selected PE, including when the production profile lookup is broken.
namespace ExecutableFixtureIdentity {
#ifdef FFXHOOKS_TARGET_STEAM_20261001
#define FFXHOOKS_FIXTURE_SHA256 "0537b2a1047f3266e73495cd4e35f63f0777f4231d417699f979954686da686d"
inline constexpr const char* ShaPrefix = "0537b2a1";
#else
#define FFXHOOKS_FIXTURE_SHA256 "78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced"
inline constexpr const char* ShaPrefix = "78ce3439";
#endif
inline constexpr const char* Sha256 = FFXHOOKS_FIXTURE_SHA256;
inline constexpr std::uint32_t Timestamp = ::FfxHooks::ExecutableProfile::Steam20261001
    ? 0x6AA2219Cu : 0x55D2F3CCu;
}
