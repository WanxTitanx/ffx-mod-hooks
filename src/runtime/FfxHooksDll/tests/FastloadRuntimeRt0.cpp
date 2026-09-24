// Jarvis-HOOK: offline contracts; no game, installed DLL, or save is touched.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#endif
#include "../hooks/BootSkipHook.h"
#include "../hooks/F8RuntimeCore.h"
#include "../shared/ffx_addresses.h"
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <cstring>

namespace {
using namespace FfxHooks::Fastload;
int checks = 0, failures = 0;
void Expect(bool ok, const char* why) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", why); }
}
std::string Source(const char* relative) {
    std::string path = __FILE__;
    path = path.substr(0, path.find_last_of("/\\") + 1) + "../" + relative;
    std::ifstream stream(path, std::ios::binary);
    Expect(stream.good(), "source contract input exists");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void TestAddresses() {
    const uint32_t pairs[][2] = {
        {RVA_FFX_FASTLOAD_SCENE_TICK, 0x00420090},
        {RVA_FFX_FASTLOAD_OPENING_LOADER, 0x00257B60},
        {RVA_FFX_FASTLOAD_OPENING_FINISH, 0x002525B0},
        {RVA_FFX_FASTLOAD_OPENING_WAIT, 0x008CB9C2},
        {RVA_FFX_FASTLOAD_SECURITY_COOKIE, 0x008613D8},
        {RVA_FFX_FASTLOAD_UI_INITIALIZER, 0x00248910},
        {RVA_FFX_FASTLOAD_UI_FSM, 0x002F0A90},
        {RVA_FFX_FASTLOAD_SCANNER, 0x002F0BB0},
        {RVA_FFX_FASTLOAD_SORTER, 0x002F10F0},
        {RVA_FFX_FASTLOAD_SELECTED_READ, 0x002F01B0},
        {RVA_FFX_FASTLOAD_CHECKSUM, 0x00247F20},
        {RVA_FFX_FASTLOAD_HYDRATE, 0x004B4E70},
        {RVA_FFX_FASTLOAD_UI_STATE, 0x008E72D8},
        {RVA_FFX_FASTLOAD_SLOT_RECORDS, 0x008E7308},
        {RVA_FFX_FASTLOAD_SELECTED_PAGE, 0x008E72DC},
        {RVA_FFX_FASTLOAD_SELECTED_ROW, 0x008E72E0},
        {RVA_FFX_FASTLOAD_SCREEN_STATE, 0x008CB994},
        {RVA_FFX_FASTLOAD_DIALOG_STATE, 0x008CB998},
        {RVA_FFX_FASTLOAD_DIRECTION, 0x008CB99C},
        {RVA_FFX_CURRENT_MENU_SCREEN_ID, 0x00EFBBF0},
        {RVA_FFX_FASTLOAD_PENDING_MENU, 0x00EFBBF4},
        {RVA_FFX_FASTLOAD_SELECT_LOAD, 0x00EFB878},
        {RVA_FFX_FASTLOAD_LOAD_COMMAND, 0x00421870},
        {RVA_FFX_FASTLOAD_LOAD_IDLE, 0x002482B0},
        {RVA_FFX_FASTLOAD_SCREEN_SETTER, 0x00248890},
        {RVA_FFX_FASTLOAD_REQUEST_RESET, 0x004B5570},
        {RVA_FFX_FASTLOAD_DIRECTION_SETTER, 0x00248860},
        {RVA_FFX_FASTLOAD_REQUEST_FLAG, 0x01466384},
        {RVA_FFX_SCENE_STATE_OBJECT, 0x00D2CA90},
        {RVA_FFX_CONTROLLED_CHR_INSTANCE_PTR, 0x00F00740},
    };
    for (const auto& pair : pairs) Expect(pair[0] == pair[1], "exact supported executable RVA");
    Expect(RVA_FFX_FASTLOAD_SCENE_TICK != RVA_FFX_SCENE_FIELD_SERVICE_TICK,
           "Fastload inner tick does not own Speed's outer tick");
}
void TestSignatures() {
    const std::array<uint8_t,16> inner{0x55,0x8B,0xEC,0x83,0xEC,0x44,0xA1,0xD8,0x13,0xC6,0x00,0x33,0xC5,0x89,0x45,0xFC};
    const std::array<uint8_t,16> opening{0x56,0x57,0x8B,0xF1,0xE8,0xE7,0xC1,0xFC,0xFF,0xD9,0xE8,0x83,0xEC,0x10,0x8B,0xF8};
    const std::array<uint8_t,8> finish{0xC6,0x05,0xC2,0xB9,0xCC,0x00,0x00,0xC3};
    const std::array<uint8_t,5> tickCaller{0xE8,0x1A,0xF3,0xFF,0xFF};
    const std::array<uint8_t,13> fieldLoadGate{0xE8,0xAF,0x74,0xE2,0xFF,0x85,0xC0,0x0F,0x84,0xD0,0x09,0x00,0x00};
    const std::array<uint8_t,127> LoadCommand{0x55,0x8B,0xEC,0x56,0xE8,0x37,0x6A,0xE2,0xFF,0x8B,0x75,0x08,0x85,0xC0,0x74,0x4B,0x81,0xFE,0x00,0x00,0x10,0x40,0x74,0x08,0x81,0xFE,0x00,0x00,0x20,0x40,0x75,0x3B,0x6A,0x01,0xE8,0xF9,0x6F,0xE2,0xFF,0x83,0xC4,0x04,0xE8,0xD1,0x3C,0x09,0x00,0x81,0xFE,0x00,0x00,0x20,0x40,0x75,0x0E,0x5E,0xC7,0x45,0x08,0x01,0x00,0x00,0x00,0x5D,0xE9,0xAB,0x6F,0xE2,0xFF,0x81,0xFE,0x00,0x00,0x10,0x40,0x75,0x2F,0x5E,0xC7,0x45,0x08,0x00,0x00,0x00,0x00,0x5D,0xE9,0x95,0x6F,0xE2,0xFF,0x83,0x3D,0xEC,0x07,0x34,0x01,0x00,0x75,0x08,0x81,0xFE,0x00,0x00,0x00,0x42,0x74,0x10,0x89,0x35,0xF0,0xBB,0x2F,0x01,0xC7,0x05,0xF4,0xBB,0x2F,0x01,0xFF,0xFF,0xFF,0xFF,0x5E,0x5D,0xC3};
    const std::array<uint8_t,12> LoadIdle{0x33,0xC0,0x39,0x05,0x94,0xB9,0xCC,0x00,0x0F,0x94,0xC0,0xC3};
    const std::array<uint8_t,35> ScreenSetter{0x55,0x8B,0xEC,0x8B,0x45,0x08,0xA3,0x94,0xB9,0xCC,0x00,0x85,0xC0,0x75,0x12,0x50,0xA3,0x98,0xB9,0xCC,0x00,0xE8,0x06,0x99,0x0A,0x00,0x8B,0xC8,0xE8,0x8F,0x9D,0x0A,0x00,0x5D,0xC3};
    const std::array<uint8_t,11> RequestReset{0xC7,0x05,0x84,0x63,0x86,0x01,0x00,0x00,0x00,0x00,0xC3};
    const std::array<uint8_t,13> DirectionSetter{0x55,0x8B,0xEC,0x8B,0x45,0x08,0xA3,0x9C,0xB9,0xCC,0x00,0x5D,0xC3};
    const std::array<uint8_t,13> ActiveSceneWriter{0x8B,0x75,0x08,0x57,0x89,0x75,0x90,0x89,0x35,0xF8,0xBB,0x2F,0x01};
    const std::array<uint8_t,62> TitleWindowGetter{0x55,0x8B,0xEC,0x8B,0x4D,0x08,0x85,0xC9,0x79,0x16,0x0F,0xBE,0x05,0x42,0x6B,0x32,0x01,0x33,0xC9,0x8D,0x04,0xC1,0x6B,0xC0,0x2C,0x05,0x30,0x6D,0x32,0x01,0x5D,0xC3,0x83,0xF9,0x07,0x7E,0x05,0xB9,0x07,0x00,0x00,0x00,0x0F,0xBE,0x05,0x42,0x6B,0x32,0x01,0x8D,0x04,0xC1,0x6B,0xC0,0x2C,0x05,0x30,0x6D,0x32,0x01,0x5D,0xC3};
    const std::array<uint8_t,13> TitleChoiceWait{0x50,0xE8,0xF9,0x27,0x01,0x00,0x83,0xC4,0x04,0x80,0x48,0x1D,0x20};
    const std::array<uint8_t,26> TitleChoicePoll{0x56,0xE8,0x8F,0x23,0x01,0x00,0x0F,0xB7,0x48,0x14,0x83,0xC4,0x04,0x83,0xCF,0xFF,0x83,0xE9,0x00,0x74,0x5D,0x83,0xE9,0x02,0x74,0x0C};
    const std::array<uint8_t,20> TitleRegistryPrimary{0xC7,0x05,0xB8,0x76,0x86,0x01,0x10,0x66,0x86,0x01,0xC7,0x05,0x58,0x8A,0x86,0x01,0x40,0x79,0x86,0x01};
    const std::array<uint8_t,20> TitleRegistrySecondary{0xC7,0x05,0xB8,0x76,0x86,0x01,0x90,0x6F,0x86,0x01,0xC7,0x05,0x58,0x8A,0x86,0x01,0x00,0x83,0x86,0x01};
    const std::array<uint8_t,37> TitleAnswerCommit{0x0F,0xBE,0x42,0x07,0x88,0x5C,0x10,0x18,0x8B,0x04,0x8D,0x50,0x8A,0x86,0x01,0xFE,0x40,0x07,0x8B,0x0C,0x8D,0x50,0x8A,0x86,0x01,0x8A,0x41,0x07,0x3A,0x41,0x06,0x75,0x04,0xC6,0x41,0x01,0x02};
    struct Fixture { Target target; const uint8_t* bytes; size_t size; std::vector<std::pair<size_t,uint32_t>> operands; };
    const Fixture fixtures[] = {
        {Target::TitleRegistryPrimary,TitleRegistryPrimary.data(),20,{{2,0x014676B8},{6,0x01466610},{12,0x01468A58},{16,0x01467940}}},
        {Target::TitleRegistrySecondary,TitleRegistrySecondary.data(),20,{{2,0x014676B8},{6,0x01466F90},{12,0x01468A58},{16,0x01468300}}},
        {Target::TitleAnswerCommit,TitleAnswerCommit.data(),37,{{11,0x01468A50},{21,0x01468A50}}},
        {Target::ActiveSceneWriter,ActiveSceneWriter.data(),13,{{9,0x00EFBBF8}}},
        {Target::TitleWindowGetter,TitleWindowGetter.data(),62,{{13,0x00F26B42},{26,0x00F26D30},{45,0x00F26B42},{56,0x00F26D30}}},
        {Target::TitleChoiceWait,TitleChoiceWait.data(),13,{}},
        {Target::TitleChoicePoll,TitleChoicePoll.data(),26,{}},
        {Target::SceneTick,inner.data(),16,{{7,0x008613D8}}},
        {Target::OpeningLoader,opening.data(),16,{}},
        {Target::OpeningFinish,finish.data(),8,{{2,0x008CB9C2}}},
        {Target::LoadCommand,LoadCommand.data(),127,{{93,0x00F407EC},{110,0x00EFBBF0},{116,0x00EFBBF4}}},
        {Target::LoadIdle,LoadIdle.data(),12,{{4,0x008CB994}}},
        {Target::ScreenSetter,ScreenSetter.data(),35,{{7,0x008CB994},{17,0x008CB998}}},
        {Target::RequestReset,RequestReset.data(),11,{{2,0x01466384}}},
        {Target::DirectionSetter,DirectionSetter.data(),13,{{7,0x008CB99C}}},
        {Target::TickCaller,tickCaller.data(),5,{}},
        {Target::FieldLoadGate,fieldLoadGate.data(),13,{}}};
    for (const auto& f : fixtures) {
        for (uintptr_t base : {uintptr_t{0x00400000},uintptr_t{0x01400000},uintptr_t{0x10000000}}) {
            std::array<uint8_t,128> bytes{};
            for (size_t i=0;i<f.size;++i) bytes[i]=f.bytes[i];
            for(const auto& operand:f.operands) for(size_t i=0;i<4;++i)
                bytes[operand.first+i]=static_cast<uint8_t>((base+operand.second)>>(8*i));
            Expect(ValidateTarget(f.target,bytes.data(),f.size,base)==TargetStatus::Match,"exact relocated signature accepted");
            for(size_t i=0;i<f.size;++i) for(unsigned bit=0;bit<8;++bit) {
                bytes[i]^=static_cast<uint8_t>(1u<<bit);
                Expect(ValidateTarget(f.target,bytes.data(),f.size,base)!=TargetStatus::Match,"every changed signature bit rejected, including relocated operand");
                bytes[i]^=static_cast<uint8_t>(1u<<bit);
            }
            Expect(ValidateTarget(f.target,nullptr,f.size,base)==TargetStatus::NullInput,"null span rejected");
            Expect(ValidateTarget(f.target,bytes.data(),f.size-1,base)==TargetStatus::WrongLength,"short span rejected");
            Expect(ValidateTarget(f.target,bytes.data(),f.size+1,base)==TargetStatus::WrongLength,"long span rejected");
            bytes[0]=0xE9;
            Expect(ValidateTarget(f.target,bytes.data(),f.size,base)==TargetStatus::Conflict,"E9 ownership conflict rejected");
            bytes[0]=0xFF;bytes[1]=0x25;
            Expect(ValidateTarget(f.target,bytes.data(),f.size,base)==TargetStatus::Conflict,"FF25 ownership conflict rejected");
        }
        Expect(ValidateTarget(f.target,f.bytes,f.size,UINT32_MAX)==TargetStatus::OutOfRange,"base overflow rejected");
    }
}
void TestNoLegacyOwner() {
    const auto source=Source("hooks/BootSkipHook.cpp");
    Expect(source.find("RVA_FFX_SCENE_FIELD_SERVICE_TICK")==std::string::npos &&
           source.find("0x00420C00")==std::string::npos,"Fastload must not construct an outer tick detour");
    Expect(source.find("ResolveSaveSlotFromMtime")==std::string::npos,"autosave identity cannot use file modification time");
}
Sample Title(uint32_t now = 100) {
    Sample s{}; s.nowMs=now; s.sceneId=23; s.fieldSystemReady=true; s.slotZeroRecord=0;
    s.currentMenu=-1;s.pendingMenu=1;
    s.activeSceneId=23;s.titleChoiceState=2;s.titleChoiceFlags=0x20;s.messageBank=0;
    s.titleAnswerRva=0x01467940;s.titleAnswerHeader=0x00010300FF000100ull;
    s.titleWindowPhase=3;s.titleAnswerResult=0xFF;
    return s;
}
void TestNativeTitleInputAdmission() {
    struct Memory {
        uint32_t activeScene=23;
        uint16_t windowState=2;
        uint8_t choiceFlags=0x20,bank=0;
    } memory;
    const ObservationReader reader{&memory,[](void* raw,uint32_t rva,void* out,size_t width) noexcept {
        const auto& m=*static_cast<Memory*>(raw);
        uint64_t value=0;
        if(rva==RVA_FFX_SCENE_STATE_OBJECT)value=23;
        else if(rva==RVA_FFX_CURRENT_MENU_SCREEN_ID)value=UINT32_MAX;
        else if(rva==RVA_FFX_FASTLOAD_PENDING_MENU)value=1;
        else if(rva==0x00EFBBF8u)value=m.activeScene;
        else if(rva==0x00F26D9Cu)value=m.windowState;
        else if(rva==0x00F26DA5u)value=m.choiceFlags;
        else if(rva==0x00F26B42u)value=m.bank;
        else if(rva==0x014676B8u)value=0x00400000u+0x01466610u;
        else if(rva==0x01468A58u)value=0x00400000u+0x01467940u;
        else if(rva==0x0146663Eu)value=3;
        else if(rva==0x01467940u)value=0x00010300FF000100ull;
        else if(rva==0x01467958u)value=0xFF;
        if(width>sizeof(value))return false;
        std::memcpy(out,&value,width);return true;
    },0x00400000u};
    auto gate=Start(true,false,0);gate.bootstrapSeen=true;
    const auto action=[&]() {
        Sample sample{};
        Expect(ReadObservedFields(reader,&sample),"title admission reads a complete native observation");
        sample.nowMs=100;sample.fieldSystemReady=true;
        return Advance(gate,sample).actions;
    };
    memory.activeScene=348;
    Expect(action()==ActionNone,"saved title23 cannot load while the active field is still bootstrap348");
    memory.activeScene=23;memory.windowState=0;memory.choiceFlags=0;
    Expect(action()==ActionNone,"bootstrap roundtrip alone cannot preempt the title startup worker");
    memory.windowState=1;memory.choiceFlags=0x20;
    Expect(action()==ActionNone,"opening title choice is not yet ready for native input");
    memory.windowState=2;memory.choiceFlags=0;
    Expect(action()==ActionNone,"visible text without an awaiting-choice contract is not title readiness");
    memory.choiceFlags=0x20;memory.bank=1;
    Expect(action()==ActionNone,"a different script message bank cannot grant title admission");
    memory.bank=0;
    Expect(action()==ActionRequestVanillaLoad,"active title and its ready choice admit the one native load request");
}
State Step(State state, Sample sample, uint32_t action, Phase phase, Failure failure=Failure::None) {
    const auto d=Advance(state,sample);
    Expect(d.actions==action,"exact action for sample");
    Expect(d.state.phase==phase,"exact next phase for sample");
    Expect(d.state.failure==failure,"exact failure for sample");
    if(action) Expect(d.state.actionGeneration==state.actionGeneration+1,"actions publish a new generation");
    return d.state;
}
void TestPolicy() {
    auto title=Title();
    State disabled=Start(false,false,0);
    for(unsigned phase=0; phase<=static_cast<unsigned>(Phase::Stopped); ++phase) {
        State s=Start(true,false,0); s.phase=static_cast<Phase>(phase);
        auto stopped=title; stopped.stopRequested=true;
        if(IsTerminal(s.phase)) {
            auto d=Advance(s,stopped);
            Expect(d.actions==ActionNone && d.state.phase==s.phase,"terminal phase absorbs stop and all future actions");
        } else Step(s,stopped,ActionNone,Phase::Stopped);
    }
    Step(disabled,title,ActionNone,Phase::Disabled);
    auto observe=Start(true,true,0);
    for(int ui : {0,10,11,12,13,14,15,16,17}) {
        auto sample=title; sample.saveUiState=ui; sample.openingReady=true;
        Step(observe,sample,ActionNone,Phase::ObserveOnly);
    }
    auto held=title; held.shiftBypassHeld=true;
    Step(Start(true,false,0),held,ActionNone,Phase::Bypassed);
    Step(Start(true,true,0),held,ActionNone,Phase::Bypassed);
    auto boot=title; boot.sceneId=0; boot.fieldSystemReady=false;
    auto state=Step(Start(true,false,0),boot,ActionNone,Phase::WaitingForOpening);
    boot.openingReady=true;
    state=Step(state,boot,ActionFinishOpening,Phase::WaitingForTitle);
    const auto openingGeneration=state.actionGeneration;
    state=Step(state,boot,ActionNone,Phase::WaitingForTitle);
    Expect(state.actionGeneration==openingGeneration,"duplicate opening callback cannot repeat Finish");
    Step(state,held,ActionNone,Phase::Bypassed);
    auto bootstrap=title;bootstrap.sceneId=348;
    state=Step(state,bootstrap,ActionNone,Phase::WaitingForTitle);
    for(uint32_t scene : {0u,1u,22u,24u,255u,UINT32_MAX}) {
        auto s=title;s.sceneId=scene;
        Step(state,s,ActionNone,Phase::WaitingForTitle);
    }
    for(int invalid=0;invalid<9;++invalid) {
        auto s=title;
        switch(invalid) {
        case 0:s.controlledCharacter=1;break;
        case 1:s.fieldSystemReady=false;break;
        case 2:s.saveLoadScreenState=1;break;
        case 3:s.saveLoadDialogState=1;break;
        case 4:s.saveDirection=1;break;
        case 5:s.selectLoad=1;break;
        case 6:s.saveUiState=11;break;
        case 7:s.pendingMenu=3;break;
        case 8:s.sceneTransitionPending=1;break;
        }
        Step(state,s,ActionNone,Phase::WaitingForTitle);
    }
    state=Step(state,title,ActionRequestVanillaLoad,Phase::RequestingVanillaLoad);
    auto busyTitle=title;busyTitle.sceneTransitionPending=1;
    auto missed=Step(Start(true,false,0),busyTitle,ActionNone,Phase::WaitingForOpening);
    missed=Step(missed,bootstrap,ActionNone,Phase::WaitingForOpening);
    Step(missed,title,ActionRequestVanillaLoad,Phase::RequestingVanillaLoad);
    auto acknowledged=title; acknowledged.actionResult=ActionResult::Accepted;
    acknowledged.actionGeneration=state.actionGeneration;
    auto stale=acknowledged; stale.actionGeneration=0;
    Step(state,stale,ActionNone,Phase::RequestingVanillaLoad);
    auto conflict=acknowledged; conflict.actionResult=ActionResult::Conflict;
    Step(state,conflict,ActionNone,Phase::FailedVisible,Failure::UnexpectedState);
    state=Step(state,acknowledged,ActionNone,Phase::WaitingForSlotScan);
    for(int ui : {0,10,11}) { auto s=title;s.saveUiState=ui;Step(state,s,ActionNone,Phase::WaitingForSlotScan); }
    auto list=title; list.saveUiState=12;list.saveLoadScreenState=2;
    auto missing=list;missing.slotZeroRecord=-1;
    Step(state,missing,ActionNone,Phase::FailedVisible,Failure::AutosaveMissing);
    for(int drift=0;drift<4;++drift) {
        auto s=list;
        if(drift==0)s.slotZeroRecord=1;
        if(drift==1)s.selectedPage=1;
        if(drift==2)s.selectedRow=1;
        if(drift==3)s.saveDirection=1;
        Step(state,s,ActionNone,Phase::FailedVisible,Failure::SlotMappingDrift);
    }
    auto rejected=list;rejected.saveUiState=16;
    Step(state,rejected,ActionNone,Phase::FailedVisible,Failure::VanillaRejected);
    auto unexpected=list;unexpected.saveUiState=99;
    Step(state,unexpected,ActionNone,Phase::FailedVisible,Failure::UnexpectedState);
    state=Step(state,list,ActionAdvanceAutosaveToRead,Phase::HandedToVanillaRead);
    for(int ui : {12,14,15,17}) { auto s=list;s.saveUiState=ui;Step(state,s,ActionNone,Phase::HandedToVanillaRead); }
    conflict.actionGeneration=state.actionGeneration;
    Step(state,conflict,ActionNone,Phase::FailedVisible,Failure::UnexpectedState);
    acknowledged.actionGeneration=state.actionGeneration;acknowledged.saveUiState=14;
    state=Step(state,acknowledged,ActionNone,Phase::WaitingForField);
    auto after=list;after.saveUiState=15;after.shiftBypassHeld=true;
    Step(state,after,ActionNone,Phase::WaitingForField);
    Step(state,rejected,ActionNone,Phase::FailedVisible,Failure::VanillaRejected);
    auto field=title;field.sceneId=100;field.activeSceneId=100;field.controlledCharacter=1;field.saveUiState=10;
    auto noActor=field;noActor.controlledCharacter=0;
    Step(state,noActor,ActionNone,Phase::WaitingForField);
    auto titleActor=field;titleActor.sceneId=23;
    Step(state,titleActor,ActionNone,Phase::WaitingForField);
    state=Step(state,field,ActionNone,Phase::Succeeded);
    for(int repeat=0;repeat<10;++repeat) Step(state,title,ActionNone,Phase::Succeeded);
    for(Phase terminal : {Phase::Disabled,Phase::Succeeded,Phase::Bypassed,Phase::FailedVisible,Phase::Stopped}) {
        State s=Start(true,false,0);s.phase=terminal;s.actionGeneration=3;
        for(int ui=0;ui<20;++ui) { auto sample=title;sample.saveUiState=ui;sample.openingReady=true;
            auto d=Advance(s,sample);Expect(d.actions==0 && d.state.phase==terminal && d.state.actionGeneration==3,"terminal phases are permanent pass-through"); }
    }
}
void TestCorrectedStartupSequence() {
    // The first title still owns an unfinished native startup sequence. Only
    // its post-bootstrap return is an admitted automatic load opportunity.
    const int trace[][5]={{23,0,0,0,0},{23,0,0,-1,1},{348,0,0,-1,1},
        {348,0,0,0,-1},{348,0,0,-1,1},{348,0,0,0,-1},
        {348,0,0,-1,1},{23,0,0,-1,1}};
    auto state=Start(true,false,0);
    for(size_t i=0;i<sizeof(trace)/sizeof(trace[0]);++i) {
        auto sample=Title(static_cast<uint32_t>(i+1));
        sample.sceneId=trace[i][0];sample.saveLoadScreenState=trace[i][1];
        sample.saveUiState=trace[i][2];sample.currentMenu=trace[i][3];sample.pendingMenu=trace[i][4];
        const auto decision=Advance(state,sample);
        Expect(decision.actions==(i==7?ActionRequestVanillaLoad:ActionNone),
               "request waits for startup to return to the ready title and occurs exactly once");
        state=decision.state;
    }
    auto acknowledged=Title(10);acknowledged.actionGeneration=state.actionGeneration;
    acknowledged.actionResult=ActionResult::Accepted;
    state=Advance(state,acknowledged).state;
    for(int drift=0;drift<5;++drift) {
        auto list=Title(11);list.saveUiState=12;list.saveLoadScreenState=2;
        switch(drift) {case 0:list.sceneId=348;break;case 1:list.controlledCharacter=1;break;
        case 2:list.saveLoadScreenState=1;break;case 3:list.saveLoadDialogState=1;break;case 4:list.selectLoad=1;break;}
        const auto decision=Advance(state,list);
        Expect(decision.actions==ActionNone&&decision.state.phase==Phase::FailedVisible,
               "UI 12 alone cannot authorize a handoff outside the witnessed load context");
    }
}
void TestDeadlines() {
    struct Case { Phase phase; uint32_t deadline; Failure failure; };
    const Case cases[]={{Phase::WaitingForOpening,120000,Failure::TitleTimeout},
        {Phase::WaitingForTitle,120000,Failure::TitleTimeout},{Phase::ObserveOnly,120000,Failure::TitleTimeout},
        {Phase::RequestingVanillaLoad,30000,Failure::LoadTimeout},{Phase::WaitingForSlotScan,30000,Failure::LoadTimeout},
        {Phase::HandedToVanillaRead,120000,Failure::FieldTimeout},{Phase::WaitingForField,120000,Failure::FieldTimeout}};
    for(const auto& c:cases) for(uint32_t start : {0u,UINT32_MAX-2000u}) {
        State s=Start(true,false,start);s.phase=c.phase;s.phaseStartedMs=start;
        auto sample=Title(start+c.deadline-1);sample.sceneId=0;sample.saveUiState=11;
        Expect(Advance(s,sample).state.failure==Failure::None,"deadline remains open until last millisecond, including wrap");
        sample.nowMs=start+c.deadline;
        Step(s,sample,ActionNone,Phase::FailedVisible,c.failure);
    }
}

void TestInitialTitleCannotPreemptStartup() {
    const State cold = Start(true, false, 100);
    const Sample firstReady = Title(150);
    const Decision early = Advance(cold, firstReady);
    Expect(early.actions == ActionNone && early.state.phase == Phase::WaitingForOpening &&
           !early.state.bootstrapSeen,
           "the first idle title must not interrupt the pending native startup sequence");
    auto boot=firstReady;boot.sceneId=348;
    const auto observed=Advance(early.state,boot);
    const auto admitted=Advance(observed.state,firstReady);
    Expect(admitted.actions==ActionRequestVanillaLoad && admitted.state.bootstrapSeen,
           "a ready title after observed startup admits the native load");
    Expect(Advance(admitted.state,firstReady).actions==ActionNone,
           "the safe request remains one-shot before acknowledgement");
    for (int32_t pending : {1, -1, 2}) {
        auto busy = firstReady;busy.sceneTransitionPending=pending;
        Expect(Advance(observed.state,busy).actions==ActionNone,
               "active and unknown transitions still block a post-startup request");
    }
}
void TestInterruptedLoadCannotClaimManualSuccess() {
    // Replay the observed 164 -> 348 -> 23 -> manual load -> 164 sequence.
    auto state=Start(true,false,0);state.phase=Phase::WaitingForField;
    state.actionGeneration=3;state.phaseStartedMs=10;
    auto sample=Title(20);sample.sceneId=164;sample.saveUiState=10;
    state=Advance(state,sample).state;
    Expect(state.phase==Phase::WaitingForField,"loaded scene without control is not completion");
    sample.sceneId=348;sample.nowMs=120;sample.sceneTransitionPending=1;
    auto interrupted=Advance(state,sample);
    Expect(interrupted.state.phase==Phase::FailedVisible && interrupted.actions==ActionNone,
           "native startup reclaiming the scene closes the automatic load without retry");
    for (uint32_t scene : {23u,164u}) {
        sample.sceneId=scene;sample.controlledCharacter=1;sample.sceneTransitionPending=0;
        interrupted=Advance(interrupted.state,sample);
        Expect(interrupted.state.phase==Phase::FailedVisible && interrupted.actions==ActionNone,
               "a later manual load cannot be credited to an interrupted Fastload");
    }
    sample=Title(21);
    Expect(Advance(state,sample).state.phase==Phase::FailedVisible,
           "returning directly from the loaded scene to title also closes the attempt");
    auto controlled=Title(22);controlled.sceneId=164;controlled.activeSceneId=164;controlled.controlledCharacter=1;
    for(int condition=0;condition<3;++condition) {
        auto busy=controlled;
        if(condition==0)busy.sceneTransitionPending=1;
        if(condition==1)busy.saveLoadScreenState=4;
        if(condition==2)busy.selectLoad=1;
        Expect(Advance(state,busy).state.phase!=Phase::Succeeded,
               "player control alone cannot complete an unsettled or repeated native load");
    }
    Expect(Advance(state,controlled).state.phase==Phase::Succeeded,
           "settled native field with control completes the owned automatic load");
}
void TestSecondLoadCannotFinishAutomatic() {
    auto state=Start(true,false,0);state.phase=Phase::WaitingForField;
    state.phaseStartedMs=10;state.actionGeneration=3;
    auto sample=Title(20);sample.sceneId=164;sample.activeSceneId=23;sample.saveUiState=10;
    state=Advance(state,sample).state;
    Expect(state.phase==Phase::WaitingForField,"reading saved scene164 does not activate the field while title23 remains active");
    auto stale=sample;stale.controlledCharacter=1;
    Expect(Advance(state,stale).state.phase!=Phase::Succeeded,"stale control cannot certify a different active scene");
    sample.nowMs=26000;sample.saveLoadScreenState=1;
    state=Advance(state,sample).state;
    Expect(state.phase==Phase::FailedVisible&&state.failure==Failure::LoadInterrupted,
           "a second load after the first read closes ownership of the automatic attempt");
    sample.saveLoadScreenState=0;sample.activeSceneId=164;sample.controlledCharacter=1;
    Expect(Advance(state,sample).state.phase==Phase::FailedVisible,"later manual field arrival never becomes automatic success");
    auto closing=Start(true,false,0);closing.phase=Phase::WaitingForField;closing.phaseStartedMs=10;
    sample=Title(20);sample.sceneId=164;sample.saveLoadScreenState=2;
    closing=Advance(closing,sample).state;
    sample.nowMs=30;
    closing=Advance(closing,sample).state;
    Expect(closing.phase==Phase::WaitingForField,
           "saved scene hydration while the first load screen is still closing is not a second load");
    sample.saveLoadScreenState=0;sample.activeSceneId=164;sample.controlledCharacter=1;
    Expect(Advance(closing,sample).state.phase==Phase::Succeeded,"the original native load may complete its own close sequence");
}
struct ChoiceMemory {
    Sample value=Title();
    unsigned admissions=0,reads=0,replies=0,commits=0,stopAt=0,failReadAt=0;
    bool replyConflict=false,headerConflict=false,registryDrift=false,headerDrift=false,foreignReply=false;
    TitleChoiceIo Io() noexcept {
        return {this,
            [](void* p) noexcept {auto& m=*static_cast<ChoiceMemory*>(p);return ++m.admissions!=m.stopAt;},
            [](void* p,Sample* out) noexcept {
                auto& m=*static_cast<ChoiceMemory*>(p);
                if(++m.reads==m.failReadAt)return false;
                *out=m.value;return true;
            },
            [](void* p,uint32_t rva,uint8_t expected,uint8_t desired) noexcept {
                auto& m=*static_cast<ChoiceMemory*>(p);++m.replies;
                if(rva!=m.value.titleAnswerRva+0x18||m.replyConflict||m.value.titleAnswerResult!=expected)return false;
                m.value.titleAnswerResult=desired;
                if(m.registryDrift)m.value.titleAnswerRva=0x01468300;
                if(m.headerDrift)m.value.titleAnswerHeader^=uint64_t(1)<<16;
                if(m.foreignReply)m.value.titleAnswerResult=2;
                return true;
            },
            [](void* p,uint32_t rva,uint64_t expected,uint64_t desired) noexcept {
                auto& m=*static_cast<ChoiceMemory*>(p);++m.commits;
                if(rva!=m.value.titleAnswerRva||m.headerConflict||m.value.titleAnswerHeader!=expected)return false;
                m.value.titleAnswerHeader=desired;return true;
            }};
    }
};
void TestTitleChoicePublication() {
    for(uint32_t address:{0x01467940u,0x01468300u}) {
        ChoiceMemory m;m.value.titleAnswerRva=address;
        Expect(PublishTitleLoad(m.Io()),"both proven native response buffers accept one Load answer");
        Expect(m.value.titleAnswerHeader==0x01010300FF010200ull&&m.value.titleAnswerResult==1,
               "published bytes equal native cursor1 confirmation, retaining control/cancel/range bytes");
        Expect(m.commits==1&&m.replies==1,"one atomic header commit publishes the prepared reply");
        Expect(!PublishTitleLoad(m.Io())&&m.commits==1&&m.replies==1,"committed Load never publishes twice");
    }
    for(unsigned admission=1;admission<=4;++admission) {
        ChoiceMemory m;m.stopAt=admission;
        Expect(!PublishTitleLoad(m.Io()),"stop at every precommit boundary rejects the title action");
        Expect(m.value.titleAnswerHeader==0x00010300FF000100ull&&m.value.titleAnswerResult==0xFF,
               "stop restores only the unused owned reply and leaves native choice active");
    }
    for(unsigned read=1;read<=3;++read) {
        ChoiceMemory m;m.failReadAt=read;
        Expect(!PublishTitleLoad(m.Io()),"a failed choice read is never reported as successful admission");
        Expect(read==3?(m.value.titleAnswerHeader==0x01010300FF010200ull&&m.value.titleAnswerResult==1):
               (m.value.titleAnswerHeader==0x00010300FF000100ull&&m.value.titleAnswerResult==0xFF),
               "a trailing read failure does not undo an already committed native choice");
    }
    for(unsigned conflict=0;conflict<5;++conflict) {
        ChoiceMemory m;m.replyConflict=conflict==0;m.headerConflict=conflict==1;
        m.registryDrift=conflict==2;m.headerDrift=conflict==3;m.foreignReply=conflict==4;
        Expect(!PublishTitleLoad(m.Io()),"conflicting reply/header/registry never becomes an accepted title action");
        Expect(AnswerByte(m.value.titleAnswerHeader,1)==1&&AnswerByte(m.value.titleAnswerHeader,7)==0,
               "conflicts cannot publish a completed answer");
        if(conflict==0||conflict==1)Expect(m.value.titleAnswerResult==0xFF,"unchanged ownership allows unused reply rollback");
        if(conflict==2||conflict==3)Expect(m.replies==1,"foreign registry/header ownership is never overwritten during rollback");
        if(conflict==4)Expect(m.value.titleAnswerResult==2,"a foreign reply is preserved");
    }
    for(unsigned invalid=0;invalid<13;++invalid) {
        ChoiceMemory m;
        switch(invalid) {
        case 0:m.value.titleAnswerRva=0x01467941;break;
        case 1:m.value.titleWindowPhase=2;break;
        case 2:m.value.titleAnswerHeader^=uint64_t(1)<<8;break;
        case 3:m.value.titleAnswerHeader|=uint64_t(3)<<16;break;
        case 4:m.value.titleAnswerHeader|=uint64_t(1)<<32;break;
        case 5:m.value.titleAnswerHeader^=uint64_t(2)<<40;break;
        case 6:m.value.titleAnswerHeader^=uint64_t(3)<<48;break;
        case 7:m.value.titleAnswerHeader|=uint64_t(1)<<56;break;
        case 8:m.value.shiftBypassHeld=true;break;
        case 9:m.value.stopRequested=true;break;
        case 10:m.value.titleChoiceState=3;break;
        case 11:m.value.activeSceneId=348;break;
        case 12:m.value.sceneTransitionPending=1;break;
        }
        Expect(!PublishTitleLoad(m.Io())&&m.replies==0&&m.commits==0,
               "unproven window, range, ownership, cancellation or transition performs no write");
    }
}
struct ActionMemory {
    Sample sample=Title();
    bool allowed=true,code=true,readable=true,opening=true,stopAfterRead=false,wrongAdmission=false,casConflict=false;
    bool stopAfterEffect=false,failTrailingRead=false,wrongFinish=false,driftAfterRequest=false;
    unsigned reads=0,validations=0,finishes=0,requests=0,handoffs=0,admissions=0,stopAtAdmission=0;
    std::string order;
    ActionIo Io() noexcept {
        return {this,
            [](void* c) noexcept {auto& m=*static_cast<ActionMemory*>(c);++m.admissions;
                return m.allowed && (m.stopAtAdmission==0||m.admissions<m.stopAtAdmission);},
            [](void* c,uint32_t) noexcept {auto& m=*static_cast<ActionMemory*>(c);++m.validations;return m.code;},
            [](void* c,Sample* s) noexcept {auto& m=*static_cast<ActionMemory*>(c);++m.reads;m.order+='r';
                if(!m.readable)return false;
                *s=m.sample;if(m.stopAfterRead)m.allowed=false;return true;},
            [](void* c,bool* active) noexcept {auto& m=*static_cast<ActionMemory*>(c);++m.reads;m.order+='o';
                if(!m.readable)return false;
                *active=m.opening;if(m.stopAfterRead)m.allowed=false;return true;},
            [](void* c) noexcept {auto& m=*static_cast<ActionMemory*>(c);++m.finishes;m.order+='F';
                if(!m.wrongFinish)m.opening=false;
                if(m.stopAfterEffect)m.allowed=false;
                if(m.failTrailingRead)m.readable=false;
                return true;},
            [](void* c,uint32_t command) noexcept {auto& m=*static_cast<ActionMemory*>(c);++m.requests;m.order+='L';
                Expect(command==0x40100000,"native request uses Load command, never Save or a modular menu ID");
                if(!m.wrongAdmission){m.sample.titleAnswerHeader=0x01010300FF010200ull;m.sample.titleAnswerResult=1;}
                if(m.stopAfterEffect)m.allowed=false;
                if(m.failTrailingRead)m.readable=false;
                if(m.driftAfterRequest)m.sample.selectedRow=1;
                return true;},
            [](void* c,int32_t expected,int32_t desired) noexcept {auto& m=*static_cast<ActionMemory*>(c);
                Expect(expected==12&&desired==14,"single compare-owned handoff is exactly 12 to 14");
                if(m.casConflict||m.sample.saveUiState!=expected)return false;
                m.order+='H';++m.handoffs;m.sample.saveUiState=desired;
                if(m.stopAfterEffect)m.allowed=false;
                return true;}};
    }
    unsigned Effects() const noexcept {return finishes+requests+handoffs;}
};
ActionMemory ReadyFor(uint32_t action) {
    ActionMemory m;
    if(action==ActionAdvanceAutosaveToRead){m.sample.saveLoadScreenState=2;m.sample.saveUiState=12;}
    return m;
}
void TestActionExecutor() {
    for(uint32_t action:{uint32_t(ActionFinishOpening),uint32_t(ActionRequestVanillaLoad),uint32_t(ActionAdvanceAutosaveToRead)}) {
        auto m=ReadyFor(action);
        auto result=ExecuteAction(action,true,m.Io());
        Expect(result.result==ActionResult::None&&m.Effects()==0&&m.reads==0&&m.validations==0,
               "observe-only executes no game reads, calls or writes through the action executor");
        for(int failure=0;failure<4;++failure) {
            m=ReadyFor(action);
            switch(failure){case 0:m.allowed=false;break;case 1:m.code=false;break;case 2:m.readable=false;break;case 3:m.stopAfterRead=true;break;}
            result=ExecuteAction(action,false,m.Io());
            Expect(result.result==ActionResult::Conflict&&m.Effects()==0,"stop, signature drift and failed/stale reads prevent every action");
        }
        // A stop arriving after code validation must also prevent the native effect.
        m=ReadyFor(action);m.stopAtAdmission=2;
        result=ExecuteAction(action,false,m.Io());
        Expect(result.result==ActionResult::Conflict&&m.Effects()==0,"stop is rechecked after code validation");
        m=ReadyFor(action);result=ExecuteAction(action,false,m.Io());
        Expect(result.result==ActionResult::Accepted&&m.Effects()==1,"each admitted action has exactly one native effect");
        const auto expected=action==ActionFinishOpening?"oFo":action==ActionRequestVanillaLoad?"rLr":"rH";
        Expect(m.order==expected,"fresh proof precedes effect; native opening/load calls require trailing readback");
        result=ExecuteAction(action,false,m.Io());
        Expect(result.result==ActionResult::Conflict&&m.Effects()==1,"changed native state rejects a repeated effect");
        m=ReadyFor(action);m.stopAfterEffect=true;
        ExecuteAction(action,false,m.Io());
        Expect(m.Effects()==1&&(action!=ActionAdvanceAutosaveToRead||m.sample.saveUiState==14),
               "stop after native admission never issues a compensating write or call");
        if(action!=ActionAdvanceAutosaveToRead) {
            m=ReadyFor(action);m.failTrailingRead=true;
            result=ExecuteAction(action,false,m.Io());
            Expect(result.result==ActionResult::Conflict&&result.failure==Failure::ReadFault&&m.Effects()==1,
                   "a failed trailing read cannot publish action acceptance");
        }
    }
    for(int drift=0;drift<16;++drift) {
        auto m=ReadyFor(ActionRequestVanillaLoad);
        switch(drift){case 0:m.sample.sceneId=348;break;case 1:m.sample.controlledCharacter=1;break;
        case 2:m.sample.fieldSystemReady=false;break;case 3:m.sample.saveLoadScreenState=1;break;
        case 4:m.sample.saveLoadDialogState=1;break;case 5:m.sample.saveDirection=1;break;
        case 6:m.sample.selectLoad=1;break;case 7:m.sample.currentMenu=0;break;
        case 8:m.sample.pendingMenu=0;break;case 9:m.sample.saveUiState=11;break;case 10:m.sample.shiftBypassHeld=true;break;
        case 11:m.sample.sceneTransitionPending=1;break;
        case 12:m.sample.activeSceneId=348;break;case 13:m.sample.titleChoiceState=0;break;
        case 14:m.sample.titleChoiceFlags=0;break;case 15:m.sample.messageBank=1;break;}
        Expect(ExecuteAction(ActionRequestVanillaLoad,false,m.Io()).result==ActionResult::Conflict&&m.Effects()==0,
               "every stale title precondition and newly held Shift prevents native request");
    }
    for(int drift=0;drift<12;++drift) {
        auto m=ReadyFor(ActionAdvanceAutosaveToRead);
        switch(drift){case 0:m.sample.slotZeroRecord=-1;break;case 1:m.sample.slotZeroRecord=1;break;
        case 2:m.sample.selectedPage=1;break;case 3:m.sample.selectedRow=1;break;case 4:m.sample.saveDirection=1;break;
        case 5:m.sample.sceneId=348;break;case 6:m.sample.controlledCharacter=1;break;
        case 7:m.sample.saveLoadScreenState=1;break;case 8:m.sample.saveLoadDialogState=1;break;
        case 9:m.sample.selectLoad=1;break;case 10:m.casConflict=true;break;
        case 11:m.sample.activeSceneId=348;break;}
        Expect(ExecuteAction(ActionAdvanceAutosaveToRead,false,m.Io()).result==ActionResult::Conflict&&m.Effects()==0,
               "fresh autosave identity and CAS ownership are mandatory, with no manual-slot fallback");
    }
    auto m=ReadyFor(ActionRequestVanillaLoad);m.wrongAdmission=true;
    Expect(ExecuteAction(ActionRequestVanillaLoad,false,m.Io()).result==ActionResult::Conflict&&m.requests==1,
           "native call without the proven trailing admission is a visible conflict, not success");
    m=ReadyFor(ActionRequestVanillaLoad);m.driftAfterRequest=true;
    Expect(ExecuteAction(ActionRequestVanillaLoad,false,m.Io()).result==ActionResult::Conflict&&m.sample.selectedRow==1,
           "selection drift in the trailing sample is rejected and left untouched");
    m=ReadyFor(ActionFinishOpening);m.wrongFinish=true;
    Expect(ExecuteAction(ActionFinishOpening,false,m.Io()).result==ActionResult::Conflict&&m.finishes==1,
           "opening helper acceptance requires wait byte cleared in trailing sample");
    m=ReadyFor(ActionAdvanceAutosaveToRead);
    Expect(ExecuteAction(ActionAdvanceAutosaveToRead,false,m.Io()).result==ActionResult::Accepted,"handoff accepted");
    m.allowed=false;
    ExecuteAction(ActionAdvanceAutosaveToRead,false,m.Io());
    Expect(m.sample.saveUiState==14&&m.handoffs==1,"stop after handoff does not roll back vanilla state");
    for(uint32_t invalid:{0u,3u,7u,8u,UINT32_MAX}) {
        m=ReadyFor(ActionRequestVanillaLoad);ExecuteAction(invalid,false,m.Io());
        Expect(m.Effects()==0,"no combined or unknown action can reach native code");
    }
}
void TestObservedFieldWidths() {
    struct Memory { std::vector<uint8_t> bytes; bool fail=false; } memory;
    memory.bytes.resize(0x00F30810u);
    auto set=[&](uint32_t rva,uint32_t value) {std::memcpy(memory.bytes.data()+rva,&value,4);};
    set(0x00D2CA90,0x015C0017); // Literal user trace: scene 23 next to an unrelated WORD 348.
    set(0x00F00740,0x12345000); set(0x008E72D8,12); set(0x008E7308,0);
    set(0x00F3080C,1);
    set(0x00EFBBF8,348);set(0x00F26D9C,0xCDEF0002);
    set(0x00F26DA5,0xAABBCC20);set(0x00F26B42,0xAABBCC00);
    const ObservationReader reader{&memory,[](void* context,uint32_t rva,void* out,size_t width) noexcept {
        auto& m=*static_cast<Memory*>(context);
        if(m.fail || rva>m.bytes.size() || width>m.bytes.size()-rva)return false;
        std::memcpy(out,m.bytes.data()+rva,width);return true;
    }};
    Sample sample{};
    Expect(ReadObservedFields(reader,&sample)&&sample.sceneId==23&&sample.controlledCharacter==0x12345000&&sample.saveUiState==12&&sample.slotZeroRecord==0&&sample.sceneTransitionPending==1,
           "production sampler must decode scene as a WORD without merging adjacent engine state");
    Expect(sample.activeSceneId==348&&sample.titleChoiceState==2&&sample.titleChoiceFlags==0x20&&sample.messageBank==0,
           "active field, choice WORD and flag/bank BYTE values keep their actual native widths");
    set(0x00D2CA90,0x006600A4);
    Expect(ReadObservedFields(reader,&sample)&&sample.sceneId==164,"loaded field 164 must not become bogus scene 6684836");
    memory.fail=true; sample.sceneId=777;
    Expect(!ReadObservedFields(reader,&sample)&&sample.sceneId==777,"an incomplete sample must not publish plausible default scene values");
    Expect(!ReadObservedFields({nullptr,nullptr},&sample)&&!ReadObservedFields(reader,nullptr),"sampler rejects missing reader/output");
}

void TestObserverPrimitives() {
    Publication p;
    Expect(p.Publish(Phase::ObserveOnly,Failure::None),"observer publication starts");
    p.RequestStop();
    Expect(!p.Publish(Phase::ObserveOnly,Failure::None) && p.Stopped(),"stop absorbs every later installer/callback publication");
    Expect(p.Read().phase==Phase::Stopped,"snapshot exposes stop");
    Publication failed;
    Expect(failed.Publish(Phase::FailedVisible,Failure::ReadFault),"failure publication accepted");
    Expect(!failed.Publish(Phase::ObserveOnly,Failure::None),"terminal failure cannot rearm");
    failed.RequestStop();
    Expect(failed.Read().failure==Failure::ReadFault,"stop preserves failure reason");
    EdgeQueue queue;
    for(Phase phase:{Phase::Disabled,Phase::Succeeded,Phase::Stopped,Phase::FailedVisible}) {
        Expect(NeedsTelemetryPump(phase,1),"worker remains available for a terminal callback's pending final edge");
        Expect(!NeedsTelemetryPump(phase,0),"worker can leave after the terminal producer drains");
    }
    for(uint32_t i=0;i<kEdgeCapacity;++i) {
        Expect(queue.HasCapacity(),"producer reserves telemetry capacity before admitting an action");
        Edge e{};e.serial=i;e.sample.sceneId=i;
        Expect(queue.Push(e),"fixed edge queue accepts each bounded record");
    }
    Expect(!queue.Push({}),"queue overflow is explicit");
    Expect(!queue.HasCapacity(),"a full queue cannot admit an unrecordable action");
    for(uint32_t i=0;i<kEdgeCapacity;++i) { Edge e{};Expect(queue.Pop(&e) && e.serial==i && e.sample.sceneId==i,"edge FIFO preserves full ordered record"); }
    Edge e{};Expect(!queue.Pop(&e),"queue empty returns without blocking");
    for(uint32_t i=0;i<3*kEdgeCapacity;++i) { e.serial=i;Expect(queue.Push(e),"ring wraps");Edge out{};Expect(queue.Pop(&out)&&out.serial==i,"ring wrap preserves order"); }
    int original=0,before=0,after=0;
    RunSceneObserver([&]() noexcept {++original;},[&](bool post) noexcept {if(post)++after;else ++before;});
    Expect(original==1 && before==1 && after==1,"scene adapter calls original exactly once between samples");
    original=0;int observed=0;
    RunOpeningObserver([&]() noexcept {++original;return uintptr_t{0};},
        []() noexcept {return OpeningEvidence{true,true,true};},
        [&](OpeningEvidence evidence) noexcept {Expect(evidence.readable&&evidence.active&&original==1,"opening readiness comes from the engine wait byte after vanilla, even when EAX is zero");++observed;});
    Expect(original==1&&observed==1,"opening original and observation execute exactly once");
    RunOpeningObserver([&]() noexcept {++original;return uintptr_t{0xDEADBEEF};},
        []() noexcept {return OpeningEvidence{true,false,true};},
        [&](OpeningEvidence evidence) noexcept {Expect(evidence.readable&&!evidence.active,"arbitrary nonzero EAX cannot invent a loaded opening");});
    Expect(original==2,"failed opening evidence still preserves the original call");
    RunOpeningObserver([&]() noexcept {++original;},[]() noexcept {return OpeningEvidence{};},
        [&](OpeningEvidence evidence) noexcept {Expect(!evidence.sampled,"an opening callback racing installation is unsampled, not a read fault");});
    using namespace FfxHooks::F8Runtime;
    ExecutableIdentity identity{0x014C,0x010B,0x55D2F3CC,0x0237D000};
    Expect(ValidateProfileAndRanges(identity,0x00400000)==Failure::None,"supported PE and all exact spans accepted");
    for(int i=0;i<4;++i) {auto wrong=identity;switch(i){case 0:wrong.machine=0x8664;break;case 1:wrong.optionalMagic=0x20B;break;case 2:++wrong.timestamp;break;case 3:--wrong.sizeOfImage;break;}
        Expect(ValidateProfileAndRanges(wrong,0x00400000)==Failure::UnsupportedProfile,"profile drift rejected before reads");}
    Expect(ValidateProfileAndRanges(identity,UINT32_MAX)==Failure::TargetOutOfRange,"mapped address overflow rejected");
}
std::string FunctionBody(const std::string& source, const std::string& name) {
    const auto start=source.find(name);if(start==std::string::npos)return {};
    const auto open=source.find('{',start);if(open==std::string::npos)return {};
    int depth=1;size_t i=open+1;
    for(;i<source.size()&&depth;++i) {if(source[i]=='{')++depth;if(source[i]=='}')--depth;}
    return depth==0?source.substr(open,i-open):std::string{};
}
void TestObserverSource() {
    const auto source=Source("hooks/BootSkipHook.cpp");
    for(const char* name:{"SceneTickShim()", "OpeningShim(void*", "ObserveSample(const Sample&", "SampleGame(Sample*",
        "ActionAdmitted(void*", "ValidateActionCode(void*", "ActionOpeningWait(void*", "NativeFinishOpening(void*",
        "NativeRequestLoad(void*", "NativeCompareUi(void*", "ObserveScene(bool", "ReadOpeningEvidence()"}) {
        const auto body=FunctionBody(source,name);
        Expect(!body.empty(),"production callback/sampler body exists");
        for(const char* forbidden:{"Sleep(","Log(","printf", "Config::", "new ", "malloc", "fopen", "CreateFile", "GetEnvironmentVariable", "VirtualQuery"})
            Expect(body.find(forbidden)==std::string::npos,"callback has no I/O, config, logging, formatting, allocation or sleep");
    }
    for(const char* forbidden:{"FFX_Save_AutoSave", "FFX_Save_WriteFileWithCrc", "fwrite", "FFX_Scene_RequestTransition", "CreateRemoteThread", "save_ram", "GetEnvironmentVariable", "delete ", "unHook("})
        Expect(source.find(forbidden)==std::string::npos,"adapter has no save writer, warp, legacy resolver or reachable gateway destruction");
    Expect(source.find("alignas(8) uint64_t g_sceneTrampoline")!=std::string::npos && source.find("alignas(8) uint64_t g_openingTrampoline")!=std::string::npos,"trampolines have stable aligned process-lifetime storage");
    const auto install=FunctionBody(source,"InstallFastloadHook(");
    Expect(install.find("ValidateTarget") < install.find("new PLH::x86Detour"),"all signatures precede detour construction");
    Expect(source.find("ParseExecutableIdentity")!=std::string::npos,"installer reuses shared PE parser");
    const auto openingProbe=FunctionBody(source,"OpeningEvidence ReadOpeningEvidence()");
    Expect(openingProbe.find("g_publication.Read().phase") < openingProbe.find("ReadObservedMemory"),
           "a stopped opening observer must call vanilla without probing game data");
    const auto openingShim=FunctionBody(source,"OpeningShim(void*");
    Expect(openingShim.find("!evidence.sampled")<openingShim.find("Failure::ReadFault"),
           "late readiness publication cannot turn a skipped opening probe into a read fault");

    const size_t openingConstruct=install.find("g_openingDetour=new PLH::x86Detour");
    const size_t optionalAdmission=install.find("if(IsTerminal(g_publication.Read().phase))");
    Expect(optionalAdmission!=std::string::npos&&optionalAdmission<openingConstruct,
           "a terminal scene callback during installation must block the optional opening detour");

}
void TestEarlyStartup() {
    const auto source=Source("dllmain.cpp");
    const auto worker=FunctionBody(source,"static DWORD WINAPI HooksWorkerThread(LPVOID)");
    const auto early=FunctionBody(source,"static void StartFastloadEarlyIfRequested()");
    const size_t capture=worker.find("CaptureF8StartupGates()"),start=worker.find("StartFastloadEarlyIfRequested()"),sleep=worker.find("Sleep("),general=worker.find("InstallHooks()");
    Expect(capture!=std::string::npos&&start!=std::string::npos&&sleep!=std::string::npos&&capture<start&&start<sleep&&sleep<general,"early Fastload follows capture and precedes general install delay");
    Expect(early.find("F8CatalogGateEnabled(\"development.fastload_autosave\")")!=std::string::npos&&early.find("LogF8CatalogGate")!=std::string::npos,"early install uses and reports exact immutable resolver result");
    for(const char* forbidden:{"dashboardReady","Aurora","NativeMenu","F7_IsEnabled","Present"})
        Expect(early.find(forbidden)==std::string::npos,"early install is independent of delayed UI/render producers");
    for(const char* required:{"FFXHOOKS_VALIDATE_ONLY","FFXHOOKS_FASTLOAD_OBSERVE_ONLY","VK_LSHIFT","VK_RSHIFT","InstallFastloadHook"})
        Expect(early.find(required)!=std::string::npos,"early install has validation/physical bypass/observe-only gates");
    const auto dllmain=FunctionBody(source,"BOOL APIENTRY DllMain(");
    const auto detach=dllmain.substr(dllmain.find("case DLL_PROCESS_DETACH:"));
    Expect(detach.find("RequestFastloadStop()")<detach.find("F7_RequestStop()"),"detach publishes Fastload stop before other producer stops");
    for(const char* forbidden:{"RemoveFastloadHook", "Sleep(", "WaitFor", "Join", "FlushFastload", "InstallFastload", "GetAsyncKeyState"})
        Expect(dllmain.find(forbidden)==std::string::npos,"DllMain cannot install, remove, wait, log telemetry, or query input");
    Expect(worker.find("FlushFastloadTelemetry()")!=std::string::npos&&worker.find("FastloadNeedsPump()")!=std::string::npos,"existing worker flushes bounded telemetry independent of Present");
    const auto adapter=Source("hooks/BootSkipHook.cpp");const auto install=FunctionBody(adapter,"InstallResult InstallFastloadHook(");
    const size_t firstDetour=install.find("new PLH::x86Detour");
    Expect(install.find("if(!options.gateEnabled)return")<firstDetour&&install.find("if(options.startupShiftHeld)")<firstDetour&&install.find("if(options.validateOnly)")<firstDetour,"OFF, Shift and validate-only return before detour construction");
    Expect(early.find("options.observeOnly = EnvFlagEnabled(\"FFXHOOKS_FASTLOAD_OBSERVE_ONLY\")")!=std::string::npos,
           "the immutable research override selects observer mode explicitly");
    const auto sampleBody=FunctionBody(adapter,"ObserveSample(const Sample&");
    Expect(sampleBody.find("ExecuteAction(actions,g_observeOnly.load")!=std::string::npos,
           "production invokes the tested executor with its actual immutable mode");
    const auto ready=FunctionBody(adapter,"bool CallbackReady()");
    Expect(ready.find("IsTerminal")!=std::string::npos&&ready.find("?1u:3u")!=std::string::npos,
           "apply callbacks remain inert until both hooks publish readiness, and after terminal state");
    Expect(source.find("FastBootSkipEnabledFromConfig") == std::string::npos && source.find("FFXHOOKS_ENABLE_FAST_BOOT_SKIP") == std::string::npos,"legacy BootSkip startup has no resolver path");
    for(Phase phase : {Phase::Disabled,Phase::ObserveOnly,Phase::Bypassed,Phase::FailedVisible,Phase::Succeeded,Phase::Stopped}) {
        for(unsigned failure=0;failure<=static_cast<unsigned>(Failure::TelemetryOverflow);++failure) {
            RuntimeSnapshot snapshot{};snapshot.phase=phase;snapshot.failure=static_cast<Failure>(failure);snapshot.gateEnabled=phase!=Phase::Disabled;
            const char* text=RuntimeDetail(snapshot,true);
            Expect(std::char_traits<char>::length(text)<=32,"every runtime detail fits F8's bounded status budget");
        }
    }
}
#ifdef _WIN32
__declspec(naked) void InvokeNativeTitleChoiceWait(void*) {
    __asm {
        mov eax,2
        jmp dword ptr [esp+4]
    }
}
#endif
void TestNativeEarlyLoadBranch() {
#ifdef _WIN32
    static_assert(sizeof(uintptr_t)==4,"native fixture must run in an isolated x86 process");
    constexpr size_t imageSize=0x0237D000u;
    struct Image {
        uint8_t* bytes=nullptr;
        unsigned nativeRequests=0;
        TitleChoiceIo choice{};
    } image;
    image.bytes=static_cast<uint8_t*>(VirtualAlloc(nullptr,imageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Expect(image.bytes!=nullptr,"native early-load fixture allocates its own image");
    if(!image.bytes)return;
    const uintptr_t base=reinterpret_cast<uintptr_t>(image.bytes);
    for(const auto& span:kCodeSpans) {
        std::memcpy(image.bytes+span.rva,span.signature,span.size);
        for(const auto& relocation:span.relocations) if(relocation.rva) {
            const uint32_t address=static_cast<uint32_t>(base+relocation.rva);
            std::memcpy(image.bytes+span.rva+relocation.offset,&address,sizeof(address));
        }
    }
    // Execute the exact call/test/branch. Only its two destinations are fixture
    // markers: ordinary field work = 1; native Load bypass = 0.
    const uint8_t fieldMarker[]={0xB8,1,0,0,0,0xC3};
    const uint8_t loadMarker[]={0x31,0xC0,0xC3};
    std::memcpy(image.bytes+0x00420E09,fieldMarker,sizeof(fieldMarker));
    std::memcpy(image.bytes+0x004217D9,loadMarker,sizeof(loadMarker));
    const uint16_t title=23;const int32_t noMenu=-1,pendingMenu=1;
    std::memcpy(image.bytes+RVA_FFX_SCENE_STATE_OBJECT,&title,sizeof(title));
    std::memcpy(image.bytes+RVA_FFX_CURRENT_MENU_SCREEN_ID,&noMenu,sizeof(noMenu));
    std::memcpy(image.bytes+RVA_FFX_FASTLOAD_PENDING_MENU,&pendingMenu,sizeof(pendingMenu));
    const uint32_t activeTitle=23;const uint16_t readyChoice=2;
    std::memcpy(image.bytes+RVA_FFX_FASTLOAD_ACTIVE_SCENE,&activeTitle,4);
    std::memcpy(image.bytes+RVA_FFX_FASTLOAD_TITLE_CHOICE_STATE,&readyChoice,2);
    const uint32_t answerPointer=static_cast<uint32_t>(base+0x01467940),windowPointer=static_cast<uint32_t>(base+0x01466610);
    std::memcpy(image.bytes+0x01468A58,&answerPointer,4);std::memcpy(image.bytes+0x014676B8,&windowPointer,4);
    const uint8_t answerHeader[]={0,1,0,0xFF,0,3,1,0};std::memcpy(image.bytes+0x01467940,answerHeader,8);
    const uint16_t openWindow=3;std::memcpy(image.bytes+0x0146663E,&openWindow,2);
    // The real await-choice instruction sets window2's flag through the real
    // native bank/stride getter. Stop before the surrounding function epilogue.
    image.bytes[0x0045962E]=0xC3;
    bool protectedCode=true;DWORD previous=0;
    for(const auto& span:kCodeSpans)
        protectedCode=VirtualProtect(image.bytes+span.rva,span.size,PAGE_EXECUTE_READ,&previous)!=FALSE&&protectedCode;
    protectedCode=VirtualProtect(image.bytes+0x004217D9,sizeof(loadMarker),PAGE_EXECUTE_READ,&previous)!=FALSE&&protectedCode;
    protectedCode=FlushInstructionCache(GetCurrentProcess(),image.bytes,imageSize)!=FALSE&&protectedCode;
    Expect(protectedCode,"native fixture executes from read/execute pages");
    if(!protectedCode){VirtualFree(image.bytes,0,MEM_RELEASE);return;}
    const auto nativeWindow=reinterpret_cast<uint8_t*(__cdecl*)(int)>(image.bytes+0x0046BE20);
    Expect(nativeWindow(2)==image.bytes+0x00F26D88,
           "real native getter selects field bank0/window2 at the proven address");
    InvokeNativeTitleChoiceWait(image.bytes+0x00459621);
    Expect(image.bytes[0x00F26DA5]==0x20&&image.bytes[0x00F26DA4]==0&&image.bytes[0x00F26DA6]==0,
           "real native await-choice code sets only the sampled flag BYTE");

    ActionIo io{};io.context=&image;
    io.admitted=[](void*) noexcept {return true;};
    io.validateCode=[](void* context,uint32_t) noexcept {
        const auto& state=*static_cast<Image*>(context);
        for(const auto& span:kCodeSpans)
            if(ValidateTarget(span.target,state.bytes+span.rva,span.size,
                              reinterpret_cast<uintptr_t>(state.bytes))!=TargetStatus::Match)return false;
        return true;
    };
    io.sample=[](void* context,Sample* sample) noexcept {
        const ObservationReader reader{context,[](void* p,uint32_t rva,void* output,size_t width) noexcept {
            const auto& state=*static_cast<Image*>(p);
            if(rva>0x0237D000u||width>0x0237D000u-rva)return false;
            std::memcpy(output,state.bytes+rva,width);return true;
        },reinterpret_cast<uintptr_t>(static_cast<Image*>(context)->bytes)};
        if(!ReadObservedFields(reader,sample))return false;
        sample->fieldSystemReady=true;sample->nowMs=150;return true;
    };
    image.choice={&image,io.admitted,io.sample,
        [](void* context,uint32_t rva,uint8_t expected,uint8_t desired) noexcept {
            auto* bytes=static_cast<Image*>(context)->bytes;
            return static_cast<uint8_t>(_InterlockedCompareExchange8(reinterpret_cast<volatile CHAR*>(bytes+rva),
                static_cast<CHAR>(desired),static_cast<CHAR>(expected)))==expected;
        },
        [](void* context,uint32_t rva,uint64_t expected,uint64_t desired) noexcept {
            auto* bytes=static_cast<Image*>(context)->bytes;
            return static_cast<uint64_t>(InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(bytes+rva),
                static_cast<LONG64>(desired),static_cast<LONG64>(expected)))==expected;
        }};
    io.requestLoad=[](void* context,uint32_t command) noexcept {
        auto& state=*static_cast<Image*>(context);++state.nativeRequests;
        return command==0x40100000&&PublishTitleLoad(state.choice);
    };
    const auto nativeGate=reinterpret_cast<int(__cdecl*)()>(image.bytes+RVA_FFX_FASTLOAD_FIELD_LOAD_GATE);
    Expect(nativeGate()==1,"native idle branch normally permits field/intro processing");
    int32_t busy=1;std::memcpy(image.bytes+RVA_FFX_SCENE_TRANSITION_PENDING,&busy,sizeof(busy));
    Expect(ExecuteAction(ActionRequestVanillaLoad,false,io).result==ActionResult::Conflict&&image.nativeRequests==0,
           "a real pending-transition sample prevents native load before any effect");
    busy=0;std::memcpy(image.bytes+RVA_FFX_SCENE_TRANSITION_PENDING,&busy,sizeof(busy));
    Sample sampled{};Expect(io.sample(io.context,&sampled),"native fixture uses the production field sampler");
    auto bootstrap=sampled;bootstrap.sceneId=348;
    const State ready=Advance(Start(true,false,100),bootstrap).state;
    const Decision decision=Advance(ready,sampled);
    const ActionOutcome outcome=ExecuteAction(decision.actions,false,io);
    Expect(image.bytes[0x01467941]==2&&image.bytes[0x01467958]==1,
           "automatic Load must publish native title choice1 instead of leaving the title worker waiting");
    Expect(decision.actions==ActionRequestVanillaLoad&&outcome.result==ActionResult::Accepted&&image.nativeRequests==1,
           "post-startup ready title commits exactly one Load choice");
    Expect(nativeGate()==1,"publishing the title choice leaves field processing active so its script can finish");
    Expect(ExecuteAction(ActionRequestVanillaLoad,false,io).result==ActionResult::Conflict&&image.nativeRequests==1,
           "an accepted title answer cannot be published twice");
    // The title script issues its normal Load command after consuming choice1.
    reinterpret_cast<void(__cdecl*)(uint32_t)>(image.bytes+RVA_FFX_FASTLOAD_LOAD_COMMAND)(0x40100000);
    Expect(nativeGate()==0,"the real native branch bypasses ordinary field/intro work after the early request");
    Sample after{};Expect(io.sample(io.context,&after)&&after.saveLoadScreenState==1&&
        after.saveDirection==0&&after.sceneId==23&&after.sceneTransitionPending==0,
        "early native admission changes only the Load state, not scene or transition state");
    Expect(ExecuteAction(ActionRequestVanillaLoad,false,io).result==ActionResult::Conflict&&image.nativeRequests==1,
           "an already-active native Load cannot be requested twice");
    VirtualFree(image.bytes,0,MEM_RELEASE);
#endif
}
void TestNativeTitleLifecycle(const char* executable) {
#ifdef _WIN32
    HMODULE mapped=LoadLibraryExA(executable,nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Expect(mapped!=nullptr,"exact private FFX image maps without entrypoint, imports or game startup");
    if(!mapped)return;
    auto* bytes=reinterpret_cast<uint8_t*>(mapped);
    const uintptr_t base=reinterpret_cast<uintptr_t>(mapped);
    bool code=true;
    for(const auto& span:kCodeSpans)code=ValidateTarget(span.target,bytes+span.rva,span.size,base)==TargetStatus::Match&&code;
    Expect(code,"title response evidence matches the independently hashed native image");
    if(!code){FreeLibrary(mapped);return;}
    const auto write=[&](uint32_t rva,const void* data,size_t width) {
        DWORD old=0,discard=0;
        if(!VirtualProtect(bytes+rva,width,PAGE_EXECUTE_READWRITE,&old))return false;
        std::memcpy(bytes+rva,data,width);
        return VirtualProtect(bytes+rva,width,old,&discard)&&FlushInstructionCache(GetCurrentProcess(),bytes+rva,width);
    };
    // Native confirmation and wait logic remain exact. Only sound output and
    // the close-animation request are inert boundaries in this private image.
    const uint8_t ret=0xC3;
    Expect(write(0x486B00,&ret,1)&&write(0x4AB8A0,&ret,1),"isolated native title fixture silences only audio/animation boundaries");
    const auto sample=[](void* context,Sample* out) noexcept {
        const ObservationReader reader{context,[](void* image,uint32_t rva,void* output,size_t width) noexcept {
            if(rva>=0x237D000||width>0x237D000-rva)return false;
            std::memcpy(output,static_cast<uint8_t*>(image)+rva,width);return true;
        },reinterpret_cast<uintptr_t>(context)};
        if(!ReadObservedFields(reader,out))return false;
        out->fieldSystemReady=true;return true;
    };
    const TitleChoiceIo choice{bytes,[](void*) noexcept {return true;},sample,
        [](void* context,uint32_t rva,uint8_t expected,uint8_t desired) noexcept {
            return static_cast<uint8_t>(_InterlockedCompareExchange8(
                reinterpret_cast<volatile char*>(static_cast<uint8_t*>(context)+rva),static_cast<char>(desired),static_cast<char>(expected)))==expected;
        },
        [](void* context,uint32_t rva,uint64_t expected,uint64_t desired) noexcept {
            return static_cast<uint64_t>(InterlockedCompareExchange64(
                reinterpret_cast<volatile LONG64*>(static_cast<uint8_t*>(context)+rva),static_cast<LONG64>(desired),static_cast<LONG64>(expected)))==expected;
        }};
    const auto registry=reinterpret_cast<void(__cdecl*)(int)>(bytes+0x4B8690);
    const auto confirm=reinterpret_cast<void(__cdecl*)(int)>(bytes+0x4B6490);
    const auto poll=reinterpret_cast<int(__cdecl*)(int,void*)>(bytes+0x459A80);
    const auto load=reinterpret_cast<void(__cdecl*)(uint32_t)>(bytes+0x421870);
    for(int variant:{0,1}) {
        const uint64_t zero=0;
        for(const auto& span:kReadSpans)Expect(write(span.rva,&zero,span.width),"private title fixture data initialized");
        registry(variant);
        const uint32_t answer=variant?0x1468300:0x1467940,window=variant?0x1466F90:0x1466610;
        const int32_t noMenu=-1,pending=1;const uint32_t scene32=23;
        const uint16_t scene=23,logical=2,physical=3,confirmKey=0x20;const uint8_t waitFlag=0x20;
        Expect(write(0xD2CA90,&scene,2)&&write(0xEFBBF8,&scene32,4)&&write(0xEFBBF0,&noMenu,4)&&
               write(0xEFBBF4,&pending,4)&&write(0xF26D9C,&logical,2)&&write(0xF26DA5,&waitFlag,1)&&
               write(window+0x2E,&physical,2),"ready native title has both logical and physical choice state");
        std::array<uint8_t,32> initial{};
        const uint8_t header[]={0,1,0,0xFF,0,3,1,0};std::memcpy(initial.data(),header,8);initial[0x18]=0xFF;
        Expect(write(answer,initial.data(),initial.size()),"native answer starts at new-game cursor0 without a committed result");
        Sample observed{};
        Expect(sample(bytes,&observed)&&TitleReady(observed)&&observed.titleAnswerRva==answer,
               "production sampler resolves both native registry variants");
        const uint8_t cursor=1;
        Expect(write(answer+2,&cursor,1)&&write(0x21D09D4,&confirmKey,2),"fixture selects actual native Load cursor and confirm input");
        confirm(2);
        std::array<uint8_t,32> manual{};std::memcpy(manual.data(),bytes+answer,manual.size());
        Expect(manual[1]==2&&manual[7]==1&&manual[0x18]==1,"unmodified native confirm commits one Load answer");
        Expect(write(answer,initial.data(),initial.size())&&write(0x21D09D4,&zero,2),"fixture resets only detached response/input data");
        Expect(PublishTitleLoad(choice)&&std::memcmp(manual.data(),bytes+answer,manual.size())==0,
               "automatic answer matches every byte produced by actual native confirmation");
        Expect(*reinterpret_cast<uint16_t*>(bytes+0x21D09D4)==0&&*reinterpret_cast<int32_t*>(bytes+0x8CB994)==0,
               "automatic response does not inject global input or start an out-of-band load");
        uint8_t worker[8]={2,0,0,0,0xFF,0xFF,0,0};
        Expect(poll(0,worker)==0&&worker[4]==1&&worker[5]==0&&*reinterpret_cast<uint16_t*>(bytes+0xF26D9C)==3,
               "actual title wait consumes Load1 and requests closure before resuming the script");
        Expect(*reinterpret_cast<int32_t*>(bytes+0x8CB994)==0,"native title consumption still precedes save reading");
        const uint16_t closed=0;Expect(write(0xF26D9C,&closed,2),"fixture completes the detached close animation");
        Expect(poll(0,worker)==1&&worker[4]==1&&worker[5]==0,"actual title wait returns Load1 after dialog closure");
        load(0x40100000);
        Expect(*reinterpret_cast<int32_t*>(bytes+0x8CB994)==1&&*reinterpret_cast<int32_t*>(bytes+0x8CB99C)==0,
               "native Load starts after the title response has been consumed and closed");
        Expect(!PublishTitleLoad(choice),"the completed title lifecycle cannot be dispatched again");
    }
    FreeLibrary(mapped);
#else
    (void)executable;
#endif
}
} // namespace
int main(int argc,char** argv) {
    TestTitleChoicePublication();
    if(argc==2)TestNativeTitleLifecycle(argv[1]);
#ifdef _WIN32
    else {std::fprintf(stderr,"Exact native fixture path required\n");return 2;}
#endif
    TestSecondLoadCannotFinishAutomatic();
    TestNativeTitleInputAdmission();
    TestNativeEarlyLoadBranch();
    TestInitialTitleCannotPreemptStartup();
    TestInterruptedLoadCannotClaimManualSuccess();
    TestAddresses(); TestSignatures(); TestNoLegacyOwner(); TestPolicy(); TestCorrectedStartupSequence(); TestDeadlines(); TestActionExecutor(); TestObserverPrimitives(); TestObservedFieldWidths(); TestObserverSource(); TestEarlyStartup();
    if(failures) { std::fprintf(stderr,"FastloadRuntimeRt0: FAIL (%d/%d failed)\n",failures,checks); return 1; }
    std::printf("FastloadRuntimeRt0: PASS (%d checks)\n",checks); return 0;
}
