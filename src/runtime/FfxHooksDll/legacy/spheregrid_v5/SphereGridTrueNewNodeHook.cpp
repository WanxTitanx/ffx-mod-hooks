#include "SphereGridTrueNewNodeHook.h"
#include "../shared/ffx_addresses.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <polyhook2/Detour/x86Detour.hpp>
#include <exception>
#endif

namespace FfxHooks {
namespace {

struct TrueNewNodePatch {
    uint16_t nodeId;
    uint8_t content;
    uint8_t status;
};

struct TrueNewLinkPatch {
    uint16_t linkId;
    uint8_t state;
};

static const int kMaxNodes = 64;
static const int kMaxLinks = 64;
static const uint16_t kUnknownCount = 0xFFFF;
static TrueNewNodePatch g_nodes[kMaxNodes] = {};
static TrueNewLinkPatch g_links[kMaxLinks] = {};
static int g_nodeCount = 0;
static int g_linkCount = 0;
static bool g_installed = false;
static uintptr_t g_base = 0;
static SphereGridTrueNewNodeLogFn g_logFn = nullptr;
static char g_manifestPath[MAX_PATH] = {};
static uint16_t g_seededNodes = kUnknownCount;
static uint16_t g_currentNodes = kUnknownCount;
static uint16_t g_seededLinks = kUnknownCount;
static uint16_t g_currentLinks = kUnknownCount;
static bool g_forceStatus = false;
static bool g_writeInit = true;
static bool g_writeApply = true;
static bool g_writeSave = false;
static bool g_observeOnly = false;
static bool g_doubleApplyPass = true;
static bool g_forceNewSlots = true;
static bool g_patchMenuRecords = true;
static volatile LONG g_postExitProbeArmed = 0;
static volatile LONG g_postExitTickLogged = 0;
static volatile LONG g_postExitWithinA53570Tick = 0;
static volatile LONG g_postExitFieldWatch = 0;
static volatile LONG g_postExitTailWatch = 0;
static volatile LONG g_deferFieldGateClear = 0;
static volatile LONG g_exitPumpEarlyHandoffDone = 0;
static const int kPostExitTickLogMax = 5;
static const int kPostExitFieldWatchMax = 8;
static const int kPostExitTailWatchMax = 32;

static const uintptr_t kAbmapMenuNodeArrayOffset = 0x808u;
static const uintptr_t kAbmapMenuSubModeOffset = 0x115B8u;
static const uintptr_t kAbmapMenuModeCbOffset = 0x115A8u;
static const uintptr_t kAbmapMenuPadInputOffset = 0x1166Eu;
static const uintptr_t kAbmapMenuDialogSlotOffset = 0x2700u;   /* menu+9984 */
static const uintptr_t kAbmapMenuDialogCbOffset = 0x2704u;       /* menu+9988 callback ptr */
static const uintptr_t kAbmapMenuAnimTickOffset = 0x11688u;      /* menu+71320 */
static const uintptr_t kAbmapMenuNodeStride = 40u;
static const uintptr_t kAbmapMenuNodeContentWordOffset = 6u;
static const uintptr_t kAbmapMenuNodeStatusByteOffset = 33u;

using InitRuntimeStateFn = int(__cdecl*)(int16_t* state);
using NoArgIntFn = int(__cdecl*)();
using RenderFrameHookFn = int(__cdecl*)(int ctx);
using UiModeActivateFn = void(__cdecl*)(int slot, int a2);
using UiModeCtxFn = void(__cdecl*)(int ctx);
using IntArgFn = int(__cdecl*)(int a1);
using RenderNotifyFn = void(__cdecl*)(int a1);
using MenuPumpEntryFn = void(__cdecl*)(int ctx, int a2, int screen);
using FieldTickFn = int(__cdecl*)(float dt);

#ifdef FFXHOOKS_HAVE_POLYHOOK
static PLH::x86Detour* g_initDetour = nullptr;
static PLH::x86Detour* g_layoutDetour = nullptr;
static PLH::x86Detour* g_defaultStateDetour = nullptr;
static PLH::x86Detour* g_applyDetour = nullptr;
static PLH::x86Detour* g_adjacencyDetour = nullptr;
static PLH::x86Detour* g_recomputeDetour = nullptr;
static PLH::x86Detour* g_saveDetour = nullptr;
static PLH::x86Detour* g_deactivateDetour = nullptr;
static PLH::x86Detour* g_uiMode19DeactDetour = nullptr;
static PLH::x86Detour* g_releaseGpuDetour = nullptr;
static PLH::x86Detour* g_exitUiFlushDetour = nullptr;
static PLH::x86Detour* g_exitConfirmDetour = nullptr;
static PLH::x86Detour* g_cameraScrollDetour = nullptr;
static PLH::x86Detour* g_dialogDispatchDetour = nullptr;
static PLH::x86Detour* g_animIndexDetour = nullptr;
static PLH::x86Detour* g_renderFrameDetour = nullptr;
static PLH::x86Detour* g_renderTeardownDetour = nullptr;
static PLH::x86Detour* g_uiModeTickDetour = nullptr;
static PLH::x86Detour* g_uiModeActivateDetour = nullptr;
static PLH::x86Detour* g_slot20HandoffDetour = nullptr;
static PLH::x86Detour* g_partySaveSliceDetour = nullptr;
static PLH::x86Detour* g_menuDrawAllDetour = nullptr;
static PLH::x86Detour* g_menuPoolUpdateDetour = nullptr;
static PLH::x86Detour* g_fieldUiDispatchDetour = nullptr;
static PLH::x86Detour* g_cursorWidgetFlushDetour = nullptr;
static PLH::x86Detour* g_menu2dEndCaptureDetour = nullptr;
static PLH::x86Detour* g_phyreFlushBindsDetour = nullptr;
static PLH::x86Detour* g_menuPumpAliveDetour = nullptr;
static PLH::x86Detour* g_menuPumpEntryDetour = nullptr;
static PLH::x86Detour* g_fieldServiceTickDetour = nullptr;
static PLH::x86Detour* g_phyreBindRtDetour = nullptr;
static PLH::x86Detour* g_fieldSceneDrawDetour = nullptr;
static PLH::x86Detour* g_renderNotifyFrameEndDetour = nullptr;
static uint64_t g_initTrampoline = 0;
static uint64_t g_layoutTrampoline = 0;
static uint64_t g_defaultStateTrampoline = 0;
static uint64_t g_applyTrampoline = 0;
static uint64_t g_adjacencyTrampoline = 0;
static uint64_t g_recomputeTrampoline = 0;
static uint64_t g_saveTrampoline = 0;
static uint64_t g_deactivateTrampoline = 0;
static uint64_t g_uiMode19DeactTrampoline = 0;
static uint64_t g_releaseGpuTrampoline = 0;
static uint64_t g_exitUiFlushTrampoline = 0;
static uint64_t g_exitConfirmTrampoline = 0;
static uint64_t g_cameraScrollTrampoline = 0;
static uint64_t g_dialogDispatchTrampoline = 0;
static uint64_t g_animIndexTrampoline = 0;
static uint64_t g_renderFrameTrampoline = 0;
static uint64_t g_renderTeardownTrampoline = 0;
static uint64_t g_uiModeTickTrampoline = 0;
static uint64_t g_uiModeActivateTrampoline = 0;
static uint64_t g_slot20HandoffTrampoline = 0;
static uint64_t g_partySaveSliceTrampoline = 0;
static uint64_t g_menuDrawAllTrampoline = 0;
static uint64_t g_menuPoolUpdateTrampoline = 0;
static uint64_t g_fieldUiDispatchTrampoline = 0;
static uint64_t g_cursorWidgetFlushTrampoline = 0;
static uint64_t g_menu2dEndCaptureTrampoline = 0;
static uint64_t g_phyreFlushBindsTrampoline = 0;
static uint64_t g_menuPumpAliveTrampoline = 0;
static uint64_t g_menuPumpEntryTrampoline = 0;
static uint64_t g_fieldServiceTickTrampoline = 0;
static uint64_t g_phyreBindRtTrampoline = 0;
static uint64_t g_fieldSceneDrawTrampoline = 0;
static uint64_t g_renderNotifyFrameEndTrampoline = 0;
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

static bool ModuleDir(char* out, size_t outSize) {
    HMODULE self = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&ModuleDir),
        &self);
    if (!self) return false;
    if (GetModuleFileNameA(self, out, static_cast<DWORD>(outSize)) == 0) return false;
    char* slash = strrchr(out, '\\');
    if (!slash) return false;
    *(slash + 1) = '\0';
    return true;
}

static void ResolveManifestPath() {
    if (g_manifestPath[0]) return;
    char dir[MAX_PATH] = {};
    if (!ModuleDir(dir, sizeof(dir))) {
        strcpy_s(g_manifestPath, "true_new_node_manifest.csv");
        return;
    }
    snprintf(g_manifestPath, sizeof(g_manifestPath), "%sconfig\\true_new_node_manifest.csv", dir);
}

static int ParseInt(const char* text) {
    if (!text) return 0;
    return static_cast<int>(strtol(text, nullptr, 0));
}

static bool LoadManifest() {
    ResolveManifestPath();
    FILE* f = nullptr;
    fopen_s(&f, g_manifestPath, "r");
    if (!f) {
        HookLog("[ffx-hooks] TrueNewNode manifest missing: %s", g_manifestPath);
        return false;
    }

    g_nodeCount = 0;
    g_linkCount = 0;
    char line[512] = {};
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n')
            continue;

        char* ctx = nullptr;
        char* kind = strtok_s(line, ",\r\n", &ctx);
        if (!kind) continue;

        if (_stricmp(kind, "meta") == 0) {
            strtok_s(nullptr, ",\r\n", &ctx);
            strtok_s(nullptr, ",\r\n", &ctx);
            char* seededNodes = strtok_s(nullptr, ",\r\n", &ctx);
            char* currentNodes = strtok_s(nullptr, ",\r\n", &ctx);
            char* seededLinks = strtok_s(nullptr, ",\r\n", &ctx);
            char* currentLinks = strtok_s(nullptr, ",\r\n", &ctx);
            if (seededNodes) g_seededNodes = static_cast<uint16_t>(ParseInt(seededNodes) & 0xFFFF);
            if (currentNodes) g_currentNodes = static_cast<uint16_t>(ParseInt(currentNodes) & 0xFFFF);
            if (seededLinks) g_seededLinks = static_cast<uint16_t>(ParseInt(seededLinks) & 0xFFFF);
            if (currentLinks) g_currentLinks = static_cast<uint16_t>(ParseInt(currentLinks) & 0xFFFF);
        } else if (_stricmp(kind, "node") == 0 && g_nodeCount < kMaxNodes) {
            char* nodeId = strtok_s(nullptr, ",\r\n", &ctx);
            char* content = strtok_s(nullptr, ",\r\n", &ctx);
            char* status = strtok_s(nullptr, ",\r\n", &ctx);
            if (!nodeId || !content || !status) continue;
            g_nodes[g_nodeCount++] = TrueNewNodePatch{
                static_cast<uint16_t>(ParseInt(nodeId)),
                static_cast<uint8_t>(ParseInt(content) & 0xFF),
                static_cast<uint8_t>(ParseInt(status) & 0xFF)
            };
        } else if (_stricmp(kind, "link") == 0 && g_linkCount < kMaxLinks) {
            char* linkId = strtok_s(nullptr, ",\r\n", &ctx);
            char* state = strtok_s(nullptr, ",\r\n", &ctx);
            if (!linkId || !state) continue;
            g_links[g_linkCount++] = TrueNewLinkPatch{
                static_cast<uint16_t>(ParseInt(linkId)),
                static_cast<uint8_t>(ParseInt(state) & 0xFF)
            };
        }
    }
    fclose(f);

    HookLog("[ffx-hooks] TrueNewNode manifest loaded nodes=%d links=%d seeded=%u/%u current=%u/%u path=%s",
        g_nodeCount,
        g_linkCount,
        g_seededNodes == kUnknownCount ? 0u : g_seededNodes,
        g_seededLinks == kUnknownCount ? 0u : g_seededLinks,
        g_currentNodes == kUnknownCount ? 0u : g_currentNodes,
        g_currentLinks == kUnknownCount ? 0u : g_currentLinks,
        g_manifestPath);
    return g_nodeCount > 0 || g_linkCount > 0;
}

