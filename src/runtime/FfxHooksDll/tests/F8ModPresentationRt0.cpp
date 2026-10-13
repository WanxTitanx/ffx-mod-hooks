#include "../hooks/F8FlagCatalog.h"
#include "../hooks/ModFeatureCatalog.h"
#include <cstdio>
#include <cstring>
#if __has_include("../hooks/F8ModPresentation.h")
#include "../hooks/F8ModPresentation.h"
#endif
using namespace FfxHooks;
static unsigned checks=0,failures=0;
static void Check(bool value,const char* why){++checks;if(!value){++failures;std::printf("FAIL %s\n",why);}}
static const char* State(const F8FlagSpec& flag,const F8RuntimeStatus& runtime,bool configured){
#ifdef FFXHOOKS_MOD_PRESENTATION_V1
    return F8Ui::ModStateText(flag,runtime,configured);
#else
    (void)configured;return runtime.hasAppliedValue?(runtime.appliedValue?"Running ON":"Running OFF"):
        flag.activation==F8Activation::RestartRequired?"Restart required":"Unavailable";
#endif
}
int main(){
    F8FlagSpec flag{};flag.activation=F8Activation::RestartRequired;F8RuntimeStatus runtime{};
    runtime.availability=F8RuntimeAvailability::ProducerUnavailable;
    Check(std::strcmp(State(flag,runtime,true),"Startup failed")==0,"failed startup is never described as merely requiring restart");
    runtime.availability=F8RuntimeAvailability::Available;runtime.hasAppliedValue=true;runtime.appliedValue=true;
    Check(std::strcmp(State(flag,runtime,true),"Running ON")==0,"active ON state remains visible");
    Check(std::strcmp(State(flag,runtime,false),"Restart required")==0,"saved OFF differs from the currently running module");
    runtime.appliedValue=false;
    Check(std::strcmp(State(flag,runtime,true),"Restart required")==0,"saved ON differs from the stopped startup module");
    Check(std::strcmp(State(flag,runtime,false),"Running OFF")==0,"confirmed OFF is not an activation failure");
#ifdef FFXHOOKS_REFORGE_GROUPS_V1
    namespace M=ModFeatures;
    Check(M::GroupFor(M::Feature::CaptureCascade)==M::MenuGroup::Capture,"Capture belongs to the Reforge regional group");
    Check(M::GroupFor(M::Feature::ItemMpRegen)==M::MenuGroup::Resources,"MP items belong to the Reforge resources group");
    for(const auto feature:{M::Feature::OmegaUltima,M::Feature::OmegaT7,M::Feature::OmegaLastSeal})
        Check(M::GroupFor(feature)==M::MenuGroup::Omega,"all Omega features share the Reforge Omega group");
    Check(M::GroupFor(M::Feature::Spira)==M::MenuGroup::Equipment&&M::GroupFor(M::Feature::Ascension)==M::MenuGroup::Equipment,"Spira abilities and Aeons belong to Reforge equipment");
    Check(M::GroupFor(M::Feature::Core)==M::MenuGroup::Additional,"independent Elemental Dominion retains its category");
#else
    Check(false,"Spira Reforge features still lack their own groups");
#endif
    std::printf("F8_MOD_PRESENTATION_RT0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
