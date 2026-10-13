#include "../hooks/CaptureCascadeCore.h"
#include <cstdio>
#include <cstring>
using namespace FfxHooks::CaptureCascade;
namespace {
unsigned checks=0,failures=0;
void Check(bool value,const char* message){++checks;if(!value){++failures;std::fprintf(stderr,"FAIL: %s\n",message);}}
Stamp Time(){Stamp stamp;stamp.utc=1791586800;stamp.playSeconds=3600;std::strcpy(stamp.build.data(),"Jarvis-HOOK-test");return stamp;}
void Progression(){
    State s=Adopt(0);Check(Valid(s),"fresh state is valid");
    Check(ObserveVictories(s,1,Time())&&s.defeated==1,"ordinary defeat without Capture awakens the region");
    Check(s.captured==0,"victory does not fabricate capture");
    Check(Current(s,Aeon::Valefor).kind==Tutorial::Cascade,"first victory queues the Cascade tutorial");
    auto revision=s.revision;Check(ObserveVictories(s,1,Time())&&s.revision==revision,"repeat observations are idempotent");
    Check(!ObserveVictories(s,0,Time())&&s.defeated==1,"lost native terminal flags do not erase progress or create a new cycle");
    Check(ObserveCapture(s,Aeon::Valefor,Time())&&s.captured==1,"capture remains separate metadata");
    Check(ObserveCapture(s,Aeon::Valefor,Time()),"duplicate captures do not award again");
    Check(s.defeats[0].utc==Time().utc&&s.captures[0].playSeconds==3600,"records retain the actual date and playtime");
    const auto old=Adopt(255);Check(old.nativeSeen==255&&old.defeated==0&&old.captured==0,"legacy victories do not silently awaken regions");
    s=Adopt(0);Check(s.defeated==0&&s.sin.queued==0&&s.cascade.queued==0,"confirmed new cycle resets both tutorials and awakening");
    Check(FamilyForMonster(339)==static_cast<unsigned>(Aeon::Anima)&&FamilyForMonster(340)==static_cast<unsigned>(Aeon::Yojimbo),"terminal flag order differs from model order");
    for(unsigned model=341;model<=343;++model)Check(FamilyForMonster(model)==7,"all three Sisters belong to the terminal family");
    Check(FamilyForMonster(900)==kNoFamily,"patrol clone cannot masquerade as a first boss victory");
}
void Tutorials(){
    State dismissed=Adopt(0);ObserveRegion(dismissed,Aeon::Shiva,true);ObserveVictories(dismissed,1,Time());
    Check(Acknowledge(dismissed,Current(dismissed,Aeon::Shiva),false,true)&&dismissed.sin.complete&&!dismissed.cascade.complete&&Valid(dismissed),"Cancel dismisses only the current SIN tutorial");
    Check(Acknowledge(dismissed,Current(dismissed,Aeon::Valefor),false,true)&&dismissed.cascade.complete&&dismissed.defeated==1&&!dismissed.captured&&Valid(dismissed),"Cancel dismisses Cascade without changing awakening or capture progress");
    State s=Adopt(0);Check(ObserveRegion(s,Aeon::Shiva,true),"first Macalania visit is recorded without a Dark Aeon");
    auto first=Current(s,Aeon::Shiva);Check(first.kind==Tutorial::Sin&&first.page==0,"Macalania introduces SIN independently");
    Check(ObserveVictories(s,1,Time()),"a Dark Aeon can be defeated with SIN introduction still pending");
    auto current=Current(s,Aeon::Valefor);Check(current.kind==Tutorial::Sin,"earlier tutorial retains queue order");
    Check(!Acknowledge(s,first),"stale acknowledgement cannot skip a page after a state transition");
    for(unsigned page=0;page<3;++page){auto view=Current(s,Aeon::Valefor);Check(view.kind==Tutorial::Sin&&view.page==page,"SIN page sequence is complete");Check(Acknowledge(s,view),"native close acknowledgement advances one page");}
    Check(s.sin.complete&&!s.cascade.complete,"tutorial completion is independent");
    auto snapshot=s;Check(Current(snapshot,Aeon::Valefor).kind==Tutorial::Cascade,"reload retains queued Cascade tutorial");
    Check(Acknowledge(s,Current(s,Aeon::Valefor)),"Cascade first page acknowledged");
    Check(Acknowledge(s,Current(s,Aeon::Valefor),true)&&Current(s,Aeon::Valefor).page==0,"previous returns to the preceding page");
    for(unsigned page=0;page<4;++page)Check(Acknowledge(s,Current(s,Aeon::Valefor)),"all Cascade pages can be acknowledged");
    Check(s.cascade.complete&&Valid(s),"Cascade completes only after its final page");
    Check(ObserveRegion(s,Aeon::Valefor,false),"awakened region receives its own first-entry notice");
    auto notice=Current(s,Aeon::Valefor);Check(notice.kind==Tutorial::Region,"region message is distinct from the full tutorial");
    Check(Acknowledge(s,notice)&&Current(s,Aeon::Valefor).kind==Tutorial::None,"region notice does not loop");
    Check(ObserveRegion(s,Aeon::Shiva,true)&&Current(s,Aeon::Shiva).kind==Tutorial::None,"return to Macalania does not repeat SIN tutorial");
    Check(ObserveVictories(s,3,Time())&&Current(s,Aeon::Ifrit).kind==Tutorial::None,"second family does not replay the full tutorial");
}
void Patrols(){
    State s=Adopt(0);ObserveVictories(s,3,Time());
    const Candidate pool[]={{Aeon::Valefor,1,{0x1384,0,0}},{Aeon::Ifrit,1,{0x1385,0,0}}};
    Slot slots[5]={{0x1001,true},{0x1002,true},{0x1003,true},{0x1004,true},{0x1005,true}};
    Config c;Check(!Select(c,s,Aeon::Valefor,pool,2,slots,5,1).selected,"feature is OFF by default");
    c.enabled=true;c.chance=10000;c.secondChance=10000;
    auto p=Select(c,s,Aeon::Valefor,pool,2,slots,5,1);
    Check(p.selected&&p.count==5&&p.groups==2,"full five-actor formation supports two patrols without a sixth slot");
    unsigned changed=0,vale=0,ifrit=0;
    for(unsigned i=0;i<5;++i){if(p.monsters[i]!=slots[i].monster)++changed;else Check(!(p.changed&(1u<<i)),"unchanged actor is not marked replaced");vale+=p.monsters[i]==0x1384;ifrit+=p.monsters[i]==0x1385;}
    Check(changed==2&&vale==1&&ifrit==1,"different patrols replace distinct slots exactly once");
    Check(!Select(c,s,Aeon::Shiva,pool,2,slots,5,1).selected,"a sleeping region cannot spawn a patrol");
    State one=Adopt(0);ObserveVictories(one,1,Time());
    p=Select(c,one,Aeon::Valefor,pool,2,slots,5,2);Check(p.groups==1,"undefeated candidate is excluded even when allowed by region");
    p=Select(c,s,Aeon::Valefor,pool,1,slots,5,3);Check(p.groups==1,"regional list cannot leak another defeated family");
    State legacy=Adopt(1);ObserveVictories(legacy,3,Time());
    p=Select(c,legacy,Aeon::Ifrit,pool,2,slots,5,3);Check(p.groups==2,"old victories qualify as candidates after another region awakens");
    for(auto& slot:slots)slot.replaceable=false;
    Check(!Select(c,s,Aeon::Valefor,pool,2,slots,5,4).selected,"protected/scripted actors are never replaced");
    slots[2].replaceable=true;p=Select(c,s,Aeon::Valefor,pool,2,slots,5,5);
    Check(p.groups==1&&p.changed==4,"one eligible slot cannot be selected twice");
    const Candidate sisters[]={{Aeon::Sisters,3,{0x138B,0x138C,0x138D}}};
    ObserveVictories(s,131,Time());
    Check(!Select(c,s,Aeon::Sisters,sisters,1,slots,5,6).selected,"Sisters never materialize as a partial group");
    for(auto& slot:slots)slot.replaceable=true;
    p=Select(c,s,Aeon::Sisters,sisters,1,slots,5,6);Check(p.groups==1&&p.count==5,"Sisters use three existing actor slots");
    c.chance=0;Check(!Select(c,s,Aeon::Valefor,pool,2,slots,5,7).selected,"zero probability preserves original encounter");
    c.chance=10001;Check(!Select(c,s,Aeon::Valefor,pool,2,slots,5,7).selected,"invalid probability is rejected");
}
}
int main(){Progression();Tutorials();Patrols();std::printf("Capture Cascade RT0: %u/%u passed\n",checks-failures,checks);return failures?1:0;}
