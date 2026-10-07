#include "SphereGridProgress8Runtime.h"
#include "../shared/ExecutableProfile.h"
#include "SphereGridProgress8Store.h"
#include "NativeSaveEvents.h"
#include "GridLearnedStore.h"
#include "RonsoPoolRuntime.h"
#include "SeymourSessionRuntime.h"
#include "SeymourOverdriveControl.h"
#include "RecoveryNative.h"
#include "F8FlagCatalog.h"
#include "../shared/Config.h"
#include <cstdio>

namespace FfxHooks::SphereGridProgress8Runtime {
namespace {
namespace Save=SphereGridProgress8Save;
namespace Codec=SphereGridProgress8;
namespace Commit=SphereGridProgress8Commit;
constexpr char KeyName[]="seymour.grid8_persistence";
std::atomic<const Source*> source{nullptr};
std::atomic<bool> attempted{false},subscribed{false},stopping{false};
std::atomic<unsigned> callbacks{0},staged{0},written{0},rejected{0};
std::atomic<Save::Persist> last{Save::Persist::Rejected};
std::atomic<std::uintptr_t> moduleBase{0};
std::atomic_flag admin=ATOMIC_FLAG_INIT;
SeymourOverdrive::Control control;
Save::Service service;
SphereGridProgress8Disk::Store store;
#ifdef FFXHOOKS_TESTING
bool (*testProfile)(std::uintptr_t)=nullptr;
bool (*testPin)(const void*)=nullptr;
#endif
bool Profile(std::uintptr_t base){
#ifdef FFXHOOKS_TESTING
    if(testProfile)return testProfile(base);
#endif
    return RecoveryNative::Profile(base);
}
bool Pin(){
#ifdef FFXHOOKS_TESTING
    if(testPin)return testPin(reinterpret_cast<const void*>(&PrimeSaveIo));
#endif
    return RecoveryNative::Pin(reinterpret_cast<const void*>(&PrimeSaveIo));
}
bool Requested(){
    const auto value=Config::ReadIntExact(KeyName,0,1);
    const auto* master=FindF8Flag("boosters.playable_seymour");
    return value.state==Config::IntReadState::Valid&&value.value==1&&master&&ResolveF8Flag(*master).value;
}
bool Active(std::uint64_t request) noexcept {
    return subscribed.load(std::memory_order_acquire)&&!stopping.load(std::memory_order_acquire)&&
        control.Current(request)&&RonsoPool::IsVerifiedSaveIoReady();
}
bool ReadLayout(Layout& value) noexcept {
    const auto* native=source.load(std::memory_order_acquire);
    const auto nonzero=[](const auto& hash){return std::any_of(hash.begin(),hash.end(),[](auto b){return b!=0;});};
    return native&&native->describe(native->context,value)&&value.generation&&nonzero(value.layout)&&nonzero(value.contents)&&
        native==source.load(std::memory_order_acquire);
}
bool Current(void*,const Commit::Scope& scope,const Codec::Key& key){
    Layout observed{};
    return Active(scope.request)&&GetCurrentThreadId()==scope.thread&&
        SeymourSession::Current({scope.session,scope.thread})&&ReadLayout(observed)&&
        observed.generation==scope.layoutGeneration&&observed.layout==key.layout&&observed.contents==key.contents&&
        SeymourSession::Current({scope.session,scope.thread})&&Active(scope.request);
}
bool Describe(void*,Commit::Scope& scope,Codec::Key& key){
    const auto request=control.Read();Layout layout{};
    if(!Active(request)||!ReadLayout(layout))return false;
    const auto session=SeymourSession::Capture();
    scope={session.revision,session.thread,layout.generation,request};
    key.layout=layout.layout;key.contents=layout.contents;
    return scope.Valid()&&Current(nullptr,scope,key);
}
bool Capture(void*,const Commit::Scope& scope,Codec::Snapshot& output){
    const auto* native=source.load(std::memory_order_acquire);Layout layout{};Codec::Key key{};
    if(!native||!ReadLayout(layout)||layout.generation!=scope.layoutGeneration)return false;
    key.layout=layout.layout;key.contents=layout.contents;
    return Current(nullptr,scope,key)&&native->capture(native->context,layout,output)&&output.Valid()&&
        native==source.load(std::memory_order_acquire)&&Current(nullptr,scope,key);
}
bool Identify(void*,const wchar_t* path,const RonsoPool::SaveImage& image,
              SphereGridProgress::Hash& pathHash,SphereGridProgress::Hash& imageHash){
    std::wstring canonical;
    return GridLearned::CanonicalSavePath(path,canonical)&&
        GridLearned::Fingerprint(canonical.data(),canonical.size()*sizeof(wchar_t),pathHash)&&
        GridLearned::Fingerprint(image.data(),image.size(),imageHash);
}
Save::Persist Publish(void*,const Codec::Key& key,const Codec::Snapshot& snapshot,const Save::Admission& admission){
    using Result=SphereGridProgress8Disk::WriteResult;
    switch(store.Publish(key,snapshot,{admission.context,admission.current})){
        case Result::Written:return Save::Persist::Written;
        case Result::Unchanged:return Save::Persist::Unchanged;
        case Result::Rejected:return Save::Persist::Rejected;
        case Result::PublishedRetired:return Save::Persist::Retired;
        case Result::PublishedUnverified:return Save::Persist::Unverified;
        default:return Save::Persist::Unavailable;
    }
}
struct Callback {
    DWORD error=GetLastError();
    Callback(){callbacks.fetch_add(1,std::memory_order_acq_rel);}
    ~Callback(){callbacks.fetch_sub(1,std::memory_order_release);SetLastError(error);}
    bool Active() const noexcept {return subscribed.load(std::memory_order_acquire)&&!stopping.load(std::memory_order_acquire);}
};
void Read(const wchar_t*,const unsigned char*,const unsigned char*,std::size_t) noexcept {}
void Write(const wchar_t*,const unsigned char*,std::size_t) noexcept {}
void Reset() noexcept {Callback call;if(call.Active())service.Invalidate();}
void LoadStarting(std::uint64_t,void* destination,const void*) noexcept {
    Callback call;
    // File previews and copies into another destination do not replace the live save.
    if(call.Active()&&reinterpret_cast<std::uintptr_t>(destination)==moduleBase.load()+::FfxHooks::ExecutableProfile::Rva<0xd2ca90u>())service.Invalidate();
}
void LoadCompleted(std::uint64_t,bool) noexcept {}
void Staged(std::uint64_t ticket,const wchar_t* path,const unsigned char* data,std::size_t size) noexcept {
    Callback call;if(!call.Active())return;
    RonsoPool::SaveImage image{};
    if(size==image.size()&&RecoveryNative::Copy(image.data(),data,size)&&service.Stage(ticket,path,image))++staged;
    else {service.Abort(ticket);++rejected;}
}
void Verified(std::uint64_t ticket,const wchar_t* path,const unsigned char* data,std::size_t size) noexcept {
    Callback call;if(!call.Active())return;
    RonsoPool::SaveImage image{};auto result=Save::Persist::Rejected;
    if(size==image.size()&&RecoveryNative::Copy(image.data(),data,size))result=service.Verified(ticket,path,image);
    else service.Abort(ticket);
    last.store(result,std::memory_order_release);
    if(result==Save::Persist::Written||result==Save::Persist::Unchanged)++written;else ++rejected;
}
void Aborted(std::uint64_t ticket) noexcept {Callback call;service.Abort(ticket);}
const NativeSaveEvents::Observer observer=[](){
    NativeSaveEvents::Observer value{Read,Write,Reset};
    value.writeStaged=Staged;value.writeVerified=Verified;value.writeAborted=Aborted;
    value.loadStarting=LoadStarting;value.loadCompleted=LoadCompleted;value.rejectRead=Reset;
    return value;
}();
bool StorageDirectory(std::wstring& directory){
    HMODULE self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&PrimeSaveIo),&self))return false;
    wchar_t path[4097]{};const auto length=GetModuleFileNameW(self,path,4097);
    if(!length||length>=4097)return false;
    directory.assign(path,length);const auto slash=directory.find_last_of(L"/\\");
    if(slash==std::wstring::npos)return false;
    directory.resize(slash);directory+=L"\\config";
    if(!CreateDirectoryW(directory.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
    directory+=L"\\grid8_progress_v2";return true;
}
}
bool RegisterSource(const Source* value) noexcept {
    if(!value||!value->describe||!value->capture||stopping.load(std::memory_order_acquire))return false;
    const Source* expected=nullptr;
    return source.compare_exchange_strong(expected,value,std::memory_order_acq_rel)||expected==value;
}
void PrimeSaveIo(std::uintptr_t base,bool validateOnly,const wchar_t* storageOverride){
    if(!Coexistence::runtime.SavePipelineAllowed())return;
    if(validateOnly||!Requested()||stopping.load(std::memory_order_acquire))return;
    if(admin.test_and_set(std::memory_order_acquire))return;
    struct Unlock {~Unlock(){admin.clear(std::memory_order_release);}} unlock;
    if(attempted.exchange(true,std::memory_order_acq_rel))return;
    try {
        if(!Profile(base)||!Pin()||stopping.load(std::memory_order_acquire))return;
        std::wstring directory;
        if(storageOverride)directory=storageOverride;else if(!StorageDirectory(directory))return;
        if(!store.Initialize(directory,true)||!service.Configure({nullptr,Describe,Capture,Identify,Current,Publish}))return;
        moduleBase.store(base);control.Publish(Requested());
        if(stopping.load(std::memory_order_acquire)||!NativeSaveEvents::SubscribeAdditional(&observer))return;
        subscribed.store(true,std::memory_order_release);
        if(stopping.load(std::memory_order_acquire)){
            subscribed.store(false,std::memory_order_release);NativeSaveEvents::UnsubscribeAdditional(&observer);
        }
    }catch(...){RequestStop();}
}
void PresentTick(){
    const auto previous=control.Read();control.Publish(Requested());
    if(previous!=control.Read())service.Invalidate();
}
void RequestStop() noexcept {stopping.store(true,std::memory_order_release);control.Stop();service.RequestStop();}
bool Remove() noexcept {
    RequestStop();subscribed.store(false,std::memory_order_release);
    NativeSaveEvents::UnsubscribeAdditional(&observer);service.Invalidate();
    // The pinned callbacks and source descriptor remain process-lifetime.
    return callbacks.load(std::memory_order_acquire)==0;
}
Status GetStatus() noexcept {return {subscribed.load(),source.load()!=nullptr,staged.load(),written.load(),rejected.load(),last.load()};}
void MenuLabel(char* out,std::size_t size){
    if(!out||!size)return;
    const auto value=Config::ReadIntExact(KeyName,0,1);
    std::snprintf(out,size,"Grid8 verified persistence: %s",value.state==Config::IntReadState::Invalid?"INVALID":value.value==1?"ON":"OFF");
}
void Detail(char* out,std::size_t size){
    if(!out||!size)return;
    const auto status=GetStatus();
    std::snprintf(out,size,"Save subscriber %s; native source %s; saved %u; rejected %u; last %u",
        status.subscribed?"ready":"restart required",status.sourceRegistered?"registered":"unavailable",
        status.written,status.rejected,static_cast<unsigned>(status.last));
}
bool MenuAction(){
    const auto value=Config::ReadIntExact(KeyName,0,1);const int next=value.state==Config::IntReadState::Valid&&value.value==0?1:0;
    if(!Config::SetInt(KeyName,next))return false;
    PresentTick();const auto observed=Config::ReadIntExact(KeyName,0,1);
    return observed.state==Config::IntReadState::Valid&&observed.value==next;
}
#ifdef FFXHOOKS_TESTING
bool SetGatesForTests(bool (*profileGate)(std::uintptr_t),bool (*pinGate)(const void*)) noexcept {
    if(attempted.load()||!profileGate||!pinGate||testProfile||testPin)return false;
    testProfile=profileGate;testPin=pinGate;return true;
}
#endif
}
