#include "../hooks/OriginalPs2RngCore.h"
#include "OriginalPs2RngReference.generated.h"
#include <cstdio>
#include <set>

namespace R = FfxHooks::OriginalPs2Rng;
namespace V = OriginalPs2RngReference;
static unsigned checks = 0, failures = 0;
static void Check(bool ok, const char* label) {
    ++checks;
    if (!ok && ++failures < 16) std::printf("FAIL %s\n", label);
}
static bool Equal(const R::State& a, const R::State& b) {
    return a.normal == b.normal && a.auxiliary == b.auxiliary && a.channels == b.channels;
}
int main() {
    Check(R::SignedHalfMix(0x80000000u) == 0xFFFF8000u, "arithmetic high-half sign extension");
    Check(R::SignedHalfMix(0x80000001u) == 0x00008000u, "signed-half addition is not rotate or OR");
    Check(R::SignedHalfMix(0xFFFFFFFFu) == 0xFFFEFFFFu, "all-one signed-half wrap");
    Check(R::SignedHalfMix(0x00010000u) == 1u, "unsigned high-half positive control");
    Check(R::TemporalArgument(0x100000000ULL, 0) == 0, "64-bit counter reduces at the native 32-bit argument");
    Check(R::TemporalArgument(0xFFFFFFFFULL, 2) == 1, "caller extra term is preserved across wrap");
    Check(R::TemporalArgument(256, 17) == 273, "counter is not truncated to eight bits");
    for (unsigned byte = 0; byte < 8; ++byte) {
        R::ClockBytes clock{}; clock[byte] = 0xA5;
        Check(R::ClockXor(clock) == 0xA5, "all eight clock bytes contribute, including status and padding");
    }
    for (const auto& v : V::vectors) {
        const auto initialized = R::Initialize(v.clock, v.frame);
        Check(initialized.seed == v.seed, "reference full seed follows one discarded warm-up");
        Check(initialized.state.normal == v.normal, "normal global retains the full final state");
        Check(initialized.state.auxiliary == v.auxiliary, "auxiliary global has its independent initializer");
        for (unsigned i = 0; i < R::ChannelCount; ++i)
            Check(initialized.state.channels[i] == v.channels[i], "all 68 channels match the pinned reference digest");
        auto wrong = (std::uint32_t(v.clock) + 1u) * (v.frame + 1u) * 0x420C56D7u + 0x2E0Au;
        Check(wrong != initialized.seed, "zero-warm-up negative control differs");
        (void)R::AdvanceNormal(wrong); (void)R::AdvanceNormal(wrong);
        Check(wrong != initialized.seed, "two-warm-up negative control differs");
    }
    const auto initial = R::Initialize(17, 1800).state;
    for (unsigned channel = 0; channel < R::ChannelCount; ++channel) {
        R::State state = initial;
        bool sawHighBit = false;
        for (unsigned n = 0; n < 128; ++n) {
            std::uint32_t result = 0;
            Check(R::AdvanceChannel(state, V::tables, channel, result), "valid native channel advances");
            Check((result & 0x80000000u) == 0 && result == (state.channels[channel] & 0x7FFFFFFFu),
                  "only the returned value is masked to 31 bits");
            sawHighBit |= (state.channels[channel] & 0x80000000u) != 0;
            Check(state.normal == initial.normal && state.auxiliary == initial.auxiliary,
                  "indexed draws never advance either unrelated global");
        }
        Check(sawHighBit, "full indexed state retains sign-bit values between draws");
        for (unsigned other = 0; other < R::ChannelCount; ++other)
            if (other != channel) Check(state.channels[other] == initial.channels[other], "channels remain independent");
    }
    R::State invalid = initial; std::uint32_t untouched = 0xDEADBEEFu;
    Check(!R::AdvanceChannel(invalid, V::tables, 68, untouched) && Equal(invalid, initial) && untouched == 0xDEADBEEFu,
          "out-of-range channel fails without altering state or output");
    auto normal = initial.normal, auxiliary = initial.auxiliary;
    const auto normalResult = R::AdvanceNormal(normal);
    const auto auxiliaryResult = R::AdvanceAuxiliary(auxiliary, 65535);
    Check(normalResult == (normal & 0x7FFFFFFFu) && auxiliaryResult == (auxiliary & 0x7FFFFFFFu),
          "normal and auxiliary consumers retain full state and mask their return");
    std::set<std::uint32_t> hd, varied;
    for (unsigned d = 0; d < 256; ++d) hd.insert(R::Initialize(static_cast<std::uint8_t>(d), 0).seed);
    unsigned outside = 0;
    for (unsigned f = 0; f < 65536; ++f) {
        const auto seed = R::Initialize(0, f).seed; varied.insert(seed);
        if (hd.count(seed) == 0) ++outside;
    }
    Check(hd.size() == 256 && varied.size() == 65536 && outside == 65280,
          "variety is measured against the entire HD set, not numeric values above 255");
    std::printf("ORIGINAL_PS2_RNG_CORE_RT0 %u/%u passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
