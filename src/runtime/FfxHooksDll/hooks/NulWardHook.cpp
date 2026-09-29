#include "NulWardHook.h"
#include "NulElementCommands.h"
#include "SharedNulRuntime.h"
#include "SharedActionRuntime.h"
#include "SharedActorRuntime.h"
#include "SharedBattleRuntime.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace FfxHooks {
namespace {
namespace Bus=CombatExtensions;
using Byte=unsigned char;
constexpr unsigned ActorCount=31,ResultsPerActor=32;
std::atomic<bool> running{false};
std::atomic<DWORD> ownerThread{0};
std::atomic<std::uint64_t> resetEpoch{1};
std::uintptr_t image=0;
bool applying=false;
std::atomic<bool> expanded{false};
std::uint64_t observedEpoch=0,nextSerial=0;
struct ActorState {
    Byte* pointer=nullptr;std::uint32_t file=0;std::uint16_t identity=0;
    std::uint64_t serial=0;unsigned charges=0;
};
struct ActionState {
    std::uint64_t serial=0,actor=0;unsigned index=0;
    std::array<Byte,72> row{};
    std::array<unsigned,ActorCount> granted{};
};
struct Reservation {
    std::uint64_t target=0,action=0;
    unsigned source=ActorCount,sub=4,command=0,mask=0;
};
std::array<ActorState,ActorCount> actors{};
std::array<ActionState,ActorCount> actions{};
std::array<Reservation,ActorCount*ResultsPerActor> reservations{};
bool Copy(void* out,const void* in,std::size_t size) noexcept {return NativeUiSupport::Copy(out,in,size);}
template<class T> bool Read(std::uintptr_t at,T& value) noexcept {
    return at>=0x10000&&at<=UINT32_MAX-sizeof(T)&&Copy(&value,reinterpret_cast<const void*>(at),sizeof(T));
}
bool Context() noexcept {
    if(!running.load())return false;
    DWORD empty=0;ownerThread.compare_exchange_strong(empty,GetCurrentThreadId());
    if(ownerThread.load()!=GetCurrentThreadId())return false;
    const auto epoch=resetEpoch.load();
    if(epoch!=observedEpoch){actors={};actions={};reservations={};observedEpoch=epoch;}
    return true;
}
std::uint64_t Serial() noexcept {
    if(nextSerial==UINT64_MAX){running=false;return 0;}return ++nextSerial;
}
void Retire(unsigned owner) noexcept {
    if(owner>=ActorCount)return;
    actors[owner]={};actions[owner]={};
    for(unsigned i=0;i<reservations.size();++i)
        if(i/ResultsPerActor==owner||reservations[i].source==owner)reservations[i]={};
}
ActorState* Actor(unsigned owner) noexcept {
    if(owner>=ActorCount||!Context())return nullptr;
    std::uint32_t pool=0,file=0;std::uint16_t slot=0xFFFF,identity=0xFFFF,status=0;
    Byte exists=0,removed[2]{};int hp=0;
    if(!Read(image+0xD334CC,pool)||pool<0x10000||pool>UINT32_MAX-ActorCount*0xF90u){Retire(owner);return nullptr;}
    const auto address=std::uintptr_t(pool)+owner*0xF90u;
    if(SharedActor::Busy(owner)||!Read(address+0xC,slot)||slot!=owner||!Read(address+0xE,identity)||identity==0xFFFF||
       (owner<18&&identity!=owner)||!Read(address+0x48,file)||!Read(address+0xDC8,exists)||!exists||
       !Read(address+0x5D0,hp)||hp<=0||!Read(address+0x606,status)||(status&1)||
       !Copy(removed,reinterpret_cast<const void*>(address+0xDCD),2)||removed[0]||removed[1]){Retire(owner);return nullptr;}
    auto& current=actors[owner];auto* pointer=reinterpret_cast<Byte*>(address);
    if(current.pointer!=pointer||current.identity!=identity||current.file!=file){
        Retire(owner);current={pointer,file,identity,Serial(),0};
    }
    return current.serial?&current:nullptr;
}
ActionState* Track(unsigned owner) noexcept {
    const auto* actor=Actor(owner);if(!actor)return nullptr;
    Byte index=255;std::int8_t count=0;std::array<Byte,72> row{};
    if(!Read(image+0xD2BDE1,count)||count<1||count>62||!Copy(&index,actor->pointer+0xDE5,1)||index>=count||
       !Copy(row.data(),reinterpret_cast<const void*>(image+0xD2AC70+72u*index),row.size())||
       row[0]!=owner||!row[3]||row[3]>4||row[2]>row[3])return nullptr;
    auto& action=actions[owner];bool same=action.serial&&action.actor==actor->serial&&action.index==index&&
        action.row[1]==row[1]&&action.row[3]==row[3];
    for(unsigned sub=0;same&&sub<row[3];++sub)same=std::memcmp(action.row.data()+8+16*sub,row.data()+8+16*sub,4)==0;
    if(!same){for(auto& reservation:reservations)if(reservation.source==owner)reservation={};
        action={};action.serial=Serial();action.actor=actor->serial;action.index=index;}
    action.row=row;return action.serial?&action:nullptr;
}
unsigned Command(const ActionState& action,unsigned sub) noexcept {
    return sub<action.row[3]?unsigned(action.row[8+16*sub])|(unsigned(action.row[9+16*sub])<<8):0;
}
bool CommandRow(unsigned command,const Byte*& row) noexcept {
    if((command&0xFFFFF000u)!=0x3000)return false;
    std::uint32_t bank=0;std::array<Byte,20> header{};
    if(!Read(image+0xD2A92C,bank)||bank<0x10000||bank>UINT32_MAX-0x100000||
       !Copy(header.data(),reinterpret_cast<const void*>(bank),header.size()))return false;
    const auto word=[&](unsigned at){return unsigned(header[at])|(unsigned(header[at+1])<<8);};
    const unsigned id=command&0xFFF;
    if(word(0)!=1||word(12)!=96||id>word(10)||word(16)!=20)return false;
    row=reinterpret_cast<const Byte*>(std::uintptr_t(bank)+20+96u*id);return true;
}
unsigned ResultIndex(const Bus::DamageCall& call,unsigned& sub) noexcept {
    const auto* target=Actor(call.target);if(!target||target->pointer!=call.targetActor)return UINT_MAX;
    auto* action=Track(call.user);if(!action||actors[call.user].pointer!=call.userActor)return UINT_MAX;
    const auto pointer=reinterpret_cast<std::uintptr_t>(call.info);
    const auto address=reinterpret_cast<std::uintptr_t>(target->pointer);
    for(unsigned group=0;group<2;++group){const auto start=address+0x774u+728u*group;
        if(pointer<start+24||pointer>=start+24+16*44||(pointer-start-24)%44)continue;
        const unsigned hit=static_cast<unsigned>((pointer-start-24)/44);Byte header[4]{};
        if(!Copy(header,reinterpret_cast<const void*>(start),4)||header[0]>hit||header[1]>16||hit>=header[1]||
           header[2]!=call.user||header[3]>=action->row[3]||Command(*action,header[3])!=call.commandId)return UINT_MAX;
        sub=header[3];return call.target*ResultsPerActor+group*16+hit;
    }
    return UINT_MAX;
}
unsigned Available(const Bus::DamageCall& call) noexcept {
    if(!applying||!Context())return 0;unsigned sub=4;const auto index=ResultIndex(call,sub);
    if(index>=reservations.size())return 0;
    auto& target=actors[call.target];unsigned mask=target.charges;
    for(unsigned i=call.target*ResultsPerActor;i<(call.target+1)*ResultsPerActor;++i){auto& reservation=reservations[i];
        if(reservation.target!=target.serial||reservation.source>=ActorCount||
           reservation.action!=actions[reservation.source].serial){reservation={};continue;}
        if(i!=index)mask&=~reservation.mask;
    }
    return mask;
}
bool Reserve(const Bus::DamageCall& call,unsigned mask) noexcept {
    if(!mask)return true;if(mask&~Available(call))return false;
    unsigned sub=4;const auto index=ResultIndex(call,sub);if(index>=reservations.size())return false;
    reservations[index]={actors[call.target].serial,actions[call.user].serial,call.user,sub,call.commandId,mask};return true;
}
bool Reserved(const Bus::DamageCall& call) noexcept {
    const auto available=Available(call);unsigned sub=4;
    const auto index=ResultIndex(call,sub);if(index>=reservations.size())return false;
    const auto& reservation=reservations[index];
    return reservation.mask&&(available&reservation.mask)==reservation.mask&&
        reservation.target==actors[call.target].serial&&reservation.action==actions[call.user].serial&&
        reservation.source==call.user&&reservation.sub==sub&&reservation.command==call.commandId;
}
bool ResolveNative(unsigned argument,unsigned mask,void* info,int& output) noexcept {
    if(!(mask&(expanded.load()?NulElements::Native:0x90u))||mask>255||!Context())return false;
    std::uint32_t pool=0;if(!Read(image+0xD334CC,pool))return false;
    const auto pointer=reinterpret_cast<std::uintptr_t>(info);
    if(pointer<pool||pointer>=std::uintptr_t(pool)+ActorCount*0xF90u)return false;
    const auto target=static_cast<unsigned>((pointer-pool)/0xF90u);auto* actor=Actor(target);if(!actor)return false;
    const auto relative=pointer-reinterpret_cast<std::uintptr_t>(actor->pointer);
    const unsigned group=relative>=0x774+728?1:0;Byte header[4]{};
    if(!Copy(header,actor->pointer+0x774+728*group,4))return false;
    auto* action=Track(header[2]);if(!action||header[3]>=action->row[3])return false;
    const auto command=Command(*action,header[3]);const Byte* row=nullptr;
    if(!CommandRow(command,row))return false;
    Byte flags[2]{};if(!Copy(flags,row+0x20,1)||!Copy(flags+1,row+0x23,1)||!(flags[1]&1)||(flags[0]&0x10))return false;
    Bus::DamageCall call{header[2],actors[header[2]].pointer,target,actor->pointer,row,command,info};
    if(Reserved(call)){output=-1;return true;}
    const unsigned wardMask=mask&(expanded?NulElements::Native:0x90u);output=0;
    if((Available(call)&wardMask)!=wardMask)return true;
    unsigned nativeMask=0;
    for(unsigned bit=1;bit<=128;bit<<=1)if((mask&bit)&&!(wardMask&bit)){
        const unsigned offset=bit==1?14:bit==2?16:bit==4?15:bit==8?13:0;Byte count=0;
        if(!offset||!Copy(&count,static_cast<const Byte*>(info)+offset,1)||!count)return true;nativeMask|=bit;
    }
    if(nativeMask&&SharedNul::Original()(argument,nativeMask,info)!=-1)return true;
    if(!Reserve(call,wardMask))return true;output=-1;return true;
}
struct ResultFrame {
    std::uint64_t epoch=0,target=0,action=0;unsigned command=0;
    std::array<std::array<Byte,4>,2> before{};Byte tide=0,shock=0;
};
struct FinishFrame {std::uint64_t epoch=0,action=0;unsigned count=0;};
thread_local std::array<ResultFrame,SharedAction::MaximumDepth> results{};
thread_local std::array<FinishFrame,SharedAction::MaximumDepth> finishes{};
void* BeforeResult(const SharedAction::ResultCall& call) noexcept {
    if(!Context()||!SharedAction::resultDepth||SharedAction::resultDepth>results.size())return nullptr;
    const auto* target=Actor(call.target);auto* action=Track(call.source);
    if(!target||!action||call.sub>=action->row[3])return nullptr;
    auto& frame=results[SharedAction::resultDepth-1];frame={};
    frame.epoch=observedEpoch;frame.target=target->serial;frame.action=action->serial;frame.command=Command(*action,call.sub);
    for(unsigned group=0;group<2;++group)
        if(!Copy(frame.before[group].data(),target->pointer+0x774+728*group,4))return nullptr;
    if(!Copy(&frame.tide,target->pointer+0x60E,1)||!Copy(&frame.shock,target->pointer+0x610,1))return nullptr;
    return &frame;
}
void AfterResult(void* token,const SharedAction::ResultCall& call,int,bool completed) noexcept {
    const auto* frame=static_cast<const ResultFrame*>(token);
    if(!completed||!frame||!Context()||frame->epoch!=observedEpoch)return;
    auto* target=Actor(call.target);auto* action=Track(call.source);
    if(!target||!action||target->serial!=frame->target||action->serial!=frame->action)return;
    bool landed=false;
    for(unsigned group=0;group<2;++group){Byte after[4]{};const auto& before=frame->before[group];
        if(!Copy(after,target->pointer+0x774+728*group,4)||before[2]!=call.source||before[3]!=call.sub||
           after[2]!=call.source||after[3]!=call.sub||after[0]<=before[0]||after[0]>16||after[0]>before[1])continue;
        for(unsigned hit=before[0];hit<after[0];++hit){Byte code=255;
            if(!Copy(&code,target->pointer+0x774+728*group+24+44*hit+1,1))continue;
            auto& reservation=reservations[call.target*ResultsPerActor+16*group+hit];
            if(reservation.target==target->serial&&reservation.action==action->serial&&reservation.source==call.source&&
               reservation.sub==call.sub){if(code==2)target->charges&=~reservation.mask;reservation={};}
            landed=landed||code==0;
        }
    }
    const Byte* row=nullptr;std::array<Byte,96> bytes{};
    if(!applying||!landed||!CommandRow(frame->command,row)||!Copy(bytes.data(),row,bytes.size()))return;
    const unsigned grant=NulElements::Grant(frame->command,bytes.data(),bytes.size(),expanded);
    if(!grant||(action->granted[call.target]&grant))return;
    target->charges|=grant;action->granted[call.target]|=grant;
    // Old Ward payloads reused Water/Lightning. Preserve the pre-existing
    // native charge instead of zeroing it or occupying status timer bytes.
    if(grant==16)Copy(target->pointer+0x60E,&frame->tide,1);
    else if(grant==128)Copy(target->pointer+0x610,&frame->shock,1);
}
void* BeforeFinish(const SharedAction::FinishCall& call) noexcept {
    if(!Context()||!SharedAction::finishDepth||SharedAction::finishDepth>finishes.size())return nullptr;
    auto* action=Track(call.owner);std::int8_t count=0;
    if(!action||action->index!=call.index||!Read(image+0xD2BDE1,count)||count<1)return nullptr;
    auto& frame=finishes[SharedAction::finishDepth-1];frame={observedEpoch,action->serial,static_cast<unsigned>(count)};return &frame;
}
void AfterFinish(void* token,const SharedAction::FinishCall& call,int result,bool completed) noexcept {
    const auto* frame=static_cast<const FinishFrame*>(token);std::int8_t count=0;
    if(!frame||!completed||result!=1||!Context()||frame->epoch!=observedEpoch||call.owner>=ActorCount||
       actions[call.owner].serial!=frame->action||!Read(image+0xD2BDE1,count)||count<0||static_cast<unsigned>(count)+1!=frame->count)return;
    for(auto& reservation:reservations)if(reservation.source==call.owner)reservation={};actions[call.owner]={};
}
void BeforeActor(unsigned,unsigned owner) noexcept {if(Context())Retire(owner);}
void ResetWards(Bus::ResetReason) noexcept {resetEpoch.fetch_add(1);}
void NewBattle() noexcept {ResetWards(Bus::ResetReason::BattleStart);}
const SharedAction::Observer actionObserver{BeforeResult,AfterResult,BeforeFinish,AfterFinish};
const SharedActor::Observer actorObserver{BeforeActor,nullptr};
unsigned ExternalMask(const char* key) noexcept {return running.load()&&expanded.load()?NulElements::KeyMask(key):0;}
const SharedNul::Wards wardProvider{ResolveNative,Available,Reserve,Reserved,ExternalMask};
const Bus::Observer combatObserver{nullptr,nullptr,nullptr,ResetWards,nullptr};
const SharedBattleRuntime::ActionObserver battleObserver{NewBattle};
} // namespace

