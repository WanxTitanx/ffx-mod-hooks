#include "NulWardHook.h"
#include "../shared/ffx_addresses.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <vector>
#include <unordered_map>

#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <polyhook2/Detour/x86Detour.hpp>
#endif

namespace FfxHooks {

namespace {

constexpr size_t kWritebackPatchLen = 5;

static const uint8_t kExpectedWritebackPatch[kWritebackPatchLen] = {
    0x89, 0x06, 0x01, 0x81, 0x50,
};

using GetActorByIndexFn = void*(__cdecl*)(uint8_t actorIndex);
using ApplyHitDamageLoopFn = int(__cdecl*)(
    int i,
    int actorRecord,
    int n,
    int a4,
    int a5,
    int n12320,
    unsigned char* a7,
    uint32_t* a8,
    int* a9,
    unsigned char** a10,
    int a11);
using ActionAftermathFn = int(__cdecl*)(int i, int a2, int ia, int* a4, int16_t a5);

static bool                 g_installed         = false;
static bool                 g_applyBlocks       = false;
static bool                 g_logEvents         = false;
static bool                 g_nativeSlots       = false;
static bool                 g_experimentP16     = false;
static bool                 g_p16Apply          = false;
static NulWardLogFn         g_logFn             = nullptr;
static uintptr_t            g_base              = 0;
static GetActorByIndexFn    g_getActor          = nullptr;

static std::unordered_map<uintptr_t, uint8_t> g_holyBlocks;
static std::unordered_map<uintptr_t, uint8_t> g_darkBlocks;

static uint8_t              g_savedWriteback[kWritebackPatchLen] = {};
static uint8_t*             g_writebackStub                    = nullptr;
static size_t               g_writebackStubLen                 = 0;
static uintptr_t            g_writebackPatchVa                 = 0;
static uintptr_t            g_writebackResumeVa                = 0;

static volatile LONG        g_castLogCount  = 0;
static volatile LONG        g_nullLogCount  = 0;
static volatile LONG        g_probeLogCount = 0;   // diag 2026-06-16: log EVERY writeback hit
static thread_local int32_t g_tlsEncodedCmd = 0;

#ifdef FFXHOOKS_HAVE_POLYHOOK
static ApplyHitDamageLoopFn g_hitLoopTrampoline     = nullptr;
static ActionAftermathFn    g_aftermathTrampoline   = nullptr;
static PLH::x86Detour*      g_hitLoopDetour         = nullptr;
static PLH::x86Detour*      g_aftermathDetour       = nullptr;
static PLH::x86Detour*      g_precheckDetour        = nullptr;
static uint64_t             g_hitLoopTrampolineVa   = 0;
static uint64_t             g_aftermathTrampolineVa = 0;
static uint64_t             g_precheckTrampolineVa  = 0;

using ResolveHitPrecheckFn = int(__cdecl*)(
    int targetIdx,
    int n8,
    int n2,
    int n3,
    int n100_1,
    int n100,
    int* p_n10000,
    uint32_t* a8,
    uint32_t* a9,
    int a10,
    char a11,
    int a12,
    int16_t a13,
    int a14,
    int* a15);

static ResolveHitPrecheckFn g_precheckTrampoline = nullptr;
static volatile LONG        g_p16LogCount = 0;
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

static void* ResolveActor(uint8_t actorIndex) {
    if (!g_getActor) return nullptr;
    return g_getActor(actorIndex);
}

static uint8_t GetHolyBlock(void* actor) {
    if (!actor) return 0;
    if (g_nativeSlots) {
        return reinterpret_cast<const uint8_t*>(actor)[FFX_BATTLE_ACTOR_NUL_HOLY_BLOCK_OFF];
    }
    const uintptr_t key = reinterpret_cast<uintptr_t>(actor);
    const auto it = g_holyBlocks.find(key);
    return it != g_holyBlocks.end() ? it->second : 0;
}

static uint8_t GetDarkBlock(void* actor) {
    if (!actor) return 0;
    if (g_nativeSlots) {
        return reinterpret_cast<const uint8_t*>(actor)[FFX_BATTLE_ACTOR_NUL_DARK_BLOCK_OFF];
    }
    const uintptr_t key = reinterpret_cast<uintptr_t>(actor);
    const auto it = g_darkBlocks.find(key);
    return it != g_darkBlocks.end() ? it->second : 0;
}

static void SetHolyBlock(void* actor, uint8_t value) {
    if (!actor) return;
    if (g_nativeSlots) {
        reinterpret_cast<uint8_t*>(actor)[FFX_BATTLE_ACTOR_NUL_HOLY_BLOCK_OFF] = value;
        return;
    }
    const uintptr_t key = reinterpret_cast<uintptr_t>(actor);
    if (value == 0) {
        g_holyBlocks.erase(key);
    } else {
        g_holyBlocks[key] = value;
    }
}

static void SetDarkBlock(void* actor, uint8_t value) {
    if (!actor) return;
    if (g_nativeSlots) {
        reinterpret_cast<uint8_t*>(actor)[FFX_BATTLE_ACTOR_NUL_DARK_BLOCK_OFF] = value;
        return;
    }
    const uintptr_t key = reinterpret_cast<uintptr_t>(actor);
    if (value == 0) {
        g_darkBlocks.erase(key);
    } else {
        g_darkBlocks[key] = value;
    }
}

static uint16_t NormalizeEncodedCommand(uint16_t raw) {
    if (raw == FFX_CMD_RADIANT_WARD_ENCODED || raw == FFX_CMD_UMBRAL_WARD_ENCODED) {
        return raw;
    }
    const uint16_t low = raw & 0x0FFFu;
    if (low == FFX_CMD_RADIANT_WARD_ID) return FFX_CMD_RADIANT_WARD_ENCODED;
    if (low == FFX_CMD_UMBRAL_WARD_ID) return FFX_CMD_UMBRAL_WARD_ENCODED;
    return 0;
}

static uint16_t ReadEncodedCmdFromActionEntry(const uint8_t* entry) {
    if (!entry) return 0;

    const uint16_t primary = *reinterpret_cast<const uint16_t*>(entry + FFX_BATTLE_ACTION_ENCODED_CMD_OFF);
    uint16_t encoded = NormalizeEncodedCommand(primary);
    if (encoded != 0) {
        return encoded;
    }

    const uint16_t legacy = *reinterpret_cast<const uint16_t*>(entry + FFX_BATTLE_ACTION_ENCODED_CMD_LEGACY_OFF);
    return NormalizeEncodedCommand(legacy);
}

static uint16_t ReadEncodedCommandFromActor(uint8_t actorIndex) {
    if (!g_base || !g_getActor) return 0;
    const auto* actor = reinterpret_cast<const uint8_t*>(g_getActor(actorIndex));
    if (!actor) return 0;

    const uint8_t slot = actor[FFX_BATTLE_ACTOR_ACTION_SLOT_OFF];
    const uint8_t* entry = nullptr;
    if (slot == 0xFFu) {
        entry = actor + FFX_BATTLE_ACTOR_INLINE_ACTION_OFF;
    } else {
        entry = reinterpret_cast<const uint8_t*>(g_base + RVA_FFX_BATTLE_ACTION_POOL_BSS)
            + static_cast<size_t>(slot) * FFX_BATTLE_ACTION_POOL_STRIDE;
    }

    return ReadEncodedCmdFromActionEntry(entry);
}

static void ClearWrongVanillaBlockBytes(void* actor, uint16_t encodedCmd) {
    if (!actor || !g_applyBlocks) return;
    auto* bytes = reinterpret_cast<uint8_t*>(actor);
    if (encodedCmd == FFX_CMD_RADIANT_WARD_ENCODED) {
        bytes[FFX_BATTLE_ACTOR_NUL_TIDE_BLOCK_OFF] = 0;
    } else if (encodedCmd == FFX_CMD_UMBRAL_WARD_ENCODED) {
        bytes[FFX_BATTLE_ACTOR_NUL_SHOCK_BLOCK_OFF] = 0;
    }
}

static void ApplyWardBlockToTarget(void* target, uint16_t encodedCmd, uint8_t attackerIdx, uint8_t targetIdx) {
    if (!target || encodedCmd == 0) return;

    const LONG n = InterlockedIncrement(&g_castLogCount);
    const bool shouldLog = g_logEvents && g_logFn && n <= 128;

    if (encodedCmd == FFX_CMD_RADIANT_WARD_ENCODED) {
        if (g_applyBlocks) {
            SetHolyBlock(target, 1);
            ClearWrongVanillaBlockBytes(target, encodedCmd);
        }
        if (shouldLog) {
            HookLog(
                "[ffx-hooks] NulWard cast #%ld Radiant att=%u tgt=%u holyBlock=%u apply=%d actor=0x%08X",
                static_cast<long>(n),
                static_cast<unsigned>(attackerIdx),
                static_cast<unsigned>(targetIdx),
                static_cast<unsigned>(GetHolyBlock(target)),
                g_applyBlocks ? 1 : 0,
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(target)));
        }
        return;
    }

