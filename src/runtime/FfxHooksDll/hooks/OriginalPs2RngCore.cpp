#include "OriginalPs2RngCore.h"

namespace FfxHooks::OriginalPs2Rng {
// No signed overflow, signed left shift or implementation-defined right shift.
std::uint32_t SignedHalfMix(std::uint32_t value) noexcept {
    const auto high = (value >> 16) | ((value & 0x80000000u) ? 0xFFFF0000u : 0u);
    return (value << 16) + high;
}
std::uint8_t ClockXor(const ClockBytes& bytes) noexcept {
    std::uint8_t result = 0;
    for (auto value : bytes) result ^= value;
    return result;
}
std::uint32_t TemporalArgument(std::uint64_t counter, std::uint32_t extra) noexcept {
    return static_cast<std::uint32_t>(counter) + extra;
}
std::uint32_t AdvanceNormal(std::uint32_t& state) noexcept {
    state = SignedHalfMix(state * 0x5D588B65u + 0x3C35u);
    return state & 0x7FFFFFFFu;
}
std::uint32_t AdvanceAuxiliary(std::uint32_t& state, std::uint32_t counter) noexcept {
    state = SignedHalfMix(state * 0x6C078965u + counter + 0x4F07u);
    return state & 0x7FFFFFFFu;
}
bool AdvanceChannel(State& state, const Tables& tables, std::size_t channel,
                    std::uint32_t& result) noexcept {
    if (channel >= ChannelCount) return false;
    auto& value = state.channels[channel];
    value = SignedHalfMix((value * tables.multipliers[channel]) ^ tables.xorValues[channel]);
    result = value & 0x7FFFFFFFu;
    return true;
}
Initialized Initialize(std::uint8_t clockXor, std::uint32_t temporalArgument) noexcept {
    Initialized value{};
    const std::uint32_t product = (std::uint32_t(clockXor) + 1u) * (temporalArgument + 1u);
    value.state.normal = product * 0x420C56D7u + 0x2E0Au;
    value.state.auxiliary = product * 0x599E67E6u + 0x301Du;
    (void)AdvanceNormal(value.state.normal);
    value.seed = value.state.normal;
    for (auto& channel : value.state.channels) channel = AdvanceNormal(value.state.normal);
    return value;
}
} // namespace FfxHooks::OriginalPs2Rng
