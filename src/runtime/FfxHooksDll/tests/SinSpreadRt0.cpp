#include "../hooks/SinSpreadCore.h"
#include <cstdio>
using namespace FfxHooks::SinSpread;
static int checks,failures;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}
int main(){
    Check(!BuildAssignment(310,1,0,Distribution::Random,false).active,"OFF admits no curse assignments");
    Check(!BuildAssignment(0x236,1,0,Distribution::Most,true).supported,"masked field aliases remain unsupported");
    bool sawZero=false,sawVariation=false;
    for(unsigned field:{310u,340u})for(unsigned seed=0;seed<128;++seed){
        for(auto mode:{Distribution::Random,Distribution::Few,Distribution::Half,Distribution::Most}){
            const auto a=BuildAssignment(static_cast<unsigned short>(field),seed,7,mode,true);
            const auto b=BuildAssignment(static_cast<unsigned short>(field),seed,7,mode,true);
            Check(a.supported && a.count==(field==310?6u:4u),"area uses its exact reviewed natural roster");
            Check(a.cursed<=a.count*80u/100u,"no distribution infects every monster species");
            if(mode!=Distribution::Random)Check(a.cursed==Quota(a.count,mode),"fixed distribution has its exact bounded quota");
            if(!a.cursed)sawZero=true;
            for(unsigned i=0;i<a.count;++i){
                Check(a.monsters[i].monster==b.monsters[i].monster && a.monsters[i].curse==b.monsters[i].curse,"same area, seed and visit produce the same assignment");
                const auto* entry=FindMonster(a.monsters[i].monster);
                Check(entry && (!a.monsters[i].curse || (entry->allowedMask&(1u<<(a.monsters[i].curse-1u)))),"a monster receives only an allowed authored curse");
                Check(a.monsters[i].threat<=2 && (!a.monsters[i].curse)==(a.monsters[i].threat==0),"uncursed is T0; Macalania threat follows curated curse");
                Check(a.monsters[i].scalePercent==100u+a.monsters[i].threat*10u,"user growth curve is ten percent per threat");
            }
            const auto next=BuildAssignment(static_cast<unsigned short>(field),seed,8,mode,true);
            for(unsigned i=0;i<a.count;++i)if(a.monsters[i].curse!=next.monsters[i].curse)sawVariation=true;
        }
    }
    Check(sawZero,"Random permits an area with no cursed monsters");
    Check(sawVariation,"another visit can produce a different assignment");
    Check(!FindMonster(124) && FindMonster(87) && FindMonster(87)->field==310,"Chimera is explicit in Macalania while scripted bosses remain excluded");
    Check(Curse(1).threat==1 && Curse(2).threat==2 && Curse(8).threat==2,"curated UNI levels are retained");
    std::uint32_t value=0;
    Check(ParseSeed("4294967295",&value) && value==0xFFFFFFFFu,"seed editor accepts the unsigned maximum");
    Check(!ParseSeed("4294967296",&value) && !ParseSeed("-1",&value) && !ParseSeed("",&value),"seed editor rejects overflow, negative and empty values");
    std::printf("SinSpreadRt0: %d/%d checks passed; failures=%d\n",checks-failures,checks,failures);return failures?1:0;
}
