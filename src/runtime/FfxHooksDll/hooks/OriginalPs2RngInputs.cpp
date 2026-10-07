#include "OriginalPs2RngInputs.h"
#include <limits>

namespace FfxHooks::OriginalPs2Rng {
namespace {
unsigned Days(unsigned year,unsigned month) noexcept {
    constexpr unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap=year%4==0&&(year%100!=0||year%400==0);
    return days[month-1]+(month==2&&leap?1u:0u);
}
std::uint8_t Bcd(unsigned value) noexcept {
    return static_cast<std::uint8_t>((value/10)*16+value%10);
}
}
bool EncodeHealthyJapanClock(const UtcCalendar& utc,ClockBytes& result) noexcept {
    if(utc.year<2000||utc.year>2099||utc.month<1||utc.month>12||
       utc.day<1||utc.day>Days(utc.year,utc.month)||utc.hour>23||utc.minute>59||utc.second>59)
        return false;
    auto date=utc;date.hour+=9;
    if(date.hour>=24){
        date.hour-=24;
        if(++date.day>Days(date.year,date.month)){
            date.day=1;
            if(++date.month>12){date.month=1;++date.year;}
        }
    }
    if(date.year>2099)return false;
    result={0,Bcd(date.second),Bcd(date.minute),Bcd(date.hour),0,
            Bcd(date.day),Bcd(date.month),Bcd(date.year-2000)};
    return true;
}
bool NtscTickIndex(std::uint64_t raw,std::uint64_t frequency,std::uint64_t& result) noexcept {
    constexpr std::uint64_t numerator=2997,denominator=50;
    if(!frequency||frequency>std::numeric_limits<std::uint64_t>::max()/(numerator*denominator))
        return false;
    // Divide before multiplying so x86 needs no nonportable 128-bit arithmetic.
    // The bound above makes the fractional product exact; the full index wraps
    // as an unsigned 64-bit event counter, matching the reference storage width.
    const auto seconds=raw/frequency, fraction=raw%frequency;
    const auto partial=((seconds%denominator)*frequency+fraction)*numerator;
    result=(seconds/denominator)*numerator+partial/(frequency*denominator);
    return true;
}
bool ResetCounterEpoch(std::uint64_t raw,std::uint64_t frequency,CounterEpoch& result) noexcept {
    std::uint64_t index=0;
    if(!NtscTickIndex(raw,frequency,index))return false;
    result={raw,index,frequency,true};return true;
}
bool CounterSinceReset(const CounterEpoch& epoch,std::uint64_t raw,std::uint64_t& result) noexcept {
    if(!epoch.valid||raw<epoch.rawAtReset)return false;
    std::uint64_t index=0;
    if(!NtscTickIndex(raw,epoch.frequency,index))return false;
    result=index-epoch.indexAtReset;return true;
}
} // namespace FfxHooks::OriginalPs2Rng
