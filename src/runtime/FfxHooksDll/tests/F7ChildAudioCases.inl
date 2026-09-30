// The input and sound dispatcher are production source. Game-domain actions
// return controlled outcomes here; their existing runtime suites test gameplay.
#include "ArenaKindAudio.inc"
;
static ArenaPlusMenuKind g_arenaPlusMenuKind=ArenaPlusMenuKind::Hub;
static int g_arenaPlusResult=0,g_arenaPlusClosed=0,g_arenaPlusLastConfirmEdge=0,g_arenaPlusInputCooldown=0,g_arenaPositionRow=0;
static volatile LONG g_arenaRenameConfirm=0,g_arenaRenameCancel=0;
static SRWLOCK g_arenaRenameLock=SRWLOCK_INIT;
static char g_arenaRenameDraft[41]{};
static constexpr int ARENA_PLUS_HUB_ROW_BACK=5;
static int actionFeedback=0,actionCalls=0;
static struct BattleInfo{int count=1;} battleInfo;
static const BattleInfo* g_arenaBattleDetail=&battleInfo;
static float ArenaPlus_ListLeft(){return .2f;}static float ArenaPlus_ListWidth(){return .4f;}
static float ArenaPlus_RowStep(){return .06f;}static float ArenaPlus_RowHeight(){return .05f;}
static void ArenaPlus_BuildRowsForKind(ArenaPlusMenuKind){}static void ArenaLibraryBuildRows(ArenaPlusMenuKind){}
static void ArenaPlus_AdjustPosition(int,int){NativeMenu::PlaySfx(actionFeedback?actionFeedback:1);}
static int ArenaPlus_MenuRowCount(ArenaPlusMenuKind){return hostRowCount;}
static int ArenaPlus_SubMenuBackRow(ArenaPlusMenuKind kind){return kind==ArenaPlusMenuKind::Hub?-1:hostRowCount-1;}
static void ArenaPlus_HandleMenuConfirm(int){++actionCalls;if(actionFeedback)NativeMenu::PlaySfx(actionFeedback);}
#include "ArenaInputAudio.inc"
#include "ArenaDispatchAudio.inc"

static bool g_sinSeedEditing=false,validSeed=true;
static FfxHooks::SinRam::Config g_sinDraft{};
static int g_sinConfirmTimer=0,g_sinLastEdge=0,g_sinPreviewField=310,g_sinLastRow=0,g_sinMenuResult=0;
static bool g_sinMenuClosed=false;
static float g_sinEasedRowY=0;
static char g_sinNotice[128]{};
enum {SIN_RAM_ROW_ENABLED,SIN_RAM_ROW_DISTRIBUTION,SIN_RAM_ROW_SEED,SIN_RAM_ROW_SHUFFLE,SIN_RAM_ROW_AREA,SIN_RAM_ROW_GUIDE,SIN_RAM_ROW_SAVE,SIN_RAM_ROW_BACK};
namespace FfxHooks::SinSpread {bool ParseSeed(const char*,std::uint32_t* value){*value=42;return validSeed;}}
static void ArenaMixRenameAbort(){g_arenaRenameConfirm=g_arenaRenameCancel=0;}
static void SinCurse_BuildLabels(){}static void SinRam_ClearSaveFeedback(){}
#include "SinInputAudio.inc"
static void ChildAudioCases(){
    const auto arenaReset=[&](ArenaPlusMenuKind kind){
        Reset(F7_MENU_FORCE);g_arenaPlusMenuKind=kind;g_arenaPlusClosed=g_arenaPlusResult=g_arenaPlusLastConfirmEdge=g_arenaPlusInputCooldown=0;
        g_arenaRenameConfirm=g_arenaRenameCancel=0;actionFeedback=actionCalls=0;hostRowCount=kind==ArenaPlusMenuKind::Hub?6:4;
    };
    for(int raw=0;raw<=15;++raw){
        const auto kind=static_cast<ArenaPlusMenuKind>(raw);arenaReset(kind);
        if(kind==ArenaPlusMenuKind::Rename||kind==ArenaPlusMenuKind::Search){hostRowCount=3;g_arenaRenameConfirm=1;}
        else{hostSelection=kind==ArenaPlusMenuKind::BattleDetail?1:0;hostEdge=0x20;}
        ArenaPlus_InputCb(1);Check(g_arenaPlusClosed&&sounds.empty(),"every Arena child defers confirm until its action outcome");
        actionFeedback=3;ArenaPlus_DispatchMenuConfirm(g_arenaPlusResult);Check(sounds==std::vector<int>{3}&&actionCalls==1,"Arena action rejection never plays optimistic success first");
        arenaReset(kind);hostRowCount=kind==ArenaPlusMenuKind::Hub?6:4;hostSelection=hostRowCount-1;
        if(kind==ArenaPlusMenuKind::Rename||kind==ArenaPlusMenuKind::Search){hostRowCount=3;g_arenaRenameCancel=1;}else hostEdge=0x20;
        ArenaPlus_InputCb(1);ArenaPlus_DispatchMenuConfirm(g_arenaPlusResult);
        Check(sounds==std::vector<int>{4},"every Arena child Back or text Cancel emits one cancel cue");
    }
    arenaReset(ArenaPlusMenuKind::Ultra);hostEdge=0x20;ArenaPlus_InputCb(1);actionFeedback=1;ArenaPlus_DispatchMenuConfirm(0);
    Check(sounds==std::vector<int>{1},"Ultra handler and dispatcher confirmations are coalesced");
    arenaReset(ArenaPlusMenuKind::Monsters);hostDirection=0x4000;hostEdge=0x20;ArenaPlus_InputCb(1);actionFeedback=3;ArenaPlus_DispatchMenuConfirm(1);
    Check(sounds==std::vector<int>{3},"Arena navigation plus refused action has one final cue");
    arenaReset(ArenaPlusMenuKind::BattleDetail);ArenaPlus_DispatchMenuConfirm(0);Check(sounds.empty()&&actionCalls==1,"battle-information row stays silent while the handler restores its menu");
    arenaReset(ArenaPlusMenuKind::Positions);hostRowCount=7;hostSelection=5;hostDirection=0x2000;actionFeedback=3;ArenaPlus_InputCb(1);Check(sounds.empty(),"horizontal input on an Arena action row performs no coordinate adjustment");
    arenaReset(ArenaPlusMenuKind::Library);g_arenaPlusInputCooldown=3;hostEdge=0x20;ArenaPlus_InputCb(1);Check(!g_arenaPlusClosed&&sounds.empty(),"Arena held-input admission remains bounded");
    arenaReset(ArenaPlusMenuKind::Positions);hostEdge=0x40;ArenaPlus_InputCb(1);Check(sounds==std::vector<int>{4},"Arena direct cancel is immediate once");
    for(int outcome=0;outcome<3;++outcome){
        Reset(F7_MENU_FORCE);g_sinSeedEditing=true;validSeed=outcome==1;g_arenaRenameCancel=outcome==2;g_arenaRenameConfirm=outcome!=2;
        SinCurse_InputCb(1);Check(sounds==std::vector<int>{outcome==2?4:outcome==1?1:3},"SIN seed distinguishes valid confirmation, error and cancel");
    }
    Reset(F7_MENU_FORCE);g_sinSeedEditing=false;g_sinConfirmTimer=g_sinLastEdge=0;g_sinMenuClosed=false;
    hostRowCount=8;hostEdge=0x20;SinCurse_InputCb(1);
    Check(g_sinMenuClosed&&sounds.empty(),"SIN confirmation waits for its eventual handler and allocation");
}
