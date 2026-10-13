#include "OmegaT7Runtime.h"
#include "OmegaT7Native.h"
#include "OmegaT7Evidence.h"
#include "OmegaT7Session.h"
#include "OmegaT7Settings.h"
#include "OmegaT7Notice.h"
#include "OmegaT7Bridge.h"
#include "OmegaUltimaCore.h"
#include "InsideSinCore.h"
#include "SharedActionRuntime.h"
#include "SharedBattleRuntime.h"
#include "SharedTurnRuntime.h"
#include "SharedQueueRuntime.h"
#include "CombatExtensionBus.h"
#include "EquipmentWorkshopRuntime.h"
#include "SinMetadataCore.h"
#include <atomic>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace FfxHooks::OmegaT7::Runtime {
namespace {
namespace A=SharedAction;namespace B=CombatExtensions;namespace N=Native;
using Byte=unsigned char;using Key=InsideSin::Identity;
std::atomic<bool> accepting{false},primed{false};std::atomic<DWORD> owner{0};
std::atomic<const char*> detail{"Omega T7 is OFF"};
std::uintptr_t image=0;Bindings bindings;SinAi::ContextReader reader=nullptr;Log logger=nullptr;
std::uint64_t generation=0,serial=0,incarnation=0,battleSeed=0,campaignSession=0;SinAi::Context encounter{};Assignment plan{};Roster roster{};
thread_local bool reacting=false,syncing=false;
#ifdef FFXHOOKS_TESTING
std::uint64_t testSeed=0;
#endif
struct ActorState {
    Key key{};unsigned profile=None;State rule{};Vitals last{};
    unsigned walkSteps=0,turnSequence=0,absorbBit=0;bool absorbOriginal=false,nameOwned=false,alive=false;
    std::uint16_t lastStatus=0;
    std::array<Byte,40> name{},marked{};
    std::uint64_t magicToken=0;unsigned magicSource=31,magicSub=4;
};
std::array<ActorState,8> actors{};
PreviewSnapshot lastPreview{};
SRWLOCK previewLock=SRWLOCK_INIT;
std::atomic<DWORD> previewOwner{0};
std::uint64_t previewCampaign=0;
struct Action {
    std::uint64_t token=0;unsigned index=255;std::uintptr_t source=0;
    std::array<Byte,72> row{};unsigned nativeCount=0;bool eligible=false,started=false;
};
std::array<Action,31> actions{};
struct Extra {Key key{};unsigned index=255,command=0,boost=0,sub=0;bool started=false;std::array<Byte,72> row{};};
std::array<Extra,8> extras{};
struct QueueFrame {Extra extra{};unsigned before=0;};
thread_local std::array<QueueFrame,SharedQueue::MaximumDepth> queueFrames{};
using AiFunction=int(__cdecl*)(unsigned,unsigned,unsigned,unsigned,unsigned,Byte*);
void* aiOriginal[1]{};
std::atomic<bool> aiInstalled{false};
thread_local unsigned aiDepth=0;
struct AiFrame {Key key{};std::uintptr_t packet=0;unsigned count=0,index=0;Byte nativeState=0;};
struct ResultFrame {
    std::uint64_t generation=0,action=0;unsigned command=0;Key target{},source{};
    Vitals before{},sourceBefore{};std::uint16_t status=0;Byte blind=0;std::array<Byte,4> negatives{};
    std::array<std::array<Byte,4>,2> groups{};bool recoveryKilled=false;
};
struct FinishFrame {std::uint64_t generation=0;Action action{};Key source{};Extra extra{};unsigned count=0;bool complete=false,retired=false;};
struct HitFrame {std::uint64_t generation=0;CommandRow row{};unsigned boost=0;};
struct HpFrame {std::uint64_t generation=0;Key target{};Vitals before{};ResultFrame* result=nullptr;};
thread_local std::array<ResultFrame,A::MaximumDepth> results{};
thread_local std::array<FinishFrame,A::MaximumDepth> finishes{};
thread_local std::array<HitFrame,B::MaximumDepth> hits{};
thread_local std::array<HpFrame,A::MaximumDepth> hpFrames{};
constexpr auto QueueRoot=ExecutableProfile::Rva<0xD2AC70>(),QueueCount=ExecutableProfile::Rva<0xD2BDE1>();
bool Monster(unsigned id) noexcept {return id>=20&&id<28;}
void Trace(const char* message,unsigned actor=31,unsigned amount=0) noexcept {
    if(!logger)return;char text[240]{};std::snprintf(text,sizeof(text),"[ffx-hooks] Omega T7 %s field=%u formation=%u battle=%llu actor=%u value=%u\n",message,encounter.field,encounter.formation,static_cast<unsigned long long>(generation),actor,amount);logger(text);
}
bool Same(unsigned id,const Key& key) noexcept {
    if(!Monster(id)||!key.Valid()||!(actors[id-20].key==key)||generation!=key.battle)return false;
    const auto address=N::Actor(image,id);return address==key.address&&N::ReadOr<std::uint32_t>(address+0x48)==key.resource&&
        SinSpread::ModelFromNative(N::ReadOr<std::uint16_t>(address+0xE,0xFFFF))==key.model;
}
void Restore(unsigned id,ActorState& state) noexcept {
    if(owner.load()!=GetCurrentThreadId()||!Same(id,state.key))return;
    if(state.absorbBit){const auto mask=N::ReadOr<Byte>(state.key.address+0x5DA);
        if((mask&state.absorbBit)&&!state.absorbOriginal)N::BasicAbsorb(image,id,state.absorbBit,false);state.absorbBit=0;}
    if(state.nameOwned){std::array<Byte,40> value{};if(N::Read(state.key.address+0x540,value)&&value==state.marked)
        NativeUiSupport::Copy(reinterpret_cast<void*>(state.key.address+0x540),state.name.data(),state.name.size());state.nameOwned=false;}
}
void Clear() noexcept {
    for(unsigned i=0;i<8;++i)Restore(i+20,actors[i]);
    actors={};actions={};extras={};generation=0;battleSeed=0;campaignSession=0;plan.fill(None);roster={};encounter={};owner=0;
}
void Name(ActorState& actor) noexcept {
    if(actor.profile>=48||actor.nameOwned||!N::Alive(actor.key.address))return;
    if(N::Read(actor.key.address+0x540,actor.name)&&SinMetadata::NamedView(actor.name,Catalog[actor.profile].name,7,&actor.marked))
        actor.nameOwned=NativeUiSupport::Copy(reinterpret_cast<void*>(actor.key.address+0x540),actor.marked.data(),actor.marked.size());
}
bool CommandsCurrent() noexcept {
    for(unsigned i=0;i<26;++i){CommandRow row{};if(!N::Command(image,bindings.rows[i].id,row)||!bindings.Matches(static_cast<Skill>(i),row))return false;}return true;
}
std::uint64_t SeedFor(std::uint64_t battle) noexcept {
#ifdef FFXHOOKS_TESTING
    if(testSeed)return testSeed;
#endif
    return Session::BattleSeed(battle);
}
bool SessionReady() noexcept {
#ifdef FFXHOOKS_TESTING
    if(testSeed)return true;
#endif
    return Session::Ready();
}
std::uint64_t CampaignSession() noexcept {
#ifdef FFXHOOKS_TESTING
    if(testSeed)return 1;
#endif
    Campaign state;std::uint64_t session=0;
    return Session::Snapshot(state,session)?session:0;
}
void Apply(unsigned id,const Key& key,const Effect& effect) noexcept;
void Observe(unsigned id,Event e) noexcept;
void Deaths() noexcept;
bool Sync() noexcept {
    if(!accepting.load()||reacting||!reader)return false;
    if(!SessionReady()){
        if(owner.load()==GetCurrentThreadId()){accepting=false;for(unsigned i=0;i<8;++i)Restore(i+20,actors[i]);detail="Omega campaign unavailable; admission closed";}
        return false;
    }
    SinAi::Context context;if(!reader(true,&context)||!context.generation||!OmegaUltima::EligibleFormation(context.field,context.formation))return false;
    if(owner.load()&&owner.load()!=GetCurrentThreadId())return false;
    if(!N::BattleOngoing(image))return false;
    if(syncing)return false;
    if(!CommandsCurrent()){accepting=false;detail="Omega T7 bindings changed; restart required";for(unsigned i=0;i<8;++i)Restore(i+20,actors[i]);Trace("binding-rejected");return false;}
    syncing=true;
    if(generation!=context.generation){
        Clear();owner=GetCurrentThreadId();generation=context.generation;encounter=context;battleSeed=SeedFor(generation);campaignSession=CampaignSession();
        const auto nativeRoster=N::ReadOr<std::uint32_t>(image+ExecutableProfile::Rva<0xD2A9C0>());
        if(!battleSeed||nativeRoster<0x10000||nativeRoster>UINT32_MAX-28){syncing=false;generation=0;return false;}
        for(unsigned i=0;i<8;++i){const auto model=SinSpread::ModelFromNative(N::ReadOr<std::uint16_t>(nativeRoster+12+2*i,0xFFFF));roster[i]=model==0xFFFF?0:model;}
        plan=Assign(battleSeed,context.field,context.formation,roster);Trace("battle-admitted");
    }
    bool previewChanged=false;
    for(unsigned i=0;i<8;++i){const unsigned id=i+20;auto& state=actors[i];const auto address=N::Actor(image,id);
        const auto model=SinSpread::ModelFromNative(N::ReadOr<std::uint16_t>(address+0xE,0xFFFF));
        const auto resource=N::ReadOr<std::uint32_t>(address+0x48);
        if(!address||!resource||!Combatant(model))continue;
        if(!state.key.Valid()||state.key.address!=address||state.key.resource!=resource||state.key.model!=model){
            Restore(id,state);if(incarnation==UINT64_MAX){accepting=false;syncing=false;return false;}
            state={};state.key={generation,++incarnation,address,resource,model};state.profile=plan[i];
            if(!Compatible(state.profile,model)){std::uint64_t used=0;for(unsigned n=0;n<8;++n)if(n!=i&&plan[n]<48)used|=1ull<<plan[n];
                const auto roll=Mix(battleSeed^Mix(i)^Mix(model));state.profile=Choose(model,static_cast<unsigned>(roll%100),Mix(roll),used);plan[i]=state.profile;}
            state.last=N::Life(address);state.lastStatus=N::ReadOr<std::uint16_t>(address+0x606);state.alive=state.last.hp!=0;
        }
        const auto maximum=N::ReadOr<unsigned>(address+0x594);
        if(model==910&&maximum>150000&&!Bridge::OwnsMiniHp(id,address,resource,maximum)){
            accepting=false;detail="Omega m910 HP contract changed; restart required";syncing=false;return false;
        }
        if(N::Alive(address))state.alive=true;
        const bool named=state.nameOwned;Name(state);
        if(!named&&state.nameOwned)previewChanged=true;
    }
    if(previewChanged){
        AcquireSRWLockExclusive(&previewLock);
        lastPreview={};lastPreview.field=encounter.field;lastPreview.formation=encounter.formation;
        for(const auto& actor:actors)if(actor.key.Valid()&&actor.profile<48&&actor.nameOwned)
            lastPreview.monsters[lastPreview.count++]={actor.key.model,actor.profile,actor.name};
        previewCampaign=campaignSession;previewOwner=GetCurrentThreadId();
        ReleaseSRWLockExclusive(&previewLock);
    }
    // Read-only changes outside damage-result callbacks (for example native
    // poison ticks or scripted breaks) still obey crossing/once semantics.
    for(unsigned i=0;i<8;++i){auto& state=actors[i];if(!Same(i+20,state.key))continue;
        Event e;e.before=state.last;e.after=N::Life(state.key.address);e.kind=e.after.hp<e.before.hp?EventKind::Received:EventKind::VitalsChanged;
        const auto status=N::ReadOr<std::uint16_t>(state.key.address+0x606);e.armorBreakAdded=!(state.lastStatus&0x40)&&(status&0x40);e.mentalBreakAdded=!(state.lastStatus&0x80)&&(status&0x80);
        if(e.armorBreakAdded||e.mentalBreakAdded)e.kind=EventKind::Received;
        Observe(i+20,e);state.lastStatus=status;state.last=N::Life(state.key.address);
    }
    Deaths();syncing=false;return true;
}
void Apply(unsigned id,const Key& key,const Effect& e) noexcept {
    if(!accepting.load()||!Same(id,key)||!N::Alive(key.address)||e.Empty())return;
    reacting=true;
    __try {
        if(e.healPercent)N::Heal(image,id,key.address,e.healPercent);
        if(!accepting.load()||!N::Alive(key.address))__leave;
        N::Grant(image,id,key.address,e.self);N::Stacks(image,id,key.address,e.stacks);
        if(e.group)for(unsigned i=0;i<8;++i){const auto& target=actors[i];if(Same(i+20,target.key)&&N::Alive(target.key.address))N::Grant(image,i+20,target.key.address,e.group);}
        if(e.ctbPercent)N::Advance(image,id,key.address,e.ctbPercent);
        auto& state=actors[id-20];
        if(e.restoreAbsorb&&state.absorbBit){if(!state.absorbOriginal&&(N::ReadOr<Byte>(key.address+0x5DA)&state.absorbBit))N::BasicAbsorb(image,id,state.absorbBit,false);state.absorbBit=0;}
        if(e.absorb){state.absorbBit=e.absorb;state.absorbOriginal=(N::ReadOr<Byte>(key.address+0x5DA)&e.absorb)!=0;N::BasicAbsorb(image,id,e.absorb,true);}
        if(e.slowTarget<31){const auto target=N::Actor(image,e.slowTarget);N::Slow(image,id,key.address,e.slowTarget,target,bindings.Get(Skill::Slowga).row);}
    }__finally {reacting=false;}
}
void Observe(unsigned id,Event e) noexcept {
    // Native work between before/after callbacks may complete the battle.
    if(!Monster(id)||!accepting.load()||!N::BattleOngoing(image))return;auto& state=actors[id-20];if(!Same(id,state.key)||state.profile>=48)return;
    const auto effect=OmegaT7::Observe(state.profile,state.rule,e);Apply(id,state.key,effect);state.last=N::Life(state.key.address);
}
void Deaths() noexcept {
    std::array<bool,8> died{};
    for(unsigned i=0;i<8;++i){auto& state=actors[i];if(!Same(i+20,state.key))continue;
        const bool death=N::ReadOr<int>(state.key.address+0x5D0)<=0||(N::ReadOr<std::uint16_t>(state.key.address+0x606)&1);
        died[i]=state.alive&&death;state.alive=!death&&N::Alive(state.key.address);}
    for(unsigned victim=0;victim<8;++victim)if(died[victim])for(unsigned i=0;i<8;++i)if(i!=victim&&actors[i].alive){Event e;e.kind=EventKind::AllyDeath;e.before=e.after=N::Life(actors[i].key.address);Observe(i+20,e);}
}
bool Queue(unsigned id,std::array<Byte,72>& row,unsigned& index,unsigned& count) noexcept {
    const auto actor=N::Actor(image,id);const int length=N::ReadOr<std::int8_t>(image+QueueCount,-1);const auto at=N::ReadOr<Byte>(actor+0xDE5,255);
    if(!actor||length<1||length>62||at>=length||!NativeUiSupport::Copy(row.data(),reinterpret_cast<void*>(image+QueueRoot+72u*at),row.size())||
       row[0]!=id||!row[3]||row[3]>4||row[2]>row[3])return false;
    index=at;count=static_cast<unsigned>(length);return true;
}
unsigned CommandAt(const Action& action,unsigned sub) noexcept {return sub<action.row[3]?N::Word(action.row.data()+8+16*sub):0;}
bool IsOd(unsigned command) noexcept {
    if(NativeOd(command))return true;CommandRow row{};if(!N::Command(image,command,row))return true;
    const unsigned menu=row[0x16]&0xF8;return row[0x26]||row[0x57]||(row[0x1F]&0x42)||menu==8||menu==16;
}
Action* Track(unsigned id) noexcept {
    if(id>=31)return nullptr;std::array<Byte,72> row{};unsigned index=0,count=0;if(!Queue(id,row,index,count))return nullptr;
    auto& state=actions[id];bool same=state.token&&state.index==index&&state.row[0]==row[0]&&state.row[1]==row[1]&&state.row[3]==row[3]&&state.source==N::Actor(image,id);
    for(unsigned n=0;same&&n<row[3];++n)same=std::memcmp(state.row.data()+8+16*n,row.data()+8+16*n,4)==0;
    if(!same){if(serial==UINT64_MAX){accepting=false;return nullptr;}state={};state.token=++serial;state.source=N::Actor(image,id);state.index=index;state.row=row;state.eligible=row[1]==0;
        state.nativeCount=row[3];
        if(Monster(id)){const auto& extra=extras[id-20];
            if(extra.key.Valid()&&Same(id,extra.key)&&row[1]==0&&row[3]==extra.sub+1&&
               !std::memcmp(row.data()+8,extra.row.data()+8,64))state.nativeCount=extra.sub;}
        for(unsigned n=0;n<state.nativeCount;++n)if(bindings.Find(CommandAt(state,n))<26||IsOd(CommandAt(state,n)))state.eligible=false;
    }
    state.row=row;return &state;
}
Extra* Owned(unsigned id,const Action& action) noexcept {
    if(!Monster(id))return nullptr;auto& extra=extras[id-20];
    if(!Same(id,extra.key)||!extra.sub||extra.sub>3||action.row[1]!=0||action.row[3]!=extra.sub+1||N::Word(action.row.data()+8+16*extra.sub)!=extra.command||
       std::memcmp(action.row.data()+8,extra.row.data()+8,64))return nullptr;
    // Native counters may move a queued packet. Its full command payload and
    // actor lease identify it; a stale queue index never owns another row.
    const int count=N::ReadOr<std::int8_t>(image+QueueCount,-1);unsigned matches=0;
    if(count<1||count>62||action.index>=static_cast<unsigned>(count))return nullptr;
    for(int i=0;i<count;++i){std::array<Byte,72> row{};if(!N::Read(image+QueueRoot+72u*i,row))return nullptr;
        if(row[0]==id&&row[1]==0&&row[3]==extra.sub+1&&!std::memcmp(row.data()+8,extra.row.data()+8,64))++matches;}
    if(matches!=1)return nullptr;extra.index=action.index;
    return &extra;
}
void* DamageEnter(const B::DamageCall& call,const void*& command) noexcept {
    if(!Sync()||!B::currentDamage||!B::currentDamage->depth||B::currentDamage->depth>hits.size())return nullptr;
    auto* action=Track(call.user);if(!action)return nullptr;action->started=true;
    auto* extra=Owned(call.user,*action);if(!extra||call.commandId!=extra->command||bindings.Find(call.commandId)>=26)return nullptr;
    if(!extra->started){extra->started=true;extra->boost=TakeCharge(actors[call.user-20].rule);}
    auto& hit=hits[B::currentDamage->depth-1];hit={};hit.generation=generation;hit.boost=extra->boost;
    const auto skill=static_cast<Skill>(bindings.Find(call.commandId));
    if(!NativeUiSupport::Copy(hit.row.data(),command,hit.row.size())||!bindings.Matches(skill,hit.row))return nullptr;
    const unsigned buffs=BuffsFor(skill),present=N::Life(reinterpret_cast<std::uintptr_t>(call.targetActor)).buffs;
    if(buffs)for(unsigned i=0;i<9;++i)if(present&(1u<<i)){hit.row[0x2E+ChanceIndex[i]]=0;hit.row[0x47+DurationIndex[i]]=0;}
    command=hit.row.data();return &hit;
}
std::int32_t DamageAmount(void* token,const B::DamageCall&,std::int32_t amount) noexcept {
    const auto& hit=*static_cast<HitFrame*>(token);if(!accepting.load()||hit.generation!=generation||!hit.boost||amount<=0)return amount;
    const auto wide=std::int64_t(amount)*(100+hit.boost)/100;return static_cast<std::int32_t>(wide>INT32_MAX?INT32_MAX:wide);
}
void* BeforeResult(const A::ResultCall& call) noexcept {
    if(!Sync()||call.target>=31||call.source>=31||call.sub>=4||!A::resultDepth||A::resultDepth>results.size())return nullptr;
    auto* action=Track(call.source);if(!action)return nullptr;action->started=true;
    if(auto* extra=Owned(call.source,*action)){if(call.sub==extra->sub&&!extra->started){extra->started=true;extra->boost=TakeCharge(actors[call.source-20].rule);}}
    auto& frame=results[A::resultDepth-1];frame={};frame.generation=generation;frame.action=action->token;frame.command=CommandAt(*action,call.sub);
    const auto target=N::Actor(image,call.target);frame.before=N::Life(target);frame.status=N::ReadOr<std::uint16_t>(target+0x606);frame.blind=N::ReadOr<Byte>(target+0x60A);
    frame.negatives={N::ReadOr<Byte>(target+0x608),N::ReadOr<Byte>(target+0x609),frame.blind,N::ReadOr<Byte>(target+0x614)};
    if(Monster(call.target))frame.target=actors[call.target-20].key;
    if(Monster(call.source)){frame.source=actors[call.source-20].key;frame.sourceBefore=N::Life(frame.source.address);}
    for(unsigned i=0;i<2;++i)if(!N::Read(target+0x774+728*i,frame.groups[i]))return nullptr;
    return &frame;
}
void AfterResult(void* token,const A::ResultCall& call,int,bool complete) noexcept {
    auto& frame=*static_cast<ResultFrame*>(token);if(!complete||!Sync()||frame.generation!=generation||actions[call.source].token!=frame.action)return;
    const auto target=N::Actor(image,call.target);CommandRow command{};if(!N::Command(image,frame.command,command))return;
    bool landed=false,evaded=false,critical=false,absorbed=false;unsigned positive=0;
    for(unsigned group=0;group<2;++group){std::array<Byte,4> after{};const auto& before=frame.groups[group];
        if(!N::Read(target+0x774+728*group,after)||before[2]!=call.source||before[3]!=call.sub||after[2]!=call.source||after[3]!=call.sub||after[0]<=before[0]||after[0]>before[1]||after[0]>16)continue;
        for(unsigned hit=before[0];hit<after[0];++hit){std::array<Byte,44> info{};if(!N::Read(target+0x774+728*group+24+44*hit,info))continue;
            landed=landed||info[1]==0;evaded=evaded||info[1]==1;critical=critical||(N::Word(info.data()+0x18)&0x100)!=0;
            int hp=0;std::memcpy(&hp,info.data()+0x20,4);if(info[1]==0&&hp>0)positive+=static_cast<unsigned>(hp);
            absorbed=absorbed||(info[1]==0&&hp<0&&!(command[0x20]&0x10));}
    }
    if(frame.recoveryKilled&&Same(call.target,frame.target)&&N::ReadOr<int>(target+0x5D0)<=0&&!(N::ReadOr<std::uint16_t>(target+0x606)&1))N::Property(image,call.target,4,1);
    const auto life=N::Life(target);const auto status=N::ReadOr<std::uint16_t>(target+0x606);
    unsigned element=command[0x2D];if(N::Dword(command.data()+0x1C)&0x40000)element|=N::ReadOr<Byte>(N::Actor(image,call.source)+0x5D9);
    Event e;e.kind=EventKind::Received;e.before=frame.before;e.after=life;e.loss=positive;e.element=N::Elements(element);
    e.physical=(command[0x20]&3)==1;e.magic=(command[0x20]&3)==2;e.landed=landed;e.evaded=evaded;e.critical=critical;e.absorbed=absorbed;
    e.dispel=(command[0x20]&0x20)!=0;e.removedBuffs=frame.before.buffs&~life.buffs;
    e.armorBreakAdded=!(frame.status&0x40)&&(status&0x40);e.mentalBreakAdded=!(frame.status&0x80)&&(status&0x80);
    if(Same(call.target,frame.target)){
        auto& state=actors[call.target-20];
        if(e.magic&&positive){const bool duplicate=state.magicToken==frame.action&&state.magicSource==call.source&&state.magicSub==call.sub;
            if(duplicate)e.magic=false;else{state.magicToken=frame.action;state.magicSource=call.source;state.magicSub=call.sub;}}
        Observe(call.target,e);
        if(frame.command==0x3016&&Mimic(state.key.model)){auto steal=e;steal.kind=EventKind::StealAttempt;Observe(call.target,steal);}
    }
    if(Same(call.source,frame.source)){
        auto own=e;own.kind=EventKind::Dealt;own.before=frame.sourceBefore;own.after=N::Life(frame.source.address);own.target=call.target;own.targetAlive=life.hp!=0;
        own.targetBlind=frame.blind!=0;own.targetArmorBreak=(frame.status&0x40)!=0;own.poisonApplied=!(frame.status&8)&&(status&8);
        own.negativeApplied=(status&~frame.status&0xFFE)!=0||(!(frame.status&1)&&(status&1)&&command[0x2E]>0);
        const unsigned negativeOffsets[]={0x608,0x609,0x60A,0x614};
        for(unsigned i=0;i<4;++i)if(N::ReadOr<Byte>(target+negativeOffsets[i])>frame.negatives[i])own.negativeApplied=true;
        own.nativeAi=actions[call.source].eligible&&call.sub<actions[call.source].nativeCount;Observe(call.source,own);
    }
    Deaths();
}
void* BeforeHp(const SharedCombat::HpCall& call) noexcept {
    if(!accepting.load()||reacting||owner.load()!=GetCurrentThreadId()||!Monster(call.target)||!A::resultDepth||A::resultDepth>results.size()||call.amount<=0)return nullptr;
    auto& result=results[A::resultDepth-1];if(result.generation!=generation||!Same(call.target,result.target)||reinterpret_cast<std::uintptr_t>(call.actor)!=result.target.address)return nullptr;
    auto& frame=hpFrames[A::resultDepth-1];frame={generation,result.target,N::Life(result.target.address),&result};return frame.before.hp?&frame:nullptr;
}
void AfterHp(void* token,const SharedCombat::HpCall& call,int,bool complete) noexcept {
    const auto frame=*static_cast<HpFrame*>(token);if(!accepting.load()||!complete||reacting||frame.generation!=generation||!Same(call.target,frame.target))return;
    auto& state=actors[call.target-20];if(state.profile!=18&&state.profile!=47)return;
    Event e;e.kind=EventKind::Hit;e.before=frame.before;e.after=N::Life(frame.target.address);e.landed=true;
    e.loss=e.after.hp<e.before.hp?e.before.hp-e.after.hp:0;Observe(call.target,e);
    if(N::ReadOr<int>(frame.target.address+0x5D0)<=0&&frame.result)frame.result->recoveryKilled=true;
}
void* BeforeFinish(const A::FinishCall& call) noexcept {
    if(!Sync()||call.owner>=31||!A::finishDepth||A::finishDepth>finishes.size())return nullptr;
    auto* action=Track(call.owner);std::array<Byte,72> row{};unsigned index=0,count=0;
    if(!action||!Queue(call.owner,row,index,count)||call.index!=index)return nullptr;
    auto& frame=finishes[A::finishDepth-1];frame={};frame.generation=generation;frame.action=*action;frame.count=count;frame.complete=row[2]>=row[3]&&!call.preserve;
    if(Monster(call.owner)){frame.source=actors[call.owner-20].key;if(const auto* extra=Owned(call.owner,*action))frame.extra=*extra;}
    return &frame;
}
void AfterFinish(void* token,const A::FinishCall& call,int result,bool complete) noexcept {
    auto& frame=*static_cast<FinishFrame*>(token);if(!complete||result!=1||!Sync()||frame.generation!=generation)return;
    const int count=N::ReadOr<std::int8_t>(image+QueueCount,-1);if(count<0||static_cast<unsigned>(count)+1!=frame.count||actions[call.owner].token!=frame.action.token)return;
    frame.retired=true;actions[call.owner]={};
    for(auto& action:actions)if(action.token&&action.index>call.index)--action.index;
    if(frame.extra.key.Valid()&&Monster(call.owner)&&Same(call.owner,frame.extra.key))extras[call.owner-20]={};
    if(!frame.complete)return;
    if(Same(call.owner,frame.source)){
        auto& actor=actors[call.owner-20];Event e;e.before=e.after=N::Life(frame.source.address);
        if(frame.extra.key.Valid()&&frame.extra.started){e.kind=EventKind::ExtraFinished;Observe(call.owner,e);}
        if(frame.action.row[1]==0)for(unsigned sub=0;sub<frame.action.nativeCount;++sub){const auto command=CommandAt(frame.action,sub);
            if(actor.key.model==224&&command==0x4081){++actor.walkSteps;e.kind=EventKind::Walk;Observe(call.owner,e);if(actor.walkSteps==4){e.kind=EventKind::FinalApproach;Observe(call.owner,e);}}
            if(actor.key.model==910&&command==0x4022){e.kind=EventKind::CoreEnergy;Observe(call.owner,e);}
        }
    }
    if(call.owner<18&&frame.action.started)for(unsigned sub=0;sub<frame.action.row[3];++sub)if(CommandAt(frame.action,sub)==0x3036||CommandAt(frame.action,sub)==0x3037)
        for(unsigned i=0;i<8;++i){Event e;e.kind=EventKind::PartyHaste;e.before=e.after=N::Life(actors[i].key.address);Observe(i+20,e);}
    Deaths();
}
std::uint32_t Targets(unsigned actor,Skill skill) noexcept {
    const auto target=TargetFor(skill);
    if(target==Target::Self)return 1u<<actor;
    if(target==Target::Monsters){std::uint32_t mask=0;for(unsigned i=0;i<8;++i)if(Same(i+20,actors[i].key)&&N::Alive(actors[i].key.address))mask|=1u<<(i+20);return mask;}
    // Native command targeting applies the authored single/random/group flags
    // and living target rules before the low-level queue append.
    using Fn=std::uint32_t(__cdecl*)(unsigned,unsigned,int,int);
    auto mask=reinterpret_cast<Fn>(image+0x3996D0)(actor,bindings.Get(skill).id,-1,0);mask&=0x3FFFFu;
    for(unsigned i=0;i<18;++i)if((mask&(1u<<i))&&!N::Alive(N::Actor(image,i)))mask&=~(1u<<i);
    if(target==Target::PartyRandom&&mask&&(mask&(mask-1)))return 0;
    return mask;
}
bool Needed(Skill skill,std::uint32_t targets) noexcept {
    const auto buffs=BuffsFor(skill),stack=StackFor(skill);if(!buffs&&stack>=4)return targets!=0;
    for(unsigned i=0;i<31;++i)if(targets&(1u<<i)){const auto life=N::Life(N::Actor(image,i));if(life.hp&&((buffs&~life.buffs)||(stack<4&&life.stacks[stack]<5)))return true;}return false;
}
bool ComposeExtra(unsigned source,const std::array<Byte,72>& original,Extra& extra) noexcept {
    if(!Monster(source)||!Sync())return false;
    const auto& actor=actors[source-20];if(!Same(source,actor.key)||!N::Alive(actor.key.address)||actor.profile>=48)return false;
    auto row=original;if(row[0]!=source||row[1]||row[2]||!row[3]||row[3]>3)return false;
    for(unsigned n=0;n<row[3];++n){const unsigned command=N::Word(row.data()+8+16*n);if(bindings.Find(command)<26||IsOd(command))return false;}
    const int before=N::ReadOr<std::int8_t>(image+QueueCount,-1);if(before<0||before>62)return false;
    auto& prior=extras[source-20];if(prior.key.Valid()&&Same(source,prior.key)){
        for(int i=0;i<before;++i){std::array<Byte,72> queued{};if(!N::Read(image+QueueRoot+72u*i,queued))return false;
            if(queued[0]==source&&queued[1]==0&&queued[3]==prior.sub+1&&!std::memcmp(queued.data()+8,prior.row.data()+8,64))return false;}
    }
    const auto key=actor.key;const auto skill=Catalog[actor.profile].skill;const auto targets=Targets(source,skill);
    if(!targets||!Needed(skill,targets)||!accepting.load()||!Same(source,key))return false;
    extra={};extra.key=key;extra.command=bindings.Get(skill).id;extra.sub=row[3];
    // Append inside the native action packet. Its ordinary input/animation
    // lifetime now spans the original action and the extra, like native combos.
    const unsigned at=8+16u*row[3];std::memset(row.data()+at,0,16);
    const std::uint16_t command=static_cast<std::uint16_t>(bindings.Get(skill).id),none=255;
    std::memcpy(row.data()+at,&command,2);std::memcpy(row.data()+at+2,&none,2);std::memcpy(row.data()+at+8,&targets,4);
    ++row[3];extra.row=row;return true;
}
void* BeforeQueue(const SharedQueue::Call& call,const Byte*& forwarded) noexcept {
    if(call.tail||!SharedQueue::depth||SharedQueue::depth>queueFrames.size())return nullptr;
    const int before=N::ReadOr<std::int8_t>(image+QueueCount,-1);if(before<0||before>=62)return nullptr;
    std::array<Byte,72> row{};if(!N::Read(reinterpret_cast<std::uintptr_t>(forwarded),row))return nullptr;
    auto& frame=queueFrames[SharedQueue::depth-1];frame={};
    if(!ComposeExtra(call.actor,row,frame.extra))return nullptr;
    frame.before=static_cast<unsigned>(before);frame.extra.index=frame.before;
    frame.extra.row[4]=frame.extra.row[5]=0;frame.extra.row[6]=static_cast<Byte>(call.priority);frame.extra.row[7]=static_cast<Byte>(call.mode);
    forwarded=frame.extra.row.data();return &frame;
}
void AfterQueue(void* token,const SharedQueue::Call& call,int result,bool complete) noexcept {
    const auto frame=*static_cast<QueueFrame*>(token);
    if(!complete||result!=-1||!accepting.load()||frame.extra.key.battle!=generation||!Same(call.actor,frame.extra.key))return;
    std::array<Byte,72> actual{};
    if(N::ReadOr<std::int8_t>(image+QueueCount,-1)!=static_cast<int>(frame.before+1)||!N::Read(image+QueueRoot+72u*frame.before,actual)||actual!=frame.extra.row){
        accepting=false;detail="Omega T7 queue ownership lost; restart required";Trace("queue-rejected");return;}
    extras[call.actor-20]=frame.extra;Trace("extra-bundled",call.actor,frame.extra.command);
}
AiFrame BeforeAi(unsigned source,unsigned script,unsigned mode,Byte* packet) noexcept {
    AiFrame frame;
    if(script||mode||!packet||!Monster(source)||!Sync())return frame;
    const auto address=reinterpret_cast<std::uintptr_t>(packet),root=image+QueueRoot;
    const int count=N::ReadOr<std::int8_t>(image+QueueCount,-1);
    if(count<1||count>62||address<root||address-root>=72u*count||(address-root)%72u)return frame;
    std::array<Byte,72> row{};
    if(!N::Read(address,row)||row[0]!=source||row[1]||row[2]||row[3]||row[4]>1)return frame;
    const auto& actor=actors[source-20];if(!Same(source,actor.key)||!N::Alive(actor.key.address))return frame;
    const auto index=static_cast<unsigned>((address-root)/72u);
    if(N::ReadOr<Byte>(actor.key.address+0xDE5,255)!=index)return frame;
    // Native claims the row (state1 / actor DE5) before asking AI to fill it.
    // Only count0 proves that no command has been prepared or dispatched yet.
    frame.key=actor.key;frame.packet=address;frame.count=static_cast<unsigned>(count);frame.index=index;frame.nativeState=row[4];return frame;
}
void AfterAi(unsigned source,const AiFrame& frame) noexcept {
    if(!frame.packet||!Sync()||!Same(source,frame.key)||N::ReadOr<std::int8_t>(image+QueueCount,-1)!=static_cast<int>(frame.count))return;
    std::array<Byte,72> row{},current{};Extra extra{};
    if(!N::Read(frame.packet,row)||row[4]!=frame.nativeState||N::ReadOr<Byte>(frame.key.address+0xDE5,255)!=frame.index||!ComposeExtra(source,row,extra)||!Same(source,frame.key)||
       !N::Read(frame.packet,current)||current!=row||N::ReadOr<std::int8_t>(image+QueueCount,-1)!=static_cast<int>(frame.count))return;
    // Native AI has filled its reserved row; the caller has not dispatched its
    // first command yet. Publish only the free subcommand, then its count. The
    // existing input, first command, state bytes and queue count remain native.
    const auto tail=frame.packet+8u+16u*extra.sub;
    if(!NativeUiSupport::Copy(reinterpret_cast<void*>(tail),extra.row.data()+8+16*extra.sub,16))return;
    if(!NativeUiSupport::Copy(reinterpret_cast<void*>(frame.packet+3),extra.row.data()+3,1)){
        (void)NativeUiSupport::Copy(reinterpret_cast<void*>(tail),row.data()+8+16*extra.sub,16);
        accepting=false;detail="Omega T7 AI packet publication failed; restart required";Trace("ai-packet-rejected");return;
    }
    extra.index=frame.index;extras[source-20]=extra;Trace("extra-ai-ready",source,extra.command);
}
int __cdecl AiShim(unsigned source,unsigned target,unsigned script,unsigned argument,unsigned mode,Byte* packet){
    const bool bounded=++aiDepth<=SharedQueue::MaximumDepth;
    const auto frame=bounded?BeforeAi(source,script,mode,packet):AiFrame{};
    int result=0;bool complete=false;
    __try{result=reinterpret_cast<AiFunction>(aiOriginal[0])(source,target,script,argument,mode,packet);complete=true;}
    __finally{if(complete&&bounded)AfterAi(source,frame);--aiDepth;}
    return result;
}
bool StartAiProducer(){
    if(aiInstalled.load())return Evidence::Profile(image);
    if(!Evidence::Profile(image))return false;
    const std::uint32_t rvas[]={Evidence::AiPrepareRva};void* shims[]={reinterpret_cast<void*>(&AiShim)};
    if(!NativeUiSupport::Install(image,rvas,shims,aiOriginal,MinHookBatch::Owner::OmegaT7,reinterpret_cast<const void*>(&AiShim))||!Evidence::PublishAi(image))return false;
    aiInstalled=true;return true;
}
void Turn(unsigned actor,void* pointer,std::uint32_t sequence) noexcept {
    if(!Sync()||!Monster(actor)||actors[actor-20].key.address!=reinterpret_cast<std::uintptr_t>(pointer))return;
    auto* action=Track(actor);auto& state=actors[actor-20];if(!action||action->row[1]||state.turnSequence==sequence)return;
    state.turnSequence=sequence;Event e;e.kind=EventKind::OwnTurn;e.before=e.after=N::Life(state.key.address);Observe(actor,e);
}
void ResetState(B::ResetReason reason) noexcept {
    if(!owner.load()||owner.load()==GetCurrentThreadId()){
        Clear();
        if(reason==B::ResetReason::NativeLoad||reason==B::ResetReason::Stop){
            AcquireSRWLockExclusive(&previewLock);
            previewOwner=0;lastPreview={};previewCampaign=0;
            ReleaseSRWLockExclusive(&previewLock);
        }
    }
}
void Battle() noexcept {ResetState(B::ResetReason::BattleStart);}
void Retire(unsigned,unsigned id) noexcept {if(owner.load()!=GetCurrentThreadId())return;if(Monster(id)){actors[id-20]={};extras[id-20]={};}if(id<31)actions[id]={};}
bool Available(unsigned field,unsigned formation) noexcept {
    return accepting.load()&&SessionReady()&&OmegaUltima::EligibleFormation(field,formation)&&
        (!owner.load()||owner.load()==GetCurrentThreadId())&&N::BattleOngoing(image)&&CommandsCurrent();
}
void Populated() noexcept {(void)Sync();}
void Review() noexcept {Notice::RequestReview();}
const A::Observer actionObserver{BeforeResult,AfterResult,BeforeFinish,AfterFinish,nullptr};
const SharedQueue::Observer queueObserver{BeforeQueue,AfterQueue};
const SharedCombat::HpObserver hpObserver{BeforeHp,AfterHp};
const SharedActor::Observer actorObserver{Retire,nullptr};
const SharedTurn::Observer turnObserver{Turn};
const SharedBattleRuntime::ActionObserver battleObserver{Battle};
const B::Observer damageObserver{DamageEnter,nullptr,nullptr,ResetState,DamageAmount};
const Bridge::Provider provider{Available,Populated,Review};
bool Profile(std::uintptr_t base) noexcept {
#ifdef FFXHOOKS_TARGET_STEAM_20261001
    return Evidence::Profile(base);
#else
    (void)base;return false;
#endif
}
bool Attach(bool nativeHooks){
    if(!primed.load())return false;
    if(!reader){detail="Omega T7 encounter context unavailable";return false;}
    if(!bindings.Ready()){detail="Omega T7 bindings unavailable or disabled";return false;}
    if(!Profile(image)){detail="Omega T7 native profile unavailable";return false;}
    if(nativeHooks&&(!EquipmentWorkshop::CombatProducerReady()||!SharedAction::Start(image)||!SharedActor::Start(image)||!SharedCombat::Start(image)||!SharedTurn::Start(image))){
        detail="Omega T7 shared combat producer unavailable";return false;
    }
    Clear();
    if(!SharedQueue::Register(SharedQueue::Slot::OmegaT7,&queueObserver)||!A::Subscribe(A::Slot::OmegaT7,&actionObserver)||!SharedCombat::RegisterHpObserver(SharedCombat::HpSlot::OmegaT7,&hpObserver)||
       !SharedActor::Subscribe(SharedActor::Slot::OmegaT7,&actorObserver)||!SharedTurn::Register(SharedTurn::Consumer::OmegaT7,&turnObserver)||
       !SharedBattleRuntime::RegisterActionObserver(SharedBattleRuntime::ActionConsumer::OmegaT7,&battleObserver)||!B::Subscribe(B::Slot::OmegaT7,&damageObserver)||!Bridge::Register(&provider)){
        detail="Omega T7 producer ownership conflict";Stop();return false;
    }
    if(nativeHooks&&!SharedQueue::Start(image)){detail="Omega T7 native queue producer unavailable";Stop();return false;}
    if(nativeHooks&&!StartAiProducer()){detail="Omega T7 native AI preparation producer unavailable";Stop();return false;}
    accepting=true;Notice::Start();detail="Omega T7 waiting for a supported natural encounter";return true;
}
}
bool Requested() noexcept {return Settings::Requested();}
void Prime(std::uintptr_t base,const std::filesystem::path& game,bool validateOnly,Log log) noexcept {
    if(!Requested()||validateOnly)return;
    try{image=base;logger=log;if(!Profile(base)){detail="Omega T7 native profile unavailable";return;}
        std::ifstream file(game/L"modules"/L"config"/L"_omega-t7-v1.json",std::ios::binary|std::ios::ate);
        const auto size=file.tellg();if(size<=0||size>32768){detail="Omega T7 bindings unavailable or oversized";return;}
        std::string text(static_cast<std::size_t>(size),'\0');file.seekg(0);if(!file.read(text.data(),size)){detail="Omega T7 bindings unreadable";return;}
        if(ParseBindings(text,bindings)!=BindingCode::Ready){detail="Omega T7 bindings unavailable or disabled";return;}
        Session::Prime(base,game,true,false,log);primed=Session::Prepared();
        if(!primed.load()){detail="Omega native save producer unavailable";return;}
        (void)B::Subscribe(B::Slot::OmegaT7,&damageObserver);
    }catch(...){detail="Omega T7 package initialization failed";}
}
bool Start(SinAi::ContextReader context){reader=context;return Attach(true);}
void RequestStop() noexcept {accepting=false;primed=false;detail="Omega T7 is OFF";Notice::Stop();Session::Stop();}
void Stop() noexcept {
    const bool wasActive=accepting.load();const auto* failure=detail.load();
    RequestStop();if(!owner.load()||owner.load()==GetCurrentThreadId())Clear();
    A::Unsubscribe(A::Slot::OmegaT7,&actionObserver);SharedCombat::UnregisterHpObserver(SharedCombat::HpSlot::OmegaT7,&hpObserver);
    SharedQueue::Unregister(SharedQueue::Slot::OmegaT7,&queueObserver);
    SharedActor::Unsubscribe(SharedActor::Slot::OmegaT7,&actorObserver);SharedTurn::Unregister(SharedTurn::Consumer::OmegaT7,&turnObserver);
    SharedBattleRuntime::UnregisterActionObserver(SharedBattleRuntime::ActionConsumer::OmegaT7,&battleObserver);B::Unsubscribe(B::Slot::OmegaT7,&damageObserver);Bridge::Unregister(&provider);
    // Startup failure cleanup must release every subscription without erasing
    // the package/profile reason before the caller logs the failed activation.
    if(!wasActive)detail=failure;
}
bool Active() noexcept {return accepting.load();}
bool Prepared() noexcept {return primed.load();}
const char* Detail() noexcept {return detail.load();}
bool Preview(PreviewSnapshot& out) noexcept {
    out={};
    // Keep the last encounter visible in the field, but never cross save or
    // thread ownership and never fabricate a next-battle assignment.
    if(!accepting.load())return false;
    const auto session=CampaignSession();if(!session)return false;
    AcquireSRWLockShared(&previewLock);
    if(previewOwner.load()==GetCurrentThreadId()&&previewCampaign==session)out=lastPreview;
    ReleaseSRWLockShared(&previewLock);
    return out.count!=0;
}
#ifdef FFXHOOKS_TESTING
bool EnableAiProducerForTests(){return StartAiProducer();}
bool PrepareForTests(std::uintptr_t base,const char* text,SinAi::ContextReader context,std::uint64_t seed,Log log){
    image=base;logger=log;reader=context;testSeed=seed;primed=ParseBindings(text,bindings)==BindingCode::Ready;return Attach(false);
}
#endif
}
