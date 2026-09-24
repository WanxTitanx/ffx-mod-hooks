#include "EquipmentWorkshopRuntime.h"
#include "NativeSaveEvents.h"
#include "RonsoPoolStore.h"
#include "RonsoPoolSave.h"
#include "F8RuntimeCore.h"
#include "MinHookBatchCoordinator.h"
#include "EquipmentWorkshopEvidence.h"
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
#include <vector>

namespace FfxHooks::EquipmentWorkshop {
namespace {
constexpr std::uintptr_t kSaveRam=0xD2CA90,kGearRam=0xD30F2C;
enum Hook {Load,Create,Swap,Free,Equip,Field,Aggregate,Protect,Shell,Gear,Row,Contains,Damage,HookCount};
constexpr std::uint32_t rvas[HookCount]={0x4B5450,0x3AB930,0x3ABA10,0x3ABCC0,0x3AB990,0x3861B0,0x39C610,0x38AE00,0x38AE80,0x3ABBF0,0x3AB890,0x3A0C40,0x38E680};
void* originals[HookCount]{};
std::uintptr_t module=0;
std::atomic<bool> accepting{false},enabled{false},started{false};
std::recursive_mutex mutex;
workshop::State state{};
bool ready=false,loading=false;
unsigned ownerThread=0;
std::atomic<RuntimeCode> code{RuntimeCode::Disabled};
Store store;
LogFn logFn=nullptr;
struct Pending {Hash disk{},payload{};std::wstring path;std::uintptr_t buffer=0;};
std::vector<Pending> pending;
struct GearView {unsigned char bytes[24]{};workshop::Piece piece{};bool managed=false;};
struct ViewScope {unsigned owner=255,count=0,nextGear=0,nextAbility=0;GearView gear[2];unsigned char rows[2][5][108]{};};
thread_local ViewScope* view=nullptr;
thread_local unsigned char fallback[217][24]{};
struct DamageScope {unsigned owner=255;const void* info=nullptr;};
thread_local DamageScope* damageScope=nullptr;

bool Copy(void* to,const void* from,std::size_t size) noexcept {
    __try{std::memcpy(to,from,size);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void Log(const char* message){if(logFn)logFn(message);}
void Fault(RuntimeCode value,const char* message){ready=false;code=value;Log(message);}
bool NativeImage(SaveImage& image){
    image.fill(0);
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
void ReadEvent(const wchar_t* path,const unsigned char* disk,const unsigned char* loaded,std::size_t size) noexcept {
    if(!accepting.load() || !path || size!=kSaveBytes)return;
    try{
        if(!RonsoPool::OwnerStore::IsSavePath(path))return;
        Pending entry{};entry.path=path;entry.buffer=reinterpret_cast<std::uintptr_t>(loaded);
        if(!Fingerprint(disk,size,entry.disk)||!Fingerprint(loaded+64,size-64,entry.payload))return;
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
bool PrepareLoad(const SaveImage& image,const void* address){
    std::lock_guard<std::recursive_mutex> lock(mutex);
    ready=false;code=RuntimeCode::WaitingForSave;
    // Native load validation clears the payload CRC DWORD at file+0x64F4
    // before calling the load-copy producer. Restore that known transition in
    // a private copy only, after proving the header CRC against the payload.
    // All other bytes still have to match the observed completed file read.
    SaveImage canonical=image;
    if(!RonsoPool::IsValidSave(canonical)){
        const auto expected=static_cast<std::uint16_t>(canonical[26]|(unsigned(canonical[27])<<8));
        if(canonical[25844]||canonical[25845]||canonical[25846]||canonical[25847]||
           RonsoPool::SaveChecksum(canonical)!=expected){
            code=RuntimeCode::Conflict;
            Log("[ffx-hooks] Workshop: native load rejected; checksum transition is not verified\n");
            return false;
        }
        canonical[25844]=canonical[26];canonical[25845]=canonical[27];
    }
    Hash payload{};if(!Fingerprint(canonical.data()+64,canonical.size()-64,payload))return false;
    const Pending* selected=nullptr;
    for(const auto& item:pending)if(item.payload==payload && item.buffer==reinterpret_cast<std::uintptr_t>(address)){selected=&item;break;}
    if(!selected)for(const auto& item:pending)if(item.payload==payload){if(selected&&selected->path!=item.path){code=RuntimeCode::Conflict;return false;}selected=&item;}
    if(!selected){Log("[ffx-hooks] Workshop: native load has no matching completed save read\n");return false;}
    workshop::State next{};const auto found=store.ReadLoaded(selected->path,selected->disk,canonical,next);
    if(found==StoreResult::Missing){
        std::uint64_t seed=0;
        if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&seed),sizeof(seed),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0 || !ImportSave(canonical,seed,next)){code=RuntimeCode::Conflict;return false;}
    }else if(found!=StoreResult::Found){code=RuntimeCode::StorageError;return false;}
    // The persisted state is packed. Compare aligned value copies instead of
    // binding a uint64_t reference to its potentially unaligned revision field.
    const auto previousRevision=state.revision,nextRevision=next.revision;
    if(nextRevision>UINT64_MAX-1024 || previousRevision>UINT64_MAX-1024){code=RuntimeCode::Conflict;return false;}
    state=next;state.revision=(std::max)(previousRevision,nextRevision)+1;
    ownerThread=GetCurrentThreadId();ready=true;loading=true;code=RuntimeCode::Ready;return true;
}
void FinishLoad(){std::lock_guard<std::recursive_mutex> lock(mutex);loading=false;if(ready&&!Refresh())code=RuntimeCode::Conflict;if(ready)Log("[ffx-hooks] Workshop: native save load verified; inventory ready\n");}
void WriteEvent(const wchar_t* path,const unsigned char* actual,std::size_t size) noexcept {
    if(!accepting.load() || !path || size!=kSaveBytes)return;
    try{
        SaveImage image{};if(!Copy(image.data(),actual,size))return;
        workshop::State saved{};
        {std::lock_guard<std::recursive_mutex> lock(mutex);if(!ready)return;saved=state;}
        workshop::State native{};if(!ImportSave(image,0,native))return;
        // Ordinary item use/loot is native-owned. Save the actual quantities;
        // equipment identity must still match every recorded native byte.
        std::memcpy(saved.items,native.items,sizeof(saved.items));
        if(!MatchesSave(image,saved)||!store.Write(path,image,saved)){
            std::lock_guard<std::recursive_mutex> lock(mutex);Fault(RuntimeCode::StorageError,"[ffx-hooks] Workshop: extension save failed; native save was not retried or replaced\n");
        }
    }catch(...){std::lock_guard<std::recursive_mutex> lock(mutex);Fault(RuntimeCode::StorageError,"[ffx-hooks] Workshop: extension save exception; native bytes preserved\n");}
}
void ResetEvent() noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(!loading){const bool wasReady=ready;ready=false;ownerThread=0;code=RuntimeCode::WaitingForSave;if(wasReady)Log("[ffx-hooks] Workshop: native reset retired the loaded inventory\n");}
}
const NativeSaveEvents::Observer observer{ReadEvent,WriteEvent,ResetEvent};

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
    if(!tracked.Begin(true,true,state)||!tracked.Observe(event,first,second,owner,before.revision,before.gear,after)||!tracked.Snapshot(state))
        Fault(RuntimeCode::Conflict,"[ffx-hooks] Workshop: inventory event did not match its native result\n");
}
using LoadFn=int(__cdecl*)(void*,const void*);
int __cdecl LoadShim(void* destination,const void* source){
    SaveImage image{};const bool candidate=accepting.load()&&destination==reinterpret_cast<void*>(module+kSaveRam)&&Copy(image.data(),source,image.size());
    if(candidate)(void)PrepareLoad(image,source);
    const int result=reinterpret_cast<LoadFn>(originals[Load])(destination,source);
    if(candidate)FinishLoad();return result;
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
    ViewScope scope{};scope.owner=owner;auto* previous=view;view=&scope;int result=0;
    __try{result=reinterpret_cast<int(__cdecl*)(unsigned)>(originals[hook])(owner);}
    __finally{view=previous;}
    return result;
}
int __cdecl FieldShim(unsigned owner){return Produce(owner,Field);}
int __cdecl AggregateShim(unsigned owner){return Produce(owner,Aggregate);}
const unsigned char* __cdecl GearShim(unsigned id,void* unknown){
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-module;
    const auto* native=reinterpret_cast<const unsigned char*(__cdecl*)(unsigned,void*)>(originals[Gear])(id,unknown);
    if(caller!=0x38677B && caller!=0x39C782)return native;
    GearView* entry=nullptr;
    if(view && view->count<2)entry=&view->gear[view->count++];
    if(!native)return nullptr;
    unsigned slot=GearSlot(native),fallbackSlot=slot;
    if(slot==200){const unsigned family=id>>12,index=id&0xFFF;fallbackSlot=family==7?200+(index<8?index:0):family==11?208+(index<8?index:0):216;}
    auto* bytes=entry?entry->bytes:fallback[fallbackSlot];
    if(!Copy(bytes,native,22))return nullptr;bytes[22]=255;bytes[23]=0;
    if(entry && enabled.load() && accepting.load() && slot<200){
        std::lock_guard<std::recursive_mutex> lock(mutex);
        const auto& piece=state.pieces[slot];
        if(ready&&ownerThread==GetCurrentThreadId()&&piece.id&&piece.native[4]==view->owner&&piece.native[6]==view->owner&&std::memcmp(piece.native,native,22)==0){
            entry->piece=piece;entry->managed=true;
            if(piece.fifthUnlocked){bytes[22]=static_cast<unsigned char>(piece.fifth);bytes[23]=static_cast<unsigned char>(piece.fifth>>8);}
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
        if(!entry.managed || !enabled.load() || !accepting.load())return native;
        auto* copy=view->rows[view->nextGear][index];if(!Copy(copy,native,108))return native;
        const auto& p=entry.piece;const unsigned rank=p.mode==1?p.rank:p.mode==2?p.ranks[index]:0;
        if(id>=98&&id<=121)copy[0x55]=static_cast<unsigned char>((std::min)(255u,unsigned(copy[0x55])+rank));
        return copy;
    }
    return native;
}
int RefineDamage(int damage,bool applied,bool magic,const void* status){
    if(!accepting.load()||!enabled.load()||!applied||damage<=0||!damageScope||damageScope->info!=status||damageScope->owner>=7)return damage;
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
    if(target<7 && Copy(&actors,reinterpret_cast<void*>(module+0xD334CC),4) && actors &&
       Copy(&id,reinterpret_cast<void*>(actors+target*0xF90+0xE),2) && id==target)scope.owner=target;
    auto* previous=damageScope;damageScope=&scope;unsigned result=0;
    __try{result=reinterpret_cast<DamageProducerFn>(originals[Damage])(user,userPtr,target,targetPtr,command,commandId,info,a8,a9,a10,a11);}
    __finally{damageScope=previous;}
    return result;
}
int __cdecl ProtectShim(const void* command,unsigned* flags,int* divisor,const void* statuses,int damage){const int result=reinterpret_cast<DamageFn>(originals[Protect])(command,flags,divisor,statuses,damage);unsigned char type=0,active=0;Copy(&type,static_cast<const unsigned char*>(command)+0x20,1);Copy(&active,static_cast<const unsigned char*>(statuses)+0xB,1);return RefineDamage(result,(type&3)==1&&active!=0,false,statuses);}
int __cdecl ShellShim(const void* command,unsigned* flags,int* divisor,const void* statuses,int damage){const int result=reinterpret_cast<DamageFn>(originals[Shell])(command,flags,divisor,statuses,damage);unsigned char type=0,active=0;Copy(&type,static_cast<const unsigned char*>(command)+0x20,1);Copy(&active,static_cast<const unsigned char*>(statuses)+0xA,1);return RefineDamage(result,(type&3)==2&&active!=0,true,statuses);}
int __cdecl ContainsShim(const void* gear,unsigned ability){
    const int result=reinterpret_cast<int(__cdecl*)(const void*,unsigned)>(originals[Contains])(gear,ability);
    if(result||!accepting.load()||!enabled.load())return result;const auto slot=GearSlot(gear);if(slot>=200)return result;
    std::lock_guard<std::recursive_mutex> lock(mutex);const auto& p=state.pieces[slot];
    return ready&&p.id&&p.fifthUnlocked&&p.fifth==(ability&0xFFFF)&&std::memcmp(p.native,gear,22)==0?1:result;
}
bool PatchByte(std::uintptr_t address,unsigned char expected,unsigned char value){
    unsigned char current=0;if(!Copy(&current,reinterpret_cast<void*>(address),1)||current!=expected)return false;
    DWORD prior=0;if(!VirtualProtect(reinterpret_cast<void*>(address),1,PAGE_EXECUTE_READWRITE,&prior))return false;
    *reinterpret_cast<volatile unsigned char*>(address)=value;FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),1);
    DWORD ignored=0;return VirtualProtect(reinterpret_cast<void*>(address),1,prior,&ignored)!=FALSE;
}
bool Profile(std::uintptr_t base){
    unsigned char header[0x1000]{};F8Runtime::ExecutableIdentity identity{};
    if(!Copy(header,reinterpret_cast<void*>(base),sizeof(header))||F8Runtime::ParseExecutableIdentity(header,sizeof(header),&identity)!=F8Runtime::ProfileResult::Supported||!F8Runtime::IsSupportedExecutable(identity))return false;
    for(const auto& span:Evidence::spans){unsigned char bytes[32]{};if(!Copy(bytes,reinterpret_cast<void*>(base+span.rva),32)||!Evidence::Matches(span,bytes,base))return false;}
    for(auto rva:{0x386786u,0x39C8A3u}){unsigned char bytes[5]{};const unsigned char expected[]={0xB9,4,0,0,0};if(!Copy(bytes,reinterpret_cast<void*>(base+rva),5)||std::memcmp(bytes,expected,5)!=0)return false;}
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
    if(!on&&!persisted){code=RuntimeCode::Disabled;return false;}
    if(validateOnly){code=RuntimeCode::Disabled;return false;}
    started=true;module=base;enabled=on;logFn=logger;
    if(!Profile(base)){code=RuntimeCode::Unsupported;return false;}
    if(!store.Initialize(directory,true)){code=RuntimeCode::StorageError;return false;}
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if(MinHookBatch::EnsureProcessInitialized()!=MinHookBatch::InitializationResult::Ready){code=RuntimeCode::Conflict;return false;}
    HMODULE pin=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Start),&pin)){code=RuntimeCode::Conflict;return false;}
    void* replacements[HookCount]={reinterpret_cast<void*>(&LoadShim),reinterpret_cast<void*>(&CreateShim),reinterpret_cast<void*>(&SwapShim),reinterpret_cast<void*>(&FreeShim),reinterpret_cast<void*>(&EquipShim),reinterpret_cast<void*>(&FieldShim),reinterpret_cast<void*>(&AggregateShim),reinterpret_cast<void*>(&ProtectShim),reinterpret_cast<void*>(&ShellShim),reinterpret_cast<void*>(&GearShim),reinterpret_cast<void*>(&RowShim),reinterpret_cast<void*>(&ContainsShim),reinterpret_cast<void*>(&DamageShim)};
    std::uintptr_t targets[HookCount]{};unsigned count=0;
    for(;count<HookCount;++count){targets[count]=base+rvas[count];if(MH_CreateHook(reinterpret_cast<void*>(targets[count]),replacements[count],&originals[count])!=MH_OK)break;}
    if(count!=HookCount){while(count)MH_RemoveHook(reinterpret_cast<void*>(targets[--count]));code=RuntimeCode::Conflict;return false;}
    const auto result=MinHookBatch::EnableBatch(&MinHookBatch::ProcessCoordinator(),MinHookBatch::RuntimeBatchIo(),MinHookBatch::Owner::EquipmentWorkshop,targets,HookCount);
    if(result.result!=MinHookBatch::BatchResult::Applied){code=RuntimeCode::Conflict;return false;}
    // Helpers are installed before either loop can read its fifth word. Every
    // helper path supplies24 bytes, including OFF, unknown gear and stop paths.
    if(!PatchByte(base+0x386787,4,5)||!PatchByte(base+0x39C8A4,4,5)){code=RuntimeCode::Conflict;return false;}
    accepting=true;code=RuntimeCode::WaitingForSave;
    if(!NativeSaveEvents::Subscribe(&observer)){accepting=false;code=RuntimeCode::Conflict;return false;}
    Log("[ffx-hooks] Workshop: native inventory/effect hooks armed; waiting for a native save load\n");return true;
