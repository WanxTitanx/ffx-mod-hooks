// Exercises the real Workshop menu and extracted F7 close adapter against a
// bounded menu-pool host. Rendering primitives count calls; no game is started.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <array>
#include <vector>
#include <string>
#include "../hooks/F7UiCore.h"
#include "../hooks/F8FlagsUiState.h"
#include "../hooks/EquipmentWorkshopRuntime.h"
#include "../hooks/AeonAscensionBridge.h"
#include "../hooks/EquipmentWorkshopCatalogBridge.h"
#include "../hooks/EquipmentWorkshopSettings.h"
#include "../hooks/EquipmentWorkshopNativeUi.h"
#include "../hooks/ElementHook.h"
namespace NativeMenu {
constexpr uintptr_t kImageBase=0x400000,POOL_VA=0x18408C0;
constexpr int POOL_MAX=32,POOL_STRIDE=152;
enum {O_ENTER=8,O_UPDATE=12,O_DRAW=16,O_AUX=20,O_VALIDATOR=28,O_COUNT=48,O_TOP=50,O_CANCEL=55,O_PAGE=58,O_GROUP62=62,O_GROUP63=63,O_ACTIVE=64,O_SLOTS=66,O_SELECTED=72};
struct Menu {int obj;};
alignas(16) unsigned char pool[POOL_MAX*POOL_STRIDE]{};
int popup=0,drawCalls=0;
uintptr_t FfxBase(){return reinterpret_cast<uintptr_t>(pool)-(POOL_VA-kImageBase);}
unsigned char* Pb(int obj,int at){return reinterpret_cast<unsigned char*>(static_cast<uintptr_t>(obj))+at;}
void WrB(int o,int at,unsigned char v){*Pb(o,at)=v;}
void WrW(int o,int at,short v){std::memcpy(Pb(o,at),&v,2);}
void WrP(int o,int at,void* v){std::memcpy(Pb(o,at),&v,4);}
int RdD(int o,int at){int v=0;std::memcpy(&v,Pb(o,at),4);return v;}
short RdW(int o,int at){short v=0;std::memcpy(&v,Pb(o,at),2);return v;}
int Alloc(){for(int i=0;i<32;++i)if(!pool[i*152+64]){auto* p=pool+i*152;std::memset(p,0,152);return static_cast<int>(reinterpret_cast<uintptr_t>(p));}return 0;}
void Register(int o){WrB(o,O_ACTIVE,1);}
void Reset(int o){std::memset(Pb(o,0),0,152);}
void ClaimModal(int o){popup=o;}
void ReleaseModalIfOwned(int o){if(popup==o)popup=0;}
void CloseMenu(Menu& m){if(m.obj)WrB(m.obj,65,1);m.obj=0;}
int padDirection=0,padEdge=0;
std::vector<int> sounds;
int PadDir(){return padDirection;} int PadEdge(){return padEdge;}
void PlaySfx(int id){sounds.push_back(id);}
float NX(float x){return x;}float NY(float x){return x;}float NW(float x){return x;}float NH(float x){return x;}
float MenuBorderPx(){return .002f;}float Osc01(int,int){return .5f;}
constexpr unsigned kMenuNeonGreenLine=1,kMenuNeonGreenLineLo=1;
void EncodeLabel(const char* s,unsigned char* out,size_t size){strncpy_s(reinterpret_cast<char*>(out),size,s,_TRUNCATE);}
struct TextCall {std::string text;float x,y;};
std::vector<TextCall> rendered;
void DrawString(const unsigned char* text,float x,float y){++drawCalls;rendered.push_back({reinterpret_cast<const char*>(text),x,y});}
void DrawStringSub(const unsigned char* text,float x,float y){DrawString(text,x,y);}
void DrawSolidRect(float,float,float,float,unsigned,unsigned){++drawCalls;}
void DrawCursor(float,float){++drawCalls;}
void DrawMenuBackdrop(){++drawCalls;}void DrawMenuNeonFrame(int){}
void DrawMenuGlassPanel(float,float,float,float,int,int){++drawCalls;}
}
static bool foreground=true;
static int cursorCount=0;
static LONG g_f7MouseWheelDelta=0;
static bool F7IsForegroundWindow(){return foreground;}
static FfxHooks::F7Ui::PointerState pointerState{};
static FfxHooks::F7Ui::PointerSample pointerSample{};
static void F7SeedPointerForDestination(){FfxHooks::F7Ui::SeedPointerForDestination(pointerState,pointerSample.buttonDown);}
static void F7AcquireCursorOwnership(){++cursorCount;}
static void F7ReleaseCursorOwnership(){if(cursorCount)--cursorCount;}
static bool EnvFlagEnabled(const char*){return false;}
static void Log(const char*,...){}
struct F7MouseInputResult {bool confirm=false,ownsDirectionalFrame=false;};
struct F7PointerSnapshot {bool valid;int wheelSteps;float x,y;FfxHooks::F7Ui::PointerDecision decision;};
static F7PointerSnapshot F7CapturePointer(){
    const auto decision=FfxHooks::F7Ui::ObservePointer(pointerState,pointerSample);
    const F7PointerSnapshot result{pointerSample.valid,pointerSample.wheelSteps,pointerSample.x,pointerSample.y,decision};
    pointerSample.wheelSteps=0;return result;
}
// Real mouse selection/wheel helper, extracted by the runner. Only device input
// and sound output are substituted; hover/release/arbitration use production code.
#include "WorkshopListMouse.inc"
namespace TestHost {
workshop::State state{};
workshop::AeonProgress aeons{};
bool available=false,failReadback=false,customizeUnlocked=true;
bool nativeDetails=false,scanActive=false,scanExpandedActive=false;
unsigned commits=0;
std::uint32_t gil=1000000;
FfxHooks::AeonAscension::Ledger receipts{};
FfxHooks::AeonAscension::SaveId saveId{};
FfxHooks::AeonAscension::Mapping paidMapping{};
}
namespace FfxHooks::EquipmentWorkshop {
bool Capture(workshop::State& out){if(!TestHost::available)return false;out=TestHost::state;return true;}
bool ReadAeons(workshop::AeonProgress& out){out=TestHost::aeons;return TestHost::available;}
workshop::Error Access(){
    workshop::Policy policy{};if(!TestHost::available)return workshop::Error::InvalidState;
    if(!Settings::Read(policy))return workshop::Error::InvalidPolicy;
    return TestHost::customizeUnlocked||policy.devIgnoreProgression?workshop::Error::Ok:workshop::Error::Locked;
}
const char* Detail(){return "Load a save to activate Equipment Workshop";}
workshop::Error Preview(const workshop::Request& r,workshop::Plan& out){
    workshop::Economy economy{};economy.gil=TestHost::gil;economy.customizeUnlocked=TestHost::customizeUnlocked?1:0;
    economy.aeons=TestHost::aeons;
    (void)CatalogBridge::Read(economy.catalog);
    if(!Settings::Read(economy.policy)||!Settings::AdmitsExpansion(r))return workshop::Error::InvalidPolicy;
    return TestHost::available?workshop::Preview(TestHost::state,r,out,economy):workshop::Error::InvalidState;
}
bool Commit(const workshop::Request& r,const workshop::Plan& reviewed){
    workshop::Plan plan{};
    if(Preview(r,plan)!=workshop::Error::Ok||std::memcmp(&plan,&reviewed,sizeof(plan)))return false;
    TestHost::state=plan.after;TestHost::gil-=plan.gilDebit;++TestHost::commits;
    if(TestHost::failReadback)TestHost::available=false;
    return true;
}
workshop::Error PreviewAscension(const AeonAscension::Request& request,AeonAscension::Plan& output){
    if(!TestHost::available)return workshop::Error::InvalidState;
    workshop::Economy economy{};economy.gil=TestHost::gil;economy.aeons=TestHost::aeons;
    economy.customizeUnlocked=TestHost::customizeUnlocked?1:0;
    if(!Settings::Read(economy.policy))return workshop::Error::InvalidPolicy;
    return AeonAscension::Preview(TestHost::state,TestHost::receipts,TestHost::saveId,TestHost::paidMapping,economy,request,output);
}
bool CommitAscension(const AeonAscension::Request& request,const AeonAscension::Plan& reviewed){
    AeonAscension::Plan current{};
    if(PreviewAscension(request,current)!=workshop::Error::Ok||std::memcmp(&current,&reviewed,sizeof(current)))return false;
    TestHost::state=current.inventory.after;TestHost::receipts=current.receipts;
    TestHost::gil-=current.inventory.gilDebit;++TestHost::commits;return true;
}
bool AscensionEffect(unsigned owner,unsigned effect) noexcept {
    for(const auto& piece:TestHost::state.pieces)if(piece.native[4]==owner)
        for(unsigned i=0;i<5;++i)if(AeonAscension::Authorized(TestHost::receipts,TestHost::saveId,piece,i,effect,TestHost::paidMapping))return true;
    return false;
}
}
namespace FfxHooks::EquipmentWorkshop::NativeUi {bool Active() noexcept {return TestHost::nativeDetails;}}
namespace FfxHooks {
bool IsElementHookInstalled(){return TestHost::scanActive;}
bool IsScanExpandedInstalled(){return TestHost::scanExpandedActive;}
}
#pragma warning(push)
#pragma warning(disable:4018) // Existing renderer loop signedness is outside this lifecycle regression.
#include "../hooks/EquipmentWorkshopMenu.inl"
#pragma warning(pop)
// Only external dependencies are stubbed. The close function below is extracted
// verbatim from dllmain.cpp by the runner so ownership omissions are exercised.
static NativeMenu::Menu g_nativeMenu{},g_arenaPlusMenu{},g_sinMenu{},g_f7Menu{};
static FfxHooks::F7Ui::ModalState g_f7UiModalState{};
static FfxHooks::F8Ui::AtomicOpenLatch g_f8MenuOpen;
static FfxHooks::F8Ui::ScalarEditor g_f8ScalarEditor;
static int g_f7MenuKind=0,g_f7CursorShowIncrements=0,g_f7EditActive=0,g_f7EditValue=0,g_f7EditDigits=0;
static int g_sinDraft=0,g_arenaPlusUltraSelection=0,g_f7ConfirmTimer=0,g_f7LastEdge=0,g_nativeHeldAction=-1;
static bool g_sinDraftActive=false,g_sinSeedEditing=false;
static float g_f7EasedRowY=-1;
static LONG g_forceSubsystem=0,g_nativeWantClose=0,g_arenaPlusWantOpen=0,g_sinWantOpen=0,g_f7WantOpenKind=-1,g_nativeWantSpawn=0;
static LONG g_f7CloseSourcePending=-1,g_nativeOtherOwnerPublished=0;
static constexpr int F7_MENU_FLAGS=4;
enum class ArenaPlusMenuKind {Ultra};static ArenaPlusMenuKind g_arenaPlusMenuKind{};
static bool ArenaPlus_IsUltraChild(ArenaPlusMenuKind){return false;}
static bool F7OwnsVisibleUi(){return EquipmentMenu::Active();}
static bool NativeMenuHubCloseDrainPending(){return false;}
static void NativeMenuForceGateClear(){g_forceSubsystem=0;}
static void NativeMenuQueueHubCloseDrain(int,bool){}
static void ArenaPlus_UltraCancelForClose(FfxHooks::F7Ui::CloseSource){}
static void ArenaPlus_CloseMenu(NativeMenu::Menu&){}
static void SinCurse_CloseMenu(){}static void F7Sub_CloseMenu(){}static void F8ReturnFlagsToGame(){}
static void ArenaPlusComposePick_Close(){}static void ArenaMixRenameAbort(){}static void SinRam_ClearSaveFeedback(){}
static const char* F7CloseSourceName(FfxHooks::F7Ui::CloseSource){return "fixture";}
#include "WorkshopCloseTransition.inc"
static bool NativeMenuLegacyModalAllocationIdle(){return !EquipmentMenu::Active();}
void NativeMenuForceGatePublish(){g_forceSubsystem=1;}
namespace FfxHooks {
long NativeMenu_ReserveAndConsumeOpenRequest(volatile long* request,long empty,volatile long* owner){
    if(*request==empty)return empty;InterlockedExchange(owner,1);return InterlockedExchange(request,empty);
}
}
static void PendingCloseAndOpenPump(){
#include "WorkshopOpenCloseOrder.inc"
}
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
#include "WorkshopMenuTransactionCases.inl"
#include "WorkshopMenuEconomyCases.inl"
#include "WorkshopF8SettingsCases.inl"
#include "TextLanguageMenuCases.inl"
#include "WorkshopMenuProgressionCases.inl"
#include "WorkshopNavigationCases.inl"
#include "ElementColorSettingsCases.inl"
#include "ElementVisibilityCases.inl"
#include "ScanSettingsMenuCases.inl"
#include "AeonWorkshopMenuCases.inl"
#include "VanguardMenuCases.inl"
#include "WorkshopCatalogMenuCases.inl"
#include "AeonAscensionMenuCases.inl"
#include "ModFeatureMenuCases.inl"
#include "ExtendedElementMenuCases.inl"
#include "ElementNameMenuCases.inl"
#include "MonsterRewardMenuCases.inl"
static void FullFifthPickerCases(){
    using A=EquipmentMenu::Action;
    for(unsigned kind=0;kind<2;++kind){
        Check(WorkshopTestOpen(),"full fifth picker fixture opens");
        auto& piece=TestHost::state.pieces[0];piece.fifthUnlocked=1;piece.native[5]=static_cast<unsigned char>(kind);
        WorkshopTestChoose(A::Piece,0);WorkshopTestChoose(A::Fifth);workshop::Policy policy{};
        for(unsigned id=0;id<131;++id){unsigned item=0,quantity=0;
            const bool valid=workshop::FifthCost(piece,static_cast<std::uint16_t>(0x8000+id),policy,item,quantity)&&workshop::CustomizeEligibility(piece,4,static_cast<std::uint16_t>(0x8000+id))==workshop::Error::Ok;
            Check(WorkshopTestLists(A::Value,0x8000+id)==valid,"real fifth picker exposes the complete type-filtered catalog");
        }
        Check(!WorkshopTestLists(A::Value,0x8014),"fifth picker never invents a native Customize recipe");
        WorkshopTestChoose(A::Value,kind?0x8055:0x8001);
        WorkshopTestChoose(A::Back);Check(TestHost::commits==0,"leaving the full picker confirmation spends nothing");
    }
    EquipmentMenu::StopReady();
}
int main(){
    foreground=true;g_forceSubsystem=1;
    InterlockedExchange(&EquipmentMenu::wantOpen,1);
    g_f7CloseSourcePending=static_cast<LONG>(FfxHooks::F7Ui::CloseSource::FocusLost);
    PendingCloseAndOpenPump();
    Check(EquipmentMenu::menu.obj!=0&&EquipmentMenu::Active()&&g_forceSubsystem!=0,"old F7 close is consumed before the new Workshop allocation");
    if(EquipmentMenu::menu.obj)EquipmentMenu::Draw(EquipmentMenu::menu.obj);
    Check(NativeMenu::drawCalls>0,"the first accepted open reaches its draw callback");
    EquipmentMenu::StopReady();
    foreground=false;
    const bool backgroundOpened=EquipmentMenu::Open();
    Check(!backgroundOpened&&!EquipmentMenu::Active(),"focus loss before allocation cancels an invisible Workshop open");
    EquipmentMenu::StopReady();
    foreground=true;g_forceSubsystem=1;
    Check(EquipmentMenu::Open()&&EquipmentMenu::Active(),"foreground request acquires a native menu object");
    // A queued F7 focus-close can be consumed after Workshop allocation in the
    // same Pump pass, before the first draw callback of the following frame.
    F7CloseTransition(FfxHooks::F7Ui::CloseSource::FocusLost,FfxHooks::F7Ui::CloseDestination::Game);
    Check(!EquipmentMenu::Active()||g_forceSubsystem!=0,"close retains a pump until Workshop releases its reservation");
    for(unsigned frame=0;frame<4;++frame)if(g_forceSubsystem)EquipmentMenu::Tick();
    Check(!EquipmentMenu::Active()&&NativeMenu::popup==0,"invisible close drains and releases the F7/F8 exclusion");
    EquipmentMenu::StopReady();
    g_forceSubsystem=1;Check(EquipmentMenu::Open(),"menu reopens after a canceled handoff");
    EquipmentMenu::Draw(EquipmentMenu::menu.obj);
    Check(NativeMenu::drawCalls>0,"unavailable inventory still renders the native information page");
    InterlockedExchange(&EquipmentMenu::wantClose,1);
    for(unsigned frame=0;frame<5;++frame)EquipmentMenu::Tick();
    Check(!EquipmentMenu::Active()&&NativeMenu::popup==0&&cursorCount==0,"shortcut close is bounded and releases modal and cursor ownership");
    WorkshopMenuTransactionCases();
    WorkshopMenuAudioCases();
    WorkshopMenuEconomyCases();
    WorkshopF8SettingsCases();
    TextLanguageMenuCases();
    WorkshopMenuProgressionCases();
    WorkshopF8DevelopmentCases();
    WorkshopNavigationCases();
    WorkshopExpansionSettingsCases();
    ElementColorSettingsCases();
    ElementVisibilityCases();
    ScanSettingsMenuCases();
    AeonWorkshopMenuCases();
    VanguardMenuCases();
    FullFifthPickerCases();
    WorkshopCatalogMenuCases();
    AeonAscensionMenuCases();
    ModFeatureMenuCases();
    ExtendedElementMenuCases();
    ElementNameMenuCases();
    NestedFeatureMenuCases();
    MonsterRewardMenuCases();
    TestHost::nativeDetails=false;Check(WorkshopTestOpen(),"native display status fixture opens");
    NativeMenu::rendered.clear();EquipmentMenu::Draw(EquipmentMenu::menu.obj);
    Check(WorkshopRendered("Native detail hook: OFF")&&WorkshopRendered("Reforge"),"Workshop explains disabled native display");
    TestHost::nativeDetails=true;NativeMenu::rendered.clear();EquipmentMenu::Draw(EquipmentMenu::menu.obj);
    Check(WorkshopRendered("Native detail hook: ON"),"Workshop identifies an active native detail hook");
    TestHost::nativeDetails=false;EquipmentMenu::StopReady();
    F8NativeSettingsReset();const int statusObj=NativeMenu::Alloc();
    Check(statusObj!=0,"Scan status fixture allocates");
    if(statusObj){F8NativeSettingsPush(statusObj,NativeSettingsPage::ElementScan);
        for(bool active:{false,true}){TestHost::scanActive=active;NativeMenu::rendered.clear();F8NativeSettingsDraw(statusObj,1);
            char label[128]{};F8NativeSettingsLabel(NativeSettingsPage::ElementScan,1,label,sizeof(label));
            Check(std::strstr(label,active?"Running: ON":"Running: OFF")&&WorkshopRendered(label),"Scan settings distinguish actual hook status from saved preferences");}
        TestHost::scanActive=false;F8NativeSettingsReset();NativeMenu::Reset(statusObj);}
    std::printf("EquipmentWorkshopMenuRt1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
