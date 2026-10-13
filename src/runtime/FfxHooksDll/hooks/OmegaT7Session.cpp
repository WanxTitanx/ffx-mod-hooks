#include "OmegaT7Session.h"
#include "NativeSaveEvents.h"
#include "NativeUiHookSupport.h"
#include "EquipmentWorkshopStore.h"
#include "RonsoPoolStore.h"
#include "RonsoPoolSave.h"
#include "../shared/ExecutableProfile.h"
#include <bcrypt.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>
namespace FfxHooks::OmegaT7::Session {
namespace {
constexpr auto SaveRva=ExecutableProfile::Rva<0xD2CA90>();
std::uintptr_t image=0;std::atomic<bool> enabled{false},ready{false};std::atomic<const char*> detail{"Omega campaign is OFF"};
std::recursive_mutex mutex;std::atomic<DWORD> owner{0};Log logger=nullptr;Store store;Campaign campaign;Seed::Clock clock;
std::uint64_t generation=0,frozenGeneration=0,frozenSeed=0;bool frozen=false;
unsigned persistedElapsed=0;std::uint64_t persistedEpoch=0;
bool saturatedClock=false;unsigned syntheticClock=0;ULONGLONG lastWall=0;
struct Read {std::wstring path;Hash native{},payload{};std::uintptr_t buffer=0;};
struct Incoming {std::uint64_t cookie=0;bool valid=false;Read source;Reference reference;Campaign state;bool adopt=false;};
struct Write {std::uint64_t cookie=0;std::wstring path;Reference reference;};
std::vector<Read> reads;std::vector<Write> writes;thread_local Incoming incoming;
template<class T>bool Load(std::uintptr_t address,T& value) noexcept {return NativeUiSupport::Copy(&value,reinterpret_cast<void*>(address),sizeof(value));}
bool OnOwner() noexcept {return owner==GetCurrentThreadId();}
void Fail(const char* reason) noexcept {ready=false;detail=reason;if(logger)try{logger(reason);}catch(...) {}}
bool Fingerprint(const void* bytes,std::size_t size,Hash& hash){return EquipmentWorkshop::Fingerprint(bytes,size,hash);}
bool Payload(const unsigned char* bytes,std::size_t size,Hash& hash){
    if(!bytes||size!=RonsoPool::kSaveSize)return false;
    RonsoPool::SaveImage copy{};if(!NativeUiSupport::Copy(copy.data(),bytes,size))return false;
    std::fill(copy.begin()+25844,copy.begin()+25848,static_cast<unsigned char>(0));return Fingerprint(copy.data()+64,copy.size()-64,hash);
}
unsigned NativeSeconds() noexcept {unsigned time=0;return Load(image+SaveRva+0xBC,time)?time:UINT32_MAX;}
void AnchorClock() noexcept {
    const auto time=NativeSeconds();saturatedClock=time>=3599999u&&time!=UINT32_MAX;syntheticClock=0;lastWall=GetTickCount64();clock.Anchor(saturatedClock?0:time);
}
bool Persist() {
    if(campaign.revision==UINT64_MAX)return false;
    auto next=campaign;++next.revision;
    if(!store.SaveCampaign(next)){Fail("[ffx-hooks] Omega T7: campaign storage conflict; admission closed\n");return false;}
    campaign=next;persistedElapsed=campaign.seed.elapsed;persistedEpoch=campaign.seed.epoch;return true;
}
void Advance(bool boundary,bool loading=false) {
    if(!enabled||!ready||!OnOwner())return;
    auto seconds=NativeSeconds();if(seconds==UINT32_MAX||seconds>3599999u){Fail("[ffx-hooks] Omega T7: invalid native play clock\n");return;}
    const auto wall=GetTickCount64();
    if(seconds==3599999u){
        // The native display/save counter saturates at 999:59:59. At that
        // ceiling only, active gameplay callbacks continue its elapsed time.
        // Loading anchors this fallback rather than charging title-screen time.
        if(!saturatedClock){saturatedClock=true;syntheticClock=0;clock.Anchor(0);lastWall=wall;}
        if(loading)lastWall=wall;
        const auto delta=(wall-lastWall)/1000;
        if(delta){syntheticClock+=static_cast<unsigned>((std::min)(delta,ULONGLONG(UINT32_MAX-syntheticClock)));lastWall+=delta*1000;}
        seconds=syntheticClock;
    }else if(saturatedClock){saturatedClock=false;clock.Anchor(seconds);lastWall=wall;}
    unsigned char battle=1;if(!Load(image+ExecutableProfile::Rva<0xD2A8E0>(),battle))return;
    const bool rotated=clock.Tick(campaign.seed,seconds,!boundary&&(battle!=0||frozen));
    const unsigned delta=campaign.seed.elapsed>=persistedElapsed?campaign.seed.elapsed-persistedElapsed:0;
    if(rotated||campaign.seed.epoch!=persistedEpoch||delta>=15)(void)Persist();
}
void ReadEvent(const wchar_t* path,const unsigned char* disk,const unsigned char* loaded,std::size_t size) noexcept {
    if(!enabled||!path||size!=RonsoPool::kSaveSize)return;
    try{if(!RonsoPool::OwnerStore::IsSavePath(path))return;Read r;r.path=path;r.buffer=reinterpret_cast<std::uintptr_t>(loaded);
        if(!Fingerprint(disk,size,r.native)||!Payload(loaded,size,r.payload))return;
        std::lock_guard<std::recursive_mutex> lock(mutex);
        reads.erase(std::remove_if(reads.begin(),reads.end(),[&](const Read& v){return v.path==r.path||v.buffer==r.buffer;}),reads.end());
        if(reads.size()==64)reads.erase(reads.begin());reads.push_back(std::move(r));
    }catch(...){Fail("[ffx-hooks] Omega T7: save identity unavailable\n");}
}
void WriteEvent(const wchar_t*,const unsigned char*,std::size_t) noexcept {}
void Reject() noexcept {ready=false;incoming={};detail="Omega campaign load rejected";}
void Reset() noexcept {
    if(!enabled)return;
    try{std::lock_guard<std::recursive_mutex> lock(mutex);Hash id{};
        if(BCryptGenRandom(nullptr,id.data(),static_cast<ULONG>(id.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0){Reject();return;}
        campaign=AdoptCampaign(id);owner=GetCurrentThreadId();++generation;frozen=false;frozenGeneration=frozenSeed=0;reads.clear();incoming={};AnchorClock();
        ready=store.SaveCampaign(campaign);persistedElapsed=0;persistedEpoch=1;detail=ready?"Omega campaign ready":"Omega campaign storage unavailable";
    }catch(...){Reject();}
}
void LoadStarting(std::uint64_t cookie,void* destination,const void* source) noexcept {
    if(!enabled||destination!=reinterpret_cast<void*>(image+SaveRva))return;
    try{std::lock_guard<std::recursive_mutex> lock(mutex);
        if(ready&&OnOwner()){Advance(false,true);if(ready&&!Persist()){ready=false;incoming={};return;}}
        ready=false;incoming={};Hash payload{};
        if(!Payload(static_cast<const unsigned char*>(source),RonsoPool::kSaveSize,payload))return;
        Read selected;bool found=false;
        for(const auto& r:reads)if(r.payload==payload&&r.buffer==reinterpret_cast<std::uintptr_t>(source)){selected=r;found=true;break;}
        if(!found)for(const auto& r:reads)if(r.payload==payload){if(found&&selected.path!=r.path){detail="Ambiguous Omega save identity";return;}selected=r;found=true;}
        if(!found){detail="Omega campaign waiting for a verified save";return;}
        Reference reference;const auto code=store.ReadReference(selected.path,selected.native,reference);bool adopt=false;
        if(code==StoreCode::Missing){reference={selected.native,selected.native};adopt=true;}
        else if(code!=StoreCode::Found&&code!=StoreCode::Recovered){detail="Omega save reference conflict";return;}
        Campaign loaded;const auto campaignCode=store.ReadCampaign(reference.campaign,loaded);
        if(campaignCode==StoreCode::Missing&&adopt)loaded=AdoptCampaign(reference.campaign);
        else if(campaignCode!=StoreCode::Found){detail="Omega campaign state conflict";return;}
        incoming={cookie,true,selected,reference,loaded,adopt};
    }catch(...){Reject();}
}
void LoadCompleted(std::uint64_t cookie,bool complete) noexcept {
    if(!enabled||!incoming.valid||incoming.cookie!=cookie)return;
    try{std::lock_guard<std::recursive_mutex> lock(mutex);const auto loaded=incoming;incoming={};
        if(!complete){ready=false;return;}
        if(loaded.adopt&&(!store.SaveCampaign(loaded.state)||!store.AdoptReference(loaded.source.path,loaded.reference))){Fail("[ffx-hooks] Omega T7: campaign adoption failed\n");return;}
        campaign=loaded.state;owner=GetCurrentThreadId();++generation;frozen=false;frozenGeneration=frozenSeed=0;reads.clear();AnchorClock();
        persistedElapsed=campaign.seed.elapsed;persistedEpoch=campaign.seed.epoch;ready=true;detail="Omega campaign ready";
    }catch(...){Reject();}
}
void WriteStaged(std::uint64_t cookie,const wchar_t* path,const unsigned char* bytes,std::size_t size) noexcept {
    if(!enabled||!ready||!cookie||!path||size!=RonsoPool::kSaveSize)return;
    try{if(!RonsoPool::OwnerStore::IsSavePath(path))return;std::lock_guard<std::recursive_mutex> lock(mutex);
        if(OnOwner())Advance(false);
        if(!ready||!Persist()||writes.size()>=8)return;
        for(const auto& old:writes)if(old.cookie==cookie||old.path==path)return;
        Write w;w.cookie=cookie;w.path=path;w.reference.campaign=campaign.id;
        if(!Fingerprint(bytes,size,w.reference.native)||!store.PrepareReference(w.path,w.reference)){Fail("[ffx-hooks] Omega T7: save reference staging failed\n");return;}
        writes.push_back(std::move(w));
    }catch(...){Fail("[ffx-hooks] Omega T7: save reference staging exception\n");}
}
void WriteVerified(std::uint64_t cookie,const wchar_t* path,const unsigned char* bytes,std::size_t size) noexcept {
    if(!enabled||!cookie||!path||size!=RonsoPool::kSaveSize)return;
    try{std::lock_guard<std::recursive_mutex> lock(mutex);const auto found=std::find_if(writes.begin(),writes.end(),[&](const Write& w){return w.cookie==cookie;});if(found==writes.end())return;
        Hash hash{};const bool ok=found->path==path&&Fingerprint(bytes,size,hash)&&hash==found->reference.native&&store.CommitReference(found->path,hash);
        writes.erase(found);if(!ok)Fail("[ffx-hooks] Omega T7: verified save reference commit failed\n");
    }catch(...){Fail("[ffx-hooks] Omega T7: save reference verification exception\n");}
}
void WriteAborted(std::uint64_t cookie) noexcept {
    try{std::lock_guard<std::recursive_mutex> lock(mutex);const auto found=std::find_if(writes.begin(),writes.end(),[&](const Write& w){return w.cookie==cookie;});
        if(found!=writes.end()){store.AbortReference(found->path,found->reference);writes.erase(found);}
    }catch(...){}
}
const NativeSaveEvents::Observer observer=[](){NativeSaveEvents::Observer v{};v.read=ReadEvent;v.write=WriteEvent;v.reset=Reset;v.rejectRead=Reject;
    v.loadStarting=LoadStarting;v.loadCompleted=LoadCompleted;v.writeStaged=WriteStaged;v.writeVerified=WriteVerified;v.writeAborted=WriteAborted;return v;}();
}
void Prime(std::uintptr_t base,const std::filesystem::path& game,bool requested,bool validateOnly,Log log) noexcept {
    if(!requested||validateOnly||enabled)return;
    try{image=base;logger=log;store=Store(game/L"modules"/L"config"/L"omega-t7");
        if(!NativeSaveEvents::Subscribe(&observer)){detail="Omega native save producer unavailable";return;}
        enabled=true;detail="Omega campaign waiting for a native save";
    }catch(...){enabled=false;detail="Omega campaign initialization failed";}
}
void Stop() noexcept {enabled=false;ready=false;NativeSaveEvents::Unsubscribe(&observer);detail="Omega campaign is OFF";}
bool Ready() noexcept {return enabled.load()&&ready.load();}
bool Prepared() noexcept {return enabled.load();}
void Tick() noexcept {if(!Ready()||!OnOwner())return;try{std::lock_guard<std::recursive_mutex> lock(mutex);Advance(false);}catch(...){Reject();}}
std::uint64_t BattleSeed(std::uint64_t battle) noexcept {
    if(!Ready()||!OnOwner()||!battle)return 0;
    try{std::lock_guard<std::recursive_mutex> lock(mutex);if(frozen&&frozenGeneration==battle)return frozenSeed;
        Advance(true);if(!ready)return 0;frozen=true;frozenGeneration=battle;frozenSeed=campaign.seed.seed;return frozenSeed;
    }catch(...){Reject();return 0;}
}
bool Snapshot(Campaign& out,std::uint64_t& session) noexcept {
    if(!Ready()||!OnOwner())return false;try{std::lock_guard<std::recursive_mutex> lock(mutex);out=campaign;session=generation;return true;}catch(...){return false;}
}
bool SetLoreSeen(std::uint64_t session) noexcept {
    if(!Ready()||!OnOwner())return false;try{std::lock_guard<std::recursive_mutex> lock(mutex);if(session!=generation)return false;
        if(campaign.seed.loreSeen)return true;campaign.seed.loreSeen=true;return Persist();}catch(...){Reject();return false;}
}
bool ClearRenewal(std::uint64_t session,std::uint64_t epoch) noexcept {
    if(!Ready()||!OnOwner())return false;try{std::lock_guard<std::recursive_mutex> lock(mutex);if(session!=generation||epoch!=campaign.seed.epoch)return false;
        campaign.seed.renewalPending=false;return Persist();}catch(...){Reject();return false;}
}
void Field(bool stable) noexcept {if(stable&&Ready()&&OnOwner()){std::lock_guard<std::recursive_mutex> lock(mutex);frozen=false;}Tick();}
const char* Detail() noexcept {return detail.load();}
}
