// Jarvis-HOOK: deterministic game-rule tests. No game is launched or modified.
#include "../hooks/VanguardRules.h"
#include <array>
#include <cstdio>
#include <limits>
namespace V=FfxHooks::Vanguard;
static unsigned checks=0,failures=0;
static void Check(bool value,const char* message){++checks;if(!value){++failures;std::printf("FAIL %s\n",message);}}
int main(){
    Check(V::Scale(1000,100,120)==833,"EHP +20 percent divides by 1.2");
    Check(V::Scale(-1000,120,100)==-1200,"restoration keeps its sign");
    Check(V::Scale(INT32_MAX,165,100)==INT32_MAX,"scaling cannot wrap positive damage");
    Check(V::BreakDamage(250,1000,true)==500&&V::BreakDamage(500,1000,true)==750&&V::BreakDamage(1000,1000,true)==1250,"Break adds one quarter of unmitigated damage");
    Check(V::MagicFromMp(40,241,false,false)==50&&V::MagicFromMp(255,9999,false,false)==325,"MP scaling has no accidental example or BYTE cap");
    Check(V::MagicFromMp(40,200,true,true)==43&&V::MagicFromMp(40,200,true,false)==50,"optional MP-zero cap is explicit");
    Check(V::MpCost(100,true,false,false,true)==25&&V::MpCost(100,false,false,false,true)==75,"Half MP and Efficiency reductions add");
    Check(V::MpCost(100,true,true,false,true)==0&&V::MpCost(100,true,false,true,true)==1,"MP-zero and One-MP retain priority");
    Check(V::EnergyAttack(1000,true,100,200,true,false)==1000&&V::EnergyAttack(1000,true,101,200,true,false)==1200,"OD thresholds are strict and use effective maximum");
    Check(V::EnergyAttack(1000,true,151,200,true,true)==1650&&V::EnergyAttack(1000,false,151,200,true,true)==1400,"explicit five-percent synergy requires both bonuses");
    Check(V::EnergyDefense(1000,151,200,true,true,false)==500&&V::EnergyDefense(-1000,200,200,true,true,false)==-1000,"Wall and Barrier stack but never mitigate healing");
    Check(V::EnergyDefense(1000,200,200,true,true,true)==1000,"fixed fractions bypass energy defense");
    Check(V::Trade(1000,1,true,false)==800&&V::Trade(1000,2,true,false)==1200&&V::Trade(1000,1,true,true)==1000,"opposite Trades cancel rather than rewarding double equipment");
    Check(V::CriticalChance(30,true,true,false,false)==80&&V::CriticalChance(30,false,false,true,true)==0,"critical chances use bounded percentage points");
    Check(V::Duration(4,1,0,true)==1&&V::Duration(255,1,75,true)==255&&V::Duration(0,4,25,false)==3,"refresh and duration resistance preserve permanent statuses");
    Check(V::OppositeMask(1)==2&&V::OppositeMask(4)==8&&V::OppositeMask(0x10)==0,"opposite pairs only cover the four specified elements");
    std::array<unsigned char,20+148*108> table{};
    auto word=[&](unsigned at,unsigned value){table[at]=static_cast<unsigned char>(value);table[at+1]=static_cast<unsigned char>(value>>8);};
    word(0,1);word(10,147);word(12,108);word(14,148*108);word(16,20);
    auto mapping=V::DefaultMapping();auto result=V::ValidateMapping(table.data(),table.size(),mapping);
    Check(result.AllValid(),"bounded neutral default rows admit their bindings");
    word(0,0);Check(!V::ValidateMapping(table.data(),table.size(),mapping).AllValid(),"native table section count cannot be missing");word(0,1);
    word(16,24);Check(!V::ValidateMapping(table.data(),table.size(),mapping).AllValid(),"native row base must agree with the validated data layout");word(16,20);
    word(18,1);Check(!V::ValidateMapping(table.data(),table.size(),mapping).AllValid(),"the full native row-offset DWORD is validated");word(18,0);
    mapping[0]=136;result=V::ValidateMapping(table.data(),table.size(),mapping);
    Check(!result.valid[0]&&!result.valid[1]&&result.valid[2],"a duplicate invalidates both bindings, not unrelated rows");
    mapping=V::DefaultMapping();mapping[0]=134;result=V::ValidateMapping(table.data(),table.size(),mapping);
    Check(!result.valid[0]&&result.valid[1],"reserved 134 cannot be assigned");
    table[20+139*108+32]=1;result=V::ValidateMapping(table.data(),table.size(),V::DefaultMapping());
    Check(!result.valid[4]&&result.valid[0],"native-effect payload rejects a hook-only binding");
    table[20+139*108+32]=0;
    Check(!V::ValidateMapping(table.data(),table.size()-1,V::DefaultMapping()).AllValid(),"truncated table rejects previous mapping evidence");
    word(12,107);Check(!V::ValidateMapping(table.data(),table.size(),V::DefaultMapping()).AllValid(),"wrong stride cannot validate");
    std::array<unsigned char,20+201*108> expanded{};
    const auto expandedWord=[&](unsigned at,unsigned value){expanded[at]=static_cast<unsigned char>(value);expanded[at+1]=static_cast<unsigned char>(value>>8);};
    expandedWord(0,1);expandedWord(10,200);expandedWord(12,108);expandedWord(14,201*108);expandedWord(16,20);
    for(unsigned id=148;id<=174;++id){
        auto reserved=V::DefaultMapping();reserved[0]=id;
        const auto checked=V::ValidateMapping(expanded.data(),expanded.size(),reserved);
        Check(!checked.valid[0]&&checked.valid[1],"Vanguard cannot occupy the reserved Aeon/Spira default range even when its row is neutral");
    }
    auto remapped=V::DefaultMapping();remapped[0]=175;
    Check(V::ValidateMapping(expanded.data(),expanded.size(),remapped).AllValid(),"an unreserved extended row remains available for explicit remapping");
    std::printf("VANGUARD_RULES %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
