#include <cstdint>
#include <cstdio>
#include <exception>
static unsigned checks=0,failures=0,installs=0,deleted=0;
static uintptr_t g_base=0,lastTarget=0;
static uint64_t g_nativeTextOutlineTramp=0;
static bool succeeds=true;
static bool profileMatches=true;
namespace FfxHooks {
namespace NativePresentationEvidence {inline constexpr unsigned textOutline[1]={0x4FAE40};}
namespace NativeUiSupport {static bool Profile(uintptr_t,const unsigned (&)[1]){return profileMatches;}}
}
static void Log(const char*,...){}
static int NativeTextOutline_MenuGuard(void*,void*,float){return 0;}
namespace PLH {
// The detour boundary is substituted; the production startup helper and the
// exact Arcana call expression are compiled from dllmain.cpp by the runner.
struct x86Detour {
    x86Detour(uint64_t target,uint64_t,uint64_t*){lastTarget=static_cast<uintptr_t>(target);}
    bool hook(){++installs;return succeeds;}
    ~x86Detour(){++deleted;}
};
}
namespace FfxHooks {using CompatibleDetour=PLH::x86Detour;}
static FfxHooks::CompatibleDetour* g_nativeTextOutlineDetour=nullptr;
#include "ArcanaStartupAdapter.inc"
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
static void Reset(){delete g_nativeTextOutlineDetour;g_nativeTextOutlineDetour=nullptr;g_base=0;lastTarget=0;installs=deleted=0;succeeds=profileMatches=true;}
int main(){
    Check(kScanBeforeArcana,"Scan must validate its original drawing dependencies before Arcana owns shared scales, independently of Arcana being requested");
    Reset();
    Check(!ArcanaEarlyTextReady(0,false)&&installs==0,"inactive combat cannot install a drawing dependency");
    Check(!ArcanaEarlyTextReady(0,true)&&installs==0,"missing module fails closed");
    constexpr uintptr_t admitted=0x51000000;
    profileMatches=false;
    Check(!ArcanaEarlyTextReady(admitted,true)&&!installs,"an unknown executable or changed outline entry cannot install the guard");
    profileMatches=true;
    Check(ArcanaEarlyTextReady(admitted,true)&&lastTarget==admitted+0x4FAE40&&installs==1,"early Arcana installs against its admitted module before legacy g_base initialization");
    Check(g_base==0,"early dependency binding does not publish unrelated legacy startup state");
    g_base=admitted;
    Check(StartNativeTextOutlineGuard()&&installs==1,"later F8 startup reuses the existing drawing guard");
    Reset();g_base=admitted;
    Check(StartNativeTextOutlineGuard()&&lastTarget==admitted+0x4FAE40,"ordinary F8-only startup retains its module binding");
    Reset();succeeds=false;
    Check(!ArcanaEarlyTextReady(admitted,true)&&installs==1&&deleted==1&&!g_nativeTextOutlineDetour,"failed detour cannot publish UI readiness and releases the failed candidate");
    succeeds=true;
    Check(ArcanaEarlyTextReady(admitted,true)&&installs==2,"a failed early attempt does not poison the later valid start");
    Reset();std::printf("ArcanaStartupRt0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
