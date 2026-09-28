#include "../hooks/ArcanaCombatCore.h"
#include <cstdio>
using namespace FfxHooks::Arcana;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
int main(){
    unsigned char command[96]{};command[24]=2;command[40]=7;
    auto f=CombatCore::Classify(command,sizeof(command),0x302B,0,1);
    Check(f.white&&!f.black&&!f.item,"white healing classification uses the native command category");
    command[24]=1;command[40]=3;command[45]=1;f=CombatCore::Classify(command,sizeof(command),0x3061,8,1);
    Check(f.black&&f.elements==1,"spell elements do not borrow the weapon element mask");
    command[24]=0;command[30]=4;command[40]=0;f=CombatCore::Classify(command,sizeof(command),0x3000,9,1);
    Check(f.ordinary&&f.elements==9&&!f.fixed,"ordinary attacks use the native weapon-properties flag");
    command[30]=0;command[40]=5;f=CombatCore::Classify(command,sizeof(command),0x3001,0,1);
    Check(f.fractional,"Gravity-like percent formulas are excluded from generic damage amplifiers");
    command[40]=23;f=CombatCore::Classify(command,sizeof(command),0x2000,0,1);
    Check(f.fixed&&f.item,"fixed item formulas retain native damage semantics");
    CombatCore::Ledger ledger;Check(!ledger.Survive(0)&&!ledger.Kill(0,1),"unbound battle events cannot grant resources");
    ledger.BeginBattle(1);
    Check(ledger.Survive(0)&&!ledger.Survive(0),"Judgement can intercept only one lethal hit per battle");
    Check(ledger.Kill(0,10)&&!ledger.Kill(0,10)&&ledger.Kill(0,11),"multihit and multitarget kills share one award per action");
    Check(ledger.ShareHealing(1,20,100,1000,25,10)==25,"Lovers scales actual restored HP");
    Check(ledger.ShareHealing(1,20,1000,1000,25,10)==75,"Lovers caps the sum across all targets in one action");
    Check(ledger.ShareHealing(1,20,9999,1000,25,10)==0,"repeated callbacks cannot exceed the action healing budget");
    Check(ledger.ShareHealing(1,21,100,1000,25,10)==25,"a different action starts a fresh bounded healing budget");
    ledger.BeginBattle(2);Check(ledger.Survive(0),"new battle generation resets one-battle effects");
    std::printf("ArcanaCombatCoreRt0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
