// Jarvis-HOOK: independent examples from the approved forty-eight-rule table.
#include "../hooks/OmegaT7Core.h"
#include <cstdio>
namespace O=FfxHooks::OmegaT7;
static unsigned checks=0,failures=0;
static void Check(bool value,const char* why,unsigned profile){++checks;if(!value){++failures;std::printf("FAIL %u %s\n",profile,why);}}
static bool Equal(const O::Effect& a,const O::Effect& b){return a.self==b.self&&a.group==b.group&&a.stacks==b.stacks&&a.healPercent==b.healPercent&&a.ctbPercent==b.ctbPercent&&a.absorb==b.absorb&&a.slowTarget==b.slowTarget&&a.restoreAbsorb==b.restoreAbsorb;}
static O::Event Base(O::EventKind kind=O::EventKind::Received){O::Event e;e.kind=kind;e.before.hp=e.before.maximum=e.after.hp=e.after.maximum=1000;return e;}
static O::Event Crossing(unsigned hp){auto e=Base();e.after.hp=hp;return e;}
static O::Event Element(unsigned element,bool absorption=false){auto e=Base();e.landed=true;e.element=element;e.absorbed=absorption;e.loss=absorption?0:10;return e;}
static O::Event Critical(bool physical=true){auto e=Base();e.landed=e.critical=true;e.physical=physical;return e;}
static O::Effect Buff(unsigned self,unsigned group=0){O::Effect r;r.self=self;r.group=group;return r;}
static O::Effect Stack(unsigned index,unsigned count){O::Effect r;r.stacks[index]=count;return r;}
static std::uint64_t coverage=0;
static void Example(unsigned profile,O::Event event,O::Effect expected,unsigned repeats=1,unsigned charge=0){
    coverage|=1ull<<profile;O::State state;O::Effect actual;
    for(unsigned n=0;n<repeats;++n){actual=O::Observe(profile,state,event);if(n+1<repeats)Check(actual.Empty()&&!state.chargePercent,"counter does not trigger early",profile);}
    Check(Equal(actual,expected)&&state.chargePercent==charge,"approved condition and exact effect",profile);
    for(unsigned guard=0;guard<3;++guard){auto blocked=event;O::State clean;
        if(guard==0)blocked.after.hp=0;else if(guard==1)blocked.after.active=false;else blocked.after.maximum=0;
        for(unsigned n=0;n<repeats+1;++n)Check(O::Observe(profile,clean,blocked).Empty()&&!clean.consumed&&!clean.counter&&!clean.chargePercent,"dead, hidden or invalid owner cannot react or consume condition",profile);
    }
}
int main(){
    auto effect=Buff(O::Shell);effect.stacks[O::Focus]=1;Example(0,Crossing(500),effect);
    Example(1,Crossing(500),Buff(0,O::Haste));
    auto evade=Base();evade.physical=evade.evaded=true;Example(2,evade,{},1,30);
    Example(3,Element(O::Ice),Buff(O::NulFrost));Example(4,Element(O::Fire),Buff(O::NulBlaze));
    Example(5,Element(O::Water),Buff(O::NulTide));Example(6,Element(O::Thunder),Buff(O::NulShock));
    auto mental=Base();mental.mentalBreakAdded=true;Example(7,mental,Stack(O::Focus,1));
    auto death=Base(O::EventKind::AllyDeath);Example(8,death,Buff(0,O::Haste));
    Example(9,Base(O::EventKind::ExtraFinished),Stack(O::Focus,1),3);
    auto poison=Base(O::EventKind::Dealt);poison.poisonApplied=poison.targetAlive=true;poison.target=6;
    effect={};effect.slowTarget=6;Example(10,poison,effect);
    auto magic=Base();magic.magic=magic.landed=true;magic.loss=1;Example(11,magic,Buff(O::Reflect),3);
    effect={};effect.ctbPercent=20;Example(12,Base(O::EventKind::PartyHaste),effect);
    auto blinded=Base(O::EventKind::Dealt);blinded.landed=blinded.targetBlind=true;Example(13,blinded,Stack(O::Cheer,1));
    Example(14,Crossing(500),Buff(O::Regen));mental.kind=O::EventKind::Dealt;Example(15,mental,Stack(O::Focus,1));
    Example(16,Critical(),Buff(O::Protect));auto dispel=Base();dispel.dispel=true;dispel.removedBuffs=O::Reflect;Example(17,dispel,Buff(O::Shell));
    auto hit=Base(O::EventKind::Hit);hit.loss=200;hit.after.hp=800;effect={};effect.healPercent=15;Example(18,hit,effect);
    Example(19,Crossing(250),Buff(O::Shell,O::Haste));effect=Stack(O::Cheer,1);effect.stacks[O::Focus]=1;Example(20,death,effect);
    auto cap=Base(O::EventKind::VitalsChanged);cap.before.stacks[O::Focus]=4;cap.after.stacks[O::Focus]=5;Example(21,cap,Buff(O::Shell));
    cap=Base(O::EventKind::VitalsChanged);cap.before.stacks[O::Cheer]=4;cap.after.stacks[O::Cheer]=5;Example(22,cap,Buff(O::Protect));
    Example(23,Critical(),Buff(0,O::Haste));effect=Buff(0,O::Haste);effect.stacks[O::Reflex]=1;Example(24,Crossing(500),effect);
    Example(25,evade,Stack(O::Reflex,1),2);Example(26,Element(O::Holy),Buff(O::Shell));Example(27,evade,Stack(O::Focus,2));
    effect={};effect.absorb=O::Water;Example(28,Element(O::Water),effect);
    effect=Buff(O::Shell);effect.stacks[O::Focus]=1;Example(29,Element(O::Dark,true),effect);
    effect={};effect.healPercent=10;Example(30,Critical(),effect);
    auto armor=Base();armor.armorBreakAdded=true;Example(31,armor,Buff(0,O::Haste));
    Example(32,Element(O::Fire,true),{},3,50);Example(33,Crossing(500),Buff(0,O::Haste));
    Example(34,Element(O::Thunder,true),Buff(O::Regen));Example(35,Element(O::Dark,true),Stack(O::Focus,1));
    auto broken=Base(O::EventKind::Dealt);broken.landed=broken.targetArmorBreak=true;Example(36,broken,Stack(O::Cheer,1));
    Example(37,death,Buff(0,O::Haste));auto negative=Base(O::EventKind::Dealt);negative.nativeAi=negative.negativeApplied=true;Example(38,negative,Stack(O::Focus,2));
    Example(39,magic,Buff(0,O::Haste),2);effect=Stack(O::Cheer,1);effect.stacks[O::Focus]=1;Example(40,Base(O::EventKind::Walk),effect);
    Example(41,Base(O::EventKind::FinalApproach),Buff(0,O::Haste));dispel.removedBuffs=O::Protect;Example(42,dispel,Stack(O::Focus,2));
    Example(43,Crossing(500),Buff(O::Shell,O::Haste));effect={};effect.healPercent=20;Example(44,armor,effect);
    Example(45,Base(O::EventKind::CoreEnergy),Stack(O::Focus,1));Example(46,Base(O::EventKind::StealAttempt),Buff(0,O::Haste));Example(47,Crossing(250),effect);
    Check(coverage==((1ull<<48)-1),"all forty-eight conditions have a positive and an inactive negative case",48);
    for(unsigned profile:{3u,4u,5u,6u,26u}){auto event=Element(O::Fire|O::Ice|O::Thunder|O::Water|O::Holy);event.loss=0;O::State s;
        Check(O::Observe(profile,s,event).Empty()&&!s.consumed,"Nul/immune zero damage cannot grant an elemental ward",profile);}
    for(unsigned profile:{11u,39u}){auto e=magic;e.loss=0;O::State s;for(unsigned i=0;i<5;++i)Check(O::Observe(profile,s,e).Empty()&&!s.counter,"non-damaging casts never advance magic counter",profile);}
    for(unsigned profile:{16u,23u,30u}){auto e=Critical();e.landed=false;O::State s;Check(O::Observe(profile,s,e).Empty(),"evaded critical never triggers",profile);}
    {O::State s;Check(O::Observe(30,s,Critical(false)).Empty(),"Resonant Shell only accepts a physical critical",30);}
    {O::State s;negative.nativeAi=false;Check(O::Observe(38,s,negative).Empty(),"an extra or counter cannot claim native-AI negative condition",38);}
    {O::State s;auto e=Base();e.after.stacks[O::Focus]=e.before.stacks[O::Focus]=5;Check(O::Observe(21,s,e).Empty(),"already capped Focus cannot mimic a crossing",21);}
    for(unsigned profile:{13u,15u,35u,36u}){O::Event e=profile==13?blinded:profile==15?mental:profile==35?Element(O::Dark,true):broken;O::State s;
        Check(!O::Observe(profile,s,e).Empty()&&O::Observe(profile,s,e).Empty(),"once-per-own-turn latch",profile);
        O::Observe(profile,s,Base(O::EventKind::OwnTurn));Check(!O::Observe(profile,s,e).Empty(),"next own turn opens exactly one new opportunity",profile);}
    {O::State s;auto e=evade;e.after.stacks[O::Focus]=4;Check(O::Observe(27,s,e).stacks[O::Focus]==1,"Focus +2 respects the native five-stack ceiling",27);}
    {O::State s;auto e=Crossing(500);e.after.buffs=O::Regen;Check(O::Observe(14,s,e).Empty()&&s.consumed,"a present buff consumes the one-time condition without renewal",14);e.after.buffs=0;Check(O::Observe(14,s,e).Empty(),"Dispel cannot rearm an already consumed grant",14);}
    {O::State s;for(unsigned cycle=0;cycle<2;++cycle){Check(O::Observe(9,s,Base(O::EventKind::ExtraFinished)).Empty()&&O::Observe(9,s,Base(O::EventKind::ExtraFinished)).Empty(),"Cataclysm waits for three completed extras",9);Check(O::Observe(9,s,Base(O::EventKind::ExtraFinished)).stacks[O::Focus]==1,"Cataclysm repeats every third completed extra",9);}}
    {O::State s;auto e=Element(O::Fire,true);for(unsigned i=0;i<3;++i)O::Observe(32,s,e);for(unsigned i=0;i<9;++i)O::Observe(32,s,e);Check(s.chargePercent==50&&s.counter==0,"an armed fusion cannot stack or bank another charge",32);Check(O::TakeCharge(s)==50&&O::TakeCharge(s)==0,"one extra consumes the fusion charge",32);}
    std::printf("OmegaT7ConditionsRt0: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
