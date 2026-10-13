#include "../hooks/CaptureCascadeCaptureCore.h"
#include "../hooks/CaptureCascadeProgress.h"
#include <cstring>
#include <cstdio>
int main(){
    using namespace FfxHooks::CaptureCascade;unsigned checks=0,failed=0;
    auto check=[&](bool value){++checks;if(!value)++failed;};
    CaptureEvidence e{true,true,true,true,true,true,18,0x1000+334,255,1};
    for(unsigned model=334;model<=343;++model){e.monster=0x1000+model;check(CapturedFamily(e)==FamilyForMonster(model));}
    e.monster=0x1000+334;
    for(unsigned missing=0;missing<6;++missing){auto bad=e;bool* gates[]={&bad.enabled,&bad.bound,&bad.nativeCaller,&bad.battle,&bad.matchingActor,&bad.death};*gates[missing]=false;check(CapturedFamily(bad)==kNoFamily);}
    for(unsigned slot:{0u,17u,26u,UINT32_MAX}){auto bad=e;bad.slot=slot;check(CapturedFamily(bad)==kNoFamily);}
    for(unsigned arena:{0u,103u,104u,254u,256u}){auto bad=e;bad.arena=arena;check(CapturedFamily(bad)==kNoFamily);}
    for(unsigned model:{9u,333u,344u,900u,909u}){auto bad=e;bad.monster=0x1000+model;check(CapturedFamily(bad)==kNoFamily);}
    e.marker=0;check(CapturedFamily(e)==kNoFamily);
    State state=Adopt(0);Stamp stamp;stamp.utc=1791504000;std::strcpy(stamp.build.data(),"test build");
    check(ObserveVictories(state,1,stamp));check(ObserveCapture(state,Aeon::Valefor,stamp));
    char label[100]{};check(ProgressLabel(&state,0,-1,label,sizeof(label))&&std::strstr(label,"Captured"));
    check(ProgressLabel(&state,0,3,label,sizeof(label))&&std::strstr(label,"2026-10-09"));
    check(ProgressLabel(&state,0,4,label,sizeof(label))&&std::strstr(label,"test build"));
    check(ProgressLabel(nullptr,0,-1,label,sizeof(label))&&std::strstr(label,"Unavailable"));
    check(!ProgressLabel(&state,8,0,label,sizeof(label)));check(!ProgressLabel(&state,0,5,label,sizeof(label)));
    std::printf("Capture Cascade capture/progress RT0: %u/%u passed\n",checks-failed,checks);return failed?1:0;
}
