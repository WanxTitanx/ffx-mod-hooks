#include "UnXBoosterHook.h"
#include "../shared/Config.h"
#include "../shared/ffx_addresses.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

namespace {

static HWND g_hwnd = nullptr;
static bool g_initialized = false;
static bool g_boostersActive = false;
static bool g_cheatsActive = false;

/* ── Booster: Entire Party Earns AP ─────────────────────────────────── */
static void ApplyEntirePartyAP() {
    /* Mark all in-party characters as "participation=2, earn=1"
     * at FFX_BattleParticipation (0x1F10EA0) and AP_Earn (0x1F10EC4).
     * Called at 30Hz during battle. */
    static uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    DWORD* participation = reinterpret_cast<DWORD*>(base + RVA_FFX_BATTLE_PARTICIPATION);
    DWORD* apEarn = reinterpret_cast<DWORD*>(base + RVA_FFX_AP_EARN);
    /* Set participation=2 for all 4 party slots */
    for (int i = 0; i < 4; i++) {
        participation[i * 2] = 2;  /* participation */
        apEarn[i * 2] = 1;        /* earn flag */
    }
}

/* ── Booster: Permanent Sensor ───────────────────────────────────────── */
static void ApplyPermanentSensor() {
    static uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    volatile BYTE* sensor = reinterpret_cast<volatile BYTE*>(base + RVA_FFX_PERMANENT_SENSOR);
    *sensor = 1;
}

/* ── Booster: Playable Seymour ────────────────────────────────────────
 * PENDENTE DE RE (2026-08-02, Jarvis-HOOK): o UnX legado escrevia
 * `ffx.party[7].in_party` POR FRAME, mas o endereco base do party struct
 * nao foi validado na db canonica (o decomp usa o global nomeado "ffx").
 * NAO portar por chute: o item fica no dashboard, o apply fica OFF. */

/* ── Cheats: Debug Flags ────────────────────────────────────────────── */
static void ApplyDebugFlags() {
    static uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    volatile BYTE* debug = reinterpret_cast<volatile BYTE*>(base + RVA_FFX_DEBUG_FLAGS);
    /* Byte layout at DebugFlags (0xD2A8F8):
     * +0x00: Invincible Enemies
     * +0x01: Invincible Party
     * +0x04: Always Overdrive
     * +0x05: Always Critical
     * +0x06: Damage 1
     * +0x07: Damage 10000
     * +0x08: Damage 99999
     * +0x09: Always Rare Drop
     * +0x0A: AP 100x
     * +0x0B: Gil 100x
     * +0x15: Permanent Sensor */
    if (FfxHooks::Config::GetBool("boosters.permanent_sensor", false))
        debug[0x15] = 1;
    if (FfxHooks::Config::GetBool("cheats.always_overdrive", false))
        debug[0x04] = 1;
    if (FfxHooks::Config::GetBool("cheats.always_critical", false))
        debug[0x05] = 1;
    if (FfxHooks::Config::GetBool("cheats.damage_value", false))
        debug[0x08] = 1;  /* 99999 */
    if (FfxHooks::Config::GetBool("cheats.always_rare_drop", false))
        debug[0x09] = 1;
    if (FfxHooks::Config::GetBool("cheats.ap_100x", false))
        debug[0x0A] = 1;
    if (FfxHooks::Config::GetBool("cheats.gil_100x", false))
        debug[0x0B] = 1;
}

/* ── 30Hz Timer Callback ────────────────────────────────────────────── */
static void CALLBACK BoosterTimerProc(HWND, UINT, UINT_PTR, DWORD) {
    if (FfxHooks::Config::GetBool("boosters.entire_party_earns_ap", false))
        ApplyEntirePartyAP();
    ApplyDebugFlags();
}

} // ns

bool FfxHooks::StartUnXBoosterHook() {
    if (g_initialized) return true;
    g_hwnd = FindWindowA(0, "FINAL FANTASY X");
    if (!g_hwnd) g_hwnd = FindWindowA("FFXGAME", nullptr);
    if (!g_hwnd) return false;
    /* 33ms timer ≈ 30Hz — same as UnX CheatTimer */
    SetTimer(g_hwnd, 0xDEAD + 1, 33, BoosterTimerProc);
    g_initialized = true;
    OutputDebugStringA("[ffx-hooks] UnXBoosterHook started (30Hz timer)\n");
    return true;
}