NulWardInstallResult InstallNulWardHook(std::uintptr_t base,bool apply,bool logEvents,NulWardLogFn log,const NulWardInstallOptions* options){
    NulWardInstallResult result{};
    if(running.load()){result.ok=image==base;return result;}
    if(!apply&&!logEvents)return result;
    // The former nativeSlots/P16 options now use the same checked external
    // charge path; neither option grants permission to patch native timers.
    if(options&&(options->nativeSlots||options->experimentP16||options->p16Apply)&&log)
        log("[ffx-hooks] NulWard legacy options use shared external charge ownership\n");
    if(!SharedNul::Start(base)||!SharedAction::Start(base)||!SharedActor::Start(base))return result;
    image=base;applying=apply;expanded=options&&options->allElements;resetEpoch.fetch_add(1);ownerThread=0;
    if(!SharedAction::Subscribe(SharedAction::Slot::NulWard,&actionObserver)||
       !SharedActor::Subscribe(SharedActor::Slot::NulWard,&actorObserver)||!SharedNul::RegisterWards(&wardProvider)||
       !Bus::Subscribe(Bus::Slot::NulWard,&combatObserver)||
       !SharedBattleRuntime::RegisterActionObserver(SharedBattleRuntime::ActionConsumer::NulWard,&battleObserver)){
        RemoveNulWardHook(log);return result;
    }
    running=true;result.ok=true;
    result.detourAftermath=reinterpret_cast<std::uintptr_t>(SharedAction::OriginalResult());
    if(log)log("[ffx-hooks] NulWard shared action/Nul/actor observers installed\n");return result;
}
static_assert(std::atomic<bool>::is_always_lock_free,"NulWard detach gate must not lock");
void RequestNulWardDetachStop() noexcept {running=false;}
// Compatibility with recovery callers; shared native owners remain authoritative.
void RequestNulWardStop() noexcept {RequestNulWardDetachStop();}
bool RemoveNulWardHook(NulWardLogFn log){
    RequestNulWardDetachStop();
    SharedNul::UnregisterWards(&wardProvider);SharedAction::Unsubscribe(SharedAction::Slot::NulWard,&actionObserver);
    SharedActor::Unsubscribe(SharedActor::Slot::NulWard,&actorObserver);Bus::Unsubscribe(Bus::Slot::NulWard,&combatObserver);
    SharedBattleRuntime::UnregisterActionObserver(SharedBattleRuntime::ActionConsumer::NulWard,&battleObserver);
    if(log)log("[ffx-hooks] NulWard stopped; shared native owners retained\n");return true;
}
bool IsNulWardHookInstalled(){return running.load();}
} // namespace FfxHooks
