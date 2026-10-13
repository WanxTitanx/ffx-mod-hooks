#include <cstdio>
#include <array>
#if __has_include("../hooks/SpiraElementalCore.h")
#include "../hooks/SpiraElementalCore.h"
#define HAS_PROGRESSIVE_ELEMENTS 1
#endif
static unsigned checks=0,failures=0;
static void Check(bool v,const char* why){++checks;if(!v){++failures;std::printf("FAIL %s\n",why);}}
#ifdef HAS_PROGRESSIVE_ELEMENTS
namespace E=FfxHooks::SpiraElemental;
struct Rng {std::array<unsigned,160> values{};unsigned at=0,size=160;};
static bool Draw(void* p,unsigned& out) noexcept {auto& r=*static_cast<Rng*>(p);if(r.at>=r.size)return false;out=r.values[r.at++];return true;}
static E::Random Random(Rng& r){return {&r,Draw};}
static E::State state;
static E::ActorKey player{1,1,0},wakka{1,1,4},monster{1,1,20},other{1,1,21};
static std::array<E::Base,E::Actors> bases;
static void Begin(){state.Start(1);bases={};for(auto& b:bases)b.exposure.fill(10000);state.Spawn(player);state.Spawn(wakka);state.Spawn(monster);state.Spawn(other);}
static void CoreCases(){
 Begin();Rng rng;
 bases[20].exposure[0]=0;bases[20].exposure[1]=-10000;
 auto action=state.Begin(player,0x3000,3,bases);
 Check(state.Hit(action,monster,{true,true,false,true,0},Random(rng)),"immune zero-damage physical hit is eligible");
 Check(state.Exposure(action,monster,0)==0&&state.Live(monster,0,0)==0,"pending Imperil cannot affect any hit of the same action");
 Check(state.Finish(action,bases),"completed native action publishes deferred changes");
 Check(state.Live(monster,0,0)==2500,"Imperil breaks native immunity by25pp");
 Check(state.Live(monster,1,-10000)==-10000,"Imperil preserves native absorption");
 // Global ordinal: another target and a prior success do not reset the chance.
 Begin();rng={};action=state.Begin(player,0x3000,1,bases);
 Check(state.Hit(action,monster,{true,true,false,true,0},Random(rng)),"first hit queues Touch");
 rng.values[rng.at]=0x20000000;
 state.Hit(action,other,{true,true,false,true,0},Random(rng));state.Finish(action,bases);
 Check(state.Live(monster,0,10000)==12500&&state.Live(other,0,10000)==10000,"second hit uses25% across a changed target after success");
 rng={};action=state.Begin(player,0x3000,1,bases);rng.values[0]=0x20000000;
 state.Hit(action,other,{true,true,false,true,0},Random(rng));state.Finish(action,bases);
 Check(state.Live(other,0,10000)==12500,"new action resets to50%");
 for(unsigned ordinal=0;ordinal<30;++ordinal){Rng yes,no;yes.values[0]=(1u<<(30-ordinal))-1;no.values[0]=1u<<(30-ordinal);
  Check(E::Geometric(ordinal,Random(yes))&&!E::Geometric(ordinal,Random(no)),"exact geometric boundary50/25/12.5/6.25 and later hits");}
 Rng veryLong;Check(E::Geometric(63,Random(veryLong))&&veryLong.at==3,"long actions retain geometric probability across31-bit RNG draws");
 // Ward is based on true owner turn tickets, with no hidden reserve at zero.
 Begin();bases[20].exposure[0]=5000;
 for(unsigned turn=1;turn<=5;++turn)Check(state.Turn(monster,turn,0,bases[20])&&state.Live(monster,0,5000)==5000-static_cast<int>(turn)*1000,"resistant monster gains10pp each own turn");
 for(unsigned turn=6;turn<20;++turn)state.Turn(monster,turn,0,bases[20]);
 Check(state.Live(monster,0,5000)==0,"Ward stops at immunity without absorption");
 rng={};action=state.Begin(player,0x3000,1,bases);state.Hit(action,monster,{true,true,false,true,0},Random(rng));state.Finish(action,bases);
 Check(state.Live(monster,0,5000)==2500,"one Imperil breaks capped Ward without paying an invisible reserve");
 state.Turn(monster,20,0,bases[20]);Check(state.Live(monster,0,5000)==1500,"Ward can rebuild after Imperil");
 bases[0].exposure[0]=5000;state.Turn(player,21,1,bases[0]);state.Turn(player,21,1,bases[0]);
 Check(state.Live(player,0,5000)==4500,"one equipped Ward grants5pp and duplicate turn notification is ignored");
 state.Turn(player,22,0,bases[0]);Check(state.Live(player,0,5000)==4500,"unequipped armor cannot grow Ward");
 // Aurochs ownership, shared result negative controls and ordinary Touch suppression.
 Begin();rng={};action=state.Begin(wakka,0x3077,3,bases);
 state.Hit(action,monster,{true,true,false,true,1},Random(rng));
 state.Hit(action,monster,{true,true,false,true,1},Random(rng));state.Finish(action,bases);
 Check(state.Live(monster,0,10000)==20000&&state.Live(monster,1,10000)==15000,"Aurochs adds100pp once; same-element Touch suppressed, other Touch remains geometric");
 Begin();rng={};action=state.Begin(wakka,0x3077,0,bases);
 state.Hit(action,monster,{true,true,false,true,0},Random(rng));state.Finish(action,bases);
 Check(state.Live(monster,0,10000)==10000,"non-elemental Aurochs Shot gets no invented weapon element");
 for(unsigned parent:{0x3075u,0x3076u,0x312Eu,0x30FDu}){
  Begin();rng={};action=state.Begin(wakka,parent,0,bases);state.Hit(action,monster,{true,true,false,true,1},Random(rng));state.Finish(action,bases);
  Check(state.Live(monster,0,10000)==10000,"shared reels results/category alone never grant Aurochs bonus");}
 // Monster rolls only once per target/action and never uses player falloff.
 Begin();rng={};action=state.Begin(monster,0x6120,0,bases);
 state.Hit(action,player,{true,true,false,false,1},Random(rng));
 state.Hit(action,player,{true,true,false,false,1},Random(rng));
 state.Hit(action,wakka,{true,true,false,false,1},Random(rng));state.Finish(action,bases);
 Check(state.Live(player,0,10000)==12500&&state.Live(wakka,0,10000)==12500&&rng.at==2,"monster25% rolls once per hostile target, without falloff or per-hit stacking");
 for(unsigned n=0;n<12;++n){rng={};action=state.Begin(player,0x3000,1,bases);state.Hit(action,monster,{true,true,false,true,0},Random(rng));state.Finish(action,bases);}
 Check(state.Live(monster,0,10000)==25000,"exposure caps at250%");
 // No effects for misses/healing/friendly targets/absorption or canceled/reused actors.
 Begin();rng={};action=state.Begin(player,0x3000,1,bases);
 for(const auto hit:{E::Hit{false,true,false,true,0},E::Hit{true,false,false,true,0},E::Hit{true,true,true,true,0}})state.Hit(action,monster,hit,Random(rng));
 state.Finish(action,bases);Check(rng.at==0&&state.Live(monster,0,10000)==10000,"miss/friendly/absorption do not roll or apply");
 rng={};action=state.Begin(player,0x3000,1,bases);state.Hit(action,monster,{true,true,false,true,0},Random(rng));state.Cancel(action);
 Check(!state.Finish(action,bases)&&state.Live(monster,0,10000)==10000,"canceled action cannot publish pending effects");
 action=state.Begin(player,0x3000,1,bases);state.Hit(action,monster,{true,true,false,true,0},Random(rng));state.Remove(monster);state.Spawn({1,2,20});state.Finish(action,bases);
 Check(state.Live({1,2,20},0,10000)==10000,"slot reuse cannot inherit pending debuffs");
 state.Start(2);Check(!state.Existing(player)&&!state.Finish(action,bases),"battle generation change clears actors/actions");
 Begin();bases[20].immune=1;action=state.Begin(player,0x3000,1,bases);rng={};state.Hit(action,monster,{true,true,false,true,0},Random(rng));state.Finish(action,bases);
 Check(state.Live(monster,0,10000)==10000,"existing Imperil immunity is honored");
 Begin();bases[20].resistance[0]=10000;action=state.Begin(wakka,0x3077,1,bases);rng={};state.Hit(action,monster,{true,true,false,true,1},Random(rng));state.Finish(action,bases);
 Check(state.Live(monster,0,10000)==10000,"Aurochs respects Imperil resistance as well");
}
#endif
int main(){
#ifdef HAS_PROGRESSIVE_ELEMENTS
 CoreCases();
#else
 Check(false,"progressive Touch/Ward rules are not implemented");
#endif
 std::printf("SpiraElementalCoreRt0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
