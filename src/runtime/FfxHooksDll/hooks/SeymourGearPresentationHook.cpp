#include "SeymourGearPresentationHook.h"
#include "SeymourGearPresentationService.h"
#include "SeymourGearPresentationEvidence.h"
#include "SeymourOverdriveControl.h"
#include "RecoveryNative.h"
#include "F7InLive.h"
#include "F8FlagCatalog.h"
#include "../shared/Config.h"
#include <atomic>
#include <cstdio>

namespace FfxHooks::SeymourGearPresentation {
namespace {
namespace N=RecoveryNative;
constexpr char Key[]="seymour.gear_presentation";
using NameFn=const std::uint8_t*(__cdecl*)(unsigned,unsigned,int,std::uint16_t*);
std::atomic<void*> g_original{nullptr};
std::atomic<State> g_state{State::NotStarted};
std::atomic<bool> g_attempted{false},g_ready{false},g_stopping{false};
std::atomic<unsigned> g_callbacks{0},g_resolved{0};
std::atomic_flag g_admin=ATOMIC_FLAG_INIT;
// Independent revision instance: names are immutable and needed outside battle.
// No Overdrive state, gear ownership or save publication is consumed here.
SeymourOverdrive::Control g_control;
N::OwnedBatch g_batch;
bool Configured() {
    const auto value=Config::ReadIntExact(Key,0,1);
    return value.state==Config::IntReadState::Valid&&value.value==1;
}
bool Requested() {
    const auto* master=FindF8Flag("boosters.playable_seymour");
    return Configured()&&master&&ResolveF8Flag(*master).value;
}
struct Call {
    std::uint64_t token=0;
    DWORD incoming=0,nativeError=0;
    bool nativeCalled=false;
};
bool Current(void* p) {
    const auto& call=*static_cast<Call*>(p);
    return g_ready.load(std::memory_order_acquire)&&g_control.Current(call.token);
}
bool Model(void* p,std::uint16_t* output,std::uint16_t value) {
    if(!Current(p))return false;
    if(!output)return true;
    const auto address=reinterpret_cast<std::uintptr_t>(output);
    // Native MsWeaponName gives ownership of this optional ushort to its caller.
    // Do not infer an inventory address or edit an entire gear record here.
    if((address&1u)||!N::Range(address,sizeof(value),0,false,true)||!Current(p))return false;
    __try {*output=value;return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
const std::uint8_t* Original(void* p,unsigned name,unsigned owner,int alternate,std::uint16_t* model) {
    auto& call=*static_cast<Call*>(p);
    const auto original=reinterpret_cast<NameFn>(g_original.load(std::memory_order_acquire));
    call.nativeCalled=true;
    SetLastError(call.incoming);
    const std::uint8_t* text=nullptr;
    __try {if(original)text=original(name,owner,alternate,model);}
    __finally {call.nativeError=GetLastError();}
    return text;
}
const std::uint8_t* __cdecl NameShim(unsigned name,unsigned owner,int alternate,std::uint16_t* model) {
    g_callbacks.fetch_add(1,std::memory_order_acq_rel);
    Call call{};call.incoming=call.nativeError=GetLastError();call.token=g_control.Read();
    const std::uint8_t* text=nullptr;
    __try {
        text=Service(Current(&call),name,owner,alternate,model,{&call,&Original,&Current,&Model});
        if(!call.nativeCalled&&text)g_resolved.fetch_add(1,std::memory_order_relaxed);
    } __finally {
        g_callbacks.fetch_sub(1,std::memory_order_release);
        SetLastError(call.nativeCalled?call.nativeError:call.incoming);
    }
    return text;
}
}

void Start(std::uintptr_t base,bool validateOnly,void (*log)(const char*)) {
    if(g_admin.test_and_set(std::memory_order_acquire))return;
    bool expected=false;
    if(!g_attempted.compare_exchange_strong(expected,true,std::memory_order_acq_rel)){
        g_admin.clear(std::memory_order_release);return;
    }
    if(validateOnly){g_state.store(State::ValidateOnly);g_admin.clear(std::memory_order_release);return;}
    if(!g_control.Publish(Requested())){
        g_state.store(State::Stopped);g_admin.clear(std::memory_order_release);return;
    }
    if(!Configured()){g_state.store(State::Off);g_admin.clear(std::memory_order_release);return;}
    if(!F7_SharedBattleRuntimeReady(base)||!N::Profile(base)||!N::Match(base,NameProof)){
        g_state.store(State::Unavailable);g_admin.clear(std::memory_order_release);return;
    }
    void* original=nullptr;
    if(!g_batch.Add(base+NameProof.rva,reinterpret_cast<void*>(&NameShim),&original)){
        g_state.store(g_batch.DiscardUnpublished()?State::Unavailable:State::StopPending);
        g_admin.clear(std::memory_order_release);return;
    }
    g_original.store(original,std::memory_order_release);
    // Publication pins the DLL: returned names survive OFF and neutralization,
    // including native callers paused after returning from this callback.
    if(!g_batch.Publish(MinHookBatch::Owner::SeymourGearPresentation,reinterpret_cast<const void*>(&Start))){
        g_state.store(g_batch.Neutralize()?State::Unavailable:State::StopPending);
    }else{
        g_ready.store(true,std::memory_order_release);g_state.store(State::Installed);
        // Stop may race the machine-code publication. Preserve the absorbing
        // stop and neutralize the batch instead of reporting false readiness.
        if(g_stopping.load(std::memory_order_acquire)){
            g_ready.store(false,std::memory_order_release);
            g_state.store(g_batch.Neutralize()?State::Stopped:State::StopPending);
        }else if(log)log("[seymour-gear] native name/model presentation installed; independent option and master required\n");
    }
    g_admin.clear(std::memory_order_release);
}
void PresentTick(){(void)g_control.Publish(Requested());}
void RequestStop() noexcept {
    g_stopping.store(true,std::memory_order_release);g_control.Stop();
    g_ready.store(false,std::memory_order_release);g_state.store(State::StopPending,std::memory_order_release);
}
bool Remove() {
    RequestStop();
    if(g_admin.test_and_set(std::memory_order_acquire))return false;
    const bool retired=g_batch.Neutralize()&&g_callbacks.load(std::memory_order_acquire)==0;
    g_state.store(retired?State::Stopped:State::StopPending,std::memory_order_release);
    g_admin.clear(std::memory_order_release);return retired;
}
void MenuLabel(char* out,std::size_t size) {
    if(!out||!size)return;
    const auto value=Config::ReadIntExact(Key,0,1);
    std::snprintf(out,size,"Equipment names/models: %s%s",value.state==Config::IntReadState::Invalid?"INVALID":
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
        case State::Unavailable:text="profile, signature or dependency unavailable";break;
        case State::Installed:text="installed; Playable Seymour master required";break;
        case State::StopPending:text="stop pending; strings/code retained";break;
        case State::Stopped:text="stopped; restart required";break;
        default:break;
    }
    std::snprintf(out,size,"Gear: %s; queries %u",text,g_resolved.load(std::memory_order_relaxed));
}
}