    if (encodedCmd == FFX_CMD_UMBRAL_WARD_ENCODED) {
        if (g_applyBlocks) {
            SetDarkBlock(target, 1);
            ClearWrongVanillaBlockBytes(target, encodedCmd);
        }
        if (shouldLog) {
            HookLog(
                "[ffx-hooks] NulWard cast #%ld Umbral att=%u tgt=%u darkBlock=%u apply=%d actor=0x%08X",
                static_cast<long>(n),
                static_cast<unsigned>(attackerIdx),
                static_cast<unsigned>(targetIdx),
                static_cast<unsigned>(GetDarkBlock(target)),
                g_applyBlocks ? 1 : 0,
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(target)));
        }
    }
}

static void OnAfterAction(uint8_t attackerIdx, uint8_t targetIdx) {
    uint16_t encodedCmd = ReadEncodedCommandFromActor(attackerIdx);
    if (encodedCmd == 0 && g_tlsEncodedCmd != 0) {
        encodedCmd = NormalizeEncodedCommand(static_cast<uint16_t>(g_tlsEncodedCmd & 0xFFFF));
    }
    if (encodedCmd == 0) return;

    void* target = ResolveActor(targetIdx);
    ApplyWardBlockToTarget(target, encodedCmd, attackerIdx, targetIdx);
}

