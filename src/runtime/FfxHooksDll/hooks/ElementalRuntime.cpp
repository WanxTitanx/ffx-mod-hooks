#include "../shared/ExecutableProfile.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "ElementalRuntime.h"
#include "ModFeatureCatalog.h"
#include "ElementPackAdmission.h"
#include "ElementBuiltinPack.h"
#include "ArcanaElemental.h"
#include "ElementMonsterProof.h"
#include "ElementBattleState.h"
#include "ElementalScanView.h"
#include "ElementScanSnapshotCache.h"
#include "ElementMenuCatalog.h"
#include "ElementScanSettings.h"
#include "F7ElementAffinities.h"
#include "CombatExtensionBus.h"
#include "EquipmentWorkshopRuntime.h"
#include "EquipmentEffects.h"
#include "EquipmentWorkshopStore.h"
#include "F8RuntimeCore.h"
#include "NovaSuperDamageHook.h"
#include "SharedElementRuntime.h"
#include "SharedActionRuntime.h"
#include "SharedNulRuntime.h"
#include "SharedActorRuntime.h"
#include "SharedBattleRuntime.h"
#include "../shared/Config.h"
#include <atomic>
#include <climits>
#include <filesystem>
#include <fstream>
#include <vector>

namespace FfxHooks::ElementalDominion {
namespace {
using Byte=unsigned char;
namespace Bus=CombatExtensions;
constexpr unsigned availableCapabilities=RegistryCapability|AffinityCapability|ContextCapability|SpellCapCapability|TacticsCapability|GravityCapability|EquipmentCapability;
std::uintptr_t module=0;
RuntimeOptions options{};
RuntimeLog logger=nullptr;
Pack pack;
PackAdmission admitted;
std::atomic<RuntimeCode> code{RuntimeCode::Disabled};
std::atomic<bool> configured{false},armed{false},ready{false},terminal{false};
std::atomic<bool> builtinSource{false};
std::atomic<DWORD> ownerThread{0};
std::atomic<std::uint64_t> generation{1};
std::atomic<unsigned> bindingCount{0};
using NulFunction=int(__cdecl*)(unsigned,unsigned,void*);
NulFunction originalNul=nullptr;
bool nulInstalled=false;

struct BankLocation {BankKind kind;unsigned global,getter;};
constexpr BankLocation locations[]={
    {BankKind::Command,::FfxHooks::ExecutableProfile::Rva<0xD2A92C>(),::FfxHooks::ExecutableProfile::Rva<0x390AE0>()},
    {BankKind::Item,::FfxHooks::ExecutableProfile::Rva<0xD2A940>(),::FfxHooks::ExecutableProfile::Rva<0x390A40>()},
    {BankKind::MonsterMagic1,::FfxHooks::ExecutableProfile::Rva<0xD2A930>(),::FfxHooks::ExecutableProfile::Rva<0x390AA0>()},
    {BankKind::MonsterMagic2,::FfxHooks::ExecutableProfile::Rva<0xD2A934>(),::FfxHooks::ExecutableProfile::Rva<0x390AC0>()},
    // Same bounded kernel producer and length used by Workshop and Vanguard.
    // No command-getter ABI is assumed for this native autoability bank.
    {BankKind::AutoAbility,::FfxHooks::ExecutableProfile::Rva<0xD2A944>(),0}
};
struct InputStamp {
    std::array<std::uint32_t,5> banks{};
    std::uint32_t languageManager=0,language=0;
    std::uint16_t abilityBytes=0;
    bool operator==(const InputStamp& other) const noexcept {
        return banks==other.banks&&languageManager==other.languageManager&&language==other.language&&abilityBytes==other.abilityBytes;
    }
};
InputStamp attemptedStamp{},activeStamp{};
bool hasAttemptedStamp=false,inTick=false;
std::uint64_t attemptedGeneration=0;

bool Copy(void* out,const void* in,std::size_t size) noexcept {
    __try {std::memcpy(out,in,size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class T> bool Read(std::uintptr_t at,T& out) noexcept {
    return at>=0x10000&&at<=(std::numeric_limits<std::uintptr_t>::max)()-sizeof(T)&&
           Copy(&out,reinterpret_cast<const void*>(at),sizeof(T));
}
const BankLocation* Location(BankKind kind) noexcept {
    for(const auto& item:locations)if(item.kind==kind)return &item;
    return nullptr;
}
bool Digest(const Byte* data,std::size_t size,std::array<Byte,32>& out) noexcept {
    try{return EquipmentWorkshop::Fingerprint(data,size,out);}catch(...){return false;}
}
bool Profile(std::uintptr_t base,const Pack& candidate) noexcept {
    Byte header[0x1000]{};F8Runtime::ExecutableIdentity identity{};
    if(!base||!Copy(header,reinterpret_cast<const void*>(base),sizeof(header))||
       F8Runtime::ParseExecutableIdentity(header,sizeof(header),&identity)!=F8Runtime::ProfileResult::Supported||
       !F8Runtime::IsSupportedExecutable(identity))return false;
#ifdef FFXHOOKS_TARGET_STEAM_20261001
    constexpr Byte localeStart[]={0xE8,0xDB,0x4D,0xD9,0xFF,0x83,0xF8,0x12,0x77,0x3E};
#else
    constexpr Byte localeStart[]={0xE8,0xDB,0x4F,0xD9,0xFF,0x83,0xF8,0x12,0x77,0x3E};
#endif
    Byte actualLocale[sizeof(localeStart)]{};
    if(!Copy(actualLocale,reinterpret_cast<const void*>(base + (::FfxHooks::ExecutableProfile::Rva<0x4AC2B0>())),sizeof(actualLocale))||
       std::memcmp(actualLocale,localeStart,sizeof(localeStart)))return false;
    for(const auto& bank:candidate.banks){
        const auto* location=Location(bank.kind);if(!location)return false;
        if(bank.kind==BankKind::AutoAbility){if(bank.bytes>65535)return false;continue;}
        Byte expected[31]={0x55,0x8B,0xEC,0xFF,0x75,0x0C,0x8B,0x45,0x08,0xFF,0x35,
            0,0,0,0,0x25,0xFF,0x0F,0,0,0x50,0xE8,0,0,0,0,0x83,0xC4,0x0C,0x5D,0xC3};
        const auto global=static_cast<std::uint32_t>(base+location->global);
        const auto relative=static_cast<std::uint32_t>(::FfxHooks::ExecutableProfile::Rva<0x3AB890>()-(location->getter+26));
        std::memcpy(expected+11,&global,4);std::memcpy(expected+22,&relative,4);
        Byte actual[sizeof(expected)]{};
        if(!Copy(actual,reinterpret_cast<const void*>(base+location->getter),sizeof(actual))||
           std::memcmp(expected,actual,sizeof(actual)))return false;
    }
    return true;
}
bool CaptureStamp(InputStamp& result) noexcept {
    result={};Byte battle=0;
    if(!module||!Read(module + (::FfxHooks::ExecutableProfile::Rva<0xD2A8E0>()),battle)||!battle||
       !Read(module + (::FfxHooks::ExecutableProfile::Rva<0x8DED48>()),result.languageManager)||result.languageManager<0x10000||
       result.languageManager>UINT32_MAX-28u||!Read(result.languageManager+4u,result.language)||
       result.language>18u)return false;
    for(unsigned i=0;i<pack.banks.size();++i){
        const auto* location=Location(pack.banks[i].kind);
        if(!location||!Read(module+location->global,result.banks[i])||
           result.banks[i]<0x10000||result.banks[i]>UINT32_MAX-pack.banks[i].bytes)return false;
        if(pack.banks[i].kind==BankKind::AutoAbility&&
           (!Read(module + (::FfxHooks::ExecutableProfile::Rva<0xD2A970>()),result.abilityBytes)||result.abilityBytes!=pack.banks[i].bytes))return false;
    }
    return true;
}
bool Locale(char (&out)[8]) noexcept {
    __try {
        const auto* text=reinterpret_cast<const char*(__cdecl*)()>(module + (::FfxHooks::ExecutableProfile::Rva<0x4AC2B0>()))();
        if(!text)return false;
        for(unsigned i=0;i<sizeof(out);++i){
            const char ch=text[i];out[i]=ch;if(!ch)return i>0;
            if(!((ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='_'||ch=='-'))return false;
        }
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    return false;
}
bool CurrentData() noexcept {
    if(!ready.load(std::memory_order_acquire)||!armed.load()||ownerThread.load()!=GetCurrentThreadId())return false;
    InputStamp current{};return CaptureStamp(current)&&current==activeStamp;
}
bool Actor(unsigned index,const void* pointer) noexcept {
    if(index>=31||!pointer||SharedActor::Busy(index))return false;
    std::uint32_t actors=0;std::uint16_t stored=0;
    if(!Read(module + (::FfxHooks::ExecutableProfile::Rva<0xD334CC>()),actors)||actors<0x10000||actors>UINT32_MAX-31u*0xF90u)return false;
    const auto address=std::uintptr_t(actors)+index*0xF90u;
    return pointer==reinterpret_cast<const void*>(address)&&Read(address+0xC,stored)&&stored==index;
}
const ActorProfile* CharacterProfile(unsigned slot,const void* actor) noexcept {
    std::uint16_t identity=0;
    if(slot>=18||!Read(reinterpret_cast<std::uintptr_t>(actor)+0xE,identity)||identity!=slot)return nullptr;
    const auto kind=slot<8?ProfileKind::Character:ProfileKind::Aeon;
    for(const auto& profile:pack.profiles)if(profile.kind==kind&&profile.id==slot)return &profile;
    return nullptr;
}
#include "ElementalMonsterProfiles.inl"
#include "ElementalEquipment.inl"
TimedAffinity Timed(unsigned target,unsigned element,const ActionToken* action,bool live) noexcept;
ActionToken TrackHit(const Bus::DamageCall&,const CommandBinding&) noexcept;
void SyncStatusActors() noexcept;
bool ResetStatusState() noexcept;
bool StartTactics();
void StopTactics() noexcept;
unsigned CardElement(unsigned kind) noexcept {
    const char* primary=kind==Arcana::Elemental::Poison?"spira.poison":"spira.gravity";
    const char* builtin=kind==Arcana::Elemental::Poison?"hook.custom03":"hook.custom04";
    auto index=pack.registry.Index(primary);
    if(index==InvalidElement)index=pack.registry.Index(builtin);
    const auto* item=pack.registry.At(index);
    return item&&!item->nativeBit?index:InvalidElement;
}
bool Affinities(unsigned target,const void* actor,std::array<AffinityValue,ElementLimit>& values,
                const ActionToken* action=nullptr,bool live=false,
                Arcana::Elemental::Snapshot* cardSnapshot=nullptr,
                Arcana::Elemental::Provider* cardSource=nullptr) noexcept {
    if(cardSnapshot)*cardSnapshot={};if(cardSource)*cardSource=nullptr;
    Byte flags[4]{};
    if(!Actor(target,actor)||!Copy(flags,static_cast<const Byte*>(actor)+0x5DA,4))return false;
    const NativeMasks masks{flags[3],flags[2],flags[1],flags[0]};
    bool rejected=false;const auto* profile=ProfileForActor(target,actor,&rejected);
    if(rejected)return false;
    std::array<std::int32_t,ElementLimit> equipment{};
    if(!EquipmentDeltas(target,actor,equipment))return false;
    F7Elements::Selection difficulty{};
    const bool hasDifficulty=F7Elements::Read(target,reinterpret_cast<std::uintptr_t>(actor),difficulty);
    Arcana::Elemental::Snapshot card{};
    const auto provider=Arcana::Elemental::provider.load(std::memory_order_acquire);
    const bool hasCard=options.core&&provider&&Arcana::Elemental::Read(target,card);
    const unsigned poison=hasCard&&(card.wards&Arcana::Elemental::Poison)?CardElement(Arcana::Elemental::Poison):InvalidElement;
    const unsigned gravity=hasCard&&(card.wards&Arcana::Elemental::Gravity)?CardElement(Arcana::Elemental::Gravity):InvalidElement;
    for(unsigned i=0;i<pack.registry.Size();++i){
        const auto* descriptor=pack.registry.At(i);AffinitySources source{};
        source.equipment=equipment[i];
        source.base=descriptor->nativeBit?NativeBase(descriptor->nativeBit,masks).value:10000;
        if(profile)for(const auto& affinity:profile->affinities)if(affinity.element==i){
            source.base=affinity.baseBp;source.locked=affinity.locked;source.lockedValue=affinity.baseBp;
        }
        if(hasDifficulty&&!source.locked)F7Elements::Base(difficulty,descriptor->nativeBit,descriptor->key.c_str(),source.base);
        if(options.tactics){const auto timed=Timed(target,i,action,live);
            if(timed.valid){source.imperil=timed.imperil;source.ward=timed.ward;}}
        values[i]=Effective(source);if(values[i].error!=Error::Ok)return false;
        const auto beforeCard=values[i].value;
        values[i].value=Arcana::Elemental::WardExposure(beforeCard,i==poison||i==gravity,source.locked);
        if(values[i].value!=beforeCard){
            if(cardSnapshot)*cardSnapshot=card;
            if(cardSource)*cardSource=provider;
        }
    }
    return true;
}
bool OffensiveSpell(const CommandBinding& binding,const Byte* row) noexcept {
    if(binding.spell==SpellClass::None||!(row[0x23]&1)||(row[0x20]&0x10)||!row[0x28]||!row[0x2A])return false;
    if(binding.spell==SpellClass::Fury)return true; // Explicit fingerprinted metadata, not a mask heuristic.
    return (row[0x20]&3)==2;
}
struct HitFrame {
    std::uint64_t generation=0;
    ActionToken action{};
    const AdmittedRow* row=nullptr;
    bool magic=false;
    bool core=false;
    bool gravity=false,gravityOverride=false,gravityAffinity=false;
    MixPolicy policy=MixPolicy::HighestExposure;
    unsigned count=0,nativeMask=0;
    std::array<unsigned,ElementLimit> elements{};
    std::array<AffinityPart,ElementLimit> parts{};
    std::array<Byte,108> command{};
    bool cardNative=false;
    Arcana::Elemental::Snapshot card{};
    Arcana::Elemental::Provider cardProvider=nullptr;
    Arcana::Elemental::Snapshot cardTarget{};
    Arcana::Elemental::Provider cardTargetProvider=nullptr;
    std::array<Byte,96> cardRow{};
};
thread_local std::array<HitFrame,Bus::MaximumDepth> frames{};
#include "ElementalArcana.inl"
void* EnterHit(const Bus::DamageCall& call,const void*& forward) noexcept {
    if(!CurrentData()||!Bus::currentDamage||!Bus::currentDamage->depth||
       Bus::currentDamage->depth>frames.size()||!call.info||
       !Actor(call.user,call.userActor)||!Actor(call.target,call.targetActor))return nullptr;
    const auto* expected=admitted.ExpectedCommand(call.commandId);
    if(!expected)return EnterCardWeapon(call,forward);
    if(expected->address!=call.command)return nullptr;
    std::array<Byte,108> bytes{};
    if(!Copy(bytes.data(),call.command,expected->width)||
       !admitted.Command(call.commandId,call.command,bytes.data(),expected->width))return nullptr;
    auto& frame=frames[Bus::currentDamage->depth-1];frame={};
    frame.generation=generation.load();frame.row=expected;frame.command=bytes;
    const auto& binding=pack.commands[expected->bindingIndex];
    const auto action=options.tactics?TrackHit(call,binding):ActionToken{};
    frame.action=action;
    frame.magic=options.magicBdl&&OffensiveSpell(binding,bytes.data());
    std::uint32_t commandFlags=0;std::memcpy(&commandFlags,bytes.data()+0x1C,4);
    if(options.gravity&&binding.gravity&&(bytes[0x23]&7)==1&&!(bytes[0x20]&0x10)&&
       !(commandFlags&0x40000u)&&(bytes[0x28]==5||bytes[0x28]==8)&&bytes[0x2A]){
        const auto* profile=ProfileForActor(call.target,call.targetActor);
        int maximum=0,current=0;
        if(profile&&profile->kind==ProfileKind::Monster&&profile->gravity.enabled&&
           Read(reinterpret_cast<std::uintptr_t>(call.targetActor)+0x594,maximum)&&maximum>0&&
           Read(reinterpret_cast<std::uintptr_t>(call.targetActor)+0x5D0,current)&&current>=0){
            frame.gravity=true;frame.gravityOverride=profile->gravity.overrideImmunity;
            frame.gravityAffinity=profile->gravity.elementalAffinity;
        }
    }
    if((options.core||options.tactics)&&binding.policy!=MixPolicy::NativeExact){
        std::array<AffinityValue,ElementLimit> values{};
        if(!Affinities(call.target,call.targetActor,values,action.Valid()?&action:nullptr,false,
                       &frame.cardTarget,&frame.cardTargetProvider))return nullptr;
        frame.policy=binding.policy;
        const auto add=[&](unsigned element,unsigned weight){AddHitElement(frame,values,element,weight);};
        for(const auto& part:binding.parts)add(part.element,part.weight);
        if(binding.augment){
            unsigned mask=bytes[0x2D];std::uint32_t flags=0;std::memcpy(&flags,bytes.data()+0x1C,4);
            if(flags&0x40000u){Byte weapon=0;if(!Copy(&weapon,static_cast<const Byte*>(call.userActor)+0x5D9,1))return &frame;mask|=weapon;}
            for(unsigned i=0;i<pack.registry.Size();++i)if(mask&pack.registry.At(i)->nativeBit)add(i,1);
            AddCardStrikes(frame,call,bytes.data(),values);
        }
        frame.command=bytes;frame.command[0x2D]=static_cast<Byte>(frame.nativeMask);
        frame.core=true;forward=frame.command.data();
    }
    if(frame.gravity){
        // Native formula8 already implements floor(maximum HP * power /16).
        // Rewriting this private, HP-only command view to power1 preserves the
        // original formula owner, variance draw, counters and downstream graph.
        frame.command[0x28]=8;frame.command[0x2A]=1;forward=frame.command.data();
    }
    return &frame;
}
void LeaveHit(void* token,const Bus::DamageCall&,unsigned,bool) noexcept {
    if(token)*static_cast<HitFrame*>(token)={};
}
void RequestCap(void* token,const Bus::DamageCall& call,Bus::CapRequests& request) noexcept {
    const auto* frame=static_cast<const HitFrame*>(token);
    if(!frame||(!frame->magic&&!frame->gravity)||frame->generation!=generation.load()||!CurrentData()||!frame->row)return;
    std::array<Byte,108> bytes{};
    if(!Copy(bytes.data(),call.command,frame->row->width)||
       !admitted.Command(call.commandId,call.command,bytes.data(),frame->row->width))return;
    request.magicEligible=frame->magic;
    if(frame->gravity){
        int live=0,scratch=0;
        const auto actor=reinterpret_cast<std::uintptr_t>(call.targetActor);
        if(!Actor(call.target,call.targetActor)||!ProfileForActor(call.target,call.targetActor)||
           !Read(actor+0x5D0,live)||!Read(actor+0x6E4,scratch)||live<0||scratch<0){request.Nonlethal(0);return;}
        // The native producer decrements +6E4 for already computed hits before
        // their visual results change +5D0. Both bounds protect multi-hit queues.
        request.Nonlethal((std::min)(live,scratch));
    }
}
bool AdvanceGeneration() noexcept {
    auto prior=generation.load();
    for(;;){
        if(prior==UINT64_MAX){RequestStop();return false;}
        if(generation.compare_exchange_weak(prior,prior+1))return true;
    }
}
void ResetElementData(Bus::ResetReason) noexcept {ready.store(false,std::memory_order_release);(void)AdvanceGeneration();}
const Bus::Observer observer{EnterHit,LeaveHit,RequestCap,ResetElementData};

const HitFrame* CurrentFrame(const void* command=nullptr,const void* target=nullptr) noexcept {
    const auto* scope=Bus::currentDamage;
    if(!scope||!scope->entered||scope->depth>Bus::MaximumDepth||!scope->depth||!CurrentData())return nullptr;
    const unsigned slot=static_cast<unsigned>(Bus::Slot::Elemental);
    if(scope->listeners[slot]!=&observer||!scope->participating[slot]||
       Bus::observers[slot].load()!=&observer||(target&&scope->call.targetActor!=target))return nullptr;
    const auto* frame=static_cast<const HitFrame*>(scope->tokens[slot]);
    if(!frame||frame->generation!=generation.load()||(!frame->row&&!frame->cardNative)||
       (command&&command!=frame->command.data()&&command!=scope->call.command))return nullptr;
    std::array<Byte,108> copy{};
    if(frame->row&&(!Copy(copy.data(),scope->call.command,frame->row->width)||
       !admitted.Command(scope->call.commandId,scope->call.command,copy.data(),frame->row->width)))return nullptr;
    if(!CurrentCardHit(*frame,scope->call))return nullptr;
    return frame;
}
bool ResolveAffinity(const Byte* target,const Byte* command,unsigned,int amount,int* output) noexcept {
    const auto* frame=CurrentFrame(command,target);if(!frame||!frame->core||!output)return false;
    if(frame->gravity&&!frame->gravityAffinity){*output=amount;return true;}
    const auto result=Resolve(amount,frame->parts.data(),frame->count,frame->policy);
    if(result.error!=Error::Ok)return false;
    *output=result.damage;return true;
}
const SharedElement::Resolver affinityResolver{ResolveAffinity};
bool ResolveTacticsNul(const HitFrame&,const Bus::DamageCall&,unsigned,void*,int&) noexcept;
void ClearNulReservations() noexcept;
void ReleaseNulReservations(ActionToken) noexcept;
bool ResolveNul(unsigned argument,unsigned,void* info,int& result) noexcept {
    const auto* frame=CurrentFrame();const auto* scope=Bus::currentDamage;
    if(!frame||!frame->core||!scope||scope->call.info!=info)return false;
    // A successful mixed decision already reserved this native result slot.
    // Recalculation must not consume its native Fire/Ice/etc. charge twice.
    if(SharedNul::ReservedWards(scope->call)){result=-1;return true;}
    if(ResolveTacticsNul(*frame,scope->call,argument,info,result))return true;
    // Native charges cannot claim coverage of an external key. Validate the
    // complete attack before the original consumes even one native charge.
    result=0;if(!frame->count)return true;
    const unsigned available=SharedNul::AvailableWards(scope->call);unsigned wardMask=0,nativeMask=0;
    for(unsigned i=0;i<frame->count;++i){
        const unsigned bit=pack.registry.At(frame->elements[i])->nativeBit;
        const unsigned protection=bit?bit:SharedNul::ExternalWardMask(pack.registry.At(frame->elements[i])->key.c_str());
        if(protection&&(available&protection)){wardMask|=protection;continue;}
        const unsigned offset=bit==1?0xEu:bit==2?0x10u:bit==4?0xFu:bit==8?0xDu:0u;
        Byte charge=0;if(!offset||!Copy(&charge,static_cast<const Byte*>(info)+offset,1)||!charge)return true;
        nativeMask|=bit;
    }
    if(nativeMask&&originalNul(argument,nativeMask,info)!=-1)return true;
    if(!SharedNul::ReserveWards(scope->call,wardMask))return true;
    result=-1;return true;
}
bool StartCore(){
    if(!SharedElement::Start(module)||!SharedNul::Start(module))return false;
    originalNul=SharedNul::Original();nulInstalled=true;
    return SharedNul::Register(&ResolveNul)&&SharedElement::RegisterResolver(&affinityResolver);
}

#include "ElementalTactics.inl"
#include "ElementalGravity.inl"

void RetireNativeActor(unsigned,unsigned slot) noexcept {
    if(slot>=ActorCount||!configured.load()||!armed.load())return;
    if(ownerThread.load()!=GetCurrentThreadId()){ResetElementData(Bus::ResetReason::BattleStart);return;}
    monsterProfiles[slot]={};
    if(options.tactics){
        ReleaseNulReservations(nativeActions[slot].token);
        if(statusActors[slot].token.Valid())(void)statusState.Remove(statusActors[slot].token);
        nativeActions[slot]={};statusActors[slot]={};
    }
}
const SharedActor::Observer actorObserver{RetireNativeActor,nullptr};

bool ReadMenuCatalog(ElementMenu::Catalog& output) noexcept {
    if(!configured.load(std::memory_order_acquire)||terminal.load())return false;
    auto result=ElementMenu::Defaults();unsigned external=ElementMenu::NativeCount;
    for(unsigned i=0;i<pack.registry.Size();++i){const auto& element=*pack.registry.At(i);
        const unsigned row=element.nativeBit?ElementMenu::NativeIndex(element.nativeBit):external++;
        if(row>=result.size())continue;
        auto& item=result[row];item.nativeBit=element.nativeBit;item.rgb=element.rgb;item.available=true;
        std::snprintf(item.key,sizeof(item.key),"%s",element.key.c_str());
        std::snprintf(item.label,sizeof(item.label),"%s",element.label.c_str());
    }output=result;return true;
}
const ElementMenu::Provider menuProvider{ReadMenuCatalog};

ElementalScanView::SnapshotCache scanCache;
std::atomic<unsigned> scanActor{18};
std::atomic<bool> scanRequested{true};
bool BuildScanSnapshot(unsigned actor,unsigned page,ElementalScanView::Snapshot& output) noexcept {
    output={};if(!CurrentData()||(!options.core&&!options.tactics))return false;
    const auto epoch=generation.load();unsigned total=0;
    std::array<unsigned,ElementLimit> visibleElements{};std::array<std::uint32_t,ElementLimit> colors{};
    for(unsigned i=0;i<pack.registry.Size();++i){const auto* descriptor=pack.registry.At(i);bool visible=false;std::uint32_t rgb=0;
        if(!ElementScan::RegisteredPresentation(descriptor->key.c_str(),descriptor->nativeBit,descriptor->rgb,rgb,visible))return false;
        if(visible){visibleElements[total]=i;colors[total++]=rgb;}}
    const unsigned count=ElementalScanView::VisibleCount(total,page);if(!count)return false;
    ElementalScanView::Snapshot snapshot{};snapshot.generation=epoch;snapshot.total=total;snapshot.page=page;snapshot.count=count;
    const auto displayNames=ElementMenu::Read();
    for(unsigned i=0;i<count;++i){
        const unsigned visible=page*ElementalScanView::PageSize+i;
        ElementalView value{};if(!ReadElement(actor,visibleElements[visible],value))return false;
        auto& row=snapshot.rows[i];const char* label=value.label?value.label:"Element";
        bool plain=true;for(const char* p=label;*p;++p)if(static_cast<unsigned char>(*p)<32||static_cast<unsigned char>(*p)>126){plain=false;break;}
        if(!plain&&value.key)label=value.key;
        const auto* descriptor=pack.registry.At(visibleElements[visible]);
        const auto named=ElementMenu::Find(displayNames,descriptor->nativeBit,value.key);
        std::snprintf(row.label,sizeof(row.label),"%s",named<ElementMenu::Count?displayNames[named].label:label);
        row.baseBp=value.baseBp;row.effectiveBp=value.effectiveBp;row.equipmentBp=value.equipmentBp;row.rgb=colors[visible];
        row.imperil=value.imperil;row.ward=value.ward;row.nul=value.nul;
        row.imperilTurns=value.imperilTurns;row.wardTurns=value.wardTurns;row.nulTurns=value.nulTurns;
        row.locked=value.locked;row.imperilImmune=value.imperilImmune;row.imperilResistanceBp=value.imperilResistanceBp;
    }
    if(epoch!=generation.load()||!CurrentData())return false;
    output=snapshot;return true;
}
void PublishScanSnapshot() noexcept {
    if(!CurrentData()||(!options.core&&!options.tactics))return;
    if(!scanRequested.exchange(false,std::memory_order_acq_rel))return;
    (void)scanCache.Publish(scanActor.load(std::memory_order_acquire),generation.load(),BuildScanSnapshot);
}
bool ReadScanSnapshot(unsigned actor,unsigned page,ElementalScanView::Snapshot& output) noexcept {
    output={};if(actor>=ActorCount||page>=ElementalScanView::MaximumPages||!ready.load()||!armed.load())return false;
    if(CurrentData())return BuildScanSnapshot(actor,page,output);
    scanActor.store(actor,std::memory_order_release);scanRequested.store(true,std::memory_order_release);
    const auto epoch=generation.load();
    const bool copied=scanCache.Copy(actor,page,epoch,output);
    if(!copied||epoch!=generation.load()||!ready.load()||!armed.load()){output={};return false;}
    return true;
}
const ElementalScanView::Provider scanProvider{ReadScanSnapshot};

bool ReadManifest(std::string& output,const std::string& selected){
    const std::string loaded=Config::GetLoadedPath();if(loaded.empty())return false;
    const std::filesystem::path ini=std::filesystem::u8path(loaded);
    const auto relative=std::filesystem::u8path(selected);
    if(selected.empty()||selected.size()>200||relative.is_absolute())return false;
    for(const auto& part:relative)if(part==".."||part==".")return false;
    std::ifstream file(ini.parent_path()/relative,std::ios::binary|std::ios::ate);
    if(!file)return false;
    const auto size=file.tellg();if(size<=0||size>1024*1024)return false;
    output.resize(static_cast<std::size_t>(size));file.seekg(0);
    return static_cast<bool>(file.read(output.data(),static_cast<std::streamsize>(output.size())));
}
} // namespace

bool PrepareText(std::uintptr_t image,RuntimeOptions selected,std::string_view manifest,bool validateOnly,RuntimeLog log){
    if(configured.load()||terminal.load())return false;
    if(validateOnly){code=RuntimeCode::ValidationOnly;return false;}
    if(!selected.core&&!selected.tactics&&!selected.gravity&&!selected.magicBdl){code=RuntimeCode::Disabled;return false;}
    try {
        Pack candidate;PackProblem problem{};
        const bool loaded=LoadPack(manifest,availableCapabilities,candidate,problem);
        const bool slots=loaded&&EnsureHookSlots(candidate);
        if(!loaded||!slots){
            code=RuntimeCode::PackInvalid;
            if(log){char message[256]{};std::snprintf(message,sizeof(message),
                "[ffx-hooks] Elemental pack rejected: code=%u field=%.128s nativeSlots=%d\n",
                static_cast<unsigned>(problem.code),problem.field.c_str(),slots?1:0);log(message);}
            return false;
        }
        if(!Profile(image,candidate)){code=RuntimeCode::Unsupported;return false;}
        HMODULE pin=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&PrepareText),&pin)){code=RuntimeCode::Conflict;return false;}
        module=image;options=selected;logger=log;pack=std::move(candidate);
        if((options.core||options.tactics||options.gravity)&&(!SharedActor::Start(module)||
            !SharedActor::Subscribe(SharedActor::Slot::Elemental,&actorObserver))){code=RuntimeCode::Conflict;RequestStop();return false;}
        if((options.core||options.tactics)&&!StartCore()){code=RuntimeCode::Conflict;terminal=true;return false;}
        if(options.gravity&&!StartGravity()){code=RuntimeCode::Conflict;RequestStop();return false;}
        if(options.tactics&&!StartTactics()){code=RuntimeCode::Conflict;RequestStop();return false;}
        if(!Bus::Subscribe(Bus::Slot::Elemental,&observer)){code=RuntimeCode::Conflict;return false;}
        if(!ElementalScanView::Register(&scanProvider)||!ElementMenu::Register(&menuProvider)){code=RuntimeCode::Conflict;RequestStop();return false;}
        configured=true;code=RuntimeCode::WaitingForData;return true;
    }catch(...){code=RuntimeCode::PackInvalid;return false;}
}
bool Prepare(std::uintptr_t image,bool validateOnly,RuntimeLog log){
    const RuntimeOptions selected{ModFeatures::Enabled(ModFeatures::Feature::Core),ModFeatures::Enabled(ModFeatures::Feature::Tactics),
        ModFeatures::Enabled(ModFeatures::Feature::Gravity),ModFeatures::Enabled(ModFeatures::Feature::MagicBdl)};
    if(validateOnly||(!selected.core&&!selected.tactics&&!selected.gravity&&!selected.magicBdl))
        return PrepareText(image,selected,{},validateOnly,log);
    try {
        const std::string selection=Config::GetString("elemental.pack","");
        bool defaultExists=false;
        if(selection.empty()){
            const std::string loaded=Config::GetLoadedPath();
            if(!loaded.empty()){
                std::error_code error;
                defaultExists=std::filesystem::exists(std::filesystem::u8path(loaded).parent_path()/"elemental-pack.json",error);
                if(error){code=RuntimeCode::PackMissing;return false;}
            }
        }
        if(UseBuiltinElements(selection,defaultExists)){
            const bool prepared=PrepareText(image,selected,BuiltinPackText(),false,log);
            builtinSource.store(prepared,std::memory_order_release);
        if(log)log(prepared?"[ffx-hooks] Built-in elements registered: eight native, Poison, Gravity\n":
                                "[ffx-hooks] Built-in element startup rejected\n");
            return prepared;
        }
        std::string manifest;if(!ReadManifest(manifest,selection.empty()?"elemental-pack.json":selection)){code=RuntimeCode::PackMissing;return false;}
        return PrepareText(image,selected,manifest,false,log);
    }catch(...){code=RuntimeCode::PackMissing;return false;}
}
bool Activate(){
    if(!configured.load()||terminal.load()||!EquipmentWorkshop::CombatProducerReady()||
       ((options.magicBdl||options.gravity)&&!IsCombatDamageClampInstalled()))return false;
    armed.store(true,std::memory_order_release);return true;
}
void TickMainThread() noexcept {
    if(!configured.load()||!armed.load())return;
    DWORD empty=0;const DWORD thread=GetCurrentThreadId();ownerThread.compare_exchange_strong(empty,thread);
    if(ownerThread.load()!=thread||inTick)return;
    inTick=true;struct Leave {~Leave(){inTick=false;}} leave;
    InputStamp current{};
    if(!CaptureStamp(current)){ready=false;hasAttemptedStamp=false;code=RuntimeCode::WaitingForData;return;}
    const auto epoch=generation.load();
    if(hasAttemptedStamp&&attemptedGeneration==epoch&&current==attemptedStamp){
        if(ready.load()){RefreshMonsterProfiles();if(options.tactics)SyncStatusActors();PublishScanSnapshot();}return;
    }
    ready=false;attemptedStamp=current;attemptedGeneration=epoch;hasAttemptedStamp=true;
    try {
        char locale[8]{};if(!Locale(locale)){code=RuntimeCode::DataMismatch;return;}
        std::array<std::vector<Byte>,5> copies;
        std::array<LoadedBank,5> inputs{};
        for(unsigned i=0;i<pack.banks.size();++i){
            copies[i].resize(pack.banks[i].bytes);
            const auto* original=reinterpret_cast<const Byte*>(std::uintptr_t(current.banks[i]));
            if(!Copy(copies[i].data(),original,copies[i].size())){code=RuntimeCode::DataMismatch;return;}
            inputs[i]={pack.banks[i].kind,locale,copies[i].data(),copies[i].size(),original};
        }
        PackAdmission candidate;AdmissionProblem problem{};
        if(!candidate.Prepare(pack,inputs.data(),static_cast<unsigned>(pack.banks.size()),Digest,problem)){
            code=RuntimeCode::DataMismatch;if(logger)logger("[ffx-hooks] Elemental pack inactive: loaded data/locale/fingerprint mismatch\n");return;
        }
        InputStamp after{};
        if(!configured.load()||!armed.load()||epoch!=generation.load()||!CaptureStamp(after)||!(current==after))return;
        // A reentrant callback holding an old row must see its generation retire
        // before vector storage is replaced, even if the bank has equal bytes.
        if(!AdvanceGeneration())return;
        attemptedGeneration=generation.load();admitted=std::move(candidate);activeStamp=current;
        bindingCount=static_cast<unsigned>(admitted.Size());
        monsterProfiles={};RefreshMonsterProfiles();
        if(options.tactics&&!ResetStatusState()){RequestStop();return;}
        ready.store(true,std::memory_order_release);code=RuntimeCode::Ready;
        scanRequested=true;PublishScanSnapshot();
        if(logger)logger("[ffx-hooks] Elemental pack admitted against loaded native banks\n");
    }catch(...){if(configured.load())code=RuntimeCode::DataMismatch;ready=false;}
}
static_assert(std::atomic<bool>::is_always_lock_free, "Detach gates must never acquire a runtime lock");
void RequestDetachStop() noexcept {
    terminal=true;armed=false;ready=false;configured=false;
}
void RequestStop() noexcept {
    RequestDetachStop();
    ElementalScanView::Unregister(&scanProvider);
    ElementMenu::Unregister(&menuProvider);
    SharedActor::Unsubscribe(SharedActor::Slot::Elemental,&actorObserver);
    StopTactics();
    SharedElement::UnregisterResolver(&affinityResolver);
    SharedNul::Unregister(&ResolveNul);
    Bus::Unsubscribe(Bus::Slot::Elemental,&observer);code=RuntimeCode::Stopped;
}
RuntimeStatus RuntimeState() noexcept {
    return {code.load(),configured.load()?availableCapabilities:0,bindingCount.load(),ownerThread.load(),generation.load()};
}
unsigned DescriptorCount() noexcept {return CurrentData()?pack.registry.Size():0;}
bool ReadElement(unsigned slot,unsigned element,ElementalView& output) noexcept {
    output={};if(!CurrentData()||slot>=ActorCount||element>=pack.registry.Size())return false;
    std::uint32_t base=0;if(!Read(module + (::FfxHooks::ExecutableProfile::Rva<0xD334CC>()),base)||base<0x10000||base>UINT32_MAX-ActorCount*0xF90u)return false;
    const auto* actor=reinterpret_cast<const Byte*>(std::uintptr_t(base)+slot*0xF90u);
    if(options.tactics)SyncStatusActors();
    std::array<AffinityValue,ElementLimit> values{};
    if(!Affinities(slot,actor,values,nullptr,true))return false;
    std::array<std::int32_t,ElementLimit> equipment{};if(!EquipmentDeltas(slot,actor,equipment))return false;
    output.equipmentBp=equipment[element];
    const auto* descriptor=pack.registry.At(element);const auto state=Timed(slot,element,nullptr,true);
    Byte native[4]{};if(!Copy(native,actor+0x5DA,4))return false;
    const NativeMasks masks{native[3],native[2],native[1],native[0]};
    output.baseBp=descriptor->nativeBit?NativeBase(descriptor->nativeBit,masks).value:10000;
    if(const auto* profile=ProfileForActor(slot,actor)){
        output.imperilResistanceBp=profile->imperilResistBp;
        for(const auto& affinity:profile->affinities)if(affinity.element==element){
            output.baseBp=affinity.baseBp;output.locked=affinity.locked;output.imperilImmune=affinity.imperilImmune;
            if(affinity.imperilResistBp<=10000)output.imperilResistanceBp=affinity.imperilResistBp;
        }
    }
    F7Elements::Selection difficulty{};
    if(!output.locked&&F7Elements::Read(slot,reinterpret_cast<std::uintptr_t>(actor),difficulty))
        F7Elements::Base(difficulty,descriptor->nativeBit,descriptor->key.c_str(),output.baseBp);
    output.key=descriptor->key.c_str();output.label=descriptor->label.c_str();output.rgb=descriptor->rgb;
    output.effectiveBp=values[element].value;output.imperil=state.imperil;output.ward=state.ward;output.nul=state.nul;
    output.imperilTurns=state.imperilTurns;output.wardTurns=state.wardTurns;output.nulTurns=state.nulTurns;
    output.locked=output.locked||state.locked;output.imperilImmune=output.imperilImmune||state.imperilImmune;
    if(state.valid)output.imperilResistanceBp=state.imperilResistanceBp;return true;
}
const char* RuntimeDetail() noexcept {
    switch(code.load()){
    case RuntimeCode::Disabled:return "Disabled";case RuntimeCode::ValidationOnly:return "Validation only";
    case RuntimeCode::WaitingForData:return builtinSource.load()?"Built-in elements; waiting for battle":"Waiting for native battle data";
    case RuntimeCode::Ready:return builtinSource.load()?"Built-in elements ready":"Ready";
    case RuntimeCode::Unsupported:return "Unsupported profile or consumer";case RuntimeCode::PackMissing:return "Manifest missing or unreadable";
    case RuntimeCode::PackInvalid:return "Manifest schema or capability mismatch";case RuntimeCode::DataMismatch:return "Loaded data or language mismatch";
    case RuntimeCode::Conflict:return "Shared runtime ownership conflict";case RuntimeCode::Stopped:return "Stopped";
    }
    return "Unavailable";
}
} // namespace FfxHooks::ElementalDominion
