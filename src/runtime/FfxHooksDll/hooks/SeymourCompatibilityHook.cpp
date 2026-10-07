#include "../shared/ExecutableProfile.h"
#include "SeymourCompatibilityHook.h"
#include "SeymourCompatibilityService.h"
#include "RecoveryNative.h"
#include "F7InLive.h"
#include "../shared/Config.h"
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace FfxHooks::SeymourCompatibility {
namespace {
namespace N=RecoveryNative;
constexpr const char* Keys[]={"seymour.command_safety","seymour.gear_visibility"};
constexpr std::uint32_t QueryRva=(::FfxHooks::ExecutableProfile::Rva<0x39a5c0>()),VisibilityRva=(::FfxHooks::ExecutableProfile::Rva<0x3ad5f0>());
constexpr std::uint32_t ActorTableRva=(::FfxHooks::ExecutableProfile::Rva<0xd334cc>()),PlayerRva=(::FfxHooks::ExecutableProfile::Rva<0xd3205c>()),InventoryRva=(::FfxHooks::ExecutableProfile::Rva<0xd30f2c>());
constexpr std::uint8_t QueryBytes[]={0x55,0x8b,0xec,0x51,0x8b,0x4d,0x0c,0x53,0x56,0x8b,0xc1,0x8b,0xf1,0x81,0xe6,0xff,0x0f,0,0,0x33,0xdb};
constexpr std::uint8_t VisibilityBytes[]={0x55,0x8b,0xec,0x57,0x8b,0x7d,0x08,0x81,0xe7,0xff,0,0,0,0x83,0xff,7,0x7d,0x48,0x69,0xff,0x94,0,0,0,0x53,0x8b,0x5d,0x0c,0x56};
constexpr RecoveryEvidence::Proof QueryProof{QueryRva,QueryBytes,sizeof(QueryBytes),nullptr,0};
constexpr RecoveryEvidence::Proof VisibilityProof{VisibilityRva,VisibilityBytes,sizeof(VisibilityBytes),nullptr,0};
using QueryFn=int(__cdecl*)(int,std::uint32_t);
using VisibilityFn=void(__cdecl*)(int,std::uint8_t);
std::atomic<void*> g_query{nullptr},g_visibility{nullptr};
std::atomic<unsigned> g_installed{0},g_requested{0},g_callbacks{0},g_filtered{0},g_changed{0};
std::atomic<InstallState> g_state{InstallState::NotStarted};
std::atomic<bool> g_stopping{false},g_pending{false},g_attempted{false};
std::atomic_flag g_busy=ATOMIC_FLAG_INIT;
std::uintptr_t g_base=0;
N::OwnedBatch g_batch;
VisibilityLease g_lease;
void (*g_log)(const char*)=nullptr;

unsigned Configured() {
    unsigned value=0;
    for(unsigned i=0;i<2;++i){
        const auto read=Config::ReadIntExact(Keys[i],0,1);
        if(read.state==Config::IntReadState::Valid&&read.value==1)value|=1u<<i;
    }
    return value;
}
bool Enabled(unsigned bit) noexcept {
    return !g_stopping.load(std::memory_order_acquire)&&
           (g_installed.load(std::memory_order_acquire)&g_requested.load(std::memory_order_acquire)&bit)!=0;
}
bool Read(std::uintptr_t address,void* out,std::size_t size,std::uintptr_t image=0) noexcept {
    return N::Range(address,size,image)&&N::Copy(out,reinterpret_cast<const void*>(address),size);
}
bool ActorTable(std::uint32_t& table) noexcept {
    if(!g_base||!Read(g_base+ActorTableRva,&table,sizeof(table),g_base)||!table||
       table>UINT32_MAX-8u*0xf90u)return false;
    std::uint8_t active=0;
    return Read(table+7u*0xf90u+0xdc8u,&active,1)&&active!=0;
}
struct Context {
    bool cleanup=false,captured=false;
    Pair before{};
    std::uint32_t actorTable=0;
    DWORD incoming=0,nativeError=0;
    int nativeResult=1;
};
bool Capture(Context& context,Scope& scope,unsigned bit) noexcept {
    if(!context.cleanup&&!Enabled(bit))return false;
    std::uint32_t table=0;
    if(!CaptureSeymourCompatibilityScope(&scope,context.cleanup)||!ActorTable(table))return false;
    if(context.actorTable&&table!=context.actorTable)return false;
    context.actorTable=table;
    scope.actorTable=table;
    return true;
}
bool QueryCapture(void* p,Scope& scope){return Capture(*static_cast<Context*>(p),scope,kCommandSafety);}
int QueryOriginal(void* p,int actor,std::uint32_t command) {
    auto& context=*static_cast<Context*>(p);
    const auto original=reinterpret_cast<QueryFn>(g_query.load(std::memory_order_acquire));
    SetLastError(context.incoming);
    int result=1;
    __try {result=original?original(actor,command):1;}
    __finally {context.nativeError=GetLastError();}
    context.nativeResult=result;return result;
}
void VisibilityOriginalCall(void* p,int actor,std::uint8_t enable) {
    auto& context=*static_cast<Context*>(p);
    const auto original=reinterpret_cast<VisibilityFn>(g_visibility.load(std::memory_order_acquire));
    SetLastError(context.incoming);
    __try {if(original)original(actor,enable);}
    __finally {context.nativeError=GetLastError();}
}
bool ReadPair(void* p,Pair& output) {
    auto& context=*static_cast<Context*>(p);Pair candidate{};Scope after{};
    if(!Capture(context,candidate.scope,kGearVisibility)||
       !Read(g_base+PlayerRva+7u*0x94u+0x2du,candidate.slots.data(),2,g_base))return false;
    for(unsigned i=0;i<2;++i){
        if(candidate.slots[i]==255)continue;
        if(candidate.slots[i]>=kInventory||!Read(g_base+InventoryRva+candidate.slots[i]*kGearBytes,
            candidate.gear[i].data(),kGearBytes,g_base))return false;
    }
    if(!candidate.Valid()||!Capture(context,after,kGearVisibility)||!(candidate.scope==after))return false;
    output=candidate;
    if(!context.captured){context.before=candidate;context.captured=true;}
    return true;
}
bool Current(void* p,const Pair& before) {
    auto& context=*static_cast<Context*>(p);Scope now{};std::array<std::uint8_t,2> slots{};
    return Capture(context,now,kGearVisibility)&&now==before.scope&&
           Read(g_base+PlayerRva+7u*0x94u+0x2du,slots.data(),slots.size(),g_base)&&slots==before.slots;
}
bool CompareFlag(void* p,unsigned which,const Gear& expected,std::uint8_t desired) {
    auto& context=*static_cast<Context*>(p);
    if(!context.captured||which>=2||context.before.slots[which]>=kInventory||
       !Current(p,context.before))return false;
    const auto address=g_base+InventoryRva+context.before.slots[which]*kGearBytes;
    Gear current{};
    if(!N::Range(address,kGearBytes,g_base,false,true)||!Read(address,current.data(),current.size(),g_base)||
       current!=expected||!Current(p,context.before))return false;
    // Owning-thread admission serializes engine metadata. CAS changes the one
    // flag byte without clobbering a simultaneous native flag-byte update.
    __try {
        return static_cast<std::uint8_t>(_InterlockedCompareExchange8(
            reinterpret_cast<volatile char*>(address+3),static_cast<char>(desired),
            static_cast<char>(expected[3])))==expected[3];
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
Io Operations(Context& context){return {&context,&ReadPair,&Current,&CompareFlag};}

int __cdecl QueryShim(int actor,std::uint32_t command) {
    g_callbacks.fetch_add(1,std::memory_order_acq_rel);
    Context context{};context.incoming=context.nativeError=GetLastError();
    int result=1;
    __try {
        result=ServiceQuery(Enabled(kCommandSafety),actor,command,{&context,&QueryOriginal,&QueryCapture});
        if(result!=context.nativeResult)g_filtered.fetch_add(1,std::memory_order_relaxed);
    } __finally {
        SetLastError(context.nativeError);
        g_callbacks.fetch_sub(1,std::memory_order_release);
    }
    return result;
}
void __cdecl VisibilityShim(int actor,std::uint8_t enable) {
    g_callbacks.fetch_add(1,std::memory_order_acq_rel);
    Context edit{},cleanup{};cleanup.cleanup=true;
    edit.incoming=edit.nativeError=GetLastError();
    const bool held=actor==7&&!g_busy.test_and_set(std::memory_order_acquire);
    __try {
        if(!held)VisibilityOriginalCall(&edit,actor,enable);
        else {
            const auto result=g_lease.Apply(Enabled(kGearVisibility),actor,enable,
                {&edit,&VisibilityOriginalCall},Operations(edit),Operations(cleanup));
            g_pending.store(g_lease.Pending(),std::memory_order_release);
            if(result==Outcome::Applied)g_changed.fetch_add(1,std::memory_order_relaxed);
            if(result==Outcome::Partial)g_state.store(InstallState::RestorePending,std::memory_order_release);
        }
    } __finally {
        if(held)g_busy.clear(std::memory_order_release);
        SetLastError(edit.nativeError);
        g_callbacks.fetch_sub(1,std::memory_order_release);
    }
}
}

void Start(std::uintptr_t moduleBase,bool validateOnly,void (*log)(const char*)) {
    bool expected=false;
    if(!g_attempted.compare_exchange_strong(expected,true,std::memory_order_acq_rel))return;
    g_log=log;
    if(validateOnly){g_state.store(InstallState::ValidateOnly);return;}
    const unsigned mask=Configured();g_requested.store(mask,std::memory_order_release);
    if(!mask){g_state.store(InstallState::Off);return;}
    if(g_stopping.load()||!F7_SharedBattleRuntimeReady(moduleBase)||!N::Profile(moduleBase)||
       !N::Match(moduleBase,RecoveryEvidence::WardActor)||
       ((mask&kCommandSafety)&&!N::Match(moduleBase,QueryProof))||
       ((mask&kGearVisibility)&&!N::Match(moduleBase,VisibilityProof))){
        g_state.store(InstallState::Unavailable);return;
    }
    g_base=moduleBase;void* query=nullptr;void* visibility=nullptr;
    const bool prepared=(!(mask&kCommandSafety)||g_batch.Add(moduleBase+QueryRva,reinterpret_cast<void*>(&QueryShim),&query))&&
        (!(mask&kGearVisibility)||g_batch.Add(moduleBase+VisibilityRva,reinterpret_cast<void*>(&VisibilityShim),&visibility));
    if(!prepared){
        g_state.store(g_batch.DiscardUnpublished()?InstallState::Unavailable:InstallState::RestorePending);return;
    }
    g_query.store(query,std::memory_order_release);g_visibility.store(visibility,std::memory_order_release);
    if(!g_batch.Publish(MinHookBatch::Owner::SeymourCompatibility,reinterpret_cast<const void*>(&Start))){
        g_state.store(g_batch.Neutralize()?InstallState::Unavailable:InstallState::RestorePending);return;
    }
    g_installed.store(mask,std::memory_order_release);g_state.store(InstallState::Installed,std::memory_order_release);
    if(g_log)g_log("[seymour-compat] scoped native compatibility installed; independent controls default OFF\n");
}
void PresentTick(){g_requested.store(Configured(),std::memory_order_release);}
void RequestStop() noexcept {g_stopping.store(true,std::memory_order_release);}
bool RestoreAtBattleBoundary() {
    if(g_busy.test_and_set(std::memory_order_acquire))return false;
    Context cleanup{};cleanup.cleanup=true;const DWORD error=GetLastError();
    const bool result=g_lease.Restore(Operations(cleanup));
    g_pending.store(g_lease.Pending(),std::memory_order_release);
    g_busy.clear(std::memory_order_release);SetLastError(error);
    if(!result)g_state.store(InstallState::RestorePending,std::memory_order_release);
    return result;
}
bool Remove() {
    RequestStop();
    if(!RestoreAtBattleBoundary())return false;
    if(!g_batch.Neutralize()||g_callbacks.load(std::memory_order_acquire)){
        g_state.store(InstallState::RestorePending);return false;
    }
    g_installed.store(0,std::memory_order_release);g_state.store(InstallState::Stopped);return true;
}
Snapshot GetSnapshot() noexcept {
    const auto state=g_state.load(std::memory_order_acquire);
    return {state,g_installed.load(std::memory_order_acquire),g_requested.load(std::memory_order_acquire),
        g_filtered.load(std::memory_order_relaxed),g_changed.load(std::memory_order_relaxed),
        state==InstallState::RestorePending};
}
void MenuLabel(int row,char* out,std::size_t size) {
    if(!out||!size)return;
    if(row<0||row>=MenuCount()){out[0]=0;return;}
    if(row==2){Detail(out,size);return;}
    const auto configured=Config::ReadIntExact(Keys[row],0,1);
    const auto snapshot=GetSnapshot();
    std::snprintf(out,size,"%s: %s%s",row==0?"Unsupported command guard":"Equipment visibility",
        configured.state==Config::IntReadState::Invalid?"INVALID":configured.value==1?"ON":"OFF",
        configured.value==1&&!(snapshot.installed&(1u<<row))?" - restart required":"");
}
bool MenuAction(int row) {
    if(row<0||row>=2)return false;
    const auto before=Config::ReadIntExact(Keys[row],0,1);
    const int next=before.state==Config::IntReadState::Invalid?0:before.value==1?0:1;
    if(!Config::SetInt(Keys[row],next))return false;
    const auto after=Config::ReadIntExact(Keys[row],0,1);PresentTick();
    return after.state==Config::IntReadState::Valid&&after.value==next;
}
void Detail(char* out,std::size_t size) {
    if(!out||!size)return;
    const auto s=GetSnapshot();
    const char* state="not started";
    switch(s.state){
        case InstallState::Off:state="OFF at startup; enable and restart";break;
        case InstallState::ValidateOnly:state="validation only; no hooks";break;
        case InstallState::Unavailable:state="profile, signature or dependency unavailable";break;
        case InstallState::Installed:state="requires owned Seymour battle";break;
        case InstallState::RestorePending:state="RESTORE PENDING";break;
        case InstallState::Stopped:state="stopped; restart required";break;
        default:break;
    }
    std::snprintf(out,size,"Hooks %u; filtered %u; gear %u; %s",s.installed,s.filtered,s.visibilityChanges,state);
}
} // namespace FfxHooks::SeymourCompatibility
