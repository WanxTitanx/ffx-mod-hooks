#include "../hooks/ArcanaNativeEffects.h"
#include <cstdio>
#include <cstring>
using namespace FfxHooks::Arcana;
namespace N=NativeEffects;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
static void Put(unsigned char* p,unsigned value,unsigned size){for(unsigned i=0;i<size;++i)p[i]=static_cast<unsigned char>(value>>(i*8));}
static unsigned Get(const unsigned char* p,unsigned size){unsigned v=0;for(unsigned i=0;i<size;++i)v|=unsigned(p[i])<<(i*8);return v;}
int main(){
    State state;AwardAll(state,0);state.slots[0]={{21,11,kEmpty}};const auto world=Aggregate(state,0);
    unsigned char flags[6]={0,0,0,1,0,0};N::MergeFlags(flags,world);
    Check(Get(flags,2)==0x28&&Get(flags+2,2)==0xB00,"card flags preserve native Master Thief and add Counter/Magic Counter/BHP/BDL");
    Check(N::ClampRole(0x3868a7)==0&&N::ClampRole(0x386972)==10&&N::ClampRole(0x123456)==-1,"only proven field clamp callers acquire a role");
    N::Shadow shadow;int value=12000,maximum=9999;
    N::AdjustClamp(0,12000,0,maximum,value,world,shadow);
    Check(maximum==99999&&value==18000&&Get(shadow.baseline.data()+0x24,4)==9999,"World raises BHP and HP before native clamping while retaining vanilla projection");
    Effects hp;hp.values[static_cast<unsigned>(EffectKind::HpPercent)]=40;value=1300;maximum=9999;
    N::AdjustClamp(0,1300,0,maximum,value,hp,shadow);
    Check(value==1820,"card HP percent multiplies the native equipment-adjusted value once");
    N::Player live{};Put(live.data()+0x24,1820,4);Put(live.data()+0x28,200,4);Put(live.data()+0x1c,1700,4);Put(live.data()+0x20,150,4);
    shadow.applied=live;shadow.baseline=live;Put(shadow.baseline.data()+0x24,1300,4);shadow.seen=0x3FF;shadow.valid=true;
    const auto original=live;const auto projection=N::Project(live,shadow);
    Check(projection.changed&&!projection.conflict&&projection.hp==1700&&Get(live.data()+0x1c,4)==1300&&Get(live.data()+0x24,4)==1300,"projection saves vanilla maximum/current HP and separately retains live HP");
    Check(Get(original.data()+0x24,4)==1820&&Get(original.data()+0x1c,4)==1700,"projection never mutates the supplied live-state snapshot");
    auto foreign=original;Put(foreign.data()+0x24,2000,4);
    const auto conflict=N::Project(foreign,shadow);
    Check(conflict.conflict&&Get(foreign.data()+0x24,4)==2000,"foreign field edits are preserved instead of rolled back as owned bytes");
    state.slots[0]={{5,18,kEmpty}};N::Battle battle{};battle[0x101+3]=200;
    N::MergeBattle(battle,Aggregate(state,0));
    Check(battle[0x101+3]==255&&battle[0x101+12]==255&&battle[0x101+14]==255,"proofs use native resistance fields without modifying current HP");
    Check((Get(battle.data()+0xF2,2)&0x20)!=0,"Auto-Reflect joins the innate temporal source");
    state.slots[0]={{30,22,kEmpty}};battle={};battle[0x9E + 3]=100;battle[0x99]=8;
    N::MergeBattle(battle,Aggregate(state,0));
    Check(battle[0x9E + 3]==100&&battle[0x99]==9,"status attack uses max chance and element strikes union with native sources");
    state.slots[0]={{44,kEmpty,kEmpty}};battle={};N::MergeBattle(battle,Aggregate(state,0));
    Check(battle[0xFC]==1&&(Get(battle.data()+0xF8,2)&0x400)!=0,"SOS Regen marks the native SOS source rather than permanent Regen");
    state.slots[0]={{14,6,kEmpty}};battle={};N::MergeBattle(battle,Aggregate(state,0));
    Check((Get(battle.data()+0x17C,2)&0x4000)==0,"card-only Half MP stays in the shared card cost layer to combine once with Lovers");
    Put(battle.data()+0x17C,0x4000,2);N::MergeBattle(battle,Aggregate(state,0));
    Check((Get(battle.data()+0x17C,2)&0x4000)!=0,"native equipment Half MP is never removed by the card layer");
    Effects newStatus;
    state.slots[0]={{0,kEmpty,kEmpty}};N::Battle fool{};N::MergeBattle(fool,Aggregate(state,0));
    Check((Get(fool.data()+0x17C,2)&3)==3,"Fool grants native First Strike and retains Sensor");
    for(auto kind:{EffectKind::ProofDeath,EffectKind::ProofSlow,EffectKind::WardHoly})newStatus.values[static_cast<unsigned>(kind)]=1;
    newStatus.values[static_cast<unsigned>(EffectKind::TouchStone)]=30;
    newStatus.values[static_cast<unsigned>(EffectKind::TouchConfuse)]=50;
    N::Battle status{};N::MergeBattle(status,newStatus);
    Check(status[0x101]==255&&status[0x101+24]==255,"Death/Slow proofs map to native resistances");
    Check(status[0x9E + 2]==30&&status[0x9E + 8]==50&&(status[0x9C]&16),"Stone/Confuse touches and Holy Ward use native channels");
    state.slots[0]={{18,kEmpty,kEmpty}};N::Battle moon{};moon[0x99]=1;moon[0x9C]=2;
    const auto moonEffects=Aggregate(state,0);N::MergeBattle(moon,moonEffects);
    Check(moon[0x99]==0x81&&moon[0x9C]==0x82,"Moon adds Shadowstrike and Shadow Ward without replacing native elements");
    Check(moon[0x9E + 14]==0&&moon[0x9E + 3]==0,"elemental Shadow never injects blindness or Poison status");
    Check(moonEffects.Get(EffectKind::AutoReflect)&&moonEffects.Get(EffectKind::EvadeCounter)&&
          moonEffects.Get(EffectKind::EvasionFlat)==30&&moonEffects.Get(EffectKind::ProofSleep)&&
          moonEffects.Get(EffectKind::ProofConfuse)&&moonEffects.Get(EffectKind::TouchSleep)==100&&
          moonEffects.Get(EffectKind::TouchConfuse)==50&&moonEffects.Get(EffectKind::IncomingDamage)==-10,
          "all eight original Moon bonuses survive the two additional elemental effects");
    for(const auto entry:{std::array<unsigned,2>{{3,0x20}},std::array<unsigned,2>{{7,0x40}},std::array<unsigned,2>{{19,0x10}}}){
        state.slots[0]={{static_cast<std::int16_t>(entry[0]),kEmpty,kEmpty}};N::Battle elemental{};
        N::MergeBattle(elemental,Aggregate(state,0));
        Check((elemental[0x99]&entry[1])&&(elemental[0x9C]&entry[1]),"Empress, Chariot and Sun pair weapon elements with their matching Wards");
    }
    std::printf("ArcanaNativeEffectsRt0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
