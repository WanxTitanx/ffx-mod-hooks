#include "../hooks/OriginalPs2RngInputs.h"
#include <cstdio>
#include <limits>

namespace R = FfxHooks::OriginalPs2Rng;
static unsigned checks = 0, failures = 0;
static void Check(bool value, const char* message) {
    ++checks;
    if (!value && ++failures < 16) std::printf("FAIL %s\n", message);
}
int main() {
    R::ClockBytes bytes{};
    Check(R::EncodeHealthyJapanClock({2026,10,5,12,34,56},bytes) &&
          bytes == R::ClockBytes{0,0x56,0x34,0x21,0,0x05,0x10,0x26},
          "one UTC sample becomes the normalized eight-byte JST BCD clock");
    Check(R::EncodeHealthyJapanClock({2024,2,28,20,0,0},bytes) &&
          bytes == R::ClockBytes{0,0,0,5,0,0x29,2,0x24}, "leap-day rollover");
    Check(R::EncodeHealthyJapanClock({2023,2,28,20,0,0},bytes) &&
          bytes == R::ClockBytes{0,0,0,5,0,1,3,0x23}, "non-leap month rollover");
    Check(R::EncodeHealthyJapanClock({2026,12,31,20,0,0},bytes) &&
          bytes == R::ClockBytes{0,0,0,5,0,1,1,0x27}, "year rollover");
    for (const R::UtcCalendar invalid : {R::UtcCalendar{1999,1,1,0,0,0},
         {2100,1,1,0,0,0},{2023,2,29,0,0,0},{2026,0,1,0,0,0},
         {2026,13,1,0,0,0},{2026,1,0,0,0,0},{2026,4,31,0,0,0},
         {2026,1,1,24,0,0},{2026,1,1,0,60,0},{2026,1,1,0,0,60},
         {2099,12,31,15,0,0}}) {
        bytes.fill(0xA5); const auto before=bytes;
        Check(!R::EncodeHealthyJapanClock(invalid,bytes) && bytes==before,
              "invalid or unsupported clock leaves the output unchanged");
    }
    for (unsigned year=2000;year<=2099;++year) {
        for (unsigned month=1;month<=12;++month) {
            Check(R::EncodeHealthyJapanClock({year,month,1,0,0,0},bytes) &&
                  bytes[0]==0 && bytes[4]==0 && bytes[3]==9 &&
                  bytes[7]==((year-2000)/10*16+(year-2000)%10),
                  "healthy status/padding and two-digit BCD year across the supported domain");
        }
    }
    std::uint64_t tick=99;
    Check(!R::NtscTickIndex(100,0,tick) && tick==99, "zero-frequency rejection");
    Check(!R::NtscTickIndex(100,std::numeric_limits<std::uint64_t>::max(),tick) && tick==99,
          "unrepresentable frequency rejects without overflow");
    Check(R::NtscTickIndex(10000000,10000000,tick) && tick==59, "one second is not assumed to be sixty ticks");
    Check(R::NtscTickIndex(500000000,10000000,tick) && tick==2997, "exact nominal NTSC interval");
    Check(R::NtscTickIndex(16683350,1000000000,tick) && tick==0, "before first nominal edge");
    Check(R::NtscTickIndex(16683351,1000000000,tick) && tick==1, "after first nominal edge");
    R::CounterEpoch epoch{};std::uint64_t count=999;
    Check(!R::CounterSinceReset(epoch,100,count) && count==999, "unobserved reset cannot invent an epoch");
    Check(R::ResetCounterEpoch(8341675,1000000000,epoch), "reset samples a free-running oscillator");
    Check(R::CounterSinceReset(epoch,16683351,count) && count==1,
          "counter reset does not restart oscillator phase");
    Check(R::CounterSinceReset(epoch,16683351+50000000000ULL,count) && count==2998,
          "unpolled frames are counted without a rendering callback or worker timer");
    Check(!R::CounterSinceReset(epoch,8341674,count) && count==2998,
          "backward monotonic samples reject without changing output");
    const auto saved=epoch;
    Check(!R::ResetCounterEpoch(7,0,epoch) && epoch.frequency==saved.frequency &&
          epoch.rawAtReset==saved.rawAtReset && epoch.indexAtReset==saved.indexAtReset,
          "failed reset preparation retains the old epoch");
    Check(R::ResetCounterEpoch(16683351,1000000000,epoch) &&
          R::CounterSinceReset(epoch,16683351,count) && count==0,
          "a native reset zeros the counter without reseeding any channel");
    std::printf("ORIGINAL_PS2_RNG_INPUTS_RT0 %u/%u passed\n",checks-failures,checks);
    return failures?1:0;
}
