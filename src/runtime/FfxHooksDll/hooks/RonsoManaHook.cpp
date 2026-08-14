#include "RonsoManaHook.h"
#include "../shared/ffx_addresses.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <polyhook2/Detour/x86Detour.hpp>
#endif

namespace FfxHooks {

namespace {

constexpr uint16_t kKimahriRuntimeId      = 3u; /* MemoryChr.Id@0x0E = pccom enum DIRECT: Tidus0 Yuna1 Auron2 Kimahri3 Wakka4 Lulu5 Rikku6. WAS WRONGLY 2 (=Auron) — every force/drain/probe hit Auron, not Kimahri. (RT2 fix 2026-06-16, proven in-game: Auron got 255 OD + Ronso Rage) */
constexpr uint32_t kMemoryChrOvrCharge     = 0x5BCu;
constexpr uint32_t kMemoryChrOvrChargeMax  = 0x5BDu;
constexpr uint32_t kMemoryChrStatIcon      = 0x5C3u;
constexpr uint32_t kMemoryChrId            = 0x00Eu;
constexpr uint32_t kMemoryChrSwitchVsOd    = 0xDF3u;
constexpr uint32_t kMemoryChrRingBuildGate = 0xDF7u; /* 792AB0 one-shot gate → 7ACEC0 */
constexpr uint32_t kMemoryChrOdReadyByte   = 0x590u; /* 79AF70: (byte>>2)&1 — middle-ring OD gate */
constexpr uint32_t kMemoryChrOdStateByte   = 0x591u; /* 7847D0 OD-gauge state machine: ==17 -> charge:=max@7848ab. SUSPECT #1 for OD-usable gate (RE 2026-06-16) */
constexpr uint32_t kMemoryChrCopycatMode   = 0x6DEu; /* sub_78ABE0: if !=0 the usability gate uses FIXED cmd 40 (Copycat) cost instead of the highlighted skill (RE 2026-06-16) */
constexpr uint32_t kMemoryChrMenuOdWord   = 0x6C8u; /* 79B500 word868: 0=OD ok, 12321=blocked */
constexpr uint16_t kMenuOdBlockedWord      = 12321u;
constexpr uint32_t kActorMenuVisibleCount  = 0x6CBu; /* 79BB70 menuCtx+1739 */
constexpr uint32_t kActorMenuVisibleCmds   = 0x71Au;
constexpr uint32_t kActorMenuVisibleCat    = 0x722u;
constexpr uint8_t  kActorMenuVisibleMax    = 7u;
constexpr uint32_t kMemoryChrIconId        = 0x00Cu;
constexpr uint16_t kRonsoCommandIdMin      = 104u;
constexpr uint16_t kRonsoCommandIdMax      = 115u;
constexpr uint16_t kKimahriOverdriveCmdId  = 282u;
constexpr uint16_t kEncodedOverdriveCmd    = static_cast<uint16_t>(0x3000u + kKimahriOverdriveCmdId);
constexpr uint8_t  kDefaultRonsoDrainCost  = 40u;
constexpr uint8_t  kDefaultRonsoPoolMax    = 255u;
/* Menu / OD gate: pool 0–255. The whole OD/Ronso ring (BuildCommandRing kind=12, IsOverdriveReady
 * force, ready-state) stays armed while charge >= this floor = the cheapest castable Ronso skill's
 * CostOverdrive. Per-skill affordability is still enforced downstream by the greyout/HasCmd shim
 * (skills costing > current charge stay grey).
 *   RT2 2026-06-16 (Jarvis-MAGIC), proven by ffx-hooks.log on the CORRECT actor (Kimahri id=3):
 *   the user re-priced Overdrive Jump to CostOverdrive=20 in command.bin (idx=104 cost=20 in the
 *   odSeen log), but this floor was still 40 (stale vanilla "Jump=40" assumption). That created a
 *   DEAD BAND at charge 20–39: Jump is affordable (>=20) yet the ring force is gated on >=40, so the
 *   Overdrive submenu re-locked after draining below 40 — exactly the "uso, drena, depois não
 *   consigo mais usar" report. Lowering to 20 closes the band so per-skill cost is the real gate.
 *   Tunable at runtime via FFXHOOKS_RONSO_GATE_MIN if cheaper skills are re-priced. */
constexpr uint8_t  kDefaultRonsoGateMin    = 20u;
constexpr uint8_t  kOverdriveMenuCategory  = 4u;
constexpr size_t   kDrainPatchLen          = 7u;
constexpr uintptr_t kDrainResumeRva        = 0x0038F1ECu;

/* command.bin Ronso 104–115 suggested costs (row order in submenu builder) */
static const uint8_t kRonsoSkillCosts[] = {
    40, 70, 50, 100, 45, 65, 70, 90, 110, 85, 250, 255,
};

static const uint8_t kDrainExpected[kDrainPatchLen] = {
    0xC6, 0x80, 0xBC, 0x05, 0x00, 0x00, 0x00,
};

using GetOvrChargeFn    = uint8_t(__cdecl*)(uint8_t actorIndex);
using GetOvrChargeMaxFn = uint8_t(__cdecl*)(uint8_t actorIndex);
using GetActorByIndexFn = void*(__cdecl*)(uint8_t actorIndex);
using GateFn            = void(__cdecl*)(int actorIdx, int16_t a2, int16_t a3);
using GreyoutFn         = int(__cdecl*)();
using SubmenuRefreshFn  = int(__cdecl*)();
using BuildMenuFn       = int(__cdecl*)(int partySlot, int menuCtx);
using RefreshMenuFn     = int(__cdecl*)(int a1, int m);
using MenuRouteFn       = int(__cdecl*)(int battleSlot, int actorPtr);
using OpenSubmenuFn     = int(__cdecl*)(int actorIdx);
using SetCommandBitFn   = int(__cdecl*)(int n3, int16_t cmdId, int on);
using HasCommandBitFn   = int(__cdecl*)(uint8_t battleSlot, int16_t cmdId);
using HasCommandSaveFn  = int(__cdecl*)(int partySlot, int cmdId);
using CopyMenuTemplateFn = int(__cdecl*)(int slotRingPtr);
using IsOverdriveReadyFn = int(__cdecl*)(unsigned __int8 battleSlot);
using BuildCommandRingFn = int(__cdecl*)(unsigned __int8 battleSlot, int n255, int ringKind, int a4, float a5, void* dst);
using MenuInputDispatchFn = int(__cdecl*)();
using PushMenuTreeEntryFn = int16_t(__cdecl*)(int16_t a1, int16_t a2, int16_t a3, int16_t a4);
using ResolveMenuTreeNodeFn = int(__cdecl*)(int a1, int a2, int a3, char a4, int a5, void* a6);
using MenuTreeResetFn = void(__cdecl*)();
using FinishMenuTreeFn = void(__cdecl*)(int a1, int a2, int a3);
using GetCommandEntryByIdFn = uint8_t*(__cdecl*)(int16_t cmdIndex, int a2);
using CmdUsabilityGateFn = int(__cdecl*)(int src, int entryIdx);
using MenuConfirmFn     = int(__cdecl*)();
using GetMenuContextFn  = void*(__cdecl*)();

static bool                g_installed      = false;
static bool                g_logOnly        = true;
static bool                g_enableGate     = false;
static bool                g_enableGreyout  = false;
static bool                g_enableDrain    = false;
static RonsoManaLogFn      g_logFn          = nullptr;
static uintptr_t           g_base           = 0;
static GetOvrChargeFn      g_getCharge      = nullptr;
static GetOvrChargeMaxFn   g_getMax         = nullptr;
static GetActorByIndexFn   g_getActor       = nullptr;
static SetCommandBitFn     g_setCommandBit  = nullptr;
static GetCommandEntryByIdFn g_getCommandEntry = nullptr;
static GetMenuContextFn    g_getMenuContext = nullptr;
static int16_t             g_odRingHeaderCmdIndex = -1;
static uint16_t            g_odRingHeaderEncoded  = 0;
static volatile LONG       g_gateLogCount   = 0;
static volatile LONG       g_greyLogCount   = 0;
static volatile LONG       g_menuLogCount   = 0;
static volatile LONG       g_menuRouteLogCount = 0;
static volatile LONG       g_menuPatchLogCount = 0;
static volatile LONG       g_poolMaxLogCount   = 0;
static volatile LONG       g_uiRingNullLogCount = 0;
static volatile LONG       g_hasCmdLogCount = 0;
static volatile LONG       g_odReadyLogCount = 0;
static volatile LONG       g_ringBuildLogCount = 0;
static volatile LONG       g_uiPushLogCount = 0;
static volatile LONG       g_uiResolveLogCount = 0;
static volatile LONG       g_menuDispatchLogCount = 0;
static volatile LONG       g_drainLogCount  = 0;
static volatile LONG       g_cmdGateLogCount = 0;
static volatile LONG       g_confirmLogCount = 0;
static volatile uint8_t    g_drainCost      = kDefaultRonsoDrainCost;
static volatile uint8_t    g_poolMax        = kDefaultRonsoPoolMax;
static volatile uint8_t    g_gateMinCost     = kDefaultRonsoGateMin;

/* PROBE (RT2 2026-06-16, Jarvis-MAGIC): the per-skill ring lock is NOT charge==liveMax
 * (pinning max:=charge already fails). Test whether it is charge==255 (absolute full) by
 * spoofing charge:=255 while Kimahri's command menu is up, saving the real charge so the
 * drain hook still subtracts the real cost from the REAL charge (no free Overdrive on use).
 * g_kimahriSavedCharge holds the real charge while the full-spoof is active. */
static volatile uint8_t    g_kimahriSavedCharge   = 0;
static volatile bool       g_kimahriFullSpoofOn    = false;
static volatile bool       g_probeFullCharge       = false; /* RT2 probe: DISPROVEN (grey != charge), off */
static volatile LONG       g_odSkillSeenLogCount   = 0;

#ifdef FFXHOOKS_HAVE_POLYHOOK
static GateFn              g_gateTrampoline    = nullptr;
static GreyoutFn           g_greyoutTrampoline = nullptr;
static SubmenuRefreshFn    g_submenuRefreshTrampoline = nullptr;
static BuildMenuFn         g_menuBuildTrampoline = nullptr;
static RefreshMenuFn       g_refreshMenuTrampoline = nullptr;
static MenuRouteFn         g_menuRouteTrampoline = nullptr;
static OpenSubmenuFn         g_openSubmenuTrampoline = nullptr;
static HasCommandBitFn     g_hasCommandBitTrampoline = nullptr;
static HasCommandSaveFn  g_hasCommandSaveTrampoline = nullptr;
static CopyMenuTemplateFn g_copyMenuTemplateTrampoline = nullptr;
static IsOverdriveReadyFn g_isOverdriveReadyTrampoline = nullptr;
static BuildCommandRingFn g_buildCommandRingTrampoline = nullptr;
static MenuInputDispatchFn g_menuInputDispatchTrampoline = nullptr;
static PushMenuTreeEntryFn g_pushMenuTreeTrampoline = nullptr;
static ResolveMenuTreeNodeFn g_resolveMenuTreeTrampoline = nullptr;
static MenuTreeResetFn g_menuTreeReset = nullptr;
static FinishMenuTreeFn g_finishMenuTree = nullptr;
static CmdUsabilityGateFn  g_cmdGateTrampoline = nullptr;
static MenuConfirmFn       g_confirmTrampoline = nullptr;
static PLH::x86Detour*     g_gateDetour        = nullptr;
static PLH::x86Detour*     g_greyoutDetour     = nullptr;
static PLH::x86Detour*     g_submenuRefreshDetour = nullptr;
static PLH::x86Detour*     g_menuBuildDetour   = nullptr;
static PLH::x86Detour*     g_refreshMenuDetour = nullptr;
static PLH::x86Detour*     g_menuRouteDetour   = nullptr;
static PLH::x86Detour*     g_openSubmenuDetour = nullptr;
static PLH::x86Detour*     g_hasCommandBitDetour = nullptr;
static PLH::x86Detour*     g_hasCommandSaveDetour = nullptr;
static PLH::x86Detour*     g_copyMenuTemplateDetour = nullptr;
static PLH::x86Detour*     g_isOverdriveReadyDetour = nullptr;
static PLH::x86Detour*     g_buildCommandRingDetour = nullptr;
static PLH::x86Detour*     g_menuInputDispatchDetour = nullptr;
static PLH::x86Detour*     g_pushMenuTreeDetour = nullptr;
static PLH::x86Detour*     g_resolveMenuTreeDetour = nullptr;
static PLH::x86Detour*     g_cmdGateDetour = nullptr;
static PLH::x86Detour*     g_confirmDetour = nullptr;
static uint64_t            g_gateTrampolineVa  = 0;
static uint64_t            g_greyTrampolineVa  = 0;
static uint64_t            g_submenuRefreshTrampolineVa = 0;
static uint64_t            g_menuBuildTrampolineVa = 0;
static uint64_t            g_refreshMenuTrampolineVa = 0;
static uint64_t            g_menuRouteTrampolineVa = 0;
static uint64_t            g_openSubmenuTrampolineVa = 0;
static uint64_t            g_hasCommandBitTrampolineVa = 0;
static uint64_t            g_hasCommandSaveTrampolineVa = 0;
static uint64_t            g_copyMenuTemplateTrampolineVa = 0;
static uint64_t            g_isOverdriveReadyTrampolineVa = 0;
static uint64_t            g_buildCommandRingTrampolineVa = 0;
static uint64_t            g_menuInputDispatchTrampolineVa = 0;
static uint64_t            g_pushMenuTreeTrampolineVa = 0;
static uint64_t            g_resolveMenuTreeTrampolineVa = 0;
static uint64_t            g_cmdGateTrampolineVa = 0;
static uint64_t            g_confirmTrampolineVa = 0;
#endif

static uint8_t             g_savedDrain[kDrainPatchLen] = {};
static uint8_t*            g_drainStub                  = nullptr;
static size_t              g_drainStubLen               = 0;
static uintptr_t           g_drainPatchVa               = 0;
static uintptr_t           g_drainResumeVa              = 0;

static void HookLog(const char* fmt, ...) {
    if (!g_logFn) return;
    char line[512] = {};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    g_logFn(line);
}

static uint8_t ReadActorOvrMax(void* actor) {
    if (!actor) return 0;
    return reinterpret_cast<uint8_t*>(actor)[kMemoryChrOvrChargeMax];
}

static uint8_t ReadOvrChargeMax(uint8_t actorIndex) {
    if (g_getMax) return g_getMax(actorIndex);
    return 0;
}

static bool MemWrite(void* dest, const void* src, size_t len) {
    if (!dest || !src || len == 0) return false;
    DWORD old = 0;
    if (!VirtualProtect(dest, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(dest, src, len);
    VirtualProtect(dest, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dest, len);
    return true;
}

static volatile LONG       g_hudBootstrapMenus = 0;

static void* GetPartyListBase() {
    if (!g_base) return nullptr;
    const uintptr_t listVa = g_base + RVA_FFX_BATTLE_PLAYER_LIST;
    const uintptr_t listBase = *reinterpret_cast<uintptr_t*>(listVa);
    if (!listBase) return nullptr;
    return reinterpret_cast<void*>(listBase);
}

static void* GetPartyListActor(uint8_t slot) {
    void* listBase = GetPartyListBase();
    if (!listBase || slot >= 18u) return nullptr;
    return reinterpret_cast<uint8_t*>(listBase) + static_cast<uintptr_t>(slot) * FFX_BATTLE_CHR_STRIDE;
}

static void* ResolveActorPtr(uint8_t actorIndex) {
    void* party = GetPartyListActor(actorIndex);
    if (party) return party;
    if (g_getActor) return g_getActor(actorIndex);
    return nullptr;
}

/* Runtime MemoryChr.Id@0x0E == 2 for Kimahri (pccom enum 3). Do not match id==3 (false positives). */
static bool IsKimahriActorPtr(void* actor) {
    if (!actor) return false;
    const uint16_t id = *reinterpret_cast<const uint16_t*>(
        reinterpret_cast<const uint8_t*>(actor) + kMemoryChrId);
    return id == kKimahriRuntimeId;
}

#ifdef FFXHOOKS_HAVE_POLYHOOK
static void ForceKimahriMiddleRingBuild(uint8_t battleSlot, const char* reason);
static void EnsureKimahriUiTreeOdRow(uint8_t battleSlot, const char* reason);
#endif

static bool IsKimahriActorIndex(uint8_t actorIndex) {
    return IsKimahriActorPtr(ResolveActorPtr(actorIndex));
}

static uint8_t FindKimahriBattleIndex() {
    for (uint8_t i = 0; i < 18u; ++i) {
        if (IsKimahriActorPtr(GetPartyListActor(i))) return i;
    }
    if (g_getActor) {
        for (uint8_t i = 0; i < 18u; ++i) {
            if (IsKimahriActorPtr(g_getActor(i))) return i;
        }
    }
    return 0xFFu;
}

static int16_t KimahriMenuTreeId(uint8_t battleSlot) {
    return static_cast<int16_t>(battleSlot + 41);
}

static bool IsKimahriMenuTreeId(int16_t treeId) {
    const uint8_t kimIdx = FindKimahriBattleIndex();
    return kimIdx != 0xFFu && treeId == KimahriMenuTreeId(kimIdx);
}

static uint32_t ReadUiDisplayBlobCase4() {
    if (!g_base) return 0;
    return *reinterpret_cast<const uint32_t*>(g_base + RVA_FFX_BATTLE_UI_DISPLAY_BLOB_CASE4);
}

static uintptr_t ReadCase2BlobPtr() {
    if (!g_base) return 0;
    return *reinterpret_cast<const uintptr_t*>(g_base + RVA_FFX_BATTLE_UI_MENU_BLOB_TYPE2_SLOT1);
}

static uintptr_t ReadMainRingBlobPtr() {
    if (!g_base) return 0;
    /* a2=0 MainRing blob ptr lives at the array base (slot 0); a2=1 player tree is _SLOT1. */
    return *reinterpret_cast<const uintptr_t*>(g_base + RVA_FFX_BATTLE_UI_MENU_BLOB_TYPE2_BASE);
}

/* Decodes one packed node descriptor of the WalkMenuBlobIndex(797420) blob format:
 *   v6 = (count+1)/2 + 2*subIdx;  nodeOff = *(i16)(blob + 2*v6 + 4);
 *   entryCount = *(u16)(blob + nodeOff);  entry[k] = *(u16)(blob + nodeOff + 2 + 2*k)
 * All offsets are bounds-clamped to keep the read inside a sane blob span. */
struct OdNode { bool valid; uint16_t entryCount; int nodeOff; };

/* SEH-guarded: a garbage offset-table slot (or a blob smaller than our 0x2000 ceiling) could make
 * these reads land outside the allocation. __try/__except turns any access violation into a clean
 * "invalid node" instead of crashing the game (RT2 hudSafe=25 partial build crashed exactly here-
 * adjacent — see §14). All locals are POD so SEH is legal in these functions. */
static OdNode OdBlobNode(const uint8_t* blob, uint8_t count, uint8_t subIdx) {
    OdNode r{false, 0, 0};
    if (!blob || subIdx == 0xFFu) return r;
    const int offByte = 2 * (((count + 1) / 2) + 2 * static_cast<int>(subIdx)) + 4;
    if (offByte < 4 || offByte > 0x2000) return r;
    __try {
        const int nodeOff = *reinterpret_cast<const int16_t*>(blob + offByte);
        if (nodeOff >= 2 && nodeOff <= 0x2000) {
            const uint16_t ec = *reinterpret_cast<const uint16_t*>(blob + nodeOff);
            if (ec != 0u && ec <= 64u) {
                r.valid = true; r.entryCount = ec; r.nodeOff = nodeOff;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        r.valid = false; r.entryCount = 0; r.nodeOff = 0;
    }
    return r;
}

static uint16_t OdBlobEntry(const uint8_t* blob, const OdNode& n, uint16_t k) {
    if (!n.valid || k >= n.entryCount) return 0xFFFFu;
    __try {
        return *reinterpret_cast<const uint16_t*>(blob + n.nodeOff + 2 + 2 * k);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0xFFFFu;
    }
}

/* A node can back the OD selector (ringKind v4 used as a secondary index into entries) only if
 * entryCount > v4 and entry[v4] != 0xFFFF. The normal ring pushes ringKind=1, the OD ring 12. */
static bool OdNodeSupportsSelector(const uint8_t* blob, const OdNode& n, uint16_t selector) {
    return n.valid && selector < n.entryCount && OdBlobEntry(blob, n, selector) != 0xFFFFu;
}

static bool OdNodeContainsEncodedOd(const uint8_t* blob, const OdNode& n) {
    if (!n.valid) return false;
    for (uint16_t k = 0; k < n.entryCount && k < 64u; ++k) {
        if (OdBlobEntry(blob, n, k) == kEncodedOverdriveCmd) return true;
    }
    return false;
}

static void LogCase2BlobDiag(uint8_t treeId, const char* ctx) {
    if (!g_logFn || !g_base) return;
    const uintptr_t blobPtr = ReadCase2BlobPtr();
    if (!blobPtr) {
        HookLog("[ffx-hooks] RonsoMana DIAG %s blob2=NULL (not initialized)", ctx);
        return;
    }
    const uint8_t* blob = reinterpret_cast<const uint8_t*>(blobPtr);
    const uint8_t maxEntries = blob[1];
    const uint8_t entryVal = (treeId + 2u < 256u) ? blob[treeId + 2] : 0xFFu;
    const uint8_t e41 = (43u < 256u) ? blob[43] : 0xFFu;
    const uint8_t e42 = (44u < 256u) ? blob[44] : 0xFFu;
    const uint8_t e43 = (45u < 256u) ? blob[45] : 0xFFu;
    HookLog(
        "[ffx-hooks] RonsoMana DIAG %s blob2=0x%08X hdr=[%02X %02X] maxE=%u treeId=%u entry=0x%02X slots41/42/43=[%02X %02X %02X] %s",
        ctx,
        static_cast<unsigned>(blobPtr),
        static_cast<unsigned>(blob[0]),
        static_cast<unsigned>(blob[1]),
        static_cast<unsigned>(maxEntries),
        static_cast<unsigned>(treeId),
        static_cast<unsigned>(entryVal),
        static_cast<unsigned>(e41),
        static_cast<unsigned>(e42),
        static_cast<unsigned>(e43),
        (treeId >= maxEntries) ? "FAIL:idx>=max" : (entryVal == 0xFF) ? "FAIL:0xFF" : "OK");
}

/* hudSafe=26 CRASH HOTFIX: the hudSafe=25 partial build crashed because the old (3) "richest node"
 * fallback WROTE a guessed subIdx (RT2 log: `BLOB-PATCH2 ... via=rich>=1 subIdx=0x01 ec=24`). That made
 * Resolve(2,1,43) "succeed" on a node that is NOT the OD ring, so finishMenuTree built/displayed bogus
 * content and the game faulted. (The old 0x00 write merely returned -1, hence no crash before.)
 *
 * New policy — SAFE by default, opt-in to write:
 *   - We only WRITE blob[2+treeId] when env FFXHOOKS_RONSO_OD_BLOBWRITE=1 is set (default = NO write,
 *     so the build is crash-safe and purely diagnostic via the ODBLOB dump).
 *   - Even when opted-in, we only write a *principled* node: (1) a node whose entries contain the
 *     encoded OD command 0x311A, or (2) a sibling party-OD treeId (41..47) already registered. We
 *     NEVER auto-write the "richest node" guess — that is the one that crashed; if we ever want it,
 *     hardcode it after reading the ODBLOB dump.
 *   - Everything found (safe + risky candidate + count) is logged regardless, for diagnosis. */
static bool RonsoOdBlobWriteEnabled() {
    char buf[8] = {0};
    const DWORD n = GetEnvironmentVariableA("FFXHOOKS_RONSO_OD_BLOBWRITE", buf, sizeof(buf));
    return n == 1u && buf[0] == '1';
}

static bool PatchCase2BlobForKimahri(uint8_t battleSlot) {
    const uintptr_t blobPtr = ReadCase2BlobPtr();
    if (!blobPtr) return false;
    uint8_t* blob = reinterpret_cast<uint8_t*>(blobPtr);
    const uint8_t count = blob[1];
    const uint8_t treeId = battleSlot + 41;
    if (treeId >= count) return false;            /* writing blob[treeId+2] would clobber node data */
    if (blob[treeId + 2] != 0xFFu) return true;   /* already registered */

    uint8_t safeIdx = 0xFFu; const char* safeHow = "none";
    uint8_t riskyIdx = 0xFFu; const char* riskyHow = "none"; uint16_t riskyEc = 0;

    /* SAFE (1): a node that literally carries the encoded OD command 0x311A. */
    for (uint8_t c = 0; c < 60u && safeIdx == 0xFFu; ++c) {
        const OdNode nd = OdBlobNode(blob, count, c);
        if (nd.valid && OdNodeContainsEncodedOd(blob, nd) && OdNodeSupportsSelector(blob, nd, 1u)) {
            safeIdx = c; safeHow = "od311A";
        }
    }
    /* SAFE (2): a sibling party-OD treeId (41..47) the game already registered. */
    for (uint8_t t = 41u; t <= 47u && safeIdx == 0xFFu; ++t) {
        if (t == treeId || t >= count) continue;
        const uint8_t sib = blob[t + 2];
        if (sib == 0xFFu) continue;
        const OdNode nd = OdBlobNode(blob, count, sib);
        if (OdNodeSupportsSelector(blob, nd, 1u)) { safeIdx = sib; safeHow = "sibling"; }
    }
    /* RISKY (diagnostic only, never auto-written): richest selector-capable node. */
    {
        bool best12 = false;
        for (uint8_t c = 0; c < 60u; ++c) {
            const OdNode nd = OdBlobNode(blob, count, c);
            if (!OdNodeSupportsSelector(blob, nd, 1u)) continue;
            const bool sup12 = OdNodeSupportsSelector(blob, nd, 12u);
            if ((sup12 && !best12) || (sup12 == best12 && nd.entryCount > riskyEc)) {
                riskyEc = nd.entryCount; best12 = sup12; riskyIdx = c; riskyHow = sup12 ? "rich>=12" : "rich>=1";
            }
        }
    }

    const bool writeEnabled = RonsoOdBlobWriteEnabled();
    static volatile LONG patchLogCount = 0;
    const LONG ln = InterlockedIncrement(&patchLogCount);
    if (g_logFn && ln <= 8) {
        HookLog(
            "[ffx-hooks] RonsoMana BLOB-PATCH2 #%ld treeId=%u count=%u write=%d safe=0x%02X(%s) risky=0x%02X(%s,ec=%u)",
            static_cast<long>(ln), static_cast<unsigned>(treeId), static_cast<unsigned>(count),
            writeEnabled ? 1 : 0,
            static_cast<unsigned>(safeIdx), safeHow,
            static_cast<unsigned>(riskyIdx), riskyHow, static_cast<unsigned>(riskyEc));
    }

    if (!writeEnabled || safeIdx == 0xFFu) {
        /* Default path: leave the slot 0xFF (vanilla-safe — OD hidden, but NO crash). The ODBLOB dump
         * tells us the exact OD node; flip FFXHOOKS_RONSO_OD_BLOBWRITE=1 once a safe node is confirmed. */
        return false;
    }

    DWORD old = 0;
    if (!VirtualProtect(blob + treeId + 2, 1, PAGE_READWRITE, &old)) return false;
    blob[treeId + 2] = safeIdx;
    VirtualProtect(blob + treeId + 2, 1, old, &old);
    if (g_logFn && ln <= 8) {
        const OdNode nn = OdBlobNode(blob, count, safeIdx);
        HookLog("[ffx-hooks] RonsoMana BLOB-PATCH2 WROTE treeId=%u subIdx=0x%02X via=%s nodeOff=0x%X ec=%u",
                static_cast<unsigned>(treeId), static_cast<unsigned>(safeIdx), safeHow,
                static_cast<unsigned>(nn.nodeOff), static_cast<unsigned>(nn.entryCount));
    }
    return true;
}

/* One-shot deep dump of both menu blobs so the exact OD node can be identified from a single RT2.
 * Read-only; runs regardless of g_logOnly. Emits, for the a2=1 (player) blob: the party-OD index
 * row (treeId 41..47) and, for node sub-indices 0..23, the entryCount + first entries (flagging the
 * node that carries 0x311A). Then the a2=0 (MainRing) index row for the same treeIds plus 109..115
 * (the working main/aeon-OD trees) so a fix-C redirect target is visible if a2=1 has no OD node. */
static void DumpOdBlobStructureOnce() {
    static volatile LONG done = 0;
    if (!g_logFn) return;
    if (InterlockedCompareExchange(&done, 1, 0) != 0) return;

    const uintptr_t blobPtr = ReadCase2BlobPtr();
    if (!blobPtr) { HookLog("[ffx-hooks] RonsoMana ODBLOB a2=1 blob=NULL"); return; }
    const uint8_t* blob = reinterpret_cast<const uint8_t*>(blobPtr);
    const uint8_t count = blob[1];
    HookLog("[ffx-hooks] RonsoMana ODBLOB a2=1 blob=0x%08X hdr0=0x%02X count=%u",
            static_cast<unsigned>(blobPtr), static_cast<unsigned>(blob[0]), static_cast<unsigned>(count));

    {
        char row[160]; int p = 0; row[0] = 0;
        for (uint8_t t = 41u; t <= 47u; ++t) {
            const uint8_t idx = (t < count) ? blob[t + 2] : 0xFFu;
            p += _snprintf_s(row + p, sizeof(row) - p, _TRUNCATE, "t%u=0x%02X ", t, static_cast<unsigned>(idx));
        }
        HookLog("[ffx-hooks] RonsoMana ODBLOB a2=1 partyOD %s", row);
    }
    for (uint8_t c = 0; c < 24u; ++c) {
        const OdNode nd = OdBlobNode(blob, count, c);
        if (!nd.valid) continue;
        char ent[200]; int q = 0; ent[0] = 0;
        bool hasOd = false;
        for (uint16_t k = 0; k < nd.entryCount && k < 12u && q < 180; ++k) {
            const uint16_t v = OdBlobEntry(blob, nd, k);
            if (v == kEncodedOverdriveCmd) hasOd = true;
            q += _snprintf_s(ent + q, sizeof(ent) - q, _TRUNCATE, "%04X ", static_cast<unsigned>(v));
        }
        HookLog("[ffx-hooks] RonsoMana ODBLOB a2=1 node[%u] off=0x%X ec=%u%s: %s",
                static_cast<unsigned>(c), static_cast<unsigned>(nd.nodeOff),
                static_cast<unsigned>(nd.entryCount), hasOd ? " <<OD311A>>" : "", ent);
    }

    const uintptr_t mainPtr = ReadMainRingBlobPtr();
    if (mainPtr > 0x10000u) {                       /* reject obvious garbage; SEH catches the rest */
        const uint8_t* mb = reinterpret_cast<const uint8_t*>(mainPtr);
        char row[256]; int p = 0; row[0] = 0; unsigned mcOut = 0; bool ok = true;
        __try {
            const uint8_t mc = mb[1];
            mcOut = mc;
            for (uint8_t t = 41u; t <= 47u && p < 230; ++t) {
                const uint8_t idx = (t < mc) ? mb[t + 2] : 0xFFu;
                p += _snprintf_s(row + p, sizeof(row) - p, _TRUNCATE, "t%u=0x%02X ", t, static_cast<unsigned>(idx));
            }
            for (uint8_t t = 109u; t <= 115u && p < 230; ++t) {
                const uint8_t idx = (t < mc) ? mb[t + 2] : 0xFFu;
                p += _snprintf_s(row + p, sizeof(row) - p, _TRUNCATE, "t%u=0x%02X ", t, static_cast<unsigned>(idx));
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ok = false;
        }
        HookLog("[ffx-hooks] RonsoMana ODBLOB a2=0 blob=0x%08X count=%u %s%s",
                static_cast<unsigned>(mainPtr), mcOut, ok ? "" : "(read-fault) ", row);
    } else {
        HookLog("[ffx-hooks] RonsoMana ODBLOB a2=0 blob=NULL");
    }
}

static int ReadMenuTreeStackDepth() {
    if (!g_base) return -1;
    return *reinterpret_cast<const int*>(g_base + RVA_FFX_BATTLE_UI_MENU_STACK);
}

static uint8_t FindBattleIndexForActorPtr(void* actor) {
    if (!actor) return 0xFFu;
    for (uint8_t i = 0; i < 18u; ++i) {
        if (GetPartyListActor(i) == actor) return i;
    }
    if (g_getActor) {
        for (uint8_t i = 0; i < 18u; ++i) {
            if (g_getActor(i) == actor) return i;
        }
    }
    return 0xFFu;
}

static void LogKimahriProbeOnce() {
    static volatile LONG probed = 0;
    if (!g_logFn || InterlockedCompareExchange(&probed, 1, 0) != 0) return;
    for (uint8_t i = 0; i < 18u; ++i) {
        void* actor = GetPartyListActor(i);
        if (!actor) continue;
        const auto* bytes = reinterpret_cast<const uint8_t*>(actor);
        HookLog(
            "[ffx-hooks] RonsoMana probe idx=%u icon=0x%02X id=0x%04X stat=0x%02X charge=%u max=%u kimahri=%d",
            static_cast<unsigned>(i),
            static_cast<unsigned>(bytes[kMemoryChrIconId]),
            static_cast<unsigned>(*reinterpret_cast<const uint16_t*>(bytes + kMemoryChrId)),
            static_cast<unsigned>(bytes[kMemoryChrStatIcon]),
            static_cast<unsigned>(g_getCharge ? g_getCharge(i) : 0),
            static_cast<unsigned>(ReadActorOvrMax(actor)),
            IsKimahriActorPtr(actor) ? 1 : 0);
    }
}

static void ResolveGameFns(uintptr_t base) {
    g_base = base;
    g_getCharge = reinterpret_cast<GetOvrChargeFn>(base + RVA_FFX_BATTLE_GET_OVR_CHARGE);
    g_getMax = reinterpret_cast<GetOvrChargeMaxFn>(base + RVA_FFX_BATTLE_GET_OVR_CHARGE_MAX);
    g_getActor = reinterpret_cast<GetActorByIndexFn>(base + RVA_FFX_BATTLE_GET_ACTOR_BY_INDEX);
    g_setCommandBit = reinterpret_cast<SetCommandBitFn>(base + RVA_FFX_BATTLE_SET_ACTOR_COMMAND_BIT);
    g_menuTreeReset = reinterpret_cast<MenuTreeResetFn>(base + RVA_FFX_BATTLE_UI_MENU_STACK_RESET);
    g_finishMenuTree = reinterpret_cast<FinishMenuTreeFn>(base + RVA_FFX_BATTLE_UI_FINISH_MENU_TREE);
    g_getCommandEntry = reinterpret_cast<GetCommandEntryByIdFn>(base + RVA_FFX_KERNEL_GET_COMMAND_ENTRY_BY_ID);
    g_getMenuContext = reinterpret_cast<GetMenuContextFn>(base + RVA_FFX_BTL_GET_MENU_CONTEXT);
}

static void ScanForOdRingHeader() {
    if (g_odRingHeaderCmdIndex >= 0) return;
    if (!g_getCommandEntry) return;
    for (int16_t i = 0; i < 50; ++i) {
        uint8_t* entry = g_getCommandEntry(i, 0);
        if (!entry) continue;
        if ((entry[22] & 0xF8) == 8 && entry[23] == 10) {
            g_odRingHeaderCmdIndex = i;
            g_odRingHeaderEncoded = static_cast<uint16_t>(i + 0x3000);
            if (g_logFn) {
                HookLog("[ffx-hooks] RonsoMana OD-HEADER found cmdIndex=%d encoded=0x%04X",
                    static_cast<int>(i), static_cast<unsigned>(g_odRingHeaderEncoded));
            }
            return;
        }
    }
    if (g_logFn) {
        HookLog("[ffx-hooks] RonsoMana WARN: OD ring header NOT FOUND in commands 0-49");
    }
}

struct KimahriPoolSpoof {
    uint8_t* actor;
    uint8_t  savedMax;
    bool     active;
};

/* hudSafe=24: the OLD transient save/restore spoof is RETIRED. FFX re-checks "Overdrive
 * usable == gauge full (charge==max)" PER-FRAME in the HUD/menu renderer, OUTSIDE our hooks,
 * so restoring max=255 right after each trampoline (what End used to do) is EXACTLY what kept
 * the Overdrive ring hidden / LEFT blocked at partial charge. max is now pinned PERSISTENTLY
 * to charge by ApplyKimahriRuntimePoolMax, so Begin/End are kept as no-ops only so the existing
 * call sites keep compiling and simply lean on the persistent pin. */
static KimahriPoolSpoof BeginKimahriMaxSpoof(void* /*actor*/) {
    return KimahriPoolSpoof{ nullptr, 0, false };
}

static void EndKimahriMaxSpoof(KimahriPoolSpoof* /*s*/) {
    /* no-op: max stays pinned to charge so the per-frame OD-full check keeps passing */
}

static void EnsureKimahriOverdriveCommandBit(int partySlot, void* actor) {
    if (g_logOnly || !g_setCommandBit || !IsKimahriActorPtr(actor)) return;
    const auto* bytes = reinterpret_cast<const uint8_t*>(actor);
    if (bytes[kMemoryChrOvrCharge] < g_gateMinCost) return;
    g_setCommandBit(partySlot, static_cast<int16_t>(kKimahriOverdriveCmdId), 1);
    if (g_odRingHeaderCmdIndex < 0) ScanForOdRingHeader();
    if (g_odRingHeaderCmdIndex >= 0) {
        g_setCommandBit(partySlot, g_odRingHeaderCmdIndex, 1);
    }
}

static void PrepareKimahriMenuRoute(void* actor) {
    if (g_logOnly || !actor || !IsKimahriActorPtr(actor)) return;
    auto* bytes = reinterpret_cast<uint8_t*>(actor);
    if (bytes[kMemoryChrOvrCharge] >= g_gateMinCost) {
        /* 792170 reads +0xDF3; 0 => 792AB0 takes 89BA80 open-submenu path */
        bytes[kMemoryChrSwitchVsOd] = 0;
    }
}

static void ApplyKimahriOverdriveReadyState(void* actor) {
    if (g_logOnly || !actor || !IsKimahriActorPtr(actor)) return;
    auto* bytes = reinterpret_cast<uint8_t*>(actor);
    const uint8_t charge = bytes[kMemoryChrOvrCharge];
    if (charge < g_gateMinCost || charge == 0) return;

    if ((bytes[kMemoryChrOdReadyByte] & 0x0Cu) != 0x0Cu) {
        bytes[kMemoryChrOdReadyByte] |= 0x0Cu; /* bits 2+3: 79AF70 + 79AEE0 (7854D0 case 5) */
        const LONG n = InterlockedIncrement(&g_odReadyLogCount);
        if (g_logFn && n <= 16) {
            HookLog(
                "[ffx-hooks] RonsoMana G0+ odReady #%ld charge=%u byte@0x590=0x%02X",
                static_cast<long>(n),
                static_cast<unsigned>(charge),
                static_cast<unsigned>(bytes[kMemoryChrOdReadyByte]));
        }
    }

    auto* menuWord = reinterpret_cast<uint16_t*>(bytes + kMemoryChrMenuOdWord);
    if (*menuWord == kMenuOdBlockedWord) {
        *menuWord = 0;
    }
}

static void ApplyKimahriRuntimePoolMax(void* actor) {
    if (g_logOnly || !actor || !IsKimahriActorPtr(actor)) return;
    auto* bytes = reinterpret_cast<uint8_t*>(actor);
    const uint8_t charge = bytes[kMemoryChrOvrCharge];
    /* hudSafe=24 (root fix for "can't even go LEFT unless the bar is maxed"): FFX gates
     * "Overdrive usable" on a FULL gauge (charge==max) and re-evaluates it PER-FRAME in the
     * HUD/menu renderer, OUTSIDE our hooks. Pinning max=255 for the 0-255 pool therefore left
     * partial charge permanently "not full", so Overdrive never appeared in the ring and LEFT
     * stayed blocked. Persistently pin max:=charge whenever Kimahri has usable charge so EVERY
     * per-frame full-check sees a full gauge while his command menu is up (the ATB/CTB is paused
     * during command input, so no overdrive gain is lost). Below the usable threshold, hand the
     * real pool (255) back so the gauge can refill toward the 0-255 pool again. */
    if (g_probeFullCharge) {
        /* RT2 probe: test if the per-skill lock is charge==255 (absolute full). Spoof the live
         * charge to 255 while the menu is up; save the real charge ONCE so the drain hook can
         * subtract the real cost from the real charge (no free Overdrive when a skill is used). */
        if (charge >= g_gateMinCost && charge != 0xFFu) {
            if (!g_kimahriFullSpoofOn) {
                g_kimahriSavedCharge = charge;
                g_kimahriFullSpoofOn = true;
                const LONG n = InterlockedIncrement(&g_poolMaxLogCount);
                if (g_logFn && n <= 16) {
                    HookLog(
                        "[ffx-hooks] RonsoMana G0 fullProbe #%ld realCharge=%u charge:=255 (test gate==255)",
                        static_cast<long>(n),
                        static_cast<unsigned>(charge));
                }
            }
            bytes[kMemoryChrOvrCharge]    = 0xFFu;
            bytes[kMemoryChrOvrChargeMax] = 0xFFu;
        } else if (charge == 0xFFu && g_kimahriFullSpoofOn) {
            bytes[kMemoryChrOvrChargeMax] = 0xFFu;
        }
        return;
    }

    if (charge >= g_gateMinCost && charge != 0) {
        if (bytes[kMemoryChrOvrChargeMax] != charge) {
            bytes[kMemoryChrOvrChargeMax] = charge;
            const LONG n = InterlockedIncrement(&g_poolMaxLogCount);
            if (g_logFn && n <= 16) {
                HookLog(
                    "[ffx-hooks] RonsoMana G0 poolPin #%ld charge=%u max:=charge (gauge-full)",
                    static_cast<long>(n),
                    static_cast<unsigned>(charge));
            }
        }
    } else if (bytes[kMemoryChrOvrChargeMax] != g_poolMax) {
        bytes[kMemoryChrOvrChargeMax] = g_poolMax;
    }
}

static bool IsEmptyCommandRingSlot(uint16_t val) {
    return val == 0xFFFFu || val == 0x00FFu || val == 0x0100u || val == 0;
}

static uintptr_t BattleCommandRingUiBase() {
    if (!g_base) return 0;
    /* The command ring buffer is heap/runtime-allocated; its absolute base pointer is
     * stored in a BSS cell that 7AEFC0/79BB70 load via `mov edi,[cell]` before indexing
     * +20592 / +1144*slot. We must DEREFERENCE that cell — earlier builds returned the
     * raw cell address (no deref, off by 0x1000), so every ring write missed the live
     * buffer and Overdrive never appeared. Returns 0 until the ring is allocated. */
    const uintptr_t cell = g_base + RVA_FFX_BATTLE_COMMAND_RING_BASE_PTR;
    return *reinterpret_cast<volatile uintptr_t*>(cell);
}

static void PatchKimahriActorVisibleMenu(void* menuCtx) {
    if (g_logOnly || !menuCtx) return;
    auto* bytes = reinterpret_cast<uint8_t*>(menuCtx);

    auto* cmds = reinterpret_cast<uint16_t*>(bytes + kActorMenuVisibleCmds);
    auto* cats = bytes + kActorMenuVisibleCat;
    uint8_t count = bytes[kActorMenuVisibleCount];

    for (uint8_t i = 0; i < count && i < kActorMenuVisibleMax; ++i) {
        if (cmds[i] == kEncodedOverdriveCmd
            || cmds[i] == static_cast<uint16_t>(kKimahriOverdriveCmdId)) {
            return;
        }
    }
    if (count >= kActorMenuVisibleMax) return;

    cmds[count] = kEncodedOverdriveCmd;
    cats[count] = 4u;
    bytes[kActorMenuVisibleCount] = static_cast<uint8_t>(count + 1u);

    const LONG n = InterlockedIncrement(&g_menuPatchLogCount);
    if (g_logFn && n <= 32) {
        HookLog(
            "[ffx-hooks] RonsoMana G0'' actorMenu #%ld count=%u enc=0x%04X ctx=0x%08X",
            static_cast<long>(n),
            static_cast<unsigned>(bytes[kActorMenuVisibleCount]),
            static_cast<unsigned>(kEncodedOverdriveCmd),
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(menuCtx)));
    }
}

static int PlaceOverdriveInRow(uint16_t* row, uint32_t slotCount) {
    for (uint32_t i = 0; i < slotCount; ++i) {
        if (row[i] == kEncodedOverdriveCmd) return static_cast<int>(i);
    }
    for (uint32_t i = 0; i < slotCount; ++i) {
        if (IsEmptyCommandRingSlot(row[i])) {
            row[i] = kEncodedOverdriveCmd;
            return static_cast<int>(i);
        }
    }
    row[0] = kEncodedOverdriveCmd;
    return 0;
}

static void PatchKimahriOverdriveRowAt(uintptr_t rowPtr, const char* tag, int tagSlot) {
    if (!rowPtr) return;
    auto* row = reinterpret_cast<uint16_t*>(rowPtr);
    const int placedAt = PlaceOverdriveInRow(row, FFX_BATTLE_MENU_OVERDRIVE_SLOT_COUNT);
    const LONG n = InterlockedIncrement(&g_menuPatchLogCount);
    if (g_logFn && n <= 32) {
        const uint16_t readback = row[placedAt >= 0 ? static_cast<uint32_t>(placedAt) : 0u];
        HookLog(
            "[ffx-hooks] RonsoMana G0'' odRow #%ld %s slot=%d enc=0x%04X at=%d row=0x%08X rb=0x%04X",
            static_cast<long>(n),
            tag,
            tagSlot,
            static_cast<unsigned>(kEncodedOverdriveCmd),
            placedAt,
            static_cast<unsigned>(rowPtr),
            static_cast<unsigned>(readback));
    }
}

static void PatchKimahriCommandRingTemplate() {
    if (g_logOnly) return;
    const uintptr_t ringBase = BattleCommandRingUiBase();
    if (!ringBase) return;

    if (g_odRingHeaderCmdIndex < 0) ScanForOdRingHeader();
    const uint16_t headerVal = (g_odRingHeaderEncoded != 0) ? g_odRingHeaderEncoded : kEncodedOverdriveCmd;

    PatchKimahriOverdriveRowAt(
        ringBase + FFX_BATTLE_COMMAND_RING_TEMPLATE_OFF + FFX_BATTLE_MENU_OVERDRIVE_ROW_OFF,
        "tpl",
        -1);

    auto* tplRing = reinterpret_cast<uint16_t*>(ringBase + FFX_BATTLE_COMMAND_RING_TEMPLATE_OFF);
    for (uint32_t i = 0; i < FFX_BATTLE_COMMAND_RING_DEFAULT_COUNT; ++i) {
        if (tplRing[i] == headerVal) return;
        if (IsEmptyCommandRingSlot(tplRing[i])) {
            tplRing[i] = headerVal;
            return;
        }
    }
}

static void PatchKimahriMainMenuOverdriveRow(uint8_t battleSlot, void* actor) {
    if (g_logOnly || !actor || !IsKimahriActorPtr(actor)) return;
    const uint8_t charge = reinterpret_cast<uint8_t*>(actor)[kMemoryChrOvrCharge];
    if (charge < g_gateMinCost || charge == 0) return;

    const uintptr_t ringBase = BattleCommandRingUiBase();
    if (!ringBase) return;

    PatchKimahriCommandRingTemplate();
    PatchKimahriOverdriveRowAt(
        ringBase
            + static_cast<uintptr_t>(battleSlot) * FFX_BATTLE_MENU_LAYOUT_STRIDE
            + FFX_BATTLE_MENU_OVERDRIVE_ROW_OFF,
        "slot",
        static_cast<int>(battleSlot));
}

static void PatchKimahriCommandRingUi(uint8_t battleSlot, void* actor) {
    if (g_logOnly || !actor || !IsKimahriActorPtr(actor)) return;
    const uint8_t charge = reinterpret_cast<uint8_t*>(actor)[kMemoryChrOvrCharge];
    if (charge < g_gateMinCost || charge == 0) return;

    const uintptr_t uiBase = BattleCommandRingUiBase();
    if (!uiBase) return;

    PatchKimahriMainMenuOverdriveRow(battleSlot, actor);

    if (g_odRingHeaderCmdIndex < 0) ScanForOdRingHeader();

    /* Write OD ring HEADER to the ring headers array (offset 0, 20 entries) */
    const uint16_t headerVal = (g_odRingHeaderEncoded != 0) ? g_odRingHeaderEncoded : kEncodedOverdriveCmd;
    auto* slotRing = reinterpret_cast<uint16_t*>(
        uiBase + static_cast<uintptr_t>(FFX_BATTLE_COMMAND_RING_SLOT_STRIDE) * battleSlot);
    int slotPlaced = -1;
    for (uint32_t i = 0; i < FFX_BATTLE_COMMAND_RING_DEFAULT_COUNT; ++i) {
        if (slotRing[i] == headerVal) {
            slotPlaced = static_cast<int>(i);
            break;
        }
        if (slotPlaced < 0 && IsEmptyCommandRingSlot(slotRing[i])) {
            slotRing[i] = headerVal;
            slotPlaced = static_cast<int>(i);
            break;
        }
    }

    const LONG n = InterlockedIncrement(&g_menuPatchLogCount);
    if (g_logFn && n <= 32) {
        HookLog(
            "[ffx-hooks] RonsoMana G0* uiRing #%ld slot=%u charge=%u bss=0x%08X hdrVal=0x%04X slot@%d",
            static_cast<long>(n),
            static_cast<unsigned>(battleSlot),
            static_cast<unsigned>(charge),
            static_cast<unsigned>(uiBase),
            static_cast<unsigned>(headerVal),
            slotPlaced);
    }
}

/* DIAG (hudSafe>=22): read back the REAL per-slot ring arrays after all writes, so RT2
 * logs show whether the encoded OD cmd actually landed now that the base is dereferenced.
 * +0 = main ring headers (20), +296 = OD category / Ronso Rage list (24). */
static void DumpKimahriRingState(uint8_t battleSlot, const char* tag) {
    if (!g_logFn) return;
    const LONG n = InterlockedIncrement(&g_menuPatchLogCount);
    if (n > 48) return;
    const uintptr_t base = BattleCommandRingUiBase();
    if (!base) {
        HookLog("[ffx-hooks] RonsoMana DIAG %s slot=%u ringBase=NULL", tag, static_cast<unsigned>(battleSlot));
        return;
    }
    const uintptr_t slot = base + static_cast<uintptr_t>(FFX_BATTLE_COMMAND_RING_SLOT_STRIDE) * battleSlot;
    const auto* hdr = reinterpret_cast<const uint16_t*>(slot);
    const auto* od  = reinterpret_cast<const uint16_t*>(slot + FFX_BATTLE_MENU_OVERDRIVE_ROW_OFF);
    HookLog(
        "[ffx-hooks] RonsoMana DIAG %s slot=%u base=0x%08X hdr=%04X %04X %04X %04X %04X %04X %04X %04X %04X %04X (enc=0x%04X)",
        tag, static_cast<unsigned>(battleSlot), static_cast<unsigned>(base),
        static_cast<unsigned>(hdr[0]), static_cast<unsigned>(hdr[1]), static_cast<unsigned>(hdr[2]),
        static_cast<unsigned>(hdr[3]), static_cast<unsigned>(hdr[4]), static_cast<unsigned>(hdr[5]),
        static_cast<unsigned>(hdr[6]), static_cast<unsigned>(hdr[7]), static_cast<unsigned>(hdr[8]),
        static_cast<unsigned>(hdr[9]), static_cast<unsigned>(kEncodedOverdriveCmd));
    HookLog(
        "[ffx-hooks] RonsoMana DIAG %s slot=%u od=%04X %04X %04X %04X %04X %04X %04X %04X",
        tag, static_cast<unsigned>(battleSlot),
        static_cast<unsigned>(od[0]), static_cast<unsigned>(od[1]), static_cast<unsigned>(od[2]),
        static_cast<unsigned>(od[3]), static_cast<unsigned>(od[4]), static_cast<unsigned>(od[5]),
        static_cast<unsigned>(od[6]), static_cast<unsigned>(od[7]));
}

static void PrepareKimahriMenuPatches(int partySlot, void* battleActor, void* menuCtx) {
    if (!battleActor || !IsKimahriActorPtr(battleActor)) return;
    ApplyKimahriRuntimePoolMax(battleActor);
    EnsureKimahriOverdriveCommandBit(partySlot, battleActor);
    PrepareKimahriMenuRoute(battleActor);
    ApplyKimahriOverdriveReadyState(battleActor);
    if (menuCtx) PatchKimahriActorVisibleMenu(menuCtx);
    PatchKimahriCommandRingTemplate();
}

static void FinalizeKimahriMenuPatches(int partySlot, void* battleActor, void* menuCtx) {
    if (!battleActor || !IsKimahriActorPtr(battleActor)) return;
    ApplyKimahriRuntimePoolMax(battleActor);
    EnsureKimahriOverdriveCommandBit(partySlot, battleActor);
    PrepareKimahriMenuRoute(battleActor);
    ApplyKimahriOverdriveReadyState(battleActor);
    if (menuCtx) PatchKimahriActorVisibleMenu(menuCtx);
    PatchKimahriCommandRingUi(static_cast<uint8_t>(partySlot), battleActor);
#ifdef FFXHOOKS_HAVE_POLYHOOK
    ForceKimahriMiddleRingBuild(static_cast<uint8_t>(partySlot), "G0-finalize");
    EnsureKimahriUiTreeOdRow(static_cast<uint8_t>(partySlot), "G0-finalize");
#endif
    DumpKimahriRingState(static_cast<uint8_t>(partySlot), "G0-finalize");
}

static void MaintainKimahriBattleUi(uint8_t kimIdx) {
    if (kimIdx == 0xFFu || g_logOnly) return;
    void* battleActor = ResolveActorPtr(kimIdx);
    if (!IsKimahriActorPtr(battleActor)) return;
    const uint8_t charge = reinterpret_cast<uint8_t*>(battleActor)[kMemoryChrOvrCharge];
    if (charge < g_gateMinCost || charge == 0) return;
    ApplyKimahriRuntimePoolMax(battleActor);
    EnsureKimahriOverdriveCommandBit(static_cast<int>(kimIdx), battleActor);
    PrepareKimahriMenuRoute(battleActor);
    ApplyKimahriOverdriveReadyState(battleActor);
    PatchKimahriCommandRingTemplate();
    PatchKimahriCommandRingUi(kimIdx, battleActor);
}

static void ApplyKimahriPoolMax(void* actor) {
    ApplyKimahriRuntimePoolMax(actor);
}

static void ApplyKimahriPoolMaxForParty() {
    /* no-op — pool max belongs in save / Ronso submenu ratio patch only */
}

static uint8_t RonsoCostForRow(size_t rowIndex) {
    if (rowIndex < sizeof(kRonsoSkillCosts) / sizeof(kRonsoSkillCosts[0])) {
        return kRonsoSkillCosts[rowIndex];
    }
    return kDefaultRonsoGateMin;
}

static bool IsRonsoCommandId(uint16_t cmdId) {
    return cmdId >= kRonsoCommandIdMin && cmdId <= kRonsoCommandIdMax;
}

static bool IsRonsoSubmenuContext() {
    if (!g_base) return false;
    const auto* cmdList = reinterpret_cast<const uint16_t*>(g_base + RVA_FFX_BATTLE_SUBMENU_CMD_LIST);
    const auto* cmdEnd = reinterpret_cast<const uint16_t*>(g_base + RVA_FFX_BATTLE_SUBMENU_CMD_LIST_END);
    for (const uint16_t* cmd = cmdList; cmd < cmdEnd; ++cmd) {
        if (*cmd == 0xFFFFu) break;
        if (IsRonsoCommandId(*cmd)) return true;
    }
    return false;
}

static void PatchKimahriGreyoutRows() {
    if (g_logOnly || !g_base || !g_getCharge) return;
    if (!IsRonsoSubmenuContext()) return;

    const uint8_t kimahriIdx = FindKimahriBattleIndex();
    if (kimahriIdx == 0xFFu) {
        if (g_logFn && g_greyLogCount <= 2) {
            HookLog("[ffx-hooks] RonsoMana G3 patch skip — Kimahri battle index not found");
        }
        return;
    }

    const uint8_t charge = g_getCharge(kimahriIdx);

    const auto* cmdList = reinterpret_cast<const uint16_t*>(g_base + RVA_FFX_BATTLE_SUBMENU_CMD_LIST);
    const auto* cmdEnd = reinterpret_cast<const uint16_t*>(g_base + RVA_FFX_BATTLE_SUBMENU_CMD_LIST_END);
    auto* row = reinterpret_cast<uint8_t*>(g_base + RVA_FFX_BATTLE_SUBMENU_ROW_BASE);

    const LONG greyN = g_greyLogCount;
    size_t rowIndex = 0;
    for (const uint16_t* cmd = cmdList; cmd < cmdEnd; ++cmd, row += FFX_BATTLE_SUBMENU_ROW_STRIDE, ++rowIndex) {
        if (*cmd == 0xFFFFu) break;
        if (!IsRonsoCommandId(*cmd)) continue;

        const uint8_t cost = RonsoCostForRow(rowIndex);
        const uint32_t ratio = (charge >= cost) ? FFX_BATTLE_SUBMENU_UI_RATIO_FULL : 0u;
        *reinterpret_cast<uint32_t*>(row + FFX_BATTLE_SUBMENU_ROW_RATIO_OFF) = ratio;
        *reinterpret_cast<uint32_t*>(row + FFX_BATTLE_SUBMENU_ROW_RATIO_OFF + 4) = ratio;

        if (g_logFn && greyN <= 2 && rowIndex < 12) {
            HookLog(
                "[ffx-hooks] RonsoMana G3 row%zu cmd=%u kimahri=%u charge=%u cost=%u ratio=%u",
                rowIndex,
                static_cast<unsigned>(*cmd),
                static_cast<unsigned>(kimahriIdx),
                static_cast<unsigned>(charge),
                static_cast<unsigned>(cost),
                ratio);
        }
    }
}

extern "C" void __cdecl RonsoMana_LogDrainHit(
    uintptr_t actorVa,
    uint8_t oldCharge,
    uint8_t drainCost,
    uint8_t applied) {
    const LONG n = InterlockedIncrement(&g_drainLogCount);
    if (!g_logFn || n > 64) return;
    HookLog(
        "[ffx-hooks] RonsoMana drain #%ld actor=0x%08X old=%u cost=%u applied=%u kimahri=%d",
        static_cast<long>(n),
        static_cast<unsigned>(actorVa),
        static_cast<unsigned>(oldCharge),
        static_cast<unsigned>(drainCost),
        static_cast<unsigned>(applied),
        IsKimahriActorPtr(reinterpret_cast<void*>(actorVa)) ? 1 : 0);
}

extern "C" void __cdecl RonsoMana_ApplyDrain(uintptr_t actorVa, uint8_t oldCharge) {
    auto* actor = reinterpret_cast<uint8_t*>(actorVa);
    const uint8_t cost = g_drainCost;
    uint8_t newCharge = 0;
    uint8_t applied = 0;

    if (!g_logOnly && IsKimahriActorPtr(reinterpret_cast<void*>(actorVa))) {
        /* If the full-charge probe spoofed charge:=255, the drain site sees 255; subtract the
         * cost from the SAVED real charge instead so no free Overdrive is granted on use. */
        uint8_t base = oldCharge;
        if (g_probeFullCharge && g_kimahriFullSpoofOn) {
            base = g_kimahriSavedCharge;
            g_kimahriFullSpoofOn = false;
        } else {
            ApplyKimahriPoolMax(actor);
        }
        newCharge = (base > cost) ? static_cast<uint8_t>(base - cost) : 0;
        applied = 1;
    }

    /* Vanilla site always zeroes Ovr_charge@+0x5BC; Kimahri apply keeps partial pool. */
    actor[kMemoryChrOvrCharge] = g_logOnly ? 0 : (applied ? newCharge : 0);
    if (!g_logOnly && applied) {
        actor[kMemoryChrOvrChargeMax] = g_poolMax; /* restore real pool max after spoof */
    }

    if (applied && !g_logOnly && g_setCommandBit) {
        const uint8_t idx = FindBattleIndexForActorPtr(reinterpret_cast<void*>(actorVa));
        if (idx != 0xFFu && newCharge >= g_gateMinCost) {
            g_setCommandBit(static_cast<int>(idx), static_cast<int16_t>(kKimahriOverdriveCmdId), 1);
        }
    }

    RonsoMana_LogDrainHit(actorVa, oldCharge, cost, applied);
}

static bool BuildDrainStub(uintptr_t resumeVa, uint8_t** outStub, size_t* outLen) {
    std::vector<uint8_t> code;
    const auto emit = [&](std::initializer_list<uint8_t> bytes) {
        code.insert(code.end(), bytes.begin(), bytes.end());
    };

    emit({ 0x51 });
    emit({ 0x50 });
    emit({ 0xE8 });
    const size_t callSite = code.size();
    emit({ 0x00, 0x00, 0x00, 0x00 });
    emit({ 0x83, 0xC4, 0x08 });
    emit({ 0xE9 });
    const size_t jmpSite = code.size();
    emit({ 0x00, 0x00, 0x00, 0x00 });

    uint8_t* stub = static_cast<uint8_t*>(VirtualAlloc(
        nullptr, code.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!stub) return false;
    memcpy(stub, code.data(), code.size());

    const uintptr_t applyVa = reinterpret_cast<uintptr_t>(&RonsoMana_ApplyDrain);
    const int32_t callRel = static_cast<int32_t>(applyVa - (reinterpret_cast<uintptr_t>(stub) + callSite + 4));
    memcpy(stub + callSite, &callRel, sizeof(callRel));

    const int32_t jmpRel = static_cast<int32_t>(resumeVa - (reinterpret_cast<uintptr_t>(stub) + jmpSite + 4));
    memcpy(stub + jmpSite, &jmpRel, sizeof(jmpRel));

    FlushInstructionCache(GetCurrentProcess(), stub, code.size());
    *outStub = stub;
    *outLen = code.size();
    return true;
}

static bool InstallDrainPatch(uintptr_t base) {
    g_drainPatchVa = base + RVA_FFX_BATTLE_OVR_CHARGE_ZERO_AFTER_ACTION;
    g_drainResumeVa = base + kDrainResumeRva;

    uint8_t actual[kDrainPatchLen] = {};
    memcpy(actual, reinterpret_cast<const void*>(g_drainPatchVa), kDrainPatchLen);
    if (memcmp(actual, kDrainExpected, kDrainPatchLen) != 0) {
        HookLog(
            "[ffx-hooks] ERROR RonsoMana drain unexpected bytes @0x%08X: %02X %02X %02X %02X %02X %02X %02X",
            static_cast<unsigned>(g_drainPatchVa),
            actual[0], actual[1], actual[2], actual[3], actual[4], actual[5], actual[6]);
        return false;
    }

    memcpy(g_savedDrain, kDrainExpected, kDrainPatchLen);
    if (!BuildDrainStub(g_drainResumeVa, &g_drainStub, &g_drainStubLen)) {
        HookLog("[ffx-hooks] ERROR RonsoMana drain stub alloc failed");
        return false;
    }

    const int32_t rel = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(g_drainStub) - (g_drainPatchVa + 5));
    uint8_t jmpPatch[kDrainPatchLen] = {
        0xE9,
        static_cast<uint8_t>(rel & 0xFF),
        static_cast<uint8_t>((rel >> 8) & 0xFF),
        static_cast<uint8_t>((rel >> 16) & 0xFF),
        static_cast<uint8_t>((rel >> 24) & 0xFF),
        0x90,
        0x90,
    };

    if (!MemWrite(reinterpret_cast<void*>(g_drainPatchVa), jmpPatch, kDrainPatchLen)) {
        HookLog("[ffx-hooks] ERROR RonsoMana drain patch write failed @0x%08X", static_cast<unsigned>(g_drainPatchVa));
        VirtualFree(g_drainStub, 0, MEM_RELEASE);
        g_drainStub = nullptr;
        return false;
    }

    HookLog(
        "[ffx-hooks] RonsoMana drain installed patch@0x%08X resume@0x%08X logOnly=%d cost=%u stub=0x%08X",
        static_cast<unsigned>(g_drainPatchVa),
        static_cast<unsigned>(g_drainResumeVa),
        g_logOnly ? 1 : 0,
        static_cast<unsigned>(g_drainCost),
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(g_drainStub)));
    return true;
}

static bool RemoveDrainPatch() {
    bool restored = true;
    if (g_drainPatchVa != 0 && g_savedDrain[0] != 0) {
        restored = MemWrite(reinterpret_cast<void*>(g_drainPatchVa), g_savedDrain, kDrainPatchLen);
    }
    if (g_drainStub) {
        VirtualFree(g_drainStub, 0, MEM_RELEASE);
        g_drainStub = nullptr;
        g_drainStubLen = 0;
    }
    g_drainPatchVa = 0;
    g_drainResumeVa = 0;
    memset(g_savedDrain, 0, sizeof(g_savedDrain));
    return restored;
}

#ifdef FFXHOOKS_HAVE_POLYHOOK

static int __cdecl BuildMenu_Shim(int partySlot, int menuCtxParam) {
    const uint8_t idx = static_cast<uint8_t>(partySlot);
    void* menuCtx = reinterpret_cast<void*>(static_cast<uintptr_t>(menuCtxParam));
    void* battleActor = ResolveActorPtr(idx);
    const bool kimahri = IsKimahriActorPtr(battleActor);

    if (kimahri) {
        PrepareKimahriMenuPatches(partySlot, battleActor, menuCtx);
    }
    KimahriPoolSpoof spoof = BeginKimahriMaxSpoof(battleActor);

    if (kimahri) {
        const uint8_t charge = g_getCharge
            ? g_getCharge(idx)
            : reinterpret_cast<uint8_t*>(battleActor)[kMemoryChrOvrCharge];
        const LONG n = InterlockedIncrement(&g_menuLogCount);
        if (g_logFn && n <= 24) {
            HookLog(
                "[ffx-hooks] RonsoMana G0 menu #%ld slot=%d charge=%u max=%u kimahri=1 ctx=0x%08X actor=0x%08X",
                static_cast<long>(n),
                partySlot,
                static_cast<unsigned>(charge),
                static_cast<unsigned>(ReadActorOvrMax(battleActor)),
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(menuCtx)),
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(battleActor)));
        }
        if (n == 1) LogKimahriProbeOnce();
    }

    const int result = g_menuBuildTrampoline(partySlot, menuCtxParam);

    if (kimahri) {
        FinalizeKimahriMenuPatches(partySlot, battleActor, menuCtx);
    }
    EndKimahriMaxSpoof(&spoof);
    return result;
}

static int __cdecl RefreshMenu_Shim(int a1, int m) {
    const uint8_t idx = static_cast<uint8_t>(m);
    void* battleActor = ResolveActorPtr(idx);
    void* menuCtx = battleActor;
    if (IsKimahriActorPtr(battleActor)) {
        PrepareKimahriMenuPatches(m, battleActor, menuCtx);
    }
    KimahriPoolSpoof spoof = BeginKimahriMaxSpoof(battleActor);
    const int result = g_refreshMenuTrampoline(a1, m);
    if (IsKimahriActorPtr(battleActor)) {
        FinalizeKimahriMenuPatches(m, battleActor, menuCtx);
    }
    EndKimahriMaxSpoof(&spoof);
    return result;
}

static uint16_t SafeReadU16At(const void* p) {
    __try {
        return *reinterpret_cast<const uint16_t*>(p);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0xFFFFu;
    }
}

/* G4 (READ-ONLY diagnostic): sub_78ABE0 is the per-command confirm/usability gate (only caller =
 * 0x792260 menu confirm handler). It returns -1=usable / 0=grey-locked, gating on
 * actor[0x5BC](OD charge) >= resolved command's CostOverdrive (entry+0x26). This shim calls the
 * original FIRST and NEVER changes the result; it only logs the inputs so we can pinpoint exactly
 * which condition locks Kimahri's Ronso skills (104-115) at partial charge:
 *   - 6DE!=0  -> gate used the FIXED Copycat(40) cost, not the skill's own;
 *   - cost@+0x26 != expected -> command.bin edit didn't reach runtime / wrong cmd resolved;
 *   - src3==0 -> a precondition locked it before the cost check;
 *   - charge < cost -> genuine affordability fail. */
static int __cdecl CmdUsabilityGate_Shim(int src, int entryIdx) {
    const int result = g_cmdGateTrampoline(src, entryIdx);
    if (g_logFn && src) {
        const uint8_t slot = *reinterpret_cast<const uint8_t*>(src);
        /* Mirror the gate EXACTLY: sub_78ABE0 derives its actor via
         * FFX_Battle_GetActorByIndex(src[0]). Use the same resolver so the
         * Kimahri filter can never silently miss the gate's own record. */
        void* actor = g_getActor ? g_getActor(slot) : ResolveActorPtr(slot);
        if (IsKimahriActorPtr(actor)) {
            const LONG n = InterlockedIncrement(&g_cmdGateLogCount);
            if (n <= 80) {
                const auto* ab = reinterpret_cast<const uint8_t*>(actor);
                const uint8_t charge = ab[kMemoryChrOvrCharge];
                const uint8_t copycat = ab[kMemoryChrCopycatMode];
                /* gate greys if actor[0x5D4](current MP, int32) < MP cost — log it to
                 * rule the MP branch in/out vs the charge<CostOverdrive branch. */
                const int32_t curMp = *reinterpret_cast<const int32_t*>(ab + 0x5D4);
                const uint8_t src3 = reinterpret_cast<const uint8_t*>(src)[3];
                uint16_t code = 0xFFFFu;
                if (entryIdx >= 0 && entryIdx < 32) {
                    code = SafeReadU16At(reinterpret_cast<const uint8_t*>(src) + 16 * entryIdx + 8);
                }
                const uint16_t id = static_cast<uint16_t>(code & 0x0FFFu);
                unsigned cost = 0xFFFFu;
                unsigned mflg = 0xFFFFu;
                if (g_getCommandEntry && code != 0xFFFFu) {
                    uint8_t* e = g_getCommandEntry(static_cast<int16_t>(id), 0);
                    if (e) { cost = e[38]; mflg = e[22]; }
                }
                HookLog(
                    "[ffx-hooks] RonsoMana G4 cmdGate #%ld slot=%u entryIdx=%d code=0x%04X id=%u cost@+0x26=%u mflg=0x%02X charge=%u mp@0x5D4=%d 6DE=0x%02X src3=0x%02X ret=%d(%s)",
                    static_cast<long>(n),
                    static_cast<unsigned>(slot),
                    entryIdx,
                    static_cast<unsigned>(code),
                    static_cast<unsigned>(id),
                    cost,
                    mflg,
                    static_cast<unsigned>(charge),
                    static_cast<int>(curMp),
                    static_cast<unsigned>(copycat),
                    static_cast<unsigned>(src3),
                    result,
                    result ? "USABLE" : "GREY");
            }
        }
    }
    return result;
}

/* G5 (READ-ONLY diagnostic): sub_792260 is the confirm handler and the ONLY caller of the
 * usability gate sub_78ABE0. It only REACHES the gate when the default switch(src[4]) case runs,
 * v20(=sub_799B50)==0, AND src[3]!=0. Since G4 never logged on Kimahri's partial-charge Jump
 * confirm, the lock is HERE (upstream of the cost check). This shim snapshots the menu context
 * (src = sub_7B0A20(), a pure getter) and the actor guard bytes, deduped so idle frames don't
 * flood, so one capture shows exactly which precondition fails:
 *   - s4mode != the command case  -> confirm routed elsewhere (never gates);
 *   - s3cnt == 0                  -> `!src[3]` short-circuits to beep BEFORE the gate;
 *   - a1075/a1294/a1544 != 0      -> an actor-status guard blocks the whole confirm body. */
static int __cdecl MenuConfirm_Shim() {
    if (g_logFn && g_getMenuContext) {
        const uint8_t* src = reinterpret_cast<const uint8_t*>(g_getMenuContext());
        if (src) {
            const uint8_t slot = src[0];
            void* actor = g_getActor ? g_getActor(slot) : nullptr;
            if (IsKimahriActorPtr(actor)) {
                const uint8_t hi = src[2];
                const uint16_t code = (hi < 32u) ? SafeReadU16At(src + 16 * hi + 8) : 0xFFFFu;
                /* dedup: only emit when the decision-relevant state changes (covers both the
                 * event-driven and per-frame call patterns without spamming). */
                const uint32_t sig = static_cast<uint32_t>(src[2])
                    | (static_cast<uint32_t>(src[3]) << 8)
                    | (static_cast<uint32_t>(src[4]) << 16)
                    | (static_cast<uint32_t>(code & 0xFF) << 24);
                static volatile LONG s_lastSig = -1;
                if (static_cast<LONG>(sig) != s_lastSig) {
                    s_lastSig = static_cast<LONG>(sig);
                    const LONG n = InterlockedIncrement(&g_confirmLogCount);
                    if (n <= 80) {
                        const auto* ab = reinterpret_cast<const uint8_t*>(actor);
                        const uint16_t id = static_cast<uint16_t>(code & 0x0FFFu);
                        HookLog(
                            "[ffx-hooks] RonsoMana G5 confirm #%ld slot=%u s1=%u s2hi=%u s3cnt=%u s4mode=%u s6=%u s7=%u code=0x%04X id=%u charge=%u 6DE=0x%02X a1075=%u a1294=%u a1544=%u",
                            static_cast<long>(n),
                            static_cast<unsigned>(slot),
                            static_cast<unsigned>(src[1]),
                            static_cast<unsigned>(src[2]),
                            static_cast<unsigned>(src[3]),
                            static_cast<unsigned>(src[4]),
                            static_cast<unsigned>(src[6]),
                            static_cast<unsigned>(src[7]),
                            static_cast<unsigned>(code),
                            static_cast<unsigned>(id),
                            static_cast<unsigned>(ab[kMemoryChrOvrCharge]),
                            static_cast<unsigned>(ab[kMemoryChrCopycatMode]),
                            static_cast<unsigned>(ab[1075]),
                            static_cast<unsigned>(ab[1294]),
                            static_cast<unsigned>(ab[1544]));
                    }
                }
            }
        }
    }
    return g_confirmTrampoline();
}

static int __cdecl HasCommandBit_Shim(uint8_t battleSlot, int16_t cmdId) {
    const int vanilla = g_hasCommandBitTrampoline(battleSlot, cmdId);
    if (g_logOnly) return vanilla;

    void* actor = ResolveActorPtr(battleSlot);
    if (!IsKimahriActorPtr(actor)) return vanilla;

    const uint8_t charge = reinterpret_cast<uint8_t*>(actor)[kMemoryChrOvrCharge];
    if (charge == 0) return vanilla;

    /* OD command HEADER (Ronso Rage itself): show when charge >= gateMin. */
    if (charge >= g_gateMinCost) {
        if (g_odRingHeaderCmdIndex < 0) ScanForOdRingHeader();
        if (cmdId == static_cast<int16_t>(kKimahriOverdriveCmdId)
            || cmdId == static_cast<int16_t>(kEncodedOverdriveCmd)
            || (cmdId & 0xFFF) == kKimahriOverdriveCmdId
            || (g_odRingHeaderCmdIndex >= 0 && cmdId == g_odRingHeaderCmdIndex)) {
            const int forced = vanilla | 1;
            const LONG n = InterlockedIncrement(&g_hasCmdLogCount);
            if (g_logFn && n <= 32) {
                HookLog(
                    "[ffx-hooks] RonsoMana G0' HasCmd #%ld slot=%u cmd=%d charge=%u vanilla=%d ->%d",
                    static_cast<long>(n),
                    static_cast<unsigned>(battleSlot),
                    static_cast<int>(cmdId),
                    static_cast<unsigned>(charge),
                    vanilla,
                    forced);
            }
            return forced;
        }
    }

    /* Individual Ronso Rage SKILLS (Jump=104, ...): each has its own cmd id; FFX_Btl_BuildActorCommandMenu
     * greys them unless OD is full. Make them usable when Kimahri can pay the skill's own CostOverdrive
     * (entry+0x26, the command.bin field). Diagnostic: log EVERY OD-cost cmd queried for Kimahri so one
     * battle reveals the learned-skill set; FORCE only the proven-learned Jump (104) for now. */
    if (g_getCommandEntry) {
        const int16_t entryIdx = static_cast<int16_t>(cmdId & 0x0FFFu);
        const uint8_t* entry = g_getCommandEntry(entryIdx, 0);
        if (entry) {
            const uint8_t cost = entry[0x26];
            if (cost > 0u && cost != 0xFFu) {
                const LONG sn = InterlockedIncrement(&g_odSkillSeenLogCount);
                if (g_logFn && sn <= 80) {
                    HookLog(
                        "[ffx-hooks] RonsoMana G0' odSeen #%ld slot=%u cmd=%d idx=%d cost=%u charge=%u vanilla=%d",
                        static_cast<long>(sn),
                        static_cast<unsigned>(battleSlot),
                        static_cast<int>(cmdId),
                        static_cast<int>(entryIdx),
                        static_cast<unsigned>(cost),
                        static_cast<unsigned>(charge),
                        vanilla);
                }
                /* learned/innate bit (0x690 bank, vanilla&2) = Kimahri actually has this Rage.
                 * Force-available any learned Rage he can pay for; keep Jump(104) explicit as a
                 * belt-and-suspenders fallback in case the innate bit isn't set as expected. */
                const bool learned = (vanilla & 2) != 0;
                if (charge >= cost && (learned || entryIdx == 104)) {
                    if (g_setCommandBit) g_setCommandBit(battleSlot, entryIdx, 1);
                    const LONG fn = InterlockedIncrement(&g_hasCmdLogCount);
                    if (g_logFn && fn <= 48) {
                        HookLog(
                            "[ffx-hooks] RonsoMana G0' odSkillFORCE #%ld slot=%u idx=%d cost=%u charge=%u vanilla=%d learned=%d",
                            static_cast<long>(fn),
                            static_cast<unsigned>(battleSlot),
                            static_cast<int>(entryIdx),
                            static_cast<unsigned>(cost),
                            static_cast<unsigned>(charge),
                            vanilla,
                            learned ? 1 : 0);
                    }
                    return vanilla | 1;
                }
            }
        }
    }
    return vanilla;
}

static int __cdecl BuildCommandRing_Shim(
    unsigned char battleSlot,
    int n255,
    int ringKind,
    int a4,
    float a5,
    void* dst) {
    void* actorPre = ResolveActorPtr(battleSlot);
    const bool kimahriPre = IsKimahriActorPtr(actorPre);
    if (kimahriPre && g_logFn) {
        const LONG n = InterlockedIncrement(&g_ringBuildLogCount);
        if (n <= 64) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(actorPre);
            HookLog(
                "[ffx-hooks] RonsoMana G0 ring #%ld slot=%u n255=%d kind=%d a4=%d charge=%u df7=%u",
                static_cast<long>(n),
                static_cast<unsigned>(battleSlot),
                n255,
                ringKind,
                a4,
                static_cast<unsigned>(bytes[kMemoryChrOvrCharge]),
                static_cast<unsigned>(bytes[kMemoryChrRingBuildGate]));
        }
    }

    if (kimahriPre) {
        DumpOdBlobStructureOnce();                 /* read-only, fires once even in log-only mode */
    }
    if (kimahriPre && !g_logOnly) {
        PatchCase2BlobForKimahri(battleSlot);
    }

    const int result = g_buildCommandRingTrampoline(battleSlot, n255, ringKind, a4, a5, dst);
    if (kimahriPre && g_logFn) {
        const LONG n = InterlockedIncrement(&g_ringBuildLogCount);
        if (n <= 64) {
            HookLog(
                "[ffx-hooks] RonsoMana G0 ring* #%ld slot=%u kind=%d result=%d stack=%d blob2=0x%08X",
                static_cast<long>(n),
                static_cast<unsigned>(battleSlot),
                ringKind,
                result,
                ReadMenuTreeStackDepth(),
                static_cast<unsigned>(ReadCase2BlobPtr()));
        }
        if (n <= 4) {
            LogCase2BlobDiag(static_cast<uint8_t>(battleSlot + 41), "G0-ring-post");
        }
    }
    if (g_logOnly) return result;

    void* actor = ResolveActorPtr(battleSlot);
    if (!IsKimahriActorPtr(actor)) return result;

    const uint8_t charge = reinterpret_cast<uint8_t*>(actor)[kMemoryChrOvrCharge];
    if (charge < g_gateMinCost || charge == 0) return result;

    /* 792AB0: 7ACEC0(...,1,...) then if 79AF70: 7ACEC0(...,12,1,...) for Overdrive row */
    if (n255 == 255 && ringKind == 1 && a4 == 0) {
        g_buildCommandRingTrampoline(battleSlot, 255, 12, 1, a5, dst);
        if (g_logFn) {
            const LONG n = InterlockedIncrement(&g_ringBuildLogCount);
            if (n <= 64) {
                HookLog(
                    "[ffx-hooks] RonsoMana G0* forceRing #%ld slot=%u charge=%u kind=12",
                    static_cast<long>(n),
                    static_cast<unsigned>(battleSlot),
                    static_cast<unsigned>(charge));
            }
        }
    }
    return result;
}

static uint8_t ReadMenuEarlyReturnFlag() {
    if (!g_base) return 0;
    return *reinterpret_cast<const uint8_t*>(g_base + RVA_FFX_BATTLE_MENU_EARLY_RETURN_FLAG);
}

#ifdef FFXHOOKS_HAVE_POLYHOOK
static void ForceKimahriMiddleRingBuild(uint8_t battleSlot, const char* reason) {
    if (!g_buildCommandRingTrampoline || g_logOnly) return;
    void* actor = ResolveActorPtr(battleSlot);
    if (!IsKimahriActorPtr(actor)) return;
    const uint8_t charge = reinterpret_cast<uint8_t*>(actor)[kMemoryChrOvrCharge];
    if (charge < g_gateMinCost || charge == 0) return;

    BuildCommandRing_Shim(battleSlot, 255, 1, 0, 2.0f, nullptr);
    BuildCommandRing_Shim(battleSlot, 255, 12, 1, 2.0f, nullptr);

    static volatile LONG forceLogCount = 0;
    const LONG n = InterlockedIncrement(&forceLogCount);
    if (g_logFn && n <= 24) {
        HookLog(
            "[ffx-hooks] RonsoMana P0* forceUiTree #%ld slot=%u charge=%u reason=%s",
            static_cast<long>(n),
            static_cast<unsigned>(battleSlot),
            static_cast<unsigned>(charge),
            reason ? reason : "?");
    }
}

static int16_t __cdecl PushMenuTreeEntry_Shim(int16_t a1, int16_t a2, int16_t a3, int16_t a4) {
    const bool kimTree = IsKimahriMenuTreeId(a3);
    if (kimTree && g_logFn) {
        const LONG n = InterlockedIncrement(&g_uiPushLogCount);
        if (n <= 64) {
            HookLog(
                "[ffx-hooks] RonsoMana B0 push #%ld a1=%d a2=%d treeId=%d ringKind=%d stack=%d",
                static_cast<long>(n),
                static_cast<int>(a1),
                static_cast<int>(a2),
                static_cast<int>(a3),
                static_cast<int>(a4),
                ReadMenuTreeStackDepth());
        }
    }
    return g_pushMenuTreeTrampoline(a1, a2, a3, a4);
}

static int __cdecl ResolveMenuTreeNode_Shim(int a1, int a2, int a3, char a4, int a5, void* a6) {
    const bool kimTree = IsKimahriMenuTreeId(static_cast<int16_t>(a3));
    const int result = g_resolveMenuTreeTrampoline(a1, a2, a3, a4, a5, a6);
    if (kimTree && g_logFn) {
        const LONG n = InterlockedIncrement(&g_uiResolveLogCount);
        if (n <= 64) {
            HookLog(
                "[ffx-hooks] RonsoMana B0 resolve #%ld a1=%d a2=%d treeId=%d a4=%d ->%d blob=0x%08X stack=%d",
                static_cast<long>(n),
                a1,
                a2,
                a3,
                static_cast<int>(a4),
                result,
                static_cast<unsigned>(ReadUiDisplayBlobCase4()),
                ReadMenuTreeStackDepth());
        }
    }
    return result;
}

static void EnsureKimahriUiTreeOdRow(uint8_t battleSlot, const char* reason) {
    if (!g_pushMenuTreeTrampoline || !g_resolveMenuTreeTrampoline || g_logOnly) return;
    void* actor = ResolveActorPtr(battleSlot);
    if (!IsKimahriActorPtr(actor)) return;
    const uint8_t charge = reinterpret_cast<uint8_t*>(actor)[kMemoryChrOvrCharge];
    if (charge < g_gateMinCost || charge == 0) return;

    const int16_t treeId = KimahriMenuTreeId(battleSlot);
    PatchCase2BlobForKimahri(battleSlot);
    LogCase2BlobDiag(static_cast<uint8_t>(treeId), "B0-inject-pre");

    if (g_menuTreeReset) g_menuTreeReset();

    g_pushMenuTreeTrampoline(2, 1, treeId, 1);
    const int mainResult = g_resolveMenuTreeTrampoline(2, 1, treeId, 1, 0, nullptr);
    if (mainResult >= 0 && g_finishMenuTree) {
        g_finishMenuTree(2, 1, treeId);
    }

    g_pushMenuTreeTrampoline(2, 1, treeId, 12);
    const int odResult = g_resolveMenuTreeTrampoline(2, 1, treeId, 1, 0, nullptr);
    if (odResult >= 0 && g_finishMenuTree) {
        g_finishMenuTree(2, 1, treeId);
    }

    static volatile LONG injectLogCount = 0;
    const LONG n = InterlockedIncrement(&injectLogCount);
    if (g_logFn && n <= 24) {
        HookLog(
            "[ffx-hooks] RonsoMana B0* inject #%ld slot=%u treeId=%d charge=%u main=%d od=%d blob=0x%08X reason=%s",
            static_cast<long>(n),
            static_cast<unsigned>(battleSlot),
            static_cast<int>(treeId),
            static_cast<unsigned>(charge),
            mainResult,
            odResult,
            static_cast<unsigned>(ReadUiDisplayBlobCase4()),
            reason ? reason : "?");
    }
}
#endif /* FFXHOOKS_HAVE_POLYHOOK */

static int __cdecl BattleMenuInputDispatch_Shim() {
    const uint8_t kimIdx = FindKimahriBattleIndex();
    void* kimActor = (kimIdx != 0xFFu) ? ResolveActorPtr(kimIdx) : nullptr;
    uint8_t df7Before = 0;
    bool prepKimahri = false;

    if (kimActor && IsKimahriActorPtr(kimActor)) {
        auto* bytes = reinterpret_cast<uint8_t*>(kimActor);
        const uint8_t charge = bytes[kMemoryChrOvrCharge];
        df7Before = bytes[kMemoryChrRingBuildGate];
        const uint16_t menuOdWord = *reinterpret_cast<uint16_t*>(bytes + kMemoryChrMenuOdWord);
        const uint8_t earlyFlag = ReadMenuEarlyReturnFlag();

        const LONG n = InterlockedIncrement(&g_menuDispatchLogCount);
        if (g_logFn && n <= 48) {
            HookLog(
                "[ffx-hooks] RonsoMana P0 dispatch #%ld kim=%u charge=%u max=%u df7=%u 590=0x%02X 591=0x%02X 6C8=%u earlyFlg=%u logOnly=%d",
                static_cast<long>(n),
                static_cast<unsigned>(kimIdx),
                static_cast<unsigned>(charge),
                static_cast<unsigned>(bytes[kMemoryChrOvrChargeMax]),
                static_cast<unsigned>(df7Before),
                static_cast<unsigned>(bytes[kMemoryChrOdReadyByte]),
                static_cast<unsigned>(bytes[kMemoryChrOdStateByte]),
                static_cast<unsigned>(menuOdWord),
                static_cast<unsigned>(earlyFlag),
                g_logOnly ? 1 : 0);
        }

        if (!g_logOnly && charge >= g_gateMinCost && charge != 0) {
            prepKimahri = true;
            ApplyKimahriRuntimePoolMax(kimActor);  /* persistent gauge-full pin (max:=charge) */
            ApplyKimahriOverdriveReadyState(kimActor);
            PrepareKimahriMenuRoute(kimActor);
            bytes[kMemoryChrRingBuildGate] = 0;
        }
    }

    /* hudSafe=24: max is already pinned to charge above (persistent), so the LEFT-press handler
     * inside 792AB0 AND the per-frame render that follows both see a full gauge. (No-op spoof.) */
    KimahriPoolSpoof dispatchSpoof = BeginKimahriMaxSpoof(kimActor);
    const int result = g_menuInputDispatchTrampoline();
    EndKimahriMaxSpoof(&dispatchSpoof);

    if (prepKimahri && kimActor && !g_logOnly) {
        auto* bytes = reinterpret_cast<uint8_t*>(kimActor);
        if (bytes[kMemoryChrRingBuildGate] != 0) {
            bytes[kMemoryChrRingBuildGate] = 0;
        }
    }

    return result;
}

static int __cdecl IsOverdriveReady_Shim(unsigned __int8 battleSlot) {
    const int vanilla = g_isOverdriveReadyTrampoline(battleSlot);
    if (g_logOnly) return vanilla;

    void* actor = ResolveActorPtr(battleSlot);
    if (!IsKimahriActorPtr(actor)) return vanilla;

    const uint8_t charge = reinterpret_cast<uint8_t*>(actor)[kMemoryChrOvrCharge];
    if (charge < g_gateMinCost || charge == 0) return vanilla;

    const LONG n = InterlockedIncrement(&g_odReadyLogCount);
    if (g_logFn && n <= 24) {
        HookLog(
            "[ffx-hooks] RonsoMana G0+ IsOdReady #%ld slot=%u charge=%u vanilla=%d ->1",
            static_cast<long>(n),
            static_cast<unsigned>(battleSlot),
            static_cast<unsigned>(charge),
            vanilla);
    }
    return 1;
}

static int __cdecl CopyMenuTemplate_Shim(int slotRingPtr) {
    const int result = g_copyMenuTemplateTrampoline(slotRingPtr);
    if (g_logOnly || !g_base) return result;

    const uintptr_t ringBase = BattleCommandRingUiBase();
    if (!ringBase) return result;

    const uintptr_t slotPtr = static_cast<uintptr_t>(slotRingPtr);
    if (slotPtr < ringBase) return result;
    const uintptr_t delta = slotPtr - ringBase;
    if (delta % FFX_BATTLE_COMMAND_RING_SLOT_STRIDE != 0) return result;

    const uint8_t battleSlot = static_cast<uint8_t>(delta / FFX_BATTLE_COMMAND_RING_SLOT_STRIDE);
    void* battleActor = ResolveActorPtr(battleSlot);
    if (!IsKimahriActorPtr(battleActor)) return result;

    const uint8_t charge = reinterpret_cast<uint8_t*>(battleActor)[kMemoryChrOvrCharge];
    if (charge < g_gateMinCost || charge == 0) return result;

    PatchKimahriOverdriveRowAt(
        slotPtr + FFX_BATTLE_MENU_OVERDRIVE_ROW_OFF,
        "7AEF",
        static_cast<int>(battleSlot));

    const LONG n = InterlockedIncrement(&g_menuPatchLogCount);
    if (g_logFn && n <= 16) {
        const auto* odRow = reinterpret_cast<const uint16_t*>(slotPtr + FFX_BATTLE_MENU_OVERDRIVE_ROW_OFF);
        HookLog(
            "[ffx-hooks] RonsoMana G0* sync #%ld slot=%u ring=0x%08X od0=0x%04X",
            static_cast<long>(n),
            static_cast<unsigned>(battleSlot),
            static_cast<unsigned>(slotPtr),
            static_cast<unsigned>(odRow[0]));
    }
    return result;
}

static int __cdecl HasCommandSave_Shim(int partySlot, int cmdId) {
    const int vanilla = g_hasCommandSaveTrampoline(partySlot, cmdId);
    if (g_logOnly) return vanilla;
    if ((cmdId & 0xFFFFF000) != 0x3000) return vanilla;
    if ((cmdId & 0xFFF) != kKimahriOverdriveCmdId) return vanilla;

    const uint8_t kimIdx = FindKimahriBattleIndex();
    if (kimIdx == 0xFFu) return vanilla;
    void* actor = ResolveActorPtr(kimIdx);
    if (!IsKimahriActorPtr(actor)) return vanilla;
    if (reinterpret_cast<uint8_t*>(actor)[kMemoryChrOvrCharge] < g_gateMinCost) return vanilla;
    return 1;
}

static int __cdecl MenuRoute_Shim(int battleSlot, int actorPtr) {
    const int vanilla = g_menuRouteTrampoline(battleSlot, actorPtr);
    if (g_logOnly) return vanilla;

    auto* actor = reinterpret_cast<uint8_t*>(actorPtr);
    if (!IsKimahriActorPtr(actor)) return vanilla;

    const uint8_t charge = actor[kMemoryChrOvrCharge];
    if (charge < g_gateMinCost || charge == 0) return vanilla;

    const LONG n = InterlockedIncrement(&g_menuRouteLogCount);
    if (g_logFn && n <= 32) {
        HookLog(
            "[ffx-hooks] RonsoMana G1' route #%ld slot=%d charge=%u max=%u vanilla=%d ->0",
            static_cast<long>(n),
            battleSlot,
            static_cast<unsigned>(charge),
            static_cast<unsigned>(actor[kMemoryChrOvrChargeMax]),
            vanilla);
    }
    return 0;
}

static int __cdecl OpenSubmenu_Shim(int actorIdx) {
    const uint8_t idx = static_cast<uint8_t>(actorIdx);
    void* actor = ResolveActorPtr(idx);
    KimahriPoolSpoof spoof = BeginKimahriMaxSpoof(actor);

    const LONG n = InterlockedIncrement(&g_gateLogCount);
    if (g_logFn && n <= 32 && IsKimahriActorPtr(actor)) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(actor);
        HookLog(
            "[ffx-hooks] RonsoMana G1' open #%ld actor=%u charge=%u max=%u spoof=%d",
            static_cast<long>(n),
            static_cast<unsigned>(idx),
            static_cast<unsigned>(bytes[kMemoryChrOvrCharge]),
            static_cast<unsigned>(bytes[kMemoryChrOvrChargeMax]),
            spoof.active ? 1 : 0);
    }