static bool EnvFlagEnabled(const char* name) {
    char value[16] = {};
    DWORD len = GetEnvironmentVariableA(name, value, sizeof(value));
    return len > 0 && (value[0] == '1' || value[0] == 'y' || value[0] == 'Y' ||
        value[0] == 't' || value[0] == 'T');
}

static bool EnvFlagDisabled(const char* name) {
    char value[16] = {};
    DWORD len = GetEnvironmentVariableA(name, value, sizeof(value));
    return len > 0 && (value[0] == '0' || value[0] == 'n' || value[0] == 'N' ||
        value[0] == 'f' || value[0] == 'F');
}

static bool HookModuleFlagExists(const char* relativePath) {
    char dir[MAX_PATH] = {};
    if (!ModuleDir(dir, sizeof(dir))) return false;
    char path[MAX_PATH] = {};
    snprintf(path, sizeof(path), "%s%s", dir, relativePath);
    const DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static inline volatile uint8_t* RuntimeStateBase() {
    if (!g_base) return nullptr;
    return reinterpret_cast<volatile uint8_t*>(g_base + RVA_FFX_SPHERE_GRID_RUNTIME_STATE_TABLE);
}

static void ConfigurePhaseModes() {
    g_forceStatus = EnvFlagEnabled("FFXHOOKS_TRUE_NEW_NODE_FORCE_STATUS");
    g_observeOnly = EnvFlagEnabled("FFXHOOKS_TRUE_NEW_NODE_OBSERVE_ONLY");
    g_writeInit = !EnvFlagDisabled("FFXHOOKS_TRUE_NEW_NODE_WRITE_INIT");
    g_writeApply = !EnvFlagDisabled("FFXHOOKS_TRUE_NEW_NODE_WRITE_APPLY");
    g_writeSave = EnvFlagEnabled("FFXHOOKS_TRUE_NEW_NODE_WRITE_SAVE");
    g_doubleApplyPass = !EnvFlagDisabled("FFXHOOKS_TRUE_NEW_NODE_DOUBLE_APPLY");
    g_forceNewSlots = !EnvFlagDisabled("FFXHOOKS_TRUE_NEW_NODE_FORCE_NEW_SLOTS");
    g_patchMenuRecords = !EnvFlagDisabled("FFXHOOKS_TRUE_NEW_NODE_PATCH_MENU");

    if (g_observeOnly) {
        g_writeInit = false;
        g_writeApply = false;
        g_writeSave = false;
    }

    // File flag overrides OBSERVE_ONLY env (RT2 R5+): survives Steam launch without env vars.
    if (HookModuleFlagExists("config\\true_new_node_writes.flag") ||
        HookModuleFlagExists("true_new_node_writes.flag")) {
        g_observeOnly = false;
        g_writeInit = !EnvFlagDisabled("FFXHOOKS_TRUE_NEW_NODE_WRITE_INIT");
        g_writeApply = !EnvFlagDisabled("FFXHOOKS_TRUE_NEW_NODE_WRITE_APPLY");
    }
}

static inline volatile uint8_t* AbmapMenuStateBase() {
    if (!g_base) return nullptr;
    __try {
        const uintptr_t ptr = *reinterpret_cast<volatile uintptr_t*>(g_base + RVA_FFX_ABMAP_MENU_STATE_PTR);
        if (ptr < 0x10000u)
            return nullptr;
        return reinterpret_cast<volatile uint8_t*>(ptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static void LogMenuProbe(const char* tag);
static void LogRuntimeProbe(volatile uint8_t* state, const char* tag);

static void LogExitSnapshot(const char* tag) {
    const uintptr_t menuPtrRaw = g_base
        ? *reinterpret_cast<volatile uintptr_t*>(g_base + RVA_FFX_ABMAP_MENU_STATE_PTR)
        : 0u;
    HookLog("[ffx-hooks] TrueNewNode exit-snapshot %s menuPtr=0x%08X seeded=%u/%u current=%u/%u",
        tag,
        static_cast<unsigned>(menuPtrRaw),
        g_seededNodes == kUnknownCount ? 0u : g_seededNodes,
        g_seededLinks == kUnknownCount ? 0u : g_seededLinks,
        g_currentNodes == kUnknownCount ? 0u : g_currentNodes,
        g_currentLinks == kUnknownCount ? 0u : g_currentLinks);

    volatile uint8_t* menu = AbmapMenuStateBase();
    if (menu) {
        __try {
            const uint16_t nodeCount = *reinterpret_cast<volatile uint16_t*>(menu + 2);
            const uint16_t linkCount = *reinterpret_cast<volatile uint16_t*>(menu + 4);
            HookLog("[ffx-hooks] TrueNewNode exit-snapshot %s menu+2 NodeCount=%u LinkCount=%u",
                tag, nodeCount, linkCount);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            HookLog("[ffx-hooks] TrueNewNode exit-snapshot %s WARN menu header read failed", tag);
        }
    } else {
        HookLog("[ffx-hooks] TrueNewNode exit-snapshot %s menu ptr null", tag);
    }

    LogRuntimeProbe(RuntimeStateBase(), tag);
    LogMenuProbe(tag);
}

static void LogFieldHandoffContext(const char* tag) {
    if (!g_base) return;
    const int menuGate = *reinterpret_cast<volatile int*>(g_base + RVA_FFX_MENU_SUBSYSTEM_ACTIVE_FLAG);
    const int altGate = *reinterpret_cast<volatile int*>(g_base + RVA_FFX_MENU_SUBSYSTEM_ALT_FLAG);
    const int skip790 = *reinterpret_cast<volatile int*>(g_base + RVA_FFX_RENDER_SKIP_SUBMIT_790);
    const int skip798 = *reinterpret_cast<volatile int*>(g_base + RVA_FFX_RENDER_DISABLED_798);
    const int suppress = *reinterpret_cast<volatile int*>(g_base + RVA_FFX_MENU_LAYER_SUPPRESS_FLAG);
    const int gate3 = *reinterpret_cast<volatile int*>(g_base + RVA_FFX_MENU_SUBSYSTEM_SUPPRESS_FLAG);
    const uintptr_t menuPtrRaw = *reinterpret_cast<volatile uintptr_t*>(g_base + RVA_FFX_ABMAP_MENU_STATE_PTR);
    uint8_t subMode = 0xFF;
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (menu) {
        __try { subMode = menu[kAbmapMenuSubModeOffset]; } __except (EXCEPTION_EXECUTE_HANDLER) { subMode = 0xFE; }
    }
    HookLog("[ffx-hooks] TrueNewNode field-handoff %s gate=%d alt=%d gate3=%d skip790=%d skip798=%d suppress=%d subMode=%02X abmap=0x%08X",
        tag, menuGate, altGate, gate3, skip790, skip798, suppress, subMode, static_cast<unsigned>(menuPtrRaw));
}

static void FinalizeMenu2DCaptureAfterExit(const char* tag) {
    if (!g_base)
        return;
    uintptr_t ctx = 0;
    __try {
        ctx = *reinterpret_cast<volatile uintptr_t*>(g_base + RVA_FFX_MENU2D_CAPTURE_CTX_PTR);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode capture-finalize %s WARN ctx ptr read failed", tag);
        return;
    }
    if (ctx < 0x10000u) {
        HookLog("[ffx-hooks] TrueNewNode capture-finalize %s ctx=null", tag);
        return;
    }
    __try {
        volatile int* capState = reinterpret_cast<volatile int*>(ctx + 4);
        const int before = *capState;
        if (before < 2)
            *capState = 2;
        HookLog("[ffx-hooks] TrueNewNode capture-finalize %s ctx=0x%08X state+4 %d->%d (release SG 2D capture)",
            tag, static_cast<unsigned>(ctx), before, *capState);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode capture-finalize %s WARN state write failed ctx=0x%08X",
            tag, static_cast<unsigned>(ctx));
    }
}

static void ClearAbmapPadInputAfterExit(const char* tag) {
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (!menu)
        return;
    __try {
        const uint16_t padBefore = *reinterpret_cast<volatile uint16_t*>(menu + kAbmapMenuPadInputOffset);
        *reinterpret_cast<volatile uint16_t*>(menu + kAbmapMenuPadInputOffset) = 0;
        HookLog("[ffx-hooks] TrueNewNode abmap-pad %s padInput=%04X->0000", tag, padBefore);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode abmap-pad %s WARN padInput clear failed", tag);
    }
}

static void ClearAbmapSubModeAfterExit(const char* tag) {
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (!menu)
        return;
    __try {
        const uint8_t before = menu[kAbmapMenuSubModeOffset];
        const uintptr_t cbBefore = *reinterpret_cast<volatile uintptr_t*>(menu + kAbmapMenuModeCbOffset);
        if (before != 0)
            menu[kAbmapMenuSubModeOffset] = 0;
        if (cbBefore != 0)
            *reinterpret_cast<volatile uintptr_t*>(menu + kAbmapMenuModeCbOffset) = 0;
        HookLog("[ffx-hooks] TrueNewNode abmap-submode %s subMode %02X->00 modeCb 0x%08X->0",
            tag, before, static_cast<unsigned>(cbBefore));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode abmap-submode %s WARN clear failed", tag);
    }
}

static bool VanillaHandoffOnly() {
    return HookModuleFlagExists("config\\true_new_node_vanilla_handoff.flag") ||
        HookModuleFlagExists("true_new_node_vanilla_handoff.flag");
}

static bool DeferHandoffTo820C00() {
    return HookModuleFlagExists("config\\true_new_node_defer_handoff.flag") ||
        HookModuleFlagExists("true_new_node_defer_handoff.flag");
}

static bool SkipPumpTailOnExitMaster() {
    return HookModuleFlagExists("config\\true_new_node_skip_pump_tail.flag") ||
        HookModuleFlagExists("true_new_node_skip_pump_tail.flag");
}

static bool ExitPumpTailSkipDefault(const char* runOneFlag, const char* legacySkipFlag) {
    if (VanillaHandoffOnly() || DeferHandoffTo820C00())
        return false;
    if (SkipPumpTailOnExitMaster())
        return true;
    if (runOneFlag) {
        char configPath[MAX_PATH] = {};
        snprintf(configPath, sizeof(configPath), "config\\%s", runOneFlag);
        if (HookModuleFlagExists(configPath) || HookModuleFlagExists(runOneFlag))
            return false;
    }
    if (legacySkipFlag) {
        char configPath[MAX_PATH] = {};
        snprintf(configPath, sizeof(configPath), "config\\%s", legacySkipFlag);
        if (HookModuleFlagExists(configPath) || HookModuleFlagExists(legacySkipFlag))
            return true;
    }
    return false;
}

static bool Skip6392A0OnExit() {
    return ExitPumpTailSkipDefault("true_new_node_run_6392a0.flag", "true_new_node_skip_6392a0.flag");
}

static bool SkipDrawAllLayersOnExit() {
    return ExitPumpTailSkipDefault("true_new_node_run_draw_all.flag", "true_new_node_skip_draw_all.flag");
}

static bool Skip642560OnExit() {
    return ExitPumpTailSkipDefault("true_new_node_run_642560.flag", "true_new_node_skip_642560.flag");
}

static bool PostExitPresentProbeActive() {
    return InterlockedCompareExchange(&g_postExitFieldWatch, 0, 0) > 0 ||
        InterlockedCompareExchange(&g_postExitTailWatch, 0, 0) > 0;
}

static void LogPhyrePresentContext(const char* tag) {
    if (!g_base)
        return;
    const int rtBind = *reinterpret_cast<volatile int*>(g_base + RVA_FFX_PHYRE_RT_BIND_ACTIVE);
    const int gate2 = *reinterpret_cast<volatile int*>(g_base + RVA_FFX_MENU_SUBSYSTEM_ALT_FLAG);
    const int gate3 = *reinterpret_cast<volatile int*>(g_base + RVA_FFX_MENU_SUBSYSTEM_SUPPRESS_FLAG);
    uintptr_t captureRaw = 0;
    int capturePhase = -999;
    __try {
        captureRaw = *reinterpret_cast<volatile uintptr_t*>(g_base + RVA_FFX_MENU2D_CAPTURE_CTX_PTR);
        if (captureRaw)
            capturePhase = *reinterpret_cast<volatile int*>(static_cast<uintptr_t>(captureRaw) + 4u);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        capturePhase = -998;
    }
    HookLog("[ffx-hooks] TrueNewNode phyre-present %s rtBind=%d gate2=%d gate3=%d capture=0x%08X phase=%d",
        tag, rtBind, gate2, gate3, static_cast<unsigned>(captureRaw), capturePhase);
}

static void LogField3DPathProbe(const char* tag) {
    if (!g_base)
        return;
    const int gate = *reinterpret_cast<volatile int*>(g_base + RVA_FFX_MENU_SUBSYSTEM_ACTIVE_FLAG);
    const int gate2 = *reinterpret_cast<volatile int*>(g_base + RVA_FFX_MENU_SUBSYSTEM_ALT_FLAG);
    const int gate3 = *reinterpret_cast<volatile int*>(g_base + RVA_FFX_MENU_SUBSYSTEM_SUPPRESS_FLAG);
    const int skip790 = *reinterpret_cast<volatile int*>(g_base + RVA_FFX_RENDER_SKIP_SUBMIT_790);
    const int skip798 = *reinterpret_cast<volatile int*>(g_base + RVA_FFX_RENDER_DISABLED_798);
    const bool skip3d = gate != 0 || gate2 != 0 || gate3 != 0;
    HookLog("[ffx-hooks] TrueNewNode field-3d-probe %s pre-820de2 gate=%d gate2=%d gate3=%d skip790=%d skip798=%d -> %s",
        tag, gate, gate2, gate3, skip790, skip798, skip3d ? "SKIP-3D-branch" : "RUN-3D-branch");
}

static void RedoDeactivateReturnToFieldUi(const char* tag) {
    if (!g_deactivateTrampoline ||
        HookModuleFlagExists("config\\true_new_node_skip_redeactivate.flag") ||
        HookModuleFlagExists("true_new_node_skip_redeactivate.flag")) {
        return;
    }
    HookLog("[ffx-hooks] TrueNewNode re-handoff %s 8E27E0()", tag);
    __try {
        const int rv = reinterpret_cast<NoArgIntFn>(g_deactivateTrampoline)();
        HookLog("[ffx-hooks] TrueNewNode re-handoff %s return-8E27E0 rv=%d", tag, rv);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode re-handoff %s FATAL inside 8E27E0", tag);
    }
}

static void RedoActivateFieldUiMode(const char* tag) {
    if (!g_uiModeActivateTrampoline)
        return;
    HookLog("[ffx-hooks] TrueNewNode re-handoff %s 8AA0B0(1,0)", tag);
    __try {
        reinterpret_cast<UiModeActivateFn>(g_uiModeActivateTrampoline)(1, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode re-handoff %s FATAL inside 8AA0B0", tag);
    }
}

static void NotifyRenderEngineAfterExit(const char* tag) {
    if (!g_base)
        return;
    __try {
        reinterpret_cast<RenderNotifyFn>(g_base + RVA_FFX_RENDER_ENGINE_MODE_NOTIFY)(static_cast<int>(0x80000001));
        HookLog("[ffx-hooks] TrueNewNode render-notify %s 886DE0(0x80000001)", tag);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode render-notify %s FATAL inside 886DE0", tag);
    }
}

static void RestoreFieldRenderOnExit(const char* tag) {
    if (!g_base)
        return;
    if (HookModuleFlagExists("config\\true_new_node_keep_menu_gate.flag") ||
        HookModuleFlagExists("true_new_node_keep_menu_gate.flag")) {
        HookLog("[ffx-hooks] TrueNewNode field-restore %s SKIPPED (keep_menu_gate flag)", tag);
        return;
    }
    volatile int* gate = reinterpret_cast<volatile int*>(g_base + RVA_FFX_MENU_SUBSYSTEM_ACTIVE_FLAG);
    volatile int* altGate = reinterpret_cast<volatile int*>(g_base + RVA_FFX_MENU_SUBSYSTEM_ALT_FLAG);
    volatile int* skip790 = reinterpret_cast<volatile int*>(g_base + RVA_FFX_RENDER_SKIP_SUBMIT_790);
    volatile int* skip798 = reinterpret_cast<volatile int*>(g_base + RVA_FFX_RENDER_DISABLED_798);
    volatile int* suppress = reinterpret_cast<volatile int*>(g_base + RVA_FFX_MENU_LAYER_SUPPRESS_FLAG);
    volatile int* gate3 = reinterpret_cast<volatile int*>(g_base + RVA_FFX_MENU_SUBSYSTEM_SUPPRESS_FLAG);
    const int gateBefore = *gate;
    const int altBefore = *altGate;
    const int gate3Before = *gate3;
    if (gateBefore != 0) *gate = 0;
    if (altBefore != 0) *altGate = 0;
    if (gate3Before != 0) *gate3 = 0;
    *skip790 = 0;
    *skip798 = 0;
    *suppress = 0;
    HookLog("[ffx-hooks] TrueNewNode field-restore %s gate %d->0 alt %d->0 gate3 %d->0 skip790/skip798/suppress -> 0",
        tag, gateBefore, altBefore, gate3Before);

    if (HookModuleFlagExists("config\\true_new_node_reset_submode.flag") ||
        HookModuleFlagExists("true_new_node_reset_submode.flag")) {
        volatile uint8_t* menu = AbmapMenuStateBase();
        if (!menu) return;
        __try {
            const uint8_t before = menu[kAbmapMenuSubModeOffset];
            if (before != 0) {
                menu[kAbmapMenuSubModeOffset] = 0;
                *reinterpret_cast<volatile uintptr_t*>(menu + kAbmapMenuModeCbOffset) = 0;
                HookLog("[ffx-hooks] TrueNewNode field-restore %s subMode %02X -> 00 modeCb -> 0 (reset_submode flag)",
                    tag, before);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            HookLog("[ffx-hooks] TrueNewNode field-restore %s WARN subMode reset failed", tag);
        }
    }
}

static void CompleteDeferredFieldHandoff(const char* tag) {
    if (VanillaHandoffOnly()) {
        HookLog("[ffx-hooks] TrueNewNode handoff %s VANILLA ONLY (true_new_node_vanilla_handoff.flag) — no gate/subMode patch",
            tag);
        return;
    }
    RestoreFieldRenderOnExit(tag);
    ClearAbmapSubModeAfterExit(tag);
    RedoDeactivateReturnToFieldUi(tag);
    RedoActivateFieldUiMode(tag);
    NotifyRenderEngineAfterExit(tag);
}

static void EarlyExitPumpHandoff(const char* tag) {
    RestoreFieldRenderOnExit(tag);
    ClearAbmapSubModeAfterExit(tag);
    ClearAbmapPadInputAfterExit(tag);
    FinalizeMenu2DCaptureAfterExit(tag);
}

static void ArmEarlyExitPumpHandoff(const char* tag) {
    if (VanillaHandoffOnly() || DeferHandoffTo820C00())
        return;
    if (InterlockedCompareExchange(&g_exitPumpEarlyHandoffDone, 1, 1) == 1)
        return;
    HookLog("[ffx-hooks] TrueNewNode exit-pump handoff %s (gate/subMode/capture before menu GPU upload)", tag);
    LogFieldHandoffContext(tag);
    EarlyExitPumpHandoff(tag);
    NotifyRenderEngineAfterExit(tag);
    InterlockedExchange(&g_exitPumpEarlyHandoffDone, 1);
    char afterTag[48];
    snprintf(afterTag, sizeof(afterTag), "%s-done", tag);
    LogFieldHandoffContext(afterTag);
}

static void PostExitUiReHandoff(const char* tag) {
    RedoDeactivateReturnToFieldUi(tag);
    RedoActivateFieldUiMode(tag);
    NotifyRenderEngineAfterExit(tag);
}

static bool PostExitProbeActive() {
    return InterlockedCompareExchange(&g_postExitProbeArmed, 1, 1) == 1;
}

static void LogDialogSlot(const char* tag) {
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (!menu) {
        HookLog("[ffx-hooks] TrueNewNode dialog-probe %s menu ptr null", tag);
        return;
    }
    __try {
        const int16_t slot = *reinterpret_cast<volatile int16_t*>(menu + kAbmapMenuDialogSlotOffset);
        const uintptr_t cb = *reinterpret_cast<volatile uintptr_t*>(menu + kAbmapMenuDialogCbOffset);
        HookLog("[ffx-hooks] TrueNewNode dialog-probe %s slot=%d cb=0x%08X",
            tag, static_cast<int>(slot), static_cast<unsigned>(cb));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode dialog-probe %s WARN read failed", tag);
    }
}

static void ClearDialogSlotIfFlagged(const char* tag) {
    if (!HookModuleFlagExists("config\\true_new_node_clear_dialog.flag") &&
        !HookModuleFlagExists("true_new_node_clear_dialog.flag")) {
        return;
    }
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (!menu) return;
    __try {
        const int16_t before = *reinterpret_cast<volatile int16_t*>(menu + kAbmapMenuDialogSlotOffset);
        *reinterpret_cast<volatile int16_t*>(menu + kAbmapMenuDialogSlotOffset) = -1;
        *reinterpret_cast<volatile uintptr_t*>(menu + kAbmapMenuDialogCbOffset) = 0;
        HookLog("[ffx-hooks] TrueNewNode dialog-clear %s slot %d -> -1", tag, static_cast<int>(before));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode dialog-clear %s WARN write failed", tag);
    }
}

static void LogAbmapTickContext(const char* tag) {
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (!menu) {
        HookLog("[ffx-hooks] TrueNewNode tick-probe %s menu ptr null", tag);
        return;
    }
    __try {
        const uint8_t subMode = menu[kAbmapMenuSubModeOffset];
        const uint16_t padInput = *reinterpret_cast<volatile uint16_t*>(menu + kAbmapMenuPadInputOffset);
        const uintptr_t modeCb = *reinterpret_cast<volatile uintptr_t*>(menu + kAbmapMenuModeCbOffset);
        HookLog("[ffx-hooks] TrueNewNode tick-probe %s subMode=%02X padInput=%04X modeCb=0x%08X",
            tag, subMode, padInput, static_cast<unsigned>(modeCb));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode tick-probe %s WARN menu tick fields read failed", tag);
    }
}

static void LogMenuProbe(const char* tag) {
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (!menu) {
        HookLog("[ffx-hooks] TrueNewNode menu-probe %s skipped (menu state ptr null)", tag);
        return;
    }
    __try {
        for (int i = 0; i < g_nodeCount; i++) {
            const TrueNewNodePatch& p = g_nodes[i];
            if (p.nodeId >= 1024) continue;
            volatile uint8_t* rec = menu + kAbmapMenuNodeArrayOffset + static_cast<uintptr_t>(p.nodeId) * kAbmapMenuNodeStride;
            const uint16_t contentWord = *reinterpret_cast<volatile uint16_t*>(rec + kAbmapMenuNodeContentWordOffset);
            const uint8_t statusByte = rec[kAbmapMenuNodeStatusByteOffset];
            HookLog("[ffx-hooks] TrueNewNode menu-probe %s node=%u contentWord=%04X status=%02X wantContent=%02X",
                tag, p.nodeId, contentWord, statusByte, p.content);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode WARN exception while probing menu records");
    }
}

static void PatchMenuRecordsForNewSlots(const char* tag, bool write) {
    if (!g_patchMenuRecords) return;
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (!menu) return;
    write = write && !g_observeOnly;

    int touched = 0;
    __try {
        for (int i = 0; i < g_nodeCount; i++) {
            const TrueNewNodePatch& p = g_nodes[i];
            if (p.nodeId >= 1024) continue;
            const bool newSlot = g_forceNewSlots &&
                g_seededNodes != kUnknownCount &&
                p.nodeId >= g_seededNodes;
            if (!newSlot) continue;

            volatile uint8_t* rec = menu + kAbmapMenuNodeArrayOffset + static_cast<uintptr_t>(p.nodeId) * kAbmapMenuNodeStride;
            volatile uint16_t* contentWord = reinterpret_cast<volatile uint16_t*>(rec + kAbmapMenuNodeContentWordOffset);
            const uint16_t oldWord = *contentWord;
            const uint8_t oldStatus = rec[kAbmapMenuNodeStatusByteOffset];
            const uint16_t wantWord = static_cast<uint16_t>(p.content & 0xFF);
            const bool needsContent = oldWord == 0xFFFFu || oldWord != wantWord;
            if (needsContent || newSlot) {
                if (write && needsContent)
                    *contentWord = wantWord;
                touched++;
                HookLog("[ffx-hooks] TrueNewNode %s menu-node=%u contentWord %04X -> %04X status=%02X%s%s",
                    tag, p.nodeId, oldWord, wantWord, oldStatus,
                    newSlot ? " (new-slot)" : "",
                    write && needsContent ? "" : " (observe)");
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode WARN exception while patching menu records");
    }

    if (touched) {
        HookLog("[ffx-hooks] TrueNewNode %s menu %s nodes=%d",
            tag, write ? "patched" : "would-patch", touched);
    }
}

static void LogRuntimeProbe(volatile uint8_t* state, const char* tag) {
    if (!state) return;
    __try {
        for (int i = 0; i < g_nodeCount; i++) {
            const TrueNewNodePatch& p = g_nodes[i];
            if (p.nodeId >= 1024) continue;
            volatile uint8_t* slot = state + static_cast<uintptr_t>(p.nodeId) * 2u;
            HookLog("[ffx-hooks] TrueNewNode probe %s node=%u live=%02X/%02X want=%02X/%02X",
                tag, p.nodeId, slot[0], slot[1], p.content, p.status);
        }
        for (int i = 0; i < g_linkCount; i++) {
            const TrueNewLinkPatch& p = g_links[i];
            if (p.linkId >= 1024) continue;
            volatile uint8_t* slot = state + 0xA00u + p.linkId;
            HookLog("[ffx-hooks] TrueNewNode probe %s link=%u live=%02X want=%02X",
                tag, p.linkId, *slot, p.state);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode WARN exception while probing runtime state");
    }
}

static void ApplyManifestToState(volatile uint8_t* state, const char* tag, bool write) {
    if (!state) return;
    write = write && !g_observeOnly;

    int touchedNodes = 0;
    int touchedLinks = 0;
    __try {
        for (int i = 0; i < g_nodeCount; i++) {
            const TrueNewNodePatch& p = g_nodes[i];
            if (p.nodeId >= 1024) continue;
            if (g_currentNodes != kUnknownCount && p.nodeId >= g_currentNodes) {
                HookLog("[ffx-hooks] TrueNewNode WARN %s node=%u outside currentNodes=%u",
                    tag, p.nodeId, g_currentNodes);
                continue;
            }
            const bool newSlot = g_forceNewSlots &&
                g_seededNodes != kUnknownCount &&
                p.nodeId >= g_seededNodes;
            volatile uint8_t* slot = state + static_cast<uintptr_t>(p.nodeId) * 2u;
            const uint8_t oldContent = slot[0];
            const uint8_t oldStatus = slot[1];
            const bool statusWanted = g_forceStatus && p.status != 0xFF;
            const uint8_t nextStatus = statusWanted ? p.status : oldStatus;
            const bool needsWrite = newSlot ||
                oldContent != p.content ||
                (statusWanted && oldStatus != nextStatus);
            if (needsWrite) {
                if (write) {
                    slot[0] = p.content;
                    if (statusWanted)
                        slot[1] = nextStatus;
                }
                touchedNodes++;
                HookLog("[ffx-hooks] TrueNewNode %s node=%u %02X/%02X -> %02X/%02X%s%s",
                    tag, p.nodeId, oldContent, oldStatus, p.content, nextStatus,
                    newSlot ? " (new-slot)" : "",
                    write ? "" : " (observe)");
            }
        }
        for (int i = 0; i < g_linkCount; i++) {
            const TrueNewLinkPatch& p = g_links[i];
            if (p.linkId >= 1024) continue;
            if (g_currentLinks != kUnknownCount && p.linkId >= g_currentLinks) {
                HookLog("[ffx-hooks] TrueNewNode WARN %s link=%u outside currentLinks=%u",
                    tag, p.linkId, g_currentLinks);
                continue;
            }
            const bool newSlot = g_forceNewSlots &&
                g_seededLinks != kUnknownCount &&
                p.linkId >= g_seededLinks;
            volatile uint8_t* slot = state + 0xA00u + p.linkId;
            const uint8_t oldState = *slot;
            if (newSlot || oldState != p.state) {
                if (write)
                    *slot = p.state;
                touchedLinks++;
                HookLog("[ffx-hooks] TrueNewNode %s link=%u %02X -> %02X%s%s",
                    tag, p.linkId, oldState, p.state,
                    newSlot ? " (new-slot)" : "",
                    write ? "" : " (observe)");
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode WARN exception while applying runtime state");
    }

    if (touchedNodes || touchedLinks)
        HookLog("[ffx-hooks] TrueNewNode %s applied nodes=%d links=%d", tag, touchedNodes, touchedLinks);
}

static void ApplyManifestOnce(const char* tag, bool write) {
    ApplyManifestToState(RuntimeStateBase(), tag, write);
}

#ifdef FFXHOOKS_HAVE_POLYHOOK
static int __cdecl InitRuntimeState_Shim(int16_t* state) {
    const int rv = reinterpret_cast<InitRuntimeStateFn>(g_initTrampoline)(state);
    volatile uint8_t* bytes = reinterpret_cast<volatile uint8_t*>(state);
    LogRuntimeProbe(bytes, "after-A53DE0/pre");
    ApplyManifestToState(bytes, "after-A53DE0", g_writeInit);
    LogRuntimeProbe(bytes, "after-A53DE0/post");
    return rv;
}

static int __cdecl LayoutLoad_Shim() {
    const int rv = reinterpret_cast<NoArgIntFn>(g_layoutTrampoline)();
    LogMenuProbe("after-A45570/pre");
    PatchMenuRecordsForNewSlots("after-A45570", g_writeInit);
    LogRuntimeProbe(RuntimeStateBase(), "after-A45570/pre");
    ApplyManifestOnce("after-A45570", g_writeInit);
    LogRuntimeProbe(RuntimeStateBase(), "after-A45570/post");
    LogMenuProbe("after-A45570/post");
    return rv;
}

static int __cdecl DefaultState_Shim() {
    const int rv = reinterpret_cast<NoArgIntFn>(g_defaultStateTrampoline)();
    LogRuntimeProbe(RuntimeStateBase(), "after-A47210/pre");
    ApplyManifestOnce("after-A47210", g_writeInit);
    LogRuntimeProbe(RuntimeStateBase(), "after-A47210/post");
    return rv;
}

static int __cdecl ApplyStateToMenu_Shim() {
    PatchMenuRecordsForNewSlots("before-A49590", g_writeApply);
    LogMenuProbe("before-A49590/pre");
    LogRuntimeProbe(RuntimeStateBase(), "before-A49590/pre");
    ApplyManifestOnce("before-A49590", g_writeApply);
    LogRuntimeProbe(RuntimeStateBase(), "before-A49590/post");
    int rv = reinterpret_cast<NoArgIntFn>(g_applyTrampoline)();
    LogRuntimeProbe(RuntimeStateBase(), "after-A49590/pass1");
    if (g_writeApply && g_doubleApplyPass) {
        ApplyManifestOnce("before-A49590-pass2", true);
        rv = reinterpret_cast<NoArgIntFn>(g_applyTrampoline)();
        LogRuntimeProbe(RuntimeStateBase(), "after-A49590/pass2");
    }
    return rv;
}

static int __cdecl AdjacencyBuild_Shim() {
    const int rv = reinterpret_cast<NoArgIntFn>(g_adjacencyTrampoline)();
    ApplyManifestOnce("after-A5B140", g_writeApply);
    LogRuntimeProbe(RuntimeStateBase(), "after-A5B140");
    return rv;
}

static int __cdecl RecomputeStats_Shim() {
    PatchMenuRecordsForNewSlots("before-A54860", g_writeApply);
    ApplyManifestOnce("before-A54860", g_writeApply);
    LogMenuProbe("before-A54860");
    LogRuntimeProbe(RuntimeStateBase(), "before-A54860");
    int rv = -1;
    __try {
        rv = reinterpret_cast<NoArgIntFn>(g_recomputeTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside A54860 trampoline");
        return -1;
    }
    LogExitSnapshot("after-A54860");
    return rv;
}

static int __cdecl DeactivateReturnToFieldUi_Shim() {
    LogExitSnapshot("entry-8E27E0");
    int rv = -1;
    __try {
        rv = reinterpret_cast<NoArgIntFn>(g_deactivateTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside 8E27E0 trampoline");
        return -1;
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe return-8E27E0 rv=%d", rv);
    return rv;
}

static int __cdecl UiMode19Deactivate_Shim() {
    HookLog("[ffx-hooks] TrueNewNode exit-probe entry-8E27B0");
    int rv = -1;
    __try {
        rv = reinterpret_cast<NoArgIntFn>(g_uiMode19DeactTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside 8E27B0 trampoline");
        return -1;
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe return-8E27B0 rv=%d", rv);
    return rv;
}

static int __cdecl ReleaseGpuOnExit_Shim() {
    HookLog("[ffx-hooks] TrueNewNode exit-probe entry-A54720");
    int rv = -1;
    __try {
        rv = reinterpret_cast<NoArgIntFn>(g_releaseGpuTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside A54720 trampoline");
        return -1;
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe return-A54720 rv=%d", rv);
    return rv;
}

static int __cdecl ExitFullUiFlush_Shim() {
    LogExitSnapshot("entry-A54660");
    HookLog("[ffx-hooks] TrueNewNode exit-probe entry-A54660 (wraps A51340 draw loop)");
    int rv = -1;
    __try {
        rv = reinterpret_cast<NoArgIntFn>(g_exitUiFlushTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside A54660 trampoline (may be A51340 mesh OOB)");
        return -1;
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe return-A54660 rv=%d", rv);
    return rv;
}

static int __cdecl ExitConfirmHandler_Shim(int a1, int a2, int a3, int a4) {
    HookLog("[ffx-hooks] TrueNewNode exit-probe entry-A56060 a3=%d a4=%d", a3, a4);
    if (a4 != 0 && a3 == 0) {
        InterlockedExchange(&g_postExitTickLogged, 0);
        InterlockedExchange(&g_postExitProbeArmed, 1);
        InterlockedExchange(&g_postExitWithinA53570Tick, 1);
        InterlockedExchange(&g_exitPumpEarlyHandoffDone, 0);
        InterlockedExchange(&g_deferFieldGateClear, 0);
        HookLog("[ffx-hooks] TrueNewNode post-exit probe armed (A53570 tail + UI mode pump paths)");
        ClearDialogSlotIfFlagged("exit-prep-A56060");
    }
    // Confirmed exit (a4!=0 && a3==0): patch menu + state before vanilla A5BB70 save loop.
    if (a4 != 0 && a3 == 0 && g_writeApply && !g_observeOnly) {
        PatchMenuRecordsForNewSlots("exit-prep-A56060", true);
        ApplyManifestOnce("exit-prep-A56060", true);
        LogExitSnapshot("exit-prep-A56060");
    }
    using ExitFn = int(__cdecl*)(int, int, int, int);
    int rv = -1;
    __try {
        rv = reinterpret_cast<ExitFn>(g_exitConfirmTrampoline)(a1, a2, a3, a4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside A56060 trampoline");
        return -1;
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe return-A56060 rv=%d", rv);
    if (a4 != 0 && a3 == 0 && rv == 2) {
        LogExitSnapshot("post-A56060-return");
        LogDialogSlot("post-A56060-return");
    }
    return rv;
}

static int16_t __cdecl DialogDispatch_Shim() {
    const bool armed = PostExitProbeActive();
    int16_t rv = -1;
    __try {
        rv = reinterpret_cast<int16_t(__cdecl*)()>(g_dialogDispatchTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside A583E0 trampoline");
        return -1;
    }
    if (armed) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe return-A583E0 rv=%d", static_cast<int>(rv));
        LogExitSnapshot("return-A583E0");
        LogDialogSlot("return-A583E0");
    }
    return rv;
}

static int __cdecl AnimIndexTick_Shim() {
    if (!PostExitProbeActive()) {
        return reinterpret_cast<NoArgIntFn>(g_animIndexTrampoline)();
    }
    volatile uint8_t* menu = AbmapMenuStateBase();
    uint32_t animTick = 0;
    if (menu) {
        __try {
            animTick = *reinterpret_cast<volatile uint32_t*>(menu + kAbmapMenuAnimTickOffset);
        } __except (EXCEPTION_EXECUTE_HANDLER) { animTick = 0; }
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe entry-A47440 anim71320=0x%08X idx=%u",
        animTick, animTick & 0x1Fu);
    int rv = -1;
    __try {
        rv = reinterpret_cast<NoArgIntFn>(g_animIndexTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside A47440 trampoline");
        return -1;
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe return-A47440 rv=%d", rv);
    return rv;
}

static int __cdecl RenderFrameHook_Shim(int ctx) {
    if (!PostExitProbeActive()) {
        return reinterpret_cast<RenderFrameHookFn>(g_renderFrameTrampoline)(ctx);
    }
    int phase28 = 0;
    __try {
        if (ctx != 0)
            phase28 = *reinterpret_cast<volatile int*>(static_cast<uintptr_t>(ctx) + 28);
    } __except (EXCEPTION_EXECUTE_HANDLER) { phase28 = -1; }
    HookLog("[ffx-hooks] TrueNewNode exit-probe entry-8E2720 ctx=0x%08X phase28=%d",
        static_cast<unsigned>(ctx), phase28);
    int rv = -1;
    __try {
        rv = reinterpret_cast<RenderFrameHookFn>(g_renderFrameTrampoline)(ctx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside 8E2720 trampoline");
        return -1;
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe return-8E2720 rv=%d", rv);
    return rv;
}

static int __cdecl RenderTeardownDispatch_Shim() {
    if (!PostExitProbeActive()) {
        return reinterpret_cast<NoArgIntFn>(g_renderTeardownTrampoline)();
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe entry-A54560");
    LogAbmapTickContext("entry-A54560");
    int rv = -1;
    __try {
        rv = reinterpret_cast<NoArgIntFn>(g_renderTeardownTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside A54560 trampoline");
        return -1;
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe return-A54560 rv=0x%08X", rv);
    return rv;
}

static int __cdecl UiModeTickLoop_Shim() {
    const bool armed = PostExitProbeActive();
    if (armed) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe entry-8AA1B0 (UpdateAllLayers)");
        LogDialogSlot("entry-8AA1B0");
    }
    int rv = -1;
    __try {
        rv = reinterpret_cast<NoArgIntFn>(g_uiModeTickTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside 8AA1B0 trampoline");
        return -1;
    }
    if (armed) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe return-8AA1B0 rv=0x%08X (tail=8AE0A0)", rv);
        HookLog("[ffx-hooks] TrueNewNode exit-probe awaiting 8AA240 / 8E0340 post-pump draw");
    }
    return rv;
}

static void __cdecl MenuDrawAllLayers_Shim() {
    const bool armed = PostExitProbeActive();
    if (armed) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe entry-8AA240 (DrawAllLayers) R26");
        LogDialogSlot("entry-8AA240");
        LogAbmapTickContext("entry-8AA240");
        ArmEarlyExitPumpHandoff("pre-8AA240");
        if (SkipDrawAllLayersOnExit()) {
            HookLog("[ffx-hooks] TrueNewNode exit-probe SKIP-8AA240 (skip_pump_tail / per-step skip flag)");
            return;
        }
    }
    __try {
        reinterpret_cast<void(__cdecl*)()>(g_menuDrawAllTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside 8AA240 trampoline");
        return;
    }
    if (armed) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe return-8AA240 (awaiting pump tail 8ABDF0/6392A0/642560)");
    }
}

static int __cdecl MenuCursorWidgetFlush_Shim() {
    if (!PostExitProbeActive()) {
        return reinterpret_cast<NoArgIntFn>(g_cursorWidgetFlushTrampoline)();
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe entry-8ABDF0 (cursor/widget flush)");
    LogDialogSlot("entry-8ABDF0");
    int rv = -1;
    __try {
        rv = reinterpret_cast<NoArgIntFn>(g_cursorWidgetFlushTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside 8ABDF0 trampoline");
        return -1;
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe return-8ABDF0 rv=0x%08X", rv);
    return rv;
}

static int __cdecl Menu2D_EndCaptureUpload_Shim() {
    if (!PostExitProbeActive()) {
        return reinterpret_cast<NoArgIntFn>(g_menu2dEndCaptureTrampoline)();
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe entry-6392A0 (EndCaptureAndUpload)");
    LogDialogSlot("entry-6392A0");
    ArmEarlyExitPumpHandoff("pre-6392A0");
    if (Skip6392A0OnExit()) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe SKIP-6392A0 (skip_pump_tail / per-step skip flag)");
        return 0;
    }
    int rv = -1;
    __try {
        rv = reinterpret_cast<NoArgIntFn>(g_menu2dEndCaptureTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside 6392A0 trampoline");
        return -1;
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe return-6392A0 rv=0x%08X gate=%d",
        rv, g_base ? *reinterpret_cast<volatile int*>(g_base + RVA_FFX_MENU_SUBSYSTEM_ACTIVE_FLAG) : -1);
    return rv;
}

static int __cdecl PhyreRenderFlushTextureBinds_Shim() {
    if (!PostExitProbeActive()) {
        return reinterpret_cast<NoArgIntFn>(g_phyreFlushBindsTrampoline)();
    }
    if (!DeferHandoffTo820C00() && !VanillaHandoffOnly() &&
        InterlockedCompareExchange(&g_exitPumpEarlyHandoffDone, 1, 1) != 1) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe entry-642560 fallback handoff (6392A0 missed)");
        ArmEarlyExitPumpHandoff("pre-642560-fallback");
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe entry-642560 (RenderFlushTextureBinds)");
    LogDialogSlot("entry-642560");
    if (Skip642560OnExit()) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe SKIP-642560 (skip_pump_tail / per-step skip flag)");
        LogFieldHandoffContext("skip-642560");
        return 0;
    }
    int rv = -1;
    __try {
        rv = reinterpret_cast<NoArgIntFn>(g_phyreFlushBindsTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside 642560 trampoline");
        return -1;
    }
    if (static_cast<unsigned>(rv) > 0x0000FFFFu) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe WARN return-642560 rv=0x%08X looks like pointer/garbage (R16 skip-A54720 refuted)", rv);
    } else {
        HookLog("[ffx-hooks] TrueNewNode exit-probe return-642560 rv=0x%08X gate-at-flush=%d (awaiting 8AA5C0)",
            rv, g_base ? *reinterpret_cast<volatile int*>(g_base + RVA_FFX_MENU_SUBSYSTEM_ACTIVE_FLAG) : -1);
    }
    return rv;
}

static int __cdecl MenuPumpAliveCheck_Shim() {
    const bool armed = PostExitProbeActive();
    if (!armed) {
        return reinterpret_cast<NoArgIntFn>(g_menuPumpAliveTrampoline)();
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe entry-8AA5C0 (pump alive/teardown)");
    LogDialogSlot("entry-8AA5C0");
    int rv = -1;
    __try {
        rv = reinterpret_cast<NoArgIntFn>(g_menuPumpAliveTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside 8AA5C0 trampoline");
        return -1;
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe return-8AA5C0 rv=0x%08X (pump epilogue OK)", rv);
    LogFieldHandoffContext("return-8AA5C0-before");
    if (DeferHandoffTo820C00()) {
        HookLog("[ffx-hooks] TrueNewNode field-restore return-8AA5C0 DEFER full handoff -> next 820C00 entry (R21 defer flag)");
        InterlockedExchange(&g_deferFieldGateClear, 1);
        LogFieldHandoffContext("return-8AA5C0-after-defer");
    } else if (!VanillaHandoffOnly()) {
        HookLog("[ffx-hooks] TrueNewNode post-pump handoff return-8AA5C0 (early-handoff=%ld)",
            static_cast<long>(InterlockedCompareExchange(&g_exitPumpEarlyHandoffDone, 0, 0)));
        NotifyRenderEngineAfterExit("return-8AA5C0");
        LogFieldHandoffContext("return-8AA5C0-after");
    }
    InterlockedExchange(&g_postExitFieldWatch, kPostExitFieldWatchMax);
    InterlockedExchange(&g_postExitTailWatch, kPostExitTailWatchMax);
    InterlockedExchange(&g_postExitProbeArmed, 0);
    HookLog("[ffx-hooks] TrueNewNode exit-probe field-watch armed (%d ticks) + tail-watch (%d)",
        kPostExitFieldWatchMax, kPostExitTailWatchMax);
    return rv;
}

static void DecrementFieldWatch() {
    const LONG left = InterlockedDecrement(&g_postExitFieldWatch);
    if (left <= 0) {
        InterlockedExchange(&g_postExitFieldWatch, 0);
        HookLog("[ffx-hooks] TrueNewNode exit-probe field-watch done");
    }
}

static void DecrementTailWatch(const char* doneTag) {
    const LONG left = InterlockedDecrement(&g_postExitTailWatch);
    if (left <= 0) {
        InterlockedExchange(&g_postExitTailWatch, 0);
        if (doneTag)
            HookLog("[ffx-hooks] TrueNewNode exit-probe tail-watch done");
    }
}

static void __cdecl MenuPumpEntry_Shim(int ctx, int a2, int screen) {
    const LONG watchLeft = InterlockedCompareExchange(&g_postExitFieldWatch, 0, 0);
    const LONG tailLeft = InterlockedCompareExchange(&g_postExitTailWatch, 0, 0);
    if (watchLeft > 0) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe entry-8AAFE0 ctx=0x%08X a2=%d screen=%d watch=%ld",
            static_cast<unsigned>(ctx), a2, screen, static_cast<long>(watchLeft));
        LogFieldHandoffContext("entry-8AAFE0");
    } else if (tailLeft > 0) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe tail entry-8AAFE0 ctx=0x%08X tail=%ld",
            static_cast<unsigned>(ctx), static_cast<long>(tailLeft));
        LogFieldHandoffContext("tail-8AAFE0");
    }
    __try {
        reinterpret_cast<MenuPumpEntryFn>(g_menuPumpEntryTrampoline)(ctx, a2, screen);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside 8AAFE0 trampoline ctx=0x%08X",
            static_cast<unsigned>(ctx));
        return;
    }
    if (watchLeft > 0) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe return-8AAFE0 watch=%ld", static_cast<long>(watchLeft));
        DecrementFieldWatch();
    } else if (tailLeft > 0) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe tail return-8AAFE0 tail=%ld", static_cast<long>(tailLeft));
    }
}

static int __cdecl FieldServiceTick_Shim(float dt) {
    const LONG watchLeft = InterlockedCompareExchange(&g_postExitFieldWatch, 0, 0);
    const LONG tailLeft = InterlockedCompareExchange(&g_postExitTailWatch, 0, 0);
    const bool deferGate = DeferHandoffTo820C00() &&
        InterlockedCompareExchange(&g_deferFieldGateClear, 1, 1) == 1;
    if (deferGate) {
        HookLog("[ffx-hooks] TrueNewNode handoff entry-820C00 deferred (before 820de2, R21 defer flag)");
        LogFieldHandoffContext("entry-820C00-before-deferred-restore");
        CompleteDeferredFieldHandoff("entry-820C00-deferred");
        InterlockedExchange(&g_deferFieldGateClear, 0);
        LogFieldHandoffContext("entry-820C00-after-deferred-restore");
    }
    if (watchLeft > 0) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe entry-820C00 dt=%.4f watch=%ld",
            dt, static_cast<long>(watchLeft));
        LogFieldHandoffContext("entry-820C00");
        LogField3DPathProbe("entry-820C00");
    } else if (tailLeft > 0 && (tailLeft == kPostExitTailWatchMax || tailLeft <= 3)) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe tail entry-820C00 dt=%.4f tail=%ld",
            dt, static_cast<long>(tailLeft));
        LogFieldHandoffContext("tail-820C00");
        LogField3DPathProbe("tail-820C00");
    }
    int rv = 0;
    __try {
        rv = reinterpret_cast<FieldTickFn>(g_fieldServiceTickTrampoline)(dt);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside 820C00 trampoline tail=%ld watch=%ld",
            static_cast<long>(tailLeft), static_cast<long>(watchLeft));
        return -1;
    }
    if (watchLeft > 0) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe return-820C00 rv=0x%08X watch=%ld (%s)",
            rv, static_cast<long>(watchLeft), rv ? "field-alive" : "field-dead");
        LogFieldHandoffContext("return-820C00");
        DecrementFieldWatch();
    } else if (tailLeft > 0 && (tailLeft == kPostExitTailWatchMax || tailLeft <= 3)) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe tail return-820C00 rv=0x%08X tail=%ld",
            rv, static_cast<long>(tailLeft));
        LogFieldHandoffContext("tail-return-820C00");
    }
    if (tailLeft > 0)
        DecrementTailWatch(tailLeft == 1 ? "tail" : nullptr);
    return rv;
}

static int __cdecl PhyreBindRenderTargetStack_Shim(int mode) {
    if (!PostExitPresentProbeActive()) {
        return reinterpret_cast<IntArgFn>(g_phyreBindRtTrampoline)(mode);
    }
    HookLog("[ffx-hooks] TrueNewNode present-probe entry-640120 mode=%d", mode);
    LogPhyrePresentContext("entry-640120");
    int rv = -1;
    __try {
        rv = reinterpret_cast<IntArgFn>(g_phyreBindRtTrampoline)(mode);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside 640120 trampoline mode=%d", mode);
        return -1;
    }
    HookLog("[ffx-hooks] TrueNewNode present-probe return-640120 mode=%d rv=0x%08X", mode, rv);
    LogPhyrePresentContext("return-640120");
    return rv;
}

static int __cdecl FieldSceneDrawDispatch_Shim() {
    if (!PostExitPresentProbeActive()) {
        return reinterpret_cast<NoArgIntFn>(g_fieldSceneDrawTrampoline)();
    }
    HookLog("[ffx-hooks] TrueNewNode present-probe entry-82BCD0 (field 3D draw dispatch)");
    LogPhyrePresentContext("entry-82BCD0");
    int rv = -1;
    __try {
        rv = reinterpret_cast<NoArgIntFn>(g_fieldSceneDrawTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside 82BCD0 trampoline");
        return -1;
    }
    HookLog("[ffx-hooks] TrueNewNode present-probe return-82BCD0 rv=0x%08X", rv);
    LogPhyrePresentContext("return-82BCD0");
    return rv;
}

static int __cdecl RenderEngineNotifyFrameEnd_Shim(int phase) {
    if (!PostExitPresentProbeActive()) {
        return reinterpret_cast<IntArgFn>(g_renderNotifyFrameEndTrampoline)(phase);
    }
    HookLog("[ffx-hooks] TrueNewNode present-probe entry-887E70 phase=%d (frame epilogue notify)", phase);
    LogPhyrePresentContext("entry-887E70");
    int rv = -1;
    __try {
        rv = reinterpret_cast<IntArgFn>(g_renderNotifyFrameEndTrampoline)(phase);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside 887E70 trampoline phase=%d", phase);
        return -1;
    }
    HookLog("[ffx-hooks] TrueNewNode present-probe return-887E70 phase=%d rv=0x%08X", phase, rv);
    LogPhyrePresentContext("return-887E70");
    return rv;
}

static int __cdecl MenuPoolUpdateLayer_Shim(int layer) {
    if (!PostExitProbeActive()) {
        return reinterpret_cast<IntArgFn>(g_menuPoolUpdateTrampoline)(layer);
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe entry-8A91E0 layer=%d", layer);
    int rv = -1;
    __try {
        rv = reinterpret_cast<IntArgFn>(g_menuPoolUpdateTrampoline)(layer);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside 8A91E0 layer=%d", layer);
        return -1;
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe return-8A91E0 layer=%d rv=%d", layer, rv);
    return rv;
}

static void __cdecl FieldUiDispatchSlot1_Shim(int ctx) {
    if (!PostExitProbeActive()) {
        reinterpret_cast<UiModeCtxFn>(g_fieldUiDispatchTrampoline)(ctx);
        return;
    }
    int phase28 = 0;
    __try {
        if (ctx != 0)
            phase28 = *reinterpret_cast<volatile int*>(static_cast<uintptr_t>(ctx) + 28);
    } __except (EXCEPTION_EXECUTE_HANDLER) { phase28 = -1; }
    HookLog("[ffx-hooks] TrueNewNode exit-probe entry-8E0340 ctx=0x%08X phase28=%d",
        static_cast<unsigned>(ctx), phase28);
    __try {
        reinterpret_cast<UiModeCtxFn>(g_fieldUiDispatchTrampoline)(ctx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside 8E0340 trampoline");
        return;
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe return-8E0340");
}

static int __cdecl PartySaveSliceTick_Shim(int a1) {
    if (!PostExitProbeActive()) {
        return reinterpret_cast<IntArgFn>(g_partySaveSliceTrampoline)(a1);
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe entry-8AE0A0 a1=%d (post-mode-loop same frame)", a1);
    LogExitSnapshot("entry-8AE0A0");
    int rv = -1;
    __try {
        rv = reinterpret_cast<IntArgFn>(g_partySaveSliceTrampoline)(a1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside 8AE0A0 trampoline");
        return -1;
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe return-8AE0A0 rv=%d", rv);
    return rv;
}

static void __cdecl UiModeActivateSlot_Shim(int slot, int a2) {
    const bool armed = PostExitProbeActive();
    if (armed) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe entry-8AA0B0 slot=%d a2=%d", slot, a2);
    }
    __try {
        reinterpret_cast<UiModeActivateFn>(g_uiModeActivateTrampoline)(slot, a2);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside 8AA0B0 slot=%d", slot);
        return;
    }
    if (armed) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe return-8AA0B0 slot=%d", slot);
    }
}

static void __cdecl UiModeSlot20Handoff_Shim(int ctx) {
    if (!PostExitProbeActive()) {
        reinterpret_cast<UiModeCtxFn>(g_slot20HandoffTrampoline)(ctx);
        return;
    }
    int phase28 = 0;
    __try {
        if (ctx != 0)
            phase28 = *reinterpret_cast<volatile int*>(static_cast<uintptr_t>(ctx) + 28);
    } __except (EXCEPTION_EXECUTE_HANDLER) { phase28 = -1; }
    HookLog("[ffx-hooks] TrueNewNode exit-probe entry-8E2870 ctx=0x%08X phase28=%d",
        static_cast<unsigned>(ctx), phase28);
    __try {
        reinterpret_cast<UiModeCtxFn>(g_slot20HandoffTrampoline)(ctx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside 8E2870 trampoline");
        return;
    }
    HookLog("[ffx-hooks] TrueNewNode exit-probe return-8E2870");
}

static int __cdecl UpdateCameraScroll_Shim() {
    const bool probeTick = InterlockedCompareExchange(&g_postExitProbeArmed, 1, 1) == 1;
    LONG tickIndex = 0;
    if (probeTick) {
        tickIndex = InterlockedIncrement(&g_postExitTickLogged);
        if (tickIndex <= kPostExitTickLogMax) {
            HookLog("[ffx-hooks] TrueNewNode exit-probe entry-A53570 tick=%ld", static_cast<long>(tickIndex));
            LogExitSnapshot("entry-A53570");
            LogAbmapTickContext("entry-A53570");
        } else if (tickIndex == kPostExitTickLogMax + 1) {
            InterlockedExchange(&g_postExitProbeArmed, 0);
        }
    }
    int rv = -1;
    __try {
        rv = reinterpret_cast<NoArgIntFn>(g_cameraScrollTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside A53570 trampoline tick=%ld",
            static_cast<long>(tickIndex));
        return -1;
    }
    if (probeTick && tickIndex > 0 && tickIndex <= kPostExitTickLogMax) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe return-A53570 tick=%ld rv=%d",
            static_cast<long>(tickIndex), rv);
    }
    if (InterlockedCompareExchange(&g_postExitWithinA53570Tick, 1, 1) == 1) {
        HookLog("[ffx-hooks] TrueNewNode exit-probe return-A53570-within-tick rv=%d", rv);
        LogExitSnapshot("return-A53570-within-tick");
        InterlockedExchange(&g_postExitWithinA53570Tick, 0);
        HookLog("[ffx-hooks] TrueNewNode exit-probe awaiting 8AA240 / 8E0340 / 8A91E0 post-pump");
    }
    return rv;
}

static int __cdecl SaveMenuToState_Shim() {
    PatchMenuRecordsForNewSlots("before-A5BB70", g_writeApply);
    ApplyManifestOnce("before-A5BB70", g_writeApply);
    LogMenuProbe("before-A5BB70");
    LogRuntimeProbe(RuntimeStateBase(), "before-A5BB70");
    int rv = -1;
    __try {
        rv = reinterpret_cast<NoArgIntFn>(g_saveTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] TrueNewNode FATAL exception inside A5BB70 trampoline");
        return -1;
    }
    // Observe save by default: only rewrite after vanilla save when WRITE_SAVE=1.
    ApplyManifestOnce("after-A5BB70", g_writeSave);
    LogRuntimeProbe(RuntimeStateBase(), "after-A5BB70");
    return rv;
}

static bool InstallDetour(uintptr_t targetVa, uint64_t hookFn, uint64_t* trampoline, PLH::x86Detour** detour, const char* label) {
    try {
        *detour = new PLH::x86Detour(static_cast<uint64_t>(targetVa), hookFn, trampoline);
        if (!(*detour)->hook()) {
            HookLog("[ffx-hooks] WARN TrueNewNode %s detour hook() false @0x%08X", label, static_cast<unsigned>(targetVa));
            delete *detour;
            *detour = nullptr;
            *trampoline = 0;
            return false;
        }
        HookLog("[ffx-hooks] TrueNewNode %s detour ok @0x%08X tramp=0x%llX",
            label, static_cast<unsigned>(targetVa), static_cast<unsigned long long>(*trampoline));
        return true;
    } catch (const std::exception& ex) {
        HookLog("[ffx-hooks] WARN TrueNewNode %s detour exception: %s", label, ex.what());
    } catch (...) {
        HookLog("[ffx-hooks] WARN TrueNewNode %s detour unknown exception", label);
    }
    delete *detour;
    *detour = nullptr;
    *trampoline = 0;
    return false;
}

static void RemoveDetour(PLH::x86Detour** detour, uint64_t* trampoline) {
    if (*detour) {
        (*detour)->unHook();
        delete *detour;
        *detour = nullptr;
    }
    if (trampoline) *trampoline = 0;
}
#endif

} // namespace

SphereGridTrueNewNodeInstallResult InstallSphereGridTrueNewNodeHook(uintptr_t base, SphereGridTrueNewNodeLogFn log) {
    SphereGridTrueNewNodeInstallResult result = { false, 0, 0 };
    if (g_installed) {
        result.ok = true;
        result.nodeCount = g_nodeCount;
        result.linkCount = g_linkCount;
        return result;
    }

    g_base = base;
    g_logFn = log;
    if (!LoadManifest()) {
        HookLog("[ffx-hooks] TrueNewNode disabled: no manifest rows");
        return result;
    }
    ConfigurePhaseModes();
    HookLog("[ffx-hooks] TrueNewNode v5.27-R27 observeOnly=%d writeInit=%d writeApply=%d writeSave=%d doubleApply=%d forceNewSlots=%d patchMenu=%d forceStatus=%d",
        g_observeOnly ? 1 : 0,
        g_writeInit ? 1 : 0,
        g_writeApply ? 1 : 0,
        g_writeSave ? 1 : 0,
        g_doubleApplyPass ? 1 : 0,
        g_forceNewSlots ? 1 : 0,
        g_patchMenuRecords ? 1 : 0,
        g_forceStatus ? 1 : 0);

#ifdef FFXHOOKS_HAVE_POLYHOOK
    int detours = 0;
    detours += InstallDetour(base + RVA_FFX_SPHERE_GRID_INIT_RUNTIME_STATE,
        reinterpret_cast<uint64_t>(&InitRuntimeState_Shim), &g_initTrampoline, &g_initDetour, "A53DE0-init") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_ABMAP_LOAD_LAYOUT,
        reinterpret_cast<uint64_t>(&LayoutLoad_Shim), &g_layoutTrampoline, &g_layoutDetour, "A45570-layout") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_SPHERE_GRID_LOAD_DEFAULT_STATE,
        reinterpret_cast<uint64_t>(&DefaultState_Shim), &g_defaultStateTrampoline, &g_defaultStateDetour, "A47210-default") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_ABMAP_APPLY_STATE_TO_MENU,
        reinterpret_cast<uint64_t>(&ApplyStateToMenu_Shim), &g_applyTrampoline, &g_applyDetour, "A49590-apply") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_ABMAP_BUILD_ADJACENCY,
        reinterpret_cast<uint64_t>(&AdjacencyBuild_Shim), &g_adjacencyTrampoline, &g_adjacencyDetour, "A5B140-adj") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_ABMAP_RECOMPUTE_STATS,
        reinterpret_cast<uint64_t>(&RecomputeStats_Shim), &g_recomputeTrampoline, &g_recomputeDetour, "A54860-recompute") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_ABMAP_SAVE_MENU_TO_STATE,
        reinterpret_cast<uint64_t>(&SaveMenuToState_Shim), &g_saveTrampoline, &g_saveDetour, "A5BB70-save") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_ABMAP_DEACTIVATE_RETURN_TO_FIELD,
        reinterpret_cast<uint64_t>(&DeactivateReturnToFieldUi_Shim), &g_deactivateTrampoline, &g_deactivateDetour, "8E27E0-deactivate") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_ABMAP_UIMODE19_DEACTIVATE_CB,
        reinterpret_cast<uint64_t>(&UiMode19Deactivate_Shim), &g_uiMode19DeactTrampoline, &g_uiMode19DeactDetour, "8E27B0-uimode19-deact") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_ABMAP_RELEASE_GPU_ON_EXIT,
        reinterpret_cast<uint64_t>(&ReleaseGpuOnExit_Shim), &g_releaseGpuTrampoline, &g_releaseGpuDetour, "A54720-release-gpu") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_ABMAP_EXIT_FULL_UI_FLUSH,
        reinterpret_cast<uint64_t>(&ExitFullUiFlush_Shim), &g_exitUiFlushTrampoline, &g_exitUiFlushDetour, "A54660-ui-flush") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_ABMAP_EXIT_CONFIRM_HANDLER,
        reinterpret_cast<uint64_t>(&ExitConfirmHandler_Shim), &g_exitConfirmTrampoline, &g_exitConfirmDetour, "A56060-exit") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_ABMAP_UPDATE_CAMERA_SCROLL,
        reinterpret_cast<uint64_t>(&UpdateCameraScroll_Shim), &g_cameraScrollTrampoline, &g_cameraScrollDetour, "A53570-camera-scroll") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_ABMAP_DIALOG_DISPATCH,
        reinterpret_cast<uint64_t>(&DialogDispatch_Shim), &g_dialogDispatchTrampoline, &g_dialogDispatchDetour, "A583E0-dialog") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_ABMAP_ANIM_INDEX_TICK,
        reinterpret_cast<uint64_t>(&AnimIndexTick_Shim), &g_animIndexTrampoline, &g_animIndexDetour, "A47440-anim") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_ABMAP_RENDER_FRAME_HOOK,
        reinterpret_cast<uint64_t>(&RenderFrameHook_Shim), &g_renderFrameTrampoline, &g_renderFrameDetour, "8E2720-render") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_ABMAP_EXIT_RENDER_TEARDOWN,
        reinterpret_cast<uint64_t>(&RenderTeardownDispatch_Shim), &g_renderTeardownTrampoline, &g_renderTeardownDetour, "A54560-teardown") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_UIMODE_TICK_LOOP,
        reinterpret_cast<uint64_t>(&UiModeTickLoop_Shim), &g_uiModeTickTrampoline, &g_uiModeTickDetour, "8AA1B0-uimode-pump") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_UIMODE_ACTIVATE_SLOT,
        reinterpret_cast<uint64_t>(&UiModeActivateSlot_Shim), &g_uiModeActivateTrampoline, &g_uiModeActivateDetour, "8AA0B0-activate") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_UIMODE_SLOT20_HANDOFF,
        reinterpret_cast<uint64_t>(&UiModeSlot20Handoff_Shim), &g_slot20HandoffTrampoline, &g_slot20HandoffDetour, "8E2870-slot20") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_PARTY_SAVE_SLICE_TICK,
        reinterpret_cast<uint64_t>(&PartySaveSliceTick_Shim), &g_partySaveSliceTrampoline, &g_partySaveSliceDetour, "8AE0A0-party-save") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_MENU_DRAW_ALL_LAYERS,
        reinterpret_cast<uint64_t>(&MenuDrawAllLayers_Shim), &g_menuDrawAllTrampoline, &g_menuDrawAllDetour, "8AA240-draw") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_MENU_POOL_UPDATE_LAYER,
        reinterpret_cast<uint64_t>(&MenuPoolUpdateLayer_Shim), &g_menuPoolUpdateTrampoline, &g_menuPoolUpdateDetour, "8A91E0-layer") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_FIELD_UI_DISPATCH_SLOT1,
        reinterpret_cast<uint64_t>(&FieldUiDispatchSlot1_Shim), &g_fieldUiDispatchTrampoline, &g_fieldUiDispatchDetour, "8E0340-field") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_MENU_CURSOR_WIDGET_FLUSH,
        reinterpret_cast<uint64_t>(&MenuCursorWidgetFlush_Shim), &g_cursorWidgetFlushTrampoline, &g_cursorWidgetFlushDetour, "8ABDF0-cursor") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_MENU2D_END_CAPTURE_AND_UPLOAD,
        reinterpret_cast<uint64_t>(&Menu2D_EndCaptureUpload_Shim), &g_menu2dEndCaptureTrampoline, &g_menu2dEndCaptureDetour, "6392A0-upload") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_PHYRE_RENDER_FLUSH_TEXTURE_BINDS,
        reinterpret_cast<uint64_t>(&PhyreRenderFlushTextureBinds_Shim), &g_phyreFlushBindsTrampoline, &g_phyreFlushBindsDetour, "642560-flush") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_MENU_PUMP_ALIVE_CHECK,
        reinterpret_cast<uint64_t>(&MenuPumpAliveCheck_Shim), &g_menuPumpAliveTrampoline, &g_menuPumpAliveDetour, "8AA5C0-alive") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_MENU_PUMP_ENTRY,
        reinterpret_cast<uint64_t>(&MenuPumpEntry_Shim), &g_menuPumpEntryTrampoline, &g_menuPumpEntryDetour, "8AAFE0-entry") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_SCENE_FIELD_SERVICE_TICK,
        reinterpret_cast<uint64_t>(&FieldServiceTick_Shim), &g_fieldServiceTickTrampoline, &g_fieldServiceTickDetour, "820C00-field") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_PHYRE_BIND_RENDER_TARGET_STACK,
        reinterpret_cast<uint64_t>(&PhyreBindRenderTargetStack_Shim), &g_phyreBindRtTrampoline, &g_phyreBindRtDetour, "640120-rtbind") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_FIELD_SCENE_DRAW_DISPATCH,
        reinterpret_cast<uint64_t>(&FieldSceneDrawDispatch_Shim), &g_fieldSceneDrawTrampoline, &g_fieldSceneDrawDetour, "82BCD0-fielddraw") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_RENDER_ENGINE_NOTIFY_FRAME_END,
        reinterpret_cast<uint64_t>(&RenderEngineNotifyFrameEnd_Shim), &g_renderNotifyFrameEndTrampoline, &g_renderNotifyFrameEndDetour, "887E70-notify") ? 1 : 0;

    if (detours == 0) {
        HookLog("[ffx-hooks] TrueNewNode disabled: no detours installed");
        return result;
    }
    HookLog("[ffx-hooks] TrueNewNode detours installed count=%d", detours);
