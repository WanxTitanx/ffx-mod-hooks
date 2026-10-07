#include "../shared/ExecutableProfile.h"
#pragma once
#include "GridLearnedStore.h"
#include "NativeSaveEvents.h"
#include "NativeSaveLoadEvents.h"
#include "RecoveryNative.h"
#include "RonsoPoolSave.h"
#include "RonsoPoolRuntime.h"
#include <array>
#include <atomic>
#include <mutex>

namespace FfxHooks::GridLearned::Runtime {
using LogFn=void(*)(const char*);
using LoadFn=int(__cdecl*)(void*,const void*);
inline std::recursive_mutex mutex;
inline State learned;
inline Store store;
inline std::uintptr_t base=0;
inline std::atomic<bool> enabled{false},attempted{false},stopping{false};
// The existing call gateways are shared infrastructure. Learning and Seymour
// subscribe independently; starting load observation does not enable learning.
inline std::atomic<bool> loadEventsReady{false},loadEventsStopped{false};
inline std::atomic_flag loadAdmin=ATOMIC_FLAG_INIT;
inline bool loadAttempted=false;
inline LogFn log=nullptr;
inline DWORD ownerThread=0;
inline std::uint64_t loadSerial=0,readSerial=0;
inline unsigned loadDepth=0;
inline RecoveryNative::OwnedBatch loadBatch;
inline std::array<void*,3> gateways{},unusedOriginals{};
struct Pending {
    Identity identity{};Hash payload{};std::uintptr_t source=0;
    std::uint64_t serial=0;bool occupied=false;
};
inline std::array<Pending,64> pending{};
inline void Emit(const char* message) noexcept {if(log)log(message);}
inline bool PublisherReady() noexcept {
    return enabled.load(std::memory_order_acquire)&&!stopping.load(std::memory_order_acquire)&&
        loadEventsReady.load(std::memory_order_acquire)&&RonsoPool::IsSaveIoReady();
}
// Loader-lock fallback closes callbacks without waiting on the state/store lock.
// Normal-context Stop owns unsubscribe, state clearing and hook neutralization.
inline void RequestStop() noexcept {
    stopping.store(true,std::memory_order_release);
    enabled.store(false,std::memory_order_release);
}

inline bool NormalizeLoaded(RonsoPool::SaveImage& image) noexcept {
    if(RonsoPool::IsValidSave(image))return true;
    // Native load clears its four-byte payload CRC after validating the header.
    constexpr std::size_t at=25844;
    if(image[at]||image[at+1]||image[at+2]||image[at+3])return false;
    const auto crc=RonsoPool::SaveChecksum(image);
    const auto header=static_cast<std::uint16_t>(image[26]|(unsigned(image[27])<<8));
    if(static_cast<std::uint16_t>(crc)!=header)return false;
    for(unsigned i=0;i<4;++i)image[at+i]=static_cast<std::uint8_t>(crc>>(8*i));
    return RonsoPool::IsValidSave(image);
}
inline bool PayloadHash(const unsigned char* data,std::size_t size,Hash& hash) {
    if(size!=RonsoPool::kSaveSize||!data)return false;
    RonsoPool::SaveImage image{};
    return RecoveryNative::Copy(image.data(),data,image.size())&&NormalizeLoaded(image)&&
        Fingerprint(image.data()+64,image.size()-64,hash);
}
inline void ReadStartingEvent(const unsigned char* buffer) noexcept {
    try {
        if(!buffer||!PublisherReady())return;
        const auto source=reinterpret_cast<std::uintptr_t>(buffer);
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if(!PublisherReady())return;
        for(auto& item:pending)if(item.occupied&&item.source==source)item={};
    } catch(...) {
        // Failed invalidation must not leave old provenance eligible for grants.
        RequestStop();
        Emit("[ffx-hooks] GridTeach read-attempt invalidation failed; learning stopped\n");
    }
}
inline void ReadEvent(const wchar_t* path,const unsigned char* disk,const unsigned char* loaded,std::size_t size) noexcept {
    try {
        if(!PublisherReady())return;
        const auto incoming=reinterpret_cast<std::uintptr_t>(loaded);
        if(incoming){
            std::lock_guard<std::recursive_mutex> lock(mutex);
            for(auto& item:pending)if(item.occupied&&item.source==incoming)item={};
        }
        if(size!=RonsoPool::kSaveSize||!disk||!loaded)return;
        std::wstring canonical;Identity identity{};Hash payload{};
        if(!CanonicalSavePath(path,canonical)||!SaveIdentity(canonical,disk,size,identity)||!PayloadHash(loaded,size,payload))return;
        const auto address=reinterpret_cast<std::uintptr_t>(loaded);
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if(!PublisherReady())return;
        Pending* selected=nullptr;
        for(auto& item:pending){
            if(item.occupied&&(item.identity.path==identity.path||item.source==address))item={};
            if(!selected&&!item.occupied)selected=&item;
        }
        if(!selected){selected=&pending[0];for(auto& item:pending)if(item.serial<selected->serial)selected=&item;}
        *selected={identity,payload,address,++readSerial,true};
    } catch(...) {Emit("[ffx-hooks] GridTeach read identity unavailable\n");}
}
inline void WriteEvent(const wchar_t* path,const unsigned char* actual,std::size_t size) noexcept {
    try {
        if(!PublisherReady()||size!=RonsoPool::kSaveSize||!actual)return;
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if(!PublisherReady()||loadDepth||!learned.Ready()||ownerThread!=GetCurrentThreadId())return;
        RonsoPool::SaveImage image{};std::wstring canonical;Identity identity{};
        if(!RecoveryNative::Copy(image.data(),actual,image.size())||!RonsoPool::IsValidSave(image)||
           !CanonicalSavePath(path,canonical)||!SaveIdentity(canonical,image.data(),image.size(),identity))return;
        State written;const auto words=learned.Words();written.Bind(identity,&words);
        if(!store.Write(written)){
            Emit("[ffx-hooks] GridTeach metadata write failed; native save is preserved and learning remains unsaved in RAM\n");return;
        }
        learned.Bind(identity,&words);
    } catch(...) {Emit("[ffx-hooks] GridTeach metadata write unavailable; native save unchanged\n");}
}
inline void ResetEvent() noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(loadDepth)return;
    learned.Clear();ownerThread=0;
    if(PublisherReady()){
        // A real native new-game/reset owns an empty transient session. Only a
        // successful native save provides its eventual persistent identity.
        pending={};learned.BeginSession();ownerThread=GetCurrentThreadId();
    }
}
inline const NativeSaveEvents::Observer observer=[](){
    // Optional callbacks are named: main's fourth positional field owns save
    // projection, not recovery read-attempt invalidation.
    NativeSaveEvents::Observer value{ReadEvent,WriteEvent,ResetEvent};
    value.readStarting=ReadStartingEvent;
    return value;
}();

