#include "EquipmentWorkshopRuntime.h"
#include "NativeSaveEvents.h"
#include "NativeGameplayEvents.h"
#include "RonsoPoolStore.h"
#include "RonsoPoolSave.h"
#include "F8RuntimeCore.h"
#include "MinHookBatchCoordinator.h"
#include "EquipmentWorkshopEvidence.h"
#include "EquipmentWorkshopSettings.h"
#include "CombatExtensionBus.h"
#include "EquipmentWorkshopCatalogBridge.h"
#include "AeonAscensionBridge.h"
#include "EquipmentEffectBridge.h"
#include "RonsoPoolSave.h"
#include "../../../../research/equipment_workshop/include/lifecycle.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>
#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <MinHook.h>
#endif
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <vector>
#include "SharedDamageRuntime.h"

namespace FfxHooks::EquipmentWorkshop {
namespace {
constexpr std::uintptr_t kSaveRam=0xD2CA90,kGearRam=0xD30F2C,kGilRam=0xD307D8;
enum Hook {Load,Create,Swap,Free,Equip,Field,Aggregate,Protect,Shell,Gear,Row,Contains,Damage,Legend,HookCount};
constexpr std::uint32_t rvas[HookCount]={0x4B5450,0x3AB930,0x3ABA10,0x3ABCC0,0x3AB990,0x3861B0,0x39C610,0x38AE00,0x38AE80,0x3ABBF0,0x3AB890,0x3A0C40,0x38E680,0x4C3150};
void* originals[HookCount]{};
std::uintptr_t module=0;
std::atomic<bool> accepting{false},enabled{false},started{false};
std::atomic<bool> sharedOnly{false};
std::atomic<PresentationAdapter> presentationAdapter{nullptr};
std::recursive_mutex mutex;
workshop::State state{};
AeonAscension::Ledger ascensionReceipts{};
AeonAscension::SaveId ascensionSave{};
bool ready=false,loading=false,saveInFlight=false;
unsigned ownerThread=0;
std::atomic<RuntimeCode> code{RuntimeCode::Disabled};
Store store;
std::wstring loadedPath;
Hash loadedDisk{};
Hash checkpointHead{};
std::array<unsigned char,64> loadedHeader{};
#ifdef FFXHOOKS_TESTING
int failBeforeWrite=-1;
#endif
LogFn logFn=nullptr;
struct Pending {Hash disk{},payload{},checkpointProof{};std::wstring path;std::uintptr_t buffer=0;bool checkpoint=false;};
std::vector<Pending> pending;
struct GearView {unsigned char bytes[24]{};workshop::Piece piece{};unsigned slot=200;bool managed=false;};
struct ViewScope {unsigned owner=255,count=0,nextGear=0,nextAbility=0;GearView gear[2];unsigned char rows[2][5][108]{};workshop::Catalog catalog{};};
thread_local ViewScope* view=nullptr;
thread_local unsigned char fallback[217][24]{};
struct DamageScope {unsigned owner=255;const void* info=nullptr;};
thread_local DamageScope* damageScope=nullptr;
struct LegendScope {unsigned owner=255,slot=200;};
thread_local LegendScope* legendScope=nullptr;

bool Copy(void* to,const void* from,std::size_t size) noexcept {
    __try{std::memcpy(to,from,size);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void Log(const char* message){if(logFn)logFn(message);}
void Fault(RuntimeCode value,const char* message){ready=false;code=value;Log(message);}
bool NativeImage(SaveImage& image){
    image.fill(0);
    std::memcpy(image.data(),loadedHeader.data(),loadedHeader.size());
    if(!module || !Copy(image.data()+64,reinterpret_cast<void*>(module+kSaveRam),0x68C0))return false;
    RonsoPool::SealSave(image);return true;
}
unsigned GearSlot(const void* pointer){
    const auto p=reinterpret_cast<std::uintptr_t>(pointer),start=module+kGearRam;
    return p>=start && p<start+4400 && (p-start)%22==0?static_cast<unsigned>((p-start)/22):200;
}
bool Refresh(){
    SaveImage image{};if(!NativeImage(image))return false;
    for(unsigned i=0;i<200;++i)if(std::memcmp(state.pieces[i].native,image.data()+0x44DC+i*22,22)!=0){
        Fault(RuntimeCode::Conflict,"[ffx-hooks] Workshop: unobserved inventory change; extension admission closed\n");return false;
    }
    workshop::State fresh{};if(!ImportSave(image,0,fresh))return false;
    if(std::memcmp(fresh.items,state.items,sizeof(state.items))!=0){std::memcpy(state.items,fresh.items,sizeof(state.items));++state.revision;}
    return true;
}
bool OnOwner(){return accepting.load() && ready && ownerThread==GetCurrentThreadId();}
const AeonAscension::Ledger* ReceiptSnapshot(){
    return AeonAscension::Nonzero(ascensionReceipts.saveId)?&ascensionReceipts:nullptr;
}
workshop::Error ReadEconomy(workshop::Economy& economy){
    if(!Settings::Read(economy.policy))return workshop::Error::InvalidPolicy;
    std::uint16_t story=0;
    if(!Copy(&economy.gil,reinterpret_cast<void*>(module+kGilRam),4)||!Copy(&story,reinterpret_cast<void*>(module+kSaveRam+0xBEC),2))return workshop::Error::InvalidState;
    unsigned char payload[0x68C0]{};
    if(!Copy(payload,reinterpret_cast<void*>(module+kSaveRam),sizeof(payload))||
       !workshop::ReadAeonProgress(payload,sizeof(payload),economy.aeons))return workshop::Error::InvalidState;
    economy.customizeUnlocked=workshop::NativeCustomizeUnlocked(story)?1:0;
    (void)CatalogBridge::Read(economy.catalog);return workshop::Error::Ok;
}
bool NormalizeLoadedImage(SaveImage& image) noexcept {
    if(RonsoPool::IsValidSave(image))return true;
    // Both completed reads and native RAM copies can arrive after the game's
    // CRC checker clears its mirror. Normalize a private copy, never game RAM.
    const auto expected=static_cast<std::uint16_t>(image[26]|(unsigned(image[27])<<8));
    if(image[25844]||image[25845]||image[25846]||image[25847]||
       RonsoPool::SaveChecksum(image)!=expected)return false;
    image[25844]=image[26];image[25845]=image[27];return true;
}
void ReadAssociated(const wchar_t* path,const unsigned char* disk,const unsigned char* loaded,std::size_t size,
                    const NativeSaveEvents::CheckpointSelection* checkpoint) noexcept {
    if(!accepting.load() || !path || size!=kSaveBytes)return;
    try{
        if(!RonsoPool::OwnerStore::IsSavePath(path))return;
        Pending entry{};entry.path=path;entry.buffer=reinterpret_cast<std::uintptr_t>(loaded);
        if(checkpoint){entry.checkpoint=checkpoint->selected;entry.checkpointProof=checkpoint->proof;}
        SaveImage canonical{};
        if(!Copy(canonical.data(),loaded,size)||!NormalizeLoadedImage(canonical)||
           !Fingerprint(disk,size,entry.disk)||!Fingerprint(canonical.data()+64,size-64,entry.payload))return;
        std::lock_guard<std::recursive_mutex> lock(mutex);
        // A successful read replaces the buffer's provenance even when two
        // save slots contain identical bytes. Never keep an older path attached
        // to an address the game has already reused for another completed read.
        pending.erase(std::remove_if(pending.begin(),pending.end(),[&](const Pending& old){
            return old.path==entry.path||old.buffer==entry.buffer;
        }),pending.end());
        if(pending.size()==1000)pending.erase(pending.begin());
        pending.push_back(std::move(entry));
        Log("[ffx-hooks] Workshop: completed native save read associated\n");
    }catch(...){Log("[ffx-hooks] Workshop: save-read association could not be captured\n");}
}
void ReadEvent(const wchar_t* path,const unsigned char* disk,const unsigned char* loaded,std::size_t size) noexcept {
    ReadAssociated(path,disk,loaded,size,nullptr);
}
void CheckpointReadEvent(const wchar_t* path,const unsigned char* disk,const unsigned char* loaded,std::size_t size,
                         const NativeSaveEvents::CheckpointSelection& selected) noexcept {
    ReadAssociated(path,disk,loaded,size,&selected);
}
bool SelectReadEvent(const wchar_t* path,const unsigned char* disk,unsigned char* selected,std::size_t size,
                     NativeSaveEvents::CheckpointSelection* selection) noexcept {
    if(!accepting.load())return true;
    if(!path||!disk||!selected||!selection||size!=kSaveBytes)return false;
    try{
        std::lock_guard<std::recursive_mutex> lock(mutex);
        SaveImage input{},output{};if(!Copy(input.data(),disk,size))return false;
        NativeSaveEvents::CheckpointSelection found{};
        const auto result=store.SelectCheckpoint(path,input,output,found);
        if(result==StoreResult::Missing){*selection=found;return true;}
        if(result==StoreResult::Found&&Copy(selected,output.data(),size)){
            *selection=found;Log("[ffx-hooks] Workshop: restoring the complete accepted paid checkpoint\n");return true;
        }
        Fault(RuntimeCode::StorageError,"[ffx-hooks] Workshop: checkpoint conflict; load rejected instead of refunding an accepted transaction\n");
    }catch(...){std::lock_guard<std::recursive_mutex> lock(mutex);Fault(RuntimeCode::StorageError,
        "[ffx-hooks] Workshop: checkpoint selection failed; no fallback to an unpaid state\n");}
    return false;
}
bool PrepareLoad(const SaveImage& image,const void* address){
    std::lock_guard<std::recursive_mutex> lock(mutex);
    ready=false;saveInFlight=false;code=RuntimeCode::WaitingForSave;
    // Native load validation clears the payload CRC DWORD at file+0x64F4
    // before calling the load-copy producer. Restore that known transition in
    // a private copy only, after proving the header CRC against the payload.
    // All other bytes still have to match the observed completed file read.
    SaveImage canonical=image;
    if(!NormalizeLoadedImage(canonical)){
        code=RuntimeCode::Conflict;
        Log("[ffx-hooks] Workshop: native load rejected; checksum transition is not verified\n");
        return false;
    }
    Hash payload{};if(!Fingerprint(canonical.data()+64,canonical.size()-64,payload))return false;
    const Pending* selected=nullptr;
    for(const auto& item:pending)if(item.payload==payload && item.buffer==reinterpret_cast<std::uintptr_t>(address)){selected=&item;break;}
    if(!selected)for(const auto& item:pending)if(item.payload==payload){if(selected&&selected->path!=item.path){code=RuntimeCode::Conflict;return false;}selected=&item;}
    if(!selected){Log("[ffx-hooks] Workshop: native load has no matching completed save read\n");return false;}
    workshop::State next{};const auto found=selected->checkpoint?
        store.ReadCheckpointLoaded(selected->path,selected->disk,selected->checkpointProof,canonical,next):
        store.ReadLoaded(selected->path,selected->disk,canonical,next);
    if(found==StoreResult::Missing){
        std::uint64_t seed=0;
        if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&seed),sizeof(seed),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0 || !ImportSave(canonical,seed,next)){code=RuntimeCode::Conflict;return false;}
        if(enabled.load()&&!store.PinLoaded(selected->path,selected->disk,canonical,next)){
            code=RuntimeCode::StorageError;
            Log("[ffx-hooks] Workshop: initial inventory seed could not be durably admitted; editing remains closed\n");return false;
        }
    }else if(found!=StoreResult::Found){code=RuntimeCode::StorageError;return false;}
    // Receipt attachments bind the persisted revision. Resolve them before
    // incrementing the in-memory revision used to invalidate old confirmations.
    AeonAscension::Ledger receipts{};
    const auto receiptResult=store.ReadReceipts(selected->path,next,receipts);
    if(receiptResult!=StoreResult::Found&&receiptResult!=StoreResult::Missing){
        Fault(RuntimeCode::StorageError,"[ffx-hooks] Workshop: Ascension receipt integrity mismatch; effects and editing closed\n");return false;
    }
    AeonAscension::SaveId save=receipts.saveId;
    if(!AeonAscension::Nonzero(save)&&!store.SaveIdentity(selected->path,selected->disk,save)){
        code=RuntimeCode::StorageError;return false;
    }
    // The persisted state is packed. Compare aligned value copies instead of
    // binding a uint64_t reference to its potentially unaligned revision field.
    const auto previousRevision=state.revision,nextRevision=next.revision;
    if(nextRevision>UINT64_MAX-1024 || previousRevision>UINT64_MAX-1024){code=RuntimeCode::Conflict;return false;}
    state=next;state.revision=(std::max)(previousRevision,nextRevision)+1;
    ascensionReceipts=receipts;ascensionSave=save;
    loadedPath=selected->path;loadedDisk=selected->disk;
    checkpointHead=selected->checkpointProof;
    std::memcpy(loadedHeader.data(),canonical.data(),loadedHeader.size());
    ownerThread=GetCurrentThreadId();ready=true;loading=true;code=RuntimeCode::Ready;return true;
}
void FinishLoad(){std::lock_guard<std::recursive_mutex> lock(mutex);loading=false;if(ready&&!Refresh())code=RuntimeCode::Conflict;if(ready)Log("[ffx-hooks] Workshop: native save load verified; inventory ready\n");}
void PrepareWriteEvent(const wchar_t* path,const unsigned char* actual,std::size_t size) noexcept {
    if(!accepting.load() || !path || size!=kSaveBytes)return;
    try{
        SaveImage image{};if(!Copy(image.data(),actual,size))return;
        workshop::State saved{};AeonAscension::Ledger receipts{};
        {std::lock_guard<std::recursive_mutex> lock(mutex);if(!ready)return;saved=state;receipts=ascensionReceipts;saveInFlight=true;code=RuntimeCode::WaitingForSave;}
        workshop::State native{};if(!ImportSave(image,0,native))return;
        // Ordinary item use/loot is native-owned. Save the actual quantities;
        // equipment identity must still match every recorded native byte.
        if(std::memcmp(saved.items,native.items,sizeof(saved.items))){
            if(saved.revision==UINT64_MAX)throw std::overflow_error("Workshop save revision exhausted");
            ++saved.revision;std::memcpy(saved.items,native.items,sizeof(saved.items));
        }
        if(!MatchesSave(image,saved)||!store.PrepareSave(path,image,saved,AeonAscension::Nonzero(receipts.saveId)?&receipts:nullptr)){
            std::lock_guard<std::recursive_mutex> lock(mutex);Fault(RuntimeCode::StorageError,"[ffx-hooks] Workshop: pre-save journal failed; editing closed, native stream not replaced\n");
        }
    }catch(...){std::lock_guard<std::recursive_mutex> lock(mutex);Fault(RuntimeCode::StorageError,"[ffx-hooks] Workshop: extension save exception; native bytes preserved\n");}
}
void WriteEvent(const wchar_t* path,const unsigned char* actual,std::size_t size) noexcept {
    if(!accepting.load()||!path||size!=kSaveBytes)return;
    try{
        SaveImage image{};if(!Copy(image.data(),actual,size))return;
        std::lock_guard<std::recursive_mutex> lock(mutex);if(!ready)return;
        // Do not snapshot later gameplay state here: commit exactly the state
        // prepared for these native bytes. A missing completion is recoverable
        // only when a later native read has the identical complete file hash.
        if(!store.CommitPrepared(path,image))Fault(RuntimeCode::StorageError,
            "[ffx-hooks] Workshop: save journal completion failed; prepared snapshot retained\n");
        else {
            saveInFlight=false;code=RuntimeCode::Ready;
            // Save As creates an independent slot; it does not silently retarget
            // the currently loaded slot's paid checkpoint.
            if(_wcsicmp(path,loadedPath.c_str())==0){
                Fingerprint(image.data(),image.size(),loadedDisk);
                std::memcpy(loadedHeader.data(),image.data(),loadedHeader.size());
            }
        }
    }catch(...){std::lock_guard<std::recursive_mutex> lock(mutex);Fault(RuntimeCode::StorageError,
        "[ffx-hooks] Workshop: save journal completion exception; native write not retried\n");}
}
void ResetEvent() noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(!loading){const bool wasReady=ready;ready=false;saveInFlight=false;ownerThread=0;ascensionReceipts={};ascensionSave={};code=RuntimeCode::WaitingForSave;if(wasReady)Log("[ffx-hooks] Workshop: native reset retired the loaded inventory\n");}
}
const NativeSaveEvents::Observer observer{ReadEvent,WriteEvent,ResetEvent,nullptr,nullptr,nullptr,nullptr,PrepareWriteEvent,SelectReadEvent,CheckpointReadEvent,ResetEvent};

struct EventSnapshot {unsigned char gear[4400]{};std::uint64_t revision=0;bool valid=false;};
EventSnapshot Before(){
    EventSnapshot capture{};std::lock_guard<std::recursive_mutex> lock(mutex);
    if(!accepting.load()||!ready)return capture;
    if(!Copy(capture.gear,reinterpret_cast<void*>(module+kGearRam),4400))return capture;
    for(unsigned i=0;i<200;++i)if(std::memcmp(capture.gear+i*22,state.pieces[i].native,22)!=0){Fault(RuntimeCode::Conflict,"[ffx-hooks] Workshop: inventory producer began from an unknown state\n");return capture;}
    capture.revision=state.revision;capture.valid=true;return capture;
}
void After(const EventSnapshot& before,workshop::InventoryEvent event,unsigned first,unsigned second=200,unsigned owner=0){
    if(!before.valid)return;
    unsigned char after[4400]{};if(!Copy(after,reinterpret_cast<void*>(module+kGearRam),4400))return;
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(!accepting.load()||!ready)return;
    const bool same=std::memcmp(before.gear,after,4400)==0;
    if(same && (event!=workshop::InventoryEvent::Swapped || first==second || first>=200 || second>=200))return;
    if(state.revision!=before.revision){Fault(RuntimeCode::Conflict,"[ffx-hooks] Workshop: overlapping inventory mutation rejected\n");return;}
    workshop::Lifecycle tracked;
    if(!tracked.Begin(true,true,state)||!tracked.Observe(event,first,second,owner,before.revision,before.gear,after)||!tracked.Snapshot(state)||
       !AeonAscension::Validate(ascensionReceipts,ascensionSave,state))
        Fault(RuntimeCode::Conflict,"[ffx-hooks] Workshop: inventory event did not match its native result\n");
}
using LoadFn=int(__cdecl*)(void*,const void*);
int __cdecl LoadShim(void* destination,const void* source){
    SaveImage image{};const bool candidate=accepting.load()&&destination==reinterpret_cast<void*>(module+kSaveRam)&&Copy(image.data(),source,image.size());
    if(candidate){
        CombatExtensions::Reset(CombatExtensions::ResetReason::NativeLoad);
        if(!sharedOnly.load())(void)PrepareLoad(image,source);
    }
    auto ticket=NativeGameplayEvents::Begin({NativeGameplayEvents::Kind::Load,255,source,destination,SaveImage{}.size()});
    int result=0;bool completed=false;
    __try{result=reinterpret_cast<LoadFn>(originals[Load])(destination,source);completed=true;}
    __finally{if(candidate&&completed&&!sharedOnly.load())FinishLoad();NativeGameplayEvents::End(ticket,completed);}
    return result;
}
unsigned __cdecl CreateShim(const void* native){const auto before=Before();const auto result=reinterpret_cast<unsigned(__cdecl*)(const void*)>(originals[Create])(native);After(before,workshop::InventoryEvent::Created,result>=0x5000&&result<0x50C8?result-0x5000:200);return result;}
int __cdecl SwapShim(unsigned a,unsigned b){const auto before=Before();const auto result=reinterpret_cast<int(__cdecl*)(unsigned,unsigned)>(originals[Swap])(a,b);After(before,workshop::InventoryEvent::Swapped,(a&0xF000)==0x7000||(a&0xF000)==0xB000?200:a&0xFFF,(b&0xF000)==0x7000||(b&0xF000)==0xB000?200:b&0xFFF);return result;}
int __cdecl FreeShim(unsigned a){const auto before=Before();const auto result=reinterpret_cast<int(__cdecl*)(unsigned)>(originals[Free])(a);After(before,workshop::InventoryEvent::Removed,(a&0xF000)==0x7000||(a&0xF000)==0xB000?200:a&0xFFF);return result;}
int __cdecl EquipShim(unsigned owner,unsigned kind,unsigned a){
    const auto before=Before();unsigned char previous=255;
    if(owner<18&&kind<2)Copy(&previous,reinterpret_cast<void*>(module+0xD3205C+owner*0x94+0x2D+kind),1);
    const int result=reinterpret_cast<int(__cdecl*)(unsigned,unsigned,unsigned)>(originals[Equip])(owner,kind,a);
    if(a==255)After(before,workshop::InventoryEvent::Unequipped,previous<200?previous:200,200,owner);
    else After(before,workshop::InventoryEvent::Equipped,a&0xFFF,previous<200?previous:200,owner);
    return result;
}
int Produce(unsigned owner,Hook hook){
    ViewScope scope{};scope.owner=owner;(void)CatalogBridge::Read(scope.catalog);auto* previous=view;view=&scope;int result=0;
    EquipmentEffects::Pipeline::Scope effectScope{};
    EquipmentEffects::Pipeline::Enter(effectScope,owner,hook==Aggregate);
    auto ticket=NativeGameplayEvents::Begin({hook==Field?NativeGameplayEvents::Kind::Field:NativeGameplayEvents::Kind::Aggregate,owner});
    bool completed=false;
    __try{result=reinterpret_cast<int(__cdecl*)(unsigned)>(originals[hook])(owner);completed=true;}
    __finally{NativeGameplayEvents::End(ticket,completed);EquipmentEffects::Pipeline::Leave(effectScope);view=previous;}
    return result;
}
int __cdecl FieldShim(unsigned owner){return Produce(owner,Field);}
int __cdecl AggregateShim(unsigned owner){return Produce(owner,Aggregate);}
void RefreshEquippedAeon(unsigned owner){
    // The native field producer recalculates equipped effects after a committed
    // change. Its real growth inputs stay native-owned; no base stats are forged.
    __try{Produce(owner,Field);}__except(EXCEPTION_EXECUTE_HANDLER){
        Fault(RuntimeCode::Conflict,"[ffx-hooks] Workshop: committed Aeon gear; native effect refresh failed\n");
    }
}
int __cdecl LegendShim(unsigned owner,unsigned level){
    const bool observe=OnOwner()&&owner<18&&level>=1&&level<=2&&!legendScope;
    EventSnapshot before{};if(observe)before=Before();
    LegendScope current{};current.owner=owner;auto* previous=legendScope;
    if(before.valid)legendScope=&current;
    int result=0;
    __try{result=reinterpret_cast<int(__cdecl*)(unsigned,unsigned)>(originals[Legend])(owner,level);}
    __finally{legendScope=previous;}
    if(before.valid){
        After(before,workshop::InventoryEvent::LegendAbilities,current.slot,200,owner);
        if(result&&OnOwner())RefreshEquippedAeon(owner);
    }
    return result;
}
const unsigned char* __cdecl GearShim(unsigned id,void* unknown){
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-module;
    const auto* native=reinterpret_cast<const unsigned char*(__cdecl*)(unsigned,void*)>(originals[Gear])(id,unknown);
    // TkGetLegendWeapon supplies the exact record selected by the native altar.
    // Record that identity before presentation adapters can substitute a copy.
    if(legendScope&&caller==0x4C30F2&&native){
        unsigned char record[22]{};const unsigned slot=GearSlot(native);
        if(slot<200&&Copy(record,native,sizeof(record))&&record[2]&&record[4]==legendScope->owner&&
           !record[5]&&(record[3]&4))legendScope->slot=slot;
    }
    if(const auto adapter=presentationAdapter.load()){const auto* shown=adapter(caller,native);if(shown!=native)return shown;}
    if(caller!=0x38677B && caller!=0x39C782)return native;
    GearView* entry=nullptr;
    if(view && view->count<2)entry=&view->gear[view->count++];
    if(!native)return nullptr;
    unsigned slot=GearSlot(native),fallbackSlot=slot;
    if(entry)entry->slot=slot;
    if(slot==200){const unsigned family=id>>12,index=id&0xFFF;fallbackSlot=family==7?200+(index<8?index:0):family==11?208+(index<8?index:0):216;}
    auto* bytes=entry?entry->bytes:fallback[fallbackSlot];
    if(!Copy(bytes,native,22))return nullptr;bytes[22]=255;bytes[23]=0;
    if(entry && enabled.load() && accepting.load() && slot<200){
        std::lock_guard<std::recursive_mutex> lock(mutex);
        const auto& piece=state.pieces[slot];
        if(ready&&ownerThread==GetCurrentThreadId()&&piece.id&&piece.native[4]==view->owner&&piece.native[6]==view->owner&&std::memcmp(piece.native,native,22)==0){
            entry->piece=piece;entry->managed=true;
            if(piece.fifthUnlocked&&(workshop::SupportedFifth(piece.fifth,&view->catalog)||EquipmentEffects::Pipeline::Fifth(view->owner,piece))){
                bytes[22]=static_cast<unsigned char>(piece.fifth);bytes[23]=static_cast<unsigned char>(piece.fifth>>8);
            }
        }
    }
    return bytes;
}
const unsigned char* __cdecl RowShim(unsigned id,const void* table,void* unknown){
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-module;
    const auto* native=reinterpret_cast<const unsigned char*(__cdecl*)(unsigned,const void*,void*)>(originals[Row])(id,table,unknown);
    if(!view || (caller!=0x3867B5 && caller!=0x39C8D9))return native;
    while(view->nextGear<view->count){
        if(view->nextAbility==5){++view->nextGear;view->nextAbility=0;continue;}
        auto& entry=view->gear[view->nextGear];const unsigned index=view->nextAbility++;
        const unsigned word=entry.bytes[14+2*index]|(unsigned(entry.bytes[15+2*index])<<8);
        if(word==0||word==255)continue;
        if((word&0xFFF)!=id || !native)return native;
        auto* copy=view->rows[view->nextGear][index];if(!Copy(copy,native,108))return native;
        bool changed=false;
        if(accepting.load())changed=EquipmentEffects::Pipeline::Row(view->owner,entry.slot,index,entry.bytes,word,table,native,copy);
        if(entry.managed&&enabled.load()&&accepting.load()){
            const auto& p=entry.piece;const unsigned rank=p.mode==1?p.rank:p.mode==2?p.ranks[index]:0;
            if(rank){workshop::RefineAbilityRow(static_cast<std::uint16_t>(word),rank,copy,&view->catalog);changed=true;}
        }
        return changed?copy:native;
    }
    return native;
}
int RefineDamage(int damage,bool applied,bool magic,const void* status){
    if(!accepting.load()||!enabled.load()||!applied||damage<=0||!damageScope||damageScope->info!=status||damageScope->owner>=18||damageScope->owner==7)return damage;
    const unsigned owner=damageScope->owner;
    std::lock_guard<std::recursive_mutex> lock(mutex);if(!ready)return damage;
    unsigned best=0;for(const auto& p:state.pieces)if(p.id&&p.native[4]==owner&&p.native[6]==owner)
        for(unsigned i=0;i<4u+p.fifthUnlocked;++i)if(workshop::Ability(p,i)==(magic?0x8054:0x8055))best=(std::max)(best,unsigned(p.mode==1?p.rank:p.mode==2?p.ranks[i]:0));
    return static_cast<int>(static_cast<std::int64_t>(damage)*(100-best)/100);
}
using DamageFn=int(__cdecl*)(const void*,unsigned*,int*,const void*,int);
using DamageProducerFn=unsigned(__cdecl*)(unsigned,void*,unsigned,void*,const void*,unsigned,void*,unsigned,unsigned,unsigned,unsigned);
unsigned __cdecl DamageShim(unsigned user,void* userPtr,unsigned target,void* targetPtr,const void* command,unsigned commandId,void* info,unsigned a8,unsigned a9,unsigned a10,unsigned a11){
    // The real Protect/Shell calls consume argument7 (DamageInfo), not an actor
    // status pointer. Carry argument3's actor identity through this exact frame.
    DamageScope scope{};scope.info=info;
    std::uint32_t actors=0;std::uint16_t id=0xFFFF;
    if(target<18&&target!=7 && Copy(&actors,reinterpret_cast<void*>(module+0xD334CC),4) && actors &&
       Copy(&id,reinterpret_cast<void*>(actors+target*0xF90+0xE),2) && id==target)scope.owner=target;
    auto* previous=damageScope;damageScope=&scope;unsigned result=0;bool completed=false;
    CombatExtensions::DamageScope extension{};
    CombatExtensions::EnterDamage(extension,{user,userPtr,target,targetPtr,command,commandId,info,a8,a9,a10,a11},accepting.load());
    NativeGameplayEvents::Damage event{user,target,commandId,userPtr,targetPtr,extension.forwardCommand,info};
    auto ticket=NativeGameplayEvents::Begin({NativeGameplayEvents::Kind::Damage,user,&event,nullptr,sizeof(event)});
    __try{result=reinterpret_cast<DamageProducerFn>(originals[Damage])(user,userPtr,target,targetPtr,
        extension.forwardCommand,commandId,info,a8,a9,a10,a11);completed=true;}
    __finally{NativeGameplayEvents::End(ticket,completed);CombatExtensions::LeaveDamage(extension,result,completed);damageScope=previous;}
    return result;
}
int __cdecl ProtectShim(const void* command,unsigned* flags,int* divisor,const void* statuses,int damage){const int result=reinterpret_cast<DamageFn>(originals[Protect])(command,flags,divisor,statuses,damage);unsigned char type=0,active=0;Copy(&type,static_cast<const unsigned char*>(command)+0x20,1);Copy(&active,static_cast<const unsigned char*>(statuses)+0xB,1);return RefineDamage(result,(type&3)==1&&active!=0,false,statuses);}
int __cdecl ShellShim(const void* command,unsigned* flags,int* divisor,const void* statuses,int damage){const int result=reinterpret_cast<DamageFn>(originals[Shell])(command,flags,divisor,statuses,damage);unsigned char type=0,active=0;Copy(&type,static_cast<const unsigned char*>(command)+0x20,1);Copy(&active,static_cast<const unsigned char*>(statuses)+0xA,1);return RefineDamage(result,(type&3)==2&&active!=0,true,statuses);}
const SharedDamage::WorkshopCallbacks sharedDamageCallbacks{ProtectShim,ShellShim,DamageShim};
int __cdecl ContainsShim(const void* gear,unsigned ability){
    const int result=reinterpret_cast<int(__cdecl*)(const void*,unsigned)>(originals[Contains])(gear,ability);
    if(result||!accepting.load()||!enabled.load())return result;const auto slot=GearSlot(gear);if(slot>=200)return result;
    std::lock_guard<std::recursive_mutex> lock(mutex);const auto& p=state.pieces[slot];
    workshop::Catalog catalog{};(void)CatalogBridge::Read(catalog);
    return ready&&p.id&&p.fifthUnlocked&&workshop::SupportedFifth(p.fifth,&catalog)&&p.fifth==(ability&0xFFFF)&&std::memcmp(p.native,gear,22)==0?1:result;
}
bool PatchByte(std::uintptr_t address,unsigned char expected,unsigned char value){
    unsigned char current=0;if(!Copy(&current,reinterpret_cast<void*>(address),1)||current!=expected)return false;
    DWORD prior=0;if(!VirtualProtect(reinterpret_cast<void*>(address),1,PAGE_EXECUTE_READWRITE,&prior))return false;
    *reinterpret_cast<volatile unsigned char*>(address)=value;FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),1);
    DWORD ignored=0;return VirtualProtect(reinterpret_cast<void*>(address),1,prior,&ignored)!=FALSE;
}
bool Profile(std::uintptr_t base,bool inventory=true){
    unsigned char header[0x1000]{};F8Runtime::ExecutableIdentity identity{};
    if(!Copy(header,reinterpret_cast<void*>(base),sizeof(header))||F8Runtime::ParseExecutableIdentity(header,sizeof(header),&identity)!=F8Runtime::ProfileResult::Supported||!F8Runtime::IsSupportedExecutable(identity))return false;
    for(const auto& span:Evidence::spans){
        if(!inventory&&span.rva!=rvas[Load]&&span.rva!=rvas[Damage])continue;
        unsigned char bytes[32]{};
        if(!Copy(bytes,reinterpret_cast<void*>(base+span.rva),32)||
           (!Evidence::Matches(span,bytes,base)&&!SharedDamage::MatchesOwned(base,span.rva,bytes)))return false;
    }
    if(inventory)for(auto rva:{0x386786u,0x39C8A3u}){unsigned char bytes[5]{};const unsigned char expected[]={0xB9,4,0,0,0};if(!Copy(bytes,reinterpret_cast<void*>(base+rva),5)||std::memcmp(bytes,expected,5)!=0)return false;}
    return true;
}
std::wstring Directory(){
    HMODULE self=nullptr;wchar_t path[4096]{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Start),&self))return {};
    const auto n=GetModuleFileNameW(self,path,4096);if(!n||n>=4096)return {};
    std::wstring value(path,n);const auto slash=value.find_last_of(L"\\/");if(slash==std::wstring::npos)return {};
    return value.substr(0,slash)+L"\\config\\equipment-workshop-v1";
}
bool Begin(std::uintptr_t base,bool on,bool validateOnly,LogFn logger,const wchar_t* overridePath){
    if(started.load())return accepting.load();
    const auto directory=overridePath?std::wstring(overridePath):Directory();
    bool persisted=false;if(!directory.empty()){WIN32_FIND_DATAW found{};HANDLE find=FindFirstFileW((directory+L"\\*.bin").c_str(),&found);if(find!=INVALID_HANDLE_VALUE){persisted=true;FindClose(find);}}
    const bool inventory=(on||persisted)&&Coexistence::runtime.SavePipelineAllowed();
    const bool effects=EquipmentEffects::Pipeline::Required()||NativeGameplayEvents::Requested();
    const bool equipment=inventory||effects;
    if(!equipment&&!CombatExtensions::Required()){code=RuntimeCode::Disabled;return false;}
    if(validateOnly){code=RuntimeCode::Disabled;return false;}
    started=true;module=base;enabled=on&&inventory;logFn=logger;sharedOnly=!inventory;
    if(!Profile(base,equipment)){code=RuntimeCode::Unsupported;return false;}
    if(inventory&&!store.Initialize(directory,true)){code=RuntimeCode::StorageError;return false;}
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if(MinHookBatch::EnsureProcessInitialized()!=MinHookBatch::InitializationResult::Ready){code=RuntimeCode::Conflict;return false;}
    HMODULE pin=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Start),&pin)){code=RuntimeCode::Conflict;return false;}
    if(!SharedDamage::Start(base)){code=RuntimeCode::Conflict;return false;}
    originals[Protect]=SharedDamage::Original(SharedDamage::Protect);
    originals[Shell]=SharedDamage::Original(SharedDamage::Shell);
    originals[Damage]=SharedDamage::Original(SharedDamage::Damage);
    void* replacements[HookCount]={reinterpret_cast<void*>(&LoadShim),reinterpret_cast<void*>(&CreateShim),reinterpret_cast<void*>(&SwapShim),reinterpret_cast<void*>(&FreeShim),reinterpret_cast<void*>(&EquipShim),reinterpret_cast<void*>(&FieldShim),reinterpret_cast<void*>(&AggregateShim),reinterpret_cast<void*>(&ProtectShim),reinterpret_cast<void*>(&ShellShim),reinterpret_cast<void*>(&GearShim),reinterpret_cast<void*>(&RowShim),reinterpret_cast<void*>(&ContainsShim),reinterpret_cast<void*>(&DamageShim),reinterpret_cast<void*>(&LegendShim)};
    std::uintptr_t targets[HookCount]{};unsigned count=0;
    for(unsigned i=0;i<HookCount;++i){
        if(i==Protect||i==Shell||i==Damage)continue;
        if(!inventory&&i!=Load&&!(effects&&(i==Field||i==Aggregate||i==Gear||i==Row)))continue;
        const auto target=base+rvas[i];
        if(MH_CreateHook(reinterpret_cast<void*>(target),replacements[i],&originals[i])!=MH_OK)break;
        targets[count++]=target;
    }
    if(count!=(inventory?unsigned(HookCount)-3u:effects?5u:1u)){while(count)MH_RemoveHook(reinterpret_cast<void*>(targets[--count]));code=RuntimeCode::Conflict;return false;}
    const auto result=MinHookBatch::EnableBatch(&MinHookBatch::ProcessCoordinator(),MinHookBatch::RuntimeBatchIo(),MinHookBatch::Owner::EquipmentWorkshop,targets,count);
    if(result.result!=MinHookBatch::BatchResult::Applied){code=RuntimeCode::Conflict;return false;}
    // Helpers are installed before either loop can read its fifth word. Every
    // helper path supplies24 bytes, including OFF, unknown gear and stop paths.
    if(equipment&&(!PatchByte(base+0x386787,4,5)||!PatchByte(base+0x39C8A4,4,5))){code=RuntimeCode::Conflict;return false;}
    if(!SharedDamage::RegisterWorkshop(&sharedDamageCallbacks)){code=RuntimeCode::Conflict;return false;}
    NativeGameplayEvents::provider=NativeGameplayEvents::Provider::Workshop;
    accepting=true;code=inventory?RuntimeCode::WaitingForSave:RuntimeCode::Disabled;
    if(!inventory){Log("[ffx-hooks] Combat producer armed; Workshop inventory and persistence remain disabled\n");return true;}
    if(!NativeSaveEvents::Subscribe(&observer)){accepting=false;code=RuntimeCode::Conflict;return false;}
    Log("[ffx-hooks] Workshop: native inventory/effect hooks armed; waiting for a native save load\n");return true;
