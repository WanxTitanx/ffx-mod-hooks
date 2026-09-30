// Runs the complete current F7Sub_InputCb body extracted by the test runner.
// The host replaces input, drawing and storage endpoints, never the callback.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vector>
#include <cstdio>
#include <cstring>
#include "../../NativeMenuShell/MenuFeedback.h"
#include "../hooks/F7ConfigEditor.h"
#include "../hooks/F7UiCore.h"
#include "../hooks/F8FlagsUiState.h"
#include "../hooks/F8FlagCatalog.h"
#include "../hooks/NativePortsHook.h"
static std::vector<int> sounds;
static bool foreground=true,saveOk=true,commitOk=true,capture=false,captureDone=false,captureCancel=false,bindOk=true;
static int hostDirection=0,hostEdge=0,hostSelection=0,hostTop=0,hostRowCount=3,pageSize=9,closed=0,pointerRow=-1;
static bool pointerConfirm=false;
static FfxHooks::NativeBindings::BindResult bindResult=FfxHooks::NativeBindings::BindResult::Ok;
namespace NativeMenu {
enum {O_SELECTED=72,O_TOP=50,O_COUNT=48,O_PAGE=58};
struct Menu{int obj=1;};
void Raw(int id){sounds.push_back(id);}
void PlaySfx(int id){FfxHooks::MenuAudio::Dispatch(id,Raw);}
int PadDir(){return hostDirection;}int PadEdge(){return hostEdge;}
int RdW(int,int offset){return offset==O_SELECTED?hostSelection:offset==O_TOP?hostTop:offset==O_COUNT?hostRowCount:pageSize;}
void WrW(int,int offset,short value){if(offset==O_SELECTED)hostSelection=value;else if(offset==O_TOP)hostTop=value;else if(offset==O_COUNT)hostRowCount=value;}
void WrB(int,int,int value){closed=value;}
float NX(float n){return n;}float NY(float n){return n;}float NW(float n){return n;}float NH(float n){return n;}
}
static FfxHooks::F7DifficultyRuntimeStatus difficulty;
namespace FfxHooks {
F7DifficultyRuntimeStatus F7_DifficultyStatus(){return difficulty;}
void F7_DifficultyApplyNow(){}
const char* F7_DifficultyGateName(F7Difficulty::AdapterGateCode){return "Unavailable";}
const char* F7_DifficultyResultName(F7Difficulty::ResultCode){return "Outcome";}
bool F7_SaveConfig(){return saveOk;}bool F7_ResetMusic(){return saveOk;}
size_t F8TabCount(){return 2;}
F8ScalarResult ResolveF8Scalar(const F8FlagSpec&){return {F8ScalarState::Valid,10};}
F8ScalarEditResult SetF8ScalarValue(const F8FlagSpec&,int value){return {saveOk?F8ScalarEditCode::Saved:F8ScalarEditCode::PersistFailed,value,{F8ScalarState::Valid,value},{}};}
F8EditResult SetF8FlagValue(const F8FlagSpec&,bool requested){F8EditResult r{};r.code=saveOk?F8EditCode::Saved:F8EditCode::PersistFailed;r.requestedValue=requested;r.effective.value=saveOk&&requested;return r;}
namespace Config {const char* BoolSourceName(BoolSource){return "Fixture";}}
namespace NativePorts {
bool MenuOpeningPadHeld(){return false;}bool BindingCaptureActive(){return capture;}
bool BeginBindingCapture(NativeBindings::Action){capture=bindOk;return bindOk;}
void CancelBindingCapture(){capture=false;}
bool ConsumeBindingCapture(NativeBindings::BindResult* result,bool* cancelled){if(!captureDone)return false;captureDone=false;*result=bindResult;*cancelled=captureCancel;return true;}
}
}
enum F7MenuKind {F7_MENU_MUSIC,F7_MENU_FORCE,F7_MENU_DIFF,F7_MENU_AI,F7_MENU_FLAGS};
enum F7RowType {F7RT_INFO,F7RT_TOGGLE,F7RT_STEPPER,F7RT_ACTION,F7RT_SCALAR,F7RT_BULK,F7RT_BINDING,F7RT_OPTIONS,F7RT_BACK};
enum {F7DC_PRESETS,F7DC_BASE,F7DC_AUTO,F7DC_WEAK,F7DC_RESIST,F7DC_ABSORB,F7DC_ACTIONS,F7DC_COUNT};
enum class NativeSettingsPage{None};
struct F7SubRow{const char* label="";F7RowType type=F7RT_INFO;int min=0,max=100,step=1;const char* desc="";};
static NativeMenu::Menu g_f7Menu;
static FfxHooks::F8Ui::AtomicOpenLatch g_f8MenuOpen;
static FfxHooks::F8Ui::CloseLatch g_f7CloseLatch;
static FfxHooks::F8Ui::ScalarEditor g_f8ScalarEditor;
static int g_f7MenuKind=0,g_f7ConfirmTimer=0,g_f7LastEdge=0,g_f7Col=0,g_f7ColRow=0,g_f7EditActive=0,g_f7EditValue=0,g_f7EditDigits=0,g_f7DiffPresetIdx=0,g_f7Tab=0,g_f7FlagCount=3,g_f7RowCount=3;
static int g_f7Vals[32]{},F7_BASE_MIN[9]{},F7_BASE_MAX[9]{100,100,100,100,100,100,100,100,100};
static bool g_f7DifficultyEnabled=false,g_f7HasLastScalarEdit=false,g_f7HasLastEdit=false;
static float g_f7EasedRowY=0;
static volatile LONG g_nativeWantSpawn=0;
static char g_f8BindingFeedback[96]{},g_nativeSettingsNotice[128]{};
static F7SubRow g_f7Rows[32];
static const FfxHooks::F8FlagSpec* g_f7FlagSpecs[32]{};
static const FfxHooks::F8FlagSpec* g_f7LastScalarEditSpec=nullptr;
static FfxHooks::F8ScalarEditResult g_f7LastScalarEdit{};
static FfxHooks::F8EditResult g_f7LastEdit{};
struct F7MouseInputResult{bool confirm=false,ownsDirectionalFrame=false;};
static F7MouseInputResult F7ListMouseTick(int,float,float,float,float,float,int,int){if(pointerRow>=0)hostSelection=pointerRow;return {pointerConfirm,pointerConfirm};}
static F7MouseInputResult F7DifficultyMouseTick(){if(pointerRow>=0)g_f7ColRow=pointerRow;return {pointerConfirm,pointerConfirm};}
struct Pointer{bool valid=false;int wheelSteps=0;float x=0,y=0;FfxHooks::F7Ui::PointerDecision decision{};};
using F7PointerSnapshot=Pointer;
static Pointer inputPointer;
static Pointer F7CapturePointer(){return inputPointer;}
static bool F7IsForegroundWindow(){return foreground;}
static void F7RequestClose(FfxHooks::F7Ui::CloseSource){closed=1;}
static bool F8NativeSettingsActive(){return false;}
static void F8NativeSettingsInput(int){}
static void F8MouseTabHitTest(int){}
static int F8NativeSettingsEnter(int,NativeSettingsPage){return 1;}
static void F7DiffSetStatus(const char*){}
static void F8RefreshScalarLabel(int){}
static int F7DiffColRows(int column){return column==F7DC_PRESETS?4:9;}
static bool F7DiffSetFieldDraft(int){return true;}
static const char* F7PresetName(int){return "Preset";}
static void F7_DiffPresetFill(int){}
static void F7DiffToggleBit(int,int){NativeMenu::PlaySfx(1);}
static void F7DiffScopeAction(int){NativeMenu::PlaySfx(1);}
static bool F7_CommitValsToConfig(){if(!commitOk)NativeMenu::PlaySfx(3);return commitOk;}
static void F7_BuildRows(int){}
static const FfxHooks::F8FlagSpec* g_f7LastEditSpec=nullptr;
static const char* F8EditCodeName(FfxHooks::F8EditCode){return "Result";}
static void F8ApplyTabBulk(bool,int){NativeMenu::PlaySfx(saveOk?1:3);}
static const char* F8ScalarEditCodeName(FfxHooks::F8ScalarEditCode){return "Result";}
static void Log(const char*,...){}
static int hostDigit=0;
static SHORT TestKey(int key){return key==hostDigit?1:0;}
#include "F7AdjustAudio.inc"
#define GetAsyncKeyState TestKey
#include "F7SaveFeedback.inc"
#include "F7InputAudio.inc"
#undef GetAsyncKeyState
namespace NativeMenu {
enum ActionId{ACT_MUSIC,ACT_BATTLE_CHEATS,ACT_EXIT,ACT_DIFFICULTY,ACT_FORCE_BATTLE,ACT_AI_SWAP,ACT_ARENA,ACT_SIN};
enum {RT_NONE,EDGE,HELD};
struct Row{ActionId action;int kind,rowType=RT_NONE,minVal=0,maxVal=1,stepVal=1;};
static Row g_rows[]={{ACT_MUSIC,EDGE},{ACT_BATTLE_CHEATS,EDGE},{ACT_EXIT,EDGE}};
static constexpr int kRowCount=3;
static int g_rowValue[3]{};static bool g_rowEdited[3]{};
static int g_ourResult=0,g_ourClosed=0;
static float g_easedRowY=0;
static bool(*g_inputAdmission)()=nullptr;
static struct {void(*onEdge)(ActionId)=nullptr;void(*onHeldEnter)(ActionId)=nullptr;} g_bridge;
#include "HubInputAudio.inc"
#include "HubAllocationPolicy.inc"
#include "HubAllocationResult.inc"
#include "HubDispatchAudio.inc"
}
#include "HubMouseAudio.inc"
static int checks=0,failed=0;
static void Check(bool ok,const char* label){++checks;if(!ok){++failed;std::printf("FAIL: %s\n",label);}}
static void Reset(int kind){
 foreground=saveOk=commitOk=bindOk=true;capture=captureDone=captureCancel=false;hostDirection=hostEdge=hostSelection=hostTop=closed=0;
 pointerRow=-1;pointerConfirm=false;hostRowCount=3;g_f7MenuKind=kind;g_f7ConfirmTimer=g_f7LastEdge=g_f7EditActive=0;g_f7Col=F7DC_PRESETS;g_f7ColRow=0;g_f7HasLastEdit=false;
 inputPointer={};
 hostDigit=0;
 g_f7CloseLatch={};g_f8ScalarEditor.Cancel();sounds.clear();for(auto& row:g_f7Rows)row={};g_f7Rows[2].type=F7RT_BACK;difficulty={};
 static FfxHooks::F8ScalarSpec scalar{"test.rate",10,1,100};static FfxHooks::F8FlagSpec flag{};flag.scalar=&scalar;flag.gate.canonicalKey="test.enabled";
 for(auto& spec:g_f7FlagSpecs)spec=&flag;for(auto& value:g_f7Vals)value=0;
}
#include "F7ChildAudioCases.inl"
int main(){
 ChildAudioCases();
 Reset(F7_MENU_DIFF);g_f7EditActive=1;g_f7EditDigits=6;g_f7EditValue=99999;hostDigit='7';F7Sub_InputCb(1);
 Check(sounds.empty(),"full direct-entry buffer does not emit another accepted-digit sound");
 for(bool saved:{false,true}){Reset(F7_MENU_MUSIC);saveOk=saved;hostSelection=2;hostEdge=0x20;F7Sub_InputCb(1);
     Check((closed!=0)==saved&&sounds==std::vector<int>{saved?1:3},"Music Save and Back closes only after successful persistence");}
 for(int row:{FfxHooks::F7Editor::MusicRows::Fade,FfxHooks::F7Editor::MusicRows::Count,FfxHooks::F7Editor::MusicRows::FirstSlot}){
   Reset(F7_MENU_MUSIC);hostRowCount=g_f7RowCount=18;g_f7Rows[row].type=F7RT_STEPPER;hostSelection=row;hostEdge=0x20;
   F7Sub_InputCb(1);Check(sounds.empty(),"Music draft-only row does not confirm a nonexistent action");
 }
 Reset(F7_MENU_FORCE);NativeMenu::g_ourClosed=0;NativeMenu::g_bridge.onEdge=[](NativeMenu::ActionId){};hostEdge=0x20;
 NativeMenu::OurListInputCb(1);NativeMenu::DispatchConfirm(NativeMenu::g_ourResult);
 Check(sounds.empty(),"hub submenu confirmation waits for allocation, not merely queuing");
 for(bool opened:{false,true}){sounds.clear();NativeMenu::PlayMenuOpenResult(opened);Check(sounds==std::vector<int>{opened?1:3},"actual submenu allocation chooses success or rejection");}
 Reset(F7_MENU_FORCE);hostRowCount=g_f7RowCount=4;
 g_f7Rows[2].type=F7RT_INFO;g_f7Rows[3].type=F7RT_BACK;hostSelection=2;hostEdge=0x20;
 F7Sub_InputCb(1);Check(sounds.empty(),"Last Encounter information does not confirm an action");
 for(int kind:{F7_MENU_MUSIC,F7_MENU_FORCE,F7_MENU_DIFF,F7_MENU_AI,F7_MENU_FLAGS}){
   Reset(kind);F7Sub_InputCb(1);Check(sounds.empty(),"all idle F7/FLAGS pages are silent");
   pointerRow=1;F7Sub_InputCb(1);F7Sub_InputCb(1);Check(sounds==std::vector<int>{1},"all F7/FLAGS pointer row changes sound once");
   Reset(kind);hostEdge=0x40;F7Sub_InputCb(1);Check(closed&&sounds==std::vector<int>{4},"all F7/FLAGS cancels sound once");
   Reset(kind);foreground=false;hostDirection=0x4000;hostEdge=0x20;F7Sub_InputCb(1);Check(sounds.empty(),"focus loss never plays action feedback");
 }
 Reset(F7_MENU_FLAGS);g_f7Rows[0].type=F7RT_OPTIONS;hostEdge=0x20;F7Sub_InputCb(1);Check(sounds==std::vector<int>{1},"opening root FLAGS child confirms once");
 for(bool success:{false,true}){Reset(F7_MENU_FLAGS);saveOk=success;g_f7Rows[0].type=F7RT_TOGGLE;hostEdge=0x20;F7Sub_InputCb(1);Check(sounds==std::vector<int>{success?1:3},"root toggle uses the storage outcome");}
 static const FfxHooks::F8ScalarSpec scalar{"test.rate",10,1,100};static FfxHooks::F8FlagSpec flag{};flag.scalar=&scalar;g_f7FlagSpecs[0]=&flag;
 for(bool success:{false,true}){Reset(F7_MENU_FLAGS);saveOk=success;g_f8ScalarEditor.Begin(0,10,1,100);hostEdge=0x20;F7Sub_InputCb(1);Check(sounds.size()==1&&sounds[0]==(success?1:3),"scalar failures report error, not navigation");Check(g_f8ScalarEditor.Active()!=success,"failed scalar save preserves the editable draft");}
 Reset(F7_MENU_FLAGS);g_f8ScalarEditor.Begin(0,10,1,100);hostEdge=0x40;F7Sub_InputCb(1);Check(sounds==std::vector<int>{4},"scalar cancel uses Back feedback");
 Reset(F7_MENU_FLAGS);bindOk=false;g_f7Rows[0].type=F7RT_BINDING;hostEdge=0x20;F7Sub_InputCb(1);Check(sounds==std::vector<int>{3},"unavailable binding capture reports rejection");
 Reset(F7_MENU_FLAGS);capture=captureDone=captureCancel=true;F7Sub_InputCb(1);Check(sounds==std::vector<int>{4},"capture cancellation is not an error");
 Reset(F7_MENU_DIFF);g_f7EditActive=1;hostEdge=0x40;F7Sub_InputCb(1);Check(sounds==std::vector<int>{4},"numeric edit cancel is audible");
 Reset(F7_MENU_DIFF);g_f7Col=F7DC_ACTIONS;difficulty.infrastructureInstalled=difficulty.callbackAdmissionOpen=true;difficulty.last.code=FfxHooks::F7Difficulty::ResultCode::NoActors;hostEdge=0x20;F7Sub_InputCb(1);Check(sounds==std::vector<int>{1},"arming difficulty outside battle does not beep an error");
 Reset(F7_MENU_FLAGS);hostDirection=0x4000;hostEdge=0x20;g_f7Rows[1].type=F7RT_OPTIONS;F7Sub_InputCb(1);Check(sounds==std::vector<int>{1},"navigation and action in one callback do not double play");
 Reset(F7_MENU_MUSIC);pointerRow=2;pointerConfirm=true;g_f7ConfirmTimer=10;F7Sub_InputCb(1);Check(!closed,"mouse confirm respects the same anti-repeat interval as keyboard");
 for(bool success:{false,true}){
   Reset(F7_MENU_FORCE);saveOk=success;NativeMenu::g_ourClosed=0;hostDirection=0x4000;hostEdge=0x20;
   NativeMenu::g_bridge.onEdge=[](NativeMenu::ActionId){if(!saveOk)NativeMenu::PlaySfx(3);};
   NativeMenu::OurListInputCb(1);Check(sounds.empty(),"hub confirmation waits for the dispatched outcome");
   NativeMenu::DispatchConfirm(NativeMenu::g_ourResult);
   Check(sounds==std::vector<int>{success?1:3},"hub outcome emits one correct sound after input plus dispatch");
 }
 Reset(F7_MENU_FORCE);NativeMenu::g_ourClosed=0;hostEdge=0x40;NativeMenu::OurListInputCb(1);Check(sounds==std::vector<int>{4},"hub cancel remains immediate and unique");
 Reset(F7_MENU_FORCE);NativeMenu::g_ourClosed=0;inputPointer.valid=true;inputPointer.x=.6f;inputPointer.y=.29f;inputPointer.decision.applyHover=true;
 F7MainMenuMouseTick(1);F7MainMenuMouseTick(1);Check(sounds==std::vector<int>{1}&&hostSelection==1,"real hub pointer helper emits one changed-hover cue");
 sounds.clear();inputPointer.y=.23f;inputPointer.decision.pressAdmitted=true;saveOk=false;
 F7MainMenuMouseTick(1);Check(sounds.empty(),"hub mouse confirmation also waits for the actual action result");NativeMenu::DispatchConfirm(NativeMenu::g_ourResult);Check(sounds==std::vector<int>{3},"hub mouse rejection never precedes error with success");
 Reset(F7_MENU_DIFF);g_f7EditActive=1;g_f7Col=F7DC_BASE;g_f7EditValue=100;hostDirection=0x2000;F7Sub_InputCb(1);Check(sounds.empty(),"numeric adjustment at its bound is silent");
 Reset(F7_MENU_FLAGS);g_f8ScalarEditor.Begin(0,100,1,100);hostDirection=0x2000;F7Sub_InputCb(1);Check(sounds.empty(),"scalar adjustment at its bound is silent");
 Reset(F7_MENU_MUSIC);g_f7Rows[0].type=F7RT_ACTION;hostDirection=0x2000;F7Sub_InputCb(1);Check(sounds.empty(),"horizontal input on a noneditable row does not confirm anything");
 Reset(F7_MENU_FORCE);NativeMenu::g_ourClosed=0;hostRowCount=1;hostDirection=0x4000;NativeMenu::OurListInputCb(1);Check(sounds.empty(),"single-row wrap does not claim a movement");
 std::printf("F7 input audio RT1: %d checks, %d failures\n",checks,failed);return failed?1:0;
}
