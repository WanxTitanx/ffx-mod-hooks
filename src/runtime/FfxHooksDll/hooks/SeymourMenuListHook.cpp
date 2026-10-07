#include "../shared/ExecutableProfile.h"
#include "SeymourMenuListHook.h"
#include "SeymourMenuListService.h"
#include "SeymourOverdriveControl.h"
#include "RecoveryNative.h"
#include "SeymourSessionRuntime.h"
#include "F8FlagCatalog.h"
#include "../shared/Config.h"
#include <atomic>
#include <cstdio>

namespace FfxHooks::SeymourMenuList {
namespace {
namespace N=RecoveryNative;
constexpr char Key[]="seymour.menu_list";
constexpr std::uintptr_t ConstructorRva=(::FfxHooks::ExecutableProfile::Rva<0x4a8ef0>()),MenuRva=(::FfxHooks::ExecutableProfile::Rva<0x1441bd4>());
constexpr std::uintptr_t FrontRva=(::FfxHooks::ExecutableProfile::Rva<0xd307e8>()),ReserveRva=(::FfxHooks::ExecutableProfile::Rva<0xd307eb>());
constexpr std::uintptr_t PlayerRva=(::FfxHooks::ExecutableProfile::Rva<0xd3205c>()),BattleRva=(::FfxHooks::ExecutableProfile::Rva<0xd2a8e0>());
// Exact 78ce3439... entry through 0x008A8F08; no absolute relocation in this span.
#ifdef FFXHOOKS_TARGET_STEAM_20261001
constexpr std::uint8_t Prefix[]={0x55,0x8B,0xEC,0x83,0xEC,0x0C,0x53,0x8B,0x5D,0x08,0x56,0x8D,0x45,0xFC,0x57,0x81,0xE3,0x00,0x00,0xFF,0xFF,0x50,0x89,0x5D,0x08};
#else
constexpr std::uint8_t Prefix[]={0x55,0x8b,0xec,0x83,0xec,0x0c,0x53,0x8b,0x5d,0x08,
    0x56,0x8d,0x45,0xfc,0x57,0x81,0xe3,0x00,0x00,0xff,0xff,0x50,0x89,0x5d,0x08};
#endif
const RecoveryEvidence::Proof Proof{ConstructorRva,Prefix,sizeof(Prefix),nullptr,0};
enum class Phase:unsigned {Off,Unavailable,Installed,StopPending,Stopped};
std::atomic<Phase> g_phase{Phase::Off};
std::atomic<bool> g_attempted{false},g_ready{false},g_stopping{false};
std::atomic<unsigned> g_callbacks{0},g_applied{0},g_rejected{0};
std::atomic<std::uint64_t> g_sequence{0};
std::atomic<void*> g_original{nullptr};
std::atomic_flag g_admin=ATOMIC_FLAG_INIT,g_busy=ATOMIC_FLAG_INIT;
std::uintptr_t g_base=0;
SeymourOverdrive::Control g_control;
N::OwnedBatch g_batch;
struct Call {
    SeymourSession::Token session{};
    std::uint64_t request=0,sequence=0;
    DWORD error=0;
    Roster expectedRoster{};
    List before{},planned{};
    bool hasRoster=false,attempted=false;
};
struct Lease {Call call{};bool owned=false;};
Lease g_lease;

bool Configured(){
    const auto value=Config::ReadIntExact(Key,0,1);
    return value.state==Config::IntReadState::Valid&&value.value==1;
}
bool Requested(){
    const auto* flag=FindF8Flag("boosters.playable_seymour");
    return Configured()&&flag&&ResolveF8Flag(*flag).value;
}
void FailClosed() noexcept {
    g_control.Stop();g_ready.store(false,std::memory_order_release);
    g_phase.store(Phase::StopPending,std::memory_order_release);
}
std::uint64_t NextSequence() noexcept {
    auto previous=g_sequence.load(std::memory_order_acquire);
    for(;;){
        if(previous==UINT64_MAX){FailClosed();return 0;}
        if(g_sequence.compare_exchange_weak(previous,previous+1,std::memory_order_acq_rel,
                                             std::memory_order_acquire))return previous+1;
    }
}
bool Identity(const Call& call,bool cleanup){
    return call.sequence&&call.sequence==g_sequence.load(std::memory_order_acquire)&&
        call.session.Valid()&&GetCurrentThreadId()==call.session.thread&&
        SeymourSession::Current(call.session,cleanup)&&
        (cleanup||(g_ready.load(std::memory_order_acquire)&&g_control.Current(call.request)&&
                   SeymourSession::PublisherReady()));
}
bool ReadMemory(Call& call,std::uintptr_t address,void* output,std::size_t size,bool cleanup){
    return Identity(call,cleanup)&&N::Range(address,size,g_base)&&Identity(call,cleanup)&&
        N::Copy(output,reinterpret_cast<const void*>(address),size)&&Identity(call,cleanup);
}
bool Current(void* context,bool cleanup){
    auto& call=*static_cast<Call*>(context);std::uint8_t battle=1;
    return ReadMemory(call,g_base+BattleRva,&battle,1,cleanup)&&battle==0&&Identity(call,cleanup);
}
bool ReadRoster(void* context,Roster& output){
    auto& call=*static_cast<Call*>(context);Roster roster{};
    if(!ReadMemory(call,g_base+FrontRva,roster.front.data(),roster.front.size(),true)||
       !ReadMemory(call,g_base+ReserveRva,roster.reserve.data(),roster.reserve.size(),true))return false;
    for(unsigned i=0;i<Capacity;++i){
        if(!ReadMemory(call,g_base+PlayerRva+i*0x94u+0x2cu,roster.flags.data()+i,1,true))return false;
    }
    if(!ValidRoster(roster))return false;
    if(!call.hasRoster){call.expectedRoster=roster;call.hasRoster=true;}
    output=roster;return true;
}
bool ReadList(void* context,List& output){
    auto& call=*static_cast<Call*>(context);
    return ReadMemory(call,g_base+MenuRva,&output,sizeof(output),true);
}
bool WriteList(void* context,bool cleanup,const List& expected,const List& value){
    auto& call=*static_cast<Call*>(context);Roster roster{};List observed{};
    if(!Current(context,cleanup)||!N::Range(g_base+MenuRva,sizeof(List),g_base,false,true)||
       !Current(context,cleanup)||!ReadRoster(context,roster)||!(roster==call.expectedRoster)||
       !ReadList(context,observed)||!(observed==expected)||!Current(context,cleanup))return false;
    if(!cleanup){call.before=expected;call.planned=value;call.attempted=true;}
    // All writes are bounded to the native 36-byte list/count/mask record. Never
    // resize adjacent globals or write character membership, stats or save bytes.
    return N::Copy(reinterpret_cast<void*>(g_base+MenuRva),&value,sizeof(value));
}
Io AdapterIo(Call& call){return {&call,Current,ReadRoster,ReadList,WriteList};}
bool Begin(Call& call){
    call.request=g_control.Read();
    if(!g_ready.load(std::memory_order_acquire)||!g_control.Current(call.request))return false;
    call.session=SeymourSession::Capture();return Current(&call,false);
}
void RestoreOwned(){
    if(!g_lease.owned)return;
    const auto active=SeymourSession::Capture(true);
    if(active.Valid()&&active.thread==GetCurrentThreadId()&&
       !(active==g_lease.call.session)&&SeymourSession::Current(active,true)){
        // A confirmed different save invalidates this lease. Retire only its
        // bookkeeping, never restore an old menu image into the new session.
        // Pending loads and foreign threads cannot take this retirement path.
        g_lease.owned=false;
        return;
    }
    const auto result=Restore(AdapterIo(g_lease.call),g_lease.call.expectedRoster,
                              g_lease.call.before,g_lease.call.planned);
    if(result==Result::Restored)g_lease.owned=false;
    else g_phase.store(Phase::StopPending,std::memory_order_release);
}
void __cdecl ConstructorShim(unsigned mode){
    g_callbacks.fetch_add(1,std::memory_order_acq_rel);
    Call call{};call.error=GetLastError();call.sequence=NextSequence();
    const bool held=!g_busy.test_and_set(std::memory_order_acquire);
    __try {
        const bool admitted=held&&Begin(call);
        const auto original=reinterpret_cast<void(__cdecl*)(unsigned)>(g_original.load(std::memory_order_acquire));
        bool completed=false;
        SetLastError(call.error);
        __try {if(original){original(mode);completed=true;}}
        __finally {call.error=GetLastError();}
        if(held&&completed){
            // A successful original constructor owns the newly built menu. It
            // retires our previous image even when this invocation is OFF.
            g_lease.owned=false;
            if(admitted&&Current(&call,false)){
                const auto result=Extend(mode,AdapterIo(call));
                if(result==Result::Applied)g_applied.fetch_add(1,std::memory_order_relaxed);
                else if(result!=Result::Unchanged)g_rejected.fetch_add(1,std::memory_order_relaxed);
                if(call.attempted&&(result==Result::Applied||result==Result::RestorePending)){
                    g_lease.call=call;g_lease.owned=true;
                }
                if(result==Result::RestorePending)FailClosed();
            }
        }
    } __finally {
        if(held)g_busy.clear(std::memory_order_release);
        g_callbacks.fetch_sub(1,std::memory_order_release);SetLastError(call.error);
    }
}
} // namespace

void Start(std::uintptr_t base,bool validateOnly,void(*log)(const char*)){
    if(g_admin.test_and_set(std::memory_order_acquire))return;
    if(g_attempted.exchange(true,std::memory_order_acq_rel)||g_stopping.load(std::memory_order_acquire)){
        g_admin.clear(std::memory_order_release);return;
    }
    if(validateOnly||!Configured()){g_admin.clear(std::memory_order_release);return;}
    if(!N::Profile(base)||!N::Match(base,Proof)||!N::Pin(reinterpret_cast<const void*>(&Start))||
       !SeymourSession::PublisherReady()){
        g_phase.store(Phase::Unavailable);g_admin.clear(std::memory_order_release);return;
    }
    g_base=base;(void)g_control.Publish(Requested());
    void* original=nullptr;
    if(!g_batch.Add(base+ConstructorRva,reinterpret_cast<void*>(&ConstructorShim),&original)){
        g_phase.store(g_batch.DiscardUnpublished()?Phase::Unavailable:Phase::StopPending);
    }else{
        g_original.store(original,std::memory_order_release);
        if(!g_batch.Publish(MinHookBatch::Owner::SeymourMenuList,reinterpret_cast<const void*>(&Start))){
            g_phase.store(g_batch.Neutralize()?Phase::Unavailable:Phase::StopPending);
        }else{
            g_ready.store(true,std::memory_order_release);
            if(g_stopping.load(std::memory_order_acquire)||!g_control.Publish(Requested())){
                g_ready.store(false,std::memory_order_release);
                g_phase.store(g_batch.Neutralize()?Phase::Stopped:Phase::StopPending);
            }else{
                g_phase.store(Phase::Installed,std::memory_order_release);
                if(log)log("[seymour-menu] native eight-character list adapter installed; active-save ownership required; default OFF\n");
            }
        }
    }
    g_admin.clear(std::memory_order_release);
}
void PresentTick(){(void)g_control.Publish(Requested());}
void PumpTick(){
    if(g_busy.test_and_set(std::memory_order_acquire))return;
    const DWORD incoming=GetLastError();
    if(!g_ready.load(std::memory_order_acquire)||!g_control.Current(g_control.Read()))RestoreOwned();
    g_busy.clear(std::memory_order_release);SetLastError(incoming);
}
void RequestStop() noexcept {
    g_stopping.store(true,std::memory_order_release);g_control.Stop();g_ready.store(false,std::memory_order_release);
}
bool Remove(){
    RequestStop();
    if(g_admin.test_and_set(std::memory_order_acquire))return false;
    if(g_busy.test_and_set(std::memory_order_acquire)){g_admin.clear(std::memory_order_release);return false;}
    const DWORD incoming=GetLastError();RestoreOwned();
    const bool done=!g_lease.owned&&g_callbacks.load(std::memory_order_acquire)==0&&g_batch.Neutralize();
    g_phase.store(done?Phase::Stopped:Phase::StopPending,std::memory_order_release);
    g_busy.clear(std::memory_order_release);g_admin.clear(std::memory_order_release);SetLastError(incoming);return done;
}
void MenuLabel(char* output,std::size_t size){
    if(!output||!size)return;
    const auto value=Config::ReadIntExact(Key,0,1);
    std::snprintf(output,size,"Eight-character menu list: %s%s",value.state==Config::IntReadState::Invalid?"INVALID":
        value.value==1?"ON":"OFF",value.value==1&&!g_ready.load()?" - restart required":"");
}
bool MenuAction(){
    const auto value=Config::ReadIntExact(Key,0,1);
    const int next=value.state==Config::IntReadState::Valid&&value.value==0?1:0;
    if(!Config::SetInt(Key,next))return false;
    PresentTick();const auto observed=Config::ReadIntExact(Key,0,1);
    return observed.state==Config::IntReadState::Valid&&observed.value==next;
}
void Detail(char* output,std::size_t size){
    if(!output||!size)return;
    std::snprintf(output,size,"Menu list state %u; applied %u; rejected %u; owned cleanup on game thread",
        static_cast<unsigned>(g_phase.load()),g_applied.load(),g_rejected.load());
}
} // namespace FfxHooks::SeymourMenuList
