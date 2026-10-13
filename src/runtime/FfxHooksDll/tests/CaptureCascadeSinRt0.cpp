#include "../hooks/SinRamScalingCore.h"
#include "../hooks/SinSpreadCore.h"
#include "../hooks/SinThreatPalette.h"
#include "../hooks/SinNaturalCore.h"
#include "../hooks/SinMetadataCore.h"
#include <cstdio>
namespace {unsigned checks=0,failures=0;void Check(bool v,const char* why){++checks;if(!v){++failures;std::fprintf(stderr,"FAIL: %s\n",why);}}}
int main(){
    using namespace FfxHooks;
    const auto normal=SinSpread::BuildAssignment(310,42,1,SinSpread::Distribution::Few,true);
    Check(normal.cursed<normal.count,"ordinary SIN keeps its existing regional quota");
    const auto awakened=SinSpread::BuildAssignment(310,42,1,SinSpread::Distribution::Few,true,7);
    Check(awakened.supported&&awakened.cursed==awakened.count,"awakening curses the entire compatible regional roster");
    for(unsigned i=0;i<awakened.count;++i)Check(awakened.monsters[i].curse&&awakened.monsters[i].threat>=7,"awakening establishes a real T7 minimum");
    SinRam::ScaleRequest r;r.config.enabled=true;r.config.threatLevel=7;
    r.encounter={310u<<16,0x1003,SinRam::EncounterOrigin::Natural,SinNatural::kCallerRva};r.correlation={1,1,2,2,2};
    r.preDifficultyHp={1000,500};r.afterDifficulty.maxHp=1000;r.afterDifficulty.overkill=500;r.afterDifficulty.stats.fill(10);
    Check(!SinRam::BuildScalePlan(r).admitted,"global F7 scalar cannot manufacture a T7 override in an early region");
    r.config.minimumThreat=7;
    const auto scaled=SinRam::BuildScalePlan(r);
    Check(scaled.admitted&&scaled.writeback.maxHp==6630&&scaled.writeback.currentHp==3315,"awakening floors Murussu at Calm Lands Shred before the single T7 HP ratio");
    Check(scaled.writeback.stats[0]==77&&scaled.writeback.stats[3]==250,"awakening raises offense and defense before T7");
    r.encounter={70u<<16,0x1009,SinRam::EncounterOrigin::Natural,SinNatural::kCallerRva};
    r.afterDifficulty.maxHp=100;r.afterDifficulty.stats.fill(1);
    const auto wolf=SinRam::BuildScalePlan(r);
    Check(wolf.admitted&&wolf.writeback.maxHp==3400&&wolf.writeback.stats[0]==63&&wolf.writeback.stats[4]==55,"early Dingo uses Calm Lands Skoll rather than its weak original base");
    r.afterDifficulty.maxHp=9000;r.afterDifficulty.stats.fill(200);
    const auto strong=SinRam::BuildScalePlan(r);
    Check(strong.writeback.maxHp==15300&&strong.writeback.stats[0]==255,"floor never downgrades an already stronger actor");
    r.config.minimumThreat=0;r.config.threatLevel=1;
    Check(!SinRam::BuildScalePlan(r).admitted,"early area stays unchanged until its save-bound awakening");
    r.config.minimumThreat=7;r.config.threatLevel=7;
    r.encounter={310u<<16,0x1003,SinRam::EncounterOrigin::Natural,SinNatural::kCallerRva};
    SinRam::RuntimeRequest nativeOnly;nativeOnly.config={true,7,true,100,1,7};nativeOnly.encounterToken=430u<<16;
    nativeOnly.origin=SinRam::EncounterOrigin::Natural;nativeOnly.transitionCallerRva=SinNatural::kCallerRva;
    nativeOnly.transitionRequestId=nativeOnly.actorRequestId=nativeOnly.transitionGeneration=1;
    Check(SinRam::NativeAbilitiesAwakening(nativeOnly,0x10DF),"ordinary Tonberry retains native abilities while receiving the regional stat floor");
    Check(!SinRam::NativeAbilitiesAwakening(nativeOnly,0x1008),"a curated Imp still requires its actual registered curse script");
    r.encounter={351u<<16,0x105B,SinRam::EncounterOrigin::Natural,SinNatural::kCallerRva};
    r.afterDifficulty.maxHp=100;r.afterDifficulty.stats.fill(1);
    const auto machina=SinRam::BuildScalePlan(r);
    Check(machina.admitted&&machina.writeback.maxHp==9350&&machina.writeback.stats[0]==67,"ordinary noncapturable machina also gain a Calm Lands stat base");
    r.encounter={310u<<16,0x1003,SinRam::EncounterOrigin::Natural,SinNatural::kCallerRva};
    Check(SinRam::BuildRuntimeStructuralScalePlan(nativeOnly,1,430,0x10DF,r.afterDifficulty).admitted,"a native-ability exception still passes the exact natural generation gate");
    r.encounter.monsterId=0x1384;Check(!SinRam::BuildScalePlan(r).admitted,"patrol clone does not inherit regional SIN buffs");
    r.encounter.monsterId=0x100E;Check(!SinRam::BuildScalePlan(r).admitted,"snowfield Skoll is not admitted to the forest by a tier override");
    r.encounter.monsterId=0x1003;r.config.minimumThreat=11;Check(!SinRam::BuildScalePlan(r).admitted,"out-of-domain awakening floor is rejected");
    Check(SinSpread::ThreatPalette(7).marker!=SinSpread::ThreatPalette(0).marker,"T7 has its own visible tier identity");
    std::array<std::uint8_t,SinMetadata::kNameSize> name{},label{};name[0]=0x50;
    Check(SinMetadata::NameView(name,1,7,&label),"native name tag accepts awakened T7");
    std::array<std::uint8_t,SinMetadata::kLootSize> loot{},reward{};
    Check(SinMetadata::RewardView(loot,7,&reward)&&!SinMetadata::RewardView(loot,11,&reward),"awakened reward view retains a bounded tier domain");
    std::printf("Capture Cascade SIN RT0: %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
