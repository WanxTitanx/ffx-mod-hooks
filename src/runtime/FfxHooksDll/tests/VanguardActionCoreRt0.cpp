// Jarvis-HOOK: action settlement is separate from prediction and native IO.
#include "../hooks/VanguardActionCore.h"
#include <cstdio>
#include <climits>
namespace V=FfxHooks::Vanguard;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
int main(){
    V::ActionLedger ledger;
    Check(ledger.Begin(0,1,0x1234,true,0x14),"real action identity starts independently of a displayed preview");
    ledger.Used(0,1,0x14);
    ledger.Lost(0,1,18,1000,700,true);ledger.Lost(0,1,19,50,0,true);
    ledger.Lost(0,1,18,700,700,true);ledger.Lost(0,1,18,700,1000,true);
    ledger.Lost(0,1,1,1000,0,true);ledger.Lost(0,1,20,1000,0,false);
    Check(ledger.Active(0,1,0x1234),"multiple hits accumulate without ending the owner's action");
    auto paid=ledger.Finish(0,1,0x1234,true,true,false,100,9999);
    Check(paid.accepted&&paid.heal==7&&paid.consume==0x14,"Vampirism uses two percent actual hostile HP loss, rounded once, and consumes used buffs after every hit");
    Check(!ledger.Finish(0,1,0x1234,true,true,false,100,9999).accepted,"duplicate completion cannot heal or consume twice");
    Check(ledger.Begin(0,2,0x1234,true,0x14),"second native action starts a new settlement");
    ledger.Used(0,2,0x14);ledger.Reapplied(0,0x10);ledger.Lost(0,2,18,5000,0,true);
    paid=ledger.Finish(0,2,0x1234,true,true,false,9900,9999);
    Check(paid.heal==99&&paid.consume==4,"new Auto-Crit instance survives old completion and restoration clamps to max HP");
    for(unsigned mode=0;mode<4;++mode){
        Check(ledger.Begin(0,3+mode,0x1234,true,0x14),"failure-policy fixture starts");
        ledger.Used(0,3+mode,0x14);ledger.Lost(0,3+mode,18,1000,0,true);
        paid=ledger.Finish(0,3+mode,mode==0?0x9999:0x1234,mode!=1,mode!=2,mode==3,100,1000);
        Check(paid.heal==0,"wrong identity, cancellation, death or Zombie never produce restoration");
        if(mode==0){Check(ledger.Active(0,3+mode,0x1234),"unrelated completion cannot retire another actor's action");ledger.Reset();}
    }
    Check(ledger.Begin(0,20,0x1234,true,0x14),"reset fixture starts");
    ledger.Used(0,20,0x14);ledger.Lost(0,20,18,INT_MAX,0,true);ledger.Reset();
    Check(!ledger.Finish(0,20,0x1234,true,true,false,0,UINT_MAX).accepted,"battle/load generation reset discards pending effects without applying them");
    Check(!ledger.Begin(31,21,0x1234,true,0)&&!ledger.Begin(0,0,0x1234,true,0)&&!ledger.Begin(0,1,0,true,0),"invalid action identities fail closed");
    Check(ledger.Begin(0,22,0x1234,false,0x14),"non-vampiric actor still has a finite buff lifetime");
    ledger.Lost(0,22,18,INT_MAX,0,true);ledger.Used(0,22,4);
    paid=ledger.Finish(0,22,0x1234,true,true,false,0,UINT_MAX);
    Check(!paid.heal&&paid.consume==4,"unowned equipment effect never activates through the ledger");
    Check(ledger.Begin(0,23,0x1234,true,0),"wide damage fixture starts");
    for(unsigned i=0;i<1000;++i)ledger.Lost(0,23,18,INT_MAX,0,true);
    paid=ledger.Finish(0,23,0x1234,true,true,false,0,INT_MAX);
    Check(paid.heal==INT_MAX,"large multi-target totals are accumulated and clamped without integer wrap");
    std::printf("VANGUARD_ACTION_CORE %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
