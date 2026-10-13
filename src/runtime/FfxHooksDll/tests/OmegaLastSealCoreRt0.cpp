#include <cstdio>
#if __has_include("../hooks/OmegaLastSealCore.h")
#include "../hooks/OmegaLastSealCore.h"
namespace O=FfxHooks::OmegaLastSeal;
static unsigned checks=0,failures=0;
static void Check(bool value,const char* message){++checks;if(!value){++failures;std::printf("FAIL %s\n",message);}}
static O::Life Life(unsigned hp=3000000,unsigned helpers=3){O::Life v;v.hp=hp;v.maximum=3000000;v.helpers=helpers;v.party=7;return v;}
static O::State Begin(){O::State s;Check(O::Begin(s,{true,false,1077,10,100,1},Life()),"exact story encounter begins");return s;}
static O::Action Player(std::uint64_t token,O::Category type=O::Category::Physical,unsigned hpDamage=0){O::Action a;a.token=token;a.actor=0;a.party=true;a.normal=true;a.completed=true;a.category=type;a.bodyDamage=hpDamage;return a;}
static O::Action Enemy(std::uint64_t token,unsigned role,const O::Plan& plan){O::Action a;a.token=token;a.actor=20+role;a.role=role;a.normal=true;a.completed=true;a.plan=plan;return a;}
int main(){
    for(auto bad: {O::Admission{false,false,1077,10,100,1},O::Admission{true,false,1076,10,99,1},O::Admission{true,false,1077,0,910,1}}){O::State s;Check(!O::Begin(s,bad,Life()),"wrong resource/formation cannot arm the boss");}
    {O::State debug;Check(O::Begin(debug,{true,true,1077,10,100,1},Life()),"forced exact encounter retains combat mechanics; reward provenance is separate");}
    auto s=Begin();auto life=Life();
    Check(O::Limit(s,life,0,O::Category::Physical,1000)==0&&O::Limit(s,life,0,O::Category::Magic,1000)==0,"both seals protect only the body");
    Check(O::Limit(s,life,1,O::Category::Physical,1000)==1000,"helpers remain vulnerable");
    life.helpers=2;Check(O::Limit(s,life,0,O::Category::Physical,1000)==1000&&O::Limit(s,life,0,O::Category::Magic,1000)==0,"killing Light opens physical while Dark still blocks magic");
    Check(O::Limit(s,life,0,O::Category::Other,1000)==0,"unclassified fixed damage cannot bypass the surviving seal");
    life.helpers=0;Check(O::Limit(s,life,0,O::Category::Physical,4000000)==2250000&&!s.finalPending,"pre-cap arithmetic never arms a phase from damage the native cap may discard");
    life.hp=750000;O::Observe(s,life,10);Check(s.finalPending,"actual HP reaching the floor arms final entry");
    for(unsigned hit=0;hit<12;++hit)Check(O::Limit(s,life,0,O::Category::Physical,999999)==0,"floor persists through remaining hits and intervening player actions");
    auto entry=O::Choose(s,life,0,0);Check(entry.kind==O::Kind::FinalEntry&&entry.count==1&&entry.commands[0]==O::LastSeal,"next body action is defensive final entry");
    auto fx=O::Complete(s,life,Enemy(11,0,entry));Check(s.finalEntered&&!s.finalPending&&s.gauge==50&&fx.revive==15,"completed entry starts final phase at fifty and activates all four helpers");
    Check(O::Limit(s,life,0,O::Category::Physical,999999)==999999,"the one-time floor is released only after final entry completes");
    s=Begin();life=Life(2000000,0);O::Observe(s,life,100);Check(s.exposure==4&&s.gauge==0,"last-helper death opens four future party actions");
    O::Complete(s,life,Player(100));Check(s.exposure==4,"the action opening exposure does not consume its first unit");
    auto skip=O::Choose(s,life,0,0);Check(skip.kind==O::Kind::Concentrate,"first exposed body turn loses its offence");O::Complete(s,life,Enemy(101,0,skip));
    for(unsigned token=102;token<106;++token){auto free=Player(token);free.normal=false;O::Complete(s,life,free);Check(s.exposure==106-token,"free swaps and reactions do not consume exposure");O::Complete(s,life,Player(token));}
    auto rebuild=O::Choose(s,life,0,0);Check(rebuild.kind==O::Kind::Rebuild&&rebuild.count==1&&rebuild.revive==3,"expired window schedules only native guard reconstruction");fx=O::Complete(s,life,Enemy(107,0,rebuild));Check(fx.revive==3&&!s.exposed&&s.gauge==0,"reconstruction ends the window without healing the body");
    s=Begin();life=Life(2100000,3);O::Observe(s,life,0);auto normal=O::Choose(s,life,0,0);Check(normal.kind==O::Kind::Normal&&normal.count==2&&normal.distinct,"phase two can choose two physical attacks to distinct targets");
    auto two=Enemy(1,0,normal);O::Complete(s,life,two);O::Complete(s,life,two);Check(s.gauge==20,"two commands and duplicate completion callbacks charge twenty total");
    auto reaction=two;reaction.token=2;reaction.normal=false;O::Complete(s,life,reaction);Check(s.gauge==20,"reactions never charge the gauge");
    for(unsigned token=3;token<7;++token){O::Complete(s,life,Enemy(token,0,normal));}
    Check(s.preparing&&s.response==3,"one hundred announces three completed party actions");
    for(unsigned token=7;token<10;++token){auto aeon=Player(token);aeon.actor=8;O::Complete(s,life,aeon);}auto nova=O::Choose(s,life,0,0);
    Check(nova.kind==O::Kind::Nova&&nova.count==2,"two living helpers produce two pulses after the response clock");O::Complete(s,life,Enemy(10,0,nova));Check(!s.preparing&&s.gauge==0,"Nova pulses reset rather than recharge the gauge");
    s=Begin();s.gauge=100;s.preparing=true;s.response=1;life=Life(2000000,0);O::Complete(s,life,Player(30));
    Check(!s.preparing&&s.exposure==4&&O::Choose(s,life,0,0).kind!=O::Kind::Nova,"last-helper death on the last response action cancels before any Nova plan");
    s=Begin();life=Life(2000000,1);O::Observe(s,life,1);auto guard=O::Choose(s,life,1,0);
    O::Complete(s,life,Enemy(2,1,guard));fx=O::Complete(s,life,Enemy(3,1,guard));Check(s.rebuild[0]&&fx.notice==O::Notice::RebuildWarning,"a lone guard announces reconstruction after two own normal actions");
    life.silenced=1;O::Observe(s,life,4);Check(!s.rebuild[0]&&O::Choose(s,life,1,0).commands[0]==O::HolyAttack,"Silence cancels the attempt and preserves a physical fallback");
    s=Begin();life=Life(1400000,0);O::Observe(s,life,1);
    for(unsigned token=2;token<4;++token)Check(O::Complete(s,life,Player(token,O::Category::Physical,100)).counter==31,"first two body-damaging actions do not counter");
    fx=O::Complete(s,life,Player(4,O::Category::Physical,9000));Check(fx.counter==0,"third same-category action produces one Repreension on the final attacker");
    Check(O::Complete(s,life,Player(4,O::Category::Physical,9000)).counter==31,"multi-hit or repeated completion cannot chain counters");
    O::Complete(s,life,Player(5,O::Category::Physical,1));O::Complete(s,life,Player(6,O::Category::Magic,1));Check(O::Complete(s,life,Player(7,O::Category::Physical,1)).counter==31,"alternating damage categories resets hostile memory");
    s.finalEntered=true;s.phase=3;s.exposed=false;s.helpers=15;life=Life(700000,15);s.gauge=80;O::Complete(s,life,Enemy(8,0,normal));Check(s.response==2,"final preparation allows two party actions");s.response=0;Check(O::Choose(s,life,0,0).count==3,"four helpers still produce at most three Nova commands");
    life.helpers=1;s.preparing=false;s.rebuild[0]=true;Check(O::Choose(s,life,1,0).kind!=O::Kind::Rebuild,"the final phase never reconstructs helpers");
    life.hp=0;fx=O::Observe(s,life,20);Check(!s.active&&fx.cleanup==15,"body death clears all pending controller state and requests helper cleanup");
    Check(O::Complete(s,life,Player(21,O::Category::Magic,1)).counter==31,"retired battle callbacks cannot produce another reaction");
    {auto state=Begin();auto command=O::Single(O::Kind::Normal,O::Attack);
        O::Complete(state,Life(),Enemy(100,0,command));O::Complete(state,Life(),Enemy(99,0,command));
        Check(state.gauge==20,"stale earlier action token cannot charge again");
        O::Complete(state,Life(3000000,2),Enemy(101,1,command));
        Check(state.gauge==20,"a dead helper's retired packet cannot charge the gauge");
    }
    {auto state=Begin();state.helpers=0;state.phase=2;auto opened=Life(1400000,0);
        O::Complete(state,opened,Player(201,O::Category::Physical,100));
        O::Complete(state,opened,Player(202,O::Category::Physical,100));
        O::Complete(state,opened,Player(203,O::Category::Other,100));
        Check(O::Complete(state,opened,Player(204,O::Category::Physical,100)).counter==31,"untyped damage interrupts the repeated physical/magic sequence");
    }
    {auto state=Begin();auto opened=Life(1000000,0);
        Check(O::ZanmatoImpact(state,opened,0,O::Category::Physical,2000000)==99999,"Zanmato body impact is capped at 99999");
        Check(O::ZanmatoImpact(state,Life(800000,0),0,O::Category::Physical,2000000)==50000,"Zanmato preserves final phase floor");
        Check(O::ZanmatoImpact(state,Life(750000,0),0,O::Category::Physical,2000000)==0,"Zanmato cannot cross a pending final entry");
        Check(O::ZanmatoImpact(state,Life(),0,O::Category::Physical,2000000)==0,"Zanmato cannot pass a closed physical seal");
        Check(O::ZanmatoImpact(state,Life(1000000,2),0,O::Category::Other,2000000)==0,"untyped Zanmato has no seal bypass");
        Check(O::ZanmatoImpact(state,Life(),1,O::Category::Other,150000)==150000,"Zanmato breaks a guard");
        Check(O::ZanmatoImpact(state,Life(),4,O::Category::Other,75000)==75000,"Zanmato breaks an echo");
        state.finalEntered=true;
        Check(O::ZanmatoImpact(state,opened,0,O::Category::Physical,2000000)==99999,"final phase retains Zanmato limit");
        state.active=false;
        Check(O::ZanmatoImpact(state,opened,0,O::Category::Other,2000000)==2000000,"outside encounter no Zanmato override");
    }
    std::printf("OmegaLastSealCoreRt0: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
#else
int main(){std::puts("FAIL approved Omega Last Seal controller is not implemented");return 1;}
#endif