struct LoadAttempt {
    Identity identity{};Learned words{};
    std::uint64_t serial=0,readSerial=0;
    std::uintptr_t source=0;
    DWORD thread=0;
    bool tracked=false,valid=false;
};
inline LoadAttempt BeforeLoad(void* destination,const void* source) noexcept {
    LoadAttempt result{};
    try {
        if(!PublisherReady()||reinterpret_cast<std::uintptr_t>(destination)!=base + (::FfxHooks::ExecutableProfile::Rva<0xD2CA90u>()))return result;
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if(!PublisherReady())return result;
        result.tracked=true;result.serial=++loadSerial;result.thread=GetCurrentThreadId();++loadDepth;
        learned.Clear();ownerThread=0;
        if(loadDepth!=1||!source)return result;
        Hash payload{};
        if(!PayloadHash(static_cast<const unsigned char*>(source),RonsoPool::kSaveSize,payload))return result;
        const Pending* selected=nullptr;
        const auto sourceAddress=reinterpret_cast<std::uintptr_t>(source);
        for(const auto& item:pending)if(item.occupied&&item.payload==payload&&item.source==sourceAddress){selected=&item;break;}
        // Content equality cannot establish which save supplied an unobserved or
        // reused buffer. Only the exact completed-read record supplies identity.
        if(!selected){Emit("[ffx-hooks] GridTeach load rejected: missing exact-buffer save provenance\n");return result;}
        result.identity=selected->identity;
        result.source=sourceAddress;result.readSerial=selected->serial;
        const auto read=store.Read(result.identity,result.words);
        if(read!=RecordRead::Found&&read!=RecordRead::Missing){
            Emit("[ffx-hooks] GridTeach load metadata invalid; no inherited commands admitted\n");return result;
        }
        result.valid=true;
    } catch(...) {
        RequestStop();
        Emit("[ffx-hooks] GridTeach load identity unavailable; learning stopped\n");
    }
    return result;
}
inline void AfterLoad(const LoadAttempt& attempt,bool completed) noexcept {
    if(!attempt.tracked)return;
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if(loadDepth)--loadDepth;
        if(!completed||!PublisherReady()||loadDepth||loadSerial!=attempt.serial||
           !attempt.valid||attempt.thread!=GetCurrentThreadId()){
            learned.Clear();ownerThread=0;return;
        }
        // Native copying occurs outside this lock. A new/short read, eviction or
        // buffer reuse during that interval revokes the captured admission token.
        bool current=false;
        for(const auto& item:pending)if(item.occupied&&item.source==attempt.source&&
            item.serial==attempt.readSerial&&item.identity==attempt.identity){current=true;break;}
        if(!current){learned.Clear();ownerThread=0;return;}
        if(learned.Bind(attempt.identity,&attempt.words))ownerThread=attempt.thread;
    } catch(...) {
        RequestStop();
        Emit("[ffx-hooks] GridTeach post-load provenance unavailable; learning stopped\n");
    }
}
inline int __cdecl LoadShim(void* destination,const void* source){
    const DWORD incoming=GetLastError();
    const auto attempt=BeforeLoad(destination,source);
    auto dispatch=loadEventsReady.load(std::memory_order_acquire)?
        NativeSaveEvents::BeginLoad(destination,source):NativeSaveEvents::LoadDispatch{};
    int result=0;bool completed=false;
    SetLastError(incoming);
    __try {result=reinterpret_cast<LoadFn>(base + (::FfxHooks::ExecutableProfile::Rva<0x4B5450u>()))(destination,source);completed=true;}
    __finally {
        const DWORD nativeError=GetLastError();
        AfterLoad(attempt,completed);
        NativeSaveEvents::EndLoad(dispatch,completed);
        SetLastError(nativeError);
    }
    return result;
}
inline bool Ready() noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return PublisherReady()&&learned.Ready()&&ownerThread==GetCurrentThreadId()&&!loadDepth;
}
inline bool Has(unsigned character,unsigned command) noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return PublisherReady()&&ownerThread==GetCurrentThreadId()&&!loadDepth&&learned.Has(character,command);
}
inline Change Set(unsigned character,unsigned command,bool on) noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(!PublisherReady()||ownerThread!=GetCurrentThreadId()||loadDepth)return Change::Unbound;
    return learned.Set(character,command,on);
}
inline bool PrepareStore() {
    HMODULE self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&PrepareStore),&self))return false;
    wchar_t modulePath[4097]{};const auto length=GetModuleFileNameW(self,modulePath,4097);
    if(!length||length>=4097)return false;
    std::wstring directory(modulePath,length);const auto slash=directory.find_last_of(L"/\\");
    if(slash==std::wstring::npos)return false;
    directory.resize(slash);directory+=L"\\config";
    if(!CreateDirectoryW(directory.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
    return store.Initialize(directory+L"\\grid_teach_v2",true);
}
inline void ReleaseUnpublished() noexcept {
    if(!loadBatch.DiscardUnpublished())return;
    for(auto& pointer:gateways)if(pointer){VirtualFree(pointer,0,MEM_RELEASE);pointer=nullptr;}
}
inline bool StartLoadEvents(std::uintptr_t module) {
    if(loadEventsStopped.load(std::memory_order_acquire)||RecoveryNative::EnvironmentEnabled("FFXHOOKS_VALIDATE_ONLY"))return false;
    if(loadAdmin.test_and_set(std::memory_order_acquire))return false;
    struct Unlock {~Unlock(){loadAdmin.clear(std::memory_order_release);}} unlock;
    if(loadEventsReady.load(std::memory_order_acquire))return base==module;
    if(loadAttempted)return false;
    loadAttempted=true;base=module;
#ifdef FFXHOOKS_HAVE_POLYHOOK
    using namespace RecoveryEvidence;
    const Proof proofs[]={LoadCallA,LoadCallB,LoadCallC};
    if(!RecoveryNative::Profile(base)||MinHookBatch::EnsureProcessInitialized()!=MinHookBatch::InitializationResult::Ready)return false;
    for(const auto& proof:proofs)if(!RecoveryNative::Match(base,proof))return false;
    for(std::size_t i=0;i<3;++i){
        auto* code=static_cast<std::uint8_t*>(RecoveryNative::AllocateCode(10));gateways[i]=code;
        if(!code){ReleaseUnpublished();return false;}
        // Replace the validated call instruction with a MinHook JMP. This
        // gateway reconstructs exactly the CALL's native return address; the
        // cdecl shim forwards both arguments to the existing load-copy entry,
        // including the Workshop's owner when present. No second entry detour.
        code[0]=0x68;const auto resume=static_cast<std::uint32_t>(base+proofs[i].rva+5);
        std::memcpy(code+1,&resume,4);code[5]=0xe9;
        const auto relative=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&LoadShim)-reinterpret_cast<std::uintptr_t>(code)-10);
        std::memcpy(code+6,&relative,4);
        if(!RecoveryNative::SealCode(code,10)||!loadBatch.Add(base+proofs[i].rva,code,&unusedOriginals[i])){ReleaseUnpublished();return false;}
    }
    if(!loadBatch.Publish(MinHookBatch::Owner::GridTeachSave,reinterpret_cast<const void*>(&StartLoadEvents))){ReleaseUnpublished();return false;}
    if(loadEventsStopped.load(std::memory_order_acquire)){loadBatch.Neutralize();return false;}
    loadEventsReady.store(true,std::memory_order_release);
    if(loadEventsStopped.load(std::memory_order_acquire)){
        loadEventsReady.store(false,std::memory_order_release);loadBatch.Neutralize();return false;
    }
    return true;
