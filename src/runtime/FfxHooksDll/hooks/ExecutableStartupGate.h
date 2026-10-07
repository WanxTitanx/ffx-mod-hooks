#pragma once
#include "F8RuntimeCore.h"
#include "../shared/ExecutableProfile.h"
#include <cstring>

namespace FfxHooks::ExecutableStartup {
inline bool HashMatches(const std::uint8_t* bytes,std::size_t size) noexcept {
    return bytes&&size==ExecutableProfile::Sha256.size()&&
        std::memcmp(bytes,ExecutableProfile::Sha256.data(),size)==0;
}
// Legacy/lab adapters have narrower local checks. They must never get a chance
// to dereference their address tables in an unknown executable or helper host.
inline bool CanStart(bool hasFfxModule, const std::uint8_t* header, std::size_t size,
                     F8Runtime::ExecutableIdentity* identity) noexcept {
    if (identity) *identity = {};
    return hasFfxModule &&
        F8Runtime::ParseExecutableIdentity(header, size, identity) == F8Runtime::ProfileResult::Supported &&
        ExecutableProfile::NativeStartupValidated;
}
}