extern "C" int32_t __cdecl NulWard_ApplyNullBlocks(
    int32_t damage,
    void* targetPlus6E4,
    int32_t encodedCmd,
    int32_t elemFlags) {
    if (!targetPlus6E4) return damage;

    void* actor = reinterpret_cast<uint8_t*>(targetPlus6E4) - 0x6E4u;
    const uint32_t elem = static_cast<uint32_t>(elemFlags);
    const bool holyHit = (elem & FFX_ELEM_HOLY) != 0;
    const bool darkHit = (elem & FFX_ELEM_DARK) != 0;

    // DIAG (2026-06-16): log EVERY hit reaching the writeback (not only consumed ones), so a
    // failed block can be attributed precisely: writeback-not-on-path (no probe line during the
    // hit), element-not-detected (holyHit/darkHit=0), or block-absent-on-this-actor (blk=0 =>
    // actor-ptr mismatch vs the cast that set it). Throttled.
    if (g_logEvents && g_logFn) {
        const LONG pn = InterlockedIncrement(&g_probeLogCount);
        if (pn <= 64) {
            HookLog(
                "[ffx-hooks] NulWard probe #%ld dmg=%d elem=0x%X holyHit=%d darkHit=%d holyBlk=%u darkBlk=%u cmd=0x%04X actor=0x%08X",
                static_cast<long>(pn),
                damage,
                elem,
                holyHit ? 1 : 0,
                darkHit ? 1 : 0,
                static_cast<unsigned>(GetHolyBlock(actor)),
                static_cast<unsigned>(GetDarkBlock(actor)),
                static_cast<unsigned>(encodedCmd & 0xFFFF),
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(actor)));
        }
    }

    uint8_t holyBefore = 0;
    uint8_t darkBefore = 0;
    bool consumed = false;
    int32_t outDamage = damage;

    if (holyHit && GetHolyBlock(actor) > 0) {
        holyBefore = GetHolyBlock(actor);
        if (g_applyBlocks) {
            SetHolyBlock(actor, 0);
            outDamage = 0;
        }
        consumed = true;
    } else if (darkHit && GetDarkBlock(actor) > 0) {
        darkBefore = GetDarkBlock(actor);
        if (g_applyBlocks) {
            SetDarkBlock(actor, 0);
            outDamage = 0;
        }
        consumed = true;
    }

    if (consumed && g_logEvents && g_logFn) {
        const LONG n = InterlockedIncrement(&g_nullLogCount);
        if (n <= 128) {
            HookLog(
                "[ffx-hooks] NulWard null #%ld cmd=0x%04X elem=0x%02X dmgIn=%d dmgOut=%d holy=%u->%u dark=%u->%u apply=%d actor=0x%08X",
                static_cast<long>(n),
                static_cast<unsigned>(encodedCmd & 0xFFFF),
                static_cast<unsigned>(elem & 0xFFu),
                damage,
                outDamage,
                static_cast<unsigned>(holyBefore),
                static_cast<unsigned>(GetHolyBlock(actor)),
                static_cast<unsigned>(darkBefore),
                static_cast<unsigned>(GetDarkBlock(actor)),
                g_applyBlocks ? 1 : 0,
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(actor)));
        }
    }

    return outDamage;
}

