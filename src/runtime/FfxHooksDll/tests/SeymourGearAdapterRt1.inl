// Actual adapter with simulated installation/configuration; real Windows exceptions.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include "SeymourGearPresentationHook.h"
#include "SeymourGearPresentationService.h"
#include "SeymourGearPresentationEvidence.h"
#include "SeymourOverdriveControl.h"
#include "MinHookBatchCoordinator.h"
#include "F8FlagCatalog.h"
#include "Config.h"
namespace Test {
unsigned checks=0,failures=0,adds=0,publishes=0,discards=0,neutralizes=0,configReads=0,originalCalls=0;
int config=1;bool master=true,persist=true,profile=true,signature=true,create=true;
bool publish=true,retire=true,discard=true,modelAllowed=true,throwOriginal=false;
bool stopOnPublish=false,revokeAtRange=false,offOnAtRange=false,persistReadback=false,masterMissing=false;
unsigned lastName=0,lastOwner=0;int lastAlternate=0;
constexpr std::uintptr_t Base=0x400000;
const std::uint8_t NativeText[]={0x41,0};
std::string scenario;
void Check(bool value,const char* reason){++checks;if(!value){++failures;std::printf("FAIL: %s\n",reason);}}
const std::uint8_t* __cdecl Native(unsigned,unsigned,int,std::uint16_t*);
void DuringPublish();void DuringRange();
}
namespace FfxHooks {
bool F7_SharedBattleRuntimeReady(std::uintptr_t base){return Test::profile&&base==Test::Base;}
const F8FlagSpec* FindF8Flag(const char*){static const F8FlagSpec flag{};return Test::masterMissing?nullptr:&flag;}
Config::BoolGateResult ResolveF8Flag(const F8FlagSpec&){return {Test::master,Config::BoolSource::DefaultValue};}
namespace Config {
IntReadResult ReadIntExact(const char*,int,int){++Test::configReads;
    return {Test::config<0?IntReadState::Invalid:IntReadState::Valid,Test::config};}
bool SetInt(const char*,int value){if(!Test::persist)return false;if(!Test::persistReadback)Test::config=value;return true;}
}
namespace RecoveryNative {
bool Range(std::uintptr_t,std::size_t size,std::uintptr_t=0,bool=false,bool=false){
    Test::DuringRange();SetLastError(1234);return Test::modelAllowed&&size==2;}
bool Profile(std::uintptr_t base){return Test::profile&&base==Test::Base;}
bool Match(std::uintptr_t,const RecoveryEvidence::Proof& proof){
    Test::Check(proof.rva==0x3a0c70&&proof.size>60,"whole native function checked");return Test::signature;}
class OwnedBatch {
public:
    bool Add(std::uintptr_t target,void*,void** original){++Test::adds;
        Test::Check(target==Test::Base+0x3a0c70,"exact native target");
        if(!Test::create)return false;
        *original=reinterpret_cast<void*>(&Test::Native);return true;}
    bool Publish(MinHookBatch::Owner owner,const void*){++Test::publishes;
        Test::Check(owner==MinHookBatch::Owner::SeymourGearPresentation,"independent owner");
        Test::DuringPublish();return Test::publish;}
    bool DiscardUnpublished(){++Test::discards;return Test::discard;}
    bool Neutralize(){++Test::neutralizes;return Test::retire;}
};
}
}
// INSERT ACTUAL GEAR ADAPTER
namespace Test {
namespace S=FfxHooks::SeymourGearPresentation;
const std::uint8_t* __cdecl Native(unsigned name,unsigned owner,int alternate,std::uint16_t* model){
    ++originalCalls;lastName=name;lastOwner=owner;lastAlternate=alternate;SetLastError(912);
    if(throwOriginal)RaiseException(0xe0525511,0,0,nullptr);
    if(model)*model=0xCAFE;
    return NativeText;
}
void DuringPublish(){
    std::uint16_t model=0;
    Check(S::NameShim(1,7,0,&model)==NativeText,"pre-publication callback forwards");
    Check(model==0xCAFE,"pre-publication original result");
    if(stopOnPublish)S::RequestStop();
}
void DuringRange(){
    if(revokeAtRange||offOnAtRange){
        config=0;S::PresentTick();if(offOnAtRange){config=1;S::PresentTick();}
        revokeAtRange=offOnAtRange=false;
    }
}
bool Catch(){
    __try {(void)S::NameShim(999,7,0,nullptr);return false;}
    __except(EXCEPTION_EXECUTE_HANDLER){return true;}
}
void Forward(unsigned name,unsigned owner,int alternate){
    std::uint16_t model=0;const auto before=originalCalls;SetLastError(21);
    const auto* text=S::NameShim(name,owner,alternate,&model);const auto error=GetLastError();
    Check(text==NativeText,"passthrough returns original text");
    Check(originalCalls==before+1&&model==0xCAFE,"original exactly once with output");
    Check(lastName==name&&lastOwner==owner&&lastAlternate==alternate,"unchanged native arguments");
    Check(error==912,"original LastError retained");
}
}
int main(int argc,char** argv){
    using namespace Test;namespace S=FfxHooks::SeymourGearPresentation;
    if(argc!=2)return 2;
    scenario=argv[1];
    if(scenario=="off")config=0;
    if(scenario=="invalid")config=-1;
    if(scenario=="profile")profile=false;
    if(scenario=="signature")signature=false;
    if(scenario=="create"||scenario=="create-rollback")create=false;
    if(scenario=="create-rollback")discard=false;
    if(scenario=="publish"||scenario=="publish-rollback")publish=false;
    if(scenario=="publish-rollback")retire=false;
    if(scenario=="master-off")master=false;
    if(scenario=="master-missing")masterMissing=true;
    if(scenario=="stop-before")S::RequestStop();
    stopOnPublish=scenario=="stop-during";
    S::Start(Base,scenario=="validate",nullptr);
    const auto state=S::g_state.load();
    if(scenario=="off"||scenario=="invalid")Check(state==S::State::Off&&!adds&&!publishes,"OFF/invalid no hooks");
    else if(scenario=="validate")Check(state==S::State::ValidateOnly&&!adds&&!configReads,"validation-only inert");
    else if(scenario=="stop-before")Check(state==S::State::Stopped&&!adds,"stop-before absorbing");
    else if(scenario=="profile"||scenario=="signature")Check(state==S::State::Unavailable&&!adds&&!publishes,"preflight before creation");
    else if(scenario=="create"||scenario=="create-rollback")Check(state==(discard?S::State::Unavailable:S::State::StopPending)&&discards==1&&!publishes,"failed create retained/rejected");
    else if(scenario=="publish"||scenario=="publish-rollback")Check(state==(retire?S::State::Unavailable:S::State::StopPending)&&neutralizes==1&&!S::g_ready.load(),"failed publication not operational");
    else if(scenario=="stop-during"){
        Check(state==S::State::Stopped&&!S::g_ready.load()&&neutralizes==1,"stop during publication cannot report installed");Forward(1,7,0);
    }else{
        Check(state==S::State::Installed&&adds==1&&publishes==1,"complete installation");
        S::Start(Base,false,nullptr);Check(adds==1,"one-shot installation");
        if(scenario=="master-off"||scenario=="master-missing")Forward(1,7,0);
        else if(scenario=="other")for(unsigned actor:{0u,1u,6u,8u,255u})Forward(1,actor,0);
        else if(scenario=="unknown")for(unsigned row:{171u,4095u,65535u})Forward(row,7,9);
        else if(scenario=="off-after"){config=0;S::PresentTick();Forward(1,7,0);}
        else if(scenario=="master-revoke"){master=false;S::PresentTick();Forward(1,7,0);}
        else if(scenario=="revoke"||scenario=="off-on"){
            revokeAtRange=scenario=="revoke";offOnAtRange=scenario=="off-on";Forward(1,7,0);
        }else if(scenario=="write-rejected"){modelAllowed=false;Forward(1,7,0);}
        else if(scenario=="exception"){
            throwOriginal=true;const bool caught=Catch();const auto error=GetLastError();
            Check(caught&&error==912&&S::g_callbacks.load()==0,"exception propagation and cleanup");throwOriginal=false;
        }else if(scenario=="menu"){
            config=-1;Check(S::MenuAction()&&config==0,"invalid config repairs OFF");
            persist=false;Check(!S::MenuAction()&&config==0,"persistence failure");
            persist=true;persistReadback=true;Check(!S::MenuAction()&&config==0,"readback mismatch");
            char small[3]={};S::MenuLabel(small,sizeof(small));Check(small[2]==0,"bounded menu label");
        }else{
            const auto before=originalCalls;
            for(unsigned row=0;row<171;++row)for(int alternate:{0,1}){
                std::uint16_t model=0;SetLastError(21);
                const auto* text=S::NameShim(row,7,alternate,&model);const auto error=GetLastError();
                Check(text==S::Names[row].data()&&model==(row<74?0x4066:0x4067),"exact catalog name/model");
                Check(error==21,"presentation preserves incoming error");
                Check(S::NameShim(row,7,alternate,nullptr)==text,"optional output");
            }
            Check(originalCalls==before,"valid name avoids unsupported native lookup");
            const auto* held=S::NameShim(1,7,0,nullptr);const auto first=*held;
            if(scenario=="retire-fail")retire=false;
            Check(S::Remove()==retire,"retirement outcome");
            Check(*held==first&&held==S::Names[1].data(),"text lifetime survives stop");Forward(1,7,0);
        }
        Check(S::g_callbacks.load()==0,"callback count drained");
    }
    std::printf("SeymourGearAdapterRt1 %s: %u/%u passed (simulated endpoints)\n",scenario.c_str(),checks-failures,checks);
    return failures?1:0;
}
