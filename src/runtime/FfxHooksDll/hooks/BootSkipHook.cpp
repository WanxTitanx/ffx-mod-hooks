#include "BootSkipHook.h"
#include "../shared/ffx_addresses.h"

#ifdef FFXHOOKS_HAVE_POLYHOOK
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <polyhook2/Detour/x86Detour.hpp>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

namespace FfxHooks {

#ifdef FFXHOOKS_HAVE_POLYHOOK

namespace {

static PLH::x86Detour*   g_detour = nullptr;
static uint64_t          g_trampoline = 0;
static BootSkipLogFn     g_logFn = nullptr;
static bool              g_installed = false;
static uintptr_t         g_base = 0;
static BootSkipConfig    g_cfg = { true, false, false, -1 };
static volatile LONG     g_tickLogs = 0;
static BootPhase         g_phase = BootPhase::kUnknown;
static uint32_t          g_lastLoggedPhase = 0xFFFFFFFFu;

typedef int (__cdecl* FieldServiceTick_t)(float dt);

static void HookLog(const char* fmt, ...) {
    if (!g_logFn) return;
    char line[512] = {};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    g_logFn(line);
}

static uint32_t ReadU32(uintptr_t rva) {
    if (!g_base) return 0;
    __try {
        return *reinterpret_cast<const volatile uint32_t*>(g_base + rva);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static BootPhase ClassifyPhase() {
    const uint32_t inst    = ReadU32(RVA_FFX_CONTROLLED_CHR_INSTANCE_PTR);
    const uint32_t menuOn  = ReadU32(RVA_FFX_MENU_SUBSYSTEM_ACTIVE);
    const uint32_t pending = ReadU32(RVA_FFX_SCENE_TRANSITION_PENDING);
    const uint32_t scene   = ReadU32(RVA_FFX_SCENE_STATE_OBJECT + FFX_SCENE_STATE_SCENE_ID_OFFSET);

    if (inst != 0) return BootPhase::kInField;
    if (menuOn != 0) return BootPhase::kMenuActive;
    if (pending != 0) return BootPhase::kSceneTransition;
    if (scene == 23u) return BootPhase::kIntroScene;
    return BootPhase::kBootOrTitle;
}

static const char* PhaseName(BootPhase p) {
    switch (p) {
    case BootPhase::kBootOrTitle: return "BootOrTitle";
    case BootPhase::kIntroScene: return "IntroScene";
    case BootPhase::kSceneTransition: return "SceneTransition";
    case BootPhase::kMenuActive: return "MenuActive";
    case BootPhase::kInField: return "InField";
    case BootPhase::kDone: return "Done";
    default: return "Unknown";
    }
}

// ponytail: last-save = newest ffx_NNN under Square Enix save dir; upgrade when RE finds global.
static int ResolveSaveSlotFromMtime() {
    char path[MAX_PATH] = {};
    if (ExpandEnvironmentStringsA(
            "%USERPROFILE%\\Documents\\SQUARE ENIX\\FINAL FANTASY X&X-2 HD Remaster\\FINAL FANTASY X",
            path, MAX_PATH) == 0) {
        return 0;
    }
    WIN32_FIND_DATAA fd = {};
    char pattern[MAX_PATH + 16] = {};
    snprintf(pattern, sizeof(pattern), "%s\\ffx_*", path);
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    int bestSlot = 0;
    FILETIME bestFt = {};
    bool have = false;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const char* name = fd.cFileName;
        if (strncmp(name, "ffx_", 4) != 0) continue;
        int slot = atoi(name + 4);
        if (slot < 0 || slot > 6) continue;
        if (!have || CompareFileTime(&fd.ftLastWriteTime, &bestFt) > 0) {
            bestFt = fd.ftLastWriteTime;
            bestSlot = slot;
            have = true;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return bestSlot;
}

static int ResolveSaveSlot() {
    if (g_cfg.saveSlot >= 0 && g_cfg.saveSlot <= 6) return g_cfg.saveSlot;
    return ResolveSaveSlotFromMtime();
}

static void TryApplyContinue() {
    // STUB — call Save_LoadOrchestrator load case or ReadFile+LoadSaveDataFromBuf after RT2.
    // Target chain: FFX_Save_ReadFile @ 0x646CE0 → Save_LoadSaveDataFromBuf @ 0x8B5450
    // Then RequestTransition(sceneFromSave) @ 0x88E9D0 — scene offset TBD in RE doc.
    const int slot = ResolveSaveSlot();
    HookLog("[ffx-hooks] BootSkip APPLY continue STUB slot=%d (not executed — RT2 gated)", slot);
}

static void TryApplyTitleSkip() {
    // STUB — prefer probe SETINPUT on main thread or FMV-complete flag @ 0x6D8420 path.
    HookLog("[ffx-hooks] BootSkip APPLY title/FMV skip STUB (not executed — RT2 gated)");
}

static int __cdecl FieldServiceTick_Shim(float dt) {
    g_phase = ClassifyPhase();
    const uint32_t phaseU = static_cast<uint32_t>(g_phase);

    if (phaseU != g_lastLoggedPhase) {
        g_lastLoggedPhase = phaseU;
        const long n = InterlockedIncrement(&g_tickLogs);
        if (n <= 200) {
            HookLog("[ffx-hooks] BootSkip phase=%s scene=0x%08X menuOn=%u inst=0x%08X pending=%u observe=%d",
                PhaseName(g_phase),
                ReadU32(RVA_FFX_SCENE_STATE_OBJECT),
                ReadU32(RVA_FFX_MENU_SUBSYSTEM_ACTIVE),
                ReadU32(RVA_FFX_CONTROLLED_CHR_INSTANCE_PTR),
                ReadU32(RVA_FFX_SCENE_TRANSITION_PENDING),
                g_cfg.observeOnly ? 1 : 0);
        }
    }

    if (!g_cfg.observeOnly) {
        if (g_cfg.applySkip && (g_phase == BootPhase::kBootOrTitle || g_phase == BootPhase::kIntroScene)) {
            TryApplyTitleSkip();
        }
        if (g_cfg.applyContinue && g_phase == BootPhase::kMenuActive) {
            TryApplyContinue();
        }
        if (g_phase == BootPhase::kInField) {
            g_phase = BootPhase::kDone;
        }
    }

    return ((FieldServiceTick_t)g_trampoline)(dt);
}

} // namespace

BootSkipInstallResult InstallBootSkipHook(uintptr_t base, BootSkipLogFn log, const BootSkipConfig& cfg) {
    BootSkipInstallResult result = { false, 0 };
    g_logFn = log;
    g_base = base;
    g_cfg = cfg;

    if (g_installed) {
        result.reasonCode = 2;
        return result;
    }

    const uint64_t targetVa = static_cast<uint64_t>(base + RVA_FFX_SCENE_FIELD_SERVICE_TICK);
    g_detour = new PLH::x86Detour(
        targetVa,
        reinterpret_cast<uint64_t>(&FieldServiceTick_Shim),
        &g_trampoline);

    if (!g_detour->hook()) {
        delete g_detour;
        g_detour = nullptr;
        result.reasonCode = 4;
        HookLog("[ffx-hooks] BootSkip detour FAILED @0x%08X", static_cast<unsigned>(targetVa));
        return result;
    }

    g_installed = true;
    result.ok = true;
    HookLog("[ffx-hooks] BootSkip installed observe=%d applySkip=%d applyContinue=%d slot=%d base=0x%08X",
        cfg.observeOnly ? 1 : 0,
        cfg.applySkip ? 1 : 0,
        cfg.applyContinue ? 1 : 0,
        cfg.saveSlot,
        static_cast<unsigned>(base));
    return result;
}

void RemoveBootSkipHook() {
    if (g_detour) {
        g_detour->unHook();
        delete g_detour;
        g_detour = nullptr;
    }
    g_trampoline = 0;
    g_installed = false;
    g_phase = BootPhase::kUnknown;
    g_lastLoggedPhase = 0xFFFFFFFFu;
}

bool IsBootSkipHookInstalled() { return g_installed; }
BootPhase BootSkipCurrentPhase() { return g_phase; }
long BootSkipTickLogCount() { return g_tickLogs; }

static bool EnvTruthy(const char* name) {
    char buf[16] = {};
    if (GetEnvironmentVariableA(name, buf, sizeof(buf)) == 0) return false;
    return buf[0] == '1' || buf[0] == 'y' || buf[0] == 'Y' || buf[0] == 't' || buf[0] == 'T';
}

BootSkipConfig BootSkipConfigFromEnvironment() {
    BootSkipConfig cfg = {};
    cfg.observeOnly = !EnvTruthy("FFXHOOKS_FAST_BOOT_SKIP_APPLY");
    if (EnvTruthy("FFXHOOKS_FAST_BOOT_OBSERVE_ONLY")) cfg.observeOnly = true;
    cfg.applySkip = EnvTruthy("FFXHOOKS_FAST_BOOT_SKIP_APPLY");
    cfg.applyContinue = cfg.applySkip;
    cfg.saveSlot = -1;
    char slotBuf[8] = {};
    if (GetEnvironmentVariableA("FFXHOOKS_FAST_BOOT_SAVE_SLOT", slotBuf, sizeof(slotBuf)) > 0) {
        cfg.saveSlot = atoi(slotBuf);
    }
    return cfg;
}

#else /* !FFXHOOKS_HAVE_POLYHOOK */

BootSkipInstallResult InstallBootSkipHook(uintptr_t, BootSkipLogFn log, const BootSkipConfig&) {
    if (log) log("[ffx-hooks] WARN BootSkip requires FFXHOOKS_HAVE_POLYHOOK");
    return { false, 3 };
}
void RemoveBootSkipHook() {}
bool IsBootSkipHookInstalled() { return false; }
BootPhase BootSkipCurrentPhase() { return BootPhase::kUnknown; }
long BootSkipTickLogCount() { return 0; }
BootSkipConfig BootSkipConfigFromEnvironment() { return { true, false, false, -1 }; }

#endif

} // namespace FfxHooks