#else
    code=RuntimeCode::Unsupported;return false;
#endif
}
}

bool Start(std::uintptr_t base,bool on,bool validateOnly,LogFn logger){return Begin(base,on,validateOnly,logger,nullptr);}
void PrimeSaveIo(bool on,bool validateOnly){
    if(validateOnly)return;
    bool persisted=false;const auto directory=Directory();
    if(!directory.empty()){WIN32_FIND_DATAW found{};HANDLE file=FindFirstFileW((directory+L"\\*.bin").c_str(),&found);if(file!=INVALID_HANDLE_VALUE){persisted=true;FindClose(file);}}
    if(on||persisted)(void)NativeSaveEvents::Subscribe(&observer);
}
void RequestStop() noexcept {accepting=false;enabled=false;NativeSaveEvents::Unsubscribe(&observer);code=RuntimeCode::Stopped;}
bool Requested(){return accepting.load();}
RuntimeStatus Status(){std::lock_guard<std::recursive_mutex> lock(mutex);return {code.load(),enabled.load(),ownerThread,state.revision};}
const char* Detail(){switch(Status().code){case RuntimeCode::Disabled:return "Enable Equipment Workshop and restart";case RuntimeCode::WaitingForSave:return "Load a save to activate Equipment Workshop";case RuntimeCode::Ready:return "Equipment Workshop ready";case RuntimeCode::Unsupported:return "Unsupported game profile";case RuntimeCode::Conflict:return "Inventory changed outside the supported path; reload your save";case RuntimeCode::StorageError:return "Equipment extension storage unavailable";case RuntimeCode::Stopped:return "Equipment Workshop stopped";}return "Unavailable";}
bool Capture(workshop::State& out){
    std::lock_guard<std::recursive_mutex> lock(mutex);
    unsigned battle=1;if(!OnOwner()||!enabled.load()||!Copy(&battle,reinterpret_cast<void*>(module+0xD2A8E0),4)||battle||!Refresh())return false;
    out=state;return true;
}
workshop::Error Preview(const workshop::Request& request,workshop::Plan& plan){workshop::State current{};if(!Capture(current))return workshop::Error::InvalidState;return workshop::Preview(current,request,plan);}
bool Commit(const workshop::Request& request,const workshop::Plan& reviewed){
    std::lock_guard<std::recursive_mutex> lock(mutex);
    workshop::State current{};if(!Capture(current))return false;
    workshop::Plan plan{};if(workshop::Preview(current,request,plan)!=workshop::Error::Ok||std::memcmp(&plan,&reviewed,sizeof(plan))!=0)return false;
    struct Write {std::uintptr_t address;unsigned size;unsigned char before[22],after[22];};
    std::vector<Write> writes;
    for(unsigned i=0;i<200;++i)if(std::memcmp(current.pieces[i].native,plan.after.pieces[i].native,22)!=0){Write w{};w.address=module+kGearRam+i*22;w.size=22;std::memcpy(w.before,current.pieces[i].native,22);std::memcpy(w.after,plan.after.pieces[i].native,22);writes.push_back(w);}
    unsigned char itemIds[512]{};if(!Copy(itemIds,reinterpret_cast<void*>(module+kSaveRam+0x3ECC),sizeof(itemIds)))return false;
    for(unsigned item=0;item<112;++item)if(current.items[item]!=plan.after.items[item]){
        unsigned slot=256;for(unsigned i=0;i<256;++i)if((itemIds[2*i]|(unsigned(itemIds[2*i+1])<<8))==0x2000+item){if(slot!=256)return false;slot=i;}
        if(slot==256)return false;Write w{};w.address=module+kSaveRam+0x40CC+slot;w.size=1;w.before[0]=static_cast<unsigned char>(current.items[item]);w.after[0]=static_cast<unsigned char>(plan.after.items[item]);writes.push_back(w);
    }
    unsigned committed=0;bool ok=true;
    for(const auto& w:writes){unsigned char observed[22]{};if(!Copy(observed,reinterpret_cast<void*>(w.address),w.size)||std::memcmp(observed,w.before,w.size)!=0||!Copy(reinterpret_cast<void*>(w.address),w.after,w.size)){ok=false;break;}++committed;if(!Copy(observed,reinterpret_cast<void*>(w.address),w.size)||std::memcmp(observed,w.after,w.size)!=0){ok=false;break;}}
    if(!ok){while(committed){const auto& w=writes[--committed];unsigned char observed[22]{};if(Copy(observed,reinterpret_cast<void*>(w.address),w.size)&&std::memcmp(observed,w.after,w.size)==0)Copy(reinterpret_cast<void*>(w.address),w.before,w.size);}Fault(RuntimeCode::Conflict,"[ffx-hooks] Workshop: transaction readback failed; only owned bytes were rolled back\n");return false;}
    state=plan.after;Log("[ffx-hooks] Workshop: reviewed inventory transaction committed; save normally to persist\n");return true;
}
#ifdef FFXHOOKS_TESTING
bool StartForTests(std::uintptr_t base,bool on,const wchar_t* directory,LogFn logger){return Begin(base,on,false,logger,directory);}
bool LoadForTests(const wchar_t* path,const SaveImage& disk,const SaveImage& loaded){ReadEvent(path,disk.data(),loaded.data(),loaded.size());return !pending.empty();}
bool CommitLoadForTests(const SaveImage& loaded){if(!PrepareLoad(loaded,loaded.data()))return false;if(!Copy(reinterpret_cast<void*>(module+kSaveRam),loaded.data()+64,0x68C0))return false;FinishLoad();return ready;}
bool WriteForTests(const wchar_t* path,const SaveImage& image){WriteEvent(path,image.data(),image.size());return code!=RuntimeCode::StorageError;}
#endif
}
