#include "OmegaLastSealRuntime.h"
#include "OmegaLastSealBridge.h"
#include "OmegaLastSealNative.h"
#include "OmegaLastSealDamage.h"
#include "OmegaLastSealEvidence.h"
#include "OmegaLastSealNotice.h"
#include "SharedQueueRuntime.h"
#include "SharedActionRuntime.h"
#include "SharedBattleRuntime.h"
#include "CombatExtensionBus.h"
#include "EquipmentWorkshopRuntime.h"
#include "BattleEndHook.h"
#include "ArcanaRuntime.h"
#include "ArcanaVictory.h"
#include "SinMetadataCore.h"
#include "../shared/Config.h"
#include <fstream>
#include <atomic>
#include <cstdio>
#include <limits>

namespace FfxHooks::OmegaLastSeal::Runtime {
namespace {
namespace N=Native::Common;namespace A=SharedAction;namespace B=CombatExtensions;
using Byte=unsigned char;using Packet=std::array<Byte,72>;
std::uintptr_t image=0;Bindings bindings{};Reader reader=nullptr;Log logger=nullptr;
std::atomic<bool> primed{false},accepting{false};std::atomic<DWORD> owner{0};
std::atomic<const char*> detail{"Omega Last Seal is OFF"};
State state{};Context context{};std::array<Native::Identity,5> keys{};
std::uint64_t serial=0,saveSession=0,arcanaBattle=0;bool admitted=false,rewardEligible=false;
thread_local bool rebuilding=false,observing=false,draining=false;
bool cleanupPending=false;
std::array<Byte,3> originalGauge{};bool gaugeOwned=false;
std::array<Byte,40> originalName{},markedName{};bool nameOwned=false;
struct Ticket {bool owned=false;Plan plan{};Packet row{};};
std::array<Ticket,62> tickets{};
struct Tracked {std::uint64_t token=0;unsigned index=255;std::uintptr_t source=0;Packet row{};bool started=false;unsigned bodyDamage=0,zanmatoDamage=0;Category category=Category::Other;bool categorySeen=false;};
std::array<Tracked,31> actions{};
struct QueueFrame {std::uint64_t battle=0;unsigned count=0;Packet row{};Plan plan{};State next{};};
struct ResultFrame {std::uint64_t battle=0,token=0;unsigned source=31,target=31,command=0;Category category=Category::Other;bool zanmato=false;};
struct FinishFrame {std::uint64_t battle=0;Tracked action{};Ticket ticket{};unsigned count=0;bool finished=false,retired=false;};
struct HitFrame {std::uint64_t battle=0;CommandRow row{};Native::Identity target{};std::array<Byte,2> resistance{};Byte borrowedValue=0;bool borrowed=false;};
struct HpFrame {std::uint64_t battle=0,token=0;unsigned target=31,source=31,before=0,special=0;bool restored=false,zanmato=false;Category category=Category::Other;};
thread_local std::array<QueueFrame,SharedQueue::MaximumDepth> queues{};
thread_local std::array<ResultFrame,A::MaximumDepth> results{};
thread_local std::array<FinishFrame,A::MaximumDepth> finishes{};
thread_local std::array<HitFrame,B::MaximumDepth> hits{};
thread_local std::array<HpFrame,32> hpFrames{};
thread_local unsigned hpDepth=0;
template<class T>bool Put(std::uintptr_t address,const T& value) noexcept {return NativeUiSupport::Copy(reinterpret_cast<void*>(address),&value,sizeof(value));}
bool Own() noexcept {return owner.load()==GetCurrentThreadId();}
bool Role(unsigned id) noexcept {return id>=20&&id<=24;}
bool Same(unsigned role) noexcept {return role<5&&Native::Same(image,role,keys[role]);}
void Trace(const char* event) noexcept {if(logger){char text[192]{};std::snprintf(text,sizeof(text),"[ffx-hooks] Omega Last Seal: %s battle=%llu phase=%u gauge=%u\n",event,static_cast<unsigned long long>(context.battle),state.phase,state.gauge);logger(text);}}
bool Witness(std::uintptr_t address,const ResourceProof& proof) noexcept {
    if(address<0x10000||!proof.size||address>UINT32_MAX-proof.size)return false;
    __try{return Hash(reinterpret_cast<const void*>(address),proof.size)==proof.hash;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool Resources(std::uintptr_t base) noexcept {
    if(!primed.load()||base!=image)return false;
    std::array<char,10> name{};
    if(!N::Read(base+0xD2C25A,name)||std::memcmp(name.data(),"omeg01_10",9))return false;
    const auto carrier=N::ReadOr<std::uint32_t>(base+0xD2A9A8);
    if(N::ReadOr<std::uint16_t>(base+0xD2A9A6)!=bindings.formation.size||!Witness(carrier,bindings.formation))return false;
    for(unsigned role=0;role<5;++role){const auto key=Native::Identify(base,role);if(!key.actor||!Witness(key.resource,bindings.monsters[role]))return false;}
    return true;
}
bool Commands() noexcept {
    for(const auto& proof:bindings.commands){CommandRow row{};if(!N::Command(image,proof.id,row)||!bindings.Matches(proof.id,row))return false;}return true;
}
Life Vitals() noexcept {
    Life out;if(!Same(0))return out;
    out.hp=N::ReadOr<unsigned>(keys[0].actor+0x5D0);out.maximum=N::ReadOr<unsigned>(keys[0].actor+0x594);
    for(unsigned role=1;role<5;++role)if(Same(role)&&N::Alive(keys[role].actor)){
        out.helpers|=1u<<(role-1);if(N::ReadOr<Byte>(keys[role].actor+0x609))out.silenced|=1u<<(role-1);
    }
    for(unsigned role=0;role<5;++role)if(Same(role)&&N::Alive(keys[role].actor)){
        const auto buffs=N::Life(keys[role].actor).buffs;
        if(!(buffs&OmegaT7::Protect))out.missingProtect|=1u<<(20+role);
        if(!(buffs&OmegaT7::Shell))out.missingShell|=1u<<(20+role);
        if(!(buffs&OmegaT7::Haste))out.missingHaste|=1u<<(20+role);
    }
    for(unsigned id=0;id<18;++id)if(N::Alive(N::Actor(image,id))){out.party|=1u<<id;out.partyBuffs=out.partyBuffs||N::Life(N::Actor(image,id)).buffs!=0;}
    return out;
}
void Clear() noexcept {
    if(Own()&&nameOwned&&Same(0)){std::array<Byte,40> current{};if(N::Read(keys[0].actor+0x540,current)&&current==markedName)Put(keys[0].actor+0x540,originalName);}
    if(Own()&&gaugeOwned&&Same(0)){
        const auto actor=keys[0].actor;
        if(N::ReadOr<Byte>(actor+0x5BD)==100&&N::ReadOr<Byte>(actor+0x718)==1){Put(actor+0x5BC,originalGauge[0]);Put(actor+0x5BD,originalGauge[1]);Put(actor+0x718,originalGauge[2]);}
    }
    state={};context={};keys={};tickets={};actions={};admitted=rewardEligible=gaugeOwned=nameOwned=cleanupPending=false;saveSession=arcanaBattle=0;owner=0;BattleNotice::Clear();
}
void Close(const char* why) noexcept {detail=why;accepting=false;Trace(why);if(Own())Clear();}
bool Sync() noexcept {
    if(!accepting.load()||!reader||rebuilding)return false;
    if(owner.load()&&!Own())return false;
    Context current;if(!reader(current)||!current.battle)return false;
    if(admitted&&current.battle==context.battle){for(unsigned role=0;role<5;++role)if(!Same(role)){Close("actor ownership changed");return false;}return true;}
    if(admitted)Clear();
    if(!Resources(image)||!Commands())return false;
    owner=GetCurrentThreadId();context=current;for(unsigned role=0;role<5;++role)keys[role]=Native::Identify(image,role);
    for(unsigned role=3;role<5;++role)if(!Native::Dormant(image,role,keys[role],true)){Clear();return false;}
    for(unsigned role=1;role<5;++role)if(!Native::Scale(image,role,keys[role])){Clear();return false;}
    if(!Begin(state,{true,!current.original,1077,10,100,current.battle},Vitals())){Clear();return false;}
    if(N::Read(keys[0].actor+0x540,originalName)&&SinMetadata::NamedView(originalName,"Last Seal",9,&markedName))nameOwned=Put(keys[0].actor+0x540,markedName);
    originalGauge={N::ReadOr<Byte>(keys[0].actor+0x5BC),N::ReadOr<Byte>(keys[0].actor+0x5BD),N::ReadOr<Byte>(keys[0].actor+0x718)};
    N::Property(image,20,0x14,100);N::Property(image,20,0x89,1);gaugeOwned=Native::Gauge(image,keys[0],0);
    saveSession=Arcana::Runtime::SaveSession();arcanaBattle=Arcana::Runtime::BattleGeneration();
    rewardEligible=current.original&&saveSession&&arcanaBattle&&N::ReadOr<std::uint16_t>(image+ExecutableProfile::Rva<0xD2CA90>())==271;
    admitted=true;Trace("admitted T9");return true;
}
bool Queue(unsigned id,Packet& row,unsigned& index,unsigned& count) noexcept {
    const auto actor=N::Actor(image,id);const int length=N::ReadOr<std::int8_t>(image+0xD2BDE1,-1);const auto at=N::ReadOr<Byte>(actor+0xDE5,255);
    if(!actor||length<1||length>62||at>=length||!N::Read(image+0xD2AC70+72u*at,row)||row[0]!=id||!row[3]||row[3]>4||row[2]>row[3])return false;
    index=at;count=static_cast<unsigned>(length);return true;
}
Tracked* Track(unsigned id) noexcept {
    if(id>=31)return nullptr;Packet row{};unsigned index=0,count=0;if(!Queue(id,row,index,count))return nullptr;
    auto& action=actions[id];bool same=action.token&&action.index==index&&action.source==N::Actor(image,id)&&action.row[1]==row[1]&&action.row[3]==row[3];
    for(unsigned n=0;same&&n<row[3];++n)same=!std::memcmp(action.row.data()+8+16*n,row.data()+8+16*n,16);
    if(!same){if(serial==UINT64_MAX){Close("action serial exhausted");return nullptr;}action={};action.token=++serial;action.index=index;action.source=N::Actor(image,id);}
    action.row=row;return &action;
}
void Cleanup() noexcept {
    for(unsigned role=1;role<5;++role)if(Same(role)){
        N::Property(image,20+role,0,0);N::Property(image,20+role,4,1);
        N::Property(image,20+role,0x15,0);N::Property(image,20+role,0xAD,1);
    }
    cleanupPending=true;
    BattleNotice::Clear();Trace("body defeated; controller retired");
}
void DrainCleanup() noexcept {
    if(!cleanupPending||draining||!admitted||!Own())return;draining=true;
    __try {
        int count=N::ReadOr<std::int8_t>(image+0xD2BDE1,-1);
        if(count<0||count>62)__leave;
        // Retire pending controlled packets only after the current native
        // retirement has completed. Native removal updates every actor's queue
        // count and wakes its usual script wait; no row memmove is fabricated.
        for(int i=count-1;i>=0;--i){Packet row{};if(!N::Read(image+0xD2AC70+72u*static_cast<unsigned>(i),row))__leave;
            if(!Role(row[0])||!tickets[i].owned||!Same(row[0]-20))continue;
            bool death=false;for(unsigned sub=0;sub<row[3]&&sub<4;++sub)death=death||N::Word(row.data()+8+16*sub)==0x407E;
            if(death)continue;
            if(A::FinishShim(row[0],static_cast<unsigned>(i),0)!=1)__leave;
        }
        cleanupPending=false;
    }__finally{draining=false;}
}
bool Apply(const State& next,const Effect& effect,bool cancelled=false) noexcept {
    for(unsigned role=1;role<5;++role)if((effect.revive&(1u<<(role-1)))&&(!Same(role)||Native::Queued(image,20+role)))return false;
    rebuilding=true;bool success=true;
    __try{for(unsigned role=1;role<5;++role)if(effect.revive&(1u<<(role-1)))if(!Native::Revive(image,role,keys[role])){success=false;break;}}
    __finally{rebuilding=false;}
    if(!success){Close("native rebirth failed");return false;}
    state=next;
    if(effect.cleanup){Cleanup();return true;}
    if(effect.revive){N::Property(image,20,0xA,0);N::Property(image,20,0xB,0);state.helpers=Vitals().helpers;}
    Native::Gauge(image,keys[0],state.gauge);BattleNotice::Publish(effect.notice,state.finalEntered,cancelled);return true;
}
void Refresh(std::uint64_t token) noexcept {
    if(!accepting.load()||!admitted||!Own()||observing)return;observing=true;
    auto next=state;const bool preparing=next.preparing;const auto effect=Observe(next,Vitals(),token);(void)Apply(next,effect,preparing&&effect.notice==Notice::Exposure);observing=false;
}
std::uint32_t Targets(unsigned actor,unsigned command,const Life& life) noexcept {
    if(command==Concentration||command==LastSeal||command==Reconstitution)return 1u<<actor;
    if(command==Protectga)return life.missingProtect;
    if(command==Shellga)return life.missingShell;
    if(command==Hastega)return life.missingHaste;
    using Select=std::uint32_t(__cdecl*)(unsigned,unsigned,int,int);
    return reinterpret_cast<Select>(image+0x3996D0)(actor,command,-1,0)&life.party;
}
void PutCommand(Packet& row,unsigned at,unsigned command,unsigned targets) noexcept {
    const std::uint16_t id=static_cast<std::uint16_t>(command),none=255;std::memcpy(row.data()+8+16*at,&id,2);std::memcpy(row.data()+10+16*at,&none,2);std::memcpy(row.data()+16+16*at,&targets,4);
}
void* BeforeQueue(const SharedQueue::Call& call,const Byte*& forwarded) noexcept {
    if(cleanupPending&&!A::resultDepth&&!A::finishDepth&&!B::currentDamage)DrainCleanup();
    if(call.tail!=0||!Role(call.actor)||!Sync()||!state.active||!SharedQueue::depth||SharedQueue::depth>queues.size())return nullptr;
    Packet original{};if(!NativeUiSupport::Copy(original.data(),call.packet,original.size())||original[0]!=call.actor||original[1]||original[2]||original[3]!=1)return nullptr;
    const unsigned command=N::Word(original.data()+8);if(command==0x407E||(call.actor!=20&&command!=Attack))return nullptr;
    Refresh(0);if(!state.active)return nullptr;
    if(!Commands()){Close("command bindings changed");return nullptr;}
    const int count=N::ReadOr<std::int8_t>(image+0xD2BDE1,-1);if(count<0||count>=62)return nullptr;
    auto& frame=queues[SharedQueue::depth-1];frame={};frame.battle=context.battle;frame.count=static_cast<unsigned>(count);frame.next=state;
    const auto life=Vitals();frame.plan=Choose(frame.next,life,call.actor-20,static_cast<unsigned>(OmegaT7::Mix(context.battle^serial^count)));
    if(!frame.plan.count)return nullptr;
    frame.row[0]=static_cast<Byte>(call.actor);frame.row[3]=static_cast<Byte>(frame.plan.count);
    frame.row[6]=static_cast<Byte>(call.priority);frame.row[7]=static_cast<Byte>(call.mode);unsigned used=0;
    for(unsigned i=0;i<frame.plan.count;++i){auto mask=Targets(call.actor,frame.plan.commands[i],life);
        if(frame.plan.distinct){mask&=~used;if(!mask)mask=life.party&~used;if(mask)mask&=0u-mask;used|=mask;}
        if(!mask)return nullptr;PutCommand(frame.row,i,frame.plan.commands[i],mask);
    }
    forwarded=frame.row.data();return &frame;
}
void AfterQueue(void* token,const SharedQueue::Call&,int result,bool complete) noexcept {
    const auto& frame=*static_cast<QueueFrame*>(token);if(!complete||result!=-1||!admitted||!Own()||frame.battle!=context.battle)return;
    Packet actual{};if(N::ReadOr<std::int8_t>(image+0xD2BDE1,-1)!=static_cast<int>(frame.count+1)||!N::Read(image+0xD2AC70+72u*frame.count,actual)||actual!=frame.row){Close("queue append ownership lost");return;}
    state=frame.next;tickets[frame.count]={true,frame.plan,frame.row};
}
void* DamageEnter(const B::DamageCall& call,const void*& forwarded) noexcept {
    if(!Sync()||!state.active||!Role(call.target)||!B::currentDamage||!B::currentDamage->depth||B::currentDamage->depth>hits.size())return nullptr;
    const unsigned role=call.target-20;if(!Same(role)||reinterpret_cast<std::uintptr_t>(call.targetActor)!=keys[role].actor)return nullptr;
    auto& frame=hits[B::currentDamage->depth-1];frame={};frame.battle=context.battle;frame.target=keys[role];
    if(!NativeUiSupport::Copy(frame.row.data(),forwarded,frame.row.size()))return nullptr;
    if(IsZanmato(call.commandId)){ZanmatoDamageView(frame.row,bindings.Find(Reprimand)->row);forwarded=frame.row.data();}
    if(role==0){
        frame.resistance={N::ReadOr<Byte>(frame.target.actor+0x647),N::ReadOr<Byte>(frame.target.actor+0x648)};
        const bool window=state.exposed&&state.exposure;
        frame.borrowedValue=window?Byte(0):Byte(255);
        frame.borrowed=Put(frame.target.actor+0x647,static_cast<std::uint16_t>(frame.borrowedValue*0x101u));
        // An authored guaranteed break must not ignore the closed-window
        // resistance. Only this target's borrowed calculation loses the chance.
        if(!window){frame.row[0x34]=frame.row[0x35]=0;forwarded=frame.row.data();}
    }
    return &frame;
}
void DamageLeave(void* token,const B::DamageCall& call,unsigned,bool) noexcept {
    const auto& frame=*static_cast<HitFrame*>(token);if(!frame.borrowed||!Own()||frame.battle!=context.battle||!Role(call.target)||!Same(call.target-20))return;
    for(unsigned n=0;n<2;++n)if(N::ReadOr<Byte>(frame.target.actor+0x647+n)==frame.borrowedValue)Put(frame.target.actor+0x647+n,frame.resistance[n]);
}
void* BeforeResult(const A::ResultCall& call) noexcept {
    if(!Sync()||call.source>=31||call.target>=31||!A::resultDepth||A::resultDepth>results.size())return nullptr;
    auto* action=Track(call.source);if(!action||call.sub>=action->row[3])return nullptr;action->started=true;
    auto& frame=results[A::resultDepth-1];frame={};frame.battle=context.battle;frame.token=action->token;frame.source=call.source;frame.target=call.target;frame.command=N::Word(action->row.data()+8+16*call.sub);
    CommandRow row{};if(N::Command(image,frame.command,row))frame.category=DamageCategory(row.data());frame.zanmato=IsZanmato(frame.command);return &frame;
}
void AfterResult(void* token,const A::ResultCall&,int,bool completed) noexcept {
    const auto& frame=*static_cast<ResultFrame*>(token);if(completed&&admitted&&Own()&&frame.battle==context.battle)Refresh(frame.token);
}
ResultFrame* CurrentResult(unsigned target) noexcept {
    if(!A::resultDepth||A::resultDepth>results.size())return nullptr;auto& result=results[A::resultDepth-1];
    return result.battle==context.battle&&result.target==target&&result.source<31&&actions[result.source].token==result.token?&result:nullptr;
}
ResultFrame HpContext(unsigned target) noexcept {
    if(const auto* result=CurrentResult(target))return *result;
    ResultFrame out;out.battle=context.battle;out.target=target;
    if(!N::ReadOr<unsigned>(image+0xD3338C))return out;
    // Some native Zanmato publications bypass the normal ResultCall seam.
    // They still belong to its executing queue packet, not a new damage budget.
    out.zanmato=true;
    for(unsigned source=0;source<18;++source){
        auto* action=Track(source);if(!action||action->row[2]>=action->row[3])continue;
        const unsigned command=N::Word(action->row.data()+8+16*action->row[2]);if(!IsZanmato(command))continue;
        if(out.source!=31){out.source=31;out.token=0;return out;}
        out.source=source;out.token=action->token;out.command=command;CommandRow row{};if(N::Command(image,command,row))out.category=DamageCategory(row.data());
    }
    return out;
}
int AdjustHp(const SharedCombat::HpCall& call,int amount) noexcept {
    if(!accepting.load()||!admitted||!state.active||!Own()||!Role(call.target)||!Same(call.target-20)||reinterpret_cast<std::uintptr_t>(call.actor)!=keys[call.target-20].actor||amount<0)return amount;
    const auto result=HpContext(call.target);const auto category=result.category;
    const bool zanmato=result.zanmato||N::ReadOr<unsigned>(image+0xD3338C)!=0;
    const auto life=Vitals();unsigned desired=static_cast<unsigned>(amount);
    if(zanmato){desired=N::ReadOr<unsigned>(keys[call.target-20].actor+0x5D0);desired=ZanmatoImpact(state,life,call.target-20,category,desired);
        if(call.target==20){const auto consumed=result.source<31?actions[result.source].zanmatoDamage:ZanmatoBodyLimit;desired=(std::min)(desired,consumed>=ZanmatoBodyLimit?0u:ZanmatoBodyLimit-consumed);}}
    else desired=Limit(state,life,call.target-20,category,desired);
    return static_cast<int>((std::min)(desired,unsigned(INT32_MAX)));
}
void* BeforeHp(const SharedCombat::HpCall& call) noexcept {
    if(!accepting.load()||!admitted||!state.active||!Own()||!Role(call.target)||!Same(call.target-20)||reinterpret_cast<std::uintptr_t>(call.actor)!=keys[call.target-20].actor||hpDepth==hpFrames.size())return nullptr;
    auto& frame=hpFrames[hpDepth++];frame={};frame.battle=context.battle;frame.target=call.target;frame.before=N::ReadOr<unsigned>(keys[call.target-20].actor+0x5D0);
    const auto result=HpContext(call.target);frame.source=result.source;frame.token=result.token;frame.category=result.category;frame.zanmato=result.zanmato;
    // The native HP leaf has a separate Zanmato-mode subtraction. Borrow only
    // its synchronous leaf scope, otherwise it would bypass the seals/floor.
    frame.special=N::ReadOr<unsigned>(image+0xD3338C);if(frame.special)frame.restored=Put(image+0xD3338C,0u);
    return &frame;
}
void AfterHp(void* token,const SharedCombat::HpCall&,int,bool completed) noexcept {
    const auto frame=*static_cast<HpFrame*>(token);
    if(frame.restored&&N::ReadOr<unsigned>(image+0xD3338C)==0)Put(image+0xD3338C,frame.special);
    if(hpDepth)--hpDepth;
    if(!completed||!accepting.load()||!admitted||!Own()||frame.battle!=context.battle||!Same(frame.target-20))return;
    const auto after=N::ReadOr<unsigned>(keys[frame.target-20].actor+0x5D0);
    if(frame.target==20&&frame.source<31&&frame.token==actions[frame.source].token&&after<frame.before){
        auto& action=actions[frame.source];const auto loss=frame.before-after;action.bodyDamage=(std::min)(UINT32_MAX-loss,action.bodyDamage)+loss;
        if(!action.categorySeen)action.category=frame.category;else if(action.category!=frame.category)action.category=Category::Other;action.categorySeen=true;
        if(frame.zanmato||frame.special)action.zanmatoDamage=(std::min)(ZanmatoBodyLimit,action.zanmatoDamage+(std::min)(loss,ZanmatoBodyLimit));
    }
    Refresh(frame.token);
}
void* BeforeFinish(const A::FinishCall& call) noexcept {
    if(!Sync()||call.owner>=31||!A::finishDepth||A::finishDepth>finishes.size())return nullptr;
    const int count=N::ReadOr<std::int8_t>(image+0xD2BDE1,-1);Packet row{};
    if(count<1||count>62||call.index>=static_cast<unsigned>(count)||!N::Read(image+0xD2AC70+72u*call.index,row)||row[0]!=call.owner||!row[3]||row[3]>4)return nullptr;
    auto& frame=finishes[A::finishDepth-1];frame={};frame.battle=context.battle;
    if(auto* action=Track(call.owner))if(action->index==call.index)frame.action=*action;
    frame.action.index=call.index;frame.action.row=row;frame.ticket=tickets[call.index];frame.count=static_cast<unsigned>(count);frame.finished=row[2]>=row[3]&&!call.preserve;
    if(frame.ticket.owned){bool same=frame.ticket.row[0]==row[0]&&frame.ticket.row[1]==row[1]&&frame.ticket.row[3]==row[3];
        for(unsigned sub=0;same&&sub<row[3];++sub)same=!std::memcmp(frame.ticket.row.data()+8+16*sub,row.data()+8+16*sub,4);
        if(!same)frame.ticket={};}
    return &frame;
}
void AfterFinish(void* token,const A::FinishCall& call,int result,bool completed) noexcept {
    auto& frame=*static_cast<FinishFrame*>(token);if(!completed||result!=1||!admitted||!Own()||frame.battle!=context.battle)return;
    if(N::ReadOr<std::int8_t>(image+0xD2BDE1,-1)!=static_cast<int>(frame.count-1)||(frame.action.token&&actions[call.owner].token!=frame.action.token))return;
    frame.retired=true;if(actions[call.owner].index==call.index)actions[call.owner]={};for(auto& action:actions)if(action.token&&action.index>call.index)--action.index;
    for(unsigned i=call.index;i+1<frame.count;++i)tickets[i]=tickets[i+1];tickets[frame.count-1]={};
}
void Counter(unsigned target) noexcept {
    if(!state.active||!Same(0)||!N::Alive(keys[0].actor)||target>=18||!N::Alive(N::Actor(image,target)))return;
    const int before=N::ReadOr<std::int8_t>(image+0xD2BDE1,-1);if(before<0||before>=62)return;
    Packet row{};row[0]=20;row[1]=row[3]=1;PutCommand(row,0,Reprimand,1u<<target);
    const auto result=reinterpret_cast<SharedQueue::Function>(image+SharedQueue::AppendRva)(20,row.data(),0,0,1);Packet actual{};
    if(result==-1&&N::ReadOr<std::int8_t>(image+0xD2BDE1,-1)==before+1&&N::Read(image+0xD2AC70+72u*before,actual)&&actual==row)tickets[before]={true,Single(Kind::Reaction,Reprimand),row};
}
void AfterRetirement(void* token,const A::FinishCall& call,int,bool) noexcept {
    const auto frame=*static_cast<FinishFrame*>(token);if(!frame.retired||!accepting.load()||!admitted||!Own()||frame.battle!=context.battle)return;
    if(cleanupPending){DrainCleanup();return;}
    if(!frame.finished)return;
    Action action;action.token=frame.action.token;action.actor=call.owner;action.role=Role(call.owner)?call.owner-20:5;
    action.party=call.owner<18;action.normal=frame.action.row[1]==0;
    // A native fully consumed 72-byte action packet is the completion proof.
    // Defend/support may have no ResultCall. Free switches use the separate
    // eight-byte input queue; unfinished/preserved packets were rejected above.
    action.completed=true;
    action.category=frame.action.category;action.bodyDamage=frame.action.bodyDamage;action.plan=frame.ticket.plan;
    if(!action.party&&!frame.ticket.owned)return;
    auto next=state;const bool preparing=next.preparing;const auto effect=Complete(next,Vitals(),action);
    if(Apply(next,effect,preparing&&effect.notice==Notice::Exposure)&&effect.counter<18)Counter(effect.counter);
    BattleNotice::Flush();
}
void ResetState(B::ResetReason) noexcept {if(!owner.load()||Own())Clear();}
void Battle() noexcept {ResetState(B::ResetReason::BattleStart);}
void Populated() noexcept {(void)Sync();}
void ActorRetiring(unsigned,unsigned id) noexcept {if(!rebuilding&&Own()&&Role(id))Clear();}
struct TerminalFrame {bool qualified=false;Arcana::VictoryReceipt receipt{};};
thread_local TerminalFrame terminal{};
void* BeforeTerminal(std::uint32_t) noexcept {
    if(!accepting.load()||!admitted||!Own())return nullptr;terminal={};const auto outcome=Native::RawOutcome(image);
    terminal.qualified=rewardEligible&&Same(0)&&!N::ReadOr<unsigned>(keys[0].actor+0x5D0)&&(outcome==2||outcome==4)&&saveSession==Arcana::Runtime::SaveSession()&&arcanaBattle==Arcana::Runtime::BattleGeneration();
    terminal.receipt={saveSession,arcanaBattle,1077,10,100,0,true,terminal.qualified};return &terminal;
}
void AfterTerminal(void* token,bool completed) noexcept {
    const auto frame=*static_cast<TerminalFrame*>(token);if(completed&&frame.qualified)(void)Arcana::Runtime::GrantVictory(frame.receipt);if(Own())Clear();
}
const SharedQueue::Observer queueObserver{BeforeQueue,AfterQueue};
const A::Observer actionObserver{BeforeResult,AfterResult,BeforeFinish,AfterFinish,AfterRetirement};
const SharedCombat::HpObserver hpObserver{BeforeHp,AfterHp,AdjustHp,true};
const SharedActor::Observer actorObserver{ActorRetiring,nullptr};
const B::Observer damageObserver{DamageEnter,DamageLeave,nullptr,ResetState,nullptr};
const SharedBattleRuntime::ActionObserver battleObserver{Battle};
const BattleTerminal::Observer terminalObserver{BeforeTerminal,AfterTerminal};
const Bridge::Provider statsProvider{UsesAuthoredStats,Populated};
bool Register(Reader contextReader,bool nativeHooks){
    if(!primed.load()||!contextReader)return false;reader=contextReader;
    if(!Bridge::Register(&statsProvider))return false;
    if(!SharedQueue::Register(&queueObserver)||!A::Subscribe(A::Slot::OmegaLastSeal,&actionObserver)||!SharedCombat::RegisterHpObserver(SharedCombat::HpSlot::OmegaLastSeal,&hpObserver)||
       !SharedActor::Subscribe(SharedActor::Slot::OmegaLastSeal,&actorObserver)||!B::Subscribe(B::Slot::OmegaLastSeal,&damageObserver)||
       !SharedBattleRuntime::RegisterActionObserver(SharedBattleRuntime::ActionConsumer::OmegaLastSeal,&battleObserver)||!BattleTerminal::Subscribe(BattleTerminal::Slot::OmegaLastSeal,&terminalObserver)){Stop();return false;}
    if(nativeHooks&&(!EquipmentWorkshop::CombatProducerReady()||!SharedQueue::Start(image)||!A::Start(image)||!SharedActor::Start(image)||!SharedCombat::Start(image)||!BattleNotice::Start(image))){Stop();return false;}
    accepting=true;detail="Omega Last Seal ready";return true;
}
}
bool Requested() noexcept {return Config::GetBool("spira.omega_last_seal",false);}
void Prime(std::uintptr_t base,const std::filesystem::path& game,bool validateOnly,Log log) noexcept {
    if(!Requested()||validateOnly||primed.load())return;
    try{image=base;logger=log;if(!ExecutableProfile::Steam20261001||!NativeUiSupport::Profile(base,Evidence::spans)){detail="Omega Last Seal executable mismatch";return;}
        std::ifstream file(game/L"modules"/L"config"/L"_omega-last-seal-v1.json",std::ios::binary|std::ios::ate);const auto size=file.tellg();
        if(size<=0||size>32768){detail="Omega Last Seal package unavailable";return;}std::string text(static_cast<std::size_t>(size),'\0');file.seekg(0);
        if(!file.read(text.data(),size)||!ParseBindings(text,bindings)){detail="Omega Last Seal package invalid or disabled";return;}
        if(!B::Subscribe(B::Slot::OmegaLastSeal,&damageObserver)){detail="Omega Last Seal damage producer conflict";return;}
        primed=true;if(!Bridge::Register(&statsProvider)){primed=false;Stop();detail="Omega Last Seal stats provider conflict";return;}detail="Omega Last Seal prepared";
    }catch(...){detail="Omega Last Seal initialization failed";}
}
bool Start(Reader contextReader){return Register(contextReader,true);}
bool UsesAuthoredStats(std::uintptr_t base) noexcept {return Resources(base);}
void RequestStop() noexcept {accepting=false;BattleNotice::Stop();}
void Stop() noexcept {
    RequestStop();if(!owner.load()||Own())Clear();SharedQueue::Unregister(&queueObserver);A::Unsubscribe(A::Slot::OmegaLastSeal,&actionObserver);
    SharedCombat::UnregisterHpObserver(SharedCombat::HpSlot::OmegaLastSeal,&hpObserver);SharedActor::Unsubscribe(SharedActor::Slot::OmegaLastSeal,&actorObserver);
    B::Unsubscribe(B::Slot::OmegaLastSeal,&damageObserver);SharedBattleRuntime::UnregisterActionObserver(SharedBattleRuntime::ActionConsumer::OmegaLastSeal,&battleObserver);
    Bridge::Unregister(&statsProvider);
    BattleTerminal::Unsubscribe(BattleTerminal::Slot::OmegaLastSeal,&terminalObserver);detail="Omega Last Seal is OFF";
}
bool Active() noexcept {return accepting.load();}
bool Prepared() noexcept {return primed.load();}
const char* Detail() noexcept {return detail.load();}
#ifdef FFXHOOKS_TESTING
bool PrepareForTests(std::uintptr_t base,const char* manifest,Reader contextReader,Log log){image=base;logger=log;primed=ParseBindings(manifest,bindings);return Register(contextReader,false);}
State SnapshotForTests() noexcept {return state;}
#endif
}
