#include "SeymourPersistentRosterHook.h"
#include "SeymourPersistentRosterCore.h"
#include "SeymourBattleCore.h"
#include "SeymourSessionRuntime.h"
#include "SeymourOverdriveControl.h"
#include "RecoveryNative.h"
#include "F8FlagCatalog.h"
#include "../shared/Config.h"
#include <atomic>
#include <cstdio>

namespace FfxHooks::SeymourPersistentRoster {
namespace {
namespace N=RecoveryNative;
constexpr char Key[]="seymour.permanent_roster";
constexpr std::uintptr_t PartyRva=0xd32494,FrontRva=0xd307e8,ReserveRva=0xd307eb;
constexpr std::uintptr_t BattleRva=0xd2a8e0,AssignRva=0x386a70;
constexpr std::uint8_t AssignPrefix[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x81,0xe3,0xff,0,0,0};
const RecoveryEvidence::Proof AssignProof{AssignRva,AssignPrefix,sizeof(AssignPrefix),nullptr,0};
std::atomic<std::uintptr_t> base{0};
std::atomic<bool> attempted{false},ready{false},stopping{false};
std::atomic_flag busy=ATOMIC_FLAG_INIT;
std::atomic<Outcome> status{Outcome::Off};
SeymourOverdrive::Control control;
Lease lease;
bool Configured(){const auto v=Config::ReadIntExact(Key,0,1);return v.state==Config::IntReadState::Valid&&v.value==1;}
bool Requested(){const auto* f=FindF8Flag("boosters.playable_seymour");return Configured()&&f&&ResolveF8Flag(*f).value;}
bool Read(std::uintptr_t at,void* output,std::size_t size) noexcept {
    return N::Range(at,size,base.load())&&N::Copy(output,reinterpret_cast<const void*>(at),size);
}
bool ImageRead(void*,Image& out){
    Image image{};
    if(!Read(base+PartyRva,&image.party,1)||!Read(base+FrontRva,image.front.data(),image.front.size())||
       !Read(base+ReserveRva,image.reserve.data(),image.reserve.size()))return false;
    out=image;return true;
}
struct Call {std::uint64_t request=0;DWORD error=0;};
bool Session(void*,bool cleanup,Token& token){token=SeymourSession::Capture(cleanup);return token.Valid();}
bool Current(void* p,const Token& token,bool cleanup){
    const auto& c=*static_cast<Call*>(p);std::uint8_t battle=1;
    return (cleanup||(ready.load(std::memory_order_acquire)&&control.Current(c.request)))&&
        SeymourSession::Current(token,cleanup)&&Read(base+BattleRva,&battle,1)&&battle==0&&
        // The memory query may run a revocation callback. Never authorize a
        // native assignment with a token checked only before that query.
        SeymourSession::Current(token,cleanup)&&
        (cleanup||(ready.load(std::memory_order_acquire)&&control.Current(c.request)));
}
bool Assign(void* p,bool on){
    auto& c=*static_cast<Call*>(p);SetLastError(c.error);
    __try {(void)reinterpret_cast<int(__cdecl*)(std::uint8_t,int)>(base+AssignRva)(7,on?1:0);}
    __finally {c.error=GetLastError();}
    return true;
}
}
void Start(std::uintptr_t module,bool validateOnly,void(*log)(const char*)){
    if(attempted.exchange(true,std::memory_order_acq_rel)||stopping.load(std::memory_order_acquire))return;
    if(validateOnly||!Configured())return;
    if(!N::Profile(module)||!N::Match(module,AssignProof)||!SeymourSession::PublisherReady()||
       !N::Pin(reinterpret_cast<const void*>(&Start))){status.store(Outcome::Rejected);return;}
    if(!SeymourBattle::RegisterPermanentRosterProvider(&BattleRevision)){
        status.store(Outcome::Rejected);return;
    }
    base=module;(void)control.Publish(Requested());ready.store(true,std::memory_order_release);
    if(stopping.load(std::memory_order_acquire)){ready.store(false);return;}
    if(log)log("[seymour-roster] native field-pump service ready; active-load owner required; default OFF\n");
}
void PumpTick(){
    if(!base.load()||busy.test_and_set(std::memory_order_acquire))return;
    Call call{};call.error=GetLastError();call.request=control.Read();
    bool completed=false;
    // Withdraw the previous save's publication before invoking a native writer.
    // The provider can be queried reentrantly and an assignment can throw after
    // changing memory. Only the completed readback may publish a settled status.
    status.store(Outcome::Rejected,std::memory_order_release);
    __try {
        const bool requested=ready.load(std::memory_order_acquire)&&control.Current(call.request);
        status.store(lease.Update(requested,{&call,Session,Current,ImageRead,Assign}),std::memory_order_release);
        completed=true;
    } __finally {
        if(!completed)status.store(lease.Owned()?Outcome::RestorePending:Outcome::Rejected,std::memory_order_release);
        busy.clear(std::memory_order_release);SetLastError(call.error);
    }
}
std::uint64_t BattleRevision() noexcept {
    const DWORD incoming=GetLastError();
    struct RestoreError {DWORD value;~RestoreError(){SetLastError(value);}} restoreError{incoming};
    const auto request=control.Read();
    if(!ready.load(std::memory_order_acquire)||!control.Current(request))return 0;
    const auto settled=[](){
        const auto value=status.load(std::memory_order_acquire);
        return value==Outcome::Applied||value==Outcome::Owned||value==Outcome::Borrowed;
    };
    // Installed is not applied: a partial or thrown native assignment has no
    // verified roster lease until the owner pump confirms its resulting state.
    if(!settled())return 0;
    const auto token=SeymourSession::Capture();Image image{};
    return token.Valid()&&ImageRead(nullptr,image)&&Present(image)&&
        SeymourSession::Current(token)&&control.Current(request)&&ready.load(std::memory_order_acquire)&&settled()?token.revision:0;
}
bool BattleReady() noexcept {return BattleRevision()!=0;}
void PresentTick(){(void)control.Publish(Requested());}
void RequestStop() noexcept {stopping.store(true,std::memory_order_release);control.Stop();ready.store(false,std::memory_order_release);}
bool Remove(){
    RequestStop();PumpTick();
    if(busy.test_and_set(std::memory_order_acquire))return false;
    const bool done=!lease.Owned();busy.clear(std::memory_order_release);return done;
}
void MenuLabel(char* out,std::size_t size){
    if(!out||!size)return;const auto value=Config::ReadIntExact(Key,0,1);
    std::snprintf(out,size,"Permanent field roster: %s%s",value.state==Config::IntReadState::Invalid?"INVALID":
        value.value==1?"ON":"OFF",value.value==1&&!ready.load()?" - restart required":"");
}
bool MenuAction(){
    const auto value=Config::ReadIntExact(Key,0,1);const int next=value.state==Config::IntReadState::Valid&&value.value==0?1:0;
    if(!Config::SetInt(Key,next))return false;PresentTick();const auto after=Config::ReadIntExact(Key,0,1);
    return after.state==Config::IntReadState::Valid&&after.value==next;
}
void Detail(char* out,std::size_t size){
    if(!out||!size)return;
    const char* text="waiting for active load and field pump";
    switch(status.load(std::memory_order_acquire)){
        case Outcome::Off:text="OFF";break;
        case Outcome::Applied:case Outcome::Owned:text="own permanent roster active";break;
        case Outcome::Borrowed:text="existing Seymour retained; not owned";break;
        case Outcome::Restored:text="own roster addition restored";break;
        case Outcome::RestorePending:text="restore pending; session/ownership changed";break;
        default:break;
    }
    std::snprintf(out,size,"Roster: %s",text);
}
}
