#include "KimahriLancetDualGrantHook.h"
#include "../shared/ffx_addresses.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <intrin.h>

namespace FfxHooks {

namespace {

static bool                       g_installed = false;
static bool                       g_armed = false;
static KimahriLancetDualGrantLogFn g_logFn = nullptr;
static uintptr_t                  g_base = 0;
static GrantCommandFn             g_vanillaGrant = nullptr;
static volatile LONG              g_dualGrantCount = 0;

static void HookLog(const char* fmt, ...) {
    if (!g_logFn) return;
    char line[512] = {};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    g_logFn(line);
}

// Vanilla Ronso learn fires when Lancet resolves on a valid target (hit/absorb),
// not on kill — gate is return addr inside FFX_Btl_ApplyActionResults @ 0x78F0B0.
static bool IsCallerLancetLearnOnUse(void* retAddr) {
    if (!g_base || !retAddr) return false;
    const uintptr_t r = reinterpret_cast<uintptr_t>(retAddr);
    const uintptr_t lo = g_base + RVA_FFX_BATTLE_APPLY_ACTION_RESULTS_LO;
    const uintptr_t hi = g_base + RVA_FFX_BATTLE_APPLY_ACTION_RESULTS_HI;
    return r >= lo && r < hi;
}

static int BlueMageIdForRonsoRage(int rageId) {
    if (rageId < FFX_CMD_RONSO_RAGE_ID_MIN || rageId > FFX_CMD_RONSO_RAGE_ID_MAX)
        return -1;
    return static_cast<int>(FFX_CMD_KIMAHRI_BLUE_MAGE_FIRST)
        + (rageId - FFX_CMD_RONSO_RAGE_ID_MIN);
}

static GrantCommandFn ResolveGrantThrough(GrantCommandFn grantThrough) {
    if (grantThrough) return grantThrough;
    return g_vanillaGrant;
}

static void GrantBlueMagePair(int charIdx, int rageId, GrantCommandFn grantThrough) {
    GrantCommandFn grant = ResolveGrantThrough(grantThrough);
    if (!grant) return;

    const int blueId = BlueMageIdForRonsoRage(rageId);
    if (blueId < 0) return;

    const int blueEnc = 0x3000 | blueId;
    const int menuEnc = static_cast<int>(FFX_CMD_BLUE_MAGIC_MENU_ENCODED);

    grant(charIdx, blueEnc, 1);
    grant(charIdx, menuEnc, 1);

    const long n = InterlockedIncrement(&g_dualGrantCount);
    if (n <= 24 || (n % 10) == 0) {
        HookLog("[ffx-hooks] KimahriLancetDualGrant #%ld ch=%d rage=%d -> blue=%d menu=%d",
            n, charIdx, rageId, blueId, static_cast<int>(FFX_CMD_BLUE_MAGIC_MENU_ID));
    }
}

} // namespace

void KimahriLancetDualGrantOnRonsoLearn(
    int charIdx,
    int rageId,
    int grantOk,
    void* retAddr,
    GrantCommandFn grantThrough) {
    if (!g_installed || !g_armed || !grantOk)
        return;
    if (charIdx != static_cast<int>(FFX_CHARACTER_KIMAHRI))
        return;
    if (rageId < FFX_CMD_RONSO_RAGE_ID_MIN || rageId > FFX_CMD_RONSO_RAGE_ID_MAX)
        return;
    if (!IsCallerLancetLearnOnUse(retAddr))
        return;

    GrantBlueMagePair(charIdx, rageId, grantThrough);
}

KimahriLancetDualGrantInstallResult InstallKimahriLancetDualGrantHook(
    uintptr_t base,
    bool armed,
    GrantCommandFn grantThrough,
    KimahriLancetDualGrantLogFn log) {
    KimahriLancetDualGrantInstallResult result = { true, armed };
    g_logFn = log;
    g_base = base;
    g_armed = armed;
    g_vanillaGrant = reinterpret_cast<GrantCommandFn>(base + RVA_FFX_GRANT_COMMAND_TO_CHARACTER);
    g_installed = true;
    g_dualGrantCount = 0;

    HookLog("[ffx-hooks] KimahriLancetDualGrant install armed=%d base=0x%08X grantThrough=%p (104..115 -> %u..%u + menu %u)",
        armed ? 1 : 0,
        static_cast<unsigned>(base),
        reinterpret_cast<void*>(grantThrough),
        static_cast<unsigned>(FFX_CMD_KIMAHRI_BLUE_MAGE_FIRST),
        static_cast<unsigned>(FFX_CMD_KIMAHRI_BLUE_MAGE_FIRST
            + (FFX_CMD_RONSO_RAGE_ID_MAX - FFX_CMD_RONSO_RAGE_ID_MIN)),
        static_cast<unsigned>(FFX_CMD_BLUE_MAGIC_MENU_ID));

    return result;
}

bool RemoveKimahriLancetDualGrantHook(KimahriLancetDualGrantLogFn log) {
    g_installed = false;
    g_armed = false;
    g_base = 0;
    g_vanillaGrant = nullptr;
    g_dualGrantCount = 0;
    g_logFn = nullptr;
    if (log) log("[ffx-hooks] KimahriLancetDualGrant removed ok");
    return true;
}

bool IsKimahriLancetDualGrantHookInstalled() {
    return g_installed;
}

} // namespace FfxHooks
