#include "VanguardRuntime.h"
#include "VanguardUiBridge.h"
#include "EquipmentWorkshopCatalogBridge.h"
#include "F8FlagCatalog.h"
#include "F8RuntimeCore.h"
#include "EquipmentWorkshopRuntime.h"
#include "MinHookBatchCoordinator.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <MinHook.h>
#endif
#include <atomic>
#include <cstring>
#include <mutex>
#include <cstdio>
#include <vector>
#include <climits>
#include <intrin.h>
#include "SharedDamageRuntime.h"
#include "SharedTurnRuntime.h"
#include "SharedElementRuntime.h"
#include "SharedActionRuntime.h"
#include "SharedCombatRuntime.h"
#include "RonsoCommandCosts.h"
#include "SharedBattleRuntime.h"
#include "VanguardActionCore.h"
namespace FfxHooks::Vanguard {
namespace {
using Byte=unsigned char;
std::uintptr_t module=0;
std::atomic<bool> running{false};
std::atomic<DWORD> ownerThread{0};
std::recursive_mutex mappingMutex;
std::array<bool,FeatureCount> flags{};
std::array<bool,FeatureCount> startupFlags{};
std::atomic<bool> startupCaptured{false};
bool installed=false;
LogFn logger=nullptr;
enum Hook : unsigned { PercentHook, FormulaHook, CriticalHook, ElementHook, CostHook, StatusHook, QuarterHook, AccuracyHook,
    AftermathHook, HpHook, FinishHook, CommitCostHook, AvailabilityHook, SelectedCostHook, CastMenuHook, CastListHook, GaugeHook,
    SwapHook, EscapeHook, SchedulerHook, HookCount };
void* originals[HookCount]{};
struct Span { unsigned rva; Byte bytes[16];Byte relocation=255,length=16; };
constexpr Span spans[]={
    {0x3892A0,{0x55,0x8B,0xEC,0x8B,0x45,0x10,0x8B,0x4D,0x08,0x0F,0xB7,0x40,0x20,0x56,0x83,0xE0}},
    {0x389CB0,{0x55,0x8B,0xEC,0x83,0xEC,0x0C,0x53,0x8B,0x5D,0x08,0x56,0x0F,0xB6,0x43,0x0C,0x57}},
    {0x389750,{0x55,0x8B,0xEC,0x57,0x8B,0x7D,0x10,0xF6,0x47,0x20,0x04,0x0F,0x84,0x8D,0x00,0x00}},
    {0x38A420,{0x55,0x8B,0xEC,0x83,0xEC,0x20,0x57,0x8B,0x7D,0x10,0x85,0xFF,0x75,0x08,0x8B,0x45}},
    {0x38D030,{0x55,0x8B,0xEC,0x51,0x57,0x8B,0x7D,0x0C,0x85,0xFF,0x0F,0x84,0xAC,0x00,0x00,0x00}},
    {0x38AEC0,{0x55,0x8B,0xEC,0x83,0xEC,0x30,0x8B,0x45,0x0C,0x53,0x56,0x57,0x8D,0xB8,0xDE,0x05}},
    {0x38C5F0,{0x55,0x8B,0xEC,0x56,0x8B,0x75,0x08,0xF6,0x86,0x16,0x06,0x00,0x00,0x40,0x74,0x1F}},
    {0x38A950,{0x55,0x8B,0xEC,0x8B,0x45,0x10,0x56,0x8B,0x70,0x1C,0xF7,0xC6,0x00,0x00,0x80,0x00}},
    {0x38F0B0,{0x55,0x8B,0xEC,0x83,0xEC,0x44,0x53,0x56,0x57,0xFF,0x75,0x08,0x33,0xC0,0x33,0xC9}},
    {0x38E2F0,{0x55,0x8B,0xEC,0x53,0x56,0x57,0xE8,0x85,0x34,0xFF,0xFF,0x8B,0x75,0x0C,0x8B,0x5D}},
    {0x3B0870,{0x55,0x8B,0xEC,0x83,0xEC,0x14,0x0F,0xBE,0x05,0xE1,0xBD,0x12,0x01,0x89,0x45,0xF4},9},
    {0x38ABE0,{0x55,0x8B,0xEC},255,3},
    {0x39AD40,{0x55,0x8B,0xEC,0x53,0x56,0x57,0xFF,0x75,0x08,0x33,0xF6,0xE8,0xE0,0x92,0xFF,0xFF}},
    {0x3B03F0,{0x55,0x8B,0xEC,0x51,0x8B,0x4D,0x14,0x53,0x8B,0x5D,0x0C,0x56,0x8B,0x75,0x10,0x33}},
    {0x4997E0,{0x55,0x8B,0xEC,0x8B,0x4D,0x10,0x56,0x8B,0x75,0x08,0x69,0xF6,0xF0,0x00,0x00,0x00}},
    {0x49B510,{0x55,0x8B,0xEC,0x51,0x56,0x8B,0x75,0x0C,0x8D,0x45,0xFC,0x50,0x56,0xFF,0x75,0x08}},
    {0x4953F0,{0x55,0x8B,0xEC,0x83,0xEC,0x1C,0x66,0xA1,0xB0,0x0A,0x5D,0x02,0x8B,0x4D,0x10,0x53},8},
    {0x3ADAF0,{0x55,0x8B,0xEC,0x51,0x53,0x56,0x57,0xFF,0x75,0x08,0xE8,0x31,0x65,0xFE,0xFF,0x8B}},
    {0x38DF00,{0x55,0x8B,0xEC,0x83,0x7D,0x10,0,0x8B,0x45,0x0C,0x74,0x10,0xC6,0x80,0xCD,0x0D}},
    {0x391000,{0x55,0x8B,0xEC,0x51,0x80,0x3D,0xE1,0xA8,0x12,0x01,0,0x0F,0x85,0xA7,0x01,0},6}
};
static_assert(std::size(spans)==HookCount&&HookCount<=MinHookBatch::kMaximumTargets,
              "Vanguard must fit one completely validated atomic hook batch");
bool Copy(void* out,const void* in,std::size_t count) noexcept {
    __try {std::memcpy(out,in,count);return true;} __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class T> T Read(const void* address,T fallback={}) noexcept {T value{};return Copy(&value,address,sizeof(value))?value:fallback;}
bool On(Feature feature) noexcept {return flags[static_cast<unsigned>(feature)];}
bool Enter() noexcept {
    if(!running.load())return false;
    DWORD expected=0;const DWORD thread=GetCurrentThreadId();ownerThread.compare_exchange_strong(expected,thread);
    return ownerThread.load()==thread;
}
Byte* Actor(unsigned id) noexcept {
    if(id>=31||!module)return nullptr;
    const auto address=Read<std::uint32_t>(reinterpret_cast<void*>(module+0xD334CC));
    if(address<0x10000||address>UINT32_MAX-31*0xF90u)return nullptr;
    auto* actor=reinterpret_cast<Byte*>(address+id*0xF90u);
    return Read<Byte>(actor+0xC,255)==id?actor:nullptr;
}
unsigned ActorId(const Byte* actor) noexcept {
    const unsigned id=actor?Read<Byte>(actor+0xC,255):255;
    return id<31&&Actor(id)==actor?id:31;
}
bool Profile(std::uintptr_t base) noexcept {
    Byte header[0x1000]{};F8Runtime::ExecutableIdentity identity{};
    if(!Copy(header,reinterpret_cast<void*>(base),sizeof(header))||
       F8Runtime::ParseExecutableIdentity(header,sizeof(header),&identity)!=F8Runtime::ProfileResult::Supported||
       !F8Runtime::IsSupportedExecutable(identity))return false;
    for(const auto& span:spans){Byte bytes[16]{},expected[16]{};std::memcpy(expected,span.bytes,16);
        if(span.relocation!=255){std::uint32_t operand=0;std::memcpy(&operand,expected+span.relocation,4);
            operand+=static_cast<std::uint32_t>(base-0x400000u);std::memcpy(expected+span.relocation,&operand,4);}
        if(!Copy(bytes,reinterpret_cast<void*>(base+span.rva),span.length)||
           (std::memcmp(bytes,expected,span.length)&&
            !(span.rva==SharedElement::kRva&&SharedElement::MatchesOwned(base,bytes,span.length))&&
            !(span.length==16&&SharedAction::MatchesOwned(base,span.rva,bytes))&&
            !SharedCombat::MatchesOwned(base,span.rva,bytes,span.length)))return false;}
    return true;
}
bool Named(const std::vector<Byte>& bytes,unsigned id,unsigned effect) {
    const std::size_t pool=20+Word(bytes.data()+14),offset=Word(bytes.data()+20+id*108);
    if(pool>=bytes.size()||offset>=bytes.size()-pool)return false;
    const Byte* name=bytes.data()+pool+offset;const auto available=bytes.size()-pool-offset;
    const char* label=Abilities[effect].label;std::size_t i=0;
    for(;label[i]&&i<available;++i){const Byte expected=label[i]==' '?58:label[i]=='\''?65:label[i]=='-'?71:static_cast<Byte>(label[i]+15);
        if(name[i]!=expected)break;}
    if(!label[i]&&i<available&&!name[i])return true;
    // Existing Asian authoring uses the stable default identity as numeric text.
    char numeric[16]{};std::snprintf(numeric,sizeof(numeric),"%u",Abilities[effect].id);
    const auto length=std::strlen(numeric);
    return length<available&&!std::memcmp(name,numeric,length)&&name[length]==0;
}
bool MappingSnapshot(MappingState& out,std::vector<Byte>& bytes,const Mapping* overrideIds=nullptr) {
    out={};out.codes.fill(MappingCode::NoKernel);out.ids=DefaultMapping();
    for(unsigned i=0;i<AbilityCount;++i){char key[96]{};std::snprintf(key,sizeof(key),"vanguard_ids.%s",Abilities[i].key);
        const auto configured=Config::ReadIntExact(key,135,4095);
        if(configured.state==Config::IntReadState::Valid)out.ids[i]=static_cast<unsigned>(configured.value);
        else if(configured.state==Config::IntReadState::Invalid)out.ids[i]=0;}
    if(overrideIds)out.ids=*overrideIds;
    if(!module)return false;
    const auto address=Read<std::uint32_t>(reinterpret_cast<void*>(module+0xD2A944));
    const auto size=Read<std::uint16_t>(reinterpret_cast<void*>(module+0xD2A970));
    if(address<0x10000||size<20||address>UINT32_MAX-size)return false;
    bytes.resize(size);if(!Copy(bytes.data(),reinterpret_cast<void*>(address),size))return false;
    const auto* data=bytes.data();if(!ValidAbilityTable(data,bytes.size()))return false;
    const unsigned last=Word(data+10);
    out.stamp=1469598103934665603ull;
    for(Byte value:bytes){out.stamp^=value;out.stamp*=1099511628211ull;}
    for(unsigned i=0;i<AbilityCount;++i){
        const auto id=out.ids[i];out.stamp^=id;out.stamp*=1099511628211ull;
        if(!AutoAbilitySlots::Vanguard(id)||id>last){out.codes[i]=MappingCode::InvalidId;continue;}
        bool duplicate=false;for(unsigned j=0;j<AbilityCount;++j)if(i!=j&&out.ids[j]==id)duplicate=true;
        if(duplicate){out.codes[i]=MappingCode::Duplicate;continue;}
        bool payload=false;for(unsigned n=16;n<108;++n)if(data[20+id*108+n])payload=true;
        out.codes[i]=payload?MappingCode::NativePayload:!Named(bytes,id,i)?MappingCode::IdentityMismatch:MappingCode::Valid;
    }
    return true;
}
using Effects=std::array<bool,AbilityCount>;
Effects Equipped(const Byte* actor,const MappingState& mapping,bool requirePassive=true) {
    Effects result{};const auto id=ActorId(actor);
    if(id>=18||id==7||Read<std::uint16_t>(actor+0xE,0xFFFF)!=id)return result;
    for(unsigned kind=0;kind<2;++kind){const auto slot=Read<Byte>(actor+0x592+kind,255);if(slot>=200)continue;
        const auto* native=reinterpret_cast<Byte*>(module+0xD30F2C+22*slot);Byte record[22]{};
        if(!Copy(record,native,22)||!record[2]||record[4]!=id||record[5]!=kind||record[6]!=id||record[11]>4)continue;
        std::uint16_t words[5]={255,255,255,255,255};
        for(unsigned n=0;n<record[11];++n)words[n]=static_cast<std::uint16_t>(Word(record+14+2*n));
        workshop::Piece piece{};
        if(EquipmentWorkshop::ReadPresentation(native,piece)&&piece.fifthUnlocked)words[4]=piece.fifth;
        for(unsigned n=0;n<AbilityCount;++n)if(Abilities[n].kind==kind&&mapping.codes[n]==MappingCode::Valid&&(!requirePassive||On(Abilities[n].feature)))
            for(auto word:words)if(word==0x8000+mapping.ids[n])result[n]=true;
    }
    return result;
}
void ReadEffects(const Byte* a,const Byte* b,Effects& first,Effects& second,std::uint64_t* proof=nullptr) noexcept {
    if(proof)*proof=0;
    try{MappingState mapping{};std::vector<Byte> bytes;
        bool valid=false;{std::lock_guard<std::recursive_mutex> lock(mappingMutex);valid=MappingSnapshot(mapping,bytes);}
        // Workshop may ask for this mapping while holding its own lock. Never
        // hold mappingMutex while calling back into Workshop presentation.
        if(valid){first=Equipped(a,mapping);second=Equipped(b,mapping);if(proof)*proof=mapping.stamp;}
    }catch(...){first={};second={};running=false;if(logger)logger("[ffx-hooks] Vanguard: equipment snapshot failed; native behavior restored\n");}
}
#include "VanguardEnergyActions.inl"
unsigned FormulaId(const Byte* source,const Byte* command) noexcept {
    return (Read<std::uint32_t>(command+0x1C)&0x40000)?Read<Byte>(source+0x5C1):command[0x28];
}
unsigned Attribute(unsigned formula) noexcept {
    switch(formula){case 1:case 2:case 14:case 17:case 18:case 19:return 1;
        case 3:case 4:case 7:case 15:case 20:return 2;default:return 0;}
}
using PercentFn=int(__cdecl*)(unsigned,unsigned,const Byte*,int);
bool HitRates(unsigned& single,unsigned& multi) noexcept {
    single=multi=100;
    const auto a=Config::ReadIntExact("vanguard_balance.single_hit_percent",0,400);
    const auto b=Config::ReadIntExact("vanguard_balance.multi_hit_percent",0,400);
    if(a.state==Config::IntReadState::Invalid||b.state==Config::IntReadState::Invalid)return false;
    if(a.state==Config::IntReadState::Valid)single=static_cast<unsigned>(a.value);
    if(b.state==Config::IntReadState::Valid)multi=static_cast<unsigned>(b.value);
    return true;
}
int __cdecl PercentShim(unsigned sourceId,unsigned targetId,const Byte* command,int amount) {
    const auto original=reinterpret_cast<PercentFn>(originals[PercentHook]);
    if(!Enter())return original(sourceId,targetId,command,amount);
    Byte row[46]{};auto* source=Actor(sourceId);auto* target=Actor(targetId);
    if(!source||!target||!Copy(row,command,sizeof(row)))return original(sourceId,targetId,command,amount);
    Effects first{},second{};std::uint64_t proof=0;ReadEffects(source,target,first,second,&proof);
    if(!running.load())return original(sourceId,targetId,command,amount);
    Byte sp[4]{},tp[4]{};
    if(!Copy(sp,reinterpret_cast<void*>(module+0x1F11240+4*sourceId),4)||!Copy(tp,reinterpret_cast<void*>(module+0x1F11240+4*targetId),4))return original(sourceId,targetId,command,amount);
    const unsigned formula=FormulaId(source,row),kind=row[0x20]&3;
    int result=amount;
    if(On(Feature::UniversalStats)||On(Feature::EhpDefense)||On(Feature::UnhinderedHealing)){
        const unsigned offense=On(Feature::UniversalStats)?Attribute(formula):kind;
        const unsigned defense=On(Feature::UniversalStats)?(formula==1?1u:formula==3?2u:0u):kind;
        if(offense==1||offense==2)result=Scale(result,100u+sp[offense-1],100);
        if((defense==1||defense==2)&&!(On(Feature::UnhinderedHealing)&&amount<0)){
            const unsigned bonus=tp[defense+1];
            result=On(Feature::EhpDefense)?Scale(result,100,100+bonus):Scale(result,bonus>=100?0:100-bonus,100);
        }
    }else result=original(sourceId,targetId,command,amount);
    const bool elemental=(row[0x2D]|((Read<std::uint32_t>(row+0x1C)&0x40000)?Read<Byte>(source+0x5D9):0))!=0;
    EnergyValues energy{Read<Byte>(source+0x5BC),Read<Byte>(source+0x5BD),Read<Byte>(target+0x5BC),Read<Byte>(target+0x5BD),
                        first[1],first[2],second[11],second[12]};
    ResolveEnergyAction(sourceId,targetId,proof,energy);
    result=EnergyAttack(result,elemental,energy.sourceCharge,energy.sourceMaximum,energy.boost,energy.burst);
    result=Trade(result,kind,second[6],second[7]);
    const bool fraction=formula==5||formula==8||(formula>=10&&formula<=13);
    if(On(Feature::HitNormalization)&&result>0&&!fraction&&(row[0x23]&1)&&!(row[0x20]&0x10)){
        unsigned single=100,multi=100;
        // The copied percentage fragment has no unambiguous formula. Explicit
        // rates are supported; both defaults are neutral100, not an inferred rule.
        if(HitRates(single,multi))result=Scale(result,row[0x2B]>1?multi:single,100);
    }
    return EnergyDefense(result,energy.targetCharge,energy.targetMaximum,energy.wall,energy.barrier,fraction);
}
using FormulaFn=int(__cdecl*)(const Byte*,const Byte*,const Byte*,int,int,unsigned,unsigned,int,int*,int*,int);
unsigned Sample(const Byte* actor,unsigned channel) {
    const auto stream=reinterpret_cast<unsigned(__cdecl*)(unsigned,unsigned)>(module+0x38D2D0)(actor[0xC],channel);
    return reinterpret_cast<unsigned(__cdecl*)(unsigned)>(module+0x398900)(stream);
}
int MagicValue(const Byte* source,const Byte* target,const Byte* command,int formula,int power,unsigned status) {
    const int mp=Read<int>(source+0x5D4);
    const auto setting=Config::ReadIntExact("vanguard_balance.mp_zero_mag_cap",0,1);
    const auto magic=std::int64_t(MagicFromMp(source[0x5AA],mp>0?static_cast<unsigned>(mp):0u,(source[0x640]&4)!=0,setting.state==Config::IntReadState::Valid&&setting.value==1));
    const auto total=magic+source[0x660];std::int64_t result=0;
    if(formula==3||formula==4){result=std::int64_t(power)*(power+total*total/6)/4;
        if(formula==3){const int defense=(status&0x80)?0:target[0x5AB];const int curve=730-(51*defense-defense*defense/11)/10;
            result=(15-target[0x660])*(result*curve/730)/15;}}
    else if(formula==7)result=std::int64_t(power)*((power+total)/2);
    else if(formula==15)result=std::int64_t(power)*(total*total*total/32+30)/16;
    else result=std::int64_t(power)*(magic*magic*magic/32+30)/16;
    if(formula!=20&&(command[0x20]&0x10)&&!(status&2))result=-result;
    return Saturate(result);
}
int __cdecl FormulaShim(const Byte* source,const Byte* target,const Byte* command,int formula,int power,unsigned status,unsigned channel,int vary,int* defense,int* magicDefense,int fallback) {
    const auto original=reinterpret_cast<FormulaFn>(originals[FormulaHook]);
    if(!Enter()||ActorId(source)>=31||ActorId(target)>=31||!command)return original(source,target,command,formula,power,status,channel,vary,defense,magicDefense,fallback);
    const bool mp=On(Feature::MpScaling)&&Attribute(static_cast<unsigned>(formula))==2&&power>=0&&power<=65535;
    const unsigned broken=formula==1?0x40u:formula==3?0x80u:0u;
    const bool breaks=On(Feature::AdditiveBreaks)&&broken&&(status&broken)&&(command[0x23]&1)&&channel==1;
    if(!mp&&!breaks)return original(source,target,command,formula,power,status,channel,vary,defense,magicDefense,fallback);
    const unsigned effective=breaks?status&~broken:status;
    int result=mp?MagicValue(source,target,command,formula,power,effective):original(source,target,command,formula,power,effective,channel,0,defense,magicDefense,fallback);
    if(mp){if(defense)*defense=(effective&0x40)?0:target[0x5A9];if(magicDefense)*magicDefense=(effective&0x80)?0:target[0x5AB];}
    if(breaks){const int raw=mp?MagicValue(source,target,command,formula,power,status):original(source,target,command,formula,power,status,channel,0,nullptr,nullptr,fallback);result=BreakDamage(result,raw,true);}
    const unsigned variance=vary?(Sample(source,0)&31u)+240u:256u;
    return formula==20?result:Scale(result,variance,256);
}
using CriticalFn=int(__cdecl*)(const Byte*,const Byte*,const Byte*,unsigned*,int);
int __cdecl CriticalShim(const Byte* source,const Byte* target,const Byte* command,unsigned* output,int amount) {
    const auto original=reinterpret_cast<CriticalFn>(originals[CriticalHook]);
    if(!Enter()||ActorId(source)>=31||ActorId(target)>=31||!command||!output)return original(source,target,command,output,amount);
    Effects first{},second{};ReadEffects(source,target,first,second);
    if(!running.load())return original(source,target,command,output,amount);
    if(!(first[0]||second[0]||first[8]||second[8])||!(command[0x20]&4))return original(source,target,command,output,amount);
    const unsigned roll=Sample(source,0)%101;
    const int baseChance=((command[0x20]&8)?source[0x5D8]:command[0x27])+source[0x5AD]+target[0x663]+source[0x662]-target[0x5AD];
    const unsigned chance=CriticalChance(baseChance,first[0],second[0],first[8],second[8]);
    const bool forced=(source[0x640]&0x10)||Read<Byte>(reinterpret_cast<void*>(module+0xD2A90D));
    if(forced||chance>=100||roll<chance){*output|=0x100;return Scale(amount,2,1);}return amount;
}
using ElementFn=int(__cdecl*)(const Byte*,const Byte*,unsigned,int);
int __cdecl ElementShim(const Byte* target,const Byte* command,unsigned mask,int amount) {
    const int result=SharedElement::Original()(target,command,mask,amount);
    if(!Enter()||!On(Feature::OppositeWeakness)||ActorId(target)>=31||result<=0)return result;
    const unsigned weak=Read<Byte>(target+0x5DD);
    // Do not replace native absorption/null or compound an existing direct weakness.
    return !(mask&weak)&&(mask&OppositeMask(weak))?Scale(result,125,100):result;
}
int AdjustCastMpCost(unsigned,const Byte*,int) noexcept;
#include "VanguardStatusCost.inl"
#include "VanguardOverdrive.inl"
#include "VanguardOverdriveUi.inl"
#include "VanguardCasting.inl"
#include "VanguardFormation.inl"
#include "VanguardAccuracy.inl"
std::uint32_t lastRegenerationEdge=0;
void RegenerateMp(unsigned slot,void* native,std::uint32_t edge) noexcept {
    if(!Enter()||!On(Feature::MpRegen)||!edge||edge==lastRegenerationEdge||slot>=18||slot==7||Actor(slot)!=native)return;
    lastRegenerationEdge=edge;auto* actor=static_cast<Byte*>(native);
    const int hp=Read<int>(actor+0x5D0),mp=Read<int>(actor+0x5D4),maximum=Read<int>(actor+0x598);
    if(!Read<Byte>(actor+0xDC8)||hp<=0||(Read<std::uint16_t>(actor+0x606)&1)||mp<0||maximum<=0||mp>=maximum)return;
    Effects current{},unused{};ReadEffects(actor,nullptr,current,unused);
    if(!running.load()||!current[9])return;
    const auto restored=(std::min)(std::int64_t(maximum),std::int64_t(mp)+std::int64_t(maximum)*2/100);
    // Only MP changes: regeneration is not a second turn or an OD healing event.
    (void)InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(actor+0x5D4),static_cast<LONG>(restored),static_cast<LONG>(mp));
}
const SharedTurn::Observer regenerationObserver{RegenerateMp};
#include "VanguardActions.inl"
const SharedAction::Legacy sharedActionCallbacks{AftermathShim,FinishShim};
const SharedCombat::Callbacks sharedCombatOperations{MpCostShim,CriticalShim,HpShim};
bool IgnoreProtection(const void* command,const void* info,int amount) noexcept {
    // Positive Zombie damage is not healing. No Reflect, affinity, status or
    // protection flag is removed: only an already negative heal bypasses guard.
    return amount<0&&command&&info&&Enter()&&On(Feature::UnhinderedHealing);
}
const SharedDamage::CombatCallbacks sharedCombatCallbacks{IgnoreProtection,BeforeActionHit};
void ReadConfiguredFlags(std::array<bool,FeatureCount>& out){
    for(unsigned i=0;i<FeatureCount;++i){char key[96]{};std::snprintf(key,sizeof(key),"vanguard.%s",Features[i].key);
        const auto* spec=FindF8Flag(key);out[i]=spec&&ResolveF8Flag(*spec).value;}
}
void PublishStatus(const MappingState* mapping,F8RuntimeAvailability failure=F8RuntimeAvailability::NotApplicable){
    for(unsigned index=0;index<FeatureCount;++index){
        if(!HasNativeConsumer(index))continue;
        char key[96]{};std::snprintf(key,sizeof(key),"vanguard.%s",Features[index].key);
        if(failure!=F8RuntimeAvailability::NotApplicable){PublishF8RuntimeStatus(key,failure,false,false);continue;}
        bool available=!flags[index]||running.load();
        for(unsigned effect=0;effect<AbilityCount;++effect)if(static_cast<unsigned>(Abilities[effect].feature)==index&&flags[index])
            available=available&&mapping&&mapping->codes[effect]==MappingCode::Valid;
        PublishF8RuntimeStatus(key,available?F8RuntimeAvailability::Available:F8RuntimeAvailability::ProducerUnavailable,
                               true,available&&running.load()&&flags[index]);
    }
}
}
static const UiProvider nativeUiProvider{ReadMapping,SaveMapping,RefreshUiStatus,ReadBindings,SaveBinding};
void CaptureStartup(){
    if(startupCaptured.load())return;
    ReadConfiguredFlags(startupFlags);
    RonsoPool::CommandCosts::Request(startupFlags[static_cast<unsigned>(Feature::Efficiency)]||startupFlags[static_cast<unsigned>(Feature::PartialOverdriveCosts)]||startupFlags[static_cast<unsigned>(Feature::EquipmentCommands)]);
    startupCaptured.store(true);
}
void RefreshUiStatus() noexcept {
    try{MappingState mapping{};std::vector<Byte> bytes;bool valid=false;
        {std::lock_guard<std::recursive_mutex> lock(mappingMutex);valid=MappingSnapshot(mapping,bytes);}
        PublishStatus(valid?&mapping:nullptr);
    }catch(...){try{PublishStatus(nullptr,F8RuntimeAvailability::ProducerUnavailable);}catch(...){}}
}
static bool WorkshopCatalog(workshop::Catalog& out) noexcept {
    out={};MappingState current{};if(!ReadMapping(current)||!current.stamp)return false;
    out.proof=current.stamp;
    for(unsigned i=0;i<AbilityCount;++i)if(current.codes[i]==MappingCode::Valid){
        // Explicit mod recipe: use the existing configurable mod quantity of
        // Ability Spheres. It is not advertised as a native Customize recipe.
        out.entries[i]={static_cast<std::uint16_t>(0x8000+current.ids[i]),
            static_cast<std::uint16_t>(Abilities[i].kind+1),73,0};
    }
    return workshop::ValidCatalog(out);
}
static const char* WorkshopAbilityName(unsigned word) noexcept {
    MappingState current{};if(!ReadMapping(current))return nullptr;
    for(unsigned i=0;i<AbilityCount;++i)if(current.codes[i]==MappingCode::Valid&&word==0x8000+current.ids[i])return Abilities[i].label;
    return nullptr;
}
static const EquipmentWorkshop::CatalogBridge::Provider nativeCatalogProvider{WorkshopCatalog,WorkshopAbilityName};
bool Start(std::uintptr_t base,bool validateOnly,LogFn log) {
    if(!Coexistence::FeatureAllowed("vanguard.enabled")){PublishStatus(nullptr,F8RuntimeAvailability::PeerOwned);return false;}
    if(validateOnly)return false;
    if(installed)return running.load();
    if(!Profile(base)){if(log)log("[ffx-hooks] Vanguard rejected: profile/signature mismatch\n");PublishStatus(nullptr,F8RuntimeAvailability::SignatureMismatch);return false;}
    module=base;logger=log;
    if(!RegisterUi(&nativeUiProvider))return false;
    if(!EquipmentWorkshop::CatalogBridge::Register(&nativeCatalogProvider)){
        UnregisterUi(&nativeUiProvider);return false;
    }
    if(startupCaptured.load())flags=startupFlags;else ReadConfiguredFlags(flags);
    bool requested=false;for(bool value:flags)requested|=value;
    if(!requested){RefreshUiStatus();if(log)log("[ffx-hooks] Vanguard: all gates OFF\n");return false;}
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if(MinHookBatch::EnsureProcessInitialized()!=MinHookBatch::InitializationResult::Ready){if(log)log("[ffx-hooks] Vanguard rejected: coordinator unavailable\n");return false;}
    HMODULE pin=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Start),&pin))return false;
    if(!CommitCostProfile(base))return false;
    if((On(Feature::Efficiency)||On(Feature::PartialOverdriveCosts))&&!GaugeDrawProfile(base))return false;
    if((On(Feature::TurnCostSwitch)||On(Feature::AutoReinforce))&&!FormationProfile(base))return false;
    if((On(Feature::Efficiency)||On(Feature::PartialOverdriveCosts))&&!RonsoPool::CommandCosts::Register(&overdriveProvider))return false;
    if(On(Feature::MpRegen)&&(!SharedTurn::Start(base)||!SharedTurn::Register(SharedTurn::Consumer::Vanguard,&regenerationObserver)))return false;
    if(On(Feature::FollowUp)&&!FollowProfile(base))return false;
    if((On(Feature::UnhinderedHealing)||On(Feature::Vampirism)||On(Feature::TurnEndBuffs)||On(Feature::FollowUp))&&
       (!SharedDamage::Start(base)||!SharedDamage::RegisterCombat(&sharedCombatCallbacks)))return false;
    if((On(Feature::Vampirism)||On(Feature::TurnEndBuffs)||On(Feature::FollowUp)||On(Feature::EquipmentCommands)||On(Feature::Quickcast)||On(Feature::WhiteMagic)||On(Feature::TurnCostSwitch)||On(Feature::AutoReinforce)||EnergyRequested())&&
       !SharedBattleRuntime::RegisterActionObserver(&actionObserver))return false;
    if(!SharedElement::Start(base)||!SharedElement::RegisterLegacy(&ElementShim))return false;
    if(!SharedAction::Start(base))return false;
    originals[AftermathHook]=reinterpret_cast<void*>(SharedAction::OriginalResult());
    originals[FinishHook]=reinterpret_cast<void*>(SharedAction::OriginalFinish());
    if(!SharedAction::RegisterLegacy(&sharedActionCallbacks))return false;
    if(!SharedCombat::Start(base))return false;
    originals[CostHook]=reinterpret_cast<void*>(SharedCombat::OriginalMp());
    originals[CriticalHook]=reinterpret_cast<void*>(SharedCombat::OriginalCritical());
    originals[HpHook]=reinterpret_cast<void*>(SharedCombat::OriginalHp());
    if(!SharedCombat::Register(SharedCombat::vanguard,&sharedCombatOperations))return false;
    void* replacements[]={reinterpret_cast<void*>(&PercentShim),reinterpret_cast<void*>(&FormulaShim),reinterpret_cast<void*>(&CriticalShim),reinterpret_cast<void*>(&ElementShim),reinterpret_cast<void*>(&MpCostShim),reinterpret_cast<void*>(&StatusShim),reinterpret_cast<void*>(&QuarterShim),reinterpret_cast<void*>(&AccuracyShim),
        reinterpret_cast<void*>(&AftermathShim),reinterpret_cast<void*>(&HpShim),reinterpret_cast<void*>(&FinishShim),reinterpret_cast<void*>(&CommitCostShim),reinterpret_cast<void*>(&AvailabilityShim),reinterpret_cast<void*>(&SelectedCostShim),reinterpret_cast<void*>(&CastMenuShim),reinterpret_cast<void*>(&CastListShim),reinterpret_cast<void*>(&GaugeShim),
        reinterpret_cast<void*>(&FormationSwapShim),reinterpret_cast<void*>(&FormationEscapeShim),reinterpret_cast<void*>(&FormationSchedulerShim)};
    std::uintptr_t targets[HookCount]{};unsigned count=0;
    for(unsigned index=0;index<HookCount;++index){
        if(index==ElementHook||index==AftermathHook||index==FinishHook||index==CostHook||index==CriticalHook||index==HpHook)continue;
        const auto target=base+spans[index].rva;
        const auto status=MH_CreateHook(reinterpret_cast<void*>(target),replacements[index],&originals[index]);
        if(status!=MH_OK){char message[128]{};std::snprintf(message,sizeof(message),"Vanguard hook %u creation status %d\n",index,static_cast<int>(status));if(log)log(message);break;}
        targets[count++]=target;
    }
    if(count!=HookCount-6u){while(count)MH_RemoveHook(reinterpret_cast<void*>(targets[--count]));return false;}
    const auto result=MinHookBatch::EnableBatch(&MinHookBatch::ProcessCoordinator(),MinHookBatch::RuntimeBatchIo(),MinHookBatch::Owner::Vanguard,targets,count);
    if(result.result!=MinHookBatch::BatchResult::Applied){char message[128]{};std::snprintf(message,sizeof(message),"Vanguard batch result %u stage %u\n",static_cast<unsigned>(result.result),static_cast<unsigned>(result.primaryFailure));if(log)log(message);return false;}
    installed=true;running=true;RefreshUiStatus();if(logger)logger("[ffx-hooks] Vanguard: native formula and equipped-ability producers admitted\n");return true;
