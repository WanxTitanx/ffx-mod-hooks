#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace FfxHooks::OriginalPs2Rng {
inline constexpr std::size_t ChannelCount = 68;
using ClockBytes = std::array<std::uint8_t, 8>;
struct Tables {
    std::array<std::uint32_t, ChannelCount> multipliers{};
    std::array<std::uint16_t, ChannelCount> xorValues{};
};
struct State {
    std::uint32_t normal = 0;
    std::uint32_t auxiliary = 0;
    std::array<std::uint32_t, ChannelCount> channels{};
};
struct Initialized {
    State state{};
    // Full normal state after the single discarded warm-up, before channel0.
    std::uint32_t seed = 0;
};

std::uint32_t SignedHalfMix(std::uint32_t value) noexcept;
std::uint8_t ClockXor(const ClockBytes& bytes) noexcept;
std::uint32_t TemporalArgument(std::uint64_t counter, std::uint32_t extra) noexcept;
std::uint32_t AdvanceNormal(std::uint32_t& state) noexcept;
std::uint32_t AdvanceAuxiliary(std::uint32_t& state, std::uint32_t counter) noexcept;
bool AdvanceChannel(State& state, const Tables& tables, std::size_t channel,
                    std::uint32_t& result) noexcept;
Initialized Initialize(std::uint8_t clockXor, std::uint32_t temporalArgument) noexcept;
} // namespace FfxHooks::OriginalPs2Rng
