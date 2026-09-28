#include "../hooks/MonsterRewardsCore.h"
#include "../hooks/MonsterRewardSettings.h"
#include <cstdio>
#include <cstdint>
#include <limits>
#include <initializer_list>
using namespace FfxHooks::MonsterRewards;
static unsigned checks=0,failures=0;
static void Check(bool value,const char* message){++checks;if(!value){++failures;std::fprintf(stderr,"FAIL: %s\n",message);}}
int main(){
    const auto basic=Calculate(65000,2,3,Kind::Ap);
    Check(basic.valid&&basic.individual==130000&&basic.combined==390000&&basic.applied==390000&&!basic.clamped,"individual multiplication exceeds the on-disk WORD before the general multiplier");
    std::int32_t ap=65000*25,gil=60000*7;
    Check(Adjust({60000,65000,65535},false,3,4,ap,gil)&&ap==4875000&&gil==1680000,"actual incoming dynamic global rates are used exactly once for both independent rewards");
    ap=65535*100;gil=60000;
    Check(Adjust({60000,65000,65535},true,2,3,ap,gil)&&ap==13107000&&gil==180000,"overkill uses its own native WORD and works when the general gil gate is OFF");
    for(const auto kind:{Kind::Ap,Kind::Gil})for(unsigned rate:{1u,2u,100u,1000u})for(unsigned global:{1u,7u,100u}){
        const auto result=Calculate(65535,rate,global,kind);const auto vanilla=kind==Kind::Ap?3u:2u;
        Check(result.valid&&std::uint64_t(result.applied)*vanilla+NativeAccumulatorCap<=static_cast<unsigned>((std::numeric_limits<std::int32_t>::max)()),"maximum native bonus and existing accumulator cannot overflow signed arithmetic");
    }
    Check(Calculate(65535,1000,100,Kind::Ap).clamped&&SafeBeforeVanilla(Kind::Ap)==382494549u&&SafeBeforeVanilla(Kind::Gil)==573741824u,"large requests are explicitly clamped at the proven pre-vanilla ceilings");
    for(unsigned invalid:{0u,1001u,(std::numeric_limits<unsigned>::max)()})Check(!Calculate(1,invalid,1,Kind::Ap).valid,"invalid per-monster rates are rejected");
    unsigned general=0;Check(ObservedGeneral(0,0,general)&&!ObservedGeneral(0,1,general)&&!ObservedGeneral(20,21,general)&&!ObservedGeneral(20,2020,general),"zero rewards and foreign/nonintegral general transformations are handled conservatively");
    unsigned species=0;Check(Species(0x1156,species)&&species==342&&!Species(342,species)&&!Species(0x2156,species),"raw native monster identity is distinguished from aliases and player IDs");
    ap=20;gil=30;Check(Adjust({30,20,40},false,1,1,ap,gil)&&ap==20&&gil==30,"neutral independent multipliers preserve native amounts");
    ap=200;gil=300;Check(!Adjust({30,20,40},false,0,2,ap,gil)&&ap==200&&gil==300,"invalid edits cannot partially modify a reward pair");
    RateTable all{};for(auto& row:all)row={1000,1000};std::string saved;RateTable restored{};
    Check(SerializeSettings(all,saved)&&saved.size()<MaximumSettingsBytes&&ParseSettings(saved,restored)&&restored[4095].ap==1000,
        "every one of 4096 monster IDs can persist both rates without the general INI pair limit");
    Check(ParseSettings("ffx.monster-rewards.v1\r\n342\t2\t3\r\n",restored)&&restored[342].ap==2&&restored[342].gil==3&&restored[1].ap==1,
        "Windows line endings and omitted neutral defaults round-trip by exact monster ID");
    for(const char* bad:{"ffx.monster-rewards.v0\n", "ffx.monster-rewards.v1\n342\t2\t3\n342\t4\t5\n",
        "ffx.monster-rewards.v1\n4096\t2\t3\n", "ffx.monster-rewards.v1\n1\t0\t3\n", "ffx.monster-rewards.v1\n1\t2\t1001\n",
        "ffx.monster-rewards.v1\n1\t2\t3\t4\n", "ffx.monster-rewards.v1\n1\t2\t3", "ffx.monster-rewards.v1\n1\t2\t3\n\n"})
        Check(!ParseSettings(bad,restored)&&restored[342].ap==1,"malformed, duplicate, truncated and out-of-range rows fail without publishing a partial table");
    std::printf("MonsterRewardsCoreRt0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