#else
    return false;
#endif
}
void RequestStop() noexcept {running=false;SharedCombat::Unregister(SharedCombat::vanguard,&sharedCombatOperations);SharedAction::UnregisterLegacy(&sharedActionCallbacks);SharedElement::UnregisterLegacy(&ElementShim);RonsoPool::CommandCosts::Unregister(&overdriveProvider);SharedTurn::Unregister(SharedTurn::Consumer::Vanguard,&regenerationObserver);SharedBattleRuntime::UnregisterActionObserver(&actionObserver);SharedDamage::UnregisterCombat(&sharedCombatCallbacks);UnregisterUi(&nativeUiProvider);EquipmentWorkshop::CatalogBridge::Unregister(&nativeCatalogProvider);}
bool Active() noexcept {return running.load();}
bool ReadBindings(BindingState& out) noexcept {try{return BindingsSnapshot(out);}catch(...){out={};return false;}}
bool SaveBinding(unsigned effect,unsigned packed,std::uint64_t expectedStamp) noexcept {
    if(effect>=AbilityCount||packed>0x100FFFF||!module||!expectedStamp||Read<Byte>(reinterpret_cast<void*>(module+0xD2A8E0),1))return false;
    const auto owner=ownerThread.load();if(owner&&owner!=GetCurrentThreadId())return false;
    try{
        std::lock_guard<std::recursive_mutex> lock(mappingMutex);BindingState current{},candidate{};
        if(!BindingsSnapshot(current)||current.stamp!=expectedStamp||!BindingsSnapshot(candidate,nullptr,static_cast<int>(effect),packed))return false;
        if(candidate.entries[effect].code!=BindingCode::Valid&&candidate.entries[effect].code!=BindingCode::Disabled)return false;
        char key[96]{};std::snprintf(key,sizeof(key),"vanguard_commands.%s",Abilities[effect].key);
        if(!Config::SetInt(key,static_cast<int>(packed)))return false;
        const auto written=Config::ReadIntExact(key,0,0x100FFFF);return written.state==Config::IntReadState::Valid&&written.value==static_cast<int>(packed);
    }catch(...){return false;}
}