    const int result = g_openSubmenuTrampoline(actorIdx);
    EndKimahriMaxSpoof(&spoof);
    return result;
}

static int __cdecl SubmenuRefresh_Shim() {
    const int result = g_submenuRefreshTrampoline();
    const uint8_t kimIdx = FindKimahriBattleIndex();
    if (kimIdx != 0xFFu) {
        MaintainKimahriBattleUi(kimIdx);
    }
    PatchKimahriGreyoutRows();

    const LONG n = InterlockedIncrement(&g_greyLogCount);
    if (g_logFn && n <= 32) {
        if (n == 1) LogKimahriProbeOnce();
        const uint8_t kimIdx = FindKimahriBattleIndex();
        HookLog(
            "[ffx-hooks] RonsoMana G3' refresh #%ld kimahri=%u ronso=%d",
            static_cast<long>(n),
            static_cast<unsigned>(kimIdx),
            IsRonsoSubmenuContext() ? 1 : 0);
    }
    return result;
}

static bool InstallDetour(
    uintptr_t base,
    uint32_t rva,
    uint64_t shimVa,
    PLH::x86Detour** detourOut,
    uint64_t* trampolineOut,
    const char* label) {
    if (!detourOut || !trampolineOut || *detourOut) return false;
    const uint64_t targetVa = static_cast<uint64_t>(base + rva);
    *trampolineOut = 0;
    try {
        *detourOut = new PLH::x86Detour(targetVa, shimVa, trampolineOut);
        const bool hooked = (*detourOut)->hook();
        if (!hooked) {
            delete *detourOut;
            *detourOut = nullptr;
            *trampolineOut = 0;
            HookLog("[ffx-hooks] ERROR RonsoMana %s detour hook() failed @0x%08X", label, static_cast<unsigned>(targetVa));
            return false;
        }
        HookLog("[ffx-hooks] RonsoMana %s detour ok target=0x%08X trampoline=0x%llX",
            label,
            static_cast<unsigned>(targetVa),
            static_cast<unsigned long long>(*trampolineOut));
        return true;
    } catch (...) {
        delete *detourOut;
        *detourOut = nullptr;
        *trampolineOut = 0;
        HookLog("[ffx-hooks] ERROR RonsoMana %s detour exception @0x%08X", label, static_cast<unsigned>(targetVa));
        return false;
    }
}

static bool RemoveDetour(PLH::x86Detour* detour, const char* label) {
    if (!detour) return true;
    const bool ok = detour->unHook();
    delete detour;
    HookLog("[ffx-hooks] RonsoMana %s detour remove %s", label, ok ? "ok" : "FAILED");
    return ok;
}

#endif /* FFXHOOKS_HAVE_POLYHOOK */

static void ReadEnvU8(const char* name, uint8_t* out, uint8_t minVal, uint8_t maxVal) {
    if (!out) return;
    char buf[16] = {};
    const DWORD len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return;
    const unsigned parsed = static_cast<unsigned>(strtoul(buf, nullptr, 10));
    if (parsed < minVal || parsed > maxVal) return;
    *out = static_cast<uint8_t>(parsed);
}

/* RETIRED 2026-06-16: opt-in escape hatch to run the old Ronso Mana LAB for reference. */
static bool RonsoManaForceEnabled() {
    char buf[8] = {};
    const DWORD len = GetEnvironmentVariableA("FFXHOOKS_RONSO_MANA_FORCE", buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return false;
    const char c = buf[0];
    return c == '1' || c == 'y' || c == 'Y' || c == 't' || c == 'T';
}

} // namespace

RonsoManaInstallResult InstallRonsoManaHook(
    uintptr_t base,
    bool enableGate,
    bool enableGreyout,
    bool enableDrain,
    bool logOnly,
    RonsoManaLogFn log) {
    RonsoManaInstallResult result = { false, 0, 0, 0 };

    /* ───────────────────────────────────────────────────────────────────────────
     * RETIRED 2026-06-16 (Jarvis-MAGIC). The Ronso Mana LAB is DISABLED by default.
     * The partial-Overdrive feature is achievable (per-command usability via HasCommandBit
     * at 0x79AD40 made the Ronso Rage skills white at partial charge — proven in-game), but
     * this hook accreted conflicting "Auron-era" layers (max:=charge pin, OD-ready forcing,
     * menu-route/command-ring/blob patches) that fight each other and caused side bugs:
     * OD-gain throttle, Entrust→0/0, intermittent Block loss, header vanishing, a crash.
     * Per Halyson's call: keep ALL of this as REFERENCE, ship NOTHING from it, rebuild a
     * clean Kimahri-only mod from scratch following the spec:
     *   docs/ai/GPT_AGENT_PROMPT_KIMAHRI_RONSO_MANA_CLEAN_BUILD.md
     * Default = no-op (vanilla). Set FFXHOOKS_RONSO_MANA_FORCE=1 to run the old lab.
     * ─────────────────────────────────────────────────────────────────────────── */
    if (!RonsoManaForceEnabled()) {
        if (log) log("[ffx-hooks] RonsoMana DISABLED (retired lab; clean rebuild spec: docs/ai/GPT_AGENT_PROMPT_KIMAHRI_RONSO_MANA_CLEAN_BUILD.md). Set FFXHOOKS_RONSO_MANA_FORCE=1 to force the old lab.");
        return result;
    }

    if (g_installed) {
        result.ok = true;
        result.stubDrain = g_drainPatchVa;
        return result;
    }
    if (!enableGate && !enableGreyout && !enableDrain) {
        if (log) log("[ffx-hooks] RonsoMana install skipped — no feature flags");
        return result;
    }

    g_logFn = log;
    g_logOnly = logOnly;
    g_enableGate = enableGate;
    g_enableGreyout = enableGreyout;
    g_enableDrain = enableDrain;
    g_gateLogCount = 0;
    g_greyLogCount = 0;
    g_menuLogCount = 0;
    g_menuRouteLogCount = 0;
    g_menuPatchLogCount = 0;
    g_poolMaxLogCount = 0;
    g_uiRingNullLogCount = 0;
    g_hasCmdLogCount = 0;
    g_odReadyLogCount = 0;
    g_ringBuildLogCount = 0;
    g_uiPushLogCount = 0;
    g_uiResolveLogCount = 0;
    g_menuDispatchLogCount = 0;
    g_drainLogCount = 0;
    g_hudBootstrapMenus = 0;
    g_drainCost = kDefaultRonsoDrainCost;
    g_poolMax = kDefaultRonsoPoolMax;
    g_gateMinCost = kDefaultRonsoGateMin;

    {
        uint8_t drainCost = kDefaultRonsoDrainCost;
        uint8_t poolMax = kDefaultRonsoPoolMax;
        uint8_t gateMin = kDefaultRonsoGateMin;
        ReadEnvU8("FFXHOOKS_RONSO_DRAIN_COST", &drainCost, 1, 255);
        ReadEnvU8("FFXHOOKS_RONSO_POOL_MAX", &poolMax, 1, 255);
        ReadEnvU8("FFXHOOKS_RONSO_GATE_MIN", &gateMin, 1, 255);
        g_drainCost = drainCost;
        g_poolMax = poolMax;
        g_gateMinCost = gateMin;
    }

    ResolveGameFns(base);
    HookLog(
        "[ffx-hooks] RonsoMana install base=0x%08X gate=%d grey=%d drain=%d logOnly=%d poolMax=%u gateMin=%u drainCost=%u hudSafe=26",
        static_cast<unsigned>(base),
        enableGate ? 1 : 0,
        enableGreyout ? 1 : 0,
        enableDrain ? 1 : 0,
        logOnly ? 1 : 0,
        static_cast<unsigned>(g_poolMax),
        static_cast<unsigned>(g_gateMinCost),
        static_cast<unsigned>(g_drainCost));

    bool ok = true;

#ifdef FFXHOOKS_HAVE_POLYHOOK
    /* hudSafe=24: PERSISTENT gauge-full pin (max:=charge while charge is usable) replaces the
     * transient spoof. FFX re-checks "OD usable == charge==max" PER-FRAME in the renderer
     * (outside our hooks); the old restore-to-255 right after each trampoline is what kept LEFT
     * blocked at partial charge. Below threshold the real 0-255 pool is handed back. */
    HookLog("[ffx-hooks] RonsoMana hudSafe=26 CRASH HOTFIX: blob write opt-in (FFXHOOKS_RONSO_OD_BLOBWRITE=1, safe nodes only) + SEH-guarded node reads + ODBLOB dump");
    if (enableGate) {
        const uint64_t pushVa = reinterpret_cast<uint64_t>(&PushMenuTreeEntry_Shim);
        if (!InstallDetour(base, RVA_FFX_BATTLE_UI_PUSH_MENU_TREE_ENTRY, pushVa, &g_pushMenuTreeDetour, &g_pushMenuTreeTrampolineVa, "B0-push")) {
            ok = false;
        } else {
            g_pushMenuTreeTrampoline = reinterpret_cast<PushMenuTreeEntryFn>(static_cast<uintptr_t>(g_pushMenuTreeTrampolineVa));
        }
        const uint64_t resolveVa = reinterpret_cast<uint64_t>(&ResolveMenuTreeNode_Shim);
        if (!InstallDetour(base, RVA_FFX_BATTLE_UI_RESOLVE_MENU_TREE_NODE, resolveVa, &g_resolveMenuTreeDetour, &g_resolveMenuTreeTrampolineVa, "B0-resolve")) {
            ok = false;
        } else {
            g_resolveMenuTreeTrampoline = reinterpret_cast<ResolveMenuTreeNodeFn>(static_cast<uintptr_t>(g_resolveMenuTreeTrampolineVa));
        }
        const uint64_t ringVa = reinterpret_cast<uint64_t>(&BuildCommandRing_Shim);
        if (!InstallDetour(base, RVA_FFX_BATTLE_UI_BUILD_COMMAND_RING, ringVa, &g_buildCommandRingDetour, &g_buildCommandRingTrampolineVa, "G0-ring")) {
            ok = false;
        } else {
            g_buildCommandRingTrampoline = reinterpret_cast<BuildCommandRingFn>(static_cast<uintptr_t>(g_buildCommandRingTrampolineVa));
        }
        const uint64_t dispatchVa = reinterpret_cast<uint64_t>(&BattleMenuInputDispatch_Shim);
        if (!InstallDetour(base, RVA_FFX_BATTLE_MENU_INPUT_DISPATCH, dispatchVa, &g_menuInputDispatchDetour, &g_menuInputDispatchTrampolineVa, "P0-dispatch")) {
            ok = false;
        } else {
            g_menuInputDispatchTrampoline = reinterpret_cast<MenuInputDispatchFn>(static_cast<uintptr_t>(g_menuInputDispatchTrampolineVa));
        }
        const uint64_t odReadyVa = reinterpret_cast<uint64_t>(&IsOverdriveReady_Shim);
        if (!InstallDetour(base, RVA_FFX_BATTLE_IS_OVERDRIVE_READY_MENU, odReadyVa, &g_isOverdriveReadyDetour, &g_isOverdriveReadyTrampolineVa, "G0-odReady")) {
            ok = false;
        } else {
            g_isOverdriveReadyTrampoline = reinterpret_cast<IsOverdriveReadyFn>(static_cast<uintptr_t>(g_isOverdriveReadyTrampolineVa));
        }
        const uint64_t syncTplVa = reinterpret_cast<uint64_t>(&CopyMenuTemplate_Shim);
        if (!InstallDetour(base, RVA_FFX_BATTLE_SYNC_COMMAND_MENU_TEMPLATE, syncTplVa, &g_copyMenuTemplateDetour, &g_copyMenuTemplateTrampolineVa, "G0-syncTpl")) {
            ok = false;
        } else {
            g_copyMenuTemplateTrampoline = reinterpret_cast<CopyMenuTemplateFn>(static_cast<uintptr_t>(g_copyMenuTemplateTrampolineVa));
        }
        const uint64_t hasSaveVa = reinterpret_cast<uint64_t>(&HasCommandSave_Shim);
        if (!InstallDetour(base, RVA_FFX_BATTLE_HAS_COMMAND_BIT_SAVE, hasSaveVa, &g_hasCommandSaveDetour, &g_hasCommandSaveTrampolineVa, "G0-saveHasCmd")) {
            ok = false;
        } else {
            g_hasCommandSaveTrampoline = reinterpret_cast<HasCommandSaveFn>(static_cast<uintptr_t>(g_hasCommandSaveTrampolineVa));
        }
        const uint64_t hasCmdVa = reinterpret_cast<uint64_t>(&HasCommandBit_Shim);
        if (!InstallDetour(base, RVA_FFX_BATTLE_HAS_COMMAND_BIT, hasCmdVa, &g_hasCommandBitDetour, &g_hasCommandBitTrampolineVa, "G0-hasCmd")) {
            ok = false;
        } else {
            g_hasCommandBitTrampoline = reinterpret_cast<HasCommandBitFn>(static_cast<uintptr_t>(g_hasCommandBitTrampolineVa));
        }
        const uint64_t routeVa = reinterpret_cast<uint64_t>(&MenuRoute_Shim);
        if (!InstallDetour(base, RVA_FFX_BATTLE_MENU_ROUTE_SWITCH_VS_OD, routeVa, &g_menuRouteDetour, &g_menuRouteTrampolineVa, "G1-route")) {
            ok = false;
        } else {
            g_menuRouteTrampoline = reinterpret_cast<MenuRouteFn>(static_cast<uintptr_t>(g_menuRouteTrampolineVa));
        }
        const uint64_t openVa = reinterpret_cast<uint64_t>(&OpenSubmenu_Shim);
        if (!InstallDetour(base, RVA_FFX_BATTLE_UI_OPEN_SUBMENU, openVa, &g_openSubmenuDetour, &g_openSubmenuTrampolineVa, "G1-open")) {
            ok = false;
        } else {
            g_openSubmenuTrampoline = reinterpret_cast<OpenSubmenuFn>(static_cast<uintptr_t>(g_openSubmenuTrampolineVa));
        }
        const uint64_t menuShimVa = reinterpret_cast<uint64_t>(&BuildMenu_Shim);
        if (!InstallDetour(base, RVA_FFX_BATTLE_BUILD_ACTOR_COMMAND_MENU, menuShimVa, &g_menuBuildDetour, &g_menuBuildTrampolineVa, "G0-menu")) {
            ok = false;
        } else {
            g_menuBuildTrampoline = reinterpret_cast<BuildMenuFn>(static_cast<uintptr_t>(g_menuBuildTrampolineVa));
            result.stubGate = base + RVA_FFX_BATTLE_BUILD_ACTOR_COMMAND_MENU;
        }
        const uint64_t refreshVa = reinterpret_cast<uint64_t>(&RefreshMenu_Shim);
        if (!InstallDetour(base, RVA_FFX_BATTLE_REFRESH_ACTOR_MENU, refreshVa, &g_refreshMenuDetour, &g_refreshMenuTrampolineVa, "G0-refresh")) {
            ok = false;
        } else {
            g_refreshMenuTrampoline = reinterpret_cast<RefreshMenuFn>(static_cast<uintptr_t>(g_refreshMenuTrampolineVa));
        }
    }
    if (enableGreyout) {
        const uint64_t shimVa = reinterpret_cast<uint64_t>(&SubmenuRefresh_Shim);
        if (!InstallDetour(base, RVA_FFX_BATTLE_SUBMENU_REFRESH, shimVa, &g_submenuRefreshDetour, &g_submenuRefreshTrampolineVa, "G3-refresh")) {
            ok = false;
        } else {
            g_submenuRefreshTrampoline = reinterpret_cast<SubmenuRefreshFn>(static_cast<uintptr_t>(g_submenuRefreshTrampolineVa));
            result.stubGreyout = base + RVA_FFX_BATTLE_SUBMENU_REFRESH;
        }
        /* G4: read-only diag detour on the per-command usability gate (sub_78ABE0). Pure logging,
         * never alters the gate result — captures WHY Ronso skills lock at partial charge. */
        const uint64_t cmdGateVa = reinterpret_cast<uint64_t>(&CmdUsabilityGate_Shim);
        if (!InstallDetour(base, RVA_FFX_BTL_CMD_USABILITY_GATE, cmdGateVa, &g_cmdGateDetour, &g_cmdGateTrampolineVa, "G4-cmdGate")) {
            ok = false;
        } else {
            g_cmdGateTrampoline = reinterpret_cast<CmdUsabilityGateFn>(static_cast<uintptr_t>(g_cmdGateTrampolineVa));
        }
        /* G5: read-only diag on the confirm handler (sub_792260) — the only caller of the gate.
         * Captures WHY the gate is skipped at partial charge (src[3]/src[4]/actor guards). */
        const uint64_t confirmVa = reinterpret_cast<uint64_t>(&MenuConfirm_Shim);
        if (!InstallDetour(base, RVA_FFX_BTL_MENU_CONFIRM_HANDLER, confirmVa, &g_confirmDetour, &g_confirmTrampolineVa, "G5-confirm")) {
            ok = false;
        } else {
            g_confirmTrampoline = reinterpret_cast<MenuConfirmFn>(static_cast<uintptr_t>(g_confirmTrampolineVa));
        }
    }
#else
    if (enableGate || enableGreyout) {
        HookLog("[ffx-hooks] WARN RonsoMana G0/G3' need PolyHook build — gate/greyout skipped");
        ok = false;
    }
#endif

    if (enableDrain) {
        if (!InstallDrainPatch(base)) {
            ok = false;
        } else {
            result.stubDrain = g_drainPatchVa;
        }
    }

    if (!ok) {
        RemoveRonsoManaHook(log);
        return result;
    }

    g_installed = true;
    result.ok = true;
    HookLog(
        "[ffx-hooks] RonsoMana installed ok gate=0x%08X grey=0x%08X drain=0x%08X",
        static_cast<unsigned>(result.stubGate),
        static_cast<unsigned>(result.stubGreyout),
        static_cast<unsigned>(result.stubDrain));
    return result;
}

bool RemoveRonsoManaHook(RonsoManaLogFn log) {
    if (!g_installed) return true;

    g_setCommandBit = nullptr;
#ifdef FFXHOOKS_HAVE_POLYHOOK
    RemoveDetour(g_hasCommandSaveDetour, "G0-saveHasCmd");
    g_hasCommandSaveDetour = nullptr;
    g_hasCommandSaveTrampoline = nullptr;
    RemoveDetour(g_copyMenuTemplateDetour, "G0-syncTpl");
    g_copyMenuTemplateDetour = nullptr;
    g_copyMenuTemplateTrampoline = nullptr;
    RemoveDetour(g_buildCommandRingDetour, "G0-ring");
    g_buildCommandRingDetour = nullptr;
    g_buildCommandRingTrampoline = nullptr;
    RemoveDetour(g_resolveMenuTreeDetour, "B0-resolve");
    g_resolveMenuTreeDetour = nullptr;
    g_resolveMenuTreeTrampoline = nullptr;
    RemoveDetour(g_pushMenuTreeDetour, "B0-push");
    g_pushMenuTreeDetour = nullptr;
    g_pushMenuTreeTrampoline = nullptr;
    g_menuTreeReset = nullptr;
    g_finishMenuTree = nullptr;
    RemoveDetour(g_menuInputDispatchDetour, "P0-dispatch");
    g_menuInputDispatchDetour = nullptr;
    g_menuInputDispatchTrampoline = nullptr;
    RemoveDetour(g_isOverdriveReadyDetour, "G0-odReady");
    g_isOverdriveReadyDetour = nullptr;
    g_isOverdriveReadyTrampoline = nullptr;
    RemoveDetour(g_hasCommandBitDetour, "G0-hasCmd");
    g_hasCommandBitDetour = nullptr;
    g_hasCommandBitTrampoline = nullptr;
    RemoveDetour(g_menuRouteDetour, "G1-route");
    g_menuRouteDetour = nullptr;
    g_menuRouteTrampoline = nullptr;
    RemoveDetour(g_openSubmenuDetour, "G1-open");
    g_openSubmenuDetour = nullptr;
    g_openSubmenuTrampoline = nullptr;
    RemoveDetour(g_refreshMenuDetour, "G0-refresh");
    g_refreshMenuDetour = nullptr;
    g_refreshMenuTrampoline = nullptr;
    RemoveDetour(g_menuBuildDetour, "G0-menu");
    g_menuBuildDetour = nullptr;
    g_menuBuildTrampoline = nullptr;
    RemoveDetour(g_submenuRefreshDetour, "G3-refresh");
    g_submenuRefreshDetour = nullptr;
    g_submenuRefreshTrampoline = nullptr;
    RemoveDetour(g_cmdGateDetour, "G4-cmdGate");
    g_cmdGateDetour = nullptr;
    g_cmdGateTrampoline = nullptr;
    RemoveDetour(g_confirmDetour, "G5-confirm");
    g_confirmDetour = nullptr;
    g_confirmTrampoline = nullptr;
    RemoveDetour(g_gateDetour, "G1-gate");
    g_gateDetour = nullptr;
    g_gateTrampoline = nullptr;
    RemoveDetour(g_greyoutDetour, "G3-greyout");
    g_greyoutDetour = nullptr;
    g_greyoutTrampoline = nullptr;
#endif

    const bool drainOk = RemoveDrainPatch();
    if (log) {
        log(drainOk
            ? "[ffx-hooks] RonsoMana removed"
            : "[ffx-hooks] WARN RonsoMana drain patch restore FAILED");
    }

    g_installed = false;
    g_logFn = nullptr;
    g_base = 0;
    g_getCharge = nullptr;
    g_getMax = nullptr;
    g_getActor = nullptr;
    return drainOk;
}

bool IsRonsoManaHookInstalled() {
    return g_installed;
}

} // namespace FfxHooks
