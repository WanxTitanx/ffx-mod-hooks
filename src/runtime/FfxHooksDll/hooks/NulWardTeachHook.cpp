#include "NulWardTeachHook.h"
#include "../shared/ffx_addresses.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <vector>

#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <polyhook2/Detour/x86Detour.hpp>
#include <exception>
#endif

namespace FfxHooks {

namespace {

using GrantCommandFn = int(__cdecl*)(int charIdx, int cmdId, int on);
using GetActorByIndexFn = void*(__cdecl*)(uint8_t actorIndex);
using PrepareSaveCmdStateFn = int(__cdecl*)();
using GetCmdEntryFn = void*(__cdecl*)(int cmdId, int zero);
using IsCmdAvailFn  = int(__cdecl*)(uint8_t charIdx, uint16_t cmdId);
using BuildMenuFn   = int(__cdecl*)(int charIdx, int actorRecord);

static bool                 g_installed = false;
static bool                 g_grantOnLoad = false;
static bool                 g_menuPatched = false;
static NulWardTeachLogFn    g_logFn = nullptr;
static uintptr_t            g_base = 0;
static uintptr_t            g_menuPatchVa = 0;   // first patched site (result compat)
static const int            kMaxMenuPatchSites = 8;
static uintptr_t            g_menuPatchSites[kMaxMenuPatchSites] = {};
static uint8_t              g_menuPatchSaved[kMaxMenuPatchSites][4] = {};
static int                  g_menuPatchCount = 0;
static GrantCommandFn       g_grant = nullptr;
static GetActorByIndexFn    g_getActor = nullptr;
static volatile LONG        g_grantOnce = 0;

// Party-wide command-bank persistence detour. FFX_Btl_PrepareSaveCommandState reloads the
// whole bank from the `party_data` kernel at EVERY battle init, wiping the one-shot startup
// grant of ids 320/321 (RE verdict 2026-06-16). We re-assert the ward bits AFTER it returns,
// just before FFX_Btl_BuildActorCommandMenu seeds each actor — so IsCommandAvailable(320/321)
// is true and the wards reach the White-magic submenu.
#ifdef FFXHOOKS_HAVE_POLYHOOK
static PLH::x86Detour*      g_prepDetour = nullptr;
static uint64_t             g_prepTrampoline = 0;
static volatile LONG        g_prepFireCount = 0;
#endif

// Menu surfacing FIX + verify detour (Jarvis-MAGIC 2026-06-16). RE PROOF (diag run): word14 of
// g_PartyWideCommandBank is reliably 0 by the time ANY FFX_Btl_BuildActorCommandMenu seeds an
// actor (avail=(0,0) for all 18 actors despite the PrepareSaveCommandState reassert logging
// 0x0000->0x0003). Cause: sub_7817D0 reloads/zeros the bank between PrepareSaveCommandState
// (0x781c20) and the menu builds (InitPartyWideCommandBank @0x781d09 + per-open
// RefreshActorMenuState), so re-asserting only at PrepareSaveCommandState never reaches the seed.
// FIX: OR the ward bits into the bank in this detour's PRE-call window — BuildActorCommandMenu's
// first act is to copy the bank into the actor avail mask, so the seed sees word14=0x0003 and
// IsCommandAvailable(320/321) becomes true for every actor whose menu is (re)built. The post-call
// probe verifies availability + White-magic placement. Disabled by RemoveNulWardTeachHook.
// Ronso's BuildActorCommandMenu gate must remain disarmed while active (shared entry).
static GetCmdEntryFn        g_getCmdEntry = nullptr;
static IsCmdAvailFn         g_isCmdAvail = nullptr;
#ifdef FFXHOOKS_HAVE_POLYHOOK
static PLH::x86Detour*      g_buildDetour = nullptr;
static uint64_t             g_buildTrampoline = 0;
static volatile LONG        g_buildDiagTotal = 0;
static int                  g_buildDiagPerChar[18] = {};
#endif

static void HookLog(const char* fmt, ...) {
    if (!g_logFn) return;
    char line[512] = {};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    g_logFn(line);
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

// IDA-proven (FFX_recon.i64, 2026-06-16, docs/reverse/FFX_NUL_WARD_TEACH_SURFACE_RE_VERDICT_2026-06-16.md):
// FFX_Btl_BuildActorCommandMenu has THREE `cmp r32, 140h` (320): two on esi (81 FE; aggregate loops)
// and one on edi (81 FF; the loop-2 PLACEMENT loop that inserts encoded ids into submenu slots).
// Only the placement loop controls whether ids 320/321 reach the White-magic submenu, so patching
// just the first match (the old behaviour) was a no-op for surfacing. Patch EVERY `cmp r32,140h`
// (and the `cmp eax,140h` form) so all loop bounds reach 322; iterating the 2 extra grown rows is
// harmless because rows 320/321 exist in the grown command.bin.
static bool TryPatchMenuBound(uintptr_t base) {
    const uintptr_t fn = base + RVA_FFX_BATTLE_BUILD_ACTOR_COMMAND_MENU;
    const size_t scanLen = 0x500;
    std::vector<uint8_t> bytes(scanLen);
    memcpy(bytes.data(), reinterpret_cast<const void*>(fn), scanLen);

    const uint32_t extended = FFX_BATTLE_MENU_COMMAND_ID_LIMIT_EXTENDED; // 322 (0x142)

    g_menuPatchCount = 0;
    for (size_t i = 0; i + 6 <= scanLen && g_menuPatchCount < kMaxMenuPatchSites; ++i) {
        uintptr_t immVa = 0;
        // cmp eax, 140h -> 3D 40 01 00 00
        if (bytes[i] == 0x3D && bytes[i + 1] == 0x40 && bytes[i + 2] == 0x01 && bytes[i + 3] == 0x00 && bytes[i + 4] == 0x00) {
            immVa = fn + i + 1;
        }
        // cmp r32, 140h -> 81 (F8..FF) 40 01 00 00 (esi=FE aggregate loops, edi=FF placement loop)
        else if (bytes[i] == 0x81 && (bytes[i + 1] & 0xF8) == 0xF8
            && bytes[i + 2] == 0x40 && bytes[i + 3] == 0x01 && bytes[i + 4] == 0x00 && bytes[i + 5] == 0x00) {
            immVa = fn + i + 2;
        }
        if (immVa) {
            const int idx = g_menuPatchCount;
            memcpy(g_menuPatchSaved[idx], reinterpret_cast<const void*>(immVa), 4);
            if (MemWrite(reinterpret_cast<void*>(immVa), &extended, sizeof(uint32_t))) {
                g_menuPatchSites[idx] = immVa;
                if (idx == 0) g_menuPatchVa = immVa;
                ++g_menuPatchCount;
                HookLog("[ffx-hooks] NulWardTeach menu bound site#%d patched @0x%08X 0x140->0x%X",
                    idx, static_cast<unsigned>(immVa), static_cast<unsigned>(extended));
            }
        }
    }
    return g_menuPatchCount > 0;
}

// DEPRECATED: born-with grant to all 7 chars — use GridTeachHook (grid node activation only).
static void GrantWardsLab() {
    if (!g_grant || InterlockedCompareExchange(&g_grantOnce, 1, 0) != 0) return;
    HookLog("[ffx-hooks] NulWardTeach WARN grant-on-load DISABLED — use grid_teach.flag + activate a sphere node");
}

// Bank coordinate for a party-wide encoded command id (>=0x3060). Mirrors
// FFX_GrantCommandToCharacter (0x785D10): word = ((id-96)&0xFFF)/16, bit = (id-96)&0xF.
static inline void WardBankCoord(uint32_t encodedId, int& wordIdx, uint16_t& bitMask) {
    const uint32_t rel = (encodedId - 96u) & 0xFFFu;
    wordIdx = static_cast<int>(rel / 16u);
    bitMask = static_cast<uint16_t>(1u << (rel & 0xFu));
}

static inline volatile uint16_t* PartyBankWord(int wordIdx) {
    if (!g_base) return nullptr;
    return reinterpret_cast<volatile uint16_t*>(
        g_base + RVA_FFX_PARTY_WIDE_COMMAND_BANK + static_cast<uintptr_t>(wordIdx) * 2u);
}

// Directly OR the Radiant+Umbral bits back into g_PartyWideCommandBank. Direct write (NOT a
// grant call) is deliberate: g_grant() re-enters FFX_Btl_BuildActorCommandMenu when a battle
// player list exists, which is unsafe to trigger from inside the PrepareSaveCommandState
// return path. Reports pre/post of the (shared) ward word for diagnostics.
static void ReassertWardBankBits(uint16_t* outPre, uint16_t* outPost) {
    int wR, wU; uint16_t mR, mU;
    WardBankCoord(FFX_CMD_RADIANT_WARD_ENCODED, wR, mR);
    WardBankCoord(FFX_CMD_UMBRAL_WARD_ENCODED, wU, mU);
    volatile uint16_t* w = PartyBankWord(wR);
    if (!w) { if (outPre) *outPre = 0; if (outPost) *outPost = 0; return; }
    __try {
        const uint16_t pre = *w;
        uint16_t add = mR;
        if (wU == wR) add |= mU;            // Radiant+Umbral share word 14
        *w = static_cast<uint16_t>(pre | add);
        if (outPre) *outPre = pre;
        if (outPost) *outPost = *w;
        if (wU != wR) {                     // future-proof if ids ever split words
            volatile uint16_t* w2 = PartyBankWord(wU);
            if (w2) *w2 = static_cast<uint16_t>(*w2 | mU);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (outPre) *outPre = 0; if (outPost) *outPost = 0;
    }
}

#ifdef FFXHOOKS_HAVE_POLYHOOK
static int __cdecl PrepareSaveCommandState_Shim() {
    const int rv = reinterpret_cast<PrepareSaveCmdStateFn>(g_prepTrampoline)();
    uint16_t pre = 0, post = 0;
    ReassertWardBankBits(&pre, &post);
    const long n = InterlockedIncrement(&g_prepFireCount);
    if (n <= 4) {
        HookLog("[ffx-hooks] NulWardTeach reassert #%ld post-PrepareSaveCmdState: bank word14 0x%04X->0x%04X",
            n, static_cast<unsigned>(pre), static_cast<unsigned>(post));
    }
    return rv;
}

static int __cdecl BuildActorCommandMenu_Fix_Shim(int charIdx, int actorRecord) {
    // FIX (pre-call): make the ward bits live in the bank for the seed that runs at the very top
    // of the original — the only window that actually feeds IsCommandAvailable for THIS build.
    uint16_t bankPre = 0, bankPost = 0;
    ReassertWardBankBits(&bankPre, &bankPost);

    const int rv = reinterpret_cast<BuildMenuFn>(g_buildTrampoline)(charIdx, actorRecord);
    if (charIdx >= 0 && charIdx < 18
        && InterlockedIncrement(&g_buildDiagTotal) <= 160
        && g_buildDiagPerChar[charIdx] < 6) {
        ++g_buildDiagPerChar[charIdx];
        __try {
            int b24a = -1, b24b = -1;
            if (g_getCmdEntry) {
                uint8_t* e0 = reinterpret_cast<uint8_t*>(g_getCmdEntry(FFX_CMD_RADIANT_WARD_ID, 0));
                uint8_t* e1 = reinterpret_cast<uint8_t*>(g_getCmdEntry(FFX_CMD_UMBRAL_WARD_ID, 0));
                if (e0) b24a = e0[24];
                if (e1) b24b = e1[24];
            }
            int a0 = -1, a1 = -1;
            if (g_isCmdAvail) {
                a0 = g_isCmdAvail(static_cast<uint8_t>(charIdx), FFX_CMD_RADIANT_WARD_ID) & 1;
                a1 = g_isCmdAvail(static_cast<uint8_t>(charIdx), FFX_CMD_UMBRAL_WARD_ID) & 1;
            }
            uintptr_t ringBase =
                *reinterpret_cast<volatile uintptr_t*>(g_base + RVA_FFX_BATTLE_COMMAND_RING_BASE_PTR);
            int loc0 = -1, loc1 = -1, whiteUsed = 0;
            uint16_t w[24] = {};
            if (ringBase > 0x10000u) {
                const uintptr_t tree =
                    ringBase + static_cast<uintptr_t>(FFX_BATTLE_COMMAND_RING_SLOT_STRIDE) * static_cast<uintptr_t>(charIdx);
                const int offs[7] = { 40, 56, 72, 120, 168, 232, 296 };
                const int cnts[7] = {  8,  8, 24,  24,  32,  32,  24 };
                for (int b = 0; b < 7; ++b) {
                    const uint16_t* buf = reinterpret_cast<const uint16_t*>(tree + offs[b]);
                    for (int i = 0; i < cnts[b]; ++i) {
                        const uint16_t s = buf[i];
                        if (s == FFX_CMD_RADIANT_WARD_ENCODED) loc0 = offs[b];
                        if (s == FFX_CMD_UMBRAL_WARD_ENCODED)  loc1 = offs[b];
                    }
                }
                const uint16_t* wbuf = reinterpret_cast<const uint16_t*>(tree + 72);
                for (int i = 0; i < 24; ++i) {
                    w[i] = wbuf[i];
                    if (w[i] != 0x00FF && w[i] != 0xFFFF && w[i] != 0) ++whiteUsed;
                }
            }
            HookLog("[ffx-hooks] NulWardDiag ch=%d bank14=0x%04X->0x%04X avail=(%d,%d) b24=(0x%X,0x%X) ring=0x%IX loc=(%d,%d) whiteUsed=%d",
                charIdx, static_cast<unsigned>(bankPre), static_cast<unsigned>(bankPost),
                a0, a1, b24a, b24b, static_cast<size_t>(ringBase), loc0, loc1, whiteUsed);
            if (whiteUsed > 0 || loc0 >= 0 || loc1 >= 0) {
                HookLog("[ffx-hooks] NulWardDiag ch=%d white+72: %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X",
                    charIdx, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10], w[11],
                    w[12], w[13], w[14], w[15], w[16], w[17], w[18], w[19], w[20], w[21], w[22], w[23]);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            HookLog("[ffx-hooks] NulWardDiag ch=%d probe exception", charIdx);
        }
    }
    return rv;
}
#endif

} // namespace

NulWardTeachInstallResult InstallNulWardTeachHook(uintptr_t base, bool grantOnLoad, NulWardTeachLogFn log) {
    NulWardTeachInstallResult result = { false, 0, false };
    if (g_installed) {
        result.ok = true;
        result.menuBoundPatchVa = g_menuPatchVa;
        result.menuBoundPatched = g_menuPatched;
        return result;
    }

    g_logFn = log;
    g_base = base;
    g_grantOnLoad = grantOnLoad;
    g_grant = reinterpret_cast<GrantCommandFn>(base + RVA_FFX_GRANT_COMMAND_TO_CHARACTER);
    g_getActor = reinterpret_cast<GetActorByIndexFn>(base + RVA_FFX_BATTLE_GET_ACTOR_BY_INDEX);
    g_getCmdEntry = reinterpret_cast<GetCmdEntryFn>(base + RVA_FFX_KERNEL_GET_COMMAND_ENTRY_BY_ID);
    g_isCmdAvail = reinterpret_cast<IsCmdAvailFn>(base + RVA_FFX_BATTLE_HAS_COMMAND_BIT);

    g_menuPatched = TryPatchMenuBound(base);
    if (!g_menuPatched) {
        HookLog("[ffx-hooks] WARN NulWardTeach menu bound patch not found @0x%08X — manual DINPUT8 grant still works",
            static_cast<unsigned>(base + RVA_FFX_BATTLE_BUILD_ACTOR_COMMAND_MENU));
    } else {
        HookLog("[ffx-hooks] NulWardTeach menu bound patched sites=%d firstVa=0x%08X limit=%u (placement loop must be among them)",
            g_menuPatchCount,
            static_cast<unsigned>(g_menuPatchVa),
            static_cast<unsigned>(FFX_BATTLE_MENU_COMMAND_ID_LIMIT_EXTENDED));
    }

    if (g_grantOnLoad) {
        GrantWardsLab();
#ifdef FFXHOOKS_HAVE_POLYHOOK
        // Re-assert the ward bank bits after every battle-init bank reload (see RE verdict).
        const uint64_t prepVa = static_cast<uint64_t>(base + RVA_FFX_BTL_PREPARE_SAVE_COMMAND_STATE);
        try {
            g_prepDetour = new PLH::x86Detour(
                prepVa,
                reinterpret_cast<uint64_t>(&PrepareSaveCommandState_Shim),
                &g_prepTrampoline);
            if (g_prepDetour->hook()) {
                HookLog("[ffx-hooks] NulWardTeach reassert detour ok @0x%08X (PrepareSaveCmdState) tramp=0x%llX",
                    static_cast<unsigned>(prepVa), static_cast<unsigned long long>(g_prepTrampoline));
            } else {
                HookLog("[ffx-hooks] WARN NulWardTeach reassert detour hook() false @0x%08X — wards may not persist past battle init",
                    static_cast<unsigned>(prepVa));
                delete g_prepDetour; g_prepDetour = nullptr; g_prepTrampoline = 0;
            }
        } catch (const std::exception& ex) {
            HookLog("[ffx-hooks] NulWardTeach reassert detour exception: %s", ex.what());
            delete g_prepDetour; g_prepDetour = nullptr; g_prepTrampoline = 0;
        } catch (...) {
            HookLog("[ffx-hooks] NulWardTeach reassert detour unknown exception");
            delete g_prepDetour; g_prepDetour = nullptr; g_prepTrampoline = 0;
        }

        // Menu surfacing FIX detour: pre-call ward-bank reassert (+ post-call verify probe).
        // Ronso's BuildActorCommandMenu gate must stay disarmed while this is installed (shared entry).
        const uint64_t buildVa = static_cast<uint64_t>(base + RVA_FFX_BATTLE_BUILD_ACTOR_COMMAND_MENU);
        try {
            g_buildDetour = new PLH::x86Detour(
                buildVa,
                reinterpret_cast<uint64_t>(&BuildActorCommandMenu_Fix_Shim),
                &g_buildTrampoline);
            if (g_buildDetour->hook()) {
                HookLog("[ffx-hooks] NulWardTeach menu FIX detour ok @0x%08X (BuildActorCommandMenu) tramp=0x%llX",
                    static_cast<unsigned>(buildVa), static_cast<unsigned long long>(g_buildTrampoline));
            } else {
                HookLog("[ffx-hooks] WARN NulWardTeach menu FIX detour hook() false @0x%08X",
                    static_cast<unsigned>(buildVa));
                delete g_buildDetour; g_buildDetour = nullptr; g_buildTrampoline = 0;
            }
        } catch (const std::exception& ex) {
            HookLog("[ffx-hooks] NulWardTeach menu FIX detour exception: %s", ex.what());
            delete g_buildDetour; g_buildDetour = nullptr; g_buildTrampoline = 0;
        } catch (...) {
            HookLog("[ffx-hooks] NulWardTeach menu FIX detour unknown exception");
            delete g_buildDetour; g_buildDetour = nullptr; g_buildTrampoline = 0;
        }
#else
        HookLog("[ffx-hooks] WARN NulWardTeach built without PolyHook — no battle-init reassert; wards wiped each battle");
#endif
    }

    g_installed = true;
    result.ok = true;
    result.menuBoundPatchVa = g_menuPatchVa;
    result.menuBoundPatched = g_menuPatched;
    return result;
}

bool RemoveNulWardTeachHook(NulWardTeachLogFn log) {
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if (g_prepDetour) { g_prepDetour->unHook(); delete g_prepDetour; g_prepDetour = nullptr; }
    g_prepTrampoline = 0;
    g_prepFireCount = 0;
    if (g_buildDetour) { g_buildDetour->unHook(); delete g_buildDetour; g_buildDetour = nullptr; }
    g_buildTrampoline = 0;
    g_buildDiagTotal = 0;
    memset(g_buildDiagPerChar, 0, sizeof(g_buildDiagPerChar));
#endif
    g_getCmdEntry = nullptr;
    g_isCmdAvail = nullptr;
    for (int i = 0; i < g_menuPatchCount; ++i) {
        if (g_menuPatchSites[i])
            MemWrite(reinterpret_cast<void*>(g_menuPatchSites[i]), g_menuPatchSaved[i], 4);
    }
    g_menuPatched = false;
    g_menuPatchVa = 0;
    g_menuPatchCount = 0;
    memset(g_menuPatchSites, 0, sizeof(g_menuPatchSites));
    memset(g_menuPatchSaved, 0, sizeof(g_menuPatchSaved));
    g_installed = false;
    g_grantOnLoad = false;
    g_grant = nullptr;
    g_getActor = nullptr;
    g_grantOnce = 0;
    g_logFn = nullptr;
    g_base = 0;
    if (log) log("[ffx-hooks] NulWardTeach removed ok");
    return true;
}

bool IsNulWardTeachHookInstalled() {
    return g_installed;
}

} // namespace FfxHooks
