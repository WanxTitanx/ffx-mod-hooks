#include "../shared/ExecutableProfile.h"
#include "ArcanaCombat.h"
#include "ArcanaCombatCore.h"
#include "ArcanaCombatEvidence.generated.h"
#include "ArcanaRuntime.h"
#include "NativeGameplayEvents.h"
#include "NativeUiHookSupport.h"
#include "SharedCombatRuntime.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <intrin.h>
#include <limits>

namespace FfxHooks::Arcana::Combat {
namespace {
enum Hook {PreCap,Mp,Ctb,Critical,Encounter,Hp,Consume,ActionResults,Ap,Rewards,Count};
constexpr std::uint32_t rvas[Count]={(::FfxHooks::ExecutableProfile::Rva<0x38ED1A>()),(::FfxHooks::ExecutableProfile::Rva<0x38D030>()),(::FfxHooks::ExecutableProfile::Rva<0x38D290>()),(::FfxHooks::ExecutableProfile::Rva<0x389750>()),(::FfxHooks::ExecutableProfile::Rva<0x380DE0>()),(::FfxHooks::ExecutableProfile::Rva<0x38E2F0>()),(::FfxHooks::ExecutableProfile::Rva<0x38E5F0>()),(::FfxHooks::ExecutableProfile::Rva<0x38DA40>()),(::FfxHooks::ExecutableProfile::Rva<0x398A10>()),(::FfxHooks::ExecutableProfile::Rva<0x3990E0>())};
void* originals[Count]{};
std::uintptr_t base=0;std::atomic<bool> active{false},attempted{false};
std::atomic<bool> producersInstalled{false};
SinProducer::Slot sinObservers;
CombatCore::Ledger ledger;
std::uint64_t nextAction=0;
std::array<std::uint64_t,31> turns{},lastTurn{},lastCtb{},lastCost{};
std::array<std::array<unsigned char,3>,31> timed{};
struct ActionInfo {bool open=false;unsigned ring=0,serial=0,command=0;std::uint64_t turn=0,token=0;CombatCore::Facts facts;unsigned char row[96]{};};
std::array<ActionInfo,31> actions;
thread_local unsigned currentAction=31;
thread_local unsigned rewardDepth=0,nativeGilRate=100;
struct DamageScope {unsigned char* actor=nullptr;unsigned char before[2]{},after[2]{};bool changed[2]{};};
thread_local std::array<DamageScope,4> damageScopes;
thread_local unsigned damageDepth=0;
bool Copy(void* out,const void* in,std::size_t size) noexcept {return NativeUiSupport::Copy(out,in,size);}
template<class T> T Read(std::uintptr_t pointer,unsigned offset){T value{};Copy(&value,reinterpret_cast<void*>(pointer+offset),sizeof(value));return value;}
std::uintptr_t Actor(unsigned id){
    if(id>=31)return 0;
    const auto table=Read<std::uint32_t>(base,(::FfxHooks::ExecutableProfile::Rva<0xD334CC>()));
    if(table<0x10000||table>UINT32_MAX-31*0xF90u)return 0;
    const auto pointer=std::uintptr_t(table)+id*0xF90u;
    if(id<kActorCount&&Read<std::uint16_t>(pointer,0xE)!=id)return 0;
    return pointer;
}
unsigned ActorId(const void* pointer){
    const auto address=reinterpret_cast<std::uintptr_t>(pointer),table=Read<std::uint32_t>(base,(::FfxHooks::ExecutableProfile::Rva<0xD334CC>()));
    if(!table||address<table||(address-table)%0xF90u)return 31;
    const auto id=(address-table)/0xF90u;return id<31&&Actor(static_cast<unsigned>(id))==address?static_cast<unsigned>(id):31;
}
bool Battle(){
    if(!active.load()||!Runtime::ActorEffects(0)||!Runtime::BattleGeneration()||!Read<unsigned char>(base,(::FfxHooks::ExecutableProfile::Rva<0xD2A8E0>())))return false;
    const auto epoch=Runtime::BattleGeneration();
    if(ledger.battle!=epoch){ledger.BeginBattle(epoch);actions={};turns={};lastTurn={};lastCtb={};lastCost={};timed={};nextAction=0;}
    return true;
}
CombatCore::Facts Facts(const unsigned char* command,unsigned id,std::uintptr_t actor){
    return CombatCore::Classify(command,96,id,Read<unsigned char>(actor,0x5D9),Read<unsigned char>(actor,0x5C1));
}
ActionInfo* Current(unsigned owner){
    if(owner>=31||!Battle())return nullptr;
    const auto actor=Actor(owner);if(!actor)return nullptr;
    unsigned commandId=0xFFFF;
    using Resolve=const unsigned char*(__cdecl*)(unsigned,unsigned,int,unsigned,unsigned*);
    const auto* command=reinterpret_cast<Resolve>(base + (::FfxHooks::ExecutableProfile::Rva<0x38CF10>()))(owner,0,-1,0,&commandId);
    unsigned char row[96]{};if(!command||!Copy(row,command,sizeof(row)))return nullptr;
    const unsigned ring=Read<unsigned char>(actor,0xDE4),serial=Read<unsigned char>(actor,0x6DF);
    auto& action=actions[owner];
    if(!action.open||action.ring!=ring||action.serial!=serial||action.command!=commandId||action.turn!=turns[owner]){
        action={};action.open=true;action.ring=ring;action.serial=serial;action.command=commandId;action.turn=turns[owner];
        action.token=++nextAction;
    }
    std::memcpy(action.row,row,sizeof(row));action.facts=Facts(row,commandId,actor);return &action;
}
void HpGain(unsigned owner,std::uint32_t amount){
    const auto actor=Actor(owner);if(!actor||!amount)return;
    const auto flags=Read<std::uint16_t>(actor,0x606);const auto hp=Read<std::int32_t>(actor,0x5D0),maximum=Read<std::int32_t>(actor,0x594);
    if(hp<=0||maximum<=0||(flags&7))return;
    const auto after=static_cast<std::int32_t>((std::min)(std::int64_t(maximum),std::int64_t(hp)+amount));
    Copy(reinterpret_cast<void*>(actor+0x5D0),&after,4);
}
void MpGain(unsigned owner,std::uint32_t amount){
    const auto actor=Actor(owner);if(!actor||!amount)return;
    const auto flags=Read<std::uint16_t>(actor,0x606);const auto hp=Read<std::int32_t>(actor,0x5D0);
    const auto mp=Read<std::int32_t>(actor,0x5D4),maximum=Read<std::int32_t>(actor,0x598);
    if(hp<=0||mp<0||maximum<=0||(flags&5))return;
    const auto after=static_cast<std::int32_t>((std::min)(std::int64_t(maximum),std::int64_t(mp)+amount));
    Copy(reinterpret_cast<void*>(actor+0x5D4),&after,4);
}
unsigned PartyRate(EffectKind kind){
    unsigned rate=kind==EffectKind::DropMultiplier?1u:0u;
    for(unsigned owner=0;owner<kActorCount;++owner){
        const auto* effects=Runtime::ActorEffects(owner);const auto actor=Actor(owner);
        if(!effects||!actor||!Read<unsigned char>(actor,0xDC8)||Read<unsigned char>(actor,0xDCC)||Read<unsigned char>(actor,0xDCD))continue;
        rate=(std::max)(rate,static_cast<unsigned>((std::max)(0,effects->Get(kind))));
    }
    return rate;
}
void __cdecl AdjustPreCap(std::uintptr_t frame) noexcept {
    if(!Battle())return;
    const unsigned user=Read<unsigned>(frame,8),target=Read<unsigned>(frame,0x10),id=Read<unsigned>(frame,0x1C);
    const auto userPointer=Read<std::uint32_t>(frame,0xC),targetPointer=Read<std::uint32_t>(frame,0x14),commandPointer=Read<std::uint32_t>(frame,0x18);
    if(user>=31||target>=31||Actor(user)!=userPointer||Actor(target)!=targetPointer)return;
    unsigned char command[96]{};if(!Copy(command,reinterpret_cast<void*>(commandPointer),sizeof(command)))return;
    const auto facts=Facts(command,id,userPointer);
    const Effects empty{};const auto* source=Runtime::ActorEffects(user);const auto* recipient=Runtime::ActorEffects(target);
    int hp=0,mp=0;if(!Copy(&hp,reinterpret_cast<void*>(frame-0x10),4)||!Copy(&mp,reinterpret_cast<void*>(frame-0xC),4))return;
    auto own=source?*source:empty;
    if(facts.item)own.values[static_cast<unsigned>(EffectKind::Healing)]+=own.Get(EffectKind::ItemHealing);
    DamageContext context;context.amount=hp<0?-std::int64_t(hp):hp;context.cap=INT32_MAX;context.healing=hp<0;
    context.whiteMagic=facts.white;context.overdrive=facts.overdrive;context.fixed=facts.fixed;context.fractional=facts.fractional;context.elements=facts.elements;
    context.deathImmune=Read<unsigned char>(targetPointer,0x641)==255;
    const auto scaled=Damage(context,own,recipient?*recipient:empty);
    const int changed=static_cast<int>(hp<0?-scaled:scaled);Copy(reinterpret_cast<void*>(frame-0x10),&changed,4);
    if(facts.item&&mp<0&&source&&source->Get(EffectKind::ItemHealing)){
        const auto value=(std::min)(std::int64_t(INT32_MAX),-std::int64_t(mp)*(100+source->Get(EffectKind::ItemHealing))/100);
        const int changedMp=-static_cast<int>(value);Copy(reinterpret_cast<void*>(frame-0xC),&changedMp,4);
    }
}
__declspec(naked) void PreCapGateway(){
    __asm {
        pushfd
        pushad
        mov esi,esp
        sub esp,528
        and esp,0FFFFFFF0h
        fxsave [esp]
        push ebp
        call AdjustPreCap
        add esp,4
        fxrstor [esp]
        mov esp,esi
        popad
        popfd
        jmp dword ptr [originals]
    }
}
int __cdecl MpShim(unsigned owner,const unsigned char* command){
    const int original=reinterpret_cast<int(__cdecl*)(unsigned,const unsigned char*)>(originals[Mp])(owner,command);
    const auto* effects=Runtime::ActorEffects(owner);const auto actor=Actor(owner);
    if(!active.load()||!effects||!actor||!command||original<=0)return original;
    const auto flags=Read<std::uint16_t>(actor,0x6BC);
    // Native One MP, Spellspring and Magic Booster combinations remain native.
    if(flags&0x8000)return original;
    unsigned char row[96]{};if(!Copy(row,command,sizeof(row)))return original;
    auto extra=*effects;
    if(flags&0x4000){extra.values[static_cast<unsigned>(EffectKind::HalfMp)]=0;extra.values[static_cast<unsigned>(EffectKind::HalfBlackMp)]=0;extra.values[static_cast<unsigned>(EffectKind::HalfWhiteMp)]=0;}
    return static_cast<int>(MpCost(static_cast<unsigned>(original),extra,row[24]==1,row[24]==2,false,false));
}
void FinishTimed(unsigned owner){
    if(owner>=31)return;const auto actor=Actor(owner);if(!actor)return;
    auto flags=Read<std::uint16_t>(actor,0x606);const unsigned bits[]={3,6,7};
    for(unsigned i=0;i<3;++i)if(timed[owner][i]){
        if(!(flags&(1u<<bits[i])))timed[owner][i]=0;
        else if(--timed[owner][i]==0)flags&=static_cast<std::uint16_t>(~(1u<<bits[i]));
    }
    Copy(reinterpret_cast<void*>(actor+0x606),&flags,2);
}
int __cdecl CtbShim(void* actor,int rank,int haste,int slow){
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-base;
    const int native=reinterpret_cast<int(__cdecl*)(void*,int,int,int)>(originals[Ctb])(actor,rank,haste,slow);
    if(!Battle()||native<=0||(caller!=(::FfxHooks::ExecutableProfile::Rva<0x3B21D5>())&&caller!=(::FfxHooks::ExecutableProfile::Rva<0x3B1B3B>())))return native;
    const unsigned owner=ActorId(actor);if(owner>=31)return native;
    const auto* effects=Runtime::ActorEffects(owner);
    const int result=effects?static_cast<int>(CtbDelay(static_cast<unsigned>(native),*effects,owner<kActorCount&&!ledger.firstAction[owner])):native;
    if(caller==(::FfxHooks::ExecutableProfile::Rva<0x3B21D5>())){
        if(owner<kActorCount)ledger.firstAction[owner]=true;
        auto* action=Current(owner);
        if(action&&lastCtb[owner]!=action->token){lastCtb[owner]=action->token;FinishTimed(owner);}
    }
    return result;
}
int __cdecl CriticalShim(const unsigned char* attacker,const unsigned char* target,const unsigned char* command,unsigned* flags,int damage){
    const auto owner=ActorId(attacker);const auto* effects=Runtime::ActorEffects(owner);
    unsigned char copy[96]{};const unsigned char* effective=command;
    if(active.load()&&effects&&effects->Get(EffectKind::CriticalChance)&&command&&Copy(copy,command,sizeof(copy))){
        const unsigned chance=(copy[32]&8)?Read<unsigned char>(reinterpret_cast<std::uintptr_t>(attacker),0x5D8):copy[39];
        copy[32]&=static_cast<unsigned char>(~8u);
        copy[39]=static_cast<unsigned char>((std::min)(255u,chance+static_cast<unsigned>(effects->Get(EffectKind::CriticalChance))));
        effective=copy;
    }
    return reinterpret_cast<SharedCombat::CriticalFn>(originals[Critical])(attacker,target,effective,flags,damage);
}
int __cdecl EncounterShim(int field,int group,float distance){
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if(active.load()&&Runtime::ActorEffects(0)&&std::isfinite(distance)&&distance>0){
        unsigned reduction=0;
        for(unsigned actor=0;actor<kActorCount;++actor){
            const auto* effects=Runtime::ActorEffects(actor);
            if(effects&&(Read<unsigned char>(base + (::FfxHooks::ExecutableProfile::Rva<0xD3205C>())+actor*0x94,0x2C)&1))
                reduction=(std::max)(reduction,static_cast<unsigned>(effects->Get(EffectKind::EncounterReduction)));
        }
        distance*=float(100-(std::min)(100u,reduction))/100.f;
    }
    const int result=reinterpret_cast<int(__cdecl*)(int,int,float)>(originals[Encounter])(field,group,distance);
    sinObservers.Natural(field,group,distance,result,caller);
    return result;
}
int __cdecl HpShim(unsigned target,unsigned char* pointer,int damage,int a4,int resultCode,int flags,int n129){
    const auto actor=reinterpret_cast<std::uintptr_t>(pointer);
    const bool admitted=Battle()&&target<31&&Actor(target)==actor&&currentAction<31;
    const auto before=admitted?Read<int>(actor,0x5D0):0;
    const auto* recipient=admitted?Runtime::ActorEffects(target):nullptr;
    bool judgement=false;int applied=damage;
    if(recipient&&recipient->Get(EffectKind::SurviveOnce)&&!ledger.judgement[target]&&damage>0&&before>0&&damage>=before&&
       !(Read<std::uint16_t>(actor,0x606)&5)&&reinterpret_cast<int(__cdecl*)(unsigned)>(base + (::FfxHooks::ExecutableProfile::Rva<0x38D460>()))(target)){
        applied=before-1;judgement=true;
    }
    const int result=reinterpret_cast<SharedCombat::HpFn>(originals[Hp])(target,pointer,applied,a4,resultCode,flags,n129);
    if(!admitted)return result;
    const auto after=Read<int>(actor,0x5D0);
    if(judgement&&after==1&&ledger.Survive(target)){
        const auto maximum=Read<unsigned>(actor,0x594);
        HpGain(target,static_cast<unsigned>(std::uint64_t(maximum)*recipient->Get(EffectKind::SurviveHeal)/100));
    }
    auto* action=Current(currentAction);const auto* source=Runtime::ActorEffects(currentAction);
    if(!action||!source)return result;
    const auto attacker=Actor(currentAction);
    if(target<kActorCount&&target!=currentAction&&action->facts.white&&after>before&&before>=0){
        const auto shared=ledger.ShareHealing(currentAction,action->token,static_cast<unsigned>(after-before),Read<unsigned>(attacker,0x594),
            static_cast<unsigned>(source->Get(EffectKind::LoversHealing)),static_cast<unsigned>(source->Get(EffectKind::LoversCapHp)));
        HpGain(currentAction,shared);
    }
    if(target>=20&&target<31&&before>0&&after<=0&&damage>0&&ledger.Kill(currentAction,action->token)){
        HpGain(currentAction,static_cast<unsigned>(std::uint64_t(Read<unsigned>(attacker,0x594))*source->Get(EffectKind::KillHp)/100));
        MpGain(currentAction,static_cast<unsigned>(std::uint64_t(Read<unsigned>(attacker,0x598))*source->Get(EffectKind::KillMp)/100));
    }
    return result;
}
int __cdecl ConsumeShim(unsigned owner){
    auto* action=Current(owner);const auto token=action?action->token:0;const auto command=action?action->command:0;
    const int result=reinterpret_cast<int(__cdecl*)(unsigned)>(originals[Consume])(owner);
    const auto* effects=Runtime::ActorEffects(owner);
    if(Battle()&&effects&&token&&lastCost[owner]!=token){
        lastCost[owner]=token;
        if(command==0x3021u||command==33u)MpGain(owner,static_cast<unsigned>(std::uint64_t(Read<unsigned>(Actor(owner),0x598))*effects->Get(EffectKind::DefendMp)/100));
    }
    return result;
}
int __cdecl ActionShim(unsigned char owner,unsigned targetType,void* rows){
    auto* action=Current(owner);const auto previous=currentAction;
    std::array<std::uint16_t,31> before{};
    if(action){currentAction=owner;for(unsigned id=0;id<31;++id)before[id]=Read<std::uint16_t>(Actor(id),0x606);}
    int result=0;
    __try{result=reinterpret_cast<int(__cdecl*)(unsigned char,unsigned,void*)>(originals[ActionResults])(owner,targetType,rows);}
    __finally{currentAction=previous;}
    const auto* effects=Runtime::ActorEffects(owner);
    if(action){
        const unsigned statuses[]={3,6,7};const EffectKind kinds[]={EffectKind::TouchPoison,EffectKind::TouchArmorBreak,EffectKind::TouchMentalBreak};
        for(unsigned id=0;id<31;++id){const auto actor=Actor(id);const auto after=Read<std::uint16_t>(actor,0x606);
            for(unsigned n=0;n<3;++n){
                const auto bit=static_cast<std::uint16_t>(1u<<statuses[n]);
                const auto nativeChance=owner<kActorCount?Runtime::NativeAttackChance(owner,statuses[n]):Read<unsigned char>(Actor(owner),0x5DE + statuses[n]);
                const bool native=nativeChance!=0||action->row[46+statuses[n]]!=0;
                if(native&&(after&bit)){timed[id][n]=0;continue;}
                if(effects&&!(before[id]&bit)&&(after&bit)&&effects->Get(kinds[n])&&(n==0||action->facts.ordinary))timed[id][n]=3;
            }
        }
        if(!Read<unsigned char>(Actor(owner),0xDE9))action->open=false;
    }
    return result;
}
int __cdecl ApShim(unsigned owner,void* actor,int amount,int gilRate){
    const unsigned before=owner<kActorCount?Read<unsigned>(base,(::FfxHooks::ExecutableProfile::Rva<0x1F10F20>())+owner*4):0;
    const int result=reinterpret_cast<int(__cdecl*)(unsigned,void*,int,int)>(originals[Ap])(owner,actor,amount,gilRate);
    if(rewardDepth)nativeGilRate=(std::max)(nativeGilRate,static_cast<unsigned>((std::max)(1,result)*100));
    const auto* effects=Runtime::ActorEffects(owner);
    if(Battle()&&effects&&Actor(owner)==reinterpret_cast<std::uintptr_t>(actor)){
        const unsigned after=Read<unsigned>(base,(::FfxHooks::ExecutableProfile::Rva<0x1F10F20>())+owner*4);
        if(after>=before){const auto adjusted=static_cast<unsigned>((std::min)(std::uint64_t(999999999),
            std::uint64_t(after)+std::uint64_t(after-before)*effects->Get(EffectKind::ApBonus)/100));
            Copy(reinterpret_cast<void*>(base + (::FfxHooks::ExecutableProfile::Rva<0x1F10F20>())+owner*4),&adjusted,4);}
    }
    return result;
}
void __cdecl RewardsShim(int id,void* actor,const void* rewards,int a4,int a5){
    const unsigned before=Read<unsigned>(base,(::FfxHooks::ExecutableProfile::Rva<0x1F10F6C>())),previousRate=nativeGilRate;
    ++rewardDepth;nativeGilRate=100;
    __try{SinProducer::ForwardRewards(sinObservers,
        reinterpret_cast<void(__cdecl*)(int,void*,const void*,int,int)>(originals[Rewards]),id,actor,rewards,a4,a5);}
    __finally{--rewardDepth;}
    if(Battle()){
        const unsigned desired=100+PartyRate(EffectKind::GilBonus),after=Read<unsigned>(base,(::FfxHooks::ExecutableProfile::Rva<0x1F10F6C>()));
        if(desired>nativeGilRate&&after>=before){const auto changed=static_cast<unsigned>((std::min)(std::uint64_t(999999999),
            std::uint64_t(before)+std::uint64_t(after-before)*desired/nativeGilRate));Copy(reinterpret_cast<void*>(base + (::FfxHooks::ExecutableProfile::Rva<0x1F10F6C>())),&changed,4);}
    }
    nativeGilRate=previousRate;
}
void Enter(const NativeGameplayEvents::Call& call) noexcept {
    if(call.kind!=NativeGameplayEvents::Kind::Damage)return;
    ++damageDepth;if(damageDepth>damageScopes.size())return;
    auto& scope=damageScopes[damageDepth-1];scope={};
    if(!Battle()||call.size!=sizeof(NativeGameplayEvents::Damage)||!call.source)return;
    const auto& damage=*static_cast<const NativeGameplayEvents::Damage*>(call.source);
    const auto* effects=Runtime::ActorEffects(damage.user);
    if(!effects||Actor(damage.user)!=reinterpret_cast<std::uintptr_t>(damage.userPointer))return;
    unsigned char command[96]{};if(!Copy(command,damage.command,sizeof(command)))return;
    const auto facts=Facts(command,damage.commandId,reinterpret_cast<std::uintptr_t>(damage.userPointer));
    if(facts.ordinary)return;
    scope.actor=static_cast<unsigned char*>(const_cast<void*>(damage.userPointer));
    const unsigned status[]={6,7};const EffectKind kinds[]={EffectKind::TouchArmorBreak,EffectKind::TouchMentalBreak};
    for(unsigned i=0;i<2;++i){
        const auto native=Runtime::NativeAttackChance(damage.user,status[i]);
        const auto current=Read<unsigned char>(reinterpret_cast<std::uintptr_t>(scope.actor),0x5DE + status[i]);
        if(effects->Get(kinds[i])&&current==(std::max)(native,static_cast<unsigned>(effects->Get(kinds[i])))){
            scope.before[i]=current;scope.after[i]=static_cast<unsigned char>(native);scope.changed[i]=true;
            Copy(scope.actor+0x5DE + status[i],&scope.after[i],1);
        }
    }
}
void Leave(const NativeGameplayEvents::Call& call,bool complete) noexcept {
    if(call.kind==NativeGameplayEvents::Kind::Damage){
        if(!damageDepth)return;
        if(damageDepth<=damageScopes.size()){auto& scope=damageScopes[damageDepth-1];
            for(unsigned i=0;i<2;++i)if(scope.changed[i]&&scope.actor){
                const auto current=Read<unsigned char>(reinterpret_cast<std::uintptr_t>(scope.actor),0x5DE + 6+i);
                if(current==scope.after[i])Copy(scope.actor+0x5DE + 6+i,&scope.before[i],1);
            }
            scope={};
        }
        --damageDepth;return;
    }
    if(!complete||!Battle())return;
    if(call.kind==NativeGameplayEvents::Kind::Aggregate&&call.actor<kActorCount&&!ledger.opening[call.actor]){
        const auto* effects=Runtime::ActorEffects(call.actor);const auto actor=Actor(call.actor);
        if(!effects||!actor)return;
        unsigned char focus=Read<unsigned char>(actor,0x660);
        focus=static_cast<unsigned char>((std::max)(int(focus),effects->Get(EffectKind::FocusOnStart)));Copy(reinterpret_cast<void*>(actor+0x660),&focus,1);
        const unsigned maximum=Read<unsigned char>(actor,0x5BD),old=Read<unsigned char>(actor,0x5BC);
        if(maximum){const auto gauge=static_cast<unsigned char>((std::min)(maximum,old+maximum*static_cast<unsigned>(effects->Get(EffectKind::OpeningOverdrive))/100));
            Copy(reinterpret_cast<void*>(actor+0x5BC),&gauge,1);ledger.opening[call.actor]=true;}
    }else if(call.kind==NativeGameplayEvents::Kind::Turn&&call.actor<31&&Actor(call.actor)==reinterpret_cast<std::uintptr_t>(call.source)){
        if(lastTurn[call.actor]==call.size)return;lastTurn[call.actor]=call.size;++turns[call.actor];
        const auto* effects=Runtime::ActorEffects(call.actor);
        if(effects)MpGain(call.actor,static_cast<unsigned>(std::uint64_t(Read<unsigned>(Actor(call.actor),0x598))*effects->Get(EffectKind::MpPerTurn)/100));
    }
}
const NativeGameplayEvents::Observer observer{Enter,Leave};
const SharedCombat::Callbacks operations{MpShim,CriticalShim,HpShim};
}
bool Start(std::uintptr_t module,bool requested,bool validateOnly,void(*log)(const char*)){
    if(!requested||validateOnly)return false;
    if(attempted.load())return active.load();
    if(!NativeUiSupport::Profile(module,Evidence::combat,SharedCombat::MatchesOwned)){if(log)log("[ffx-hooks] Arcana combat profile rejected\n");return false;}
    attempted=true;base=module;
    void* replacements[Count]={reinterpret_cast<void*>(PreCapGateway),reinterpret_cast<void*>(MpShim),reinterpret_cast<void*>(CtbShim),
        reinterpret_cast<void*>(CriticalShim),reinterpret_cast<void*>(EncounterShim),reinterpret_cast<void*>(HpShim),reinterpret_cast<void*>(ConsumeShim),
        reinterpret_cast<void*>(ActionShim),reinterpret_cast<void*>(ApShim),reinterpret_cast<void*>(RewardsShim)};
    if(!NativeGameplayEvents::Subscribe(&observer))return false;
    if(!SharedCombat::Start(base)){NativeGameplayEvents::Unsubscribe(&observer);return false;}
    originals[Mp]=reinterpret_cast<void*>(&SharedCombat::AfterArcanaMp);
    originals[Critical]=reinterpret_cast<void*>(&SharedCombat::AfterArcanaCritical);
    originals[Hp]=reinterpret_cast<void*>(&SharedCombat::AfterArcanaHp);
    if(!SharedCombat::Register(SharedCombat::arcana,&operations)){NativeGameplayEvents::Unsubscribe(&observer);return false;}
#ifdef FFXHOOKS_HAVE_POLYHOOK
    HMODULE pin=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Start),&pin))return false;
    std::uintptr_t targets[Count]{};unsigned count=0;
    for(unsigned index=0;index<Count;++index){
        if(index==Mp||index==Critical||index==Hp)continue;
        const auto target=base+rvas[index];
        if(MH_CreateHook(reinterpret_cast<void*>(target),replacements[index],&originals[index])!=MH_OK)break;
        targets[count++]=target;
    }
    if(count!=Count-3u){while(count)MH_RemoveHook(reinterpret_cast<void*>(targets[--count]));return false;}
    const auto report=MinHookBatch::EnableBatch(&MinHookBatch::ProcessCoordinator(),MinHookBatch::RuntimeBatchIo(),MinHookBatch::Owner::ArcanaGameplay,targets,count);
    if(report.result!=MinHookBatch::BatchResult::Applied)return false;
