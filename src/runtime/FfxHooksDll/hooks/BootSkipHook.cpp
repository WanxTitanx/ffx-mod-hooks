#include "BootSkipHook.h"
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#endif
#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <polyhook2/Detour/x86Detour.hpp>
#endif

namespace FfxHooks::Fastload {
namespace {
Publication g_publication;
std::atomic<uint32_t> g_installAttempt{0},g_openingCalls{0},g_sceneCalls{0},g_callbacks{0};
std::atomic<uint32_t> g_ready{0},g_elapsed{0},g_dropped{0},g_sceneThread{0},g_actionGeneration{0};
std::atomic<bool> g_gate{false},g_validatedOnly{false},g_observeOnly{true};
std::atomic<OpeningStatus> g_openingStatus{OpeningStatus::Disabled};
LogFn g_log=nullptr;
EdgeQueue g_edges;
#ifdef FFXHOOKS_HAVE_POLYHOOK
uintptr_t g_base=0;
uint32_t g_startedMs=0;
PLH::x86Detour* g_sceneDetour=nullptr;
PLH::x86Detour* g_openingDetour=nullptr;
alignas(8) uint64_t g_sceneTrampoline=0;
alignas(8) uint64_t g_openingTrampoline=0;
std::atomic_flag g_sampleBusy=ATOMIC_FLAG_INIT;
Sample g_previous{};
State g_policy{}; // Only accessed by the serialized callback producer after installation.
bool g_havePrevious=false,g_sawTitle=false;
unsigned g_observedStage=0;
uint32_t g_serial=0;
// A deadline and its reason are read together by the worker and callback producer.
std::atomic<uint64_t> g_deadline{0};
static_assert(std::atomic<uint64_t>::is_always_lock_free,"Fastload deadline must be lock-free");

void SetDeadline(uint32_t now,Failure reason) noexcept {
    g_deadline.store(static_cast<uint64_t>(now)|(static_cast<uint64_t>(reason)<<32),std::memory_order_release);
}
void CheckDeadline(uint32_t now) noexcept {
    if(IsTerminal(g_publication.Read().phase))return;
    const uint64_t deadline=g_deadline.load(std::memory_order_acquire);
    const auto reason=static_cast<Failure>(deadline>>32);
    const uint32_t limit=reason==Failure::LoadTimeout?kLoadDeadlineMs:
        reason==Failure::FieldTimeout?kFieldDeadlineMs:kTitleDeadlineMs;
    if(static_cast<uint32_t>(now-static_cast<uint32_t>(deadline))>=limit)
        g_publication.Publish(Phase::FailedVisible,reason);
}
// Installer only: every dereference is bounded by PE size, mapped image ownership,
// page access, and an SEH leaf. Callback samples use only these immutable spans.
bool ReadableSpan(uintptr_t address,size_t length,uintptr_t imageBase,bool executable=false,bool writable=false) {
    if(!address||!length||address>UINT32_MAX-length)return false;
    const uintptr_t end=address+length;
    while(address<end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if(!VirtualQuery(reinterpret_cast<const void*>(address),&mbi,sizeof(mbi)) ||
           mbi.State!=MEM_COMMIT || mbi.Type!=MEM_IMAGE ||
           reinterpret_cast<uintptr_t>(mbi.AllocationBase)!=imageBase ||
           (mbi.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const DWORD protection=mbi.Protect&0xFFu;
        const bool readable=protection==PAGE_READONLY||protection==PAGE_READWRITE||protection==PAGE_WRITECOPY||
            protection==PAGE_EXECUTE_READ||protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY;
        const bool code=protection==PAGE_EXECUTE_READ||protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY;
        const bool write=protection==PAGE_READWRITE||protection==PAGE_WRITECOPY||protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY;
        if(!readable||(executable&&!code)||(writable&&!write))return false;
        const uintptr_t next=reinterpret_cast<uintptr_t>(mbi.BaseAddress)+mbi.RegionSize;
        if(next<=address)return false;
        address=next;
    }
    return true;
}
bool CopyEvidence(uintptr_t address,uint8_t* out,size_t length) noexcept {
    __try {for(size_t i=0;i<length;++i)out[i]=reinterpret_cast<const volatile uint8_t*>(address)[i];return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ReadObservedMemory(void*, uint32_t rva, void* output, size_t width) noexcept {
    __try {
        std::memcpy(output, reinterpret_cast<const void*>(g_base + rva), width);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallbackReady() noexcept {
    if(IsTerminal(g_publication.Read().phase))return false;
    const uint32_t required=g_observeOnly.load(std::memory_order_acquire)?1u:3u;
    return (g_ready.load(std::memory_order_acquire)&required)==required;
}
bool AdmitCallbackThread() noexcept {
    const uint32_t thread=GetCurrentThreadId();uint32_t expected=0;
    g_sceneThread.compare_exchange_strong(expected,thread,std::memory_order_acq_rel);
    if(g_sceneThread.load(std::memory_order_acquire)==thread)return true;
    g_publication.Publish(Phase::FailedVisible,Failure::ThreadConflict);return false;
}
bool ShiftHeld() noexcept {
    return (GetAsyncKeyState(VK_LSHIFT)&0x8000)!=0 || (GetAsyncKeyState(VK_RSHIFT)&0x8000)!=0;
}
OpeningEvidence ReadOpeningEvidence() noexcept {
    if(IsTerminal(g_publication.Read().phase) || !CallbackReady() || !AdmitCallbackThread())return {};
    uint8_t active = 0;
    const bool readable = ReadObservedMemory(nullptr, RVA_FFX_FASTLOAD_OPENING_WAIT, &active, sizeof(active));
    return {readable, readable && active == 1, true};
}
static bool SampleGame(Sample* sample) noexcept {
    if (!ReadObservedFields({nullptr, &ReadObservedMemory, g_base}, sample)) return false;
    sample->nowMs=GetTickCount();
    // Only invoked on the validated inner field/save tick, whose thread owns load admission.
    sample->fieldSystemReady=true;
    if(sample->sceneId==kTitleSceneId && sample->controlledCharacter==0)
        sample->shiftBypassHeld=ShiftHeld();
    return true;
}
bool ActionAdmitted(void*) noexcept {
    if(!CallbackReady() || g_observeOnly.load(std::memory_order_acquire) ||
       g_sceneThread.load(std::memory_order_acquire)!=GetCurrentThreadId())return false;
    const auto phase=g_publication.Read().phase;
    if((phase==Phase::WaitingForOpening || phase==Phase::WaitingForTitle) && ShiftHeld()) {
        g_publication.Publish(Phase::Bypassed,Failure::None);return false;
    }
    return true;
}
bool ValidateActionCode(void*,uint32_t action) noexcept {
    for(const auto& span:kCodeSpans) {
        if(span.target==Target::SceneTick || span.target==Target::OpeningLoader)continue; // Our own detours.
        if(action==ActionFinishOpening && span.target!=Target::OpeningFinish)continue;
        uint8_t bytes[128]={};
        if(!CopyEvidence(g_base+span.rva,bytes,span.size) ||
           ValidateTarget(span.target,bytes,span.size,g_base)!=TargetStatus::Match)return false;
    }
    return true;
}
bool ActionSample(void*,Sample* sample) noexcept {return SampleGame(sample);}
bool ActionOpeningWait(void*,bool* active) noexcept {
    uint8_t value=0;
    if(!ReadObservedMemory(nullptr,RVA_FFX_FASTLOAD_OPENING_WAIT,&value,sizeof(value)))return false;
    *active=value==1;return value<=1;
}
// SEH leaves contain no C++ objects requiring unwind. Native helpers are validated
// scalar branches; file reading remains in the subsequent vanilla tick.
bool NativeFinishOpening(void*) noexcept {
    if(!ActionAdmitted(nullptr))return false;
    __try {reinterpret_cast<void(__cdecl*)()>(g_base+RVA_FFX_FASTLOAD_OPENING_FINISH)();return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool NativeCompareTitleReply(void*,uint32_t rva,uint8_t expected,uint8_t desired) noexcept {
    if(rva<0x18||!KnownAnswer(rva-0x18))return false;
    __try {return static_cast<uint8_t>(_InterlockedCompareExchange8(reinterpret_cast<volatile CHAR*>(g_base+rva),
                static_cast<CHAR>(desired),static_cast<CHAR>(expected)))==expected;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool NativeCompareTitleHeader(void*,uint32_t rva,uint64_t expected,uint64_t desired) noexcept {
    if(!KnownAnswer(rva)||(g_base+rva)%8!=0)return false;
    __try {return static_cast<uint64_t>(InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(g_base+rva),
                static_cast<LONG64>(desired),static_cast<LONG64>(expected)))==expected;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool NativeRequestLoad(void*,uint32_t command) noexcept {
    if(command!=kNativeLoadCommand || !ActionAdmitted(nullptr))return false;
    return PublishTitleLoad({nullptr,ActionAdmitted,ActionSample,NativeCompareTitleReply,NativeCompareTitleHeader});
}
bool NativeCompareUi(void*,int32_t expected,int32_t desired) noexcept {
    if(expected!=kSaveUiListReady || desired!=kSaveUiOpenRead || !ActionAdmitted(nullptr))return false;
    __try {return InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(g_base+RVA_FFX_FASTLOAD_UI_STATE),desired,expected)==expected;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
const ActionIo kActionIo{nullptr,ActionAdmitted,ValidateActionCode,ActionSample,ActionOpeningWait,
    NativeFinishOpening,NativeRequestLoad,NativeCompareUi};
bool SameSample(const Sample& a,const Sample& b) noexcept {
    return a.sceneId==b.sceneId&&a.controlledCharacter==b.controlledCharacter&&
        a.activeSceneId==b.activeSceneId&&a.titleChoiceState==b.titleChoiceState&&
        a.titleChoiceFlags==b.titleChoiceFlags&&a.messageBank==b.messageBank&&
        a.titleAnswerRva==b.titleAnswerRva&&a.titleAnswerHeader==b.titleAnswerHeader&&
        a.titleWindowPhase==b.titleWindowPhase&&a.titleAnswerResult==b.titleAnswerResult&&
        a.saveUiState==b.saveUiState&&a.slotZeroRecord==b.slotZeroRecord&&a.selectedPage==b.selectedPage&&a.selectedRow==b.selectedRow&&
        a.saveLoadScreenState==b.saveLoadScreenState&&a.saveLoadDialogState==b.saveLoadDialogState&&a.saveDirection==b.saveDirection&&
        a.currentMenu==b.currentMenu&&a.pendingMenu==b.pendingMenu&&a.selectLoad==b.selectLoad&&
        a.sceneTransitionPending==b.sceneTransitionPending&&a.shiftBypassHeld==b.shiftBypassHeld;
}
static void ObserveSample(const Sample& sample,EdgeKind kind) noexcept {
    if(!CallbackReady())return;
    if(g_sampleBusy.test_and_set(std::memory_order_acquire)) {
        g_dropped.fetch_add(1,std::memory_order_relaxed);
        g_publication.Publish(Phase::FailedVisible,Failure::ThreadConflict);return;
    }
    struct Unlock {~Unlock(){g_sampleBusy.clear(std::memory_order_release);}} unlock;
    if(!CallbackReady())return;
    // Only this producer can fill the queue. Reserve capacity before any effect;
    // the worker can only make additional room while the action is admitted.
    if(!g_edges.HasCapacity()) {
        g_dropped.fetch_add(1,std::memory_order_relaxed);
        g_publication.Publish(Phase::FailedVisible,Failure::TelemetryOverflow);return;
    }
    uint32_t actions=ActionNone;
    ActionResult result=ActionResult::None;
    if(g_observeOnly.load(std::memory_order_acquire)) {
        if(kind!=EdgeKind::OpeningReturn) {
            if(sample.shiftBypassHeld)g_publication.Publish(Phase::Bypassed,Failure::None);
            if(sample.sceneId==kTitleSceneId)g_sawTitle=true;
            if(g_sawTitle&&g_observedStage==0&&(sample.saveLoadScreenState!=0||sample.saveUiState==11)) {
                g_observedStage=1;SetDeadline(sample.nowMs,Failure::LoadTimeout);
            }
            if(g_observedStage==1&&(sample.saveUiState==14||sample.selectLoad!=0)) {
                g_observedStage=2;SetDeadline(sample.nowMs,Failure::FieldTimeout);
            }
            if(sample.saveUiState==kSaveUiInvalid)g_publication.Publish(Phase::FailedVisible,Failure::VanillaRejected);
            if(g_sawTitle&&sample.sceneId!=0&&sample.sceneId!=kTitleSceneId&&sample.controlledCharacter!=0)
                g_publication.Publish(Phase::Succeeded,Failure::None);
        }
    } else {
        const auto decision=Advance(g_policy,sample);
        g_policy=decision.state;actions=decision.actions;
        if(actions==ActionRequestVanillaLoad)SetDeadline(sample.nowMs,Failure::LoadTimeout);
        if(actions==ActionAdvanceAutosaveToRead)SetDeadline(sample.nowMs,Failure::FieldTimeout);
        g_actionGeneration.store(g_policy.actionGeneration,std::memory_order_release);
        const bool published=g_publication.Publish(g_policy.phase,g_policy.failure);
        if(actions && published) {
            const auto outcome=ExecuteAction(actions,g_observeOnly.load(std::memory_order_acquire),kActionIo);
            result=outcome.result;
            if(result==ActionResult::Accepted) {
                Sample acknowledged=sample;acknowledged.actionResult=result;
                acknowledged.actionGeneration=g_policy.actionGeneration;
                g_policy=Advance(g_policy,acknowledged).state;
            } else g_policy=Fail(g_policy,outcome.failure==Failure::None?Failure::UnexpectedState:outcome.failure).state;
            g_publication.Publish(g_policy.phase,g_policy.failure);
        }
    }
    if(actions || kind==EdgeKind::OpeningReturn || !g_havePrevious || !SameSample(sample,g_previous)) {
        Edge edge{};edge.serial=++g_serial;edge.kind=kind;edge.sample=sample;edge.actions=actions;edge.result=result;
        edge.sample.actionGeneration=g_actionGeneration.load(std::memory_order_acquire);
        if(!g_edges.Push(edge)) {
            g_dropped.fetch_add(1,std::memory_order_relaxed);
            g_publication.Publish(Phase::FailedVisible,Failure::TelemetryOverflow);
        }
        if(kind!=EdgeKind::OpeningReturn){g_previous=sample;g_havePrevious=true;}
    }
    if(g_publication.Read().phase==Phase::Succeeded) {
        OpeningStatus ready=OpeningStatus::Ready;
        g_openingStatus.compare_exchange_strong(ready,OpeningStatus::Missed,std::memory_order_acq_rel);
    }
    g_elapsed.store(static_cast<uint32_t>(sample.nowMs-g_startedMs),std::memory_order_relaxed);
    CheckDeadline(sample.nowMs);
}
struct CallbackScope {
    CallbackScope() noexcept {g_callbacks.fetch_add(1,std::memory_order_acq_rel);}
    ~CallbackScope(){g_callbacks.fetch_sub(1,std::memory_order_release);}
};
void ObserveScene(bool post) noexcept {
    if(!CallbackReady() || !AdmitCallbackThread())return;
    Sample sample{};
    if(!SampleGame(&sample)){g_publication.Publish(Phase::FailedVisible,Failure::ReadFault);return;}
    ObserveSample(sample,post?EdgeKind::SceneAfter:EdgeKind::SceneBefore);
}
using SceneTickFn=void(__cdecl*)();
using OpeningFn=void(__fastcall*)(void*,void*);
static void __cdecl SceneTickShim() {
    CallbackScope callback;
    const auto original=reinterpret_cast<SceneTickFn>(static_cast<uintptr_t>(g_sceneTrampoline));
    g_sceneCalls.fetch_add(1,std::memory_order_relaxed);
    RunSceneObserver(original,ObserveScene);
}
static void __fastcall OpeningShim(void* self,void* edx) {
    CallbackScope callback;
    const auto original=reinterpret_cast<OpeningFn>(static_cast<uintptr_t>(g_openingTrampoline));
    // The caller ignores EAX; the loader's success branch sets the validated wait byte.
    RunOpeningObserver([&]() noexcept {original(self,edx);}, &ReadOpeningEvidence,
        [](OpeningEvidence evidence) noexcept {
            g_openingCalls.fetch_add(1,std::memory_order_relaxed);
            if(!CallbackReady() || !evidence.sampled)return;
            if(!evidence.readable) {g_publication.Publish(Phase::FailedVisible,Failure::ReadFault);return;}
            g_openingStatus.store(OpeningStatus::Observed,std::memory_order_release);
            Sample sample{};sample.nowMs=GetTickCount();sample.openingReady=evidence.active;sample.shiftBypassHeld=ShiftHeld();
            ObserveSample(sample,EdgeKind::OpeningReturn);
        });
}

#endif
} // namespace

InstallResult InstallFastloadHook(uintptr_t base,LogFn log,const InstallOptions& options) {
    if(!options.gateEnabled)return {}; // OFF does not read game memory or touch feature state.
    uint32_t expected=0;
    if(!g_installAttempt.compare_exchange_strong(expected,1,std::memory_order_acq_rel))return {InstallCode::Retained,g_publication.Read().failure};
    g_gate.store(true,std::memory_order_release);g_log=log;
    g_observeOnly.store(options.observeOnly,std::memory_order_release);
    if(g_publication.Stopped())return {InstallCode::Retained,Failure::None};
    if(options.startupShiftHeld) {g_publication.Publish(Phase::Bypassed,Failure::None);return {InstallCode::Bypassed,Failure::None};}
#ifdef FFXHOOKS_HAVE_POLYHOOK
    g_base=base;g_startedMs=GetTickCount();SetDeadline(g_startedMs,Failure::TitleTimeout);
    uint8_t header[0x1000]={};
    F8Runtime::ExecutableIdentity identity{};
    Failure failure=Failure::UnsupportedProfile;
    if(ReadableSpan(base,sizeof(header),base)&&CopyEvidence(base,header,sizeof(header))&&
        F8Runtime::ParseExecutableIdentity(header,sizeof(header),&identity)==F8Runtime::ProfileResult::Supported)
        failure=ValidateProfileAndRanges(identity,base);
    if(failure==Failure::None) for(const auto& span:kReadSpans)
        if(!ReadableSpan(base+span.rva,span.width,base)){failure=Failure::TargetOutOfRange;break;}
    if(failure==Failure::None && !options.observeOnly)for(const auto& span:kWriteSpans)
        if(!ReadableSpan(base+span.rva,span.width,base,false,true)){failure=Failure::TargetOutOfRange;break;}
    // All target bytes must match before either gateway is constructed. Never follow a foreign jump.
    if(failure==Failure::None) for(const auto& span:kCodeSpans) {
        uint8_t bytes[128]={};
        if(!ReadableSpan(base+span.rva,span.size,base,true)||!CopyEvidence(base+span.rva,bytes,span.size)) {failure=Failure::TargetOutOfRange;break;}
        const auto checked=ValidateTarget(span.target,bytes,span.size,base);
        if(checked!=TargetStatus::Match) {failure=checked==TargetStatus::Conflict?Failure::TargetConflict:Failure::SignatureMismatch;break;}
    }
    if(failure!=Failure::None){g_publication.Publish(Phase::FailedVisible,failure);return {InstallCode::Failed,failure};}
    if(options.validateOnly){g_validatedOnly.store(true,std::memory_order_release);return {InstallCode::ValidatedOnly,Failure::None};}
    if(g_publication.Stopped())return {InstallCode::Retained,Failure::None};
    g_policy=Start(true,options.observeOnly,g_startedMs);
    if(!g_publication.Publish(g_policy.phase,Failure::None))return {InstallCode::Retained,Failure::None};
    try {
        // Installed PolyHook2 2025-06-21 publishes *userTrampVar before writing its target
        // jump (x86Detour.cpp:73 vs :81). Aligned gateways and objects live for the process.
        g_sceneDetour=new PLH::x86Detour(static_cast<uint64_t>(base+RVA_FFX_FASTLOAD_SCENE_TICK),
            reinterpret_cast<uint64_t>(&SceneTickShim),&g_sceneTrampoline);
        if(!g_sceneDetour->hook()||!g_sceneTrampoline)throw InstallCode::Failed;
        g_ready.fetch_or(1u,std::memory_order_release);
        if(IsTerminal(g_publication.Read().phase))
            return {InstallCode::Retained,g_publication.Read().failure};
        g_openingDetour=new PLH::x86Detour(static_cast<uint64_t>(base+RVA_FFX_FASTLOAD_OPENING_LOADER),
            reinterpret_cast<uint64_t>(&OpeningShim),&g_openingTrampoline);
        if(!g_openingDetour->hook()||!g_openingTrampoline)throw InstallCode::Failed;
        g_ready.fetch_or(2u,std::memory_order_release);
        OpeningStatus disabled=OpeningStatus::Disabled;
        g_openingStatus.compare_exchange_strong(disabled,OpeningStatus::Ready,std::memory_order_acq_rel);
    } catch(...) {
        // A failed hook may already be reachable. Retain both objects and gateways;
        // never retry or destroy them based only on a zero C++ callback count.
        g_openingStatus.store(OpeningStatus::Failed,std::memory_order_release);
        g_publication.Publish(Phase::FailedVisible,Failure::DetourFailed);
        RequestFastloadStop();return {InstallCode::Retained,Failure::DetourFailed};
    }
    const auto finalState=g_publication.Read();
    return {IsTerminal(finalState.phase)?InstallCode::Retained:
        options.observeOnly?InstallCode::Observing:InstallCode::Armed,finalState.failure};
#else
    (void)base;(void)options;
    g_publication.Publish(Phase::FailedVisible,Failure::DetourFailed);
    return {InstallCode::NoPolyHook,Failure::DetourFailed};
#endif
}
void RequestFastloadStop() noexcept {g_publication.RequestStop();}
void RemoveFastloadHook() noexcept {
    RequestFastloadStop();
    // Normal-context retirement is inert retention, as with Speed. PolyHook unhook frees
    // its gateway; even a zero callback count cannot prove no suspended prologue entrant.
    // Dynamic DLL unload remains unsupported; process exit owns final reclamation.
}
RuntimeSnapshot GetRuntimeSnapshot() noexcept {
    RuntimeSnapshot s{};const auto state=g_publication.Read();s.phase=state.phase;s.failure=state.failure;
    s.observeOnly=g_observeOnly.load(std::memory_order_acquire);s.actionGeneration=g_actionGeneration.load(std::memory_order_acquire);
    s.gateEnabled=g_gate.load(std::memory_order_acquire);s.validatedOnly=g_validatedOnly.load(std::memory_order_acquire);
    const uint32_t ready=g_ready.load(std::memory_order_acquire);
    s.sceneTickReady=(ready&1u)!=0;s.openingSkipReady=(ready&2u)!=0;
    s.opening=g_openingStatus.load(std::memory_order_acquire);
    s.sceneTickCount=g_sceneCalls.load(std::memory_order_relaxed);s.openingCallbackCount=g_openingCalls.load(std::memory_order_relaxed);
    s.elapsedMs=g_elapsed.load(std::memory_order_relaxed);s.droppedEdges=g_dropped.load(std::memory_order_relaxed);
    return s;
}
bool FastloadNeedsPump() noexcept {
    // Read terminal publication first: its release includes the producer's prior
    // counter increment. Unspecified function-argument order would lose that proof.
    const auto phase=g_publication.Read().phase;
    const auto callbacks=g_callbacks.load(std::memory_order_acquire);
    return NeedsTelemetryPump(phase,callbacks);
}
void FlushFastloadTelemetry() {
    if(!g_log)return;
#ifdef FFXHOOKS_HAVE_POLYHOOK
    CheckDeadline(GetTickCount());
#endif
    Edge edge{};
    // Bound each drain even if callbacks continue producing during a flush.
    for(uint32_t i=0;i<kEdgeCapacity&&g_edges.Pop(&edge);++i) {
        const Sample& s=edge.sample;char line[768]={};
        std::snprintf(line,sizeof(line),"[ffx-hooks] Fastload trace serial=%u edge=%u ms=%u scene=%u controlled=%u screen=%d dialog=%d direction=%d ui=%d page=%d row=%d slot0=%d menu=%d pending=%d selectLoad=%d openingReady=%u generation=%u actions=%u result=%u transition=%d activeScene=%u choiceState=%u choiceFlags=%u messageBank=%u answerRva=0x%08X answerState=%u answerCursor=%u answerCount=%u answer=%u windowPhase=%u\n",
            edge.serial,static_cast<unsigned>(edge.kind),s.nowMs,s.sceneId,s.controlledCharacter?1u:0u,
            s.saveLoadScreenState,s.saveLoadDialogState,s.saveDirection,s.saveUiState,s.selectedPage,s.selectedRow,
            s.slotZeroRecord,s.currentMenu,s.pendingMenu,s.selectLoad,s.openingReady?1u:0u,
            s.actionGeneration,edge.actions,static_cast<unsigned>(edge.result),s.sceneTransitionPending,
            s.activeSceneId,static_cast<unsigned>(s.titleChoiceState),static_cast<unsigned>(s.titleChoiceFlags),
            static_cast<unsigned>(s.messageBank),s.titleAnswerRva,static_cast<unsigned>(AnswerByte(s.titleAnswerHeader,1)),
            static_cast<unsigned>(AnswerByte(s.titleAnswerHeader,2)),static_cast<unsigned>(AnswerByte(s.titleAnswerHeader,7)),
            static_cast<unsigned>(s.titleAnswerResult),static_cast<unsigned>(s.titleWindowPhase));
        g_log(line);
    }
    static Phase previous=Phase::Disabled;static Failure previousFailure=Failure::None;
    const auto s=GetRuntimeSnapshot();
    if(s.phase!=previous||s.failure!=previousFailure) {
        char line[256]={};std::snprintf(line,sizeof(line),"[ffx-hooks] Fastload runtime phase=%s failure=%s observe=%u sceneTicks=%u openingCalls=%u opening=%u dropped=%u attempts=%u\n",
            PhaseName(s.phase),FailureName(s.failure),s.observeOnly?1u:0u,s.sceneTickCount,s.openingCallbackCount,
            static_cast<unsigned>(s.opening),s.droppedEdges,s.actionGeneration);
        g_log(line);previous=s.phase;previousFailure=s.failure;
    }
}
} // namespace FfxHooks::Fastload
