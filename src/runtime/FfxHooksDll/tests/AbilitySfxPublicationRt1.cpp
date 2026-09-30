// Jarvis-HOOK: exercise production callbacks at the earliest legal publication
// boundary. The detour test double enters the callback before hook() returns.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define FFXHOOKS_HAVE_POLYHOOK
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include "../hooks/CompatibleDetour.h"

namespace PublicationTest {
int checks=0,failures=0,created=0,playCalls=0,handoffCalls=0;
bool playArguments=false,handoffArguments=false;
bool playEntered=false,handoffEntered=false;
void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}
void __fastcall OriginalPlay(void* self,void*,int ctx,int sequence,int p3,int p4){
    ++playCalls;playArguments=self==reinterpret_cast<void*>(0x1234)&&ctx==11&&sequence==22&&p3==33&&p4==44;
}
int __cdecl OriginalHandoff(int a,int b){++handoffCalls;handoffArguments=a==0&&b==0;return 41;}
bool EnterPlay(std::uint64_t address){
    using Fn=void(__fastcall*)(void*,void*,int,int,int,int);
    __try{reinterpret_cast<Fn>(static_cast<std::uintptr_t>(address))(reinterpret_cast<void*>(0x1234),nullptr,11,22,33,44);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnterHandoff(std::uint64_t address){
    using Fn=int(__cdecl*)(int,int);
    __try{return reinterpret_cast<Fn>(static_cast<std::uintptr_t>(address))(0,0)==41;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
namespace FfxHooks {
class PublicationTestDetour {
    std::uint64_t replacement_;std::uint64_t* original_;
public:
    PublicationTestDetour(std::uint64_t,std::uint64_t replacement,std::uint64_t* original)
        :replacement_(replacement),original_(original){}
    bool hook(){
        using namespace PublicationTest;
        if(created++==0){
            *original_=reinterpret_cast<std::uintptr_t>(&OriginalPlay);
            playEntered=EnterPlay(replacement_);
        }else{
            *original_=reinterpret_cast<std::uintptr_t>(&OriginalHandoff);
            handoffEntered=EnterHandoff(replacement_);
        }
        return true;
    }
    bool unHook(){return true;}
};
}
// Reuse the real installer and shims; replace only the patch-publication boundary.
// CompatibleDetour.h was already included, so its own implementation is unchanged.
#define CompatibleDetour PublicationTestDetour
#include "../hooks/AbilitySfxHook.cpp"
#undef CompatibleDetour

int main(){
    static_assert(sizeof(void*)==4,"This checks the actual x86 callback ABI");
    using namespace PublicationTest;
    FfxHooks::Coexistence::runtime.Observe(true);
    const auto result=FfxHooks::InstallAbilitySfxHook(0,true,nullptr);
    Check(result.ok,"production installer completes both callbacks");
    Check(playEntered,"play callback can enter before hook returns");
    Check(playCalls==1&&playArguments,"play forwards the original once with all thiscall arguments");
    Check(handoffEntered,"handoff callback can enter before hook returns");
    Check(handoffCalls==1&&handoffArguments,"handoff forwards original arguments and return value");
    Check(!FfxHooks::RemoveAbilitySfxHook(nullptr),"peer teardown retains published callback state");
    FfxHooks::PlayBattleStreaming_Hook(reinterpret_cast<void*>(0x1234),nullptr,11,22,33,44);
    Check(FfxHooks::BattleStreamingHandoff_Hook(0,0)==41&&playCalls==2&&handoffCalls==2,
        "retained callbacks still forward after a refused peer teardown");
    std::printf("Ability SFX publication RT1: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
