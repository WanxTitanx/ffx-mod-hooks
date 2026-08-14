#include "DoubleTripleDropHook.h"
#include "../shared/ffx_addresses.h"

#ifdef FFXHOOKS_HAVE_POLYHOOK
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <polyhook2/Detour/x86Detour.hpp>
#include <exception>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#endif

namespace FfxHooks {

#ifdef FFXHOOKS_HAVE_POLYHOOK

namespace {

static PLH::x86Detour*        g_detour = nullptr;
static uint64_t               g_trampoline = 0;
static DoubleTripleDropLogFn  g_logFn = nullptr;
static bool                   g_installed = false;
static bool                   g_apply = false;
static bool                   g_logHits = false;
static uintptr_t              g_base = 0;
static volatile LONG          g_hitCount = 0;
static volatile LONG          g_logLines = 0;

typedef int (__cdecl *AddItemFn)(int itemId, int qtyDelta);

static void HookLog(const char* fmt, ...) {
    if (!g_logFn) return;
    char line[384] = {};
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    g_logFn(line);
}

static bool IsBattleItemId(int itemId) {
    return (static_cast<unsigned>(itemId) & 0xF000u) == FFX_ITEM_ID_NAMESPACE_MASK;
}

static bool IsWhitelistedBattleCallerRva(uintptr_t callerRva) {
    switch (callerRva) {
    case RVA_FFX_BATTLE_INVENTORY_ADD_CALLER_DROP:
    case RVA_FFX_BATTLE_INVENTORY_ADD_CALLER_MENU:
    case RVA_FFX_BATTLE_INVENTORY_ADD_CALLER_ACTION:
    case RVA_FFX_BATTLE_INVENTORY_ADD_CALLER_COMMAND:
        return true;
    default:
        return false;
    }
}

static uint16_t ReadActorAutoAbilities2(const void* actor) {
    if (!actor) return 0;
    __try {
        const auto* bytes = reinterpret_cast<const uint8_t*>(actor);
        return *reinterpret_cast<const uint16_t*>(bytes + FFX_MEMORY_CHR_AUTO_ABILITIES_2_OFF);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static void* GetPartyListBase() {
    if (!g_base) return nullptr;
    __try {
        const uintptr_t listVa = g_base + RVA_FFX_BATTLE_PLAYER_LIST;
        const uintptr_t listBase = *reinterpret_cast<const uintptr_t*>(listVa);
        if (!listBase) return nullptr;
        return reinterpret_cast<void*>(listBase);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static void* GetPartyListActor(uint8_t slot) {
    void* listBase = GetPartyListBase();
    if (!listBase || slot >= FFX_BATTLE_PARTY_SCAN_SLOTS) return nullptr;
    return reinterpret_cast<uint8_t*>(listBase)
        + static_cast<uintptr_t>(slot) * FFX_BATTLE_CHR_STRIDE;
}

/* ponytail: Gillionaire-style party MAX — scan active slots, never stack mult per character. */
static int PartyDropMultiplierMax() {
    int mult = 1;
    for (uint8_t slot = 0; slot < FFX_BATTLE_PARTY_SCAN_SLOTS; ++slot) {
        const uint16_t ab2 = ReadActorAutoAbilities2(GetPartyListActor(slot));
        if ((ab2 & FFX_AUTOABILITY2_TRIPLE_DROP) != 0) {
            if (mult < 3) mult = 3;
        } else if ((ab2 & FFX_AUTOABILITY2_DOUBLE_DROP) != 0) {
            if (mult < 2) mult = 2;
        }
    }
    return mult;
}

static void MaybeLogHit(uintptr_t callerRva, int itemId, int qtyIn, int mult, int qtyOut) {
    InterlockedIncrement(&g_hitCount);
    if (!g_logHits || !g_logFn) return;
    const long lineNo = InterlockedIncrement(&g_logLines);
    if (lineNo > 128) return;
    HookLog(
        "[ffx-hooks] DoubleTripleDrop #%ld caller=0x%08X item=0x%04X qtyIn=%d mult=%d qtyOut=%d",
        lineNo,
        static_cast<unsigned>(callerRva),
        static_cast<unsigned>(itemId & 0xFFFF),
        qtyIn,
        mult,
        qtyOut);
}

static int __cdecl AddItem_Shim(int itemId, int qtyDelta) {
    int outQty = qtyDelta;
    if (g_apply && qtyDelta > 0 && IsBattleItemId(itemId)) {
        const uintptr_t callerRva =
            reinterpret_cast<uintptr_t>(_ReturnAddress()) - g_base;
        if (IsWhitelistedBattleCallerRva(callerRva)) {
            const int mult = PartyDropMultiplierMax();
            if (mult > 1) {
                if (qtyDelta <= INT_MAX / mult)
                    outQty = qtyDelta * mult;
                else
                    outQty = INT_MAX;
                MaybeLogHit(callerRva, itemId, qtyDelta, mult, outQty);
            }
        }
    }
    return reinterpret_cast<AddItemFn>(g_trampoline)(itemId, outQty);
}

} // namespace

DoubleTripleDropInstallResult InstallDoubleTripleDropHook(
    uintptr_t base,
    bool apply,
    bool logHits,
    DoubleTripleDropLogFn log) {
    DoubleTripleDropInstallResult result = { false, 0 };
    g_logFn = log;
    g_base = base;
    g_apply = apply;
    g_logHits = logHits;

    if (g_installed) {
        result.reasonCode = 2;
        return result;
    }

    const uint64_t targetVa = static_cast<uint64_t>(base + RVA_FFX_INVENTORY_ADD_ITEM);
    HookLog(
        "[ffx-hooks] DoubleTripleDrop installing detour at VA=0x%08X apply=%d log=%d",
        static_cast<unsigned>(targetVa),
        apply ? 1 : 0,
        logHits ? 1 : 0);

    try {
        g_detour = new PLH::x86Detour(
            targetVa,
            reinterpret_cast<uint64_t>(&AddItem_Shim),
            &g_trampoline);
        if (!g_detour->hook()) {
            HookLog("[ffx-hooks] DoubleTripleDrop hook() returned false");
            delete g_detour;
            g_detour = nullptr;
            g_trampoline = 0;
            result.reasonCode = 4;
            return result;
        }
        g_installed = true;
        result.ok = true;
        HookLog(
            "[ffx-hooks] DoubleTripleDrop detour installed; trampoline=0x%llX",
            static_cast<unsigned long long>(g_trampoline));
    } catch (const std::exception& ex) {
        HookLog("[ffx-hooks] DoubleTripleDrop install exception: %s", ex.what());
        delete g_detour;
        g_detour = nullptr;
        g_trampoline = 0;
        result.reasonCode = 4;
    } catch (...) {
        HookLog("[ffx-hooks] DoubleTripleDrop install unknown exception");
        delete g_detour;
        g_detour = nullptr;
        g_trampoline = 0;
        result.reasonCode = 4;
    }
    return result;
}

void RemoveDoubleTripleDropHook(DoubleTripleDropLogFn log) {
    if (!g_installed) return;
    if (g_detour) {
        g_detour->unHook();
        delete g_detour;
        g_detour = nullptr;
    }
    g_trampoline = 0;
    g_installed = false;
    g_apply = false;
    g_base = 0;
    if (log) log("[ffx-hooks] DoubleTripleDrop detour removed\n");
}

bool IsDoubleTripleDropHookInstalled() {
    return g_installed;
}

long DoubleTripleDropHookHitCount() {
    return static_cast<long>(InterlockedCompareExchange(&g_hitCount, 0, 0));
}

#else // !FFXHOOKS_HAVE_POLYHOOK

DoubleTripleDropInstallResult InstallDoubleTripleDropHook(
    uintptr_t /*base*/,
    bool /*apply*/,
    bool /*logHits*/,
    DoubleTripleDropLogFn /*log*/) {
    return { false, 3 };
}

void RemoveDoubleTripleDropHook(DoubleTripleDropLogFn /*log*/) {}
bool IsDoubleTripleDropHookInstalled() { return false; }
long DoubleTripleDropHookHitCount() { return 0; }

#endif

} // namespace FfxHooks