static bool BuildWritebackStub(uintptr_t resumeVa, uint8_t** outStub, size_t* outLen) {
    std::vector<uint8_t> code;
    const auto emit = [&](std::initializer_list<uint8_t> bytes) {
        code.insert(code.end(), bytes.begin(), bytes.end());
    };

    emit({ 0xFF, 0x75, static_cast<uint8_t>(FFX_BATTLE_COMPUTE_HIT_DAMAGE_ELEM_FLAGS_OFF) });
    emit({ 0xFF, 0x75, static_cast<uint8_t>(FFX_BATTLE_COMPUTE_HIT_DAMAGE_ARG_N12320) });
    emit({ 0x51 });
    emit({ 0x50 });
    emit({ 0xE8 });
    const size_t callSite = code.size();
    emit({ 0x00, 0x00, 0x00, 0x00 });
    emit({ 0x83, 0xC4, 0x10 });
    emit({ 0x89, 0x06 });
    emit({ 0x01, 0x81, 0x50, 0x06, 0x00, 0x00 });
    emit({ 0xE9 });
    const size_t jmpSite = code.size();
    emit({ 0x00, 0x00, 0x00, 0x00 });

    uint8_t* stub = static_cast<uint8_t*>(VirtualAlloc(
        nullptr,
        code.size(),
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (!stub) return false;
    memcpy(stub, code.data(), code.size());

    const uintptr_t applyVa = reinterpret_cast<uintptr_t>(&NulWard_ApplyNullBlocks);
    const int32_t callRel = static_cast<int32_t>(applyVa - (reinterpret_cast<uintptr_t>(stub) + callSite + 4));
    memcpy(stub + callSite, &callRel, sizeof(callRel));

    const int32_t jmpRel = static_cast<int32_t>(resumeVa - (reinterpret_cast<uintptr_t>(stub) + jmpSite + 4));
    memcpy(stub + jmpSite, &jmpRel, sizeof(jmpRel));

    FlushInstructionCache(GetCurrentProcess(), stub, code.size());
    *outStub = stub;
    *outLen = code.size();
    return true;
}

static bool InstallWritebackPatch(uintptr_t base) {
    g_writebackPatchVa = base + RVA_FFX_BATTLE_DAMAGE_WRITEBACK_PATCH;
    g_writebackResumeVa = base + RVA_FFX_BATTLE_DAMAGE_WRITEBACK_RESUME;

    uint8_t actual[kWritebackPatchLen] = {};
    memcpy(actual, reinterpret_cast<const void*>(g_writebackPatchVa), kWritebackPatchLen);
    if (memcmp(actual, kExpectedWritebackPatch, kWritebackPatchLen) != 0) {
        HookLog(
            "[ffx-hooks] ERROR NulWard writeback unexpected bytes @0x%08X: %02X %02X %02X %02X %02X",
            static_cast<unsigned>(g_writebackPatchVa),
            actual[0], actual[1], actual[2], actual[3], actual[4]);
        return false;
    }

    memcpy(g_savedWriteback, kExpectedWritebackPatch, kWritebackPatchLen);
    if (!BuildWritebackStub(g_writebackResumeVa, &g_writebackStub, &g_writebackStubLen)) {
        HookLog("[ffx-hooks] ERROR NulWard writeback stub alloc failed");
        return false;
    }

    const int32_t rel = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(g_writebackStub) - (g_writebackPatchVa + 5));
    const uint8_t jmpPatch[kWritebackPatchLen] = {
        0xE9,
        static_cast<uint8_t>(rel & 0xFF),
        static_cast<uint8_t>((rel >> 8) & 0xFF),
        static_cast<uint8_t>((rel >> 16) & 0xFF),
        static_cast<uint8_t>((rel >> 24) & 0xFF),
    };

    if (!MemWrite(reinterpret_cast<void*>(g_writebackPatchVa), jmpPatch, kWritebackPatchLen)) {
        HookLog("[ffx-hooks] ERROR NulWard writeback patch write failed @0x%08X", static_cast<unsigned>(g_writebackPatchVa));
        VirtualFree(g_writebackStub, 0, MEM_RELEASE);
        g_writebackStub = nullptr;
        return false;
    }

    HookLog(
        "[ffx-hooks] NulWard writeback installed patch@0x%08X resume@0x%08X apply=%d log=%d stub=0x%08X",
        static_cast<unsigned>(g_writebackPatchVa),
        static_cast<unsigned>(g_writebackResumeVa),
        g_applyBlocks ? 1 : 0,
        g_logEvents ? 1 : 0,
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(g_writebackStub)));
    return true;
}

