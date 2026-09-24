// Exercises the real Workshop menu and extracted F7 close adapter against a
// bounded menu-pool host. Rendering primitives count calls; no game is started.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#include "../hooks/F7UiCore.h"
#include "../hooks/F8FlagsUiState.h"
#include "../hooks/EquipmentWorkshopRuntime.h"
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
int PadDir(){return 0;} int PadEdge(){return 0;} void PlaySfx(int){}
float NX(float x){return x;}float NY(float x){return x;}float NW(float x){return x;}float NH(float x){return x;}
float MenuBorderPx(){return .002f;}float Osc01(int,int){return .5f;}
constexpr unsigned kMenuNeonGreenLine=1,kMenuNeonGreenLineLo=1;
void EncodeLabel(const char* s,unsigned char* out,size_t size){strncpy_s(reinterpret_cast<char*>(out),size,s,_TRUNCATE);}
void DrawString(const unsigned char*,float,float){++drawCalls;}
void DrawStringSub(const unsigned char*,float,float){++drawCalls;}
void DrawSolidRect(float,float,float,float,unsigned,unsigned){++drawCalls;}
void DrawCursor(float,float){++drawCalls;}
void DrawMenuBackdrop(){++drawCalls;}void DrawMenuNeonFrame(int){}
void DrawMenuGlassPanel(float,float,float,float,int,int){++drawCalls;}
}
static bool foreground=true;
static int cursorCount=0;
static LONG g_f7MouseWheelDelta=0;
static bool F7IsForegroundWindow(){return foreground;}
static void F7SeedPointerForDestination(){}
static void F7AcquireCursorOwnership(){++cursorCount;}
static void F7ReleaseCursorOwnership(){if(cursorCount)--cursorCount;}
static bool EnvFlagEnabled(const char*){return false;}
static void Log(const char*,...){}
struct F7MouseInputResult {bool ownsDirectionalFrame=false,confirm=false;};
static F7MouseInputResult F7ListMouseTick(int,float,float,float,float,float,int,int){return {};}
namespace FfxHooks::EquipmentWorkshop {
bool Capture(workshop::State&){return false;}
const char* Detail(){return "Load a save to activate Equipment Workshop";}
workshop::Error Preview(const workshop::Request&,workshop::Plan&){return workshop::Error::InvalidState;}
bool Commit(const workshop::Request&,const workshop::Plan&){return false;}
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
    std::printf("EquipmentWorkshopMenuRt1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
