// Exercises the production settings adapter with an injected typed snapshot.
// This portable test does not claim Windows INI parsing or disk persistence.
#include "../hooks/ElementScanSettings.h"
#include <cstdio>
#include <map>
#include <string>

namespace {
std::map<std::string, FfxHooks::Config::IntReadResult> snapshot;
bool allowWrite=true, staleReadback=false;
unsigned writes=0, checks=0, failures=0;
void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
void Reset(){snapshot.clear();allowWrite=true;staleReadback=false;writes=0;}
void Seed(const char* key,int value){snapshot[key]={FfxHooks::Config::IntReadState::Valid,value};}
}
namespace FfxHooks::Config {
IntReadResult ReadIntExact(const char* key,int minimum,int maximum){
    const auto found=snapshot.find(key);
    if(found==snapshot.end())return {IntReadState::Missing,0};
    const auto result=found->second;
    if(result.state==IntReadState::Valid&&(result.value<minimum||result.value>maximum))
        return {IntReadState::Invalid,0};
    return result;
}
bool SetInt(const char* key,int value){
    ++writes;
    if(!allowWrite)return false;
    if(!staleReadback)Seed(key,value);
    return true;
}
}
int main(){
    namespace E=FfxHooks::ElementScan;
    namespace C=FfxHooks::Config;
    Reset();E::Settings settings{};
    Check(E::ReadSettings(settings)&&settings.enabled[3]==0&&E::VisibleCount(settings)==3,
          "missing fourth preferences retain the legacy three-column view");
    for(unsigned bit:{32u,64u}){
        Reset();Seed(E::BitKey,static_cast<int>(bit));Seed("element_scan.dark_enabled",0);
        Seed("element_scan.extra_rgb",0x123456);Seed("element_scan.other_enabled",1);
        Seed("element_scan.other_rgb",0xABCDEF);
        Check(E::ReadSettings(settings)&&settings.enabled[3]==1&&settings.rgb[3]==0xABCDEF,
              "the complementary Custom column reads its own visibility and color");
        Check(settings.extraBit==bit&&settings.rgb[2]==0x123456&&settings.enabled[1]==0,
              "fourth-column settings preserve the legacy bit, color and hidden Darkness");
        const auto row=E::Orbs(0x60,settings);
        Check(row[1].bit==bit&&row[2].bit==(0x60u^bit)&&row[1].rgb!=row[2].rgb,
              "both native Custom masks retain separate configured colors");
    }
    Reset();Check(E::SaveEnabled(3,true)&&writes==1&&E::ReadSettings(settings)&&settings.enabled[3]==1,
                  "the fourth toggle saves and verifies readback");
    Check(E::SaveColor(3,0x112233)&&writes==2&&E::ReadSettings(settings)&&settings.rgb[3]==0x112233,
          "the fourth color saves and verifies readback");
    const auto before=snapshot;allowWrite=false;
    Check(!E::SaveEnabled(3,false)&&!E::SaveColor(3,0x445566)&&snapshot.size()==before.size()&&
          E::ReadSettings(settings)&&settings.enabled[3]==1&&settings.rgb[3]==0x112233,
          "failed persistence leaves the prior fourth-column preferences effective");
    allowWrite=true;staleReadback=true;
    Check(!E::SaveEnabled(3,false)&&!E::SaveColor(3,0x445566),
          "reported write success with stale readback is rejected");
    staleReadback=false;const auto writesBeforeInvalid=writes;
    Check(!E::SaveEnabled(4,true)&&!E::SaveColor(4,1)&&!E::SaveColor(3,0x1000000)&&
          writes==writesBeforeInvalid,"invalid indices and colors never reach persistence");
    for(const auto* key:{"element_scan.other_enabled","element_scan.other_rgb"}){
        Reset();Seed(key,key==std::string("element_scan.other_enabled")?2:0x1000000);
        E::Settings untouched{};untouched.extraBit=64;
        Check(!E::ReadSettings(untouched)&&untouched.extraBit==64,
              "invalid fourth preferences fail closed without publishing a partial snapshot");
        snapshot[key]={C::IntReadState::Invalid,0};
        Check(!E::ReadSettings(untouched),"malformed fourth preferences are not treated as missing");
    }
    std::printf("ELEMENT_SCAN_SETTINGS_CONTRACT_RT0 %u/%u passed\n",checks-failures,checks);
    return failures?1:0;
}
