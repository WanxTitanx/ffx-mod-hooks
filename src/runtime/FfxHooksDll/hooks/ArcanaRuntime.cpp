#include "../shared/ExecutableProfile.h"
#include "ArcanaRuntime.h"
#include "ArcanaElemental.h"
#include "ArcanaAcquisition.h"
#include "ArcanaCatalog.generated.h"
#include "ArcanaRuntimeEvidence.generated.h"
#include "NativeSaveEvents.h"
#include "NativeGameplayEvents.h"
#include "NativeUiHookSupport.h"
#include "SharedClampRuntime.h"
#include "EquipmentWorkshopStore.h"
#include "RonsoPoolSave.h"
#include "RonsoPoolStore.h"
#include <algorithm>
#include <atomic>
#include <intrin.h>
#include <memory>
#include <mutex>
#include <vector>
#include <cstdio>

namespace FfxHooks::Arcana::Runtime {
namespace {
constexpr std::uint32_t saveRva=(::FfxHooks::ExecutableProfile::Rva<0xD2CA90>()),playerRva=(::FfxHooks::ExecutableProfile::Rva<0xD3205C>()),playerFile=0x560C,actorTableRva=(::FfxHooks::ExecutableProfile::Rva<0xD334CC>());
using SaveImage=RonsoPool::SaveImage;
std::uintptr_t base=0;
Settings settings{};
std::atomic<bool> primed{false},enabled{false},ready{false},started{false},loading{false};
std::atomic<bool> bound{false};
std::atomic<unsigned> ownerThread{0},appliedMask{0};
std::atomic<Code> code{Code::Off};
std::atomic<std::uint64_t> battleGeneration{0};
std::atomic<bool> battleInSession{false};
std::recursive_mutex mutex;
State state;
std::uint64_t generation=0;
std::array<Effects,kActorCount> effects{};
std::array<std::array<unsigned char,25>,kActorCount> nativeInflict{};
std::array<NativeEffects::Shadow,kActorCount> shadows{};
Resources restore;
Hash packHash{};
Store store;
void(*logger)(const char*)=nullptr;
void* standaloneOriginals[4]{};
std::atomic<bool> standaloneMode{false};
struct Pending {std::wstring path;Hash disk{},payload{};std::uintptr_t buffer=0;};
std::vector<Pending> pending;
struct Load {bool valid=false;Record record;};
thread_local Load incoming;
struct FieldScope {unsigned actor=255;NativeEffects::Shadow shadow;bool accepted=false;};
thread_local std::array<FieldScope,4> fields;
thread_local unsigned fieldDepth=0;
struct AggregateScope {unsigned actor=255;std::uintptr_t pointer=0;std::uint32_t hp=0,mp=0;bool valid=false;};
thread_local std::array<AggregateScope,4> aggregates;
thread_local unsigned aggregateDepth=0;
struct WriteCookie {Record record;std::wstring path;std::uint64_t generation=0;bool prepared=false;};
void Notice(const char* message){if(logger)logger(message);}
bool Copy(void* out,const void* in,std::size_t bytes) noexcept {return NativeUiSupport::Copy(out,in,bytes);}
bool Fingerprint(const void* data,std::size_t size,Hash& hash){return EquipmentWorkshop::Fingerprint(data,size,hash);}
bool Payload(const unsigned char* bytes,std::size_t size,Hash& hash){
    if(!bytes||size!=RonsoPool::kSaveSize)return false;
    SaveImage copy{};if(!Copy(copy.data(),bytes,size))return false;
    // The native load path clears this checksum mirror. It is not lineage.
    std::fill(copy.begin()+25844,copy.begin()+25848,static_cast<unsigned char>(0));
    return Fingerprint(copy.data()+64,copy.size()-64,hash);
}
bool InBattle() noexcept {unsigned char value=1;return !Copy(&value,reinterpret_cast<void*>(base + (::FfxHooks::ExecutableProfile::Rva<0xD2A8E0>())),1)||value!=0;}
bool OnOwner() noexcept {return ownerThread.load()==GetCurrentThreadId();}
bool Development() noexcept {return settings.developmentEnabled?settings.developmentEnabled():settings.fullDeck;}
void CacheEffects(){for(unsigned actor=0;actor<kActorCount;++actor)effects[actor]=Aggregate(state,actor);}
std::uintptr_t BattleActor(unsigned actor);
bool ReadElementalEffects(unsigned actor,Elemental::Snapshot& output) noexcept {
    const auto* current=ActorEffects(actor);const auto battle=BattleGeneration();
    if(!started.load()||!current||!battle||!InBattle()||!BattleActor(actor))return false;
    output=Elemental::Collect(*current,battle,state.revision);return true;
}
void ReconcileNative(){
    std::array<unsigned char,Acquisition::kPayloadBytes> payload{};
    if(!enabled.load()||!ready.load()||!OnOwner()||!Copy(payload.data(),reinterpret_cast<void*>(base+saveRva),payload.size()))return;
    const auto awarded=Acquisition::Reconcile(state,payload.data(),payload.size());
    if(awarded){char message[128]{};std::snprintf(message,sizeof(message),"[ffx-hooks] Arcana: awarded %u card(s) from observed pilgrimage/challenge milestones\n",awarded);Notice(message);}
}
unsigned char* Player(unsigned actor){return reinterpret_cast<unsigned char*>(base+playerRva+actor*NativeEffects::kPlayerBytes);}
std::uintptr_t BattleActor(unsigned actor){
    if(actor>=kActorCount)return 0;
    std::uint32_t table=0;std::uint16_t id=0xFFFF;
    if(!Copy(&table,reinterpret_cast<void*>(base+actorTableRva),4)||table<0x10000||table>UINT32_MAX-31*0xF90u)return 0;
    const auto pointer=std::uintptr_t(table)+actor*0xF90u;
    return Copy(&id,reinterpret_cast<void*>(pointer+0xE),2)&&id==actor?pointer:0;
}
void Refresh(unsigned actor){
    if(actor>=kActorCount||!started.load()||!OnOwner())return;
    reinterpret_cast<int(__cdecl*)(unsigned)>(base + (::FfxHooks::ExecutableProfile::Rva<0x3861B0>()))(actor);
}
void ReadEvent(const wchar_t* path,const unsigned char* disk,const unsigned char* loaded,std::size_t size) noexcept {
    if(!primed.load()||!enabled.load()||!path||size!=RonsoPool::kSaveSize)return;
    try {
        if(!RonsoPool::OwnerStore::IsSavePath(path))return;
        Pending entry;entry.path=path;entry.buffer=reinterpret_cast<std::uintptr_t>(loaded);
        if(!Fingerprint(disk,size,entry.disk)||!Payload(loaded,size,entry.payload))return;
        std::lock_guard<std::recursive_mutex> lock(mutex);
        pending.erase(std::remove_if(pending.begin(),pending.end(),[&](const Pending& old){return old.path==entry.path||old.buffer==entry.buffer;}),pending.end());
        if(pending.size()==64)pending.erase(pending.begin());
        pending.push_back(std::move(entry));
    }catch(...){code=Code::StorageError;}
}
void WriteEvent(const wchar_t*,const unsigned char*,std::size_t) noexcept {}
void RejectReadEvent() noexcept {
    ready=false;bound=false;loading=false;incoming.valid=false;code=Code::Conflict;
    // Keep the old shadows until serialization strips any applied temporary
    // fields. A failed read must neither mint a new collection nor persist buffs.
    try{std::lock_guard<std::recursive_mutex> lock(mutex);pending.clear();}
    catch(...){code=Code::StorageError;}
}
void ResetEvent() noexcept {
    if(!enabled.load())return;
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        state={};state.mode=settings.defaultMode;
        if(Development())AwardAll(state,state.revision);
        ++generation;restore={};shadows={};appliedMask=0;CacheEffects();loading=false;battleInSession=false;
        ownerThread=GetCurrentThreadId();bound=true;ready=true;code=Code::Ready;
    }catch(...){ready=false;code=Code::Conflict;}
}
void BeginLoad(const NativeGameplayEvents::Call& call) noexcept {
    if(call.destination!=reinterpret_cast<void*>(base+saveRva)||call.size!=RonsoPool::kSaveSize)return;
    ready=false;bound=false;loading=true;incoming.valid=false;battleInSession=false;
    try {
        Hash payload{};if(!Payload(static_cast<const unsigned char*>(call.source),call.size,payload))return;
        Pending selected;bool found=false;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            for(const auto& entry:pending)if(entry.payload==payload&&entry.buffer==reinterpret_cast<std::uintptr_t>(call.source)){selected=entry;found=true;break;}
            if(!found)for(const auto& entry:pending)if(entry.payload==payload){
                if(found&&selected.path!=entry.path){code=Code::Conflict;return;}
                selected=entry;found=true;
            }
        }
        if(!found){code=Code::Waiting;return;}
        std::array<Hash,sizeof(kCompatiblePackHashes)/sizeof(kCompatiblePackHashes[0])> previous{};
        for(std::size_t i=0;i<previous.size();++i)std::copy(std::begin(kCompatiblePackHashes[i]),std::end(kCompatiblePackHashes[i]),previous[i].begin());
        Record record;bool migrated=false;
        const auto result=store.ReadCompatible(selected.path,selected.disk,packHash,previous.data(),previous.size(),record,migrated);
        if(result==StoreCode::Missing){record.nativeHash=selected.disk;record.packHash=packHash;record.state.mode=settings.defaultMode;}
        else if(result!=StoreCode::Found&&result!=StoreCode::Recovered){code=result==StoreCode::Foreign?Code::Conflict:Code::StorageError;return;}
        if(migrated)Notice("[ffx-hooks] Arcana: known balance pack upgraded; collection and equipped cards preserved\n");
        if(Development())AwardAll(record.state,record.state.revision);
        incoming.record=record;incoming.valid=true;
    }catch(...){code=Code::StorageError;}
}
void EndLoad(const NativeGameplayEvents::Call& call,bool complete) noexcept {
    if(call.destination!=reinterpret_cast<void*>(base+saveRva))return;
    loading=false;
    if(!complete||!incoming.valid){ready=false;return;}
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        state=incoming.record.state;restore=incoming.record.resources;++generation;
        shadows={};appliedMask=0;CacheEffects();ownerThread=GetCurrentThreadId();
        incoming.valid=false;bound=true;ready=true;code=Code::Ready;
        ReconcileNative();
        Notice("[ffx-hooks] Arcana: native save session bound; equipment state recovered\n");
    }catch(...){ready=false;code=Code::Conflict;}
}
void BeginField(unsigned actor) noexcept {
    ++fieldDepth;
    if(fieldDepth>fields.size())return;
    auto& scope=fields[fieldDepth-1];scope={};scope.actor=actor;
    if(actor>=kActorCount||loading.load()||!OnOwner()||(!ready.load()&&!(appliedMask.load()&(1u<<actor))))return;
    scope.accepted=Copy(scope.shadow.baseline.data(),Player(actor),scope.shadow.baseline.size());
}
void EndField(bool complete) noexcept {
    if(!fieldDepth)return;
    if(fieldDepth>fields.size()){--fieldDepth;return;}
    auto& scope=fields[fieldDepth-1];
    try {
      std::lock_guard<std::recursive_mutex> lock(mutex);
      if(scope.accepted&&complete&&scope.actor<kActorCount){
        if(enabled.load()&&scope.shadow.seen==0x3FF&&Copy(scope.shadow.applied.data(),Player(scope.actor),scope.shadow.applied.size())){
            scope.shadow.valid=true;shadows[scope.actor]=scope.shadow;
            bool changed=false;
            for(unsigned i=0x24;i<=0x36;++i)if(scope.shadow.applied[i]!=scope.shadow.baseline[i])changed=true;
            for(unsigned i=0x4A;i<0x50;++i)if(scope.shadow.applied[i]!=scope.shadow.baseline[i])changed=true;
            if(changed)appliedMask.fetch_or(1u<<scope.actor);else appliedMask.fetch_and(~(1u<<scope.actor));
            restore.valid&=static_cast<std::uint8_t>(~(1u<<scope.actor));
        }else if(!enabled.load()){shadows[scope.actor]={};appliedMask.fetch_and(~(1u<<scope.actor));}
      }
    }catch(...){code=Code::Conflict;}
    scope={};--fieldDepth;
}
void AdjustSharedClamp(std::uintptr_t caller,int& value,int& minimum,int& maximum) noexcept {
    if(enabled.load()&&ready.load()&&!loading.load()&&fieldDepth&&fieldDepth<=fields.size()&&OnOwner()){
        auto& scope=fields[fieldDepth-1];
        if(scope.accepted&&scope.actor<kActorCount){
            const int role=NativeEffects::ClampRole(static_cast<std::uint32_t>(caller));
            if(role==0){
                Copy(scope.shadow.baseline.data()+0x4A,Player(scope.actor)+0x4A,6);
                unsigned char flags[6]{};std::memcpy(flags,scope.shadow.baseline.data()+0x4A,6);
                NativeEffects::MergeFlags(flags,effects[scope.actor]);Copy(Player(scope.actor)+0x4A,flags,6);
            }
            if(role>=0&&role<10)NativeEffects::AdjustClamp(role,value,minimum,maximum,value,effects[scope.actor],scope.shadow);
            if((role==10||role==11)&&(restore.valid&(1u<<scope.actor))){
                const auto stored=role==10?restore.hp[scope.actor]:restore.mp[scope.actor];
                value=static_cast<int>((std::min)(stored,static_cast<std::uint32_t>(INT32_MAX)));
            }
        }
    }
}
void BeginAggregate(unsigned actor) noexcept {
    ++aggregateDepth;if(aggregateDepth>aggregates.size())return;
    auto& scope=aggregates[aggregateDepth-1];scope={};scope.actor=actor;
    if(!enabled.load()||!ready.load()||loading.load()||!OnOwner()||actor>=kActorCount)return;
    scope.pointer=BattleActor(actor);
    scope.valid=scope.pointer&&Copy(&scope.hp,reinterpret_cast<void*>(scope.pointer+0x5D0),4)&&Copy(&scope.mp,reinterpret_cast<void*>(scope.pointer+0x5D4),4);
}
void EndAggregate(bool completed) noexcept {
    if(!aggregateDepth)return;
    if(aggregateDepth>aggregates.size()){--aggregateDepth;return;}
    auto& scope=aggregates[aggregateDepth-1];
    if(completed&&scope.valid&&enabled.load()&&ready.load()){
        NativeEffects::Battle before{};
        if(Copy(before.data(),reinterpret_cast<void*>(scope.pointer+0x540),before.size())){
            std::copy(before.begin()+0x9E,before.begin()+0xB7,nativeInflict[scope.actor].begin());
            auto after=before;NativeEffects::MergeBattle(after,effects[scope.actor]);
            for(unsigned i=0;i<after.size();++i)if(after[i]!=before[i])Copy(reinterpret_cast<void*>(scope.pointer+0x540+i),after.data()+i,1);
            // Re-evaluate max-HP/MP and current values with the composed native
            // BHP/BDL flags. Native battle buffs (Stamina/Mana) stay authoritative.
            reinterpret_cast<int(__cdecl*)(unsigned,void*,int)>(base + (::FfxHooks::ExecutableProfile::Rva<0x38D330>()))(scope.actor,reinterpret_cast<void*>(scope.pointer),-1);
            std::uint32_t hpMax=0,mpMax=0;
            if(Copy(&hpMax,reinterpret_cast<void*>(scope.pointer+0x594),4)&&Copy(&mpMax,reinterpret_cast<void*>(scope.pointer+0x598),4)){
                const auto hp=(std::min)(scope.hp,hpMax),mp=(std::min)(scope.mp,mpMax);
                Copy(reinterpret_cast<void*>(scope.pointer+0x5D0),&hp,4);Copy(reinterpret_cast<void*>(scope.pointer+0x5D4),&mp,4);
            }
        }
    }
    scope={};--aggregateDepth;
}
void EnterEvent(const NativeGameplayEvents::Call& call) noexcept {
    if(!primed.load())return;
    if(call.kind==NativeGameplayEvents::Kind::Load){BeginLoad(call);return;}
    if(call.kind==NativeGameplayEvents::Kind::Field){BeginField(call.actor);return;}
    if(call.kind==NativeGameplayEvents::Kind::Aggregate){BeginAggregate(call.actor);return;}
    if(call.kind==NativeGameplayEvents::Kind::Battle&&enabled.load()&&ready.load()&&OnOwner()){
        battleGeneration.fetch_add(1);
        battleInSession=true;
        for(unsigned actor=0;actor<kActorCount;++actor){
            bool equipped=false;for(auto card:state.slots[actor])if(card!=kEmpty)equipped=true;
            if(equipped)Refresh(actor);
        }
    }
}
void LeaveEvent(const NativeGameplayEvents::Call& call,bool complete) noexcept {
    if(call.kind==NativeGameplayEvents::Kind::Load)EndLoad(call,complete);
    else if(call.kind==NativeGameplayEvents::Kind::Field)EndField(complete);
    else if(call.kind==NativeGameplayEvents::Kind::Aggregate)EndAggregate(complete);
}
const NativeGameplayEvents::Observer gameplayObserver{EnterEvent,LeaveEvent};
bool RequiresProjection() noexcept {return appliedMask.load()!=0;}
bool ProjectEvent(const wchar_t* path,const unsigned char* source,unsigned char* output,std::size_t size,void** cookie) noexcept {
    if(size!=RonsoPool::kSaveSize||!path||!cookie)return false;
    *cookie=nullptr;
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if(!InBattle())ReconcileNative();
        Resources resources;
        for(unsigned actor=0;actor<kActorCount;++actor){
            if(!shadows[actor].valid)continue;
            NativeEffects::Player player{};
            std::memcpy(player.data(),source+playerFile+actor*NativeEffects::kPlayerBytes,player.size());
            const auto projected=NativeEffects::Project(player,shadows[actor]);
            std::memcpy(output+playerFile+actor*NativeEffects::kPlayerBytes,player.data(),player.size());
            if(projected.conflict){code=Code::Conflict;continue;}
            resources.valid|=static_cast<std::uint8_t>(1u<<actor);
            resources.hp[actor]=projected.hp;resources.mp[actor]=projected.mp;
        }
        // Serialization must remain vanilla-safe even if extension allocation
        // fails after the game's CRT has already opened the native save.
        if(bound.load())try {
            auto current=std::make_unique<WriteCookie>();
            current->record.state=state;current->record.packHash=packHash;
            current->record.resources=resources;current->path=path;current->generation=generation;
            *cookie=current.release();
        }catch(...){code=Code::StorageError;}
        return true;
    }catch(...){code=Code::StorageError;return false;}
}
bool PrepareEvent(void* opaque,const wchar_t*,const unsigned char* bytes,std::size_t size) noexcept {
    if(!opaque)return !bound.load();
    auto& cookie=*static_cast<WriteCookie*>(opaque);
    try {
        if(!Fingerprint(bytes,size,cookie.record.nativeHash)||!store.Prepare(cookie.path,cookie.record)){
            code=Code::StorageError;Notice("[ffx-hooks] ERROR Arcana extension could not be prepared; retry saving before leaving\n");return false;
        }
        cookie.prepared=true;return true;
    }catch(...){code=Code::StorageError;return false;}
}
void FinishEvent(void* opaque,const unsigned char* bytes,std::size_t size,bool success) noexcept {
    std::unique_ptr<WriteCookie> cookie(static_cast<WriteCookie*>(opaque));if(!cookie)return;
    try {
        Hash hash{};
        if(success&&cookie->prepared&&Fingerprint(bytes,size,hash)&&hash==cookie->record.nativeHash&&store.Commit(cookie->path,hash)){
            std::lock_guard<std::recursive_mutex> lock(mutex);
            if(cookie->generation==generation&&enabled.load())code=Code::Ready;
        }else if(success){code=Code::StorageError;Notice("[ffx-hooks] ERROR Arcana extension commit failed; native save remains valid\n");}
        else if(cookie->prepared)store.Abort(cookie->path,cookie->record);
    }catch(...){code=Code::StorageError;}
}
const NativeSaveEvents::Observer saveObserver{ReadEvent,WriteEvent,ResetEvent,ProjectEvent,PrepareEvent,FinishEvent,RequiresProjection,
    nullptr,nullptr,nullptr,RejectReadEvent};
