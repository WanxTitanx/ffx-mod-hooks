#pragma once
#include "OriginalPs2RngCore.h"

namespace FfxHooks::OriginalPs2Rng {
struct UtcCalendar {
    unsigned year=0, month=0, day=0, hour=0, minute=0, second=0;
};
struct CounterEpoch {
    std::uint64_t rawAtReset=0, indexAtReset=0, frequency=0;
    bool valid=false;
};

// Healthy sceCdReadClock contract for the SLPS_250.88 reference: JST BCD,
// zero status/reserved byte, normalized month, years 2000..2099.
bool EncodeHealthyJapanClock(const UtcCalendar& utc, ClockBytes& result) noexcept;

// Nominal interlaced NTSC VBlank cadence: 2997/50 Hz. This is a continuous
// virtual oscillator, independent of game/present FPS and speed multipliers.
// Reset subtracts its current index; it must not restart oscillator phase.
bool NtscTickIndex(std::uint64_t raw, std::uint64_t frequency, std::uint64_t& result) noexcept;
bool ResetCounterEpoch(std::uint64_t raw, std::uint64_t frequency, CounterEpoch& result) noexcept;
bool CounterSinceReset(const CounterEpoch& epoch, std::uint64_t raw, std::uint64_t& result) noexcept;
} // namespace FfxHooks::OriginalPs2Rng