#else
    code=RuntimeCode::Unsupported;return false;
#endif
}
}

bool Start(std::uintptr_t base,bool on,bool validateOnly,LogFn logger){return Begin(base,on,validateOnly,logger,nullptr);}
void PrimeSaveIo(bool on,bool validateOnly){
    if(!Coexistence::runtime.SavePipelineAllowed())return;
    if(validateOnly)return;
    bool persisted=false;const auto directory=Directory();
    if(!directory.empty()){WIN32_FIND_DATAW found{};HANDLE file=FindFirstFileW((directory+L"\\*.bin").c_str(),&found);if(file!=INVALID_HANDLE_VALUE){persisted=true;FindClose(file);}}
    if(on||persisted)(void)NativeSaveEvents::Subscribe(&observer);
}
void RequestStop() noexcept {accepting=false;enabled=false;SharedDamage::UnregisterWorkshop(&sharedDamageCallbacks);NativeSaveEvents::Unsubscribe(&observer);code=RuntimeCode::Stopped;}
bool Requested(){return accepting.load()&&!sharedOnly.load();}
bool CombatProducerReady(){return accepting.load();}
RuntimeStatus Status(){std::lock_guard<std::recursive_mutex> lock(mutex);return {code.load(),enabled.load(),ownerThread,state.revision};}
const char* Detail(){switch(Status().code){case RuntimeCode::Disabled:return "Enable Equipment Workshop and restart";case RuntimeCode::WaitingForSave:return "Load a save to activate Equipment Workshop";case RuntimeCode::Ready:return "Equipment Workshop ready";case RuntimeCode::Unsupported:return "Unsupported game profile";case RuntimeCode::Conflict:return "Inventory changed outside the supported path; reload your save";case RuntimeCode::StorageError:return "Equipment extension storage unavailable";case RuntimeCode::Stopped:return "Equipment Workshop stopped";}return "Unavailable";}
bool Capture(workshop::State& out){
    std::lock_guard<std::recursive_mutex> lock(mutex);
    // 78CE... VA7816F1/782744 clear only this BYTE on battle exit.
    // Adjacent state is independent and may remain nonzero in the field.
    std::uint8_t battle=1;if(!OnOwner()||!enabled.load()||saveInFlight||!Copy(&battle,reinterpret_cast<void*>(module+0xD2A8E0),sizeof(battle))||battle||!Refresh())return false;
    out=state;return true;
}
void SetPresentationAdapter(PresentationAdapter adapter){presentationAdapter.store(adapter);}
bool ReadAeons(workshop::AeonProgress& out){
    std::lock_guard<std::recursive_mutex> lock(mutex);workshop::Economy economy{};
    if(!OnOwner()||!enabled.load()||ReadEconomy(economy)!=workshop::Error::Ok)return false;
    out=economy.aeons;return true;
}
bool ReadPresentation(const void* native,workshop::Piece& out){
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(!OnOwner()||!enabled.load())return false;
    const unsigned slot=GearSlot(native);if(slot>=200)return false;
    unsigned char actual[22]{};const auto& piece=state.pieces[slot];
    if(!piece.id||!Copy(actual,native,22)||std::memcmp(actual,piece.native,22))return false;
    out=piece;return true;
}
workshop::Error Access(){
    std::lock_guard<std::recursive_mutex> lock(mutex);workshop::State current{};
    if(!Capture(current))return workshop::Error::InvalidState;
    workshop::Economy economy{};const auto error=ReadEconomy(economy);if(error!=workshop::Error::Ok)return error;
    return economy.customizeUnlocked||economy.policy.devIgnoreProgression?workshop::Error::Ok:workshop::Error::Locked;
}
workshop::Error Preview(const workshop::Request& request,workshop::Plan& plan){
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(request.op==workshop::Op::Create||request.op==workshop::Op::Swap||request.op==workshop::Op::Retire)return workshop::Error::Protected;
    if(!Settings::AdmitsExpansion(request))return workshop::Error::InvalidPolicy;
    workshop::State current{};if(!Capture(current))return workshop::Error::InvalidState;
    workshop::Economy economy{};const auto error=ReadEconomy(economy);
    return error==workshop::Error::Ok?AeonAscension::PreviewGeneric(current,ascensionReceipts,ascensionSave,{},economy,request,plan):error;
}
namespace {
template<class Stage>
bool CommitInventory(const workshop::State& current,const workshop::Plan& plan,
                     const AeonAscension::Ledger* receipts,unsigned targetSlot,Stage stage){
    struct Write {std::uintptr_t address;unsigned size;unsigned char before[22],after[22];};
    std::vector<Write> writes;
    for(unsigned i=0;i<200;++i)if(std::memcmp(current.pieces[i].native,plan.after.pieces[i].native,22)!=0){Write w{};w.address=module+kGearRam+i*22;w.size=22;std::memcpy(w.before,current.pieces[i].native,22);std::memcpy(w.after,plan.after.pieces[i].native,22);writes.push_back(w);}
    unsigned char itemIds[512]{};if(!Copy(itemIds,reinterpret_cast<void*>(module+kSaveRam+0x3ECC),sizeof(itemIds)))return false;
    for(unsigned item=0;item<112;++item)if(current.items[item]!=plan.after.items[item]){
        unsigned slot=256;for(unsigned i=0;i<256;++i)if((itemIds[2*i]|(unsigned(itemIds[2*i+1])<<8))==0x2000+item){if(slot!=256)return false;slot=i;}
        if(slot==256)return false;Write w{};w.address=module+kSaveRam+0x40CC+slot;w.size=1;w.before[0]=static_cast<unsigned char>(current.items[item]);w.after[0]=static_cast<unsigned char>(plan.after.items[item]);writes.push_back(w);
    }
    if(plan.gilDebit){
        // Native Gil joins the same reviewed intent and rollback as inventory.
        const std::uint32_t before=plan.gilBefore,after=before-plan.gilDebit;
        Write w{};w.address=module+kGilRam;w.size=4;
        std::memcpy(w.before,&before,4);std::memcpy(w.after,&after,4);writes.push_back(w);
    }
    SaveImage beforeIntent{};bool intentReady=false;
    try{intentReady=NativeImage(beforeIntent)&&stage(beforeIntent);}
    catch(...){intentReady=false;}
    if(!intentReady){
        Fault(RuntimeCode::StorageError,"[ffx-hooks] Workshop: durable transaction intent failed; nothing was charged or changed\n");return false;
    }
    unsigned committed=0;bool ok=true;
    for(const auto& w:writes){
#ifdef FFXHOOKS_TESTING
        if(failBeforeWrite==static_cast<int>(committed)){failBeforeWrite=-1;ok=false;break;}
#endif
        unsigned char observed[22]{};
        if(!Copy(observed,reinterpret_cast<void*>(w.address),w.size)||std::memcmp(observed,w.before,w.size)!=0){ok=false;break;}
        // A failed copy may have written a prefix before its access violation.
        // Include the attempted record in rollback before issuing the first byte.
        ++committed;
        if(!Copy(reinterpret_cast<void*>(w.address),w.after,w.size)||
           !Copy(observed,reinterpret_cast<void*>(w.address),w.size)||std::memcmp(observed,w.after,w.size)!=0){ok=false;break;}
    }
    bool persistenceFailed=false;
    if(ok){
        try{
            SaveImage actual{},serialized{},projected{};NativeSaveEvents::CheckpointOwnership pool{};
            NativeSaveEvents::WriteTransaction projection;
            ok=NativeImage(actual)&&NativeSaveEvents::SerializeCheckpoint(actual.data(),serialized.data(),serialized.size(),&pool)&&
                NativeSaveEvents::ProjectWrite(loadedPath.c_str(),serialized.data(),projected.data(),projected.size(),projection);
            if(ok){
                if(serialized!=projected)RonsoPool::SealSave(projected);
                ok=NativeSaveEvents::PrepareWrite(loadedPath.c_str(),projected.data(),projected.size(),projection);
            }
            std::uint32_t paidGil=0;
            if(ok){std::memcpy(&paidGil,projected.data()+0x3D88,sizeof(paidGil));
                ok=paidGil==plan.gilBefore-plan.gilDebit&&store.PublishCheckpoint(loadedPath,loadedDisk,projected,pool,plan.after,checkpointHead,receipts);}
            NativeSaveEvents::FinishWrite(projection,projected.data(),projected.size(),ok);
        }catch(...){ok=false;}
        persistenceFailed=!ok;
    }
    if(!ok){
        while(committed){const auto& w=writes[--committed];
            for(unsigned i=0;i<w.size;++i){unsigned char actual=0;
                if(w.before[i]!=w.after[i]&&Copy(&actual,reinterpret_cast<void*>(w.address+i),1)&&actual==w.after[i])
                    Copy(reinterpret_cast<void*>(w.address+i),w.before+i,1);
            }
        }
        Fault(persistenceFailed?RuntimeCode::StorageError:RuntimeCode::Conflict,
            "[ffx-hooks] Workshop: transaction not accepted; owned-byte rollback attempted, admission closed\n");return false;
    }
    state=plan.after;
    if(receipts)ascensionReceipts=*receipts;
    if(targetSlot<workshop::GearCount&&workshop::IsAeon(state.pieces[targetSlot]))RefreshEquippedAeon(state.pieces[targetSlot].native[4]);
    Log(ready?"[ffx-hooks] Workshop: paid inventory, currency and RNG durably committed together\n":
              "[ffx-hooks] Workshop: paid checkpoint accepted; reload required to restore native presentation\n");return true;
}
} // namespace
bool Commit(const workshop::Request& request,const workshop::Plan& reviewed){
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(request.op==workshop::Op::Create||request.op==workshop::Op::Swap||request.op==workshop::Op::Retire)return false;
    if(!Settings::AdmitsExpansion(request))return false;
    workshop::State current{};if(!Capture(current))return false;
    workshop::Economy economy{};if(ReadEconomy(economy)!=workshop::Error::Ok)return false;
    workshop::Plan plan{};
    if(AeonAscension::PreviewGeneric(current,ascensionReceipts,ascensionSave,{},economy,request,plan)!=workshop::Error::Ok||
       std::memcmp(&plan,&reviewed,sizeof(plan)))return false;
    const auto* receipts=ReceiptSnapshot();
    return CommitInventory(current,plan,receipts,request.slot,[&](const SaveImage& before){
        return store.StageTransaction(loadedPath,loadedDisk,before,current,request,economy,plan,receipts);
    });
}
workshop::Error PreviewAscension(const AeonAscension::Request& request,AeonAscension::Plan& plan){
    std::lock_guard<std::recursive_mutex> lock(mutex);
    AeonAscension::Mapping mapping{};
    if(!AeonAscension::ReadMapping(mapping))return workshop::Error::UnsupportedAbility;
    workshop::State current{};if(!Capture(current))return workshop::Error::InvalidState;
    workshop::Economy economy{};const auto error=ReadEconomy(economy);if(error!=workshop::Error::Ok)return error;
    return AeonAscension::Preview(current,ascensionReceipts,ascensionSave,mapping,economy,request,plan);
}
bool CommitAscension(const AeonAscension::Request& request,const AeonAscension::Plan& reviewed){
    std::lock_guard<std::recursive_mutex> lock(mutex);
    AeonAscension::Mapping mapping{};if(!AeonAscension::ReadMapping(mapping))return false;
    workshop::State current{};if(!Capture(current))return false;
    workshop::Economy economy{};if(ReadEconomy(economy)!=workshop::Error::Ok)return false;
    AeonAscension::Plan plan{};
    if(AeonAscension::Preview(current,ascensionReceipts,ascensionSave,mapping,economy,request,plan)!=workshop::Error::Ok||
       std::memcmp(&plan,&reviewed,sizeof(plan)))return false;
    return CommitInventory(current,plan.inventory,&plan.receipts,request.slot,[&](const SaveImage& before){
        return store.StageAscensionTransaction(loadedPath,loadedDisk,before,current,ascensionReceipts,
            ascensionSave,mapping,economy,request,plan);
    });
}
bool AscensionEffect(unsigned owner,unsigned effect) noexcept {
    try{
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if(!OnOwner()||!enabled.load()||owner<8||owner>=18||effect>=2||!ascensionReceipts.count)return false;
        AeonAscension::Mapping mapping{};if(!AeonAscension::ReadMapping(mapping))return false;
        workshop::Economy economy{};if(ReadEconomy(economy)!=workshop::Error::Ok)return false;
        const unsigned slot=economy.aeons.gear[(owner-8)*2+(effect?0:1)];if(slot>=workshop::GearCount)return false;
        const auto& piece=state.pieces[slot];unsigned char native[22]{};
        if(piece.native[4]!=owner||workshop::AeonAccess(piece,slot,economy.aeons)!=workshop::Error::Ok||
           !Copy(native,reinterpret_cast<void*>(module+kGearRam+slot*22),22)||std::memcmp(native,piece.native,22))return false;
        for(unsigned position=0;position<5;++position)
            if(AeonAscension::Authorized(ascensionReceipts,ascensionSave,piece,position,effect,mapping))return true;
    }catch(...){return false;}
    return false;
}
#ifdef FFXHOOKS_TESTING
bool StartForTests(std::uintptr_t base,bool on,const wchar_t* directory,LogFn logger){return Begin(base,on,false,logger,directory);}
bool LoadForTests(const wchar_t* path,const SaveImage& disk,const SaveImage& loaded){
    SaveImage selected{};NativeSaveEvents::CheckpointSelection selection{};
    const auto result=store.SelectCheckpoint(path,disk,selected,selection);
    if(result!=StoreResult::Found&&result!=StoreResult::Missing)return false;
    if(result==StoreResult::Found){workshop::State verified{};Hash hash{};Fingerprint(disk.data(),disk.size(),hash);
        if(store.ReadCheckpointLoaded(path,hash,selection.proof,loaded,verified)!=StoreResult::Found)return false;}
    ReadAssociated(path,disk.data(),loaded.data(),loaded.size(),&selection);return !pending.empty();
}
bool CommitLoadForTests(const SaveImage& loaded){if(!PrepareLoad(loaded,loaded.data()))return false;if(!Copy(reinterpret_cast<void*>(module+kSaveRam),loaded.data()+64,0x68C0))return false;FinishLoad();return ready;}
bool WriteForTests(const wchar_t* path,const SaveImage& image){PrepareWriteEvent(path,image.data(),image.size());WriteEvent(path,image.data(),image.size());return code!=RuntimeCode::StorageError;}
void FailBeforeWriteForTests(unsigned index){std::lock_guard<std::recursive_mutex> lock(mutex);failBeforeWrite=static_cast<int>(index);}
void DamageProducerForTests(void* producer){
    if(producer&&accepting.load()){
        originals[Damage]=producer;
        // Test-owned endpoint remains behind the shared entry after Workshop
        // unregisters; synthetic pointers must never reach the real native body.
        SharedDamage::originals[SharedDamage::Damage]=producer;
    }
}
#endif
}