static bool RemoveWritebackPatch() {
    bool restored = true;
    if (g_writebackPatchVa != 0 && g_savedWriteback[0] != 0) {
        restored = MemWrite(reinterpret_cast<void*>(g_writebackPatchVa), g_savedWriteback, kWritebackPatchLen);
    }
    if (g_writebackStub) {
        VirtualFree(g_writebackStub, 0, MEM_RELEASE);
        g_writebackStub = nullptr;
        g_writebackStubLen = 0;
    }
    g_writebackPatchVa = 0;
    g_writebackResumeVa = 0;
    memset(g_savedWriteback, 0, sizeof(g_savedWriteback));
    return restored;
}

#ifdef FFXHOOKS_HAVE_POLYHOOK

static int __cdecl ApplyHitDamageLoop_Shim(
    int i,
    int actorRecord,
    int n,
    int a4,
    int a5,
    int n12320,
    unsigned char* a7,
    uint32_t* a8,
    int* a9,
    unsigned char** a10,
    int a11) {
    g_tlsEncodedCmd = n12320;
    return g_hitLoopTrampoline(i, actorRecord, n, a4, a5, n12320, a7, a8, a9, a10, a11);
}

static int __cdecl ActionAftermath_Shim(int i, int a2, int ia, int* a4, int16_t a5) {
    const int result = g_aftermathTrampoline(i, a2, ia, a4, a5);
    if (g_logEvents || g_applyBlocks) {
        OnAfterAction(static_cast<uint8_t>(i), static_cast<uint8_t>(ia));
    }
    return result;
}

static bool PrecheckElemLooksHolyDark(int n8, int n2, const uint32_t* a8) {
    const uint32_t probe = static_cast<uint32_t>(n8) | static_cast<uint32_t>(n2);
    if ((probe & FFX_ELEM_HOLY) != 0 || (probe & FFX_ELEM_DARK) != 0) {
        return true;
    }
    if (a8 && ((*a8 & FFX_ELEM_HOLY) != 0 || (*a8 & FFX_ELEM_DARK) != 0)) {
        return true;
    }
    return false;
}

