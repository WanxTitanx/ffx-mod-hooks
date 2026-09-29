// Actual-source Windows adapter harness. All platform/native endpoints are private fakes.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <new>
#include "SeymourOverdriveHook.h"
#include "SeymourOverdrivePlan.h"
#include "SeymourOverdriveControl.h"
#include "SeymourCompatibilityHook.h"
#include "RecoveryEvidence.generated.h"
#include "MinHookBatchCoordinator.h"
#include "Config.h"
namespace Test {
unsigned checks=0,failures=0,adds=0,publishes=0,discards=0,neutralizes=0,allocations=0,seals=0;
unsigned failAdd=0,failAllocation=0,failSeal=0,nativeCalls=0,extendedCalls=0,gauges=0,lastGauge=99;
bool profile=true,signature=true,pinned=true,published=true,retired=true,persist=true,owned=true;
bool raiseNative=false,raiseExtended=false,reenter=false;
int configured=1;
std::uintptr_t base=0,actors=0,replacement=0;
DWORD ownerThread=0;
std::string scenario;
void Check(bool ok,const char* reason){++checks;if(!ok){++failures;std::printf("FAIL %s\n",reason);}}
int __cdecl NativeCounter(int,unsigned,int);
int __cdecl NativeTurn(int,void*);
int __cdecl NativeDamage(int,void*,int,void*,int,int,int);
int __cdecl NativeDeath(int,void*,int,void*);
int __cdecl NativeWin();
int __cdecl NativeGauge(int,void*,int);
}
namespace FfxHooks {
bool F7_SharedBattleRuntimeReady(std::uintptr_t base){return Test::profile&&base==Test::base;}
bool CaptureSeymourCompatibilityScope(SeymourCompatibility::Scope* out,bool) noexcept {
    if(!out||!Test::owned||GetCurrentThreadId()!=Test::ownerThread)return false;
    *out={(std::uint64_t(3)<<32)|Test::ownerThread,9,Test::ownerThread};return true;
}
namespace Config {
IntReadResult ReadIntExact(const char*,int,int){
    return {Test::configured<0?IntReadState::Invalid:IntReadState::Valid,Test::configured};
}
bool SetInt(const char*,int value){if(!Test::persist)return false;Test::configured=value;return true;}
}
namespace RecoveryNative {
bool Range(std::uintptr_t at,std::size_t size,std::uintptr_t image=0,bool=false,bool=false){
    const auto fits=[&](std::uintptr_t p,std::size_t n){return p&&at>=p&&size<=n&&at-p<=n-size;};
    return (!image||image==Test::base)&&(fits(Test::base,0x237d000)||
           (!image&&(fits(Test::actors,31*0xf90)||fits(Test::replacement,31*0xf90))));
}
bool Copy(void* out,const void* input,std::size_t size){std::memcpy(out,input,size);return true;}
bool Profile(std::uintptr_t base){return Test::profile&&base==Test::base;}
bool Match(std::uintptr_t,const RecoveryEvidence::Proof&){return Test::signature;}
bool Pin(const void*){return Test::pinned;}
void* AllocateCode(std::size_t size){
    if(++Test::allocations==Test::failAllocation)return nullptr;
    return VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
}
bool SealCode(void*,std::size_t){return ++Test::seals!=Test::failSeal;}
class OwnedBatch {
public:
    bool Add(std::uintptr_t target,void*,void** original){
        if(++Test::adds==Test::failAdd)return false;
        using namespace SeymourOverdrive;
        void* calls[]={reinterpret_cast<void*>(&Test::NativeCounter),reinterpret_cast<void*>(&Test::NativeTurn),
            reinterpret_cast<void*>(&Test::NativeDamage),reinterpret_cast<void*>(&Test::NativeDeath),reinterpret_cast<void*>(&Test::NativeWin)};
        for(unsigned i=0;i<FunctionCount;++i)if(target-Test::base==Functions[i].rva){*original=calls[i];return true;}
        return false;
    }
    bool Publish(MinHookBatch::Owner owner,const void*){
        ++Test::publishes;Test::Check(owner==MinHookBatch::Owner::SeymourOverdrive,"independent owner");return Test::published;
    }
    bool DiscardUnpublished(){++Test::discards;return true;}
    bool Neutralize(){++Test::neutralizes;return Test::retired;}
};
}
}
// INSERT ACTUAL OVERDRIVE ADAPTER
namespace Test {
namespace S=FfxHooks::SeymourOverdrive;
void MaybeThrow(bool extended){
    if(extended?raiseExtended:raiseNative){SetLastError(extended?904:902);RaiseException(0xe0524242,0,0,nullptr);}
}
int __cdecl NativeCounter(int,unsigned,int){++nativeCalls;SetLastError(902);MaybeThrow(false);return 11;}
int __cdecl NativeTurn(int,void*){++nativeCalls;SetLastError(902);MaybeThrow(false);return 12;}
int __cdecl NativeDamage(int,void*,int,void*,int,int,int){++nativeCalls;SetLastError(902);MaybeThrow(false);return 13;}
int __cdecl NativeDeath(int,void*,int,void*){++nativeCalls;SetLastError(902);MaybeThrow(false);return 14;}
int __cdecl NativeWin(){++nativeCalls;SetLastError(902);MaybeThrow(false);return 15;}
int __cdecl NativeGauge(int actor,void*,int){++gauges;lastGauge=static_cast<unsigned>(actor);SetLastError(905);return 1;}
void RevokeInside(){
    if(scenario=="revoke"||scenario=="off-on"){
        configured=0;S::PresentTick();if(scenario=="off-on"){configured=1;S::PresentTick();}
    }else if(scenario=="stop-inside")S::RequestStop();
    else if(scenario=="replace-table")*reinterpret_cast<unsigned*>(base+0xd334cc)=static_cast<unsigned>(replacement);
    else if(scenario=="lose-owner")owned=false;
}
int __cdecl ExtendedCounter(int,unsigned,int){++extendedCalls;SetLastError(904);MaybeThrow(true);return 31;}
int __cdecl ExtendedTurn(int actor,void* character){
    ++extendedCalls;MaybeThrow(true);
    if(reenter){Check(S::TurnShim(actor,character)==12,"nested event calls original once");}
    RevokeInside();const auto result=S::CounterProxy(7,13,0);
    S::GaugeProxy(7,character,3);S::GaugeProxy(2,nullptr,3);
    SetLastError(904);return 100+result;
}
int __cdecl ExtendedDamage(int a,void* p,int b,void* q,int d,int dealt,int applied){
    ++extendedCalls;Check(a==20&&p==nullptr&&b==0&&q==nullptr&&d==100&&dealt==75&&applied==1,"damage ABI args intact");
    SetLastError(904);return 33;
}
int __cdecl ExtendedDeath(int a,void* p,int b,void* q){
    ++extendedCalls;Check(a==20&&p==nullptr&&b==0&&q==nullptr,"death ABI args intact");SetLastError(904);return 34;
}
int __cdecl ExtendedWin(){++extendedCalls;SetLastError(904);return 35;}
bool CatchCounter(){
    __try {(void)S::CounterShim(7,1,0);return false;}
    __except(EXCEPTION_EXECUTE_HANDLER){return true;}
}
DWORD WINAPI OtherThread(void*){return static_cast<DWORD>(S::CounterShim(7,1,0));}
void Initialize(){
    ownerThread=GetCurrentThreadId();
    base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x237d000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    actors=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,31*0xf90,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    replacement=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,31*0xf90,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!base||!actors||!replacement)throw std::bad_alloc();
    *reinterpret_cast<unsigned*>(base+0xd334cc)=static_cast<unsigned>(actors);
    for(auto table:{actors,replacement}){
        const auto actor=table+7*0xf90;*reinterpret_cast<unsigned char*>(actor+0xdc8)=1;
        *reinterpret_cast<int*>(actor+0x594)=1000;*reinterpret_cast<int*>(actor+0x6f4)=100;
    }
    for(unsigned i=0;i<S::FunctionCount;++i){const auto& spec=S::Functions[i];
        Check(S::Reference(i,static_cast<unsigned>(base),reinterpret_cast<unsigned char*>(base+spec.rva),spec.size),"fixture reference built");}
    // This generated test-only JMP reaches NativeGauge, never any FFX code.
    unsigned char stub[]={0xb8,0,0,0,0,0xff,0xe0};const auto fn=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&NativeGauge));
    std::memcpy(stub+1,&fn,4);std::memcpy(reinterpret_cast<void*>(base+S::GaugeRva),stub,sizeof(stub));
    DWORD old=0;Check(VirtualProtect(reinterpret_cast<void*>(base+S::GaugeRva),sizeof(stub),PAGE_EXECUTE_READ,&old)!=0,"private test endpoint RX");
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(base+S::GaugeRva),sizeof(stub));
}
void SubstituteBodies(){
    void* bodies[]={reinterpret_cast<void*>(&ExtendedCounter),reinterpret_cast<void*>(&ExtendedTurn),
        reinterpret_cast<void*>(&ExtendedDamage),reinterpret_cast<void*>(&ExtendedDeath),reinterpret_cast<void*>(&ExtendedWin)};
    for(unsigned i=0;i<S::FunctionCount;++i)S::g_clones[i].store(bodies[i]);
}
}
int main(int argc,char** argv){
    using namespace Test;namespace S=FfxHooks::SeymourOverdrive;
    if(argc!=2)return 2;scenario=argv[1];Initialize();
    if(scenario=="off")configured=0;
    if(scenario=="invalid")configured=-1;
    if(scenario=="profile")profile=false;
    if(scenario=="signature")signature=false;
    if(scenario=="pin")pinned=false;
    if(scenario=="alloc")failAllocation=3;
    if(scenario=="seal")failSeal=3;
    if(scenario=="create1")failAdd=1;
    if(scenario=="create5")failAdd=5;
    if(scenario=="publish"||scenario=="rollback")published=false;
    if(scenario=="rollback")retired=false;
    if(scenario=="byte-mismatch")*reinterpret_cast<unsigned char*>(base+S::Functions[S::Death].rva+40)^=1;
    if(scenario=="stop-before")S::RequestStop();
    S::Start(base,scenario=="validate",nullptr);
    const auto state=S::g_state.load();
    if(scenario=="off"||scenario=="invalid")Check(state==S::State::Off&&adds==0&&allocations==0,"OFF/invalid has no native writes");
    else if(scenario=="validate")Check(state==S::State::ValidateOnly&&adds==0&&allocations==0,"validation only inert");
    else if(scenario=="stop-before")Check(state==S::State::Stopped&&!adds&&!allocations,"stop before install absorbing");
    else if(scenario=="profile"||scenario=="signature"||scenario=="byte-mismatch"||scenario=="pin"||scenario=="alloc"||scenario=="seal")
        Check(state==S::State::Unavailable&&adds==0&&!S::g_ready.load(),"complete preflight before detours");
    else if(scenario=="create1"||scenario=="create5")Check(!S::g_ready.load()&&!publishes&&discards==1,"partial create not installed");
    else if(scenario=="publish"||scenario=="rollback")Check(!S::g_ready.load()&&neutralizes==1&&state==(retired?S::State::Unavailable:S::State::StopPending),"failed publication state retained");
    else {
        Check(state==S::State::Installed&&adds==5&&publishes==1&&S::g_ready.load(),"complete installation");
        SubstituteBodies();S::Start(base,false,nullptr);Check(adds==5,"no duplicate install");
        auto* character=reinterpret_cast<void*>(actors+7*0xf90);
        if(scenario=="counter"){
            Check(S::CounterShim(7,1,0)==31&&nativeCalls==0&&extendedCalls==1,"Seymour counter extends once");
            Check(GetLastError()==904,"native last error retained");
            Check(S::CounterShim(6,1,0)==11&&nativeCalls==1,"other counter unchanged");
        }else if(scenario=="events"){
            Check(S::DamageShim(20,nullptr,0,nullptr,100,75,1)==33,"damage uses one extended event");
            Check(S::DeathShim(20,nullptr,0,nullptr)==34,"death uses one extended event");
            Check(S::WinShim()==35&&extendedCalls==3&&nativeCalls==0,"win and no duplicated original events");
        }else if(scenario=="wrong-pointer"||scenario=="null-pointer"){
            Check(S::TurnShim(7,scenario=="null-pointer"?nullptr:reinterpret_cast<void*>(actors))==12&&extendedCalls==0,"unproved actor pointer falls through");
        }else if(scenario=="field"||scenario=="zero-hp"||scenario=="zero-damage"||scenario=="inactive"){
            if(scenario=="field")owned=false;
            if(scenario=="zero-hp")*reinterpret_cast<int*>(actors+7*0xf90+0x594)=0;
            if(scenario=="zero-damage")*reinterpret_cast<int*>(actors+7*0xf90+0x6f4)=0;
            if(scenario=="inactive")*reinterpret_cast<unsigned char*>(actors+7*0xf90+0xdc8)=0;
            Check(S::CounterShim(7,1,0)==11&&!extendedCalls,"unowned/unready player not extended");
        }else if(scenario=="wrong-thread"){
            HANDLE h=CreateThread(nullptr,0,OtherThread,nullptr,0,nullptr);Check(h!=nullptr,"thread created");
            DWORD code=99;if(h){Check(WaitForSingleObject(h,5000)==WAIT_OBJECT_0,"thread returned");GetExitCodeThread(h,&code);CloseHandle(h);}
            Check(code==11&&!extendedCalls,"other thread uses native only");
        }else if(scenario=="exception"||scenario=="native-exception"){
            if(scenario=="exception")raiseExtended=true;else {owned=false;raiseNative=true;}
            const bool caught=CatchCounter();const auto error=GetLastError();
            Check(caught&&error==(raiseExtended?904u:902u),"exception and error propagate");
            Check(S::g_frame==nullptr&&S::g_callbacks.load()==0,"exception releases frame/callback");
        }else if(scenario=="menu"){
            configured=-1;Check(S::MenuAction()&&configured==0,"invalid config repaired OFF");persist=false;
            Check(!S::MenuAction()&&configured==0,"persist failure never enables");char out[8]{};S::MenuLabel(out,sizeof(out));Check(out[7]==0,"bounded label");
        }else if(scenario=="off-after"){
            configured=0;S::PresentTick();Check(S::CounterShim(7,1,0)==11&&!extendedCalls,"live OFF stops extra counter");
        }else {
            reenter=scenario=="reentrant";
            const bool revoked=scenario=="revoke"||scenario=="off-on"||scenario=="stop-inside"||scenario=="replace-table"||scenario=="lose-owner";
            Check(S::TurnShim(7,character)==(revoked?111:131),"in-flight scope checked at counter");
            Check(gauges==(revoked?1u:2u)&&lastGauge==2,"only additional Seymour gauge is suppressed");
            Check(extendedCalls==(revoked?1u:2u),"native/extended counter dispatch count");
            Check(nativeCalls==(revoked?1u:reenter?1u:0u),"no duplicated original event");
        }
        Check(S::g_callbacks.load()==0&&S::g_frame==nullptr,"all frames restored");
        raiseNative=raiseExtended=false;
        if(scenario=="retire-fail")retired=false;
        Check(S::Remove()==retired,"normal retirement outcome");
        Check(S::g_originals[S::Counter].load()!=nullptr&&S::g_clones[S::Counter].load()!=nullptr,"reachable code retained");
        Check(S::CounterShim(7,1,0)==11,"stopped callback native fallback");
    }
    std::printf("SeymourOverdriveAdapterRt1 %s: %u/%u passed (simulated endpoints)\n",scenario.c_str(),checks-failures,checks);
    return failures?1:0;
}
