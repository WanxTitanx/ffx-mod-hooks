#include <cstdio>
#if !__has_include("../hooks/MinigameAssistCore.h")
int main(){std::puts("FAIL: minigame assistance is not implemented");return 1;}
#else
#include "../hooks/MinigameAssistCore.h"
using namespace FfxHooks::MinigameAssist;
namespace {unsigned checks=0,failures=0;void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}}
int main(){
    Check(Rules().size()>30,"all four minigame paths have explicit bounded rules");
    for(const auto& rule:Rules()){
        const auto encoded=0x2E000000u|rule.original;
        Check(!FindRule(rule.script,rule.offset,encoded,0),"OFF never changes an instruction");
        const auto* found=FindRule(rule.script,rule.offset,encoded,3);
        Check(found&&found->replacement==rule.replacement,"each approved instruction maps to its exact replacement");
        Check(!FindRule(rule.script,rule.offset,encoded^0x10000u,3)&&!FindRule(rule.script,rule.offset+1,encoded,3),"opcode/operand mismatch or shifted code cannot receive assistance");
        Check(rule.replacement>0&&rule.replacement<32768,"all overrides retain the native signed-immediate instruction width");
    }
    auto value=[](unsigned script,unsigned offset,unsigned from){const auto* r=FindRule(script,offset,0x2E000000u|from,3);return r?r->replacement:from;};
    Check(value(0,0x3a5f,900)==1800&&value(1,0xa5c,1200)==2400,"butterfly play windows become 60 and 80 seconds at 30 ticks per second");
    Check(value(2,0x1ef35,12)*10+value(2,0x1ef3b,8)==192,"Wobbly target becomes 19.2 seconds");
    Check(value(2,0x1f208,16)*10+value(2,0x1f20e,8)==252,"Dodger target becomes 25.2 seconds");
    Check(value(2,0x1f4ce,18)*10+value(2,0x1f4d4,5)==278,"Hyper Dodger target becomes 27.8 seconds");
    Check(value(2,0x23ebb,3)==5&&value(2,0x23ec3,3)==1,"Catcher awards five seconds per player balloon and penalizes one per bird");
    Check(!FindRule(2,0x23ed9,0x2E000003,3),"the opponent's balloon calculation remains native");
    Check(!FindRule(3,0x64,0x2E000050,3),"player movement setup is not a rival-speed rule");
    std::printf("MinigameAssistRt0: %u/%u checks passed\n",checks-failures,checks);return failures?1:0;
}
#endif