#else
    return false;
#endif
    producersInstalled=true;active=true;if(log)log("[ffx-hooks] Arcana combat consumers installed; live validation remains separate\n");return true;
}
void Stop() noexcept {active=false;SharedCombat::Unregister(SharedCombat::arcana,&operations);NativeGameplayEvents::Unsubscribe(&observer);}
bool Active() noexcept {return active.load();}
namespace {
bool OwnsProducer(std::uintptr_t module,Hook hook,const void* replacement) noexcept {
    if (!producersInstalled.load() || module!=base || !originals[hook]) return false;
    unsigned char bytes[5]{};
    return NativeUiSupport::Copy(bytes,reinterpret_cast<const void*>(base+rvas[hook]),sizeof(bytes)) &&
        SinProducer::OwnsJump(bytes,base+rvas[hook],reinterpret_cast<std::uintptr_t>(replacement));
}
}
bool OwnsNaturalProducer(std::uintptr_t module) noexcept {return OwnsProducer(module,Encounter,reinterpret_cast<const void*>(&EncounterShim));}
bool OwnsRewardProducer(std::uintptr_t module) noexcept {return OwnsProducer(module,Rewards,reinterpret_cast<const void*>(&RewardsShim));}
bool AttachSinObservers(std::uintptr_t module,const SinProducer::Observers* observers) noexcept {
    if (!observers || (observers->natural && !OwnsNaturalProducer(module)) ||
        (observers->reward && !OwnsRewardProducer(module))) return false;
    return sinObservers.Attach(observers);
}
void DetachSinObservers(const SinProducer::Observers* observers) noexcept {sinObservers.Detach(observers);}
unsigned PartyDropMultiplier() noexcept {
    if(!active.load()||!Runtime::BattleGeneration()||!Runtime::ActorEffects(0))return 1;
    unsigned best=1;
    for(unsigned owner=0;owner<kActorCount;++owner){
        const auto* effects=Runtime::ActorEffects(owner);
        if(effects&&Actor(owner))best=(std::max)(best,static_cast<unsigned>((std::max)(1,effects->Get(EffectKind::DropMultiplier))));
    }
    return best;
}
}
