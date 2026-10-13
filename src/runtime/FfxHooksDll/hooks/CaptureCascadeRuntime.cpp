#include "CaptureCascadeRuntime.h"
#include "CaptureCascadeStore.h"
#include "CaptureCascadeCatalog.generated.h"
#include "CaptureCascadeRooms.generated.h"
#include "CaptureCascadeText.h"
#include "CaptureCascadeBridge.h"
#include "CaptureCascadeBattle.h"
#include "ArcanaFieldDialogCore.h"
#include "NativeSaveEvents.h"
#include "NativeUiHookSupport.h"
#include "EquipmentWorkshopStore.h"
#include "RonsoPoolSave.h"
#include "RonsoPoolStore.h"
#include "UiLanguageSettings.h"
#include "TextLanguageHook.h"
#include "../shared/ExecutableProfile.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace FfxHooks::CaptureCascade::Runtime {
namespace {
static_assert(std::atomic<bool>::is_always_lock_free&&std::atomic<Code>::is_always_lock_free,"Loader-lock teardown uses only lock-free admission stores");
constexpr auto saveRva=ExecutableProfile::Rva<0xD2CA90>();
std::uintptr_t base=0;
std::atomic<bool> enabled{false},ready{false};
std::atomic<bool> cascade{false};
std::atomic<Code> code{Code::Off};
std::recursive_mutex mutex;
State state;
Store store;
std::uint64_t generation=0;
DWORD owner=0;
Aeon region=Aeon::Count;
Hash nativeHash{};
void(*logger)(const char*)=nullptr;
struct Read {std::wstring path;Hash native{},payload{};std::uintptr_t buffer=0;};
std::vector<Read> reads;
struct Incoming {std::uint64_t cookie=0;bool valid=false;Record record;};
thread_local Incoming incoming;
struct Write {std::uint64_t cookie=0,generation=0;std::wstring path;Record record;};
std::vector<Write> writes;
struct Natural {bool armed=false;DWORD thread=0;std::uint64_t generation=0;SinNatural::Evidence evidence;std::uint32_t entropy=0,request=0;Bridge::CurrentRequest current=nullptr;} natural;
bool Copy(void* out,const void* in,std::size_t size) noexcept {return NativeUiSupport::Copy(out,in,size);}
bool Fingerprint(const void* data,std::size_t size,Hash& out){return EquipmentWorkshop::Fingerprint(data,size,out);}
bool Payload(const unsigned char* data,std::size_t size,Hash& out){
    if(!data||size!=RonsoPool::kSaveSize)return false;
    RonsoPool::SaveImage copy{};if(!Copy(copy.data(),data,size))return false;
    std::fill(copy.begin()+25844,copy.begin()+25848,static_cast<unsigned char>(0));
    return Fingerprint(copy.data()+64,copy.size()-64,out);
}
std::uint8_t Flags(const unsigned char* payload) noexcept {
    std::uint8_t result=0;for(unsigned i=0;i<8;++i)if(payload[0xC89+i]&0x80)result|=static_cast<std::uint8_t>(1u<<i);return result;
}
bool OnOwner() noexcept {return owner==GetCurrentThreadId();}
void Log(const char* text) noexcept {if(logger)try{logger(text);}catch(...){}}
Stamp Now() noexcept {
    Stamp stamp;FILETIME time{};GetSystemTimeAsFileTime(&time);
    const auto value=(std::uint64_t(time.dwHighDateTime)<<32)|time.dwLowDateTime;
    stamp.utc=value>=116444736000000000ULL?(value-116444736000000000ULL)/10000000ULL:0;
    std::snprintf(stamp.build.data(),stamp.build.size(),"cascade1 " __DATE__ " " __TIME__);
    return stamp;
}
void ReadEvent(const wchar_t* path,const unsigned char* disk,const unsigned char* loaded,std::size_t size) noexcept {
    if(!enabled||!path||size!=RonsoPool::kSaveSize)return;
    try{
        if(!RonsoPool::OwnerStore::IsSavePath(path))return;
        Read entry;entry.path=path;entry.buffer=reinterpret_cast<std::uintptr_t>(loaded);
        if(!Fingerprint(disk,size,entry.native)||!Payload(loaded,size,entry.payload))return;
        std::lock_guard<std::recursive_mutex> lock(mutex);
        reads.erase(std::remove_if(reads.begin(),reads.end(),[&](const Read& r){return r.path==entry.path||r.buffer==entry.buffer;}),reads.end());
        if(reads.size()==64)reads.erase(reads.begin());
        reads.push_back(std::move(entry));
    }catch(...){ready=false;code=Code::StorageError;}
}
void WriteEvent(const wchar_t*,const unsigned char*,std::size_t) noexcept {}
void RejectRead() noexcept {ready=false;code=Code::Conflict;incoming={};}
void Reset() noexcept {
    if(!enabled)return;
    try{std::lock_guard<std::recursive_mutex> lock(mutex);ready=false;state=Adopt(0);nativeHash={};reads.clear();natural={};incoming={};
        region=Aeon::Count;++generation;owner=GetCurrentThreadId();ready=true;code=Code::Ready;
    }catch(...){ready=false;code=Code::Conflict;}
}
void LoadStarting(std::uint64_t cookie,void* destination,const void* source) noexcept {
    if(!enabled||destination!=reinterpret_cast<void*>(base+saveRva))return;
    ready=false;incoming={};
    try{
        Hash hash{};if(!Payload(static_cast<const unsigned char*>(source),RonsoPool::kSaveSize,hash))return;
        Read selected;bool found=false;
        std::lock_guard<std::recursive_mutex> lock(mutex);natural={};
        for(const auto& candidate:reads)if(candidate.payload==hash&&candidate.buffer==reinterpret_cast<std::uintptr_t>(source)){selected=candidate;found=true;break;}
        if(!found)for(const auto& candidate:reads)if(candidate.payload==hash){
            if(found&&selected.path!=candidate.path){code=Code::Conflict;return;}
            selected=candidate;found=true;
        }
        if(!found){code=Code::Waiting;return;}
        Record record;const auto result=store.Read(selected.path,selected.native,record);
        if(result==StoreCode::Missing){
            std::array<unsigned char,0xC91> payload{};
            if(!Copy(payload.data(),static_cast<const unsigned char*>(source)+64,payload.size()))return;
            record.nativeHash=selected.native;record.state=Adopt(Flags(payload.data()));
        }else if(result!=StoreCode::Found&&result!=StoreCode::Recovered){code=result==StoreCode::Foreign?Code::Conflict:Code::StorageError;return;}
        incoming={cookie,true,record};
    }catch(...){code=Code::StorageError;}
}
void LoadCompleted(std::uint64_t cookie,bool complete) noexcept {
    if(!enabled||!incoming.valid||incoming.cookie!=cookie)return;
    try{
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if(!complete){incoming={};ready=false;return;}
        state=incoming.record.state;nativeHash=incoming.record.nativeHash;
        incoming={};reads.clear();region=Aeon::Count;++generation;owner=GetCurrentThreadId();ready=true;code=Code::Ready;
        Log("[ffx-hooks] Capture Cascade: native save session bound; legacy victories do not awaken old regions\n");
    }catch(...){ready=false;code=Code::Conflict;}
}
void WriteStaged(std::uint64_t cookie,const wchar_t* path,const unsigned char* bytes,std::size_t size) noexcept {
    if(!enabled||!ready||!cookie||!path||size!=RonsoPool::kSaveSize)return;
    try{
        if(!RonsoPool::OwnerStore::IsSavePath(path))return;
        std::lock_guard<std::recursive_mutex> lock(mutex);
        std::array<unsigned char,0xC91> saved{};
        if(!Copy(saved.data(),bytes+64,saved.size())||(state.nativeSeen&~Flags(saved.data()))){code=Code::Conflict;return;}
        if(writes.size()>=8){code=Code::StorageError;return;}
        for(const auto& old:writes)if(old.cookie==cookie||old.path==path){code=Code::Conflict;return;}
        Write write;write.cookie=cookie;write.generation=generation;write.path=path;write.record.state=state;
        if(!Fingerprint(bytes,size,write.record.nativeHash)||!store.Prepare(write.path,write.record)){
            code=Code::StorageError;Log("[ffx-hooks] Capture Cascade: sidecar preparation failed; native save is not modified\n");return;
        }
        writes.push_back(std::move(write));
    }catch(...){code=Code::StorageError;}
}
void WriteVerified(std::uint64_t cookie,const wchar_t* path,const unsigned char* bytes,std::size_t size) noexcept {
    if(!enabled||!cookie||!path||size!=RonsoPool::kSaveSize)return;
    try{
        std::lock_guard<std::recursive_mutex> lock(mutex);
        const auto entry=std::find_if(writes.begin(),writes.end(),[&](const Write& w){return w.cookie==cookie;});
        if(entry==writes.end())return;
        Hash hash{};
        const bool ok=entry->path==path&&Fingerprint(bytes,size,hash)&&hash==entry->record.nativeHash&&store.Commit(entry->path,hash);
        if(entry->generation==generation){if(ok){nativeHash=hash;code=Code::Ready;}else code=Code::StorageError;}
        if(!ok)Log("[ffx-hooks] Capture Cascade: verified save sidecar commit failed; pending recovery retained\n");
        writes.erase(entry);
    }catch(...){code=Code::StorageError;}
}
void WriteAborted(std::uint64_t cookie) noexcept {
    try{
        std::lock_guard<std::recursive_mutex> lock(mutex);
        const auto entry=std::find_if(writes.begin(),writes.end(),[&](const Write& w){return w.cookie==cookie;});
        if(entry!=writes.end()){store.Abort(entry->path,entry->record);writes.erase(entry);}
    }catch(...){code=Code::StorageError;}
}
const NativeSaveEvents::Observer observer=[](){
    NativeSaveEvents::Observer value{};value.read=ReadEvent;value.write=WriteEvent;value.reset=Reset;value.rejectRead=RejectRead;
    value.writeStaged=WriteStaged;value.writeVerified=WriteVerified;value.writeAborted=WriteAborted;
    value.loadStarting=LoadStarting;value.loadCompleted=LoadCompleted;return value;
}();
const Bridge::Provider provider{ObserveNatural,EffectiveSin,Battle::Run};
CaptureCascade::View FromNative(const Arcana::Presentation::View& view) noexcept {
    if(view.kind!=Arcana::Presentation::Kind::System)return {};
    if(view.card==0)return {Tutorial::Sin,view.page,kNoFamily,view.revision};
    if(view.card==1)return {Tutorial::Cascade,view.page,state.first,view.revision};
    if(view.card>=2&&view.card<10)return {Tutorial::Region,view.page,static_cast<std::uint8_t>(view.card-2),view.revision};
    return {};
}
}
void Prime(std::uintptr_t module,bool requested,bool validateOnly,void(*log)(const char*),bool awaken) noexcept {
    if(!requested||validateOnly)return;
    try{
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if(enabled)return;
        if(!Arcana::FieldPresentation::Profile(module)){code=Code::Unsupported;return;}
        base=module;logger=log;
        if(!NativeSaveEvents::Subscribe(&observer)||!Bridge::Register(&provider)){
            NativeSaveEvents::Unsubscribe(&observer);code=Code::Unsupported;return;
        }
        cascade=awaken;enabled=true;code=Code::Waiting;
    }catch(...){enabled=false;code=Code::Unsupported;}
}
void Stop() noexcept {enabled=false;ready=false;code=Code::Stopped;Bridge::Unregister(&provider);NativeSaveEvents::Unsubscribe(&observer);}
bool Requested() noexcept {return enabled.load()&&cascade.load();}
bool TutorialsRequested() noexcept {return enabled.load();}
bool Ready() noexcept {return enabled.load()&&ready.load();}
Code Status() noexcept {return code.load();}
const char* Detail() noexcept {
    switch(code.load()){
    case Code::Off:return "Capture Cascade OFF - enable and restart";
    case Code::Waiting:return "Load a save to bind Capture Cascade";
    case Code::Ready:return "Capture Cascade save state ready";
    case Code::Conflict:return "Capture Cascade save identity conflict";
    case Code::StorageError:return "Capture Cascade sidecar storage error";
    case Code::Unsupported:return "Capture Cascade native producer unavailable";
    default:return "Capture Cascade stopped";
    }
}
bool Snapshot(State& output,std::uint64_t& session) noexcept {
    if(!Ready())return false;
    try{std::unique_lock<std::recursive_mutex> lock(mutex,std::try_to_lock);if(!lock||!Ready())return false;output=state;session=generation;return true;}catch(...){return false;}
}
bool CaptureNotice(std::uint16_t room,bool stable,Arcana::Presentation::View& output,std::uint64_t& session) noexcept {
    if(!Ready()||!OnOwner())return false;
    try{
        std::unique_lock<std::recursive_mutex> lock(mutex,std::try_to_lock);if(!lock||!Ready())return false;
        if(stable){
            if(cascade&&!Battle::AdmitNativeAssets(base)){ready=false;code=Code::Unsupported;return false;}
            std::array<unsigned char,0xC91> payload{};
            if(!Copy(payload.data(),reinterpret_cast<const void*>(base+saveRva),payload.size())||
               (unsigned(payload[0])|(unsigned(payload[1])<<8))!=room)return false;
            const auto before=state.defeated;
            if(cascade&&!ObserveVictories(state,Flags(payload.data()),Now())){ready=false;code=Code::Conflict;return false;}
            if(state.defeated!=before){
                for(unsigned i=0;i<kFamilies;++i)if((state.defeated&~before)&(1u<<i))region=static_cast<Aeon>(i);
                Log("[ffx-hooks] Capture Cascade: terminal Dark Aeon victory recorded; tutorial pending\n");
            }
            region=Rooms::Region(room);
            const bool macalania=region==Aeon::Shiva;
            ObserveRegion(state,region,macalania);
        }
        session=generation;output={};const auto view=Current(state,region);
        if(view.kind!=Tutorial::None){output.kind=Arcana::Presentation::Kind::System;output.page=view.page;output.revision=view.revision;
            output.card=view.kind==Tutorial::Sin?0:view.kind==Tutorial::Cascade?1:static_cast<std::uint8_t>(2+view.family);}
        return true;
    }catch(...){return false;}
}
Arcana::Error AcknowledgeNotice(std::uint64_t session,const Arcana::Presentation::View& view,Arcana::Presentation::Navigation navigation) noexcept {
    if(!Ready()||!OnOwner()||navigation==Arcana::Presentation::Navigation::None)return Arcana::Error::InvalidState;
    try{
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if(session!=generation||!Acknowledge(state,FromNative(view),navigation==Arcana::Presentation::Navigation::Previous,navigation==Arcana::Presentation::Navigation::Skip))return Arcana::Error::Stale;
        return Arcana::Error::None;
    }catch(...){return Arcana::Error::InvalidState;}
}
bool FormatNotice(const Arcana::Presentation::View& view,char* output,std::size_t capacity) noexcept {
    try{
        std::lock_guard<std::recursive_mutex> lock(mutex);
        const bool portuguese=UiLanguage::Settings::Current()==UiLanguage::Locale::Portuguese&&TextLanguage::Native::Inspect().fontReady;
        if(!Format(FromNative(view),portuguese,output,capacity))return false;
        if(portuguese){std::array<unsigned char,2048> proof{};std::size_t size=0;
            if(!TextLanguage::Native::EncodeFieldLiteral(output,proof.data(),proof.size(),size))return Format(FromNative(view),false,output,capacity);
        }
        return true;
    }catch(...){return false;}
}
bool EncodeNotice(const char* text,unsigned char* output,std::size_t capacity,std::size_t& size) noexcept {
    if(TextLanguage::Native::EncodeFieldLiteral(text,output,capacity,size))return true;
    Arcana::FieldDialog::EncodedText encoded;
    if(!output||!Arcana::FieldDialog::Encode(text,encoded)||encoded.size>capacity)return false;
    std::memcpy(output,encoded.bytes.data(),encoded.size);size=encoded.size;return true;
}
Arcana::FieldPresentation::SupplementalSource NoticeSource() noexcept {
    return {CaptureNotice,AcknowledgeNotice,FormatNotice,EncodeNotice};
}
bool Awakened(unsigned field) noexcept {
    if(!Requested()||!Ready()||!OnOwner())return false;
    try{std::unique_lock<std::recursive_mutex> lock(mutex,std::try_to_lock);if(!lock)return false;
        const auto family=static_cast<unsigned>(Catalog::RegionForField(field));return family<kFamilies&&(state.defeated&(1u<<family));
    }catch(...){return false;}
}
SinRam::Config EffectiveSin(unsigned field,const SinRam::Config& original) noexcept {
    auto config=original;if(Awakened(field)){config.enabled=true;config.seeded=true;config.distribution=100;config.minimumThreat=7;}return config;
}
bool ObserveNatural(const SinNatural::Evidence& evidence,std::uint32_t request,Bridge::CurrentRequest current) noexcept {
    if(!Requested()||!Ready()||!OnOwner()||!SinNatural::Admitted(evidence)||!current||!current(request,evidence.nativeFieldRow))return false;
    try{
        std::unique_lock<std::recursive_mutex> lock(mutex,std::try_to_lock);if(!lock)return false;
        natural={};region=Catalog::RegionForField(static_cast<unsigned>(evidence.field));
        if(region==Aeon::Count)return false;
        ObserveRegion(state,region,region==Aeon::Shiva);
        if(!(state.defeated&(1u<<static_cast<unsigned>(region)))||state.encounters==UINT64_MAX||state.revision==UINT64_MAX)return false;
        ++state.encounters;++state.revision;
        std::uint32_t seed=0;std::memcpy(&seed,nativeHash.data(),4);
        natural={true,GetCurrentThreadId(),generation,evidence,seed^static_cast<std::uint32_t>(state.encounters)^static_cast<std::uint32_t>(generation*0x9E3779B9u),request,current};
        return true;
    }catch(...){return false;}
}
bool ConsumeNatural(SinNatural::Evidence& evidence,std::uint32_t& entropy) noexcept {
    if(!Ready()||!OnOwner())return false;
    try{
        std::unique_lock<std::recursive_mutex> lock(mutex,std::try_to_lock);if(!lock||!natural.armed)return false;
        const auto value=natural;natural.armed=false;
        if(value.thread!=GetCurrentThreadId()||value.generation!=generation||!value.current||!value.current(value.request,value.evidence.nativeFieldRow))return false;
        evidence=value.evidence;entropy=value.entropy;return true;
    }catch(...){return false;}
}
bool RecordCapture(unsigned model) noexcept {
    if(!Ready()||!OnOwner())return false;
    const auto family=FamilyForMonster(model);if(family>=kFamilies)return false;
    try{std::unique_lock<std::recursive_mutex> lock(mutex,std::try_to_lock);return lock&&ObserveCapture(state,static_cast<Aeon>(family),Now());}catch(...){return false;}
}
}