#else
    HookLog("[ffx-hooks] WARN TrueNewNode needs PolyHook");
    return result;
#endif

    g_installed = true;
    result.ok = true;
    result.nodeCount = g_nodeCount;
    result.linkCount = g_linkCount;
    HookLog("[ffx-hooks] TrueNewNode installed base=0x%08X nodes=%d links=%d",
        static_cast<unsigned>(base), g_nodeCount, g_linkCount);
    return result;
}

bool RemoveSphereGridTrueNewNodeHook(SphereGridTrueNewNodeLogFn log) {
#ifdef FFXHOOKS_HAVE_POLYHOOK
    RemoveDetour(&g_initDetour, &g_initTrampoline);
    RemoveDetour(&g_layoutDetour, &g_layoutTrampoline);
    RemoveDetour(&g_defaultStateDetour, &g_defaultStateTrampoline);
    RemoveDetour(&g_applyDetour, &g_applyTrampoline);
    RemoveDetour(&g_adjacencyDetour, &g_adjacencyTrampoline);
    RemoveDetour(&g_recomputeDetour, &g_recomputeTrampoline);
    RemoveDetour(&g_saveDetour, &g_saveTrampoline);
    RemoveDetour(&g_deactivateDetour, &g_deactivateTrampoline);
    RemoveDetour(&g_uiMode19DeactDetour, &g_uiMode19DeactTrampoline);
    RemoveDetour(&g_releaseGpuDetour, &g_releaseGpuTrampoline);
    RemoveDetour(&g_exitUiFlushDetour, &g_exitUiFlushTrampoline);
    RemoveDetour(&g_exitConfirmDetour, &g_exitConfirmTrampoline);
    RemoveDetour(&g_cameraScrollDetour, &g_cameraScrollTrampoline);
    RemoveDetour(&g_dialogDispatchDetour, &g_dialogDispatchTrampoline);
    RemoveDetour(&g_animIndexDetour, &g_animIndexTrampoline);
    RemoveDetour(&g_renderFrameDetour, &g_renderFrameTrampoline);
    RemoveDetour(&g_renderTeardownDetour, &g_renderTeardownTrampoline);
    RemoveDetour(&g_uiModeTickDetour, &g_uiModeTickTrampoline);
    RemoveDetour(&g_uiModeActivateDetour, &g_uiModeActivateTrampoline);
    RemoveDetour(&g_slot20HandoffDetour, &g_slot20HandoffTrampoline);
    RemoveDetour(&g_partySaveSliceDetour, &g_partySaveSliceTrampoline);
    RemoveDetour(&g_menuDrawAllDetour, &g_menuDrawAllTrampoline);
    RemoveDetour(&g_menuPoolUpdateDetour, &g_menuPoolUpdateTrampoline);
    RemoveDetour(&g_fieldUiDispatchDetour, &g_fieldUiDispatchTrampoline);
    RemoveDetour(&g_cursorWidgetFlushDetour, &g_cursorWidgetFlushTrampoline);
    RemoveDetour(&g_menu2dEndCaptureDetour, &g_menu2dEndCaptureTrampoline);
    RemoveDetour(&g_phyreFlushBindsDetour, &g_phyreFlushBindsTrampoline);
    RemoveDetour(&g_menuPumpAliveDetour, &g_menuPumpAliveTrampoline);
    RemoveDetour(&g_menuPumpEntryDetour, &g_menuPumpEntryTrampoline);
    RemoveDetour(&g_fieldServiceTickDetour, &g_fieldServiceTickTrampoline);
    RemoveDetour(&g_phyreBindRtDetour, &g_phyreBindRtTrampoline);
    RemoveDetour(&g_fieldSceneDrawDetour, &g_fieldSceneDrawTrampoline);
    RemoveDetour(&g_renderNotifyFrameEndDetour, &g_renderNotifyFrameEndTrampoline);
#endif
    g_installed = false;
    InterlockedExchange(&g_postExitProbeArmed, 0);
    InterlockedExchange(&g_postExitFieldWatch, 0);
    InterlockedExchange(&g_postExitTailWatch, 0);
    InterlockedExchange(&g_deferFieldGateClear, 0);
    InterlockedExchange(&g_exitPumpEarlyHandoffDone, 0);
    InterlockedExchange(&g_postExitTickLogged, 0);
    InterlockedExchange(&g_postExitWithinA53570Tick, 0);
    g_nodeCount = 0;
    g_linkCount = 0;
    g_base = 0;
    g_logFn = nullptr;
    if (log) log("[ffx-hooks] TrueNewNode removed ok");
    return true;
}

bool IsSphereGridTrueNewNodeHookInstalled() {
    return g_installed;
}

} // namespace FfxHooks