int __cdecl LoadShim(void* destination,const void* source){
    auto ticket=NativeGameplayEvents::Begin({NativeGameplayEvents::Kind::Load,255,source,destination,RonsoPool::kSaveSize});
    int result=0;bool complete=false;
    __try{result=reinterpret_cast<int(__cdecl*)(void*,const void*)>(standaloneOriginals[0])(destination,source);complete=true;}
    __finally{NativeGameplayEvents::End(ticket,complete);}
    return result;
}
int Produce(unsigned actor,void* original,NativeGameplayEvents::Kind kind){
    auto ticket=NativeGameplayEvents::Begin({kind,actor});int result=0;bool complete=false;
    __try{result=reinterpret_cast<int(__cdecl*)(unsigned)>(original)(actor);complete=true;}
    __finally{NativeGameplayEvents::End(ticket,complete);}
    return result;
}
int __cdecl FieldShim(unsigned actor){return Produce(actor,standaloneOriginals[1],NativeGameplayEvents::Kind::Field);}
int __cdecl AggregateShim(unsigned actor){return Produce(actor,standaloneOriginals[2],NativeGameplayEvents::Kind::Aggregate);}
unsigned __cdecl DamageShim(unsigned user,void* userPointer,unsigned target,void* targetPointer,const void* command,unsigned commandId,void* information,unsigned a8,unsigned a9,unsigned a10,unsigned a11){
    NativeGameplayEvents::Damage damage{user,target,commandId,userPointer,targetPointer,command,information};
    auto ticket=NativeGameplayEvents::Begin({NativeGameplayEvents::Kind::Damage,user,&damage,nullptr,sizeof(damage)});
    using Fn=unsigned(__cdecl*)(unsigned,void*,unsigned,void*,const void*,unsigned,void*,unsigned,unsigned,unsigned,unsigned);
    unsigned result=0;bool complete=false;
    __try{result=reinterpret_cast<Fn>(standaloneOriginals[3])(user,userPointer,target,targetPointer,command,commandId,information,a8,a9,a10,a11);complete=true;}
    __finally{NativeGameplayEvents::End(ticket,complete);}
    return result;
}
}
bool Prime(std::uintptr_t module,const Settings& options,bool validateOnly,void(*log)(const char*)){
    if(!Coexistence::runtime.SavePipelineAllowed())return false;
    if(!options.enabled||validateOnly)return false;
    if(options.defaultMode!=Mode::Twin&&options.defaultMode!=Mode::Constellation)return false;
    if(primed.load())return enabled.load();
    if(!NativeUiSupport::Profile(module,Evidence::basic)||!SharedClamp::Profile(module)){
        code=Code::Unsupported;return false;
    }
    base=module;settings=options;logger=log;std::copy(std::begin(kPackHash),std::end(kPackHash),packHash.begin());
    if(!NativeGameplayEvents::Subscribe(&gameplayObserver)||!NativeSaveEvents::Subscribe(&saveObserver)){
        NativeGameplayEvents::Unsubscribe(&gameplayObserver);NativeSaveEvents::Unsubscribe(&saveObserver);code=Code::Conflict;return false;
    }
    enabled=true;primed=true;code=Code::Waiting;return true;
}
bool Start(){
    if(!primed.load()||!enabled.load())return false;
    if(started.load())return true;
    const bool standalone=NativeGameplayEvents::provider.load()==NativeGameplayEvents::Provider::None;
    standaloneMode=standalone;
    if(!SharedClamp::Profile(base)||(standalone&&!NativeUiSupport::Profile(base,Evidence::basic))){code=Code::Unsupported;return false;}
    if(!SharedClamp::Start(base)||!SharedClamp::Register(SharedClamp::Slot::Arcana,&AdjustSharedClamp)){code=Code::Conflict;return false;}
    if(standalone){
        const std::uint32_t rvas[]={(::FfxHooks::ExecutableProfile::Rva<0x4B5450u>()),(::FfxHooks::ExecutableProfile::Rva<0x3861B0u>()),(::FfxHooks::ExecutableProfile::Rva<0x39C610u>()),(::FfxHooks::ExecutableProfile::Rva<0x38E680u>())};
        void* replacements[]={reinterpret_cast<void*>(LoadShim),reinterpret_cast<void*>(FieldShim),reinterpret_cast<void*>(AggregateShim),reinterpret_cast<void*>(DamageShim)};
        if(!NativeUiSupport::Install(base,rvas,replacements,standaloneOriginals,MinHookBatch::Owner::ArcanaGameplay,reinterpret_cast<const void*>(&Start))){code=Code::Conflict;return false;}
        NativeGameplayEvents::provider=NativeGameplayEvents::Provider::Arcana;
    }
    if(!Elemental::Register(&ReadElementalEffects)){Stop();code=Code::Conflict;return false;}
    started=true;return true;
}
void Stop() noexcept {
    enabled=false;ready=false;code=Code::Stopped;
    Elemental::Unregister(&ReadElementalEffects);
    if(!appliedMask.load()){NativeSaveEvents::Unsubscribe(&saveObserver);NativeGameplayEvents::Unsubscribe(&gameplayObserver);}
}
void Tick() noexcept {
    if(!enabled.load()||!ready.load()||!OnOwner()||InBattle())return;
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        ReconcileNative();
        if(Development()&&AwardAll(state,state.revision)==Error::None)CacheEffects();
        for(unsigned actor=0;actor<kActorCount;++actor){
            bool equipped=false;for(auto id:state.slots[actor])if(id!=kEmpty)equipped=true;
            if(equipped&&!shadows[actor].valid)Refresh(actor);
        }
    }catch(...){code=Code::Conflict;}
}
bool Requested() noexcept {return primed.load()&&enabled.load();}
bool Capture(State& out,std::uint64_t& session) noexcept {
    if(!enabled.load()||!ready.load()||!OnOwner()||InBattle())return false;
    try {std::lock_guard<std::recursive_mutex> lock(mutex);out=state;session=generation;return true;}catch(...){return false;}
}
Error EquipCard(std::uint64_t session,std::uint64_t revision,unsigned actor,unsigned slot,std::int16_t card,bool transfer) noexcept {
    if(!enabled.load()||!ready.load()||!OnOwner()||InBattle())return Error::InvalidState;
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if(session!=generation)return Error::Stale;
        const auto before=state;const auto result=Equip(state,revision,actor,slot,card,transfer);
        if(result!=Error::None)return result;
        CacheEffects();
        for(unsigned id=0;id<kActorCount;++id)if(before.slots[id]!=state.slots[id])Refresh(id);
        return Error::None;
    }catch(...){code=Code::Conflict;return Error::InvalidState;}
}
Error SelectMode(std::uint64_t session,std::uint64_t revision,Mode mode,bool releaseThird) noexcept {
    if(!enabled.load()||!ready.load()||!OnOwner()||InBattle())return Error::InvalidState;
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);if(session!=generation)return Error::Stale;
        auto slots=state.slots;if(releaseThird&&mode==Mode::Twin)for(auto& actor:slots)actor[2]=kEmpty;
        const auto before=state;const auto result=ChangeMode(state,revision,mode,releaseThird?&slots:nullptr);
        if(result!=Error::None)return result;
        CacheEffects();for(unsigned id=0;id<kActorCount;++id)if(before.slots[id]!=state.slots[id])Refresh(id);return Error::None;
    }catch(...){code=Code::Conflict;return Error::InvalidState;}
}
const Effects* ActorEffects(unsigned actor) noexcept {return enabled.load()&&ready.load()&&OnOwner()&&actor<kActorCount?&effects[actor]:nullptr;}
unsigned NativeAttackChance(unsigned actor,unsigned status) noexcept {return actor<kActorCount&&status<25&&OnOwner()?nativeInflict[actor][status]:0;}
std::uint64_t BattleGeneration() noexcept {return battleInSession.load()?battleGeneration.load():0;}
Code Status() noexcept {return code.load();}
const char* Detail() noexcept {
    switch(code.load()){
    case Code::Off:return "Arcana OFF - enable the module and restart";
    case Code::Waiting:return "Load a save to activate Arcana";
    case Code::Ready:return "Arcana ready - changes persist when you save";
    case Code::Unsupported:return "Arcana: unsupported game profile";
    case Code::Conflict:return "Arcana state changed outside its owner; reload a matching save";
    case Code::StorageError:return "Arcana data was not saved. Check storage and save again before leaving.";
    case Code::Stopped:return "Arcana stopped";
    }
    return "Arcana unavailable";
}
NativeUi::Callbacks Bindings(void(*images)(const NativeUi::Images&) noexcept){return {Capture,EquipCard,SelectMode,images,Tick,Detail};}
}
