// Windows RT1 template: actual adapter/scope, simulated platform and native endpoints.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include "SeymourCompatibilityHook.h"
#include "SeymourBattleCore.h"
#include "RecoveryBattleEpoch.h"
#include "RecoveryEvidence.generated.h"
#include "Config.h"
namespace TestPlatform {
unsigned checks=0,failed=0,adds=0,publishes=0,discards=0,neutralizes=0;
unsigned queries=0,visibilityCalls=0,failAdd=0,configReads=0;
bool profile=true,signature=true,publish=true,neutralize=true,persist=true;
bool reenter=false,raiseOriginal=false;
bool nativeBattle=true;
int queryResult=0;
std::uintptr_t base=0,actors=0;
std::array<int,2> configured{1,1};
void Check(bool ok,const char* detail){++checks;if(!ok){++failed;std::printf("FAIL: %s\n",detail);}}
int __cdecl NativeQuery(int,std::uint32_t){
    ++queries;SetLastError(901);
    if(raiseOriginal)RaiseException(0xE0424242,0,0,nullptr);
    return queryResult;
}
void __cdecl NativeVisibility(int actor,std::uint8_t enable);
}
namespace FfxHooks {
using namespace SeymourBattle;
std::atomic<std::uint64_t> g_compatibilityEpoch{0};
std::atomic<void*> g_adapterPublicationPtr{nullptr};
std::atomic<unsigned> g_producer{static_cast<unsigned>(ProducerPublication::Ready)};
AtomicCommandMailbox g_commands;
AtomicTelemetryMailbox g_telemetry;
bool F7_SharedBattleRuntimeReady(std::uintptr_t base){return base==TestPlatform::base&&TestPlatform::profile;}
bool F7_DifficultyInBattle(){return TestPlatform::nativeBattle;}
namespace Config {
IntReadResult ReadIntExact(const char* key,int,int){
    ++TestPlatform::configReads;
    const unsigned i=std::strcmp(key,"seymour.command_safety")==0?0u:1u;
    const int v=TestPlatform::configured[i];
    return {v<0?IntReadState::Invalid:IntReadState::Valid,v<0?0:v};
}
bool SetInt(const char* key,int value){
    if(!TestPlatform::persist)return false;
    TestPlatform::configured[std::strcmp(key,"seymour.command_safety")==0?0u:1u]=value;return true;
}
}
namespace RecoveryNative {
bool Range(std::uintptr_t p,std::size_t size,std::uintptr_t image=0,bool=false,bool=false){
    if(image&&image!=TestPlatform::base)return false;
    const auto fits=[=](std::uintptr_t start,std::size_t length){return p>=start&&size<=length&&p-start<=length-size;};
    return fits(TestPlatform::base,0x237d000)||(!image&&fits(TestPlatform::actors,31*0xf90));
}
bool Copy(void* out,const void* in,std::size_t size){std::memcpy(out,in,size);return true;}
bool Profile(std::uintptr_t base){return base==TestPlatform::base&&TestPlatform::profile;}
bool Match(std::uintptr_t base,const RecoveryEvidence::Proof& proof){return TestPlatform::signature&&Range(base+proof.rva,proof.size,base);}
class OwnedBatch {
public:
    bool Add(std::uintptr_t target,void*,void** original){
        if(++TestPlatform::adds==TestPlatform::failAdd)return false;
        *original=target-TestPlatform::base==0x39a5c0?reinterpret_cast<void*>(&TestPlatform::NativeQuery):reinterpret_cast<void*>(&TestPlatform::NativeVisibility);return true;
    }
    bool Publish(MinHookBatch::Owner owner,const void*){
        ++TestPlatform::publishes;TestPlatform::Check(owner==MinHookBatch::Owner::SeymourCompatibility,"independent owner");return TestPlatform::publish;
    }
    bool DiscardUnpublished(){++TestPlatform::discards;return true;}
    bool Neutralize(){++TestPlatform::neutralizes;return TestPlatform::neutralize;}
};
}
// INSERT REAL SCOPE
}
// INSERT REAL ADAPTER
namespace TestPlatform {
void __cdecl NativeVisibility(int actor,std::uint8_t enable){
    ++visibilityCalls;
    if(raiseOriginal){SetLastError(903);RaiseException(0xE0424242,0,0,nullptr);}
    if(reenter&&visibilityCalls==1)FfxHooks::SeymourCompatibility::VisibilityShim(actor,enable);
    SetLastError(902);
}
bool CatchVisibilityException(){
    __try {FfxHooks::SeymourCompatibility::VisibilityShim(7,1);return false;}
    __except(EXCEPTION_EXECUTE_HANDLER){return true;}
}
bool CatchQueryException(){
    __try {(void)FfxHooks::SeymourCompatibility::QueryShim(7,0x3017);return false;}
    __except(EXCEPTION_EXECUTE_HANDLER){return true;}
}
DWORD WINAPI WrongThread(void*){
    const auto result=FfxHooks::SeymourCompatibility::QueryShim(7,0x3017);
    FfxHooks::SeymourCompatibility::VisibilityShim(7,1);
    return static_cast<DWORD>(result);
}
void Initialize(){
    base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x237d000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    actors=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,31*0xf90,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!base||!actors)throw std::bad_alloc();
    *reinterpret_cast<std::uint32_t*>(base+0xd334cc)=static_cast<std::uint32_t>(actors);
    *reinterpret_cast<std::uint8_t*>(actors+7*0xf90+0xdc8)=1;
    auto* slots=reinterpret_cast<std::uint8_t*>(base+0xd3205c+7*0x94+0x2d);slots[0]=10;slots[1]=11;
    for(unsigned i=0;i<2;++i){auto* gear=reinterpret_cast<std::uint8_t*>(base+0xd30f2c+(10+i)*22);
        std::memset(gear,0x5a,22);gear[2]=1;gear[3]=0xac;gear[4]=7;gear[5]=static_cast<std::uint8_t>(i);gear[6]=7;}
    const auto thread=GetCurrentThreadId();FfxHooks::RecoveryBattleEpoch::Begin(thread);
    const auto epoch=FfxHooks::RecoveryBattleEpoch::Read();
    FfxHooks::g_compatibilityEpoch.store((std::uint64_t(epoch.generation)<<32)|thread);
    FfxHooks::g_adapterPublicationPtr.store(reinterpret_cast<void*>(base));
    FfxHooks::SeymourBattle::PublishRequested(&FfxHooks::g_commands,true);
    FfxHooks::SeymourBattle::Telemetry t{};
    t.generation=FfxHooks::SeymourBattle::ReadCommand(&FfxHooks::g_commands).generation;
    t.threadId=thread;t.callback=FfxHooks::SeymourBattle::CallbackKind::Entry;
    t.state=FfxHooks::SeymourBattle::State::AppliedBattleRoster;t.outcome=FfxHooks::SeymourBattle::ServiceOutcome::Applied;
    FfxHooks::SeymourBattle::PublishTelemetry(&FfxHooks::g_telemetry,t);
}
}
int main(int argc,char** argv){
    using namespace TestPlatform;
    namespace S=FfxHooks::SeymourCompatibility;
    if(argc!=2)return 2;
    const std::string mode=argv[1];Initialize();
    if(mode=="off")configured={0,0};
    if(mode=="invalid-config")configured={-1,-1};
    if(mode=="profile")profile=false;
    if(mode=="signature")signature=false;
    if(mode=="create1")failAdd=1;
    if(mode=="create2")failAdd=2;
    if(mode=="publish"||mode=="rollback")publish=false;
    if(mode=="rollback")neutralize=false;
    S::Start(base,mode=="validate",nullptr);const auto state=S::GetSnapshot();
    if(mode=="off"||mode=="invalid-config"){
        Check(state.state==S::InstallState::Off&&!state.installed&&!adds&&!publishes,"OFF/invalid creates nothing");
    }else if(mode=="validate"){
        Check(state.state==S::InstallState::ValidateOnly&&!adds&&!configReads,"validate-only is inert");
    }else if(mode=="profile"||mode=="signature"){
        Check(state.state==S::InstallState::Unavailable&&!adds&&!publishes,"preflight rejects before patching");
        char detail[192]{};S::Detail(detail,sizeof(detail));
        Check(std::strstr(detail,"unavailable")!=nullptr,"preflight reason visible");
    }else if(mode=="create1"||mode=="create2"){
        Check(state.state==S::InstallState::Unavailable&&!state.installed&&discards==1&&!publishes,"partial create discarded");
    }else if(mode=="publish"||mode=="rollback"){
        Check(!state.installed&&neutralizes==1,"failed publication not operational");
        Check(state.state==(mode=="rollback"?S::InstallState::RestorePending:S::InstallState::Unavailable),"rollback failure retained");
        Check(S::g_query.load()&&S::g_visibility.load(),"originals retained");
    }else{
        Check(state.installed==3&&state.state==S::InstallState::Installed&&adds==2&&publishes==1,"two capabilities installed");
        S::Start(base,false,nullptr);Check(adds==2&&publishes==1,"no duplicate install");
        std::array<std::uint8_t,4400> before{};
        std::memcpy(before.data(),reinterpret_cast<void*>(base+0xd30f2c),before.size());
        if(mode=="query"){
            for(int actor:{0,6,7,8,263})for(auto command:{0x3017u,0x3024u,0x3025u,0x3026u,0x302au,0x3000u}){
                const auto prior=queries;Check(S::QueryShim(actor,command)==(actor==7&&S::UnsafeCommand(command)?1:0),"scoped query");
                Check(queries==prior+1&&GetLastError()==901,"original once, error preserved");}
            queryResult=-2;Check(S::QueryShim(7,0x3017)==-2,"native rejection retained");
        }else if(mode=="scope"||mode=="stale-epoch"||mode=="field"){
            if(mode=="scope")FfxHooks::SeymourBattle::PublishRequested(&FfxHooks::g_commands,false);
            else if(mode=="field")nativeBattle=false;
            else FfxHooks::RecoveryBattleEpoch::Begin(GetCurrentThreadId());
            Check(S::QueryShim(7,0x3017)==0,"unowned session passthrough");
            S::VisibilityShim(7,1);Check(!S::g_lease.Pending(),"unowned session does not write");
        }else if(mode=="wrong-thread"){
            HANDLE worker=CreateThread(nullptr,0,WrongThread,nullptr,0,nullptr);
            Check(worker!=nullptr,"test thread created");
            DWORD result=99;
            if(worker){Check(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0,"thread completed");
                Check(GetExitCodeThread(worker,&result)!=0&&result==0,"foreign thread query unchanged");CloseHandle(worker);}
            Check(!S::g_lease.Pending()&&std::memcmp(before.data(),reinterpret_cast<void*>(base+0xd30f2c),before.size())==0,"foreign thread did not write");
        }else if(mode=="query-exception"){
            raiseOriginal=true;const bool caught=CatchQueryException();const auto error=GetLastError();
            Check(caught&&error==901,"query exception and native error preserved");
            Check(S::g_callbacks.load()==0,"query exception count released");
        }else if(mode=="exception"){
            raiseOriginal=true;const bool caught=CatchVisibilityException();const auto error=GetLastError();
            Check(caught,"native exception propagated");Check(error==903,"native exception error preserved");
            Check(S::g_callbacks.load()==0,"SEH callback decrement");
            Check(!S::g_busy.test_and_set(),"SEH lock release");S::g_busy.clear();
        }else if(mode=="menu"){
            configured[0]=-1;Check(S::MenuAction(0)&&configured[0]==0,"invalid repaired OFF");
            persist=false;Check(!S::MenuAction(0)&&configured[0]==0,"failed persistence preserved");
            Check(!S::MenuAction(-1)&&!S::MenuAction(2),"invalid action rejected");
            char label[5]{};S::MenuLabel(0,label,sizeof(label));Check(label[4]==0,"bounded label");
        }else{
            if(mode=="reentrant")reenter=true;
            S::VisibilityShim(7,1);Check(S::g_lease.Pending(),"gear lease acquired");
            Check(visibilityCalls==(reenter?2u:1u)&&GetLastError()==902,"original once per callback/error");
            auto expected=before;expected[10*22+3]|=2;expected[11*22+3]|=2;
            Check(std::memcmp(expected.data(),reinterpret_cast<void*>(base+0xd30f2c),expected.size())==0,"only two bits changed");
            if(mode=="replaced-actor-table"){
                actors=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,31*0xf90,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
                Check(actors!=0,"replacement table allocated");
                *reinterpret_cast<std::uint32_t*>(base+0xd334cc)=static_cast<std::uint32_t>(actors);
                *reinterpret_cast<std::uint8_t*>(actors+7*0xf90+0xdc8)=1;
                Check(!S::RestoreAtBattleBoundary()&&S::g_lease.Pending(),"old lease cannot adopt a different actor table");
                Check(std::memcmp(expected.data(),reinterpret_cast<void*>(base+0xd30f2c),expected.size())==0,"stale lease performs no restoration");
            }else if(mode=="foreign"){
                auto* gear=reinterpret_cast<std::uint8_t*>(base+0xd30f2c+10*22);gear[4]=1;
                Check(!S::RestoreAtBattleBoundary()&&S::g_lease.Pending()&&gear[4]==1,"foreign owner preserved");
            }else{
                if(mode=="off-cleanup"){configured={0,0};S::PresentTick();}
                if(mode=="master-cleanup")FfxHooks::SeymourBattle::PublishRequested(&FfxHooks::g_commands,false);
                if(mode=="stop-cleanup")S::RequestStop();
                Check(S::RestoreAtBattleBoundary()&&!S::g_lease.Pending(),"OFF/stop cleanup allowed");
                Check(std::memcmp(before.data(),reinterpret_cast<void*>(base+0xd30f2c),before.size())==0,"owned bytes restored");
                Check(S::Remove()&&S::Remove(),"idempotent retirement");
                Check(S::g_query.load()&&S::g_visibility.load(),"retained original code");
            }
        }
        Check(S::g_callbacks.load()==0,"all callbacks finished");
    }
    std::printf("SeymourCompatibilityAdapterRt1 %s: %u/%u passed (simulated endpoints)\n",mode.c_str(),checks-failed,checks);
    return failed?1:0;
}
