#include "SeymourGearSortHook.h"
#include "SeymourGearSortCore.h"
#include "SeymourGearWithinCore.h"
#include "SeymourOverdriveControl.h"
#include "RecoveryNative.h"
#include "RonsoPoolRuntime.h"
#include "SeymourSessionRuntime.h"
#include "EquipmentWorkshopRuntime.h"
#include "F8FlagCatalog.h"
#include "../shared/Config.h"
#include <atomic>
#include <cstdio>

namespace FfxHooks::SeymourGearSort {
namespace {
namespace N=RecoveryNative;
constexpr char KeyName[]="seymour.gear_sorting";
constexpr std::uint32_t SwapRva=0x3aba10,CountRva=0x4c1ba0,WithinRva=0x4c9c10;
constexpr std::uint32_t RefreshRva=0x4c94b0,CountsRva=0x146a3a4;
constexpr std::uint32_t RowsRva=0x1197730,GearRva=0xd30f2c,PlayerRva=0xd3205c,BattleRva=0xd2a8e0;
// Verified instruction boundaries from the exact 78ce... image. These are entry
// signatures, not a claim that a prefix proves every caller or entire function.
constexpr std::uint8_t GroupEntry[]={0x55,0x8b,0xec,0x81,0xec,0x20,0x04,0,0};
constexpr std::uint8_t WithinEntry[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x33,0xdb,0xbe,0xa4,0xa3,0x86,0x01};
constexpr std::uint16_t WithinRelocations[]={10};
constexpr std::uint8_t RefreshEntry[]={0x55,0x8b,0xec,0x51,0x56,0x57,0xe8,0xe5,0x86,0xff,0xff};
constexpr std::uint8_t GroupHelperEntry[]={0x55,0x8b,0xec,0xb8,0x34,0x19,0,0,0xe8,0x03,0xfa,0x07,0};
constexpr RecoveryEvidence::Proof HelperProofs[]={
    {RefreshRva,RefreshEntry,sizeof(RefreshEntry),nullptr,0},
    {WithinRva,GroupHelperEntry,sizeof(GroupHelperEntry),nullptr,0}};
constexpr RecoveryEvidence::Proof Proofs[]={
    {0x4c9f80,GroupEntry,sizeof(GroupEntry),nullptr,0},
    {0x4ca180,GroupEntry,sizeof(GroupEntry),nullptr,0},
    {0x4c9bc0,WithinEntry,sizeof(WithinEntry),WithinRelocations,1}};
enum class Phase:unsigned {NotStarted,Off,Unavailable,Installed,Stopped,StopPending,Partial};
std::atomic<Phase> g_phase{Phase::NotStarted};
std::atomic<bool> g_attempted{false},g_ready{false};
std::atomic<unsigned> g_callbacks{0},g_sorted{0},g_rejected{0};
std::atomic_flag g_admin=ATOMIC_FLAG_INIT,g_busy=ATOMIC_FLAG_INIT;
std::array<std::atomic<void*>,3> g_original{};
std::uintptr_t g_base=0;
SeymourOverdrive::Control g_control;
N::OwnedBatch g_batch;

bool Configured(){const auto v=Config::ReadIntExact(KeyName,0,1);return v.state==Config::IntReadState::Valid&&v.value==1;}
bool Requested(){const auto* f=FindF8Flag("boosters.playable_seymour");return Configured()&&f&&ResolveF8Flag(*f).value;}
bool Read(std::uintptr_t at,void* out,std::size_t size){return N::Range(at,size,g_base)&&N::Copy(out,reinterpret_cast<const void*>(at),size);}
bool WorkshopCurrent(unsigned thread){
    const auto status=EquipmentWorkshop::Status();
    return status.code==EquipmentWorkshop::RuntimeCode::Disabled||
        (status.code==EquipmentWorkshop::RuntimeCode::Ready&&status.ownerThread==thread&&EquipmentWorkshop::Requested());
}
struct Call {
    std::uint64_t token=0,generation=0;
    unsigned thread=0;
    DWORD incoming=0,error=0;
};
bool Current(void* p,const Snapshot& expected){
    const auto& c=*static_cast<Call*>(p);std::uint8_t battle=1;
    return g_ready.load(std::memory_order_acquire)&&g_control.Current(c.token)&&
        RonsoPool::IsSaveIoReady()&&SeymourSession::Current({c.generation,c.thread})&&
        expected.generation==c.generation&&expected.thread==c.thread&&GetCurrentThreadId()==c.thread&&
        Read(g_base+BattleRva,&battle,1)&&!battle&&WorkshopCurrent(c.thread)&&
        // Memory/ownership queries may reenter control callbacks. Revalidate
        // after those queries; an earlier token check cannot authorize the write.
        RonsoPool::IsSaveIoReady()&&SeymourSession::Current({c.generation,c.thread})&&
        GetCurrentThreadId()==c.thread&&g_control.Current(c.token)&&g_ready.load(std::memory_order_acquire);
}
bool Capture(void* p,Snapshot& out){
    const auto& c=*static_cast<Call*>(p);Snapshot s{};s.generation=c.generation;s.thread=c.thread;
    if(!Current(p,s))return false;
    const auto count=reinterpret_cast<int(__cdecl*)()>(g_base+CountRva)();
    if(count<0||count>static_cast<int>(Capacity))return false;
    s.count=static_cast<unsigned>(count);
    if((s.count&&!Read(g_base+RowsRva,s.rows.data(),s.count*4))||!Read(g_base+GearRva,s.gear.data(),sizeof(s.gear)))return false;
    for(unsigned i=0;i<Players;++i)if(!Read(g_base+PlayerRva+i*0x94+0x2d,s.equipped.data()+i*Types,Types))return false;
    if(!Valid(s)||!Current(p,s))return false;
    out=s;return true;
}
bool NativeSwap(void* p,std::uint16_t a,std::uint16_t b){
    auto& c=*static_cast<Call*>(p);Snapshot marker{};marker.generation=c.generation;marker.thread=c.thread;
    if(!Current(p,marker))return false;
    // Invoke the entry, not its trampoline: Workshop observes this permutation
    // and updates its item identities and fifth-slot/refinement sidecar once.
    SetLastError(c.error);
    __try {(void)reinterpret_cast<int(__cdecl*)(unsigned,unsigned)>(g_base+SwapRva)(a,b);}
    __finally {c.error=GetLastError();}
    return true; // Full post-swap byte/index verification is performed by Apply.
}
bool Begin(Call& call){
    call.token=g_control.Read();
    if(!g_ready.load(std::memory_order_acquire)||!g_control.Current(call.token)||!RonsoPool::IsSaveIoReady())return false;
    // The completed native copy, not the first sorting caller or a file preview,
    // owns this thread and save identity.
    const auto session=SeymourSession::Capture();
    if(!session.Valid())return false;
    call.generation=session.revision;call.thread=session.thread;
    Snapshot marker{};marker.generation=call.generation;marker.thread=call.thread;return Current(&call,marker);
}
void FailClosed() noexcept {
    // A partial native mutation is not recoverable by toggling OFF/ON. Keep the
    // installed forwarding gateways, but never start another custom permutation.
    g_control.Stop();g_ready.store(false,std::memory_order_release);
    g_phase.store(Phase::Partial,std::memory_order_release);
}
bool NativeRefresh(void* p){
    auto& c=*static_cast<Call*>(p);Snapshot marker{};marker.generation=c.generation;marker.thread=c.thread;
    if(!Current(p,marker))return false;
    SetLastError(c.error);
    __try {reinterpret_cast<void(__cdecl*)()>(g_base+RefreshRva)();}
    __finally {c.error=GetLastError();}
    return true;
}
bool ReadNativeCounts(void* p,NativeCounts& output){
    const auto& c=*static_cast<Call*>(p);Snapshot marker{};marker.generation=c.generation;marker.thread=c.thread;
    return Current(p,marker)&&Read(g_base+CountsRva,output.data(),sizeof(output))&&Current(p,marker);
}
bool NativeGroup(void* p,unsigned start,unsigned count){
    auto& c=*static_cast<Call*>(p);Snapshot marker{};marker.generation=c.generation;marker.thread=c.thread;
    if(start>Capacity||count>Capacity-start||!Current(p,marker))return false;
    SetLastError(c.error);
    __try {reinterpret_cast<void(__cdecl*)(void*,int,unsigned)>(g_base+WithinRva)(
        reinterpret_cast<void*>(g_base+RowsRva),static_cast<int>(start),count);}
    __finally {c.error=GetLastError();}
    return true;
}
WithinIo GroupIo(Call& call){return {&call,Capture,Current,NativeRefresh,ReadNativeCounts,NativeGroup};}
bool RefreshAfterGrouping(Call& call){
    Snapshot expected{};Counts counts{};const auto io=GroupIo(call);
    // Owner-only grouping need not be type-grouped yet, so do not require
    // WithinReady here. Refresh the original fourteen counters, never sixteen.
    return Capture(&call,expected)&&Count(expected,counts)&&ReadExact(expected,io)&&
        NativeRefresh(&call)&&ReadExact(expected,io)&&NativeCountsMatch(counts,io)&&Current(&call,expected);
}
int Run(unsigned index,Order order){
    g_callbacks.fetch_add(1,std::memory_order_acq_rel);Call call{};
    call.incoming=call.error=GetLastError();
    const bool held=!g_busy.test_and_set(std::memory_order_acquire);
    int result=0;bool custom=false,completed=false;
    __try {
        if(!held){g_rejected.fetch_add(1);result=0;} // Never recurse into an in-progress permutation.
        else if(!Begin(call)){
            const auto original=reinterpret_cast<int(__cdecl*)()>(g_original[index].load(std::memory_order_acquire));
            SetLastError(call.incoming);
            __try {result=original?original():0;} __finally {call.error=GetLastError();}
        }else{
            custom=true;
            const auto value=Apply(true,order,{&call,Capture,Current,NativeSwap});
            result=value==Result::Applied||value==Result::Unchanged;
            if(result&&!RefreshAfterGrouping(call)){result=0;FailClosed();}
            if(result)g_sorted.fetch_add(1);else g_rejected.fetch_add(1);
            if(value==Result::Partial)FailClosed();
            completed=true;
        }
    } __finally {
        if(custom&&!completed)FailClosed();
        if(held)g_busy.clear(std::memory_order_release);
        g_callbacks.fetch_sub(1,std::memory_order_release);SetLastError(call.error);
    }
    return result;
}
int __cdecl OwnerShim(){return Run(0,Order::Owner);}
int __cdecl TypeShim(){return Run(1,Order::OwnerAndType);}
void __cdecl WithinShim(){
    g_callbacks.fetch_add(1,std::memory_order_acq_rel);Call c{};c.incoming=c.error=GetLastError();
    const bool held=!g_busy.test_and_set(std::memory_order_acquire);
    bool custom=false,completed=false;
    __try {
        if(held){
            if(!Begin(c)){
                const auto original=reinterpret_cast<void(__cdecl*)()>(g_original[2].load(std::memory_order_acquire));
                SetLastError(c.incoming);
                __try {if(original)original();} __finally {c.error=GetLastError();}
            }else{
                custom=true;
                // Replace only the native fourteen-group dispatcher. The same
                // native sorter handles all sixteen groups and keeps its Swap
                // entry (including Workshop); no second private inventory writer.
                const auto value=SortWithin(true,GroupIo(c));
                if(value==Result::Applied||value==Result::Unchanged)g_sorted.fetch_add(1);
                else g_rejected.fetch_add(1);
                if(value==Result::Partial)FailClosed();
                completed=true;
            }
        }else g_rejected.fetch_add(1);
    } __finally {
        if(custom&&!completed)FailClosed();
        if(held)g_busy.clear(std::memory_order_release);
        g_callbacks.fetch_sub(1,std::memory_order_release);SetLastError(c.error);
    }
}
}
void Start(std::uintptr_t base,bool validateOnly,void(*log)(const char*)){
    if(g_admin.test_and_set(std::memory_order_acquire))return;
    if(g_attempted.exchange(true)){g_admin.clear(std::memory_order_release);return;}
    if(validateOnly||!Configured()){g_phase.store(Phase::Off);g_admin.clear(std::memory_order_release);return;}
    bool accepted=SeymourSession::PublisherReady()&&RonsoPool::IsSaveIoReady()&&N::Profile(base);
    for(const auto& proof:Proofs)accepted=accepted&&N::Match(base,proof);
    for(const auto& proof:HelperProofs)accepted=accepted&&N::Match(base,proof);
    for(const auto rva:{SwapRva,CountRva,WithinRva})accepted=accepted&&N::Range(base+rva,1,base,true);
    if(!accepted||!g_control.Publish(Requested())){g_phase.store(Phase::Unavailable);g_admin.clear(std::memory_order_release);return;}
    g_base=base;void* shims[]={reinterpret_cast<void*>(&OwnerShim),reinterpret_cast<void*>(&TypeShim),reinterpret_cast<void*>(&WithinShim)};
    bool prepared=true;
    for(unsigned i=0;i<3&&prepared;++i){void* original=nullptr;prepared=g_batch.Add(base+Proofs[i].rva,shims[i],&original);g_original[i].store(original,std::memory_order_release);}
    if(!prepared)g_phase.store(g_batch.DiscardUnpublished()?Phase::Unavailable:Phase::StopPending);
    else if(!g_batch.Publish(MinHookBatch::Owner::SeymourGearSort,reinterpret_cast<const void*>(&Start)))
        g_phase.store(g_batch.Neutralize()?Phase::Unavailable:Phase::StopPending);
    else{
        g_ready.store(true,std::memory_order_release);g_phase.store(Phase::Installed);
        if(!g_control.Publish(Requested())){g_ready.store(false);g_phase.store(g_batch.Neutralize()?Phase::Stopped:Phase::StopPending);}
        else if(log)log("[seymour-sort] eight-owner sorting installed; existing native swap/Workshop path retained; default OFF\n");
    }
    g_admin.clear(std::memory_order_release);
}
void PresentTick(){(void)g_control.Publish(Requested());}
void RequestStop() noexcept {g_control.Stop();g_ready.store(false,std::memory_order_release);}
bool Remove(){
    RequestStop();if(g_admin.test_and_set(std::memory_order_acquire))return false;
    const bool done=g_batch.Neutralize()&&g_callbacks.load(std::memory_order_acquire)==0;
    g_phase.store(done?Phase::Stopped:Phase::StopPending);g_admin.clear(std::memory_order_release);return done;
}
void MenuLabel(char* out,std::size_t size){if(!out||!size)return;const auto v=Config::ReadIntExact(KeyName,0,1);
    std::snprintf(out,size,"Eight-owner equipment sorting: %s%s",v.state==Config::IntReadState::Invalid?"INVALID":v.value==1?"ON":"OFF",v.value==1&&!g_ready.load()?" - restart required":"");}
bool MenuAction(){const auto v=Config::ReadIntExact(KeyName,0,1);const int next=v.state==Config::IntReadState::Valid&&v.value==0?1:0;
    if(!Config::SetInt(KeyName,next))return false;PresentTick();const auto after=Config::ReadIntExact(KeyName,0,1);return after.state==Config::IntReadState::Valid&&after.value==next;}
void Detail(char* out,std::size_t size){if(out&&size)std::snprintf(out,size,"Sort state %u; complete %u; rejected %u; native swap path",static_cast<unsigned>(g_phase.load()),g_sorted.load(),g_rejected.load());}
}
