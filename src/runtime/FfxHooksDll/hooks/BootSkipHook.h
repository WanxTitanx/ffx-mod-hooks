#pragma once
// Jarvis-HOOK: default-OFF, restart-required Fastload through the vanilla load FSM.
#include <array>
#include <atomic>
#include "F8RuntimeCore.h"
// Exact code evidence; this validator does not read process memory or install hooks.
#include <cstddef>
#include <cstdint>
#include "../shared/ffx_addresses.h"
namespace FfxHooks::Fastload {
enum class Target : uint8_t { SceneTick, OpeningLoader, OpeningFinish, LoadCommand, LoadIdle,
    ScreenSetter, RequestReset, DirectionSetter, TickCaller, FieldLoadGate,
    ActiveSceneWriter, TitleWindowGetter, TitleChoiceWait, TitleChoicePoll,
    TitleRegistryPrimary, TitleRegistrySecondary, TitleAnswerCommit };
enum class TargetStatus : uint8_t { Match, NullInput, WrongLength, OutOfRange, Conflict, Mismatch };
inline constexpr uint8_t kSceneSignature[] = {0x55,0x8B,0xEC,0x83,0xEC,0x44,0xA1,0xD8,0x13,0xC6,0x00,0x33,0xC5,0x89,0x45,0xFC};
inline constexpr uint8_t kOpeningSignature[] = {0x56,0x57,0x8B,0xF1,0xE8,0xE7,0xC1,0xFC,0xFF,0xD9,0xE8,0x83,0xEC,0x10,0x8B,0xF8};
inline constexpr uint8_t kFinishSignature[] = {0xC6,0x05,0xC2,0xB9,0xCC,0x00,0x00,0xC3};
// These are read-only admission evidence, not extra detour targets. The outer
// tick calls the inner observer before its native Load-vs-field branch.
inline constexpr uint8_t kTickCallerSignature[] = {0xE8,0x1A,0xF3,0xFF,0xFF};
inline constexpr uint8_t kFieldLoadGateSignature[] = {
    0xE8,0xAF,0x74,0xE2,0xFF,0x85,0xC0,0x0F,0x84,0xD0,0x09,0x00,0x00
};
// Full native request bodies, pinned from the supported executable, including relative calls.
inline constexpr uint8_t kLoadCommandSignature[] = {
    0x55,0x8B,0xEC,0x56,0xE8,0x37,0x6A,0xE2,0xFF,0x8B,0x75,0x08,0x85,0xC0,0x74,0x4B,
    0x81,0xFE,0x00,0x00,0x10,0x40,0x74,0x08,0x81,0xFE,0x00,0x00,0x20,0x40,0x75,0x3B,
    0x6A,0x01,0xE8,0xF9,0x6F,0xE2,0xFF,0x83,0xC4,0x04,0xE8,0xD1,0x3C,0x09,0x00,0x81,
    0xFE,0x00,0x00,0x20,0x40,0x75,0x0E,0x5E,0xC7,0x45,0x08,0x01,0x00,0x00,0x00,0x5D,
    0xE9,0xAB,0x6F,0xE2,0xFF,0x81,0xFE,0x00,0x00,0x10,0x40,0x75,0x2F,0x5E,0xC7,0x45,
    0x08,0x00,0x00,0x00,0x00,0x5D,0xE9,0x95,0x6F,0xE2,0xFF,0x83,0x3D,0xEC,0x07,0x34,
    0x01,0x00,0x75,0x08,0x81,0xFE,0x00,0x00,0x00,0x42,0x74,0x10,0x89,0x35,0xF0,0xBB,
    0x2F,0x01,0xC7,0x05,0xF4,0xBB,0x2F,0x01,0xFF,0xFF,0xFF,0xFF,0x5E,0x5D,0xC3
};
inline constexpr uint8_t kLoadIdleSignature[] = {
    0x33,0xC0,0x39,0x05,0x94,0xB9,0xCC,0x00,0x0F,0x94,0xC0,0xC3
};
inline constexpr uint8_t kScreenSetterSignature[] = {
    0x55,0x8B,0xEC,0x8B,0x45,0x08,0xA3,0x94,0xB9,0xCC,0x00,0x85,0xC0,0x75,0x12,0x50,
    0xA3,0x98,0xB9,0xCC,0x00,0xE8,0x06,0x99,0x0A,0x00,0x8B,0xC8,0xE8,0x8F,0x9D,0x0A,
    0x00,0x5D,0xC3
};
inline constexpr uint8_t kRequestResetSignature[] = {
    0xC7,0x05,0x84,0x63,0x86,0x01,0x00,0x00,0x00,0x00,0xC3
};
inline constexpr uint8_t kDirectionSetterSignature[] = {
    0x55,0x8B,0xEC,0x8B,0x45,0x08,0xA3,0x9C,0xB9,0xCC,0x00,0x5D,0xC3
};
// Native read-only title admission evidence; no additional detours.
inline constexpr uint8_t kActiveSceneWriterSignature[] = {0x8B,0x75,0x08,0x57,0x89,0x75,0x90,0x89,0x35,0xF8,0xBB,0x2F,0x01};
inline constexpr uint8_t kTitleWindowGetterSignature[] = {0x55,0x8B,0xEC,0x8B,0x4D,0x08,0x85,0xC9,0x79,0x16,0x0F,0xBE,0x05,0x42,0x6B,0x32,0x01,0x33,0xC9,0x8D,0x04,0xC1,0x6B,0xC0,0x2C,0x05,0x30,0x6D,0x32,0x01,0x5D,0xC3,0x83,0xF9,0x07,0x7E,0x05,0xB9,0x07,0x00,0x00,0x00,0x0F,0xBE,0x05,0x42,0x6B,0x32,0x01,0x8D,0x04,0xC1,0x6B,0xC0,0x2C,0x05,0x30,0x6D,0x32,0x01,0x5D,0xC3};
inline constexpr uint8_t kTitleChoiceWaitSignature[] = {0x50,0xE8,0xF9,0x27,0x01,0x00,0x83,0xC4,0x04,0x80,0x48,0x1D,0x20};
inline constexpr uint8_t kTitleChoicePollSignature[] = {0x56,0xE8,0x8F,0x23,0x01,0x00,0x0F,0xB7,0x48,0x14,0x83,0xC4,0x04,0x83,0xCF,0xFF,0x83,0xE9,0x00,0x74,0x5D,0x83,0xE9,0x02,0x74,0x0C};
inline constexpr uint8_t kTitleRegistryPrimarySignature[]={0xC7,0x05,0xB8,0x76,0x86,0x01,0x10,0x66,0x86,0x01,0xC7,0x05,0x58,0x8A,0x86,0x01,0x40,0x79,0x86,0x01};
inline constexpr uint8_t kTitleRegistrySecondarySignature[]={0xC7,0x05,0xB8,0x76,0x86,0x01,0x90,0x6F,0x86,0x01,0xC7,0x05,0x58,0x8A,0x86,0x01,0x00,0x83,0x86,0x01};
inline constexpr uint8_t kTitleAnswerCommitSignature[]={0x0F,0xBE,0x42,0x07,0x88,0x5C,0x10,0x18,0x8B,0x04,0x8D,0x50,0x8A,0x86,0x01,0xFE,0x40,0x07,0x8B,0x0C,0x8D,0x50,0x8A,0x86,0x01,0x8A,0x41,0x07,0x3A,0x41,0x06,0x75,0x04,0xC6,0x41,0x01,0x02};
struct Relocation {size_t offset;uint32_t rva;};
struct CodeSpan {Target target;uint32_t rva;const uint8_t* signature;size_t size;std::array<Relocation,4> relocations;};
inline constexpr CodeSpan kCodeSpans[] = {
    {Target::TitleRegistryPrimary,0x004B86CAu,kTitleRegistryPrimarySignature,sizeof(kTitleRegistryPrimarySignature),{{{2,0x014676B8u},{6,0x01466610u},{12,0x01468A58u},{16,0x01467940u}}}},
    {Target::TitleRegistrySecondary,0x004B8791u,kTitleRegistrySecondarySignature,sizeof(kTitleRegistrySecondarySignature),{{{2,0x014676B8u},{6,0x01466F90u},{12,0x01468A58u},{16,0x01468300u}}}},
    {Target::TitleAnswerCommit,0x004B64CBu,kTitleAnswerCommitSignature,sizeof(kTitleAnswerCommitSignature),{{{11,0x01468A50u},{21,0x01468A50u}}}},
    {Target::ActiveSceneWriter,0x00472EA4u,kActiveSceneWriterSignature,sizeof(kActiveSceneWriterSignature),{{{9,0x00EFBBF8u}}}},
    {Target::TitleWindowGetter,0x0046BE20u,kTitleWindowGetterSignature,sizeof(kTitleWindowGetterSignature),{{{13,0x00F26B42u},{26,0x00F26D30u},{45,0x00F26B42u},{56,0x00F26D30u}}}},
    {Target::TitleChoiceWait,0x00459621u,kTitleChoiceWaitSignature,sizeof(kTitleChoiceWaitSignature),{}},
    {Target::TitleChoicePoll,0x00459A8Bu,kTitleChoicePollSignature,sizeof(kTitleChoicePollSignature),{}},
    {Target::SceneTick,RVA_FFX_FASTLOAD_SCENE_TICK,kSceneSignature,sizeof(kSceneSignature),{{{7,RVA_FFX_FASTLOAD_SECURITY_COOKIE},{0,0},{0,0}}}},
    {Target::OpeningLoader,RVA_FFX_FASTLOAD_OPENING_LOADER,kOpeningSignature,sizeof(kOpeningSignature),{}},
    {Target::OpeningFinish,RVA_FFX_FASTLOAD_OPENING_FINISH,kFinishSignature,sizeof(kFinishSignature),{{{2,RVA_FFX_FASTLOAD_OPENING_WAIT},{0,0},{0,0}}}},
    {Target::LoadCommand,RVA_FFX_FASTLOAD_LOAD_COMMAND,kLoadCommandSignature,sizeof(kLoadCommandSignature),{{{93,0x00F407ECu},{110,0x00EFBBF0u},{116,0x00EFBBF4u}}}},
    {Target::LoadIdle,RVA_FFX_FASTLOAD_LOAD_IDLE,kLoadIdleSignature,sizeof(kLoadIdleSignature),{{{4,0x008CB994u},{0,0},{0,0}}}},
    {Target::ScreenSetter,RVA_FFX_FASTLOAD_SCREEN_SETTER,kScreenSetterSignature,sizeof(kScreenSetterSignature),{{{7,0x008CB994u},{17,0x008CB998u},{0,0}}}},
    {Target::RequestReset,RVA_FFX_FASTLOAD_REQUEST_RESET,kRequestResetSignature,sizeof(kRequestResetSignature),{{{2,0x01466384u},{0,0},{0,0}}}},
    {Target::DirectionSetter,RVA_FFX_FASTLOAD_DIRECTION_SETTER,kDirectionSetterSignature,sizeof(kDirectionSetterSignature),{{{7,0x008CB99Cu},{0,0},{0,0}}}},
    {Target::TickCaller,RVA_FFX_FASTLOAD_TICK_CALLER,kTickCallerSignature,sizeof(kTickCallerSignature),{}},
    {Target::FieldLoadGate,RVA_FFX_FASTLOAD_FIELD_LOAD_GATE,kFieldLoadGateSignature,sizeof(kFieldLoadGateSignature),{}},
};
inline constexpr TargetStatus ValidateTarget(Target target,const uint8_t* bytes,size_t length,uintptr_t base) noexcept {
    if(!bytes)return TargetStatus::NullInput;
    const CodeSpan* span=nullptr;
    for(const auto& candidate:kCodeSpans)if(candidate.target==target)span=&candidate;
    if(!span)return TargetStatus::OutOfRange;
    if(length!=span->size)return TargetStatus::WrongLength;
    if(base==0||base>UINT32_MAX-0x0237D000u)return TargetStatus::OutOfRange;
    if(bytes[0]==0xE9||(bytes[0]==0xFF&&bytes[1]==0x25))return TargetStatus::Conflict;
    for(size_t i=0;i<length;++i) {
        uint8_t expected=span->signature[i];
        for(const auto& relocation:span->relocations) {
            if(relocation.rva&&i>=relocation.offset&&i<relocation.offset+4)
                expected=static_cast<uint8_t>((static_cast<uint32_t>(base)+relocation.rva)>>(8*(i-relocation.offset)));
        }
        if(bytes[i]!=expected)return TargetStatus::Mismatch;
    }
    return TargetStatus::Match;
}
// Shared offline-tested policy; production effects require fresh adapter admission.
enum class Phase : uint8_t { Disabled, ObserveOnly, WaitingForOpening, WaitingForTitle,
    RequestingVanillaLoad, WaitingForSlotScan, HandedToVanillaRead, WaitingForField,
    Succeeded, Bypassed, FailedVisible, Stopped };
enum class Failure : uint8_t { None, UnsupportedProfile, TargetOutOfRange, TargetConflict,
    SignatureMismatch, DetourFailed, AutosaveMissing, SlotMappingDrift, VanillaRejected,
    UnexpectedState, TitleTimeout, LoadTimeout, FieldTimeout, ReadFault, ThreadConflict,
    TelemetryOverflow, LoadInterrupted };
enum Action : uint32_t { ActionNone=0, ActionFinishOpening=1u<<0,
    ActionRequestVanillaLoad=1u<<1, ActionAdvanceAutosaveToRead=1u<<2 };
enum class ActionResult : uint8_t { None, Accepted, Conflict };
inline constexpr uint32_t kTitleDeadlineMs=120000, kLoadDeadlineMs=30000, kFieldDeadlineMs=120000;
inline constexpr uint32_t kTitleSceneId=23, kBootstrapSceneId=348, kNativeLoadCommand=0x40100000;
inline constexpr int32_t kAutosaveSlot=0, kSaveUiListReady=12,
    kSaveUiOpenRead=14, kSaveUiInvalid=16;
struct Sample {
    uint32_t nowMs=0, sceneId=0, controlledCharacter=0;
    uint32_t activeSceneId=UINT32_MAX;
    uint16_t titleChoiceState=0;
    uint8_t titleChoiceFlags=0,messageBank=0xFF;
    uint32_t titleAnswerRva=0;
    uint64_t titleAnswerHeader=0;
    uint16_t titleWindowPhase=0;
    uint8_t titleAnswerResult=0xFF;
    int32_t saveLoadScreenState=0, saveLoadDialogState=0, saveDirection=0, saveUiState=0;
    int32_t selectedPage=0, selectedRow=0, slotZeroRecord=-1, selectLoad=0;
    int32_t currentMenu=0, pendingMenu=0;
    int32_t sceneTransitionPending=0;
    bool fieldSystemReady=false, openingReady=false, shiftBypassHeld=false, stopRequested=false;
    ActionResult actionResult=ActionResult::None;
    uint32_t actionGeneration=0;
};
struct State {
    Phase phase=Phase::Disabled;
    Failure failure=Failure::None;
    uint32_t startedMs=0, phaseStartedMs=0, actionGeneration=0;
    bool bootstrapSeen=false, openingHookObserved=false, openingFinished=false, requestOwned=false, autosaveHandoffOwned=false;
    bool loadedSceneSeen=false, loadedReadClosed=false;
};
struct Decision { State state; uint32_t actions=ActionNone; };
inline constexpr bool IsTerminal(Phase p) noexcept {
    return p==Phase::Disabled || p==Phase::Succeeded || p==Phase::Bypassed ||
        p==Phase::FailedVisible || p==Phase::Stopped;
}
inline constexpr bool NeedsTelemetryPump(Phase phase,uint32_t callbacks) noexcept {
    // A callback may publish its terminal phase before enqueueing the final edge.
    return !IsTerminal(phase) || callbacks!=0;
}
inline constexpr State Start(bool enabled, bool observeOnly, uint32_t nowMs) noexcept {
    State s{}; s.phase=enabled ? (observeOnly ? Phase::ObserveOnly : Phase::WaitingForOpening) : Phase::Disabled;
    s.startedMs=nowMs;s.phaseStartedMs=nowMs;return s;
}
inline constexpr uint8_t AnswerByte(uint64_t header,unsigned offset) noexcept {
    return static_cast<uint8_t>(header>>(8u*offset));
}
inline constexpr bool KnownAnswer(uint32_t rva) noexcept {
    return rva==RVA_FFX_FASTLOAD_ANSWER2_PRIMARY||rva==RVA_FFX_FASTLOAD_ANSWER2_SECONDARY;
}
inline constexpr bool TitleControlIdle(const Sample& s) noexcept {
    // The title's startup worker can still request bootstrap348 after a scene23
    // roundtrip. Only its native window2 await-choice state is past that branch.
    // The saved scene alone may also precede the actual field loader's scene.
    return s.sceneId==kTitleSceneId && s.activeSceneId==kTitleSceneId &&
        s.messageBank==0 && s.titleChoiceState==2 && (s.titleChoiceFlags&0x20u)!=0 &&
        s.controlledCharacter==0 && s.fieldSystemReady && s.sceneTransitionPending==0 &&
        s.saveLoadScreenState==0 && s.saveLoadDialogState==0 && s.saveDirection==0 &&
        s.selectLoad==0 && s.currentMenu==-1 && s.pendingMenu==1 && (s.saveUiState==0 || s.saveUiState==10);
}
inline constexpr bool TitleReady(const Sample& s) noexcept {
    const auto h=s.titleAnswerHeader;
    return TitleControlIdle(s)&&KnownAnswer(s.titleAnswerRva)&&s.titleWindowPhase==3&&
        AnswerByte(h,1)==1&&AnswerByte(h,4)==0&&(AnswerByte(h,5)==2||AnswerByte(h,5)==3)&&
        AnswerByte(h,2)<AnswerByte(h,5)&&AnswerByte(h,6)==1&&AnswerByte(h,7)==0;
}
inline constexpr uint64_t LoadAnswerHeader(uint64_t header) noexcept {
    return (header&~((uint64_t(0xFF)<<8)|(uint64_t(0xFF)<<16)|(uint64_t(0xFF)<<56)))|
        (uint64_t(2)<<8)|(uint64_t(1)<<16)|(uint64_t(1)<<56);
}
inline constexpr bool LoadAnswerCommitted(const Sample& s) noexcept {
    return KnownAnswer(s.titleAnswerRva)&&AnswerByte(s.titleAnswerHeader,1)==2&&
        AnswerByte(s.titleAnswerHeader,2)==1&&AnswerByte(s.titleAnswerHeader,6)==1&&
        AnswerByte(s.titleAnswerHeader,7)==1&&s.titleAnswerResult==1;
}
inline constexpr bool AutosaveReady(const Sample& s) noexcept {
    return s.sceneId==kTitleSceneId && s.activeSceneId==kTitleSceneId &&
        s.controlledCharacter==0 && s.fieldSystemReady && s.sceneTransitionPending==0 &&
        s.saveLoadScreenState==2 && s.saveLoadDialogState==0 && s.saveDirection==0 &&
        s.selectLoad==0 && s.saveUiState==kSaveUiListReady &&
        s.slotZeroRecord==kAutosaveSlot && s.selectedPage==0 && s.selectedRow==0;
}
inline constexpr Decision Fail(State state, Failure failure) noexcept {
    state.phase=Phase::FailedVisible;state.failure=failure;return {state,ActionNone};
}
inline constexpr Decision Advance(State state, const Sample& s) noexcept {
    if(IsTerminal(state.phase)) return {state,ActionNone};
    if(s.stopRequested) {state.phase=Phase::Stopped;return {state,ActionNone};}
    const bool beforeRequest=state.phase==Phase::ObserveOnly || state.phase==Phase::WaitingForOpening || state.phase==Phase::WaitingForTitle;
    if(beforeRequest && s.shiftBypassHeld) {state.phase=Phase::Bypassed;return {state,ActionNone};}
    if(beforeRequest && static_cast<uint32_t>(s.nowMs-state.startedMs)>=kTitleDeadlineMs)
        return Fail(state,Failure::TitleTimeout);
    if((state.phase==Phase::RequestingVanillaLoad || state.phase==Phase::WaitingForSlotScan) &&
        static_cast<uint32_t>(s.nowMs-state.phaseStartedMs)>=kLoadDeadlineMs) return Fail(state,Failure::LoadTimeout);
    if((state.phase==Phase::HandedToVanillaRead || state.phase==Phase::WaitingForField) &&
        static_cast<uint32_t>(s.nowMs-state.phaseStartedMs)>=kFieldDeadlineMs) return Fail(state,Failure::FieldTimeout);
    if(state.phase==Phase::ObserveOnly) return {state,ActionNone};
    if(beforeRequest) {
        if(s.sceneId==kBootstrapSceneId && s.controlledCharacter==0)state.bootstrapSeen=true;
        if(s.openingReady && !state.openingFinished) {
            state.openingHookObserved=true;state.openingFinished=true;state.phase=Phase::WaitingForTitle;
            ++state.actionGeneration;return {state,ActionFinishOpening};
        }
        // Seeing bootstrap is necessary but does not prove the returned title
        // finished initializing. TitleReady also requires native choice admission.
        if(state.bootstrapSeen && TitleReady(s)) {
            state.phase=Phase::RequestingVanillaLoad;state.phaseStartedMs=s.nowMs;
            ++state.actionGeneration;return {state,ActionRequestVanillaLoad};
        }
        return {state,ActionNone};
    }
    if(s.actionResult!=ActionResult::None && s.actionGeneration==state.actionGeneration &&
        (state.phase==Phase::RequestingVanillaLoad || state.phase==Phase::HandedToVanillaRead)) {
        if(s.actionResult==ActionResult::Conflict) return Fail(state,Failure::UnexpectedState);
        if(state.phase==Phase::RequestingVanillaLoad) {state.requestOwned=true;state.phase=Phase::WaitingForSlotScan;}
        else {state.autosaveHandoffOwned=false;state.requestOwned=false;state.phase=Phase::WaitingForField;}
        return {state,ActionNone};
    }
    if(state.phase==Phase::RequestingVanillaLoad || state.phase==Phase::HandedToVanillaRead) return {state,ActionNone};
    if(s.saveUiState==kSaveUiInvalid) return Fail(state,Failure::VanillaRejected);
    if(state.phase==Phase::WaitingForSlotScan) {
        if(s.saveUiState==kSaveUiListReady) {
            if(s.slotZeroRecord<0) return Fail(state,Failure::AutosaveMissing);
            if(s.slotZeroRecord!=kAutosaveSlot || s.selectedPage!=0 || s.selectedRow!=0 || s.saveDirection!=0)
                return Fail(state,Failure::SlotMappingDrift);
            if(!AutosaveReady(s)) return Fail(state,Failure::UnexpectedState);
            state.phase=Phase::HandedToVanillaRead;state.phaseStartedMs=s.nowMs;
            state.autosaveHandoffOwned=true;++state.actionGeneration;
            return {state,ActionAdvanceAutosaveToRead};
        }
        if(s.saveUiState!=0 && s.saveUiState!=10 && s.saveUiState!=11) return Fail(state,Failure::UnexpectedState);
    }
    if(state.phase==Phase::WaitingForField) {
        const bool startupScene=s.sceneId==kBootstrapSceneId || s.sceneId==349;
        if(startupScene || (state.loadedSceneSeen && s.sceneId==kTitleSceneId))
            return Fail(state,Failure::LoadInterrupted);
        if(state.loadedReadClosed&&(s.saveLoadScreenState!=0||s.selectLoad!=0))
            return Fail(state,Failure::LoadInterrupted);
        const bool fieldScene=s.sceneId!=0 && s.sceneId!=kTitleSceneId;
        if(fieldScene)state.loadedSceneSeen=true;
        // Hydration may expose the saved scene before the first read closes.
        // Only a new load after that witnessed close loses automatic ownership.
        if(state.loadedSceneSeen&&s.saveLoadScreenState==0&&s.selectLoad==0)state.loadedReadClosed=true;
        if(fieldScene && s.activeSceneId==s.sceneId && s.controlledCharacter!=0 && s.sceneTransitionPending==0 &&
           s.saveLoadScreenState==0 && s.selectLoad==0)state.phase=Phase::Succeeded;
    }
    return {state,ActionNone};
}
inline constexpr const char* PhaseName(Phase p) noexcept {
    switch(p) {
    case Phase::Disabled:return "DISABLED";case Phase::ObserveOnly:return "OBSERVING";
    case Phase::WaitingForOpening:return "WAIT OPENING";case Phase::WaitingForTitle:return "WAIT TITLE";
    case Phase::RequestingVanillaLoad:return "REQUEST LOAD";case Phase::WaitingForSlotScan:return "WAIT SCAN";
    case Phase::HandedToVanillaRead:return "VANILLA READ";case Phase::WaitingForField:return "WAIT FIELD";
    case Phase::Succeeded:return "SUCCEEDED";case Phase::Bypassed:return "BYPASSED (SHIFT)";
    case Phase::FailedVisible:return "FALLBACK";case Phase::Stopped:return "STOPPED";
    }return "UNKNOWN";
}
// Bounded injectable effects shared by the native adapter and offline fixtures.
struct TitleChoiceIo {
    void* context=nullptr;
    bool (*admitted)(void*) noexcept=nullptr;
    bool (*sample)(void*,Sample*) noexcept=nullptr;
    bool (*compareReply)(void*,uint32_t,uint8_t,uint8_t) noexcept=nullptr;
    bool (*compareHeader)(void*,uint32_t,uint64_t,uint64_t) noexcept=nullptr;
};
inline bool PublishTitleLoad(const TitleChoiceIo& io) noexcept {
    if(!io.admitted||!io.sample||!io.compareReply||!io.compareHeader||!io.admitted(io.context))return false;
    Sample before{};
    if(!io.sample(io.context,&before)||!TitleReady(before)||before.shiftBypassHeld||before.stopRequested)return false;
    const uint32_t address=before.titleAnswerRva;
    const auto rollback=[&]() noexcept {
        Sample current{};
        // Only undo our unused reply while the exact uncommitted header and
        // registry still belong to this action. Never undo an accepted choice.
        if(before.titleAnswerResult!=1&&io.sample(io.context,&current)&&current.titleAnswerRva==address&&
           current.titleAnswerHeader==before.titleAnswerHeader&&current.titleAnswerResult==1)
            io.compareReply(io.context,address+0x18,1,before.titleAnswerResult);
    };
    if(!io.admitted(io.context)||!io.compareReply(io.context,address+0x18,before.titleAnswerResult,1))return false;
    Sample checked{};
    if(!io.admitted(io.context)||!io.sample(io.context,&checked)||!TitleReady(checked)||
       checked.shiftBypassHeld||checked.stopRequested||checked.titleAnswerRva!=address||
       checked.titleAnswerHeader!=before.titleAnswerHeader||checked.titleAnswerResult!=1) {rollback();return false;}
    if(!io.admitted(io.context)||!io.compareHeader(io.context,address,before.titleAnswerHeader,
                                                 LoadAnswerHeader(before.titleAnswerHeader))) {rollback();return false;}
    Sample after{};
    return io.sample(io.context,&after)&&after.titleAnswerRva==address&&LoadAnswerCommitted(after);
}
struct ActionIo {
    void* context=nullptr;
    bool (*admitted)(void*) noexcept=nullptr;
    bool (*validateCode)(void*,uint32_t) noexcept=nullptr;
    bool (*sample)(void*,Sample*) noexcept=nullptr;
    bool (*openingWait)(void*,bool*) noexcept=nullptr;
    bool (*finishOpening)(void*) noexcept=nullptr;
    bool (*requestLoad)(void*,uint32_t) noexcept=nullptr;
    bool (*compareUi)(void*,int32_t,int32_t) noexcept=nullptr;
};
struct ActionOutcome {ActionResult result=ActionResult::None;Failure failure=Failure::None;};
inline ActionOutcome ExecuteAction(uint32_t action,bool observeOnly,const ActionIo& io) noexcept {
    if(observeOnly || action==ActionNone)return {};
    const ActionOutcome conflict{ActionResult::Conflict,Failure::UnexpectedState};
    if(action!=ActionFinishOpening && action!=ActionRequestVanillaLoad && action!=ActionAdvanceAutosaveToRead)
        return conflict;
    if(!io.admitted || !io.validateCode || !io.admitted(io.context))return conflict;
    if(!io.validateCode(io.context,action))return {ActionResult::Conflict,Failure::SignatureMismatch};
    if(!io.admitted(io.context))return conflict;
    if(action==ActionFinishOpening) {
        if(!io.openingWait || !io.finishOpening)return conflict;
        bool active=false;
        if(!io.openingWait(io.context,&active))return {ActionResult::Conflict,Failure::ReadFault};
        if(!active || !io.admitted(io.context) || !io.finishOpening(io.context))return conflict;
        if(!io.admitted(io.context))return conflict;
        if(!io.openingWait(io.context,&active))return {ActionResult::Conflict,Failure::ReadFault};
        return active?conflict:ActionOutcome{ActionResult::Accepted,Failure::None};
    }
    Sample before{};
    if(!io.sample || !io.sample(io.context,&before))return {ActionResult::Conflict,Failure::ReadFault};
    if(action==ActionRequestVanillaLoad) {
        if(!io.requestLoad || !TitleReady(before) || before.shiftBypassHeld || before.stopRequested ||
           !io.admitted(io.context) || !io.requestLoad(io.context,kNativeLoadCommand))return conflict;
        // The title script owns the committed Load choice. It closes its dialog
        // and issues the native request itself; this adapter never reads a save
        // while that script is still waiting for a choice.
        if(!io.admitted(io.context))return conflict;
        Sample after{};
        if(!io.sample(io.context,&after))return {ActionResult::Conflict,Failure::ReadFault};
        if(after.saveLoadScreenState!=0||!LoadAnswerCommitted(after)||after.titleAnswerRva!=before.titleAnswerRva)return conflict;
        if(!TitleControlIdle(after) || after.saveUiState!=before.saveUiState ||
           after.selectedPage!=before.selectedPage || after.selectedRow!=before.selectedRow ||
           after.slotZeroRecord!=before.slotZeroRecord)return conflict;
        return {ActionResult::Accepted,Failure::None};
    }
    if(!io.compareUi || !AutosaveReady(before) || before.stopRequested || !io.admitted(io.context) ||
       !io.compareUi(io.context,kSaveUiListReady,kSaveUiOpenRead))return conflict;
    // After the one CAS, vanilla owns the transaction. No reads or writes are
    // needed here; in particular, a later stop cannot restore UI 12.
    return {ActionResult::Accepted,Failure::None};
}
inline constexpr const char* FailureName(Failure f) noexcept {
    switch(f) {
    case Failure::None:return "NONE";case Failure::UnsupportedProfile:return "UNSUPPORTED BUILD";
    case Failure::TargetOutOfRange:return "TARGET OUT OF RANGE";case Failure::TargetConflict:return "TARGET CONFLICT";
    case Failure::SignatureMismatch:return "SIGNATURE MISMATCH";case Failure::DetourFailed:return "DETOUR FAILED";
    case Failure::AutosaveMissing:return "AUTOSAVE MISSING";case Failure::SlotMappingDrift:return "SLOT MAPPING";
    case Failure::VanillaRejected:return "CHECKSUM/LANGUAGE";case Failure::UnexpectedState:return "UNEXPECTED STATE";
    case Failure::LoadInterrupted:return "LOAD INTERRUPTED";
    case Failure::TitleTimeout:return "TITLE TIMEOUT";case Failure::LoadTimeout:return "LOAD TIMEOUT";
    case Failure::FieldTimeout:return "FIELD TIMEOUT";case Failure::ReadFault:return "READ FAULT";
    case Failure::ThreadConflict:return "THREAD CONFLICT";case Failure::TelemetryOverflow:return "TRACE OVERFLOW";
    }return "UNKNOWN";
}
struct Span { uint32_t rva; size_t width; };
inline constexpr Span kWriteSpans[] = {
    {RVA_FFX_FASTLOAD_OPENING_WAIT,1},{RVA_FFX_FASTLOAD_UI_STATE,4},
    {RVA_FFX_FASTLOAD_ANSWER2_PRIMARY,8},{RVA_FFX_FASTLOAD_ANSWER2_PRIMARY+0x18,1},
    {RVA_FFX_FASTLOAD_ANSWER2_SECONDARY,8},{RVA_FFX_FASTLOAD_ANSWER2_SECONDARY+0x18,1},
};
inline constexpr Span kReadSpans[] = {
    {RVA_FFX_SCENE_STATE_OBJECT,2}, {RVA_FFX_CONTROLLED_CHR_INSTANCE_PTR,4},
    {RVA_FFX_FASTLOAD_UI_STATE,4}, {RVA_FFX_FASTLOAD_SLOT_RECORDS,4},
    {RVA_FFX_FASTLOAD_SELECTED_PAGE,4}, {RVA_FFX_FASTLOAD_SELECTED_ROW,4},
    {RVA_FFX_FASTLOAD_SCREEN_STATE,4}, {RVA_FFX_FASTLOAD_DIALOG_STATE,4},
    {RVA_FFX_FASTLOAD_DIRECTION,4}, {RVA_FFX_CURRENT_MENU_SCREEN_ID,4},
    {RVA_FFX_FASTLOAD_PENDING_MENU,4}, {RVA_FFX_FASTLOAD_SELECT_LOAD,4},
    {RVA_FFX_FASTLOAD_OPENING_WAIT,1}, {RVA_FFX_FASTLOAD_SECURITY_COOKIE,4},
    {RVA_FFX_FASTLOAD_REQUEST_FLAG,4},
    {RVA_FFX_SCENE_TRANSITION_PENDING,4},
    {RVA_FFX_FASTLOAD_ACTIVE_SCENE,4}, {RVA_FFX_FASTLOAD_MESSAGE_BANK,1},
    {RVA_FFX_FASTLOAD_TITLE_CHOICE_STATE,2}, {RVA_FFX_FASTLOAD_TITLE_CHOICE_FLAGS,1},
    {RVA_FFX_FASTLOAD_WINDOW2_REGISTRY,4},{RVA_FFX_FASTLOAD_ANSWER2_REGISTRY,4},
    {RVA_FFX_FASTLOAD_WINDOW2_PRIMARY+0x2E,2},{RVA_FFX_FASTLOAD_WINDOW2_SECONDARY+0x2E,2},
    {RVA_FFX_FASTLOAD_ANSWER2_PRIMARY,8},{RVA_FFX_FASTLOAD_ANSWER2_PRIMARY+0x18,1},
    {RVA_FFX_FASTLOAD_ANSWER2_SECONDARY,8},{RVA_FFX_FASTLOAD_ANSWER2_SECONDARY+0x18,1},
};
inline Failure ValidateProfileAndRanges(const F8Runtime::ExecutableIdentity& identity, uintptr_t base) noexcept {
    if (!F8Runtime::IsSupportedExecutable(identity)) return Failure::UnsupportedProfile;
    if (!base || base>UINT32_MAX-identity.sizeOfImage) return Failure::TargetOutOfRange;
    for(const auto& span:kReadSpans)
        if(F8Runtime::ValidateImageRange(span.rva,span.width,identity.sizeOfImage)!=F8Runtime::ProfileResult::Supported)
            return Failure::TargetOutOfRange;
    for(const auto& span:kCodeSpans)
        if(F8Runtime::ValidateImageRange(span.rva,span.size,identity.sizeOfImage)!=F8Runtime::ProfileResult::Supported)
            return Failure::TargetOutOfRange;
    return Failure::None;
}
// Executable xrefs load sceneId as WORD at VA 0x0112CA90. Reading a DWORD
// merges an adjacent scene field, as demonstrated by the rejected live trace.
struct ObservationReader {
    void* context = nullptr;
    bool (*read)(void*, uint32_t, void*, size_t) noexcept = nullptr;
    uintptr_t imageBase=0;
};
inline bool ReadObservedFields(const ObservationReader& reader, Sample* output) noexcept {
    if (!reader.read || !output) return false;
    Sample sample{};
    uint16_t scene = 0;
    if (!reader.read(reader.context, RVA_FFX_SCENE_STATE_OBJECT, &scene, sizeof(scene))) return false;
    sample.sceneId = scene;
    struct Field { uint32_t rva; void* output; };
    const Field fields[] = {
        {RVA_FFX_CONTROLLED_CHR_INSTANCE_PTR, &sample.controlledCharacter},
        {RVA_FFX_FASTLOAD_UI_STATE, &sample.saveUiState},
        {RVA_FFX_FASTLOAD_SLOT_RECORDS, &sample.slotZeroRecord},
        {RVA_FFX_FASTLOAD_SELECTED_PAGE, &sample.selectedPage},
        {RVA_FFX_FASTLOAD_SELECTED_ROW, &sample.selectedRow},
        {RVA_FFX_FASTLOAD_SCREEN_STATE, &sample.saveLoadScreenState},
        {RVA_FFX_FASTLOAD_DIALOG_STATE, &sample.saveLoadDialogState},
        {RVA_FFX_FASTLOAD_DIRECTION, &sample.saveDirection},
        {RVA_FFX_CURRENT_MENU_SCREEN_ID, &sample.currentMenu},
        {RVA_FFX_FASTLOAD_PENDING_MENU, &sample.pendingMenu},
        {RVA_FFX_FASTLOAD_SELECT_LOAD, &sample.selectLoad},
        {RVA_FFX_SCENE_TRANSITION_PENDING, &sample.sceneTransitionPending},
        {RVA_FFX_FASTLOAD_ACTIVE_SCENE, &sample.activeSceneId},
    };
    for (const auto& field : fields) {
        if (!reader.read(reader.context, field.rva, field.output, sizeof(uint32_t))) return false;
    }
    if(!reader.read(reader.context,RVA_FFX_FASTLOAD_MESSAGE_BANK,&sample.messageBank,1)||
       !reader.read(reader.context,RVA_FFX_FASTLOAD_TITLE_CHOICE_STATE,&sample.titleChoiceState,2)||
       !reader.read(reader.context,RVA_FFX_FASTLOAD_TITLE_CHOICE_FLAGS,&sample.titleChoiceFlags,1))return false;
    if(reader.imageBase&&sample.activeSceneId==kTitleSceneId) {
        uint32_t window=0,answer=0,windowRva=0;
        if(!reader.read(reader.context,RVA_FFX_FASTLOAD_WINDOW2_REGISTRY,&window,4)||
           !reader.read(reader.context,RVA_FFX_FASTLOAD_ANSWER2_REGISTRY,&answer,4))return false;
        if(window==reader.imageBase+RVA_FFX_FASTLOAD_WINDOW2_PRIMARY&&answer==reader.imageBase+RVA_FFX_FASTLOAD_ANSWER2_PRIMARY) {
            windowRva=RVA_FFX_FASTLOAD_WINDOW2_PRIMARY;sample.titleAnswerRva=RVA_FFX_FASTLOAD_ANSWER2_PRIMARY;
        } else if(window==reader.imageBase+RVA_FFX_FASTLOAD_WINDOW2_SECONDARY&&answer==reader.imageBase+RVA_FFX_FASTLOAD_ANSWER2_SECONDARY) {
            windowRva=RVA_FFX_FASTLOAD_WINDOW2_SECONDARY;sample.titleAnswerRva=RVA_FFX_FASTLOAD_ANSWER2_SECONDARY;
        }
        if(sample.titleAnswerRva&&(!reader.read(reader.context,windowRva+0x2E,&sample.titleWindowPhase,2)||
           !reader.read(reader.context,sample.titleAnswerRva,&sample.titleAnswerHeader,8)||
           !reader.read(reader.context,sample.titleAnswerRva+0x18,&sample.titleAnswerResult,1)))return false;
    }
    *output = sample;
    return true;
}

// One packed publication makes stop absorbing even if an installer or callback races it.
class Publication {
    std::atomic<uint32_t> word_{0};
    static constexpr uint32_t kStop=0x80000000u;
public:
    bool Publish(Phase phase, Failure failure) noexcept {
        uint32_t old=word_.load(std::memory_order_acquire);
        for(unsigned attempt=0;attempt<8;++attempt) {
            const auto previous=static_cast<Phase>(old&0xFFu);
            if((old&kStop) || (IsTerminal(previous)&&previous!=Phase::Disabled)) return false;
            const uint32_t next=static_cast<uint32_t>(phase)|(static_cast<uint32_t>(failure)<<8);
            if(word_.compare_exchange_weak(old,next,std::memory_order_acq_rel)) return true;
        }
        RequestStop();return false;
    }
    void RequestStop() noexcept {word_.fetch_or(kStop,std::memory_order_release);}
    bool Stopped() const noexcept {return (word_.load(std::memory_order_acquire)&kStop)!=0;}
    State Read() const noexcept {
        const uint32_t w=word_.load(std::memory_order_acquire);
        State s{};s.phase=static_cast<Phase>(w&0xFFu);s.failure=static_cast<Failure>((w>>8)&0xFFu);
        if((w&kStop) && s.failure==Failure::None) s.phase=Phase::Stopped;
        return s;
    }
};
static_assert(std::atomic<uint32_t>::is_always_lock_free,"Fastload stop must be lock-free");
inline constexpr uint32_t kEdgeCapacity=128;
enum class EdgeKind : uint8_t { SceneBefore, SceneAfter, OpeningReturn };
struct Edge { uint32_t serial=0,actions=0; EdgeKind kind=EdgeKind::SceneBefore; Sample sample{};ActionResult result=ActionResult::None; };
// One serialized callback producer and one worker consumer. Full queues fail visibly.
class EdgeQueue {
    std::array<Edge,kEdgeCapacity> records_{};
    std::atomic<uint32_t> write_{0},read_{0};
public:
    bool HasCapacity() const noexcept {
        return static_cast<uint32_t>(write_.load(std::memory_order_relaxed)-read_.load(std::memory_order_acquire))<kEdgeCapacity;
    }
    bool Push(const Edge& edge) noexcept {
        const uint32_t w=write_.load(std::memory_order_relaxed),r=read_.load(std::memory_order_acquire);
        if(static_cast<uint32_t>(w-r)>=kEdgeCapacity)return false;
        records_[w%kEdgeCapacity]=edge;write_.store(w+1,std::memory_order_release);return true;
    }
    bool Pop(Edge* edge) noexcept {
        const uint32_t r=read_.load(std::memory_order_relaxed),w=write_.load(std::memory_order_acquire);
        if(!edge||r==w)return false;
        *edge=records_[r%kEdgeCapacity];read_.store(r+1,std::memory_order_release);return true;
    }
};
template<class Original,class Observe>
inline void RunSceneObserver(Original original,Observe observe) noexcept {
    observe(false);original();observe(true);
}
struct OpeningEvidence { bool readable=false;bool active=false;bool sampled=false; };
template<class Original, class Probe, class Observe>
inline void RunOpeningObserver(Original original, Probe probe, Observe observe) noexcept {
    original();
    observe(probe());
}
enum class InstallCode : uint8_t { Disabled, Observing, ValidatedOnly, Bypassed, Failed, Retained, NoPolyHook, Armed };
enum class OpeningStatus : uint8_t { Disabled, Ready, Observed, Missed, Failed };
struct InstallOptions {bool gateEnabled=false,validateOnly=false,startupShiftHeld=false,observeOnly=true;};
struct InstallResult {InstallCode code=InstallCode::Disabled;Failure failure=Failure::None;};
struct RuntimeSnapshot {
    Phase phase=Phase::Disabled;Failure failure=Failure::None;
    uint32_t actionGeneration=0,openingCallbackCount=0,sceneTickCount=0,elapsedMs=0,droppedEdges=0;
    bool gateEnabled=false,observeOnly=true,openingSkipReady=false,sceneTickReady=false,validatedOnly=false;
    OpeningStatus opening=OpeningStatus::Disabled;
};
inline constexpr const char* RuntimeDetail(const RuntimeSnapshot& s,bool nextEnabled) noexcept {
    if(s.validatedOnly)return "VALIDATED ONLY";
    if(!s.gateEnabled)return nextEnabled?"ARMED NEXT BOOT":"DISABLED";
    if(s.phase==Phase::Succeeded&&s.observeOnly)return "OBSERVED FIELD";
    if(s.phase==Phase::FailedVisible) {
        switch(s.failure) {
        case Failure::AutosaveMissing:return "FALLBACK: AUTOSAVE MISSING";
        case Failure::VanillaRejected:return "FALLBACK: CHECKSUM/LANGUAGE";
        default:return FailureName(s.failure);
        }
    }
    return PhaseName(s.phase);
}
using LogFn=void(*)(const char*);
InstallResult InstallFastloadHook(uintptr_t base,LogFn log,const InstallOptions& options);
void RequestFastloadStop() noexcept;
void RemoveFastloadHook() noexcept;
RuntimeSnapshot GetRuntimeSnapshot() noexcept;
void FlushFastloadTelemetry();
bool FastloadNeedsPump() noexcept;
} // namespace FfxHooks::Fastload
