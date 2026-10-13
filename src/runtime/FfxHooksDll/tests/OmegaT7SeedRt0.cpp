#include <cstdio>
#if __has_include("../hooks/OmegaT7Seed.h")
#include "../hooks/OmegaT7Seed.h"
namespace S=FfxHooks::OmegaT7::Seed;
static unsigned checks=0,failures=0;
static void Check(bool value,const char* text){++checks;if(!value){++failures;std::printf("FAIL %s\n",text);}}
int main(){
    for(unsigned base=1;base<10000;++base){const auto state=S::Adopt(base);
        Check(S::Valid(state)&&state.duration>=900&&state.duration<=1800,"every seed schedules fifteen to thirty minutes of game time");
        Check(S::Adopt(base).seed==state.seed,"legacy adoption is deterministic for a campaign identity");}
    auto state=S::Adopt(12345);S::Clock clock;clock.Anchor(100);const auto seed=state.seed;
    Check(!clock.Tick(state,200,false)&&state.elapsed==100&&state.seed==seed,"native game-time delta advances the existing deadline");
    clock.Anchor(150);Check(!clock.Tick(state,150,false)&&state.elapsed==100&&state.seed==seed,"reload rebases the clock without resetting seed or elapsed progress");
    auto saved=state;clock.Anchor(0);clock.Tick(state,50,false);Check(state.elapsed==saved.elapsed+50,"progress continues after loading an older native timer");
    const auto duration=state.duration;clock.Anchor(100);clock.Tick(state,100+duration,true);
    Check(state.due&&state.seed==seed,"deadline expiring in battle is deferred without changing that battle's mark");
    const auto frozen=state;clock.Tick(state,200+duration,true);Check(state.seed==frozen.seed&&state.epoch==frozen.epoch,"further battle events cannot reroll a due seed");
    Check(clock.Tick(state,200+duration,false)&&state.seed!=seed&&state.epoch==2&&!state.elapsed&&!state.due,"the next safe field boundary applies one renewal");
    const auto next=state;clock.Anchor(0);clock.Tick(state,0,false);Check(state.seed==next.seed&&state.duration==next.duration,"reload cannot choose another seed or interval");
    auto copy=state;clock.Tick(state,0,false);Check(state.seed==copy.seed&&state.elapsed==copy.elapsed,"same clock sample is idempotent");
    state.elapsed=state.duration-1;clock.Anchor(1000);Check(!clock.Tick(state,999,false)&&state.elapsed==state.duration-1,"clock rollback never spends or erases the remaining time");
    Check(clock.Tick(state,1000,false),"the next genuine second reaches the preserved deadline");
    state=S::Adopt(777);state.loreSeen=true;clock.Anchor(1);clock.Tick(state,1000000,false);
    Check(state.epoch==2&&state.loreSeen,"a long battle/absence applies one renewal and preserves campaign lore receipt");
    state=S::Adopt(1);state.epoch=UINT64_MAX;state.elapsed=state.duration;state.due=true;
    Check(!clock.Tick(state,1000001,false)&&state.epoch==UINT64_MAX,"epoch exhaustion cannot wrap into an old seed");
    std::printf("OmegaT7SeedRt0: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
#else
int main(){std::puts("FAIL Omega T7 seed policy is not implemented");return 1;}
#endif
