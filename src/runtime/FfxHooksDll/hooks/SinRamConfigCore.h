#pragma once

#include "SinRamScalingCore.h"

#include <cstddef>
#include <cstdint>

namespace FfxHooks::SinRamConfig {

// The value shares the existing bounded F7 document but owns no persistence capability.
inline constexpr std::size_t kMaximumDocumentBytes = 16384;
inline constexpr std::size_t kMaximumJsonDepth = 8;

enum class Code : std::uint8_t {
    Ok = 0,
    InvalidArgument,
    TooLarge,
    Malformed,
    DepthExceeded,
    DuplicateKey,
    UnknownKey,
    WrongType,
    OutOfRange,
    OutputTooSmall,
};

struct ParseResult {
    Code code = Code::InvalidArgument;
    std::size_t offset = 0;
    bool present = false;
};

struct SerializeResult {
    Code code = Code::InvalidArgument;
    std::size_t length = 0;
};

// Parses a complete F7 JSON document while exposing only S.I.N. state. On every
// failure, output is reset to OFF/T0; Difficulty validity is not an input or output.
ParseResult ParseDocument(
    const char* document, std::size_t length, SinRam::Config* output);

// Emits only the canonical value for the root "sinRam" member. The existing F7
// atomic saver remains the sole owner of commas, the root object, and file I/O.
SerializeResult SerializeValue(
    const SinRam::Config& config, char* output, std::size_t capacity);

}  // namespace FfxHooks::SinRamConfig
