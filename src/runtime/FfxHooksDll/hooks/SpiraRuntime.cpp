#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include "SpiraRuntime.h"
#include "SpiraRules.h"
#include "ModFeatureCatalog.h"
#include "VanguardCatalog.h"
#include "AeonAscensionBridge.h"
#include "EquipmentEffectBridge.h"
#include "EquipmentWorkshopRuntime.h"
#include "EquipmentEffects.h"
#include "CombatExtensionBus.h"
#include "SharedTurnRuntime.h"
#include "SharedActorRuntime.h"
#include "SharedClampRuntime.h"
#include "NovaSuperDamageHook.h"
#include "NativeUiHookSupport.h"
#include "../shared/Config.h"
#include <array>
#include <atomic>
#include <vector>

namespace FfxHooks::SpiraAbilities {
namespace {
namespace W=EquipmentWorkshop;
namespace Bus=CombatExtensions;
namespace Pipeline=EquipmentEffects::Pipeline;
using Byte=unsigned char;
std::uintptr_t module=0;
RuntimeOptions options{};
LogFn logger=nullptr;
std::atomic<bool> configured{false},armed{false},ready{false},terminal{false};
std::atomic<DWORD> ownerThread{0};
std::atomic<std::uint64_t> epoch{1};
Mapping mapping=DefaultMapping();
using ForeignMapping=std::array<unsigned,Vanguard::AbilityCount>;
ForeignMapping foreignMapping{};
std::vector<Byte> kernelSnapshot;
std::uint32_t languageManager=0,languageId=0;
Bindings bindings{};
std::array<std::array<Byte,108>,Count> rows{};
std::array<std::array<Byte,20>,3> vanillaBreaks{};
std::uint32_t bankAddress=0;
std::uint16_t bankBytes=0;
std::uint64_t proof=0;
// NativeUiSupport publishes these trampolines before enabling the batch.
void* originals[2]{};
bool inTick=false;
bool Copy(void* out,const void* source,std::size_t size) noexcept {return NativeUiSupport::Copy(out,source,size);}
template<class T> bool Read(std::uintptr_t address,T& out) noexcept {
    return address>=0x10000&&address<=UINT32_MAX-sizeof(T)&&Copy(&out,reinterpret_cast<const void*>(address),sizeof(T));
}
void ReadMappings(Mapping& ids,ForeignMapping& foreign){
    ids=DefaultMapping();
    for(unsigned i=0;i<Count;++i){char key[96]{};std::snprintf(key,sizeof(key),"spira_ids.%s",Entries[i].key);
        const auto item=Config::ReadIntExact(key,148,4095);
        if(item.state==Config::IntReadState::Valid)ids[i]=static_cast<unsigned>(item.value);
        else if(item.state==Config::IntReadState::Invalid)ids[i]=0;
    }
    for(unsigned i=0;i<foreign.size();++i){char key[96]{};std::snprintf(key,sizeof(key),"vanguard_ids.%s",Vanguard::Abilities[i].key);
        const auto item=Config::ReadIntExact(key,135,4095);
        foreign[i]=item.state==Config::IntReadState::Missing?Vanguard::Abilities[i].id:
            item.state==Config::IntReadState::Valid?static_cast<unsigned>(item.value):0;
    }
}
bool Language(std::uint32_t& manager,std::uint32_t& id) noexcept {
    return Read(module+0x8DED48,manager)&&manager>=0x10000&&manager<=UINT32_MAX-8&&Read(manager+4,id)&&id<=18;
}
bool Current() noexcept {
    std::uint32_t pointer=0;std::uint16_t size=0;
    std::uint32_t manager=0,language=0;
    if(!ready.load()||!armed.load()||ownerThread.load()!=GetCurrentThreadId()||
       !Read(module+0xD2A944,pointer)||pointer!=bankAddress||!Read(module+0xD2A970,size)||size!=bankBytes||
       !Language(manager,language)||manager!=languageManager||language!=languageId||kernelSnapshot.size()!=size)return false;
    // A pointer/length or row-prefix check misses changed names, headers and
    // reused allocations. Compare the bounded admitted bank without allocating
    // or hashing in a native callback; configuration is checked independently.
    std::array<Byte,512> chunk{};
    for(std::size_t offset=0;offset<size;offset+=chunk.size()){
        const auto count=(std::min)(chunk.size(),std::size_t(size)-offset);
        if(!Copy(chunk.data(),reinterpret_cast<const void*>(std::uintptr_t(pointer)+offset),count)||
           std::memcmp(chunk.data(),kernelSnapshot.data()+offset,count))return false;
    }
    try{Mapping current{};ForeignMapping foreign{};ReadMappings(current,foreign);
        return current==mapping&&foreign==foreignMapping;
    }catch(...){return false;}
}
bool RowCurrent(unsigned index,const Byte* original=nullptr) noexcept {
    if(!Current()||index>=Count||!bindings[index].rowVerified)return false;
    const auto address=std::uintptr_t(bankAddress)+20+mapping[index]*108;
    if(original&&original!=reinterpret_cast<const Byte*>(address))return false;
    std::array<Byte,108> actual{};
    return Copy(actual.data(),reinterpret_cast<const void*>(address),actual.size())&&actual==rows[index];
}
bool EnabledIndex(unsigned index) noexcept {
    return index<Count&&(index<2?options.ascension:options.spira)&&
        (Entries[index].definition==Definition::Defined||Entries[index].definition==Definition::NativeBaseOnly)&&RowCurrent(index);
}
Byte* Actor(unsigned owner) noexcept {
    if(owner>=31||SharedActor::Busy(owner))return nullptr;
    std::uint32_t pool=0;std::uint16_t slot=65535,identity=65535;
    if(!Read(module+0xD334CC,pool)||pool<0x10000||pool>UINT32_MAX-31*0xF90u)return nullptr;
    auto* actor=reinterpret_cast<Byte*>(std::uintptr_t(pool)+owner*0xF90u);
    if(!Copy(&slot,actor+0xC,2)||slot!=owner||!Copy(&identity,actor+0xE,2)||(owner<18&&identity!=owner))return nullptr;
    return actor;
}
bool Equipped(unsigned owner,unsigned effect) noexcept {
    if(owner>=18||!EnabledIndex(effect))return false;
    const auto& definition=Entries[effect];if((definition.owners&(1u<<owner))==0||definition.kind>=2)return false;
    const auto kind=definition.kind;Byte slot=255;
    // Field and battle share canonical persistent equipped identities; never
    // infer an exclusive owner from a monster appearance or table word.
    if(!Read(module+0xD3205C+owner*0x94u+0x2Du+kind,slot)||slot>=200)return false;
    const auto address=module+0xD30F2C+slot*22u;std::array<Byte,22> gear{};
    if(!Copy(gear.data(),reinterpret_cast<const void*>(address),gear.size())||!gear[2]||
       gear[4]!=owner||gear[5]!=kind||gear[6]!=owner||gear[11]>4)return false;
    if(effect<2)return W::AscensionEffect(owner,effect);
    for(unsigned i=0;i<gear[11];++i)if(Word(gear.data()+14+i*2)==bindings[effect].word)return true;
    workshop::Piece piece{};
    return W::ReadPresentation(reinterpret_cast<const void*>(address),piece)&&piece.fifthUnlocked&&
        workshop::NativeSlotsFilled(piece)&&piece.fifth==bindings[effect].word&&piece.abilities[4]!=0;
}
bool PaidMapping(AeonAscension::Mapping& out) noexcept {
    out={};if(!options.ascension||!RowCurrent(0)||!RowCurrent(1))return false;
    out.enabled=true;out.proof=proof;out.words={static_cast<std::uint16_t>(bindings[0].word),static_cast<std::uint16_t>(bindings[1].word)};
    for(unsigned i=0;i<3;++i){
        std::array<Byte,20> actual{};
        if(!Copy(actual.data(),reinterpret_cast<const void*>(std::uintptr_t(bankAddress)+20+(23+i)*108+0x58),20)||actual!=vanillaBreaks[i])return false;
        const auto flags=Word(actual.data()+12);if(flags&(0x200u<<i))out.replacements[i]=static_cast<std::uint16_t>(0x8017+i);
    }
    return AeonAscension::ValidMapping(out);
}
const AeonAscension::Provider paidProvider{PaidMapping};
bool Fifth(unsigned owner,const workshop::Piece& piece) noexcept {
    if(!piece.fifthUnlocked||!workshop::NativeSlotsFilled(piece)||!piece.abilities[4]||piece.native[4]!=owner)return false;
    for(unsigned i=0;i<Count;++i)if(bindings[i].word==piece.fifth&&OwnerKindMatches(i,owner,piece.native[5])&&EnabledIndex(i))
        return i<2?W::AscensionEffect(owner,i):true;
    return false;
}
bool EffectRow(unsigned owner,unsigned slot,unsigned position,const Byte* gear,unsigned word,const void* table,
               const Byte* original,Byte* output) noexcept {
    if(!Current()||!gear||!output||table!=reinterpret_cast<const void*>(std::uintptr_t(bankAddress)))return false;
    for(unsigned i=0;i<Count;++i)if(AutoAbilitySlots::Ascension(i,mapping[i])&&bindings[i].rowVerified&&word==0x8000u+mapping[i]){
        const bool admitted=slot<200&&position<5&&gear[2]&&gear[4]==owner&&gear[6]==owner&&
            OwnerKindMatches(i,owner,gear[5])&&EnabledIndex(i)&&RowCurrent(i,original)&&
            (i>=2||W::AscensionEffect(owner,i));
        // These are private row copies. Rejected exclusive payloads never reach
        // the native flags/stat aggregator through a copied equipment ID.
        if(!admitted){std::memset(output+16,0,92);return true;}
        if(i==0){output[0x64]=0;output[0x65]=6;return true;}
        if(i==1){output[0x64]=0;output[0x65]=8;return true;}
        return false;
    }
    return false;
}
const Pipeline::Provider equipmentProvider{Fifth,EffectRow};
struct DoubleScope {unsigned owner=31;Byte* actor=nullptr;DoubleScope* previous=nullptr;unsigned depth=0;};
thread_local DoubleScope* doubleScope=nullptr;
void AdjustClamp(std::uintptr_t caller,int&,int&,int& maximum) noexcept {
    unsigned owner=31;bool hp=false,mp=false;
    if(Current()&&Pipeline::current&&Pipeline::current->depth<=16&&!Pipeline::current->battle){
        owner=Pipeline::current->owner;hp=caller==0x3868A7;mp=caller==0x3868CF;
    }
    if(Current()&&doubleScope&&doubleScope->depth<=16&&Actor(doubleScope->owner)==doubleScope->actor){
        owner=doubleScope->owner;hp=caller==0x38D3A4;mp=caller==0x38D418;
    }
    if((hp||mp)&&owner<18){
        const bool paid=options.ascension&&W::AscensionEffect(owner,0);
        if(hp)maximum=HpCeiling(maximum,owner,paid,options.spira&&Equipped(owner,5));
        if(mp)maximum=MpCeiling(maximum,owner,paid);
    }
}
int __cdecl DoubleShim(unsigned owner,Byte* actor,int mask){
    DoubleScope frame{owner,actor,doubleScope,doubleScope?doubleScope->depth+1:1};doubleScope=&frame;int result=0;
    __try{result=reinterpret_cast<int(__cdecl*)(unsigned,Byte*,int)>(originals[0])(owner,actor,mask);}
    __finally{doubleScope=frame.previous;}
    return result;
}
int __cdecl RewardShim(unsigned item,int quantity,void* rewards){
    if(rewards==reinterpret_cast<void*>(module+0x1F10EA0)&&
       (item&0xFFFFF000u)==0x2000u&&quantity>0){
        const auto multiplier=PartyDropMultiplier();
        if(multiplier>1){
            // Bound before native signed addition to an existing BYTE quantity.
            // Only this award is scaled; the accumulated total is never replayed.
            const auto scaled=static_cast<std::int64_t>(quantity)*multiplier;
            quantity=static_cast<int>((std::min)(scaled,std::int64_t{99}));
        }
    }
    return reinterpret_cast<int(__cdecl*)(unsigned,int,void*)>(originals[1])(item,quantity,rewards);
}
struct Hit {std::uint64_t generation=0;bool valid=false,paid=false;std::array<Byte,108> command{};};
thread_local std::array<Hit,Bus::MaximumDepth> hits{};
void* EnterHit(const Bus::DamageCall& call,const void*& forwarded) noexcept {
    if(!Current()||!Bus::currentDamage||!Bus::currentDamage->depth||Bus::currentDamage->depth>hits.size()||
       Actor(call.user)!=call.userActor||Actor(call.target)!=call.targetActor)return nullptr;
    const auto family=call.commandId>>12;const unsigned width=family==2||family==3?96:family==4||family==6?92:0;
    if(!width||!forwarded)return nullptr;
    auto& hit=hits[Bus::currentDamage->depth-1];hit={};hit.generation=epoch.load();
    if(!Copy(hit.command.data(),forwarded,width))return nullptr;
    const auto* row=hit.command.data();
    hit.valid=(row[0x23]&1)!=0&&(row[0x20]&0x10)==0&&row[0x28]!=0;
    hit.paid=hit.valid&&call.user>=8&&call.user<18&&options.ascension&&W::AscensionEffect(call.user,1);
    // The admitted equipment row supplies native BDL bit0x800. Preserve the
    // command WORD+0x20 selector and the separate native negative floor. Only
    // the shared upper-clamp policy expands positive HP damage.
    return &hit;
}
void LeaveHit(void* token,const Bus::DamageCall&,unsigned,bool) noexcept {if(token)*static_cast<Hit*>(token)={};}
void Cap(void* token,const Bus::DamageCall& call,Bus::CapRequests& request) noexcept {
    const auto* hit=static_cast<const Hit*>(token);
    if(hit&&hit->paid&&hit->generation==epoch.load()&&Current())request.aeonAuthorized=W::AscensionEffect(call.user,1);
}
int Modify(void* token,const Bus::DamageCall& call,int amount) noexcept {
    const auto* hit=static_cast<const Hit*>(token);
    if(!hit||!hit->valid||hit->generation!=epoch.load()||!Current()||!options.spira)return amount;
    return BargainDamage(amount,true,Equipped(call.user,4),Equipped(call.target,4));
}
bool AdvanceEpoch() noexcept {
    auto previous=epoch.load();
    for(;;){if(previous==UINT64_MAX){RequestStop();return false;}
        if(epoch.compare_exchange_weak(previous,previous+1))return true;}
}
void ResetData(Bus::ResetReason) noexcept {ready=false;(void)AdvanceEpoch();}
const Bus::Observer combatObserver{EnterHit,LeaveHit,Cap,ResetData,Modify};
void Turn(unsigned owner,void* actor,std::uint32_t) noexcept {
    if(!Current()||!options.spira||owner>=18||Actor(owner)!=actor||!Equipped(owner,2))return;
    int hp=0,current=0,maximum=0;auto* bytes=static_cast<Byte*>(actor);
    if(!Copy(&hp,bytes+0x5D0,4)||hp<=0||!Copy(&current,bytes+0x5D4,4)||!Copy(&maximum,bytes+0x598,4))return;
    const int next=ManaSpring(current,maximum);if(next!=current)Copy(bytes+0x5D4,&next,4);
}
const SharedTurn::Observer turnObserver{Turn};
bool Profile(std::uintptr_t base){
    Byte header[0x1000]{};F8Runtime::ExecutableIdentity identity{};
    if(!Copy(header,reinterpret_cast<void*>(base),sizeof(header))||
       F8Runtime::ParseExecutableIdentity(header,sizeof(header),&identity)!=F8Runtime::ProfileResult::Supported||!F8Runtime::IsSupportedExecutable(identity))return false;
    const Byte clamp[]={0x55,0x8B,0xEC,0x8B,0x45,0x08,0x8B,0x4D,0x0C,0x3B,0xC1,0x7D,0x02,0x8B,0xC1,0x8B,0x4D,0x10,0x3B,0xC1,0x7E,0x02,0x8B,0xC1,0x5D,0xC3};
    const Byte doubled[]={0x55,0x8B,0xEC,0x53,0x8B,0x5D,0x10,0x56,0x8B,0x75,0x0C,0x57,0x0F,0xB6,0x96,0x40};
    const Byte reward[]={0x55,0x8B,0xEC,0x8B,0x55,0x08,0x85,0xD2,0x74,0x62,0x8B,0x45,0x0C,0x85,0xC0,0x7E,0x5B};
    Byte actual[sizeof(clamp)]{};
    if(!Copy(actual,reinterpret_cast<void*>(base+0x39A0D0),sizeof(clamp))||
       (std::memcmp(actual,clamp,sizeof(clamp))&&!SharedClamp::MatchesOwned(base,actual,sizeof(clamp)))||
       !Copy(actual,reinterpret_cast<void*>(base+0x38D330),sizeof(doubled))||std::memcmp(actual,doubled,sizeof(doubled))||
       !Copy(actual,reinterpret_cast<void*>(base+0x398AD0),sizeof(reward))||std::memcmp(actual,reward,sizeof(reward)))return false;
    for(const unsigned caller:{0x3868A7u,0x3868CFu,0x38D3A4u,0x38D418u}){
        Byte bytes[5]{};std::int32_t relative=0;
        if(!Copy(bytes,reinterpret_cast<void*>(base+caller-5),5)||bytes[0]!=0xE8)return false;
        std::memcpy(&relative,bytes+1,4);if(caller+relative!=0x39A0D0u)return false;
    }
    return true;
}
} // namespace

bool Prepare(std::uintptr_t image,RuntimeOptions selected,bool validateOnly,LogFn log){
    if(configured.load()||terminal.load()||validateOnly||(!selected.spira&&!selected.ascension))return false;
    if(!Profile(image))return false;
    module=image;options=selected;logger=log;
    const std::uint32_t rvas[]={0x38D330,0x398AD0};
    void* replacements[]={reinterpret_cast<void*>(&DoubleShim),reinterpret_cast<void*>(&RewardShim)};
    if(!SharedClamp::Start(image)||!SharedClamp::Register(SharedClamp::Slot::Spira,&AdjustClamp))return false;
    if(!NativeUiSupport::Install(image,rvas,replacements,originals,MinHookBatch::Owner::SpiraRuntime,reinterpret_cast<const void*>(&DoubleShim))){RequestStop();return false;}
    if(!Pipeline::Register(&equipmentProvider)||!Bus::Subscribe(Bus::Slot::Aeon,&combatObserver)||
       !AeonAscension::RegisterProvider(&paidProvider)||
       (selected.spira&&(!SharedTurn::Start(image)||!SharedTurn::Register(SharedTurn::Consumer::Spira,&turnObserver)))){RequestStop();return false;}
    configured=true;return true;
}
bool Prepare(std::uintptr_t image,bool validateOnly,LogFn log){
    return Prepare(image,{ModFeatures::Enabled(ModFeatures::Feature::Spira),ModFeatures::Enabled(ModFeatures::Feature::Ascension)},validateOnly,log);
}
bool Activate() noexcept {
    if(!configured.load()||terminal.load()||!W::CombatProducerReady()||!IsCombatDamageClampInstalled())return false;
    armed=true;return true;
}
void TickMainThread() noexcept {
    if(!configured.load()||!armed.load()||inTick)return;
    DWORD empty=0;ownerThread.compare_exchange_strong(empty,GetCurrentThreadId());if(ownerThread.load()!=GetCurrentThreadId())return;
    inTick=true;struct Leave{~Leave(){inTick=false;}} leave;
    try{
        std::uint32_t address=0;std::uint16_t size=0;
        if(!Read(module+0xD2A944,address)||!Read(module+0xD2A970,size)||address<0x10000||size<20||address>UINT32_MAX-size){ready=false;return;}
        if(Current())return;
        ready=false;
        std::uint32_t manager=0,language=0;if(!Language(manager,language))return;
        std::vector<Byte> bytes(size);if(!Copy(bytes.data(),reinterpret_cast<void*>(std::uintptr_t(address)),size)){ready=false;return;}
        Mapping ids{};ForeignMapping foreign{};ReadMappings(ids,foreign);
        auto result=Validate(bytes.data(),bytes.size(),ids,foreign.data(),static_cast<unsigned>(foreign.size()),NameEncoding::Latin,AllConsumers);
        const auto numeric=Validate(bytes.data(),bytes.size(),ids,foreign.data(),static_cast<unsigned>(foreign.size()),NameEncoding::NumericPlaceholder,AllConsumers);
        bool any=false;std::uint64_t hash=1469598103934665603ull;
        for(const Byte byte:bytes){hash^=byte;hash*=1099511628211ull;}
        for(unsigned i=0;i<Count;++i){if(!result[i].rowVerified&&numeric[i].rowVerified)result[i]=numeric[i];
            hash^=ids[i];hash*=1099511628211ull;if(result[i].rowVerified){any=true;std::memcpy(rows[i].data(),bytes.data()+20+ids[i]*108,108);}}
        for(const auto id:foreign){hash^=id;hash*=1099511628211ull;}
        unsigned last=0;std::size_t pool=0;
        if(!any||!Table(bytes.data(),bytes.size(),last,pool)||last<25){ready=false;return;}
        for(unsigned i=0;i<3;++i)std::memcpy(vanillaBreaks[i].data(),bytes.data()+20+(23+i)*108+0x58,20);
        if(!AdvanceEpoch())return;
        mapping=ids;foreignMapping=foreign;bindings=result;bankAddress=address;bankBytes=size;proof=hash?hash:1;
        languageManager=manager;languageId=language;kernelSnapshot=std::move(bytes);ready=true;
        if(logger)logger("[ffx-hooks] Spira loaded ability identities admitted\n");
    }catch(...){ready=false;}
}
static_assert(std::atomic<bool>::is_always_lock_free, "Detach gates must never acquire a runtime lock");
void RequestDetachStop() noexcept {
    terminal=true;armed=false;ready=false;configured=false;
}
void RequestStop() noexcept {
    RequestDetachStop();
    SharedClamp::Unregister(SharedClamp::Slot::Spira,&AdjustClamp);
    SharedTurn::Unregister(SharedTurn::Consumer::Spira,&turnObserver);
    Pipeline::Unregister(&equipmentProvider);AeonAscension::UnregisterProvider(&paidProvider);Bus::Unsubscribe(Bus::Slot::Aeon,&combatObserver);
}
bool Ready() noexcept {return Current();}
bool HasEffect(unsigned owner,unsigned effect) noexcept {return Equipped(owner,effect);}
unsigned PartyDropMultiplier() noexcept {
    if(!Current()||!options.spira)return 1;
    unsigned result=1;for(unsigned owner=0;owner<18;++owner){
        const auto* actor=Actor(owner);Byte active=0;
        if(actor&&Copy(&active,actor+0xDC8,1)&&active)result=DropMaximum(result,Equipped(owner,7),Equipped(owner,8));
    }
    return result;
}
const char* RuntimeDetail() noexcept {return terminal.load()?"Stopped":Current()?"Ready":configured.load()?"Waiting for native ability data":"Disabled";}
} // namespace FfxHooks::SpiraAbilities