static int __cdecl ResolveHitPrecheck_Shim(
    int targetIdx,
    int n8,
    int n2,
    int n3,
    int n100_1,
    int n100,
    int* p_n10000,
    uint32_t* a8,
    uint32_t* a9,
    int a10,
    char a11,
    int a12,
    int16_t a13,
    int a14,
    int* a15) {
    const int vanilla = g_precheckTrampoline(
        targetIdx, n8, n2, n3, n100_1, n100, p_n10000, a8, a9, a10, a11, a12, a13, a14, a15);

    void* actor = ResolveActor(static_cast<uint8_t>(targetIdx & 0xFF));
    const uint8_t holy = GetHolyBlock(actor);
    const uint8_t dark = GetDarkBlock(actor);
    const bool elemHit = PrecheckElemLooksHolyDark(n8, n2, a8);

    if ((g_logEvents || g_experimentP16) && g_logFn && holy + dark > 0) {
        const LONG n = InterlockedIncrement(&g_p16LogCount);
        if (n <= 64) {
            HookLog(
                "[ffx-hooks] NulWard P16 #%ld tgt=%d vanilla=%d holy=%u dark=%u n8=%d n2=%d elemHit=%d",
                static_cast<long>(n),
                targetIdx,
                vanilla,
                static_cast<unsigned>(holy),
                static_cast<unsigned>(dark),
                n8,
                n2,
                elemHit ? 1 : 0);
        }
    }

    if (g_p16Apply && g_applyBlocks && elemHit && vanilla != 0) {
        if ((holy > 0 && (static_cast<uint32_t>(n8 | n2 | (a8 ? *a8 : 0)) & FFX_ELEM_HOLY) != 0)
            || (dark > 0 && (static_cast<uint32_t>(n8 | n2 | (a8 ? *a8 : 0)) & FFX_ELEM_DARK) != 0)) {
            if (holy > 0) SetHolyBlock(actor, 0);
            if (dark > 0) SetDarkBlock(actor, 0);
            if (g_logFn) {
                HookLog("[ffx-hooks] NulWard P16 apply null tgt=%d vanilla=%d->0", targetIdx, vanilla);
            }
            return 0;
        }
    }

    return vanilla;
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
            HookLog("[ffx-hooks] ERROR NulWard %s detour hook() failed @0x%08X", label, static_cast<unsigned>(targetVa));
            return false;
        }
        HookLog(
            "[ffx-hooks] NulWard %s detour ok target=0x%08X trampoline=0x%llX",
            label,
            static_cast<unsigned>(targetVa),
            static_cast<unsigned long long>(*trampolineOut));
        return true;
    } catch (...) {
        delete *detourOut;
        *detourOut = nullptr;
        *trampolineOut = 0;
        HookLog("[ffx-hooks] ERROR NulWard %s detour exception @0x%08X", label, static_cast<unsigned>(targetVa));
        return false;
    }
}

static bool RemoveDetour(PLH::x86Detour* detour, const char* label) {
    if (!detour) return true;
    const bool ok = detour->unHook();
    delete detour;
    HookLog("[ffx-hooks] NulWard %s detour remove %s", label, ok ? "ok" : "FAILED");
    return ok;
}

#endif /* FFXHOOKS_HAVE_POLYHOOK */

static void ResolveGameFns(uintptr_t base) {
    g_base = base;
    g_getActor = reinterpret_cast<GetActorByIndexFn>(base + RVA_FFX_BATTLE_GET_ACTOR_BY_INDEX);
}

} // namespace

