#include "InsideSinRuntime.h"
#include "SharedActionRuntime.h"
#include "SharedActorRuntime.h"
#include "SharedBattleRuntime.h"
#include "SharedCombatRuntime.h"
#include "CombatExtensionBus.h"
#include "SinMetadataCore.h"
#include "EquipmentWorkshopRuntime.h"
#include "../shared/Config.h"
#include "InsideSinSettings.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <intrin.h>

namespace FfxHooks::InsideSin::Runtime {
namespace {
using Byte=std::uint8_t;
namespace Bus=CombatExtensions;
std::atomic<bool> accepting{false};
std::atomic<const char*> detail{"Inside Sin is OFF"};
std::uintptr_t image=0;
SinAi::ContextReader reader=nullptr;Log logger=nullptr;
Bindings bindings{};Ledger ledger;
std::atomic<DWORD> owner{0};std::uint64_t generation=0,serial=0,incarnation=0;
SinAi::Context encounter{};
bool syncing=false;
thread_local bool conditional=false;
constexpr auto ActorRoot=ExecutableProfile::Rva<0xD334CC>();
constexpr auto QueueRoot=ExecutableProfile::Rva<0xD2AC70>();
constexpr auto QueueCount=ExecutableProfile::Rva<0xD2BDE1>();
constexpr auto AppendRva=ExecutableProfile::Rva<0x3B0BA0>();
// Steam 25501027, SHA 0537b2a1...: cdecl(self, property, target, value).
// The native setter, not a raw HP/status patch, owns status side effects.
constexpr unsigned PropertyRva=0x3B4B70;
using PropertyFn=int(__cdecl*)(unsigned,unsigned,int,int);
void Property(unsigned actor,unsigned field,int value) noexcept {
    (void)reinterpret_cast<PropertyFn>(image+PropertyRva)(actor,field,-13,value);
}
struct ActorState {
    Identity key{};Preset preset=Preset::None;GeminiLatch gemini{};
    bool nameOwned=false;
    std::array<Byte,SinMetadata::kNameSize> originalName{},markedName{};
};
std::array<ActorState,8> actors{};
struct Action {
    Identity source{};std::uint64_t token=0;unsigned index=255;
    std::array<Byte,72> row{};std::array<Identity,8> received{};
    bool eligible=false;
};
std::array<Action,31> actions{};
struct ResultFrame {std::uint64_t generation=0;unsigned source=31,target=31;std::uint64_t action=0;Identity targetKey{};Reaction pending{};bool recoveryKilled=false;};
thread_local std::array<ResultFrame,SharedAction::MaximumDepth> results{};
struct FinishFrame {std::uint64_t generation=0;Action action{};unsigned count=0;bool complete=false,retired=false;};
thread_local std::array<FinishFrame,SharedAction::MaximumDepth> finishes{};
struct HitFrame {std::uint64_t generation=0;CommandRow row{};};
thread_local std::array<HitFrame,Bus::MaximumDepth> hits{};
struct HpFrame {Identity key{};unsigned before=0;ResultFrame* result=nullptr;};
thread_local std::array<HpFrame,SharedAction::MaximumDepth> hpFrames{};
template<class T> bool Read(std::uintptr_t address,T& value) noexcept {
    return NativeUiSupport::Copy(&value,reinterpret_cast<const void*>(address),sizeof(value));
}
template<class T> T ReadOr(std::uintptr_t address,T fallback={}) noexcept {T value=fallback;return Read(address,value)?value:fallback;}
std::uintptr_t Actor(unsigned id) noexcept {
    if(id>=31||!image||SharedActor::Busy(id))return 0;
    const auto root=ReadOr<std::uint32_t>(image+ActorRoot);
    return root>=0x10000&&root<=UINT32_MAX-31*0xF90u?root+id*0xF90u:0;
}
bool Alive(std::uintptr_t actor) noexcept {
    return actor&&ReadOr<Byte>(actor+0xDC8)&&ReadOr<int>(actor+0x5D0)>0&&
        !(ReadOr<std::uint16_t>(actor+0x606,1)&5)&&!ReadOr<Byte>(actor+0xDCD,1)&&!ReadOr<Byte>(actor+0xDCE,1);
}
Vitals Life(std::uintptr_t actor) noexcept {
    Vitals value{};if(!Alive(actor))return value;
    const int maximum=ReadOr<int>(actor+0x594);if(maximum<=0)return value;
    value.hp=ReadOr<unsigned>(actor+0x5D0);value.maximum=static_cast<unsigned>(maximum);
    for(unsigned i=0;i<5;++i)if(ReadOr<Byte>(actor+BuffOffsets[i]))value.buffs|=1u<<i;
    value.zombie=(ReadOr<std::uint16_t>(actor+0x606)&2)!=0;return value;
}
void Trace(const char* event,unsigned actor=31,unsigned amount=0) noexcept {
    if(!logger)return;char line[256]{};
    std::snprintf(line,sizeof(line),"[ffx-hooks] Inside Sin %s battle=%llu field=%u formation=%u actor=%u amount=%u\n",event,
        static_cast<unsigned long long>(generation),encounter.field,encounter.formation,actor,amount);logger(line);
}
bool Command(unsigned id,CommandRow& out) noexcept {
    // The four native banks share this 92-byte gameplay prefix. Inspect the
    // actual bank so an OD in monmagic1/command/item cannot earn a SIN extra.
    unsigned table=0,width=0;
    switch(id>>12){
        case 2:table=ExecutableProfile::Rva<0xD2A940>();width=96;break;
        case 3:table=ExecutableProfile::Rva<0xD2A92C>();width=96;break;
        case 4:table=ExecutableProfile::Rva<0xD2A930>();width=92;break;
        case 6:table=ExecutableProfile::Rva<0xD2A934>();width=92;break;
        default:return false;
    }
    const auto root=ReadOr<std::uint32_t>(image+table);std::array<Byte,20> header{};
    if(root<0x10000||!NativeUiSupport::Copy(header.data(),reinterpret_cast<void*>(root),header.size())||
       SinAi::U16(header.data())!=1||SinAi::U16(header.data()+8)!=0||SinAi::U16(header.data()+12)!=width)return false;
    const auto index=id&0xFFFu,count=SinAi::U16(header.data()+10)+1u,offset=SinAi::U32(header.data()+16);
    if(index>=count||count>4096||offset<20||offset>UINT32_MAX-width*count||root>UINT32_MAX-offset-width*count)return false;
    return NativeUiSupport::Copy(out.data(),reinterpret_cast<void*>(root+offset+width*index),out.size());
}
bool CommandsCurrent() noexcept {
    for(const auto& d:Definitions){const auto* bound=bindings.GetBinding(d.preset);CommandRow row{};
        if(!bound||!Command(bound->id,row)||!bindings.Matches(d.preset,row))return false;}
    return true;
}
void RestoreName(unsigned id,ActorState& state) noexcept {
    if(!state.nameOwned||owner!=GetCurrentThreadId()||Actor(id)!=state.key.address||
       ReadOr<std::uint32_t>(state.key.address+0x48)!=state.key.resource||
       SinSpread::ModelFromNative(ReadOr<std::uint16_t>(state.key.address+0xE,0xFFFF))!=state.key.model)return;
    std::array<Byte,SinMetadata::kNameSize> current{};
    if(Read(state.key.address+SinMetadata::kNameOffset,current)&&current==state.markedName)
        (void)NativeUiSupport::Copy(reinterpret_cast<void*>(state.key.address+SinMetadata::kNameOffset),state.originalName.data(),state.originalName.size());
    state.nameOwned=false;
}
void MarkName(ActorState& state) noexcept {
    const auto* preset=Get(state.preset);if(!preset||!state.key.Valid()||state.nameOwned)return;
    if(Read(state.key.address+SinMetadata::kNameOffset,state.originalName)&&
       SinMetadata::NamedView(state.originalName,preset->name,preset->tier,&state.markedName))
        state.nameOwned=NativeUiSupport::Copy(reinterpret_cast<void*>(state.key.address+SinMetadata::kNameOffset),state.markedName.data(),state.markedName.size());
}
void Clear() noexcept {
    for(unsigned i=0;i<actors.size();++i)RestoreName(FirstMonster+i,actors[i]);
    actors={};actions={};ledger.Reset();generation=0;owner=0;encounter={};
}
bool Sync() noexcept {
    if(!accepting.load()||conditional||!reader)return false;
    SinAi::Context context{};
    if(!reader(true,&context)||!context.generation||!EligibleField(context.field,context.formation))return false;
    if(owner&&owner!=GetCurrentThreadId())return false;
    if(syncing)return false;
    if(!CommandsCurrent()){
        // Never silently resume a partially consumed battle after a data swap.
        // A compatible restart is required; conditional latches cannot rearm.
        detail="Inside Sin command binding changed; restart required";accepting=false;
        for(unsigned i=0;i<actors.size();++i)RestoreName(FirstMonster+i,actors[i]);
        if(logger)logger("[ffx-hooks] Inside Sin command bindings rejected; native behavior retained until restart\n");
        return false;
    }
    syncing=true;
    if(context.generation!=generation){Clear();owner=GetCurrentThreadId();generation=context.generation;encounter=context;Trace("battle-admitted");}
    const auto infections=context.enabled?Infections(context.seed,context.visit,static_cast<unsigned>(context.distribution)):0u;
    for(unsigned i=0;i<8;++i){const unsigned id=i+FirstMonster;const auto address=Actor(id);auto& state=actors[i];
        const auto model=SinSpread::ModelFromNative(ReadOr<std::uint16_t>(address+0xE,0xFFFF));
        const auto resource=ReadOr<std::uint32_t>(address+0x48);
        if(!address||!resource||!ModelAllowed(context.field,model)){ledger.Retire(id);state={};continue;}
        if(state.key.Valid()&&state.key.address==address&&state.key.resource==resource&&state.key.model==model){MarkName(state);continue;}
        if(incarnation==UINT64_MAX){accepting=false;syncing=false;return false;}
        state={};state.key={generation,++incarnation,address,resource,model};
        if(Infected(infections,model))state.preset=ChooseForActor(context.seed,context.visit,generation,model,id);
        if(state.preset!=Preset::None){ledger.Bind(id,state.key,state.preset);MarkName(state);Trace(Get(state.preset)->key,id);}
    }
    // The pair may have other allies. Its opposite species and incarnation,
    // not a generic ally-count change, are the authority for this trait.
    auto& a=actors[0];auto& b=actors[1];
    if(Alive(a.key.address)&&Alive(b.key.address)){a.gemini.Bind(a.key,b.key);b.gemini.Bind(b.key,a.key);}
    detail="Inside Sin regional pool ready";syncing=false;return true;
}
bool Same(unsigned id,const Identity& key) noexcept {
    if(id<FirstMonster||id>=EndMonster||!key.Valid()||!(actors[id-FirstMonster].key==key)||generation!=key.battle)return false;
    const auto address=Actor(id);return address==key.address&&ReadOr<std::uint32_t>(address+0x48)==key.resource&&
        SinSpread::ModelFromNative(ReadOr<std::uint16_t>(address+0xE,0xFFFF))==key.model;
}
void Grant(unsigned actor,const Identity& key,unsigned buffs) noexcept {
    if(!accepting.load()||!Same(actor,key)||!Alive(key.address))return;
    for(unsigned i=0;i<5;++i)if(buffs&(1u<<i)){
        // 255 is auto/permanent and is immune to Dispel. Use the game's status
        // setter only for absent temporary effects; never renew Auto-Haste.
        constexpr unsigned fields[]={0x30,0x31,0x32,0x37,0x38};
        if(!ReadOr<Byte>(key.address+BuffOffsets[i]))Property(actor,fields[i],RemovableDuration);
    }
}
void React(unsigned actor,const Identity& key,const Reaction& reaction) noexcept {
    if(!accepting.load()||!reaction.triggered||!Same(actor,key))return;
    auto life=Life(key.address);if(!life.hp)return;
    conditional=true;
    __try {
        if(reaction.heal){
            // Native HP application owns clamping, bookkeeping and death-facing
            // state. Zombie turns the same recovery into positive HP damage.
            const auto amount=HealingAmount(reaction.heal,life.zombie);
            // This is an independent recovery, not additional damage caused by
            // the attacker. Use the admitted native HP leaf so other mods do
            // not attribute Zombie recovery damage to that attack's ledger.
            if(amount)SharedCombat::OriginalHp()(actor,reinterpret_cast<Byte*>(key.address),amount,0,0,0,0);
            // The HP leaf consumes an already resolved signed amount. Its
            // normal caller settles KO separately, so a lethal Zombie heal
            // must use the same native status/KO path instead of leaving HP=0
            // with a live status bit. Field 4 is the KO bit despite old labels.
            if(life.zombie&&ReadOr<int>(key.address+0x5D0)<=0)Property(actor,4,1);
        }
        Grant(actor,key,reaction.buffs);
    }__finally {conditional=false;}
    Trace("conditional",actor,reaction.heal);
}
bool Queue(unsigned actor,std::array<Byte,72>& row,unsigned& index,unsigned& count) noexcept {
    const auto address=Actor(actor);const int length=ReadOr<std::int8_t>(image+QueueCount,-1);
    const unsigned slot=ReadOr<Byte>(address+0xDE5,255);
    if(!address||length<1||length>62||slot>=static_cast<unsigned>(length)||
       !NativeUiSupport::Copy(row.data(),reinterpret_cast<void*>(image+QueueRoot+72u*slot),row.size())||
       row[0]!=actor||!row[3]||row[3]>4||row[2]>row[3])return false;
    index=slot;count=static_cast<unsigned>(length);return true;
}
bool Overdrive(unsigned command) noexcept {
    if((command>=0x61B9&&command<=0x61BB)||(command>=0x61DE&&command<=0x61E0))return true;
    CommandRow row{};if(!Command(command,row))return true;
    const unsigned menu=row[0x16]&0xF8;
    return row[0x26]||row[0x57]||(row[0x1F]&0x42)||menu==8||menu==16;
}
Action* Track(unsigned actor) noexcept {
    if(actor>=31)return nullptr;std::array<Byte,72> row{};unsigned index=0,count=0;
    if(!Queue(actor,row,index,count))return nullptr;
    auto& state=actions[actor];bool same=state.token&&state.index==index&&state.row[0]==row[0]&&state.row[1]==row[1]&&state.row[3]==row[3];
    for(unsigned sub=0;same&&sub<row[3];++sub)same=std::memcmp(state.row.data()+8+sub*16,row.data()+8+sub*16,4)==0;
    if(!same){
        if(serial==UINT64_MAX){accepting=false;return nullptr;}state={};state.token=++serial;state.index=index;state.row=row;
        if(actor>=FirstMonster&&actor<EndMonster)state.source=actors[actor-FirstMonster].key;
        state.eligible=row[1]==0;
        for(unsigned sub=0;sub<row[3];++sub){const unsigned command=SinAi::U16(row.data()+8+sub*16);
            if(bindings.Find(command)!=Preset::None||Overdrive(command))state.eligible=false;}
    }
    return &state;
}
void* BeforeResult(const SharedAction::ResultCall& call) noexcept {
    if(!Sync()||call.source>=31||call.target>=31||call.sub>=4||!SharedAction::resultDepth||SharedAction::resultDepth>results.size())return nullptr;
    auto* action=Track(call.source);if(!action)return nullptr;
    auto& frame=results[SharedAction::resultDepth-1];frame={};frame.generation=generation;frame.source=call.source;frame.target=call.target;frame.action=action->token;
    if(call.target>=FirstMonster&&call.target<EndMonster)frame.targetKey=actors[call.target-FirstMonster].key;
    return &frame;
}
void AfterResult(void* token,const SharedAction::ResultCall&,int,bool completed) noexcept {
    auto& frame=*static_cast<ResultFrame*>(token);
    if(!completed||!Sync()||frame.generation!=generation)return;
    if(Same(frame.target,frame.targetKey)&&actions[frame.source].token==frame.action)
        actions[frame.source].received[frame.target-FirstMonster]=frame.targetKey;
    if(frame.recoveryKilled&&Same(frame.target,frame.targetKey)&&ReadOr<int>(frame.targetKey.address+0x5D0)<=0&&
       !(ReadOr<std::uint16_t>(frame.targetKey.address+0x606)&1))Property(frame.target,4,1);
    if(frame.pending.triggered)React(frame.target,frame.targetKey,frame.pending);
    for(unsigned i=0;i<2;++i){auto& current=actors[i];const auto& partner=actors[1-i];
        if(!current.key.Valid()||!partner.key.Valid()||!Same(FirstMonster+i,current.key)||!Same(FirstMonster+1-i,partner.key))continue;
        const bool dead=ReadOr<int>(partner.key.address+0x5D0)<=0||(ReadOr<std::uint16_t>(partner.key.address+0x606)&1)!=0;
        const auto grant=current.gemini.Death(partner.key,dead,Life(current.key.address));
        if(grant.triggered){React(FirstMonster+i,current.key,grant);Trace("gemini-partner-death",FirstMonster+i);}
    }
}
void* BeforeHp(const SharedCombat::HpCall& call) noexcept {
    if(!accepting.load()||conditional||owner.load()!=GetCurrentThreadId()||!SharedAction::resultDepth||SharedAction::resultDepth>results.size()||call.amount<=0||
       call.target<FirstMonster||call.target>=EndMonster)return nullptr;
    auto& result=results[SharedAction::resultDepth-1];
    if(result.generation!=generation||result.target!=call.target||!Same(call.target,result.targetKey)||
       reinterpret_cast<std::uintptr_t>(call.actor)!=result.targetKey.address)return nullptr;
    auto& frame=hpFrames[SharedAction::resultDepth-1];frame={result.targetKey,Life(result.targetKey.address).hp,&result};return frame.before?&frame:nullptr;
}
void AfterHp(void* token,const SharedCombat::HpCall& call,int,bool completed) noexcept {
    const auto frame=*static_cast<HpFrame*>(token);
    if(!accepting.load()||!completed||conditional||!Same(call.target,frame.key)||!frame.result||frame.result->generation!=generation)return;
    auto& action=actions[frame.result->source];if(action.token!=frame.result->action)return;
    const auto life=Life(frame.key.address);
    if(life.hp<frame.before)action.received[call.target-FirstMonster]=frame.key;
    const auto reaction=ledger.Hit(call.target,frame.key,frame.before,life,true);
    if(reaction.triggered){
        // Recover before the next native hit. Only status grants wait until
        // the result writer finishes copying its precomputed status snapshot.
        React(call.target,frame.key,{true,0,reaction.heal});
        frame.result->pending={true,reaction.buffs,0};
        frame.result->recoveryKilled=ReadOr<int>(frame.key.address+0x5D0)<=0;
    }
}
void* BeforeFinish(const SharedAction::FinishCall& call) noexcept {
    if(!Sync()||call.owner>=31||!SharedAction::finishDepth||SharedAction::finishDepth>finishes.size())return nullptr;
    auto* action=Track(call.owner);std::array<Byte,72> row{};unsigned index=0,count=0;
    if(!action||!Queue(call.owner,row,index,count)||index!=call.index)return nullptr;
    auto& frame=finishes[SharedAction::finishDepth-1];frame={generation,*action,count,row[2]>=row[3]&&!call.preserve,false};return &frame;
}
void AfterFinish(void* token,const SharedAction::FinishCall& call,int result,bool completed) noexcept {
    auto& frame=*static_cast<FinishFrame*>(token);
    if(!completed||result!=1||!Sync()||frame.generation!=generation||call.owner>=31)return;
    const int count=ReadOr<std::int8_t>(image+QueueCount,-1);
    if(count<0||static_cast<unsigned>(count)+1!=frame.count||actions[call.owner].token!=frame.action.token)return;
    frame.retired=true;actions[call.owner]={};
    if(!frame.complete)return;
    for(unsigned i=0;i<8;++i)if(Same(FirstMonster+i,frame.action.received[i]))
        React(FirstMonster+i,frame.action.received[i],ledger.ReceivedAction(FirstMonster+i,frame.action.received[i],Life(frame.action.received[i].address)));
}
void AfterRetirement(void* token,const SharedAction::FinishCall& call,int,bool) noexcept {
    const auto frame=*static_cast<FinishFrame*>(token);
    if(!frame.retired||!frame.complete||!frame.action.eligible||!Sync()||frame.generation!=generation||
       !Same(call.owner,frame.action.source)||!Alive(frame.action.source.address))return;
    auto& state=actors[call.owner-FirstMonster];std::uint32_t living=0;
    for(unsigned id=0;id<18;++id)if(Alive(Actor(id)))living|=1u<<id;
    const auto roll=SinSpread::Mix(static_cast<unsigned>(frame.action.token)^encounter.seed^static_cast<unsigned>(generation));
    const auto plan=Extra(state.preset,Life(state.key.address).buffs,living,roll);
    const auto* binding=bindings.GetBinding(plan.preset);if(!binding)return;
    const int before=ReadOr<std::int8_t>(image+QueueCount,-1);if(before<0||before>=62)return;
    std::array<Byte,72> proposal{};proposal[0]=static_cast<Byte>(call.owner);proposal[1]=proposal[3]=1;
    const std::uint16_t none=255;std::memcpy(proposal.data()+8,&binding->id,2);std::memcpy(proposal.data()+10,&none,2);
    const std::uint32_t targets=plan.self?1u<<call.owner:plan.targets;std::memcpy(proposal.data()+16,&targets,4);
    using Append=int(__cdecl*)(unsigned,const Byte*,unsigned,unsigned,unsigned);
    const int added=reinterpret_cast<Append>(image+AppendRva)(call.owner,proposal.data(),0,0,1);
    std::array<Byte,72> actual{};
    if(added!=-1||ReadOr<std::int8_t>(image+QueueCount,-1)!=before+1||
       !NativeUiSupport::Copy(actual.data(),reinterpret_cast<void*>(image+QueueRoot+72u*before),actual.size())||actual!=proposal){
        accepting=false;detail="Inside Sin queue ownership lost; admission closed";Trace("queue-rejected",call.owner);return;
    }
    Trace("extra-enqueued",call.owner,binding->id);
}
void* BeforeDamage(const Bus::DamageCall& call,const void*& forwarded) noexcept {
    if(!Sync()||!Bus::currentDamage||!Bus::currentDamage->depth||Bus::currentDamage->depth>hits.size())return nullptr;
    const auto preset=bindings.Find(call.commandId);const auto* d=Get(preset);
    if(!d||!d->self||call.user!=call.target||call.user<FirstMonster||call.user>=EndMonster||
       Actor(call.user)!=reinterpret_cast<std::uintptr_t>(call.userActor)||call.userActor!=call.targetActor)return nullptr;
    auto& frame=hits[Bus::currentDamage->depth-1];frame={};frame.generation=generation;
    if(!NativeUiSupport::Copy(frame.row.data(),forwarded,frame.row.size())||!bindings.Matches(preset,frame.row))return nullptr;
    FilterExistingBuffs(frame.row,Life(reinterpret_cast<std::uintptr_t>(call.targetActor)).buffs);forwarded=frame.row.data();return &frame;
}
void ResetData(Bus::ResetReason) noexcept {if(!owner||owner==GetCurrentThreadId())Clear();}
void Battle() noexcept {ResetData(Bus::ResetReason::BattleStart);}
void Retire(unsigned,unsigned actor) noexcept {
    if(owner&&owner!=GetCurrentThreadId())return;
    if(actor>=FirstMonster&&actor<EndMonster){RestoreName(actor,actors[actor-FirstMonster]);actors[actor-FirstMonster]={};ledger.Retire(actor);}
    if(actor<actions.size())actions[actor]={};
}
const SharedAction::Observer actionObserver{BeforeResult,AfterResult,BeforeFinish,AfterFinish,AfterRetirement};
const SharedCombat::HpObserver hpObserver{BeforeHp,AfterHp};
const SharedActor::Observer actorObserver{Retire,nullptr};
const SharedBattleRuntime::ActionObserver battleObserver{Battle};
const Bus::Observer damageObserver{BeforeDamage,nullptr,nullptr,ResetData};
bool Profile(std::uintptr_t base) noexcept {
#ifndef FFXHOOKS_TARGET_STEAM_20261001
    (void)base;return false;
#else
    constexpr EquipmentWorkshop::Evidence::Span effects[]={
        {PropertyRva,{0x55,0x8B,0xEC,0x83,0xEC,0x2C,0x53,0x8B,0x5D,0x14,0x56,0x57,0x68,0xFF,0x00,0x00,
        0x00,0x33,0xC0,0x6A,0x01,0x53,0x89,0x45,0xE0,0xE8,0x32,0x55,0xFE,0xFF,0x68,0xFF},{255,255,255,255}}
    };
    if(!NativeUiSupport::Profile(base,effects))return false;
    Byte expected[16]={0x55,0x8B,0xEC,0x51,0x53,0x0F,0xBE,0x1D,0,0,0,0,0x89,0x5D,0xFC,0x83};
    const auto address=static_cast<std::uint32_t>(base+QueueCount);std::memcpy(expected+8,&address,4);
    Byte actual[16]{};return NativeUiSupport::Copy(actual,reinterpret_cast<void*>(base+AppendRva),16)&&!std::memcmp(actual,expected,16);
#endif
}
bool Attach(std::uintptr_t base,const Bindings& selected,SinAi::ContextReader context,Log log,bool nativeHooks){
    if(accepting.load()||!context||!selected.Ready()||!Profile(base))return false;
    if(nativeHooks&&(!EquipmentWorkshop::CombatProducerReady()||!SharedAction::Start(base)||!SharedCombat::Start(base)||!SharedActor::Start(base)))return false;
    image=base;bindings=selected;reader=context;logger=log;Clear();
    if(!SharedAction::Subscribe(SharedAction::Slot::InsideSin,&actionObserver)||
       !SharedCombat::RegisterHpObserver(&hpObserver)||!SharedActor::Subscribe(SharedActor::Slot::InsideSin,&actorObserver)||
       !SharedBattleRuntime::RegisterActionObserver(SharedBattleRuntime::ActionConsumer::InsideSin,&battleObserver)||
       !Bus::Subscribe(Bus::Slot::InsideSin,&damageObserver)){Stop();return false;}
    accepting=true;detail="Inside Sin waiting for a supported natural encounter";return true;
}
}
bool Requested() noexcept {return Settings::Requested();}
void Prime(bool validateOnly) noexcept {
    // Request only the existing combat producer before Workshop installs. The
    // descriptor cannot mutate until late startup validates the complete pack.
    if(Requested()&&!validateOnly)(void)Bus::Subscribe(Bus::Slot::InsideSin,&damageObserver);
}
bool Start(std::uintptr_t base,const wchar_t* path,SinAi::ContextReader context,Log log){
    if(!path||accepting.load())return accepting.load();
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};std::string bytes;bool read=false;
    if(GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<=16384){bytes.resize(static_cast<std::size_t>(size.QuadPart));DWORD actual=0;
        read=ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&actual,nullptr)&&actual==bytes.size();}
    CloseHandle(file);Bindings selected{};bool enabled=false;
    const auto code=read?ParseBindings(bytes,selected,enabled):BindingCode::Invalid;
    if(code!=BindingCode::Ready){detail=code==BindingCode::Disabled?"Inside Sin is OFF":code==BindingCode::Unresolved?
        "Inside Sin command references are unresolved; native behavior retained":"Inside Sin manifest is invalid; native behavior retained";return false;}
    if(!Attach(base,selected,context,log,true)){detail="Inside Sin native producer is unavailable";return false;}return true;
}
void RequestStop() noexcept {
    accepting=false;detail="Inside Sin is OFF";
}
void Stop() noexcept {
    RequestStop();
    if(!owner||owner==GetCurrentThreadId())Clear();
    SharedAction::Unsubscribe(SharedAction::Slot::InsideSin,&actionObserver);SharedCombat::UnregisterHpObserver(&hpObserver);
    SharedActor::Unsubscribe(SharedActor::Slot::InsideSin,&actorObserver);
    SharedBattleRuntime::UnregisterActionObserver(SharedBattleRuntime::ActionConsumer::InsideSin,&battleObserver);
    Bus::Unsubscribe(Bus::Slot::InsideSin,&damageObserver);
    // Trampolines and immutable descriptors remain process-lived. No detached
    // callback can write; native effects already earned keep their game lifetime.
}
const char* Detail() noexcept {return detail.load();}
#ifdef FFXHOOKS_TESTING
bool PrepareForTests(std::uintptr_t base,const Bindings& selected,SinAi::ContextReader context,Log log){return Attach(base,selected,context,log,false);}
void ResetForTests() noexcept {Clear();}
#endif
} // namespace FfxHooks::InsideSin::Runtime
