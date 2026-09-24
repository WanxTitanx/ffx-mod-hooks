#include "../hooks/SinMetadataCore.h"
#include "../hooks/SinNaturalCore.h"
#include "../hooks/SinSpreadCore.h"
#include <cstdio>
#include <algorithm>
namespace M=FfxHooks::SinMetadata;
namespace N=FfxHooks::SinNatural;
namespace S=FfxHooks::SinSpread;
static int checks,failures;
static void Check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL: %s\n",label);}}
static unsigned Word(const std::uint8_t* p){return p[0]|(unsigned(p[1])<<8);}
int main(){
    const auto preview=S::BuildAssignment(340,1259714269u,1,S::Distribution::Most,true);
    const auto realSnowfield=S::BuildAssignment(333,1259714269u,1,S::Distribution::Most,true);
    Check(realSnowfield.supported && realSnowfield.count==4 && realSnowfield.cursed==3,
        "the reported native Macalania field333 admits the saved eighty-percent seed");
    Check(realSnowfield.Find(19) && preview.Find(19) && realSnowfield.Find(19)->curse==preview.Find(19)->curse &&
        realSnowfield.Find(12) && realSnowfield.Find(12)->curse==preview.Find(12)->curse,
        "the real snowfield uses the same Flan and Wolf assignments shown in its preview");
    Check(preview.Find(19)->curse==5 && preview.Find(12)->curse==1 && preview.Find(4)->curse==2 && preview.Find(37)->curse==0,
        "the reported seed reproduces Frost-Flood Flan, Veil Wolf, Counter Mafdet and unchanged Evil Eye");
    N::Evidence good{N::kCallerRva,-1,310,2,41,2,3};
    Check(N::Admitted(good),"a real walking caller carries field identity independently of row index");
    auto bad=good;bad.caller=0x381D8C;Check(!N::Admitted(bad),"script/label entry cannot manufacture a natural encounter");
    bad=good;bad.caller=0;Check(!N::Admitted(bad),"direct external Force calls are not random walking");
    for(int value:{0,1,-2}){bad=good;bad.result=value;Check(!N::Admitted(bad),"no-start results never publish retained field bytes");}
    bad=good;bad.field=-1;Check(!N::Admitted(bad),"negative field rejected");
    bad=good;bad.field=65536;Check(!N::Admitted(bad),"wrapped field rejected");
    bad=good;bad.group=-1;Check(!N::Admitted(bad),"negative group rejected");
    bad=good;bad.selectedGroup=1;Check(!N::Admitted(bad),"a stale selected group is rejected");
    bad=good;bad.nativeFieldRow=0xFFFF;Check(!N::Admitted(bad),"invalid native row rejected");
    bad=good;bad.formation=0xFF;Check(!N::Admitted(bad),"missing selected formation rejected");
    Check(S::NativeMonsterId(3)==0x1003 && S::ModelFromNative(0x1003)==3,"native monster family tag is explicitly decoded");
    Check(S::ModelFromNative(3)==0xFFFF && S::ModelFromNative(0x0103)==0xFFFF && S::ModelFromNative(0x2003)==0xFFFF,"bare IDs and unrelated families cannot alias a monster");
    std::array<std::uint8_t,M::kNameSize> name{};name.fill(0xA5);
    const std::uint8_t murussu[]={0x5C,0x84,0x81,0x84,0x82,0x82,0x84,0};
    std::copy(std::begin(murussu),std::end(murussu),name.begin());const auto before=name;
    std::array<std::uint8_t,M::kNameSize> marked{};
    Check(M::NameView(name,1,1,&marked),"a bounded native enemy name accepts the curse label");
    const std::uint8_t suffix[]={0x3A,0x6A,0x65,0x74,0x78,0x7B,0x3A,0x63,0x31,0x6C,0};
    Check(std::equal(std::begin(suffix),std::end(suffix),marked.begin()+7),"Veil T1 uses the independently recovered native font bytes");
    Check(name==before && marked.back()==0xA5,"building a label preserves the source and buffer tail");
    for(unsigned curse=1;curse<=8;++curse)for(unsigned threat=1;threat<=2;++threat)
        Check(M::NameView(name,curse,threat,&marked),"every admitted curse and threat has a bounded label");
    auto unchanged=marked;name.fill(0x70);name.back()=0;
    Check(!M::NameView(name,1,1,&marked)&&marked==unchanged,"long names are not truncated or partially modified");
    name.fill(0x70);Check(!M::NameView(name,1,1,&marked),"unterminated names fail closed");
    name=before;name[0]=1;Check(!M::NameView(name,1,1,&marked),"unknown encoded control payloads are preserved");
    name=before;Check(!M::NameView(name,0,1,&marked)&&!M::NameView(name,1,0,&marked),"neutral and unknown labels are unavailable");
    std::array<std::uint8_t,M::kLootSize> loot{},scaled{};loot.fill(0xA5);
    loot[0]=250;loot[1]=0;loot[2]=0xA4;loot[3]=1;loot[4]=0x48;loot[5]=3;const auto base=loot;
    Check(M::RewardView(loot,1,&scaled)&&Word(scaled.data())==275&&Word(scaled.data()+2)==462&&Word(scaled.data()+4)==924,"T1 scales Gil, normal AP and overkill AP by ten percent");
    Check(std::equal(scaled.begin()+6,scaled.end(),loot.begin()+6)&&loot==base,"items, gear, steals and source data remain exact");
    Check(M::RewardView(loot,2,&scaled)&&Word(scaled.data())==300&&Word(scaled.data()+2)==504&&Word(scaled.data()+4)==1008,"T2 uses twenty percent without compounding");
    Check(Word(scaled.data()+2)*10u==5040u,"existing global reward multipliers act on the scaled input");
    for(unsigned value:{0u,1u,5u,99u,32767u,60000u,65535u})for(unsigned threat:{1u,2u}){
        const auto expected=std::min(65535u,value*(100u+threat*10u)/100u);
        Check(M::ScaledReward(static_cast<std::uint16_t>(value),threat)==expected,"native u16 reward bounds and truncation hold");
    }
    unchanged=marked;auto same=scaled;Check(!M::RewardView(loot,0,&scaled)&&scaled==same&&!M::RewardView(loot,3,&scaled),"unsupported threats do not publish a reward view");
    std::printf("SinMetadataRt0: %d/%d passed; failures=%d\n",checks-failures,checks,failures);return failures?1:0;
}
