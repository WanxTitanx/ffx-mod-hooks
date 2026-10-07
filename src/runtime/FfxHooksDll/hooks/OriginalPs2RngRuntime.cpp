#include "OriginalPs2RngRuntime.h"
#include "OriginalPs2RngEvidence.generated.h"
#include "RecoveryNative.h"
#include "F8FlagCatalog.h"
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace FfxHooks::OriginalPs2Rng::Runtime {
namespace {
namespace E=Evidence;
namespace N=RecoveryNative;
static_assert(std::atomic<bool>::is_always_lock_free&&std::atomic<unsigned>::is_always_lock_free&&
              std::atomic<Code>::is_always_lock_free,"RNG detach must use lock-free admission/status atomics");
constexpr char Key[]="rng.original_ps2";
using InitFn=unsigned(__cdecl*)(unsigned);
using ClockFn=unsigned(__cdecl*)();
using ResetFn=unsigned(__cdecl*)();
std::uintptr_t g_base=0;
void* g_original[3]{};
N::OwnedBatch g_batch;
std::atomic<bool> g_attempted{false},g_requested{false},g_accepting{false};
std::atomic<bool> g_stopRequested{false};
std::atomic<unsigned> g_active{0},g_owner{0},g_revision{0};
std::atomic<Code> g_code{Code::Disabled};
std::atomic<Reason> g_reason{Reason::None};
std::atomic<std::uint64_t> g_resets{0},g_initializations{0};
std::atomic_flag g_admin=ATOMIC_FLAG_INIT;
SRWLOCK g_epochLock=SRWLOCK_INIT;
CounterEpoch g_epoch{};
std::uint64_t g_generation=0,g_frequency=0;
LogFunction g_log=nullptr;
struct ClockScope {unsigned value=0,consumed=0;};
thread_local ClockScope* g_scope=nullptr;
thread_local unsigned g_depth=0;
struct Event {std::uint64_t generation=0,counter=0,clock=0;unsigned argument=0,seed=0;};
SRWLOCK g_eventLock=SRWLOCK_INIT;
Event g_events[8]{};
unsigned g_eventRead=0,g_eventWrite=0;
std::atomic<unsigned> g_dropped{0};

bool Performance(std::uint64_t& raw,std::uint64_t& frequency) noexcept;
bool Calendar(UtcCalendar& value) noexcept;
unsigned Thread() noexcept;
#ifdef FFXHOOKS_TESTING
InputSource g_testSource{};
#endif
bool Performance(std::uint64_t& raw,std::uint64_t& frequency) noexcept {
#ifdef FFXHOOKS_TESTING
    if(g_testSource.performance)return g_testSource.performance(g_testSource.context,raw,frequency);
#endif
    LARGE_INTEGER now{},rate{};
    if(!QueryPerformanceCounter(&now)||!QueryPerformanceFrequency(&rate)||now.QuadPart<0||rate.QuadPart<=0)return false;
    raw=static_cast<std::uint64_t>(now.QuadPart);frequency=static_cast<std::uint64_t>(rate.QuadPart);return true;
}
bool Calendar(UtcCalendar& value) noexcept {
#ifdef FFXHOOKS_TESTING
    if(g_testSource.calendar)return g_testSource.calendar(g_testSource.context,value);
#endif
    SYSTEMTIME time{};GetSystemTime(&time);
    value={time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond};return true;
}
unsigned Thread() noexcept {
#ifdef FFXHOOKS_TESTING
    if(g_testSource.thread)return g_testSource.thread(g_testSource.context);
#endif
    return GetCurrentThreadId();
}
void SetCode(Code code,Reason reason=Reason::None) noexcept {
    if(reason!=Reason::None)g_reason.store(reason,std::memory_order_release);
    const bool progressing=code==Code::AwaitingReset||code==Code::AwaitingBoundary||code==Code::Active;
    auto previous=g_code.load(std::memory_order_acquire);
    for(unsigned attempt=0;attempt<4;++attempt){
        if(progressing&&(g_stopRequested.load(std::memory_order_acquire)||previous==Code::Unavailable||
                         previous==Code::StopPending||previous==Code::Stopped))return;
        if(code==Code::Unavailable&&(previous==Code::StopPending||previous==Code::Stopped))return;
        if(g_code.compare_exchange_weak(previous,code,std::memory_order_acq_rel)){
            g_revision.fetch_add(1,std::memory_order_release);return;
        }
    }
}
void Unavailable(Reason reason) noexcept {
    g_accepting.store(false,std::memory_order_release);
    const auto code=g_code.load(std::memory_order_acquire);
    if(code!=Code::StopPending&&code!=Code::Stopped)SetCode(Code::Unavailable,reason);
}
bool Enter() noexcept {
    if(g_stopRequested.load(std::memory_order_acquire)||!g_accepting.load(std::memory_order_acquire))return false;
    g_active.fetch_add(1,std::memory_order_acq_rel);
    if(!g_stopRequested.load(std::memory_order_acquire)&&g_accepting.load(std::memory_order_acquire))return true;
    g_active.fetch_sub(1,std::memory_order_acq_rel);return false;
}
void Leave() noexcept {g_active.fetch_sub(1,std::memory_order_acq_rel);}
bool Caller(std::uintptr_t caller,const RecoveryEvidence::Proof& proof) noexcept {
    return caller==g_base+proof.rva+proof.size&&N::Match(g_base,proof);
}
bool ReadEpoch(unsigned thread,CounterEpoch& epoch,std::uint64_t& generation) noexcept {
    if(!TryAcquireSRWLockShared(&g_epochLock))return false;
    const bool valid=g_epoch.valid&&g_owner.load(std::memory_order_acquire)==thread;
    if(valid){epoch=g_epoch;generation=g_generation;}
    ReleaseSRWLockShared(&g_epochLock);return valid;
}
void Queue(const Event& event) noexcept {
    if(!TryAcquireSRWLockExclusive(&g_eventLock)){++g_dropped;return;}
    if(g_eventWrite-g_eventRead<8){g_events[g_eventWrite%8]=event;++g_eventWrite;}
    else ++g_dropped;
    ReleaseSRWLockExclusive(&g_eventLock);
}
bool ConsumerWitnesses() noexcept {
    return N::Match(g_base,E::Normal)&&N::Match(g_base,E::Indexed)&&N::Match(g_base,E::CounterGetter);
}
unsigned __cdecl ClockShim() {
    // Once committed, an admitted initializer must retain its matching clock
    // even if normal-context shutdown closes admission concurrently.
    if(g_scope&&g_depth==1){++g_scope->consumed;return g_scope->value;}
    return reinterpret_cast<ClockFn>(g_original[1])();
}
unsigned __cdecl ResetShim() {
    const DWORD incomingError=GetLastError();
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto original=reinterpret_cast<ResetFn>(g_original[2]);
    const bool entered=Enter();bool prepared=false,completed=false;
    unsigned result=0,thread=0;CounterEpoch next{};
    if(entered){
        thread=Thread();std::uint64_t raw=0,frequency=0;
        if(!Caller(caller,E::ResetCaller)||g_depth)Unavailable(Reason::BoundaryChanged);
        else if(g_owner.load()&&g_owner.load()!=thread)Unavailable(Reason::WrongThread);
        else if(!Performance(raw,frequency)||frequency!=g_frequency||!ResetCounterEpoch(raw,frequency,next))
            Unavailable(Reason::ClockUnavailable);
        else prepared=true;
    }
    __try {SetLastError(incomingError);result=original();completed=true;}
    __finally {
        const DWORD nativeError=GetLastError();
        if(entered){
            if(prepared&&completed&&g_accepting.load(std::memory_order_acquire)){
                if(TryAcquireSRWLockExclusive(&g_epochLock)){
                    const unsigned owner=g_owner.load(std::memory_order_acquire);
                    const bool sameOwner=!owner||owner==thread;
                    if(sameOwner){
                        g_epoch=next;if(++g_generation==0)++g_generation;
                        g_owner.store(thread,std::memory_order_release);++g_resets;
                    }
                    ReleaseSRWLockExclusive(&g_epochLock);
                    if(!sameOwner)Unavailable(Reason::WrongThread);
                    else if(g_accepting.load(std::memory_order_acquire))
                        SetCode(g_initializations.load()?Code::Active:Code::AwaitingBoundary);
                }else Unavailable(Reason::BoundaryChanged);
            }else if(!completed)Unavailable(Reason::NativeInitializationFailed);
            Leave();
        }
        SetLastError(nativeError);
    }
    return result;
}
unsigned __cdecl InitShim(unsigned nativeArgument) {
    const DWORD incomingError=GetLastError();
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto original=reinterpret_cast<InitFn>(g_original[0]);
    ++g_depth;
    const bool entered=g_depth==1&&Enter();
    bool apply=false,completed=false;unsigned result=0,effective=nativeArgument;
    ClockScope scope{};ClockScope* previous=g_scope;Event event{};
    if(entered){
        const unsigned thread=Thread();CounterEpoch epoch{};std::uint64_t generation=0,raw=0,frequency=0,counter=0;
        UtcCalendar utc{};ClockBytes clock{};std::uint32_t nativeCounter=0;
        if(!Caller(caller,E::InitCaller))Unavailable(Reason::BoundaryChanged);
        else if(g_owner.load()&&g_owner.load()!=thread)Unavailable(Reason::WrongThread);
        else if(!ReadEpoch(thread,epoch,generation))Unavailable(Reason::CounterOriginMissing);
        else if(!ConsumerWitnesses())Unavailable(Reason::SignatureMismatch);
        else if(!N::Copy(&nativeCounter,reinterpret_cast<const void*>(g_base+E::CounterRva),sizeof(nativeCounter))||nativeCounter)
            Unavailable(Reason::ForeignCounter);
        else if(!Performance(raw,frequency)||frequency!=epoch.frequency||!CounterSinceReset(epoch,raw,counter)||
                !Calendar(utc)||!EncodeHealthyJapanClock(utc,clock))Unavailable(Reason::ClockUnavailable);
        else {
            CounterEpoch current{};std::uint64_t currentGeneration=0;
            if(!ReadEpoch(thread,current,currentGeneration)||currentGeneration!=generation)
                Unavailable(Reason::BoundaryChanged);
            else if(g_accepting.load(std::memory_order_acquire)){
                // The incoming value includes the caller's additional term.
                // Preserve it; never substitute a final seed for this argument.
                effective=nativeArgument+static_cast<std::uint32_t>(counter);
                const auto clockXor=ClockXor(clock);scope.value=unsigned(clockXor)+1;
                event.generation=generation;event.counter=counter;event.argument=effective;
                std::memcpy(&event.clock,clock.data(),clock.size());event.seed=Initialize(clockXor,effective).seed;
                g_scope=&scope;apply=true;
            }
        }
    }
    __try {
#ifdef FFXHOOKS_TESTING
        if(apply&&g_testSource.committed)g_testSource.committed(g_testSource.context);
#endif
        SetLastError(incomingError);result=original(effective);completed=true;
    } __finally {
        const DWORD nativeError=GetLastError();
        g_scope=previous;
        if(entered){
            if(apply){
                if(!completed)Unavailable(Reason::NativeInitializationFailed);
                else if(scope.consumed!=1)Unavailable(Reason::ClockCallMismatch);
                else {++g_initializations;Queue(event);if(g_accepting.load(std::memory_order_acquire))SetCode(Code::Active);}
            }
            Leave();
        }
        --g_depth;
        SetLastError(nativeError);
    }
    return result;
}
bool TablesMatch() noexcept {
    std::uint8_t multipliers[sizeof(E::MultipliersBytes)]{},xors[sizeof(E::XorsBytes)]{};
    return N::Range(g_base+E::MultipliersRva,sizeof(multipliers),g_base)&&
        N::Range(g_base+E::XorsRva,sizeof(xors),g_base)&&
        N::Copy(multipliers,reinterpret_cast<void*>(g_base+E::MultipliersRva),sizeof(multipliers))&&
        N::Copy(xors,reinterpret_cast<void*>(g_base+E::XorsRva),sizeof(xors))&&
        std::memcmp(multipliers,E::MultipliersBytes,sizeof(multipliers))==0&&
        std::memcmp(xors,E::XorsBytes,sizeof(xors))==0;
}
}

bool Start(std::uintptr_t base,bool enabled,bool validateOnly,LogFunction log) noexcept {
    bool expected=false;if(!g_attempted.compare_exchange_strong(expected,true))return false;
    g_requested=enabled;g_log=log;
    if(g_stopRequested.load(std::memory_order_acquire)){SetCode(Code::Stopped);Service();return false;}
    if(!enabled){SetCode(Code::Disabled);Service();return false;}
    if(g_admin.test_and_set(std::memory_order_acquire))return false;
    g_base=base;Reason failure=Reason::None;
    if(!N::Profile(base))failure=Reason::UnsupportedProfile;
    else if(!N::Match(base,E::Initializer)||!N::Match(base,E::Clock)||!N::Match(base,E::Reset)||
            !N::Match(base,E::InitCaller)||!N::Match(base,E::ResetCaller)||!ConsumerWitnesses()||!TablesMatch())
        failure=Reason::SignatureMismatch;
    std::uint32_t channels[68]{};
    if(failure==Reason::None){
        if(!N::Range(base+E::ChannelsRva,sizeof(channels),base)||
           !N::Copy(channels,reinterpret_cast<void*>(base+E::ChannelsRva),sizeof(channels)))failure=Reason::UnsupportedProfile;
        else for(auto value:channels)if(value){failure=Reason::CounterOriginMissing;break;}
    }
    std::uint64_t raw=0,index=0;
    if(failure==Reason::None&&(!Performance(raw,g_frequency)||!NtscTickIndex(raw,g_frequency,index)))failure=Reason::ClockUnavailable;
    if(failure==Reason::None&&validateOnly)failure=Reason::ValidationOnly;
    if(failure==Reason::None&&g_stopRequested.load(std::memory_order_acquire))failure=Reason::BoundaryChanged;
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if(failure==Reason::None){
        if(MinHookBatch::EnsureProcessInitialized()!=MinHookBatch::InitializationResult::Ready||
           !g_batch.Add(base+E::InitializerRva,reinterpret_cast<void*>(&InitShim),&g_original[0])||
           !g_batch.Add(base+E::ClockRva,reinterpret_cast<void*>(&ClockShim),&g_original[1])||
           !g_batch.Add(base+E::ResetRva,reinterpret_cast<void*>(&ResetShim),&g_original[2])){
            g_batch.DiscardUnpublished();failure=Reason::HookConflict;
        }else if(g_stopRequested.load(std::memory_order_acquire)){
            g_batch.DiscardUnpublished();failure=Reason::BoundaryChanged;
        }else if(!g_batch.Publish(MinHookBatch::Owner::OriginalPs2Rng,reinterpret_cast<void*>(&InitShim))){
            g_batch.Neutralize();failure=Reason::HookConflict;
        }
    }
#else
    if(failure==Reason::None)failure=Reason::HookConflict;
#endif
    if(g_stopRequested.load(std::memory_order_acquire)){
        g_accepting.store(false,std::memory_order_release);
        SetCode(g_active.load()==0&&g_batch.Neutralize()?Code::Stopped:Code::StopPending);
    }else if(failure==Reason::None){
        SetCode(Code::AwaitingReset);g_accepting.store(true,std::memory_order_release);
        if(g_stopRequested.load(std::memory_order_acquire)){
            g_accepting.store(false,std::memory_order_release);SetCode(Code::StopPending);
        }
    }
    else Unavailable(failure);
    g_admin.clear(std::memory_order_release);Service();return failure==Reason::None&&!g_stopRequested.load();
}
Snapshot Status() noexcept {return {g_code.load(),g_reason.load(),g_requested.load(),g_owner.load(),g_resets.load(),g_initializations.load()};}
const char* Detail() noexcept {
    const auto s=Status();
    if(s.code==Code::Disabled)return "Original PS2 RNG is off; changes require restarting the game.";
    if(s.code==Code::AwaitingReset)return "Original PS2 RNG: waiting for the native counter reset.";
    if(s.code==Code::AwaitingBoundary)return "Original PS2 RNG: waiting for native RNG initialization.";
    if(s.code==Code::Active)return "Experimental PS2 RNG initialized using the nominal NTSC clock.";
    if(s.code==Code::StopPending)return "Original PS2 RNG: shutdown pending; an initialization is still active.";
    if(s.code==Code::Stopped)return "Original PS2 RNG stopped; consumed RNG history is unchanged.";
    switch(s.reason){
    case Reason::CounterOriginMissing:return "Unavailable: the native counter reset was not observed. Restart the game.";
    case Reason::WrongThread:return "Unavailable: RNG initialization changed its owner thread.";
    case Reason::ForeignCounter:return "Unavailable: another source already advances the native counter.";
    case Reason::ClockUnavailable:return "Unavailable: the reference clock input could not be validated.";
    case Reason::UnsupportedProfile:return "Unavailable executable.";
    case Reason::SignatureMismatch:case Reason::HookConflict:return "Unavailable: RNG modification detected.";
    case Reason::ValidationOnly:return "Original PS2 RNG validation only; no hooks installed.";
    default:return "Unavailable: native RNG initialization did not satisfy its boundary contract.";
    }
}
void Service() noexcept {
    static std::atomic<unsigned> published{~0u};
    const auto revision=g_revision.load(std::memory_order_acquire);
    if(published.exchange(revision)!=revision){
        const auto s=Status();auto availability=F8RuntimeAvailability::Available;
        if(s.code==Code::Unavailable)availability=s.reason==Reason::UnsupportedProfile?F8RuntimeAvailability::UnsupportedBuild:
            s.reason==Reason::SignatureMismatch?F8RuntimeAvailability::SignatureMismatch:
            s.reason==Reason::HookConflict||s.reason==Reason::ForeignCounter?F8RuntimeAvailability::Conflict:F8RuntimeAvailability::ProducerUnavailable;
        else if(s.code==Code::AwaitingReset||s.code==Code::AwaitingBoundary)availability=F8RuntimeAvailability::Pending;
        else if(s.code==Code::StopPending)availability=F8RuntimeAvailability::RestorePending;
        PublishF8RuntimeStatus(Key,availability,true,s.code==Code::Active);
        if(g_log)g_log(Detail());
    }
    Event events[8]{};unsigned count=0;
    if(TryAcquireSRWLockExclusive(&g_eventLock)){
        while(g_eventRead!=g_eventWrite&&count<8){events[count++]=g_events[g_eventRead%8];++g_eventRead;}
        ReleaseSRWLockExclusive(&g_eventLock);
    }
    if(g_log)for(unsigned i=0;i<count;++i){char text[256]{};const auto& e=events[i];
        std::snprintf(text,sizeof(text),"[OriginalPs2Rng] generation=%llu counter=%llu argument=0x%08X clock=0x%016llX seed=0x%08X nativeCalls=1",
            static_cast<unsigned long long>(e.generation),static_cast<unsigned long long>(e.counter),e.argument,
            static_cast<unsigned long long>(e.clock),e.seed);g_log(text);
    }
}
void RequestStop() noexcept {
    g_stopRequested.store(true,std::memory_order_release);
    g_accepting.store(false,std::memory_order_release);
    if(g_requested.load(std::memory_order_acquire)&&g_code.load(std::memory_order_acquire)!=Code::Stopped){
        g_code.store(Code::StopPending,std::memory_order_release);
        g_revision.fetch_add(1,std::memory_order_release);
    }
}
bool Stop() noexcept {
    RequestStop();if(g_active.load(std::memory_order_acquire))return false;
    if(g_admin.test_and_set(std::memory_order_acquire))return false;
    const bool stopped=g_batch.Neutralize();if(stopped)SetCode(g_requested.load()?Code::Stopped:Code::Disabled);
    g_admin.clear(std::memory_order_release);Service();return stopped;
}
#ifdef FFXHOOKS_TESTING
bool SetInputSourceForTests(const InputSource& value) noexcept {
    if(g_attempted.load()||!value.performance||!value.calendar)return false;g_testSource=value;return true;
}
#endif
} // namespace FfxHooks::OriginalPs2Rng::Runtime
