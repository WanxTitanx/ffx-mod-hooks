#include "../hooks/CaptureCascadeBattleCore.h"
#include "../hooks/CaptureCascadeCatalog.generated.h"
#include "../hooks/CaptureCascadeRooms.generated.h"
#include <cstdio>
#include <cstring>
using namespace FfxHooks::CaptureCascade;
namespace {
unsigned checks=0,failures=0;
void Check(bool v,const char* why){++checks;if(!v){++failures;std::fprintf(stderr,"FAIL: %s\n",why);}}
void Put(std::vector<unsigned char>& b,unsigned at,unsigned value,unsigned size){for(unsigned i=0;i<size;++i)b[at+i]=static_cast<unsigned char>(value>>(i*8));}
}
int main(){
    std::vector<unsigned char> source(128);Put(source,0,8,4);Put(source,12,48,4);Put(source,16,80,4);Put(source,32,128,4);
    for(unsigned i=0;i<8;++i)Put(source,60+i*2,i<5?0x1001+i:0xFFFF,2);
    const auto original=source;FormationProof proof{source.size(),60,FormationHash(source.data(),source.size()),31};
    State state=Adopt(0);Stamp stamp;stamp.utc=1;std::strcpy(stamp.build.data(),"test");ObserveVictories(state,3,stamp);
    Config config;config.enabled=true;config.chance=config.secondChance=10000;
    const Candidate pool[]={{Aeon::Valefor,1,{0x1384,0,0}},{Aeon::Ifrit,1,{0x1385,0,0}}};
    std::vector<unsigned char> output{99};Selection plan;
    Check(BuildFormation(proof,source.data(),source.size(),config,state,Aeon::Valefor,pool,2,123,output,plan)==FormationCode::Prepared,"exact ordinary formation is prepared");
    Check(source==original&&output.size()==source.size()&&plan.groups==2,"original resource and total extent are preserved");
    bool bytes=true;for(unsigned i=0;i<source.size()&&i<output.size();++i)if(source[i]!=output[i]&&(i<60||i>=70))bytes=false;
    Check(bytes,"scripts, geometry, empty slots and non-selected sections stay byte-identical");
    auto mutation=source;mutation[43]^=1;output={99};
    Check(BuildFormation(proof,mutation.data(),mutation.size(),config,state,Aeon::Valefor,pool,2,123,output,plan)==FormationCode::Invalid&&output==std::vector<unsigned char>{99},"foreign source hash is rejected without publishing output");
    auto bad=proof;bad.slots=120;
    Check(BuildFormation(bad,source.data(),source.size(),config,state,Aeon::Valefor,pool,2,123,output,plan)==FormationCode::Invalid,"out-of-bounds actor table is rejected");
    bad=proof;bad.replaceable=255;
    Check(BuildFormation(bad,source.data(),source.size(),config,state,Aeon::Valefor,pool,2,123,output,plan)==FormationCode::Invalid,"an empty slot can never be admitted for replacement");
    config.enabled=false;
    Check(BuildFormation(proof,source.data(),source.size(),config,state,Aeon::Valefor,pool,2,123,output,plan)==FormationCode::Unchanged&&output==std::vector<unsigned char>{99},"OFF never publishes a borrowed view");
    Check(Catalog::RegionForField(68)==Aeon::Valefor&&Catalog::RegionForField(300)==Aeon::Ixion&&Catalog::RegionForField(610)==Aeon::Count,"regional catalog uses proven encounter keys and excludes Home");
    Check(Catalog::MacalaniaRoom(110)&&!Catalog::MacalaniaRoom(310),"native room IDs are distinct from encounter-table IDs");
    Check(Rooms::Region(19)==Aeon::Valefor&&Rooms::Region(110)==Aeon::Shiva&&Rooms::Region(140)==Aeon::Ixion,"regional arrival notices use native event room identities");
    Check(Rooms::Region(56)==Aeon::Yojimbo&&Rooms::Region(223)==Aeon::Count&&Rooms::Region(23)==Aeon::Count,"Cavern room cannot awaken the Calm Lands surface or title screen");
    std::printf("Capture Cascade battle RT0: %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
