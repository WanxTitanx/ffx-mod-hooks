#include "SeymourOverdriveHook.h"
#include "SeymourOverdrivePlan.h"
#include "SeymourOverdriveControl.h"
#include "SeymourCompatibilityHook.h"
#include "RecoveryNative.h"
#include "F7InLive.h"
#include "../shared/Config.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>

// Native-event extension informed by cxldalyy/playable-seymour-mod (MIT).
// Only verified local party bounds change; counter and gauge formulas stay native.
namespace FfxHooks::SeymourOverdrive {
namespace {
namespace N=RecoveryNative;
using Scope=SeymourCompatibility::Scope;
using CounterFn=int(__cdecl*)(int,unsigned,int);
using TurnFn=int(__cdecl*)(int,void*);
using DamageFn=int(__cdecl*)(int,void*,int,void*,int,int,int);
using DeathFn=int(__cdecl*)(int,void*,int,void*);
using WinFn=int(__cdecl*)();
using GaugeFn=int(__cdecl*)(int,void*,int);
constexpr char Key[]="seymour.overdrive_events";
constexpr unsigned ActorTableRva=0xd334cc,ActorStride=0xf90;
std::uintptr_t g_base=0;
std::atomic<State> g_state{State::NotStarted};
std::atomic<bool> g_attempted{false},g_ready{false};
std::atomic<unsigned> g_callbacks{0},g_extended{0},g_denied{0};
std::atomic_flag g_admin=ATOMIC_FLAG_INIT;
std::array<std::atomic<void*>,FunctionCount> g_originals{},g_clones{};
N::OwnedBatch g_batch;
Control g_control;
void (*g_log)(const char*)=nullptr;
struct Frame {Scope scope{};std::uint64_t token=0;void* character=nullptr;};
thread_local Frame* g_frame=nullptr;

bool Configured() {
    const auto value=Config::ReadIntExact(Key,0,1);
    return value.state==Config::IntReadState::Valid&&value.value==1;
}
bool Read(std::uintptr_t address,void* out,std::size_t size,std::uintptr_t image=0) noexcept {
    return N::Range(address,size,image)&&N::Copy(out,reinterpret_cast<const void*>(address),size);
}
bool Capture(Frame& frame) noexcept {
    if(!g_ready.load(std::memory_order_acquire))return false;
    frame.token=g_control.Read();
    if(!g_control.Current(frame.token)||!CaptureSeymourCompatibilityScope(&frame.scope,false))return false;
    std::uint32_t table=0,afterTable=0;
    if(!Read(g_base+ActorTableRva,&table,4,g_base)||!table||table>UINT32_MAX-8u*ActorStride)return false;
    const auto actor=table+7u*ActorStride;
    std::uint8_t active=0;
    std::int32_t maxHp=0,damageBase=0;
    if(!Read(actor+0xdc8,&active,1)||!active||!Read(actor+0x594,&maxHp,4)||
       !Read(actor+0x6f4,&damageBase,4)||maxHp<=0||damageBase<=0)return false;
    frame.scope.actorTable=table;frame.character=reinterpret_cast<void*>(actor);
    Scope after{};
    return CaptureSeymourCompatibilityScope(&after,false)&&after.epoch==frame.scope.epoch&&
           after.commandGeneration==frame.scope.commandGeneration&&after.thread==frame.scope.thread&&
           g_control.Current(frame.token)&&Read(g_base+ActorTableRva,&afterTable,4,g_base)&&afterTable==table;
}
bool FrameCurrent() noexcept {
    if(!g_frame||!g_control.Current(g_frame->token))return false;
    Frame now{};
    return Capture(now)&&now.scope==g_frame->scope&&now.token==g_frame->token&&now.character==g_frame->character;
}
int __cdecl CounterProxy(int actor,unsigned mode,int forced) {
    const DWORD error=GetLastError();
    const bool extended=actor==7&&FrameCurrent();
    const auto fn=reinterpret_cast<CounterFn>((extended?g_clones[Counter]:g_originals[Counter]).load(std::memory_order_acquire));
    SetLastError(error);
    return fn?fn(actor,mode,forced):0;
}
void __cdecl GaugeProxy(int actor,void* character,int amount) {
    const DWORD error=GetLastError();
    const bool admitted=actor!=7||(FrameCurrent()&&character==g_frame->character);
    SetLastError(error);
    // Keep the already-owned native gauge endpoint (including Ronso behavior).
    if(admitted)reinterpret_cast<GaugeFn>(g_base+GaugeRva)(actor,character,amount);
}
template<class Fn,class... Args>
int Invoke(unsigned which,bool mayExtend,void* expectedCharacter,Args... args) {
    g_callbacks.fetch_add(1,std::memory_order_acq_rel);
    const DWORD incoming=GetLastError();
    DWORD nativeError=incoming;
    Frame frame{};Frame* previous=g_frame;
    int result=0;
    __try {
        const bool extended=mayExtend&&!previous&&Capture(frame)&&
            (!expectedCharacter||expectedCharacter==frame.character);
        const auto fn=reinterpret_cast<Fn>((extended?g_clones[which]:g_originals[which]).load(std::memory_order_acquire));
        if(extended){g_frame=&frame;g_extended.fetch_add(1,std::memory_order_relaxed);}
        else if(mayExtend)g_denied.fetch_add(1,std::memory_order_relaxed);
        SetLastError(incoming);
        __try {if(fn)result=fn(args...);}
        __finally {nativeError=GetLastError();}
    } __finally {
        g_frame=previous;
        g_callbacks.fetch_sub(1,std::memory_order_release);
        SetLastError(nativeError);
    }
    return result;
}
int __cdecl CounterShim(int actor,unsigned mode,int forced) {
    return Invoke<CounterFn>(Counter,actor==7,nullptr,actor,mode,forced);
}
int __cdecl TurnShim(int actor,void* character) {
    return Invoke<TurnFn>(Turn,actor==7&&character,character,actor,character);
}
bool EventArguments(int attacker,void* source,int target,void* destination) noexcept {
    return (attacker!=7||source)&&(target!=7||destination)&&
           (attacker!=7||target!=7||source==destination);
}
int __cdecl DamageShim(int attacker,void* source,int target,void* destination,int damage,int dealt,int applied) {
    return Invoke<DamageFn>(Damage,EventArguments(attacker,source,target,destination),
        attacker==7?source:target==7?destination:nullptr,attacker,source,target,destination,damage,dealt,applied);
}
int __cdecl DeathShim(int attacker,void* source,int target,void* destination) {
    return Invoke<DeathFn>(Death,EventArguments(attacker,source,target,destination),
        attacker==7?source:target==7?destination:nullptr,attacker,source,target,destination);
}
int __cdecl WinShim(){return Invoke<WinFn>(Win,true,nullptr);}

bool BuildBodies(std::uintptr_t base) {
    std::array<std::array<std::uint8_t,1024>,FunctionCount> source{};
    for(unsigned i=0;i<FunctionCount;++i){
        const auto& spec=Functions[i];std::array<std::uint8_t,1024> expected{};
        if(!Reference(i,static_cast<std::uint32_t>(base),expected.data(),expected.size())||
           !N::Range(base+spec.rva,spec.size,base,true)||!Read(base+spec.rva,source[i].data(),spec.size,base)||
           std::memcmp(source[i].data(),expected.data(),spec.size)!=0)return false;
    }
    if(!N::Pin(reinterpret_cast<const void*>(&Start)))return false;
    for(unsigned i=0;i<FunctionCount;++i){
        const auto& spec=Functions[i];void* memory=N::AllocateCode(spec.size);
        if(!memory)return false;
        const auto address=reinterpret_cast<std::uintptr_t>(memory);
        const Targets routes{static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&CounterProxy)),
                             static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&GaugeProxy))};
        if(address>UINT32_MAX||!Relocate(i,static_cast<std::uint32_t>(base),static_cast<std::uint32_t>(address),
            routes,source[i].data(),spec.size,static_cast<std::uint8_t*>(memory),spec.size)||!N::SealCode(memory,spec.size))return false;
        g_clones[i].store(memory,std::memory_order_release);
    }
    // Five small one-shot allocations remain alive, even after failure. A zero
    // C++ count cannot disprove a thread paused in a machine-code prologue.
    return true;
}
}
void Start(std::uintptr_t base,bool validateOnly,void (*log)(const char*)) {
    if(g_admin.test_and_set(std::memory_order_acquire))return;
    bool expected=false;
    if(!g_attempted.compare_exchange_strong(expected,true,std::memory_order_acq_rel)){
        g_admin.clear(std::memory_order_release);return;
    }
    g_log=log;
    if(validateOnly){g_state.store(State::ValidateOnly);g_admin.clear(std::memory_order_release);return;}
    const bool requested=Configured();
    if(!g_control.Publish(requested)){g_state.store(State::Stopped);g_admin.clear(std::memory_order_release);return;}
    if(!requested){g_state.store(State::Off);g_admin.clear(std::memory_order_release);return;}
    if(base>UINT32_MAX||!ImageBaseValid(static_cast<std::uint32_t>(base))||
       !F7_SharedBattleRuntimeReady(base)||!N::Profile(base)||!N::Match(base,RecoveryEvidence::WardActor)||
       !N::Range(base+GaugeRva,1,base,true)||!BuildBodies(base)){
        g_state.store(State::Unavailable);g_admin.clear(std::memory_order_release);return;
    }
    g_base=base;
    void* shims[]={reinterpret_cast<void*>(&CounterShim),reinterpret_cast<void*>(&TurnShim),
        reinterpret_cast<void*>(&DamageShim),reinterpret_cast<void*>(&DeathShim),reinterpret_cast<void*>(&WinShim)};
    for(unsigned i=0;i<FunctionCount;++i){
        void* original=nullptr;
        if(!g_batch.Add(base+Functions[i].rva,shims[i],&original)){
            g_state.store(g_batch.DiscardUnpublished()?State::Unavailable:State::StopPending);
            g_admin.clear(std::memory_order_release);return;
        }
        g_originals[i].store(original,std::memory_order_release);
    }
    if(!g_batch.Publish(MinHookBatch::Owner::SeymourOverdrive,reinterpret_cast<const void*>(&Start))){
        g_state.store(g_batch.Neutralize()?State::Unavailable:State::StopPending);
    }else if(!g_control.Current(g_control.Read())){
        g_state.store(g_batch.Neutralize()?State::Stopped:State::StopPending);
    }else{
        g_ready.store(true,std::memory_order_release);g_state.store(State::Installed);
        if(g_log)g_log("[seymour-overdrive] native event bodies installed; own-battle scope required; default OFF\n");
    }
    g_admin.clear(std::memory_order_release);
}
void PresentTick(){(void)g_control.Publish(Configured());}
void RequestStop() noexcept {g_control.Stop();g_ready.store(false,std::memory_order_release);}
bool Remove() {
    RequestStop();
    if(g_admin.test_and_set(std::memory_order_acquire))return false;
    const bool done=g_batch.Neutralize()&&g_callbacks.load(std::memory_order_acquire)==0;
    g_state.store(done?State::Stopped:State::StopPending,std::memory_order_release);
    g_admin.clear(std::memory_order_release);
    // No learned state is rolled back: native progression belongs to the current
    // save. Stop closes additional events and retains every reachable code block.
    return done;
}
void MenuLabel(char* out,std::size_t size) {
    if(!out||!size)return;
    const auto value=Config::ReadIntExact(Key,0,1);
    std::snprintf(out,size,"Native Overdrive events: %s%s",value.state==Config::IntReadState::Invalid?"INVALID":
        value.value==1?"ON":"OFF",value.value==1&&!g_ready.load()?" - restart required":"");
}
bool MenuAction() {
    const auto before=Config::ReadIntExact(Key,0,1);
    const int next=before.state==Config::IntReadState::Invalid?0:before.value==1?0:1;
    if(!Config::SetInt(Key,next))return false;
    const auto after=Config::ReadIntExact(Key,0,1);PresentTick();
    return after.state==Config::IntReadState::Valid&&after.value==next;
}
void Detail(char* out,std::size_t size) {
    if(!out||!size)return;
    const char* text="not started";
    switch(g_state.load(std::memory_order_acquire)){
        case State::Off:text="OFF at startup; enable and restart";break;
        case State::ValidateOnly:text="validate-only; no patches";break;
        case State::Unavailable:text="native signature/dependency unavailable";break;
        case State::Installed:text="installed; own Seymour battle required";break;
        case State::StopPending:text="stop pending; code retained";break;
        case State::Stopped:text="stopped; restart required";break;
        default:break;
    }
    std::snprintf(out,size,"Overdrive: %s; events %u",text,g_extended.load(std::memory_order_relaxed));
}
}
