#include "../hooks/ArcanaElemental.h"
#include <cstdio>
#include <cstring>
using namespace FfxHooks::Arcana;
namespace E=Elemental;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
static E::Snapshot offered{};
static bool Supply(unsigned actor,E::Snapshot& result) noexcept {result=offered;return actor==0;}
static bool Other(unsigned,E::Snapshot&) noexcept {return false;}
int main(){
    State state;AwardAll(state,0);state.slots[0]={{13,12,kEmpty}};
    const auto effects=Aggregate(state,0);const auto both=E::Collect(effects,12,state.revision);
    Check(both.strikes==E::All&&both.wards==E::All,"Death and Hanged Man supply both external strikes and Wards");
    Check(effects.Get(EffectKind::TouchDeath)==100&&effects.Get(EffectKind::IncomingDamage)==-20&&
          effects.Get(EffectKind::KillHp)==20&&effects.Get(EffectKind::DefendMp)==3,"both cards retain their original effects");
    Check(!E::Collect(effects,0,0).strikes,"no battle epoch publishes external weapon effects");
    E::Snapshot snapshot{};Check(!E::Read(0,snapshot),"provider is OFF by default");
    offered=both;Check(E::Register(Supply)&&E::Register(Supply),"same provider registration is idempotent");
    Check(!E::Register(Other),"another provider cannot replace the active owner");
    E::Unregister(Other);Check(E::Read(0,snapshot)&&snapshot==both,"foreign teardown preserves the active provider");
    Check(!E::Read(7,snapshot)&&!snapshot.battle&&!snapshot.strikes,"aeons and monsters cannot borrow player card effects");
    Check(!E::Read(1,snapshot),"provider rejects a foreign actor without reusing a prior snapshot");
    offered.strikes=4;Check(!E::Read(0,snapshot),"undeclared bits fail closed");
    offered=both;offered.battle=0;Check(!E::Read(0,snapshot),"retired battle epochs fail closed");
    offered=both;E::Unregister(Supply);Check(!E::Read(0,snapshot),"stop retires every external strike and Ward");
    unsigned char row[96]{};row[0x1E]=4;row[0x23]=1;
    for(unsigned formula=0;formula<256;++formula){
        const bool expected=formula==1||formula==2||formula==3||formula==4||formula==7||formula==14||
                            formula==15||formula==17||formula==18||formula==19||formula==20;
        Check(E::EligibleWeapon(row,sizeof(row),formula)==expected,"fixed, fractional and unknown weapon formulas stay native");
    }
    Check(!E::EligibleWeapon(nullptr,96,1)&&!E::EligibleWeapon(row,95,1),"bounded input is mandatory");
    row[0x1E]=0;Check(!E::EligibleWeapon(row,96,1),"spells without weapon inheritance cannot gain a strike");row[0x1E]=4;
    row[0x20]=0x10;Check(!E::EligibleWeapon(row,96,1),"healing stays native");row[0x20]=0;
    row[0x23]=3;Check(!E::EligibleWeapon(row,96,1),"mixed HP/MP commands stay native");
    Check(E::WardExposure(10000,true,false)==5000&&E::WardExposure(15000,true,false)==7500,"extra Wards halve positive resolved exposure");
    Check(E::WardExposure(0,true,false)==0&&E::WardExposure(-10000,true,false)==-10000,"extra Wards preserve immunity and absorption");
    Check(E::WardExposure(15000,true,true)==15000&&E::WardExposure(15000,false,false)==15000,"locked affinities and absent Wards remain authoritative");
    std::printf("ArcanaElementalRt0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
