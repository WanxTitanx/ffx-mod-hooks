#include "SeymourSessionRuntime.h"
#include "SeymourActiveLoadCore.h"
#include "NativeSaveEvents.h"
#include "GridTeachHook.h"
#include "RonsoPoolRuntime.h"
#include "RecoveryNative.h"
#include "../shared/Config.h"
#include <mutex>

namespace FfxHooks::SeymourSession {
namespace {
std::recursive_mutex mutex;
ActiveLoad state;
std::atomic<bool> subscribed{false},stopping{false};
std::atomic_flag admin=ATOMIC_FLAG_INIT;
bool PublishersAvailable() noexcept {
    return subscribed.load(std::memory_order_acquire)&&NativeSaveLoadEventsReady()&&RonsoPool::IsSaveIoReady();
}
bool Requested(){
    for(const char* key:{"seymour.permanent_roster","seymour.gear_sorting","seymour.grid8","seymour.menu_list","seymour.grid8_persistence"}){
        const auto value=Config::ReadIntExact(key,0,1);
        if(value.state==Config::IntReadState::Valid&&value.value==1)return true;
    }
    return false;
}
bool Copy(void*,void* destination,const void* source,std::size_t size){
    return RecoveryNative::Range(reinterpret_cast<std::uintptr_t>(source),size)&&
        RecoveryNative::Copy(destination,source,size);
}
void Read(const wchar_t*,const unsigned char*,const unsigned char*,std::size_t) noexcept {}
void Write(const wchar_t*,const unsigned char*,std::size_t) noexcept {}
void Before(std::uint64_t cookie,void* destination,const void* source) noexcept {
    const DWORD error=GetLastError();
    try {std::lock_guard<std::recursive_mutex> lock(mutex);state.Begin(cookie,destination,source,GetCurrentThreadId(),{nullptr,Copy});}
    catch(...){RequestStop();}
    SetLastError(error);
}
void After(std::uint64_t cookie,bool complete) noexcept {
    const DWORD error=GetLastError();
    try {std::lock_guard<std::recursive_mutex> lock(mutex);
        const bool admitted=state.End(cookie,complete&&!stopping.load(std::memory_order_acquire)&&PublishersAvailable(),GetCurrentThreadId(),{nullptr,Copy});
        // Stop can run without this mutex while the payload is being read back.
        // Only a previously admitted session may survive for owned cleanup.
        if(admitted&&(!PublishersAvailable()||stopping.load(std::memory_order_acquire)))state.Invalidate();}
    catch(...){RequestStop();}
    SetLastError(error);
}
void Reset() noexcept {
    const DWORD error=GetLastError();
    try {std::lock_guard<std::recursive_mutex> lock(mutex);
        if(stopping.load(std::memory_order_acquire)||!PublishersAvailable())state.Invalidate();
        else {
            const bool admitted=state.Reset(GetCurrentThreadId());
            if(admitted&&(!PublishersAvailable()||stopping.load(std::memory_order_acquire)))state.Invalidate();
        }}
    catch(...){RequestStop();}
    SetLastError(error);
}
const NativeSaveEvents::Observer observer=[](){
    NativeSaveEvents::Observer o{Read,Write,Reset};o.loadStarting=Before;o.loadCompleted=After;return o;
}();
}
void PrimeSaveIo(std::uintptr_t base,bool validateOnly){
    if(!Coexistence::runtime.SavePipelineAllowed())return;
    if(validateOnly||!Requested()||stopping.load(std::memory_order_acquire))return;
    if(admin.test_and_set(std::memory_order_acquire))return;
    struct Unlock {~Unlock(){admin.clear(std::memory_order_release);}} unlock;
    if(subscribed.load(std::memory_order_acquire))return;
    if(!RecoveryNative::Profile(base)||!RecoveryNative::Pin(reinterpret_cast<const void*>(&PrimeSaveIo)))return;
    {std::lock_guard<std::recursive_mutex> lock(mutex);state.Configure(base+0xd2ca90u);}
    if(!NativeSaveEvents::SubscribeAdditional(&observer))return;
    if(!StartNativeSaveLoadEvents(base)||stopping.load(std::memory_order_acquire)){
        NativeSaveEvents::UnsubscribeAdditional(&observer);return;
    }
    subscribed.store(true,std::memory_order_release);
    if(stopping.load(std::memory_order_acquire))NativeSaveEvents::UnsubscribeAdditional(&observer);
}
bool PublisherReady() noexcept {
    return PublishersAvailable()&&!stopping.load(std::memory_order_acquire);
}
Token Capture(bool cleanup) noexcept {
    try {std::lock_guard<std::recursive_mutex> lock(mutex);
        if(!PublishersAvailable()||(stopping.load(std::memory_order_acquire)&&!cleanup))return {};
        const auto token=state.Capture(GetCurrentThreadId(),cleanup);
        // A nonblocking stop or reentrant producer query may revoke admission
        // after the first check. Cleanup remains valid only for the same token.
        if(!PublishersAvailable()||(stopping.load(std::memory_order_acquire)&&!cleanup))return {};
        return state.Current(token,token.thread,cleanup)?token:Token{};
    }catch(...){RequestStop();return {};}
}
bool Current(const Token& token,bool cleanup) noexcept {
    const auto current=Capture(cleanup);return current.Valid()&&current==token;
}
void RequestStop() noexcept {stopping.store(true,std::memory_order_release);}
bool Remove() noexcept {
    RequestStop();
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        // Stop alone preserves owned cleanup. Normal removal must retire that
        // identity before future loads become unobserved by this subscriber.
        subscribed.store(false,std::memory_order_release);
        state.Stop();state.Invalidate();
        NativeSaveEvents::UnsubscribeAdditional(&observer);
    } catch(...) {
        subscribed.store(false,std::memory_order_release);
        NativeSaveEvents::UnsubscribeAdditional(&observer);
        return false;
    }
    // Captured callbacks and their storage remain alive, but cannot readmit.
    return true;
}
}
