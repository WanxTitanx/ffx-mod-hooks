#include "../hooks/SinSpreadCore.h"
#include "../hooks/SinRamScalingCore.h"
#include "../hooks/SinMetadataCore.h"
#include <cstdio>
#include <cstring>
namespace S=FfxHooks::SinSpread;
namespace R=FfxHooks::SinRam;
namespace M=FfxHooks::SinMetadata;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;if(failures<12)std::printf("FAIL %s\n",label);}}
int main(){
    for(unsigned field:{611u,612u,613u}){
        const auto calm=S::BuildAssignment(static_cast<std::uint16_t>(field),2461293065u,9,S::Distribution::All,true);
        Check(calm.supported&&calm.Find(89)&&calm.Find(89)->curse&&S::NaturalMonsterPair(field,89),
              "actual Calm Lands tori01/02/03 routes include Coeurl at hundred percent");
    }
    Check(!S::AreaForField(610),"tori00 is a Home encounter family and cannot inherit Calm Lands by name prefix");
    for(unsigned field:{310u,311u,312u}){
        const auto forest=S::BuildAssignment(static_cast<std::uint16_t>(field),2461293065u,3,S::Distribution::All,true);
        Check(forest.supported&&forest.count==6&&forest.cursed==6&&forest.Find(3)&&forest.Find(3)->curse,
              "all three Macalania forest aliases sharing the native encounter block admit Murussu and its reviewed companions");
    }
    Check(!S::BuildAssignment(313,1,1,S::Distribution::All,true).supported,
          "the distinct Spherimorph event block is not a natural forest alias");
    Check(S::NaturalMonsterPair(330,81)&&S::NaturalMonsterPair(333,81)&&S::NaturalMonsterPair(333,87),
          "the installed snow formations admit their known Blue Element and Chimera models");
    Check(!S::NaturalMonsterPair(340,87)&&!S::NaturalMonsterPair(330,87),
          "regional coverage does not invent Chimera membership in other snow fields");
    for(const auto& area:S::kAreas){
        Check(S::NextArea(S::NextArea(area.id),true)==area.id,"area preview cycles forwards and backwards through the actual supported catalog");
        unsigned total=0;for(const auto& m:S::kMonsters)if(m.field==area.id)++total;
        unsigned shown=0;for(unsigned index=0;index<S::Page(total,0).pages;++index){const auto page=S::Page(total,index);
            Check(page.count<=6&&page.first==shown&&page.first+page.count<=total,"a preview page contains at most six rows without overlap or omission");shown+=page.count;}
        Check(shown==total,"paged preview can inspect every curated monster in larger areas");
    }
    for(const auto& entry:S::kMonsters){
        const auto* exact=S::FindMonster(entry.id,entry.field);
        Check(exact==&entry&&entry.allowedMask,"every area/monster pair has its own explicit authored allowlist");
        for(unsigned seed=0;seed<128;++seed){
            const auto assignment=S::BuildAssignment(entry.field,seed,7,S::Distribution::All,true);
            const auto* actor=assignment.Find(entry.id);
            Check(actor&&assignment.cursed==assignment.count&&actor->curse&&
                  (entry.allowedMask&(std::uint64_t(1)<<(actor->curse-1))),
                  "100 percent infects each compatible species without crossing its per-monster allowlist");
            if(!actor)continue;
            Check(actor->threat<=S::MaximumThreatForField(entry.field),"threat follows the reviewed regional progression");
            if(actor->curse==21)Check(entry.id==186,"tail/stone is exclusive to Anacondaur");
            if(actor->curse==22)Check(entry.id==66,"colossus fist is exclusive to Ogre");
            if(actor->curse==23)Check(entry.id==64,"pestilent breath is exclusive to Malboro");
            if(actor->curse==40)Check(entry.id==198||entry.id==200,"Fallen Touch belongs only to the two Fallen Monks");
        }
    }
    Check(S::FindMonster(63,430)->allowedMask!=S::FindMonster(63,486)->allowedMask,
          "the same Nidhogg model has distinct cavern and Gagazet compatibility pools");
    Check(S::Curse(13).id==13&&S::Curse(41).id==41&&std::strcmp(S::Curse(13).name,S::Curse(41).name),
          "UNI013A and UNI013B never collide in the runtime identity or seed");
    for(unsigned field:{426u,446u,487u,493u,517u,521u,0x236u,220u})
        Check(!S::BuildAssignment(static_cast<std::uint16_t>(field),1,1,S::Distribution::All,true).supported,
              "scripted-only, excluded and masked-alias fields remain outside natural SIN authority");
    for(const auto& pair:S::kNaturalPairs){
        const auto assignment=S::BuildAssignment(pair.field,1,1,S::Distribution::All,true);
        const auto* actor=assignment.Find(pair.monster);Check(actor!=nullptr,"every natural pair belongs to its canonical seed roster");
        if(!actor)continue;
        R::ScaleRequest request{};request.config.enabled=true;request.config.threatLevel=static_cast<int>(actor->threat);
        request.encounter={std::uint32_t(pair.field)<<16,S::NativeMonsterId(pair.monster),R::EncounterOrigin::Natural,
                           FfxHooks::ExecutableProfile::Rva<0x471CEF>()};
        request.correlation={1,1,1,1,1};request.preDifficultyHp={100,50};
        request.afterDifficulty.maxHp=100;request.afterDifficulty.overkill=100;request.afterDifficulty.stats.fill(10);
        const auto plan=R::BuildStructuralScalePlan(request);
        Check(plan.admitted&&plan.writeback.maxHp==100+actor->threat*10,
              "new T3 through T6 pairs compose once through the existing Difficulty memory owner");
        request.encounter.origin=R::EncounterOrigin::Arena;
        Check(!R::BuildStructuralScalePlan(request).admitted,"an exact monster and field cannot grant Arena authority");
    }
    Check(!S::NaturalMonsterPair(350,334)&&!S::NaturalMonsterPair(486,344)&&!S::NaturalMonsterPair(515,263),
          "dark aeons, Penance and weapon child actors never enter the ordinary natural pool");
    Check(!S::NaturalMonsterPair(492,63)&&S::NaturalMonsterPair(492,52),
          "actual underwater formation membership is distinct from the broader regional pool");
    std::array<std::uint8_t,M::kLootSize> original{},reward{};original[0]=100;
    Check(M::RewardView(original,6,&reward)&&reward[0]==160,"T6 rewards use the same bounded ten-percent curve");
    Check(!M::RewardView(original,7,&reward),"unsupported reward tiers fail closed");
    std::printf("SIN_EXPANDED_CATALOG_RT0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
