// Jarvis-HOOK: the complete family remains one bounded atomic MinHook batch.
#include "../hooks/MinHookBatchCoordinator.h"
#include <array>
#include <cstdio>
namespace B=FfxHooks::MinHookBatch;
struct Boundary {unsigned enabled=0,disabled=0,exact=0,applies=0,failAt=0;};
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
static bool Init(void*){return true;}
static bool Enable(void* p,std::uintptr_t){auto& b=*static_cast<Boundary*>(p);return ++b.enabled!=b.failAt;}
static bool Disable(void* p,std::uintptr_t){++static_cast<Boundary*>(p)->disabled;return true;}
static bool Exact(void* p,std::uintptr_t){++static_cast<Boundary*>(p)->exact;return true;}
static bool Apply(void* p){++static_cast<Boundary*>(p)->applies;return true;}
int main(){
    std::array<std::uintptr_t,33> targets{};for(unsigned i=0;i<targets.size();++i)targets[i]=0x10000u+32u*i;
    B::Coordinator coordinator;Boundary boundary{};const B::BatchIo io{&boundary,Enable,Disable,Apply,Exact};
    Check(B::EnsureInitialized(&coordinator,{nullptr,Init})==B::InitializationResult::Ready,"shared owner initializes once");
    auto result=B::EnableBatch(&coordinator,io,B::Owner::Vanguard,targets.data(),32);
    Check(result.result==B::BatchResult::Applied&&boundary.enabled==32&&boundary.applies==1,
          "all thirty-two reviewed targets publish in a single owned batch");
    boundary={};result=B::EnableBatch(&coordinator,io,B::Owner::Vanguard,targets.data(),33);
    Check(result.result==B::BatchResult::InvalidArgument&&!boundary.enabled&&!boundary.applies,
          "over-limit batch is rejected before the first queue side effect");
    boundary={};boundary.failAt=32;result=B::EnableBatch(&coordinator,io,B::Owner::Vanguard,targets.data(),32);
    Check(result.result==B::BatchResult::EnableFailedNeutralized&&boundary.enabled==32&&boundary.disabled==32&&
          boundary.exact==32&&result.neutralized,"last-target failure neutralizes every owned target, not only a sixteen-entry prefix");
    Check(B::GetSnapshot(coordinator).state==B::State::Idle,"fully neutralized batch releases only its own coordinator ownership");
    boundary={};targets[31]=targets[0];result=B::EnableBatch(&coordinator,io,B::Owner::Vanguard,targets.data(),32);
    Check(result.result==B::BatchResult::InvalidArgument&&!boundary.enabled,"duplicates beyond the old boundary remain invalid");
    std::printf("VANGUARD_BATCH %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