bool ReadMapping(MappingState& out) noexcept {
    try{std::lock_guard<std::recursive_mutex> lock(mappingMutex);std::vector<Byte> bytes;return MappingSnapshot(out,bytes);}
    catch(...){out={};out.codes.fill(MappingCode::NoKernel);return false;}
}
bool SaveMapping(unsigned effect,unsigned id) noexcept {
    if(effect>=AbilityCount||!AutoAbilitySlots::Vanguard(id)||!module||Read<Byte>(reinterpret_cast<void*>(module+0xD2A8E0),1))return false;
    const auto thread=ownerThread.load();if(thread&&thread!=GetCurrentThreadId())return false;
    try{std::lock_guard<std::recursive_mutex> lock(mappingMutex);MappingState current{};std::vector<Byte> bytes;
        if(!MappingSnapshot(current,bytes))return false;auto ids=current.ids;ids[effect]=id;
        MappingState candidate{};if(!MappingSnapshot(candidate,bytes,&ids)||candidate.codes[effect]!=MappingCode::Valid)return false;
        char key[96]{};std::snprintf(key,sizeof(key),"vanguard_ids.%s",Abilities[effect].key);
        if(!Config::SetInt(key,static_cast<int>(id)))return false;
        const auto readback=Config::ReadIntExact(key,135,4095);return readback.state==Config::IntReadState::Valid&&readback.value==static_cast<int>(id);
    }catch(...){return false;}
}
const char* MappingDetail(MappingCode code) noexcept {
    switch(code){case MappingCode::Valid:return "Valid";case MappingCode::NoKernel:return "Kernel unavailable";
    case MappingCode::InvalidId:return "Missing/reserved ID";case MappingCode::Duplicate:return "Duplicate ID";
    case MappingCode::NativePayload:return "Native effect payload";case MappingCode::IdentityMismatch:return "Different ability identity";}
    return "Invalid";
}
}