#else
    return false;
#endif
}
inline void RequestLoadEventsStop() noexcept {
    loadEventsStopped.store(true,std::memory_order_release);
    loadEventsReady.store(false,std::memory_order_release);
}
inline bool StopLoadEvents() noexcept {
    RequestLoadEventsStop();
    if(loadAdmin.test_and_set(std::memory_order_acquire))return false;
    const bool result=loadBatch.Neutralize();
    loadAdmin.clear(std::memory_order_release);return result;
}
inline bool Start(std::uintptr_t module,LogFn logger) {
    if(stopping.load(std::memory_order_acquire)||RecoveryNative::EnvironmentEnabled("FFXHOOKS_VALIDATE_ONLY"))return false;
    if(enabled.load())return base==module;
    if(attempted.exchange(true))return false;
    log=logger;
    if(!StartLoadEvents(module))return false;
    try {if(!PrepareStore())return false;}catch(...){return false;}
    if(!NativeSaveEvents::SubscribeAdditional(&observer))return false;
    if(stopping.load(std::memory_order_acquire)){
        NativeSaveEvents::UnsubscribeAdditional(&observer);return false;
    }
    enabled.store(true,std::memory_order_release);
    Emit("[ffx-hooks] GridTeach save observer registered; native publisher and active save readiness are separate. Legacy global learning is not imported\n");
    return true;
}
inline bool Stop() noexcept {
    RequestStop();
    NativeSaveEvents::UnsubscribeAdditional(&observer);
    {std::lock_guard<std::recursive_mutex> lock(mutex);learned.Clear();ownerThread=0;}
    // Other subscribers may still need the single native-copy publisher.
    // Its process-wide retirement is ordered separately by the DLL owner.
    return true;
}
} // namespace FfxHooks::GridLearned::Runtime
