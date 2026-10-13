#pragma once
#include "OmegaT7Core.h"
#include <cstdint>
namespace FfxHooks::OmegaT7::Seed {
struct State {
    std::uint64_t seed=0,epoch=0;
    unsigned duration=0,elapsed=0;
    bool due=false,loreSeen=false,renewalPending=false;
};
inline State Adopt(std::uint64_t campaign) noexcept {
    State s;s.seed=Mix(campaign^0x4F4D454741543731ULL);if(!s.seed)s.seed=1;
    s.epoch=1;s.duration=900+static_cast<unsigned>(Mix(s.seed)%901);return s;
}
inline bool Valid(const State& s) noexcept {
    return s.seed&&s.epoch&&s.duration>=900&&s.duration<=1800&&s.elapsed<=s.duration&&s.due==(s.elapsed==s.duration);
}
class Clock {
    unsigned last_=0;bool anchored_=false;
public:
    void Anchor(unsigned nativeSeconds) noexcept {last_=nativeSeconds;anchored_=true;}
    bool Tick(State& s,unsigned nativeSeconds,bool battle) noexcept {
        if(!Valid(s))return false;
        if(!anchored_){Anchor(nativeSeconds);return false;}
        const unsigned delta=nativeSeconds>=last_?nativeSeconds-last_:0;
        last_=nativeSeconds;
        const auto remaining=s.duration-s.elapsed;
        s.elapsed+=delta>remaining?remaining:delta;s.due=s.elapsed==s.duration;
        if(!s.due||battle||s.epoch==UINT64_MAX)return false;
        auto next=Mix(s.seed^Mix(s.epoch+1));
        if(!next||next==s.seed)next=s.seed^0x9E3779B97F4A7C15ULL;
        if(!next)next=1;
        s.seed=next;++s.epoch;s.duration=900+static_cast<unsigned>(Mix(next)%901);s.elapsed=0;s.due=false;s.renewalPending=true;
        return true;
    }
};
}
