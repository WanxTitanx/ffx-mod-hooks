// Jarvis-HOOK: approved catalog behavior, independent of engine addresses/assets.
#include <cstdio>
#if __has_include("../hooks/OmegaT7Core.h")
#include "../hooks/OmegaT7Core.h"
namespace O=FfxHooks::OmegaT7;
static unsigned checks=0,failures=0;
static void Check(bool v,const char* why){++checks;if(!v){++failures;std::printf("FAIL %s\n",why);}}
int main(){
    const auto native=O::Assign(123,590,8,{218,218,49,49,83,0,0,0});
    Check(native[0]<48&&native[1]<48&&native[4]<48,"observed Omega encounter assigns marks to every combatant");
    Check(O::Catalog.size()==48,"exact approved 24 generic plus 24 species catalog");
    for(unsigned i=0;i<48;++i)Check(O::Catalog[i].id==i&&O::Catalog[i].key&&O::Catalog[i].name,"stable unique profile identity");
    for(unsigned model:{239u,242u,83u,240u,218u,219u,244u,90u,224u,188u,49u,910u,251u,252u,253u,256u}){
        unsigned generic=0,specific=0;
        for(unsigned ticket=0;ticket<100;++ticket){auto p=O::Choose(model,ticket,0,0);Check(p<48,"every normal species receives one supported profile");generic+=p<24;specific+=p>=24;}
        Check(generic==75&&specific==25,"single-slot ticket weights are exactly 75/25");
    }
    O::Roster roster{218,218,218,218,218,0,0,0};
    for(unsigned seed=1;seed<500;++seed){auto a=O::Assign(seed,590,17,roster),b=O::Assign(seed,590,17,roster);std::uint64_t seen=0;
        for(unsigned slot=0;slot<5;++slot){Check(a[slot]==b[slot]&&a[slot]<48,"same seed, composition and slot remain stable across encounters");Check(!(seen&(1ull<<a[slot])),"five identical species obtain five distinct marks");seen|=1ull<<a[slot];}
        Check(a[5]==O::None&&a[6]==O::None&&a[7]==O::None,"empty slots receive no mark");}
    roster={239,211,251,240,83,219,0,0};const auto planned=O::Assign(123,590,13,roster);
    Check(planned[1]==O::None&&planned[2]!=O::None,"chest has no mark; a reserved Mimic can have an inert prepared assignment");
    O::Vitals alive;alive.hp=alive.maximum=1000;
    for(unsigned profile=0;profile<48;++profile){O::State state;O::Event event;event.before=alive;event.after=alive;event.after.hp=0;
        Check(O::Observe(profile,state,event).Empty(),"no condition reacts from a dead owner");}
    auto cross=[&](unsigned profile,unsigned hp,unsigned expectedSelf,unsigned expectedGroup){O::State state;O::Event e;e.kind=O::EventKind::Received;e.before=alive;e.after=alive;e.after.hp=hp;e.landed=true;
        auto effect=O::Observe(profile,state,e);Check(effect.self==expectedSelf&&effect.group==expectedGroup,"crossing grants the approved self/group effects");
        e.before=e.after;e.after.buffs=0;Check(O::Observe(profile,state,e).Empty(),"remaining below threshold or Dispel cannot repeat a crossing");
        e.before=alive;e.after.hp=hp;Check(O::Observe(profile,state,e).Empty(),"healing and a second crossing cannot rearm a one-time reaction");};
    cross(1,500,0,O::Haste);cross(14,500,O::Regen,0);cross(19,250,O::Shell,O::Haste);cross(33,500,0,O::Haste);cross(43,500,O::Shell,O::Haste);
    O::State state;O::Event e;e.kind=O::EventKind::Hit;e.before=alive;e.after=alive;e.after.hp=800;e.loss=200;e.landed=true;
    Check(O::Observe(18,state,e).healPercent==15,"Fortress reacts to one actual hit of at least twenty percent");
    Check(O::Observe(18,state,e).Empty(),"Fortress recovery is once per incarnation");
    state={};e.after.hp=899;e.loss=101;Check(O::Observe(18,state,e).Empty(),"small hits never aggregate into Fortress's threshold");
    e.kind=O::EventKind::Received;e.loss=303;Check(O::Observe(18,state,e).Empty(),"whole-action damage cannot masquerade as one large hit");
    state={};e={};e.before=e.after=alive;e.kind=O::EventKind::Received;e.physical=e.evaded=true;
    Check(O::Observe(2,state,e).Empty()&&state.chargePercent==30,"physical evade charges the next Aeroja without a second skill");
    O::Observe(2,state,e);Check(O::TakeCharge(state)==30&&O::TakeCharge(state)==0,"charges do not stack and are consumed once");
    state={};e={};e.before=e.after=alive;e.kind=O::EventKind::Dealt;e.target=2;e.poisonApplied=true;e.targetAlive=true;
    Check(O::Observe(10,state,e).slowTarget==2&&O::Observe(10,state,e).Empty(),"successful Poison permits only one native Slow attempt per target");
    state={};e.poisonApplied=false;Check(O::Observe(10,state,e).Empty(),"blocked Poison cannot bypass resistance with conditional Slow");
    state={};e={};e.before=e.after=alive;e.kind=O::EventKind::Received;e.dispel=true;e.removedBuffs=O::Reflect;
    Check(O::Observe(17,state,e).self==O::Shell,"Dispel of Reflect gives Shell without restoring Reflect");
    state={};e.dispel=false;Check(O::Observe(17,state,e).Empty(),"natural expiry is not Dispel");
    state={};e={};e.before=e.after=alive;e.kind=O::EventKind::PartyHaste;
    Check(O::Observe(12,state,e).ctbPercent==20&&O::Observe(12,state,e).Empty(),"Haste observation advances CTB at most once per own turn");
    auto turn=e;turn.kind=O::EventKind::OwnTurn;O::Observe(12,state,turn);
    Check(O::Observe(12,state,e).ctbPercent==20,"next genuine own turn opens the next CTB window");
    state={};e={};e.before=e.after=alive;e.kind=O::EventKind::Received;e.landed=true;e.element=O::Fire;
    Check(O::Observe(28,state,e).absorb==O::Fire&&state.absorbTurns==2,"Prism records only the first basic element for two own turns");
    turn=e;turn.kind=O::EventKind::OwnTurn;Check(!O::Observe(28,state,turn).restoreAbsorb&&O::Observe(28,state,turn).restoreAbsorb,"temporary absorption expires at its two-turn boundary");
    e.element=O::Ice;Check(O::Observe(28,state,e).Empty(),"Prism never rearms after expiration");
    std::printf("OmegaT7CoreRt0: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
#else
int main(){std::puts("FAIL Omega T7 policy is not implemented");return 1;}
#endif