NulWardInstallResult InstallNulWardHook(
    uintptr_t base,
    bool applyBlocks,
    bool logEvents,
    NulWardLogFn log,
    const NulWardInstallOptions* options) {
    NulWardInstallResult result = { false, 0, 0, 0, 0 };
    if (g_installed) {
        result.ok = true;
        result.stubWriteback = g_writebackPatchVa;
#ifdef FFXHOOKS_HAVE_POLYHOOK
        result.detourAftermath = static_cast<uintptr_t>(g_aftermathTrampolineVa);
        result.detourHitLoop = static_cast<uintptr_t>(g_hitLoopTrampolineVa);
#endif
        return result;
    }
    if (!applyBlocks && !logEvents) {
        if (log) {
            log("[ffx-hooks] NulWard install skipped: neither apply nor log requested");
        }
        return result;
    }

    g_logFn = log;
    g_applyBlocks = applyBlocks;
    g_logEvents = logEvents;
    g_nativeSlots = options && options->nativeSlots;
    g_experimentP16 = options && options->experimentP16;
    g_p16Apply = options && options->p16Apply;
    g_castLogCount = 0;
    g_nullLogCount = 0;
    g_probeLogCount = 0;
#ifdef FFXHOOKS_HAVE_POLYHOOK
    g_p16LogCount = 0;
#endif
    ResolveGameFns(base);

    bool ok = true;

#ifdef FFXHOOKS_HAVE_POLYHOOK
    const uint64_t hitLoopShimVa = reinterpret_cast<uint64_t>(&ApplyHitDamageLoop_Shim);
    const uint64_t aftermathShimVa = reinterpret_cast<uint64_t>(&ActionAftermath_Shim);

    if (!InstallDetour(base, RVA_FFX_BATTLE_APPLY_HIT_DAMAGE_LOOP, hitLoopShimVa, &g_hitLoopDetour, &g_hitLoopTrampolineVa, "hitLoop")) {
        ok = false;
    } else {
        g_hitLoopTrampoline = reinterpret_cast<ApplyHitDamageLoopFn>(g_hitLoopTrampolineVa);
    }

    if (ok && !InstallDetour(base, RVA_FFX_BATTLE_ACTION_AFTERMATH, aftermathShimVa, &g_aftermathDetour, &g_aftermathTrampolineVa, "aftermath")) {
        ok = false;
    } else if (ok) {
        g_aftermathTrampoline = reinterpret_cast<ActionAftermathFn>(g_aftermathTrampolineVa);
    }

    if (ok && (g_experimentP16 || g_p16Apply)) {
        const uint64_t precheckShimVa = reinterpret_cast<uint64_t>(&ResolveHitPrecheck_Shim);
        if (!InstallDetour(base, RVA_FFX_BATTLE_RESOLVE_HIT_PRECHECK, precheckShimVa, &g_precheckDetour, &g_precheckTrampolineVa, "precheck")) {
            ok = false;
        } else {
            g_precheckTrampoline = reinterpret_cast<ResolveHitPrecheckFn>(g_precheckTrampolineVa);
            result.detourPrecheck = static_cast<uintptr_t>(g_precheckTrampolineVa);
        }
    }

    result.detourHitLoop = static_cast<uintptr_t>(g_hitLoopTrampolineVa);
    result.detourAftermath = static_cast<uintptr_t>(g_aftermathTrampolineVa);
#else
    HookLog("[ffx-hooks] WARN NulWard detours require FFXHOOKS_HAVE_POLYHOOK — writeback-only build");
#endif

    if (ok && !InstallWritebackPatch(base)) {
        ok = false;
    }

    if (!ok) {
        RemoveNulWardHook(log);
        return result;
    }

    g_installed = true;
    result.ok = true;
    result.stubWriteback = g_writebackPatchVa;
    HookLog(
        "[ffx-hooks] NulWard installed apply=%d log=%d native=%d p16=%d p16apply=%d base=0x%08X",
        applyBlocks ? 1 : 0,
        logEvents ? 1 : 0,
        g_nativeSlots ? 1 : 0,
        g_experimentP16 ? 1 : 0,
        g_p16Apply ? 1 : 0,
        static_cast<unsigned>(base));
    return result;
}

bool RemoveNulWardHook(NulWardLogFn log) {
    if (!g_installed && g_writebackPatchVa == 0
#ifdef FFXHOOKS_HAVE_POLYHOOK
        && !g_hitLoopDetour && !g_aftermathDetour
#endif
    ) {
        return true;
    }

    const bool writebackOk = RemoveWritebackPatch();

#ifdef FFXHOOKS_HAVE_POLYHOOK
    RemoveDetour(g_hitLoopDetour, "hitLoop");
    g_hitLoopDetour = nullptr;
    g_hitLoopTrampoline = nullptr;
    g_hitLoopTrampolineVa = 0;

    RemoveDetour(g_aftermathDetour, "aftermath");
    g_aftermathDetour = nullptr;
    g_aftermathTrampoline = nullptr;
    g_aftermathTrampolineVa = 0;

    RemoveDetour(g_precheckDetour, "precheck");
    g_precheckDetour = nullptr;
    g_precheckTrampoline = nullptr;
    g_precheckTrampolineVa = 0;
#endif

    g_holyBlocks.clear();
    g_darkBlocks.clear();
    g_installed = false;
    g_applyBlocks = false;
    g_logEvents = false;
    g_nativeSlots = false;
    g_experimentP16 = false;
    g_p16Apply = false;
    g_logFn = nullptr;
    g_base = 0;
    g_getActor = nullptr;
    g_tlsEncodedCmd = 0;

    if (log) {
        log(writebackOk ? "[ffx-hooks] NulWard removed ok" : "[ffx-hooks] NulWard remove: writeback restore FAILED");
    }
    return writebackOk;
}

bool IsNulWardHookInstalled() {
    return g_installed;
}

} // namespace FfxHooks
