/* ffx-hooks.dll - FFX runtime hook layer (C++, PolyHook2/MinHook).
 *
 * The FF10 module loader loads this DLL from modules\. Player-facing gameplay writers
 * are default OFF. Families with validated executable profiles and signatures fail closed
 * at their documented boundaries; legacy and lab adapters retain their narrower gates.
 * Normal-context stop paths neutralize owned state, but dynamic FreeLibrary/hot unload is
 * unsupported. Validation-only mode gathers offline evidence without authorizing mutation.
 *
 * Release candidates are built with build_hooks.ps1 -WithPolyHook -Release and
 * vcpkg's x86-windows-static dependencies. The separate no-PolyHook build remains
 * a loader/shared-memory compatibility stub and installs no detours.
 *
 * This DLL neither replaces nor modifies ffx-probe.dll.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <share.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <float.h>
#include <intrin.h>

/* PolyHook2 and D3D types are present only in the full runtime build. */
#ifdef FFXHOOKS_HAVE_POLYHOOK
#  include <polyhook2/Detour/x86Detour.hpp>
#  include <polyhook2/MemProtector.hpp>
#  include <d3d11.h>
#  include <dxgi.h>
#  include <d3dcompiler.h>
#endif

#include "shared/ffx_addresses.h"
#include "shared/ffx_hooks_block.h"
#include "shared/Config.h"             // Unified INI/env/flag authority resolver.
#include "../FfxDinput8Probe/ffx_probe_block.h"
#include "hooks/BootSkipHook.h"
#include "hooks/MusicHook.h"
#include "hooks/NovaSuperDamageHook.h"
#include "hooks/RonsoPoolRuntime.h"
#include "hooks/NulWardHook.h"
#include "hooks/NulWardTeachHook.h"
#include "hooks/GridTeachHook.h"
#include "hooks/KimahriLancetDualGrantHook.h"
#include "hooks/PhaseTurnEdgeHook.h"
#include "hooks/PhaseTurnEdgeSidecar.h"
#include "hooks/ElementHook.h"
#include "hooks/AbilitySfxHook.h"
#include "hooks/ResolverLogHook.h"
#include "hooks/ResolverOwnerPolicy.h"
#include "hooks/FieldProbeHook.h"
#include "hooks/FieldScoutHook.h"
#include "hooks/BattleEndHook.h"
#include "hooks/ArenaProgressSidecar.h"
#include "hooks/ItemStackCapHook.h"
#include "hooks/DoubleTripleDropHook.h"
#include "hooks/SinCurseHook.h"
#include "hooks/ArenaComposeRestore.h"
#include "hooks/ArenaPlusComposePick.h"
#include "hooks/CustomMixRuntime.h"
#include "hooks/ArenaBrowserCore.h"
#include "hooks/ArenaMixPolicy.h"
#include "hooks/ArenaMixLibrary.h"
#include "hooks/ArenaBattleProgram.h"
#include "hooks/ArenaPlusGil.h"
#include "hooks/F7InLive.h"
#include "hooks/F7AiSwap.h"
#include "hooks/F7UnsafePrototypePolicy.h"
#include "hooks/F7UiCore.h"
#include "hooks/MinHookBatchCoordinator.h"
#include "hooks/SharedBattleRuntime.h"
#include "hooks/SeymourBattleHook.h"
#include "hooks/F8FlagCatalog.h"
#include "hooks/F8FlagsUiState.h"
#include "hooks/NativePortsHook.h"
#include "hooks/NativeGamepadHook.h"
#include "hooks/NativeLanguageHook.h"
#include "hooks/SinAiHook.h"
#include "hooks/EquipmentWorkshopRuntime.h"
#include "hooks/NativeSaveEvents.h"
#include "hooks/FmvSpeedHook.h"
#include "hooks/SinSpreadCore.h"
#include "hooks/F8RuntimeCore.h"
#include "hooks/InGameMenuDashboard.h"   // F8 dashboard renderer and input adapter.
#include "hooks/UnXBoosterHook.h"        // F8 booster/cheat runtime consumer (30 Hz).
#include "hooks/SpeedHackHook.h"         // F8: Ctrl+Shift+K 1x/2x/4x/8x cycle (gate boosters.speed_hack)
#include "hooks/DialogSkipHook.h"        // Dialog voice skip (gate input.dialog_skip).
#include "hooks/MaechenHook.h"           // default-off native F9 client
#ifdef FFXHOOKS_HAVE_POLYHOOK
#include "../NativeMenuShell/NativeMenuShell.h"   // Shared native F7/F8/F9 menu primitives.
#include "../BattlePhotoMode/PhotoModeActions.h"  // Aurora RAM-action bridge contract.
#endif
/* Hook modules included when their RVA is confirmed: */
/* #include "hooks/ElementHook.h" â€” now active for element_scan_dark.flag */

extern "C" __declspec(dllexport) const char* FF10HgetName(void) {
    return "ffx-hooks (Jarvis PolyHook2 engine hook layer)";
}

extern "C" __declspec(dllexport) const char* FF10HgetVer(void) {
    return "0.2.0-rt2-candidate";
}

/* â”€â”€ Address helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
static HMODULE g_module = NULL;
static uintptr_t g_base = 0;
static bool g_runtimeValidateOnly = false;
static inline uintptr_t rva(uintptr_t offset) { return g_base + offset; }
static bool AuroraFfxCodeAddress(uintptr_t address) {
    return g_base != 0 && address >= g_base && address < g_base + 0x08000000u;
}
static uint32_t AuroraFfxCodeRva(uintptr_t address) {
    return AuroraFfxCodeAddress(address) ? static_cast<uint32_t>(address - g_base) : 0;
}

/* â”€â”€ Fault probe (SGM exit-crash) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 * Logs the faulting instruction + accessed address (with FFX.exe RVA) for the
 * first few access violations. Diagnostic only: never handles, always passes
 * the exception on (EXCEPTION_CONTINUE_SEARCH) so behavior is unchanged. */
static void Log(const char* fmt, ...);
static LONG CALLBACK FfxFaultProbeVeh(EXCEPTION_POINTERS* info) {
    static volatile long s_count = 0;
    if (!info || !info->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&s_count) > 8) return EXCEPTION_CONTINUE_SEARCH;

    const uintptr_t pc = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    const ULONG_PTR* ei = info->ExceptionRecord->ExceptionInformation;
    const uintptr_t accessed = info->ExceptionRecord->NumberParameters >= 2
        ? static_cast<uintptr_t>(ei[1]) : 0;
    const char* op = info->ExceptionRecord->NumberParameters >= 1
        ? (ei[0] == 0 ? "READ" : ei[0] == 1 ? "WRITE" : "EXEC") : "?";
    Log("[ffx-hooks] FAULT AV %s pc=0x%08X (FFX rva=0x%08X%s) accessed=0x%08X\n",
        op, static_cast<unsigned>(pc), AuroraFfxCodeRva(pc),
        AuroraFfxCodeAddress(pc) ? "" : " NOT-FFX", static_cast<unsigned>(accessed));

    // Walk the EBP frame chain to recover the call stack (x86 frame-pointer based).
    // Each FFX rva pinpoints the teardown caller that triggered the corrupt free.
    if (CONTEXT* ctx = info->ContextRecord) {
        Log("[ffx-hooks] FAULT ctx eax=0x%08X ebx=0x%08X ecx=0x%08X edx=0x%08X esi=0x%08X edi=0x%08X esp=0x%08X ebp=0x%08X\n",
            ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx, ctx->Esi, ctx->Edi, ctx->Esp, ctx->Ebp);
        uintptr_t* frame = reinterpret_cast<uintptr_t*>(ctx->Ebp);
        for (int depth = 0; depth < 16 && frame; ++depth) {
            if (IsBadReadPtr(frame, 8)) break;
            const uintptr_t ret = frame[1];
            const uintptr_t next = frame[0];
            Log("[ffx-hooks] FAULT frame[%d] ret=0x%08X (FFX rva=0x%08X%s)\n",
                depth, static_cast<unsigned>(ret), AuroraFfxCodeRva(ret),
                AuroraFfxCodeAddress(ret) ? "" : " NOT-FFX");
            if (next <= reinterpret_cast<uintptr_t>(frame)) break; // stack grows down; stop on non-increasing
            frame = reinterpret_cast<uintptr_t*>(next);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* â”€â”€ Simple log (%TEMP%\ffx-hooks.log) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
static FILE* g_log = nullptr;

static void EarlyLogLine(const char* message) {
    char tmp[MAX_PATH] = {};
    char path[MAX_PATH] = {};
    const DWORD tmpLen = GetTempPathA(MAX_PATH, tmp);
    if (tmpLen == 0 || tmpLen >= MAX_PATH) return;
    lstrcpynA(path, tmp, MAX_PATH);
    lstrcatA(path, "ffx-hooks-early.log");

    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    DWORD written = 0;
    WriteFile(file, message, lstrlenA(message), &written, nullptr);
    CloseHandle(file);
}

static void Log(const char* fmt, ...) {
    char line[1024] = {};
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    OutputDebugStringA(line);
    if (g_log) {
        fputs(line, g_log);
        fflush(g_log);
    }
}

static void LogLine(const char* message) {
    Log("%s\n", message);
}
static std::atomic<ULONGLONG> g_startupBegin{0};
static void StartupTiming(const char* stage) {
    const auto begin=g_startupBegin.load();
    if(begin)Log("[ffx-hooks] StartupTiming stage=%s elapsed_ms=%llu\n",stage,
        static_cast<unsigned long long>(GetTickCount64()-begin));
}

static void OpenLog() {
    char tmp[MAX_PATH] = {};
    char path[MAX_PATH] = {};
    char cntPath[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tmp);
    _snprintf_s(path, _TRUNCATE, "%sffx-hooks.log", tmp);
    _snprintf_s(cntPath, _TRUNCATE, "%sffx-hooks.log.cnt", tmp);
    // Log rotation by open count (max 10 -- 2026-08-03): every 10th open, moves the log
    // current to ffx-hooks.log.oldN (accumulates history .old1..old9; .old1 overwritten after 9
    // cuts). The main log never accumulates more than ~10 sessions; the user deletes .old* whenever they want.
    int opens = 0;
    FILE* f = _fsopen(cntPath, "r", _SH_DENYNO);
    if (f) { if (fscanf(f, "%d", &opens) != 1) opens = 0; fclose(f); }
    ++opens;
    f = _fsopen(cntPath, "w", _SH_DENYNO);
    if (f) { fprintf(f, "%d", opens); fclose(f); }
    bool rotated = false;
    if (opens >= 10) {
        int slot = 1;
        char oldPath[MAX_PATH] = {};
        for (; slot <= 9; ++slot) {
            _snprintf_s(oldPath, _TRUNCATE, "%sffx-hooks.log.old%d", tmp, slot);
            if (GetFileAttributesA(oldPath) == INVALID_FILE_ATTRIBUTES) break;
        }
        _snprintf_s(oldPath, _TRUNCATE, "%sffx-hooks.log.old%d", tmp, slot);
        MoveFileExA(path, oldPath, MOVEFILE_REPLACE_EXISTING);
        f = _fsopen(cntPath, "w", _SH_DENYNO);
        if (f) { fprintf(f, "0"); fclose(f); }
        rotated = true;
    }
    g_log = _fsopen(path, "a", _SH_DENYNO);
    if (g_log) {
        Log("[ffx-hooks] log opened: %s (opens=%d%s)\n", path, opens, rotated ? " [rotated]" : "");
    } else {
        OutputDebugStringA("[ffx-hooks] WARN failed to open %TEMP%\\ffx-hooks.log\n");
    }
}

/* â”€â”€ Shared memory block â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
static HANDLE        g_mmf   = NULL;
static FFXHooksBlock* g_block = nullptr;

static bool CreateBlock() {
    g_mmf = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                0, sizeof(FFXHooksBlock), FFXHOOKS_MMF_NAME);
    if (!g_mmf) return false;
    g_block = static_cast<FFXHooksBlock*>(
        MapViewOfFile(g_mmf, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FFXHooksBlock)));
    if (!g_block) { CloseHandle(g_mmf); g_mmf = NULL; return false; }
    memset(g_block, 0, sizeof(FFXHooksBlock));
    g_block->magic                    = FFXHOOKS_MAGIC;
    g_block->version                  = FFXHOOKS_VERSION;
    g_block->musicOverrideTrackIndex  = -1;
    return true;
}

static void DestroyBlock() {
    if (g_block) { UnmapViewOfFile(g_block); g_block = nullptr; }
    if (g_mmf)   { CloseHandle(g_mmf); g_mmf = NULL; }
}

static bool EnvFlagEnabled(const char* name) {
    char value[16] = {};
    DWORD len = GetEnvironmentVariableA(name, value, sizeof(value));
    return len > 0 && (value[0] == '1' || value[0] == 'y' || value[0] == 'Y' ||
                       value[0] == 't' || value[0] == 'T');
}

static bool ModuleRelativePath(const char* relativePath, char* outPath, size_t outPathSize) {
    if (!g_module || !relativePath || !relativePath[0] || !outPath || outPathSize == 0) return false;

    char modulePath[MAX_PATH] = {};
    if (GetModuleFileNameA(g_module, modulePath, sizeof(modulePath)) == 0) return false;

    char* slash = strrchr(modulePath, '\\');
    if (!slash) return false;
    *(slash + 1) = '\0';

    _snprintf_s(outPath, outPathSize, _TRUNCATE, "%s%s", modulePath, relativePath);
    return true;
}

static bool ModuleDirectoryPath(char* outPath, size_t outPathSize) {
    if (!g_module || !outPath || outPathSize == 0) return false;

    char modulePath[MAX_PATH] = {};
    if (GetModuleFileNameA(g_module, modulePath, sizeof(modulePath)) == 0) return false;

    char* slash = strrchr(modulePath, '\\');
    if (!slash) return false;
    *(slash + 1) = '\0';

    lstrcpynA(outPath, modulePath, static_cast<int>(outPathSize));
    return true;
}

static bool GameRootDirectoryPath(char* outPath, size_t outPathSize) {
    if (!outPath || outPathSize == 0) return false;

    char exePath[MAX_PATH] = {};
    if (GetModuleFileNameA(GetModuleHandleA(nullptr), exePath, sizeof(exePath)) == 0) return false;

    char* slash = strrchr(exePath, '\\');
    if (!slash) return false;
    *(slash + 1) = '\0';

    lstrcpynA(outPath, exePath, static_cast<int>(outPathSize));
    return true;
}

static bool ResolveModBtlRoot(char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    char env[MAX_PATH * 2] = {};
    if (GetEnvironmentVariableA("FFXHOOKS_ARENAPLUS_MOD_BTL", env, sizeof(env)) > 0) {
        DWORD attr = GetFileAttributesA(env);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            lstrcpynA(out, env, static_cast<int>(outSize));
            return true;
        }
    }
    char cfg[MAX_PATH] = {};
    if (ModuleRelativePath("config\\arena_plus_compose_mod_btl.txt", cfg, sizeof(cfg))) {
        FILE* f = nullptr;
        if (fopen_s(&f, cfg, "r") == 0 && f) {
            if (fgets(out, static_cast<int>(outSize), f)) {
                size_t len = strlen(out);
                while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) out[--len] = '\0';
            }
            fclose(f);
        }
        DWORD attr = GetFileAttributesA(out);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) return true;
    }
    char gameRoot[MAX_PATH] = {};
    if (!GameRootDirectoryPath(gameRoot, sizeof(gameRoot))) return false;
    _snprintf_s(out, outSize, _TRUNCATE,
        "%sdata\\mods\\ffx_ps2\\ffx\\master\\jppc\\battle\\btl", gameRoot);
    DWORD attr = GetFileAttributesA(out);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

static bool ModuleFileExists(const char* relativePath) {
    char path[MAX_PATH] = {};
    if (!ModuleRelativePath(relativePath, path, sizeof(path))) return false;
    const DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool ModuleFlagEnabled(const char* relativePath) {
    return ModuleFileExists(relativePath);
}

static bool FpsScoutEnabledFromConfig() {
    return FfxHooks::Config::CheckEnabled("labs.fps_scout", "FFXHOOKS_ENABLE_FPS_SCOUT", "fps_scout.flag", false);
}

/* â”€â”€ FPS scout (read-only) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
static volatile LONG g_fpsScoutRunning = 0;
static volatile LONG g_fpsScoutLockInit = 0;
static CRITICAL_SECTION g_fpsScoutLock;
static FILE* g_fpsScoutPresentCsv = nullptr;
static FILE* g_fpsScoutTickCsv = nullptr;
static FILE* g_fpsScoutMseqCsv = nullptr;
static FILE* g_fpsScoutModeCsv = nullptr;
static FILE* g_fpsScoutSummary = nullptr;
static HANDLE g_fpsScoutProbeMmf = NULL;
static FFXProbeBlock* g_fpsScoutProbeBlock = nullptr;
static LARGE_INTEGER g_fpsScoutQpcFreq = {};
static LARGE_INTEGER g_fpsScoutLastPresentQpc = {};
static uint64_t g_fpsScoutPresentIndex = 0;
static uint32_t g_fpsScoutLastHeartbeat = 0;
static char g_fpsScoutOutputDir[MAX_PATH] = {};

static bool FpsScoutJoinPath(const char* base, const char* leaf, char* outPath, size_t outPathSize) {
    if (!base || !base[0] || !leaf || !leaf[0] || !outPath || outPathSize == 0) return false;
    const size_t baseLen = strlen(base);
    const bool hasSlash = baseLen > 0 && (base[baseLen - 1] == '\\' || base[baseLen - 1] == '/');
    _snprintf_s(outPath, outPathSize, _TRUNCATE, "%s%s%s", base, hasSlash ? "" : "\\", leaf);
    return outPath[0] != '\0';
}

static bool FpsScoutFileExistsAbsolute(const char* path) {
    if (!path || !path[0]) return false;
    const DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool FpsScoutDirectoryExistsAbsolute(const char* path) {
    if (!path || !path[0]) return false;
    const DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool FpsScoutEnsureDirectory(const char* path) {
    if (!path || !path[0]) return false;
    if (FpsScoutDirectoryExistsAbsolute(path)) return true;
    return CreateDirectoryA(path, nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool FpsScoutFindRepoRootFrom(const char* startDir, char* outRoot, size_t outRootSize) {
    if (!startDir || !startDir[0] || !outRoot || outRootSize == 0) return false;

    char cursor[MAX_PATH] = {};
    lstrcpynA(cursor, startDir, static_cast<int>(sizeof(cursor)));
    size_t len = strlen(cursor);
    while (len > 0 && (cursor[len - 1] == '\\' || cursor[len - 1] == '/')) {
        cursor[len - 1] = '\0';
        --len;
    }

    for (;;) {
        char marker[MAX_PATH] = {};
        FpsScoutJoinPath(cursor, "PORT_STATUS.md", marker, sizeof(marker));
        if (FpsScoutFileExistsAbsolute(marker)) {
            lstrcpynA(outRoot, cursor, static_cast<int>(outRootSize));
            return true;
        }

        char* slash = strrchr(cursor, '\\');
        char* slash2 = strrchr(cursor, '/');
        if (slash2 && (!slash || slash2 > slash)) slash = slash2;
        if (!slash || slash == cursor || (slash == cursor + 2 && cursor[1] == ':')) break;
        *slash = '\0';
    }
    return false;
}

static bool FpsScoutResolveOutputRoot(char* outRoot, size_t outRootSize, char* outSource, size_t outSourceSize) {
    if (!outRoot || outRootSize == 0) return false;
    if (outSource && outSourceSize > 0) outSource[0] = '\0';

    char envRoot[MAX_PATH] = {};
    const DWORD envLen = GetEnvironmentVariableA("FFXHOOKS_FPS_SCOUT_ROOT", envRoot, sizeof(envRoot));
    if (envLen > 0 && envLen < sizeof(envRoot)) {
        lstrcpynA(outRoot, envRoot, static_cast<int>(outRootSize));
        if (outSource) lstrcpynA(outSource, "env:FFXHOOKS_FPS_SCOUT_ROOT", static_cast<int>(outSourceSize));
        return true;
    }

    char moduleDir[MAX_PATH] = {};
    if (ModuleDirectoryPath(moduleDir, sizeof(moduleDir)) &&
        FpsScoutFindRepoRootFrom(moduleDir, outRoot, outRootSize)) {
        if (outSource) lstrcpynA(outSource, "module-parent:PORT_STATUS.md", static_cast<int>(outSourceSize));
        return true;
    }

    char gameRoot[MAX_PATH] = {};
    if (GameRootDirectoryPath(gameRoot, sizeof(gameRoot)) &&
        FpsScoutFindRepoRootFrom(gameRoot, outRoot, outRootSize)) {
        if (outSource) lstrcpynA(outSource, "game-parent:PORT_STATUS.md", static_cast<int>(outSourceSize));
        return true;
    }

    if (moduleDir[0]) {
        lstrcpynA(outRoot, moduleDir, static_cast<int>(outRootSize));
        if (outSource) lstrcpynA(outSource, "module-directory-fallback", static_cast<int>(outSourceSize));
        return true;
    }
    if (gameRoot[0]) {
        lstrcpynA(outRoot, gameRoot, static_cast<int>(outRootSize));
        if (outSource) lstrcpynA(outSource, "game-directory-fallback", static_cast<int>(outSourceSize));
        return true;
    }
    return false;
}

static FILE* FpsScoutOpenCsv(const char* fileName) {
    char path[MAX_PATH] = {};
    if (!FpsScoutJoinPath(g_fpsScoutOutputDir, fileName, path, sizeof(path))) return nullptr;
    return _fsopen(path, "w", _SH_DENYNO);
}

static void FpsScoutCloseOutputs() {
    if (g_fpsScoutPresentCsv) { fclose(g_fpsScoutPresentCsv); g_fpsScoutPresentCsv = nullptr; }
    if (g_fpsScoutTickCsv) { fclose(g_fpsScoutTickCsv); g_fpsScoutTickCsv = nullptr; }
    if (g_fpsScoutMseqCsv) { fclose(g_fpsScoutMseqCsv); g_fpsScoutMseqCsv = nullptr; }
    if (g_fpsScoutModeCsv) { fclose(g_fpsScoutModeCsv); g_fpsScoutModeCsv = nullptr; }
    if (g_fpsScoutSummary) { fclose(g_fpsScoutSummary); g_fpsScoutSummary = nullptr; }
}

static int64_t FpsScoutQpcUs(const LARGE_INTEGER& qpc) {
    if (g_fpsScoutQpcFreq.QuadPart <= 0) return 0;
    return static_cast<int64_t>(
        (static_cast<double>(qpc.QuadPart) * 1000000.0) /
        static_cast<double>(g_fpsScoutQpcFreq.QuadPart));
}

static void FpsScoutCloseProbe() {
    if (g_fpsScoutProbeBlock) {
        UnmapViewOfFile(g_fpsScoutProbeBlock);
        g_fpsScoutProbeBlock = nullptr;
    }
    if (g_fpsScoutProbeMmf) {
        CloseHandle(g_fpsScoutProbeMmf);
        g_fpsScoutProbeMmf = NULL;
    }
}

static uint32_t FpsScoutReadHeartbeat() {
    if (!g_fpsScoutProbeBlock) {
        g_fpsScoutProbeMmf = OpenFileMappingA(FILE_MAP_READ, FALSE, FFXPROBE_MMF_NAME);
        if (g_fpsScoutProbeMmf) {
            g_fpsScoutProbeBlock = static_cast<FFXProbeBlock*>(
                MapViewOfFile(g_fpsScoutProbeMmf, FILE_MAP_READ, 0, 0, sizeof(FFXProbeBlock)));
            if (!g_fpsScoutProbeBlock) {
                CloseHandle(g_fpsScoutProbeMmf);
                g_fpsScoutProbeMmf = NULL;
            }
        }
    }
    if (!g_fpsScoutProbeBlock) return 0;
    if (g_fpsScoutProbeBlock->magic != FFXPROBE_MAGIC ||
        g_fpsScoutProbeBlock->version != FFXPROBE_VERSION) {
        return 0;
    }
    return g_fpsScoutProbeBlock->heartbeat;
}

static bool FpsScoutUnxDetected() {
    return GetModuleHandleA("UnX.dll") != NULL ||
           GetModuleHandleA("unx.dll") != NULL;
}

static bool FpsScoutSpecialKDetected() {
    const HMODULE dxgi=GetModuleHandleA("dxgi.dll");
    return GetModuleHandleA("SpecialK.dll") != NULL ||
           GetModuleHandleA("SpecialK32.dll") != NULL ||
           GetModuleHandleA("SpecialK64.dll") != NULL ||
           (dxgi && GetProcAddress(dxgi,"SK_GetGameWindow") && GetProcAddress(dxgi,"SK_CreateFuncHook"));
}

static void FpsScoutFlushIfNeeded() {
    if ((g_fpsScoutPresentIndex % 60ull) != 0ull) return;
    if (g_fpsScoutPresentCsv) fflush(g_fpsScoutPresentCsv);
    if (g_fpsScoutModeCsv) fflush(g_fpsScoutModeCsv);
}

static bool FpsScoutStart() {
    if (InterlockedCompareExchange(&g_fpsScoutRunning, 1, 0) != 0) {
        return true;
    }
    if (InterlockedCompareExchange(&g_fpsScoutLockInit, 1, 0) == 0) {
        InitializeCriticalSection(&g_fpsScoutLock);
    }

    QueryPerformanceFrequency(&g_fpsScoutQpcFreq);
    g_fpsScoutLastPresentQpc.QuadPart = 0;
    g_fpsScoutPresentIndex = 0;
    g_fpsScoutLastHeartbeat = 0;
    g_fpsScoutOutputDir[0] = '\0';

    char root[MAX_PATH] = {};
    char source[64] = {};
    if (!FpsScoutResolveOutputRoot(root, sizeof(root), source, sizeof(source))) {
        Log("[ffx-hooks] FPS Scout failed: could not resolve output root\n");
        InterlockedExchange(&g_fpsScoutRunning, 0);
        return false;
    }

    char workDir[MAX_PATH] = {};
    if (!FpsScoutJoinPath(root, "work", workDir, sizeof(workDir)) ||
        !FpsScoutEnsureDirectory(workDir)) {
        Log("[ffx-hooks] FPS Scout failed: could not create work dir under %s\n", root);
        InterlockedExchange(&g_fpsScoutRunning, 0);
        return false;
    }

    SYSTEMTIME st = {};
    GetLocalTime(&st);
    char sessionName[64] = {};
    _snprintf_s(sessionName, sizeof(sessionName), _TRUNCATE,
        "fps_scout_%04u%02u%02u_%02u%02u%02u",
        static_cast<unsigned>(st.wYear),
        static_cast<unsigned>(st.wMonth),
        static_cast<unsigned>(st.wDay),
        static_cast<unsigned>(st.wHour),
        static_cast<unsigned>(st.wMinute),
        static_cast<unsigned>(st.wSecond));
    if (!FpsScoutJoinPath(workDir, sessionName, g_fpsScoutOutputDir, sizeof(g_fpsScoutOutputDir)) ||
        !FpsScoutEnsureDirectory(g_fpsScoutOutputDir)) {
        Log("[ffx-hooks] FPS Scout failed: could not create output dir under %s\n", workDir);
        InterlockedExchange(&g_fpsScoutRunning, 0);
        return false;
    }

    g_fpsScoutPresentCsv = FpsScoutOpenCsv("present.csv");
    g_fpsScoutTickCsv = FpsScoutOpenCsv("tick.csv");
    g_fpsScoutMseqCsv = FpsScoutOpenCsv("mseq.csv");
    g_fpsScoutModeCsv = FpsScoutOpenCsv("mode.csv");
    g_fpsScoutSummary = FpsScoutOpenCsv("summary.md");
    if (!g_fpsScoutPresentCsv || !g_fpsScoutTickCsv || !g_fpsScoutMseqCsv ||
        !g_fpsScoutModeCsv || !g_fpsScoutSummary) {
        Log("[ffx-hooks] FPS Scout failed: could not open all output files in %s\n", g_fpsScoutOutputDir);
        FpsScoutCloseOutputs();
        InterlockedExchange(&g_fpsScoutRunning, 0);
        return false;
    }

    fputs("qpc_us,present_index,present_dt_ms,sync_interval,present_flags,heartbeat,heartbeat_delta,mode_guess,unx_detected,specialk_detected\n", g_fpsScoutPresentCsv);
    fputs("qpc_us,tick_index,tick_dt_ms,arg_fspeed,caller_ret,mode_guess\n", g_fpsScoutTickCsv);
    fputs("qpc_us,mode_guess,actor_or_instance,cursor_fixed_inst_1856,frame_scale_inst_2016,playback_speed_inst_1876,last_eval_frame_inst_2012\n", g_fpsScoutMseqCsv);
    fputs("qpc_us,mode_guess,source,confidence,field_id,battle_state,menu_state,loading_flag,fmv_cutscene_flag,heartbeat\n", g_fpsScoutModeCsv);

    fprintf(g_fpsScoutSummary,
        "# FFX FPS Scout Summary\n\n"
        "Status: read-only scout armed. It logs Present cadence and optional ffx-probe heartbeat only.\n\n"
        "- output_dir: `%s`\n"
        "- root_source: `%s`\n"
        "- activation: `FFXHOOKS_ENABLE_FPS_SCOUT=1` or `fps_scout.flag`\n"
        "- writes_to_gameplay_timing_mseq: `none`\n"
        "- tick_csv: header-only until a tick hook is separately proved safe\n"
        "- mseq_csv: header-only until a safe instance sampler is separately proved\n\n",
        g_fpsScoutOutputDir,
        source[0] ? source : "unknown");
    fflush(g_fpsScoutPresentCsv);
    fflush(g_fpsScoutTickCsv);
    fflush(g_fpsScoutMseqCsv);
    fflush(g_fpsScoutModeCsv);
    fflush(g_fpsScoutSummary);

    Log("[ffx-hooks] FPS Scout armed output=%s source=%s\n", g_fpsScoutOutputDir, source);
    return true;
}

static void FpsScoutStop() {
    if (InterlockedCompareExchange(&g_fpsScoutRunning, 0, 1) != 1) {
        return;
    }
    EnterCriticalSection(&g_fpsScoutLock);
    if (g_fpsScoutSummary) {
        fprintf(g_fpsScoutSummary,
            "\n## Stop\n\n"
            "- present_rows: `%llu`\n"
            "- last_heartbeat: `%u`\n"
            "- final_status: `closed by DLL detach/remove`\n",
            static_cast<unsigned long long>(g_fpsScoutPresentIndex),
            static_cast<unsigned>(g_fpsScoutLastHeartbeat));
        fflush(g_fpsScoutSummary);
    }
    FpsScoutCloseOutputs();
    FpsScoutCloseProbe();
    LeaveCriticalSection(&g_fpsScoutLock);
    Log("[ffx-hooks] FPS Scout stopped rows=%llu\n",
        static_cast<unsigned long long>(g_fpsScoutPresentIndex));
}

static double g_nativePerformanceAverageMs = 0.0;
static void FpsScoutOnPresent(UINT syncInterval, UINT flags) {
    const bool recording=InterlockedCompareExchange(&g_fpsScoutRunning,1,1)==1 && g_fpsScoutPresentCsv;
    if (!recording && !FfxHooks::NativePorts::CurrentSettings().performance) {
        g_nativePerformanceAverageMs=0.0;
        return;
    }

    LARGE_INTEGER now = {};
    if (g_fpsScoutQpcFreq.QuadPart<=0) QueryPerformanceFrequency(&g_fpsScoutQpcFreq);
    QueryPerformanceCounter(&now);
    double dtMs = 0.0;
    if (g_fpsScoutLastPresentQpc.QuadPart != 0 && g_fpsScoutQpcFreq.QuadPart > 0) {
        dtMs = static_cast<double>(now.QuadPart - g_fpsScoutLastPresentQpc.QuadPart) * 1000.0 /
               static_cast<double>(g_fpsScoutQpcFreq.QuadPart);
    }
    g_fpsScoutLastPresentQpc = now;
    if (dtMs>0.0 && dtMs<1000.0)
        g_nativePerformanceAverageMs=g_nativePerformanceAverageMs>0.0
            ? g_nativePerformanceAverageMs*0.90+dtMs*0.10:dtMs;
    if (!recording) return;

    const uint32_t heartbeat = FpsScoutReadHeartbeat();
    const uint32_t heartbeatDelta =
        (g_fpsScoutLastHeartbeat != 0 && heartbeat >= g_fpsScoutLastHeartbeat)
            ? (heartbeat - g_fpsScoutLastHeartbeat)
            : 0u;
    g_fpsScoutLastHeartbeat = heartbeat;

    EnterCriticalSection(&g_fpsScoutLock);
    const uint64_t row = ++g_fpsScoutPresentIndex;
    fprintf(g_fpsScoutPresentCsv,
        "%lld,%llu,%.3f,%u,%u,%u,%u,unknown,%d,%d\n",
        static_cast<long long>(FpsScoutQpcUs(now)),
        static_cast<unsigned long long>(row),
        dtMs,
        static_cast<unsigned>(syncInterval),
        static_cast<unsigned>(flags),
        static_cast<unsigned>(heartbeat),
        static_cast<unsigned>(heartbeatDelta),
        FpsScoutUnxDetected() ? 1 : 0,
        FpsScoutSpecialKDetected() ? 1 : 0);

    if (g_fpsScoutModeCsv && (row == 1ull || (row % 120ull) == 0ull)) {
        fprintf(g_fpsScoutModeCsv,
            "%lld,unknown,present_hook,0,-1,-1,-1,-1,-1,%u\n",
            static_cast<long long>(FpsScoutQpcUs(now)),
            static_cast<unsigned>(heartbeat));
    }
    FpsScoutFlushIfNeeded();
    LeaveCriticalSection(&g_fpsScoutLock);
}

static bool AuroraConfigPath(char* outPath, size_t outPathSize) {
    return ModuleRelativePath("config\\aurora_overlay.ini", outPath, outPathSize);
}

static bool AuroraConfigExists() {
    return ModuleFileExists("config\\aurora_overlay.ini");
}

static bool TryParseIntText(const char* value, int* out) {
    if (out) *out = 0;
    if (!value) return false;

    while (*value == ' ' || *value == '\t') ++value;
    if (!*value) return false;

    char* end = nullptr;
    long parsed = strtol(value, &end, 0);
    if (end == value) return false;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    if (*end != '\0') return false;
    if (out) *out = static_cast<int>(parsed);
    return true;
}

static bool TryParseAddressText(const char* value, uintptr_t* out) {
    if (out) *out = 0;
    if (!value) return false;

    while (*value == ' ' || *value == '\t') ++value;
    if (!*value) return false;

    char* end = nullptr;
    unsigned long parsed = strtoul(value, &end, 0);
    if (end == value) return false;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    if (*end != '\0') return false;
    if (out) *out = static_cast<uintptr_t>(parsed);
    return true;
}

static bool TryEnvInt(const char* name, int* out) {
    char value[32] = {};
    DWORD len = GetEnvironmentVariableA(name, value, sizeof(value));
    if (len == 0 || len >= sizeof(value)) return false;
    return TryParseIntText(value, out);
}

static bool TryEnvAddress(const char* name, uintptr_t* out) {
    char value[32] = {};
    DWORD len = GetEnvironmentVariableA(name, value, sizeof(value));
    if (len == 0 || len >= sizeof(value)) return false;
    return TryParseAddressText(value, out);
}

static bool AuroraConfigString(const char* key, char* out, size_t outSize) {
    if (!key || !out || outSize == 0) return false;

    char path[MAX_PATH] = {};
    if (!AuroraConfigPath(path, sizeof(path))) return false;
    if (!ModuleFileExists("config\\aurora_overlay.ini")) return false;

    out[0] = '\0';
    DWORD len = GetPrivateProfileStringA("aurora", key, "", out, static_cast<DWORD>(outSize), path);
    return len > 0 && len < outSize;
}

static int AuroraConfigInt(const char* key, int fallback) {
    char value[32] = {};
    if (!AuroraConfigString(key, value, sizeof(value))) return fallback;

    int parsed = fallback;
    return TryParseIntText(value, &parsed) ? parsed : fallback;
}

static uintptr_t AuroraConfigAddress(const char* key, uintptr_t fallback) {
    char value[32] = {};
    if (!AuroraConfigString(key, value, sizeof(value))) return fallback;

    uintptr_t parsed = fallback;
    return TryParseAddressText(value, &parsed) ? parsed : fallback;
}

static int SettingInt(const char* envName, const char* configKey, int fallback) {
    int parsed = fallback;
    if (TryEnvInt(envName, &parsed)) return parsed;
    return AuroraConfigInt(configKey, fallback);
}

static uintptr_t SettingAddress(const char* envName, const char* configKey, uintptr_t fallback) {
    uintptr_t parsed = fallback;
    if (TryEnvAddress(envName, &parsed)) return parsed;
    return AuroraConfigAddress(configKey, fallback);
}

static int EnvInt(const char* name, int fallback) {
    int parsed = fallback;
    return TryEnvInt(name, &parsed) ? parsed : fallback;
}

static bool TryModuleTextInt(const char* relativePath, int* out) {
    if (out) *out = 0;
    if (!relativePath || !relativePath[0]) return false;

    char path[MAX_PATH] = {};
    if (!ModuleRelativePath(relativePath, path, sizeof(path))) return false;

    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    char value[64] = {};
    DWORD read = 0;
    const BOOL ok = ReadFile(file, value, sizeof(value) - 1, &read, nullptr);
    CloseHandle(file);
    if (!ok || read == 0) return false;
    value[read] = '\0';
    return TryParseIntText(value, out);
}

static uintptr_t EnvAddress(const char* name, uintptr_t fallback) {
    uintptr_t parsed = fallback;
    return TryEnvAddress(name, &parsed) ? parsed : fallback;
}

static FfxHooks::Config::BoolGateResult ResolveF8CatalogGate(const char* canonicalKey) {
    const FfxHooks::F8FlagSpec* flag = FfxHooks::FindF8Flag(canonicalKey);
    if (!flag) return { false, FfxHooks::Config::BoolSource::DefaultValue };
    return FfxHooks::ResolveF8Flag(*flag);
}

struct F8StartupGateSnapshot {
    const char* canonicalKey;
    FfxHooks::Config::BoolGateResult result;
};

// Restart-required gates are resolved once after Config::Load and remain immutable for the process.
static F8StartupGateSnapshot g_f8StartupGates[] = {
    { "boosters.speed_hack_fmv", { false, FfxHooks::Config::BoolSource::DefaultValue } },
    { "field_scout.master", { false, FfxHooks::Config::BoolSource::DefaultValue } },
    { "field_scout.heavy", { false, FfxHooks::Config::BoolSource::DefaultValue } },
    { "field_scout.max", { false, FfxHooks::Config::BoolSource::DefaultValue } },
    { "field_scout.ultra", { false, FfxHooks::Config::BoolSource::DefaultValue } },
    { "arena_plus.master", { false, FfxHooks::Config::BoolSource::DefaultValue } },
    { "arena_plus.victory_hook", { false, FfxHooks::Config::BoolSource::DefaultValue } },
    { "arena_plus.resolver_log", { false, FfxHooks::Config::BoolSource::DefaultValue } },
    { "arena_plus.music", { false, FfxHooks::Config::BoolSource::DefaultValue } },
    { "development.fastload_autosave", { false, FfxHooks::Config::BoolSource::DefaultValue } },
    { "labs.nova_super_damage", { false, FfxHooks::Config::BoolSource::DefaultValue } },
    { "labs.kimahri_ronso_mana", { false, FfxHooks::Config::BoolSource::DefaultValue } },
    { "labs.equipment_workshop", { false, FfxHooks::Config::BoolSource::DefaultValue } },
    { "labs.grid_teach", { false, FfxHooks::Config::BoolSource::DefaultValue } },
    { "labs.kimahri_lancet_dual_grant", { false, FfxHooks::Config::BoolSource::DefaultValue } },
    { "labs.item_stack_cap", { false, FfxHooks::Config::BoolSource::DefaultValue } },
    { "labs.double_triple_drop", { false, FfxHooks::Config::BoolSource::DefaultValue } },
};
static int g_itemStackCapStartupValue = 255;
static std::atomic<bool> g_f8StartupGatesCaptured{false};
static INIT_ONCE g_f8StartupGateOnce = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK CaptureF8StartupGatesCallback(PINIT_ONCE, PVOID, PVOID*) {
    for (F8StartupGateSnapshot& gate : g_f8StartupGates) {
        gate.result = ResolveF8CatalogGate(gate.canonicalKey);
    }
    g_itemStackCapStartupValue = EnvInt("FFXHOOKS_ITEM_STACK_CAP",
        FfxHooks::Config::GetInt("labs.item_stack_cap_value", 255));
    if (g_itemStackCapStartupValue < 1) g_itemStackCapStartupValue = 1;
    if (g_itemStackCapStartupValue > 255) g_itemStackCapStartupValue = 255;
    g_f8StartupGatesCaptured.store(true, std::memory_order_release);
    return TRUE;
}

static void CaptureF8StartupGates() {
    InitOnceExecuteOnce(&g_f8StartupGateOnce, CaptureF8StartupGatesCallback, nullptr, nullptr);
}

static const F8StartupGateSnapshot* FindF8StartupGate(const char* canonicalKey) {
    if (!canonicalKey || !g_f8StartupGatesCaptured.load(std::memory_order_acquire)) return nullptr;
    for (const F8StartupGateSnapshot& gate : g_f8StartupGates) {
        if (strcmp(gate.canonicalKey, canonicalKey) == 0) return &gate;
    }
    return nullptr;
}

static bool F8CatalogGateEnabled(const char* canonicalKey) {
    const F8StartupGateSnapshot* snapshot = FindF8StartupGate(canonicalKey);
    return snapshot ? snapshot->result.value : ResolveF8CatalogGate(canonicalKey).value;
}

static void LogF8CatalogGate(const char* canonicalKey, const char* consumer) {
    const F8StartupGateSnapshot* snapshot = FindF8StartupGate(canonicalKey);
    const FfxHooks::Config::BoolGateResult result =
        snapshot ? snapshot->result : ResolveF8CatalogGate(canonicalKey);
    Log("[ffx-hooks] F8 gate key=%s value=%d source=%s consumer=%s\n",
        canonicalKey ? canonicalKey : "?",
        result.value ? 1 : 0,
        FfxHooks::Config::BoolSourceName(result.source),
        consumer ? consumer : "?");
}

static void PublishResolvedF8Status(const char* canonicalKey,
                                    FfxHooks::F8RuntimeAvailability availability,
                                    bool includeAppliedValue) {
    const FfxHooks::Config::BoolGateResult effective = ResolveF8CatalogGate(canonicalKey);
    if (!FfxHooks::PublishF8RuntimeStatus(
            canonicalKey, availability, includeAppliedValue, effective.value)) {
        Log("[ffx-hooks] WARN F8 status publish rejected key=%s\n",
            canonicalKey ? canonicalKey : "?");
    }
}

static bool ArenaPlusMusicFlagEnabledRaw() {
    return F8CatalogGateEnabled("arena_plus.music");
}

static bool MusicHookEnabledFromConfig() {
    if (ModuleFileExists("music.flag.off") ||
        ModuleFileExists("config\\music.flag.off")) return false;
    return FfxHooks::Config::CheckEnabled("music.enabled", "FFXHOOKS_ENABLE_MUSIC", "music.flag", false) ||
           ArenaPlusMusicFlagEnabledRaw();
}

static bool NovaSuperDamageFlagEnabled() {
    return F8CatalogGateEnabled("labs.nova_super_damage");
}

static bool NovaSuperDamageLogFlagEnabled() {
    return FfxHooks::Config::CheckEnabled("labs.nova_super_damage_log", "FFXHOOKS_NOVA_SUPER_DAMAGE_LOG", "nova_super_damage_log.flag", false);
}

static bool RonsoManaFlagEnabled() {
    return F8CatalogGateEnabled("labs.kimahri_ronso_mana");
}

static bool NulWardFlagEnabled() {
    return FfxHooks::Config::CheckEnabled("labs.nul_ward", "FFXHOOKS_ENABLE_NUL_WARD", "nul_ward.flag", false);
}

static bool NulWardApplyEnabled() {
    return FfxHooks::Config::CheckEnabled("labs.nul_ward_apply", "FFXHOOKS_NUL_WARD_APPLY", "nul_ward_apply.flag", false);
}

static bool NulWardLogFlagEnabled() {
    return NulWardFlagEnabled() ||
           FfxHooks::Config::CheckEnabled("labs.nul_ward_log", "FFXHOOKS_NUL_WARD_LOG", "nul_ward_log.flag", false) ||
           NulWardApplyEnabled();
}

static bool NulWardNativeSlotsEnabled() {
    return FfxHooks::Config::CheckEnabled("labs.nul_ward_native_slots", "FFXHOOKS_NUL_WARD_NATIVE_SLOTS", "nul_ward_native_slots.flag", false);
}

static bool NulWardP16Enabled() {
    return FfxHooks::Config::CheckEnabled("labs.nul_ward_p16", "FFXHOOKS_NUL_WARD_P16", "nul_ward_p16.flag", false);
}

static bool NulWardP16ApplyEnabled() {
    return FfxHooks::Config::CheckEnabled("labs.nul_ward_p16_apply", "FFXHOOKS_NUL_WARD_P16_APPLY", "nul_ward_p16_apply.flag", false);
}

static bool NulWardTeachEnabled() {
    return FfxHooks::Config::CheckEnabled("labs.nul_ward_teach", "FFXHOOKS_NUL_WARD_TEACH", "nul_ward_teach.flag", false);
}

static bool NulWardTeachGrantEnabled() {
    // Explicit opt-in ONLY â€” do NOT tie to nul_ward_teach.flag (that caused born-with grant for all chars).
    return FfxHooks::Config::CheckEnabled("labs.nul_ward_teach_grant", "FFXHOOKS_NUL_WARD_TEACH_GRANT", "nul_ward_teach_grant.flag", false);
}

static bool GridTeachEnabled() {
    return F8CatalogGateEnabled("labs.grid_teach");
}

static bool KimahriLancetDualGrantEnabled() {
    return F8CatalogGateEnabled("labs.kimahri_lancet_dual_grant");
}

static bool ItemStackCapFlagEnabled() {
    return F8CatalogGateEnabled("labs.item_stack_cap");
}

static bool ItemStackCapLogFlagEnabled() {
    return ItemStackCapFlagEnabled() ||
           FfxHooks::Config::CheckEnabled("labs.item_stack_cap_log", "FFXHOOKS_ITEM_STACK_CAP_LOG", "item_stack_cap_log.flag", false);
}

/* Env-only gate â€” no flag files yet (coordination with parallel DLL lane). */
static bool DoubleTripleDropEnabled() {
    return F8CatalogGateEnabled("labs.double_triple_drop");
}

static bool DoubleTripleDropLogEnabled() {
    return DoubleTripleDropEnabled() ||
           FfxHooks::Config::CheckEnabled("labs.double_triple_drop_log", "FFXHOOKS_DOUBLE_TRIPLE_DROP_LOG", "double_triple_drop_log.flag", false);
}

static bool ElementScanDarkEnabled() {
    return FfxHooks::Config::CheckEnabled("labs.element_scan_dark", "FFXHOOKS_ELEMENT_SCAN_DARK", "element_scan_dark.flag", false);
}

static bool AbilitySfxFlagEnabled() {
    return FfxHooks::Config::CheckEnabled("labs.ability_sfx", "FFXHOOKS_ENABLE_ABILITY_SFX", "ability_sfx.flag", false);
}

static bool AbilitySfxLogFlagEnabled() {
    return AbilitySfxFlagEnabled() ||
           FfxHooks::Config::CheckEnabled("labs.ability_sfx_log", "FFXHOOKS_ABILITY_SFX_LOG", "ability_sfx_log.flag", false);
}

static bool FieldProbeRt2FlagEnabled() {
    return FfxHooks::Config::CheckEnabled("labs.field_probe_rt2", "FFXHOOKS_ENABLE_FIELD_PROBE_RT2", "field_probe_rt2.flag", false);
}

static bool FieldProbeEncounterOnlyFlagEnabled() {
    return FfxHooks::Config::CheckEnabled("labs.field_probe_encounter", "FFXHOOKS_FIELD_PROBE_ENCOUNTER", "field_probe_encounter.flag", false);
}

static bool FieldProbeTextureOnlyFlagEnabled() {
    return FfxHooks::Config::CheckEnabled("labs.field_probe_texture", "FFXHOOKS_FIELD_PROBE_TEXTURE", "field_probe_texture.flag", false);
}

static bool FieldScoutFlagEnabled() {
    return F8CatalogGateEnabled("field_scout.master");
}

static bool FieldScoutMapOnlyFlagEnabled() {
    return FfxHooks::Config::CheckEnabled("labs.field_scout_map_only", "FFXHOOKS_FIELD_SCOUT_MAP_ONLY", "field_scout_map_only.flag", false);
}

static bool FieldScoutHeavyFlagEnabled() {
    return F8CatalogGateEnabled("field_scout.heavy");
}

static bool FieldScoutUltraFlagEnabled() {
    return F8CatalogGateEnabled("field_scout.ultra");
}

static bool FieldScoutUltraSubFlagEnabled(const char* flagName, const char* configFlagName, const char* envName) {
    if (!FieldScoutHeavyFlagEnabled() || !FieldScoutUltraFlagEnabled()) return false;
    return EnvFlagEnabled(envName) ||
           ModuleFlagEnabled(flagName) ||
           ModuleFlagEnabled(configFlagName);
}

static bool FieldScoutMaxFlagEnabled() {
    return F8CatalogGateEnabled("field_scout.max");
}

static FfxHooks::FieldScoutUltraOptions FieldScoutBuildUltraOptions() {
    FfxHooks::FieldScoutUltraOptions options = {};
    if (!FieldScoutHeavyFlagEnabled() || !FieldScoutUltraFlagEnabled()) return options;
    options.master = true;
    options.fieldLogic = FieldScoutUltraSubFlagEnabled(
        "field_scout_ultra.field_logic.flag",
        "config\\field_scout_ultra.field_logic.flag",
        "FFXHOOKS_FIELD_SCOUT_ULTRA_FIELD_LOGIC");
    options.collision = FieldScoutUltraSubFlagEnabled(
        "field_scout_ultra.collision.flag",
        "config\\field_scout_ultra.collision.flag",
        "FFXHOOKS_FIELD_SCOUT_ULTRA_COLLISION");
    options.encounters = FieldScoutUltraSubFlagEnabled(
        "field_scout_ultra.encounters.flag",
        "config\\field_scout_ultra.encounters.flag",
        "FFXHOOKS_FIELD_SCOUT_ULTRA_ENCOUNTERS");
    options.sceneEnv = FieldScoutUltraSubFlagEnabled(
        "field_scout_ultra.scene_env.flag",
        "config\\field_scout_ultra.scene_env.flag",
        "FFXHOOKS_FIELD_SCOUT_ULTRA_SCENE_ENV");
    options.pipelineHints = FieldScoutUltraSubFlagEnabled(
        "field_scout_ultra.pipeline.flag",
        "config\\field_scout_ultra.pipeline.flag",
        "FFXHOOKS_FIELD_SCOUT_ULTRA_PIPELINE");
    return options;
}

static FfxHooks::MusicHookTarget MusicHookTargetFromEnv() {
    char value[32] = {};
    DWORD len = GetEnvironmentVariableA("FFXHOOKS_MUSIC_TARGET", value, sizeof(value));
    if (len > 0 && len < sizeof(value)) {
        if (value[0] == 's' || value[0] == 'S' || value[0] == 'c' || value[0] == 'C') {
            return FfxHooks::MusicHookTarget::SwitchCrossfade;
        }
    }
    if (ModuleFlagEnabled("music_target_switch.flag") ||
        ModuleFlagEnabled("config\\music_target_switch.flag") ||
        ArenaPlusMusicFlagEnabledRaw()) {
        return FfxHooks::MusicHookTarget::SwitchCrossfade;
    }
    return FfxHooks::MusicHookTarget::PlayTrack;
}

#ifdef FFXHOOKS_HAVE_POLYHOOK
/* â”€â”€ Aurora battle actor W2S overlay (lab-only GDI fallback) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
static HWND          g_auroraOverlayHwnd = NULL;
static HANDLE        g_auroraOverlayThread = NULL;
static volatile LONG g_auroraOverlayRunning = 0;
static volatile LONG g_auroraOverlayVisible = 0;
static volatile LONG g_auroraOverlayDetail = 0;
static volatile LONG g_auroraDeveloperUiEnabled = 0;
static uintptr_t     g_auroraW2SAddress = 0;
static bool          g_auroraW2SManual = false;
static bool          g_auroraW2SScan = true;
static uintptr_t     g_auroraW2SScanStartAddress = 0x40000000u;
static uintptr_t     g_auroraW2SScanCursor = 0;
static int           g_auroraW2SScanBudgetMs = 2;
static int           g_auroraW2SScanCooldownMs = 100;
static uint32_t      g_auroraW2SScanMinRoots = 4u;
static uintptr_t     g_auroraW2SScanLastRegionBase = 0;
static uintptr_t     g_auroraW2SScanLastRegionEnd = 0;
static uint32_t      g_auroraW2SScanLastProbes = 0;
static uint32_t      g_auroraW2SScanPassCount = 0;
static DWORD         g_auroraW2SScanLastElapsedMs = 0;
static DWORD         g_auroraLastScanTick = 0;
static DWORD         g_auroraLastLogTick = 0;
static DWORD         g_auroraLastWaitLogTick = 0;
static uint32_t      g_auroraLastLabelCount = 0;
static uint32_t      g_auroraLastActorCount = 0;
static uint32_t      g_auroraLastPartyLabelCount = 0;
static uint32_t      g_auroraLastMonsterLabelCount = 0;
static uint32_t      g_auroraLastNpcLabelCount = 0;
static uint32_t      g_auroraLastObjectLabelCount = 0;
static uint32_t      g_auroraLastOtherLabelCount = 0;
static volatile LONG g_auroraActorOverlayEnabled = 0;

/* â”€â”€ Jarvis in-game plugin menu (D3D11 texture, no external window) â”€â”€â”€â”€â”€â”€â”€ */
static volatile LONG g_ingameMenuEnabled = 0;
static volatile LONG g_ingameMenuOpen = 0;
static int           g_ingameMenuSelected = 0;
static int           g_ingameMenuScroll = 0;
static DWORD         g_ingameMenuLastScanTick = 0;
static char          g_ingameMenuStatus[192] = "Runtime plugin switchboard ready";
static HWND          g_ingameMenuInputHwnd = NULL;
static WNDPROC       g_ingameMenuOriginalWndProc = nullptr;
static DWORD         g_ingameMenuLastKeyTick[256] = {};
// The window procedure only publishes focus/wheel facts. The F7 main-thread
// adapter consumes them so no menu object is touched from the window callback.
static volatile LONG g_f7ForegroundLost = 0;
static volatile LONG g_f7MouseWheelDelta = 0;

struct InGameMenuPlugin {
    char file[64];
    char label[96];
    char kind[40];
    bool rootHook;
    bool diskOn;
    bool diskOff;
    bool loaded;
};

static const int INGAME_MENU_MAX_PLUGINS = 32;
static InGameMenuPlugin g_ingameMenuPlugins[INGAME_MENU_MAX_PLUGINS] = {};
static int              g_ingameMenuPluginCount = 0;

/* --- F5 flags panel (2026-08-14, Jarvis-MAGIC/Shiva) ---
 * Toggles INI keys ([f7]/[music]/[labs]) via Config::SetBool (persists to
 * ffx-hooks.ini). Install-time gates apply on the NEXT game restart. --- */
struct InGameMenuFlag {
    const char* key;
    const char* label;
};
static const InGameMenuFlag kInGameMenuFlags[] = {
    { "f7.inlive",                   "F7 In-Live" },
    { "f7.aiswap",                   "F7 Monster AI Observer" },
    { "music.enabled",               "Battle music hook" },
    { "music.arena_plus",            "Arena+ music" },
    { "labs.nova_super_damage",      "Nova Super Damage" },
    { "labs.nova_super_damage_log",  "Nova Super Damage (log)" },
    { "labs.kimahri_ronso_mana",     "Kimahri Ronso Mana" },
    { "labs.nul_ward",               "Nul Ward" },
    { "labs.nul_ward_apply",         "Nul Ward (apply)" },
    { "labs.nul_ward_log",           "Nul Ward (log)" },
    { "labs.nul_ward_native_slots",  "Nul Ward native slots" },
    { "labs.nul_ward_p16",           "Nul Ward P16" },
    { "labs.nul_ward_p16_apply",     "Nul Ward P16 (apply)" },
    { "labs.nul_ward_teach",         "Nul Ward teach" },
    { "labs.nul_ward_teach_grant",   "Nul Ward teach grant" },
    { "labs.grid_teach",             "Grid Teach" },
    { "labs.kimahri_lancet_dual_grant", "Kimahri Lancet dual grant" },
    { "labs.item_stack_cap",         "Item stack cap 255" },
    { "labs.item_stack_cap_log",     "Item stack cap (log)" },
    { "labs.double_triple_drop",     "Double/Triple Drop" },
    { "labs.double_triple_drop_log", "Double/Triple Drop (log)" },
    { "labs.element_scan_dark",      "Element Scan (Holy/Dark)" },
    { "labs.ability_sfx",            "Ability SFX" },
    { "labs.ability_sfx_log",        "Ability SFX (log)" },
    { "labs.field_probe_rt2",        "Field Probe RT2" },
    { "labs.field_probe_encounter",  "Field Probe (encounter)" },
    { "labs.field_probe_texture",    "Field Probe (texture)" },
    { "labs.field_scout",            "Field Scout" },
    { "labs.field_scout_map_only",   "Field Scout (map only)" },
    { "labs.field_scout_heavy",      "Field Scout (heavy)" },
    { "labs.field_scout_ultra",      "Field Scout (ultra)" },
    { "labs.field_scout_max",        "Field Scout (max)" },
    { "labs.fps_scout",              "FPS Scout" },
    { "labs.arena_plus_compose_f7",  "Arena+ Compose (F7)" },
    { "labs.arena_plus_progress",    "Arena+ Progress" },
};
static const int kInGameMenuFlagCount = (int)(sizeof(kInGameMenuFlags) / sizeof(kInGameMenuFlags[0]));
static volatile LONG g_ingameMenuMode = 0;  /* 0 = plugin switchboard (F11), 1 = flags panel (F5) */

static const uint32_t RVA_ACTIVE_CHR_COUNT = 0x01FC44E0u; /* VA 0x23C44E0 - 0x400000 */
static const uint32_t RVA_ACTIVE_CHR_TABLE = 0x01FC44E4u; /* VA 0x23C44E4 - 0x400000 */
static const uint32_t RVA_CONTROLLED_CHR_INSTANCE = 0x00F00740u; /* VA 0x1300740 - 0x400000 */
static const uint32_t RVA_BATTLE_PLAYER_LIST = 0x00D334CCu;
static const uint32_t RVA_BATTLE_ENEMY_LIST = 0x00D34460u;
static const uint32_t ACTIVE_CHR_STRIDE = 0x880u;
static const uint32_t BATTLE_CHR_STRIDE = 0xF90u;
static const uint32_t ACTIVE_CHR_MAX_COUNT = 4096u;
static const uint32_t AURORA_W2S_SCAN_MIN_ROOTS_DEFAULT = 4u;

enum AuroraActorKind {
    AURORA_ACTOR_PARTY = 0,
    AURORA_ACTOR_MONSTER = 1,
    AURORA_ACTOR_NPC = 2,
    AURORA_ACTOR_OBJECT = 3,
    AURORA_ACTOR_OTHER = 4
};

struct AuroraActor {
    uint16_t id;
    uint32_t index;
    uintptr_t inst;
    float x;
    float y;
    float z;
    float topX;
    float topY;
    float topZ;
    float labelScreenX;
    float labelScreenY;
    float labelClipW;
    float screenX;
    float screenY;
    float clipW;
    AuroraActorKind kind;
    bool visible;
    bool hasTopAnchor;
};

static AuroraActor g_auroraSniffActors[32] = {};
static uint32_t    g_auroraSniffActorCount = 0;
static int         g_auroraSniffViewportWidth = 0;
static int         g_auroraSniffViewportHeight = 0;
static float       g_auroraSniffW2SMatrix[16] = {};
static DWORD       g_auroraSniffW2SMatrixTick = 0;
static int         g_auroraSniffW2SMaxAgeMs = 3000;
static int         g_auroraD3DProjectionRefreshMs = 250;
static int         g_auroraD3DSniffAutoPauseHits = 3;
static uintptr_t   g_auroraSniffW2SCaller = 0;
static uint32_t    g_auroraSniffW2SOffset = 0;
static char        g_auroraSniffW2SLayout = 0;
static int         g_auroraSniffW2SCoordMode = 0;
static uint32_t    g_auroraSniffW2SActorCount = 0;
static uint32_t    g_auroraSniffW2SVisible = 0;
static float       g_auroraSniffW2SSpread = 0.0f;
static float       g_auroraSniffW2SScore = -FLT_MAX;
static DWORD       g_auroraLastSniffW2SUseLogTick = 0;

static bool AuroraD3DSniffCompactBattleScene();

static AuroraActorKind AuroraClassifyActor(uint16_t id) {
    if (id >= 0x0001 && id <= 0x0007) return AURORA_ACTOR_PARTY;
    if (id >= 0x1000 && id <= 0x1FFF) return AURORA_ACTOR_MONSTER;
    if (id >= 0x2000 && id <= 0x2FFF) return AURORA_ACTOR_NPC;
    if (id >= 0x5000 && id <= 0x5FFF) return AURORA_ACTOR_OBJECT;
    return AURORA_ACTOR_OTHER;
}

static bool AuroraKindVisibleByDefault(AuroraActorKind kind) {
    return kind == AURORA_ACTOR_PARTY || kind == AURORA_ACTOR_MONSTER;
}

static void AuroraResetLabelStats() {
    g_auroraLastLabelCount = 0;
    g_auroraLastPartyLabelCount = 0;
    g_auroraLastMonsterLabelCount = 0;
    g_auroraLastNpcLabelCount = 0;
    g_auroraLastObjectLabelCount = 0;
    g_auroraLastOtherLabelCount = 0;
}

static char AuroraKindPrefix(AuroraActorKind kind) {
    switch (kind) {
        case AURORA_ACTOR_PARTY: return 'P';
        case AURORA_ACTOR_MONSTER: return 'M';
        case AURORA_ACTOR_NPC: return 'N';
        case AURORA_ACTOR_OBJECT: return 'O';
        default: return 'X';
    }
}

static COLORREF AuroraKindColor(AuroraActorKind kind) {
    switch (kind) {
        case AURORA_ACTOR_PARTY: return RGB(80, 235, 210);
        case AURORA_ACTOR_MONSTER: return RGB(255, 120, 70);
        case AURORA_ACTOR_NPC: return RGB(120, 240, 120);
        case AURORA_ACTOR_OBJECT: return RGB(255, 210, 70);
        default: return RGB(210, 170, 255);
    }
}

static bool AuroraFinite(float v) {
    return _finite(v) != 0;
}

static double AuroraNowMsPrecise() {
    static LARGE_INTEGER freq = {};
    if (freq.QuadPart == 0) {
        if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) {
            freq.QuadPart = -1;
        }
    }
    if (freq.QuadPart > 0) {
        LARGE_INTEGER now = {};
        QueryPerformanceCounter(&now);
        return (static_cast<double>(now.QuadPart) * 1000.0) / static_cast<double>(freq.QuadPart);
    }
    return static_cast<double>(GetTickCount());
}

static bool AuroraPtrOk(uintptr_t p) {
    return p >= 0x10000u && p < 0x7FFF0000u &&
        p != 0xCDCDCDCDu && p != 0xDDDDDDDDu && p != 0xFEEEFEEEu;
}

static bool AuroraReadBytes(uintptr_t address, void* out, size_t len) {
    __try {
        memcpy(out, reinterpret_cast<const void*>(address), len);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        memset(out, 0, len);
        return false;
    }
}

static bool AuroraReadU8(uintptr_t address, uint8_t* out) {
    return AuroraReadBytes(address, out, sizeof(uint8_t));
}

static bool AuroraReadU16(uintptr_t address, uint16_t* out) {
    return AuroraReadBytes(address, out, sizeof(uint16_t));
}

static bool AuroraReadU32(uintptr_t address, uint32_t* out) {
    return AuroraReadBytes(address, out, sizeof(uint32_t));
}

static bool AuroraReadFloat(uintptr_t address, float* out) {
    return AuroraReadBytes(address, out, sizeof(float)) && AuroraFinite(*out);
}

static bool AuroraReadMatrix(uintptr_t address, float out[16]) {
    if (!AuroraReadBytes(address, out, sizeof(float) * 16)) return false;
    for (int i = 0; i < 16; ++i) {
        if (!AuroraFinite(out[i])) return false;
    }
    return true;
}

static float AuroraLabelFallbackLift(AuroraActorKind kind) {
    switch (kind) {
        case AURORA_ACTOR_PARTY: return 4.2f;
        case AURORA_ACTOR_MONSTER: return 5.0f;
        case AURORA_ACTOR_NPC: return 3.2f;
        case AURORA_ACTOR_OBJECT: return 1.5f;
        default: return 3.0f;
    }
}

static bool AuroraLooksLikeBonePoint(float x, float y, float z) {
    return AuroraFinite(x) && AuroraFinite(y) && AuroraFinite(z) &&
        fabsf(x) < 500.0f && fabsf(y) < 500.0f && fabsf(z) < 500.0f;
}

static bool AuroraTryReadTopBoneLocal(uintptr_t inst, uint16_t boneCount,
    float* outX, float* outY, float* outZ) {
    if (!AuroraPtrOk(inst) || boneCount == 0) return false;

    uint32_t poseArray = 0;
    if (!AuroraReadU32(inst + 0x328, &poseArray) || !AuroraPtrOk(poseArray)) {
        return false;
    }

    const uint32_t maxBones = boneCount > 192 ? 192u : static_cast<uint32_t>(boneCount);
    bool found = false;
    float bestX = 0.0f, bestY = -FLT_MAX, bestZ = 0.0f;

    for (uint32_t bone = 0; bone < maxBones; ++bone) {
        float m[16] = {};
        const uintptr_t matrixAddr = static_cast<uintptr_t>(poseArray) + bone * 352u + 136u;
        if (!AuroraReadMatrix(matrixAddr, m)) continue;

        float x = m[12], y = m[13], z = m[14];
        if (!AuroraLooksLikeBonePoint(x, y, z)) {
            x = m[3]; y = m[7]; z = m[11];
            if (!AuroraLooksLikeBonePoint(x, y, z)) continue;
        }

        if (!found || y > bestY) {
            found = true;
            bestX = x;
            bestY = y;
            bestZ = z;
        }
    }

    if (!found) return false;
    if (outX) *outX = bestX;
    if (outY) *outY = bestY;
    if (outZ) *outZ = bestZ;
    return true;
}

static bool AuroraTransformLocalPointRowMajor(const float m[16], float x, float y, float z,
    float* outX, float* outY, float* outZ) {
    if (!m || !outX || !outY || !outZ) return false;
    const float wx = x * m[0] + y * m[4] + z * m[8] + m[12];
    const float wy = x * m[1] + y * m[5] + z * m[9] + m[13];
    const float wz = x * m[2] + y * m[6] + z * m[10] + m[14];
    if (!AuroraFinite(wx) || !AuroraFinite(wy) || !AuroraFinite(wz)) return false;
    *outX = wx;
    *outY = wy;
    *outZ = wz;
    return true;
}

static bool AuroraReadBattleActiveMasks(bool partyModelIds[16], bool monsterIds[0x1000]) {
    if (partyModelIds) memset(partyModelIds, 0, sizeof(bool) * 16);
    if (monsterIds) memset(monsterIds, 0, sizeof(bool) * 0x1000);
    if (!g_base) return false;

    bool found = false;
    bool enemyFound = false;
    uint32_t playerBase = 0;
    if (partyModelIds && AuroraReadU32(rva(RVA_BATTLE_PLAYER_LIST), &playerBase) && AuroraPtrOk(playerBase)) {
        for (uint32_t i = 0; i < 18; ++i) {
            const uintptr_t chr = static_cast<uintptr_t>(playerBase) + i * BATTLE_CHR_STRIDE;
            uint16_t battleId = 0;
            uint8_t inBattle = 0;
            if (!AuroraReadU16(chr + 0x0E, &battleId) ||
                !AuroraReadU8(chr + 0xDC8, &inBattle) ||
                inBattle == 0) {
                continue;
            }
            const uint16_t modelId = static_cast<uint16_t>(battleId + 1u);
            if (modelId < 16) {
                partyModelIds[modelId] = true;
                found = true;
            }
        }
    }

    uint32_t enemyBase = 0;
    if (monsterIds && AuroraReadU32(rva(RVA_BATTLE_ENEMY_LIST), &enemyBase) && AuroraPtrOk(enemyBase)) {
        for (uint32_t i = 0; i < 16; ++i) {
            const uintptr_t chr = static_cast<uintptr_t>(enemyBase) + i * BATTLE_CHR_STRIDE;
            uint16_t battleId = 0;
            uint8_t inBattle = 0;
            if (!AuroraReadU16(chr + 0x0E, &battleId) ||
                !AuroraReadU8(chr + 0xDC8, &inBattle) ||
                inBattle == 0) {
                continue;
            }
            if (battleId >= 0x1000 && battleId <= 0x1FFF) {
                monsterIds[battleId - 0x1000] = true;
                enemyFound = true;
                found = true;
            }
        }
    }

    return found && enemyFound;
}

static bool AuroraFindGameClientRect(RECT* out) {
    struct EnumCtx {
        DWORD pid;
        HWND best;
        LONG bestArea;
    } ctx = { GetCurrentProcessId(), NULL, 0 };

    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        EnumCtx* ctx = reinterpret_cast<EnumCtx*>(lp);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != ctx->pid || !IsWindowVisible(hwnd)) return TRUE;

        char cls[128] = {};
        GetClassNameA(hwnd, cls, sizeof(cls));
        if (lstrcmpA(cls, "JarvisFfxLabMenuWindow") == 0 ||
            lstrcmpA(cls, "JarvisFfxAuroraOverlayWindow") == 0) {
            return TRUE;
        }

        RECT wr = {};
        if (!GetWindowRect(hwnd, &wr)) return TRUE;
        const LONG w = wr.right - wr.left;
        const LONG h = wr.bottom - wr.top;
        const LONG area = (w > 0 && h > 0) ? w * h : 0;
        if (area > ctx->bestArea) {
            ctx->best = hwnd;
            ctx->bestArea = area;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    if (!ctx.best) return false;
    RECT client = {};
    POINT origin = {};
    if (!GetClientRect(ctx.best, &client)) return false;
    if (!ClientToScreen(ctx.best, &origin)) return false;
    client.left += origin.x;
    client.right += origin.x;
    client.top += origin.y;
    client.bottom += origin.y;
    if (client.right <= client.left || client.bottom <= client.top) return false;
    if (out) *out = client;
    return true;
}

static bool AuroraKeyPressed(int vk) {
    static bool keyDown[256] = {};
    const int key = vk & 0xFF;
    const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool pressed = down && !keyDown[key];
    keyDown[key] = down;
    return pressed;
}

static bool AuroraDeveloperUiEnabled() {
    return InterlockedCompareExchange(&g_auroraDeveloperUiEnabled, 0, 0) != 0;
}

static bool AuroraDeveloperUiEnabledFromExplicitSources(
    bool environmentEnabled,
    bool overlayFileEnabled,
    bool overlayConfigEnabled,
    bool d3dFileEnabled,
    bool sniffFileEnabled) {
    return environmentEnabled || overlayFileEnabled || overlayConfigEnabled ||
           d3dFileEnabled || sniffFileEnabled;
}

static bool AuroraDeveloperHotkeyPressed(int virtualKey) {
    const bool pressed = AuroraKeyPressed(virtualKey);
    const bool developerEnabled = AuroraDeveloperUiEnabled();
    const bool controlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    const bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    return pressed && developerEnabled && controlDown && altDown && !shiftDown;
}

static bool InGameMenuConfigEnabled() {
    return EnvFlagEnabled("FFXHOOKS_INGAME_MENU") ||
           EnvFlagEnabled("FFXHOOKS_ENABLE_INGAME_MENU") ||
           ModuleFlagEnabled("ingame_menu.flag") ||
           ModuleFlagEnabled("config\\ingame_menu.flag") ||
           (AuroraConfigExists() && AuroraConfigInt("ingame_menu", 0) != 0) ||
           FfxHooks::Config::GetBool("dashboard.ingame_menu", false);
}

static bool InGameMenuStartOpen() {
    return EnvFlagEnabled("FFXHOOKS_INGAME_MENU_OPEN") ||
           ModuleFlagEnabled("ingame_menu_open.flag") ||
           ModuleFlagEnabled("config\\ingame_menu_open.flag") ||
           (AuroraConfigExists() && AuroraConfigInt("ingame_menu_open", 0) != 0);
}

static bool InGamePathExists(const char* path) {
    if (!path || !path[0]) return false;
    const DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static void InGameJoinPath(const char* dir, const char* file, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!dir || !file) return;
    _snprintf_s(out, outSize, _TRUNCATE, "%s%s", dir, file);
}

static void InGameMenuSetStatus(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(g_ingameMenuStatus, sizeof(g_ingameMenuStatus), _TRUNCATE, fmt, ap);
    va_end(ap);
    Log("[ffx-hooks] InGameMenu: %s\n", g_ingameMenuStatus);
}

static const char* InGameMenuKnownLabel(const char* file) {
    if (!file) return nullptr;
    if (_stricmp(file, "dinput8.dll") == 0) return "FFX Module Loader";
    if (_stricmp(file, "dxgi.dll") == 0) return "UnX / Special K DXGI";
    if (_stricmp(file, "unx.dll") == 0) return "UnX companion";
    if (_stricmp(file, "ff10-file-loader.dll") == 0) return "FFX External File Loader";
    if (_stricmp(file, "ffx-hooks.dll") == 0) return "FFX Hooks";
    if (_stricmp(file, "ffx-probe.dll") == 0) return "FFX Probe";
    return nullptr;
}

static const char* InGameMenuKnownKind(const char* file, bool rootHook) {
    if (!file) return rootHook ? "Root hook" : "Module DLL";
    if (_stricmp(file, "dinput8.dll") == 0) return "Loader core";
    if (_stricmp(file, "ffx-hooks.dll") == 0) return "Engine hook layer";
    if (_stricmp(file, "ffx-probe.dll") == 0) return "Main-thread bridge";
    if (_stricmp(file, "ff10-file-loader.dll") == 0) return "Module loader DLL";
    return rootHook ? "Root hook" : "Runtime plugin";
}

static bool InGameReadSmallTextFile(const char* path, char* out, size_t outSize) {
    if (!path || !out || outSize < 2) return false;
    out[0] = '\0';

    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    const DWORD size = GetFileSize(file, nullptr);
    if (size == INVALID_FILE_SIZE || size == 0 || size >= outSize) {
        CloseHandle(file);
        return false;
    }

    DWORD read = 0;
    const BOOL ok = ReadFile(file, out, size, &read, nullptr);
    CloseHandle(file);
    if (!ok || read == 0 || read >= outSize) {
        out[0] = '\0';
        return false;
    }
    out[read] = '\0';
    return true;
}

static bool InGameJsonStringValue(const char* json, const char* key, char* out, size_t outSize) {
    if (!json || !key || !out || outSize == 0) return false;
    out[0] = '\0';

    char needle[80] = {};
    _snprintf_s(needle, sizeof(needle), _TRUNCATE, "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p != ':') return false;
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p != '"') return false;
    ++p;

    size_t pos = 0;
    while (*p && *p != '"' && pos + 1 < outSize) {
        if (*p == '\\' && p[1]) {
            ++p;
            if (*p == 'n' || *p == 'r' || *p == 't') {
                out[pos++] = ' ';
            } else {
                out[pos++] = *p;
            }
            ++p;
            continue;
        }
        out[pos++] = *p++;
    }
    out[pos] = '\0';
    return pos > 0;
}

static bool InGameFirstJsonString(const char* json, const char* const* keys, int keyCount, char* out, size_t outSize) {
    if (!keys || keyCount <= 0) return false;
    for (int i = 0; i < keyCount; ++i) {
        if (InGameJsonStringValue(json, keys[i], out, outSize)) return true;
    }
    return false;
}

static void InGameMenuApplyManifest(InGameMenuPlugin& item, const char* file) {
    if (!file || !file[0]) return;

    char moduleDir[MAX_PATH] = {};
    if (!ModuleDirectoryPath(moduleDir, sizeof(moduleDir))) return;

    char baseName[64] = {};
    lstrcpynA(baseName, file, static_cast<int>(sizeof(baseName)));
    char* dllSuffix = strstr(baseName, ".dll");
    if (dllSuffix) *dllSuffix = '\0';

    char candidates[6][MAX_PATH] = {};
    _snprintf_s(candidates[0], sizeof(candidates[0]), _TRUNCATE, "%s%s.plugin.json", moduleDir, file);
    _snprintf_s(candidates[1], sizeof(candidates[1]), _TRUNCATE, "%s%s.plugin.json", moduleDir, baseName);
    _snprintf_s(candidates[2], sizeof(candidates[2]), _TRUNCATE, "%sconfig\\%s.plugin.json", moduleDir, file);
    _snprintf_s(candidates[3], sizeof(candidates[3]), _TRUNCATE, "%sconfig\\%s.plugin.json", moduleDir, baseName);
    _snprintf_s(candidates[4], sizeof(candidates[4]), _TRUNCATE, "%sconfig\\plugins\\%s.json", moduleDir, baseName);
    _snprintf_s(candidates[5], sizeof(candidates[5]), _TRUNCATE, "%sconfig\\plugins\\%s.json", moduleDir, file);

    char json[4096] = {};
    bool loadedManifest = false;
    for (int i = 0; i < 6; ++i) {
        if (InGameReadSmallTextFile(candidates[i], json, sizeof(json))) {
            loadedManifest = true;
            break;
        }
    }
    if (!loadedManifest) return;

    char value[128] = {};
    const char* const labelKeys[] = { "displayName", "display_name", "title", "name", "id" };
    if (InGameFirstJsonString(json, labelKeys, 5, value, sizeof(value))) {
        lstrcpynA(item.label, value, static_cast<int>(sizeof(item.label)));
    }

    const char* const kindKeys[] = { "kind", "category", "type" };
    if (InGameFirstJsonString(json, kindKeys, 3, value, sizeof(value))) {
        lstrcpynA(item.kind, value, static_cast<int>(sizeof(item.kind)));
    }
}

static void InGameMenuAddOrUpdatePlugin(const char* file, bool rootHook, bool diskOn, bool diskOff, bool loaded) {
    if (!file || !file[0]) return;

    int index = -1;
    for (int i = 0; i < g_ingameMenuPluginCount; ++i) {
        if (_stricmp(g_ingameMenuPlugins[i].file, file) == 0) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        if (g_ingameMenuPluginCount >= INGAME_MENU_MAX_PLUGINS) return;
        index = g_ingameMenuPluginCount++;
        memset(&g_ingameMenuPlugins[index], 0, sizeof(g_ingameMenuPlugins[index]));
        lstrcpynA(g_ingameMenuPlugins[index].file, file, static_cast<int>(sizeof(g_ingameMenuPlugins[index].file)));

        const char* label = InGameMenuKnownLabel(file);
        lstrcpynA(g_ingameMenuPlugins[index].label, label ? label : file, static_cast<int>(sizeof(g_ingameMenuPlugins[index].label)));
        lstrcpynA(g_ingameMenuPlugins[index].kind, InGameMenuKnownKind(file, rootHook), static_cast<int>(sizeof(g_ingameMenuPlugins[index].kind)));
        if (!rootHook) {
            InGameMenuApplyManifest(g_ingameMenuPlugins[index], file);
        }
    }

    InGameMenuPlugin& item = g_ingameMenuPlugins[index];
    item.rootHook = item.rootHook || rootHook;
    item.diskOn = item.diskOn || diskOn;
    item.diskOff = item.diskOff || diskOff;
    item.loaded = item.loaded || loaded || GetModuleHandleA(file) != NULL;
}

static void InGameMenuAddRootHook(const char* file) {
    char root[MAX_PATH] = {};
    char path[MAX_PATH] = {};
    char disabledPath[MAX_PATH] = {};
    const bool hasRoot = GameRootDirectoryPath(root, sizeof(root));
    if (hasRoot) {
        InGameJoinPath(root, file, path, sizeof(path));
        _snprintf_s(disabledPath, sizeof(disabledPath), _TRUNCATE, "%s%s.disabled", root, file);
    }

    InGameMenuAddOrUpdatePlugin(
        file,
        true,
        hasRoot && InGamePathExists(path),
        hasRoot && InGamePathExists(disabledPath),
        GetModuleHandleA(file) != NULL);
}

static int InGameMenuPluginPriority(const InGameMenuPlugin& item) {
    if (_stricmp(item.file, "ffx-hooks.dll") == 0) return 10;
    if (_stricmp(item.file, "ffx-probe.dll") == 0) return 20;
    if (_stricmp(item.file, "dxgi.dll") == 0) return 30;
    if (_stricmp(item.file, "unx.dll") == 0) return 40;
    if (_stricmp(item.file, "ff10-file-loader.dll") == 0) return 50;
    if (_stricmp(item.file, "dinput8.dll") == 0) return 60;
    if (item.loaded) return 70;
    if (item.diskOn) return 80;
    if (item.diskOff) return 90;
    return 100;
}

static void InGameMenuSortPlugins() {
    for (int i = 1; i < g_ingameMenuPluginCount; ++i) {
        InGameMenuPlugin current = g_ingameMenuPlugins[i];
        int j = i - 1;
        while (j >= 0) {
            const int leftPriority = InGameMenuPluginPriority(g_ingameMenuPlugins[j]);
            const int rightPriority = InGameMenuPluginPriority(current);
            const bool move =
                leftPriority > rightPriority ||
                (leftPriority == rightPriority && _stricmp(g_ingameMenuPlugins[j].label, current.label) > 0);
            if (!move) break;
            g_ingameMenuPlugins[j + 1] = g_ingameMenuPlugins[j];
            --j;
        }
        g_ingameMenuPlugins[j + 1] = current;
    }
}

static void InGameMenuRefreshPlugins() {
    g_ingameMenuPluginCount = 0;
    memset(g_ingameMenuPlugins, 0, sizeof(g_ingameMenuPlugins));

    InGameMenuAddRootHook("dinput8.dll");
    InGameMenuAddRootHook("dxgi.dll");
    InGameMenuAddRootHook("unx.dll");

    char moduleDir[MAX_PATH] = {};
    if (ModuleDirectoryPath(moduleDir, sizeof(moduleDir))) {
        char pattern[MAX_PATH] = {};
        WIN32_FIND_DATAA data = {};

        InGameJoinPath(moduleDir, "*.dll", pattern, sizeof(pattern));
        HANDLE find = FindFirstFileA(pattern, &data);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                    InGameMenuAddOrUpdatePlugin(data.cFileName, false, true, false, GetModuleHandleA(data.cFileName) != NULL);
                }
            } while (FindNextFileA(find, &data));
            FindClose(find);
        }

        InGameJoinPath(moduleDir, "*.dll.disabled", pattern, sizeof(pattern));
        find = FindFirstFileA(pattern, &data);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                    char file[64] = {};
                    lstrcpynA(file, data.cFileName, static_cast<int>(sizeof(file)));
                    char* disabled = strstr(file, ".disabled");
                    if (disabled) *disabled = '\0';
                    InGameMenuAddOrUpdatePlugin(file, false, false, true, GetModuleHandleA(file) != NULL);
                }
            } while (FindNextFileA(find, &data));
            FindClose(find);
        }
    }

    InGameMenuSortPlugins();
    g_ingameMenuLastScanTick = GetTickCount();
}

static const char* InGameMenuPluginState(const InGameMenuPlugin& item, COLORREF* color) {
    if (item.loaded) {
        if (color) *color = RGB(94, 220, 185);
        return "Rodando";
    }
    if (item.diskOn) {
        if (color) *color = RGB(94, 220, 185);
        return "Ligado disco";
    }
    if (item.diskOff) {
        if (color) *color = RGB(255, 64, 87);
        return "Desligado";
    }
    if (color) *color = RGB(246, 196, 92);
    return "Ausente";
}

static FfxHooks::F8Ui::SwitchboardRowMapper InGameMenuSwitchboardRows() {
    return FfxHooks::F8Ui::SwitchboardRowMapper(
        AuroraDeveloperUiEnabled(), g_ingameMenuPluginCount);
}

static int InGameMenuRowCount() {
    if (InterlockedCompareExchange(&g_ingameMenuMode, 0, 0)) return kInGameMenuFlagCount;
    return InGameMenuSwitchboardRows().Count();
}

static void InGameMenuClampSelection() {
    const int count = InGameMenuRowCount();
    if (count <= 0) {
        g_ingameMenuSelected = 0;
        g_ingameMenuScroll = 0;
        return;
    }
    if (g_ingameMenuSelected < 0) g_ingameMenuSelected = 0;
    if (g_ingameMenuSelected >= count) g_ingameMenuSelected = count - 1;
    if (g_ingameMenuScroll < 0) g_ingameMenuScroll = 0;
    if (g_ingameMenuScroll > g_ingameMenuSelected) g_ingameMenuScroll = g_ingameMenuSelected;
}

static void InGameMenuActivateSelection() {
    InGameMenuClampSelection();
    const int row = g_ingameMenuSelected;
    if (InterlockedCompareExchange(&g_ingameMenuMode, 0, 0)) {
        if (row >= 0 && row < kInGameMenuFlagCount) {
            const InGameMenuFlag& f = kInGameMenuFlags[row];
            const bool cur = FfxHooks::Config::GetBool(f.key, false);
            FfxHooks::Config::SetBool(f.key, !cur);
            Log("[ffx-hooks] Flags panel: %s = %s (was %s; INI written)\n", f.key, !cur ? "ON" : "OFF", cur ? "ON" : "OFF");
            InGameMenuSetStatus("%s = %s (INI saved; restart the game to apply)", f.label, cur ? "OFF" : "ON");
        }
        return;
    }
    const FfxHooks::F8Ui::SwitchboardRow mapped =
        InGameMenuSwitchboardRows().At(row);
    switch (mapped.kind) {
        case FfxHooks::F8Ui::SwitchboardRowKind::AuroraActor: {
            if (!AuroraDeveloperUiEnabled()) {
                InGameMenuSetStatus("Aurora developer UI is disabled; enable an Aurora overlay source");
                return;
            }
            const LONG enabled =
                InterlockedCompareExchange(&g_auroraActorOverlayEnabled, 0, 0) ? 0 : 1;
            InterlockedExchange(&g_auroraActorOverlayEnabled, enabled);
            InterlockedExchange(&g_auroraOverlayVisible, enabled);
            InGameMenuSetStatus("Aurora actor labels %s", enabled ? "on" : "off");
            return;
        }
        case FfxHooks::F8Ui::SwitchboardRowKind::AuroraDetail: {
            if (!AuroraDeveloperUiEnabled()) {
                InGameMenuSetStatus("Aurora developer UI is disabled; enable an Aurora overlay source");
                return;
            }
            const LONG detail =
                InterlockedCompareExchange(&g_auroraOverlayDetail, 0, 0) ? 0 : 1;
            InterlockedExchange(&g_auroraOverlayDetail, detail);
            InGameMenuSetStatus("Aurora detail labels %s", detail ? "on" : "off");
            return;
        }
        case FfxHooks::F8Ui::SwitchboardRowKind::Refresh:
            InGameMenuRefreshPlugins();
            InGameMenuSetStatus("Plugin list refreshed: %d item(s)", g_ingameMenuPluginCount);
            return;
        case FfxHooks::F8Ui::SwitchboardRowKind::Plugin:
            if (mapped.pluginIndex >= 0 && mapped.pluginIndex < g_ingameMenuPluginCount) {
                const InGameMenuPlugin& item = g_ingameMenuPlugins[mapped.pluginIndex];
                if (item.loaded) {
                    InGameMenuSetStatus("%s is loaded; live DLL unload is blocked, use editor for next boot staging", item.file);
                } else if (item.diskOff) {
                    InGameMenuSetStatus("%s is staged off; enable it in the editor before next boot", item.file);
                } else if (item.diskOn) {
                    InGameMenuSetStatus("%s is staged on; restart FFX if it is not loaded yet", item.file);
                } else {
                    InGameMenuSetStatus("%s is absent from this runtime", item.file);
                }
            }
            return;
        default:
            return;
    }
}

static bool InGameMenuKeyAllowedNow(int vk) {
    const int key = vk & 0xFF;
    const DWORD now = GetTickCount();
    const DWORD elapsed = now - g_ingameMenuLastKeyTick[key];
    const bool repeatKey =
        vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT ||
        vk == VK_HOME || vk == VK_END || vk == 'W' || vk == 'S';
    const DWORD debounceMs = repeatKey ? 90u : 150u;
    if (elapsed < debounceMs) return false;
    g_ingameMenuLastKeyTick[key] = now;
    return true;
}

static bool InGameMenuProcessKey(int vk, const char* source) {
    if (!InterlockedCompareExchange(&g_ingameMenuEnabled, 1, 1)) return false;
    if (!InGameMenuKeyAllowedNow(vk)) return true;

    if (vk == VK_OEM_3 || vk == VK_F11) {
        const bool flagsMode = (vk == VK_OEM_3);
        const LONG wasOpen = InterlockedCompareExchange(&g_ingameMenuOpen, 1, 1);
        if (wasOpen && InterlockedCompareExchange(&g_ingameMenuMode, 0, 0) == (flagsMode ? 1 : 0)) {
            InterlockedExchange(&g_ingameMenuOpen, 0);
            Log("[ffx-hooks] InGameMenu closed source=%s\n", source ? source : "unknown");
            return true;
        }
        InterlockedExchange(&g_ingameMenuMode, flagsMode ? 1 : 0);
        InterlockedExchange(&g_ingameMenuOpen, 1);
        g_ingameMenuSelected = 0;
        g_ingameMenuScroll = 0;
        if (!flagsMode) InGameMenuRefreshPlugins();
        InGameMenuSetStatus(flagsMode
            ? "Flags panel (`): Enter toggles the INI flag; restart the game to apply"
            : "Plugin switchboard (F11) ready");
        Log("[ffx-hooks] InGameMenu open=1 mode=%s source=%s\n", flagsMode ? "flags" : "plugins", source ? source : "unknown");
        return true;
    }

    // The legacy F8/INSERT InGameMenu fallback is deliberately unreachable.
    // Only the dashboard owns those keys; when disabled, input returns to the game.
    if (false && (vk == VK_F8 || vk == VK_INSERT)) {
        if (FfxHooks::Config::GetBool("dashboard.enabled", false)) {
            Log("[ffx-hooks] InGameMenu: F8/INSERT owned by dashboard (arbitration)\n");
            return true;  // consumed — dashboard DWP handles it
        }
        const LONG open = InterlockedCompareExchange(&g_ingameMenuOpen, 0, 0) ? 0 : 1;
        InterlockedExchange(&g_ingameMenuOpen, open);
        if (open) {
            InGameMenuRefreshPlugins();
            InGameMenuSetStatus("Runtime plugin switchboard ready; F8 opens/closes");
        }
        Log("[ffx-hooks] InGameMenu open=%d source=%s\n", open ? 1 : 0, source ? source : "unknown");
        return true;
    }

    if (!InterlockedCompareExchange(&g_ingameMenuOpen, 1, 1)) return false;

    if (vk == VK_ESCAPE || vk == VK_BACK || vk == 'C' || vk == 'X') {
        InterlockedExchange(&g_ingameMenuOpen, 0);
        Log("[ffx-hooks] InGameMenu open=0 source=%s\n", source ? source : "unknown");
        return true;
    }

    if (vk == VK_UP || vk == 'W' || vk == VK_NUMPAD8) {
        --g_ingameMenuSelected;
        InGameMenuClampSelection();
        return true;
    }
    if (vk == VK_DOWN || vk == 'S' || vk == VK_NUMPAD2) {
        ++g_ingameMenuSelected;
        InGameMenuClampSelection();
        return true;
    }
    if (vk == VK_HOME) {
        g_ingameMenuSelected = 0;
        g_ingameMenuScroll = 0;
        return true;
    }
    if (vk == VK_END) {
        g_ingameMenuSelected = InGameMenuRowCount() - 1;
        InGameMenuClampSelection();
        return true;
    }
    if (vk == VK_RETURN || vk == VK_SPACE || vk == 'E' || vk == 'Z') {
        InGameMenuActivateSelection();
        return true;
    }

    return true;
}

static bool InGameMenuHandleInput() {
    if (!InterlockedCompareExchange(&g_ingameMenuEnabled, 1, 1)) return false;

    if (AuroraKeyPressed(VK_OEM_3)) InGameMenuProcessKey(VK_OEM_3, "poll");
    if (AuroraKeyPressed(VK_F11)) InGameMenuProcessKey(VK_F11, "poll");

    if (InterlockedCompareExchange(&g_ingameMenuOpen, 1, 1)) {
        if (AuroraKeyPressed(VK_ESCAPE)) InGameMenuProcessKey(VK_ESCAPE, "poll");
        if (AuroraKeyPressed(VK_BACK)) InGameMenuProcessKey(VK_BACK, "poll");
        if (AuroraKeyPressed('C')) InGameMenuProcessKey('C', "poll");
        if (AuroraKeyPressed('X')) InGameMenuProcessKey('X', "poll");
        if (AuroraKeyPressed(VK_UP)) InGameMenuProcessKey(VK_UP, "poll");
        if (AuroraKeyPressed(VK_DOWN)) InGameMenuProcessKey(VK_DOWN, "poll");
        if (AuroraKeyPressed('W')) InGameMenuProcessKey('W', "poll");
        if (AuroraKeyPressed('S')) InGameMenuProcessKey('S', "poll");
        if (AuroraKeyPressed(VK_NUMPAD8)) InGameMenuProcessKey(VK_NUMPAD8, "poll");
        if (AuroraKeyPressed(VK_NUMPAD2)) InGameMenuProcessKey(VK_NUMPAD2, "poll");
        if (AuroraKeyPressed(VK_HOME)) InGameMenuProcessKey(VK_HOME, "poll");
        if (AuroraKeyPressed(VK_END)) InGameMenuProcessKey(VK_END, "poll");
        if (AuroraKeyPressed(VK_RETURN)) InGameMenuProcessKey(VK_RETURN, "poll");
        if (AuroraKeyPressed(VK_SPACE)) InGameMenuProcessKey(VK_SPACE, "poll");
        if (AuroraKeyPressed('E')) InGameMenuProcessKey('E', "poll");
        if (AuroraKeyPressed('Z')) InGameMenuProcessKey('Z', "poll");
    }

    return InterlockedCompareExchange(&g_ingameMenuOpen, 1, 1) != 0;
}

static bool ArenaMixRenameMessage(UINT message, WPARAM character);
static bool ArenaMixRenameInputActive();
static void ArenaMixRenameAbort();
static LRESULT CALLBACK InGameMenuWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (FfxHooks::NativePorts::BindingCaptureMessage(msg, wParam)) return 0;
    if (ArenaMixRenameMessage(msg, wParam)) return 0;
    LRESULT nativePortResult = 0;
    if (FfxHooks::NativePorts::HandleWindowMessage(hwnd, msg, wParam, lParam,
        FfxHooks::NativePorts::BindingCaptureActive() || ArenaMixRenameInputActive(), &nativePortResult)) return nativePortResult;
    switch (msg) {
        case WM_ACTIVATEAPP:
            if (!wParam) {
                FfxHooks::SpeedHackNotifyForegroundLost();
                FfxHooks::Maechen_NotifyForegroundLost();
                InterlockedExchange(&g_f7ForegroundLost, 1);
            }
            break;
        case WM_KILLFOCUS:
            FfxHooks::SpeedHackNotifyForegroundLost();
            FfxHooks::Maechen_NotifyForegroundLost();
            InterlockedExchange(&g_f7ForegroundLost, 1);
            break;
        case WM_MOUSEWHEEL:
            InterlockedExchangeAdd(
                &g_f7MouseWheelDelta,
                static_cast<LONG>(GET_WHEEL_DELTA_WPARAM(wParam)));
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (InGameMenuProcessKey(static_cast<int>(wParam), "wndproc")) {
                return 0;
            }
            break;
    }

    WNDPROC original = g_ingameMenuOriginalWndProc;
    return original ? CallWindowProcA(original, hwnd, msg, wParam, lParam)
                    : DefWindowProcA(hwnd, msg, wParam, lParam);
}

static void InGameMenuInstallWndProc(HWND hwnd) {
    // Focus loss must neutralize Speed Hack even when the optional F8 menu is disabled.
    // InGameMenuProcessKey keeps menu input behind its own configuration gate.
    if (!hwnd) return;
    if (g_ingameMenuInputHwnd == hwnd && g_ingameMenuOriginalWndProc) return;

    if (g_ingameMenuInputHwnd && g_ingameMenuOriginalWndProc && IsWindow(g_ingameMenuInputHwnd)) {
        LONG_PTR current = GetWindowLongPtrA(g_ingameMenuInputHwnd, GWLP_WNDPROC);
        if (current == reinterpret_cast<LONG_PTR>(InGameMenuWndProc)) {
            SetWindowLongPtrA(g_ingameMenuInputHwnd, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(g_ingameMenuOriginalWndProc));
        }
    }

    SetLastError(0);
    LONG_PTR previous = SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(InGameMenuWndProc));
    if (!previous && GetLastError() != 0) {
        Log("[ffx-hooks] WARN InGameMenu WndProc install failed hwnd=%p err=%u\n",
            hwnd, GetLastError());
        return;
    }

    g_ingameMenuInputHwnd = hwnd;
    g_ingameMenuOriginalWndProc = reinterpret_cast<WNDPROC>(previous);
    Log("[ffx-hooks] InGameMenu WndProc installed hwnd=%p\n", hwnd);
}

static void InGameMenuRestoreWndProc() {
    if (!g_ingameMenuInputHwnd || !g_ingameMenuOriginalWndProc) return;
    if (IsWindow(g_ingameMenuInputHwnd)) {
        LONG_PTR current = GetWindowLongPtrA(g_ingameMenuInputHwnd, GWLP_WNDPROC);
        if (current == reinterpret_cast<LONG_PTR>(InGameMenuWndProc)) {
            SetWindowLongPtrA(g_ingameMenuInputHwnd, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(g_ingameMenuOriginalWndProc));
            Log("[ffx-hooks] InGameMenu WndProc restored hwnd=%p\n", g_ingameMenuInputHwnd);
        }
    }
    g_ingameMenuInputHwnd = NULL;
    g_ingameMenuOriginalWndProc = nullptr;
}

static void InGameDrawFilledRect(HDC hdc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
}

static void InGameDrawRoundFillStroke(HDC hdc, const RECT& rect, COLORREF fillColor, COLORREF strokeColor, int radius) {
    HBRUSH fill = CreateSolidBrush(fillColor);
    HPEN pen = CreatePen(PS_SOLID, 1, strokeColor);
    HGDIOBJ oldBrush = SelectObject(hdc, fill);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(fill);
    DeleteObject(pen);
}

static void InGameDrawRoundStroke(HDC hdc, RECT rect, COLORREF color, int thickness, int radius) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    for (int i = 0; i < thickness; ++i) {
        RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
        InflateRect(&rect, -1, -1);
    }
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void InGameDrawSoftText(HDC hdc, const char* text, RECT rect, UINT format,
    COLORREF color, bool glow) {
    if (!text) return;
    SetBkMode(hdc, TRANSPARENT);

    if (glow) {
        RECT glowRect = rect;
        SetTextColor(hdc, RGB(0, 82, 88));
        OffsetRect(&glowRect, -1, 0);
        DrawTextA(hdc, text, -1, &glowRect, format);
        glowRect = rect;
        OffsetRect(&glowRect, 1, 0);
        DrawTextA(hdc, text, -1, &glowRect, format);
    }

    RECT shadow = rect;
    OffsetRect(&shadow, 1, 1);
    SetTextColor(hdc, RGB(0, 7, 11));
    DrawTextA(hdc, text, -1, &shadow, format);

    SetTextColor(hdc, color);
    DrawTextA(hdc, text, -1, &rect, format);
}

static void InGameMenuStatePalette(COLORREF stateColor, COLORREF* fill, COLORREF* stroke,
    COLORREF* glow, COLORREF* text) {
    const BYTE r = GetRValue(stateColor);
    const BYTE g = GetGValue(stateColor);
    const BYTE b = GetBValue(stateColor);
    const bool red = r > 180 && g < 120;
    const bool amber = r > 190 && g > 120 && b < 135;

    if (red) {
        if (fill) *fill = RGB(63, 22, 31);
        if (stroke) *stroke = RGB(211, 67, 84);
        if (glow) *glow = RGB(118, 24, 38);
        if (text) *text = RGB(255, 166, 174);
    } else if (amber) {
        if (fill) *fill = RGB(69, 47, 15);
        if (stroke) *stroke = RGB(221, 167, 62);
        if (glow) *glow = RGB(116, 78, 18);
        if (text) *text = RGB(255, 216, 132);
    } else {
        if (fill) *fill = RGB(15, 58, 45);
        if (stroke) *stroke = RGB(69, 186, 147);
        if (glow) *glow = RGB(25, 108, 84);
        if (text) *text = RGB(166, 244, 210);
    }
}

static void InGameDrawStatePill(HDC hdc, RECT rect, const char* text, COLORREF color) {
    COLORREF fill = RGB(15, 58, 45);
    COLORREF stroke = RGB(69, 186, 147);
    COLORREF glowColor = RGB(25, 108, 84);
    COLORREF textColor = RGB(166, 244, 210);
    InGameMenuStatePalette(color, &fill, &stroke, &glowColor, &textColor);

    RECT glow = rect;
    InflateRect(&glow, 2, 2);
    InGameDrawRoundStroke(hdc, glow, glowColor, 1, 14);
    RECT softGlow = rect;
    InflateRect(&softGlow, 1, 1);
    InGameDrawRoundStroke(hdc, softGlow, stroke, 1, 12);

    InGameDrawRoundFillStroke(hdc, rect, fill, stroke, 12);

    InGameDrawSoftText(hdc, text, rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
        textColor, false);
}

static void InGameDrawNativePerformance(HDC hdc,const RECT& rc) {
    if (!FfxHooks::NativePorts::CurrentSettings().performance) return;
    const int available=(rc.right-rc.left)/2-228;
    if(available<70)return;
    const int width=available<242?available:242;
    RECT card={rc.left+18,rc.top+14,rc.left+18+width,rc.top+48};
    InGameDrawRoundFillStroke(hdc,card,RGB(18,38,47),RGB(75,116,126),6);
    char text[64] = {};
    if (g_nativePerformanceAverageMs>0.0) {
        if(width<190)_snprintf_s(text,sizeof(text),_TRUNCATE,"%.0f FPS",1000.0/g_nativePerformanceAverageMs);
        else _snprintf_s(text,sizeof(text),_TRUNCATE,"%.1f FPS  |  %.2f ms",1000.0/g_nativePerformanceAverageMs,g_nativePerformanceAverageMs);
    }
    else strcpy_s(text,"Measuring frame timing...");
    SetBkMode(hdc,TRANSPARENT);SetTextColor(hdc,RGB(223,237,244));
    DrawTextA(hdc,text,-1,&card,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
}

static bool InGameDrawSpeedHackIndicator(HDC hdc, const RECT& rc) {
    if (!hdc || rc.right <= rc.left || rc.bottom <= rc.top) return false;

    const FfxHooks::SpeedHackRuntimeSnapshot snapshot =
        FfxHooks::GetSpeedHackRuntimeSnapshot();
    const FfxHooks::SpeedHackIndicatorState indicator =
        FfxHooks::ResolveSpeedHackIndicator(
            FfxHooks::Config::GetBool("boosters.speed_hack", false),
            snapshot);
    if (indicator.mode == FfxHooks::SpeedHackIndicatorMode::Hidden) return false;

    FfxHooks::F8Ui::SpeedIndicatorLabelMode labelMode =
        FfxHooks::F8Ui::SpeedIndicatorLabelMode::Unavailable;
    if (indicator.mode == FfxHooks::SpeedHackIndicatorMode::Unavailable) {
        labelMode = FfxHooks::F8Ui::SpeedIndicatorLabelMode::Unavailable;
    } else if (indicator.mode == FfxHooks::SpeedHackIndicatorMode::Conflict) {
        labelMode = FfxHooks::F8Ui::SpeedIndicatorLabelMode::Conflict;
    } else if (indicator.mode == FfxHooks::SpeedHackIndicatorMode::Paused) {
        labelMode = FfxHooks::F8Ui::SpeedIndicatorLabelMode::Paused;
    } else if (indicator.backend == FfxHooks::SpeedHackBackend::Movie) {
        labelMode=indicator.mode==FfxHooks::SpeedHackIndicatorMode::Applied?
            FfxHooks::F8Ui::SpeedIndicatorLabelMode::MovieApplied:FfxHooks::F8Ui::SpeedIndicatorLabelMode::MovieArmed;
    } else if (indicator.backend == FfxHooks::SpeedHackBackend::FastFieldScenes) {
        labelMode = indicator.mode == FfxHooks::SpeedHackIndicatorMode::Applied
            ? FfxHooks::F8Ui::SpeedIndicatorLabelMode::FastFieldScenes
            : FfxHooks::F8Ui::SpeedIndicatorLabelMode::FastFieldScenesArmed;
    } else if (indicator.backend == FfxHooks::SpeedHackBackend::NativeStandard) {
        labelMode = FfxHooks::F8Ui::SpeedIndicatorLabelMode::StandardBoost;
    } else {
        labelMode = FfxHooks::F8Ui::SpeedIndicatorLabelMode::Armed;
    }
    char text[FfxHooks::F8Ui::SpeedIndicatorCharacterBudget + 1] = {};
    const char* shortcut=FfxHooks::NativePorts::BindingText(FfxHooks::NativeBindings::Action::SpeedCycle);
    if(strlen(shortcut)>22)shortcut="Configured shortcut";
    if (!FfxHooks::F8Ui::BuildSpeedIndicatorLabel(
            static_cast<unsigned>(indicator.factor), labelMode, text, sizeof(text),shortcut)) {
        return false;
    }

    // Keep the pill centered below the top edge: the user's performance OSD occupies the
    // upper-right corner, while the F8 panel begins lower and therefore does not cover it.
    const int centerX = (rc.left + rc.right) / 2;
    const bool armed = labelMode == FfxHooks::F8Ui::SpeedIndicatorLabelMode::Armed;
    const bool unavailable = indicator.mode == FfxHooks::SpeedHackIndicatorMode::Unavailable;
    const bool conflict = indicator.mode == FfxHooks::SpeedHackIndicatorMode::Conflict;
    const bool blocked = unavailable || conflict ||
        indicator.mode == FfxHooks::SpeedHackIndicatorMode::Paused;
    const int pillWidth = armed ? 344 : unavailable ? 250 : 390;
    const int pillHeight = 34;
    RECT pill = {
        centerX - pillWidth / 2,
        rc.top + 14,
        centerX + pillWidth / 2,
        rc.top + 14 + pillHeight,
    };
    const bool accelerated = indicator.mode == FfxHooks::SpeedHackIndicatorMode::Applied;
    const COLORREF fill = conflict
        ? RGB(67, 24, 28)
        : blocked
        ? RGB(66, 43, 17)
        : accelerated ? RGB(12, 55, 43) : RGB(18, 38, 47);
    const COLORREF stroke = conflict
        ? RGB(230, 91, 102)
        : blocked
        ? RGB(221, 167, 62)
        : accelerated ? RGB(69, 220, 167) : RGB(75, 116, 126);
    const COLORREF textColor = conflict
        ? RGB(255, 205, 209)
        : blocked
        ? RGB(255, 216, 132)
        : accelerated ? RGB(185, 255, 223) : RGB(220, 232, 234);

    InGameDrawRoundFillStroke(hdc, pill, fill, stroke, 12);
    HFONT font = CreateFontA(
        -17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Bahnschrift");
    HGDIOBJ oldFont = font ? SelectObject(hdc, font) : NULL;
    InGameDrawSoftText(
        hdc, text, pill, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
        textColor, accelerated);
    if (oldFont) SelectObject(hdc, oldFont);
    if (font) DeleteObject(font);
    return true;
}

enum InGameMenuIconType {
    InGameMenuIconEye = 0,
    InGameMenuIconList = 1,
    InGameMenuIconRefresh = 2,
    InGameMenuIconPuzzle = 3,
    InGameMenuIconShield = 4,
    InGameMenuIconGear = 5
};

static int InGameMenuIconForRow(FfxHooks::F8Ui::SwitchboardRowKind kind, const char* file) {
    if (kind == FfxHooks::F8Ui::SwitchboardRowKind::AuroraActor) return InGameMenuIconEye;
    if (kind == FfxHooks::F8Ui::SwitchboardRowKind::AuroraDetail) return InGameMenuIconList;
    if (kind == FfxHooks::F8Ui::SwitchboardRowKind::Refresh) return InGameMenuIconRefresh;
    if (file && _stricmp(file, "ffx-probe.dll") == 0) return InGameMenuIconShield;
    if (file && (_stricmp(file, "dxgi.dll") == 0 || _stricmp(file, "unx.dll") == 0)) return InGameMenuIconGear;
    return InGameMenuIconPuzzle;
}

static int InGameMenuIconForLegacyFlagRow(int row) {
    if (row == 0) return InGameMenuIconEye;
    if (row == 1) return InGameMenuIconList;
    if (row == 2) return InGameMenuIconRefresh;
    return InGameMenuIconPuzzle;
}

static void InGameDrawIcon(HDC hdc, int type, const RECT& box, COLORREF color) {
    const int cx = (box.left + box.right) / 2;
    const int cy = (box.top + box.bottom) / 2;
    const int w = box.right - box.left;
    const int h = box.bottom - box.top;
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HBRUSH fill = CreateSolidBrush(color);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

    if (type == InGameMenuIconEye) {
        Ellipse(hdc, box.left + 2, cy - h / 4, box.right - 2, cy + h / 4);
        SelectObject(hdc, fill);
        Ellipse(hdc, cx - 3, cy - 3, cx + 3, cy + 3);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
    } else if (type == InGameMenuIconList) {
        for (int i = 0; i < 3; ++i) {
            const int y = box.top + 5 + i * 7;
            SelectObject(hdc, fill);
            Ellipse(hdc, box.left + 2, y - 2, box.left + 6, y + 2);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            MoveToEx(hdc, box.left + 10, y, NULL);
            LineTo(hdc, box.right - 2, y);
        }
    } else if (type == InGameMenuIconRefresh) {
        Arc(hdc, box.left + 3, box.top + 4, box.right - 3, box.bottom - 4, box.right - 5, cy, box.left + 6, cy);
        POINT arrow[3] = {
            { box.right - 8, box.top + 7 },
            { box.right - 3, box.top + 11 },
            { box.right - 10, box.top + 13 }
        };
        SelectObject(hdc, fill);
        Polygon(hdc, arrow, 3);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
    } else if (type == InGameMenuIconPuzzle) {
        Rectangle(hdc, cx - 7, cy - 7, cx + 8, cy + 8);
        SelectObject(hdc, fill);
        Rectangle(hdc, cx - 2, cy - 12, cx + 3, cy - 7);
        Rectangle(hdc, cx + 8, cy - 2, cx + 12, cy + 3);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
    } else if (type == InGameMenuIconShield) {
        POINT shield[5] = {
            { cx, box.top + 3 },
            { box.right - 4, box.top + 7 },
            { box.right - 6, cy + 5 },
            { cx, box.bottom - 3 },
            { box.left + 4, cy + 5 }
        };
        Polygon(hdc, shield, 5);
        MoveToEx(hdc, cx, box.top + 7, NULL);
        LineTo(hdc, cx, box.bottom - 7);
    } else {
        Ellipse(hdc, cx - 8, cy - 8, cx + 8, cy + 8);
        Ellipse(hdc, cx - 3, cy - 3, cx + 3, cy + 3);
        for (int i = 0; i < 8; ++i) {
            const double a = 3.14159265358979323846 * i / 4.0;
            const int x1 = cx + static_cast<int>(cos(a) * 6.0);
            const int y1 = cy + static_cast<int>(sin(a) * 6.0);
            const int x2 = cx + static_cast<int>(cos(a) * 11.0);
            const int y2 = cy + static_cast<int>(sin(a) * 11.0);
            MoveToEx(hdc, x1, y1, NULL);
            LineTo(hdc, x2, y2);
        }
    }

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(fill);
    DeleteObject(pen);
}

static void InGameDrawKeycap(HDC hdc, RECT rect, const char* text) {
    InGameDrawRoundFillStroke(hdc, rect, RGB(18, 38, 47), RGB(75, 116, 126), 8);
    InGameDrawSoftText(hdc, text, rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
        RGB(230, 238, 238), false);
}

static bool InGameMenuDraw(HDC hdc, const RECT& rc) {
    if (!InterlockedCompareExchange(&g_ingameMenuEnabled, 1, 1) ||
        !InterlockedCompareExchange(&g_ingameMenuOpen, 1, 1)) {
        return false;
    }

    const DWORD now = GetTickCount();
    if (InterlockedCompareExchange(&g_ingameMenuMode, 0, 0) == 0 &&
        (g_ingameMenuLastScanTick == 0 || now - g_ingameMenuLastScanTick > 1500)) {
        InGameMenuRefreshPlugins();
    }

    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return true;

    int panelW = width - 72;
    if (panelW > 506) panelW = 506;
    if (panelW < 380) panelW = width - 18;
    int panelH = height - 28;
    if (panelH > 371) panelH = 371;
    if (panelH < 315) panelH = height - 16;
    if (panelW < 260 || panelH < 220) return true;

    RECT panel = {
        (width - panelW) / 2,
        (height - panelH) / 2,
        (width + panelW) / 2,
        (height + panelH) / 2
    };

    RECT outerGlow = panel;
    InflateRect(&outerGlow, 6, 6);
    InGameDrawRoundStroke(hdc, outerGlow, RGB(0, 112, 136), 1, 26);
    RECT midGlow = panel;
    InflateRect(&midGlow, 3, 3);
    InGameDrawRoundStroke(hdc, midGlow, RGB(0, 205, 230), 1, 24);
    InGameDrawRoundFillStroke(hdc, panel, RGB(4, 11, 17), RGB(47, 241, 255), 22);
    InGameDrawRoundStroke(hdc, panel, RGB(47, 241, 255), 2, 22);
    RECT innerFrame = panel;
    InflateRect(&innerFrame, -6, -6);
    InGameDrawRoundStroke(hdc, innerFrame, RGB(18, 82, 94), 1, 16);

    SetBkMode(hdc, TRANSPARENT);
    HFONT titleFont = CreateFontA(
        -18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Bahnschrift");
    HFONT rowFont = CreateFontA(
        -15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Bahnschrift");
    HFONT smallFont = CreateFontA(
        -11, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Bahnschrift");
    HFONT pillFont = CreateFontA(
        -12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Bahnschrift");

    HGDIOBJ oldFont = SelectObject(hdc, titleFont);
    RECT title = { panel.left + 28, panel.top + 12, panel.right - 28, panel.top + 37 };
    const bool flagsMode = InterlockedCompareExchange(&g_ingameMenuMode, 0, 0) != 0;
    InGameDrawSoftText(hdc, flagsMode ? "FFX HOOKS - FLAGS" : "JARVIS FFX IN-GAME MENU", title,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
        RGB(70, 222, 207), true);

    SelectObject(hdc, smallFont);
    RECT subtitle = { panel.left + 28, panel.top + 38, panel.right - 28, panel.top + 56 };
    InGameDrawSoftText(hdc, flagsMode ? "INI flags - Enter toggles - restart the game to apply" : "Runtime switchboard / loaded DLLs / safe live controls", subtitle,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
        RGB(173, 192, 199), false);

    const int rowTop = panel.top + 68;
    const int rowStride = 43;
    const int rowHeight = 37;
    const int footerHeight = 28;
    int maxRows = (panel.bottom - rowTop - footerHeight - 2) / rowStride;
    if (maxRows > 6) maxRows = 6;
    if (maxRows < 3) maxRows = 3;

    InGameMenuClampSelection();
    if (g_ingameMenuSelected < g_ingameMenuScroll) {
        g_ingameMenuScroll = g_ingameMenuSelected;
    } else if (g_ingameMenuSelected >= g_ingameMenuScroll + maxRows) {
        g_ingameMenuScroll = g_ingameMenuSelected - maxRows + 1;
    }

    const int count = InGameMenuRowCount();
    SelectObject(hdc, rowFont);
    for (int visible = 0, row = g_ingameMenuScroll; row < count && visible < maxRows; ++row, ++visible) {
        RECT rowRect = {
            panel.left + 30,
            rowTop + visible * rowStride,
            panel.right - 30,
            rowTop + visible * rowStride + rowHeight
        };
        const bool selected = row == g_ingameMenuSelected;
        if (selected) {
            RECT selectedGlow = rowRect;
            InflateRect(&selectedGlow, 2, 2);
            InGameDrawRoundStroke(hdc, selectedGlow, RGB(46, 229, 214), 1, 16);
        }
        InGameDrawRoundFillStroke(
            hdc,
            rowRect,
            selected ? RGB(5, 70, 74) : RGB(5, 29, 38),
            selected ? RGB(63, 245, 226) : RGB(20, 82, 96),
            14);

        if (selected) {
            POINT arrow[3] = {
                { rowRect.left - 15, rowRect.top + rowHeight / 2 },
                { rowRect.left - 7, rowRect.top + rowHeight / 2 - 7 },
                { rowRect.left - 7, rowRect.top + rowHeight / 2 + 7 }
            };
            HBRUSH arrowBrush = CreateSolidBrush(RGB(72, 236, 224));
            HPEN arrowPen = CreatePen(PS_SOLID, 1, RGB(72, 236, 224));
            HGDIOBJ oldArrowBrush = SelectObject(hdc, arrowBrush);
            HGDIOBJ oldArrowPen = SelectObject(hdc, arrowPen);
            Polygon(hdc, arrow, 3);
            SelectObject(hdc, oldArrowBrush);
            SelectObject(hdc, oldArrowPen);
            DeleteObject(arrowBrush);
            DeleteObject(arrowPen);
        }

        char label[128] = {};
        const char* state = "OK";
        COLORREF stateColor = RGB(116, 213, 180);
        const char* file = "";
        FfxHooks::F8Ui::SwitchboardRow mapped = {
            FfxHooks::F8Ui::SwitchboardRowKind::Invalid, -1,
        };
        if (InterlockedCompareExchange(&g_ingameMenuMode, 0, 0)) {
            if (row >= 0 && row < kInGameMenuFlagCount) {
                const InGameMenuFlag& f = kInGameMenuFlags[row];
                lstrcpynA(label, f.label, static_cast<int>(sizeof(label)));
                if (FfxHooks::Config::GetBool(f.key, false)) {
                    state = "ON"; stateColor = RGB(94, 220, 185);
                } else {
                    state = "OFF"; stateColor = RGB(255, 64, 87);
                }
            }
        } else {
            mapped = InGameMenuSwitchboardRows().At(row);
            const char* staticLabel =
                FfxHooks::F8Ui::SwitchboardStaticLabel(mapped.kind);
            if (staticLabel) {
                lstrcpynA(label, staticLabel, static_cast<int>(sizeof(label)));
            }
            switch (mapped.kind) {
                case FfxHooks::F8Ui::SwitchboardRowKind::AuroraActor:
                    if (InterlockedCompareExchange(&g_auroraActorOverlayEnabled, 1, 1) &&
                        InterlockedCompareExchange(&g_auroraOverlayVisible, 1, 1)) {
                        state = "On";
                        stateColor = RGB(94, 220, 185);
                    } else {
                        state = "Off";
                        stateColor = RGB(255, 64, 87);
                    }
                    break;
                case FfxHooks::F8Ui::SwitchboardRowKind::AuroraDetail:
                    if (InterlockedCompareExchange(&g_auroraOverlayDetail, 1, 1)) {
                        state = "On";
                        stateColor = RGB(94, 220, 185);
                    } else {
                        state = "Off";
                        stateColor = RGB(255, 64, 87);
                    }
                    break;
                case FfxHooks::F8Ui::SwitchboardRowKind::Refresh:
                    state = "Scan";
                    stateColor = RGB(246, 196, 92);
                    break;
                case FfxHooks::F8Ui::SwitchboardRowKind::Plugin:
                    if (mapped.pluginIndex >= 0 && mapped.pluginIndex < g_ingameMenuPluginCount) {
                        const InGameMenuPlugin& item = g_ingameMenuPlugins[mapped.pluginIndex];
                        lstrcpynA(label, item.label, static_cast<int>(sizeof(label)));
                        file = item.file;
                        state = InGameMenuPluginState(item, &stateColor);
                    }
                    break;
                default:
                    break;
            }
        }

        RECT iconRect = { rowRect.left + 18, rowRect.top + 10, rowRect.left + 36, rowRect.top + 28 };
        const int iconType = flagsMode
            ? InGameMenuIconForLegacyFlagRow(row)
            : InGameMenuIconForRow(mapped.kind, file);
        InGameDrawIcon(hdc, iconType, iconRect,
            selected ? RGB(161, 255, 236) : RGB(190, 205, 210));

        RECT stateRect = { rowRect.right - 110, rowRect.top + 7, rowRect.right - 16, rowRect.top + 27 };
        SelectObject(hdc, pillFont);
        InGameDrawStatePill(hdc, stateRect, state, stateColor);

        SelectObject(hdc, rowFont);
        RECT labelRect = { rowRect.left + 58, rowRect.top, stateRect.left - 12, rowRect.bottom };
        InGameDrawSoftText(hdc, label, labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
            selected ? RGB(242, 255, 253) : RGB(211, 224, 228), false);
    }

    HPEN footerPen = CreatePen(PS_SOLID, 1, RGB(14, 78, 91));
    HGDIOBJ oldPen = SelectObject(hdc, footerPen);
    const int footerLineY = panel.bottom - 38;
    MoveToEx(hdc, panel.left + 35, footerLineY, NULL);
    LineTo(hdc, panel.right - 35, footerLineY);
    SelectObject(hdc, oldPen);
    DeleteObject(footerPen);

    SelectObject(hdc, smallFont);
    const int hintY = panel.bottom - 27;
    const int groupW = 300;
    int hintX = panel.left + (panelW - groupW) / 2;
    RECT key = { hintX, hintY, hintX + 31, hintY + 17 };
    InGameDrawKeycap(hdc, key, "W/S");
    RECT label = { key.right + 6, hintY, key.right + 78, hintY + 17 };
    InGameDrawSoftText(hdc, "Select", label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
        RGB(197, 210, 214), false);

    hintX = label.right + 13;
    key = { hintX, hintY, hintX + 42, hintY + 17 };
    InGameDrawKeycap(hdc, key, "Enter");
    label = { key.right + 6, hintY, key.right + 49, hintY + 17 };
    InGameDrawSoftText(hdc, "Activate", label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
        RGB(197, 210, 214), false);

    hintX = label.right + 13;
    key = { hintX, hintY, hintX + 31, hintY + 17 };
    InGameDrawKeycap(hdc, key, "Esc");
    label = { key.right + 6, hintY, key.right + 50, hintY + 17 };
    InGameDrawSoftText(hdc, "Close", label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
        RGB(197, 210, 214), false);

    SelectObject(hdc, oldFont);
    DeleteObject(titleFont);
    DeleteObject(rowFont);
    DeleteObject(smallFont);
    DeleteObject(pillFont);
    return true;
}

static uint32_t AuroraReadActors(AuroraActor* actors, uint32_t maxActors) {
    if (!g_base || !actors || maxActors == 0) return 0;
    uint32_t count = 0;
    uint32_t table = 0;
    if (!AuroraReadU32(rva(RVA_ACTIVE_CHR_COUNT), &count) ||
        !AuroraReadU32(rva(RVA_ACTIVE_CHR_TABLE), &table)) {
        return 0;
    }
    if (count == 0 || count > ACTIVE_CHR_MAX_COUNT || !AuroraPtrOk(table)) {
        return 0;
    }

    bool battlePartyModelIds[16] = {};
    bool battleMonsterIds[0x1000] = {};
    const bool hasBattleMask = AuroraReadBattleActiveMasks(battlePartyModelIds, battleMonsterIds);
    uint32_t controlledRaw = 0;
    const uintptr_t controlledInst =
        AuroraReadU32(rva(RVA_CONTROLLED_CHR_INSTANCE), &controlledRaw) && AuroraPtrOk(controlledRaw)
            ? static_cast<uintptr_t>(controlledRaw)
            : 0;

    uint32_t outCount = 0;
    for (uint32_t idx = 0; idx < count && outCount < maxActors; ++idx) {
        const uintptr_t inst = static_cast<uintptr_t>(table) + idx * ACTIVE_CHR_STRIDE;
        uint8_t active = 0;
        uint16_t id = 0;
        uint32_t skel = 0;
        uint32_t flags = 0;
        uint16_t bones = 0;
        if (!AuroraReadU8(inst + 0x002, &active) || active == 0) continue;
        if (!AuroraReadU16(inst + 0x000, &id)) continue;
        AuroraReadU32(inst + 0x194, &flags);
        if (!AuroraReadU32(inst + 0x1C0, &skel) || !AuroraPtrOk(skel)) continue;
        if (!AuroraReadU16(static_cast<uintptr_t>(skel) + 10, &bones) || bones == 0 || bones > 512) continue;
        AuroraActorKind kind = AuroraClassifyActor(id);
        if (controlledInst != 0 && inst == controlledInst && kind != AURORA_ACTOR_MONSTER) {
            kind = AURORA_ACTOR_PARTY;
        }
        if (kind == AURORA_ACTOR_OTHER) continue;
        if (hasBattleMask && kind == AURORA_ACTOR_PARTY) {
            if (id >= 16 || !battlePartyModelIds[id]) continue;
        } else if (hasBattleMask && kind == AURORA_ACTOR_MONSTER) {
            if (id < 0x1000 || id > 0x1FFF || !battleMonsterIds[id - 0x1000]) continue;
        }

        float x = 0, y = 0, z = 0;
        if (!AuroraReadFloat(inst + 0x00C, &x) ||
            !AuroraReadFloat(inst + 0x010, &y) ||
            !AuroraReadFloat(inst + 0x014, &z)) {
            continue;
        }
        const bool zeroPosition = fabsf(x) < 0.01f && fabsf(y) < 0.01f && fabsf(z) < 0.01f;
        if (kind == AURORA_ACTOR_PARTY) {
            const bool reserveClone = (flags & 0x80u) != 0;
            const bool absurdBattleReserve =
                fabsf(x) > 1000.0f || fabsf(y) > 1000.0f || fabsf(z) > 1000.0f;
            if (zeroPosition || reserveClone || absurdBattleReserve) continue;
        }

        AuroraActor& a = actors[outCount++];
        memset(&a, 0, sizeof(a));
        a.id = id;
        a.index = idx;
        a.inst = inst;
        a.x = x;
        a.y = y;
        a.z = z;
        a.kind = kind;
        float localTopX = 0.0f, localTopY = 0.0f, localTopZ = 0.0f;
        if (AuroraTryReadTopBoneLocal(inst, bones, &localTopX, &localTopY, &localTopZ)) {
            float world[16] = {};
            if (AuroraReadMatrix(inst + 0x1D0, world) &&
                AuroraTransformLocalPointRowMajor(world, localTopX, localTopY, localTopZ,
                    &a.topX, &a.topY, &a.topZ)) {
                a.hasTopAnchor = true;
            } else {
                a.topX = x + localTopX;
                a.topY = y + localTopY;
                a.topZ = z + localTopZ;
                a.hasTopAnchor = true;
            }
        }
    }
    return outCount;
}

static uintptr_t AuroraReadControlledChrInstance() {
    uint32_t inst = 0;
    if (!g_base || !AuroraReadU32(rva(RVA_CONTROLLED_CHR_INSTANCE), &inst)) return 0;
    return AuroraPtrOk(inst) ? static_cast<uintptr_t>(inst) : 0;
}

static void AuroraApplyProjectionCoordMode(int mode, float x, float y, float z,
    float* outX, float* outY, float* outZ) {
    const float ox = x;
    const float oy = y;
    const float oz = z;
    switch (mode) {
        case 1:
            x = ox; y = oz; z = oy;
            break;
        case 2:
            x = ox; y = -oz; z = oy;
            break;
        case 3:
            x = -ox; y = oz; z = oy;
            break;
        case 4:
            x = -ox; y = -oz; z = oy;
            break;
        case 5:
            x = ox; y = oy; z = -oz;
            break;
        case 6:
            x = -ox; y = oy; z = oz;
            break;
        default:
            break;
    }
    if (outX) *outX = x;
    if (outY) *outY = y;
    if (outZ) *outZ = z;
}

static bool AuroraProjectColumn(const float m[16], float x, float y, float z, int width, int height,
    float* screenX, float* screenY, float* clipW) {
    AuroraApplyProjectionCoordMode(g_auroraSniffW2SCoordMode, x, y, z, &x, &y, &z);
    const float cx = m[0] * x + m[1] * y + m[2] * z + m[3];
    const float cy = m[4] * x + m[5] * y + m[6] * z + m[7];
    const float cw = m[12] * x + m[13] * y + m[14] * z + m[15];
    if (!AuroraFinite(cx) || !AuroraFinite(cy) || !AuroraFinite(cw) || fabsf(cw) < 0.00001f) {
        return false;
    }
    const float ndcX = cx / cw;
    const float ndcY = cy / cw;
    if (!AuroraFinite(ndcX) || !AuroraFinite(ndcY)) return false;
    if (screenX) *screenX = (ndcX + 1.0f) * 0.5f * static_cast<float>(width);
    if (screenY) *screenY = (1.0f - ndcY) * 0.5f * static_cast<float>(height);
    if (clipW) *clipW = cw;
    return true;
}

static bool AuroraSameClipSign(float a, float b) {
    if (a == 0.0f || b == 0.0f) return true;
    return (a > 0.0f) == (b > 0.0f);
}

static float AuroraClampFloat(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static bool AuroraConsiderLabelCandidate(const float m[16], const AuroraActor& actor,
    int width, int height, float x, float y, float z, float bonus,
    float* bestScore, float* outX, float* outY, float* outW) {
    float sx = 0.0f, sy = 0.0f, cw = 0.0f;
    if (!AuroraProjectColumn(m, x, y, z, width, height, &sx, &sy, &cw)) return false;
    if (!AuroraSameClipSign(actor.clipW, cw)) return false;

    const float dx = fabsf(sx - actor.screenX);
    const float up = actor.screenY - sy;
    if (dx > width * 0.40f) return false;
    if (sy < -height * 0.50f || sy > height * 1.25f) return false;

    const float targetUp =
        actor.kind == AURORA_ACTOR_MONSTER ? 96.0f :
        actor.kind == AURORA_ACTOR_PARTY ? 62.0f :
        46.0f;
    float score = bonus + 240.0f - dx * 0.60f - fabsf(up - targetUp) * 1.15f;
    if (up < 8.0f) score -= 300.0f;
    if (up > height * 0.38f) score -= 260.0f + (up - height * 0.38f) * 0.75f;

    if (score <= *bestScore) return false;
    *bestScore = score;
    if (outX) *outX = sx;
    if (outY) *outY = sy;
    if (outW) *outW = cw;
    return true;
}

static float AuroraBattleLabelFallbackScreenLift(const AuroraActor& actor, int height) {
    if (height <= 0) return 96.0f;

    const float yRatio = AuroraClampFloat(actor.screenY / static_cast<float>(height), 0.0f, 1.0f);
    const float perspectiveScale = 0.74f + yRatio * 0.46f;

    float lift = 0.0f;
    switch (actor.kind) {
        case AURORA_ACTOR_PARTY:
            lift = static_cast<float>(height) * 0.110f * perspectiveScale;
            return AuroraClampFloat(lift, 92.0f, static_cast<float>(height) * 0.18f);
        case AURORA_ACTOR_MONSTER:
            lift = static_cast<float>(height) * 0.105f * perspectiveScale;
            return AuroraClampFloat(lift, 82.0f, static_cast<float>(height) * 0.17f);
        case AURORA_ACTOR_NPC:
            lift = static_cast<float>(height) * 0.125f * perspectiveScale;
            return AuroraClampFloat(lift, 78.0f, static_cast<float>(height) * 0.19f);
        default:
            lift = static_cast<float>(height) * 0.105f * perspectiveScale;
            return AuroraClampFloat(lift, 58.0f, static_cast<float>(height) * 0.16f);
    }
}

static float AuroraBattleLabelMinY(int height) {
    return AuroraClampFloat(static_cast<float>(height) * 0.145f, 96.0f, 240.0f);
}

static float AuroraBattleLabelMaxY(int height) {
    if (height <= 0) return 720.0f;
    const float gameplayMax = static_cast<float>(height) * 0.73f;
    const float hudGuard = static_cast<float>(height) -
        AuroraClampFloat(static_cast<float>(height) * 0.25f, 230.0f, 380.0f);
    const float usefulMax = gameplayMax < hudGuard ? gameplayMax : hudGuard;
    return AuroraClampFloat(usefulMax, 420.0f, static_cast<float>(height) - 260.0f);
}

static bool AuroraBattleRightHudBand(float labelX, float labelY, int width, int height, float* minHudXOut) {
    if (!AuroraFinite(labelX) || !AuroraFinite(labelY) || width <= 0 || height <= 0) return false;
    const float minHudX = static_cast<float>(width) -
        AuroraClampFloat(static_cast<float>(width) * 0.18f, 330.0f, 520.0f);
    if (minHudXOut) *minHudXOut = minHudX;
    return labelX > minHudX &&
        labelY > static_cast<float>(height) * 0.11f &&
        labelY < static_cast<float>(height) * 0.62f;
}

static void AuroraProjectActorLabel(const float m[16], AuroraActor* actor, int width, int height) {
    if (!actor) return;

    const float partyFieldLift = AuroraClampFloat(static_cast<float>(height) * 0.48f, 320.0f, 760.0f);
    const float screenLift =
        actor->kind == AURORA_ACTOR_MONSTER ? AuroraBattleLabelFallbackScreenLift(*actor, height) :
        actor->kind == AURORA_ACTOR_PARTY ? partyFieldLift :
        28.0f;
    actor->labelScreenX = actor->screenX;
    actor->labelScreenY = actor->screenY - screenLift;
    actor->labelClipW = actor->clipW;

    if (!actor->hasTopAnchor) return;

    float bestScore = -FLT_MAX;
    float bestX = actor->labelScreenX;
    float bestY = actor->labelScreenY;
    float bestW = actor->labelClipW;
    bool found = false;
    const float lift =
        actor->kind == AURORA_ACTOR_MONSTER ? 0.55f :
        actor->kind == AURORA_ACTOR_PARTY ? 0.35f :
        0.25f;

    found |= AuroraConsiderLabelCandidate(m, *actor, width, height,
        actor->topX, actor->topY + lift, actor->topZ,
        2000.0f, &bestScore, &bestX, &bestY, &bestW);
    found |= AuroraConsiderLabelCandidate(m, *actor, width, height,
        actor->x, actor->topY + lift, actor->z,
        850.0f, &bestScore, &bestX, &bestY, &bestW);

    if (found) {
        actor->labelScreenX = bestX;
        actor->labelScreenY = bestY;
        actor->labelClipW = bestW;
    }
}

static bool AuroraProjectActors(const float m[16], AuroraActor* actors, uint32_t actorCount, int width, int height,
    uint32_t* visibleOut, float* spreadOut) {
    uint32_t visible = 0;
    float minX = 999999.0f, minY = 999999.0f, maxX = -999999.0f, maxY = -999999.0f;
    float sign = 0.0f;

    for (uint32_t i = 0; i < actorCount; ++i) {
        float sx = 0, sy = 0, cw = 0;
        actors[i].visible = false;
        if (!AuroraProjectColumn(m, actors[i].x, actors[i].y, actors[i].z, width, height, &sx, &sy, &cw)) {
            continue;
        }
        if (sign == 0.0f && cw != 0.0f) sign = cw > 0.0f ? 1.0f : -1.0f;
        if (sign != 0.0f && ((cw > 0.0f ? 1.0f : -1.0f) != sign)) {
            continue;
        }
        actors[i].screenX = sx;
        actors[i].screenY = sy;
        actors[i].clipW = cw;
        AuroraProjectActorLabel(m, &actors[i], width, height);
        const bool inLooseBounds =
            sx >= -width * 0.25f && sx <= width * 1.25f &&
            sy >= -height * 0.25f && sy <= height * 1.25f;
        actors[i].visible = inLooseBounds;
        if (inLooseBounds) {
            ++visible;
            if (sx < minX) minX = sx;
            if (sx > maxX) maxX = sx;
            if (sy < minY) minY = sy;
            if (sy > maxY) maxY = sy;
        }
    }

    const float dx = maxX - minX;
    const float dy = maxY - minY;
    const float spread = (visible >= 2) ? sqrtf(dx * dx + dy * dy) : 0.0f;
    if (visibleOut) *visibleOut = visible;
    if (spreadOut) *spreadOut = spread;
    return visible > 0;
}

static float AuroraScoreProjectedBattleActors(const AuroraActor* actors, uint32_t actorCount,
    int width, int height, uint32_t visible, float spread) {
    if (!actors || actorCount == 0 || width <= 0 || height <= 0) return -FLT_MAX;

    const bool compactBattle = AuroraD3DSniffCompactBattleScene();
    if (!compactBattle) {
        return static_cast<float>(visible) * 10000.0f + spread;
    }

    const float minY = AuroraBattleLabelMinY(height);
    const float maxY = AuroraBattleLabelMaxY(height);
    const float minX = 18.0f;
    const float maxX = static_cast<float>(width) - 18.0f;
    const float targetSpread = static_cast<float>(width) * 0.70f;
    const float partyTooHighY = static_cast<float>(height) * 0.34f;
    const float partyHudY = maxY - static_cast<float>(height) * 0.025f;
    const float monsterSkyY = AuroraClampFloat(
        static_cast<float>(height) * 0.30f,
        minY + static_cast<float>(height) * 0.10f,
        maxY - static_cast<float>(height) * 0.18f);
    const float monsterTooLowY = maxY - static_cast<float>(height) * 0.02f;

    int saneLabels = 0;
    int partyLabels = 0;
    int monsterLabels = 0;
    float partyXSum = 0.0f;
    float partyYSum = 0.0f;
    float monsterXSum = 0.0f;
    float monsterYSum = 0.0f;
    int finiteLabels = 0;
    float minLabelY = 999999.0f;
    float maxLabelY = -999999.0f;
    float penalty = fabsf(spread - targetSpread) * 0.20f;
    for (uint32_t i = 0; i < actorCount; ++i) {
        const AuroraActor& a = actors[i];
        if (!a.visible) {
            penalty += 1600.0f;
            continue;
        }

        float labelX = AuroraFinite(a.labelScreenX) ? a.labelScreenX : a.screenX;
        float labelY = AuroraFinite(a.labelScreenY) ? a.labelScreenY :
            a.screenY - AuroraBattleLabelFallbackScreenLift(a, height);

        bool labelShapeSane = false;
        if (AuroraFinite(labelX) && AuroraFinite(labelY) && AuroraFinite(a.screenY)) {
            const float up = a.screenY - labelY;
            const float expectedUp = AuroraBattleLabelFallbackScreenLift(a, height);
            const float minUp = AuroraClampFloat(expectedUp * 0.42f, 58.0f, static_cast<float>(height) * 0.13f);
            const float maxUp = AuroraClampFloat(expectedUp * 1.72f, 160.0f, static_cast<float>(height) * 0.42f);
            labelShapeSane =
                a.screenY >= minY &&
                a.screenY <= static_cast<float>(height) + 80.0f &&
                up >= minUp &&
                up <= maxUp;
            if (!labelShapeSane) {
                penalty += 18000.0f;
                if (up < minUp) penalty += (minUp - up) * 60.0f;
                if (up > maxUp) penalty += (up - maxUp) * 40.0f;
            }
        }

        const bool pointUseful =
            AuroraFinite(labelX) && AuroraFinite(labelY) &&
            labelX >= minX && labelX <= maxX &&
            labelY >= minY && labelY <= maxY &&
            labelShapeSane;
        if (pointUseful) {
            ++saneLabels;
        } else {
            penalty += 6000.0f;
            if (AuroraFinite(labelY)) {
                if (labelY < minY) penalty += (minY - labelY) * 14.0f;
                if (labelY > maxY) penalty += (labelY - maxY) * 14.0f;
            }
        }

        if (AuroraFinite(labelX) && AuroraFinite(labelY)) {
            ++finiteLabels;
            if (labelY < minLabelY) minLabelY = labelY;
            if (labelY > maxLabelY) maxLabelY = labelY;

            float rightHudMinX = 0.0f;
            if (AuroraBattleRightHudBand(labelX, labelY, width, height, &rightHudMinX)) {
                penalty += 4800.0f + (labelX - rightHudMinX) * 12.0f;
            }

            if (a.kind == AURORA_ACTOR_PARTY) {
                ++partyLabels;
                partyXSum += labelX;
                partyYSum += labelY;
                if (labelY < partyTooHighY) {
                    penalty += 900.0f + (partyTooHighY - labelY) * 7.0f;
                }
                if (labelY > partyHudY) {
                    penalty += 4200.0f + (labelY - partyHudY) * 16.0f;
                }
            } else if (a.kind == AURORA_ACTOR_MONSTER) {
                ++monsterLabels;
                monsterXSum += labelX;
                monsterYSum += labelY;
                if (labelY < monsterSkyY) {
                    penalty += 26000.0f + (monsterSkyY - labelY) * 32.0f;
                }
                if (labelY > monsterTooLowY) {
                    penalty += 2200.0f + (labelY - monsterTooLowY) * 8.0f;
                }
            }
        }

        if (AuroraFinite(a.screenY)) {
            if (a.screenY < minY * 0.50f) penalty += (minY * 0.50f - a.screenY) * 2.0f;
            if (a.screenY > static_cast<float>(height) + 80.0f) {
                penalty += (a.screenY - static_cast<float>(height) - 80.0f) * 2.0f;
            }
        }
    }

    if (finiteLabels >= 5) {
        const float labelYRange = maxLabelY - minLabelY;
        const float requiredYRange = AuroraClampFloat(static_cast<float>(height) * 0.085f, 82.0f, 150.0f);
        if (labelYRange < requiredYRange) {
            penalty += 18000.0f + (requiredYRange - labelYRange) * 110.0f;
        }
    }

    if (visible >= 4) {
        const int requiredSane = static_cast<int>(visible >= actorCount && visible > 1 ? visible - 1 : visible);
        if (saneLabels < requiredSane) {
            penalty += static_cast<float>(requiredSane - saneLabels) * 18000.0f;
        }
    }

    if (partyLabels >= 2 && monsterLabels >= 1) {
        const float partyAvgX = partyXSum / static_cast<float>(partyLabels);
        const float partyAvgY = partyYSum / static_cast<float>(partyLabels);
        const float monsterAvgX = monsterXSum / static_cast<float>(monsterLabels);
        const float monsterAvgY = monsterYSum / static_cast<float>(monsterLabels);
        if (partyAvgX > monsterAvgX - static_cast<float>(width) * 0.03f) {
            penalty += 12000.0f + (partyAvgX - monsterAvgX) * 8.0f;
        }
        if (partyAvgY > partyHudY) {
            penalty += 6000.0f + (partyAvgY - partyHudY) * 12.0f;
        }
        if (monsterAvgY < monsterSkyY) {
            penalty += 18000.0f + (monsterSkyY - monsterAvgY) * 28.0f;
        }
        if (partyAvgY + static_cast<float>(height) * 0.08f < monsterAvgY) {
            penalty += 5500.0f + (monsterAvgY - partyAvgY) * 4.0f;
        }
        const float roleAvgYDelta = fabsf(partyAvgY - monsterAvgY);
        const float minRoleAvgYDelta = AuroraClampFloat(static_cast<float>(height) * 0.032f, 34.0f, 72.0f);
        if (roleAvgYDelta < minRoleAvgYDelta) {
            penalty += 7200.0f + (minRoleAvgYDelta - roleAvgYDelta) * 85.0f;
        }
    }

    return static_cast<float>(visible) * 10000.0f +
        static_cast<float>(saneLabels) * 7000.0f -
        penalty;
}

static void AuroraUpdateSniffActorSnapshot(const AuroraActor* actors, uint32_t actorCount, int width, int height) {
    if (!actors || actorCount == 0 || width <= 0 || height <= 0) {
        g_auroraSniffActorCount = 0;
        g_auroraSniffViewportWidth = 0;
        g_auroraSniffViewportHeight = 0;
        return;
    }
    if (actorCount > 32) actorCount = 32;
    memcpy(g_auroraSniffActors, actors, sizeof(AuroraActor) * actorCount);
    g_auroraSniffActorCount = actorCount;
    g_auroraSniffViewportWidth = width;
    g_auroraSniffViewportHeight = height;
}

static bool AuroraActorListHasKind(const AuroraActor* actors, uint32_t actorCount, AuroraActorKind kind) {
    if (!actors) return false;
    for (uint32_t i = 0; i < actorCount; ++i) {
        if (actors[i].kind == kind) return true;
    }
    return false;
}

static bool AuroraIsCompactBattleActorList(const AuroraActor* actors, uint32_t actorCount) {
    return actors && actorCount >= 4 && actorCount <= 12 &&
        AuroraActorListHasKind(actors, actorCount, AURORA_ACTOR_PARTY) &&
        AuroraActorListHasKind(actors, actorCount, AURORA_ACTOR_MONSTER);
}

static float AuroraCompactBattleMinSpread(int width) {
    if (width <= 0) return 700.0f;
    return AuroraClampFloat(static_cast<float>(width) * 0.34f, 700.0f, 1150.0f);
}

static float AuroraCompactBattleMinScore(uint32_t actorCount) {
    if (actorCount == 0) return 0.0f;
    return static_cast<float>(actorCount) * 17000.0f - 12000.0f;
}

static float AuroraCompactBattleMinSniffScore(uint32_t actorCount) {
    if (actorCount == 0) return 0.0f;
    return static_cast<float>(actorCount) * 17000.0f - 2200.0f;
}

static bool AuroraTryGetSniffW2SMatrix(const AuroraActor* sourceActors, uint32_t actorCount, int width, int height,
    float outMatrix[16], uintptr_t* usedAddress, uint32_t* visibleOut) {
    if (!sourceActors || actorCount == 0 || !outMatrix || g_auroraSniffW2SMatrixTick == 0) return false;
    const DWORD now = GetTickCount();
    const DWORD maxAgeMs = static_cast<DWORD>(g_auroraSniffW2SMaxAgeMs < 100 ? 100 : g_auroraSniffW2SMaxAgeMs);
    if (now - g_auroraSniffW2SMatrixTick > maxAgeMs) return false;

    float matrix[16] = {};
    memcpy(matrix, g_auroraSniffW2SMatrix, sizeof(matrix));

    AuroraActor actors[32] = {};
    if (actorCount > 32) actorCount = 32;
    memcpy(actors, sourceActors, sizeof(AuroraActor) * actorCount);
    uint32_t visible = 0;
    float spread = 0.0f;
    if (!AuroraProjectActors(matrix, actors, actorCount, width, height, &visible, &spread)) return false;
    const uint32_t minVisible =
        actorCount >= g_auroraW2SScanMinRoots ? g_auroraW2SScanMinRoots : actorCount;
    if (visible < minVisible || spread < 80.0f) return false;
    if (AuroraIsCompactBattleActorList(sourceActors, actorCount) &&
        spread < AuroraCompactBattleMinSpread(width)) {
        return false;
    }
    if (AuroraIsCompactBattleActorList(sourceActors, actorCount)) {
        const float score = AuroraScoreProjectedBattleActors(actors, actorCount, width, height, visible, spread);
        const float minScore = AuroraCompactBattleMinSniffScore(actorCount);
        if (score < minScore) {
            static DWORD s_lastSniffRejectLogTick = 0;
            const DWORD rejectNow = GetTickCount();
            if (rejectNow - s_lastSniffRejectLogTick > 1000) {
                s_lastSniffRejectLogTick = rejectNow;
                Log("[ffx-hooks] AuroraOverlay W2S compact sniff rejected visible=%u spread=%.1f score=%.1f minSniffScore=%.1f\n",
                    static_cast<unsigned>(visible),
                    spread,
                    score,
                    minScore);
            }
            return false;
        }
    }

    memcpy(outMatrix, matrix, sizeof(matrix));
    if (usedAddress) *usedAddress = 0;
    if (visibleOut) *visibleOut = visible;

    if (now - g_auroraLastSniffW2SUseLogTick > 2000) {
        g_auroraLastSniffW2SUseLogTick = now;
        const float logScore = AuroraIsCompactBattleActorList(sourceActors, actorCount)
            ? AuroraScoreProjectedBattleActors(actors, actorCount, width, height, visible, spread)
            : -FLT_MAX;
        Log("[ffx-hooks] AuroraOverlay W2S using D3D sniff matrix caller=0x%08X rva=0x%08X offset=0x%X layout=%c coord=%d visible=%u spread=%.1f score=%.1f ageMs=%u\n",
            static_cast<unsigned>(g_auroraSniffW2SCaller),
            static_cast<unsigned>(AuroraFfxCodeRva(g_auroraSniffW2SCaller)),
            static_cast<unsigned>(g_auroraSniffW2SOffset),
            g_auroraSniffW2SLayout ? g_auroraSniffW2SLayout : '?',
            g_auroraSniffW2SCoordMode,
            static_cast<unsigned>(visible),
            spread,
            logScore,
            static_cast<unsigned>(now - g_auroraSniffW2SMatrixTick));
    }
    return true;
}

static bool AuroraLooksLikeW2SBlock(uintptr_t address) {
    float f[64] = {};
    if (!AuroraReadBytes(address, f, sizeof(f))) return false;
    for (int i = 0; i < 64; ++i) {
        if (!AuroraFinite(f[i]) || fabsf(f[i]) > 100000.0f) return false;
    }

    for (int i = 16; i < 32; ++i) {
        if (fabsf(f[i]) > 0.001f) return false;
    }

    const int diagBase = 32;
    const int diagIdx[4] = { 0, 5, 10, 15 };
    const float diagExpected[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
    for (int i = 0; i < 4; ++i) {
        if (fabsf(f[diagBase + diagIdx[i]] - diagExpected[i]) > 0.02f) return false;
    }
    for (int i = 0; i < 16; ++i) {
        bool isDiag = (i == 0 || i == 5 || i == 10 || i == 15);
        if (!isDiag && fabsf(f[diagBase + i]) > 0.02f) return false;
    }

    return true;
}

static bool AuroraFindW2SByScan(const AuroraActor* sourceActors, uint32_t actorCount, int width, int height,
    uintptr_t* outAddress, float outMatrix[16], uint32_t* visibleOut) {
    if (!sourceActors || actorCount == 0) return false;
    AuroraActor actors[32] = {};
    if (actorCount > 32) actorCount = 32;
    const bool compactBattle = AuroraIsCompactBattleActorList(sourceActors, actorCount);

    uintptr_t bestAddress = 0;
    uint32_t bestVisible = 0;
    float bestSpread = 0.0f;
    float bestScore = -FLT_MAX;
    float bestMatrix[16] = {};

    SYSTEM_INFO si = {};
    GetSystemInfo(&si);
    const uintptr_t minAddr = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    const uintptr_t maxAddr = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);
    const uintptr_t resetAddr =
        (g_auroraW2SScanStartAddress >= minAddr && g_auroraW2SScanStartAddress < maxAddr)
            ? g_auroraW2SScanStartAddress
            : minAddr;
    if (g_auroraW2SScanCursor < minAddr || g_auroraW2SScanCursor >= maxAddr) {
        g_auroraW2SScanCursor = resetAddr;
    }

    uintptr_t addr = g_auroraW2SScanCursor;
    const double started = AuroraNowMsPrecise();
    DWORD budgetMs = static_cast<DWORD>(g_auroraW2SScanBudgetMs <= 0 ? 1 : g_auroraW2SScanBudgetMs);
    if (compactBattle && budgetMs < 8) budgetMs = 8;
    uint32_t probes = 0;
    uintptr_t lastRegionBase = 0;
    uintptr_t lastRegionEnd = 0;
    while (addr < maxAddr) {
        MEMORY_BASIC_INFORMATION mbi = {};
        if (!VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi))) {
            addr += 0x10000;
            g_auroraW2SScanCursor = addr;
            if (AuroraNowMsPrecise() - started >= static_cast<double>(budgetMs)) break;
            continue;
        }

        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const uintptr_t end = base + mbi.RegionSize;
        lastRegionBase = base;
        lastRegionEnd = end;
        const DWORD protect = mbi.Protect & 0xFFu;
        const bool readable =
            protect == PAGE_READONLY || protect == PAGE_READWRITE ||
            protect == PAGE_WRITECOPY || protect == PAGE_EXECUTE_READ ||
            protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
            !(mbi.Protect & PAGE_GUARD) && readable && mbi.RegionSize >= 0x100) {
            uintptr_t p = addr > base ? addr : base;
            p = (p + 0x0Fu) & ~static_cast<uintptr_t>(0x0Fu);
            for (; p + 0x100 <= end; p += 0x10) {
                if ((++probes & 0xFFu) == 0 &&
                    AuroraNowMsPrecise() - started >= static_cast<double>(budgetMs)) {
                    g_auroraW2SScanCursor = p;
                    break;
                }
                if (!AuroraLooksLikeW2SBlock(p)) continue;
                float m[16] = {};
                if (!AuroraReadMatrix(p, m)) continue;
                memcpy(actors, sourceActors, sizeof(AuroraActor) * actorCount);
                uint32_t visible = 0;
                float spread = 0.0f;
                if (!AuroraProjectActors(m, actors, actorCount, width, height, &visible, &spread)) continue;
                const uint32_t minVisible =
                    actorCount >= g_auroraW2SScanMinRoots ? g_auroraW2SScanMinRoots : actorCount;
                if (visible < minVisible || spread < 80.0f) continue;
                if (compactBattle && spread < AuroraCompactBattleMinSpread(width)) continue;
                const float score = compactBattle
                    ? AuroraScoreProjectedBattleActors(actors, actorCount, width, height, visible, spread)
                    : (static_cast<float>(visible) * 10000.0f + spread);
                if (compactBattle && score < AuroraCompactBattleMinScore(actorCount)) continue;
                if (score > bestScore ||
                    (fabsf(score - bestScore) < 1.0f && spread > bestSpread)) {
                    bestAddress = p;
                    bestVisible = visible;
                    bestSpread = spread;
                    bestScore = score;
                    memcpy(bestMatrix, m, sizeof(bestMatrix));
                }
            }
            if (bestAddress) break;
            if (p + 0x100 <= end) break;
        }
        if (end <= addr) break;
        addr = end;
        g_auroraW2SScanCursor = addr;
        if (AuroraNowMsPrecise() - started >= static_cast<double>(budgetMs)) break;
    }

    g_auroraW2SScanLastRegionBase = lastRegionBase;
    g_auroraW2SScanLastRegionEnd = lastRegionEnd;
    g_auroraW2SScanLastProbes = probes;
    g_auroraW2SScanLastElapsedMs =
        static_cast<DWORD>((AuroraNowMsPrecise() - started) + 0.5);

    if (addr >= maxAddr || g_auroraW2SScanCursor >= maxAddr) {
        g_auroraW2SScanCursor = minAddr;
        ++g_auroraW2SScanPassCount;
        Log("[ffx-hooks] AuroraOverlay W2S incremental scan pass complete pass=%u probes=%u elapsed=%ums; restarting\n",
            static_cast<unsigned>(g_auroraW2SScanPassCount),
            static_cast<unsigned>(g_auroraW2SScanLastProbes),
            static_cast<unsigned>(g_auroraW2SScanLastElapsedMs));
    }

    if (!bestAddress) return false;
    if (outAddress) *outAddress = bestAddress;
    if (outMatrix) memcpy(outMatrix, bestMatrix, sizeof(bestMatrix));
    if (visibleOut) *visibleOut = bestVisible;
    return true;
}

static bool AuroraGetW2SMatrix(const AuroraActor* actors, uint32_t actorCount, int width, int height,
    float outMatrix[16], uintptr_t* usedAddress, uint32_t* visibleOut) {
    const bool compactBattle = AuroraIsCompactBattleActorList(actors, actorCount);
    if (!compactBattle && actorCount > 0 && actorCount <= 16 &&
        AuroraTryGetSniffW2SMatrix(actors, actorCount, width, height, outMatrix, usedAddress, visibleOut)) {
        return true;
    }

    if (g_auroraW2SAddress) {
        if (AuroraReadMatrix(g_auroraW2SAddress, outMatrix)) {
            if (compactBattle && !g_auroraW2SManual) {
                AuroraActor projected[32] = {};
                uint32_t projectedCount = actorCount > 32 ? 32 : actorCount;
                memcpy(projected, actors, sizeof(AuroraActor) * projectedCount);
                uint32_t visible = 0;
                float spread = 0.0f;
                const uint32_t minVisible =
                    projectedCount >= g_auroraW2SScanMinRoots ? g_auroraW2SScanMinRoots : projectedCount;
                const bool usable =
                    AuroraProjectActors(outMatrix, projected, projectedCount, width, height, &visible, &spread) &&
                    visible >= minVisible &&
                    spread >= AuroraCompactBattleMinSpread(width) &&
                    AuroraScoreProjectedBattleActors(projected, projectedCount, width, height, visible, spread) >=
                        AuroraCompactBattleMinScore(projectedCount);
                if (!usable) {
                    static DWORD s_lastCachedRejectLogTick = 0;
                    const DWORD now = GetTickCount();
                    if (now - s_lastCachedRejectLogTick > 1000) {
                        s_lastCachedRejectLogTick = now;
                        const float score = AuroraScoreProjectedBattleActors(projected, projectedCount, width, height, visible, spread);
                        Log("[ffx-hooks] AuroraOverlay W2S cached battle candidate rejected addr=0x%08X visible=%u spread=%.1f minSpread=%.1f score=%.1f minScore=%.1f\n",
                            static_cast<unsigned>(g_auroraW2SAddress),
                            static_cast<unsigned>(visible),
                            spread,
                            AuroraCompactBattleMinSpread(width),
                            score,
                            AuroraCompactBattleMinScore(projectedCount));
                    }
                    g_auroraW2SAddress = 0;
                } else {
                    if (usedAddress) *usedAddress = g_auroraW2SAddress;
                    if (visibleOut) *visibleOut = visible;
                    return true;
                }
            } else {
                if (usedAddress) *usedAddress = g_auroraW2SAddress;
                if (visibleOut) *visibleOut = 0;
                return true;
            }
        }
        if (!g_auroraW2SAddress && !g_auroraW2SManual) {
            // rejected cached auto-scan address; continue into live scan below
        } else {
            if (g_auroraW2SManual) return false;
            g_auroraW2SAddress = 0;
        }
    }

    if (g_auroraW2SManual) return false;

    if (!g_auroraW2SScan) {
        if (compactBattle) return false;
        return AuroraTryGetSniffW2SMatrix(actors, actorCount, width, height, outMatrix, usedAddress, visibleOut);
    }
    if (actorCount < g_auroraW2SScanMinRoots) {
        if (compactBattle) return false;
        return AuroraTryGetSniffW2SMatrix(actors, actorCount, width, height, outMatrix, usedAddress, visibleOut);
    }
    const DWORD now = GetTickCount();
    const DWORD cooldownMs = static_cast<DWORD>(g_auroraW2SScanCooldownMs < 0 ? 0 : g_auroraW2SScanCooldownMs);
    if (now - g_auroraLastScanTick >= cooldownMs) {
        g_auroraLastScanTick = now;

        uintptr_t found = 0;
        uint32_t visible = 0;
        if (AuroraFindW2SByScan(actors, actorCount, width, height, &found, outMatrix, &visible)) {
            g_auroraW2SAddress = found;
            if (usedAddress) *usedAddress = found;
            if (visibleOut) *visibleOut = visible;
            Log("[ffx-hooks] AuroraOverlay W2S scan candidate=0x%08X visible=%u cursor=0x%08X probes=%u elapsed=%ums pass=%u\n",
                static_cast<unsigned>(found),
                static_cast<unsigned>(visible),
                static_cast<unsigned>(g_auroraW2SScanCursor),
                static_cast<unsigned>(g_auroraW2SScanLastProbes),
                static_cast<unsigned>(g_auroraW2SScanLastElapsedMs),
                static_cast<unsigned>(g_auroraW2SScanPassCount));
            return true;
        }
    }
    if (compactBattle) {
        return AuroraTryGetSniffW2SMatrix(actors, actorCount, width, height, outMatrix, usedAddress, visibleOut);
    }
    return AuroraTryGetSniffW2SMatrix(actors, actorCount, width, height, outMatrix, usedAddress, visibleOut);
}

static void AuroraDrawCross(HDC hdc, int x, int y, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, x - 7, y, nullptr);
    LineTo(hdc, x + 8, y);
    MoveToEx(hdc, x, y - 7, nullptr);
    LineTo(hdc, x, y + 8);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void AuroraDrawStatus(HDC hdc, const RECT& rc, COLORREF color, const char* text) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    HFONT font = CreateFontA(
        15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HGDIOBJ oldFont = SelectObject(hdc, font);

    RECT tr = { rc.left + 14, rc.top + 14, rc.right - 14, rc.top + 80 };
    DrawTextA(hdc, text, -1, &tr, DT_LEFT | DT_TOP | DT_NOCLIP);

    SelectObject(hdc, oldFont);
    DeleteObject(font);
}

static bool AuroraRectIntersects(const RECT& a, const RECT& b) {
    return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}

static void AuroraClampRectToViewport(RECT* r, int width, int height) {
    if (!r) return;
    const int w = r->right - r->left;
    const int h = r->bottom - r->top;
    if (r->left < 2) {
        r->left = 2;
        r->right = r->left + w;
    }
    if (r->right > width - 2) {
        r->right = width - 2;
        r->left = r->right - w;
    }
    if (r->top < 2) {
        r->top = 2;
        r->bottom = r->top + h;
    }
    if (r->bottom > height - 2) {
        r->bottom = height - 2;
        r->top = r->bottom - h;
    }
}

static void AuroraResolveLabelRect(RECT* r, const RECT* used, int usedCount, int width, int height) {
    if (!r) return;
    AuroraClampRectToViewport(r, width, height);

    for (int attempt = 0; attempt < 10; ++attempt) {
        bool hit = false;
        for (int i = 0; i < usedCount; ++i) {
            if (AuroraRectIntersects(*r, used[i])) {
                hit = true;
                break;
            }
        }
        if (!hit) return;

        OffsetRect(r, 0, (attempt % 2 == 0) ? -18 : 26);
        AuroraClampRectToViewport(r, width, height);
    }
}

static bool AuroraBattleLabelPointSane(const AuroraActor& actor, float labelX, float labelY,
    int width, int height) {
    if (!AuroraFinite(labelX) || !AuroraFinite(labelY) || width <= 0 || height <= 0) return false;
    if (labelX < 6.0f || labelX > static_cast<float>(width) - 6.0f) return false;
    const float minY = AuroraBattleLabelMinY(height);
    const float maxY = AuroraBattleLabelMaxY(height);
    if (labelY < minY || labelY > maxY) return false;
    const float up = actor.screenY - labelY;
    const float expectedUp = AuroraBattleLabelFallbackScreenLift(actor, height);
    const float minUp = AuroraClampFloat(expectedUp * 0.42f, 58.0f, static_cast<float>(height) * 0.13f);
    const float maxUp = AuroraClampFloat(expectedUp * 1.72f, 160.0f, static_cast<float>(height) * 0.42f);
    if (up < minUp || up > maxUp) return false;
    if (actor.screenY < minY || actor.screenY > static_cast<float>(height) + 80.0f) return false;
    return true;
}

static void AuroraDrawTextShadow(HDC hdc, const char* text, RECT rect, UINT format, COLORREF color) {
    RECT shadow = rect;
    OffsetRect(&shadow, 1, 1);
    SetTextColor(hdc, RGB(0, 0, 0));
    DrawTextA(hdc, text, -1, &shadow, format);
    SetTextColor(hdc, color);
    DrawTextA(hdc, text, -1, &rect, format);
}

static void AuroraLogWait(const char* reason, uint32_t actorCount, uintptr_t w2sAddress,
    uint32_t visible, int width, int height) {
    const DWORD now = GetTickCount();
    if (now - g_auroraLastWaitLogTick <= 2000) return;
    g_auroraLastWaitLogTick = now;
    Log("[ffx-hooks] AuroraOverlay wait reason=%s roots=%u w2s=0x%08X visible=%u viewport=%dx%d scanCursor=0x%08X scanRegion=0x%08X-0x%08X scanProbes=%u scanElapsed=%ums scanPass=%u\n",
        reason,
        static_cast<unsigned>(actorCount),
        static_cast<unsigned>(w2sAddress),
        static_cast<unsigned>(visible),
        width, height,
        static_cast<unsigned>(g_auroraW2SScanCursor),
        static_cast<unsigned>(g_auroraW2SScanLastRegionBase),
        static_cast<unsigned>(g_auroraW2SScanLastRegionEnd),
        static_cast<unsigned>(g_auroraW2SScanLastProbes),
        static_cast<unsigned>(g_auroraW2SScanLastElapsedMs),
        static_cast<unsigned>(g_auroraW2SScanPassCount));
}

static void AuroraDrawOverlayGdiContent(HDC hdc, const RECT& rc, bool fillColorKey) {
    if (fillColorKey) {
        HBRUSH clearBrush = CreateSolidBrush(RGB(1, 1, 1));
        FillRect(hdc, &rc, clearBrush);
        DeleteObject(clearBrush);
    }

    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;

    AuroraActor actors[32] = {};
    const uint32_t actorCount = AuroraReadActors(actors, 32);
    g_auroraLastActorCount = actorCount;
    AuroraUpdateSniffActorSnapshot(actors, actorCount, width, height);
    if (actorCount == 0) {
        AuroraResetLabelStats();
        AuroraDrawStatus(hdc, rc, RGB(180, 255, 150),
            "AURORA W2S overlay: waiting for ActiveChrInstance roots\n"
            "Enter a field/battle scene; F9 toggles, F10 detail");
        AuroraLogWait("no-roots", actorCount, 0, 0, width, height);
        return;
    }

    if (!g_auroraW2SManual && actorCount < g_auroraW2SScanMinRoots) {
        AuroraResetLabelStats();
        char status[192] = {};
        _snprintf_s(status, sizeof(status), _TRUNCATE,
            "AURORA W2S overlay: roots=%u, waiting for stable scene\n"
            "W2S scan starts at %u roots; F9 toggles, F10 detail",
            static_cast<unsigned>(actorCount),
            static_cast<unsigned>(g_auroraW2SScanMinRoots));
        AuroraDrawStatus(hdc, rc, RGB(180, 255, 150), status);
        AuroraLogWait("not-enough-roots", actorCount, 0, 0, width, height);
        return;
    }

    float matrix[16] = {};
    uintptr_t w2sAddress = 0;
    uint32_t scanVisible = 0;
    if (!AuroraGetW2SMatrix(actors, actorCount, width, height, matrix, &w2sAddress, &scanVisible)) {
        AuroraResetLabelStats();
        char status[256] = {};
        _snprintf_s(status, sizeof(status), _TRUNCATE,
            "AURORA W2S overlay: roots=%u, waiting for W2S matrix\n"
            "scan=%d cursor=0x%08X probes=%u pass=%u; F9 toggles, F10 detail",
            static_cast<unsigned>(actorCount),
            g_auroraW2SScan ? 1 : 0,
            static_cast<unsigned>(g_auroraW2SScanCursor),
            static_cast<unsigned>(g_auroraW2SScanLastProbes),
            static_cast<unsigned>(g_auroraW2SScanPassCount));
        AuroraDrawStatus(hdc, rc, RGB(255, 220, 110), status);
        AuroraLogWait("no-w2s", actorCount, g_auroraW2SAddress, scanVisible, width, height);
        return;
    }

    uint32_t visible = 0;
    float spread = 0.0f;
    AuroraProjectActors(matrix, actors, actorCount, width, height, &visible, &spread);
    const uint32_t minVisible =
        actorCount >= g_auroraW2SScanMinRoots ? g_auroraW2SScanMinRoots : actorCount;
    if (!g_auroraW2SManual && visible < minVisible) {
        char status[192] = {};
        _snprintf_s(status, sizeof(status), _TRUNCATE,
            "AURORA W2S overlay: rejected matrix 0x%08X\n"
            "roots=%u visible=%u; rescanning",
            static_cast<unsigned>(w2sAddress),
            static_cast<unsigned>(actorCount),
            static_cast<unsigned>(visible));
        AuroraDrawStatus(hdc, rc, RGB(255, 170, 110), status);
        AuroraLogWait("w2s-rejected", actorCount, w2sAddress, visible, width, height);
        AuroraResetLabelStats();
        g_auroraW2SAddress = 0;
        return;
    }
    SetBkMode(hdc, TRANSPARENT);
    const bool detail = InterlockedCompareExchange(&g_auroraOverlayDetail, 0, 0) != 0;
    HFONT font = CreateFontA(
        detail ? 13 : 15,
        0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HGDIOBJ oldFont = SelectObject(hdc, font);

    int kindIndex[5] = {};
    uint32_t drawnByKind[5] = {};
    uint32_t drawn = 0;
    RECT usedLabelRects[64] = {};
    int usedLabelRectCount = 0;
    uint32_t defaultVisibleMonsterCount = 0;
    int fieldPartyActorIndex = -1;
    float bestFieldPartyScore = -FLT_MAX;
    const uintptr_t controlledInst = AuroraReadControlledChrInstance();
    for (uint32_t i = 0; i < actorCount; ++i) {
        if (!actors[i].visible) continue;
        if (actors[i].kind == AURORA_ACTOR_MONSTER) {
            ++defaultVisibleMonsterCount;
        } else if (actors[i].kind == AURORA_ACTOR_PARTY) {
            float score = 0.0f;
            if (controlledInst != 0 && actors[i].inst == controlledInst) score += 250.0f;
            score -= fabsf(actors[i].screenX - width * 0.5f) * 0.4f;
            score -= fabsf(actors[i].screenY - height * 0.55f) * 0.2f;
            if (score > bestFieldPartyScore) {
                bestFieldPartyScore = score;
                fieldPartyActorIndex = static_cast<int>(i);
            }
        }
    }
    for (uint32_t i = 0; i < actorCount; ++i) {
        if (!actors[i].visible) continue;
        if (!detail && !AuroraKindVisibleByDefault(actors[i].kind)) continue;
        if (!detail && defaultVisibleMonsterCount == 0 && actors[i].kind == AURORA_ACTOR_PARTY &&
            static_cast<int>(i) != fieldPartyActorIndex) {
            continue;
        }

        const bool fieldPartyMode =
            !detail && defaultVisibleMonsterCount == 0 && actors[i].kind == AURORA_ACTOR_PARTY;
        const bool battleActorMode = !detail && defaultVisibleMonsterCount > 0;
        const float battleLift = AuroraBattleLabelFallbackScreenLift(actors[i], height);
        float labelX = actors[i].labelScreenX;
        float labelY = actors[i].labelScreenY;
        if (!AuroraFinite(labelX) || !AuroraFinite(labelY)) {
            labelX = actors[i].screenX;
            labelY = actors[i].screenY - battleLift;
        }
        if (fieldPartyMode) {
            labelX = actors[i].screenX;

            const float fieldRootLift = AuroraClampFloat(static_cast<float>(height) * 0.48f, 320.0f, 760.0f);
            const float minFieldUp = AuroraClampFloat(static_cast<float>(height) * 0.22f, 160.0f, 360.0f);
            const float maxFieldUp = AuroraClampFloat(static_cast<float>(height) * 0.72f, 520.0f, 980.0f);
            float fieldLabelY = actors[i].labelScreenY;
            const float fieldUp = actors[i].screenY - fieldLabelY;
            if (!AuroraFinite(fieldLabelY) || fieldLabelY < -height * 0.25f ||
                fieldLabelY > height * 1.25f || fieldUp < minFieldUp || fieldUp > maxFieldUp) {
                fieldLabelY = actors[i].screenY - fieldRootLift;
            }

            const float minFieldY = AuroraClampFloat(static_cast<float>(height) * 0.55f, 360.0f, height - 140.0f);
            const float maxFieldY = AuroraClampFloat(static_cast<float>(height) * 0.66f, minFieldY + 36.0f, height - 84.0f);
            labelX = AuroraClampFloat(labelX, 24.0f, static_cast<float>(width) - 24.0f);
            labelY = AuroraClampFloat(fieldLabelY, minFieldY, maxFieldY);
        } else if (battleActorMode) {
            labelX = actors[i].labelScreenX;
            labelY = actors[i].labelScreenY;
            if (!AuroraBattleLabelPointSane(actors[i], labelX, labelY, width, height)) {
                labelX = actors[i].screenX;
                labelY = actors[i].screenY - battleLift;
            }
            if (!AuroraBattleLabelPointSane(actors[i], labelX, labelY, width, height)) {
                if (!AuroraFinite(labelX) || !AuroraFinite(labelY)) continue;
                labelX = AuroraClampFloat(labelX, 24.0f, static_cast<float>(width) - 24.0f);
                labelY = AuroraClampFloat(labelY, AuroraBattleLabelMinY(height), AuroraBattleLabelMaxY(height));
            }
        }
        const int sx = static_cast<int>(labelX + 0.5f);
        const int sy = static_cast<int>(labelY + 0.5f);
        if (!fieldPartyMode && (sx < -40 || sx > width + 40 || sy < -40 || sy > height + 40)) continue;

        const AuroraActorKind kind = actors[i].kind;
        const char prefix = AuroraKindPrefix(kind);
        const int slot = kindIndex[static_cast<int>(kind)]++;
        const COLORREF color = AuroraKindColor(kind);
        AuroraDrawCross(hdc, sx, sy, color);
        SetTextColor(hdc, color);

        char text[160] = {};
        if (detail) {
            _snprintf_s(text, sizeof(text), _TRUNCATE,
                "%c%d %04X @%08X\nroot %.1f %.1f %.1f\nhead %.0f %.0f w=%.2f",
                prefix, slot, actors[i].id, static_cast<unsigned>(actors[i].inst),
                actors[i].x, actors[i].y, actors[i].z,
                actors[i].labelScreenX, actors[i].labelScreenY, actors[i].labelClipW);
        } else {
            _snprintf_s(text, sizeof(text), _TRUNCATE,
                "%c%d",
                prefix, slot);
        }

        RECT tr = detail
            ? RECT{ sx + 9, sy - 44, sx + 296, sy + 62 }
            : RECT{ sx - 24, sy - 31, sx + 24, sy - 11 };
        if (!battleActorMode) {
            AuroraResolveLabelRect(&tr, usedLabelRects, usedLabelRectCount, width, height);
        }
        const UINT textFormat = detail
            ? (DT_LEFT | DT_NOCLIP)
            : (DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        AuroraDrawTextShadow(hdc, text, tr, textFormat, color);
        if (usedLabelRectCount < static_cast<int>(sizeof(usedLabelRects) / sizeof(usedLabelRects[0]))) {
            usedLabelRects[usedLabelRectCount++] = tr;
        }
        ++drawnByKind[static_cast<int>(kind)];
        ++drawn;
    }
    g_auroraLastLabelCount = drawn;
    g_auroraLastPartyLabelCount = drawnByKind[AURORA_ACTOR_PARTY];
    g_auroraLastMonsterLabelCount = drawnByKind[AURORA_ACTOR_MONSTER];
    g_auroraLastNpcLabelCount = drawnByKind[AURORA_ACTOR_NPC];
    g_auroraLastObjectLabelCount = drawnByKind[AURORA_ACTOR_OBJECT];
    g_auroraLastOtherLabelCount = drawnByKind[AURORA_ACTOR_OTHER];

    SelectObject(hdc, oldFont);
    DeleteObject(font);

    const DWORD now = GetTickCount();
    if (now - g_auroraLastLogTick > 2000) {
        g_auroraLastLogTick = now;
        Log("[ffx-hooks] AuroraOverlay roots=%u w2s=0x%08X visible=%u labels=%u P=%u M=%u N=%u O=%u X=%u detail=%d viewport=%dx%d spread=%.1f\n",
            static_cast<unsigned>(actorCount),
            static_cast<unsigned>(w2sAddress),
            static_cast<unsigned>(visible),
            static_cast<unsigned>(drawn),
            static_cast<unsigned>(g_auroraLastPartyLabelCount),
            static_cast<unsigned>(g_auroraLastMonsterLabelCount),
            static_cast<unsigned>(g_auroraLastNpcLabelCount),
            static_cast<unsigned>(g_auroraLastObjectLabelCount),
            static_cast<unsigned>(g_auroraLastOtherLabelCount),
            detail ? 1 : 0,
            width, height, spread);
    }
}

static void AuroraPaintOverlay(HWND hwnd, HDC hdc, const RECT& rc) {
    (void)hwnd;
    AuroraDrawOverlayGdiContent(hdc, rc, true);
}

/* D3D11 in-frame texture quad path. It keeps GDI only as an off-screen text
 * rasterizer, uploads the pixels to a transparent BGRA texture, then draws one
 * full-screen quad in IDXGISwapChain::Present. */
struct AuroraD3DVertex {
    float x, y, z;
    float u, v;
};

template <typename T>
static void AuroraSafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

typedef HRESULT (STDMETHODCALLTYPE *AuroraPresentFn)(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);
typedef HRESULT (WINAPI *AuroraD3DCompileFn)(
    LPCVOID sourceData,
    SIZE_T sourceSize,
    LPCSTR sourceName,
    const D3D_SHADER_MACRO* defines,
    ID3DInclude* include,
    LPCSTR entrypoint,
    LPCSTR target,
    UINT flags1,
    UINT flags2,
    ID3DBlob** code,
    ID3DBlob** errorMsgs);
typedef HRESULT (WINAPI *AuroraD3D11CreateDeviceAndSwapChainFn)(
    IDXGIAdapter* adapter,
    D3D_DRIVER_TYPE driverType,
    HMODULE software,
    UINT flags,
    const D3D_FEATURE_LEVEL* featureLevels,
    UINT featureLevelsCount,
    UINT sdkVersion,
    const DXGI_SWAP_CHAIN_DESC* swapChainDesc,
    IDXGISwapChain** swapChain,
    ID3D11Device** device,
    D3D_FEATURE_LEVEL* featureLevel,
    ID3D11DeviceContext** immediateContext);
typedef HRESULT (STDMETHODCALLTYPE *AuroraD3DMapFn)(
    ID3D11DeviceContext* context,
    ID3D11Resource* resource,
    UINT subresource,
    D3D11_MAP mapType,
    UINT mapFlags,
    D3D11_MAPPED_SUBRESOURCE* mapped);
typedef void (STDMETHODCALLTYPE *AuroraD3DUnmapFn)(
    ID3D11DeviceContext* context,
    ID3D11Resource* resource,
    UINT subresource);
typedef void (STDMETHODCALLTYPE *AuroraD3DUpdateSubresourceFn)(
    ID3D11DeviceContext* context,
    ID3D11Resource* dstResource,
    UINT dstSubresource,
    const D3D11_BOX* dstBox,
    const void* srcData,
    UINT srcRowPitch,
    UINT srcDepthPitch);

static PLH::x86Detour* g_auroraD3DPresentDetour = nullptr;
static uint64_t        g_auroraD3DPresentTrampoline = 0;
// One arbiter serializes the physical Present hook shared by Aurora/F7 and the F8 producer.
static FfxHooks::F8Runtime::AtomicPresentHookArbiter g_auroraD3DPresentHookArbiter{};
/* Once published, terminal means no Present-owned F8 consumer may advertise itself LIVE even
 * if an InstallHooks frame still holds an older "producer armed" snapshot. */
alignas(4) static volatile LONG g_auroraD3DPresentTerminal = 0;
alignas(4) static volatile LONG g_maechenConfigEnabledPublished = 0;
alignas(4) static volatile LONG g_maechenNativePumpReadyPublished = 0;
alignas(4) static volatile LONG g_maechenInstallState = 0; // 0=waiting, 1=installing, 2=ready, 3=failed
static volatile LONG   g_auroraD3DRenderEnabled = 0;
static volatile LONG   g_auroraD3DInPresent = 0;
static DWORD           g_auroraD3DLastLogTick = 0;
static PLH::x86Detour* g_auroraD3DCreateDetour = nullptr;
static uint64_t        g_auroraD3DCreateTrampoline = 0;
static bool            g_auroraD3DCreateHooked = false;
static HANDLE          g_auroraD3DFallbackThread = NULL;
static volatile LONG   g_auroraD3DFallbackRunning = 0;
static DWORD           g_auroraD3DLastTextureUpdateTick = 0;
static int             g_auroraD3DUpdateIntervalMs = 33;
static bool            g_auroraD3DTexturePrimed = false;
static bool            g_auroraD3DW2SSniffEnabled = false;
static bool            g_auroraD3DProjectWhenW2SReady = false;
static bool            g_auroraD3DLightSniffAfterW2S = true;
static PLH::x86Detour* g_auroraD3DMapDetour = nullptr;
static uint64_t        g_auroraD3DMapTrampoline = 0;
static bool            g_auroraD3DMapHooked = false;
static PLH::x86Detour* g_auroraD3DUnmapDetour = nullptr;
static uint64_t        g_auroraD3DUnmapTrampoline = 0;
static bool            g_auroraD3DUnmapHooked = false;
static PLH::x86Detour* g_auroraD3DUpdateSubresourceDetour = nullptr;
static uint64_t        g_auroraD3DUpdateSubresourceTrampoline = 0;
static bool            g_auroraD3DUpdateSubresourceHooked = false;
static volatile LONG   g_auroraD3DW2SSniffInHook = 0;
static DWORD           g_auroraD3DLastW2SSniffLogTick = 0;
static uintptr_t       g_auroraD3DLastW2SSniffCaller = 0;
static uintptr_t       g_auroraD3DLastW2SSniffMapCaller = 0;
static uint32_t        g_auroraD3DW2SSniffHits = 0;
static DWORD           g_auroraD3DLastW2SAutoPauseLogTick = 0;
static volatile LONG   g_auroraD3DSniffMapCalls = 0;
static volatile LONG   g_auroraD3DSniffMapCB = 0;
static volatile LONG   g_auroraD3DSniffUnmapCalls = 0;
static volatile LONG   g_auroraD3DSniffUnmapTracked = 0;
static volatile LONG   g_auroraD3DSniffUpdateCalls = 0;
static volatile LONG   g_auroraD3DSniffUpdateCB = 0;
static volatile LONG   g_auroraD3DSniffPatternChecks = 0;
static volatile LONG   g_auroraD3DSniffProjectionSamples = 0;
static volatile LONG   g_auroraD3DSniffProjectionHits = 0;
static DWORD           g_auroraD3DLastW2SSniffStatsLogTick = 0;

struct AuroraD3DSniffCallerSample {
    uintptr_t caller;
    uint32_t hits;
    UINT byteWidth;
};

static AuroraD3DSniffCallerSample g_auroraD3DSniffMapCallers[8] = {};
static AuroraD3DSniffCallerSample g_auroraD3DSniffUpdateCallers[8] = {};

struct AuroraD3DMappedResource {
    ID3D11Resource* resource;
    void* data;
    UINT byteWidth;
    uintptr_t mapCaller;
    DWORD tick;
};

static AuroraD3DMappedResource g_auroraD3DMappedResources[64] = {};

static bool AuroraD3DSniffFallbackMatrixFresh(DWORD now) {
    if (g_auroraSniffW2SMatrixTick == 0) return false;
    const DWORD maxAgeMs = static_cast<DWORD>(g_auroraSniffW2SMaxAgeMs < 100 ? 100 : g_auroraSniffW2SMaxAgeMs);
    return now - g_auroraSniffW2SMatrixTick <= maxAgeMs;
}

static bool AuroraD3DShouldInspectW2SSniff() {
    if (!g_auroraD3DW2SSniffEnabled) return false;
    if (!g_auroraD3DLightSniffAfterW2S) return true;
    if (g_auroraD3DProjectWhenW2SReady) return true;
    const bool compactActorScene = g_auroraSniffActorCount > 0 && g_auroraSniffActorCount <= 16;
    if (g_auroraW2SAddress != 0 && !compactActorScene) return false;

    const DWORD now = GetTickCount();
    const LONG projectionHits = InterlockedCompareExchange(&g_auroraD3DSniffProjectionHits, 0, 0);
    if (g_auroraD3DSniffAutoPauseHits > 0 &&
        projectionHits >= g_auroraD3DSniffAutoPauseHits &&
        AuroraD3DSniffFallbackMatrixFresh(now)) {
        if (g_auroraD3DLastW2SAutoPauseLogTick == 0 ||
            now - g_auroraD3DLastW2SAutoPauseLogTick >= 5000u) {
            g_auroraD3DLastW2SAutoPauseLogTick = now;
            Log("[ffx-hooks] AuroraD3D W2S sniff auto-paused projectionHits=%ld threshold=%d matrixAgeMs=%u\n",
                projectionHits,
                g_auroraD3DSniffAutoPauseHits,
                static_cast<unsigned>(now - g_auroraSniffW2SMatrixTick));
        }
        return false;
    }

    return true;
}

static IDXGISwapChain*           g_auroraD3DSwapChain = nullptr;
static ID3D11Device*             g_auroraD3DDevice = nullptr;
static ID3D11DeviceContext*      g_auroraD3DContext = nullptr;
static ID3D11RenderTargetView*   g_auroraD3DRtv = nullptr;
static ID3D11Texture2D*          g_auroraD3DTexture = nullptr;
static ID3D11ShaderResourceView* g_auroraD3DSrv = nullptr;
static ID3D11SamplerState*       g_auroraD3DSampler = nullptr;
static ID3D11Buffer*             g_auroraD3DVertexBuffer = nullptr;
static ID3D11VertexShader*       g_auroraD3DVertexShader = nullptr;
static ID3D11PixelShader*        g_auroraD3DPixelShader = nullptr;
static ID3D11InputLayout*        g_auroraD3DInputLayout = nullptr;
static ID3D11BlendState*         g_auroraD3DBlend = nullptr;
static ID3D11RasterizerState*    g_auroraD3DRaster = nullptr;
static ID3D11DepthStencilState*  g_auroraD3DDepth = nullptr;
static HDC                      g_auroraD3DMemDc = NULL;
static HBITMAP                  g_auroraD3DDib = NULL;
static HGDIOBJ                  g_auroraD3DOldDib = NULL;
static uint32_t*                g_auroraD3DPixels = nullptr;
static UINT                     g_auroraD3DWidth = 0;
static UINT                     g_auroraD3DHeight = 0;

static bool AuroraD3DIsFfxCodeAddress(uintptr_t address) {
    return AuroraFfxCodeAddress(address);
}

static uint32_t AuroraD3DCallerRva(uintptr_t caller) {
    return AuroraFfxCodeRva(caller);
}

static bool AuroraD3DTryGetConstantBufferByteWidth(ID3D11Resource* resource, UINT* byteWidthOut) {
    if (byteWidthOut) *byteWidthOut = 0;
    if (!resource) return false;

    ID3D11Buffer* buffer = nullptr;
    HRESULT hr = resource->QueryInterface(__uuidof(ID3D11Buffer), reinterpret_cast<void**>(&buffer));
    if (FAILED(hr) || !buffer) return false;

    D3D11_BUFFER_DESC desc = {};
    buffer->GetDesc(&desc);
    AuroraSafeRelease(buffer);

    if ((desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) == 0) return false;
    if (desc.ByteWidth < 0x100u || desc.ByteWidth > 0x10000u) return false;
    if (byteWidthOut) *byteWidthOut = desc.ByteWidth;
    return true;
}

static void AuroraD3DRecordSniffCaller(AuroraD3DSniffCallerSample* samples, int count,
    uintptr_t caller, UINT byteWidth) {
    if (!samples || count <= 0 || !caller) return;

    int empty = -1;
    int weakest = 0;
    uint32_t weakestHits = 0xFFFFFFFFu;
    for (int i = 0; i < count; ++i) {
        if (samples[i].caller == caller) {
            ++samples[i].hits;
            samples[i].byteWidth = byteWidth;
            return;
        }
        if (!samples[i].caller && empty < 0) empty = i;
        if (samples[i].hits < weakestHits) {
            weakestHits = samples[i].hits;
            weakest = i;
        }
    }

    const int slot = empty >= 0 ? empty : weakest;
    samples[slot].caller = caller;
    samples[slot].hits = 1;
    samples[slot].byteWidth = byteWidth;
}

static void AuroraD3DResetW2SSniffStats() {
    g_auroraD3DLastW2SSniffLogTick = 0;
    g_auroraD3DLastW2SSniffStatsLogTick = 0;
    g_auroraD3DLastW2SSniffCaller = 0;
    g_auroraD3DLastW2SSniffMapCaller = 0;
    g_auroraD3DW2SSniffHits = 0;
    g_auroraD3DLastW2SAutoPauseLogTick = 0;
    InterlockedExchange(&g_auroraD3DSniffMapCalls, 0);
    InterlockedExchange(&g_auroraD3DSniffMapCB, 0);
    InterlockedExchange(&g_auroraD3DSniffUnmapCalls, 0);
    InterlockedExchange(&g_auroraD3DSniffUnmapTracked, 0);
    InterlockedExchange(&g_auroraD3DSniffUpdateCalls, 0);
    InterlockedExchange(&g_auroraD3DSniffUpdateCB, 0);
    InterlockedExchange(&g_auroraD3DSniffPatternChecks, 0);
    InterlockedExchange(&g_auroraD3DSniffProjectionSamples, 0);
    InterlockedExchange(&g_auroraD3DSniffProjectionHits, 0);
    memset(g_auroraD3DMappedResources, 0, sizeof(g_auroraD3DMappedResources));
    memset(g_auroraD3DSniffMapCallers, 0, sizeof(g_auroraD3DSniffMapCallers));
    memset(g_auroraD3DSniffUpdateCallers, 0, sizeof(g_auroraD3DSniffUpdateCallers));
    memset(g_auroraSniffW2SMatrix, 0, sizeof(g_auroraSniffW2SMatrix));
    g_auroraSniffW2SMatrixTick = 0;
    g_auroraSniffW2SCaller = 0;
    g_auroraSniffW2SOffset = 0;
    g_auroraSniffW2SLayout = 0;
    g_auroraSniffW2SCoordMode = 0;
    g_auroraSniffW2SActorCount = 0;
    g_auroraSniffW2SVisible = 0;
    g_auroraSniffW2SSpread = 0.0f;
    g_auroraSniffW2SScore = -FLT_MAX;
}

static void AuroraD3DLogW2SSniffStatsIfDue(DWORD now) {
    if (!g_auroraD3DW2SSniffEnabled) return;
    const bool sniffActive = AuroraD3DShouldInspectW2SSniff();
    const DWORD intervalMs = sniffActive ? 5000u : 30000u;
    if (g_auroraD3DLastW2SSniffStatsLogTick &&
        now - g_auroraD3DLastW2SSniffStatsLogTick < intervalMs) {
        return;
    }
    g_auroraD3DLastW2SSniffStatsLogTick = now;

    Log("[ffx-hooks] AuroraD3D W2S sniff stats mapCalls=%ld mapCB=%ld unmapCalls=%ld unmapTracked=%ld updateCalls=%ld updateCB=%ld patternChecks=%ld projectionSamples=%ld projectionHits=%ld hits=%u hooks=%d/%d/%d sniffActive=%d autoPauseHits=%d\n",
        g_auroraD3DSniffMapCalls,
        g_auroraD3DSniffMapCB,
        g_auroraD3DSniffUnmapCalls,
        g_auroraD3DSniffUnmapTracked,
        g_auroraD3DSniffUpdateCalls,
        g_auroraD3DSniffUpdateCB,
        g_auroraD3DSniffPatternChecks,
        g_auroraD3DSniffProjectionSamples,
        g_auroraD3DSniffProjectionHits,
        static_cast<unsigned>(g_auroraD3DW2SSniffHits),
        g_auroraD3DMapHooked ? 1 : 0,
        g_auroraD3DUnmapHooked ? 1 : 0,
        g_auroraD3DUpdateSubresourceHooked ? 1 : 0,
        sniffActive ? 1 : 0,
        g_auroraD3DSniffAutoPauseHits);

    for (int i = 0; i < 8; ++i) {
        const AuroraD3DSniffCallerSample& s = g_auroraD3DSniffMapCallers[i];
        if (!s.caller) continue;
        Log("[ffx-hooks] AuroraD3D W2S sniff mapCaller[%d]=0x%08X rva=0x%08X hits=%u byteWidth=%u\n",
            i,
            static_cast<unsigned>(s.caller),
            static_cast<unsigned>(AuroraD3DCallerRva(s.caller)),
            static_cast<unsigned>(s.hits),
            static_cast<unsigned>(s.byteWidth));
    }
    for (int i = 0; i < 8; ++i) {
        const AuroraD3DSniffCallerSample& s = g_auroraD3DSniffUpdateCallers[i];
        if (!s.caller) continue;
        Log("[ffx-hooks] AuroraD3D W2S sniff updateCaller[%d]=0x%08X rva=0x%08X hits=%u byteWidth=%u\n",
            i,
            static_cast<unsigned>(s.caller),
            static_cast<unsigned>(AuroraD3DCallerRva(s.caller)),
            static_cast<unsigned>(s.hits),
            static_cast<unsigned>(s.byteWidth));
    }
}

static bool AuroraD3DLooksLikeW2SUploadBlock(const void* data, UINT byteWidth, UINT* offsetOut) {
    if (offsetOut) *offsetOut = 0;
    if (!data || byteWidth < 0x100u) return false;

    const BYTE* bytes = static_cast<const BYTE*>(data);
    const UINT scanLimit = byteWidth > 0x1000u ? 0x1000u : byteWidth;
    for (UINT offset = 0; offset + 0x100u <= scanLimit; offset += 0x10u) {
        bool match = false;
        __try {
            const float* f = reinterpret_cast<const float*>(bytes + offset);
            match = true;
            for (int i = 0; i < 64; ++i) {
                if (!AuroraFinite(f[i]) || fabsf(f[i]) > 100000.0f) {
                    match = false;
                    break;
                }
            }
            if (match) {
                for (int i = 16; i < 32; ++i) {
                    if (fabsf(f[i]) > 0.001f) {
                        match = false;
                        break;
                    }
                }
            }
            if (match) {
                const int diagBase = 32;
                const int diagIdx[4] = { 0, 5, 10, 15 };
                const float diagExpected[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
                for (int i = 0; i < 4; ++i) {
                    if (fabsf(f[diagBase + diagIdx[i]] - diagExpected[i]) > 0.02f) {
                        match = false;
                        break;
                    }
                }
            }
            if (match) {
                const int diagBase = 32;
                for (int i = 0; i < 16; ++i) {
                    const bool isDiag = (i == 0 || i == 5 || i == 10 || i == 15);
                    if (!isDiag && fabsf(f[diagBase + i]) > 0.02f) {
                        match = false;
                        break;
                    }
                }
            }
            if (match) {
                if (fabsf(f[60]) > 0.02f || fabsf(f[61]) > 0.02f ||
                    fabsf(f[62]) > 0.02f || fabsf(f[63] - 1.0f) > 0.02f) {
                    match = false;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            match = false;
        }
        if (match) {
            if (offsetOut) *offsetOut = offset;
            return true;
        }
    }
    return false;
}

static bool AuroraD3DMatrixFinite(const float* m) {
    if (!m) return false;
    for (int i = 0; i < 16; ++i) {
        if (!AuroraFinite(m[i]) || fabsf(m[i]) > 1000000.0f) return false;
    }
    return true;
}

static void AuroraD3DTransposeMatrix(const float in[16], float out[16]) {
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out[r * 4 + c] = in[c * 4 + r];
        }
    }
}

static bool AuroraD3DTryProjectUploadMatrix(const float candidate[16], bool transpose,
    float outMatrix[16], uint32_t* visibleOut, float* spreadOut, int* coordModeOut, float* scoreOut) {
    if (!candidate || !outMatrix) return false;
    if (!AuroraD3DMatrixFinite(candidate)) return false;

    float matrix[16] = {};
    if (transpose) {
        AuroraD3DTransposeMatrix(candidate, matrix);
    } else {
        memcpy(matrix, candidate, sizeof(matrix));
    }

    const uint32_t actorCount = g_auroraSniffActorCount > 32 ? 32 : g_auroraSniffActorCount;
    const int width = g_auroraSniffViewportWidth;
    const int height = g_auroraSniffViewportHeight;
    if (actorCount == 0 || width <= 0 || height <= 0) return false;
    const uint32_t minVisible =
        actorCount >= g_auroraW2SScanMinRoots ? g_auroraW2SScanMinRoots : actorCount;
    if (actorCount < minVisible) return false;

    const bool compactBattle = AuroraD3DSniffCompactBattleScene();
    const int modeCount = compactBattle ? 7 : 1;
    const int savedMode = g_auroraSniffW2SCoordMode;
    int bestMode = 0;
    uint32_t bestVisible = 0;
    float bestSpread = 0.0f;
    float bestScore = -FLT_MAX;
    bool found = false;

    for (int mode = 0; mode < modeCount; ++mode) {
        g_auroraSniffW2SCoordMode = mode;
        AuroraActor actors[32] = {};
        memcpy(actors, g_auroraSniffActors, sizeof(AuroraActor) * actorCount);
        uint32_t visible = 0;
        float spread = 0.0f;
        if (!AuroraProjectActors(matrix, actors, actorCount, width, height, &visible, &spread)) continue;
        if (visible < minVisible || spread < 80.0f) continue;

        const float score = AuroraScoreProjectedBattleActors(actors, actorCount, width, height, visible, spread);
        if (!found || score > bestScore) {
            found = true;
            bestMode = mode;
            bestVisible = visible;
            bestSpread = spread;
            bestScore = score;
        }
    }
    g_auroraSniffW2SCoordMode = savedMode;
    if (!found) return false;

    memcpy(outMatrix, matrix, sizeof(matrix));
    if (visibleOut) *visibleOut = bestVisible;
    if (spreadOut) *spreadOut = bestSpread;
    if (coordModeOut) *coordModeOut = bestMode;
    if (scoreOut) *scoreOut = bestScore;
    return true;
}

static bool AuroraD3DFindProjectedW2SUploadBlock(const void* data, UINT byteWidth, UINT* offsetOut,
    char* layoutOut, float outMatrix[16], uint32_t* visibleOut, float* spreadOut,
    int* coordModeOut, float* scoreOut) {
    if (offsetOut) *offsetOut = 0;
    if (layoutOut) *layoutOut = 0;
    if (!data || !outMatrix || byteWidth < 0x40u) return false;
    if (g_auroraSniffActorCount == 0 || g_auroraSniffViewportWidth <= 0 || g_auroraSniffViewportHeight <= 0) {
        return false;
    }

    const BYTE* bytes = static_cast<const BYTE*>(data);
    const UINT scanLimit = byteWidth > 0x1000u ? 0x1000u : byteWidth;
    bool found = false;
    UINT bestOffset = 0;
    char bestLayout = 0;
    uint32_t bestVisible = 0;
    float bestSpread = 0.0f;
    int bestCoordMode = 0;
    float bestScore = -FLT_MAX;
    float bestMatrix[16] = {};
    for (UINT offset = 0; offset + 0x40u <= scanLimit; offset += 0x10u) {
        __try {
            const float* candidate = reinterpret_cast<const float*>(bytes + offset);
            uint32_t visible = 0;
            float spread = 0.0f;
            int coordMode = 0;
            float score = -FLT_MAX;
            if (AuroraD3DTryProjectUploadMatrix(candidate, false, outMatrix, &visible, &spread, &coordMode, &score)) {
                if (!found || score > bestScore) {
                    found = true;
                    bestOffset = offset;
                    bestLayout = 'N';
                    bestVisible = visible;
                    bestSpread = spread;
                    bestCoordMode = coordMode;
                    bestScore = score;
                    memcpy(bestMatrix, outMatrix, sizeof(bestMatrix));
                }
            }
            if (AuroraD3DTryProjectUploadMatrix(candidate, true, outMatrix, &visible, &spread, &coordMode, &score)) {
                if (!found || score > bestScore) {
                    found = true;
                    bestOffset = offset;
                    bestLayout = 'T';
                    bestVisible = visible;
                    bestSpread = spread;
                    bestCoordMode = coordMode;
                    bestScore = score;
                    memcpy(bestMatrix, outMatrix, sizeof(bestMatrix));
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
    if (!found) return false;

    memcpy(outMatrix, bestMatrix, sizeof(bestMatrix));
    if (offsetOut) *offsetOut = bestOffset;
    if (layoutOut) *layoutOut = bestLayout;
    if (visibleOut) *visibleOut = bestVisible;
    if (spreadOut) *spreadOut = bestSpread;
    if (coordModeOut) *coordModeOut = bestCoordMode;
    if (scoreOut) *scoreOut = bestScore;
    return true;
}

static bool AuroraD3DSniffSnapshotHasKind(AuroraActorKind kind) {
    const uint32_t actorCount = g_auroraSniffActorCount > 32 ? 32 : g_auroraSniffActorCount;
    for (uint32_t i = 0; i < actorCount; ++i) {
        if (g_auroraSniffActors[i].kind == kind) return true;
    }
    return false;
}

static bool AuroraD3DSniffCompactBattleScene() {
    return g_auroraSniffActorCount >= 4 && g_auroraSniffActorCount <= 12 &&
        AuroraD3DSniffSnapshotHasKind(AURORA_ACTOR_PARTY) &&
        AuroraD3DSniffSnapshotHasKind(AURORA_ACTOR_MONSTER);
}

static void AuroraD3DStoreSniffW2SMatrix(const float matrix[16], UINT offset, uintptr_t caller, char layout,
    uint32_t visible = 0, float spread = 0.0f, int coordMode = 0, float score = -FLT_MAX) {
    if (!matrix) return;
    const DWORD now = GetTickCount();
    const float candidateScore = score == -FLT_MAX ?
        (static_cast<float>(visible) * 10000.0f + spread) :
        score;
    if (AuroraD3DSniffCompactBattleScene() && g_auroraSniffW2SMatrixTick != 0 &&
        g_auroraSniffW2SActorCount == g_auroraSniffActorCount) {
        const uint32_t actorTarget = g_auroraSniffActorCount;
        const bool currentComplete = actorTarget > 0 && g_auroraSniffW2SVisible >= actorTarget;
        const bool candidateComplete = actorTarget > 0 && visible >= actorTarget;
        const bool differentCandidate = g_auroraSniffW2SOffset != offset ||
            g_auroraSniffW2SLayout != layout ||
            g_auroraSniffW2SCoordMode != coordMode;

        if (g_auroraSniffW2SVisible > 0 && visible == 0) return;
        if (currentComplete && !candidateComplete) return;

        if (differentCandidate) {
            const bool clearlyBetter =
                (candidateComplete && !currentComplete) ||
                (!currentComplete && visible > g_auroraSniffW2SVisible + 1) ||
                (candidateComplete && currentComplete &&
                    candidateScore > g_auroraSniffW2SScore + 80.0f);
            if (!clearlyBetter) {
                g_auroraSniffW2SMatrixTick = now;
                return;
            }
        } else if (currentComplete && candidateComplete &&
            candidateScore <= g_auroraSniffW2SScore + 120.0f &&
            fabsf(spread - g_auroraSniffW2SSpread) < 260.0f) {
            g_auroraSniffW2SMatrixTick = now;
            return;
        }
    }
    memcpy(g_auroraSniffW2SMatrix, matrix, sizeof(g_auroraSniffW2SMatrix));
    g_auroraSniffW2SCaller = caller;
    g_auroraSniffW2SOffset = offset;
    g_auroraSniffW2SLayout = layout;
    g_auroraSniffW2SCoordMode = coordMode;
    g_auroraSniffW2SActorCount = g_auroraSniffActorCount;
    g_auroraSniffW2SVisible = visible;
    g_auroraSniffW2SSpread = spread;
    g_auroraSniffW2SScore = candidateScore;
    g_auroraSniffW2SMatrixTick = now;
}

static bool AuroraD3DShouldProbeProjectedW2S(DWORD now) {
    const bool compactActorScene = g_auroraSniffActorCount > 0 && g_auroraSniffActorCount <= 16;
    if (g_auroraW2SAddress && !g_auroraD3DProjectWhenW2SReady && !compactActorScene) return false;
    if (g_auroraD3DProjectionRefreshMs <= 0 || g_auroraSniffW2SMatrixTick == 0) return true;
    return now - g_auroraSniffW2SMatrixTick >= static_cast<DWORD>(g_auroraD3DProjectionRefreshMs);
}

static void AuroraD3DReportW2SUpload(const char* path, ID3D11Resource* resource, const void* data,
    UINT byteWidth, UINT offset, uintptr_t caller, uintptr_t mapCaller) {
    if (!path || !resource || !data) return;

    __try {
        const BYTE* bytes = static_cast<const BYTE*>(data);
        const float* matrix = reinterpret_cast<const float*>(bytes + offset);
        if (AuroraD3DMatrixFinite(matrix)) {
            AuroraD3DStoreSniffW2SMatrix(matrix, offset, caller ? caller : mapCaller, 'P');
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    ++g_auroraD3DW2SSniffHits;
    const DWORD now = GetTickCount();
    const bool newCaller = caller != g_auroraD3DLastW2SSniffCaller ||
        mapCaller != g_auroraD3DLastW2SSniffMapCaller;
    if (!newCaller && now - g_auroraD3DLastW2SSniffLogTick < 2000) return;

    g_auroraD3DLastW2SSniffLogTick = now;
    g_auroraD3DLastW2SSniffCaller = caller;
    g_auroraD3DLastW2SSniffMapCaller = mapCaller;

    Log("[ffx-hooks] AuroraD3D W2S upload sniff hit=%u path=%s resource=%p data=%p byteWidth=%u offset=0x%X caller=0x%08X rva=0x%08X mapCaller=0x%08X mapRva=0x%08X\n",
        static_cast<unsigned>(g_auroraD3DW2SSniffHits),
        path,
        resource,
        data,
        static_cast<unsigned>(byteWidth),
        static_cast<unsigned>(offset),
        static_cast<unsigned>(caller),
        static_cast<unsigned>(AuroraD3DCallerRva(caller)),
        static_cast<unsigned>(mapCaller),
        static_cast<unsigned>(AuroraD3DCallerRva(mapCaller)));
}

static void AuroraD3DReportProjectedW2SUpload(const char* path, ID3D11Resource* resource,
    UINT byteWidth, UINT offset, uintptr_t caller, char layout, const float matrix[16],
    uint32_t visible, float spread, int coordMode, float score) {
    if (!path || !resource || !matrix) return;

    AuroraD3DStoreSniffW2SMatrix(matrix, offset, caller, layout, visible, spread, coordMode, score);
    InterlockedIncrement(&g_auroraD3DSniffProjectionHits);
    ++g_auroraD3DW2SSniffHits;

    const DWORD now = GetTickCount();
    if (now - g_auroraD3DLastW2SSniffLogTick < 1000) return;

    g_auroraD3DLastW2SSniffLogTick = now;
    g_auroraD3DLastW2SSniffCaller = caller;
    g_auroraD3DLastW2SSniffMapCaller = 0;

    Log("[ffx-hooks] AuroraD3D W2S upload project hit=%u path=%s resource=%p byteWidth=%u offset=0x%X layout=%c coord=%d visible=%u spread=%.1f score=%.1f caller=0x%08X rva=0x%08X\n",
        static_cast<unsigned>(g_auroraD3DW2SSniffHits),
        path,
        resource,
        static_cast<unsigned>(byteWidth),
        static_cast<unsigned>(offset),
        layout ? layout : '?',
        coordMode,
        static_cast<unsigned>(visible),
        spread,
        score,
        static_cast<unsigned>(caller),
        static_cast<unsigned>(AuroraD3DCallerRva(caller)));
}

static void AuroraD3DRememberMappedConstantBuffer(ID3D11Resource* resource, void* data,
    UINT byteWidth, uintptr_t mapCaller) {
    if (!resource || !data || byteWidth < 0x100u) return;

    DWORD oldestTick = 0xFFFFFFFFu;
    int oldest = 0;
    for (int i = 0; i < 64; ++i) {
        if (!g_auroraD3DMappedResources[i].resource ||
            g_auroraD3DMappedResources[i].resource == resource) {
            oldest = i;
            break;
        }
        if (g_auroraD3DMappedResources[i].tick < oldestTick) {
            oldestTick = g_auroraD3DMappedResources[i].tick;
            oldest = i;
        }
    }

    g_auroraD3DMappedResources[oldest].resource = resource;
    g_auroraD3DMappedResources[oldest].data = data;
    g_auroraD3DMappedResources[oldest].byteWidth = byteWidth;
    g_auroraD3DMappedResources[oldest].mapCaller = mapCaller;
    g_auroraD3DMappedResources[oldest].tick = GetTickCount();
}

static bool AuroraD3DForgetMappedConstantBuffer(ID3D11Resource* resource,
    AuroraD3DMappedResource* outMapped) {
    if (outMapped) memset(outMapped, 0, sizeof(*outMapped));
    if (!resource) return false;

    for (int i = 0; i < 64; ++i) {
        if (g_auroraD3DMappedResources[i].resource == resource) {
            if (outMapped) *outMapped = g_auroraD3DMappedResources[i];
            memset(&g_auroraD3DMappedResources[i], 0, sizeof(g_auroraD3DMappedResources[i]));
            return true;
        }
    }
    return false;
}

static HRESULT STDMETHODCALLTYPE AuroraD3DMapShim(ID3D11DeviceContext* context, ID3D11Resource* resource,
    UINT subresource, D3D11_MAP mapType, UINT mapFlags, D3D11_MAPPED_SUBRESOURCE* mapped) {
    AuroraD3DMapFn original = reinterpret_cast<AuroraD3DMapFn>(g_auroraD3DMapTrampoline);
    if (!original) return E_FAIL;

    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const bool inspect = AuroraD3DShouldInspectW2SSniff();
    if (inspect) {
        InterlockedIncrement(&g_auroraD3DSniffMapCalls);
    }
    const HRESULT hr = original(context, resource, subresource, mapType, mapFlags, mapped);
    if (inspect && SUCCEEDED(hr) && mapped && mapped->pData &&
        InterlockedCompareExchange(&g_auroraD3DW2SSniffInHook, 1, 0) == 0) {
        __try {
            UINT byteWidth = 0;
            if (AuroraD3DTryGetConstantBufferByteWidth(resource, &byteWidth)) {
                InterlockedIncrement(&g_auroraD3DSniffMapCB);
                AuroraD3DRecordSniffCaller(g_auroraD3DSniffMapCallers, 8, caller, byteWidth);
                AuroraD3DRememberMappedConstantBuffer(resource, mapped->pData, byteWidth, caller);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("[ffx-hooks] WARN AuroraD3D W2S sniff exception in Map\n");
        }
        InterlockedExchange(&g_auroraD3DW2SSniffInHook, 0);
    }
    return hr;
}

static void STDMETHODCALLTYPE AuroraD3DUnmapShim(ID3D11DeviceContext* context, ID3D11Resource* resource,
    UINT subresource) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const bool inspect = AuroraD3DShouldInspectW2SSniff();
    if (inspect) {
        InterlockedIncrement(&g_auroraD3DSniffUnmapCalls);
    }
    if (inspect &&
        InterlockedCompareExchange(&g_auroraD3DW2SSniffInHook, 1, 0) == 0) {
        __try {
            AuroraD3DMappedResource mapped = {};
            if (AuroraD3DForgetMappedConstantBuffer(resource, &mapped)) {
                InterlockedIncrement(&g_auroraD3DSniffUnmapTracked);
                InterlockedIncrement(&g_auroraD3DSniffPatternChecks);
                UINT offset = 0;
                if (AuroraD3DLooksLikeW2SUploadBlock(mapped.data, mapped.byteWidth, &offset)) {
                    AuroraD3DReportW2SUpload("Map/Unmap", resource, mapped.data,
                        mapped.byteWidth, offset, caller, mapped.mapCaller);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("[ffx-hooks] WARN AuroraD3D W2S sniff exception in Unmap\n");
        }
        InterlockedExchange(&g_auroraD3DW2SSniffInHook, 0);
    }

    AuroraD3DUnmapFn original = reinterpret_cast<AuroraD3DUnmapFn>(g_auroraD3DUnmapTrampoline);
    if (original) original(context, resource, subresource);
}

static void STDMETHODCALLTYPE AuroraD3DUpdateSubresourceShim(ID3D11DeviceContext* context,
    ID3D11Resource* dstResource, UINT dstSubresource, const D3D11_BOX* dstBox,
    const void* srcData, UINT srcRowPitch, UINT srcDepthPitch) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const bool inspect = AuroraD3DShouldInspectW2SSniff();
    if (inspect) {
        InterlockedIncrement(&g_auroraD3DSniffUpdateCalls);
    }
    if (inspect && srcData &&
        InterlockedCompareExchange(&g_auroraD3DW2SSniffInHook, 1, 0) == 0) {
        __try {
            UINT byteWidth = 0;
            UINT offset = 0;
            if (AuroraD3DTryGetConstantBufferByteWidth(dstResource, &byteWidth)) {
                InterlockedIncrement(&g_auroraD3DSniffUpdateCB);
                const LONG checkIndex = InterlockedIncrement(&g_auroraD3DSniffPatternChecks);
                AuroraD3DRecordSniffCaller(g_auroraD3DSniffUpdateCallers, 8, caller, byteWidth);
                if (AuroraD3DLooksLikeW2SUploadBlock(srcData, byteWidth, &offset)) {
                    AuroraD3DReportW2SUpload("UpdateSubresource", dstResource, srcData,
                        byteWidth, offset, caller, 0);
                } else if ((checkIndex & 0x0F) == 0 && AuroraD3DShouldProbeProjectedW2S(GetTickCount())) {
                    InterlockedIncrement(&g_auroraD3DSniffProjectionSamples);
                    char layout = 0;
                    float matrix[16] = {};
                    uint32_t visible = 0;
                    float spread = 0.0f;
                    int coordMode = 0;
                    float score = -FLT_MAX;
                    if (AuroraD3DFindProjectedW2SUploadBlock(srcData, byteWidth, &offset,
                        &layout, matrix, &visible, &spread, &coordMode, &score)) {
                        AuroraD3DReportProjectedW2SUpload("UpdateSubresource/project",
                            dstResource, byteWidth, offset, caller, layout, matrix, visible, spread, coordMode, score);
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("[ffx-hooks] WARN AuroraD3D W2S sniff exception in UpdateSubresource\n");
        }
        InterlockedExchange(&g_auroraD3DW2SSniffInHook, 0);
    }

    AuroraD3DUpdateSubresourceFn original =
        reinterpret_cast<AuroraD3DUpdateSubresourceFn>(g_auroraD3DUpdateSubresourceTrampoline);
    if (original) {
        original(context, dstResource, dstSubresource, dstBox, srcData, srcRowPitch, srcDepthPitch);
    }
}

static void AuroraD3DReleaseResources() {
    if (g_auroraD3DMemDc) {
        if (g_auroraD3DOldDib) {
            SelectObject(g_auroraD3DMemDc, g_auroraD3DOldDib);
            g_auroraD3DOldDib = NULL;
        }
        DeleteDC(g_auroraD3DMemDc);
        g_auroraD3DMemDc = NULL;
    }
    if (g_auroraD3DDib) {
        DeleteObject(g_auroraD3DDib);
        g_auroraD3DDib = NULL;
    }
    g_auroraD3DPixels = nullptr;
    g_auroraD3DTexturePrimed = false;
    g_auroraD3DLastTextureUpdateTick = 0;
    AuroraSafeRelease(g_auroraD3DDepth);
    AuroraSafeRelease(g_auroraD3DRaster);
    AuroraSafeRelease(g_auroraD3DBlend);
    AuroraSafeRelease(g_auroraD3DInputLayout);
    AuroraSafeRelease(g_auroraD3DPixelShader);
    AuroraSafeRelease(g_auroraD3DVertexShader);
    AuroraSafeRelease(g_auroraD3DVertexBuffer);
    AuroraSafeRelease(g_auroraD3DSampler);
    AuroraSafeRelease(g_auroraD3DSrv);
    AuroraSafeRelease(g_auroraD3DTexture);
    AuroraSafeRelease(g_auroraD3DRtv);
    AuroraSafeRelease(g_auroraD3DContext);
    AuroraSafeRelease(g_auroraD3DDevice);
    AuroraSafeRelease(g_auroraD3DSwapChain);
    g_auroraD3DWidth = 0;
    g_auroraD3DHeight = 0;
    memset(g_auroraD3DMappedResources, 0, sizeof(g_auroraD3DMappedResources));
}

static bool AuroraD3DInstallContextSniffer(ID3D11DeviceContext* context) {
    if (!g_auroraD3DW2SSniffEnabled || !context) return false;
    if (g_auroraD3DMapDetour && g_auroraD3DUnmapDetour && g_auroraD3DUpdateSubresourceDetour) {
        return g_auroraD3DMapHooked || g_auroraD3DUnmapHooked || g_auroraD3DUpdateSubresourceHooked;
    }

    void** vtable = *reinterpret_cast<void***>(context);
    if (!vtable) {
        Log("[ffx-hooks] WARN AuroraD3D W2S sniff context vtable missing\n");
        return false;
    }

    const uint64_t mapVa = reinterpret_cast<uint64_t>(vtable[14]);
    const uint64_t unmapVa = reinterpret_cast<uint64_t>(vtable[15]);
    const uint64_t updateVa = reinterpret_cast<uint64_t>(vtable[48]);
    Log("[ffx-hooks] AuroraD3D W2S sniff installing context hooks Map=0x%08X Unmap=0x%08X UpdateSubresource=0x%08X\n",
        static_cast<unsigned>(mapVa),
        static_cast<unsigned>(unmapVa),
        static_cast<unsigned>(updateVa));

    try {
        if (!g_auroraD3DMapDetour && mapVa) {
            g_auroraD3DMapDetour = new PLH::x86Detour(
                mapVa, reinterpret_cast<uint64_t>(AuroraD3DMapShim), &g_auroraD3DMapTrampoline);
            g_auroraD3DMapHooked = g_auroraD3DMapDetour->hook();
            Log("[ffx-hooks] AuroraD3D W2S sniff Map hook ok=%d trampoline=0x%llX\n",
                g_auroraD3DMapHooked ? 1 : 0,
                static_cast<unsigned long long>(g_auroraD3DMapTrampoline));
        }
        if (!g_auroraD3DUnmapDetour && unmapVa) {
            g_auroraD3DUnmapDetour = new PLH::x86Detour(
                unmapVa, reinterpret_cast<uint64_t>(AuroraD3DUnmapShim), &g_auroraD3DUnmapTrampoline);
            g_auroraD3DUnmapHooked = g_auroraD3DUnmapDetour->hook();
            Log("[ffx-hooks] AuroraD3D W2S sniff Unmap hook ok=%d trampoline=0x%llX\n",
                g_auroraD3DUnmapHooked ? 1 : 0,
                static_cast<unsigned long long>(g_auroraD3DUnmapTrampoline));
        }
        if (!g_auroraD3DUpdateSubresourceDetour && updateVa) {
            g_auroraD3DUpdateSubresourceDetour = new PLH::x86Detour(
                updateVa,
                reinterpret_cast<uint64_t>(AuroraD3DUpdateSubresourceShim),
                &g_auroraD3DUpdateSubresourceTrampoline);
            g_auroraD3DUpdateSubresourceHooked = g_auroraD3DUpdateSubresourceDetour->hook();
            Log("[ffx-hooks] AuroraD3D W2S sniff UpdateSubresource hook ok=%d trampoline=0x%llX\n",
                g_auroraD3DUpdateSubresourceHooked ? 1 : 0,
                static_cast<unsigned long long>(g_auroraD3DUpdateSubresourceTrampoline));
        }
    } catch (const std::exception& ex) {
        Log("[ffx-hooks] ERROR AuroraD3D W2S sniff hook exception: %s\n", ex.what());
    } catch (...) {
        Log("[ffx-hooks] ERROR AuroraD3D W2S sniff hook unknown exception\n");
    }

    return g_auroraD3DMapHooked || g_auroraD3DUnmapHooked || g_auroraD3DUpdateSubresourceHooked;
}

static void AuroraD3DRemoveContextSniffer() {
    memset(g_auroraD3DMappedResources, 0, sizeof(g_auroraD3DMappedResources));

    if (g_auroraD3DUpdateSubresourceDetour) {
        try {
            if (g_auroraD3DUpdateSubresourceHooked) {
                const bool ok = g_auroraD3DUpdateSubresourceDetour->unHook();
                Log("[ffx-hooks] AuroraD3D W2S sniff UpdateSubresource unHook result=%d\n", ok ? 1 : 0);
            }
        } catch (...) {
            Log("[ffx-hooks] WARN AuroraD3D W2S sniff UpdateSubresource unHook exception\n");
        }
        delete g_auroraD3DUpdateSubresourceDetour;
        g_auroraD3DUpdateSubresourceDetour = nullptr;
    }
    g_auroraD3DUpdateSubresourceTrampoline = 0;
    g_auroraD3DUpdateSubresourceHooked = false;

    if (g_auroraD3DUnmapDetour) {
        try {
            if (g_auroraD3DUnmapHooked) {
                const bool ok = g_auroraD3DUnmapDetour->unHook();
                Log("[ffx-hooks] AuroraD3D W2S sniff Unmap unHook result=%d\n", ok ? 1 : 0);
            }
        } catch (...) {
            Log("[ffx-hooks] WARN AuroraD3D W2S sniff Unmap unHook exception\n");
        }
        delete g_auroraD3DUnmapDetour;
        g_auroraD3DUnmapDetour = nullptr;
    }
    g_auroraD3DUnmapTrampoline = 0;
    g_auroraD3DUnmapHooked = false;

    if (g_auroraD3DMapDetour) {
        try {
            if (g_auroraD3DMapHooked) {
                const bool ok = g_auroraD3DMapDetour->unHook();
                Log("[ffx-hooks] AuroraD3D W2S sniff Map unHook result=%d\n", ok ? 1 : 0);
            }
        } catch (...) {
            Log("[ffx-hooks] WARN AuroraD3D W2S sniff Map unHook exception\n");
        }
        delete g_auroraD3DMapDetour;
        g_auroraD3DMapDetour = nullptr;
    }
    g_auroraD3DMapTrampoline = 0;
    g_auroraD3DMapHooked = false;
    InterlockedExchange(&g_auroraD3DW2SSniffInHook, 0);
}

static AuroraD3DCompileFn AuroraD3DGetCompiler() {
    static HMODULE compiler = NULL;
    static AuroraD3DCompileFn fn = nullptr;
    if (fn) return fn;

    const char* dlls[] = { "d3dcompiler_47.dll", "d3dcompiler_46.dll", "d3dcompiler_43.dll" };
    for (int i = 0; i < 3 && !compiler; ++i) {
        compiler = LoadLibraryA(dlls[i]);
    }
    if (!compiler) return nullptr;
    fn = reinterpret_cast<AuroraD3DCompileFn>(GetProcAddress(compiler, "D3DCompile"));
    return fn;
}

static bool AuroraD3DCompileShader(const char* source, const char* entry, const char* target, ID3DBlob** outBlob) {
    if (!source || !entry || !target || !outBlob) return false;
    *outBlob = nullptr;
    AuroraD3DCompileFn compile = AuroraD3DGetCompiler();
    if (!compile) {
        Log("[ffx-hooks] WARN AuroraD3D D3DCompile unavailable\n");
        return false;
    }

    ID3DBlob* errors = nullptr;
    const HRESULT hr = compile(
        source,
        strlen(source),
        "AuroraOverlayShader",
        nullptr,
        nullptr,
        entry,
        target,
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        outBlob,
        &errors);
    if (FAILED(hr)) {
        const char* err = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "";
        Log("[ffx-hooks] WARN AuroraD3D shader compile failed entry=%s target=%s hr=0x%08X %s\n",
            entry, target, static_cast<unsigned>(hr), err ? err : "");
        AuroraSafeRelease(errors);
        AuroraSafeRelease(*outBlob);
        return false;
    }
    AuroraSafeRelease(errors);
    return true;
}

static bool AuroraD3DCreateShadersAndStates() {
    static const char* shaderSource =
        "Texture2D tex0 : register(t0);\n"
        "SamplerState samp0 : register(s0);\n"
        "struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; };\n"
        "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
        "PSIn VSMain(VSIn input) {\n"
        "  PSIn output;\n"
        "  output.pos = float4(input.pos, 1.0);\n"
        "  output.uv = input.uv;\n"
        "  return output;\n"
        "}\n"
        "float4 PSMain(PSIn input) : SV_Target {\n"
        "  return tex0.Sample(samp0, input.uv);\n"
        "}\n";

    ID3DBlob* vs = nullptr;
    ID3DBlob* ps = nullptr;
    if (!AuroraD3DCompileShader(shaderSource, "VSMain", "vs_4_0", &vs) ||
        !AuroraD3DCompileShader(shaderSource, "PSMain", "ps_4_0", &ps)) {
        AuroraSafeRelease(vs);
        AuroraSafeRelease(ps);
        return false;
    }

    HRESULT hr = g_auroraD3DDevice->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_auroraD3DVertexShader);
    if (FAILED(hr)) {
        Log("[ffx-hooks] WARN AuroraD3D CreateVertexShader failed hr=0x%08X\n", static_cast<unsigned>(hr));
        AuroraSafeRelease(vs);
        AuroraSafeRelease(ps);
        return false;
    }
    hr = g_auroraD3DDevice->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &g_auroraD3DPixelShader);
    if (FAILED(hr)) {
        Log("[ffx-hooks] WARN AuroraD3D CreatePixelShader failed hr=0x%08X\n", static_cast<unsigned>(hr));
        AuroraSafeRelease(vs);
        AuroraSafeRelease(ps);
        return false;
    }

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    hr = g_auroraD3DDevice->CreateInputLayout(layout, 2, vs->GetBufferPointer(), vs->GetBufferSize(), &g_auroraD3DInputLayout);
    AuroraSafeRelease(vs);
    AuroraSafeRelease(ps);
    if (FAILED(hr)) {
        Log("[ffx-hooks] WARN AuroraD3D CreateInputLayout failed hr=0x%08X\n", static_cast<unsigned>(hr));
        return false;
    }

    AuroraD3DVertex quad[] = {
        { -1.0f,  1.0f, 0.0f, 0.0f, 0.0f },
        {  1.0f,  1.0f, 0.0f, 1.0f, 0.0f },
        { -1.0f, -1.0f, 0.0f, 0.0f, 1.0f },
        { -1.0f, -1.0f, 0.0f, 0.0f, 1.0f },
        {  1.0f,  1.0f, 0.0f, 1.0f, 0.0f },
        {  1.0f, -1.0f, 0.0f, 1.0f, 1.0f }
    };
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(quad);
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData = {};
    vbData.pSysMem = quad;
    hr = g_auroraD3DDevice->CreateBuffer(&vbDesc, &vbData, &g_auroraD3DVertexBuffer);
    if (FAILED(hr)) {
        Log("[ffx-hooks] WARN AuroraD3D CreateBuffer failed hr=0x%08X\n", static_cast<unsigned>(hr));
        return false;
    }

    D3D11_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    hr = g_auroraD3DDevice->CreateSamplerState(&sampler, &g_auroraD3DSampler);
    if (FAILED(hr)) return false;

    D3D11_BLEND_DESC blend = {};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = g_auroraD3DDevice->CreateBlendState(&blend, &g_auroraD3DBlend);
    if (FAILED(hr)) return false;

    D3D11_RASTERIZER_DESC raster = {};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = FALSE;
    hr = g_auroraD3DDevice->CreateRasterizerState(&raster, &g_auroraD3DRaster);
    if (FAILED(hr)) return false;

    D3D11_DEPTH_STENCIL_DESC depth = {};
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    hr = g_auroraD3DDevice->CreateDepthStencilState(&depth, &g_auroraD3DDepth);
    return SUCCEEDED(hr);
}

static bool AuroraD3DEnsureResources(IDXGISwapChain* swapChain) {
    if (!swapChain) return false;

    DXGI_SWAP_CHAIN_DESC sd = {};
    if (FAILED(swapChain->GetDesc(&sd))) return false;
    UINT width = sd.BufferDesc.Width;
    UINT height = sd.BufferDesc.Height;
    if ((width == 0 || height == 0) && sd.OutputWindow) {
        RECT cr = {};
        if (GetClientRect(sd.OutputWindow, &cr)) {
            width = static_cast<UINT>(cr.right - cr.left);
            height = static_cast<UINT>(cr.bottom - cr.top);
        }
    }
    if (width == 0 || height == 0) return false;
    InGameMenuInstallWndProc(sd.OutputWindow);
    FfxHooks::NativePorts::BindWindow(sd.OutputWindow);

    if (g_auroraD3DSwapChain == swapChain &&
        g_auroraD3DWidth == width &&
        g_auroraD3DHeight == height &&
        g_auroraD3DTexture && g_auroraD3DSrv && g_auroraD3DRtv) {
        return true;
    }

    AuroraD3DReleaseResources();

    g_auroraD3DSwapChain = swapChain;
    g_auroraD3DSwapChain->AddRef();
    HRESULT hr = swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_auroraD3DDevice));
    if (FAILED(hr) || !g_auroraD3DDevice) {
        Log("[ffx-hooks] WARN AuroraD3D GetDevice failed hr=0x%08X\n", static_cast<unsigned>(hr));
        AuroraD3DReleaseResources();
        return false;
    }
    g_auroraD3DDevice->GetImmediateContext(&g_auroraD3DContext);
    if (!g_auroraD3DContext) {
        AuroraD3DReleaseResources();
        return false;
    }
    AuroraD3DInstallContextSniffer(g_auroraD3DContext);

    ID3D11Texture2D* backBuffer = nullptr;
    hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
    if (FAILED(hr) || !backBuffer) {
        Log("[ffx-hooks] WARN AuroraD3D GetBuffer failed hr=0x%08X\n", static_cast<unsigned>(hr));
        AuroraD3DReleaseResources();
        return false;
    }
    hr = g_auroraD3DDevice->CreateRenderTargetView(backBuffer, nullptr, &g_auroraD3DRtv);
    AuroraSafeRelease(backBuffer);
    if (FAILED(hr)) {
        Log("[ffx-hooks] WARN AuroraD3D CreateRenderTargetView failed hr=0x%08X\n", static_cast<unsigned>(hr));
        AuroraD3DReleaseResources();
        return false;
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DYNAMIC;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = g_auroraD3DDevice->CreateTexture2D(&texDesc, nullptr, &g_auroraD3DTexture);
    if (FAILED(hr)) {
        Log("[ffx-hooks] WARN AuroraD3D CreateTexture2D failed hr=0x%08X\n", static_cast<unsigned>(hr));
        AuroraD3DReleaseResources();
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    hr = g_auroraD3DDevice->CreateShaderResourceView(g_auroraD3DTexture, &srvDesc, &g_auroraD3DSrv);
    if (FAILED(hr)) {
        Log("[ffx-hooks] WARN AuroraD3D CreateShaderResourceView failed hr=0x%08X\n", static_cast<unsigned>(hr));
        AuroraD3DReleaseResources();
        return false;
    }

    g_auroraD3DMemDc = CreateCompatibleDC(NULL);
    if (!g_auroraD3DMemDc) {
        AuroraD3DReleaseResources();
        return false;
    }
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(width);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(height);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    g_auroraD3DDib = CreateDIBSection(g_auroraD3DMemDc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!g_auroraD3DDib || !bits) {
        AuroraD3DReleaseResources();
        return false;
    }
    g_auroraD3DOldDib = SelectObject(g_auroraD3DMemDc, g_auroraD3DDib);
    g_auroraD3DPixels = static_cast<uint32_t*>(bits);

    if (!AuroraD3DCreateShadersAndStates()) {
        AuroraD3DReleaseResources();
        return false;
    }

    g_auroraD3DWidth = width;
    g_auroraD3DHeight = height;
    Log("[ffx-hooks] AuroraD3D resources ready %ux%u\n",
        static_cast<unsigned>(width), static_cast<unsigned>(height));
    return true;
}

static bool AuroraD3DUpdateTexture() {
    if (!g_auroraD3DContext || !g_auroraD3DTexture || !g_auroraD3DMemDc || !g_auroraD3DPixels ||
        g_auroraD3DWidth == 0 || g_auroraD3DHeight == 0) {
        return false;
    }

    const size_t pixelCount = static_cast<size_t>(g_auroraD3DWidth) * static_cast<size_t>(g_auroraD3DHeight);
    memset(g_auroraD3DPixels, 0, pixelCount * sizeof(uint32_t));
    RECT rc = { 0, 0, static_cast<LONG>(g_auroraD3DWidth), static_cast<LONG>(g_auroraD3DHeight) };
    if (InterlockedCompareExchange(&g_auroraActorOverlayEnabled, 1, 1) &&
        InterlockedCompareExchange(&g_auroraOverlayVisible, 1, 1)) {
        AuroraDrawOverlayGdiContent(g_auroraD3DMemDc, rc, false);
    } else {
        AuroraResetLabelStats();
    }
    InGameMenuDraw(g_auroraD3DMemDc, rc);
    InGameDrawSpeedHackIndicator(g_auroraD3DMemDc, rc);
    InGameDrawNativePerformance(g_auroraD3DMemDc,rc);
    GdiFlush();

    for (size_t i = 0; i < pixelCount; ++i) {
        if ((g_auroraD3DPixels[i] & 0x00FFFFFFu) != 0) {
            g_auroraD3DPixels[i] |= 0xFF000000u;
        }
    }

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hr = g_auroraD3DContext->Map(g_auroraD3DTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return false;

    const BYTE* src = reinterpret_cast<const BYTE*>(g_auroraD3DPixels);
    BYTE* dst = reinterpret_cast<BYTE*>(mapped.pData);
    const UINT rowBytes = g_auroraD3DWidth * 4;
    for (UINT y = 0; y < g_auroraD3DHeight; ++y) {
        memcpy(dst + static_cast<size_t>(mapped.RowPitch) * y, src + static_cast<size_t>(rowBytes) * y, rowBytes);
    }
    g_auroraD3DContext->Unmap(g_auroraD3DTexture, 0);
    return true;
}

struct AuroraD3DBackup {
    ID3D11InputLayout* inputLayout;
    ID3D11Buffer* vertexBuffer;
    UINT vertexStride;
    UINT vertexOffset;
    D3D11_PRIMITIVE_TOPOLOGY topology;
    ID3D11VertexShader* vertexShader;
    ID3D11PixelShader* pixelShader;
    ID3D11ShaderResourceView* psSrv;
    ID3D11SamplerState* psSampler;
    ID3D11BlendState* blend;
    FLOAT blendFactor[4];
    UINT sampleMask;
    ID3D11DepthStencilState* depth;
    UINT stencilRef;
    ID3D11RasterizerState* raster;
    ID3D11RenderTargetView* rtv;
    ID3D11DepthStencilView* dsv;
    D3D11_VIEWPORT viewports[16];
    UINT viewportCount;
};

static void AuroraD3DReleaseBackup(AuroraD3DBackup& b) {
    AuroraSafeRelease(b.inputLayout);
    AuroraSafeRelease(b.vertexBuffer);
    AuroraSafeRelease(b.vertexShader);
    AuroraSafeRelease(b.pixelShader);
    AuroraSafeRelease(b.psSrv);
    AuroraSafeRelease(b.psSampler);
    AuroraSafeRelease(b.blend);
    AuroraSafeRelease(b.depth);
    AuroraSafeRelease(b.raster);
    AuroraSafeRelease(b.rtv);
    AuroraSafeRelease(b.dsv);
}

static void AuroraD3DDrawQuad() {
    if (!g_auroraD3DContext || !g_auroraD3DRtv || !g_auroraD3DSrv) return;

    AuroraD3DBackup b = {};
    b.viewportCount = 16;
    g_auroraD3DContext->IAGetInputLayout(&b.inputLayout);
    g_auroraD3DContext->IAGetVertexBuffers(0, 1, &b.vertexBuffer, &b.vertexStride, &b.vertexOffset);
    g_auroraD3DContext->IAGetPrimitiveTopology(&b.topology);
    g_auroraD3DContext->VSGetShader(&b.vertexShader, nullptr, nullptr);
    g_auroraD3DContext->PSGetShader(&b.pixelShader, nullptr, nullptr);
    g_auroraD3DContext->PSGetShaderResources(0, 1, &b.psSrv);
    g_auroraD3DContext->PSGetSamplers(0, 1, &b.psSampler);
    g_auroraD3DContext->OMGetBlendState(&b.blend, b.blendFactor, &b.sampleMask);
    g_auroraD3DContext->OMGetDepthStencilState(&b.depth, &b.stencilRef);
    g_auroraD3DContext->RSGetState(&b.raster);
    g_auroraD3DContext->OMGetRenderTargets(1, &b.rtv, &b.dsv);
    g_auroraD3DContext->RSGetViewports(&b.viewportCount, b.viewports);

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(g_auroraD3DWidth);
    vp.Height = static_cast<float>(g_auroraD3DHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    UINT stride = sizeof(AuroraD3DVertex);
    UINT offset = 0;
    FLOAT blendFactor[4] = { 0, 0, 0, 0 };
    g_auroraD3DContext->IASetInputLayout(g_auroraD3DInputLayout);
    g_auroraD3DContext->IASetVertexBuffers(0, 1, &g_auroraD3DVertexBuffer, &stride, &offset);
    g_auroraD3DContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_auroraD3DContext->VSSetShader(g_auroraD3DVertexShader, nullptr, 0);
    g_auroraD3DContext->PSSetShader(g_auroraD3DPixelShader, nullptr, 0);
    g_auroraD3DContext->PSSetShaderResources(0, 1, &g_auroraD3DSrv);
    g_auroraD3DContext->PSSetSamplers(0, 1, &g_auroraD3DSampler);
    g_auroraD3DContext->OMSetRenderTargets(1, &g_auroraD3DRtv, nullptr);
    g_auroraD3DContext->OMSetBlendState(g_auroraD3DBlend, blendFactor, 0xFFFFFFFFu);
    g_auroraD3DContext->OMSetDepthStencilState(g_auroraD3DDepth, 0);
    g_auroraD3DContext->RSSetState(g_auroraD3DRaster);
    g_auroraD3DContext->RSSetViewports(1, &vp);
    g_auroraD3DContext->Draw(6, 0);

    g_auroraD3DContext->IASetInputLayout(b.inputLayout);
    g_auroraD3DContext->IASetVertexBuffers(0, 1, &b.vertexBuffer, &b.vertexStride, &b.vertexOffset);
    g_auroraD3DContext->IASetPrimitiveTopology(b.topology);
    g_auroraD3DContext->VSSetShader(b.vertexShader, nullptr, 0);
    g_auroraD3DContext->PSSetShader(b.pixelShader, nullptr, 0);
    g_auroraD3DContext->PSSetShaderResources(0, 1, &b.psSrv);
    g_auroraD3DContext->PSSetSamplers(0, 1, &b.psSampler);
    g_auroraD3DContext->OMSetRenderTargets(1, &b.rtv, b.dsv);
    g_auroraD3DContext->OMSetBlendState(b.blend, b.blendFactor, b.sampleMask);
    g_auroraD3DContext->OMSetDepthStencilState(b.depth, b.stencilRef);
    g_auroraD3DContext->RSSetState(b.raster);
    if (b.viewportCount > 0) {
        g_auroraD3DContext->RSSetViewports(b.viewportCount, b.viewports);
    }
    AuroraD3DReleaseBackup(b);
}

#ifdef FFXHOOKS_HAVE_POLYHOOK
static void NativeMenu_PresentTick();   // F7/force-gate adapter; runs every Present, including field gameplay.
#endif
static void AuroraD3DRender(IDXGISwapChain* swapChain) {
    if (!swapChain) return;
    // Input consumers need the native window even when every overlay is hidden.
    // Reuse the existing subclass instead of making input depend on an OSD draw.
    if (!g_ingameMenuInputHwnd || !IsWindow(g_ingameMenuInputHwnd) || !g_ingameMenuOriginalWndProc) {
        DXGI_SWAP_CHAIN_DESC windowDesc{};
        if (SUCCEEDED(swapChain->GetDesc(&windowDesc)) && windowDesc.OutputWindow)
            InGameMenuInstallWndProc(windowDesc.OutputWindow);
    }
    if (g_ingameMenuInputHwnd && g_ingameMenuOriginalWndProc)
        FfxHooks::NativePorts::BindWindow(g_ingameMenuInputHwnd);
    // Present admits producer work every frame; the runtime enforces its 33 ms cadence.
    // Keep it ahead of render/menu early-outs so OFF restoration cannot be starved.
    FfxHooks::UnXBoosterFrameTick(GetTickCount());
#ifdef FFXHOOKS_HAVE_POLYHOOK
    // Aurora owns this coordinate override. Run it after the actor update as required by the
    // PhotoMode contract; live thread/order confirmation remains RT2-pending.
    if (PhotoMode::g_pm.on) { __try { PhotoMode::Tick(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    // Force gate: F7 rewrites dword_13407E4=1 each frame so the pump can run in the field.
    __try { NativeMenu_PresentTick(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
#endif
    const bool menuOpen = InGameMenuHandleInput();
    if (AuroraDeveloperHotkeyPressed(VK_F9)) {
        const LONG visible = InterlockedCompareExchange(&g_auroraOverlayVisible, 0, 0) ? 0 : 1;
        InterlockedExchange(&g_auroraOverlayVisible, visible);
        InterlockedExchange(&g_auroraActorOverlayEnabled, visible);
        Log("[ffx-hooks] AuroraD3D visible=%d\n", visible ? 1 : 0);
    }
    if (AuroraDeveloperHotkeyPressed(VK_F10)) {
        const LONG detail = InterlockedCompareExchange(&g_auroraOverlayDetail, 0, 0) ? 0 : 1;
        InterlockedExchange(&g_auroraOverlayDetail, detail);
        Log("[ffx-hooks] AuroraD3D detail=%d\n", detail ? 1 : 0);
    }
    const bool actorVisible =
        InterlockedCompareExchange(&g_auroraActorOverlayEnabled, 1, 1) &&
        InterlockedCompareExchange(&g_auroraOverlayVisible, 1, 1);
    const FfxHooks::SpeedHackRuntimeSnapshot speedSnapshot =
        FfxHooks::GetSpeedHackRuntimeSnapshot();
    const FfxHooks::SpeedHackIndicatorState speedIndicator =
        FfxHooks::ResolveSpeedHackIndicator(
            FfxHooks::Config::GetBool("boosters.speed_hack", false),
            speedSnapshot);
    const bool speedIndicatorVisible =
        speedIndicator.mode != FfxHooks::SpeedHackIndicatorMode::Hidden;
    if (!FfxHooks::NativePorts::CurrentSettings().performance) {
        if (!actorVisible && !menuOpen && !speedIndicatorVisible) return;
    }

    if (!AuroraD3DEnsureResources(swapChain)) return;
    const DWORD textureNow = GetTickCount();
    const DWORD elapsed = textureNow - g_auroraD3DLastTextureUpdateTick;
    const bool shouldUpdateTexture =
        !g_auroraD3DTexturePrimed ||
        g_auroraD3DUpdateIntervalMs <= 0 ||
        elapsed >= static_cast<DWORD>(g_auroraD3DUpdateIntervalMs);
    if (shouldUpdateTexture) {
        if (!AuroraD3DUpdateTexture()) return;
        g_auroraD3DLastTextureUpdateTick = textureNow;
        g_auroraD3DTexturePrimed = true;
    }
    AuroraD3DDrawQuad();

    const DWORD now = GetTickCount();
    if (now - g_auroraD3DLastLogTick > 3000) {
        g_auroraD3DLastLogTick = now;
        Log("[ffx-hooks] AuroraD3D frame rendered labels=%u P=%u M=%u N=%u O=%u X=%u actors=%u size=%ux%u updateMs=%d\n",
            static_cast<unsigned>(g_auroraLastLabelCount),
            static_cast<unsigned>(g_auroraLastPartyLabelCount),
            static_cast<unsigned>(g_auroraLastMonsterLabelCount),
            static_cast<unsigned>(g_auroraLastNpcLabelCount),
            static_cast<unsigned>(g_auroraLastObjectLabelCount),
            static_cast<unsigned>(g_auroraLastOtherLabelCount),
            static_cast<unsigned>(g_auroraLastActorCount),
            static_cast<unsigned>(g_auroraD3DWidth),
            static_cast<unsigned>(g_auroraD3DHeight),
            g_auroraD3DUpdateIntervalMs);
    }
    AuroraD3DLogW2SSniffStatsIfDue(now);
}

static HRESULT STDMETHODCALLTYPE AuroraD3DPresentShim(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    FpsScoutOnPresent(syncInterval, flags);
    // Input, native ownership, configuration, and logging stay on Present. The global callback
    // consumes only the packed 8x route and never samples input on a game-owned thread.
    FfxHooks::SpeedHackFrameTick();
    FfxHooks::SeymourBattlePresentTick();
    const FfxHooks::SpeedHackRuntimeSnapshot speedRuntime =
        FfxHooks::GetSpeedHackRuntimeSnapshot();
    const bool speedBaseReady = speedRuntime.nativeStateReady &&
        speedRuntime.nativeAvailabilityReady && speedRuntime.globalTickHookReady;
    if (speedBaseReady) {
        PublishResolvedF8Status(
            "boosters.speed_hack",
            speedRuntime.phase == FfxHooks::SpeedHackRuntimePhase::Conflict
                ? FfxHooks::F8RuntimeAvailability::Conflict
                : speedRuntime.phase == FfxHooks::SpeedHackRuntimePhase::Unavailable
                    ? FfxHooks::F8RuntimeAvailability::ProducerUnavailable
                : FfxHooks::F8RuntimeAvailability::Available,
            speedRuntime.phase != FfxHooks::SpeedHackRuntimePhase::Conflict &&
                speedRuntime.phase != FfxHooks::SpeedHackRuntimePhase::Unavailable);
    }
    if (InterlockedCompareExchange(&g_auroraD3DRenderEnabled, 1, 1) == 1 &&
        InterlockedCompareExchange(&g_auroraD3DInPresent, 1, 0) == 0) {
        __try {
            AuroraD3DRender(swapChain);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("[ffx-hooks] WARN AuroraD3D exception while rendering\n");
        }
        InterlockedExchange(&g_auroraD3DInPresent, 0);
    }
    return reinterpret_cast<AuroraPresentFn>(g_auroraD3DPresentTrampoline)(swapChain, syncInterval, flags);
}

static bool AuroraD3DPresentReady() {
    return FfxHooks::F8Runtime::ReadPresentHookPhysicalState(
               &g_auroraD3DPresentHookArbiter) ==
           FfxHooks::F8Runtime::PresentHookPhysicalState::Ready;
}

static void TryInstallMaechenWhenReady() {
    // WHY: CreateDevice/fallback arming is not a usable Present producer. Either
    // publication may arrive first, so both sides retry this once-only CAS gate.
    if (InterlockedCompareExchange(&g_maechenConfigEnabledPublished, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_maechenNativePumpReadyPublished, 0, 0) == 0 ||
        !AuroraD3DPresentReady() ||
        InterlockedCompareExchange(&g_auroraD3DPresentTerminal, 0, 0) != 0) {
        return;
    }
    if (InterlockedCompareExchange(&g_maechenInstallState, 1, 0) != 0) return;
    if (FfxHooks::Maechen_Install(LogLine)) {
        InterlockedExchange(&g_maechenInstallState, 2);
        Log("[ffx-hooks] Maechen armed (plain F9; Present and native pump ready)\n");
    } else {
        InterlockedExchange(&g_maechenInstallState, 3);
        Log("[ffx-hooks] WARN Maechen install failed closed\n");
    }
}

// Real-swapchain and late-fallback installers may race; only arbiter results may publish
// Ready or the once-only Terminal state to the UnX lifecycle.
static void PublishAuroraD3DPresentResult(
    FfxHooks::F8Runtime::PresentHookResult result) {
    if (result == FfxHooks::F8Runtime::PresentHookResult::Ready) {
        FfxHooks::NotifyUnXBoosterPresentProducer(true, false);
        FfxHooks::NotifySeymourBattlePresentProducer(true, false);
        TryInstallMaechenWhenReady();
    } else if (result == FfxHooks::F8Runtime::PresentHookResult::PublishTerminal) {
        InterlockedExchange(&g_auroraD3DPresentTerminal, 1);
        FfxHooks::NotifyUnXBoosterPresentProducer(false, true);
        FfxHooks::NotifySeymourBattlePresentProducer(false, true);
        // Speed and Dialog are Present-owned producers even when the visible dashboard is
        // disabled. A terminal Present failure closes both consumers and corrects their F8
        // availability instead of leaving a LIVE row that can no longer observe configuration.
        FfxHooks::RequestSpeedHackStop();
        FfxHooks::RequestDialogSkipStop();
        PublishResolvedF8Status(
            "input.dialog_skip",
            FfxHooks::F8RuntimeAvailability::ProducerUnavailable,
            false);
        PublishResolvedF8Status(
            "boosters.speed_hack",
            FfxHooks::F8RuntimeAvailability::ProducerUnavailable,
            false);
    }
}

static bool AuroraD3DHookPresentFromVtable(void** vtable, const char* reason) {
    if (!vtable || !vtable[8]) {
        Log("[ffx-hooks] WARN AuroraD3D swapchain vtable missing\n");
        return false;
    }

    if (!FfxHooks::F8Runtime::TryBeginPresentHookInstall(
            &g_auroraD3DPresentHookArbiter)) {
        return AuroraD3DPresentReady();
    }

    const uint64_t presentVa = reinterpret_cast<uint64_t>(vtable[8]);
    const uint64_t shimVa = reinterpret_cast<uint64_t>(AuroraD3DPresentShim);
    Log("[ffx-hooks] AuroraD3D installing Present hook from real swapchain VA=0x%08X reason=%s\n",
        static_cast<unsigned>(presentVa), reason ? reason : "unknown");

    try {
        g_auroraD3DPresentDetour = new PLH::x86Detour(presentVa, shimVa, &g_auroraD3DPresentTrampoline);
        const bool hooked = g_auroraD3DPresentDetour->hook();
        Log("[ffx-hooks] AuroraD3D Present hook result ok=%d trampoline=0x%llX\n",
            hooked ? 1 : 0,
            static_cast<unsigned long long>(g_auroraD3DPresentTrampoline));
        if (!hooked) {
            delete g_auroraD3DPresentDetour;
            g_auroraD3DPresentDetour = nullptr;
            g_auroraD3DPresentTrampoline = 0;
        }
        const FfxHooks::F8Runtime::PresentHookResult result =
            FfxHooks::F8Runtime::CompletePresentHookInstall(
                &g_auroraD3DPresentHookArbiter, hooked);
        PublishAuroraD3DPresentResult(result);
    } catch (const std::exception& ex) {
        Log("[ffx-hooks] ERROR AuroraD3D Present hook exception: %s\n", ex.what());
        delete g_auroraD3DPresentDetour;
        g_auroraD3DPresentDetour = nullptr;
        g_auroraD3DPresentTrampoline = 0;
        const FfxHooks::F8Runtime::PresentHookResult result =
            FfxHooks::F8Runtime::CompletePresentHookInstall(
                &g_auroraD3DPresentHookArbiter, false);
        PublishAuroraD3DPresentResult(result);
    } catch (...) {
        Log("[ffx-hooks] ERROR AuroraD3D Present hook unknown exception\n");
        delete g_auroraD3DPresentDetour;
        g_auroraD3DPresentDetour = nullptr;
        g_auroraD3DPresentTrampoline = 0;
        const FfxHooks::F8Runtime::PresentHookResult result =
            FfxHooks::F8Runtime::CompletePresentHookInstall(
                &g_auroraD3DPresentHookArbiter, false);
        PublishAuroraD3DPresentResult(result);
    }
    return AuroraD3DPresentReady();
}

static bool AuroraD3DHookPresentFromSwapChain(IDXGISwapChain* swapChain, const char* reason) {
    if (!swapChain) return false;
    void** vtable = *reinterpret_cast<void***>(swapChain);
    return AuroraD3DHookPresentFromVtable(vtable, reason);
}

static bool TryPublishAuroraD3DPresentTerminal() {
    const FfxHooks::F8Runtime::PresentHookResult result =
        FfxHooks::F8Runtime::RequestPresentHookTerminal(
            &g_auroraD3DPresentHookArbiter);
    PublishAuroraD3DPresentResult(result);
    return result == FfxHooks::F8Runtime::PresentHookResult::PublishTerminal;
}

static HRESULT WINAPI AuroraD3D11CreateDeviceAndSwapChainShim(
    IDXGIAdapter* adapter,
    D3D_DRIVER_TYPE driverType,
    HMODULE software,
    UINT flags,
    const D3D_FEATURE_LEVEL* featureLevels,
    UINT featureLevelsCount,
    UINT sdkVersion,
    const DXGI_SWAP_CHAIN_DESC* swapChainDesc,
    IDXGISwapChain** swapChain,
    ID3D11Device** device,
    D3D_FEATURE_LEVEL* featureLevel,
    ID3D11DeviceContext** immediateContext) {
    AuroraD3D11CreateDeviceAndSwapChainFn original =
        reinterpret_cast<AuroraD3D11CreateDeviceAndSwapChainFn>(g_auroraD3DCreateTrampoline);
    if (!original) {
        return E_FAIL;
    }

    const HRESULT hr = original(
        adapter,
        driverType,
        software,
        flags,
        featureLevels,
        featureLevelsCount,
        sdkVersion,
        swapChainDesc,
        swapChain,
        device,
        featureLevel,
        immediateContext);
    if (SUCCEEDED(hr) && swapChain && *swapChain) {
        AuroraD3DHookPresentFromSwapChain(*swapChain, "D3D11CreateDeviceAndSwapChain");
    } else {
        Log("[ffx-hooks] AuroraD3D CreateDeviceAndSwapChain hr=0x%08X swap=%p\n",
            static_cast<unsigned>(hr), swapChain ? *swapChain : nullptr);
    }
    return hr;
}

static bool AuroraD3DCreateDummySwapChain(void*** outVtable) {
    if (!outVtable) return false;
    *outVtable = nullptr;

    WNDCLASSA wc = {};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "JarvisFfxAuroraD3DProbeWindow";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "Aurora D3D probe",
        WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return false;

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 64;
    sd.BufferDesc.Height = 64;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    IDXGISwapChain* swap = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL selected = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        levels,
        3,
        D3D11_SDK_VERSION,
        &sd,
        &swap,
        &device,
        &selected,
        &context);
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            levels,
            3,
            D3D11_SDK_VERSION,
            &sd,
            &swap,
            &device,
            &selected,
            &context);
    }

    bool ok = false;
    if (SUCCEEDED(hr) && swap) {
        *outVtable = *reinterpret_cast<void***>(swap);
        ok = *outVtable != nullptr;
    } else {
        Log("[ffx-hooks] WARN AuroraD3D dummy swapchain failed hr=0x%08X\n", static_cast<unsigned>(hr));
    }
    AuroraSafeRelease(context);
    AuroraSafeRelease(device);
    AuroraSafeRelease(swap);
    DestroyWindow(hwnd);
    return ok;
}

static DWORD WINAPI AuroraD3DLatePresentFallbackThreadProc(LPVOID) {
    int delayMs = EnvInt("FFXHOOKS_AURORA_D3D_FALLBACK_DELAY_MS", 15000);
    if (delayMs < 0) delayMs = 0;
    if (delayMs > 120000) delayMs = 120000;
    Log("[ffx-hooks] AuroraD3D late Present fallback armed delay=%dms\n", delayMs);

    DWORD waited = 0;
    while (InterlockedCompareExchange(&g_auroraD3DFallbackRunning, 1, 1) == 1 &&
           waited < static_cast<DWORD>(delayMs) &&
           !AuroraD3DPresentReady()) {
        Sleep(250);
        waited += 250;
    }

    bool attemptedPresentHook = false;
    if (InterlockedCompareExchange(&g_auroraD3DFallbackRunning, 1, 1) == 1 &&
        !AuroraD3DPresentReady()) {
        attemptedPresentHook = true;
        void** vtable = nullptr;
        Log("[ffx-hooks] AuroraD3D late Present fallback probing dummy swapchain\n");
        if (AuroraD3DCreateDummySwapChain(&vtable) && vtable) {
            AuroraD3DHookPresentFromVtable(vtable, "late-dummy-swapchain");
        } else {
            Log("[ffx-hooks] WARN AuroraD3D late Present fallback could not create dummy swapchain\n");
        }
    }

    if (attemptedPresentHook) {
        TryPublishAuroraD3DPresentTerminal();
    }

    InterlockedExchange(&g_auroraD3DFallbackRunning, 0);
    g_auroraD3DFallbackThread = NULL;
    return 0;
}

static bool StartAuroraD3DLatePresentFallback() {
    if (InterlockedCompareExchange(&g_auroraD3DFallbackRunning, 1, 0) != 0) {
        return true;
    }

    DWORD tid = 0;
    g_auroraD3DFallbackThread = CreateThread(
        nullptr, 0, AuroraD3DLatePresentFallbackThreadProc, nullptr, 0, &tid);
    if (g_auroraD3DFallbackThread) {
        Log("[ffx-hooks] AuroraD3D late Present fallback thread created tid=%u\n",
            static_cast<unsigned>(tid));
        CloseHandle(g_auroraD3DFallbackThread);
        return true;
    } else {
        InterlockedExchange(&g_auroraD3DFallbackRunning, 0);
        Log("[ffx-hooks] WARN AuroraD3D late Present fallback thread create failed (err=%u)\n",
            GetLastError());
        // This ends UnX once but leaves the shared physical Present state installable for
        // a later real CreateDevice path used by Aurora/F7.
        TryPublishAuroraD3DPresentTerminal();
        return false;
    }
}

static bool InstallAuroraD3D11Overlay() {
    if (g_auroraD3DCreateDetour) {
        return g_auroraD3DCreateHooked;
    }

    HMODULE d3d11 = GetModuleHandleA("d3d11.dll");
    if (!d3d11) {
        d3d11 = LoadLibraryA("d3d11.dll");
    }
    if (!d3d11) {
        Log("[ffx-hooks] WARN AuroraD3D d3d11.dll unavailable\n");
        return false;
    }

    FARPROC createProc = GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain");
    if (!createProc) {
        Log("[ffx-hooks] WARN AuroraD3D D3D11CreateDeviceAndSwapChain export missing\n");
        return false;
    }

    const uint64_t createVa = reinterpret_cast<uint64_t>(createProc);
    const uint64_t shimVa = reinterpret_cast<uint64_t>(AuroraD3D11CreateDeviceAndSwapChainShim);
    Log("[ffx-hooks] AuroraD3D installing CreateDeviceAndSwapChain hook VA=0x%08X\n",
        static_cast<unsigned>(createVa));

    try {
        g_auroraD3DCreateDetour = new PLH::x86Detour(createVa, shimVa, &g_auroraD3DCreateTrampoline);
        g_auroraD3DCreateHooked = g_auroraD3DCreateDetour->hook();
        Log("[ffx-hooks] AuroraD3D CreateDeviceAndSwapChain hook result ok=%d trampoline=0x%llX\n",
            g_auroraD3DCreateHooked ? 1 : 0,
            static_cast<unsigned long long>(g_auroraD3DCreateTrampoline));
        if (!g_auroraD3DCreateHooked) {
            delete g_auroraD3DCreateDetour;
            g_auroraD3DCreateDetour = nullptr;
            g_auroraD3DCreateTrampoline = 0;
        }
    } catch (const std::exception& ex) {
        Log("[ffx-hooks] ERROR AuroraD3D CreateDeviceAndSwapChain hook exception: %s\n", ex.what());
        delete g_auroraD3DCreateDetour;
        g_auroraD3DCreateDetour = nullptr;
        g_auroraD3DCreateTrampoline = 0;
        g_auroraD3DCreateHooked = false;
    } catch (...) {
        Log("[ffx-hooks] ERROR AuroraD3D CreateDeviceAndSwapChain hook unknown exception\n");
        delete g_auroraD3DCreateDetour;
        g_auroraD3DCreateDetour = nullptr;
        g_auroraD3DCreateTrampoline = 0;
        g_auroraD3DCreateHooked = false;
    }
    return g_auroraD3DCreateHooked;
}

static void RemoveAuroraD3D11Overlay() {
    FfxHooks::FmvSpeed::Neutralize();FfxHooks::FmvSpeed::RequestStop();
    FfxHooks::SinAi::RequestStop();
    FfxHooks::NativeLanguage::Stop();
    FfxHooks::NativePorts::Stop();
    InGameMenuRestoreWndProc();
    AuroraD3DRemoveContextSniffer();

    if (g_auroraD3DPresentDetour) {
        try {
            if (AuroraD3DPresentReady()) {
                const bool ok = g_auroraD3DPresentDetour->unHook();
                Log("[ffx-hooks] AuroraD3D Present unHook result=%d\n", ok ? 1 : 0);
            }
        } catch (...) {
            Log("[ffx-hooks] WARN AuroraD3D Present unHook exception\n");
        }
        delete g_auroraD3DPresentDetour;
        g_auroraD3DPresentDetour = nullptr;
    }
    g_auroraD3DPresentTrampoline = 0;
    FfxHooks::F8Runtime::ResetPresentHookPhysicalState(
        &g_auroraD3DPresentHookArbiter);
    if (g_auroraD3DCreateDetour) {
        try {
            if (g_auroraD3DCreateHooked) {
                const bool ok = g_auroraD3DCreateDetour->unHook();
                Log("[ffx-hooks] AuroraD3D CreateDeviceAndSwapChain unHook result=%d\n", ok ? 1 : 0);
            }
        } catch (...) {
            Log("[ffx-hooks] WARN AuroraD3D CreateDeviceAndSwapChain unHook exception\n");
        }
        delete g_auroraD3DCreateDetour;
        g_auroraD3DCreateDetour = nullptr;
    }
    g_auroraD3DCreateTrampoline = 0;
    g_auroraD3DCreateHooked = false;
    AuroraD3DReleaseResources();
}

static LRESULT CALLBACK AuroraOverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc = {};
            GetClientRect(hwnd, &rc);
            AuroraPaintOverlay(hwnd, hdc, rc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_CLOSE:
            InterlockedExchange(&g_auroraOverlayVisible, 0);
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            g_auroraOverlayHwnd = NULL;
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static void AuroraSyncOverlayWindow(HWND hwnd) {
    RECT rc = {};
    if (AuroraFindGameClientRect(&rc)) {
        SetWindowPos(hwnd, HWND_TOPMOST, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

static DWORD WINAPI AuroraOverlayThreadProc(LPVOID) {
    HINSTANCE inst = GetModuleHandleA(nullptr);
    WNDCLASSA wc = {};
    wc.lpfnWndProc = AuroraOverlayWndProc;
    wc.hInstance = inst;
    wc.lpszClassName = "JarvisFfxAuroraOverlayWindow";
    wc.hCursor = LoadCursorA(nullptr, (LPCSTR)IDC_ARROW);
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        wc.lpszClassName,
        "Jarvis FFX Aurora Overlay",
        WS_POPUP,
        0, 0, 640, 480,
        nullptr, nullptr, inst, nullptr);
    g_auroraOverlayHwnd = hwnd;
    if (!hwnd) {
        Log("[ffx-hooks] WARN AuroraOverlay window create failed (err=%u)\n", GetLastError());
        InterlockedExchange(&g_auroraOverlayRunning, 0);
        return 0;
    }

    SetLayeredWindowAttributes(hwnd, RGB(1, 1, 1), 255, LWA_COLORKEY);
    AuroraSyncOverlayWindow(hwnd);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);
    Log("[ffx-hooks] AuroraOverlay window ready; Ctrl+Alt+F9 toggles, Ctrl+Alt+F10 detail (Shift off)\n");

    MSG msg = {};
    while (InterlockedCompareExchange(&g_auroraOverlayRunning, 1, 1) == 1) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (AuroraDeveloperHotkeyPressed(VK_F9)) {
            const LONG visible = InterlockedCompareExchange(&g_auroraOverlayVisible, 0, 0) ? 0 : 1;
            InterlockedExchange(&g_auroraOverlayVisible, visible);
            ShowWindow(hwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
            Log("[ffx-hooks] AuroraOverlay visible=%d\n", visible ? 1 : 0);
        }
        if (AuroraDeveloperHotkeyPressed(VK_F10)) {
            const LONG detail = InterlockedCompareExchange(&g_auroraOverlayDetail, 0, 0) ? 0 : 1;
            InterlockedExchange(&g_auroraOverlayDetail, detail);
            Log("[ffx-hooks] AuroraOverlay detail=%d\n", detail ? 1 : 0);
        }
        if (InterlockedCompareExchange(&g_auroraOverlayVisible, 0, 0)) {
            AuroraSyncOverlayWindow(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        Sleep(33);
    }

    if (hwnd) DestroyWindow(hwnd);
    return 0;
}

#ifdef FFXHOOKS_HAVE_POLYHOOK
static bool NativeMenuArmedFromConfig() {
    const bool explicitNativeMenuFlag =
        ModuleFlagEnabled("native_menu.flag") ||
        ModuleFlagEnabled("config\\native_menu.flag");
    const bool fileFlag =
        explicitNativeMenuFlag ||
        ModuleFlagEnabled("f7_inlive.flag") ||          // F7 In-Live also arms the native menu (F7)
        ModuleFlagEnabled("config\\f7_inlive.flag");
    const bool envFlag = EnvFlagEnabled("FFXHOOKS_ENABLE_NATIVE_MENU");
    const bool dashboardEnabled = FfxHooks::Config::GetBool("dashboard.enabled", false);
    const bool maechenEnabled = FfxHooks::Config::GetBool("maechen.enabled", false);
    /* Field Scout walk: env-only NativeMenu + Aurora Present hook soft-locks titl00 boot.
     * Require the explicit native_menu.flag itself when field_scout* is armed. Neither the
     * dashboard setting, the environment, nor f7_inlive.flag may bypass this guard. */
    if (FieldScoutFlagEnabled() || FieldScoutMapOnlyFlagEnabled()) {
        return explicitNativeMenuFlag;
    }
    return dashboardEnabled || maechenEnabled || envFlag || fileFlag || F8CatalogGateEnabled("labs.equipment_workshop");
}
#endif

static bool StartAuroraOverlayIfEnabled() {
    const bool envEnabled = EnvFlagEnabled("FFXHOOKS_ENABLE_AURORA_OVERLAY");
    const bool inGameMenuEnabled = InGameMenuConfigEnabled();
#ifdef FFXHOOKS_HAVE_POLYHOOK
    const bool nativeMenuEnabled = NativeMenuArmedFromConfig();
#else
    const bool nativeMenuEnabled = false;
#endif
    const bool auroraOverlayFileEnabled =
        ModuleFlagEnabled("aurora_overlay.flag") ||
        ModuleFlagEnabled("config\\aurora_overlay.flag");
    const bool configExists = AuroraConfigExists();
    const bool configEnabled = configExists && AuroraConfigInt("enabled", 0) != 0;
    char configMode[32] = {};
    const bool configModeSet = configEnabled && AuroraConfigString("mode", configMode, sizeof(configMode));
    const bool d3dFileEnabled =
        ModuleFlagEnabled("aurora_overlay_d3d11.flag") ||
        ModuleFlagEnabled("config\\aurora_overlay_d3d11.flag");
    const bool sniffFileEnabled =
        ModuleFlagEnabled("aurora_w2s_sniff.flag") ||
        ModuleFlagEnabled("config\\aurora_w2s_sniff.flag");
    const bool fileEnabled =
        auroraOverlayFileEnabled ||
        configEnabled ||
        inGameMenuEnabled ||
        nativeMenuEnabled ||
        d3dFileEnabled ||
        sniffFileEnabled;
    // The menu may arm Present, but only an Aurora source may claim its developer controls.
    const bool auroraDeveloperUiEnabled = AuroraDeveloperUiEnabledFromExplicitSources(
        envEnabled, auroraOverlayFileEnabled, configEnabled, d3dFileEnabled, sniffFileEnabled);
    InterlockedExchange(&g_auroraDeveloperUiEnabled, auroraDeveloperUiEnabled ? 1 : 0);
    if (!envEnabled && !fileEnabled) {
        return false;
    }

    char mode[32] = {};
    DWORD modeLen = GetEnvironmentVariableA("FFXHOOKS_AURORA_OVERLAY_MODE", mode, sizeof(mode));
    const bool d3dRequested =
        inGameMenuEnabled ||
        nativeMenuEnabled ||
        d3dFileEnabled ||
        sniffFileEnabled ||
        (configModeSet && (configMode[0] == 'd' || configMode[0] == 'D')) ||
        (modeLen > 0 && modeLen < sizeof(mode) && (mode[0] == 'd' || mode[0] == 'D'));

    if (inGameMenuEnabled) {
        InterlockedExchange(&g_ingameMenuEnabled, 1);
        InterlockedExchange(&g_ingameMenuOpen, InGameMenuStartOpen() ? 1 : 0);
        InGameMenuRefreshPlugins();
        InGameMenuSetStatus("Runtime plugin switchboard ready; F8 opens/closes");
    }

    g_auroraOverlayDetail = SettingInt("FFXHOOKS_AURORA_DETAIL", "detail", 0) ? 1 : 0;
    g_auroraW2SScan = SettingInt("FFXHOOKS_AURORA_W2S_SCAN", "w2s_scan", 1) != 0;
    g_auroraW2SAddress = SettingAddress("FFXHOOKS_AURORA_W2S_ADDR", "w2s_addr", 0);
    g_auroraW2SManual = g_auroraW2SAddress != 0;
    g_auroraW2SScanStartAddress = SettingAddress("FFXHOOKS_AURORA_W2S_SCAN_START", "w2s_scan_start", 0x40000000u);
    g_auroraW2SScanCursor = g_auroraW2SScanStartAddress;
    g_auroraW2SScanBudgetMs = SettingInt("FFXHOOKS_AURORA_W2S_SCAN_BUDGET_MS", "w2s_scan_budget_ms", 2);
    if (g_auroraW2SScanBudgetMs < 1) g_auroraW2SScanBudgetMs = 1;
    if (g_auroraW2SScanBudgetMs > 20) g_auroraW2SScanBudgetMs = 20;
    g_auroraW2SScanCooldownMs = SettingInt("FFXHOOKS_AURORA_W2S_SCAN_COOLDOWN_MS", "w2s_scan_cooldown_ms", 100);
    if (g_auroraW2SScanCooldownMs < 0) g_auroraW2SScanCooldownMs = 0;
    if (g_auroraW2SScanCooldownMs > 5000) g_auroraW2SScanCooldownMs = 5000;
    g_auroraW2SScanMinRoots =
        static_cast<uint32_t>(SettingInt("FFXHOOKS_AURORA_W2S_SCAN_MIN_ROOTS", "w2s_scan_min_roots", AURORA_W2S_SCAN_MIN_ROOTS_DEFAULT));
    if (g_auroraW2SScanMinRoots < 1u) g_auroraW2SScanMinRoots = 1u;
    if (g_auroraW2SScanMinRoots > 32u) g_auroraW2SScanMinRoots = 32u;
    g_auroraW2SScanLastRegionBase = 0;
    g_auroraW2SScanLastRegionEnd = 0;
    g_auroraW2SScanLastProbes = 0;
    g_auroraW2SScanPassCount = 0;
    g_auroraW2SScanLastElapsedMs = 0;
    g_auroraLastScanTick = 0;
    g_auroraLastLogTick = 0;
    g_auroraD3DLastLogTick = 0;
    g_auroraD3DUpdateIntervalMs = SettingInt("FFXHOOKS_AURORA_D3D_UPDATE_MS", "d3d_update_ms", 33);
    if (g_auroraD3DUpdateIntervalMs < 0) g_auroraD3DUpdateIntervalMs = 0;
    if (g_auroraD3DUpdateIntervalMs > 1000) g_auroraD3DUpdateIntervalMs = 1000;
    g_auroraSniffW2SMaxAgeMs = SettingInt("FFXHOOKS_AURORA_D3D_SNIFF_MATRIX_MAX_AGE_MS", "d3d_sniff_matrix_max_age_ms", 3000);
    if (g_auroraSniffW2SMaxAgeMs < 100) g_auroraSniffW2SMaxAgeMs = 100;
    if (g_auroraSniffW2SMaxAgeMs > 10000) g_auroraSniffW2SMaxAgeMs = 10000;
    g_auroraD3DProjectionRefreshMs = SettingInt("FFXHOOKS_AURORA_D3D_SNIFF_PROJECT_REFRESH_MS", "d3d_sniff_project_refresh_ms", 250);
    if (g_auroraD3DProjectionRefreshMs < 0) g_auroraD3DProjectionRefreshMs = 0;
    if (g_auroraD3DProjectionRefreshMs > 5000) g_auroraD3DProjectionRefreshMs = 5000;
    g_auroraD3DSniffAutoPauseHits = SettingInt("FFXHOOKS_AURORA_D3D_SNIFF_AUTOPAUSE_HITS", "d3d_sniff_autopause_hits", 3);
    if (g_auroraD3DSniffAutoPauseHits < 0) g_auroraD3DSniffAutoPauseHits = 0;
    if (g_auroraD3DSniffAutoPauseHits > 100) g_auroraD3DSniffAutoPauseHits = 100;
    g_auroraD3DW2SSniffEnabled =
        sniffFileEnabled || SettingInt("FFXHOOKS_AURORA_D3D_SNIFF_W2S", "d3d_sniff_w2s", 0) != 0;
    g_auroraD3DProjectWhenW2SReady =
        SettingInt("FFXHOOKS_AURORA_D3D_SNIFF_PROJECT_WITH_W2S", "d3d_sniff_project_with_w2s", 0) != 0;
    g_auroraD3DLightSniffAfterW2S =
        SettingInt("FFXHOOKS_AURORA_D3D_SNIFF_LIGHT_AFTER_W2S", "d3d_sniff_light_after_w2s", 1) != 0;
    AuroraD3DResetW2SSniffStats();
    g_auroraD3DTexturePrimed = false;
    g_auroraD3DLastTextureUpdateTick = 0;

    if (d3dRequested) {
        InterlockedExchange(&g_auroraD3DRenderEnabled, 1);
        const bool nativeMenuOnlyPresent =
            nativeMenuEnabled &&
            !envEnabled &&
            !inGameMenuEnabled &&
            !configEnabled &&
            !d3dFileEnabled &&
            !sniffFileEnabled &&
            !ModuleFlagEnabled("aurora_overlay.flag") &&
            !ModuleFlagEnabled("config\\aurora_overlay.flag");
        if (nativeMenuOnlyPresent) {
            InterlockedExchange(&g_auroraActorOverlayEnabled, 0);
            InterlockedExchange(&g_auroraOverlayVisible, 0);
            Log("[ffx-hooks] AuroraD3D: native_menu-only Present hook (actor overlay OFF; F7/F7 menu)\n");
        }
        Log("[ffx-hooks] AuroraOverlay enabled mode=d3d11 detail=%d scan=%d addr=0x%08X manual=%d updateMs=%d scanBudgetMs=%d scanCooldownMs=%d scanMinRoots=%u scanStart=0x%08X sniffW2S=%d sniffMatrixMaxAgeMs=%d sniffProjectRefreshMs=%d sniffAutoPauseHits=%d sniffProjectWithW2S=%d sniffLightAfterW2S=%d\n",
            static_cast<int>(g_auroraOverlayDetail),
            g_auroraW2SScan ? 1 : 0,
            static_cast<unsigned>(g_auroraW2SAddress),
            g_auroraW2SManual ? 1 : 0,
            g_auroraD3DUpdateIntervalMs,
            g_auroraW2SScanBudgetMs,
            g_auroraW2SScanCooldownMs,
            static_cast<unsigned>(g_auroraW2SScanMinRoots),
            static_cast<unsigned>(g_auroraW2SScanStartAddress),
            g_auroraD3DW2SSniffEnabled ? 1 : 0,
            g_auroraSniffW2SMaxAgeMs,
            g_auroraD3DProjectionRefreshMs,
            g_auroraD3DSniffAutoPauseHits,
            g_auroraD3DProjectWhenW2SReady ? 1 : 0,
            g_auroraD3DLightSniffAfterW2S ? 1 : 0);
        Log("[ffx-hooks] AuroraOverlay gate env=%d file=%d config=%d d3dFile=%d sniffFile=%d\n",
            envEnabled ? 1 : 0,
            fileEnabled ? 1 : 0,
            configEnabled ? 1 : 0,
            d3dFileEnabled ? 1 : 0,
            sniffFileEnabled ? 1 : 0);
        if (inGameMenuEnabled) {
            Log("[ffx-hooks] InGameMenu enabled mode=d3d11 open=%d\n",
                InterlockedCompareExchange(&g_ingameMenuOpen, 1, 1) ? 1 : 0);
        }
        if (InstallAuroraD3D11Overlay()) {
            StartAuroraD3DLatePresentFallback();
            return true;
        }
        InterlockedExchange(&g_auroraD3DRenderEnabled, 0);
        Log("[ffx-hooks] WARN AuroraD3D install failed; falling back to GDI overlay window\n");
    }

    if (InterlockedCompareExchange(&g_auroraOverlayRunning, 1, 0) != 0) {
        return false;
    }

    Log("[ffx-hooks] AuroraOverlay enabled mode=gdi detail=%d scan=%d addr=0x%08X manual=%d scanBudgetMs=%d scanCooldownMs=%d scanMinRoots=%u scanStart=0x%08X sniffW2S=%d sniffMatrixMaxAgeMs=%d sniffProjectRefreshMs=%d sniffAutoPauseHits=%d sniffProjectWithW2S=%d sniffLightAfterW2S=%d\n",
        static_cast<int>(g_auroraOverlayDetail),
        g_auroraW2SScan ? 1 : 0,
        static_cast<unsigned>(g_auroraW2SAddress),
        g_auroraW2SManual ? 1 : 0,
        g_auroraW2SScanBudgetMs,
        g_auroraW2SScanCooldownMs,
        static_cast<unsigned>(g_auroraW2SScanMinRoots),
        static_cast<unsigned>(g_auroraW2SScanStartAddress),
        g_auroraD3DW2SSniffEnabled ? 1 : 0,
        g_auroraSniffW2SMaxAgeMs,
        g_auroraD3DProjectionRefreshMs,
        g_auroraD3DSniffAutoPauseHits,
        g_auroraD3DProjectWhenW2SReady ? 1 : 0,
        g_auroraD3DLightSniffAfterW2S ? 1 : 0);
    Log("[ffx-hooks] AuroraOverlay gate env=%d file=%d config=%d d3dFile=%d sniffFile=%d\n",
        envEnabled ? 1 : 0,
        fileEnabled ? 1 : 0,
        configEnabled ? 1 : 0,
        d3dFileEnabled ? 1 : 0,
        sniffFileEnabled ? 1 : 0);

    DWORD tid = 0;
    g_auroraOverlayThread = CreateThread(nullptr, 0, AuroraOverlayThreadProc, nullptr, 0, &tid);
    if (g_auroraOverlayThread) {
        Log("[ffx-hooks] AuroraOverlay thread created tid=%u\n", static_cast<unsigned>(tid));
        CloseHandle(g_auroraOverlayThread);
        g_auroraOverlayThread = NULL;
    } else {
        InterlockedExchange(&g_auroraOverlayRunning, 0);
        Log("[ffx-hooks] WARN AuroraOverlay thread create failed (err=%u)\n", GetLastError());
    }
    return false;
}

static void StopAuroraOverlay() {
    InterlockedExchange(&g_auroraD3DRenderEnabled, 0);
    InterlockedExchange(&g_auroraD3DFallbackRunning, 0);
    RemoveAuroraD3D11Overlay();
    InterlockedExchange(&g_auroraOverlayRunning, 0);
    if (g_auroraOverlayHwnd) {
        PostMessageA(g_auroraOverlayHwnd, WM_CLOSE, 0, 0);
    }
}

/* â”€â”€ Lab menu overlay (experimental, not native FFX UI) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
static HWND          g_labMenuHwnd = NULL;
static HANDLE        g_labMenuThread = NULL;
static volatile LONG g_labMenuRunning = 0;
static volatile LONG g_labMenuVisible = 1;
static int           g_labMenuSelected = 0;
static int           g_labMusicCustomTrack = 28;
static int           g_labLastMusicTrack = -1;
static bool          g_musicHookArmed = false;
static char          g_labMenuStatus[256] = "F6 show/hide; click/double-click; Enter plays; scanner uses Left/Right/PageUp/PageDown";

static const uint32_t RVA_ENCOUNTER_GET_CURRENT_FIELD = 0x0048D600u;
static const uint32_t RVA_ENCOUNTER_GET_SCENE_STATE   = 0x0048C7A0u;
static const uint32_t ENCOUNTER_SCENE_GROUP_OFFSET    = 16u;

struct LabMenuItem {
    const char* label;
};

static const LabMenuItem kLabMenuItems[] = {
    { "Music: Custom FMOD id" },
    { "Music: Scan previous id" },
    { "Music: Scan next id" },
    { "Music: Crisis (FMOD id 28, heard)" },
    { "Music: Battle Theme (FMOD id 16, confirmed)" },
    { "Music: Thunder Plain (FMOD id 29, confirmed)" },
    { "Music: Clear override" },
    { "Force Battle: Current area (field/group live, formation 0)" },
    { "Force Battle: Preset/env (default 2/0/0 Geneaux proof)" },
    { "Hide menu" }
};

static const int kLabMenuCount = sizeof(kLabMenuItems) / sizeof(kLabMenuItems[0]);
static const int kLabMenuCustomTrackIndex = 0;
static const int kLabMenuScanPreviousIndex = 1;
static const int kLabMenuScanNextIndex = 2;

static const char* LabMusicRuntimeName(int track) {
    switch (track) {
        case 10: return "Unwavering Decision";
        case 11: return "Leap In The Dark";
        case 12: return "Enemy Attack";
        case 13: return "The Summoning";
        case 14: return "Macalania Forest";
        case 15: return "Wandering Flame";
        case 16: return "Battle Theme";
        case 17: return "Phantoms";
        case 18: return "Out Of The Frying Pan";
        case 19: return "Mi'ihen Road";
        case 20: return "Wanna Ride The Ciparph";
        case 21: return "Brave Advancement";
        case 22: return "Spira Unplugged";
        case 23: return "The Trials";
        case 24: return "The Blitzers";
        case 25: return "The Deceased Laugh";
        case 26: return "Seymour's Ambition";
        case 27: return "Blitz Off";
        case 28: return "Crisis";
        case 29: return "Thunder Plain";
        case 30: return "Rikku's Theme";
        case 31: return "Underwater Ruins";
        case 32: return "Braska's Daughter";
        case 33: return "Permitted Passage";
        case 34: return "Seymour's Theme";
        case 35: return "Confrontation";
        case 36: return "Guadosalam";
        case 37: return "People Of The North Pole";
        case 38: return "Brass De Chocobo";
        case 39: return "Besaid";
        case 40: return "Blazing Desert";
        case 41: return "The Truth Revealed";
        case 42: return "Seymour Battle";
        case 43: return "Illusion";
        case 44: return "Creep";
        case 45: return "Twilight";
        case 46: return "Inori Effect";
        case 48: return "Prelude";
        case 49: return "Otherworld";
        case 50: return "Decisive Battle";
        case 51: return "Jecht's Theme";
        case 128: return "Sprouting";
        case 129: return "The Splendid Performance";
        case 130: return "Zanarkand";
        case 131: return "Start";
        case 132: return "Djose Temple";
        case 133: return "Travel Company";
        case 134: return "Yuna's Decision";
        case 135: return "Path Of Repentance";
        case 136: return "Yuna's Theme";
        case 137: return "My Father's Murderer";
        case 138: return "Victory Fanfare";
        case 139: return "Silence Before The Storm";
        case 140: return "Game Over";
        case 141: return "Someday The Dream Will End";
        case 142: return "Nostalgia";
        case 143: return "Good Night";
        case 144: return "Hum Of The Fayth";
        case 145: return "Challenge";
        case 146: return "Auron's Theme";
        case 147: return "Luca";
        case 148: return "Wakka's Theme";
        case 149: return "Hum Of The Fayth";
        case 150: return "Song Of Prayer Bahamut";
        case 151: return "Song Of Prayer Anima";
        case 152: return "Song Of Prayer Ixion";
        case 153: return "Song Of Prayer Bodyguard";
        case 154: return "Song Of Prayer Ifrit";
        case 155: return "Song Of Prayer Shiva";
        case 156: return "Hymn Valefor";
        case 157: return "Song Of Prayer Spira";
        case 158: return "Song Of Prayer The Lonzo";
        case 159: return "Song Of Prayer Yunalesca";
        case 160: return "Hum Of The Fayth";
        case 161: return "Tidus's Theme";
        case 162: return "At The End Of The Abyss";
        case 163: return "Oui Are Al Bhed";
        case 164: return "The Advancers";
        case 165: return "Run";
        case 166: return "Raid";
        case 167: return "Marriage Ceremony";
        case 168: return "Tragedy";
        case 169: return "I Can Fly";
        case 170: return "Sea Of Mists";
        case 171: return "Lulu's Theme";
        case 172: return "Darkness";
        case 174: return "Time Of Judgment";
        case 176: return "Summoned Beasts Battle";
        case 177: return "Reception For Great Sage Micah";
        case 178: return "The Temple Band";
        case 179: return "Song Of Prayer Spira";
        case 181: return "Raid";
        default: return nullptr;
    }
}

static void LabMenuSetStatus(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(g_labMenuStatus, sizeof(g_labMenuStatus), _TRUNCATE, fmt, ap);
    va_end(ap);
    Log("[ffx-hooks] LabMenu: %s\n", g_labMenuStatus);
    if (g_labMenuHwnd) {
        InvalidateRect(g_labMenuHwnd, nullptr, TRUE);
    }
}

static bool LabProbeArm(
    uint32_t opcode,
    uint32_t addr,
    uint32_t len,
    uint32_t abi,
    uint32_t a0,
    uint32_t a1,
    uint32_t a2,
    int32_t* ret,
    uint32_t* status,
    uint32_t* err,
    uint8_t* readBuf,
    uint32_t readLen) {
    HANDLE mmf = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, FFXPROBE_MMF_NAME);
    if (!mmf) {
        if (err) *err = GetLastError();
        return false;
    }

    FFXProbeBlock* probe = static_cast<FFXProbeBlock*>(
        MapViewOfFile(mmf, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FFXProbeBlock)));
    if (!probe) {
        if (err) *err = GetLastError();
        CloseHandle(mmf);
        return false;
    }

    bool ok = false;
    __try {
        if (probe->magic == FFXPROBE_MAGIC && probe->hooked) {
            probe->opcode = opcode;
            probe->addr = addr;
            probe->len = len;
            probe->abi = abi;
            probe->arg0 = a0;
            probe->arg1 = a1;
            probe->arg2 = a2;
            probe->status = FFXPROBE_ST_IDLE;
            probe->errCode = 0;
            const uint32_t seq = probe->seq + 1u;
            probe->seq = seq;

            for (int i = 0; i < 400; ++i) {
                if (probe->ackSeq == seq) {
                    break;
                }
                Sleep(5);
            }

            if (probe->ackSeq == seq) {
                if (status) *status = probe->status;
                if (ret) *ret = probe->ret;
                if (err) *err = probe->errCode;
                if (readBuf && readLen > 0 && readLen <= sizeof(probe->buf)) {
                    for (uint32_t i = 0; i < readLen; ++i) {
                        readBuf[i] = probe->buf[i];
                    }
                }
                ok = probe->status == FFXPROBE_ST_OK;
            } else {
                if (status) *status = 0xFFFFFFFFu;
                if (err) *err = 0;
            }
        } else {
            if (status) *status = probe->status;
            if (err) *err = 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (status) *status = FFXPROBE_ST_ERR;
        if (err) *err = GetExceptionCode();
    }

    UnmapViewOfFile(probe);
    CloseHandle(mmf);
    return ok;
}

static bool LabProbeCallRva(uint32_t rvaValue, uint32_t abi, int32_t* ret, uint32_t* status, uint32_t* err) {
    HANDLE mmf = OpenFileMappingA(FILE_MAP_READ, FALSE, FFXPROBE_MMF_NAME);
    if (!mmf) {
        if (err) *err = GetLastError();
        return false;
    }

    FFXProbeBlock* probe = static_cast<FFXProbeBlock*>(
        MapViewOfFile(mmf, FILE_MAP_READ, 0, 0, sizeof(FFXProbeBlock)));
    uint32_t base = 0;
    if (probe) {
        base = probe->moduleBase;
        UnmapViewOfFile(probe);
    }
    CloseHandle(mmf);

    if (!base) {
        if (status) *status = 0;
        if (err) *err = 0;
        return false;
    }

    return LabProbeArm(FFXPROBE_OP_CALL, base + rvaValue, 0, abi, 0, 0, 0,
        ret, status, err, nullptr, 0);
}

static bool LabProbeReadAbsoluteByte(uint32_t addr, uint8_t* value, uint32_t* status, uint32_t* err) {
    uint8_t b = 0;
    int32_t ret = 0;
    const bool ok = LabProbeArm(FFXPROBE_OP_READ, addr, 1, 0, 0, 0, 0,
        &ret, status, err, &b, 1);
    if (value) *value = b;
    return ok;
}

static bool LabProbeSoundCmd(uint32_t cmd, uint32_t param0, uint32_t param1, int32_t* ret, uint32_t* status, uint32_t* err) {
    return LabProbeArm(FFXPROBE_OP_SOUNDCMD, 0, 0, 0, cmd, param0, param1,
        ret, status, err, nullptr, 0);
}

static bool LabProbeForceBattle(uint32_t field, uint32_t group, uint32_t formation, int32_t* ret, uint32_t* status, uint32_t* err) {
    return LabProbeArm(FFXPROBE_OP_FORCEBATTLE, 0, 0, 0, field, group, formation,
        ret, status, err, nullptr, 0);
}

static const char* LabForceRetMeaning(int32_t ret) {
    if (ret == -1) return "queued";
    if (ret == 0) return "not-started";
    return "unknown-ret";
}

static void LabMenuAdjustCustomTrack(int delta) {
    int next = g_labMusicCustomTrack + delta;
    if (next < 0) next = 0;
    if (next > 0xB5) next = 0xB5;
    if (next != g_labMusicCustomTrack) {
        g_labMusicCustomTrack = next;
        const char* name = LabMusicRuntimeName(g_labMusicCustomTrack);
        LabMenuSetStatus("scanner FMOD id=%d%s%s (Enter to play; H/N mark after hearing)",
            g_labMusicCustomTrack, name ? " - " : "", name ? name : "");
    }
}

static void LabMenuSelectMusic(int track) {
    const int triggerTrack = (track == 4) ? 7 : 4;
    g_labLastMusicTrack = track;
    if (g_block) {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_block->musicOverrideTrackIndex), track);
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_block->musicSeq));
    }

    int32_t ret = 0;
    uint32_t status = 0, err = 0;
    const bool ok = LabProbeSoundCmd(23, static_cast<uint32_t>(triggerTrack), 0, &ret, &status, &err);
    const char* name = LabMusicRuntimeName(track);
    LabMenuSetStatus("music FMOD id=%d%s%s trigger=%d -> soundcmd status=%u ret=%d err=0x%08X%s%s",
        track, name ? " - " : "", name ? name : "", triggerTrack, status, ret, err,
        ok ? "" : " (probe not ready?)",
        g_musicHookArmed ? "" : " (music hook not armed)");
}

static void LabMenuScanMusic(int delta) {
    LabMenuAdjustCustomTrack(delta);
    LabMenuSelectMusic(g_labMusicCustomTrack);
}

static void LabMenuClearMusic() {
    if (g_block) {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_block->musicOverrideTrackIndex), -1);
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_block->musicSeq));
    }
    LabMenuSetStatus("music override cleared");
}

static void LabMenuForceCurrentBattle() {
    int32_t fieldRet = 0, sceneRet = 0, forceRet = 0;
    uint32_t status = 0, err = 0;
    if (!LabProbeCallRva(RVA_ENCOUNTER_GET_CURRENT_FIELD, FFXPROBE_ABI_CDECL_I, &fieldRet, &status, &err)) {
        LabMenuSetStatus("force current: GetCurrentField failed status=%u err=0x%08X", status, err);
        return;
    }
    if (!LabProbeCallRva(RVA_ENCOUNTER_GET_SCENE_STATE, FFXPROBE_ABI_CDECL_I, &sceneRet, &status, &err)) {
        LabMenuSetStatus("force current: GetSceneState failed status=%u err=0x%08X", status, err);
        return;
    }

    uint8_t group = 0;
    if (sceneRet != 0) {
        if (!LabProbeReadAbsoluteByte(static_cast<uint32_t>(sceneRet) + ENCOUNTER_SCENE_GROUP_OFFSET, &group, &status, &err)) {
            LabMenuSetStatus("force current: read group failed ptr=0x%08X status=%u err=0x%08X",
                static_cast<unsigned>(sceneRet), status, err);
            return;
        }
    }

    if (fieldRet < 0 || fieldRet > 0xFFFF) {
        LabMenuSetStatus("force current: field out of range (%d)", fieldRet);
        return;
    }

    LabProbeForceBattle(static_cast<uint32_t>(fieldRet), group, 0, &forceRet, &status, &err);
    LabMenuSetStatus("force current field=%d group=%u formation=0 -> status=%u ret=%d %s err=0x%08X",
        fieldRet, static_cast<unsigned>(group), status, forceRet, LabForceRetMeaning(forceRet), err);
}

static void LabMenuForcePresetBattle() {
    const int field = EnvInt("FFXHOOKS_FORCE_FIELD", 2);
    const int group = EnvInt("FFXHOOKS_FORCE_GROUP", 0);
    const int formation = EnvInt("FFXHOOKS_FORCE_FORMATION", 0);
    int32_t ret = 0;
    uint32_t status = 0, err = 0;
    LabProbeForceBattle(static_cast<uint32_t>(field), static_cast<uint32_t>(group), static_cast<uint32_t>(formation),
        &ret, &status, &err);
    LabMenuSetStatus("force preset field=%d group=%d formation=%d -> status=%u ret=%d %s err=0x%08X",
        field, group, formation, status, ret, LabForceRetMeaning(ret), err);
}

static void LabMenuExecuteSelection() {
    switch (g_labMenuSelected) {
        case 0: LabMenuSelectMusic(g_labMusicCustomTrack); break;
        case 1: LabMenuScanMusic(-1); break;
        case 2: LabMenuScanMusic(1); break;
        case 3: LabMenuSelectMusic(28); break;
        case 4: LabMenuSelectMusic(16); break;
        case 5: LabMenuSelectMusic(29); break;
        case 6: LabMenuClearMusic(); break;
        case 7: LabMenuForceCurrentBattle(); break;
        case 8: LabMenuForcePresetBattle(); break;
        case 9:
        default:
            InterlockedExchange(&g_labMenuVisible, 0);
            if (g_labMenuHwnd) ShowWindow(g_labMenuHwnd, SW_HIDE);
            break;
    }
}

static void LabMenuHandleKey(int vk) {
    switch (vk) {
        case VK_ESCAPE:
            InterlockedExchange(&g_labMenuVisible, 0);
            if (g_labMenuHwnd) ShowWindow(g_labMenuHwnd, SW_HIDE);
            break;
        case VK_UP:
            g_labMenuSelected = (g_labMenuSelected + kLabMenuCount - 1) % kLabMenuCount;
            if (g_labMenuHwnd) InvalidateRect(g_labMenuHwnd, nullptr, TRUE);
            break;
        case VK_DOWN:
            g_labMenuSelected = (g_labMenuSelected + 1) % kLabMenuCount;
            if (g_labMenuHwnd) InvalidateRect(g_labMenuHwnd, nullptr, TRUE);
            break;
        case VK_LEFT:
            if (g_labMenuSelected == kLabMenuCustomTrackIndex ||
                g_labMenuSelected == kLabMenuScanPreviousIndex ||
                g_labMenuSelected == kLabMenuScanNextIndex) {
                LabMenuAdjustCustomTrack(-1);
            }
            break;
        case VK_RIGHT:
            if (g_labMenuSelected == kLabMenuCustomTrackIndex ||
                g_labMenuSelected == kLabMenuScanPreviousIndex ||
                g_labMenuSelected == kLabMenuScanNextIndex) {
                LabMenuAdjustCustomTrack(1);
            }
            break;
        case VK_NEXT:
            if (g_labMenuSelected == kLabMenuCustomTrackIndex ||
                g_labMenuSelected == kLabMenuScanPreviousIndex ||
                g_labMenuSelected == kLabMenuScanNextIndex) {
                LabMenuAdjustCustomTrack(-10);
            }
            break;
        case VK_PRIOR:
            if (g_labMenuSelected == kLabMenuCustomTrackIndex ||
                g_labMenuSelected == kLabMenuScanPreviousIndex ||
                g_labMenuSelected == kLabMenuScanNextIndex) {
                LabMenuAdjustCustomTrack(10);
            }
            break;
        case VK_RETURN:
            LabMenuExecuteSelection();
            break;
        case 'H':
            if (g_labLastMusicTrack >= 0) LabMenuSetStatus("MARK heard FMOD id=%d", g_labLastMusicTrack);
            break;
        case 'N':
            if (g_labLastMusicTrack >= 0) LabMenuSetStatus("MARK no-audio FMOD id=%d", g_labLastMusicTrack);
            break;
    }
}

static bool LabMenuRowFromPoint(LPARAM lParam, int* row) {
    const int y = static_cast<short>((lParam >> 16) & 0xFFFF);
    const int index = (y - 42) / 22;
    if (index < 0 || index >= kLabMenuCount || y < 42) {
        return false;
    }
    if (row) *row = index;
    return true;
}

static LRESULT CALLBACK LabMenuWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc = {};
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(235, 230, 190));

            RECT line = { 14, 12, rc.right - 14, 34 };
            DrawTextA(hdc, "JARVIS FFX LAB MENU", -1, &line, DT_LEFT | DT_SINGLELINE);
            for (int i = 0; i < kLabMenuCount; ++i) {
                line.top = 42 + i * 22;
                line.bottom = line.top + 22;
                char text[256] = {};
                if (i == kLabMenuCustomTrackIndex) {
                    const char* name = LabMusicRuntimeName(g_labMusicCustomTrack);
                    _snprintf_s(text, sizeof(text), _TRUNCATE, "%c %s %d%s%s",
                        i == g_labMenuSelected ? '>' : ' ', kLabMenuItems[i].label,
                        g_labMusicCustomTrack, name ? " - " : "", name ? name : "");
                } else if (i == kLabMenuScanPreviousIndex) {
                    int prev = g_labMusicCustomTrack - 1;
                    if (prev < 0) prev = 0;
                    const char* name = LabMusicRuntimeName(prev);
                    _snprintf_s(text, sizeof(text), _TRUNCATE, "%c %s -> %d%s%s",
                        i == g_labMenuSelected ? '>' : ' ', kLabMenuItems[i].label,
                        prev, name ? " - " : "", name ? name : "");
                } else if (i == kLabMenuScanNextIndex) {
                    int next = g_labMusicCustomTrack + 1;
                    if (next > 0xB5) next = 0xB5;
                    const char* name = LabMusicRuntimeName(next);
                    _snprintf_s(text, sizeof(text), _TRUNCATE, "%c %s -> %d%s%s",
                        i == g_labMenuSelected ? '>' : ' ', kLabMenuItems[i].label,
                        next, name ? " - " : "", name ? name : "");
                } else {
                    _snprintf_s(text, sizeof(text), _TRUNCATE, "%c %s",
                        i == g_labMenuSelected ? '>' : ' ', kLabMenuItems[i].label);
                }
                SetTextColor(hdc, i == g_labMenuSelected ? RGB(120, 220, 255) : RGB(230, 230, 230));
                DrawTextA(hdc, text, -1, &line, DT_LEFT | DT_SINGLELINE);
            }
            line.top = 42 + kLabMenuCount * 22 + 8;
            line.bottom = rc.bottom - 10;
            SetTextColor(hdc, RGB(180, 220, 150));
            DrawTextA(hdc, g_labMenuStatus, -1, &line, DT_LEFT | DT_WORDBREAK);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_KEYDOWN:
            LabMenuHandleKey(static_cast<int>(wParam));
            return 0;
        case WM_LBUTTONDOWN: {
            int row = -1;
            if (LabMenuRowFromPoint(lParam, &row)) {
                g_labMenuSelected = row;
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            int row = -1;
            if (LabMenuRowFromPoint(lParam, &row)) {
                g_labMenuSelected = row;
                InvalidateRect(hwnd, nullptr, TRUE);
                LabMenuExecuteSelection();
            }
            return 0;
        }
        case WM_RBUTTONDOWN:
            LabMenuExecuteSelection();
            return 0;
        case WM_CLOSE:
            InterlockedExchange(&g_labMenuVisible, 0);
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            g_labMenuHwnd = NULL;
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static bool LabMenuKeyPressed(int vk) {
    return (GetAsyncKeyState(vk) & 0x0001) != 0;
}

static bool LabMenuAllowWithInGameMenu() {
    return EnvFlagEnabled("FFXHOOKS_ALLOW_LEGACY_LAB_MENU_WITH_INGAME") ||
           EnvFlagEnabled("FFXHOOKS_ALLOW_MENU_WITH_INGAME");
}

static int LabMenuToggleKey() {
    // 2026-08-02 (Jarvis-HOOK): F7 = FFX Editor In-Live (menu nativo); F8 = InGameMenu (plugin switchboard).
    // Legacy LabMenu moved to F6 — no key collision between the three menus.
    return VK_F6;
}

static DWORD WINAPI LabMenuThreadProc(LPVOID) {
    HINSTANCE inst = GetModuleHandleA(nullptr);
    WNDCLASSA wc = {};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = LabMenuWndProc;
    wc.hInstance = inst;
    wc.lpszClassName = "JarvisFfxLabMenuWindow";
    wc.hCursor = LoadCursorA(nullptr, (LPCSTR)IDC_ARROW);
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        "Jarvis FFX Lab Menu",
        WS_POPUP | WS_BORDER,
        48, 48, 620, 380,
        nullptr, nullptr, inst, nullptr);
    g_labMenuHwnd = hwnd;
    if (!hwnd) {
        LabMenuSetStatus("menu window create failed err=%u", GetLastError());
        InterlockedExchange(&g_labMenuRunning, 0);
        return 0;
    }

    const bool inGameMenuEnabled = InGameMenuConfigEnabled();
    if (inGameMenuEnabled) {
        InterlockedExchange(&g_labMenuVisible, 0);
    }

    ShowWindow(hwnd, InterlockedCompareExchange(&g_labMenuVisible, 0, 0) ? SW_SHOWNOACTIVATE : SW_HIDE);
    UpdateWindow(hwnd);
    LabMenuSetStatus(
        inGameMenuEnabled
            ? "ready; F6 show/hide; click selects; double/right-click executes; FMOD ids are not WAV names"
            : "ready; F6 show/hide; click selects; double/right-click executes; FMOD ids are not WAV names");

    MSG msg = {};
    const int toggleKey = LabMenuToggleKey();
    while (InterlockedCompareExchange(&g_labMenuRunning, 1, 1) == 1) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        if (LabMenuKeyPressed(toggleKey)) {
            const LONG visible = InterlockedCompareExchange(&g_labMenuVisible, 0, 0) ? 0 : 1;
            InterlockedExchange(&g_labMenuVisible, visible);
            ShowWindow(hwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
        }
        if (InterlockedCompareExchange(&g_labMenuVisible, 0, 0)) {
            if (LabMenuKeyPressed(VK_ESCAPE)) LabMenuHandleKey(VK_ESCAPE);
            if (LabMenuKeyPressed(VK_UP)) LabMenuHandleKey(VK_UP);
            if (LabMenuKeyPressed(VK_DOWN)) LabMenuHandleKey(VK_DOWN);
            if (LabMenuKeyPressed(VK_LEFT)) LabMenuHandleKey(VK_LEFT);
            if (LabMenuKeyPressed(VK_RIGHT)) LabMenuHandleKey(VK_RIGHT);
            if (LabMenuKeyPressed(VK_NEXT)) LabMenuHandleKey(VK_NEXT);
            if (LabMenuKeyPressed(VK_PRIOR)) LabMenuHandleKey(VK_PRIOR);
            if (LabMenuKeyPressed('H')) LabMenuHandleKey('H');
            if (LabMenuKeyPressed('N')) LabMenuHandleKey('N');
            if (LabMenuKeyPressed(VK_RETURN)) LabMenuHandleKey(VK_RETURN);
        }
        Sleep(30);
    }

    if (hwnd) DestroyWindow(hwnd);
    return 0;
}

static void StartLabMenuIfEnabled() {
    if (!EnvFlagEnabled("FFXHOOKS_ENABLE_MENU")) {
        return;
    }
    if (InGameMenuConfigEnabled() && !LabMenuAllowWithInGameMenu()) {
        Log("[ffx-hooks] LabMenu skipped because InGameMenu is enabled; set FFXHOOKS_ALLOW_LEGACY_LAB_MENU_WITH_INGAME=1 to run both (LabMenu uses F7)\n");
        return;
    }
    if (InterlockedCompareExchange(&g_labMenuRunning, 1, 0) != 0) {
        return;
    }
    DWORD tid = 0;
    g_labMenuThread = CreateThread(nullptr, 0, LabMenuThreadProc, nullptr, 0, &tid);
    if (g_labMenuThread) {
        Log("[ffx-hooks] LabMenu thread created tid=%u\n", static_cast<unsigned>(tid));
        CloseHandle(g_labMenuThread);
        g_labMenuThread = NULL;
    } else {
        InterlockedExchange(&g_labMenuRunning, 0);
        Log("[ffx-hooks] WARN LabMenu thread create failed (err=%u)\n", GetLastError());
    }
}

static void StopLabMenu() {
    InterlockedExchange(&g_labMenuRunning, 0);
    if (g_labMenuHwnd) {
        PostMessageA(g_labMenuHwnd, WM_CLOSE, 0, 0);
    }
}
#endif

static bool BytesMatch(const uint8_t* actual, const uint8_t* expected, const char* mask, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (mask[i] == 'x' && actual[i] != expected[i]) {
            return false;
        }
    }
    return true;
}

static bool ReadProcessBytesSafe(uintptr_t address, uint8_t* out, size_t len) {
    __try {
        memcpy(out, reinterpret_cast<const void*>(address), len);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[ffx-hooks] WARN read failed at VA 0x%08X (seh=0x%08X)\n",
            static_cast<unsigned>(address), static_cast<unsigned>(GetExceptionCode()));
        memset(out, 0, len);
        return false;
    }
}

static void LogBytes(const char* label, const uint8_t* bytes, size_t len) {
    char hex[192] = {};
    size_t pos = 0;
    const size_t n = len < 24 ? len : 24;
    for (size_t i = 0; i < n && pos < sizeof(hex); ++i) {
        const int written = _snprintf_s(hex + pos, sizeof(hex) - pos, _TRUNCATE,
            "%02X%s", bytes[i], (i + 1 < n) ? " " : "");
        if (written <= 0) break;
        pos += static_cast<size_t>(written);
    }
    Log("[ffx-hooks] %s bytes: %s\n", label, hex);
}

static bool ValidateHookTarget(
    const char* label,
    uintptr_t targetRva,
    const uint8_t* expected,
    const char* mask,
    size_t len) {
    uint8_t actual[64] = {};
    if (len > sizeof(actual)) {
        Log("[ffx-hooks] WARN %s signature too long (%u bytes)\n", label, static_cast<unsigned>(len));
        return false;
    }

    const uintptr_t target = rva(targetRva);
    Log("[ffx-hooks] validate %s at VA 0x%08X (RVA 0x%08X)\n",
        label, static_cast<unsigned>(target), static_cast<unsigned>(targetRva));
    if (!ReadProcessBytesSafe(target, actual, len)) {
        return false;
    }

    LogBytes(label, actual, len);
    if (!BytesMatch(actual, expected, mask, len)) {
        Log("[ffx-hooks] WARN %s signature mismatch\n", label);
        return false;
    }

    Log("[ffx-hooks] %s signature OK\n", label);
    return true;
}

struct MusicTargetValidation {
    bool playTrackOk;
    bool switchCrossfadeOk;
    bool prepBattleTrackOk;
    bool playTrackWithPreloadOk;
};

static MusicTargetValidation ValidateMusicTargets() {
    static const uint8_t expectedPlayTrack[] = {
        0x55, 0x8B, 0xEC, 0x53, 0x8B, 0x5D, 0x00, 0x56, 0x57, 0x8B, 0xF1,
        0x81, 0xFB, 0xB5, 0x00, 0x00, 0x00
    };
    static const char maskPlayTrack[] = "xxxxxx?xxxxxxxxxx";

    static const uint8_t expectedSwitchCrossfade[] = {
        0x55, 0x8B, 0xEC, 0x53, 0x8B, 0x5D, 0x00, 0x57, 0x8B, 0xF9, 0x81, 0xFB,
        0xB5, 0x00, 0x00, 0x00, 0x0F, 0x87, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x47,
        0x00, 0x56, 0x8B, 0xF3, 0xC1, 0xE6, 0x04, 0x2B, 0xF3, 0x83, 0x7C, 0xB0
    };
    static const char maskSwitchCrossfade[] = "xxxxxx?xxxxxxxxxxx????xx?xxxxxxxxxxx";

    static const uint8_t expectedPrepBattleTrack[] = {
        0xE9
    };
    static const char maskPrepBattleTrack[] = "x";

    static const uint8_t expectedPlayTrackWithPreload[] = {
        0x55, 0x8B, 0xEC
    };
    static const char maskPlayTrackWithPreload[] = "xxx";

    MusicTargetValidation result = {};
    result.playTrackOk = ValidateHookTarget(
        "FFX_FmodMusic_PlayTrackByIndex",
        RVA_FMOD_PLAY_TRACK,
        expectedPlayTrack,
        maskPlayTrack,
        sizeof(expectedPlayTrack));
    result.switchCrossfadeOk = ValidateHookTarget(
        "FFX_FmodMusic_SwitchTrackCrossfade",
        RVA_FMOD_SWITCH_CROSSFADE,
        expectedSwitchCrossfade,
        maskSwitchCrossfade,
        sizeof(expectedSwitchCrossfade));
    result.prepBattleTrackOk = ValidateHookTarget(
        "FFX_Music_PrepBattleTrack",
        RVA_MUSIC_PREP_BATTLE_TRACK,
        expectedPrepBattleTrack,
        maskPrepBattleTrack,
        sizeof(expectedPrepBattleTrack));
    result.playTrackWithPreloadOk = ValidateHookTarget(
        "FFX_Music_PlayTrackWithPreload",
        RVA_MUSIC_PLAY_TRACK_WITH_PRELOAD,
        expectedPlayTrackWithPreload,
        maskPlayTrackWithPreload,
        sizeof(expectedPlayTrackWithPreload));
    return result;
}

static bool WaitForProbeHeartbeat(DWORD timeoutMs) {
    const DWORD start = GetTickCount();
    HANDLE probeMmf = NULL;
    FFXProbeBlock* probe = nullptr;

    Log("[ffx-hooks] waiting for ffx-probe heartbeat (timeout=%ums)\n",
        static_cast<unsigned>(timeoutMs));
    while (GetTickCount() - start < timeoutMs) {
        if (!probeMmf) {
            probeMmf = OpenFileMappingA(FILE_MAP_READ, FALSE, FFXPROBE_MMF_NAME);
            if (probeMmf) {
                probe = static_cast<FFXProbeBlock*>(
                    MapViewOfFile(probeMmf, FILE_MAP_READ, 0, 0, sizeof(FFXProbeBlock)));
                if (!probe) {
                    Log("[ffx-hooks] WARN failed to map ffx-probe block (err=%u)\n", GetLastError());
                    CloseHandle(probeMmf);
                    probeMmf = NULL;
                } else {
                    Log("[ffx-hooks] ffx-probe block mapped\n");
                }
            }
        }

        if (probe && probe->magic == FFXPROBE_MAGIC && probe->hooked) {
            const uint32_t heartbeat0 = probe->heartbeat;
            Sleep(120);
            const uint32_t heartbeat1 = probe->heartbeat;
            if (heartbeat1 != heartbeat0) {
                Log("[ffx-hooks] ffx-probe heartbeat OK (%u -> %u), moduleBase=0x%08X\n",
                    heartbeat0, heartbeat1, probe->moduleBase);
                UnmapViewOfFile(probe);
                CloseHandle(probeMmf);
                return true;
            }
        }

        Sleep(100);
    }

    if (probe) UnmapViewOfFile(probe);
    if (probeMmf) CloseHandle(probeMmf);
    Log("[ffx-hooks] WARN ffx-probe heartbeat not ready before timeout\n");
    return false;
}

/* Native menu shell adapter - OFF by default.
 * NativeMenuShell.h provides the game-native text/menu primitives used by the F7/F8/F9
 * surfaces. Aurora PhotoModeActions are a separate developer-only bridge. The legacy arm
 * remains FFXHOOKS_ENABLE_NATIVE_MENU=1, with FFXHOOKS_NATIVE_MENU_HOTKEY selecting a VK
 * code (F7 by default). The adapter detours the IDA-verified main-thread menu pump at
 * VA 0x8A9C50, int __cdecl(unsigned int), and only draws while the menu subsystem is live
 * (dword_13407E4). Direct accesses use the explicit guards shown at each call site;
 * normal-context cleanup is reversible, while dynamic hot unload remains unsupported. */
#ifdef FFXHOOKS_HAVE_POLYHOOK
// PhotoModeActions.h declares g_base/g_pm as extern
namespace PhotoMode { uintptr_t g_base = 0; State g_pm; }

static PLH::x86Detour*   g_nativeMenuPumpDetour = nullptr;
static uint64_t          g_nativeMenuPumpTramp  = 0;
static NativeMenu::Menu  g_nativeMenu           = { 0 };
static NativeMenu::Menu  g_arenaPlusMenu        = { 0 };
static int               g_nativeMenuHotkey     = VK_F7;
static int               g_nativeHeldAction     = -1;   // -1 = none; else ActionId in "hold" mode
static volatile LONG     g_nativeMenuInHook     = 0;
static volatile LONG     g_forceSubsystem       = 0;   // 1 = force dword_13407E4=1 every frame (Present) -> pump runs on field
static volatile LONG     g_nativeMenuProducerReady = 0;
static volatile LONG     g_nativeMenuHubCloseDrainObj = 0;
static volatile LONG     g_nativeMenuHubCloseDrainPumpPasses = 0;
static constexpr LONG    kNativeMenuHubCloseDrainMaxPumpPasses = 8;
static volatile LONG     g_nativeOtherOwnerPublished = 0; // pump-owned objects collapsed for Present
static volatile LONG     g_nativeWantSpawn      = 0;   // spawn request (consumed by the pump hook when the pump runs)
static volatile LONG     g_nativeWantClose      = 0;   // close request
static volatile LONG     g_arenaPlusWantOpen    = 0;   // request to open the Arena+ screen itself at the pump
static volatile LONG     g_arenaNpcInNowWhat    = 0;   // Common.013B "Now what?" dialog active (blocks overlap)
static volatile LONG     g_arenaNpcPendingOpen  = 0;   // open Arena+ after the vanilla dialog closes
static volatile LONG     g_arenaNpcOpenDelay    = 0;   // pump frames before opening (avoids overlap)
static FfxHooks::F7Ui::ModalState g_f7UiModalState;
static volatile LONG     g_f7CloseSourcePending = -1;
static int               g_f7CursorShowIncrements = 0;
static FfxHooks::F7Ui::PointerState g_f7PointerState;

struct F7MouseInputResult {
    bool confirm = false;
    bool ownsDirectionalFrame = false;
};

static void F7CloseTransition(
    FfxHooks::F7Ui::CloseSource source,
    FfxHooks::F7Ui::CloseDestination destination);
static F7MouseInputResult F7ListMouseTick(
    int obj,
    float left,
    float top,
    float width,
    float step,
    float rowHeight,
    int rowCount,
    int page);
static void F7MainMenuMouseTick(int obj);

static void F7SeedPointerForDestination() {
    const bool physicalButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    FfxHooks::F7Ui::SeedPointerForDestination(g_f7PointerState, physicalButtonDown);
}

static bool F7IsForegroundWindow() {
    HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    if (g_ingameMenuInputHwnd && IsWindow(g_ingameMenuInputHwnd)) {
        return foreground == g_ingameMenuInputHwnd;
    }
    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(foreground, &foregroundPid);
    return foregroundPid == GetCurrentProcessId();
}

static void F7AcquireCursorOwnership() {
    if (g_f7CursorShowIncrements > 0) return;
    int displayCount = -1;
    // ShowCursor is a process-global counter. Track every increment needed to
    // reach visibility and undo exactly those increments at the terminal close.
    do {
        displayCount = ShowCursor(TRUE);
        ++g_f7CursorShowIncrements;
    } while (displayCount < 0 && g_f7CursorShowIncrements < 64);
    g_f7UiModalState.cursorOwned = g_f7CursorShowIncrements > 0;
}

static void F7ReleaseCursorOwnership() {
    while (g_f7CursorShowIncrements > 0) {
        ShowCursor(FALSE);
        --g_f7CursorShowIncrements;
    }
    g_f7UiModalState.cursorOwned = false;
}

static void F7RequestClose(FfxHooks::F7Ui::CloseSource source) {
    FfxHooks::NativePorts::CancelBindingCapture();
    InterlockedCompareExchange(
        &g_f7CloseSourcePending, static_cast<LONG>(source), -1);
}

static bool NativeMenuHubCloseDrainPending() {
    return InterlockedCompareExchange(&g_nativeMenuHubCloseDrainObj, 0, 0) != 0;
}

static bool NativeMenuHubObjectStillOwned(int obj) {
    if (!obj || !g_base) return false;
    const uintptr_t poolBase =
        g_base + (NativeMenu::POOL_VA - 0x400000u);
    const uintptr_t address = static_cast<uintptr_t>(static_cast<uint32_t>(obj));
    const uintptr_t poolSpan =
        static_cast<uintptr_t>(NativeMenu::POOL_MAX * NativeMenu::POOL_STRIDE);
    if (address < poolBase || address - poolBase >= poolSpan ||
        (address - poolBase) % NativeMenu::POOL_STRIDE != 0) {
        return false;
    }
    __try {
        if (*NativeMenu::Pb(obj, NativeMenu::O_ACTIVE) == 0) return false;
        if (NativeMenu::RdD(obj, NativeMenu::O_UPDATE) !=
            static_cast<int32_t>(reinterpret_cast<uintptr_t>(&NativeMenu::OurListInputCb))) {
            return false;
        }
        if (NativeMenu::RdD(obj, NativeMenu::O_DRAW) !=
            static_cast<int32_t>(reinterpret_cast<uintptr_t>(&NativeMenu::OurDraw))) {
            return false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

static void NativeMenuQueueHubCloseDrain(int closingObject, bool pumpAvailable) {
    if (!closingObject) return;
    if (!NativeMenuHubObjectStillOwned(closingObject)) {
        Log("[ffx-hooks] F7 hub close drain: obj=0x%08X already released (not pending)\n",
            closingObject);
        return;
    }
    if (!pumpAvailable) {
        __try {
            NativeMenu::Reset(closingObject);
            Log("[ffx-hooks] F7 hub close drain: obj=0x%08X reset synchronously (teardown)\n",
                closingObject);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("[ffx-hooks] WARN F7 hub close drain: teardown reset faulted obj=0x%08X\n",
                closingObject);
        }
        return;
    }
    const LONG previous = InterlockedCompareExchange(
        &g_nativeMenuHubCloseDrainObj, static_cast<LONG>(closingObject), 0);
    if (previous == 0 || previous == closingObject) {
        InterlockedExchange(&g_nativeMenuHubCloseDrainPumpPasses, 0);
        InterlockedExchange(&g_forceSubsystem, 1);
        int active = -1;
        int closeFlag = -1;
        __try {
            active = *NativeMenu::Pb(closingObject, NativeMenu::O_ACTIVE);
            closeFlag = *NativeMenu::Pb(closingObject, 65);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        Log("[ffx-hooks] F7 hub close drain pending obj=0x%08X active=%d close=%d (force retained)\n",
            closingObject, active, closeFlag);
        return;
    }
    Log("[ffx-hooks] WARN F7 hub close drain collision: pending obj=0x%08X preserved, new obj=0x%08X\n",
        static_cast<int>(previous), closingObject);
    if (NativeMenuHubObjectStillOwned(closingObject)) {
        __try {
            NativeMenu::Reset(closingObject);
            Log("[ffx-hooks] F7 hub close drain collision: new obj=0x%08X reset synchronously\n",
                closingObject);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("[ffx-hooks] WARN F7 hub close drain collision: reset faulted obj=0x%08X\n",
                closingObject);
        }
    }
}

static void NativeMenuPollHubCloseDrainAfterPump() {
    const LONG pending =
        InterlockedCompareExchange(&g_nativeMenuHubCloseDrainObj, 0, 0);
    if (pending == 0) return;
    const int obj = static_cast<int>(pending);
    if (!NativeMenuHubObjectStillOwned(obj)) {
        if (InterlockedCompareExchange(&g_nativeMenuHubCloseDrainObj, 0, pending) ==
            pending) {
            const LONG passes = InterlockedExchange(
                &g_nativeMenuHubCloseDrainPumpPasses, 0);
            Log("[ffx-hooks] F7 hub close drain released obj=0x%08X pumpPasses=%d\n",
                obj, static_cast<int>(passes));
        }
        return;
    }
    const LONG passes = InterlockedIncrement(&g_nativeMenuHubCloseDrainPumpPasses);
    if (passes < kNativeMenuHubCloseDrainMaxPumpPasses) return;
    int resetOk = 0;
    if (NativeMenuHubObjectStillOwned(obj)) {
        __try {
            NativeMenu::Reset(obj);
            resetOk = 1;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("[ffx-hooks] WARN F7 hub close drain: fallback reset faulted obj=0x%08X\n",
                obj);
        }
    }
    if (InterlockedCompareExchange(&g_nativeMenuHubCloseDrainObj, 0, pending) ==
        pending) {
        InterlockedExchange(&g_nativeMenuHubCloseDrainPumpPasses, 0);
        Log("[ffx-hooks] WARN F7 hub close drain: bounded fallback released obj=0x%08X after %d pump passes reset=%d\n",
            obj, static_cast<int>(passes), resetOk);
    }
}

/* Vanilla dispatcher contract (static RE of FFX.exe 78CE3439, 2026-09-15):
 * 0x8AAFE0 fires and clears the game continuation callback at 0x1840834
 * whenever the pump deactivates the subsystem, and the pump deactivates at the
 * end of every frame while the request bitmask at 0x18408AC is zero. Forced
 * custom-menu sessions never set vanilla request bits, so that deactivation ran
 * on every forced frame, consuming game callbacks that later transitions
 * (battle exit -> reward screen -> field reload) still needed and leaving the
 * reward screen black while gameplay logic continued. While a forced owner is
 * published we keep bit 31 set (outside the layer range 0..24 managed by
 * 0x8AA0B0 and the per-frame slot loop, and the same "system keeps the
 * subsystem alive" semantic 0x8AA5C0 itself applies) so 0x8AA5C0 stays nonzero
 * and the subsystem state stays vanilla-consistent until a real close clears
 * both the gate and the bit without touching the game's callback. */
static constexpr uint32_t kNativeMenuGateRva = 0x13407E4u;
static constexpr uint32_t kNativeMenuRequestRva = 0x18408ACu;
static constexpr uint32_t kNativeMenuKeepAliveBit = 0x80000000u;
static volatile LONG g_nativeMenuKeepAlivePublished = 0;

static void NativeMenuForceGatePublish() {
    if (!g_base) return;
    *reinterpret_cast<volatile int*>(g_base + (kNativeMenuGateRva - 0x400000u)) = 1;
    __try {
        *reinterpret_cast<volatile uint32_t*>(
            g_base + (kNativeMenuRequestRva - 0x400000u)) |= kNativeMenuKeepAliveBit;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (InterlockedExchange(&g_nativeMenuKeepAlivePublished, 1) == 0) {
        Log("[ffx-hooks] NativeMenu keep-alive engaged (0x18408AC bit31 set)\n");
    }
}

static void NativeMenuForceGateClear() {
    if (!g_base) return;
    /* Ownership rule: the gate and the request-mask bit belong to vanilla
     * unless a forced publish wrote them. The pump tail reaches this on every
     * vanilla pump call too; clearing a gate we never wrote would drop
     * dispatcher 0x8AAFE0 onto its gate==0 init branch, whose 0x8AA520 wipe
     * erases the layer table and request mask every other frame and starves
     * vanilla menus such as the post-battle reward screen. */
    if (InterlockedExchange(&g_nativeMenuKeepAlivePublished, 0) == 0) return;
    __try {
        *reinterpret_cast<volatile uint32_t*>(
            g_base + (kNativeMenuRequestRva - 0x400000u)) &= ~kNativeMenuKeepAliveBit;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    *reinterpret_cast<volatile int*>(g_base + (kNativeMenuGateRva - 0x400000u)) = 0;
    Log("[ffx-hooks] NativeMenu keep-alive released (0x18408AC bit31 cleared)\n");
}

static void NativeMenuAbortHubCloseDrainForStop() {
    const LONG pending =
        InterlockedCompareExchange(&g_nativeMenuHubCloseDrainObj, 0, 0);
    if (pending != 0) {
        const int obj = static_cast<int>(pending);
        const bool owned = NativeMenuHubObjectStillOwned(obj);
        int resetOk = 0;
        if (owned) {
            __try {
                NativeMenu::Reset(obj);
                resetOk = 1;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Log("[ffx-hooks] WARN F7 hub close drain: stop reset faulted obj=0x%08X\n",
                    obj);
            }
        }
        if (InterlockedCompareExchange(&g_nativeMenuHubCloseDrainObj, 0, pending) ==
            pending) {
            InterlockedExchange(&g_nativeMenuHubCloseDrainPumpPasses, 0);
            Log("[ffx-hooks] F7 hub close drain aborted for stop obj=0x%08X owned=%d reset=%d\n",
                obj, owned ? 1 : 0, resetOk);
        }
    }
    InterlockedExchange(&g_forceSubsystem, 0);
    NativeMenuForceGateClear();
}

/* Native-menu boundary tracer (diagnostic; OFF by default).
 * Vanilla post-battle flow (static RE of FFX.exe 78CE3439, 2026-09-15):
 * battle exit sets menu mode 0xCCB994=2 via 0x85B0A4; the per-tick mode
 * machine 0x820090 waits for uiMgr(*0xCE81E4)+8==4 and 0xCCB998==0, then
 * 0x8B3D60 requests layer 0xF/0x10 through 0x18408AC / 0x8AA0B0. Dispatcher
 * 0x8AAFE0 runs three branches: gate!=0 -> pump; gate==0 && 0x13407E8!=0 ->
 * fade step 0x8AADB0; gate==0 && transition==0 -> full init then pump. Field
 * render resumes only when 0xCCB994 returns to 0. Our forced F7 gate always
 * picks the pump branch, so a transition/bootstrap flag left stuck is not
 * visible statically: this tracer samples the boundary DWORDs once per
 * Present and logs only transitions, so one RT2 run shows the last sane
 * value and the flag that stopped moving. */
struct NativeMenuTraceField {
    uint32_t idaVa;   /* FFX.exe IDA VA (image base 0x400000) */
    uint8_t  kind;    /* 0=dword, 1=word, 2=byte, 3=dword at *0xCE81E4+off */
    uint8_t  pad;
    uint16_t off;     /* kind 3 only: offset inside the UI manager object */
    const char* name;
};

static const NativeMenuTraceField kNativeMenuTraceFields[] = {
    {0x13407E4u, 0, 0, 0x000, "gate"},      /* dispatcher/pump gate */
    {0x13407E8u, 0, 0, 0x000, "fade"},      /* menu transition/fade flag */
    {0x13407ECu, 0, 0, 0x000, "maskEC"},    /* field-block mask (PC stub clears it) */
    {0x13407F8u, 0, 0, 0x000, "fadeCtr"},   /* fade counter 0x80 -> 0 by -4/frame */
    {0x1340804u, 0, 0, 0x000, "rdyBlk"},    /* request-ready block in 0x820860 */
    {0x134080Cu, 0, 0, 0x000, "trB0C"},     /* transition bookkeeping */
    {0x1340810u, 0, 0, 0x000, "trB10"},     /* set by 0x8AADB0, drives 0x8AB2F0 */
    {0x1340814u, 0, 0, 0x000, "sys814"},    /* system-menu marker (arg 0x800003) */
    {0x1340819u, 2, 0, 0x000, "init819"},   /* cleared by dispatcher init */
    {0x1840834u, 0, 0, 0x000, "contCb"},    /* continuation callback record */
    {0x18408B8u, 0, 0, 0x000, "initArg"},   /* dispatcher init arg */
    {0x1840844u, 2, 0, 0x000, "boot844"},   /* bootstrap flag */
    {0x1840845u, 2, 0, 0x000, "dispSeen"},  /* dispatcher-entered marker */
    {0x18408ACu, 0, 0, 0x000, "reqMask"},   /* layer request bitmask */
    {0x12FBBF0u, 0, 0, 0x000, "pendId"},    /* pending native-menu request id */
    {0x12FBBF4u, 0, 0, 0x000, "pendSt"},    /* pending request status (-1 idle) */
    {0x12FB790u, 0, 0, 0x000, "kill790"},   /* 2D render disable */
    {0x12FB794u, 0, 0, 0x000, "kill794"},   /* 2D kill (aux) */
    {0x12FB798u, 0, 0, 0x000, "kill798"},   /* 2D batch-upload skip */
    {0x12FB7C0u, 0, 0, 0x000, "cd7C0"},     /* transition spin countdown */
    {0x12FB79Cu, 0, 0, 0x000, "fldLatch"},  /* field-mode renderer latch */
    {0xCCB994u,  0, 0, 0x000, "mode"},      /* menu mode: 0 free / 2 postbattle */
    {0xCCB998u,  0, 0, 0x000, "subBusy"},   /* mode sub-operation busy */
    {0xCCB99Cu,  0, 0, 0x000, "modeReq"},   /* mode request written by 0x648860 */
    {0x12FB878u, 0, 0, 0x000, "defer878"},  /* deferred spawn flag (mode 2->4) */
    {0x1597F34u, 2, 0, 0x000, "uiInit34"},  /* UI subsystem init byte */
    {0x133C8D0u, 2, 0, 0x000, "flg8D0"},    /* field-entry flag (0x822520) */
    {0x1841C24u, 0, 0, 0x000, "fadeDl"},    /* pump fade alpha delta */
    {0x1841C28u, 0, 0, 0x000, "fadeAl"},    /* pump fade alpha accumulator */
    {0xCE81E4u,  0, 0, 0x000, "uiMgr"},     /* UI manager singleton pointer */
    {0xCE81E4u,  3, 0, 0x008, "uiSt8"},     /* inner state: mode 2 needs ==4 */
    {0xCE81E4u,  3, 0, 0x00C, "uiFldC"},    /* deferred spawn cookie (-1 check) */
    {0xCE81E4u,  3, 0, 0x358, "uiF358"},    /* mode-2 spawn gate */
    {0xCE81E4u,  3, 0, 0x360, "uiF360"},    /* 0x648220 check */
    {0xCE81E4u,  3, 0, 0x3B0, "uiF3B0"},    /* 0x648260 check */
};

static volatile LONG g_nativeMenuTraceBusy = 0;
static int g_nativeMenuTraceArmed = 0; /* 0=unknown, 1=on, -1=off */
static uint32_t g_nativeMenuTracePrev[
    sizeof(kNativeMenuTraceFields) / sizeof(kNativeMenuTraceFields[0])];
static bool g_nativeMenuTracePrimed = false;
static uint64_t g_nativeMenuTraceSeq = 0;

static bool NativeMenuBoundaryTraceEnabled() {
    if (g_nativeMenuTraceArmed == 1) return true;
    if (g_nativeMenuTraceArmed == -1) return false;
    const bool enabled =
        SettingInt("FFXHOOKS_NM_BOUNDARY_TRACE", "native_menu.boundary_trace", 0) != 0 ||
        ModuleFlagEnabled("native_menu_trace.flag") ||
        ModuleFlagEnabled("config\\native_menu_trace.flag");
    /* Latch only the armed edge so a flag dropped after boot is still picked up. */
    if (enabled) g_nativeMenuTraceArmed = 1;
    return enabled;
}

static void NativeMenuBoundaryTraceTick() {
    if (!g_base || !NativeMenuBoundaryTraceEnabled()) return;
    if (InterlockedCompareExchange(&g_nativeMenuTraceBusy, 1, 0) != 0) return;
    __try {
        const size_t count =
            sizeof(kNativeMenuTraceFields) / sizeof(kNativeMenuTraceFields[0]);
        uint32_t cur[sizeof(kNativeMenuTraceFields) / sizeof(kNativeMenuTraceFields[0])];
        for (size_t i = 0; i < count; ++i) {
            const NativeMenuTraceField& f = kNativeMenuTraceFields[i];
            const volatile uint8_t* p =
                reinterpret_cast<const volatile uint8_t*>(g_base + (f.idaVa - 0x400000u));
            uint32_t v;
            if (f.kind == 3) {
                const uintptr_t mgr =
                    *reinterpret_cast<const volatile uint32_t*>(p);
                v = mgr ? *reinterpret_cast<const volatile uint32_t*>(mgr + f.off)
                        : 0xFFFFFFFFu;
            } else if (f.kind == 2) {
                v = *p;
            } else if (f.kind == 1) {
                v = *reinterpret_cast<const volatile uint16_t*>(p);
            } else {
                v = *reinterpret_cast<const volatile uint32_t*>(p);
            }
            cur[i] = v;
        }
        ++g_nativeMenuTraceSeq;
        if (!g_nativeMenuTracePrimed) {
            g_nativeMenuTracePrimed = true;
            char line[960] = {};
            int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
                "[ffx-hooks] NMTRACE base #%llu", g_nativeMenuTraceSeq);
            for (size_t i = 0; i < count && n > 0; ++i) {
                n += _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE,
                    " %s=%X", kNativeMenuTraceFields[i].name, cur[i]);
            }
            memcpy(g_nativeMenuTracePrev, cur, sizeof(cur));
            Log("%s\n", line);
        } else {
            char line[960] = {};
            int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
                "[ffx-hooks] NMTRACE #%llu", g_nativeMenuTraceSeq);
            for (size_t i = 0; i < count && n > 0; ++i) {
                if (cur[i] == g_nativeMenuTracePrev[i]) continue;
                n += _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE,
                    " %s %X->%X", kNativeMenuTraceFields[i].name,
                    g_nativeMenuTracePrev[i], cur[i]);
            }
            if (n > 0 && line[0] != '\0' && strstr(line, "->") != nullptr) {
                memcpy(g_nativeMenuTracePrev, cur, sizeof(cur));
                Log("%s\n", line);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        /* A bad read mid-transition must never fault the Present thread. */
    }
    InterlockedExchange(&g_nativeMenuTraceBusy, 0);
}

static const uint32_t RVA_FFX_EVENT_STRING_RESOLVE = 0x0046BEC0u; /* IDA FFX_EventStringResolve @ 0x86BEC0 */

static const uint32_t RVA_ARENA_CAPTURE_COUNTS = 0x00D30C9Cu;
static const uint32_t RVA_ARENA_UNLOCK_FLAGS   = 0x00D30D04u;
// Dark Aeon defeat: FFXED save bits (3273..3280 bit7) may be zero in RAM; game runtime uses SaveData+0x18F4.
static const uint32_t RVA_DARK_AEON_FFXED_BASE   = 0x00D2D759u; // save+3273 (FFXED Optional Bosses)
static const uint32_t RVA_DARK_AEON_RUNTIME_BASE = 0x00D2E384u; // save+0x18F4 (vanilla runtime array, 9 bytes)
// RVA_SAVE_GIL 0x00D307D8u â€” defined in ffx_addresses.h
static const uint32_t RVA_SCRIPTED_ENCOUNTER_0 = 0x00D2CA20u;
static const uint32_t RVA_SCRIPTED_ENCOUNTER_1 = 0x00D2CA24u;
static const uint32_t RVA_SCRIPTED_FORMATION   = 0x00D2CA28u;
static const uint32_t RVA_MS_BATTLE_ENCOUNT_EXE = 0x00380DE0u;
static const uint32_t RVA_BATTLE_LAUNCH_7002 = 0x003A3550u; // IDA VA 0x7A3550, Battle.launchBattle
static const uint32_t RVA_BATTLE_REQUEST_781D60 = 0x00381D60u; // IDA VA 0x781D60, queue battleToken
static const uint32_t RVA_COMMON_SET_BATTLE_FLAGS = 0x0046FBB0u; // IDA VA 0x86FBB0, Common.SetBattleFlags backend
static const uint32_t RVA_BATTLE_QUEUE_GATE = 0x00D2A8E0u; // byte_112A8E0, sub_7817C0 gate
static const uint32_t RVA_BATTLE_QUEUE_STATE = 0x00D2A8E2u; // n2 / encounter state
static const uint32_t RVA_BATTLE_QUEUE_MODE = 0x00D2A9D4u; // word_112A9D4
static const uint32_t RVA_BATTLE_BUSY_GATE = 0x00D2CA2Cu; // byte_112CA2C
static const uint32_t RVA_BATTLE_QUEUE_FIELD = 0x00D2C254u; // dword_112C254, HIWORD consumed by sub_790C60
static const uint32_t RVA_BATTLE_QUEUE_GROUP = 0x00D2C258u; // byte_112C258
static const uint32_t RVA_BATTLE_QUEUE_FORMATION = 0x00D2C259u; // byte_112C259
static const uint32_t RVA_BATTLE_NAME = 0x00D2C25Au; // g_FFX_Battle_EncounterName (13 bytes)
static const uint32_t RVA_BATTLE_FLAGS_0 = 0x00F26B08u; // dword_1326B08
static const uint32_t RVA_BATTLE_FLAGS_1 = 0x00F26B10u; // dword_1326B10
static const uint32_t RVA_BATTLE_FLAGS_2 = 0x00F26B14u; // dword_1326B14
static const int ARENA_CAPTURE_COUNT_LEN = 104;
static const int ARENA_UNLOCK_FLAG_LEN = 35;
static const int ARENA_DARK_FLAG_LEN = 9;
static const int ARENA_PLUS_COMBO_COUNT = 8;
static const int ARENA_PLUS_PRESET_COMBO_COUNT = 5;
static const int ARENA_PLUS_CUSTOM_MIX_COMBO_COUNT = 3;
static const int ARENA_PLUS_LABEL_CAP = 64;
static const int ARENA_PLUS_MAX_MENU_ROWS = 1100;
static const int ARENA_PLUS_VISIBLE_PAGE = 8;
static const int ARENA_PLUS_ULTRA_CHOICE_COUNT = 8;
static const int ARENA_PLUS_ULTRA_ROW_SCENERY = 0;
static const int ARENA_PLUS_ULTRA_ROW_CAMERA = 1;
static const int ARENA_PLUS_ULTRA_ROW_FIRST_CHOICE = 2;
static const int ARENA_PLUS_ULTRA_ROW_AUTO = 10;
static const int ARENA_PLUS_ULTRA_ROW_POSITIONS = 11;
static const int ARENA_PLUS_ULTRA_ROW_NATIVE = 12;
static const int ARENA_PLUS_ULTRA_ROW_REMOVE_LAST = 13;
static const int ARENA_PLUS_ULTRA_ROW_CLEAR = 14;
static const int ARENA_PLUS_ULTRA_ROW_EXPORT = 15;
static const int ARENA_PLUS_ULTRA_ROW_LIBRARY = 16;
static const int ARENA_PLUS_ULTRA_ROW_LAUNCH = 17;
static const int ARENA_PLUS_ULTRA_ROW_BACK = 18;
static const int ARENA_PLUS_ULTRA_ROW_COUNT = 19;
static const int ARENA_PLUS_POSITION_ROW_COUNT = 7;

/* Legacy flat row indices â€” launch helpers still reference dark/combo slots. */
static const int ARENA_PLUS_ROW_SAFE_BATTLE = 0;
static const int ARENA_PLUS_ROW_FIRST_DARK = 1;
static const int ARENA_PLUS_ROW_FIRST_COMBO = ARENA_PLUS_ROW_FIRST_DARK + ARENA_DARK_FLAG_LEN;
static const int ARENA_PLUS_ROW_BACK = ARENA_PLUS_ROW_FIRST_COMBO + ARENA_PLUS_COMBO_COUNT;
static const int ARENA_PLUS_ROW_COUNT = ARENA_PLUS_ROW_BACK + 1;

enum class ArenaPlusMenuKind : int {
    Hub = 0,
    DarkRematch = 1,
    AeonGauntlet = 2,
    CustomMix = 3,
    Ultra = 4,
    Positions = 5,
    Library = 6,
    LibraryItem = 7,
    Rename = 8,
    Scenery = 9,
    Monsters = 10,
    Soundtrack = 11,
    Formation = 12,
    Search = 13,
    Battles = 14,
    BattleDetail = 15,
};

static const int ARENA_PLUS_HUB_ROW_SAFE = 0;
static const int ARENA_PLUS_HUB_ROW_DARK = 1;
static const int ARENA_PLUS_HUB_ROW_GAUNTLET = 2;
static const int ARENA_PLUS_HUB_ROW_MIX = 3;
static const int ARENA_PLUS_HUB_ROW_ULTRA = 4;
static const int ARENA_PLUS_HUB_ROW_BACK = 5;
static const int ARENA_PLUS_HUB_ROW_COUNT = 6;

static ArenaPlusMenuKind g_arenaPlusMenuKind = ArenaPlusMenuKind::Hub;
static FfxHooks::ArenaMonsters::Category g_arenaMonsterCategory=FfxHooks::ArenaMonsters::Category::Fiends;
static std::array<const FfxHooks::ArenaMonsters::Entry*,344> g_arenaMonsterRows{};
static int g_arenaMonsterCount=0;
static int g_arenaMonsterSelectedRow=0;
static bool ArenaPlus_IsUltraChild(ArenaPlusMenuKind kind) {
    return kind==ArenaPlusMenuKind::Monsters || kind==ArenaPlusMenuKind::Soundtrack || kind==ArenaPlusMenuKind::Formation || kind==ArenaPlusMenuKind::Search || kind==ArenaPlusMenuKind::Battles || kind==ArenaPlusMenuKind::BattleDetail;
}

static int g_arenaPlusActiveRowCount = ARENA_PLUS_HUB_ROW_COUNT;
static FfxHooks::CustomMixUltra::SelectionInput g_arenaPlusUltraSelection{};
static char g_arenaPlusUltraPreview[ARENA_PLUS_LABEL_CAP] = {};
static uint8_t g_arenaPlusMixRequiredSlots = 0;
static char g_arenaMonsterQuery[41]={},g_arenaSceneryQuery[41]={},g_arenaBattleQuery[41]={};
static bool g_arenaMonsterSearchAll=false;
static ArenaPlusMenuKind g_arenaSearchParent=ArenaPlusMenuKind::Monsters;
static std::array<FfxHooks::ArenaScenery::Choice,FfxHooks::ArenaScenery::kChoiceCount> g_arenaSceneryRows{};
static int g_arenaSceneryCount=0;
static std::array<size_t,1024> g_arenaBattleRows{};
static int g_arenaBattleCount=0;
static const FfxHooks::ArenaBattleProgram::Encounter* g_arenaBattleDetail=nullptr;
static char* ArenaPlus_SearchQuery(ArenaPlusMenuKind kind) {
    return kind==ArenaPlusMenuKind::Scenery?g_arenaSceneryQuery:kind==ArenaPlusMenuKind::Battles?g_arenaBattleQuery:g_arenaMonsterQuery;
}
static bool ArenaPlus_ShowFormationPane() {
    return !g_arenaPlusMixRequiredSlots && (g_arenaPlusMenuKind==ArenaPlusMenuKind::Ultra ||
        g_arenaPlusMenuKind==ArenaPlusMenuKind::Scenery || ArenaPlus_IsUltraChild(g_arenaPlusMenuKind));
}

static int g_arenaPlusUltraSelectedRow = 0;
static FfxHooks::ArenaPositions::Layout g_arenaPositionDraft{};
static uint8_t g_arenaPositionSlot = 0;
static int g_arenaPositionRow = 0;
static std::vector<FfxHooks::ArenaMixLibrary::Entry> g_arenaLibraryEntries;
static FfxHooks::ArenaMixLibrary::Entry g_arenaLibraryEntry;
static ArenaPlusMenuKind g_arenaLibraryParent = ArenaPlusMenuKind::CustomMix;
static int g_arenaLibraryRow = 0;
static char g_arenaLibraryStatus[64] = {};
static std::string g_arenaMixName = "Custom Mix";
static std::string g_arenaMixLoadedId;
static SRWLOCK g_arenaRenameLock = SRWLOCK_INIT;
static char g_arenaRenameDraft[41] = {};
static bool g_arenaRenameSelectAll = false;
static volatile LONG g_arenaRenameActive = 0, g_arenaRenameConfirm = 0, g_arenaRenameCancel = 0;
static bool ArenaMixRenameInputActive() { return InterlockedCompareExchange(&g_arenaRenameActive,0,0)!=0; }

static void ArenaMixRenameAbort() {
    InterlockedExchange(&g_arenaRenameActive,0);
    InterlockedExchange(&g_arenaRenameConfirm,0);
    InterlockedExchange(&g_arenaRenameCancel,0);
}
static bool ArenaMixRenameMessage(UINT message, WPARAM character) {
    if (InterlockedCompareExchange(&g_arenaRenameActive,0,0)==0) return false;
    if (message==WM_KILLFOCUS || (message==WM_ACTIVATEAPP && !character)) {
        ArenaMixRenameAbort(); return false;
    }
    if (message==WM_KEYDOWN) {
        if(character==VK_RETURN) InterlockedExchange(&g_arenaRenameConfirm,1);
        else if(character==VK_ESCAPE) InterlockedExchange(&g_arenaRenameCancel,1);
        else if(character=='A' && (GetAsyncKeyState(VK_CONTROL)&0x8000)) {
            AcquireSRWLockExclusive(&g_arenaRenameLock);g_arenaRenameSelectAll=true;ReleaseSRWLockExclusive(&g_arenaRenameLock);
        } else if(character==VK_DELETE) {
            AcquireSRWLockExclusive(&g_arenaRenameLock);g_arenaRenameDraft[0]=0;g_arenaRenameSelectAll=false;ReleaseSRWLockExclusive(&g_arenaRenameLock);
        }
        return true;
    }
    if (message!=WM_CHAR) return false;
    AcquireSRWLockExclusive(&g_arenaRenameLock);
    size_t length=strlen(g_arenaRenameDraft);
    if(character==8){if(g_arenaRenameSelectAll)length=0;else if(length)--length;g_arenaRenameDraft[length]=0;g_arenaRenameSelectAll=false;}
    else if(character>=32 && character<=126){if(g_arenaRenameSelectAll){length=0;g_arenaRenameDraft[0]=0;g_arenaRenameSelectAll=false;}if(length<40){g_arenaRenameDraft[length]=static_cast<char>(character);g_arenaRenameDraft[length+1]=0;}}
    ReleaseSRWLockExclusive(&g_arenaRenameLock);
    return true;
}
static bool ArenaLibraryPaths(std::string* root, std::string* legacy) {
    char current[MAX_PATH]={},old[MAX_PATH]={};
    if(!ModuleRelativePath("config\\arena-mixes",current,sizeof(current)) ||
       !ModuleRelativePath("arena_formations",old,sizeof(old))) return false;
    *root=current;*legacy=old;return true;
}
static void ArenaLibraryRefresh() {
    std::string root,legacy;
    g_arenaLibraryEntries = ArenaLibraryPaths(&root,&legacy)
        ? FfxHooks::ArenaMixLibrary::Scan(root,legacy) : std::vector<FfxHooks::ArenaMixLibrary::Entry>{};
    g_arenaLibraryRow=0;
}
static void ArenaLibraryStatus(const std::string& message) {
    strncpy_s(g_arenaLibraryStatus,message.c_str(),_TRUNCATE);
}
static FfxHooks::ArenaMix::Rules ArenaPlus_MixRules();

static const char* const kArenaPlusUltraChoiceNames[ARENA_PLUS_ULTRA_CHOICE_COUNT] = {
    "Valefor", "Ifrit", "Ixion", "Shiva",
    "Bahamut", "Yojimbo", "Anima", "Magus",
};

static const FfxHooks::CustomMixUltra::MonsterChoice
    kArenaPlusUltraChoices[ARENA_PLUS_ULTRA_CHOICE_COUNT] = {
        FfxHooks::CustomMixUltra::MonsterChoice::Valefor,
        FfxHooks::CustomMixUltra::MonsterChoice::Ifrit,
        FfxHooks::CustomMixUltra::MonsterChoice::Ixion,
        FfxHooks::CustomMixUltra::MonsterChoice::Shiva,
        FfxHooks::CustomMixUltra::MonsterChoice::Bahamut,
        FfxHooks::CustomMixUltra::MonsterChoice::Yojimbo,
        FfxHooks::CustomMixUltra::MonsterChoice::Anima,
        FfxHooks::CustomMixUltra::MonsterChoice::Magus,
    };

// â”€â”€ SIN Curse submenu state â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// -- S.I.N. RAM submenu state --
#define SIN_RAM_ROW_ENABLED       0
#define SIN_RAM_ROW_DISTRIBUTION  1
#define SIN_RAM_ROW_SEED          2
#define SIN_RAM_ROW_SHUFFLE       3
#define SIN_RAM_ROW_AREA          4
#define SIN_RAM_ROW_SAVE          5
#define SIN_RAM_ROW_GUIDE         6
#define SIN_RAM_ROW_BACK          7
#define SIN_RAM_ROW_COUNT         8

static NativeMenu::Menu  g_sinMenu             = { 0 };
static int               g_sinMenuResult       = 0;
static bool              g_sinMenuClosed       = false;
static volatile LONG     g_sinWantOpen         = 0;
static int               g_sinLastEdge         = 0; // Preserve the rising-edge state across menu respawns.
static int               g_sinConfirmTimer     = 0; // Debounce confirmation without delaying navigation.
static int               g_sinLastRow          = SIN_RAM_ROW_ENABLED;
static float             g_sinEasedRowY        = -1.0f;
static FfxHooks::SinRam::Config g_sinDraft     = {};
static uint16_t g_sinPreviewField=310;
static bool g_sinShowGuide=false,g_sinSeedEditing=false;
static char g_sinNotice[96] = {};
static bool              g_sinDraftActive      = false;
enum class SinRamSaveFeedback : unsigned char {
    None = 0,
    Saved,
    Failed,
};
// WHY: Save confirmation destroys and respawns the native menu object. A bounded value state
// keeps the real persistence result visible across that respawn without owning any extra I/O.
static SinRamSaveFeedback g_sinSaveFeedback = SinRamSaveFeedback::None;

static unsigned char     g_sinLabels[SIN_RAM_ROW_COUNT][64] = {};
static unsigned char     g_sinSubLabels[SIN_RAM_ROW_COUNT][64] = {};

// Row colors (ARGB)
static const unsigned int kSinStatusTop    = 0xD06B382Eu;
static const unsigned int kSinStatusBot    = 0xD0271110u;
static const unsigned int kSinRegionTop    = 0x68283850u;
static const unsigned int kSinRegionBot    = 0x48182028u;
static const unsigned int kSinBackTop      = 0xC0222A34u;
static const unsigned int kSinBackBot      = 0xC00A1018u;

static void SinCurse_BuildLabels();

static void SinRam_ClearSaveFeedback() {
    g_sinSaveFeedback = SinRamSaveFeedback::None;
    g_sinNotice[0]=0;
}

// â”€â”€ SIN menu draw â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

static int __cdecl SinCurse_DrawCb(int obj) {
    using namespace NativeMenu;
    static int frame=0;const int F=++frame;
    const int selected=RdW(obj,O_SELECTED);
    DrawMenuBackdrop();DrawMenuNeonFrame(F);
    auto text=[](const char* value,float x,float y,bool smallFont){unsigned char encoded[128]{};EncodeLabel(value,encoded,sizeof(encoded));if(smallFont)DrawStringSub(encoded,x,y);else DrawString(encoded,x,y);};
    DrawMenuGlassPanel(NX(0.047f),NY(0.054f),NW(0.906f),NH(0.126f),F,0);
    text("S.I.N. - Curses of Sin",NX(0.071f),NY(0.081f),false);
    text("Seeded encounters in Macalania",NX(0.071f),NY(0.137f),true);
    const float left=NX(0.075f),top=NY(0.235f),width=NW(0.325f),step=NH(0.066f),height=NH(0.057f);
    BOOL animations=FALSE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);
    const bool motion=animations && !EnvFlagEnabled("FFXHOOKS_REDUCED_MOTION") && !EnvFlagEnabled("FFXHOOKS_ARENAPLUS_REDUCED_MOTION");
    const float selectedY=top+selected*step;
    g_sinEasedRowY=FfxHooks::F8Ui::ResolveSelectionRowY(!motion,g_sinEasedRowY,selectedY);
    for(int row=0;row<SIN_RAM_ROW_COUNT;++row){
        const float y=top+row*step;
        const unsigned base=row==SIN_RAM_ROW_SAVE?0x40334D49u:row==SIN_RAM_ROW_BACK?0x30263340u:0x403E345Cu;
        DrawSolidRect(left,y,width,height,base,base-0x18000000u);
        if(row==selected){
            const unsigned alpha=motion?0x44u+static_cast<unsigned>(Osc01(F,44)*32.0f):0x58u;
            DrawSolidRect(left,g_sinEasedRowY,width,height,(alpha<<24)|0x00305068u,(alpha<<24)|0x00182038u);
            DrawSolidRect(left,g_sinEasedRowY+height-MenuBorderPx()*0.45f,width,MenuBorderPx()*0.45f,kMenuNeonGreenLine,kMenuNeonGreenLineLo);
            DrawCursor(left-NW(0.026f),g_sinEasedRowY+NH(0.002f));
        }
        if(row==SIN_RAM_ROW_SEED && g_sinSeedEditing){
            char edit[64]{};AcquireSRWLockShared(&g_arenaRenameLock);_snprintf_s(edit,sizeof(edit),_TRUNCATE,"Seed: %s_",g_arenaRenameDraft);ReleaseSRWLockShared(&g_arenaRenameLock);
            text(edit,left+NW(0.015f),y+NH(0.017f),true);
        }else DrawStringSub(g_sinLabels[row],left+NW(0.015f),y+NH(0.017f));
    }
    const float pane=NX(0.425f),paneWidth=NW(0.51f);
    DrawMenuGlassPanel(pane,NY(0.215f),paneWidth,NH(0.60f),F,1);
    if(g_sinShowGuide){
        text("How curses spread",pane+NW(0.02f),NY(0.244f),false);
        const char* lines[]={"Threat comes from the curse, not a slider.","Each monster has its own compatible traits.","Random may leave an area without curses.","Fixed shares: 20%, 50% or 80% of the roster.","Changing screens produces another seeded layout.","A battle in progress keeps its assignment.","Save changes for the next natural encounter."};
        for(int i=0;i<7;++i)text(lines[i],pane+NW(0.02f),NY(0.32f+i*0.055f),true);
    }else{
        const auto status=FfxHooks::F7_SinRamStatus();
        const auto preview=FfxHooks::SinSpread::BuildAssignment(g_sinPreviewField,g_sinDraft.seed,status.areaVisit,
            static_cast<FfxHooks::SinSpread::Distribution>(g_sinDraft.distribution),g_sinDraft.enabled);
        text(FfxHooks::SinSpread::AreaName(g_sinPreviewField),pane+NW(0.02f),NY(0.244f),false);
        char line[128]{};
        _snprintf_s(line,sizeof(line),_TRUNCATE,"Area preview | %u of %u marked | T0 = unchanged",preview.cursed,preview.count);
        text(line,pane+NW(0.02f),NY(0.306f),true);
        for(unsigned i=0;i<preview.count;++i){
            const auto& item=preview.monsters[i];const auto* monster=FfxHooks::SinSpread::FindMonster(item.monster);
            const float y=NY(0.368f+i*(preview.count>5?0.056f:0.068f));
            const unsigned shade=item.threat==2?0x503F315Au:item.threat==1?0x40305060u:0x20243342u;
            DrawSolidRect(pane+NW(0.012f),y,paneWidth-NW(0.024f),NH(preview.count>5?0.048f:0.059f),shade,shade-0x10000000u);
            _snprintf_s(line,sizeof(line),_TRUNCATE,"%s - T%u %s",monster?monster->name:"Monster",item.threat,FfxHooks::SinSpread::Curse(item.curse).name);
            text(line,pane+NW(0.022f),y+NH(0.017f),true);
        }
        text("HP, AP, Gil: +10% per Threat.",pane+NW(0.02f),NY(0.715f),true);
        text("Stats: +5% per Threat, then +Threat.",pane+NW(0.02f),NY(0.750f),true);
        text(FfxHooks::SinAi::Ready()?"After Difficulty. Model growth: preview.":"Save and restart to enable curse scripts.",pane+NW(0.02f),NY(0.785f),true);
    }
    const auto runtime=FfxHooks::F7_SinRamStatus();
    const bool unsaved=!runtime.configValid || g_sinDraft.enabled!=runtime.config.enabled ||
        g_sinDraft.distribution!=runtime.config.distribution || g_sinDraft.seed!=runtime.config.seed;
    const char* notice=g_sinNotice[0]?g_sinNotice:(g_sinSeedEditing?"Type a seed. Enter confirms; Esc cancels.":
        unsaved?"Unsaved changes - choose Save for next encounter.":
        runtime.state==FfxHooks::F7SinRamState::Unavailable?"Battle runtime unavailable; restart with the native battle features enabled.":
        runtime.config.enabled && !FfxHooks::SinAi::Ready()?FfxHooks::SinAi::Detail():
        runtime.currentAssignment?FfxHooks::SinAi::Detail():"Changes are saved for the next natural encounter.");
    text(notice,NX(0.075f),NY(0.846f),true);
    DrawMenuGlassPanel(NX(0.047f),NY(0.90f),NW(0.906f),NH(0.063f),F,1);
    float hint=NX(0.071f);const float y=NY(0.916f);
    hint=DrawInputHint(hint,y,PC_PAD_UP,PC_PAD_DOWN,PC_KB_UP,PC_KB_DOWN,"Navigate",0xFFFFFFFFu);
    hint=DrawInputHint(hint,y,PC_PAD_LEFT,PC_PAD_RIGHT,PC_KB_LEFT,PC_KB_RIGHT,"Change",0xFFFFFFFFu);
    hint=DrawInputHint(hint,y,PC_PAD_FACE_D,PC_SKIP,PC_KB_ENTER,PC_SKIP,"Select",0xFFFFFFFFu);
    DrawInputHint(hint,y,PC_PAD_FACE_R, PC_SKIP, PC_KB_BACKSPACE, PC_SKIP, "Back",0xFFFFFFFFu);
    return obj;
}

static int __cdecl SinCurse_InputCb(int obj) {
    if (!F7IsForegroundWindow()) {
        F7RequestClose(FfxHooks::F7Ui::CloseSource::FocusLost);
        return obj;
    }
    if(g_sinSeedEditing){
        if(InterlockedExchange(&g_arenaRenameCancel,0)){ArenaMixRenameAbort();g_sinSeedEditing=false;g_sinNotice[0]=0;SinCurse_BuildLabels();}
        else if(InterlockedExchange(&g_arenaRenameConfirm,0)){
            char seed[41]{};AcquireSRWLockShared(&g_arenaRenameLock);strcpy_s(seed,g_arenaRenameDraft);ReleaseSRWLockShared(&g_arenaRenameLock);
            uint32_t parsed=0;
            if(FfxHooks::SinSpread::ParseSeed(seed,&parsed)){g_sinDraft.seed=parsed;ArenaMixRenameAbort();g_sinSeedEditing=false;SinRam_ClearSaveFeedback();SinCurse_BuildLabels();NativeMenu::PlaySfx(4);}
            else {strncpy_s(g_sinNotice,"Enter a whole seed from 0 to 4294967295.",_TRUNCATE);NativeMenu::PlaySfx(3);}
        }
        g_sinConfirmTimer=12;g_sinLastEdge=NativeMenu::PadEdge()&0x60;return obj;
    }
    const int selectionBeforeMouse = NativeMenu::RdW(obj, NativeMenu::O_SELECTED);
    const F7MouseInputResult mouse = F7ListMouseTick(
        obj, NativeMenu::NX(0.075f), NativeMenu::NY(0.235f),
        NativeMenu::NW(0.325f), NativeMenu::NH(0.066f), NativeMenu::NH(0.057f),
        NativeMenu::RdW(obj, NativeMenu::O_COUNT), NativeMenu::RdW(obj, NativeMenu::O_PAGE));
    if (NativeMenu::RdW(obj, NativeMenu::O_SELECTED) != selectionBeforeMouse) {
        g_sinEasedRowY = -1.0f;
    }
    // The confirmation cooldown advances every frame but blocks only Enter, not navigation.
    if (g_sinConfirmTimer > 0) --g_sinConfirmTimer;

    int dir = NativeMenu::PadDir();
    dir = FfxHooks::F7Ui::ResolveDirectionalInput(dir, mouse.ownsDirectionalFrame);
    const int edge = NativeMenu::PadEdge();
    const int confirmEdge = edge & 0x20;
    const int cancelEdge   = edge & 0x40;
    // Keep edge state across respawns so a held button cannot confirm twice.
    const bool confirmPressed =
        ((confirmEdge != 0) && !(g_sinLastEdge & 0x20) && (g_sinConfirmTimer == 0)) ||
        (mouse.confirm && g_sinConfirmTimer == 0);
    const bool cancelPressed  = (cancelEdge != 0) && !(g_sinLastEdge & 0x40);
    g_sinLastEdge = edge & 0x60;

    int sel = NativeMenu::RdW(obj, NativeMenu::O_SELECTED);
    const int count = NativeMenu::RdW(obj, NativeMenu::O_COUNT);
    int top = NativeMenu::RdW(obj, NativeMenu::O_TOP);
    const int page = NativeMenu::RdW(obj, NativeMenu::O_PAGE);
    if (count > 0) {
        bool draftChanged = false;
        if ((dir & 0x8000) || (dir & 0x2000)) {
            if (sel == SIN_RAM_ROW_ENABLED) {
                const bool nextEnabled = (dir & 0x2000) != 0;
                draftChanged = g_sinDraft.enabled != nextEnabled;
                g_sinDraft.enabled = nextEnabled;
            } else if (sel == SIN_RAM_ROW_DISTRIBUTION) {
                const unsigned choices[]={0,20,50,80};unsigned index=0;
                for(;index<3 && choices[index]!=g_sinDraft.distribution;++index){}
                g_sinDraft.distribution=choices[(index+((dir&0x8000)?3u:1u))%4u];draftChanged=true;
            } else if (sel == SIN_RAM_ROW_SEED) {
                const uint32_t previous=g_sinDraft.seed;
                if((dir&0x8000) && g_sinDraft.seed>0)--g_sinDraft.seed;
                if((dir&0x2000) && g_sinDraft.seed<UINT32_MAX)++g_sinDraft.seed;
                draftChanged=previous!=g_sinDraft.seed;
            } else if (sel == SIN_RAM_ROW_AREA) {
                g_sinPreviewField=g_sinPreviewField==310?340:310;draftChanged=true;
            }
            if (draftChanged) {
                SinRam_ClearSaveFeedback();
                SinCurse_BuildLabels();
                NativeMenu::PlaySfx(1);
            }
        }
        if (dir & 0x1000) {
            sel = (sel > 0) ? (sel - 1) : (count - 1);
            NativeMenu::PlaySfx(1);
        } else if (dir & 0x4000) {
            sel = (sel < count - 1) ? (sel + 1) : 0;
            NativeMenu::PlaySfx(1);
        }
        if (sel < 0) sel = 0;
        if (sel > count - 1) sel = count - 1;
        if (sel < top) top = sel;
        if (sel >= top + page) top = sel - page + 1;
        if (top > count - page) top = count - page;
        if (top < 0) top = 0;

        NativeMenu::WrW(obj, NativeMenu::O_SELECTED, static_cast<int16_t>(sel));
        NativeMenu::WrW(obj, NativeMenu::O_TOP,     static_cast<int16_t>(top));

        if (confirmPressed) {
            if (sel == SIN_RAM_ROW_BACK) {
                NativeMenu::PlaySfx(4);
            } else if (sel != SIN_RAM_ROW_SAVE) {
                // WHY: Save owns its SFX after the atomic saver returns, so failure can never
                // inherit an optimistic confirm sound from this generic input path.
                NativeMenu::PlaySfx(1);
            }
            g_sinLastRow = sel;
            g_sinConfirmTimer = 10;
            g_sinMenuResult = sel;
            g_sinMenuClosed = true;
        } else if (cancelPressed) {
            NativeMenu::PlaySfx(4);
            g_sinMenuResult = -1;
            g_sinMenuClosed = true;
        }
    }
    return obj;
}

// â”€â”€ SIN menu lifecycle â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

static NativeMenu::Poll SinCurse_PollMenu(const NativeMenu::Menu& m) {
    if (g_sinMenuClosed) {
        if (g_sinMenuResult >= 0) return NativeMenu::Poll{ NativeMenu::POLL_CONFIRM, g_sinMenuResult };
        return NativeMenu::Poll{ NativeMenu::POLL_CANCEL, 0 };
    }
    return NativeMenu::Poll{ NativeMenu::POLL_NAV, 0 };
}

static void SinCurse_CloseMenu() {
    if (!g_sinMenu.obj) return;
    NativeMenu::WrB(g_sinMenu.obj, 65, 1);
    g_sinMenu.obj = 0;
    g_sinMenuClosed = false;
    g_sinMenuResult = 0;
}

static void SinCurse_BuildLabels() {
    char line[96]{};
    auto set=[](int row,const char* value){NativeMenu::EncodeLabel(value,g_sinLabels[row],64);g_sinSubLabels[row][0]=0;};
    _snprintf_s(line,sizeof(line),_TRUNCATE,"Curses: %s",g_sinDraft.enabled?"ON":"OFF");set(SIN_RAM_ROW_ENABLED,line);
    _snprintf_s(line,sizeof(line),_TRUNCATE,"Cursed monsters: %s",FfxHooks::SinSpread::DistributionName(static_cast<FfxHooks::SinSpread::Distribution>(g_sinDraft.distribution)));set(SIN_RAM_ROW_DISTRIBUTION,line);
    _snprintf_s(line,sizeof(line),_TRUNCATE,"Seed: %u",static_cast<unsigned>(g_sinDraft.seed));set(SIN_RAM_ROW_SEED,line);
    set(SIN_RAM_ROW_SHUFFLE,"Generate a new seed");
    set(SIN_RAM_ROW_AREA,g_sinPreviewField==310?"Preview: Woods":"Preview: Snowfield");
    set(SIN_RAM_ROW_SAVE,g_sinSaveFeedback==SinRamSaveFeedback::Saved?"Saved for next encounter":
        g_sinSaveFeedback==SinRamSaveFeedback::Failed?"Save failed - memory only":"Save for next encounter");
    set(SIN_RAM_ROW_GUIDE,g_sinShowGuide?"Show area preview":"How curses work");set(SIN_RAM_ROW_BACK,"Back");
}

#include "hooks/EquipmentWorkshopMenu.inl"

static void HydrateNativeMenuBattleCheats() {
    const FfxHooks::F8FlagSpec* flag =
        FfxHooks::FindF8Flag("cheats.invincible_party");
    if (!flag) return;
    const FfxHooks::Config::BoolGateResult effective = FfxHooks::ResolveF8Flag(*flag);
    for (int row = 0; row < NativeMenu::kRowCount; ++row) {
        if (NativeMenu::g_rows[row].action != NativeMenu::ACT_BATTLE_CHEATS) continue;
        NativeMenu::g_rowValue[row] = effective.value ? 1 : 0;
        NativeMenu::g_rowEdited[row] = true;
        return;
    }
}

static NativeMenu::Menu SpawnHydratedNativeMenu() {
    HydrateNativeMenuBattleCheats();
    NativeMenu::Menu menu = NativeMenu::SpawnMenu();
    if (menu.obj) {
        F7SeedPointerForDestination();
        F7AcquireCursorOwnership();
        g_f7UiModalState.open = true;
        g_f7UiModalState.inputBlockOwned = false;
        g_f7UiModalState.nativeGateOwned = true;
        g_f7UiModalState.forceGateOwned = true;
        g_f7UiModalState.draftActive = false;
        g_f7UiModalState.selection = 0;
        g_f7UiModalState.firstVisible = 0;
        g_f7UiModalState.submenu = -1;
        InterlockedExchange(&g_f7MouseWheelDelta, 0);
    }
    return menu;
}

static NativeMenu::Menu SinCurse_SpawnMenu() {
    if (!g_sinDraftActive) {
        const FfxHooks::F7ConfigStateSnapshot snapshot = FfxHooks::F7_GetConfigSnapshot();
        // WHY: an invalid persisted member is displayed as INVALID but never becomes an edit
        // seed. The draft begins from the same canonical OFF/T0 value used by the parser.
        g_sinDraft = snapshot.sinRamValid ? snapshot.sinRam : FfxHooks::SinRam::Config{};
        g_sinDraft.seeded=true;g_sinDraft.threatLevel=0;g_sinSeedEditing=false;
        const auto location=FfxHooks::F7_SinRamStatus();
        if(location.areaField==310 || location.areaField==340)g_sinPreviewField=location.areaField;
        g_sinDraftActive = true;
        SinRam_ClearSaveFeedback();
    }
    SinCurse_BuildLabels();
    g_sinEasedRowY = -1.0f;

    int obj = NativeMenu::Alloc();
    if (!obj) {
        g_sinDraft = {};
        g_sinDraftActive = false;
        SinRam_ClearSaveFeedback();
        return NativeMenu::Menu{ 0 };
    }
    F7SeedPointerForDestination();

    int initSel = g_sinLastRow;
    if (initSel < 0 || initSel >= SIN_RAM_ROW_COUNT) initSel = SIN_RAM_ROW_ENABLED;

    NativeMenu::WrW(obj, NativeMenu::O_COUNT,    static_cast<int16_t>(SIN_RAM_ROW_COUNT));
    NativeMenu::WrW(obj, NativeMenu::O_PAGE,     static_cast<int16_t>(SIN_RAM_ROW_COUNT));
    NativeMenu::WrW(obj, NativeMenu::O_TOP,      0);
    NativeMenu::WrW(obj, NativeMenu::O_SELECTED, static_cast<int16_t>(initSel));
    NativeMenu::WrB(obj, NativeMenu::O_SLOTS,    1);
    NativeMenu::WrB(obj, NativeMenu::O_CANCEL,   1);
    NativeMenu::WrB(obj, NativeMenu::O_GROUP62,  2);
    NativeMenu::WrB(obj, NativeMenu::O_GROUP63,  1);
    NativeMenu::WrP(obj, NativeMenu::O_ENTER,     (void*)0);
    NativeMenu::WrP(obj, NativeMenu::O_UPDATE,    (void*)(uintptr_t)&SinCurse_InputCb);
    NativeMenu::WrP(obj, NativeMenu::O_DRAW,      (void*)(uintptr_t)&SinCurse_DrawCb);
    NativeMenu::WrP(obj, NativeMenu::O_AUX,       (void*)(uintptr_t)&NativeMenu::OurAux);
    NativeMenu::WrP(obj, NativeMenu::O_VALIDATOR, (void*)0);

    g_sinMenuClosed = false;
    g_sinMenuResult = 0;
    g_sinLastEdge = NativeMenu::PadEdge() & 0x60;
    NativeMenu::Register(obj);

    return NativeMenu::Menu{ obj };
}

static void SinCurse_HandleConfirm(int row) {
    if (row == SIN_RAM_ROW_BACK) {
        F7CloseTransition(
            FfxHooks::F7Ui::CloseSource::BackRow,
            FfxHooks::F7Ui::CloseDestination::Hub);
        g_sinDraftActive = false;
        g_sinSeedEditing = false;
        ArenaMixRenameAbort();
        return;
    }
    if(row==SIN_RAM_ROW_ENABLED){SinRam_ClearSaveFeedback();g_sinDraft.enabled=!g_sinDraft.enabled;}
    else if(row==SIN_RAM_ROW_DISTRIBUTION){const unsigned choices[]={0,20,50,80};unsigned i=0;for(;i<3 && choices[i]!=g_sinDraft.distribution;++i){}g_sinDraft.distribution=choices[(i+1)%4];SinRam_ClearSaveFeedback();}
    else if(row==SIN_RAM_ROW_SEED){
        ArenaMixRenameAbort();AcquireSRWLockExclusive(&g_arenaRenameLock);
        _snprintf_s(g_arenaRenameDraft,sizeof(g_arenaRenameDraft),_TRUNCATE,"%u",static_cast<unsigned>(g_sinDraft.seed));g_arenaRenameSelectAll=true;
        ReleaseSRWLockExclusive(&g_arenaRenameLock);g_sinSeedEditing=true;InterlockedExchange(&g_arenaRenameActive,1);
    }else if(row==SIN_RAM_ROW_SHUFFLE){
        g_sinDraft.seed=FfxHooks::SinSpread::Mix(g_sinDraft.seed^static_cast<uint32_t>(GetTickCount64())^0x53494E31u);SinRam_ClearSaveFeedback();
    }else if(row==SIN_RAM_ROW_AREA){g_sinPreviewField=g_sinPreviewField==310?340:310;}
    else if(row==SIN_RAM_ROW_GUIDE){g_sinShowGuide=!g_sinShowGuide;}
    else if(row==SIN_RAM_ROW_SAVE){
        g_sinDraft.seeded=true;g_sinDraft.threatLevel=0;
        const bool saved=FfxHooks::F7_SetSinRamConfig(g_sinDraft) && FfxHooks::F7_SaveConfig();
        g_sinSaveFeedback = saved ? SinRamSaveFeedback::Saved : SinRamSaveFeedback::Failed;
        const bool restart=g_sinDraft.enabled && (FfxHooks::F7_SinRamStatus().state==FfxHooks::F7SinRamState::Unavailable || !FfxHooks::SinAi::Ready());
        strncpy_s(g_sinNotice,saved?(restart?"Saved. Restart the game to enable the battle runtime.":"Saved. The next natural encounter uses these settings."):"Unable to save; changes are active only for this session.",_TRUNCATE);
        NativeMenu::PlaySfx(saved ? 4 : 3);
        Log("[ffx-hooks] S.I.N. seeded config save=%d enabled=%d distribution=%u seed=%u\n",saved?1:0,g_sinDraft.enabled?1:0,g_sinDraft.distribution,static_cast<unsigned>(g_sinDraft.seed));
    }
    g_sinMenu=SinCurse_SpawnMenu();if(!g_sinMenu.obj)g_nativeMenu=SpawnHydratedNativeMenu();
}

static const char* kArenaPlusDarkNames[ARENA_DARK_FLAG_LEN] = {
    "Dark Valefor", "Dark Ifrit", "Dark Ixion", "Dark Shiva", "Dark Bahamut",
    "Dark Yojimbo", "Dark Anima", "Dark Magus Sisters", "Penance"
};

struct ArenaPlusDarkFlagSpec {
    uint32_t byteRva;
    uint8_t bit;
};

// FFXED save offsets 3273..3280 (bit 7). Runtime mirror often stale; pair with RVA_DARK_AEON_RUNTIME_BASE.
static const ArenaPlusDarkFlagSpec kArenaPlusDarkFlagSpecs[ARENA_DARK_FLAG_LEN] = {
    { RVA_DARK_AEON_FFXED_BASE + 0u, 7 }, // save+3273 Dark Valefor
    { RVA_DARK_AEON_FFXED_BASE + 1u, 7 }, // save+3274 Dark Ifrit
    { RVA_DARK_AEON_FFXED_BASE + 2u, 7 }, // save+3275 Dark Ixion
    { RVA_DARK_AEON_FFXED_BASE + 3u, 7 }, // save+3276 Dark Shiva
    { RVA_DARK_AEON_FFXED_BASE + 4u, 7 }, // save+3277 Dark Bahamut
    { RVA_DARK_AEON_FFXED_BASE + 5u, 7 }, // save+3278 Dark Yojimbo
    { RVA_DARK_AEON_FFXED_BASE + 6u, 7 }, // save+3279 Dark Anima
    { RVA_DARK_AEON_FFXED_BASE + 7u, 7 }, // save+3280 Dark Magus Sisters
    { RVA_DARK_AEON_RUNTIME_BASE + 8u, 0 }, // Penance: runtime byte @ save+0x18FC
};

#define ARENA_PLUS_MUSIC_TRACK_DEFAULT 145  /* Challenge â€” all Arena+ F7 rows (Dark Aeons + Penance) */

/* Legacy per-row table removed: every Arena+ boss uses ARENA_PLUS_MUSIC_TRACK_DEFAULT unless
 * overridden by arena_plus_music_<row>.txt / FFXHOOKS_ARENAPLUS_MUSIC_TRACK_<N>. */

struct ArenaPlusBossRoute {
    int field;
    int group;
    int formation;
    uint32_t battleToken;
    uint32_t transition;
    const char* battleId;
    const char* evidence;
};

static const ArenaPlusBossRoute kArenaPlusBossRoutes[ARENA_DARK_FLAG_LEN] = {
    {  72,  1, 70, 0x00480046u, 0, "bsil07_70", "event token bsvr0000: bsil07_70 [00480046h]; legacy route RT2 OK" },
    { 353,  1, 70, 0x01610046u, 0, "bika03_70", "event token bika0300: bika03_70 [01610046h]; legacy route hit natural Cactuar" },
    { 303,  0, 70, 0x012F0046u, 0, "kami03_70", "event token kami0300: kami03_70 [012F0046h]; legacy route hit natural encounter" },
    { 340, 11, 70, 0x01540046u, 0, "mcyt00_70", "event token mcyt0000: mcyt00_70 [01540046h]; legacy route hit natural encounter" },
    { 521,  0, 70, 0x02090046u, 0, "dome06_70", "event token dome0600: dome06_70 [02090046h]" },
    { 430,  0, 70, 0x01AE0046u, 0, "nagi05_70", "event token nagi0500: nagi05_70 [01AE0046h]; legacy route hit natural encounter" },
    { 486,  0, 70, 0x01E60046u, 0, "mtgz01_70", "event token mtgz0000: mtgz01_70 [01E60046h]; legacy route did not visibly launch" },
    { 220,  0, 70, 0x00DC0046u, 0, "kino00_70", "event token kino0000: kino00_70 [00DC0046h]; alternates kino01/kino05 tracked" },
    { 395,  0, 70, 0x018B0046u, 0, "hiku15_70", "event token matu0000: hiku15_70 [018B0046h]" },
};

static const char* kArenaPlusComboNames[ARENA_PLUS_COMBO_COUNT] = {
    "Duo",
    "Trio",
    "Quartet",
    "Penta",
    "Specials (Final 3)",
    "Custom Mix x3",
    "Custom Mix x4",
    "Custom Mix x5"
};

/* Spira Reforge multi-boss combo rows on REAL map battles (HD btlmap scenes).
   Launch via Battle.7002 / 781D60 tokens â€” same RT2 path as solo Dark Aeons (zzzz00 debug field excluded). */
static const ArenaPlusBossRoute kArenaPlusComboRoutes[ARENA_PLUS_COMBO_COUNT] = {
    { 353,  0,  0, 0x01610000u, 2, "bika03_00", "Spira Reforge Duo @ bika03_00 Bikanel token=0x01610000" },
    { 340,  0, 21, 0x01540015u, 2, "mcyt00_21", "Spira Reforge Trio @ mcyt00_21 Macalania token=0x01540015" },
    { 430,  2, 24, 0x01AE0018u, 2, "nagi05_24", "Spira Reforge Quartet @ nagi05_24 Cavern token=0x01AE0018" },
    { 430,  2, 50, 0x01AE0032u, 2, "nagi05_50", "Spira Reforge Penta @ nagi05_50 Cavern token=0x01AE0032" },
    { 430,  2, 25, 0x01AE0019u, 2, "nagi05_25", "Spira Reforge Specials @ nagi05_25 Cavern token=0x01AE0019 (Final 3)" },
    { 340,  0, 22, 0x01540016u, 2, "mcyt00_22", "Compose custom x3 @ mcyt00_22 Macalania token=0x01540016 (preset-safe)" },
    { 430,  2, 23, 0x01AE0017u, 2, "nagi05_23", "Compose custom x4 @ nagi05_23 Cavern token=0x01AE0017 (preset-safe)" },
    { 430,  2, 22, 0x01AE0016u, 2, "nagi05_22", "Compose custom x5 @ nagi05_22 Cavern token=0x01AE0016 (preset-safe)" },
};

enum class ArenaPlusDirectRequestAuthority : uint8_t {
    LegacyExperimental = 0,
    CustomMixUltraExactCarrier,
};

// WHY: Ultra borrows exactly one already loaded vanilla encounter. This closed
// route cannot be replaced by the catalog, environment, scenario, or disk picker.
static const ArenaPlusBossRoute kArenaPlusUltraCarrierRoute = {
    517, 0, 0,
    FfxHooks::CustomMixUltra::kCarrierEncounterToken,
    2,
    "dome02_00",
    "CustomMix Ultra exact RAM carrier dome02_00 token=0x02050000",
};

static const int kArenaPlusComboGilCosts[ARENA_PLUS_COMBO_COUNT] = {
    /* Preset sums = component Dark Aeon gil (dossier Â§13.1). Custom Mix uses pick sum. */
    200000,  /* Duo: Valefor+Ifrit */
    325000,  /* Trio: +Ixion */
    450000,  /* Quartet: +Shiva */
    625000,  /* Penta: +Bahamut */
    750000,  /* Specials: Yojimbo+Anima+Magus */
    0, 0, 0  /* Custom Mix x3/x4/x5 â€” ArenaPlusComposePick pending pick sum */
};

/* Per-Dark-Aeon entry fee (dossier Â§13.1). Env override: FFXHOOKS_ARENAPLUS_GIL_COST_<N>. */
static const int kArenaPlusDarkGilCosts[ARENA_DARK_FLAG_LEN] = {
    100000,  /* Dark Valefor */
    100000,  /* Dark Ifrit */
    125000,  /* Dark Ixion */
    125000,  /* Dark Shiva */
    175000,  /* Dark Bahamut */
    200000,  /* Dark Yojimbo */
    250000,  /* Dark Anima */
    300000,  /* Dark Magus Sisters (+3 actors, one fee) */
    500000,  /* Penance */
};

/* Spira Reforge Arena+ catalog v2 overlay (Phase 5).
   Gated by arena_plus_catalog.flag. When the flag is present AND the JSON loads cleanly,
   per-slot battleToken/battleId are replaced by values from the catalog file
   (mods/Spira Reforge/arena/spira-arena-catalog.json). Anything that fails (file missing,
   parse error, missing row) leaves the hardcoded kArenaPlusBossRoutes[] slot intact.
   Memory model: g_arenaPlusCatalogStrings owns the C-string storage; route pointers
   reference into it; overlay is built once at boot, never freed. */
struct ArenaPlusRouteOverlay {
    bool          active;
    ArenaPlusBossRoute route;
};
static ArenaPlusRouteOverlay g_arenaPlusOverlay[ARENA_DARK_FLAG_LEN] = {};
static char                  g_arenaPlusCatalogStrings[8192] = {};
static size_t                g_arenaPlusCatalogStringsUsed = 0;
static bool                  g_arenaPlusCatalogLoaded = false;

/* Per-slot progress_flag pulled from the catalog (Phase 6).
   Falls back to a synthesized "arena.dark.<battleId>" key when the catalog
   does not carry a progress_flag for the slot. Used by ArenaPlus_GetTierLockState. */
static const char* g_arenaPlusProgressFlags[ARENA_DARK_FLAG_LEN] = {};

static char g_arenaPlusLabels[ARENA_PLUS_MAX_MENU_ROWS][ARENA_PLUS_LABEL_CAP] = {};
static unsigned char g_arenaPlusLabelBytes[ARENA_PLUS_MAX_MENU_ROWS][ARENA_PLUS_LABEL_CAP] = {};
static unsigned char g_arenaPlusHubDescBytes[ARENA_PLUS_MAX_MENU_ROWS][ARENA_PLUS_LABEL_CAP] = {};
static uint8_t g_arenaPlusDarkValues[ARENA_DARK_FLAG_LEN] = {};
static bool g_arenaPlusDarkReadOk[ARENA_DARK_FLAG_LEN] = {};
static int g_arenaPlusDarkMask = 0;
static volatile int g_arenaPlusDrawCalls = 0;
static volatile int g_arenaPlusResult = 0;
static volatile int g_arenaPlusClosed = 0;
static volatile int g_arenaPlusInputCooldown = 0;
static int g_arenaPlusLastConfirmEdge = 0;
static volatile LONG g_arenaPlusPendingBattle7002 = 0;
static volatile LONG g_arenaPlusPendingBattleToken = 0;
static volatile LONG g_arenaPlusPendingTransition = 0;
static volatile LONG g_arenaPlusPendingDark = -1;
static volatile LONG g_arenaPlusPendingGilCost = 0;
static volatile LONG g_arenaPlusPendingExpireTick = 0;
static volatile LONG g_arenaPlusPendingDifficultyField = -1;
static volatile LONG g_arenaPlusPendingDifficultySource =
    static_cast<LONG>(FfxHooks::F7Difficulty::BattleFieldSource::Missing);
static volatile LONG g_arenaPlusBattle7002TemplateReady = 0;
static volatile LONG g_arenaPlusBattle7002TemplateCtx = 0;
static volatile LONG g_arenaPlusBattle7002TemplateA2 = 0;
static volatile LONG g_arenaPlusBattle7002TemplateTick = 0;
static volatile LONG g_arenaPlusBattle7002TemplateCount = 0;
static uint32_t g_arenaPlusBattle7002TemplateStack[8] = {};

// g_FFX_MenuSubsystemActive (VA 0x13407E4): allocate only while this value proves the subsystem is live.
static bool NativeMenu_SubsystemLive() {
    if (!g_base) return false;
    return *reinterpret_cast<volatile int*>(g_base + (0x13407E4u - 0x400000u)) != 0;
}

static bool ArenaPlus_IsEnabled() {
    return F8CatalogGateEnabled("arena_plus.master");
}

static bool ArenaPlus_NpcHookEnabled() {
    if (!ArenaPlus_IsEnabled()) return false;
    if (EnvFlagEnabled("FFXHOOKS_DISABLE_ARENA_PLUS_NPC")) return false;
    return EnvFlagEnabled("FFXHOOKS_ENABLE_ARENA_PLUS_NPC") ||
           ModuleFlagEnabled("arena_plus_npc.flag") ||
           ModuleFlagEnabled("config\\arena_plus_npc.flag");
}

static bool ArenaPlus_LabRoutesEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ARENAPLUS_LAB_ROUTES") ||
           ModuleFlagEnabled("arena_plus_lab_routes.flag") ||
           ModuleFlagEnabled("config\\arena_plus_lab_routes.flag");
}

static bool ArenaPlus_UnlockAllEnabled() {
    return ResolveF8CatalogGate("arena_plus.unlock_all").value;
}

static bool ArenaPlus_MixAvailable() {
    return F8CatalogGateEnabled("arena_plus.master") && !g_runtimeValidateOnly &&
           FfxHooks::CustomMixUltra::Runtime::ProductionOperational();
}

static bool ArenaPlus_MixEnabled() {
    return ArenaPlus_MixAvailable() && ResolveF8CatalogGate("arena_plus.compose_f7").value;
}

static void ArenaPlus_PublishMixAvailability() {
    const bool ready = ArenaPlus_MixAvailable() &&
        InterlockedCompareExchange(&g_nativeMenuProducerReady, 0, 0) != 0;
    static volatile LONG lastReady = -1;
    if (InterlockedExchange(&lastReady, ready ? 1 : 0) != (ready ? 1 : 0))
        PublishResolvedF8Status("arena_plus.compose_f7",
            ready ? FfxHooks::F8RuntimeAvailability::Available
                  : FfxHooks::F8RuntimeAvailability::ProducerUnavailable, ready);
}

static bool ArenaPlus_ChargeGilEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ARENAPLUS_CHARGE_GIL") ||
           ModuleFlagEnabled("arena_plus_charge_gil.flag") ||
           ModuleFlagEnabled("config\\arena_plus_charge_gil.flag");
}

bool ArenaPlus_IsChargeGilEnabled() {
    return ArenaPlus_ChargeGilEnabled();
}

int ArenaPlus_GilCostForDarkIndex(int dark) {
    if (!ArenaPlus_ChargeGilEnabled()) return 0;
    if (dark < 0 || dark >= ARENA_DARK_FLAG_LEN) return 0;
    int cost = kArenaPlusDarkGilCosts[dark];
    char envName[64] = {};
    _snprintf_s(envName, sizeof(envName), _TRUNCATE, "FFXHOOKS_ARENAPLUS_GIL_COST_%d", dark);
    cost = EnvInt(envName, cost);
    if (cost < 0) cost = 0;
    if (cost > 999999999) cost = 999999999;
    return cost;
}

int ArenaPlus_GilCostForPickKey(const char* key) {
    if (!key || !key[0]) return 0;
    static const char* const kKeys[] = {
        "valefor", "ifrit", "ixion", "shiva", "bahamut", "yojimbo", "anima", "magus"
    };
    for (int i = 0; i < 8; ++i) {
        if (_stricmp(key, kKeys[i]) == 0)
            return ArenaPlus_GilCostForDarkIndex(i);
    }
    return 0;
}

int ArenaPlus_GilCostSumPickKeys(const char* const* keys, int count) {
    if (!keys || count <= 0) return 0;
    long long sum = 0;
    for (int i = 0; i < count; ++i) {
        sum += ArenaPlus_GilCostForPickKey(keys[i]);
        if (sum > 999999999) return 999999999;
    }
    return static_cast<int>(sum);
}

/* Arena+ Multi Dark Aeon spike (Phase 4 - RE document:
   docs/reverse/FFX_ARENA_PLUS_CUSTOM_TOKEN_RESOLVER_HOOK_SPIKE.md).
   Default-OFF read-only detour on FFX_Field_ResolveEncounterToken@0x7828B0 that logs
   every (token -> result) call so we can measure real-world resolver traffic before
   spec'ing the custom-token redirect path. Safe to keep around: the hook never mutates
   the token nor the return value. */
static bool ArenaPlus_ResolverLogEnabled() {
    return F8CatalogGateEnabled("arena_plus.resolver_log");
}

struct ArenaResolverLogInstallContext {
    uintptr_t base = 0;
    FfxHooks::ResolverLogFn log = nullptr;
    FfxHooks::ResolverLogInstallResult result = {false, 0};
};

static bool AttemptResolverLogInstall(void* context) noexcept {
    if (!context) return false;
    ArenaResolverLogInstallContext& install =
        *static_cast<ArenaResolverLogInstallContext*>(context);
    install.result = FfxHooks::InstallResolverLogHook(install.base, install.log);
    return install.result.ok;
}

static void InstallArenaResolverLogFromStartupPlan(
    const FfxHooks::ResolverOwner::SharedResolverStartupPlan& plan,
    bool validateOnly) {
    using FfxHooks::ResolverOwner::ExecuteSharedResolverStartup;
    using FfxHooks::ResolverOwner::SharedResolverStartupReason;
    using FfxHooks::ResolverOwner::SharedResolverStartupReasonName;

    if (plan.reason == SharedResolverStartupReason::NotRequested ||
        plan.reason == SharedResolverStartupReason::F7Reserved) {
        Log("[ffx-hooks] ResolverLog not armed reason=%s\n",
            SharedResolverStartupReasonName(plan.reason));
        return;
    }
    if (plan.reason == SharedResolverStartupReason::ConflictF7ResolverOwner) {
        // WHY: F7's exact three-target batch requires the original ResolveEncounter prologue.
        // Chaining a PolyHook detour here would invalidate its loaded-signature gate and create
        // two unrelated teardown owners for the same machine entry.
        Log("[ffx-hooks] ResolverLog startup skipped reason=%s owner=F7Difficulty "
            "target_rva=0x%08X\n",
            SharedResolverStartupReasonName(plan.reason),
            static_cast<unsigned>(FfxHooks::F7Difficulty::kResolveEncounterRva));
        PublishResolvedF8Status(
            "arena_plus.resolver_log", FfxHooks::F8RuntimeAvailability::Conflict, false);
        return;
    }
    if (validateOnly) {
        Log("[ffx-hooks] ResolverLog install blocked reason=VALIDATE_ONLY\n");
        PublishResolvedF8Status(
            "arena_plus.resolver_log",
            FfxHooks::F8RuntimeAvailability::ProducerUnavailable,
            false);
        return;
    }

    ArenaResolverLogInstallContext context{g_base, LogLine, {false, 0}};
    const FfxHooks::ResolverOwner::SharedResolverStartupExecution execution =
        ExecuteSharedResolverStartup(plan, &context, &AttemptResolverLogInstall);
    Log("[ffx-hooks] ResolverLog install attempted=%d ok=%d reason_code=%u owner_reason=%s\n",
        execution.installAttempted ? 1 : 0,
        execution.installed ? 1 : 0,
        static_cast<unsigned>(context.result.reasonCode),
        SharedResolverStartupReasonName(plan.reason));
    PublishResolvedF8Status(
        "arena_plus.resolver_log",
        execution.installed
            ? FfxHooks::F8RuntimeAvailability::Available
            : FfxHooks::F8RuntimeAvailability::ProducerUnavailable,
        execution.installed);
}

// Opt-in for the redirect side of the resolver hook (Opcao A in the RE doc).
// Without this flag the hook stays read-only no matter what tables we publish.
static bool ArenaPlus_CustomTokenResolverEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ENABLE_ARENA_PLUS_CUSTOM_TOKEN_RESOLVER") ||
           ModuleFlagEnabled("arena_plus_custom_token_resolver.flag") ||
           ModuleFlagEnabled("config\\arena_plus_custom_token_resolver.flag");
}

// Opt-in for the BattleEndHook scaffold (Lane 3 of post-plan work). Default OFF
// because (a) the hook is brand new and not yet RT2-proven, (b) victory/defeat
// discrimination is not implemented yet â€” the old sub_888CE0/input-pad
// confusion was resolved, so the hook only logs and never touches the sidecar
// until the real outcome word is mapped.
// Opt-in for the PhaseTurnEdgeHook probe (Phase Rotation CTB edge).
// Default OFF because this is a brand new hook not yet RT2-proven.
static bool PhaseTurnEdgeHookEnabled() {
    return false;
}

static FfxHooks::PhaseTurnEdgeSidecar g_phaseTurnEdgeSidecar = {};
static const uint32_t PHASE_TURN_EDGE_MAX_ENTRIES = 64u;
static const uint32_t PHASE_TURN_EDGE_MAX_ACTORS = 16u;

struct PhaseTurnEdgeRuntimeState {
    uint64_t battleSignature;
    bool     battleSignatureValid;
    bool     fired[PHASE_TURN_EDGE_MAX_ENTRIES][PHASE_TURN_EDGE_MAX_ACTORS];
};

static PhaseTurnEdgeRuntimeState g_phaseTurnEdgeRuntimeState = {};

static uint64_t PhaseTurnEdgeFnv1a64(uint64_t hash, uint32_t value) {
    hash ^= static_cast<uint64_t>(value);
    hash *= 1099511628211ull;
    return hash;
}

static void PhaseTurnEdgeResetRuntimeState(const char* reason, uint64_t newSignature) {
    memset(&g_phaseTurnEdgeRuntimeState, 0, sizeof(g_phaseTurnEdgeRuntimeState));
    g_phaseTurnEdgeRuntimeState.battleSignature = newSignature;
    g_phaseTurnEdgeRuntimeState.battleSignatureValid = newSignature != 0;
    Log("[ffx-hooks] PhaseTurnEdge runtime state reset reason=%s sig=0x%llX\n",
        reason ? reason : "unknown",
        static_cast<unsigned long long>(newSignature));
}

static bool PhaseTurnEdgeBuildBattleSignature(uint64_t* outSignature) {
    if (!outSignature || !g_base) return false;

    uint32_t enemyBase = 0;
    if (!AuroraReadU32(rva(RVA_BATTLE_ENEMY_LIST), &enemyBase) || !AuroraPtrOk(enemyBase))
        return false;

    uint64_t sig = 1469598103934665603ull;
    sig = PhaseTurnEdgeFnv1a64(sig, enemyBase);

    for (uint32_t i = 0; i < PHASE_TURN_EDGE_MAX_ACTORS; ++i) {
        const uintptr_t chr = static_cast<uintptr_t>(enemyBase) + i * FFX_BATTLE_CHR_STRIDE;
        uint16_t battleId = 0;
        uint8_t inBattle = 0;
        uint32_t scriptChunks = 0;
        uint32_t scriptData = 0;

        if (!AuroraReadU8(chr + 0xDC8, &inBattle)) {
            sig = PhaseTurnEdgeFnv1a64(sig, i);
            sig = PhaseTurnEdgeFnv1a64(sig, 0xDEAD0000u | i);
            continue;
        }

        sig = PhaseTurnEdgeFnv1a64(sig, i);
        sig = PhaseTurnEdgeFnv1a64(sig, inBattle);

        if (inBattle == 0) {
            sig = PhaseTurnEdgeFnv1a64(sig, 0);
            continue;
        }

        if (!AuroraReadU16(chr + 0x0E, &battleId))
            battleId = 0;
        if (!AuroraReadU32(chr + 0xF78, &scriptChunks))
            scriptChunks = 0;
        if (!AuroraReadU32(chr + 0xF7C, &scriptData))
            scriptData = 0;

        sig = PhaseTurnEdgeFnv1a64(sig, battleId);
        sig = PhaseTurnEdgeFnv1a64(sig, scriptChunks);
        sig = PhaseTurnEdgeFnv1a64(sig, scriptData);
    }

    *outSignature = sig;
    return true;
}

static void PhaseTurnEdgeRefreshBattleState() {
    uint64_t signature = 0;
    if (!PhaseTurnEdgeBuildBattleSignature(&signature))
        return;

    if (!g_phaseTurnEdgeRuntimeState.battleSignatureValid ||
        g_phaseTurnEdgeRuntimeState.battleSignature != signature) {
        PhaseTurnEdgeResetRuntimeState("battle-signature-change", signature);
    }
}

static uintptr_t PhaseTurnEdgeResolveActorPtr(uint32_t actorSlot) {
    if (!g_base || actorSlot > 0xFFu)
        return 0;

    using GetActorByIndexFn = void*(__cdecl*)(uint8_t actorIndex);
    auto getActor = reinterpret_cast<GetActorByIndexFn>(g_base + RVA_FFX_BATTLE_GET_ACTOR_BY_INDEX);
    if (!getActor)
        return 0;

    __try {
        return reinterpret_cast<uintptr_t>(getActor(static_cast<uint8_t>(actorSlot)));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static bool PhaseTurnEdgeTryFormatMonsterId(uintptr_t actorPtr, char* outMonsterId, size_t outMonsterIdSize, uint16_t* outBattleId) {
    if (!actorPtr || !outMonsterId || outMonsterIdSize == 0)
        return false;

    uint16_t battleId = 0;
    uint8_t inBattle = 0;
    __try {
        battleId = *reinterpret_cast<volatile uint16_t*>(actorPtr + 0x0E);
        inBattle = *reinterpret_cast<volatile uint8_t*>(actorPtr + 0xDC8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    if (inBattle == 0 || battleId < 0x1000 || battleId > 0x1FFF)
        return false;

    const uint32_t monsterNo = static_cast<uint32_t>(battleId - 0x1000u);
    _snprintf_s(outMonsterId, outMonsterIdSize, _TRUNCATE, "m%03u", monsterNo);
    if (outBattleId)
        *outBattleId = battleId;
    return true;
}

static bool PhaseTurnEdgeTryDispatchCommand(uint32_t actorSlot, uint16_t skillId, uint16_t targetMaskLiteral, int* outResolvedMask, int* outQueueRv) {
    if (!g_base)
        return false;

    using ResolveTargetMaskFn = int(__cdecl*)(int actorSlot, int targetSentinel, int a3, int a4);
    using QueueScriptCommandFn = int(__cdecl*)(int actorSlot, int16_t commandId, int targetMask, int forceFlag, int n64);

    auto resolveTarget = reinterpret_cast<ResolveTargetMaskFn>(g_base + RVA_FFX_BATTLE_RESOLVE_TARGET_MASK);
    auto queueCommand = reinterpret_cast<QueueScriptCommandFn>(g_base + RVA_FFX_BATTLE_QUEUE_SCRIPT_COMMAND);
    if (!resolveTarget || !queueCommand)
        return false;

    int resolvedMask = 0;
    int queueRv = -1;
    __try {
        resolvedMask = resolveTarget(static_cast<int>(actorSlot), static_cast<int>(targetMaskLiteral), 0, 1);
        if (resolvedMask != 0)
            queueRv = queueCommand(static_cast<int>(actorSlot), static_cast<int16_t>(skillId), resolvedMask, 1, -1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        resolvedMask = 0;
        queueRv = -1;
    }

    if (outResolvedMask) *outResolvedMask = resolvedMask;
    if (outQueueRv) *outQueueRv = queueRv;
    return resolvedMask != 0 && queueRv == 0;
}

static void __cdecl PhaseTurnEdge_OnEvent(const FfxHooks::PhaseTurnEdgeEvent& ev) {
    if (ev.battleActiveFlag == 0 || g_phaseTurnEdgeSidecar.count <= 0)
        return;

    PhaseTurnEdgeRefreshBattleState();

    // Runtime v1 reacts to the CTB edge globally: when any valid turn passes, scan the
    // active monster roster and dispatch sidecar actions for matching monsters, instead of
    // tying the feature to the actor that owned the CTB edge.
    for (uint32_t actorSlot = 0; actorSlot < PHASE_TURN_EDGE_MAX_ACTORS; ++actorSlot) {
        uintptr_t actorPtr = 0;
        if (actorSlot == ev.actorSlot)
            actorPtr = ev.actorPtr;
        if (!actorPtr)
            actorPtr = PhaseTurnEdgeResolveActorPtr(actorSlot);
        if (!actorPtr)
            continue;

        char monsterId[8] = {};
        uint16_t battleId = 0;
        if (!PhaseTurnEdgeTryFormatMonsterId(actorPtr, monsterId, sizeof(monsterId), &battleId))
            continue;

        for (int i = 0; i < g_phaseTurnEdgeSidecar.count && i < static_cast<int>(PHASE_TURN_EDGE_MAX_ENTRIES); ++i) {
            const FfxHooks::PhaseTurnEdgeEntry& entry = g_phaseTurnEdgeSidecar.entries[i];
            if (_stricmp(entry.monsterId, monsterId) != 0)
                continue;

            if (entry.onlyOnce && g_phaseTurnEdgeRuntimeState.fired[i][actorSlot])
                continue;

            if (entry.guardVar >= 0 && !entry.onlyOnce) {
                Log("[ffx-hooks] PhaseTurnEdge monster=%s slot=%u edgeSlot=%u entry=%d guardVar=%d onlyOnce=0 unsupported in runtime v1; skipped to avoid infinite repeat\n",
                    monsterId,
                    static_cast<unsigned>(actorSlot),
                    static_cast<unsigned>(ev.actorSlot),
                    i,
                    entry.guardVar);
                continue;
            }

            int resolvedMask = 0;
            int queueRv = -1;
            const bool ok = PhaseTurnEdgeTryDispatchCommand(actorSlot, entry.skillId, entry.targetMask, &resolvedMask, &queueRv);
            if (ok) {
                if (entry.onlyOnce)
                    g_phaseTurnEdgeRuntimeState.fired[i][actorSlot] = true;

                Log("[ffx-hooks] PhaseTurnEdge dispatch OK monster=%s battleId=0x%04X slot=%u edgeSlot=%u entry=%d skill=0x%04X target=0x%04X resolved=0x%08X onlyOnce=%d guardVar=%d\n",
                    monsterId,
                    battleId,
                    static_cast<unsigned>(actorSlot),
                    static_cast<unsigned>(ev.actorSlot),
                    i,
                    entry.skillId,
                    entry.targetMask,
                    static_cast<unsigned>(resolvedMask),
                    entry.onlyOnce ? 1 : 0,
                    entry.guardVar);
            } else {
                Log("[ffx-hooks] PhaseTurnEdge dispatch FAILED monster=%s battleId=0x%04X slot=%u edgeSlot=%u entry=%d skill=0x%04X target=0x%04X resolved=0x%08X rv=%d onlyOnce=%d guardVar=%d\n",
                    monsterId,
                    battleId,
                    static_cast<unsigned>(actorSlot),
                    static_cast<unsigned>(ev.actorSlot),
                    i,
                    entry.skillId,
                    entry.targetMask,
                    static_cast<unsigned>(resolvedMask),
                    queueRv,
                    entry.onlyOnce ? 1 : 0,
                    entry.guardVar);
            }
        }
    }
}

static bool ArenaPlus_VictoryHookEnabled() {
    return F8CatalogGateEnabled("arena_plus.victory_hook");
}

// BattleEnd callback (scaffold). Today it only logs the event with a clear
// "scaffold-only" suffix, because we do not yet:
//   (a) distinguish victory vs defeat vs escape (TODO RT2 spike on the real
//       battle-end outcome word; the old sub_888CE0 input/pad hypothesis is stale);
//   (b) map effectHandle/nextEncounterTok back to a stable Arena+ row id.
//
// When both TODOs are filled in, this is the single point that should call
// FfxHooks::ArenaProgress_RecordCleared(progressFlag, ev). The progressFlag
// will come from the Arena+ catalog (already loaded via ArenaPlus_LoadCatalogOverlay).
static void __cdecl ArenaPlus_OnBattleEnd(const FfxHooks::BattleEndEvent& ev) {
    Log("[ffx-hooks] ArenaPlus_OnBattleEnd #%ld handle=0x%08X nextTok=0x%08X result=%u scaffold-only\n",
        ev.sequenceNo,
        static_cast<unsigned>(ev.effectHandle),
        static_cast<unsigned>(ev.nextEncounterTok),
        static_cast<unsigned>(ev.result));

    // Intentionally NOT calling ArenaProgress_RecordCleared here. Doing so before
    // we can prove (a) and (b) above would risk marking a row CLEARED on defeat
    // or on a random non-Arena+ battle. The plug-in line, when ready, is:
    //
    //     if (ev.result == FfxHooks::BattleEndResult::kVictory) {
    //         const char* flag = ArenaPlus_MapEffectHandleToProgressFlag(ev.effectHandle);
    //         if (flag) FfxHooks::ArenaProgress_RecordCleared(flag);
    //     }
}

/* Arena+ catalog v2 reader (Phase 5). When this flag and JSON file are present, the catalog
   overrides battleToken/battleId per slot. Hardcoded fallback always wins on any failure. */
static bool ArenaPlus_CatalogEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ENABLE_ARENA_PLUS_CATALOG") ||
           ModuleFlagEnabled("arena_plus_catalog.flag") ||
           ModuleFlagEnabled("config\\arena_plus_catalog.flag");
}

static const char* ArenaPlus_CatalogIntern(const char* src) {
    if (!src) return nullptr;
    const size_t len = strlen(src);
    if (len + 1 > sizeof(g_arenaPlusCatalogStrings) - g_arenaPlusCatalogStringsUsed) return nullptr;
    char* dst = g_arenaPlusCatalogStrings + g_arenaPlusCatalogStringsUsed;
    memcpy(dst, src, len + 1);
    g_arenaPlusCatalogStringsUsed += len + 1;
    return dst;
}

static bool ArenaPlus_CatalogReadFile(const char* path, char* buffer, size_t bufferLen, size_t* bytesRead) {
    if (!path || !buffer || bufferLen < 16) return false;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > static_cast<LONGLONG>(bufferLen - 1)) {
        CloseHandle(h);
        return false;
    }
    DWORD read = 0;
    BOOL ok = ReadFile(h, buffer, static_cast<DWORD>(size.QuadPart), &read, nullptr);
    CloseHandle(h);
    if (!ok || read == 0) return false;
    buffer[read] = '\0';
    if (bytesRead) *bytesRead = read;
    return true;
}

// Minimal JSON-scan helpers (defensive, format-tolerant; we control the JSON layout).
static const char* ArenaPlus_CatalogFindString(const char* start, const char* end, const char* key, char* out, size_t outLen) {
    if (!start || !end || !key || !out || outLen < 2) return nullptr;
    char needle[64] = {};
    _snprintf_s(needle, sizeof(needle), _TRUNCATE, "\"%s\"", key);
    const char* hit = strstr(start, needle);
    if (!hit || hit >= end) return nullptr;
    const char* p = hit + strlen(needle);
    while (p < end && (*p == ' ' || *p == ':' || *p == '\t')) ++p;
    if (p >= end || *p != '"') return nullptr;
    ++p;
    size_t i = 0;
    while (p < end && *p != '"' && i + 1 < outLen) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return p < end ? p + 1 : end;
}

static int ArenaPlus_CatalogSlotForBaseTemplate(const char* baseTemplate) {
    if (!baseTemplate || !baseTemplate[0]) return -1;
    for (int i = 0; i < ARENA_DARK_FLAG_LEN; ++i) {
        if (_stricmp(baseTemplate, kArenaPlusBossRoutes[i].battleId) == 0) {
            return i;
        }
    }
    return -1;
}

static unsigned int ArenaPlus_CatalogParseToken(const char* hex) {
    if (!hex || !hex[0]) return 0;
    if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex += 2;
    unsigned int v = 0;
    while (*hex) {
        char c = *hex++;
        unsigned int d;
        if (c >= '0' && c <= '9') d = static_cast<unsigned int>(c - '0');
        else if (c >= 'a' && c <= 'f') d = static_cast<unsigned int>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = static_cast<unsigned int>(c - 'A' + 10);
        else break;
        v = (v << 4) | d;
    }
    return v;
}

// Best-effort overlay loader. Walks the JSON line by line looking for rows with the v2 shape:
//   "battle_token": "0xXXXXXXXX"
//   "base_template": "<battleId>"
//   (optional) "label": "<short>"
// If both token+base_template are found in the same row block and base_template matches a
// hardcoded slot, the overlay slot is populated. Best-effort: failures are logged + ignored.
static void ArenaPlus_LoadCatalogOverlay() {
    if (!ArenaPlus_CatalogEnabled()) {
        return;
    }
    if (g_arenaPlusCatalogLoaded) {
        return;
    }

    char path[MAX_PATH] = {};
    bool ok = false;
    char envPath[MAX_PATH] = {};
    DWORD envLen = GetEnvironmentVariableA("FFXHOOKS_ARENAPLUS_CATALOG_PATH", envPath, sizeof(envPath));
    if (envLen > 0 && envLen < sizeof(envPath)) {
        lstrcpynA(path, envPath, sizeof(path));
        ok = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    }
    if (!ok) ok = ModuleRelativePath("spira-arena-catalog.json", path, sizeof(path)) &&
                  GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    if (!ok) ok = ModuleRelativePath("config\\spira-arena-catalog.json", path, sizeof(path)) &&
                  GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    if (!ok) {
        Log("[ffx-hooks] ArenaPlus catalog flag set but spira-arena-catalog.json not found (looked in modules/, config/, $FFXHOOKS_ARENAPLUS_CATALOG_PATH); using hardcoded routes\n");
        g_arenaPlusCatalogLoaded = true;
        return;
    }

    static char buffer[65536];
    size_t bytes = 0;
    if (!ArenaPlus_CatalogReadFile(path, buffer, sizeof(buffer), &bytes)) {
        Log("[ffx-hooks] ArenaPlus catalog read failed (path=%s); using hardcoded routes\n", path);
        g_arenaPlusCatalogLoaded = true;
        return;
    }

    const char* end = buffer + bytes;
    const char* cursor = strstr(buffer, "\"rows\"");
    if (!cursor) {
        Log("[ffx-hooks] ArenaPlus catalog has no rows[]; using hardcoded routes\n");
        g_arenaPlusCatalogLoaded = true;
        return;
    }

    int overrides = 0;
    while (cursor && cursor < end) {
        const char* rowStart = strchr(cursor, '{');
        if (!rowStart) break;
        const char* rowEnd = strchr(rowStart, '}');
        if (!rowEnd) break;

        char baseTemplate[64] = {};
        char tokenStr[32] = {};
        char label[96] = {};
        char progressFlag[96] = {};
        ArenaPlus_CatalogFindString(rowStart, rowEnd, "base_template", baseTemplate, sizeof(baseTemplate));
        ArenaPlus_CatalogFindString(rowStart, rowEnd, "battle_token", tokenStr, sizeof(tokenStr));
        ArenaPlus_CatalogFindString(rowStart, rowEnd, "label", label, sizeof(label));
        ArenaPlus_CatalogFindString(rowStart, rowEnd, "progress_flag", progressFlag, sizeof(progressFlag));

        const int slot = ArenaPlus_CatalogSlotForBaseTemplate(baseTemplate);
        const unsigned int tok = ArenaPlus_CatalogParseToken(tokenStr);
        if (slot >= 0 && tok != 0 && !g_arenaPlusOverlay[slot].active) {
            g_arenaPlusOverlay[slot].active = true;
            g_arenaPlusOverlay[slot].route = kArenaPlusBossRoutes[slot];
            g_arenaPlusOverlay[slot].route.battleToken = tok;
            const char* internedBattleId = ArenaPlus_CatalogIntern(baseTemplate);
            if (internedBattleId) g_arenaPlusOverlay[slot].route.battleId = internedBattleId;
            const char* internedLabel = label[0] ? ArenaPlus_CatalogIntern(label) : nullptr;
            if (internedLabel) g_arenaPlusOverlay[slot].route.evidence = internedLabel;
            if (progressFlag[0] && !g_arenaPlusProgressFlags[slot]) {
                g_arenaPlusProgressFlags[slot] = ArenaPlus_CatalogIntern(progressFlag);
            }
            ++overrides;
            Log("[ffx-hooks] ArenaPlus catalog overlay slot=%d battleId=%s token=0x%08X label=%s progress=%s\n",
                slot, baseTemplate, tok, label[0] ? label : "(none)", progressFlag[0] ? progressFlag : "(none)");
        }

        cursor = rowEnd + 1;
    }

    Log("[ffx-hooks] ArenaPlus catalog loaded (path=%s) overlays=%d/%d\n", path, overrides, ARENA_DARK_FLAG_LEN);
    g_arenaPlusCatalogLoaded = true;
}

static const ArenaPlusBossRoute& ArenaPlus_GetRoute(int dark) {
    if (dark < 0 || dark >= ARENA_DARK_FLAG_LEN) return kArenaPlusBossRoutes[0];
    if (g_arenaPlusOverlay[dark].active) return g_arenaPlusOverlay[dark].route;
    return kArenaPlusBossRoutes[dark];
}

// Loads the custom-token redirect sidecar (Lane 2 of the post-plan work).
// Looks for mods/Spira Reforge/arena/spira-arena-custom-tokens.json next to the DLL,
// parses minimal {"redirects": [{"custom_token":"0xA0010046","alias_token":"0x00DC0046"}]},
// and registers the table with the ResolverLogHook. The redirect path stays disarmed unless
// arena_plus_custom_token_resolver.flag is on at boot.
static void ArenaPlus_LoadCustomTokenRedirects() {
    if (!ArenaPlus_CustomTokenResolverEnabled()) {
        Log("[ffx-hooks] ArenaPlus custom-token resolver not armed (arena_plus_custom_token_resolver.flag)\n");
        return;
    }
    if (!FfxHooks::IsResolverLogHookInstalled()) {
        Log("[ffx-hooks] ArenaPlus custom-token redirect requested but ResolverLogHook is not installed; ignoring\n");
        return;
    }

    char path[MAX_PATH] = {};
    bool found = false;
    char envPath[MAX_PATH] = {};
    DWORD envLen = GetEnvironmentVariableA("FFXHOOKS_ARENAPLUS_CUSTOM_TOKENS_PATH", envPath, sizeof(envPath));
    if (envLen > 0 && envLen < sizeof(envPath)) {
        lstrcpynA(path, envPath, sizeof(path));
        found = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    }
    if (!found) found = ModuleRelativePath("mods\\Spira Reforge\\arena\\spira-arena-custom-tokens.json", path, sizeof(path)) &&
                       GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    if (!found) found = ModuleRelativePath("spira-arena-custom-tokens.json", path, sizeof(path)) &&
                       GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    if (!found) {
        Log("[ffx-hooks] ArenaPlus custom-token redirect armed but no sidecar found; hook stays observe-only\n");
        FfxHooks::SetCustomTokenRedirectEnabled(true);
        return;
    }

    static char buffer[16384];
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        Log("[ffx-hooks] ArenaPlus custom-token sidecar open failed (path=%s err=%lu)\n", path, GetLastError());
        FfxHooks::SetCustomTokenRedirectEnabled(true);
        return;
    }
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 ||
        size.QuadPart >= static_cast<LONGLONG>(sizeof(buffer))) {
        Log("[ffx-hooks] ArenaPlus custom-token sidecar size invalid (path=%s)\n", path);
        CloseHandle(h);
        FfxHooks::SetCustomTokenRedirectEnabled(true);
        return;
    }
    DWORD bytes = 0;
    if (!ReadFile(h, buffer, static_cast<DWORD>(size.QuadPart), &bytes, nullptr) || bytes == 0) {
        Log("[ffx-hooks] ArenaPlus custom-token sidecar read failed (path=%s)\n", path);
        CloseHandle(h);
        FfxHooks::SetCustomTokenRedirectEnabled(true);
        return;
    }
    buffer[bytes] = '\0';
    CloseHandle(h);

    const char* end = buffer + bytes;
    const char* redirects = strstr(buffer, "\"redirects\"");
    if (!redirects) {
        Log("[ffx-hooks] ArenaPlus custom-token sidecar has no redirects[]; redirect armed but empty\n");
        FfxHooks::SetCustomTokenRedirectEnabled(true);
        return;
    }

    FfxHooks::CustomTokenRedirect table[32] = {};
    size_t tableUsed = 0;

    const char* cursor = redirects;
    while (cursor && cursor < end && tableUsed < 32) {
        const char* rowStart = strchr(cursor, '{');
        if (!rowStart) break;
        const char* rowEnd = strchr(rowStart, '}');
        if (!rowEnd) break;

        char customStr[16] = {};
        char aliasStr[16] = {};
        ArenaPlus_CatalogFindString(rowStart, rowEnd, "custom_token", customStr, sizeof(customStr));
        ArenaPlus_CatalogFindString(rowStart, rowEnd, "alias_token", aliasStr, sizeof(aliasStr));

        const unsigned int customTok = ArenaPlus_CatalogParseToken(customStr);
        const unsigned int aliasTok = ArenaPlus_CatalogParseToken(aliasStr);
        if (customTok != 0 && aliasTok != 0) {
            const unsigned int hi = (customTok >> 16) & 0xFFFFu;
            if (hi >= 0xA001u && hi <= 0xAFFFu) {
                table[tableUsed].customToken = customTok;
                table[tableUsed].aliasToken = aliasTok;
                ++tableUsed;
                Log("[ffx-hooks] ArenaPlus redirect entry: 0x%08X -> 0x%08X\n", customTok, aliasTok);
            } else {
                Log("[ffx-hooks] ArenaPlus redirect entry skipped (custom 0x%08X outside 0xA001..0xAFFF)\n", customTok);
            }
        }

        cursor = rowEnd + 1;
    }

    const size_t accepted = FfxHooks::SetCustomTokenRedirects(table, tableUsed);
    FfxHooks::SetCustomTokenRedirectEnabled(true);
    Log("[ffx-hooks] ArenaPlus custom-token resolver ARMED path=%s entries=%zu/%zu\n",
        path, accepted, tableUsed);
}

// Phase 6: tier-lock state per Arena+ slot.
//   CLEARED -> progress sidecar shows cleared=true for the slot's progress_flag.
//   READY   -> progress sidecar is enabled and the flag is not cleared (or sidecar disabled).
//   LOCKED  -> reserved for unlock_requires gating; consumer must opt-in by passing
//              `requireUnlockChain=true` (the chain itself is consulted from the catalog
//              row by the future UI layer; today we never return LOCKED automatically to
//              avoid surprising the existing F7 menu).
// Caller hint: this is read-only, safe to call from menu render paths, and never throws.
enum class ArenaPlusTierLockState : int { READY = 0, CLEARED = 1, LOCKED = 2 };

static const char* ArenaPlus_ProgressFlagForSlot(int dark) {
    if (dark < 0 || dark >= ARENA_DARK_FLAG_LEN) return nullptr;
    if (g_arenaPlusProgressFlags[dark]) return g_arenaPlusProgressFlags[dark];
    // Synthesized fallback when catalog row had no explicit progress_flag.
    static char buf[ARENA_DARK_FLAG_LEN][80];
    const char* battleId = ArenaPlus_GetRoute(dark).battleId;
    if (!battleId) battleId = "unknown";
    _snprintf_s(buf[dark], sizeof(buf[dark]), _TRUNCATE, "arena.dark.%s", battleId);
    return buf[dark];
}

static ArenaPlusTierLockState ArenaPlus_GetTierLockState(int dark) {
    if (!FfxHooks::ArenaProgress_Enabled()) return ArenaPlusTierLockState::READY;
    const char* flag = ArenaPlus_ProgressFlagForSlot(dark);
    if (flag && FfxHooks::ArenaProgress_IsRowCleared(flag)) {
        return ArenaPlusTierLockState::CLEARED;
    }
    return ArenaPlusTierLockState::READY;
}

static const char* ArenaPlus_TierLockStateLabel(ArenaPlusTierLockState s) {
    switch (s) {
        case ArenaPlusTierLockState::CLEARED: return "CLEARED";
        case ArenaPlusTierLockState::LOCKED:  return "LOCKED";
        default:                              return "READY";
    }
}

static bool ArenaPlus_MusicEnabled() {
    return ArenaPlusMusicFlagEnabledRaw();
}

static bool ArenaPlus_AutoCarrierEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ARENAPLUS_AUTO_CARRIER") ||
           ModuleFlagEnabled("arena_plus_auto_carrier.flag") ||
           ModuleFlagEnabled("config\\arena_plus_auto_carrier.flag");
}

static uint32_t ArenaPlus_AutoCarrierTtlMs() {
    int ttl = EnvInt("FFXHOOKS_ARENAPLUS_AUTO_CARRIER_TTL_MS", 15000);
    if (ttl < 1000) ttl = 1000;
    if (ttl > 120000) ttl = 120000;
    return static_cast<uint32_t>(ttl);
}

static bool ArenaPlus_TemplateReplayEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ARENAPLUS_TEMPLATE_REPLAY") ||
           ModuleFlagEnabled("arena_plus_template_replay.flag") ||
           ModuleFlagEnabled("config\\arena_plus_template_replay.flag");
}

static uint32_t ArenaPlus_TemplateReplayMaxAgeMs() {
    int age = EnvInt("FFXHOOKS_ARENAPLUS_TEMPLATE_REPLAY_MAX_AGE_MS", 1800000);
    int fileAge = 0;
    if (TryModuleTextInt("arena_plus_template_replay_max_age_ms.txt", &fileAge) ||
        TryModuleTextInt("config\\arena_plus_template_replay_max_age_ms.txt", &fileAge)) {
        age = fileAge;
    }
    if (age < 1000) age = 1000;
    if (age > 1800000) age = 1800000;
    return static_cast<uint32_t>(age);
}

static uint32_t ArenaPlus_UnprovenDirectFallbackTtlMs() {
    int ttl = EnvInt("FFXHOOKS_ARENAPLUS_UNPROVEN_DIRECT_TTL_MS", 15000);
    int fileTtl = 0;
    if (TryModuleTextInt("arena_plus_unproven_direct_ttl_ms.txt", &fileTtl) ||
        TryModuleTextInt("config\\arena_plus_unproven_direct_ttl_ms.txt", &fileTtl)) {
        ttl = fileTtl;
    }
    if (ttl < 1000) ttl = 1000;
    if (ttl > 120000) ttl = 120000;
    return static_cast<uint32_t>(ttl);
}

static bool ArenaPlus_TemplateReplayClaimSuccessEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ARENAPLUS_TEMPLATE_REPLAY_CLAIM_SUCCESS") ||
           ModuleFlagEnabled("arena_plus_template_replay_claim_success.flag") ||
           ModuleFlagEnabled("config\\arena_plus_template_replay_claim_success.flag");
}

static bool ArenaPlus_DirectRequest781D60Enabled() {
    return EnvFlagEnabled("FFXHOOKS_ARENAPLUS_DIRECT_REQUEST") ||
           ModuleFlagEnabled("arena_plus_direct_request.flag") ||
           ModuleFlagEnabled("config\\arena_plus_direct_request.flag");
}

static bool ArenaPlus_DirectRequestClaimSuccessEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ARENAPLUS_DIRECT_REQUEST_CLAIM_SUCCESS") ||
           ModuleFlagEnabled("arena_plus_direct_request_claim_success.flag") ||
           ModuleFlagEnabled("config\\arena_plus_direct_request_claim_success.flag");
}

static bool ArenaPlus_PrepareBattleFlagsEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ARENAPLUS_PREPARE_BATTLE_FLAGS") ||
           ModuleFlagEnabled("arena_plus_prepare_battle_flags.flag") ||
           ModuleFlagEnabled("config\\arena_plus_prepare_battle_flags.flag");
}

static bool ArenaPlus_BossRouteMapped(int dark) {
    return dark >= 0 &&
           dark < ARENA_DARK_FLAG_LEN &&
           ArenaPlus_GetRoute(dark).battleToken != 0;
}

static const ArenaPlusBossRoute& ArenaPlus_GetComboRoute(int combo) {
    if (combo < 0 || combo >= ARENA_PLUS_COMBO_COUNT) return kArenaPlusComboRoutes[0];
    return kArenaPlusComboRoutes[combo];
}

static bool ArenaPlus_ComboRouteMapped(int combo) {
    if (combo < 0 || combo >= ARENA_PLUS_COMBO_COUNT) return false;
    const ArenaPlusBossRoute& route = ArenaPlus_GetComboRoute(combo);
    return route.battleId != nullptr && route.battleId[0] != '\0' &&
           route.battleToken != 0 && route.field >= 0 && route.formation >= 0;
}

static bool ArenaPlus_ComboBattlesEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ENABLE_ARENA_PLUS_COMBO_BATTLES") ||
           ModuleFlagEnabled("arena_plus_combo_battles.flag") ||
           ModuleFlagEnabled("config\\arena_plus_combo_battles.flag") ||
           ArenaPlus_LabRoutesEnabled();
}

static bool ArenaPlus_ComboRouteAllowed(int combo) {
    if (combo >= ARENA_PLUS_PRESET_COMBO_COUNT && combo < ARENA_PLUS_COMBO_COUNT)
        return ArenaPlus_MixEnabled();
    if (!ArenaPlus_ComboRouteMapped(combo) || !ArenaPlus_ComboBattlesEnabled()) return false;
    const auto rules = ArenaPlus_MixRules();
    static const uint16_t required[] = {0x03u, 0x07u, 0x0Fu, 0x1Fu, 0xE0u};
    return combo >= 0 && combo < ARENA_PLUS_PRESET_COMBO_COUNT &&
           (rules.bypass || (rules.defeatedMask & required[combo]) == required[combo]);
}

static int ArenaPlus_ComboGilCost(int combo) {
    if (!ArenaPlus_ChargeGilEnabled()) return 0;
    if (combo < 0 || combo >= ARENA_PLUS_COMBO_COUNT) return 0;
    if (ArenaPlusComposePick_IsCustomMixCombo(combo)) {
        const int pickSum = ArenaPlusComposePick_PendingGilCost();
        if (pickSum > 0) return pickSum;
        return 0;
    }
    char envName[64] = {};
    _snprintf_s(envName, sizeof(envName), _TRUNCATE, "FFXHOOKS_ARENAPLUS_COMBO_GIL_COST_%d", combo);
    int cost = EnvInt(envName, kArenaPlusComboGilCosts[combo]);
    if (cost < 0) cost = 0;
    if (cost > 999999999) cost = 999999999;
    return cost;
}

static int ArenaPlus_BossGilCost(int dark) {
    return ArenaPlus_GilCostForDarkIndex(dark);
}

static int ArenaPlus_DefaultMusicTrack(int dark) {
    if (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) {
        return ARENA_PLUS_MUSIC_TRACK_DEFAULT;
    }
    return 139; /* Silence Before The Storm â€” neutral fallback */
}

static int ArenaPlus_BossMusicTrack(int dark) {
    if (!ArenaPlus_MusicEnabled()) return -1;

    int track = ArenaPlus_DefaultMusicTrack(dark);
    track = EnvInt("FFXHOOKS_ARENAPLUS_MUSIC_TRACK", track);

    int fileTrack = 0;
    if (TryModuleTextInt("arena_plus_music_default.txt", &fileTrack) ||
        TryModuleTextInt("config\\arena_plus_music_default.txt", &fileTrack)) {
        track = fileTrack;
    }

    if (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) {
        char envName[64] = {};
        _snprintf_s(envName, sizeof(envName), _TRUNCATE, "FFXHOOKS_ARENAPLUS_MUSIC_TRACK_%d", dark);
        track = EnvInt(envName, track);

        char rowPath[64] = {};
        char rowConfigPath[80] = {};
        _snprintf_s(rowPath, sizeof(rowPath), _TRUNCATE, "arena_plus_music_%d.txt", dark);
        _snprintf_s(rowConfigPath, sizeof(rowConfigPath), _TRUNCATE, "config\\arena_plus_music_%d.txt", dark);
        if (TryModuleTextInt(rowPath, &fileTrack) ||
            TryModuleTextInt(rowConfigPath, &fileTrack)) {
            track = fileTrack;
        }
    }

    if (track < -1 || track > 0xB5) {
        Log("[ffx-hooks] ArenaPlus: music track out of range row=%d track=%d; override disabled\n",
            dark, track);
        return -1;
    }
    return track;
}

static bool ArenaPlus_ArmMusicOverrideTrack(int dark, int track, const char* phase) {
    if (track < 0) return false;
    if (!g_block) {
        Log("[ffx-hooks] ArenaPlus: music override unavailable row=%d track=%d phase=%s (no shared block)\n",
            dark, track, phase ? phase : "?");
        return false;
    }
    if (!g_musicHookArmed) {
        Log("[ffx-hooks] ArenaPlus: music override requested row=%d track=%d %s%s phase=%s but MusicHook is not armed\n",
            dark,
            track,
            LabMusicRuntimeName(track) ? "name=" : "",
            LabMusicRuntimeName(track) ? LabMusicRuntimeName(track) : "",
            phase ? phase : "?");
        return false;
    }

    InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_block->musicOverrideTrackIndex), track);
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_block->musicSeq));
    Log("[ffx-hooks] ArenaPlus: music override armed row=%d name=%s track=%d%s%s phase=%s target=%s\n",
        dark,
        (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
        track,
        LabMusicRuntimeName(track) ? " " : "",
        LabMusicRuntimeName(track) ? LabMusicRuntimeName(track) : "",
        phase ? phase : "?",
        FfxHooks::GetMusicHookTargetName(MusicHookTargetFromEnv()));
    return true;
}

static bool ArenaPlus_ArmMusicOverride(int dark, const char* phase) {
    return ArenaPlus_ArmMusicOverrideTrack(dark, ArenaPlus_BossMusicTrack(dark), phase);
}

static void ArenaPlus_ClearMusicOverride(int dark, const char* phase) {
    if (!g_block) return;
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_block->musicOverrideTrackIndex), -1);
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_block->musicSeq));
    Log("[ffx-hooks] ArenaPlus: music override cleared row=%d phase=%s\n",
        dark,
        phase ? phase : "?");
}

static uint32_t ArenaPlus_MusicDelayMs() {
    int delayMs = EnvInt("FFXHOOKS_ARENAPLUS_MUSIC_DELAY_MS", 800);
    int fileDelay = 0;
    if (TryModuleTextInt("arena_plus_music_delay_ms.txt", &fileDelay) ||
        TryModuleTextInt("config\\arena_plus_music_delay_ms.txt", &fileDelay)) {
        delayMs = fileDelay;
    }
    if (delayMs < 0) delayMs = 0;
    if (delayMs > 15000) delayMs = 15000;
    return static_cast<uint32_t>(delayMs);
}

static int ArenaPlus_MusicFadeFrames() {
    int fade = EnvInt("FFXHOOKS_ARENAPLUS_MUSIC_FADE_FRAMES", 90);
    int fileFade = 0;
    if (TryModuleTextInt("arena_plus_music_fade_frames.txt", &fileFade) ||
        TryModuleTextInt("config\\arena_plus_music_fade_frames.txt", &fileFade)) {
        fade = fileFade;
    }
    if (fade < 0) fade = 0;
    if (fade > 600) fade = 600;
    return fade;
}

static bool ArenaPlus_TriggerMusicSoundCmd(int dark, int track, const char* phase) {
    if (track < 0) return false;

    /* Lab-proved chain: musicOverride=<desired> + soundcmd 23 <trigger!=desired> 0
     * -> SwitchCrossfade consumes override -> preload -> PlayTrack. */
    const uint32_t triggerTrack = (track == 4) ? 7u : 4u;
    if (g_block) {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_block->musicOverrideTrackIndex), track);
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_block->musicSeq));
    }
    int32_t ret = 0;
    uint32_t status = 0, err = 0;
    const bool ok = LabProbeSoundCmd(23, triggerTrack, 0, &ret, &status, &err);
    Log("[ffx-hooks] ArenaPlus: music lab recipe row=%d name=%s override=%d%s%s trigger=%u phase=%s -> ok=%d status=%u ret=%d err=0x%08X\n",
        dark,
        (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
        track,
        LabMusicRuntimeName(track) ? " " : "",
        LabMusicRuntimeName(track) ? LabMusicRuntimeName(track) : "",
        triggerTrack,
        phase ? phase : "?",
        ok ? 1 : 0,
        status,
        ret,
        err);
    return ok;
}

static bool ArenaPlus_MusicHookProbeSoundCmd(unsigned int triggerTrack, int32_t* retOut) {
    int32_t ret = 0;
    uint32_t status = 0, err = 0;
    const bool ok = LabProbeSoundCmd(23, triggerTrack, 0, &ret, &status, &err);
    if (retOut) {
        *retOut = ret;
    }
    Log("[ffx-hooks] ArenaPlus: MusicHook lab soundcmd trigger=%u override-armed -> ok=%d status=%u ret=%d err=0x%08X\n",
        triggerTrack,
        ok ? 1 : 0,
        status,
        ret,
        err);
    return ok;
}

struct ArenaPlusMusicSoundCmdJob {
    int dark;
    int track;
    uint32_t delayMs;
};

static DWORD WINAPI ArenaPlus_MusicSoundCmdThread(LPVOID param) {
    ArenaPlusMusicSoundCmdJob* job = static_cast<ArenaPlusMusicSoundCmdJob*>(param);
    if (!job) return 0;
    const int dark = job->dark;
    const int track = job->track;
    const uint32_t delayMs = job->delayMs;
    HeapFree(GetProcessHeap(), 0, job);

    if (delayMs > 0) Sleep(delayMs);

    if (FfxHooks::GetArenaBattleMusicPending() < 0) {
        Log("[ffx-hooks] ArenaPlus: music fallback skipped row=%d track=%d (intercept inactive or already handled)\n",
            dark, track);
        return 0;
    }

    ArenaPlus_TriggerMusicSoundCmd(dark, track, "direct-request-delayed-soundcmd");
    return 0;
}

static void ArenaPlus_ScheduleMusicSoundCmd(int dark, int track, const char* phase) {
    if (track < 0 || !ArenaPlus_MusicEnabled()) return;
    ArenaPlusMusicSoundCmdJob* job = static_cast<ArenaPlusMusicSoundCmdJob*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ArenaPlusMusicSoundCmdJob)));
    if (!job) {
        ArenaPlus_TriggerMusicSoundCmd(dark, track, "direct-request-soundcmd-alloc-failed");
        return;
    }

    job->dark = dark;
    job->track = track;
    job->delayMs = ArenaPlus_MusicDelayMs();
    const uint32_t delayMs = job->delayMs;
    HANDLE thread = CreateThread(nullptr, 0, ArenaPlus_MusicSoundCmdThread, job, 0, nullptr);
    if (!thread) {
        const DWORD err = GetLastError();
        HeapFree(GetProcessHeap(), 0, job);
        Log("[ffx-hooks] ArenaPlus: music soundcmd thread create failed row=%d track=%d phase=%s err=0x%08X; trying immediate\n",
            dark, track, phase ? phase : "?", err);
        ArenaPlus_TriggerMusicSoundCmd(dark, track, "direct-request-soundcmd-thread-failed");
        return;
    }
    CloseHandle(thread);
    Log("[ffx-hooks] ArenaPlus: music soundcmd scheduled row=%d name=%s track=%d%s%s delayMs=%u phase=%s\n",
        dark,
        (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
        track,
        LabMusicRuntimeName(track) ? " " : "",
        LabMusicRuntimeName(track) ? LabMusicRuntimeName(track) : "",
        delayMs,
        phase ? phase : "?");
}

static bool ArenaPlus_BossRouteAllowed(int dark) {
    if (!ArenaPlus_BossRouteMapped(dark)) return false;
    if (ArenaPlus_UnlockAllEnabled()) return true;
    return g_arenaPlusDarkReadOk[dark] && g_arenaPlusDarkValues[dark] != 0;
}

static bool ArenaPlus_BossDefeatedFlagSet(int dark) {
    return dark >= 0 && dark < ARENA_DARK_FLAG_LEN &&
           g_arenaPlusDarkReadOk[dark] &&
           g_arenaPlusDarkValues[dark] != 0;
}

static bool ArenaPlus_ReadByteRva(uint32_t dataRva, uint8_t* value, uint32_t* status, uint32_t* err) {
    if (value) *value = 0;
    if (!g_base) {
        if (status) *status = 0;
        if (err) *err = 0;
        return false;
    }
    const uintptr_t abs = g_base + dataRva;
    if (abs > 0xFFFFFFFFu) {
        if (status) *status = FFXPROBE_ST_ERR;
        if (err) *err = 0;
        return false;
    }
    // Already running inside FFX's process and often inside the main-thread menu hook.
    // Do not round-trip through ffx-probe here: waiting for the probe from the hook can deadlock/timeout.
    __try {
        if (value) *value = *reinterpret_cast<volatile uint8_t*>(abs);
        if (status) *status = FFXPROBE_ST_OK;
        if (err) *err = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (status) *status = FFXPROBE_ST_ERR;
        if (err) *err = GetExceptionCode();
        return false;
    }
}

static bool ArenaPlus_ReadU32Rva(uint32_t dataRva, uint32_t* value, uint32_t* status, uint32_t* err) {
    if (value) *value = 0;
    if (!g_base) {
        if (status) *status = 0;
        if (err) *err = 0;
        return false;
    }
    const uintptr_t abs = g_base + dataRva;
    if (abs > 0xFFFFFFFFu) {
        if (status) *status = FFXPROBE_ST_ERR;
        if (err) *err = 0;
        return false;
    }
    __try {
        if (value) *value = *reinterpret_cast<volatile uint32_t*>(abs);
        if (status) *status = FFXPROBE_ST_OK;
        if (err) *err = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (status) *status = FFXPROBE_ST_ERR;
        if (err) *err = GetExceptionCode();
        return false;
    }
}

static bool ArenaPlus_ReadU16Rva(uint32_t dataRva, uint16_t* value, uint32_t* status, uint32_t* err) {
    if (value) *value = 0;
    if (!g_base) {
        if (status) *status = 0;
        if (err) *err = 0;
        return false;
    }
    const uintptr_t abs = g_base + dataRva;
    if (abs > 0xFFFFFFFFu) {
        if (status) *status = FFXPROBE_ST_ERR;
        if (err) *err = 0;
        return false;
    }
    __try {
        if (value) *value = *reinterpret_cast<volatile uint16_t*>(abs);
        if (status) *status = FFXPROBE_ST_OK;
        if (err) *err = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (status) *status = FFXPROBE_ST_ERR;
        if (err) *err = GetExceptionCode();
        return false;
    }
}

static bool ArenaPlus_WriteU32Rva(uint32_t dataRva, uint32_t value, uint32_t* status, uint32_t* err) {
    if (!g_base) {
        if (status) *status = 0;
        if (err) *err = 0;
        return false;
    }
    const uintptr_t abs = g_base + dataRva;
    if (abs > 0xFFFFFFFFu) {
        if (status) *status = FFXPROBE_ST_ERR;
        if (err) *err = 0;
        return false;
    }
    __try {
        *reinterpret_cast<volatile uint32_t*>(abs) = value;
        if (status) *status = FFXPROBE_ST_OK;
        if (err) *err = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (status) *status = FFXPROBE_ST_ERR;
        if (err) *err = GetExceptionCode();
        return false;
    }
}

static bool ArenaPlus_ReadGil(uint32_t* gil, uint32_t* status, uint32_t* err) {
    return ArenaPlus_ReadU32Rva(RVA_SAVE_GIL, gil, status, err);
}

bool ArenaPlus_ReadGilForCompose(uint32_t* gil, uint32_t* status, uint32_t* err) {
    return ArenaPlus_ReadGil(gil, status, err);
}

static bool ArenaPlus_WriteGil(uint32_t gil, uint32_t* status, uint32_t* err) {
    return ArenaPlus_WriteU32Rva(RVA_SAVE_GIL, gil, status, err);
}

static bool ArenaPlus_CheckGilForLaunch(int dark, uint32_t cost, uint32_t* gilBefore) {
    if (gilBefore) *gilBefore = 0;
    if (cost == 0) return true;

    uint32_t gil = 0;
    uint32_t status = 0;
    uint32_t err = 0;
    if (!ArenaPlus_ReadGil(&gil, &status, &err)) {
        Log("[ffx-hooks] ArenaPlus: gil precheck read failed row=%d name=%s cost=%u status=%u err=0x%08X; launch blocked\n",
            dark,
            (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
            cost,
            status,
            err);
        return false;
    }

    if (gil < cost) {
        Log("[ffx-hooks] ArenaPlus: insufficient gil row=%d name=%s cost=%u current=%u; launch blocked\n",
            dark,
            (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
            cost,
            gil);
        return false;
    }

    if (gilBefore) *gilBefore = gil;
    return true;
}

static void ArenaPlus_ChargeGilAfterLaunch(int dark, uint32_t cost, uint32_t gilBefore) {
    if (cost == 0) return;
    const uint32_t gilAfter = gilBefore >= cost ? (gilBefore - cost) : 0;
    uint32_t status = 0;
    uint32_t err = 0;
    const bool ok = ArenaPlus_WriteGil(gilAfter, &status, &err);
    Log("[ffx-hooks] ArenaPlus: direct gil charge %s row=%d name=%s cost=%u gil=%u->%u status=%u err=0x%08X\n",
        ok ? "applied" : "FAILED",
        dark,
        (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
        cost,
        gilBefore,
        gilAfter,
        status,
        err);
}

static bool ArenaPlus_IsBattleQueueArmed() {
    uint8_t queueState = 0;
    uint32_t st = 0;
    uint32_t err = 0;
    if (!ArenaPlus_ReadByteRva(RVA_BATTLE_QUEUE_STATE, &queueState, &st, &err)) return false;
    return queueState == 2;
}

static void ArenaPlus_LogBattleQueueState(const char* phase, const ArenaPlusBossRoute* route, int dark) {
    uint8_t queueGate = 0;
    uint8_t busyGate = 0;
    uint8_t queueState = 0;
    uint16_t queueMode = 0;
    uint32_t queueField = 0;
    uint8_t queueGroup = 0;
    uint8_t queueFormation = 0;
    uint32_t flags0 = 0;
    uint32_t flags1 = 0;
    uint32_t flags2 = 0;
    uint32_t st = 0, err = 0;

    const bool okQueueGate = ArenaPlus_ReadByteRva(RVA_BATTLE_QUEUE_GATE, &queueGate, &st, &err);
    const bool okBusyGate = ArenaPlus_ReadByteRva(RVA_BATTLE_BUSY_GATE, &busyGate, &st, &err);
    const bool okQueueState = ArenaPlus_ReadByteRva(RVA_BATTLE_QUEUE_STATE, &queueState, &st, &err);
    const bool okQueueMode = ArenaPlus_ReadU16Rva(RVA_BATTLE_QUEUE_MODE, &queueMode, &st, &err);
    const bool okQueueField = ArenaPlus_ReadU32Rva(RVA_BATTLE_QUEUE_FIELD, &queueField, &st, &err);
    const bool okQueueGroup = ArenaPlus_ReadByteRva(RVA_BATTLE_QUEUE_GROUP, &queueGroup, &st, &err);
    const bool okQueueFormation = ArenaPlus_ReadByteRva(RVA_BATTLE_QUEUE_FORMATION, &queueFormation, &st, &err);
    const bool okFlags0 = ArenaPlus_ReadU32Rva(RVA_BATTLE_FLAGS_0, &flags0, &st, &err);
    const bool okFlags1 = ArenaPlus_ReadU32Rva(RVA_BATTLE_FLAGS_1, &flags1, &st, &err);
    const bool okFlags2 = ArenaPlus_ReadU32Rva(RVA_BATTLE_FLAGS_2, &flags2, &st, &err);

    Log("[ffx-hooks] ArenaPlus: battle queue %s row=%d name=%s battleId=%s token=0x%08X gate=%s%u busy=%s%u n2=%s%u c254=%s%08X fieldHi=%u group=%s%u formation=%s%u mode=%s%04X flags=[%s%08X %s%08X %s%08X]\n",
        phase ? phase : "?",
        dark,
        (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
        route && route->battleId ? route->battleId : "?",
        route ? route->battleToken : 0,
        okQueueGate ? "" : "?", static_cast<unsigned>(queueGate),
        okBusyGate ? "" : "?", static_cast<unsigned>(busyGate),
        okQueueState ? "" : "?", static_cast<unsigned>(queueState),
        okQueueField ? "" : "?", queueField,
        static_cast<unsigned>((queueField >> 16) & 0xFFFFu),
        okQueueGroup ? "" : "?", static_cast<unsigned>(queueGroup),
        okQueueFormation ? "" : "?", static_cast<unsigned>(queueFormation),
        okQueueMode ? "" : "?", static_cast<unsigned>(queueMode),
        okFlags0 ? "" : "?", flags0,
        okFlags1 ? "" : "?", flags1,
        okFlags2 ? "" : "?", flags2);
}

static bool ArenaPlus_CallCommonSetBattleFlags0200(const char* phase, const ArenaPlusBossRoute& route, int dark) {
    if (!g_base) return false;
    typedef int (__cdecl* FnSetBattleFlags)(int, int, int);
    int ret = 0;
    uint32_t err = 0;
    bool ok = false;
    __try {
        ret = reinterpret_cast<FnSetBattleFlags>(g_base + RVA_COMMON_SET_BATTLE_FLAGS)(0x0200, 0, 0);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        err = GetExceptionCode();
        ok = false;
    }
    Log("[ffx-hooks] ArenaPlus: Common.SetBattleFlags backend %s row=%d name=%s battleId=%s args=[0x0200 0 0] -> ok=%d ret=0x%08X err=0x%08X\n",
        phase ? phase : "?",
        dark,
        (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
        route.battleId ? route.battleId : "?",
        ok ? 1 : 0,
        static_cast<unsigned>(ret),
        err);
    return ok;
}

static bool ArenaPlus_ReadFlag(const char* label, uint32_t dataRva, uint8_t* value) {
    uint32_t status = 0, err = 0;
    const bool ok = ArenaPlus_ReadByteRva(dataRva, value, &status, &err);
    if (!ok || status != FFXPROBE_ST_OK) {
        Log("[ffx-hooks] ArenaPlus: read fail %s rva=0x%08X status=%u err=0x%08X\n",
            label ? label : "?", dataRva, status, err);
        return false;
    }
    return true;
}

static const uint32_t FFX_SAVE_FILE_HEADER = 0x40u;
static const uint32_t FFX_SAVE_GIL_FILE_OFF = 0x3D88u;           // file = header + SaveData+0x3D48
static const uint32_t FFX_SAVE_ARENA_CAPTURE_FILE_OFF = 0x424Cu; // file = header + SaveData+0x420C
static const uint32_t FFXED_DARK_AEON_FILE_OFF = 3273u;          // FFXED absolute in PC .ffx blob
static const uint32_t FFX_SAVE_PENANCE_FILE_OFF = 0x193Cu;       // file = header + SaveData+0x18FC
static const int FFX_SAVE_FILE_MIN_SIZE = 0x4300;                // past arena captures + ffxed bytes

struct ArenaPlusDiskDarkCache {
    bool valid = false;
    int matchedSlot = -1;
    uint8_t ffxedBytes[8] = {};
    uint8_t penanceByte = 0;
};

static ArenaPlusDiskDarkCache g_arenaPlusDiskDark = {};

static bool TryEnvString(const char* name, char* out, size_t outCap) {
    if (!name || !out || outCap == 0) return false;
    DWORD len = GetEnvironmentVariableA(name, out, static_cast<DWORD>(outCap));
    return len > 0 && len < outCap;
}

static bool ArenaPlus_BuildSaveSlotPath(int slot, char* path, size_t pathCap) {
    if (!path || pathCap == 0 || slot < 0 || slot > 9) return false;
    char dir[MAX_PATH] = {};
    if (TryEnvString("FFXHOOKS_ARENAPLUS_SAVE_DIR", dir, sizeof(dir))) {
        _snprintf_s(path, pathCap, _TRUNCATE, "%s\\ffx_%03d", dir, slot);
        return true;
    }
    char profile[MAX_PATH] = {};
    if (GetEnvironmentVariableA("USERPROFILE", profile, sizeof(profile)) == 0) return false;
    _snprintf_s(path, pathCap, _TRUNCATE,
        "%s\\Documents\\SQUARE ENIX\\FINAL FANTASY X&X-2 HD Remaster\\FINAL FANTASY X\\ffx_%03d",
        profile, slot);
    return true;
}

static bool ArenaPlus_ReadSaveFile(const char* path, uint8_t* fileBuf, size_t fileBufCap, DWORD* outRead) {
    if (!path || !fileBuf || fileBufCap == 0 || !outRead) return false;
    *outRead = 0;
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER fileSize = {};
    if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart < FFX_SAVE_FILE_MIN_SIZE) {
        CloseHandle(file);
        return false;
    }
    const DWORD toRead = static_cast<DWORD>((fileSize.QuadPart < static_cast<LONGLONG>(fileBufCap))
        ? fileSize.QuadPart : static_cast<LONGLONG>(fileBufCap));
    DWORD read = 0;
    const BOOL ok = ReadFile(file, fileBuf, toRead, &read, nullptr);
    CloseHandle(file);
    if (!ok || read < static_cast<DWORD>(FFX_SAVE_FILE_MIN_SIZE)) return false;
    *outRead = read;
    return true;
}

static int ArenaPlus_FileArenaCaptureSum(const uint8_t* fileBuf, DWORD fileLen) {
    if (!fileBuf || fileLen < FFX_SAVE_ARENA_CAPTURE_FILE_OFF + ARENA_CAPTURE_COUNT_LEN) return -1;
    int sum = 0;
    for (int i = 0; i < ARENA_CAPTURE_COUNT_LEN; ++i) {
        sum += fileBuf[FFX_SAVE_ARENA_CAPTURE_FILE_OFF + static_cast<size_t>(i)];
    }
    return sum;
}

static void ArenaPlus_RefreshDiskSaveDarkCache() {
    g_arenaPlusDiskDark = {};
    uint32_t ramGil = 0;
    uint32_t st = 0, err = 0;
    if (!ArenaPlus_ReadU32Rva(RVA_SAVE_GIL, &ramGil, &st, &err) || st != FFXPROBE_ST_OK) {
        Log("[ffx-hooks] ArenaPlus: disk save match skipped (ram gil read fail)\n");
        return;
    }
    int ramCaptureSum = 0;
    for (int i = 0; i < ARENA_CAPTURE_COUNT_LEN; ++i) {
        uint8_t v = 0;
        if (!ArenaPlus_ReadFlag("captureCounts", RVA_ARENA_CAPTURE_COUNTS + static_cast<uint32_t>(i), &v)) {
            Log("[ffx-hooks] ArenaPlus: disk save match skipped (ram capture read fail)\n");
            return;
        }
        ramCaptureSum += v;
    }

    uint8_t fileBuf[28000] = {};
    for (int slot = 0; slot <= 9; ++slot) {
        char path[MAX_PATH] = {};
        if (!ArenaPlus_BuildSaveSlotPath(slot, path, sizeof(path))) continue;
        DWORD fileLen = 0;
        if (!ArenaPlus_ReadSaveFile(path, fileBuf, sizeof(fileBuf), &fileLen)) continue;

        if (fileLen < FFX_SAVE_GIL_FILE_OFF + 4u) continue;
        const uint32_t fileGil = static_cast<uint32_t>(fileBuf[FFX_SAVE_GIL_FILE_OFF]) |
            (static_cast<uint32_t>(fileBuf[FFX_SAVE_GIL_FILE_OFF + 1]) << 8) |
            (static_cast<uint32_t>(fileBuf[FFX_SAVE_GIL_FILE_OFF + 2]) << 16) |
            (static_cast<uint32_t>(fileBuf[FFX_SAVE_GIL_FILE_OFF + 3]) << 24);
        const int fileCaptureSum = ArenaPlus_FileArenaCaptureSum(fileBuf, fileLen);
        if (fileCaptureSum < 0 || fileGil != ramGil || fileCaptureSum != ramCaptureSum) continue;

        if (fileLen < FFXED_DARK_AEON_FILE_OFF + 8u) continue;
        for (int i = 0; i < 8; ++i) {
            g_arenaPlusDiskDark.ffxedBytes[i] = fileBuf[FFXED_DARK_AEON_FILE_OFF + static_cast<size_t>(i)];
        }
        g_arenaPlusDiskDark.penanceByte = (fileLen > FFX_SAVE_PENANCE_FILE_OFF)
            ? fileBuf[FFX_SAVE_PENANCE_FILE_OFF] : 0u;
        g_arenaPlusDiskDark.valid = true;
        g_arenaPlusDiskDark.matchedSlot = slot;
        Log("[ffx-hooks] ArenaPlus: disk save matched slot=%d gil=%u captureSum=%d ffxed=[%02X %02X %02X %02X %02X %02X %02X %02X] penance=0x%02X\n",
            slot, ramGil, ramCaptureSum,
            g_arenaPlusDiskDark.ffxedBytes[0], g_arenaPlusDiskDark.ffxedBytes[1],
            g_arenaPlusDiskDark.ffxedBytes[2], g_arenaPlusDiskDark.ffxedBytes[3],
            g_arenaPlusDiskDark.ffxedBytes[4], g_arenaPlusDiskDark.ffxedBytes[5],
            g_arenaPlusDiskDark.ffxedBytes[6], g_arenaPlusDiskDark.ffxedBytes[7],
            g_arenaPlusDiskDark.penanceByte);
        return;
    }
    Log("[ffx-hooks] ArenaPlus: disk save match failed (gil=%u captureSum=%d)\n", ramGil, ramCaptureSum);
}

static bool ArenaPlus_ReadDarkAeonDefeated(int index, uint8_t* rawByte, bool* defeated, bool allowDiskCache = true) {
    if (index < 0 || index >= ARENA_DARK_FLAG_LEN || !rawByte || !defeated) return false;
    const ArenaPlusDarkFlagSpec& spec = kArenaPlusDarkFlagSpecs[index];
    uint8_t ffxedByte = 0;
    uint8_t runtimeByte = 0;
    const bool okFfxed = ArenaPlus_ReadFlag("darkAeonFfxed", spec.byteRva, &ffxedByte);
    const bool okRuntime = ArenaPlus_ReadFlag(
        "darkAeonRuntime", RVA_DARK_AEON_RUNTIME_BASE + static_cast<uint32_t>(index), &runtimeByte);

    bool ffxedDef = false;
    if (index < 8) {
        ffxedDef = ((ffxedByte >> spec.bit) & 1u) != 0;
    } else {
        ffxedDef = ffxedByte != 0;
    }
    const bool runtimeDef = runtimeByte != 0;

    uint8_t diskByte = 0;
    bool diskDef = false;
    if (allowDiskCache && g_arenaPlusDiskDark.valid) {
        if (index < 8) {
            diskByte = g_arenaPlusDiskDark.ffxedBytes[index];
            diskDef = ((diskByte >> 7) & 1u) != 0;
        } else {
            diskByte = g_arenaPlusDiskDark.penanceByte;
            diskDef = diskByte != 0;
        }
    }

    *defeated = ffxedDef || runtimeDef || diskDef;
    *rawByte = runtimeByte ? runtimeByte : (diskByte ? diskByte : ffxedByte);
    return okFfxed || okRuntime || (allowDiskCache && g_arenaPlusDiskDark.valid);
}

static FfxHooks::ArenaMix::Rules ArenaPlus_MixRules() {
    FfxHooks::ArenaMix::Rules rules{};
    rules.bypass = ArenaPlus_UnlockAllEnabled();
    rules.requiredSlots = g_arenaPlusMixRequiredSlots;
    // Only the current in-memory save contributes unlocks. A cached disk slot from
    // Dark Rematch must not unlock bosses after loading a different save.
    for (int i = 0; i < ARENA_PLUS_ULTRA_CHOICE_COUNT; ++i) {
        uint8_t raw = 0;
        bool defeated = false;
        if (ArenaPlus_ReadDarkAeonDefeated(i, &raw, &defeated, false) && defeated)
            rules.defeatedMask |= static_cast<uint16_t>(1u << i);
    }
    // Read the current save mirror only. A missing byte never unlocks a creation.
    for(unsigned i=0;i<35;++i) {
        uint8_t value=0;uint32_t status=0,error=0;
        if(ArenaPlus_ReadByteRva(RVA_ARENA_UNLOCK_FLAGS+i,&value,&status,&error)) {
            const auto bit=uint64_t{1}<<i;rules.arenaKnownMask|=bit;
            if(value)rules.arenaUnlockedMask|=bit;
        }
    }
    return rules;
}

static float ArenaPlus_ListLeft() {
    if(ArenaPlus_ShowFormationPane())return 0.057f;
    return g_arenaPlusMenuKind==ArenaPlusMenuKind::Positions?0.46f:
        (g_arenaPlusMenuKind==ArenaPlusMenuKind::Library||g_arenaPlusMenuKind==ArenaPlusMenuKind::LibraryItem||g_arenaPlusMenuKind==ArenaPlusMenuKind::Rename)?0.12f:0.271f;
}
static float ArenaPlus_ListWidth() {
    if(ArenaPlus_ShowFormationPane())return 0.57f;
    return (g_arenaPlusMenuKind==ArenaPlusMenuKind::Library||g_arenaPlusMenuKind==ArenaPlusMenuKind::LibraryItem||g_arenaPlusMenuKind==ArenaPlusMenuKind::Rename)?0.76f:0.458f;
}
static float ArenaPlus_RowStep() {
    return g_arenaPlusMenuKind == ArenaPlusMenuKind::Hub ? 0.105f : 0.063f;
}

static float ArenaPlus_RowHeight() {
    return g_arenaPlusMenuKind == ArenaPlusMenuKind::Hub ? 0.095f : 0.056f;
}

static void ArenaLibraryBuildRows(ArenaPlusMenuKind kind) {
    if(kind==ArenaPlusMenuKind::Library){
        g_arenaPlusActiveRowCount=static_cast<int>(g_arenaLibraryEntries.size())+2;
        for(size_t i=0;i<g_arenaLibraryEntries.size();++i){const auto& e=g_arenaLibraryEntries[i];
            _snprintf_s(g_arenaPlusLabels[i],ARENA_PLUS_LABEL_CAP,_TRUNCATE,"%s%.40s",
                !e.valid?"[Invalid] ":e.builtin?"[Preset] ":e.preset.legacy?"[Legacy] ":"",e.preset.name.c_str());}
        strcpy_s(g_arenaPlusLabels[g_arenaLibraryEntries.size()],"Refresh Library");
        strcpy_s(g_arenaPlusLabels[g_arenaLibraryEntries.size()+1],"Back");
    }else if(kind==ArenaPlusMenuKind::LibraryItem){
        g_arenaPlusActiveRowCount=5;
        strcpy_s(g_arenaPlusLabels[0],g_arenaLibraryEntry.preset.legacy?"Convert to Current Arena":g_arenaLibraryEntry.builtin?"Load Preset for Editing":"Load JSON for Editing");
        strcpy_s(g_arenaPlusLabels[1],g_arenaLibraryEntry.builtin||g_arenaLibraryEntry.preset.legacy?"Editor Import: Export First":"Import Edited Battle (.bin)");
        strcpy_s(g_arenaPlusLabels[2],"Export a Copy");
        strcpy_s(g_arenaPlusLabels[3],g_arenaLibraryEntry.builtin||g_arenaLibraryEntry.preset.legacy?"Rename: Export a Copy First":"Rename Battle");
        strcpy_s(g_arenaPlusLabels[4],"Back");
    }else{
        g_arenaPlusActiveRowCount=3;
        AcquireSRWLockShared(&g_arenaRenameLock);
        _snprintf_s(g_arenaPlusLabels[0],ARENA_PLUS_LABEL_CAP,_TRUNCATE,"%s_",g_arenaRenameDraft);
        ReleaseSRWLockShared(&g_arenaRenameLock);
        strcpy_s(g_arenaPlusLabels[1],"Save Name");strcpy_s(g_arenaPlusLabels[2],"Cancel");
    }
    for(int i=0;i<g_arenaPlusActiveRowCount;++i)NativeMenu::EncodeLabel(g_arenaPlusLabels[i],g_arenaPlusLabelBytes[i],ARENA_PLUS_LABEL_CAP);
}

static int ArenaPlus_MenuRowCount(ArenaPlusMenuKind kind) {
    switch (kind) {
    case ArenaPlusMenuKind::Monsters: return g_arenaMonsterCount+2;
    case ArenaPlusMenuKind::Search: return 3;
    case ArenaPlusMenuKind::Battles: return g_arenaBattleCount+2;
    case ArenaPlusMenuKind::BattleDetail: return (g_arenaBattleDetail?g_arenaBattleDetail->count:0)+3;
    case ArenaPlusMenuKind::Soundtrack: return static_cast<int>(std::size(FfxHooks::ArenaSoundtrack::kTracks))+1;
    case ArenaPlusMenuKind::Formation: return g_arenaPlusUltraSelection.activationCount+1;
    case ArenaPlusMenuKind::Scenery: return g_arenaPlusMixRequiredSlots?static_cast<int>(FfxHooks::ArenaScenery::Count(g_arenaPlusMixRequiredSlots))+1:g_arenaSceneryCount+2;
    case ArenaPlusMenuKind::Hub: return ARENA_PLUS_HUB_ROW_COUNT;
    case ArenaPlusMenuKind::DarkRematch: return ARENA_DARK_FLAG_LEN + 1;
    case ArenaPlusMenuKind::AeonGauntlet: return ARENA_PLUS_PRESET_COMBO_COUNT + 1;
    case ArenaPlusMenuKind::CustomMix: return ARENA_PLUS_CUSTOM_MIX_COMBO_COUNT + 2;
    case ArenaPlusMenuKind::Ultra: return ARENA_PLUS_ULTRA_ROW_COUNT+(g_arenaPlusMixRequiredSlots?0:2);
    case ArenaPlusMenuKind::Positions: return ARENA_PLUS_POSITION_ROW_COUNT;
    case ArenaPlusMenuKind::Library: return static_cast<int>(g_arenaLibraryEntries.size())+2;
    case ArenaPlusMenuKind::LibraryItem: return 5;
    case ArenaPlusMenuKind::Rename: return 3;
    default: return ARENA_PLUS_HUB_ROW_COUNT;
    }
}

static int ArenaPlus_SubMenuBackRow(ArenaPlusMenuKind kind) {
    switch (kind) {
    case ArenaPlusMenuKind::Monsters:
    case ArenaPlusMenuKind::Soundtrack:
    case ArenaPlusMenuKind::Formation:
    case ArenaPlusMenuKind::Search:
    case ArenaPlusMenuKind::Battles:
    case ArenaPlusMenuKind::BattleDetail: return ArenaPlus_MenuRowCount(kind)-1;
    case ArenaPlusMenuKind::Scenery: return ArenaPlus_MenuRowCount(kind)-1;
    case ArenaPlusMenuKind::DarkRematch: return ARENA_DARK_FLAG_LEN;
    case ArenaPlusMenuKind::AeonGauntlet: return ARENA_PLUS_PRESET_COMBO_COUNT;
    case ArenaPlusMenuKind::CustomMix: return ARENA_PLUS_CUSTOM_MIX_COMBO_COUNT + 1;
    case ArenaPlusMenuKind::Ultra: return ARENA_PLUS_ULTRA_ROW_BACK+(g_arenaPlusMixRequiredSlots?0:2);
    case ArenaPlusMenuKind::Positions: return ARENA_PLUS_POSITION_ROW_COUNT - 1;
    case ArenaPlusMenuKind::Library: return static_cast<int>(g_arenaLibraryEntries.size())+1;
    case ArenaPlusMenuKind::LibraryItem: return 4;
    case ArenaPlusMenuKind::Rename: return 2;
    default: return -1;
    }
}

static const char* ArenaPlus_UltraPreviewSlotName(uint16_t monsterId) {
    return FfxHooks::ArenaMonsters::Name(monsterId);
}

static void ArenaPlus_BuildUltraPreview() {
    FfxHooks::ArenaBrowser::FormatSummary(g_arenaPlusUltraSelection,g_arenaPlusMixRequiredSlots,
        FfxHooks::CustomMixUltra::Runtime::StatusName(FfxHooks::CustomMixUltra::Runtime::ProductionStatus().code),
        g_arenaPlusUltraPreview,sizeof(g_arenaPlusUltraPreview));
}

static uint32_t ArenaPlus_MixEntryCost() {
    uint64_t total = 0;
    if (g_arenaPlusMixRequiredSlots &&
        FfxHooks::CustomMixUltra::BuildSelection(g_arenaPlusUltraSelection).result ==
            FfxHooks::CustomMixUltra::SelectionResult::Ready) {
        for (uint8_t i = 0; i < g_arenaPlusUltraSelection.activationCount; ++i)
            total += static_cast<uint32_t>(ArenaPlus_GilCostForDarkIndex(
                static_cast<int>(g_arenaPlusUltraSelection.activations[i])));
    }
    return static_cast<uint32_t>(total > 999999999u ? 999999999u : total);
}

static void ArenaPlus_BuildUltraRows() {
    g_arenaPlusActiveRowCount = ARENA_PLUS_ULTRA_ROW_COUNT;
    ArenaPlus_BuildUltraPreview();
    const auto rules = ArenaPlus_MixRules();
    for (int row = 0; row < ARENA_PLUS_ULTRA_CHOICE_COUNT; ++row) {
        const bool unlocked = FfxHooks::ArenaMix::ChoiceUnlocked(kArenaPlusUltraChoices[row], rules);
        _snprintf_s(g_arenaPlusLabels[row + ARENA_PLUS_ULTRA_ROW_FIRST_CHOICE], ARENA_PLUS_LABEL_CAP, _TRUNCATE,
            "%s  %s", kArenaPlusUltraChoiceNames[row], unlocked ? (row == 7 ? "+3" : "+1") : "OFF");
    }
    if(g_arenaPlusMixRequiredSlots==0) {
        for(int row=0;row<6;++row)
            _snprintf_s(g_arenaPlusLabels[2+row],ARENA_PLUS_LABEL_CAP,_TRUNCATE,"%s  >",FfxHooks::ArenaMonsters::kCategoryNames[row]);
        const auto* track=FfxHooks::ArenaSoundtrack::Get(g_arenaPlusUltraSelection.musicTrack);
        _snprintf_s(g_arenaPlusLabels[8],ARENA_PLUS_LABEL_CAP,_TRUNCATE,"Music: %.32s%s",track?track->name:"Invalid",ArenaPlus_MusicEnabled()?"":" [OFF]");
        _snprintf_s(g_arenaPlusLabels[9],ARENA_PLUS_LABEL_CAP,_TRUNCATE,"Your Formation  %u/8  >",FfxHooks::CustomMixUltra::BuildSelection(g_arenaPlusUltraSelection).expanded.monsterCount);
    }
    const auto* scenery = FfxHooks::ArenaScenery::Get(g_arenaPlusUltraSelection.scenery);
    _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_ULTRA_ROW_SCENERY],
        ARENA_PLUS_LABEL_CAP, _TRUNCATE, "Arena: %s", scenery ? scenery->label : "Choose arena");
    _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_ULTRA_ROW_CAMERA], ARENA_PLUS_LABEL_CAP, _TRUNCATE,
        "Camera: %s",g_arenaPlusUltraSelection.camera==FfxHooks::ArenaScenery::Camera::Tactical?"Tactical (overhead)":"Arena default");
    _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_ULTRA_ROW_AUTO],
        ARENA_PLUS_LABEL_CAP, _TRUNCATE, "Auto Arrange");
    _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_ULTRA_ROW_POSITIONS],
        ARENA_PLUS_LABEL_CAP, _TRUNCATE, "Edit Positions");
    _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_ULTRA_ROW_NATIVE],
        ARENA_PLUS_LABEL_CAP, _TRUNCATE, "Use Native Positions");
    _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_ULTRA_ROW_REMOVE_LAST],
        ARENA_PLUS_LABEL_CAP, _TRUNCATE, "Remove Last");
    _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_ULTRA_ROW_CLEAR],
        ARENA_PLUS_LABEL_CAP, _TRUNCATE, "Clear");
    strcpy_s(g_arenaPlusLabels[ARENA_PLUS_ULTRA_ROW_EXPORT],"Export Battle");
    strcpy_s(g_arenaPlusLabels[ARENA_PLUS_ULTRA_ROW_LIBRARY],"Saved Battles");
    _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_ULTRA_ROW_LAUNCH],
        ARENA_PLUS_LABEL_CAP, _TRUNCATE, "Launch  %s",
        ArenaPlus_MixEnabled() && FfxHooks::ArenaMix::CanLaunch(g_arenaPlusUltraSelection, rules)
            ? "READY" : "OFF");
    const uint32_t cost = ArenaPlus_MixEntryCost();
    if (cost && ArenaPlus_MixEnabled() &&
        FfxHooks::ArenaMix::CanLaunch(g_arenaPlusUltraSelection, rules))
        _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_ULTRA_ROW_LAUNCH],
            ARENA_PLUS_LABEL_CAP, _TRUNCATE, "Launch  %uG", cost);
    _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_ULTRA_ROW_BACK],
        ARENA_PLUS_LABEL_CAP, _TRUNCATE, "Back");
    if(!g_arenaPlusMixRequiredSlots) {
        for(int row=18;row>=8;--row)strcpy_s(g_arenaPlusLabels[row+2],g_arenaPlusLabels[row]);
        for(int row=7;row>=2;--row)strcpy_s(g_arenaPlusLabels[row+1],g_arenaPlusLabels[row]);
        strcpy_s(g_arenaPlusLabels[2],"Search All Monsters  >");
        strcpy_s(g_arenaPlusLabels[9],"Battle Presets  >");
        g_arenaPlusActiveRowCount=21;
    }
    for (int row = 0; row < g_arenaPlusActiveRowCount; ++row) {
        NativeMenu::EncodeLabel(
            g_arenaPlusLabels[row], g_arenaPlusLabelBytes[row], ARENA_PLUS_LABEL_CAP);
    }
}

static const char* ArenaPlus_PositionName(uint8_t slot) {
    const auto expanded = FfxHooks::CustomMixUltra::BuildSelection(g_arenaPlusUltraSelection);
    if (slot >= expanded.expanded.monsterCount) return "Empty";
    return FfxHooks::ArenaMonsters::Name(expanded.expanded.monsterIds[slot]);
}

static void ArenaPlus_BuildPositionRows() {
    g_arenaPlusActiveRowCount = ARENA_PLUS_POSITION_ROW_COUNT;
    const auto point = g_arenaPositionDraft.points[g_arenaPositionSlot];
    _snprintf_s(g_arenaPlusLabels[0], ARENA_PLUS_LABEL_CAP, _TRUNCATE,
        "Slot %u: %s", static_cast<unsigned>(g_arenaPositionSlot + 1u),
        ArenaPlus_PositionName(g_arenaPositionSlot));
    _snprintf_s(g_arenaPlusLabels[1], ARENA_PLUS_LABEL_CAP, _TRUNCATE, "X Side       %.2f", point.x);
    _snprintf_s(g_arenaPlusLabels[2], ARENA_PLUS_LABEL_CAP, _TRUNCATE, "Z Depth      %.2f", point.z);
    strcpy_s(g_arenaPlusLabels[3], "Reset This Slot");
    strcpy_s(g_arenaPlusLabels[4], "Auto Arrange All");
    strcpy_s(g_arenaPlusLabels[5], "Apply Positions");
    strcpy_s(g_arenaPlusLabels[6], "Cancel");
    for (int row = 0; row < ARENA_PLUS_POSITION_ROW_COUNT; ++row)
        NativeMenu::EncodeLabel(g_arenaPlusLabels[row], g_arenaPlusLabelBytes[row], ARENA_PLUS_LABEL_CAP);
}

static bool ArenaPlus_AdjustPosition(int row, int direction) {
    if (!g_arenaPositionDraft.enabled || g_arenaPositionDraft.count == 0u) return false;
    bool changed = false;
    if (row == 0) {
        const int count = g_arenaPositionDraft.count;
        g_arenaPositionSlot = static_cast<uint8_t>((g_arenaPositionSlot + direction + count) % count);
        changed = true;
    } else if (row == 1 || row == 2) {
        const float step = (GetAsyncKeyState(VK_SHIFT)&0x8000) ? 1.0f : 4.0f;
        changed = FfxHooks::ArenaPositions::Move(&g_arenaPositionDraft, g_arenaPositionSlot,
            row == 1 ? step * direction : 0.0f, row == 2 ? step * direction : 0.0f);
    }
    if (changed) ArenaPlus_BuildPositionRows();
    NativeMenu::PlaySfx(changed ? 1 : 3);
    return changed;
}

static void ArenaPlus_DrawPositionPreview() {
    using namespace NativeMenu;
    const float left = NX(0.053f), top = NY(0.245f), width = NW(0.34f), height = NH(0.47f);
    DrawSolidRect(left, top, width, height, 0xE00D1924u, 0xE006111Au);
    const auto drawPoint = [=](float x, float z, const char* label, bool selected, bool party) {
        const float px = left + width * ((x + 170.0f) / 340.0f);
        const float py = top + height * ((220.0f - z) / 320.0f);
        const float size = NW(selected ? 0.009f : 0.006f);
        const unsigned color = party ? 0xFF709CBEu : selected ? kMenuNeonGreenLine : 0xFFD4B7F4u;
        DrawSolidRect(px - size, py - size, size * 2, size * 2, color, color);
        unsigned char encoded[16] = {};
        EncodeLabel(label, encoded, 16);
        DrawStringSub(encoded, px + size, py - NH(0.009f));
    };
    std::array<FfxHooks::ArenaPositions::Point,7> partyPoints{};
    const auto partyCount=FfxHooks::ArenaBattleProgram::PartyPreview(g_arenaPlusUltraSelection.scenery,g_arenaPlusUltraSelection.camera,&partyPoints);
    for(unsigned i=0;i<partyCount;++i)drawPoint(partyPoints[i].x,partyPoints[i].z,"P",false,true);
    for (uint8_t i = 0; i < g_arenaPositionDraft.count && i < 8u; ++i) {
        char number[8] = {}; _snprintf_s(number, sizeof(number), _TRUNCATE, "%u", static_cast<unsigned>(i+1u));
        drawPoint(g_arenaPositionDraft.points[i].x, g_arenaPositionDraft.points[i].z,
            number, i == g_arenaPositionSlot, false);
    }
    unsigned char hint[64] = {};
    EncodeLabel("X / Z relative to party   P = party", hint, 64);
    DrawStringSub(hint, left, top + height + NH(0.022f));
}

static void ArenaPlus_BuildHubRows() {
    g_arenaPlusActiveRowCount = ARENA_PLUS_HUB_ROW_COUNT;
    _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_HUB_ROW_SAFE], ARENA_PLUS_LABEL_CAP, _TRUNCATE,
        "RT2 Current Battle");
    NativeMenu::EncodeLabel("Replay the current encounter",
        g_arenaPlusHubDescBytes[ARENA_PLUS_HUB_ROW_SAFE], ARENA_PLUS_LABEL_CAP);
    _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_HUB_ROW_DARK], ARENA_PLUS_LABEL_CAP, _TRUNCATE,
        "Dark Aeon Rematch");
    NativeMenu::EncodeLabel("Face the fallen guardians alone",
        g_arenaPlusHubDescBytes[ARENA_PLUS_HUB_ROW_DARK], ARENA_PLUS_LABEL_CAP);
    _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_HUB_ROW_GAUNTLET], ARENA_PLUS_LABEL_CAP, _TRUNCATE,
        "Aeon Gauntlet");
    NativeMenu::EncodeLabel("Fight a preset group of bosses",
        g_arenaPlusHubDescBytes[ARENA_PLUS_HUB_ROW_GAUNTLET], ARENA_PLUS_LABEL_CAP);
    _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_HUB_ROW_MIX], ARENA_PLUS_LABEL_CAP, _TRUNCATE,
        "Custom Mix");
    NativeMenu::EncodeLabel("Choose three, four or five bosses",
        g_arenaPlusHubDescBytes[ARENA_PLUS_HUB_ROW_MIX], ARENA_PLUS_LABEL_CAP);
    const auto ultraStatus = FfxHooks::CustomMixUltra::Runtime::ProductionStatus();
    _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_HUB_ROW_ULTRA],
        ARENA_PLUS_LABEL_CAP, _TRUNCATE, "Custom Mix Ultra");
    const char* ultraDescription = "Monsters, Arena creations and Dark Aeons";
    if (ultraStatus.code == FfxHooks::CustomMixUltra::Runtime::StatusCode::RestoreConflict)
        ultraDescription = "Restore failed - restart required";
    else if (!F8CatalogGateEnabled("arena_plus.master"))
        ultraDescription = "Turn Arena+ Master ON and restart";
    else if (!ArenaPlus_MixAvailable())
        ultraDescription = "Arena+ setup unavailable - check hooks log";
    else if (!ArenaPlus_MixEnabled())
        ultraDescription = "Turn Compose F7 ON in F8";
    NativeMenu::EncodeLabel(ultraDescription,
        g_arenaPlusHubDescBytes[ARENA_PLUS_HUB_ROW_ULTRA], ARENA_PLUS_LABEL_CAP);
    g_arenaPlusHubDescBytes[ARENA_PLUS_HUB_ROW_BACK][0] = 0;
    _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_HUB_ROW_BACK], ARENA_PLUS_LABEL_CAP, _TRUNCATE,
        "Back");
    for (int row = 0; row < g_arenaPlusActiveRowCount; ++row) {
        NativeMenu::EncodeLabel(
            g_arenaPlusLabels[row], g_arenaPlusLabelBytes[row], ARENA_PLUS_LABEL_CAP);
    }
}

static int ArenaPlus_CustomMixComboIndex(int mixRow) {
    return ARENA_PLUS_PRESET_COMBO_COUNT + mixRow;
}

static void ArenaPlus_BuildComboRowLabel(int combo, int labelRow) {
    const bool comboEnabled = ArenaPlus_ComboBattlesEnabled();
    const bool mapped = ArenaPlus_ComboRouteMapped(combo);
    const char* status = mapped ? (comboEnabled ? "READY COMBO" : "LAB FLAG") : "NO ROUTE";
    char statusBuf[40] = {};
    const int gilCost = ArenaPlus_ComboGilCost(combo);
    if (combo >= ARENA_PLUS_PRESET_COMBO_COUNT) {
        const auto rules = ArenaPlus_MixRules();
        status = !ArenaPlus_MixEnabled() ? "OFF" :
            (rules.bypass || rules.defeatedMask != 0u) ? "READY" : "OFF";
    } else if (mapped && comboEnabled && !ArenaPlus_ComboRouteAllowed(combo)) {
        status = "OFF";
    } else if (mapped && comboEnabled && gilCost > 0) {
        _snprintf_s(statusBuf, sizeof(statusBuf), _TRUNCATE, "COST %dG", gilCost);
        status = statusBuf;
    }
    _snprintf_s(g_arenaPlusLabels[labelRow], ARENA_PLUS_LABEL_CAP, _TRUNCATE,
        "%-18s %s", kArenaPlusComboNames[combo], status);
}

static void ArenaPlus_BuildRowsForKind(ArenaPlusMenuKind kind) {
    g_arenaPlusActiveRowCount = ArenaPlus_MenuRowCount(kind);

    if(kind==ArenaPlusMenuKind::Search) {
        char query[41]={};AcquireSRWLockShared(&g_arenaRenameLock);strcpy_s(query,g_arenaRenameDraft);ReleaseSRWLockShared(&g_arenaRenameLock);
        _snprintf_s(g_arenaPlusLabels[0],ARENA_PLUS_LABEL_CAP,_TRUNCATE,"Search: %.40s",query);
        strcpy_s(g_arenaPlusLabels[1],"Apply Search");strcpy_s(g_arenaPlusLabels[2],"Cancel");
        g_arenaPlusActiveRowCount=3;
        for(int row=0;row<3;++row)NativeMenu::EncodeLabel(g_arenaPlusLabels[row],g_arenaPlusLabelBytes[row],ARENA_PLUS_LABEL_CAP);
        return;
    }
    if(kind==ArenaPlusMenuKind::Battles) {
        g_arenaBattleCount=0;const auto& entries=FfxHooks::ArenaBattleProgram::Encounters();
        for(size_t i=0;i<entries.size() && g_arenaBattleCount<1024;++i) {
            const auto& entry=entries[i];const auto* arena=entry.scenery==0xffffu?nullptr:FfxHooks::ArenaScenery::Get(static_cast<FfxHooks::ArenaScenery::Choice>(entry.scenery));
            std::string words=entry.name.data();if(arena){words+=' ';words+=arena->label;}
            for(unsigned slot=0;slot<entry.count;++slot){words+=' ';words+=FfxHooks::ArenaMonsters::Name(entry.monsters[slot]);}
            if(!FfxHooks::ArenaBrowser::Matches(g_arenaBattleQuery,words.c_str(),entry.name.data()))continue;
            g_arenaBattleRows[g_arenaBattleCount]=i;const int row=++g_arenaBattleCount;
            _snprintf_s(g_arenaPlusLabels[row],ARENA_PLUS_LABEL_CAP,_TRUNCATE,"%s  %.30s",entry.name.data(),arena?arena->label:"Current arena");
        }
        _snprintf_s(g_arenaPlusLabels[0],ARENA_PLUS_LABEL_CAP,_TRUNCATE,"Search: %.30s  [%d]",g_arenaBattleQuery[0]?g_arenaBattleQuery:"All battles",g_arenaBattleCount);
        g_arenaPlusActiveRowCount=g_arenaBattleCount+2;strcpy_s(g_arenaPlusLabels[g_arenaPlusActiveRowCount-1],"Back to Ultra");
        for(int row=0;row<g_arenaPlusActiveRowCount;++row)NativeMenu::EncodeLabel(g_arenaPlusLabels[row],g_arenaPlusLabelBytes[row],ARENA_PLUS_LABEL_CAP);
        return;
    }
    if(kind==ArenaPlusMenuKind::BattleDetail) {
        const unsigned count=g_arenaBattleDetail?g_arenaBattleDetail->count:0;
        for(unsigned row=0;row<count;++row)_snprintf_s(g_arenaPlusLabels[row],ARENA_PLUS_LABEL_CAP,_TRUNCATE,"%u. %.45s",row+1,FfxHooks::ArenaMonsters::Name(g_arenaBattleDetail->monsters[row]));
        FfxHooks::CustomMixUltra::SelectionInput candidate{};
        const bool available=g_arenaBattleDetail&&FfxHooks::ArenaBattleProgram::UseEncounter(*g_arenaBattleDetail,g_arenaPlusUltraSelection,&candidate);
        strcpy_s(g_arenaPlusLabels[count],available?"Use This Lineup":"Lineup: Encounter Only");
        strcpy_s(g_arenaPlusLabels[count+1],g_arenaBattleDetail&&g_arenaBattleDetail->scenery!=0xffffu?"Use This Arena Only":"Arena: Keep Current");
        strcpy_s(g_arenaPlusLabels[count+2],"Back to Battles");g_arenaPlusActiveRowCount=static_cast<int>(count)+3;
        for(int row=0;row<g_arenaPlusActiveRowCount;++row)NativeMenu::EncodeLabel(g_arenaPlusLabels[row],g_arenaPlusLabelBytes[row],ARENA_PLUS_LABEL_CAP);
        return;
    }
    if(ArenaPlus_IsUltraChild(kind)) {
        const auto rules=ArenaPlus_MixRules();
        if(kind==ArenaPlusMenuKind::Monsters) {
            g_arenaMonsterCount=0;
            for(const auto& entry:FfxHooks::ArenaMonsters::kEntries) {
                if(!g_arenaMonsterSearchAll && entry.category!=g_arenaMonsterCategory)continue;
                if(!FfxHooks::ArenaBrowser::Matches(g_arenaMonsterQuery,entry.name,entry.key,FfxHooks::ArenaMonsters::kCategoryNames[static_cast<unsigned>(entry.category)]))continue;
                g_arenaMonsterRows[g_arenaMonsterCount]=&entry;const int row=++g_arenaMonsterCount;
                const bool allowed=FfxHooks::ArenaMix::ChoiceUnlocked(entry.choice,rules);
                unsigned selected=0;
                for(unsigned i=0;i<g_arenaPlusUltraSelection.activationCount;++i)
                    if(g_arenaPlusUltraSelection.activations[i]==entry.choice)++selected;
                const char* status=allowed?(entry.count==3?"+3":"+1"):
                    !entry.count?"Encounter only":(!rules.bypass&&!rules.defeatedMask)?"Mix locked":
                    entry.unlockIndex>=0?((rules.arenaKnownMask&(uint64_t{1}<<entry.unlockIndex))?"Arena locked":"Arena unreadable"):"Defeat first";
                char selectedText[16]={};if(selected)_snprintf_s(selectedText,sizeof(selectedText),_TRUNCATE," x%u",selected);
                char captured[20]={};
                if(entry.captureIndex>=0) {
                    uint8_t count=0;uint32_t state=0,error=0;
                    if(ArenaPlus_ReadByteRva(RVA_ARENA_CAPTURE_COUNTS+entry.captureIndex,&count,&state,&error))
                        _snprintf_s(captured,sizeof(captured),_TRUNCATE," C:%u",static_cast<unsigned>(count));
                }
                _snprintf_s(g_arenaPlusLabels[row],ARENA_PLUS_LABEL_CAP,_TRUNCATE,"%.35s%s  %s%s",entry.name,selectedText,status,captured);
            }
            _snprintf_s(g_arenaPlusLabels[0],ARENA_PLUS_LABEL_CAP,_TRUNCATE,"Search: %.30s  [%d]",g_arenaMonsterQuery[0]?g_arenaMonsterQuery:"Type a name",g_arenaMonsterCount);
            g_arenaPlusActiveRowCount=g_arenaMonsterCount+2;
        } else if(kind==ArenaPlusMenuKind::Soundtrack) {
            for(int row=0;row<g_arenaPlusActiveRowCount-1;++row) {
                const auto& track=FfxHooks::ArenaSoundtrack::kTracks[row];
                _snprintf_s(g_arenaPlusLabels[row],ARENA_PLUS_LABEL_CAP,_TRUNCATE,"%s%.45s",track.id==g_arenaPlusUltraSelection.musicTrack?"[Selected] ":"",track.name);
            }
        } else {
            for(unsigned row=0;row<g_arenaPlusUltraSelection.activationCount;++row) {
                const auto* entry=FfxHooks::ArenaMonsters::Get(g_arenaPlusUltraSelection.activations[row]);
                _snprintf_s(g_arenaPlusLabels[row],ARENA_PLUS_LABEL_CAP,_TRUNCATE,"%u. %.38s  Remove",row+1,entry?entry->name:"Invalid");
            }
        }
        strcpy_s(g_arenaPlusLabels[g_arenaPlusActiveRowCount-1],"Back to Ultra");
        for(int row=0;row<g_arenaPlusActiveRowCount;++row)
            NativeMenu::EncodeLabel(g_arenaPlusLabels[row],g_arenaPlusLabelBytes[row],ARENA_PLUS_LABEL_CAP);
        return;
    }

    if (kind == ArenaPlusMenuKind::Scenery) {
        const auto count=FfxHooks::ArenaScenery::Count(g_arenaPlusMixRequiredSlots);
        g_arenaSceneryCount=0;const int offset=g_arenaPlusMixRequiredSlots?0:1;
        for(unsigned index=0;index<count;++index){
            const auto choice=FfxHooks::ArenaScenery::At(g_arenaPlusMixRequiredSlots,index);const auto* info=FfxHooks::ArenaScenery::Get(choice);
            if(!g_arenaPlusMixRequiredSlots&&!FfxHooks::ArenaBrowser::Matches(g_arenaSceneryQuery,info->label,info->key,info->source))continue;
            g_arenaSceneryRows[g_arenaSceneryCount]=choice;
            _snprintf_s(g_arenaPlusLabels[g_arenaSceneryCount+offset],ARENA_PLUS_LABEL_CAP,_TRUNCATE,"%s%.51s",choice==g_arenaPlusUltraSelection.scenery?"* ":"",info->label);
            ++g_arenaSceneryCount;
        }
        if(offset)_snprintf_s(g_arenaPlusLabels[0],ARENA_PLUS_LABEL_CAP,_TRUNCATE,"Search: %.30s  [%d]",g_arenaSceneryQuery[0]?g_arenaSceneryQuery:"All arenas",g_arenaSceneryCount);
        g_arenaPlusActiveRowCount=g_arenaSceneryCount+offset+1;strcpy_s(g_arenaPlusLabels[g_arenaPlusActiveRowCount-1],"Back");
    } else if (kind == ArenaPlusMenuKind::Library || kind == ArenaPlusMenuKind::LibraryItem || kind == ArenaPlusMenuKind::Rename) {
        ArenaLibraryBuildRows(kind);
    } else if (kind == ArenaPlusMenuKind::Positions) {
        ArenaPlus_BuildPositionRows();
    } else if (kind == ArenaPlusMenuKind::Hub) {
        ArenaPlus_BuildHubRows();
    } else if (kind == ArenaPlusMenuKind::DarkRematch) {
        // WHY: only Dark Rematch renders disk-backed defeat state. Ultra and the
        // hub must never enter the legacy save reader merely by building rows.
        ArenaPlus_RefreshDiskSaveDarkCache();
        g_arenaPlusDarkMask = 0;
        const bool unlockAll = ArenaPlus_UnlockAllEnabled();
        const bool autoCarrier = ArenaPlus_AutoCarrierEnabled();
        for (int i = 0; i < ARENA_DARK_FLAG_LEN; ++i) {
            uint8_t v = 0;
            bool defeatedFlag = false;
            const bool ok = ArenaPlus_ReadDarkAeonDefeated(i, &v, &defeatedFlag);
            g_arenaPlusDarkValues[i] = defeatedFlag ? 1u : 0u;
            g_arenaPlusDarkReadOk[i] = ok;
            if (ok && defeatedFlag) g_arenaPlusDarkMask |= (1 << i);
            const bool route = ArenaPlus_BossRouteMapped(i);
            const bool defeated = ok && defeatedFlag;
            const bool allowed = route && (defeated || unlockAll);
            const char* status = "READ ERR";
            if (ok && !route) {
                status = defeated ? "DEFEATED NR" : "NO ROUTE";
            } else if (ok && defeated) {
                status = "DEFEATED";
            } else if (ok && unlockAll && route) {
                status = "READY LAB";
            } else if (ok) {
                status = "LOCKED";
            }
            char statusBuf[40] = {};
            const int gilCost = ArenaPlus_BossGilCost(i);
            if (allowed && gilCost > 0) {
                _snprintf_s(statusBuf, sizeof(statusBuf), _TRUNCATE, "COST %dG", gilCost);
                status = statusBuf;
            } else if (allowed && autoCarrier) {
                _snprintf_s(statusBuf, sizeof(statusBuf), _TRUNCATE, "%s AUTO", status);
                status = statusBuf;
            }
            _snprintf_s(g_arenaPlusLabels[i], ARENA_PLUS_LABEL_CAP, _TRUNCATE,
                "%-18s %s", kArenaPlusDarkNames[i], status);
        }
        _snprintf_s(g_arenaPlusLabels[ARENA_DARK_FLAG_LEN], ARENA_PLUS_LABEL_CAP, _TRUNCATE, "Back");
    } else if (kind == ArenaPlusMenuKind::AeonGauntlet) {
        for (int i = 0; i < ARENA_PLUS_PRESET_COMBO_COUNT; ++i)
            ArenaPlus_BuildComboRowLabel(i, i);
        _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_PRESET_COMBO_COUNT], ARENA_PLUS_LABEL_CAP, _TRUNCATE, "Back");
    } else if (kind == ArenaPlusMenuKind::CustomMix) {
        for (int i = 0; i < ARENA_PLUS_CUSTOM_MIX_COMBO_COUNT; ++i)
            ArenaPlus_BuildComboRowLabel(ArenaPlus_CustomMixComboIndex(i), i);
        strcpy_s(g_arenaPlusLabels[ARENA_PLUS_CUSTOM_MIX_COMBO_COUNT],"Saved Battles");
        _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_CUSTOM_MIX_COMBO_COUNT+1], ARENA_PLUS_LABEL_CAP, _TRUNCATE, "Back");
    }

    for (int i = 0; i < g_arenaPlusActiveRowCount; ++i) {
        NativeMenu::EncodeLabel(g_arenaPlusLabels[i], g_arenaPlusLabelBytes[i], ARENA_PLUS_LABEL_CAP);
    }
}

static void ArenaPlus_BuildRows() {
    ArenaPlus_BuildRowsForKind(g_arenaPlusMenuKind);
}

static void ArenaPlus_LogFlagsSummary() {
    ArenaPlus_RefreshDiskSaveDarkCache();
    int captureNonZero = 0;
    int captureSum = 0;
    for (int i = 0; i < ARENA_CAPTURE_COUNT_LEN; ++i) {
        uint8_t v = 0;
        if (!ArenaPlus_ReadFlag("captureCounts", RVA_ARENA_CAPTURE_COUNTS + static_cast<uint32_t>(i), &v)) return;
        if (v != 0) ++captureNonZero;
        captureSum += v;
    }

    int unlockNonZero = 0;
    for (int i = 0; i < ARENA_UNLOCK_FLAG_LEN; ++i) {
        uint8_t v = 0;
        if (!ArenaPlus_ReadFlag("unlockFlags", RVA_ARENA_UNLOCK_FLAGS + static_cast<uint32_t>(i), &v)) return;
        if (v != 0) ++unlockNonZero;
    }

    int darkMask = 0;
    uint8_t darkValues[ARENA_DARK_FLAG_LEN] = {};
    for (int i = 0; i < ARENA_DARK_FLAG_LEN; ++i) {
        uint8_t v = 0;
        bool defeatedFlag = false;
        if (!ArenaPlus_ReadDarkAeonDefeated(i, &v, &defeatedFlag)) return;
        darkValues[i] = v;
        if (defeatedFlag) darkMask |= (1 << i);
    }

    Log("[ffx-hooks] ArenaPlus: flags captureNonZero=%d captureSum=%d unlockNonZero=%d darkMask=0x%03X\n",
        captureNonZero, captureSum, unlockNonZero, darkMask);
    for (int i = 0; i < ARENA_DARK_FLAG_LEN; ++i) {
        uint8_t ffxedByte = 0;
        uint8_t runtimeByte = 0;
        uint8_t diskByte = 0;
        bool defeatedFlag = false;
        if (!ArenaPlus_ReadDarkAeonDefeated(i, &runtimeByte, &defeatedFlag)) return;
        const uint32_t ffxedRva = kArenaPlusDarkFlagSpecs[i].byteRva;
        ArenaPlus_ReadFlag("darkAeonFfxedLog", ffxedRva, &ffxedByte);
        ArenaPlus_ReadFlag("darkAeonRuntimeLog", RVA_DARK_AEON_RUNTIME_BASE + static_cast<uint32_t>(i), &runtimeByte);
        if (g_arenaPlusDiskDark.valid) {
            diskByte = (i < 8) ? g_arenaPlusDiskDark.ffxedBytes[i] : g_arenaPlusDiskDark.penanceByte;
        }
        Log("[ffx-hooks] ArenaPlus: dark[%d] %-18s ramFfxed=0x%02X ramRt=0x%02X disk=0x%02X defeated=%u\n",
            i, kArenaPlusDarkNames[i],
            static_cast<unsigned>(ffxedByte),
            static_cast<unsigned>(runtimeByte),
            static_cast<unsigned>(diskByte),
            defeatedFlag ? 1u : 0u);
    }
}

struct ArenaPlusGlassTone {
    unsigned int top;
    unsigned int bottom;
    unsigned int accent;
};

// Roles keep the translucent material while separating setup, roster and actions.
// Labels, the native cursor and a solid selection rail carry meaning without color.
static const ArenaPlusGlassTone kArenaGlassSetup = {0x68183A50u, 0x38102438u, 0xFF8EDAF8u};
static const ArenaPlusGlassTone kArenaGlassRoster = {0x6834284Bu, 0x38221A35u, 0xFFC6ABF4u};
static const ArenaPlusGlassTone kArenaGlassLayout = {0x68133B3Au, 0x3810292Cu, 0xFF85DAD0u};
static const ArenaPlusGlassTone kArenaGlassLibrary = {0x683E3420u, 0x382C251Au, 0xFFE5CB8Fu};
static const ArenaPlusGlassTone kArenaGlassLaunch = {0x68193D29u, 0x38112C21u, 0xFF95E4ADu};
static const ArenaPlusGlassTone kArenaGlassRemove = {0x68432B30u, 0x382C1B25u, 0xFFF0B5BDu};
static const ArenaPlusGlassTone kArenaGlassBack = {0x682C3442u, 0x381B222Eu, 0xFFB5C4D8u};
static const ArenaPlusGlassTone kArenaCategoryGlass[] = {
    {0x68163835u,0x3813292Bu,0xFF85DAD0u}, // Fiends.
    {0x68382C20u,0x382E2118u,0xFFDDB999u}, // Story bosses.
    {0x68413820u,0x382B2419u,0xFFE5CB8Fu}, // Arena creations.
    {0x681B3243u,0x38172536u,0xFF8EDAF8u}, // Aeons.
    {0x68322849u,0x38221B35u,0xFFC6ABF4u}, // Dark Aeons.
    {0x682B323Fu,0x381C232Cu,0xFFB5C4D8u}, // Special actors.
};
static bool g_arenaPlusSelectionPulseAllowed = false;

static ArenaPlusGlassTone ArenaPlus_RowGlassTone(int row) {
    if (row == ArenaPlus_SubMenuBackRow(g_arenaPlusMenuKind) ||
        (g_arenaPlusMenuKind == ArenaPlusMenuKind::Hub && row == ARENA_PLUS_HUB_ROW_BACK))
        return kArenaGlassBack;
    switch (g_arenaPlusMenuKind) {
    case ArenaPlusMenuKind::Ultra:
        if(!g_arenaPlusMixRequiredSlots){
            if(row==2)return kArenaGlassSetup;if(row==9)return kArenaGlassLibrary;
            if(row>=3&&row<=8)return kArenaCategoryGlass[row-3];
            if(row>=10)row-=2;
        }
        if (row <= ARENA_PLUS_ULTRA_ROW_CAMERA) return kArenaGlassSetup;
        if (row < ARENA_PLUS_ULTRA_ROW_AUTO) return kArenaGlassRoster;
        if (row <= ARENA_PLUS_ULTRA_ROW_NATIVE) return kArenaGlassLayout;
        if (row <= ARENA_PLUS_ULTRA_ROW_CLEAR) return kArenaGlassRemove;
        if (row <= ARENA_PLUS_ULTRA_ROW_LIBRARY) return kArenaGlassLibrary;
        return kArenaGlassLaunch;
    case ArenaPlusMenuKind::Monsters:
        if(row>0 && row<=g_arenaMonsterCount)
            return kArenaCategoryGlass[static_cast<unsigned>(g_arenaMonsterRows[row-1]->category)];
        return kArenaCategoryGlass[static_cast<unsigned>(g_arenaMonsterCategory)];
    case ArenaPlusMenuKind::Formation: return kArenaGlassRoster;
    case ArenaPlusMenuKind::Battles:
    case ArenaPlusMenuKind::BattleDetail:
    case ArenaPlusMenuKind::Soundtrack: return kArenaGlassLibrary;
    case ArenaPlusMenuKind::Search: return kArenaGlassSetup;
    case ArenaPlusMenuKind::Scenery: return kArenaGlassSetup;
    case ArenaPlusMenuKind::Positions: return kArenaGlassLayout;
    case ArenaPlusMenuKind::Library:
    case ArenaPlusMenuKind::LibraryItem:
    case ArenaPlusMenuKind::Rename: return kArenaGlassLibrary;
    case ArenaPlusMenuKind::CustomMix:
        return row == ARENA_PLUS_CUSTOM_MIX_COMBO_COUNT ? kArenaGlassLibrary : kArenaGlassRoster;
    case ArenaPlusMenuKind::Hub:
        if (row == ARENA_PLUS_HUB_ROW_SAFE) return kArenaGlassLaunch;
        if (row == ARENA_PLUS_HUB_ROW_GAUNTLET) return kArenaGlassLibrary;
        if (row == ARENA_PLUS_HUB_ROW_ULTRA) return kArenaGlassSetup;
        return kArenaGlassRoster;
    default: return kArenaGlassRoster;
    }
}

static void ArenaPlus_DrawFormationPane(int frame) {
    using namespace NativeMenu;
    const float x=NX(0.655f),y=NY(0.215f),w=NW(0.298f),h=NH(0.625f);
    DrawMenuGlassPanel(x,y,w,h,frame,0);
    auto text=[&](const char* value,float top){unsigned char encoded[64]={};EncodeLabel(value,encoded,64);DrawStringSub(encoded,x+NW(0.017f),NY(top));};
    text("YOUR FORMATION",0.237f);
    const auto roster=FfxHooks::CustomMixUltra::BuildSelection(g_arenaPlusUltraSelection);
    char line[64]={};_snprintf_s(line,sizeof(line),_TRUNCATE,"%u / 8 slots",roster.expanded.monsterCount);text(line,0.267f);
    for(unsigned i=0;i<8;++i){
        if(i<roster.expanded.monsterCount)_snprintf_s(line,sizeof(line),_TRUNCATE,"%u  %.23s",i+1,ArenaPlus_UltraPreviewSlotName(roster.expanded.monsterIds[i]));
        else _snprintf_s(line,sizeof(line),_TRUNCATE,"%u  --",i+1);
        text(line,0.301f+static_cast<float>(i)*0.037f);
    }
    text("POSITION OVERVIEW",0.587f);
    const float gx=x+NW(0.017f),gy=NY(0.614f),gw=w-NW(0.034f),gh=NH(0.103f);
    DrawSolidRect(gx,gy,gw,gh,0x30102028u,0x20101820u);
    std::array<FfxHooks::ArenaPositions::Point,8> monsters{};
    std::array<FfxHooks::ArenaPositions::Point,7> party{};
    const unsigned count=FfxHooks::ArenaBattleProgram::MonsterPreview(g_arenaPlusUltraSelection,&monsters);
    const unsigned partyCount=FfxHooks::ArenaBattleProgram::PartyPreview(g_arenaPlusUltraSelection.scenery,g_arenaPlusUltraSelection.camera,&party);
    float loX=-180,hiX=180,loZ=-90,hiZ=220;
    const auto bounds=[&](FfxHooks::ArenaPositions::Point point){if(point.x<loX)loX=point.x-16;if(point.x>hiX)hiX=point.x+16;if(point.z<loZ)loZ=point.z-16;if(point.z>hiZ)hiZ=point.z+16;};
    for(unsigned i=0;i<count;++i)bounds(monsters[i]);
    for(unsigned i=0;i<partyCount;++i)bounds(party[i]);
    const auto mark=[&](FfxHooks::ArenaPositions::Point point,unsigned color){
        const float px=gx+gw*(point.x-loX)/(hiX-loX),py=gy+gh*(1.0f-(point.z-loZ)/(hiZ-loZ));
        DrawSolidRect(px-NW(0.002f),py-NH(0.003f),NW(0.004f),NH(0.006f),color,color);
    };
    for(unsigned i=0;i<count;++i)mark(monsters[i],0x80685078u);
    for(unsigned i=0;i<partyCount;++i)mark(party[i],0x80307860u);
    const auto* arena=FfxHooks::ArenaScenery::Get(g_arenaPlusUltraSelection.scenery);
    const auto* music=FfxHooks::ArenaSoundtrack::Get(g_arenaPlusUltraSelection.musicTrack);
    _snprintf_s(line,sizeof(line),_TRUNCATE,"Arena: %.22s",arena?arena->label:"Unknown");text(line,0.748f);
    _snprintf_s(line,sizeof(line),_TRUNCATE,"Music: %.22s",music?music->name:"Unknown");text(line,0.787f);
}

static int __cdecl ArenaPlus_Draw(int obj) {
    using namespace NativeMenu;
    ++g_arenaPlusDrawCalls;
    const int F = g_arenaPlusDrawCalls;

    static unsigned char s_title[64], s_sub[64], s_foot[64];
    static bool s_enc = false;
    if (!s_enc) {
        EncodeLabel("Mouse/Confirm Select   Cancel Back   F7 Close", s_foot, 64);
        s_enc = true;
    }

    const char* titleText = "Arena+";
    const char* subText = "Choose a battle or build your own";
    switch (g_arenaPlusMenuKind) {
    case ArenaPlusMenuKind::DarkRematch:
        titleText = "Dark Aeon Rematch";
        subText = "Solo Dark Aeons + Penance - flags + Gil";
        break;
    case ArenaPlusMenuKind::AeonGauntlet:
        titleText = "Aeon Gauntlet";
        subText = "Preset multi-boss challenges";
        break;
    case ArenaPlusMenuKind::CustomMix:
        titleText = "Custom Mix";
        subText = "Pick bosses - compose - fight";
        break;
    case ArenaPlusMenuKind::Library:
        titleText="Saved Battles";
        subText=g_arenaLibraryStatus[0]?g_arenaLibraryStatus:"modules/config/arena-mixes + arena_formations";
        break;
    case ArenaPlusMenuKind::LibraryItem:
        titleText=g_arenaLibraryEntry.preset.name.c_str();
        subText=g_arenaLibraryStatus[0]?g_arenaLibraryStatus:g_arenaLibraryEntry.preset.legacy?
            "Legacy scenery becomes the current arena":"Load a draft, export or rename";
        break;
    case ArenaPlusMenuKind::Rename:
        titleText="Rename Battle";
        subText=g_arenaLibraryStatus[0]?g_arenaLibraryStatus:"Type name; Enter saves; Esc cancels";
        break;
    case ArenaPlusMenuKind::Positions:
        titleText = "Monster Positions";
        subText = "Left/Right adjusts; Shift is fine; Apply for next launch";
        break;
    case ArenaPlusMenuKind::Monsters:
        titleText=g_arenaMonsterSearchAll?"Search All Monsters":FfxHooks::ArenaMonsters::kCategoryNames[static_cast<unsigned>(g_arenaMonsterCategory)];
        subText="Add monsters; Back keeps your formation. C = captures";
        break;
    case ArenaPlusMenuKind::Search:
        titleText="Search";subText="Type name; Enter applies; Esc cancels; Ctrl+A selects all";break;
    case ArenaPlusMenuKind::Battles:
        titleText="Battle Presets";subText="Search by arena, battle ID or monster; choose a lineup";break;
    case ArenaPlusMenuKind::BattleDetail:
        titleText=g_arenaBattleDetail?g_arenaBattleDetail->name.data():"Battle Preset";
        subText="Use the lineup in your mix, or choose only its arena";break;
    case ArenaPlusMenuKind::Soundtrack:
        titleText="Battle Soundtrack";
        subText=ArenaPlus_MusicEnabled()?"Choose the next battle soundtrack":"Enable Arena+ Music in F8, then restart";
        break;
    case ArenaPlusMenuKind::Formation:
        titleText="Your Formation";
        subText="Select an entry to remove it; Magus stays a group";
        break;
    case ArenaPlusMenuKind::Scenery:
        titleText = "Battle Arena";
        subText = "Choose scenery; your boss formation is preserved";
        break;
    case ArenaPlusMenuKind::Ultra:
        titleText = g_arenaPlusMixRequiredSlots == 3 ? "Custom Mix x3" :
            g_arenaPlusMixRequiredSlots == 4 ? "Custom Mix x4" :
            g_arenaPlusMixRequiredSlots == 5 ? "Custom Mix x5" : "Custom Mix Ultra";
        if(g_arenaMixName!="Custom Mix"&&g_arenaMixName!="Custom Mix Ultra") titleText=g_arenaMixName.c_str();
        ArenaPlus_BuildUltraPreview();
        subText = g_arenaPlusUltraPreview;
        break;
    default:
        break;
    }
    EncodeLabel(titleText, s_title, 64);
    EncodeLabel(subText, s_sub, 64);

    DrawMenuBackdrop();
    DrawMenuNeonFrame(F);

    const float hx = NX(0.047f), hy = NY(0.054f), hw = NW(0.906f), hh = NH(0.126f);
    DrawMenuGlassPanel(hx, hy, hw, hh, F, 0);
    DrawString(s_title, NX(0.071f), NY(0.081f));
    DrawString(s_sub, NX(0.071f), NY(0.137f));

    const int top = RdW(obj, O_TOP);
    const int page = RdW(obj, O_PAGE);
    const int count = RdW(obj, O_COUNT);
    const int sel = RdW(obj, O_SELECTED);
    const float vLeft = NX(ArenaPlus_ListLeft());
    const float vTop = NY(0.215f), vWidth = NW(ArenaPlus_ListWidth());
    const float vStep = NH(ArenaPlus_RowStep()), vBarH = NH(ArenaPlus_RowHeight());
    const float selLine = MenuBorderPx() * 0.45f;
    const float cursorOff = NW(0.020f);

    // Match the F7 hub: the glass lift itself pulses; the cursor and green edge
    // remain visible when reduced motion is requested. No opaque accent rails.
    const float pulse = g_arenaPlusSelectionPulseAllowed ? Osc01(F,44) : 0.0f;
    for (int r = 0; r < page; ++r) {
        const int row = top + r;
        if (row >= count || row >= g_arenaPlusActiveRowCount) break;
        const ArenaPlusGlassTone tone = ArenaPlus_RowGlassTone(row);
        unsigned int c0 = tone.top;
        unsigned int c1 = tone.bottom;

        const float vy = vTop + (float)r * vStep;
        DrawSolidRect(vLeft, vy, vWidth, vBarH, c0, c1);
        const float glassEdge = MenuBorderPx() * 0.22f;
        DrawSolidRect(vLeft, vy, vWidth, glassEdge, kMenuGlassSheen, 0x10FFFFFFu);
        const unsigned int edge = (tone.accent & 0x00FFFFFFu) | 0x68000000u;
        DrawSolidRect(vLeft, vy + vBarH - glassEdge, vWidth, glassEdge, edge, edge);
        if (row == sel) {
            const unsigned int alpha=0x44u+static_cast<unsigned int>(pulse*32.0f);
            DrawSolidRect(vLeft,vy,vWidth,vBarH,(alpha<<24u)|0x00305068u,(alpha<<24u)|0x00182038u);
            DrawSolidRect(vLeft,vy+vBarH-selLine,vWidth,selLine,kMenuNeonGreenLine,kMenuNeonGreenLineLo);
            DrawCursor(vLeft-cursorOff,vy+NH(0.001f));
        }
        const bool hubDescRow = (g_arenaPlusMenuKind == ArenaPlusMenuKind::Hub &&
            row >= ARENA_PLUS_HUB_ROW_SAFE && row <= ARENA_PLUS_HUB_ROW_ULTRA);
        DrawString(g_arenaPlusLabelBytes[row], vLeft + NW(0.015f), vy + NH(0.016f));
        if (hubDescRow && g_arenaPlusHubDescBytes[row][0] != 0) {
            DrawStringSub(g_arenaPlusHubDescBytes[row], vLeft + NW(0.015f), vy + NH(0.058f));
        }
    }

    if (count > page && g_arenaPlusMenuKind != ArenaPlusMenuKind::Positions) {
        char range[64] = {};
        const int end = top + page < count ? top + page : count;
        _snprintf_s(range, sizeof(range), _TRUNCATE, "%d-%d of %d%s",
            top + 1, end, count, end < count ? "   More below" : "");
        unsigned char encoded[64] = {};
        EncodeLabel(range, encoded, 64);
        DrawStringSub(encoded, vLeft, NY(0.837f));
    }
    if (g_arenaPlusMenuKind == ArenaPlusMenuKind::Positions) ArenaPlus_DrawPositionPreview();
    if(ArenaPlus_ShowFormationPane())ArenaPlus_DrawFormationPane(F);

    const float fx = NX(0.047f), fy = NY(0.887f), fw = NW(0.906f), fh = NH(0.070f);
    DrawMenuGlassPanel(fx, fy, fw, fh, F, 1);
    (void)s_foot;
    {
        float hintX = NX(0.071f); const float hintY = fy + NH(0.018f);
        hintX = DrawInputHint(hintX, hintY, PC_PAD_UP, PC_PAD_DOWN, PC_KB_UP, PC_KB_DOWN, "Navigate", 0xFFFFFFFFu);
        hintX = DrawInputHint(hintX, hintY, PC_PAD_FACE_D, PC_SKIP, PC_KB_ENTER, PC_SKIP, "Select", 0xFFFFFFFFu);
        hintX = DrawInputHint(hintX, hintY, PC_PAD_FACE_R, PC_SKIP, PC_KB_BACKSPACE, PC_SKIP, "Back", 0xFFFFFFFFu);
        DrawInputHint(hintX, hintY, PC_SKIP, PC_SKIP, PC_KB_F7, PC_SKIP, "Close", 0xFFFFFFFFu);
    }
    return obj;
}

static int __cdecl ArenaPlus_InputCb(int obj) {
    if (!F7IsForegroundWindow()) {
        F7RequestClose(FfxHooks::F7Ui::CloseSource::FocusLost);
        return obj;
    }
    const F7MouseInputResult mouse = F7ListMouseTick(
        obj, NativeMenu::NX(ArenaPlus_ListLeft()), NativeMenu::NY(0.215f),
        NativeMenu::NW(ArenaPlus_ListWidth()), NativeMenu::NH(ArenaPlus_RowStep()), NativeMenu::NH(ArenaPlus_RowHeight()),
        NativeMenu::RdW(obj, NativeMenu::O_COUNT), NativeMenu::RdW(obj, NativeMenu::O_PAGE));
    if(g_arenaPlusMenuKind==ArenaPlusMenuKind::Rename || g_arenaPlusMenuKind==ArenaPlusMenuKind::Search){
        if(g_arenaPlusMenuKind==ArenaPlusMenuKind::Search)ArenaPlus_BuildRowsForKind(ArenaPlusMenuKind::Search);
        else ArenaLibraryBuildRows(ArenaPlusMenuKind::Rename);
        const int clicked=NativeMenu::RdW(obj,NativeMenu::O_SELECTED);
        if(InterlockedExchange(&g_arenaRenameConfirm,0)){g_arenaPlusResult=1;g_arenaPlusClosed=1;}
        else if(InterlockedExchange(&g_arenaRenameCancel,0)){g_arenaPlusResult=2;g_arenaPlusClosed=1;}
        else if(mouse.confirm&&(clicked==1||clicked==2)){g_arenaPlusResult=clicked;g_arenaPlusClosed=1;}
        return obj;
    }
    int dir = NativeMenu::PadDir();
    dir = FfxHooks::F7Ui::ResolveDirectionalInput(dir, mouse.ownsDirectionalFrame);
    const int edge = NativeMenu::PadEdge();
    const int confirmEdge = edge & 0x20;
    const int cancelEdge = edge & 0x40;
    const bool confirmPressed =
        (confirmEdge && !(g_arenaPlusLastConfirmEdge & 0x20)) || mouse.confirm;
    const bool cancelPressed = cancelEdge && !(g_arenaPlusLastConfirmEdge & 0x40);
    g_arenaPlusLastConfirmEdge = edge & 0x60;
    int sel = NativeMenu::RdW(obj, NativeMenu::O_SELECTED);
    const int count = NativeMenu::RdW(obj, NativeMenu::O_COUNT);
    int top = NativeMenu::RdW(obj, NativeMenu::O_TOP);
    const int page = NativeMenu::RdW(obj, NativeMenu::O_PAGE);
    if (count <= 0) {
        if (g_arenaPlusInputCooldown > 0) --g_arenaPlusInputCooldown;
        return obj;
    }

    if (dir & 0x1000) {
        sel = (sel > 0) ? (sel - 1) : (count - 1);
        NativeMenu::PlaySfx(1);
    } else if (dir & 0x4000) {
        sel = (sel < count - 1) ? (sel + 1) : 0;
        NativeMenu::PlaySfx(1);
    }

    if (sel < 0) sel = 0;
    if (sel > count - 1) sel = count - 1;
    if (sel < top) top = sel;
    if (sel >= top + page) top = sel - page + 1;
    if (top > count - page) top = count - page;
    if (top < 0) top = 0;
    NativeMenu::WrW(obj, NativeMenu::O_SELECTED, static_cast<int16_t>(sel));
    NativeMenu::WrW(obj, NativeMenu::O_TOP, static_cast<int16_t>(top));

    if (g_arenaPlusMenuKind == ArenaPlusMenuKind::Positions && (dir & (0x8000 | 0x2000))) {
        g_arenaPositionRow = sel;
        ArenaPlus_AdjustPosition(sel, (dir & 0x8000) ? -1 : 1);
    }
    const bool confirmBlocked = g_arenaPlusInputCooldown > 0;
    if (!g_arenaPlusClosed) {
        if (confirmPressed) {
            if (!confirmBlocked) {
                // Ultra validates symbolic capacity and launch readiness in its
                // handler, so only that handler may choose success vs failure SFX.
                const bool ultraHubRow =
                    g_arenaPlusMenuKind == ArenaPlusMenuKind::Hub &&
                    sel == ARENA_PLUS_HUB_ROW_ULTRA;
                if (g_arenaPlusMenuKind != ArenaPlusMenuKind::Ultra && !ultraHubRow) {
                    NativeMenu::PlaySfx(1);
                }
                g_arenaPlusResult = sel;
                g_arenaPlusClosed = 1;
            }
        } else if (cancelPressed) {
            if (!confirmBlocked) {
                NativeMenu::PlaySfx(4);
                g_arenaPlusResult = -1;
                g_arenaPlusClosed = 1;
            }
        }
    }

    if (g_arenaPlusInputCooldown > 0) --g_arenaPlusInputCooldown;
    return obj;
}

static NativeMenu::Poll ArenaPlus_PollMenu(const NativeMenu::Menu& m) {
    const int sel = NativeMenu::RdW(m.obj, NativeMenu::O_SELECTED);
    if (g_arenaPlusClosed) {
        if (g_arenaPlusResult >= 0) return NativeMenu::Poll{ NativeMenu::POLL_CONFIRM, g_arenaPlusResult };
        return NativeMenu::Poll{ NativeMenu::POLL_CANCEL, sel };
    }
    return NativeMenu::Poll{ NativeMenu::POLL_NAV, sel };
}

static void ArenaPlus_CloseMenu(NativeMenu::Menu& m) {
    if (!m.obj) return;
    NativeMenu::WrB(m.obj, 65, 1);
    m.obj = 0;
    g_arenaPlusClosed = 0;
    g_arenaPlusResult = 0;
    g_arenaPlusInputCooldown = 0;
    g_arenaPlusLastConfirmEdge = 0;
}

static NativeMenu::Menu ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind kind);

static bool ArenaPlus_LaunchSafeBattleFromPump();
static bool ArenaPlus_LaunchBossBattleFromPump(int dark);
static bool ArenaPlus_LaunchComboBattleFromPump(int combo);
static FfxHooks::CustomMixUltra::Runtime::EditorLaunchOutcome
    ArenaPlus_Ultra_LaunchFromPump();

static NativeMenu::Menu ArenaPlus_SpawnPreparedMenu(ArenaPlusMenuKind kind) {
    g_arenaPlusMenuKind = kind;
    BOOL clientAreaAnimation = FALSE;
    g_arenaPlusSelectionPulseAllowed =
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &clientAreaAnimation, 0) &&
        clientAreaAnimation && !EnvFlagEnabled("FFXHOOKS_ARENAPLUS_REDUCED_MOTION");
    int obj = NativeMenu::Alloc();
    if (!obj) return NativeMenu::Menu{ 0 };
    F7SeedPointerForDestination();

    NativeMenu::WrW(obj, NativeMenu::O_COUNT,    static_cast<int16_t>(g_arenaPlusActiveRowCount));
    NativeMenu::WrW(obj, NativeMenu::O_PAGE,     static_cast<int16_t>(ARENA_PLUS_VISIBLE_PAGE));
    NativeMenu::WrW(obj, NativeMenu::O_TOP,      0);
    NativeMenu::WrW(obj, NativeMenu::O_SELECTED, 0);
    NativeMenu::WrB(obj, NativeMenu::O_SLOTS,    1);
    NativeMenu::WrB(obj, NativeMenu::O_CANCEL,   1);
    NativeMenu::WrB(obj, NativeMenu::O_GROUP62,  2);
    NativeMenu::WrB(obj, NativeMenu::O_GROUP63,  1);
    NativeMenu::WrP(obj, NativeMenu::O_ENTER,     (void*)0);
    NativeMenu::WrP(obj, NativeMenu::O_UPDATE,    (void*)(uintptr_t)&ArenaPlus_InputCb);
    NativeMenu::WrP(obj, NativeMenu::O_DRAW,      (void*)(uintptr_t)&ArenaPlus_Draw);
    NativeMenu::WrP(obj, NativeMenu::O_AUX,       (void*)(uintptr_t)&NativeMenu::OurAux);
    NativeMenu::WrP(obj, NativeMenu::O_VALIDATOR, (void*)0);

    NativeMenu::g_ourClosed = 0;
    NativeMenu::g_ourResult = 0;
    g_arenaPlusClosed = 0;
    g_arenaPlusResult = 0;
    g_arenaPlusInputCooldown = EnvInt("FFXHOOKS_ARENAPLUS_INPUT_COOLDOWN", 10);
    if (g_arenaPlusInputCooldown < 0) g_arenaPlusInputCooldown = 0;
    if (g_arenaPlusInputCooldown > 60) g_arenaPlusInputCooldown = 60;
    g_arenaPlusLastConfirmEdge = NativeMenu::PadEdge() & 0x60;
    g_arenaPlusDrawCalls = 0;
    if(kind==ArenaPlusMenuKind::Monsters) {
        if(g_arenaMonsterSelectedRow<0||g_arenaMonsterSelectedRow>=g_arenaPlusActiveRowCount)g_arenaMonsterSelectedRow=0;
        NativeMenu::WrW(obj,NativeMenu::O_SELECTED,static_cast<int16_t>(g_arenaMonsterSelectedRow));
        NativeMenu::WrW(obj,NativeMenu::O_TOP,static_cast<int16_t>(g_arenaMonsterSelectedRow>=ARENA_PLUS_VISIBLE_PAGE?g_arenaMonsterSelectedRow-ARENA_PLUS_VISIBLE_PAGE+1:0));
    }
    if (kind == ArenaPlusMenuKind::Positions)
        NativeMenu::WrW(obj, NativeMenu::O_SELECTED, static_cast<int16_t>(g_arenaPositionRow));
    if (kind == ArenaPlusMenuKind::Library) {
        NativeMenu::WrW(obj,NativeMenu::O_SELECTED,static_cast<int16_t>(g_arenaLibraryRow));
        NativeMenu::WrW(obj,NativeMenu::O_TOP,static_cast<int16_t>(g_arenaLibraryRow>=8?g_arenaLibraryRow-7:0));
    }
    NativeMenu::Register(obj);
    return NativeMenu::Menu{ obj };
}

static NativeMenu::Menu ArenaPlus_SpawnUltraMenu() {
    // WHY: the Ultra editor is a RAM-only surface. Its dedicated builder and
    // prepared allocation path cannot reach Dark-save or Compose-pick readers.
    ArenaPlus_BuildUltraRows();
    NativeMenu::Menu menu = ArenaPlus_SpawnPreparedMenu(ArenaPlusMenuKind::Ultra);
    if (menu.obj) {
        NativeMenu::WrW(menu.obj, NativeMenu::O_SELECTED, static_cast<int16_t>(g_arenaPlusUltraSelectedRow));
        const int top = g_arenaPlusUltraSelectedRow >= ARENA_PLUS_VISIBLE_PAGE
            ? g_arenaPlusUltraSelectedRow - ARENA_PLUS_VISIBLE_PAGE + 1 : 0;
        NativeMenu::WrW(menu.obj, NativeMenu::O_TOP, static_cast<int16_t>(top));
    }
    return menu;
}

static NativeMenu::Menu ArenaPlus_SpawnHubMenu() {
    ArenaPlus_BuildHubRows();
    return ArenaPlus_SpawnPreparedMenu(ArenaPlusMenuKind::Hub);
}

static NativeMenu::Menu ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind kind) {
    if (kind == ArenaPlusMenuKind::Ultra) return ArenaPlus_SpawnUltraMenu();
    if (kind == ArenaPlusMenuKind::Hub) return ArenaPlus_SpawnHubMenu();
    ArenaPlus_BuildRowsForKind(kind);
    return ArenaPlus_SpawnPreparedMenu(kind);
}

static NativeMenu::Menu ArenaPlus_SpawnMenu() {
    return ArenaPlus_SpawnHubMenu();
}

static NativeMenu::Menu ArenaPlus_ReopenMenu() {
    return ArenaPlus_SpawnMenuKind(g_arenaPlusMenuKind);
}

static void ArenaPlus_Ultra_Reopen() {
    g_arenaPlusMenu = ArenaPlus_SpawnUltraMenu();
    if (!g_arenaPlusMenu.obj) InterlockedExchange(&g_forceSubsystem, 0);
}

static void ArenaPlus_OpenSearch(ArenaPlusMenuKind parent) {
    g_arenaSearchParent=parent;
    AcquireSRWLockExclusive(&g_arenaRenameLock);strcpy_s(g_arenaRenameDraft,ArenaPlus_SearchQuery(parent));g_arenaRenameSelectAll=true;ReleaseSRWLockExclusive(&g_arenaRenameLock);
    ArenaMixRenameAbort();InterlockedExchange(&g_arenaRenameActive,1);
    g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::Search);
}
static void ArenaPlus_FinishSearch(bool apply) {
    if(apply){AcquireSRWLockShared(&g_arenaRenameLock);strcpy_s(ArenaPlus_SearchQuery(g_arenaSearchParent),41,g_arenaRenameDraft);ReleaseSRWLockShared(&g_arenaRenameLock);}
    ArenaMixRenameAbort();g_arenaMonsterSelectedRow=0;
    g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(g_arenaSearchParent);
}

static void ArenaPlus_Ultra_CloseAfterLaunch() {
    g_nativeHeldAction = -1;
    InterlockedExchange(&g_forceSubsystem, 0);
    NativeMenuForceGateClear();
}

static void ArenaLibraryOpen(ArenaPlusMenuKind parent) {
    g_arenaLibraryParent=parent;g_arenaLibraryStatus[0]=0;ArenaLibraryRefresh();
    g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::Library);
}
static bool ArenaLibraryExport(const FfxHooks::ArenaMixLibrary::Preset& preset) {
    std::string root,legacy,id,error;
    auto named=preset;SYSTEMTIME now{};GetSystemTime(&now);char title[41]={};
    _snprintf_s(title,sizeof(title),_TRUNCATE,"%.31s %02u:%02u:%02u",preset.name.c_str(),
        static_cast<unsigned>(now.wHour),static_cast<unsigned>(now.wMinute),static_cast<unsigned>(now.wSecond));
    named.name=title;
    if(!ArenaLibraryPaths(&root,&legacy)||!FfxHooks::ArenaMixLibrary::Save(root,named,&id,&error)){
        ArenaLibraryStatus(error.empty()?"Cannot resolve export folder":error);NativeMenu::PlaySfx(3);return false;
    }
    if(g_arenaPlusMenuKind==ArenaPlusMenuKind::Ultra){g_arenaMixName=named.name;g_arenaMixLoadedId=id;}
    ArenaLibraryRefresh();for(size_t i=0;i<g_arenaLibraryEntries.size();++i)if(g_arenaLibraryEntries[i].id==id)g_arenaLibraryRow=static_cast<int>(i);
    ArenaLibraryStatus("Exported JSON + native editor battle");
    g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::Library);NativeMenu::PlaySfx(4);return true;
}
static void ArenaLibraryHandle(int row) {
    using namespace FfxHooks::ArenaMixLibrary;
    if(g_arenaPlusMenuKind==ArenaPlusMenuKind::Rename){
        if(row==1){char name[41]={};AcquireSRWLockShared(&g_arenaRenameLock);strcpy_s(name,g_arenaRenameDraft);ReleaseSRWLockShared(&g_arenaRenameLock);
            std::string value=name;while(!value.empty()&&value.front()==' ')value.erase(value.begin());while(!value.empty()&&value.back()==' ')value.pop_back();
            std::string root,legacy,error;
            if(!ArenaLibraryPaths(&root,&legacy)||!Rename(root,g_arenaLibraryEntry.id,value,&error)){
                ArenaLibraryStatus(error);NativeMenu::PlaySfx(3);g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::Rename);return;}
            if(g_arenaMixLoadedId==g_arenaLibraryEntry.id)g_arenaMixName=value;
            g_arenaLibraryEntry.preset.name=value;ArenaLibraryStatus("Battle renamed; file identifier unchanged");ArenaLibraryRefresh();
        }
        ArenaMixRenameAbort();g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::LibraryItem);return;
    }
    if(g_arenaPlusMenuKind==ArenaPlusMenuKind::Library){
        if(row>=0&&row<static_cast<int>(g_arenaLibraryEntries.size())){
            g_arenaLibraryRow=row;g_arenaLibraryEntry=g_arenaLibraryEntries[row];
            g_arenaLibraryStatus[0]=0;if(!g_arenaLibraryEntry.valid)ArenaLibraryStatus(g_arenaLibraryEntry.error);
            g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::LibraryItem);
        }else if(row==static_cast<int>(g_arenaLibraryEntries.size())){ArenaLibraryRefresh();g_arenaLibraryStatus[0]=0;g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::Library);}
        else g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(g_arenaLibraryParent);
        return;
    }
    if(row==4){g_arenaLibraryStatus[0]=0;g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::Library);return;}
    if(row==3){
        if(g_arenaLibraryEntry.builtin||g_arenaLibraryEntry.preset.legacy||!g_arenaLibraryEntry.valid){NativeMenu::PlaySfx(3);g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::LibraryItem);return;}
        AcquireSRWLockExclusive(&g_arenaRenameLock);strncpy_s(g_arenaRenameDraft,g_arenaLibraryEntry.preset.name.c_str(),_TRUNCATE);g_arenaRenameSelectAll=true;ReleaseSRWLockExclusive(&g_arenaRenameLock);
        g_arenaLibraryStatus[0]=0;InterlockedExchange(&g_arenaRenameActive,1);g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::Rename);return;
    }
    std::string root,legacy,error;Preset preset;
    if(row==1&&(g_arenaLibraryEntry.builtin||g_arenaLibraryEntry.preset.legacy)){
        ArenaLibraryStatus("Export a copy, edit its .bin, then import it");NativeMenu::PlaySfx(3);g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::LibraryItem);return;}
    if(!ArenaLibraryPaths(&root,&legacy)||!Load(root,legacy,g_arenaLibraryEntry,&preset,&error,row==1)){
        ArenaLibraryStatus(error);NativeMenu::PlaySfx(3);g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::LibraryItem);return;}
    if(row==2){if(!ArenaLibraryExport(preset))g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::LibraryItem);return;}
    if(row==0||row==1){
        FfxHooks::CustomMixUltra::Runtime::ProductionCancel(FfxHooks::CustomMixUltra::Runtime::CancelReason::NewGeneration);
        g_arenaPlusUltraSelection=preset.selection;g_arenaPlusMixRequiredSlots=preset.requiredSlots;g_arenaMixName=preset.name;
        g_arenaMixLoadedId=!g_arenaLibraryEntry.builtin&&!g_arenaLibraryEntry.preset.legacy?g_arenaLibraryEntry.id:std::string{};
        g_arenaPlusUltraSelectedRow=0;FfxHooks::CustomMixUltra::Runtime::ProductionPublishSelection(g_arenaPlusUltraSelection);
        ArenaPlus_Ultra_Reopen();
    }
}

static void ArenaPlus_Ultra_HandleConfirm(int row) {
    using namespace FfxHooks::CustomMixUltra;
    using namespace FfxHooks::CustomMixUltra::Runtime;

    if(row>=0 && row<ArenaPlus_MenuRowCount(ArenaPlusMenuKind::Ultra))g_arenaPlusUltraSelectedRow=row;
    if(!g_arenaPlusMixRequiredSlots){
        if(row==2){g_arenaMonsterSearchAll=true;ArenaPlus_OpenSearch(ArenaPlusMenuKind::Monsters);return;}
        if(row==9){g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::Battles);return;}
        if(row>=10)row-=2;else if(row>=3)--row;
    }
    const auto rules = ArenaPlus_MixRules();
    if(g_arenaPlusMixRequiredSlots==0 && row>=2 && row<=9) {
        if(row<8) {g_arenaMonsterSearchAll=false;g_arenaMonsterQuery[0]=0;g_arenaMonsterCategory=static_cast<FfxHooks::ArenaMonsters::Category>(row-2);g_arenaMonsterSelectedRow=0;g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::Monsters);}
        else g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(row==8?ArenaPlusMenuKind::Soundtrack:ArenaPlusMenuKind::Formation);
        return;
    }
    if(row==ARENA_PLUS_ULTRA_ROW_CAMERA){
        g_arenaPlusUltraSelection.camera=g_arenaPlusUltraSelection.camera==FfxHooks::ArenaScenery::Camera::Arena?
            FfxHooks::ArenaScenery::Camera::Tactical:FfxHooks::ArenaScenery::Camera::Arena;
        ProductionPublishSelection(g_arenaPlusUltraSelection);ArenaPlus_Ultra_Reopen();return;
    }
    if (row == ARENA_PLUS_ULTRA_ROW_SCENERY) {
        g_arenaPlusMenu = ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::Scenery);
        return;
    }
    if (row >= ARENA_PLUS_ULTRA_ROW_FIRST_CHOICE &&
        row < ARENA_PLUS_ULTRA_ROW_FIRST_CHOICE + ARENA_PLUS_ULTRA_CHOICE_COUNT) {
        const int choice = row - ARENA_PLUS_ULTRA_ROW_FIRST_CHOICE;
        if (!ArenaPlus_MixEnabled() || !FfxHooks::ArenaMix::CanAdd(
                g_arenaPlusUltraSelection, kArenaPlusUltraChoices[choice], rules)) {
            NativeMenu::PlaySfx(3);
            ArenaPlus_Ultra_Reopen();
            return;
        }
        const SelectionEditResult edit = TryAddChoice(
            g_arenaPlusUltraSelection, kArenaPlusUltraChoices[choice]);
        if (edit.accepted) {
            g_arenaPlusUltraSelection = edit.selection;
            ProductionPublishSelection(g_arenaPlusUltraSelection);
            NativeMenu::PlaySfx(1);
        } else {
            NativeMenu::PlaySfx(3);
        }
        ArenaPlus_Ultra_Reopen();
        return;
    }
    if(row==ARENA_PLUS_ULTRA_ROW_LIBRARY){ArenaLibraryOpen(ArenaPlusMenuKind::Ultra);return;}
    if(row==ARENA_PLUS_ULTRA_ROW_EXPORT){
        FfxHooks::ArenaMixLibrary::Preset preset; preset.name=g_arenaMixName;preset.requiredSlots=g_arenaPlusMixRequiredSlots;preset.selection=g_arenaPlusUltraSelection;
        g_arenaLibraryParent=ArenaPlusMenuKind::Ultra;
        if(!ArenaLibraryExport(preset)){ArenaLibraryRefresh();g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::Library);}
        return;
    }
    if (row == ARENA_PLUS_ULTRA_ROW_AUTO || row == ARENA_PLUS_ULTRA_ROW_POSITIONS) {
        const auto expanded = BuildSelection(g_arenaPlusUltraSelection);
        if (!ArenaPlus_MixEnabled() || expanded.result != SelectionResult::Ready) {
            NativeMenu::PlaySfx(3); ArenaPlus_Ultra_Reopen(); return;
        }
        if (row == ARENA_PLUS_ULTRA_ROW_AUTO) {
            g_arenaPlusUltraSelection.positions = FfxHooks::ArenaScenery::Generate(g_arenaPlusUltraSelection.scenery, expanded.expanded.monsterCount);
            ProductionPublishSelection(g_arenaPlusUltraSelection);
            NativeMenu::PlaySfx(1); ArenaPlus_Ultra_Reopen();
        } else {
            g_arenaPositionDraft = g_arenaPlusUltraSelection.positions.enabled
                ? g_arenaPlusUltraSelection.positions : FfxHooks::ArenaScenery::Generate(g_arenaPlusUltraSelection.scenery, expanded.expanded.monsterCount);
            g_arenaPositionSlot = 0; g_arenaPositionRow = 0;
            g_arenaPlusMenu = ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::Positions);
        }
        return;
    }
    if (row == ARENA_PLUS_ULTRA_ROW_NATIVE) {
        g_arenaPlusUltraSelection.positions = {};
        ProductionPublishSelection(g_arenaPlusUltraSelection);
        NativeMenu::PlaySfx(1); ArenaPlus_Ultra_Reopen(); return;
    }
    if (row == ARENA_PLUS_ULTRA_ROW_REMOVE_LAST) {
        const SelectionEditResult edit = RemoveLastChoice(g_arenaPlusUltraSelection);
        if (edit.accepted) {
            g_arenaPlusUltraSelection = edit.selection;
            ProductionPublishSelection(g_arenaPlusUltraSelection);
            NativeMenu::PlaySfx(1);
        } else {
            NativeMenu::PlaySfx(3);
        }
        ArenaPlus_Ultra_Reopen();
        return;
    }
    if (row == ARENA_PLUS_ULTRA_ROW_CLEAR) {
        const auto scenery = g_arenaPlusUltraSelection.scenery;
        const auto camera = g_arenaPlusUltraSelection.camera;
        const auto music = g_arenaPlusUltraSelection.musicTrack;
        g_arenaPlusUltraSelection = ClearSelection().selection;
        g_arenaPlusUltraSelection.musicTrack=music;
        g_arenaPlusUltraSelection.scenery = scenery;
        g_arenaPlusUltraSelection.camera = camera;
        ProductionPublishSelection(g_arenaPlusUltraSelection);
        NativeMenu::PlaySfx(1);
        ArenaPlus_Ultra_Reopen();
        return;
    }
    if (row == ARENA_PLUS_ULTRA_ROW_LAUNCH) {
        if (!ArenaPlus_MixEnabled() ||
            !FfxHooks::ArenaMix::CanLaunch(g_arenaPlusUltraSelection, rules)) {
            NativeMenu::PlaySfx(3);
            ProductionPublishSelection(g_arenaPlusUltraSelection);
            Log("[ffx-hooks] ArenaPlus: CustomMix Ultra launch rejected status=%s\n",
                StatusName(ProductionStatus().code));
            ArenaPlus_Ultra_Reopen();
            return;
        }
        const EditorLaunchOutcome editorLaunch = ArenaPlus_Ultra_LaunchFromPump();
        g_arenaPlusUltraSelection = editorLaunch.selection;
        if (editorLaunch.disposition ==
            EditorLaunchDisposition::CloseWithArmedRequest) {
            NativeMenu::PlaySfx(4);
            ArenaPlus_Ultra_CloseAfterLaunch();
            Log("[ffx-hooks] ArenaPlus: CustomMix Ultra carrier queued then request armed\n");
            return;
        }
        NativeMenu::PlaySfx(3);
        if (editorLaunch.disposition ==
            EditorLaunchDisposition::CloseWithVanillaCarrier) {
            ArenaPlus_Ultra_CloseAfterLaunch();
            Log("[ffx-hooks] ArenaPlus: CustomMix Ultra arm FAILED after exact carrier queue; carrier remains vanilla\n");
            return;
        }
        Log("[ffx-hooks] ArenaPlus: CustomMix Ultra queue FAILED; selection retained\n");
        ArenaPlus_Ultra_Reopen();
        return;
    }
    if (row == ARENA_PLUS_ULTRA_ROW_BACK) {
        NativeMenu::PlaySfx(4);
        ProductionCancel(CancelReason::Back);
        g_arenaPlusUltraSelection = {};
        g_arenaPlusMenu = g_arenaPlusMixRequiredSlots
            ? ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::CustomMix) : ArenaPlus_SpawnHubMenu();
        if (!g_arenaPlusMenu.obj) InterlockedExchange(&g_forceSubsystem, 0);
        Log("[ffx-hooks] ArenaPlus: Custom Mix returned to parent menu\n");
    }
}

static void ArenaPlus_HandleMenuConfirm(int row) {
    const int backRow = ArenaPlus_SubMenuBackRow(g_arenaPlusMenuKind);

    if(g_arenaPlusMenuKind==ArenaPlusMenuKind::Library || g_arenaPlusMenuKind==ArenaPlusMenuKind::LibraryItem || g_arenaPlusMenuKind==ArenaPlusMenuKind::Rename){ArenaLibraryHandle(row);return;}
    if (g_arenaPlusMenuKind == ArenaPlusMenuKind::Positions) {
        g_arenaPositionRow = row;
        if (row >= 0 && row <= 2) ArenaPlus_AdjustPosition(row, 1);
        else if (row == 3 || row == 4) {
            auto generated = FfxHooks::ArenaScenery::Generate(g_arenaPlusUltraSelection.scenery, g_arenaPositionDraft.count);
            if (row == 3) {
                auto candidate = g_arenaPositionDraft;
                candidate.points[g_arenaPositionSlot] = generated.points[g_arenaPositionSlot];
                candidate.automatic = false;
                if (FfxHooks::ArenaPositions::Validate(candidate, candidate.count) == FfxHooks::ArenaPositions::Issue::None)
                    g_arenaPositionDraft = candidate;
                else NativeMenu::PlaySfx(3);
            } else g_arenaPositionDraft = generated;
        } else if (row == 5 || row == 6) {
            const auto count = FfxHooks::CustomMixUltra::BuildSelection(g_arenaPlusUltraSelection).expanded.monsterCount;
            if (row == 5 && ArenaPlus_MixEnabled() &&
                FfxHooks::ArenaPositions::Validate(g_arenaPositionDraft, count) == FfxHooks::ArenaPositions::Issue::None) {
                g_arenaPlusUltraSelection.positions = g_arenaPositionDraft;
                FfxHooks::CustomMixUltra::Runtime::ProductionPublishSelection(g_arenaPlusUltraSelection);
            } else if (row == 5) {
                NativeMenu::PlaySfx(3);
                g_arenaPlusMenu = ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::Positions); return;
            }
            g_arenaPositionDraft = {};
            ArenaPlus_Ultra_Reopen(); return;
        }
        g_arenaPlusMenu = ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::Positions); return;
    }

    if(ArenaPlus_IsUltraChild(g_arenaPlusMenuKind)) {
        using namespace FfxHooks::CustomMixUltra;
        using namespace FfxHooks::CustomMixUltra::Runtime;
        if(g_arenaPlusMenuKind==ArenaPlusMenuKind::Search){ArenaPlus_FinishSearch(row==1);return;}
        if(g_arenaPlusMenuKind==ArenaPlusMenuKind::Battles){
            if(row==0){ArenaPlus_OpenSearch(ArenaPlusMenuKind::Battles);return;}
            if(row>0&&row<=g_arenaBattleCount){g_arenaBattleDetail=&FfxHooks::ArenaBattleProgram::Encounters()[g_arenaBattleRows[row-1]];g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::BattleDetail);return;}
        }
        if(g_arenaPlusMenuKind==ArenaPlusMenuKind::BattleDetail){
            const int count=g_arenaBattleDetail?g_arenaBattleDetail->count:0;
            if(row==count&&g_arenaBattleDetail){SelectionInput selected{};
                if(FfxHooks::ArenaBattleProgram::UseEncounter(*g_arenaBattleDetail,g_arenaPlusUltraSelection,&selected)){
                    g_arenaPlusUltraSelection=selected;g_arenaMixName=std::string("Mix ")+g_arenaBattleDetail->name.data();g_arenaMixLoadedId.clear();ProductionPublishSelection(selected);ArenaPlus_Ultra_Reopen();return;
                }NativeMenu::PlaySfx(3);
            }else if(row==count+1&&g_arenaBattleDetail&&g_arenaBattleDetail->scenery!=0xffffu){
                g_arenaPlusUltraSelection.scenery=static_cast<FfxHooks::ArenaScenery::Choice>(g_arenaBattleDetail->scenery);
                const auto expanded=BuildSelection(g_arenaPlusUltraSelection).expanded.monsterCount;
                g_arenaPlusUltraSelection.positions=FfxHooks::ArenaScenery::Generate(g_arenaPlusUltraSelection.scenery,expanded);
                ProductionPublishSelection(g_arenaPlusUltraSelection);ArenaPlus_Ultra_Reopen();return;
            }else if(row==backRow){g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::Battles);return;}
            g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::BattleDetail);return;
        }
        if(g_arenaPlusMenuKind==ArenaPlusMenuKind::Monsters&&row==0){ArenaPlus_OpenSearch(ArenaPlusMenuKind::Monsters);return;}
        if(row==backRow){Log("[ffx-hooks] ArenaBrowser Back kind=%d activations=%u\n",static_cast<int>(g_arenaPlusMenuKind),g_arenaPlusUltraSelection.activationCount);ArenaPlus_Ultra_Reopen();return;}
        if(g_arenaPlusMenuKind==ArenaPlusMenuKind::Monsters && row>0 && row<=g_arenaMonsterCount) {
            g_arenaMonsterSelectedRow=row;const auto* entry=g_arenaMonsterRows[row-1];
            if(entry && ArenaPlus_MixEnabled() && FfxHooks::ArenaMix::CanAdd(g_arenaPlusUltraSelection,entry->choice,ArenaPlus_MixRules())) {
                const auto edit=TryAddChoice(g_arenaPlusUltraSelection,entry->choice);
                if(edit.accepted) {g_arenaPlusUltraSelection=edit.selection;ProductionPublishSelection(g_arenaPlusUltraSelection);NativeMenu::PlaySfx(1);Log("[ffx-hooks] ArenaBrowser Add choice=%u activations=%u\n",static_cast<unsigned>(entry->choice),g_arenaPlusUltraSelection.activationCount);}
            } else NativeMenu::PlaySfx(3);
            g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::Monsters);return;
        }
        if(g_arenaPlusMenuKind==ArenaPlusMenuKind::Soundtrack && row>=0 && row<backRow) {
            g_arenaPlusUltraSelection.musicTrack=FfxHooks::ArenaSoundtrack::kTracks[row].id;
            ProductionPublishSelection(g_arenaPlusUltraSelection);ArenaPlus_Ultra_Reopen();return;
        }
        if(g_arenaPlusMenuKind==ArenaPlusMenuKind::Formation && row>=0 && row<g_arenaPlusUltraSelection.activationCount) {
            auto& selection=g_arenaPlusUltraSelection;
            for(unsigned i=static_cast<unsigned>(row)+1;i<selection.activationCount;++i)selection.activations[i-1]=selection.activations[i];
            --selection.activationCount;selection.activations[selection.activationCount]={};
            const auto count=BuildSelection(selection).expanded.monsterCount;
            selection.positions=count?FfxHooks::ArenaScenery::Generate(selection.scenery,count):FfxHooks::ArenaPositions::Layout{};
            ProductionPublishSelection(selection);g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::Formation);return;
        }
        ArenaPlus_Ultra_Reopen();return;
    }

    if (g_arenaPlusMenuKind == ArenaPlusMenuKind::Hub) {
        if (row == ARENA_PLUS_HUB_ROW_SAFE) {
            const bool queued = ArenaPlus_LaunchSafeBattleFromPump();
            if (queued) {
                g_nativeHeldAction = -1;
                InterlockedExchange(&g_forceSubsystem, 0);
                Log("[ffx-hooks] ArenaPlus: battle queued; force-gate off\n");
            } else {
                Log("[ffx-hooks] ArenaPlus: battle not queued; reopening Arena+ hub\n");
                g_arenaPlusMenu = ArenaPlus_SpawnMenu();
            }
            return;
        }
        if (row == ARENA_PLUS_HUB_ROW_DARK) {
            g_arenaPlusMenu = ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::DarkRematch);
            Log("[ffx-hooks] ArenaPlus: hub -> Dark Aeon Rematch sub-menu\n");
            return;
        }
        if (row == ARENA_PLUS_HUB_ROW_GAUNTLET) {
            g_arenaPlusMenu = ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::AeonGauntlet);
            Log("[ffx-hooks] ArenaPlus: hub -> Aeon Gauntlet sub-menu\n");
            return;
        }
        if (row == ARENA_PLUS_HUB_ROW_MIX) {
            g_arenaPlusMenu = ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::CustomMix);
            Log("[ffx-hooks] ArenaPlus: hub -> Custom Mix sub-menu\n");
            return;
        }
        if (row == ARENA_PLUS_HUB_ROW_ULTRA) {
            using namespace FfxHooks::CustomMixUltra::Runtime;
            if (ArenaPlus_MixEnabled()) {
                g_arenaPlusMixRequiredSlots = 0;
                g_arenaMonsterQuery[0]=g_arenaSceneryQuery[0]=g_arenaBattleQuery[0]=0;g_arenaMonsterSearchAll=false;
                g_arenaMixName="Custom Mix Ultra";g_arenaMixLoadedId.clear();
                g_arenaPlusUltraSelectedRow = 0;
                NativeMenu::PlaySfx(1);
                ProductionCancel(CancelReason::NewGeneration);
                g_arenaPlusUltraSelection = {};
                ProductionPublishSelection(g_arenaPlusUltraSelection);
                g_arenaPlusMenu = ArenaPlus_SpawnUltraMenu();
                Log("[ffx-hooks] ArenaPlus: hub -> CustomMix Ultra RAM-only sub-menu\n");
            } else {
                NativeMenu::PlaySfx(3);
                Log("[ffx-hooks] ArenaPlus: CustomMix Ultra unavailable status=%s\n",
                    StatusName(ProductionStatus().code));
                g_arenaPlusMenu = ArenaPlus_SpawnHubMenu();
            }
            if (!g_arenaPlusMenu.obj) InterlockedExchange(&g_forceSubsystem, 0);
            return;
        }
        if (row == ARENA_PLUS_HUB_ROW_BACK) {
            F7CloseTransition(
                FfxHooks::F7Ui::CloseSource::BackRow,
                FfxHooks::F7Ui::CloseDestination::Hub);
            return;
        }
        return;
    }

    if (g_arenaPlusMenuKind == ArenaPlusMenuKind::Ultra) {
        ArenaPlus_Ultra_HandleConfirm(row);
        return;
    }

    if (g_arenaPlusMenuKind == ArenaPlusMenuKind::Scenery) {
        if(!g_arenaPlusMixRequiredSlots&&row==0){ArenaPlus_OpenSearch(ArenaPlusMenuKind::Scenery);return;}
        const int index=row-(g_arenaPlusMixRequiredSlots?0:1);
        if(index>=0&&index<g_arenaSceneryCount){
            g_arenaPlusUltraSelection.scenery=g_arenaSceneryRows[index];
            const auto count=FfxHooks::CustomMixUltra::BuildSelection(g_arenaPlusUltraSelection).expanded.monsterCount;
            if(g_arenaPlusUltraSelection.positions.automatic || (!g_arenaPlusUltraSelection.positions.enabled && static_cast<unsigned>(g_arenaPlusUltraSelection.scenery)>=9)){
                g_arenaPlusUltraSelection.positions=FfxHooks::ArenaScenery::Generate(g_arenaPlusUltraSelection.scenery,count);
                g_arenaPlusUltraSelection.positions.automatic=true;
            }
            FfxHooks::CustomMixUltra::Runtime::ProductionPublishSelection(g_arenaPlusUltraSelection);
        }
        ArenaPlus_Ultra_Reopen();return;
    }

    if (row == backRow) {
        const int prevKind = static_cast<int>(g_arenaPlusMenuKind);
        g_arenaPlusMenu = ArenaPlus_SpawnMenu();
        Log("[ffx-hooks] ArenaPlus: sub-menu back to hub (kind was %d)\n", prevKind);
        return;
    }

    if (g_arenaPlusMenuKind == ArenaPlusMenuKind::DarkRematch) {
        const int dark = row;
        if (ArenaPlus_BossRouteAllowed(dark)) {
            const bool queued = ArenaPlus_LaunchBossBattleFromPump(dark);
            if (queued) {
                g_nativeHeldAction = -1;
                InterlockedExchange(&g_forceSubsystem, 0);
                Log("[ffx-hooks] ArenaPlus: boss battle queued/armed; force-gate off\n");
            } else {
                Log("[ffx-hooks] ArenaPlus: boss battle not queued; reopening Dark Rematch\n");
                g_arenaPlusMenu = ArenaPlus_ReopenMenu();
            }
        } else {
            Log("[ffx-hooks] ArenaPlus: dark row %d %s locked/no-route\n", row, kArenaPlusDarkNames[dark]);
            g_arenaPlusMenu = ArenaPlus_ReopenMenu();
        }
        return;
    }

    if (g_arenaPlusMenuKind == ArenaPlusMenuKind::CustomMix) {
        if(row==ARENA_PLUS_CUSTOM_MIX_COMBO_COUNT){ArenaLibraryOpen(ArenaPlusMenuKind::CustomMix);return;}
        g_arenaMixName="Custom Mix";g_arenaMixLoadedId.clear();
        if (row >= 0 && row < ARENA_PLUS_CUSTOM_MIX_COMBO_COUNT && ArenaPlus_MixEnabled()) {
            g_arenaPlusMixRequiredSlots = static_cast<uint8_t>(row + 3);
            g_arenaPlusUltraSelectedRow = 0;
            FfxHooks::CustomMixUltra::Runtime::ProductionCancel(
                FfxHooks::CustomMixUltra::Runtime::CancelReason::NewGeneration);
            g_arenaPlusUltraSelection = {};
            g_arenaPlusUltraSelection.scenery = FfxHooks::ArenaScenery::Default(g_arenaPlusMixRequiredSlots);
            FfxHooks::CustomMixUltra::Runtime::ProductionPublishSelection(g_arenaPlusUltraSelection);
            g_arenaPlusMenu = ArenaPlus_SpawnUltraMenu();
        } else {
            NativeMenu::PlaySfx(3);
            g_arenaPlusMenu = ArenaPlus_ReopenMenu();
        }
        return;
    }

    const int combo = (g_arenaPlusMenuKind == ArenaPlusMenuKind::CustomMix)
        ? ArenaPlus_CustomMixComboIndex(row)
        : row;
    if (!ArenaPlus_ComboRouteAllowed(combo)) {
        Log("[ffx-hooks] ArenaPlus: combo row %d %s locked (mapped=%d enabled=%d)\n",
            row, kArenaPlusComboNames[combo],
            ArenaPlus_ComboRouteMapped(combo) ? 1 : 0,
            ArenaPlus_ComboBattlesEnabled() ? 1 : 0);
        g_arenaPlusMenu = ArenaPlus_ReopenMenu();
        return;
    }
    const bool queued = ArenaPlus_LaunchComboBattleFromPump(combo);
    if (queued) {
        g_nativeHeldAction = -1;
        InterlockedExchange(&g_forceSubsystem, 0);
        Log("[ffx-hooks] ArenaPlus: combo battle queued; force-gate off\n");
    } else {
        Log("[ffx-hooks] ArenaPlus: combo battle not queued; reopening sub-menu\n");
        g_arenaPlusMenu = ArenaPlus_ReopenMenu();
    }
}

// â”€â”€ SIN Curse submenu â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// "Spira Instinct Network" â€” runtime curse toggle, intensity, and status.

static bool ArenaPlus_ReadCurrentEncounterRoute(int* outField, int* outGroup, char* source, int sourceCap) {
    if (outField) *outField = 0;
    if (outGroup) *outGroup = 0;
    if (source && sourceCap > 0) source[0] = 0;
    if (!g_base) return false;

    typedef int (__cdecl* FnNoArgInt)(void);
    int field = 0;
    int scene = 0;
    bool ok = false;
    __try {
        field = reinterpret_cast<FnNoArgInt>(g_base + RVA_ENCOUNTER_GET_CURRENT_FIELD)();
        scene = reinterpret_cast<FnNoArgInt>(g_base + RVA_ENCOUNTER_GET_SCENE_STATE)();
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[ffx-hooks] ArenaPlus: current route accessors exception 0x%08X\n", GetExceptionCode());
        return false;
    }
    if (!ok || field < 0 || field > 0xFFFF || scene == 0) return false;

    uint8_t group = 0;
    __try {
        group = *reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(scene) + ENCOUNTER_SCENE_GROUP_OFFSET);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[ffx-hooks] ArenaPlus: current route group read exception scene=0x%08X seh=0x%08X\n",
            static_cast<unsigned>(scene), GetExceptionCode());
        return false;
    }

    if (outField) *outField = field;
    if (outGroup) *outGroup = static_cast<int>(group);
    if (source && sourceCap > 0) _snprintf_s(source, sourceCap, _TRUNCATE, "current");
    return true;
}

static const uint32_t RVA_BTL_BATTLEFIELD_FIELD = 0x00D2C254u; // MemoryBtl: LO=battlefield_id HI=field_idx
static int g_pendingScenarioBackdropFieldIdx = -1;
static uint16_t g_pendingScenarioBackdropBfId = 0;
static int g_pendingScenarioBackdropFrames = 0;
static char g_pendingEncounterPinName[32] = {};
static int g_pendingEncounterPinFrames = 0;
static int g_pendingEncounterPinGroup = -1;
static int g_pendingEncounterPinFormation = -1;

/* Deferred restore is only a timer. Path and byte authority live in the self-owned marker so boot
   and frame-pump recovery execute the same verified, one-shot transaction. */
static int g_pendingRestoreFrames = 0;

static void ArenaPlus_ArmDeferredFileRestore(int frames) {
    if (!ArenaPlusComposePick_IsAvailable()) {
        Log("[ffx-hooks] ArenaPlus: deferred restore unavailable (compose transactions quarantined)\n");
        return;
    }
    if (g_runtimeValidateOnly) {
        Log("[ffx-hooks] ArenaPlus: deferred restore not armed (validation-only)\n");
        return;
    }
    if (!FfxHooks::ArenaComposeRestore::ModuleMarkerPresent(g_module)) {
        Log("[ffx-hooks] ArenaPlus: deferred restore not armed (no self-owned pending marker)\n");
        return;
    }
    g_pendingRestoreFrames = frames;
    Log("[ffx-hooks] ArenaPlus: armed verified deferred restore (%d frames)\n", frames);
}

static void ArenaPlus_TickDeferredFileRestore() {
    if (g_pendingRestoreFrames <= 0) return;
    g_pendingRestoreFrames--;
    if (g_pendingRestoreFrames > 0) return;
    if (g_runtimeValidateOnly) {
        Log(
            "[ffx-hooks] ArenaPlus: deferred restore retained result=%s\n",
            FfxHooks::ArenaComposeRestore::ResultName(
                FfxHooks::ArenaComposeRestore::Result::BlockedValidateOnly));
        return;
    }
    const bool featureEnabled = ArenaPlusComposePick_IsEnabled();
    char modBtlRoot[MAX_PATH * 2] = {};
    if (!ResolveModBtlRoot(modBtlRoot, sizeof(modBtlRoot))) {
        Log("[ffx-hooks] ArenaPlus: deferred restore retained result=PATH_REJECTED\n");
        return;
    }
    const FfxHooks::ArenaComposeRestore::Result result =
        FfxHooks::ArenaComposeRestore::RestorePendingForModule(
            g_module,
            modBtlRoot,
            FfxHooks::ArenaComposeRestore::RestorePolicy{ g_runtimeValidateOnly, featureEnabled });
    Log(
        "[ffx-hooks] ArenaPlus: deferred restore result=%s\n",
        FfxHooks::ArenaComposeRestore::ResultName(result));
}

static void ArenaPlus_RestorePendingComposeOnBoot(bool validateOnly) {
    if (!ArenaPlusComposePick_IsAvailable()) {
        Log("[ffx-hooks] ArenaPlus: boot restore result=QUARANTINED\n");
        return;
    }
    if (validateOnly) {
        // WHY: validation-only returns before resolving paths or opening the marker. Feature OFF
        // is intentionally not included: restoring an exact self-owned v1 marker is teardown, not
        // activation. A stale compose_last.json is never considered in either mode.
        const FfxHooks::ArenaComposeRestore::Result blocked =
            FfxHooks::ArenaComposeRestore::RestorePendingForModule(
                g_module,
                nullptr,
                FfxHooks::ArenaComposeRestore::RestorePolicy{ true, false });
        Log(
            "[ffx-hooks] ArenaPlus: boot restore result=%s\n",
            FfxHooks::ArenaComposeRestore::ResultName(blocked));
        return;
    }
    const bool featureEnabled = ArenaPlusComposePick_IsEnabled();
    char modBtlRoot[MAX_PATH * 2] = {};
    if (!ResolveModBtlRoot(modBtlRoot, sizeof(modBtlRoot))) {
        Log("[ffx-hooks] ArenaPlus: boot restore result=PATH_REJECTED\n");
        return;
    }
    const FfxHooks::ArenaComposeRestore::Result result =
        FfxHooks::ArenaComposeRestore::RestorePendingForModule(
            g_module,
            modBtlRoot,
            FfxHooks::ArenaComposeRestore::RestorePolicy{ validateOnly, featureEnabled });
    Log(
        "[ffx-hooks] ArenaPlus: boot restore result=%s\n",
        FfxHooks::ArenaComposeRestore::ResultName(result));
}

/* RT2-proved (2026-06-23): patching MemoryBtl field_idx @ 0xD2C254 during load hijacks the
 * vanilla encounter table (mcfrâ†’Chimera, bikaâ†’Sand Worm) even when 781D60 queued the composed carrier bin.
 * Custom Mix scene + roster come from compose (scenario template â†’ chunk0/chunk2/chunk3 in carrier output).
 * Runtime backdrop patch is opt-in only via arena_plus_scenario_backdrop.flag (lab/experiments). */
static bool ArenaPlus_ScenarioRuntimeBackdropEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ARENAPLUS_SCENARIO_BACKDROP") ||
           ModuleFlagEnabled("arena_plus_scenario_backdrop.flag") ||
           ModuleFlagEnabled("config\\arena_plus_scenario_backdrop.flag");
}

static bool ArenaPlus_ShouldPatchScenarioFieldIdx(int combo, int scenarioField) {
    if (scenarioField < 0) return false;
    if (combo == 5) return scenarioField == 36 || scenarioField == 42; /* x3 Macalania mcfr/mcyt only */
    if (combo == 6 || combo == 7) return scenarioField == 63;         /* x4/x5 cavern shares nagi idx */
    return false;
}

/* Cross-map Custom Mix: deploy on scenario bin (bika02_01, â€¦) and queue via 781D60(token)
 * for that map â€” NOT the nagi/mcyt compose carrier. MsBattleEncountExe alone does not arm n2=2. */
static bool ArenaPlus_IsScenarioCrossMap(int combo, int scenarioField) {
    if (scenarioField < 0) return false;
    if (combo < 5 || combo > 7) return false;
    switch (combo) {
    case 5: return scenarioField == 36 || scenarioField == 24;
    case 6: return scenarioField == 47 || scenarioField == 24;
    case 7: return scenarioField == 24;
    default: return false;
    }
}

static bool ArenaPlus_ShouldApplyScenarioCrossMapBackdrop(int combo, int scenarioField) {
    if (scenarioField < 0) return false;
    if (combo < 5 || combo > 7) return false;
    if (!ArenaPlus_ScenarioRuntimeBackdropEnabled()) return false;
    if (ArenaPlus_IsScenarioCrossMap(combo, scenarioField)) return false;
    switch (combo) {
    case 5: return scenarioField == 42;
    case 6: return scenarioField == 63;
    case 7: return scenarioField == 63;
    default: return false;
    }
}

static bool ArenaPlus_BuildScenarioLaunchRoute(
    const char* deployId,
    int group,
    int formation,
    ArenaPlusBossRoute* out) {
    if (!deployId || !deployId[0] || !out) return false;
    *out = {};
    out->group = group;
    out->formation = formation;
    out->transition = 2;
    out->battleId = deployId;

    struct Row { const char* id; uint32_t token; int tableId; const char* note; };
    static const Row kRows[] = {
        { "bika02_01", 0x01600001u, 352, "Custom Mix Bikanel @ bika02_01 token=0x01600001 (table 352 f1)" },
        { "kino00_00", 0x00DC0000u, 220, "Custom Mix Mushroom Rock Road @ kino00_00 token=0x00DC0000 (table 220 f0)" },
        { "mcfr00_00", 0x01360000u, 310, "Custom Mix Macalania Forest @ mcfr00_00 token=0x01360000" },
        { "mcyt00_00", 0x01540000u, 340, "Custom Mix Macalania Open @ mcyt00_00 token=0x01540000" },
        { "mcyt00_21", 0x01540015u, 340, "Custom Mix Macalania Open2 @ mcyt00_21 token=0x01540015" },
    };
    for (const Row& row : kRows) {
        if (_stricmp(deployId, row.id) != 0) continue;
        out->battleToken = row.token;
        out->field = row.tableId;
        out->evidence = row.note;
        return true;
    }
    return false;
}

static void ArenaPlus_PinEncounterName(const char* battleId, bool quiet) {
    if (!g_base || !battleId || !battleId[0]) return;
    char nameBuf[14] = {};
    for (int i = 0; i < 13 && battleId[i]; ++i)
        nameBuf[i] = battleId[i];
    bool ok = false;
    uint32_t err = 0;
    __try {
        char* dest = reinterpret_cast<char*>(g_base + RVA_BATTLE_NAME);
        for (int i = 0; i < 13; ++i)
            dest[i] = nameBuf[i];
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        err = GetExceptionCode();
        ok = false;
    }
    if (!quiet || !ok) {
        Log("[ffx-hooks] ArenaPlus: encounter name pin %s -> '%s' err=0x%08X\n",
            ok ? "ok" : "FAILED",
            nameBuf,
            err);
    }
}

static void ArenaPlus_RestoreCarrierQueueFormation(const ArenaPlusBossRoute& route, bool quiet) {
    if (!g_base) return;
    bool ok = false;
    uint32_t err = 0;
    uint8_t groupBefore = 0;
    uint8_t formationBefore = 0;
    __try {
        groupBefore = *reinterpret_cast<volatile uint8_t*>(g_base + RVA_BATTLE_QUEUE_GROUP);
        formationBefore = *reinterpret_cast<volatile uint8_t*>(g_base + RVA_BATTLE_QUEUE_FORMATION);
        *reinterpret_cast<volatile uint8_t*>(g_base + RVA_BATTLE_QUEUE_GROUP) = static_cast<uint8_t>(route.group & 0xFF);
        *reinterpret_cast<volatile uint8_t*>(g_base + RVA_BATTLE_QUEUE_FORMATION) = static_cast<uint8_t>(route.formation & 0xFF);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        err = GetExceptionCode();
        ok = false;
    }
    if (!quiet || !ok) {
        Log("[ffx-hooks] ArenaPlus: carrier queue G/F restore %s group %u->%u formation %u->%u err=0x%08X\n",
            ok ? "ok" : "FAILED",
            static_cast<unsigned>(groupBefore),
            static_cast<unsigned>(route.group & 0xFFu),
            static_cast<unsigned>(formationBefore),
            static_cast<unsigned>(route.formation & 0xFFu),
            err);
    }
}

static void ArenaPlus_ArmEncounterPinByName(const char* battleId, int group, int formation) {
    if (!battleId || !battleId[0]) {
        g_pendingEncounterPinFrames = 0;
        g_pendingEncounterPinName[0] = '\0';
        g_pendingEncounterPinGroup = -1;
        g_pendingEncounterPinFormation = -1;
        return;
    }
    lstrcpynA(g_pendingEncounterPinName, battleId, static_cast<int>(sizeof(g_pendingEncounterPinName)));
    g_pendingEncounterPinGroup = group;
    g_pendingEncounterPinFormation = formation;
    g_pendingEncounterPinFrames = 900;
}

static void ArenaPlus_ArmEncounterPin(const ArenaPlusBossRoute& route) {
    if (!route.battleId || !route.battleId[0]) {
        g_pendingEncounterPinFrames = 0;
        g_pendingEncounterPinName[0] = '\0';
        g_pendingEncounterPinGroup = -1;
        g_pendingEncounterPinFormation = -1;
        return;
    }
    lstrcpynA(g_pendingEncounterPinName, route.battleId, static_cast<int>(sizeof(g_pendingEncounterPinName)));
    g_pendingEncounterPinGroup = route.group;
    g_pendingEncounterPinFormation = route.formation;
    g_pendingEncounterPinFrames = 900;
}

static void ArenaPlus_TickEncounterPinPending() {
    if (g_pendingEncounterPinFrames <= 0 || !g_pendingEncounterPinName[0]) return;
    g_pendingEncounterPinFrames--;
    ArenaPlus_PinEncounterName(g_pendingEncounterPinName, true);
    /* Keep carrier G/F stable while the loader reads the mod bin path. */
    if (g_pendingEncounterPinGroup >= 0 && g_pendingEncounterPinFormation >= 0 && g_base) {
        __try {
            *reinterpret_cast<volatile uint8_t*>(g_base + RVA_BATTLE_QUEUE_GROUP) =
                static_cast<uint8_t>(g_pendingEncounterPinGroup & 0xFF);
            *reinterpret_cast<volatile uint8_t*>(g_base + RVA_BATTLE_QUEUE_FORMATION) =
                static_cast<uint8_t>(g_pendingEncounterPinFormation & 0xFF);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

static void ArenaPlus_ApplyScenarioBackdropVisualOnly(int fieldIdx, uint16_t battlefieldId, bool quiet) {
    if (!g_base || fieldIdx < 0 || battlefieldId == 0) return;

    bool ok = false;
    uint32_t err = 0;
    uint32_t fieldHi = 0;
    __try {
        volatile uint32_t* btlFieldWord = reinterpret_cast<volatile uint32_t*>(g_base + RVA_BTL_BATTLEFIELD_FIELD);
        *btlFieldWord = (static_cast<uint32_t>(fieldIdx & 0xFFFF) << 16) |
            static_cast<uint32_t>(battlefieldId & 0xFFFF);
        fieldHi = (*btlFieldWord >> 16) & 0xFFFFu;
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        err = GetExceptionCode();
        ok = false;
    }

    if (!quiet || !ok) {
        Log("[ffx-hooks] ArenaPlus: scenario backdrop visual-only %s fieldIdx=%d battlefieldId=%u (0x%04X) c254FieldHi=%u err=0x%08X\n",
            ok ? "patched" : "FAILED",
            fieldIdx,
            static_cast<unsigned>(battlefieldId),
            static_cast<unsigned>(battlefieldId),
            fieldHi,
            err);
    }
}

static void ArenaPlus_ApplyScenarioBackdropBfOnly(uint16_t battlefieldId, bool quiet) {
    if (!g_base || battlefieldId == 0) return;

    bool ok = false;
    uint32_t err = 0;
    uint32_t before = 0;
    uint32_t after = 0;
    __try {
        volatile uint32_t* btlFieldWord = reinterpret_cast<volatile uint32_t*>(g_base + RVA_BTL_BATTLEFIELD_FIELD);
        before = *btlFieldWord;
        *btlFieldWord = (before & 0xFFFF0000u) | static_cast<uint32_t>(battlefieldId & 0xFFFFu);
        after = *btlFieldWord;
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        err = GetExceptionCode();
        ok = false;
    }

    if (!quiet || !ok) {
        Log("[ffx-hooks] ArenaPlus: scenario backdrop bf-only %s battlefieldId=%u (0x%04X) c254=0x%08X->0x%08X fieldHi=%u err=0x%08X\n",
            ok ? "patched" : "FAILED",
            static_cast<unsigned>(battlefieldId),
            static_cast<unsigned>(battlefieldId),
            before,
            after,
            (after >> 16) & 0xFFFFu,
            err);
    }
}

static void ArenaPlus_ArmScenarioBackdropPending(int fieldIdx, uint16_t battlefieldId) {
    if (fieldIdx < 0 || battlefieldId == 0) {
        g_pendingScenarioBackdropFieldIdx = -1;
        g_pendingScenarioBackdropBfId = 0;
        g_pendingScenarioBackdropFrames = 0;
        return;
    }
    g_pendingScenarioBackdropFieldIdx = fieldIdx;
    g_pendingScenarioBackdropBfId = battlefieldId;
    g_pendingScenarioBackdropFrames = 900;
}

static void ArenaPlus_TickScenarioBackdropPending() {
    if (g_pendingScenarioBackdropFieldIdx < 0 || g_pendingScenarioBackdropFrames <= 0) return;
    g_pendingScenarioBackdropFrames--;
    ArenaPlus_ApplyScenarioBackdropVisualOnly(g_pendingScenarioBackdropFieldIdx, g_pendingScenarioBackdropBfId, true);
}

static bool ArenaPlus_ForceBattleDirect(
    int field, int group, int formation,
    FfxHooks::F7Difficulty::BattleFieldSource fieldSource,
    int32_t* outRet, uint32_t* outErr) {
    if (outRet) *outRet = 0;
    if (outErr) *outErr = 0;
    if (!g_base) return false;

    bool ok = false;
    int32_t ret = 0;
    uint32_t err = 0;
    volatile uint32_t* script0 = reinterpret_cast<volatile uint32_t*>(g_base + RVA_SCRIPTED_ENCOUNTER_0);
    volatile uint32_t* script1 = reinterpret_cast<volatile uint32_t*>(g_base + RVA_SCRIPTED_ENCOUNTER_1);
    volatile uint8_t* formationByte = reinterpret_cast<volatile uint8_t*>(g_base + RVA_SCRIPTED_FORMATION);
    typedef int (__cdecl* FnMsBattleEncountExe)(int, int, float);
    const FfxHooks::F7Difficulty::BattleFieldRequest fieldRequest =
        FfxHooks::F7_BeginPendingBattleFieldRequest();

    FfxHooks::F7_BeginExplicitLaunchCapture();
    __try {
        *script0 = 1u;
        *script1 = 1u;
        *formationByte = static_cast<uint8_t>(formation);
        ret = reinterpret_cast<FnMsBattleEncountExe>(g_base + RVA_MS_BATTLE_ENCOUNT_EXE)(field, group, 0.0f);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        err = GetExceptionCode();
        ok = false;
    }
    FfxHooks::F7_EndExplicitLaunchCapture();

    __try {
        *script0 = 0u;
        *script1 = 0u;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!err) err = GetExceptionCode();
    }

    if (outRet) *outRet = ret;
    if (outErr) *outErr = err;
    if (ok && ret == -1) {
        FfxHooks::F7_CommitPendingBattleFieldRequest(
            fieldRequest, field, fieldSource);
    } else {
        FfxHooks::F7_CancelPendingBattleFieldRequest(fieldRequest);
    }
    return ok;
}

static bool ArenaPlus_Battle7002SwapArgs() {
    return EnvFlagEnabled("FFXHOOKS_ARENAPLUS_BATTLE7002_SWAP") ||
           ModuleFlagEnabled("arena_plus_battle7002_swap.flag") ||
           ModuleFlagEnabled("config\\arena_plus_battle7002_swap.flag");
}

static bool ArenaPlus_DirectBattle7002Enabled() {
    return EnvFlagEnabled("FFXHOOKS_ARENAPLUS_DIRECT_BATTLE7002") ||
           ModuleFlagEnabled("arena_plus_direct_battle7002.flag") ||
           ModuleFlagEnabled("config\\arena_plus_direct_battle7002.flag");
}

static bool ArenaPlus_UseEventTransition() {
    return EnvFlagEnabled("FFXHOOKS_ARENAPLUS_EVENT_TRANSITION") ||
           ModuleFlagEnabled("arena_plus_event_transition.flag") ||
           ModuleFlagEnabled("config\\arena_plus_event_transition.flag");
}

static bool ArenaPlus_AllowLegacyBossFallback() {
    return EnvFlagEnabled("FFXHOOKS_ARENAPLUS_LEGACY_BOSS_FALLBACK") ||
           ModuleFlagEnabled("arena_plus_legacy_boss_fallback.flag") ||
           ModuleFlagEnabled("config\\arena_plus_legacy_boss_fallback.flag");
}

static bool ArenaPlus_LaunchBattle7002Exact(
    const ArenaPlusBossRoute& route, int dark,
    FfxHooks::F7Difficulty::BattleFieldSource fieldSource,
    int32_t* outRet, uint32_t* outErr) {
    if (outRet) *outRet = 0;
    if (outErr) *outErr = 0;
    if (!g_base || route.battleToken == 0) return false;

    typedef int (__cdecl* FnBattleLaunch7002)(uint32_t, uint32_t, uint32_t);
    uint32_t stack[8] = {};
    const bool swapArgs = ArenaPlus_Battle7002SwapArgs();
    if (swapArgs) {
        stack[0] = route.battleToken;
        stack[1] = route.transition;
    } else {
        // RT2 vanilla trace showed stack[0]=transition, stack[1]=battle token:
        // [00000002 025C0005 ...] for Battle.launchBattle.
        stack[0] = route.transition;
        stack[1] = route.battleToken;
    }

    int32_t ret = 0;
    uint32_t err = 0;
    bool ok = false;
    const FfxHooks::F7Difficulty::BattleFieldRequest fieldRequest =
        FfxHooks::F7_BeginPendingBattleFieldRequest();
    Log("[ffx-hooks] ArenaPlus: Battle.7002 exact try row=%d name=%s battleId=%s token=0x%08X transition=%u stack0=0x%08X stack1=0x%08X swap=%d\n",
        dark,
        (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
        route.battleId ? route.battleId : "?",
        route.battleToken,
        route.transition,
        stack[0],
        stack[1],
        swapArgs ? 1 : 0);

    FfxHooks::F7_BeginExplicitLaunchCapture();
    __try {
        ret = reinterpret_cast<FnBattleLaunch7002>(g_base + RVA_BATTLE_LAUNCH_7002)(
            0,
            0,
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&stack[0])));
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        err = GetExceptionCode();
        ok = false;
    }
    FfxHooks::F7_EndExplicitLaunchCapture();

    if (outRet) *outRet = ret;
    if (outErr) *outErr = err;
    Log("[ffx-hooks] ArenaPlus: Battle.7002 exact result row=%d battleId=%s token=0x%08X -> ok=%d ret=0x%08X %s err=0x%08X\n",
        dark,
        route.battleId ? route.battleId : "?",
        route.battleToken,
        ok ? 1 : 0,
        static_cast<unsigned>(ret),
        LabForceRetMeaning(ret),
        err);
    const bool queued = ok && ret == -1;
    if (queued) {
        FfxHooks::F7_CommitPendingBattleFieldRequest(
            fieldRequest, route.field, fieldSource);
    } else {
        FfxHooks::F7_CancelPendingBattleFieldRequest(fieldRequest);
    }
    return queued;
}

static bool ArenaPlus_IsExactUltraCarrierRoute(
    const ArenaPlusBossRoute& route,
    int dark,
    FfxHooks::F7Difficulty::BattleFieldSource fieldSource) {
    return dark == -1 && route.field == 517 && route.group == 0 &&
           route.formation == 0 && route.battleToken == 0x02050000u &&
           route.transition == 2 && route.battleId != nullptr &&
           strcmp(route.battleId, "dome02_00") == 0 &&
           fieldSource == FfxHooks::F7Difficulty::BattleFieldSource::Ultra;
}

static bool ArenaPlus_LaunchBattle781D60Request(
    const ArenaPlusBossRoute& route, int dark,
    ArenaPlusDirectRequestAuthority authority,
    FfxHooks::F7Difficulty::BattleFieldSource fieldSource,
    int32_t* outRet, uint32_t* outErr, bool* outQueueArmed) {
    if (outRet) *outRet = 0;
    if (outErr) *outErr = 0;
    if (outQueueArmed) *outQueueArmed = false;
    const bool authorityAllowed =
        (authority == ArenaPlusDirectRequestAuthority::LegacyExperimental &&
            ArenaPlus_DirectRequest781D60Enabled()) ||
        (authority == ArenaPlusDirectRequestAuthority::CustomMixUltraExactCarrier &&
            ArenaPlus_MixEnabled() &&
            ArenaPlus_IsExactUltraCarrierRoute(route, dark, fieldSource));
    if (!g_base || route.battleToken == 0 || !authorityAllowed) return false;

    typedef int (__cdecl* FnBattleRequest781D60)(int, char, char);
    ArenaPlus_LogBattleQueueState("direct-request pre", &route, dark);
    ArenaPlus_CallCommonSetBattleFlags0200("direct-request prelaunch", route, dark);
    ArenaPlus_LogBattleQueueState("direct-request after-flags", &route, dark);

    int32_t ret = 0;
    uint32_t err = 0;
    bool ok = false;
    const FfxHooks::F7Difficulty::BattleFieldRequest fieldRequest =
        FfxHooks::F7_BeginPendingBattleFieldRequest();
    const char transition = static_cast<char>(route.transition & 0xFFu);
    Log("[ffx-hooks] ArenaPlus: direct request 781D60 try row=%d name=%s battleId=%s token=0x%08X args=[token,1,%u]\n",
        dark,
        (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
        route.battleId ? route.battleId : "?",
        route.battleToken,
        static_cast<unsigned>(route.transition & 0xFFu));
    FfxHooks::F7_BeginExplicitLaunchCapture();
    __try {
        ret = reinterpret_cast<FnBattleRequest781D60>(g_base + RVA_BATTLE_REQUEST_781D60)(
            static_cast<int>(route.battleToken),
            static_cast<char>(1),
            transition);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        err = GetExceptionCode();
        ok = false;
    }
    FfxHooks::F7_EndExplicitLaunchCapture();

    ArenaPlus_LogBattleQueueState("direct-request post", &route, dark);
    if (outRet) *outRet = ret;
    if (outErr) *outErr = err;
    const bool queueArmed = ArenaPlus_IsBattleQueueArmed();
    if (outQueueArmed) *outQueueArmed = queueArmed;
    Log("[ffx-hooks] ArenaPlus: direct request 781D60 result row=%d battleId=%s -> ok=%d ret=0x%08X %s err=0x%08X queueArmed=%d\n",
        dark,
        route.battleId ? route.battleId : "?",
        ok ? 1 : 0,
        static_cast<unsigned>(ret),
        LabForceRetMeaning(ret),
        err,
        queueArmed ? 1 : 0);
    const bool queued = ok && ret == -1 && queueArmed;
    if (queued) {
        FfxHooks::F7_CommitPendingBattleFieldRequest(
            fieldRequest, route.field, fieldSource);
    } else {
        FfxHooks::F7_CancelPendingBattleFieldRequest(fieldRequest);
    }
    return queued;
}

struct ArenaPlusUltraLaunchContext {
    int32_t returnValue = 0;
    uint32_t exceptionCode = 0;
    bool queueArmed = false;
};

static FfxHooks::CustomMixUltra::Runtime::CarrierQueueResult
ArenaPlus_UltraQueueCarrier(void* rawContext) noexcept {
    ArenaPlusUltraLaunchContext* context =
        static_cast<ArenaPlusUltraLaunchContext*>(rawContext);
    if (!context) return {};
    bool queued = false;
    try {
        queued = ArenaPlus_LaunchBattle781D60Request(
            kArenaPlusUltraCarrierRoute,
            -1,
            ArenaPlusDirectRequestAuthority::CustomMixUltraExactCarrier,
            FfxHooks::F7Difficulty::BattleFieldSource::Ultra,
            &context->returnValue,
            &context->exceptionCode,
            &context->queueArmed);
    } catch (...) {
        queued = false;
    }
    return FfxHooks::CustomMixUltra::Runtime::CarrierQueueResult{
        queued, context->returnValue, context->queueArmed};
}

static bool ArenaPlus_UltraArmRequest(
    void*,
    const FfxHooks::CustomMixUltra::SelectionInput& selection,
    uint64_t nowTick) noexcept {
    return ArenaPlus_MixEnabled() &&
        FfxHooks::ArenaMix::CanLaunch(selection, ArenaPlus_MixRules()) &&
        FfxHooks::CustomMixUltra::Runtime::ProductionArmSelection(selection, nowTick);
}

static void ArenaPlus_UltraCancelRequest(
    void*, FfxHooks::CustomMixUltra::Runtime::CancelReason reason) noexcept {
    FfxHooks::CustomMixUltra::Runtime::ProductionCancel(reason);
}

static FfxHooks::CustomMixUltra::Runtime::EditorLaunchOutcome
ArenaPlus_Ultra_LaunchFromPump() {
    using namespace FfxHooks::CustomMixUltra::Runtime;
    EditorLaunchOutcome rejected{};
    rejected.selection = g_arenaPlusUltraSelection;
    if (!ArenaPlus_MixEnabled() || !FfxHooks::ArenaMix::CanLaunch(
            g_arenaPlusUltraSelection, ArenaPlus_MixRules())) return rejected;
    const uint32_t cost = ArenaPlus_MixEntryCost();
    uint32_t gilBefore = 0;
    if (!ArenaPlus_CheckGilForLaunch(-1, cost, &gilBefore)) return rejected;
    const int selectedMusic=static_cast<int>(g_arenaPlusUltraSelection.musicTrack);
    const bool musicArmed = ArenaPlus_MusicEnabled() && g_musicHookArmed &&
        ArenaPlus_ArmMusicOverrideTrack(-1, selectedMusic, "mix-prelaunch");
    if (musicArmed)
        FfxHooks::SetArenaBattleMusicPending(selectedMusic, ArenaPlus_MusicFadeFrames());
    ArenaPlusUltraLaunchContext context{};
    const FfxHooks::CustomMixUltra::Runtime::LaunchIo io{
        &context,
        &ArenaPlus_UltraQueueCarrier,
        &ArenaPlus_UltraArmRequest,
        &ArenaPlus_UltraCancelRequest,
        [](void*,const FfxHooks::CustomMixUltra::SelectionInput& selection){
            return FfxHooks::CustomMixUltra::Runtime::ProductionPrepareSelection(selection);
        },
    };
    const auto outcome = LaunchEditorSelection(g_arenaPlusUltraSelection, GetTickCount64(), io);
    if (outcome.launch.requestArmed) {
        ArenaPlus_ChargeGilAfterLaunch(-1, cost, gilBefore);
    } else if (musicArmed) {
        FfxHooks::ClearArenaBattleMusicPending();
        ArenaPlus_ClearMusicOverride(-1, "mix-launch-failed");
    }
    return outcome;
}

static bool ArenaPlus_LaunchBattle7002Template(
    const ArenaPlusBossRoute& route, int dark,
    FfxHooks::F7Difficulty::BattleFieldSource fieldSource,
    int32_t* outRet, uint32_t* outErr) {
    if (outRet) *outRet = 0;
    if (outErr) *outErr = 0;
    if (!g_base || route.battleToken == 0 || !ArenaPlus_TemplateReplayEnabled()) return false;
    if (ArenaPlus_ChargeGilEnabled()) {
        Log("[ffx-hooks] ArenaPlus: template replay skipped because gil charge is enabled row=%d name=%s\n",
            dark,
            (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?");
        return false;
    }
    if (InterlockedCompareExchange(&g_arenaPlusBattle7002TemplateReady, 1, 1) != 1) {
        Log("[ffx-hooks] ArenaPlus: template replay unavailable row=%d name=%s (no Battle.7002 template captured yet)\n",
            dark,
            (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?");
        return false;
    }

    const uint32_t ctx = static_cast<uint32_t>(
        InterlockedCompareExchange(&g_arenaPlusBattle7002TemplateCtx, 0, 0));
    const uint32_t a2 = static_cast<uint32_t>(
        InterlockedCompareExchange(&g_arenaPlusBattle7002TemplateA2, 0, 0));
    const uint32_t tick = static_cast<uint32_t>(
        InterlockedCompareExchange(&g_arenaPlusBattle7002TemplateTick, 0, 0));
    const uint32_t now = GetTickCount();
    const uint32_t ageMs = now - tick;
    const uint32_t maxAgeMs = ArenaPlus_TemplateReplayMaxAgeMs();
    if (ctx == 0 || a2 == 0 || tick == 0 || ageMs > maxAgeMs) {
        Log("[ffx-hooks] ArenaPlus: template replay stale/invalid row=%d name=%s ctx=0x%08X a2=0x%08X ageMs=%u maxAgeMs=%u\n",
            dark,
            (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
            ctx,
            a2,
            ageMs,
            maxAgeMs);
        InterlockedExchange(&g_arenaPlusBattle7002TemplateReady, 0);
        return false;
    }

    uint32_t stack[8] = {};
    for (int i = 0; i < 8; ++i) stack[i] = g_arenaPlusBattle7002TemplateStack[i];
    if (stack[0] == 0 || stack[1] == 0) {
        Log("[ffx-hooks] ArenaPlus: template replay stack invalid row=%d name=%s stack0=0x%08X stack1=0x%08X\n",
            dark,
            (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
            stack[0],
            stack[1]);
        InterlockedExchange(&g_arenaPlusBattle7002TemplateReady, 0);
        return false;
    }

    const uint32_t old0 = stack[0];
    const uint32_t old1 = stack[1];
    stack[0] = ArenaPlus_UseEventTransition() ? route.transition : old0;
    stack[1] = route.battleToken;

    typedef int (__cdecl* FnBattleLaunch7002)(uint32_t, uint32_t, uint32_t);
    int32_t ret = 0;
    uint32_t err = 0;
    bool ok = false;
    const FfxHooks::F7Difficulty::BattleFieldRequest fieldRequest =
        FfxHooks::F7_BeginPendingBattleFieldRequest();
    Log("[ffx-hooks] ArenaPlus: template replay try row=%d name=%s battleId=%s ctx=0x%08X a2=0x%08X ageMs=%u old=[%08X %08X] new=[%08X %08X]\n",
        dark,
        (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
        route.battleId ? route.battleId : "?",
        ctx,
        a2,
        ageMs,
        old0,
        old1,
        stack[0],
        stack[1]);

    ArenaPlus_LogBattleQueueState("template-replay pre", &route, dark);
    if (ArenaPlus_PrepareBattleFlagsEnabled()) {
        ArenaPlus_CallCommonSetBattleFlags0200("template-replay prelaunch", route, dark);
        ArenaPlus_LogBattleQueueState("template-replay after-flags", &route, dark);
    }

    FfxHooks::F7_BeginExplicitLaunchCapture();
    __try {
        ret = reinterpret_cast<FnBattleLaunch7002>(g_base + RVA_BATTLE_LAUNCH_7002)(
            ctx,
            a2,
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&stack[0])));
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        err = GetExceptionCode();
        ok = false;
    }
    FfxHooks::F7_EndExplicitLaunchCapture();

    ArenaPlus_LogBattleQueueState("template-replay post", &route, dark);
    if (outRet) *outRet = ret;
    if (outErr) *outErr = err;
    Log("[ffx-hooks] ArenaPlus: template replay result row=%d battleId=%s -> ok=%d ret=0x%08X %s err=0x%08X claimSuccess=%d\n",
        dark,
        route.battleId ? route.battleId : "?",
        ok ? 1 : 0,
        static_cast<unsigned>(ret),
        LabForceRetMeaning(ret),
        err,
        ArenaPlus_TemplateReplayClaimSuccessEnabled() ? 1 : 0);
    const bool queued = ok && ret == -1 && ArenaPlus_TemplateReplayClaimSuccessEnabled();
    if (queued) {
        FfxHooks::F7_CommitPendingBattleFieldRequest(
            fieldRequest, route.field, fieldSource);
    } else {
        FfxHooks::F7_CancelPendingBattleFieldRequest(fieldRequest);
    }
    return queued;
}

static void ArenaPlus_ClearPendingBattle7002Override() {
    InterlockedExchange(&g_arenaPlusPendingBattle7002, 0);
    InterlockedExchange(&g_arenaPlusPendingBattleToken, 0);
    InterlockedExchange(&g_arenaPlusPendingTransition, 0);
    InterlockedExchange(&g_arenaPlusPendingDark, -1);
    InterlockedExchange(&g_arenaPlusPendingGilCost, 0);
    InterlockedExchange(&g_arenaPlusPendingExpireTick, 0);
    InterlockedExchange(&g_arenaPlusPendingDifficultyField, -1);
    InterlockedExchange(
        &g_arenaPlusPendingDifficultySource,
        static_cast<LONG>(FfxHooks::F7Difficulty::BattleFieldSource::Missing));
}

static bool ArenaPlus_ArmBattle7002Override(
    const ArenaPlusBossRoute& route,
    int dark,
    uint32_t expireMs,
    FfxHooks::F7Difficulty::BattleFieldSource fieldSource) {
    if (route.battleToken == 0) return false;
    const int gilCost = ArenaPlus_BossGilCost(dark);
    uint32_t expireTick = 0;
    if (expireMs > 0) {
        expireTick = GetTickCount() + expireMs;
        if (expireTick == 0) expireTick = 1;
    }
    InterlockedExchange(&g_arenaPlusPendingBattleToken, static_cast<LONG>(route.battleToken));
    InterlockedExchange(&g_arenaPlusPendingTransition, static_cast<LONG>(route.transition));
    InterlockedExchange(&g_arenaPlusPendingDark, static_cast<LONG>(dark));
    InterlockedExchange(&g_arenaPlusPendingGilCost, static_cast<LONG>(gilCost));
    InterlockedExchange(&g_arenaPlusPendingExpireTick, static_cast<LONG>(expireTick));
    InterlockedExchange(&g_arenaPlusPendingDifficultyField, static_cast<LONG>(route.field));
    InterlockedExchange(&g_arenaPlusPendingDifficultySource, static_cast<LONG>(fieldSource));
    InterlockedExchange(&g_arenaPlusPendingBattle7002, 1);
    Log("[ffx-hooks] ArenaPlus: armed Battle.7002 override row=%d name=%s battleId=%s token=0x%08X transition=%u gilCost=%d expireMs=%u mode=next-vanilla-launch\n",
        dark,
        (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
        route.battleId ? route.battleId : "?",
        route.battleToken,
        route.transition,
        gilCost,
        expireMs);
    return true;
}

static bool ArenaPlus_LaunchSafeBattleFromPump() {
    int field = 0;
    int group = 0;
    int formation = EnvInt("FFXHOOKS_ARENAPLUS_FORMATION", 0);
    char source[32] = {};
    bool routeOk = false;

    if (!EnvFlagEnabled("FFXHOOKS_ARENAPLUS_PRESET_BATTLE")) {
        routeOk = ArenaPlus_ReadCurrentEncounterRoute(&field, &group, source, sizeof(source));
    }
    if (!routeOk) {
        field = EnvInt("FFXHOOKS_ARENAPLUS_FIELD", 2);
        group = EnvInt("FFXHOOKS_ARENAPLUS_GROUP", 0);
        formation = EnvInt("FFXHOOKS_ARENAPLUS_FORMATION", formation);
        _snprintf_s(source, sizeof(source), _TRUNCATE, "preset");
    }

    int32_t ret = 0;
    uint32_t err = 0;
    const bool ok = ArenaPlus_ForceBattleDirect(
        field, group, formation, FfxHooks::F7Difficulty::BattleFieldSource::Arena,
        &ret, &err);
    Log("[ffx-hooks] ArenaPlus: launch safe battle route=%s field=%d group=%d formation=%d -> ok=%d ret=%d %s err=0x%08X\n",
        source, field, group, formation, ok ? 1 : 0, ret, LabForceRetMeaning(ret), err);
    return ok && ret == -1;
}

static bool ArenaPlus_LaunchBossBattleFromPump(int dark) {
    if (!ArenaPlus_BossRouteMapped(dark)) {
        Log("[ffx-hooks] ArenaPlus: boss route missing row=%d name=%s battleId=%s evidence=%s\n",
            dark,
            (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
            (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? ArenaPlus_GetRoute(dark).battleId : "?",
            (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? ArenaPlus_GetRoute(dark).evidence : "?");
        return false;
    }

    const ArenaPlusBossRoute& route = ArenaPlus_GetRoute(dark);
    const int gilCost = ArenaPlus_BossGilCost(dark);
    const int musicTrack = ArenaPlus_BossMusicTrack(dark);
    uint32_t gilBefore = 0;
    if (gilCost > 0 && !ArenaPlus_CheckGilForLaunch(dark, static_cast<uint32_t>(gilCost), &gilBefore)) {
        return false;
    }

    Log("[ffx-hooks] ArenaPlus: launch boss row=%d name=%s battleId=%s token=0x%08X transition=%u legacyField=%d legacyGroup=%d legacyFormation=%d lab=%d chargeGil=%d gilCost=%d music=%d flagOk=%d flag=%u evidence=%s\n",
        dark,
        kArenaPlusDarkNames[dark],
        route.battleId ? route.battleId : "?",
        route.battleToken,
        route.transition,
        route.field,
        route.group,
        route.formation,
        ArenaPlus_LabRoutesEnabled() ? 1 : 0,
        ArenaPlus_ChargeGilEnabled() ? 1 : 0,
        gilCost,
        musicTrack,
        g_arenaPlusDarkReadOk[dark] ? 1 : 0,
        static_cast<unsigned>(g_arenaPlusDarkValues[dark]),
        route.evidence ? route.evidence : "?");

    const bool musicArmed = false;

    if (musicTrack >= 0 && ArenaPlus_MusicEnabled() && g_musicHookArmed) {
        FfxHooks::SetArenaBattleMusicPending(musicTrack, ArenaPlus_MusicFadeFrames());
        Log("[ffx-hooks] ArenaPlus: music battle-theme intercept armed row=%d name=%s track=%d%s%s fadeFrames=%d\n",
            dark,
            kArenaPlusDarkNames[dark],
            musicTrack,
            LabMusicRuntimeName(musicTrack) ? " " : "",
            LabMusicRuntimeName(musicTrack) ? LabMusicRuntimeName(musicTrack) : "",
            ArenaPlus_MusicFadeFrames());
    }

    int32_t ret = 0;
    uint32_t err = 0;
    const bool directRequestOk = ArenaPlus_LaunchBattle781D60Request(
        route, dark, ArenaPlusDirectRequestAuthority::LegacyExperimental,
        FfxHooks::F7Difficulty::BattleFieldSource::Arena, &ret, &err, nullptr);
    if (directRequestOk) {
        ArenaPlus_ChargeGilAfterLaunch(dark, static_cast<uint32_t>(gilCost), gilBefore);
        if (musicTrack >= 0 && ArenaPlus_MusicEnabled()) {
            ArenaPlus_ScheduleMusicSoundCmd(dark, musicTrack, "direct-request-postlaunch-fallback");
        }
        return true;
    }

    if (musicTrack >= 0 && ArenaPlus_MusicEnabled()) {
        FfxHooks::ClearArenaBattleMusicPending();
    }

    if (musicArmed) ArenaPlus_ClearMusicOverride(dark, "direct-request-failed");

    const bool templateOk = ArenaPlus_LaunchBattle7002Template(
        route, dark, FfxHooks::F7Difficulty::BattleFieldSource::Arena, &ret, &err);
    if (templateOk) return true;

    if (!ArenaPlus_DirectBattle7002Enabled()) {
        const bool autoCarrier = ArenaPlus_AutoCarrierEnabled();
        const bool unprovenDirectAttempt = (ret == -1 && err == 0) &&
            (ArenaPlus_DirectRequest781D60Enabled() || ArenaPlus_TemplateReplayEnabled());
        const uint32_t ttlMs = autoCarrier
            ? ArenaPlus_AutoCarrierTtlMs()
            : (unprovenDirectAttempt ? ArenaPlus_UnprovenDirectFallbackTtlMs() : 0);
        const bool armed = ArenaPlus_ArmBattle7002Override(
            route, dark, ttlMs, FfxHooks::F7Difficulty::BattleFieldSource::Arena);
        if (!armed) {
            if (musicArmed) ArenaPlus_ClearMusicOverride(dark, "pending-override-failed");
            return false;
        }
        if (!autoCarrier) return true;

        const bool carrierQueued = ArenaPlus_LaunchSafeBattleFromPump();
        Log("[ffx-hooks] ArenaPlus: auto-carrier %s row=%d name=%s ttlMs=%u note=uses RT2 Current Battle as Battle.7002 carrier candidate\n",
            carrierQueued ? "queued" : "FAILED",
            dark,
            kArenaPlusDarkNames[dark],
            ttlMs);
        if (!carrierQueued) {
            ArenaPlus_ClearPendingBattle7002Override();
            if (musicArmed) ArenaPlus_ClearMusicOverride(dark, "auto-carrier-failed");
        }
        return carrierQueued;
    }

    ret = 0;
    err = 0;
    const bool exactOk = ArenaPlus_LaunchBattle7002Exact(
        route, dark, FfxHooks::F7Difficulty::BattleFieldSource::Arena, &ret, &err);
    if (exactOk) return true;
    if (!ArenaPlus_AllowLegacyBossFallback()) {
        Log("[ffx-hooks] ArenaPlus: exact token launch failed; legacy fallback disabled battleId=%s token=0x%08X ret=0x%08X err=0x%08X\n",
            route.battleId ? route.battleId : "?",
            route.battleToken,
            static_cast<unsigned>(ret),
            err);
        if (musicArmed) ArenaPlus_ClearMusicOverride(dark, "launch-failed-no-fallback");
        return false;
    }

    ret = 0;
    err = 0;
    const bool ok = ArenaPlus_ForceBattleDirect(
        route.field, route.group, route.formation,
        FfxHooks::F7Difficulty::BattleFieldSource::Arena, &ret, &err);
    Log("[ffx-hooks] ArenaPlus: legacy boss fallback result battleId=%s field=%d group=%d formation=%d -> ok=%d ret=%d %s err=0x%08X\n",
        route.battleId ? route.battleId : "?",
        route.field,
        route.group,
        route.formation,
        ok ? 1 : 0,
        ret,
        LabForceRetMeaning(ret),
        err);
    const bool legacyQueued = ok && ret == -1;
    if (!legacyQueued && musicArmed) ArenaPlus_ClearMusicOverride(dark, "legacy-fallback-failed");
    return legacyQueued;
}

static bool ArenaPlus_LaunchComboBattleFromPump(int combo) {
    if (ArenaPlusComposePick_IsCustomMixCombo(combo) &&
        !ArenaPlusComposePick_IsAvailable()) {
        // KEY: callers other than the visible picker also converge here. Reject before the preset
        // fallback can launch a vanilla carrier under a row labelled as a composed Custom Mix.
        Log("[ffx-hooks] ArenaPlus: Custom Mix launch rejected (compose transactions quarantined)\n");
        return false;
    }
    if (!ArenaPlus_ComboRouteMapped(combo)) {
        Log("[ffx-hooks] ArenaPlus: combo route missing row=%d name=%s\n",
            combo,
            (combo >= 0 && combo < ARENA_PLUS_COMBO_COUNT) ? kArenaPlusComboNames[combo] : "?");
        return false;
    }
    if (!ArenaPlus_ComboBattlesEnabled()) {
        Log("[ffx-hooks] ArenaPlus: combo battles disabled row=%d name=%s (need arena_plus_combo_battles.flag or lab routes)\n",
            combo,
            kArenaPlusComboNames[combo]);
        return false;
    }

    if (!ArenaPlus_ComboRouteAllowed(combo)) {
        Log("[ffx-hooks] ArenaPlus: gauntlet progression requirement not met row=%d\n", combo);
        return false;
    }

    const ArenaPlusBossRoute baseRoute = ArenaPlus_GetComboRoute(combo);
    ArenaPlusBossRoute route = baseRoute;
    int scenarioField = 0;
    int scenarioGroup = 0;
    int scenarioFormation = 0;
    int scenarioBattlefieldId = 0;
    char scenarioBackdropBattleId[32] = {};
    const bool scenarioRouteApplied = ArenaPlusComposePick_ApplyLaunchRouteOverride(
        combo,
        &scenarioField,
        &scenarioGroup,
        &scenarioFormation,
        scenarioBackdropBattleId,
        static_cast<int>(sizeof(scenarioBackdropBattleId)),
        &scenarioBattlefieldId);
    const bool scenarioCrossMap = scenarioRouteApplied && scenarioBattlefieldId > 0 &&
        ArenaPlus_IsScenarioCrossMap(combo, scenarioField);
    const bool scenarioFieldPatch = scenarioRouteApplied && scenarioBattlefieldId > 0 &&
        ArenaPlus_ScenarioRuntimeBackdropEnabled() &&
        ArenaPlus_ShouldPatchScenarioFieldIdx(combo, scenarioField);
    const bool scenarioCrossMapBackdrop = scenarioRouteApplied && scenarioBattlefieldId > 0 &&
        ArenaPlus_ShouldApplyScenarioCrossMapBackdrop(combo, scenarioField);
    const bool scenarioVisualBackdrop = scenarioFieldPatch || scenarioCrossMapBackdrop;
    /* Deferred restore for the composed bin (non-cross-map path): after about 30 seconds, restore
       the canonical file from .bak so random encounters cannot inherit Dark Aeons. This mirrors
       the 781D60 path's cleanup contract (2026-08-02). */
    if (scenarioRouteApplied && scenarioBackdropBattleId[0] && !scenarioCrossMap) {
        ArenaPlus_ArmDeferredFileRestore(1800);
    }
    if (scenarioRouteApplied && scenarioBattlefieldId > 0 && !scenarioVisualBackdrop && !scenarioCrossMap) {
        Log("[ffx-hooks] ArenaPlus: scenario compose template=%s bf=%d (no runtime backdrop â€” cavern/carrier match)\n",
            scenarioBackdropBattleId[0] ? scenarioBackdropBattleId : "?",
            scenarioBattlefieldId);
    }
    if (scenarioCrossMap) {
        Log("[ffx-hooks] ArenaPlus: scenario 781D60 launch deploy=%s fieldIdx=%d G/F=%d/%d bf=%d (not carrier %s)\n",
            scenarioBackdropBattleId[0] ? scenarioBackdropBattleId : "?",
            scenarioField,
            scenarioGroup,
            scenarioFormation,
            scenarioBattlefieldId,
            route.battleId ? route.battleId : "?");
    }
    if (scenarioFieldPatch) {
        Log("[ffx-hooks] ArenaPlus: scenario backdrop field+bf patch (opt-in flag; chunk0 from compose template)\n");
    }
    const int gilCost = ArenaPlus_ComboGilCost(combo);
    const int musicTrack = ARENA_PLUS_MUSIC_TRACK_DEFAULT;
    const int comboRowId = ARENA_PLUS_ROW_FIRST_COMBO + combo;
    uint32_t gilBefore = 0;
    if (gilCost > 0) {
        uint32_t gilStatus = 0;
        uint32_t gilErr = 0;
        if (!ArenaPlus_ReadGil(&gilBefore, &gilStatus, &gilErr) || gilStatus != FFXPROBE_ST_OK) {
            Log("[ffx-hooks] ArenaPlus: combo gil read failed row=%d name=%s cost=%u\n",
                combo, kArenaPlusComboNames[combo], gilCost);
            return false;
        }
        if (gilBefore < static_cast<uint32_t>(gilCost)) {
            Log("[ffx-hooks] ArenaPlus: combo insufficient gil row=%d name=%s cost=%u current=%u\n",
                combo, kArenaPlusComboNames[combo], gilCost, gilBefore);
            return false;
        }
    }

    Log("[ffx-hooks] ArenaPlus: launch combo row=%d menuRow=%d name=%s battleId=%s token=0x%08X transition=%u carrierFGF=%d/%d/%d scenarioBackdrop=%d bf=%d gilCost=%d evidence=%s\n",
        combo,
        comboRowId,
        kArenaPlusComboNames[combo],
        route.battleId ? route.battleId : "?",
        route.battleToken,
        route.transition,
        route.field,
        route.group,
        route.formation,
        scenarioVisualBackdrop ? scenarioField : -1,
        scenarioVisualBackdrop ? scenarioBattlefieldId : 0,
        gilCost,
        route.evidence ? route.evidence : "?");

    /* FIX 2026-08-05: CustomMix musiic — set g_block->musicOverrideTrackIndex BEFORE launch.
     * SetArenaBattleMusicPending (Arena+ interceptor) requires ArenaPlus_MusicEnabled() + g_musicHookArmed,
     * which may be false. The MusicLock (g_block->musicOverrideTrackIndex) works with ALL interceptors
     * (PlayTrack, SwitchCrossfade, PlayTrackWithPreload) and is consumed by ConsumeOverride() on the
     * FIRST PlayTrack call during battle transition — BEFORE InitScene_Shim fires. */
    if (musicTrack >= 0 && g_block && g_musicHookArmed) {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_block->musicOverrideTrackIndex),
                            static_cast<LONG>(musicTrack));
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_block->musicSeq));
        Log("[ffx-hooks] ArenaPlus: custom mix music lock armed pre-launch track=%d seq=%ld\n",
            musicTrack,
            InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&g_block->musicSeq), 0, 0));
    }
    if (musicTrack >= 0 && ArenaPlus_MusicEnabled() && g_musicHookArmed) {
        FfxHooks::SetArenaBattleMusicPending(musicTrack, ArenaPlus_MusicFadeFrames());
    }

    int32_t ret = 0;
    uint32_t err = 0;

    if (scenarioCrossMap) {
        ArenaPlus_ArmScenarioBackdropPending(-1, 0);
        if (!scenarioBackdropBattleId[0]) {
            if (musicTrack >= 0 && ArenaPlus_MusicEnabled()) {
                FfxHooks::ClearArenaBattleMusicPending();
            }
            Log("[ffx-hooks] ArenaPlus: scenario cross-map missing deploy battle id\n");
            return false;
        }
        ArenaPlusBossRoute scenarioLaunchRoute = {};
        if (!ArenaPlus_BuildScenarioLaunchRoute(
                scenarioBackdropBattleId,
                scenarioGroup,
                scenarioFormation,
                &scenarioLaunchRoute)) {
            if (musicTrack >= 0 && ArenaPlus_MusicEnabled()) {
                FfxHooks::ClearArenaBattleMusicPending();
            }
            Log("[ffx-hooks] ArenaPlus: scenario cross-map unknown deploy id '%s'\n",
                scenarioBackdropBattleId);
            return false;
        }
        Log("[ffx-hooks] ArenaPlus: scenario 781D60 token=0x%08X table=%d deploy=%s\n",
            scenarioLaunchRoute.battleToken,
            scenarioLaunchRoute.field,
            scenarioBackdropBattleId);
        /* BUGFIX 2026-08-05 (Jarvis-HOOK): Pin the CARRIER battleId (nagi05_23) not the
         * scenario backdrop (bika02_01) so the engine loads the composed carrier bin
         * instead of the vanilla scenario bin. scenarioBackdropBattleId is used only
         * for the 781D60 token route (ArenaPlus_BuildScenarioLaunchRoute). */
        ArenaPlus_ArmEncounterPinByName(route.battleId, scenarioGroup, scenarioFormation);
        const bool directRequestOk = ArenaPlus_LaunchBattle781D60Request(
            scenarioLaunchRoute,
            comboRowId,
            ArenaPlusDirectRequestAuthority::LegacyExperimental,
            FfxHooks::F7Difficulty::BattleFieldSource::CustomMix,
            &ret,
            &err,
            nullptr);
        if (directRequestOk) {
            ArenaPlus_PinEncounterName(route.battleId, false);
            /* Clear pin timer + queue G/F after scenario launch to avoid stale
               carrier G/F leaking into random encounters on the same field. */
            g_pendingEncounterPinFrames = 0;
            g_pendingEncounterPinGroup = -1;
            g_pendingEncounterPinFormation = -1;
            if (g_base) {
                __try {
                    *reinterpret_cast<volatile uint8_t*>(g_base + RVA_BATTLE_QUEUE_GROUP) = 0;
                    *reinterpret_cast<volatile uint8_t*>(g_base + RVA_BATTLE_QUEUE_FORMATION) = 0;
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            if (gilCost > 0) {
                const uint32_t gilAfter = gilBefore - static_cast<uint32_t>(gilCost);
                uint32_t gilStatus = 0;
                uint32_t gilErr = 0;
                const bool chargeOk = ArenaPlus_WriteGil(gilAfter, &gilStatus, &gilErr);
                Log("[ffx-hooks] ArenaPlus: combo gil charge %s row=%d cost=%u gil=%u->%u\n",
                    (chargeOk && gilStatus == FFXPROBE_ST_OK) ? "applied" : "FAILED",
                    combo, gilCost, gilBefore, gilAfter);
            }
            if (musicTrack >= 0 && ArenaPlus_MusicEnabled()) {
                ArenaPlus_ScheduleMusicSoundCmd(comboRowId, musicTrack, "combo-scenario-781d60");
            }
            /* Arm deferred restore: after ~30s, restore the scene bin from .spiraforge.bak
               so random encounters don't find Dark Aeons in place of normal fiends. */
            ArenaPlus_ArmDeferredFileRestore(1800); /* ~30s at 60fps */
            return true;
        }
        if (musicTrack >= 0 && ArenaPlus_MusicEnabled()) {
            FfxHooks::ClearArenaBattleMusicPending();
        }
        Log("[ffx-hooks] ArenaPlus: scenario 781D60 launch failed deploy=%s token=0x%08X\n",
            scenarioBackdropBattleId,
            scenarioLaunchRoute.battleToken);
        return false;
    }

    if (scenarioVisualBackdrop) {
        ArenaPlus_ArmScenarioBackdropPending(scenarioField, static_cast<uint16_t>(scenarioBattlefieldId));
        ArenaPlus_ApplyScenarioBackdropVisualOnly(scenarioField, static_cast<uint16_t>(scenarioBattlefieldId), false);
    } else {
        ArenaPlus_ArmScenarioBackdropPending(-1, 0);
    }
    g_pendingEncounterPinFrames = 0;
    g_pendingEncounterPinName[0] = '\0';
    g_pendingEncounterPinGroup = -1;
    g_pendingEncounterPinFormation = -1;
    /* Clear the queue G/F bytes so stale carrier values don't leak into random encounters
       on the same field (e.g. Macalania Lake field=340 + carrier mcyt00_22 field=340). */
    if (g_base) {
        __try {
            *reinterpret_cast<volatile uint8_t*>(g_base + RVA_BATTLE_QUEUE_GROUP) = 0;
            *reinterpret_cast<volatile uint8_t*>(g_base + RVA_BATTLE_QUEUE_FORMATION) = 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    const bool directRequestOk = ArenaPlus_LaunchBattle781D60Request(
        route, comboRowId, ArenaPlusDirectRequestAuthority::LegacyExperimental,
        FfxHooks::F7Difficulty::BattleFieldSource::CustomMix,
        &ret, &err, nullptr);
    if (directRequestOk) {
        if (scenarioVisualBackdrop)
            ArenaPlus_ApplyScenarioBackdropVisualOnly(scenarioField, static_cast<uint16_t>(scenarioBattlefieldId), false);
        if (gilCost > 0) {
            const uint32_t gilAfter = gilBefore - static_cast<uint32_t>(gilCost);
            uint32_t gilStatus = 0;
            uint32_t gilErr = 0;
            const bool chargeOk = ArenaPlus_WriteGil(gilAfter, &gilStatus, &gilErr);
            Log("[ffx-hooks] ArenaPlus: combo gil charge %s row=%d cost=%u gil=%u->%u\n",
                (chargeOk && gilStatus == FFXPROBE_ST_OK) ? "applied" : "FAILED",
                combo, gilCost, gilBefore, gilAfter);
        }
        if (musicTrack >= 0 && ArenaPlus_MusicEnabled()) {
            ArenaPlus_ScheduleMusicSoundCmd(comboRowId, musicTrack, "combo-direct-request-postlaunch");
        }
        return true;
    }

    if (musicTrack >= 0 && ArenaPlus_MusicEnabled()) {
        FfxHooks::ClearArenaBattleMusicPending();
    }

    const bool templateOk = ArenaPlus_LaunchBattle7002Template(
        route, comboRowId, FfxHooks::F7Difficulty::BattleFieldSource::CustomMix,
        &ret, &err);
    if (templateOk) {
        if (gilCost > 0) {
            const uint32_t gilAfter = gilBefore - static_cast<uint32_t>(gilCost);
            uint32_t gilStatus = 0;
            uint32_t gilErr = 0;
            ArenaPlus_WriteGil(gilAfter, &gilStatus, &gilErr);
        }
        return true;
    }

    if (!ArenaPlus_DirectBattle7002Enabled()) {
        const bool autoCarrier = ArenaPlus_AutoCarrierEnabled();
        const bool unprovenDirectAttempt = (ret == -1 && err == 0) &&
            (ArenaPlus_DirectRequest781D60Enabled() || ArenaPlus_TemplateReplayEnabled());
        const uint32_t ttlMs = autoCarrier
            ? ArenaPlus_AutoCarrierTtlMs()
            : (unprovenDirectAttempt ? ArenaPlus_UnprovenDirectFallbackTtlMs() : 0);
        const bool armed = ArenaPlus_ArmBattle7002Override(
            route, comboRowId, ttlMs, FfxHooks::F7Difficulty::BattleFieldSource::CustomMix);
        if (!armed) return false;
        if (!autoCarrier) return true;

        const bool carrierQueued = ArenaPlus_LaunchSafeBattleFromPump();
        Log("[ffx-hooks] ArenaPlus: combo auto-carrier %s row=%d name=%s ttlMs=%u\n",
            carrierQueued ? "queued" : "FAILED",
            combo,
            kArenaPlusComboNames[combo],
            ttlMs);
        if (!carrierQueued) ArenaPlus_ClearPendingBattle7002Override();
        return carrierQueued;
    }

    ret = 0;
    err = 0;
    const bool exactOk = ArenaPlus_LaunchBattle7002Exact(
        route, comboRowId, FfxHooks::F7Difficulty::BattleFieldSource::CustomMix,
        &ret, &err);
    if (exactOk) {
        if (gilCost > 0) {
            const uint32_t gilAfter = gilBefore - static_cast<uint32_t>(gilCost);
            uint32_t gilStatus = 0;
            uint32_t gilErr = 0;
            ArenaPlus_WriteGil(gilAfter, &gilStatus, &gilErr);
        }
        return true;
    }

    if (!ArenaPlus_AllowLegacyBossFallback()) {
        Log("[ffx-hooks] ArenaPlus: combo token launch failed; legacy FGF disabled battleId=%s token=0x%08X\n",
            route.battleId ? route.battleId : "?",
            route.battleToken);
        return false;
    }

    if (scenarioRouteApplied) {
        route.field = scenarioField;
        route.group = scenarioGroup;
        route.formation = scenarioFormation;
    }

    ret = 0;
    err = 0;
    const bool ok = ArenaPlus_ForceBattleDirect(
        route.field, route.group, route.formation,
        FfxHooks::F7Difficulty::BattleFieldSource::CustomMix, &ret, &err);
    Log("[ffx-hooks] ArenaPlus: combo legacy FGF fallback row=%d battleId=%s field=%d group=%d formation=%d -> ok=%d ret=%d %s err=0x%08X\n",
        combo,
        route.battleId ? route.battleId : "?",
        route.field,
        route.group,
        route.formation,
        ok ? 1 : 0,
        ret,
        LabForceRetMeaning(ret),
        err);
    if (!(ok && ret == -1)) return false;

    if (gilCost > 0) {
        const uint32_t gilAfter = gilBefore - static_cast<uint32_t>(gilCost);
        uint32_t gilStatus = 0;
        uint32_t gilErr = 0;
        ArenaPlus_WriteGil(gilAfter, &gilStatus, &gilErr);
    }
    if (musicTrack >= 0 && ArenaPlus_MusicEnabled()) {
        ArenaPlus_ScheduleMusicSoundCmd(comboRowId, musicTrack, "combo-fgf-fallback");
    }
    return true;
}

static bool ArenaPlus_OpenMenuFromRequest() {
    if (!ArenaPlus_IsEnabled()) return false;
    if (g_arenaPlusMenu.obj) ArenaPlus_CloseMenu(g_arenaPlusMenu);
    g_arenaPlusMenu = ArenaPlus_SpawnMenu();
    Log("[ffx-hooks] ArenaPlus: native menu open obj=0x%08X darkMask=0x%03X cooldown=%d\n",
        static_cast<unsigned>(g_arenaPlusMenu.obj), g_arenaPlusDarkMask, static_cast<int>(g_arenaPlusInputCooldown));
    return g_arenaPlusMenu.obj != 0;
}

static void ArenaPlus_RequestOpen(const char* source) {
    Log("[ffx-hooks] ArenaPlus_RequestOpen source=%s (native UI request; snapshot read-only)\n",
        source ? source : "?");
    ArenaPlus_LogFlagsSummary();
    if (source && strstr(source, "Npc") != nullptr) {
        InterlockedExchange(&g_forceSubsystem, 1);
    }
    InterlockedExchange(&g_arenaPlusWantOpen, 1);
}

/* Arena+ NPC hook â€” Common.displayFieldChoice [013B] @ nagi0700 "Now what?" (string 0x4A).
 * Extends max choice index 4->5, best-effort appends "Arena+." to choice text, intercepts index 5. */
static bool ArenaTrace_ReadU32Abs(uintptr_t abs, uint32_t* out);
static bool ArenaTrace_WriteU32Abs(uintptr_t abs, uint32_t value);
static bool ArenaTrace_ReadU8Abs(uintptr_t abs, uint8_t* out);

static const uint32_t kArenaNpcNowWhatStringId = 0x4Au;
static const uint32_t kArenaNpcNowWhatVanillaMax = 4u;
static const uint32_t kArenaNpcArenaPlusIndex = 5u;
static const int kArenaNpcOpenDelayFrames = 30;

/* US event text bytes (FfxEncoding.us) */
static const unsigned char kUsChatDot[] = { 82, 116, 112, 116, 72 };
static const unsigned char kUsExitDot[] = { 84, 124, 105, 116, 72 };
static const unsigned char kUsArenaPlusDot[] = { 80, 116, 117, 116, 112, 69, 72 };
static const unsigned char kUsFightMonstersDot[] = {
    85, 105, 118, 116, 116, 58, 114, 113, 116, 116, 117, 116, 72
};

static volatile LONG g_arenaNpcChoiceIndex = -1; /* 5 = 6th row */
static volatile LONG g_arenaNpcScanDone = 0;     /* one-shot text scan per process */
static volatile LONG g_arenaNpcTextPatched = 0;  /* label append succeeded */

static int ArenaPlus_EncodedLabelLen(const unsigned char* enc) {
    if (!enc) return 0;
    int n = 0;
    while (enc[n] != 0 && n < 256) ++n;
    return n;
}

static bool ArenaPlus_MemReadable(const void* p, size_t len) {
    if (!p || len == 0) return false;
    __try {
        const volatile unsigned char* b = static_cast<const volatile unsigned char*>(p);
        (void)b[0];
        (void)b[len - 1];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool ArenaPlus_MemWrite(void* p, const void* src, size_t len) {
    if (!p || !src || len == 0) return false;
    __try {
        memcpy(p, src, len);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool ArenaPlus_FindEncodedNeedle(uintptr_t start, size_t span, const unsigned char* needle, uintptr_t* outHit) {
    if (!needle || !outHit || span == 0) return false;
    const int nlen = ArenaPlus_EncodedLabelLen(needle);
    if (nlen <= 0 || span < static_cast<size_t>(nlen)) return false;
    for (size_t off = 0; off + static_cast<size_t>(nlen) <= span; ++off) {
        const void* p = reinterpret_cast<const void*>(start + off);
        if (!ArenaPlus_MemReadable(p, static_cast<size_t>(nlen))) continue;
        if (memcmp(p, needle, static_cast<size_t>(nlen)) == 0) {
            *outHit = start + off;
            return true;
        }
    }
    return false;
}

static bool ArenaPlus_FindEncodedBytes(uintptr_t start, size_t span, const unsigned char* needle, int nlen, uintptr_t* outHit) {
    if (!needle || !outHit || nlen <= 0 || span == 0) return false;
    const size_t need = static_cast<size_t>(nlen);
    if (span < need) return false;

    uintptr_t cur = start;
    const uintptr_t end = start + span;
    while (cur + need <= end) {
        MEMORY_BASIC_INFORMATION mbi = {};
        if (!VirtualQuery(reinterpret_cast<LPCVOID>(cur), &mbi, sizeof(mbi))) break;
        const uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const uintptr_t regionEnd = regionBase + mbi.RegionSize;
        if (mbi.State != MEM_COMMIT ||
            (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
            (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                            PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0) {
            cur = regionEnd;
            continue;
        }

        uintptr_t scanStart = (cur > regionBase) ? cur : regionBase;
        uintptr_t scanEnd = (end < regionEnd) ? end : regionEnd;
        if (scanEnd < scanStart + need) {
            cur = regionEnd;
            continue;
        }
        for (uintptr_t hit = scanStart; hit + need <= scanEnd; ++hit) {
            if (memcmp(reinterpret_cast<const void*>(hit), needle, need) == 0) {
                *outHit = hit;
                return true;
            }
        }
        cur = regionEnd;
    }
    return false;
}

static bool ArenaPlus_NpcIsNowWhatStack(uint32_t argStack) {
    uint32_t stringId = 0;
    uint32_t maxIndex = 0;
    if (!ArenaTrace_ReadU32Abs(static_cast<uintptr_t>(argStack) + 8, &stringId)) return false;
    if (!ArenaTrace_ReadU32Abs(static_cast<uintptr_t>(argStack) + 16, &maxIndex)) return false;
    return stringId == kArenaNpcNowWhatStringId && maxIndex == kArenaNpcNowWhatVanillaMax;
}

static bool ArenaPlus_NpcValidateNowWhatExit(uintptr_t exitHit) {
    if (!exitHit) return false;
    const uintptr_t winStart = (exitHit > 0x200u) ? (exitHit - 0x200u) : 0u;
    const size_t winSpan = static_cast<size_t>(exitHit - winStart);
    uintptr_t dummy = 0;
    if (ArenaPlus_FindEncodedBytes(winStart, winSpan, kUsChatDot, 5, &dummy)) return true;
    if (ArenaPlus_FindEncodedBytes(winStart, winSpan, kUsFightMonstersDot, 13, &dummy)) return true;
    return false;
}

static bool ArenaPlus_NpcFindUsExitInBlock(uintptr_t base, size_t span, uintptr_t* outExitHit) {
    if (!base || !outExitHit || span < 5) return false;
    uintptr_t hit = 0;
    if (!ArenaPlus_FindEncodedBytes(base, span, kUsExitDot, 5, &hit)) return false;
    if (!ArenaPlus_NpcValidateNowWhatExit(hit)) return false;
    *outExitHit = hit;
    return true;
}

static uintptr_t ArenaPlus_NpcResolveEventString(uint32_t stringId) {
    if (!g_base || stringId > 0xFFFFu) return 0;
    typedef uintptr_t(__cdecl* FfxEventStringResolveFn)(int id);
    const auto fn = reinterpret_cast<FfxEventStringResolveFn>(g_base + RVA_FFX_EVENT_STRING_RESOLVE);
    __try {
        const uintptr_t p = fn(static_cast<int>(stringId));
        if (p < 0x10000u || p > 0x7FFE0000u) return 0;
        return p;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static void ArenaPlus_NpcLogBytesPreview(const char* tag, uintptr_t p, int n) {
    if (!p || n <= 0 || !tag) return;
    char line[220] = {};
    int w = 0;
    for (int i = 0; i < n && w < static_cast<int>(sizeof(line) - 4); ++i) {
        uint8_t b = 0;
        if (!ArenaTrace_ReadU8Abs(p + static_cast<uintptr_t>(i), &b)) break;
        w += snprintf(line + w, sizeof(line) - static_cast<size_t>(w), "%s%02X", i ? " " : "", b);
    }
    Log("[ffx-hooks] ArenaPlus NPC: %s @0x%08X [%s]\n", tag, static_cast<unsigned>(p), line);
}

static bool ArenaPlus_NpcApplyAppendAtExit(uintptr_t exitHit) {
    const uintptr_t ins = exitHit + 5u;
    if (!ArenaPlus_MemReadable(reinterpret_cast<const void*>(ins), 8)) return false;
    const unsigned char term = *reinterpret_cast<const unsigned char*>(ins);
    if (term != 0x00 && term != 0x03) return false;

    unsigned char patch[16] = {};
    patch[0] = 0x03;
    memcpy(patch + 1, kUsArenaPlusDot, 7);
    patch[8] = 0x00;
    const size_t patchLen = (term == 0x00) ? 9u : 8u;
    if (!ArenaPlus_MemWrite(reinterpret_cast<void*>(ins), patch, patchLen)) return false;

    InterlockedExchange(&g_arenaNpcChoiceIndex, static_cast<LONG>(kArenaNpcArenaPlusIndex));
    InterlockedExchange(&g_arenaNpcTextPatched, 1);
    Log("[ffx-hooks] ArenaPlus NPC: appended US Arena+. after Exit.@0x%08X (row index 5)\n",
        static_cast<unsigned>(ins));
    return true;
}

static bool ArenaPlus_NpcEnsureNowWhatPatch(uint32_t ctx, uint32_t a2) {
    (void)ctx;
    (void)a2;
    if (InterlockedCompareExchange(&g_arenaNpcScanDone, 0, 0) != 0) {
        return InterlockedCompareExchange(&g_arenaNpcTextPatched, 0, 0) != 0;
    }

    const uintptr_t strPtr = ArenaPlus_NpcResolveEventString(kArenaNpcNowWhatStringId);
    if (!strPtr) {
        InterlockedExchange(&g_arenaNpcScanDone, 1);
        Log("[ffx-hooks] ArenaPlus NPC: resolve string 0x%02X failed (use F7)\n",
            kArenaNpcNowWhatStringId);
        return false;
    }

    uintptr_t exitHit = 0;
    if (!ArenaPlus_NpcFindUsExitInBlock(strPtr, 0x400u, &exitHit)) {
        ArenaPlus_NpcLogBytesPreview("resolved string", strPtr, 80);
        InterlockedExchange(&g_arenaNpcScanDone, 1);
        Log("[ffx-hooks] ArenaPlus NPC: Exit. missing in resolved string @0x%08X\n",
            static_cast<unsigned>(strPtr));
        return false;
    }

    if (ArenaPlus_NpcApplyAppendAtExit(exitHit)) {
        InterlockedExchange(&g_arenaNpcScanDone, 1);
        return true;
    }

    InterlockedExchange(&g_arenaNpcScanDone, 1);
    Log("[ffx-hooks] ArenaPlus NPC: append failed @0x%08X (use F7)\n",
        static_cast<unsigned>(exitHit + 5u));
    return false;
}

static bool ArenaPlus_NpcPrepareNowWhatStack(uint32_t argStack, uint32_t* outMaxIndex) {
    if (!argStack || !outMaxIndex) return false;
    if (!ArenaPlus_NpcIsNowWhatStack(argStack)) return false;
    if (InterlockedCompareExchange(&g_arenaNpcTextPatched, 0, 0) == 0) return false;

    uint32_t maxIndex = 0;
    if (!ArenaTrace_ReadU32Abs(static_cast<uintptr_t>(argStack) + 16, &maxIndex)) return false;
    *outMaxIndex = maxIndex;

    if (g_arenaPlusMenu.obj) {
        ArenaPlus_CloseMenu(g_arenaPlusMenu);
    }
    SinCurse_CloseMenu();
    InterlockedExchange(&g_arenaPlusWantOpen, 0);
    InterlockedExchange(&g_sinWantOpen, 0);
    InterlockedExchange(&g_arenaNpcPendingOpen, 0);
    InterlockedExchange(&g_arenaNpcOpenDelay, 0);
    InterlockedExchange(&g_arenaNpcChoiceIndex, static_cast<LONG>(kArenaNpcArenaPlusIndex));

    if (!ArenaTrace_WriteU32Abs(static_cast<uintptr_t>(argStack) + 16, kArenaNpcArenaPlusIndex)) return false;
    ArenaTrace_WriteU32Abs(static_cast<uintptr_t>(argStack) + 12, 0);
    return true;
}

static bool ArenaTrace_IsEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ENABLE_ARENA_TRACE") ||
           ModuleFlagEnabled("arena_trace.flag") ||
           ModuleFlagEnabled("config\\arena_trace.flag");
}

typedef int (__cdecl* ArenaTraceAtelHandlerFn)(uint32_t ctx, uint32_t a2, uint32_t argStack);

static PLH::x86Detour* g_arenaTraceCommon013BDetour = nullptr;
static PLH::x86Detour* g_arenaTraceSgEvent401DDetour = nullptr;
static PLH::x86Detour* g_arenaTraceBattle7002Detour = nullptr;
static uint64_t g_arenaTraceCommon013BTramp = 0;
static uint64_t g_arenaTraceSgEvent401DTramp = 0;
static uint64_t g_arenaTraceBattle7002Tramp = 0;
static volatile LONG g_arenaTraceEnabled = 0;
static volatile LONG g_arenaTraceInCommon013B = 0;
static volatile LONG g_arenaTraceInSgEvent401D = 0;
static volatile LONG g_arenaTraceInBattle7002 = 0;

static bool ArenaTrace_ReadU8Abs(uintptr_t abs, uint8_t* out) {
    if (out) *out = 0;
    if (!abs) return false;
    __try {
        if (out) *out = *reinterpret_cast<volatile uint8_t*>(abs);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool ArenaTrace_ReadU16Abs(uintptr_t abs, uint16_t* out) {
    if (out) *out = 0;
    if (!abs) return false;
    __try {
        if (out) *out = *reinterpret_cast<volatile uint16_t*>(abs);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool ArenaTrace_ReadU32Abs(uintptr_t abs, uint32_t* out) {
    if (out) *out = 0;
    if (!abs) return false;
    __try {
        if (out) *out = *reinterpret_cast<volatile uint32_t*>(abs);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool ArenaTrace_WriteU32Abs(uintptr_t abs, uint32_t value) {
    if (!abs) return false;
    __try {
        *reinterpret_cast<volatile uint32_t*>(abs) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static uint32_t ArenaTrace_IdbVa(uint32_t runtimePtr) {
    const uintptr_t p = static_cast<uintptr_t>(runtimePtr);
    if (g_base && p >= g_base && p < g_base + 0x08000000u) {
        return static_cast<uint32_t>(0x400000u + (p - g_base));
    }
    return runtimePtr;
}

static void ArenaTrace_LogAtelStack(const char* tag, uint32_t argStack) {
    uint32_t s[6] = {};
    bool ok[6] = {};
    for (int i = 0; i < 6; ++i) {
        ok[i] = ArenaTrace_ReadU32Abs(static_cast<uintptr_t>(argStack) + static_cast<uintptr_t>(i * 4), &s[i]);
    }
    Log("[ffx-hooks] ArenaTrace %s stack@0x%08X [%s%08X %s%08X %s%08X %s%08X %s%08X %s%08X]\n",
        tag ? tag : "?",
        argStack,
        ok[0] ? "" : "?", s[0],
        ok[1] ? "" : "?", s[1],
        ok[2] ? "" : "?", s[2],
        ok[3] ? "" : "?", s[3],
        ok[4] ? "" : "?", s[4],
        ok[5] ? "" : "?", s[5]);
}

static void ArenaPlus_CaptureBattle7002Template(uint32_t ctx, uint32_t a2, uint32_t argStack) {
    if (!ArenaPlus_IsEnabled() || argStack == 0) return;

    uint32_t s[8] = {};
    bool ok[8] = {};
    for (int i = 0; i < 8; ++i) {
        ok[i] = ArenaTrace_ReadU32Abs(static_cast<uintptr_t>(argStack) + static_cast<uintptr_t>(i * 4), &s[i]);
    }
    if (!ok[0] || !ok[1] || s[0] == 0 || s[1] == 0) return;

    for (int i = 0; i < 8; ++i) g_arenaPlusBattle7002TemplateStack[i] = s[i];
    InterlockedExchange(&g_arenaPlusBattle7002TemplateCtx, static_cast<LONG>(ctx));
    InterlockedExchange(&g_arenaPlusBattle7002TemplateA2, static_cast<LONG>(a2));
    InterlockedExchange(&g_arenaPlusBattle7002TemplateTick, static_cast<LONG>(GetTickCount()));
    const LONG count = InterlockedIncrement(&g_arenaPlusBattle7002TemplateCount);
    InterlockedExchange(&g_arenaPlusBattle7002TemplateReady, 1);

    Log("[ffx-hooks] ArenaPlus: captured Battle.7002 template #%ld ctx=0x%08X a2=0x%08X stack=[%08X %08X %08X %08X %08X %08X]\n",
        count,
        ctx,
        a2,
        s[0],
        s[1],
        s[2],
        s[3],
        s[4],
        s[5]);
}

static bool ArenaPlus_TryOverrideBattle7002(uint32_t argStack) {
    if (InterlockedCompareExchange(&g_arenaPlusPendingBattle7002, 1, 1) != 1) return false;

    const uint32_t token = static_cast<uint32_t>(
        InterlockedCompareExchange(&g_arenaPlusPendingBattleToken, 0, 0));
    const uint32_t eventTransition = static_cast<uint32_t>(
        InterlockedCompareExchange(&g_arenaPlusPendingTransition, 0, 0));
    const int dark = static_cast<int>(InterlockedCompareExchange(&g_arenaPlusPendingDark, -1, -1));
    const uint32_t gilCost = static_cast<uint32_t>(
        InterlockedCompareExchange(&g_arenaPlusPendingGilCost, 0, 0));
    const uint32_t expireTick = static_cast<uint32_t>(
        InterlockedCompareExchange(&g_arenaPlusPendingExpireTick, 0, 0));
    const int32_t difficultyField = static_cast<int32_t>(
        InterlockedCompareExchange(&g_arenaPlusPendingDifficultyField, -1, -1));
    const auto difficultySource = static_cast<FfxHooks::F7Difficulty::BattleFieldSource>(
        InterlockedCompareExchange(
            &g_arenaPlusPendingDifficultySource,
            static_cast<LONG>(FfxHooks::F7Difficulty::BattleFieldSource::Missing),
            static_cast<LONG>(FfxHooks::F7Difficulty::BattleFieldSource::Missing)));
    if (token == 0 || argStack == 0) {
        Log("[ffx-hooks] ArenaPlus: pending Battle.7002 override invalid token=0x%08X argStack=0x%08X dark=%d gilCost=%u expireTick=0x%08X\n",
            token, argStack, dark, gilCost, expireTick);
        ArenaPlus_ClearPendingBattle7002Override();
        return false;
    }
    if (expireTick != 0 && static_cast<int32_t>(GetTickCount() - expireTick) > 0) {
        Log("[ffx-hooks] ArenaPlus: pending Battle.7002 override expired row=%d name=%s token=0x%08X expireTick=0x%08X now=0x%08X; override canceled\n",
            dark,
            (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
            token,
            expireTick,
            GetTickCount());
        ArenaPlus_ClearPendingBattle7002Override();
        return false;
    }

    uint32_t gilBefore = 0;
    uint32_t gilAfter = 0;
    if (gilCost > 0) {
        uint32_t gilStatus = 0;
        uint32_t gilErr = 0;
        const bool gilOk = ArenaPlus_ReadGil(&gilBefore, &gilStatus, &gilErr);
        if (!gilOk || gilStatus != FFXPROBE_ST_OK) {
            Log("[ffx-hooks] ArenaPlus: gil charge read failed row=%d name=%s cost=%u status=%u err=0x%08X; override canceled\n",
                dark,
                (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
                gilCost,
                gilStatus,
                gilErr);
            ArenaPlus_ClearPendingBattle7002Override();
            return false;
        }
        if (gilBefore < gilCost) {
            Log("[ffx-hooks] ArenaPlus: insufficient gil row=%d name=%s cost=%u current=%u; override canceled\n",
                dark,
                (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
                gilCost,
                gilBefore);
            ArenaPlus_ClearPendingBattle7002Override();
            return false;
        }
        gilAfter = gilBefore - gilCost;
    }

    uint32_t old0 = 0, old1 = 0;
    const bool r0 = ArenaTrace_ReadU32Abs(static_cast<uintptr_t>(argStack), &old0);
    const bool r1 = ArenaTrace_ReadU32Abs(static_cast<uintptr_t>(argStack) + 4, &old1);
    const bool swapArgs = ArenaPlus_Battle7002SwapArgs();
    const bool useEventTransition = ArenaPlus_UseEventTransition();
    const uint32_t transition = useEventTransition ? eventTransition : old0;
    const uint32_t new0 = swapArgs ? token : transition;
    const uint32_t new1 = swapArgs ? transition : token;
    const bool w0 = ArenaTrace_WriteU32Abs(static_cast<uintptr_t>(argStack), new0);
    const bool w1 = ArenaTrace_WriteU32Abs(static_cast<uintptr_t>(argStack) + 4, new1);

    Log("[ffx-hooks] ArenaPlus: Battle.7002 override %s row=%d name=%s token=0x%08X transition=0x%08X old=[%s%08X %s%08X] new=[%08X %08X] swap=%d eventTransition=%d argStack=0x%08X\n",
        (w0 && w1) ? "applied" : "FAILED",
        dark,
        (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
        token,
        transition,
        r0 ? "" : "?", old0,
        r1 ? "" : "?", old1,
        new0,
        new1,
        swapArgs ? 1 : 0,
        useEventTransition ? 1 : 0,
        argStack);

    if (w0 && w1) {
        // Publish only after this deferred route actually replaces Battle.7002.
        // A carrier launch may have published its own field earlier; the target
        // route must be the immutable one-shot value consumed by initialization.
        FfxHooks::F7_PublishPendingBattleField(difficultyField, difficultySource);
        if (gilCost > 0) {
            uint32_t gilStatus = 0;
            uint32_t gilErr = 0;
            const bool chargeOk = ArenaPlus_WriteGil(gilAfter, &gilStatus, &gilErr);
            Log("[ffx-hooks] ArenaPlus: gil charge %s row=%d name=%s cost=%u gil=%u->%u status=%u err=0x%08X\n",
                (chargeOk && gilStatus == FFXPROBE_ST_OK) ? "applied" : "FAILED",
                dark,
                (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
                gilCost,
                gilBefore,
                gilAfter,
                gilStatus,
                gilErr);
        }
        ArenaPlus_ClearPendingBattle7002Override();
        return true;
    }
    return false;
}

static int ArenaTrace_CallOriginalAtel(
    const char* tag,
    uint64_t trampoline,
    volatile LONG* guard,
    uint32_t ctx,
    uint32_t a2,
    uint32_t argStack) {
    if (!trampoline) {
        Log("[ffx-hooks] ArenaTrace %s trampoline missing\n", tag ? tag : "?");
        return 0;
    }
    if (InterlockedCompareExchange(guard, 1, 0) != 0) {
        return reinterpret_cast<ArenaTraceAtelHandlerFn>(trampoline)(ctx, a2, argStack);
    }

    Log("[ffx-hooks] ArenaTrace %s enter ctx=0x%08X a2=0x%08X argStack=0x%08X\n",
        tag ? tag : "?", ctx, a2, argStack);
    ArenaTrace_LogAtelStack(tag, argStack);

    int ret = 0;
    bool ok = false;
    __try {
        ret = reinterpret_cast<ArenaTraceAtelHandlerFn>(trampoline)(ctx, a2, argStack);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[ffx-hooks] WARN ArenaTrace %s original raised exception 0x%08X\n",
            tag ? tag : "?", GetExceptionCode());
    }
    Log("[ffx-hooks] ArenaTrace %s leave ok=%d ret=0x%08X\n",
        tag ? tag : "?", ok ? 1 : 0, static_cast<unsigned>(ret));
    InterlockedExchange(guard, 0);
    return ret;
}

static int __cdecl ArenaPlus_Common013B(uint32_t ctx, uint32_t a2, uint32_t argStack) {
    const bool traceOn = InterlockedCompareExchange(&g_arenaTraceEnabled, 0, 0) != 0;
    const bool npcOn = ArenaPlus_NpcHookEnabled();
    bool npcPrepared = false;
    LONG npcChoiceIndex = -1;

    if (npcOn && argStack != 0 && ArenaPlus_NpcIsNowWhatStack(argStack)) {
        ArenaPlus_NpcEnsureNowWhatPatch(ctx, a2);
        uint32_t vanillaMax = 0;
        if (ArenaPlus_NpcPrepareNowWhatStack(argStack, &vanillaMax)) {
            npcPrepared = true;
            npcChoiceIndex = kArenaNpcArenaPlusIndex;
            InterlockedIncrement(&g_arenaNpcInNowWhat);
            Log("[ffx-hooks] ArenaPlus NPC: Now what? row6 armed maxWas=%u\n", vanillaMax);
        } else {
            Log("[ffx-hooks] ArenaPlus NPC: vanilla 5 rows (text patch not ready; F7 works)\n");
        }
    }

    const int ret = ArenaTrace_CallOriginalAtel(
        traceOn ? "Common.013B displayFieldChoice" : "Common.013B",
        g_arenaTraceCommon013BTramp,
        &g_arenaTraceInCommon013B,
        ctx,
        a2,
        argStack);

    if (npcPrepared) {
        uint32_t selected = 0;
        if (ArenaTrace_ReadU32Abs(static_cast<uintptr_t>(argStack) + 12, &selected) &&
            selected == static_cast<uint32_t>(npcChoiceIndex) &&
            InterlockedCompareExchange(&g_arenaNpcTextPatched, 0, 0) != 0) {
            Log("[ffx-hooks] ArenaPlus NPC: row %u picked -> defer Arena+ (%d frames)\n",
                selected, kArenaNpcOpenDelayFrames);
            ArenaTrace_WriteU32Abs(static_cast<uintptr_t>(argStack) + 12, kArenaNpcNowWhatVanillaMax);
            InterlockedExchange(&g_arenaNpcPendingOpen, 1);
            InterlockedExchange(&g_arenaNpcOpenDelay, kArenaNpcOpenDelayFrames);
            InterlockedDecrement(&g_arenaNpcInNowWhat);
            return static_cast<int>(kArenaNpcNowWhatVanillaMax);
        }
        InterlockedDecrement(&g_arenaNpcInNowWhat);
    }
    return ret;
}

static int __cdecl ArenaTrace_Common013B(uint32_t ctx, uint32_t a2, uint32_t argStack) {
    return ArenaPlus_Common013B(ctx, a2, argStack);
}

static int __cdecl ArenaTrace_SgEvent401D(uint32_t ctx, uint32_t a2, uint32_t argStack) {
    return ArenaTrace_CallOriginalAtel("SgEvent.401D showModularMenu", g_arenaTraceSgEvent401DTramp,
        &g_arenaTraceInSgEvent401D, ctx, a2, argStack);
}

static int __cdecl ArenaTrace_Battle7002(uint32_t ctx, uint32_t a2, uint32_t argStack) {
    ArenaPlus_CaptureBattle7002Template(ctx, a2, argStack);
    ArenaPlus_TryOverrideBattle7002(argStack);
    return ArenaTrace_CallOriginalAtel("Battle.7002 launchBattle", g_arenaTraceBattle7002Tramp,
        &g_arenaTraceInBattle7002, ctx, a2, argStack);
}

static bool ArenaTrace_InstallDetour(
    const char* tag,
    uint32_t idbVa,
    void* hook,
    PLH::x86Detour** detour,
    uint64_t* trampoline) {
    if (!detour || !trampoline || *detour) return false;
    const uintptr_t targetVa = g_base + (static_cast<uintptr_t>(idbVa) - 0x400000u);
    try {
        *detour = new PLH::x86Detour(
            static_cast<uint64_t>(targetVa),
            reinterpret_cast<uint64_t>(hook),
            trampoline);
        const bool ok = (*detour)->hook();
        Log("[ffx-hooks] ArenaTrace hook %s target=0x%08X runtime=0x%08X ok=%d trampoline=0x%llX\n",
            tag ? tag : "?",
            idbVa,
            static_cast<unsigned>(targetVa),
            ok ? 1 : 0,
            static_cast<unsigned long long>(*trampoline));
        if (!ok) {
            delete *detour;
            *detour = nullptr;
            *trampoline = 0;
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        Log("[ffx-hooks] ERROR ArenaTrace hook %s exception: %s\n", tag ? tag : "?", ex.what());
    } catch (...) {
        Log("[ffx-hooks] ERROR ArenaTrace hook %s unknown exception\n", tag ? tag : "?");
    }
    *detour = nullptr;
    *trampoline = 0;
    return false;
}

static void StartArenaTraceIfEnabled() {
    const bool traceEnabled = ArenaTrace_IsEnabled();
    const bool npcHookEnabled = ArenaPlus_NpcHookEnabled();
    const bool arenaOverrideEnabled = ArenaPlus_IsEnabled() && ArenaPlus_LabRoutesEnabled();
    if (!traceEnabled && !npcHookEnabled && !arenaOverrideEnabled) {
        Log("[ffx-hooks] ArenaTrace: disabled (arena_trace / arena_plus_npc / Battle.7002 override off)\n");
        return;
    }
    if (!g_base) {
        Log("[ffx-hooks] ArenaTrace: g_base not resolved - abort\n");
        return;
    }
    InterlockedExchange(&g_arenaTraceEnabled, traceEnabled ? 1 : 0);
    Log("[ffx-hooks] ArenaTrace: trace=%d npc013B=%d arenaOverride=%d\n",
        traceEnabled ? 1 : 0,
        npcHookEnabled ? 1 : 0,
        arenaOverrideEnabled ? 1 : 0);
    if (traceEnabled || npcHookEnabled) {
        ArenaTrace_InstallDetour("Common.013B", 0x8600E0u, reinterpret_cast<void*>(&ArenaPlus_Common013B),
            &g_arenaTraceCommon013BDetour, &g_arenaTraceCommon013BTramp);
    }
    if (traceEnabled) {
        ArenaTrace_InstallDetour("SgEvent.401D", 0xA78210u, reinterpret_cast<void*>(&ArenaTrace_SgEvent401D),
            &g_arenaTraceSgEvent401DDetour, &g_arenaTraceSgEvent401DTramp);
    }
    if (arenaOverrideEnabled) {
        ArenaTrace_InstallDetour("Battle.7002", 0x7A3550u, reinterpret_cast<void*>(&ArenaTrace_Battle7002),
            &g_arenaTraceBattle7002Detour, &g_arenaTraceBattle7002Tramp);
    }
}

static void ArenaTrace_StopDetour(PLH::x86Detour** detour, uint64_t* trampoline) {
    if (!detour || !*detour) return;
    __try {
        (*detour)->unHook();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[ffx-hooks] WARN ArenaTrace unhook exception\n");
    }
    delete *detour;
    *detour = nullptr;
    if (trampoline) *trampoline = 0;
}

static void StopArenaTrace() {
    InterlockedExchange(&g_arenaTraceEnabled, 0);
    ArenaTrace_StopDetour(&g_arenaTraceBattle7002Detour, &g_arenaTraceBattle7002Tramp);
    ArenaTrace_StopDetour(&g_arenaTraceSgEvent401DDetour, &g_arenaTraceSgEvent401DTramp);
    ArenaTrace_StopDetour(&g_arenaTraceCommon013BDetour, &g_arenaTraceCommon013BTramp);
}

static void ArenaTrace_MenuPoolTick(const char* source) {
    if (!InterlockedCompareExchange(&g_arenaTraceEnabled, 0, 0) || !g_base) return;

    static bool s_seenArenaObj = false;
    static uint32_t s_lastObj = 0;
    static uint32_t s_lastSig = 0;

    const uintptr_t pool = g_base + (0x18408C0u - 0x400000u);
    bool found = false;

    for (int i = 0; i < 32; ++i) {
        const uintptr_t obj = pool + static_cast<uintptr_t>(i * 152);
        uint8_t active = 0;
        uint16_t group = 0;
        if (!ArenaTrace_ReadU8Abs(obj + 64, &active)) break;
        if (!active) continue;
        if (!ArenaTrace_ReadU16Abs(obj + 62, &group)) continue;
        if (group != 0x0106u) continue;

        uint32_t state = 0, input = 0, draw = 0, enter = 0, valid = 0;
        uint16_t count = 0, top = 0, page = 0, selected = 0;
        uint8_t slots = 0, result = 0;
        ArenaTrace_ReadU32Abs(obj + 40, &state);
        ArenaTrace_ReadU32Abs(obj + 12, &input);
        ArenaTrace_ReadU32Abs(obj + 16, &draw);
        ArenaTrace_ReadU32Abs(obj + 8, &enter);
        ArenaTrace_ReadU32Abs(obj + 28, &valid);
        ArenaTrace_ReadU16Abs(obj + 48, &count);
        ArenaTrace_ReadU16Abs(obj + 50, &top);
        ArenaTrace_ReadU16Abs(obj + 58, &page);
        ArenaTrace_ReadU16Abs(obj + 72, &selected);
        ArenaTrace_ReadU8Abs(obj + 66, &slots);
        ArenaTrace_ReadU8Abs(obj + 69, &result);

        const uint32_t sig =
            (state & 0xFFu) ^
            (static_cast<uint32_t>(count) << 8) ^
            (static_cast<uint32_t>(top) << 16) ^
            (static_cast<uint32_t>(page) << 24) ^
            (static_cast<uint32_t>(selected) << 4) ^
            (static_cast<uint32_t>(result) << 20) ^
            (input << 1) ^
            (draw << 3) ^
            (valid << 5);
        const bool shouldLog =
            !s_seenArenaObj ||
            s_lastObj != static_cast<uint32_t>(obj) ||
            s_lastSig != sig;

        if (shouldLog) {
            Log("[ffx-hooks] ArenaTrace menuPool source=%s slot=%d obj=0x%08X group=0x%03X state=%u count=%u top=%u page=%u active=%u slots=%u result=%d selected=%d cb(input=0x%08X/ida=0x%08X draw=0x%08X/ida=0x%08X enter=0x%08X/ida=0x%08X valid=0x%08X/ida=0x%08X)\n",
                source ? source : "?",
                i,
                static_cast<unsigned>(obj),
                static_cast<unsigned>(group),
                static_cast<unsigned>(state),
                static_cast<unsigned>(count),
                static_cast<unsigned>(top),
                static_cast<unsigned>(page),
                static_cast<unsigned>(active),
                static_cast<unsigned>(slots),
                static_cast<int>(static_cast<signed char>(result)),
                static_cast<int>(static_cast<int16_t>(selected)),
                input, ArenaTrace_IdbVa(input),
                draw, ArenaTrace_IdbVa(draw),
                enter, ArenaTrace_IdbVa(enter),
                valid, ArenaTrace_IdbVa(valid));
        }
        s_seenArenaObj = true;
        s_lastObj = static_cast<uint32_t>(obj);
        s_lastSig = sig;
        found = true;
    }

    if (s_seenArenaObj && !found) {
        Log("[ffx-hooks] ArenaTrace menuPool source=%s group=0x106 gone\n", source ? source : "?");
        s_seenArenaObj = false;
        s_lastObj = 0;
        s_lastSig = 0;
    }
}

// Edge bridge: map confirmed rows to the developer-only Aurora PhotoMode actions.
/* â”€â”€ F7 In-Live submenu: declarations (definitions in sections below) â”€â”€â”€â”€â”€â”€â”€â”€ */
enum F7MenuKind { F7_MENU_MUSIC = 0, F7_MENU_FORCE, F7_MENU_DIFF, F7_MENU_AI, F7_MENU_FLAGS };
enum F7RowType { F7RT_INFO = 0, F7RT_TOGGLE, F7RT_STEPPER, F7RT_ACTION, F7RT_SCALAR, F7RT_BULK, F7RT_BINDING, F7RT_OPTIONS, F7RT_BACK };
static char g_f8BindingFeedback[96] = {};

struct F7SubRow {
    const char* label;
    F7RowType   type;
    int         min, max, step;
    const char* desc;   // 2026-08-16: item description line (FLAGS) — nullptr = no help
};

static NativeMenu::Menu g_f7Menu         = { 0 };
static volatile LONG    g_f7WantOpenKind = -1;
static int              g_f7MenuKind     = F7_MENU_MUSIC;
static FfxHooks::F8Ui::AtomicOpenLatch g_f8MenuOpen;
static int              g_f7Vals[32]     = {};
static F7SubRow         g_f7Rows[32]     = {};
static int              g_f7RowCount     = 0;
static unsigned char    g_f7Labels[32][64]    = {};
static unsigned char    g_f7SubLabels[32][48] = {};
static int              g_f7ConfirmTimer = 0;
static int              g_f7LastEdge     = 0;
static FfxHooks::F8Ui::CloseLatch g_f7CloseLatch;
static float            g_f7EasedRowY    = -1.0f;
static int              g_f7DrawCalls    = 0;
static bool g_f7DifficultyEnabled = false;
static int g_f7DiffPresetIdx = -1;   // Preset identity only; -1 means custom.
static char             g_f7FmtBuf[48]   = {};
static int              g_f7FlagCount    = 0;   // FLAGS physical functional rows (without Back)
static const FfxHooks::F8FlagSpec* g_f7FlagSpecs[32] = {};
static char             g_f8ScalarLabels[32][64] = {};
static char             g_f8BulkStatus[64] = {};
// R8-U1: per-row verdicts written back after a bulk run — one buffer per row so the
// row's desc can point at it until the next tab rebuild resets desc to flag->help.
static char             g_f8RowVerdicts[32][96] = {};
static FfxHooks::F8Ui::ScalarEditor g_f8ScalarEditor;
static const FfxHooks::F8FlagSpec* g_f7LastEditSpec = nullptr;
static FfxHooks::F8EditResult g_f7LastEdit = {};
static bool             g_f7HasLastEdit = false;
static const FfxHooks::F8FlagSpec* g_f7LastScalarEditSpec = nullptr;
static FfxHooks::F8ScalarEditResult g_f7LastScalarEdit = {};
static bool             g_f7HasLastScalarEdit = false;

/* FLAGS uses catalog-owned tab names and immutable row metadata. */
static int              g_f7Tab        = 0;

struct F7PointerSnapshot {
    bool valid;
    int wheelSteps;
    float x;
    float y;
    FfxHooks::F7Ui::PointerDecision decision;
};

static bool F7OwnsVisibleUi() {
    const bool directF8Flags =
        g_f8MenuOpen.Load() && g_f7Menu.obj && g_f7MenuKind == F7_MENU_FLAGS;
    return EquipmentMenu::Active() || g_nativeMenu.obj || g_arenaPlusMenu.obj || g_sinMenu.obj ||
           (g_f7Menu.obj && !directF8Flags) || ArenaPlusComposePick_IsActive() ||
           g_nativeHeldAction >= 0 ||
           (!g_f8MenuOpen.Load() &&
            (InterlockedCompareExchange(&g_nativeWantSpawn, 0, 0) != 0 ||
             InterlockedCompareExchange(&g_arenaPlusWantOpen, 0, 0) != 0 ||
             InterlockedCompareExchange(&g_sinWantOpen, 0, 0) != 0 ||
             InterlockedCompareExchange(&g_f7WantOpenKind, -1, -1) >= 0));
}

static bool F7OwnsUiPublishedForPresent() {
    if (g_f8MenuOpen.Load()) return false;
    return InterlockedCompareExchange(&g_nativeOtherOwnerPublished, 0, 0) != 0 ||
           InterlockedCompareExchange(&g_nativeWantSpawn, 0, 0) != 0 ||
           InterlockedCompareExchange(&g_arenaPlusWantOpen, 0, 0) != 0 ||
           InterlockedCompareExchange(&g_sinWantOpen, 0, 0) != 0 ||
           InterlockedCompareExchange(&g_f7WantOpenKind, -1, -1) >= 0;
}

static F7PointerSnapshot F7CapturePointer() {
    F7PointerSnapshot result = {};
    if (!F7IsForegroundWindow()) return result;
    HWND hwnd = g_ingameMenuInputHwnd;
    if (!hwnd || !IsWindow(hwnd)) hwnd = GetForegroundWindow();
    if (!hwnd) return result;

    POINT point = {};
    RECT client = {};
    if (!GetCursorPos(&point) || !ScreenToClient(hwnd, &point) ||
        !GetClientRect(hwnd, &client)) {
        return result;
    }
    const LONG width = client.right - client.left;
    const LONG height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return result;

    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    result.valid = true;
    result.wheelSteps = -static_cast<int>(
        InterlockedExchange(&g_f7MouseWheelDelta, 0) / WHEEL_DELTA);
    result.x = static_cast<float>(point.x) / static_cast<float>(width) *
               NativeMenu::MenuPhysW();
    result.y = static_cast<float>(point.y) / static_cast<float>(height) *
               NativeMenu::MenuPhysH();
    FfxHooks::F7Ui::PointerSample sample{};
    sample.valid = true;
    sample.buttonDown = down;
    sample.wheelSteps = result.wheelSteps;
    sample.x = result.x;
    sample.y = result.y;
    result.decision = FfxHooks::F7Ui::ObservePointer(g_f7PointerState, sample);
    return result;
}

static void F7MainMenuMouseTick(int obj) {
    using namespace NativeMenu;
    const F7PointerSnapshot pointer = F7CapturePointer();
    if (!pointer.valid || !obj) return;
    int selection = RdW(obj, O_SELECTED);
    int firstVisible = RdW(obj, O_TOP);
    const int rowCount = RdW(obj, O_COUNT);
    const int page = RdW(obj, O_PAGE);
    if (pointer.wheelSteps != 0) {
        FfxHooks::F7Ui::ScrollList(
            pointer.wheelSteps, rowCount, page, selection, firstVisible);
    }
    const FfxHooks::F7Ui::ListGeometry geometry{
        NX(0.5625f), NY(0.213f), NW(0.375f), NH(0.0648f), NH(0.0593f), page};
    const FfxHooks::F7Ui::Hit hit = FfxHooks::F7Ui::HitTestRows(
        pointer.x, pointer.y, geometry, firstVisible, rowCount);
    const FfxHooks::F7Ui::ListPointerResolution mouse =
        FfxHooks::F7Ui::ResolveListPointerInput(selection, hit, pointer.decision);
    selection = mouse.selection;
    if (selection != RdW(obj, O_SELECTED)) g_easedRowY = -1.0f;
    WrW(obj, O_SELECTED, static_cast<int16_t>(selection));
    WrW(obj, O_TOP, static_cast<int16_t>(firstVisible));
    if (mouse.confirm && !g_ourClosed) {
        PlaySfx(1);
        g_ourResult = mouse.selection;
        g_ourClosed = 1;
    }
}

static F7MouseInputResult F7ListMouseTick(
    int obj,
    float left,
    float top,
    float width,
    float step,
    float rowHeight,
    int rowCount,
    int page) {
    using namespace NativeMenu;
    const F7PointerSnapshot pointer = F7CapturePointer();
    if (!pointer.valid || !obj || rowCount <= 0 || page <= 0) return {};
    int selection = RdW(obj, O_SELECTED);
    int firstVisible = RdW(obj, O_TOP);
    if (pointer.wheelSteps != 0) {
        FfxHooks::F7Ui::ScrollList(
            pointer.wheelSteps, rowCount, page, selection, firstVisible);
    }
    const FfxHooks::F7Ui::ListGeometry geometry{
        left, top, width, step, rowHeight, page};
    const FfxHooks::F7Ui::Hit hit = FfxHooks::F7Ui::HitTestRows(
        pointer.x, pointer.y, geometry, firstVisible, rowCount);
    const FfxHooks::F7Ui::ListPointerResolution mouse =
        FfxHooks::F7Ui::ResolveListPointerInput(selection, hit, pointer.decision);
    selection = mouse.selection;
    WrW(obj, O_SELECTED, static_cast<int16_t>(selection));
    WrW(obj, O_TOP, static_cast<int16_t>(firstVisible));
    return F7MouseInputResult{mouse.confirm, mouse.ownsDirectionalFrame};
}

static NativeMenu::Menu F7Sub_SpawnMenu(int kind);
static NativeMenu::Poll F7Sub_PollMenu(const NativeMenu::Menu& m);
static void F7Sub_CloseMenu();
static void F8ReleaseCursorOwnership();
static void F8RollbackRejectedDirectOpen();
static void F8ReturnFlagsToGame();
static void F7Sub_HandleConfirm(int row);
static const char* F8EditCodeName(FfxHooks::F8EditCode code);
static const char* F8ScalarEditCodeName(FfxHooks::F8ScalarEditCode code);
static void F8RefreshScalarLabel(int row);
static void F8ApplyTabBulk(bool requestedValue, int selectedRow);
static void F7_LeverApply(NativeMenu::ActionId act, int val);   // KEYSTONE B (2026-08-02): direct-action forward declaration (OnEdge).
static void F7_CommitValsToConfig();   // Forward declaration used by F7Sub_InputCb.

static void NativeMenu_OnEdge(NativeMenu::ActionId a) {
    using namespace NativeMenu;
    // The IFRIT lane repurposed this surface as the in-live editor. PhotoMode remains a separate,
    // closed developer-only path; do not invoke it from these player-facing actions.
    switch (a) {
        case ACT_BATTLE_CHEATS: {
            // KEYSTONE B (2026-08-02): direct actions WITHOUT submenu
            const int row = NativeMenu::g_ourResult;
            const int val = (row >= 0 && row < NativeMenu::kRowCount) ? NativeMenu::g_rowValue[row] : 0;
            F7_LeverApply(a, val);
            break;
        }
        case ACT_AI_SWAP:
            Log("[ffx-hooks] NativeMenu: Monster AI Observer selected, opening observe-only submenu\n");
            InterlockedExchange(&g_f7WantOpenKind, F7_MENU_AI);
            break;
        case ACT_EXIT: break;   // The caller owns CloseMenu; no action is required here.
        case ACT_ARENA:
            if (!ArenaPlus_IsEnabled()) {
                Log("[ffx-hooks] ArenaPlus: disabled (set FFXHOOKS_ENABLE_ARENA_PLUS=1 or create modules\\arena_plus.flag)\n");
                break;
            }
            ArenaPlus_RequestOpen("NativeMenuShell");
            break;
        case ACT_SIN:
            Log("[ffx-hooks] NativeMenu: SIN selected, opening SIN submenu\n");
            InterlockedExchange(&g_sinWantOpen, 1);
            break;
        case ACT_MUSIC:
            Log("[ffx-hooks] NativeMenu: MUSIC selected, opening F7 music submenu\n");
            InterlockedExchange(&g_f7WantOpenKind, F7_MENU_MUSIC);
            break;
        case ACT_FORCE_BATTLE:
            Log("[ffx-hooks] NativeMenu: FORCE BATTLE selected, opening F7 force submenu\n");
            InterlockedExchange(&g_f7WantOpenKind, F7_MENU_FORCE);
            break;
        case ACT_DIFFICULTY:
            Log("[ffx-hooks] NativeMenu: DIFFICULTY selected, opening F7 difficulty submenu\n");
            InterlockedExchange(&g_f7WantOpenKind, F7_MENU_DIFF);
            break;
        default:
            Log("[ffx-hooks] NativeMenu: item %d selected (not-wired yet; only the name)\n", (int)a);
            break;
    }
}
// Bridge HELD: enters the "hold" sub-mode (the pump applies per-frame in NativeMenu_TickHeld).
static void NativeMenu_OnHeldEnter(NativeMenu::ActionId a) { g_nativeHeldAction = (int)a; }

// HELD sub-mode per-frame: reads deltas (keyboard for now) + calls Aurora's continuous action.
// (Native pad: swap GetAsyncKeyState for the bits at 0x8BE440 in a future step.)
static void NativeMenu_TickHeld() {
    // Lane IFRIT: no row is HELD for now (all EDGE) -> dormant. Stub kept for when a continuous
    // action (e.g. Camera/Actor nudge, which needs per-frame re-poke) is wired. ESC exits the
    // sub-mode for safety.
    if (!F7IsForegroundWindow()) {
        F7RequestClose(FfxHooks::F7Ui::CloseSource::FocusLost);
        return;
    }
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
        F7RequestClose(FfxHooks::F7Ui::CloseSource::Cancel);
    }
}

static bool ArenaPlusNpcDelayedOpenPending() {
    return InterlockedCompareExchange(&g_arenaNpcPendingOpen, 0, 0) != 0 ||
           InterlockedCompareExchange(&g_arenaNpcOpenDelay, 0, 0) > 0;
}

static bool NativeMenuLegacyModalAllocationIdle() {
    // WHY: Maechen Pump may acquire or publish its terminal reap wake in this
    // pass. Every later legacy allocation must share this one post-Pump view so
    // no Arena/F7/native/compose/held owner can coexist with a second modal.
    return FfxHooks::F8Ui::LegacyModalAllocationIdle({
        FfxHooks::Maechen_BlocksNativeModalAllocation() || EquipmentMenu::Active(),
        g_nativeMenu.obj != 0,
        g_arenaPlusMenu.obj != 0,
        g_sinMenu.obj != 0,
        g_f7Menu.obj != 0,
        ArenaPlusComposePick_IsActive(),
        g_nativeHeldAction >= 0,
    });
}

static void NativeMenuPromoteDelayedArenaRequestIfReady() {
    if (!NativeMenuLegacyModalAllocationIdle() ||
        InterlockedCompareExchange(&g_arenaNpcInNowWhat, 0, 0) != 0) return;
    const LONG delay = InterlockedCompareExchange(&g_arenaNpcOpenDelay, 0, 0);
    if (delay > 0) {
        InterlockedDecrement(&g_arenaNpcOpenDelay);
        return;
    }
    if (FfxHooks::NativeMenu_ReserveAndConsumeOpenRequest(
            &g_arenaNpcPendingOpen, 0, &g_nativeOtherOwnerPublished) != 0) {
        ArenaPlus_RequestOpen("NpcNowWhat");
    }
}

static FfxHooks::F8Ui::ArenaOpenResult NativeMenuTryOpenArenaRequest() {
    const bool requestPending =
        InterlockedCompareExchange(&g_arenaPlusWantOpen, 0, 0) != 0;
    return FfxHooks::F8Ui::TryHandleArenaOpenRequest(
        requestPending,
        NativeMenuLegacyModalAllocationIdle() &&
            InterlockedCompareExchange(&g_arenaNpcInNowWhat, 0, 0) == 0,
        []() {
            if (FfxHooks::NativeMenu_ReserveAndConsumeOpenRequest(
                    &g_arenaPlusWantOpen, 0, &g_nativeOtherOwnerPublished) == 0) {
                return false;
            }
            if (!ArenaPlus_OpenMenuFromRequest()) {
                g_nativeMenu = SpawnHydratedNativeMenu();
                if (!g_nativeMenu.obj) InterlockedExchange(&g_forceSubsystem, 0);
            }
            return true;
        });
}

static bool NativeMenuOtherOwnerOrRequestActivePumpOnly() {
    return EquipmentMenu::Active() || g_nativeMenu.obj || g_arenaPlusMenu.obj || g_sinMenu.obj || g_f7Menu.obj ||
           ArenaPlusComposePick_IsActive() || g_nativeHeldAction >= 0 ||
           NativeMenuHubCloseDrainPending() ||
           InterlockedCompareExchange(&EquipmentMenu::wantOpen,0,0)!=0 ||
           InterlockedCompareExchange(&g_nativeWantSpawn, 0, 0) != 0 ||
           InterlockedCompareExchange(&g_nativeWantClose, 0, 0) != 0 ||
           InterlockedCompareExchange(&g_arenaPlusWantOpen, 0, 0) != 0 ||
           ArenaPlusNpcDelayedOpenPending() ||
           InterlockedCompareExchange(&g_sinWantOpen, 0, 0) != 0 ||
           InterlockedCompareExchange(&g_f7WantOpenKind, -1, -1) >= 0;
}

static bool NativeMenuOtherOwnerOrRequestPublished() {
    return InterlockedCompareExchange(&g_nativeOtherOwnerPublished, 0, 0) != 0 ||
           NativeMenuHubCloseDrainPending() ||
           InterlockedCompareExchange(&EquipmentMenu::wantOpen,0,0)!=0 ||
           InterlockedCompareExchange(&g_nativeWantSpawn, 0, 0) != 0 ||
           InterlockedCompareExchange(&g_nativeWantClose, 0, 0) != 0 ||
           InterlockedCompareExchange(&g_arenaPlusWantOpen, 0, 0) != 0 ||
           ArenaPlusNpcDelayedOpenPending() ||
           InterlockedCompareExchange(&g_sinWantOpen, 0, 0) != 0 ||
           InterlockedCompareExchange(&g_f7WantOpenKind, -1, -1) >= 0;
}

static void NativeMenuPublishOwnerReservationFromPumpState() {
    // WHY: request consumption and raw object publication form one ownership
    // handoff. Clear the reservation only after Pump sees neither side owned.
    InterlockedExchange(
        &g_nativeOtherOwnerPublished,
        NativeMenuOtherOwnerOrRequestActivePumpOnly() ? 1 : 0);
}

// Runs on PRESENT (every frame, ALL contexts -- field/dialogue too).
// F7 toggles the "force". While on, rewrites dword_13407E4=1 -> the field tick starts CALLING
// the pump on field -> our menu draws/ticks WITHOUT a competing game menu. (The field clears the
// gate on its own each frame; that is why we rewrite it every frame.) Proof: forcing the gate for
// ~frames did not crash (RT2 probe).
static bool F8MovieOwnsRate(){return FfxHooks::Config::GetBool("boosters.speed_hack_fmv",false)&&FfxHooks::FmvSpeed::Playing();}
static void F8MoviePublish(unsigned factor,bool admitted,unsigned epoch){FfxHooks::FmvSpeed::SetDesired(factor,admitted,epoch);}
static void F8MovieDecorate(FfxHooks::SpeedHackRuntimeSnapshot* out){
    if(!out)return;const auto movie=FfxHooks::FmvSpeed::CurrentStatus();
    if(!movie.playing || movie.requested<=1)return;
    out->requestedFactor=static_cast<uint8_t>(movie.requested);out->backend=FfxHooks::SpeedHackBackend::Movie;
    out->routedFactor=static_cast<uint8_t>(movie.applied);out->appliedFactor=static_cast<uint8_t>(movie.applied);
    using C=FfxHooks::FmvSpeed::Code;using P=FfxHooks::SpeedHackRuntimePhase;
    out->phase=movie.code==C::Applied||movie.code==C::Limited?P::Applied:movie.code==C::Conflict?P::Conflict:
        movie.code==C::AudioUnavailable||movie.code==C::Unsupported?P::Unavailable:movie.code==C::RestorePending?P::Paused:P::Armed;
}
static const FfxHooks::SpeedHackMovieBridge g_f8MovieBridge{F8MovieOwnsRate,FfxHooks::FmvSpeed::PublicationEpoch,F8MoviePublish,FfxHooks::FmvSpeed::Neutralize,F8MovieDecorate};
static bool F7RootInputAdmitted() {
    return F7IsForegroundWindow() && !FfxHooks::NativePorts::MenuOpeningPadHeld();
}
static FfxHooks::DashShortcutSample F8ConfiguredShortcut() {
    const auto value=FfxHooks::NativePorts::SampleShortcut(FfxHooks::NativeBindings::Action::MenuF8);
    return {value.down,value.mismatch};
}
static bool F8ConfiguredSpeedShortcut() {
    static bool suppressed=false;
    const bool down=FfxHooks::NativePorts::BindingDown(FfxHooks::NativeBindings::Action::SpeedCycle);
    if(!down)suppressed=false;
    if(down && (F7OwnsUiPublishedForPresent() || g_f8MenuOpen.Load()))suppressed=true;
    return down && !suppressed;
}
static void NativeMenu_PresentTick() {
    if (!g_base) return;

    NativeMenuBoundaryTraceTick();   // diagnostic sampler; gated + SEH inside
    ArenaPlus_PublishMixAvailability();

    // WHY: the menu pump may stop immediately after Launch. Present remains the
    // existing per-frame producer, so the 30-second one-shot expires even when
    // no later battle or native-menu callback arrives to claim it.
    FfxHooks::CustomMixUltra::Runtime::ProductionTick(GetTickCount64());
    if (!ArenaPlus_MixEnabled()) FfxHooks::CustomMixUltra::Runtime::ProductionClearPositionBattle();
    if (!ArenaPlus_MixEnabled() &&
        FfxHooks::CustomMixUltra::Runtime::ProductionStatus().code ==
            FfxHooks::CustomMixUltra::Runtime::StatusCode::Queued) {
        FfxHooks::CustomMixUltra::Runtime::ProductionCancel(
            FfxHooks::CustomMixUltra::Runtime::CancelReason::Cancel);
    }

    if (InterlockedCompareExchange(&g_nativeMenuProducerReady, 0, 0) == 0) return;
    static LONG firstReadyPresent=0;
    if(InterlockedCompareExchange(&firstReadyPresent,1,0)==0)StartupTiming("first-ready-present");
    const bool f7Foreground = F7IsForegroundWindow();

    FfxHooks::NativePorts::Tick(!F7OwnsUiPublishedForPresent() && !g_f8MenuOpen.Load());
    FfxHooks::Dash_Tick(f7Foreground);
    FfxHooks::F7_SinObserveLocation();
    static bool workshopHeld=false;
    const auto workshopShortcut=FfxHooks::NativePorts::SampleShortcut(FfxHooks::NativeBindings::Action::Workshop);
    const bool workshopDown=workshopShortcut.down&&!workshopShortcut.mismatch;
    if(f7Foreground&&workshopDown&&!workshopHeld&&!FfxHooks::NativePorts::BindingCaptureActive()){
        if(EquipmentMenu::Active())InterlockedExchange(&EquipmentMenu::wantClose,1);
        else if(!F7OwnsUiPublishedForPresent()&&!g_f8MenuOpen.Load()&&!FfxHooks::Maechen_BlocksNativeModalAllocation()&&
            *reinterpret_cast<volatile int*>(g_base+(0x13407E4u-0x400000u))==0){
            InterlockedExchange(&EquipmentMenu::wantOpen,1);InterlockedExchange(&g_forceSubsystem,1);
        }
    }
    workshopHeld=workshopDown;
    if (FfxHooks::Dash_F8Pressed() && !FfxHooks::NativePorts::BindingCaptureActive() && !EquipmentMenu::Active()) {
        if (g_f8MenuOpen.Exchange(false)) {
            // F8 closes the menu, never commits an unfinished scalar draft.
            g_f8ScalarEditor.Cancel();
            InterlockedExchange(&g_nativeWantClose, 1);
        } else if (FfxHooks::Maechen_BlocksNativeModalAllocation()) {
            // WHY: closing an existing F8 menu stays available above, but a new
            // F8 request must not race Maechen menu ownership or its terminal reap wake.
            Log("[ffx-hooks] F8 BLOCKED (Maechen owns the native pump)\n");
        } else {
            const bool gameMenuOpen =
                *reinterpret_cast<volatile int*>(g_base + (0x13407E4u - 0x400000u)) != 0;
            if (gameMenuOpen) {
                Log("[ffx-hooks] F8 BLOCKED (game menu already open) - close the game menu first\n");
            } else {
                g_f8MenuOpen.Store(true);
                InterlockedExchange(&g_forceSubsystem, 1);
                // Publish the kind before the spawn-ready edge so Pump cannot
                // consume a direct F8 request before its subtype is visible.
                InterlockedExchange(&g_f7WantOpenKind, F7_MENU_FLAGS);
                InterlockedExchange(&g_nativeWantSpawn, 1);
                Log("[ffx-hooks] F8 -> opening catalog FLAGS submenu\n");
            }
        }
    }

    static bool s_hk = false;
    static bool s_hkChordSuppressed = false;
    static DWORD s_hkLastEdge = 0;
    const bool configurable=FfxHooks::NativePorts::Status().supported;
    const auto shortcut=FfxHooks::NativePorts::SampleShortcut(FfxHooks::NativeBindings::Action::MenuF7);
    const bool down = configurable?shortcut.down:(GetAsyncKeyState(g_nativeMenuHotkey) & 0x8000) != 0;
    const bool modifiersDown = configurable?shortcut.mismatch:
        (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 ||
        (GetAsyncKeyState(VK_MENU) & 0x8000) != 0 ||
        (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (!down) s_hkChordSuppressed = false;
    if (down && (modifiersDown || FfxHooks::NativePorts::BindingCaptureActive())) s_hkChordSuppressed = true;
    const bool edge = f7Foreground && down && !s_hk && !s_hkChordSuppressed && !EquipmentMenu::Active();
    // Sampling the held bit while backgrounded prevents a key held in another
    // application from becoming a fresh F7 edge when focus returns.
    s_hk = down;
    const bool focusLost =
        InterlockedExchange(&g_f7ForegroundLost, 0) != 0 || !f7Foreground;
    if (focusLost &&
        FfxHooks::CustomMixUltra::Runtime::ProductionStatus().code ==
            FfxHooks::CustomMixUltra::Runtime::StatusCode::Queued) {
        FfxHooks::CustomMixUltra::Runtime::ProductionCancel(
            FfxHooks::CustomMixUltra::Runtime::CancelReason::FocusLoss);
        Log("[ffx-hooks] ArenaPlus: CustomMix Ultra queued request canceled on focus loss\n");
    }
    if(!f7Foreground&&InterlockedExchange(&EquipmentMenu::wantOpen,0)!=0)
        Log("[ffx-hooks] Workshop UI: pending open canceled on focus loss\n");
    if(focusLost&&EquipmentMenu::Active())InterlockedExchange(&EquipmentMenu::wantClose,1);
    if (focusLost && F7OwnsUiPublishedForPresent() && !EquipmentMenu::Active()) {
        F7RequestClose(FfxHooks::F7Ui::CloseSource::FocusLost);
    }
    if (edge) {
        if (FfxHooks::Maechen_MenuOwned()) {
            Log("[ffx-hooks] NativeMenu: F7 BLOCKED (Maechen owns the native menu)\n");
        } else {
        // FIX 2026-08-02 (user RT2): F7 toggle debounce (250ms)
        // a stuck F7 (or key) toggles the menu every frame = flicker. Same pattern as EKey.
        const DWORD now = GetTickCount();
        if (now - s_hkLastEdge >= 250) {
            s_hkLastEdge = now;
        if (!F7OwnsUiPublishedForPresent() &&
            InterlockedCompareExchange(&g_forceSubsystem, 0, 0) == 0) {
            // ANTI-DOUBLE-INPUT GUARD: if the subsystem is already active without our force,
            // a GAME menu is open -> do NOT open ours on top (two menus navigating = chaos).
            // Only open when the game is "clean".
            const bool gameMenuOpen =
                *reinterpret_cast<volatile int*>(g_base + (0x13407E4u - 0x400000u)) != 0;
            if (gameMenuOpen) {
                Log("[ffx-hooks] NativeMenu: F7 BLOCKED (game menu already open) - close the game menu first\n");
            } else {
                InterlockedExchange(&g_forceSubsystem, 1);
                InterlockedExchange(&g_nativeWantSpawn, 1);   // 1st F7 on clean context: enable force + request spawn
            }
        } else if (F7OwnsUiPublishedForPresent()) {
            F7RequestClose(FfxHooks::F7Ui::CloseSource::Hotkey);
        } else {
            Log("[ffx-hooks] NativeMenu: F7 BLOCKED (another modal owns the native pump)\n");
        }
        }
        }
    }

    if (NativeMenuHubCloseDrainPending() || EquipmentMenu::NeedsPump()) {
        InterlockedExchange(&g_forceSubsystem, 1);
    }
    const bool forcedByCustomMenu =
        InterlockedCompareExchange(&g_forceSubsystem, 0, 0) != 0;
    const bool gameMenuOpen =
        *reinterpret_cast<volatile int*>(g_base + (0x13407E4u - 0x400000u)) != 0 &&
        !forcedByCustomMenu;
    const bool otherCustomMenuOpen =
        g_f8MenuOpen.Load() || NativeMenuOtherOwnerOrRequestPublished();
    FfxHooks::Maechen_PresentTick(
        gameMenuOpen, otherCustomMenuOpen, f7Foreground);
    const bool maechenMenuOwned = FfxHooks::Maechen_MenuOwned();
    const bool maechenPumpWake = FfxHooks::Maechen_PumpWakePending();
    if (maechenMenuOwned || maechenPumpWake) {
        InterlockedExchange(&g_forceSubsystem, 1);
    }
    if (InterlockedCompareExchange(&g_forceSubsystem, 0, 0) != 0) {
        NativeMenuForceGatePublish();  // FORCE the gate -> pump runs on field
        // F7, Arena, and Maechen all emit native 2D strictly inside the pump's
        // batch phase via object callbacks (O_DRAW). Present-time emission
        // lands after that phase and never renders — the F9 black screen.
    }
}

// O detour do pump: MAIN THREAD, 1x/frame com o subsistema vivo.
// KEYSTONE B (2026-08-02): the F7 levers — immediate RAM effect (roadmap IFRIT_F7_INLIVE).
static void F7_LeverApply(NativeMenu::ActionId act, int val) {
    using namespace FfxHooks;
    switch (act) {
        case NativeMenu::ACT_BATTLE_CHEATS: {
            const F8FlagSpec* flag = FindF8Flag("cheats.invincible_party");
            if (!flag) {
                Log("[ffx-hooks] F7 lever: cheats.invincible_party catalog row missing\n");
                break;
            }
            const F8EditResult result = SetF8FlagValue(*flag, val != 0);
            for (int row = 0; row < NativeMenu::kRowCount; ++row) {
                if (NativeMenu::g_rows[row].action == NativeMenu::ACT_BATTLE_CHEATS) {
                    NativeMenu::g_rowValue[row] = result.effective.value ? 1 : 0;
                    break;
                }
            }
            Log("[ffx-hooks] F7 lever: key=%s edit=%s requested=%d effective=%d source=%s "
                "runtime=%s has_readback=%d readback=%d\n",
                flag->gate.canonicalKey,
                F8EditCodeName(result.code),
                result.requestedValue ? 1 : 0,
                result.effective.value ? 1 : 0,
                Config::BoolSourceName(result.effective.source),
                F8AvailabilityName(result.runtime.availability),
                result.runtime.hasAppliedValue ? 1 : 0,
                result.runtime.appliedValue ? 1 : 0);
            break;
        }
        case NativeMenu::ACT_FORCE_BATTLE:      // force com o field do stepper (F7 tick-based)
            F7_ForceFieldBattle(val, 0);
            Log("[ffx-hooks] F7 lever: force battle field=%d group=0 (queued)\n", val);
            break;
        default:
            break;
    }
}

static int __cdecl NativeMenu_PumpHook(unsigned int a1) {
    // High-confidence render-visibility RE: clear both 2D kill switches before the pump because
    // its menu batch enqueue/flush runs inside this call. 0x12FB790 disables 2D; 0x12FB798 skips upload.
    if (InterlockedCompareExchange(&g_forceSubsystem, 0, 0) != 0 && g_base) {
        *reinterpret_cast<volatile int*>(g_base + (0x12FB790u - 0x400000u)) = 0;  // g_Render2D_Disabled = 0
        *reinterpret_cast<volatile int*>(g_base + (0x12FB798u - 0x400000u)) = 0;  // Do not skip batch upload.
    }
    // Resolve hub pointer input before the native update callback samples the
    // controller. A pressed row is authoritative for this frame; movement-only
    // hover still leaves the callback free to accept controller navigation.
    if (g_nativeMenu.obj) F7MainMenuMouseTick(g_nativeMenu.obj);
    // 1) Run the original pump. The game updates/draws while OurDraw queues menu quads in its batch.
    int r = reinterpret_cast<int(__cdecl*)(unsigned int)>(g_nativeMenuPumpTramp)(a1);
    NativeMenuPollHubCloseDrainAfterPump();
    // After the pump, but before field tick sub_820C00 checks 0x820DE2, keep unk_13407E4=1.
    // This follows the vanilla menu path that suppresses field 3D, so our queued menu remains visible.
    if (InterlockedCompareExchange(&g_forceSubsystem, 0, 0) != 0)
        NativeMenuForceGatePublish();
    // 2) Run our bounded, reentrancy-guarded work under SEH.
    if (InterlockedCompareExchange(&g_nativeMenuInHook, 1, 0) != 0) return r;
    __try {
        ArenaTrace_MenuPoolTick("NativeMenu_PumpHook");
        ArenaPlus_TickScenarioBackdropPending();
        ArenaPlus_TickEncounterPinPending();
        ArenaPlus_TickDeferredFileRestore();
        FfxHooks::F7_TickMainThread();   // Bounded Force scheduler plus redundant CustomMix deadline tick.
        FfxHooks::Maechen_PumpTick(F7IsForegroundWindow());
        EquipmentMenu::Tick();
        // WHY: visible menu ownership and the one-shot reap wake are separate.
        // Clear shared force only after both and every other pump owner are gone.
        if (!FfxHooks::Maechen_MenuOwned() &&
            !FfxHooks::Maechen_PumpWakePending() &&
            !NativeMenuOtherOwnerOrRequestActivePumpOnly()) {
            InterlockedExchange(&g_forceSubsystem, 0);
            NativeMenuForceGateClear();
        }
        // F7 input runs from Present, which wakes the menu pump; this callback only consumes requests.
        // A forced wake can reach field gameplay, so modal ownership must be checked before allocation.
        if (FfxHooks::NativeMenu_ReserveAndConsumeOpenRequest(
                &g_nativeWantSpawn, 0, &g_nativeOtherOwnerPublished) != 0) {
            const LONG pendingKind = FfxHooks::NativeMenu_ReserveAndConsumeOpenRequest(
                &g_f7WantOpenKind, -1, &g_nativeOtherOwnerPublished);
            // Reaching this callback proves the forced pump is live for the current allocation attempt.
            const bool idle = NativeMenuLegacyModalAllocationIdle() &&
                (NativeMenu_SubsystemLive() ||
                 InterlockedCompareExchange(&g_forceSubsystem, 0, 0) != 0);
            // F7 never publishes FLAGS; this consumed subtype is the stable
            // request identity even if Present changed the local open latch.
            const bool directF8FlagsRequest = pendingKind == F7_MENU_FLAGS;
            const FfxHooks::F8Ui::PendingOpenDisposition openDisposition =
                FfxHooks::F8Ui::DecidePendingOpen(idle, directF8FlagsRequest);
            if (pendingKind >= 0 &&
                openDisposition == FfxHooks::F8Ui::PendingOpenDisposition::Allocate) {
                g_f7Menu = F7Sub_SpawnMenu(static_cast<int>(pendingKind));
                Log("[ffx-hooks] F7: direct submenu %d opened obj=0x%08X\n",
                    static_cast<int>(pendingKind), static_cast<unsigned>(g_f7Menu.obj));
                if (!g_f7Menu.obj) InterlockedExchange(&g_forceSubsystem, 0);
            } else if (pendingKind < 0 &&
                       openDisposition == FfxHooks::F8Ui::PendingOpenDisposition::Allocate) {
                g_nativeMenu = SpawnHydratedNativeMenu();
                Log("[ffx-hooks] NativeMenu open obj=0x%08X (force-gate)\n", (unsigned)g_nativeMenu.obj);
            } else if (pendingKind >= 0) {
                if (openDisposition ==
                    FfxHooks::F8Ui::PendingOpenDisposition::RejectDirectF8) {
                    // WHY: both request atoms are already consumed. Roll back
                    // only F8-local pre-open state; Maechen still owns its wake,
                    // shared force, and any modal lifecycle in this race.
                    F8RollbackRejectedDirectOpen();
                    Log("[ffx-hooks] F8: FLAGS open rejected (native pump busy); local state rolled back\n");
                } else {
                    Log("[ffx-hooks] F7: direct submenu %d not opened (busy)\n", static_cast<int>(pendingKind));
                }
            }
        }
        const LONG f7CloseSource = InterlockedExchange(&g_f7CloseSourcePending, -1);
        if (f7CloseSource >= 0) {
            F7CloseTransition(
                static_cast<FfxHooks::F7Ui::CloseSource>(f7CloseSource),
                FfxHooks::F7Ui::CloseDestination::Game);
        }
        if (InterlockedExchange(&g_nativeWantClose, 0) != 0) {
            if (g_arenaPlusMenuKind == ArenaPlusMenuKind::Ultra || ArenaPlus_IsUltraChild(g_arenaPlusMenuKind) ||
                FfxHooks::CustomMixUltra::Runtime::ProductionStatus().code ==
                    FfxHooks::CustomMixUltra::Runtime::StatusCode::Queued) {
                FfxHooks::CustomMixUltra::Runtime::ProductionCancel(
                    FfxHooks::CustomMixUltra::Runtime::CancelReason::Close);
                g_arenaPlusUltraSelection = {};
            }
            NativeMenu::CloseMenu(g_nativeMenu); g_nativeHeldAction = -1;
            ArenaPlus_CloseMenu(g_arenaPlusMenu);
            SinCurse_CloseMenu();
            EquipmentMenu::Close();
            F7Sub_CloseMenu();
            ArenaPlusComposePick_Close();
            InterlockedExchange(&g_arenaPlusWantOpen, 0);
            InterlockedExchange(&g_sinWantOpen, 0);
            InterlockedExchange(&g_f7WantOpenKind, -1);
            InterlockedExchange(&g_forceSubsystem, 0);   // Release the forced pump so field rendering returns to normal.
            NativeMenuForceGateClear();  // Clear the direct gate that previously caused a black screen.
            Log("[ffx-hooks] NativeMenu close (force-gate off)\n");
        }
        // Retire every queued legacy close before allocating a different menu.
        // Otherwise an old F7/F8 close can consume Workshop's first frame and
        // release the force gate while its native object still needs a drain.
        if(InterlockedCompareExchange(&EquipmentMenu::wantOpen,0,0) && NativeMenuLegacyModalAllocationIdle()){
            FfxHooks::NativeMenu_ReserveAndConsumeOpenRequest(&EquipmentMenu::wantOpen, 0, &g_nativeOtherOwnerPublished);
            if(EquipmentMenu::Open()){
                InterlockedExchange(&g_forceSubsystem,1);
                NativeMenuForceGatePublish();
            }else{
                Log("[ffx-hooks] Workshop UI: open declined; reservation will be reconciled\n");
            }
        }
        NativeMenuPromoteDelayedArenaRequestIfReady();
        NativeMenuTryOpenArenaRequest();
        if (g_nativeHeldAction >= 0) {
            NativeMenu_TickHeld();
        } else if (ArenaPlusComposePick_IsActive()) {
            ArenaPlusComposePick_Tick();
            const ArenaPlusComposePollResult cp = ArenaPlusComposePick_PollMenu();
            if (cp.what == ArenaPlusComposePollKind::Launch) {
                ArenaPlusComposePick_Close();
                // Compose-crash fix (2026-08-02): release the force gate before MsBattleEncountExe.
                // Starting battle with 0x13407E4=1 kept menu mode live and crashed UpdateWindowTitle.
                InterlockedExchange(&g_forceSubsystem, 0);
                NativeMenuForceGateClear();  // Clear the direct gate.
                const bool queued = ArenaPlus_LaunchComboBattleFromPump(cp.combo);
                if (queued) {
                    g_nativeHeldAction = -1;
                    Log("[ffx-hooks] ArenaPlus: custom mix composed+queued combo=%d\n", cp.combo);
                } else {
                    Log("[ffx-hooks] ArenaPlus: custom mix launch failed combo=%d; reopening picker\n", cp.combo);
                    if (!ArenaPlusComposePick_Open(cp.combo))
                        g_arenaPlusMenu = ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::CustomMix);
                }
            } else if (cp.what == ArenaPlusComposePollKind::LaunchCached) {
                ArenaPlusComposePick_Close();
                InterlockedExchange(&g_forceSubsystem, 0);   // Apply the same clean relaunch fix as the normal Launch path.
                NativeMenuForceGateClear();
                const bool queued = ArenaPlus_LaunchComboBattleFromPump(cp.combo);
                if (queued) {
                    g_nativeHeldAction = -1;
                    Log("[ffx-hooks] ArenaPlus: custom mix relaunch (cached bin) combo=%d\n", cp.combo);
                } else {
                    Log("[ffx-hooks] ArenaPlus: cached mix launch failed combo=%d; reopening picker\n", cp.combo);
                    if (!ArenaPlusComposePick_Open(cp.combo))
                        g_arenaPlusMenu = ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::CustomMix);
                }
            } else if (cp.what == ArenaPlusComposePollKind::Back) {
                ArenaPlusComposePick_Close();
                g_arenaPlusMenu = ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::CustomMix);
                if (!g_arenaPlusMenu.obj) InterlockedExchange(&g_forceSubsystem, 0);
                Log("[ffx-hooks] ArenaPlus: compose pick back to Custom Mix sub-menu\n");
            }
        } else if (g_arenaPlusMenu.obj) {
            NativeMenu::Poll p = ArenaPlus_PollMenu(g_arenaPlusMenu);
            if (p.what == NativeMenu::POLL_CONFIRM) {
                const int row = p.row;
                ArenaPlus_CloseMenu(g_arenaPlusMenu);
                ArenaPlus_HandleMenuConfirm(row);
            } else if (p.what == NativeMenu::POLL_CANCEL) {
                ArenaPlus_CloseMenu(g_arenaPlusMenu);
                if(g_arenaPlusMenuKind==ArenaPlusMenuKind::Library || g_arenaPlusMenuKind==ArenaPlusMenuKind::LibraryItem || g_arenaPlusMenuKind==ArenaPlusMenuKind::Rename){
                    ArenaLibraryHandle(ArenaPlus_SubMenuBackRow(g_arenaPlusMenuKind));
                } else if(g_arenaPlusMenuKind==ArenaPlusMenuKind::Search){
                    ArenaPlus_FinishSearch(false);
                } else if(g_arenaPlusMenuKind==ArenaPlusMenuKind::BattleDetail){
                    g_arenaPlusMenu=ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::Battles);
                } else if (ArenaPlus_IsUltraChild(g_arenaPlusMenuKind)) {
                    ArenaPlus_Ultra_Reopen();
                } else if (g_arenaPlusMenuKind == ArenaPlusMenuKind::Scenery) {
                    ArenaPlus_Ultra_Reopen();
                } else if (g_arenaPlusMenuKind == ArenaPlusMenuKind::Positions) {
                    g_arenaPositionDraft = {};
                    ArenaPlus_Ultra_Reopen();
                } else if (g_arenaPlusMenuKind == ArenaPlusMenuKind::Ultra) {
                    FfxHooks::CustomMixUltra::Runtime::ProductionCancel(
                        FfxHooks::CustomMixUltra::Runtime::CancelReason::Cancel);
                    g_arenaPlusUltraSelection = {};
                    g_arenaPlusMenu = g_arenaPlusMixRequiredSlots
                        ? ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::CustomMix) : ArenaPlus_SpawnHubMenu();
                    if (!g_arenaPlusMenu.obj) {
                        InterlockedExchange(&g_forceSubsystem, 0);
                    }
                    Log("[ffx-hooks] ArenaPlus: CustomMix Ultra cancel to Arena+ hub\n");
                } else {
                    F7CloseTransition(
                        FfxHooks::F7Ui::CloseSource::Cancel,
                        FfxHooks::F7Ui::CloseDestination::Hub);
                }
            }
        } else if (g_sinMenu.obj) {
            NativeMenu::Poll p = SinCurse_PollMenu(g_sinMenu);
            if (p.what == NativeMenu::POLL_CONFIRM) {
                const int row = p.row;
                SinCurse_CloseMenu();
                SinCurse_HandleConfirm(row);
            } else if (p.what == NativeMenu::POLL_CANCEL) {
                SinCurse_CloseMenu();
                F7CloseTransition(
                    FfxHooks::F7Ui::CloseSource::Cancel,
                    FfxHooks::F7Ui::CloseDestination::Hub);
            }
        } else if (g_f7Menu.obj) {
            NativeMenu::Poll p = F7Sub_PollMenu(g_f7Menu);
            if (p.what == NativeMenu::POLL_CONFIRM) {
                const int row = p.row;
                const bool wasDirectF8Flags =
                    g_f8MenuOpen.Load() && g_f7MenuKind == F7_MENU_FLAGS;
                F7Sub_CloseMenu();
                F7Sub_HandleConfirm(row);
                if (row >= 0) {
                    if (g_f7MenuKind == F7_MENU_FORCE && (row == 0 || row == 1)) {
                        // Both force execution and the explicit Repeat save keep
                        // the editor open so the player can verify or adjust again.
                        g_f7Menu = F7Sub_SpawnMenu(F7_MENU_FORCE);
                        if (!g_f7Menu.obj) InterlockedExchange(&g_forceSubsystem, 0);
                    } else if (wasDirectF8Flags) {
                        F8ReturnFlagsToGame();
                    } else {
                        F7CloseTransition(
                            FfxHooks::F7Ui::CloseSource::BackRow,
                            FfxHooks::F7Ui::CloseDestination::Hub);
                        InterlockedExchange(&g_nativeWantSpawn, 1); // Reopen through the pump path that avoids the historical black screen.
                    }
                }
            } else if (p.what == NativeMenu::POLL_CANCEL) {
                const bool wasDirectF8Flags =
                    g_f8MenuOpen.Load() && g_f7MenuKind == F7_MENU_FLAGS;
                F7Sub_CloseMenu();
                if (wasDirectF8Flags) {
                    F8ReturnFlagsToGame();
                } else {
                    F7CloseTransition(
                        FfxHooks::F7Ui::CloseSource::Cancel,
                        FfxHooks::F7Ui::CloseDestination::Hub);
                    InterlockedExchange(&g_nativeWantSpawn, 1); // Use the pump path proven to avoid the historical black screen.
                }
            }
        } else if (g_nativeMenu.obj) {
            // WHY (2026-08-02, RT2): the pump must PollMenu and, on Confirm, call CloseMenu followed
            // by DispatchConfirm/NativeMenu_OnEdge. This opens or reopens submenus and releases force
            // ownership. An older adapter consumed g_ourClosed before this block, stole Confirm, and
            // prevented every submenu from opening. Do not consume g_ourClosed earlier.
            NativeMenu::Poll p = NativeMenu::PollMenu(g_nativeMenu);
            if (p.what == NativeMenu::POLL_CONFIRM) {
                const bool inRange = (p.row >= 0 && p.row < NativeMenu::kRowCount);
                const NativeMenu::ActionId act = inRange ? NativeMenu::g_rows[p.row].action : NativeMenu::ACT_EXIT;
                const bool held = inRange && (NativeMenu::g_rows[p.row].kind == NativeMenu::HELD);
                const int closingHubObject = g_nativeMenu.obj;
                NativeMenu::CloseMenu(g_nativeMenu);     // Generic input closes on Confirm (+66=1).
                NativeMenu::ReleaseModalIfOwned(closingHubObject);
                NativeMenuQueueHubCloseDrain(closingHubObject, true);
                NativeMenu::DispatchConfirm(p.row);      // Invoke the edge/held bridge.
                const FfxHooks::F8Ui::ArenaOpenResult arenaResult =
                    NativeMenuTryOpenArenaRequest();
                if (arenaResult == FfxHooks::F8Ui::ArenaOpenResult::NoRequest) {
                    // WHY: DeferredBlocked still owns the ACT_ARENA request. Only
                    // true absence may fall through to Sin/F7/native allocation.
                    const bool sinRequested = FfxHooks::NativeMenu_ReserveAndConsumeOpenRequest(
                        &g_sinWantOpen, 0, &g_nativeOtherOwnerPublished) != 0;
                    if (sinRequested) {
                        g_sinMenu = SinCurse_SpawnMenu();
                        if (!g_sinMenu.obj) {
                            g_nativeMenu = SpawnHydratedNativeMenu();
                            if (!g_nativeMenu.obj) InterlockedExchange(&g_forceSubsystem, 0);
                        }
                        Log("[ffx-hooks] SinCurse: submenu opened obj=0x%08X\n", static_cast<unsigned>(g_sinMenu.obj));
                    } else {
                        const LONG f7Kind = FfxHooks::NativeMenu_ReserveAndConsumeOpenRequest(
                            &g_f7WantOpenKind, -1, &g_nativeOtherOwnerPublished);
                        if (f7Kind >= 0) {
                            g_f7Menu = F7Sub_SpawnMenu((int)f7Kind);
                            if (!g_f7Menu.obj) {
                                g_nativeMenu = SpawnHydratedNativeMenu();
                                if (!g_nativeMenu.obj) InterlockedExchange(&g_forceSubsystem, 0);
                            }
                            Log("[ffx-hooks] F7: submenu %d opened obj=0x%08X\n", (int)f7Kind, static_cast<unsigned>(g_f7Menu.obj));
                        } else if (act != NativeMenu::ACT_EXIT && !held)
                            g_nativeMenu = SpawnHydratedNativeMenu();  // Reopen the persistent hub for the next choice.
                        else if (act == NativeMenu::ACT_EXIT) {
                            F7CloseTransition(
                                FfxHooks::F7Ui::CloseSource::BackRow,
                                FfxHooks::F7Ui::CloseDestination::Game);
                        }
                    }
                }
            } else if (p.what == NativeMenu::POLL_CANCEL) {
                F7CloseTransition(
                    FfxHooks::F7Ui::CloseSource::Cancel,
                    FfxHooks::F7Ui::CloseDestination::Game);
            }
        }
        static int s_drawDbg = 0;
        if (g_nativeMenu.obj && ((++s_drawDbg) % 120) == 0)
            Log("[ffx-hooks] NativeMenu DBG OurDraw calls=%d (up=drawing but covered; stopped=NOT drawing)\n", NativeMenu::g_ourDrawCalls);
        NativeMenuPublishOwnerReservationFromPumpState();
        if (g_f7CursorShowIncrements > 0 && !F7OwnsVisibleUi() &&
            InterlockedCompareExchange(&g_forceSubsystem, 0, 0) == 0) {
            // Battle-launch actions may terminate the menu family without a
            // Back event. Reconcile the remaining cursor owner here, on Pump,
            // where the portable modal mirror is otherwise exclusively owned.
            const FfxHooks::F7Ui::CloseEffects effects = FfxHooks::F7Ui::CloseModal(
                g_f7UiModalState, FfxHooks::F7Ui::CloseSource::Stop,
                FfxHooks::F7Ui::CloseDestination::Game);
            if (effects.releaseCursor) F7ReleaseCursorOwnership();
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[ffx-hooks] WARN NativeMenu hook exception\n");
    }
    InterlockedExchange(&g_nativeMenuInHook, 0);
    return r;
}

/* â”€â”€ F7 In-Live submenus: definitions (enum/globals at top of section) â”€â”€â”€â”€â”€ */
static const char* F7BoolName(int v)          { return v ? "ON" : "OFF"; }
static const char* F7MulName(int permille)    { _snprintf_s(g_f7FmtBuf, sizeof(g_f7FmtBuf), _TRUNCATE, "x%d.%02d", permille / 1000, (permille % 1000) / 10); return g_f7FmtBuf; }
static const char* F7TrackName(int t) {
    if (t < 0) return "None";
    const char* n = LabMusicRuntimeName(t);   // Complete runtime crosswalk for tracks 10..181 (2026-08-02).
    if (n) return n;
    _snprintf_s(g_f7FmtBuf, sizeof(g_f7FmtBuf), _TRUNCATE, "Track %d", t);
    return g_f7FmtBuf;
}
static const char* F7PresetName(int v) {
    switch (v) {
        case 0: return "Off"; case 1: return "Hunter"; case 2: return "Sombra de Sin";
        case 3: return "True Nightmare"; default: return "Custom";
    }
}
static const char* F7AutoName(int v) {
    switch (v) {
        case 0: return "None"; case 1: return "Protect"; case 2: return "Shell";
        case 3: return "Haste"; case 4: return "Regen"; case 5: return "Reflect";
        case 6: return "Protect+Shell"; case 7: return "Full Defense";
        default: return "?";
    }
}
static const char* F7ElemName(int v) {
    switch (v) {
        case 0: return "None"; case 1: return "Fire"; case 2: return "Ice";
        case 3: return "Thunder"; case 4: return "Water"; case 5: return "Holy";
        default: return "?";
    }
}

// Map UI values to masks: auto-status bits 15..23 and element bits 0..4.
static const uint32_t F7_AUTO_MASKS[8] = {
    0x00000000u, 0x00010000u, 0x00008000u, 0x00800000u, 0x00400000u,
    0x00020000u, 0x00018000u, 0x00C38000u
};
static const uint8_t F7_ELEM_MASKS[6] = { 0x00u, 0x01u, 0x02u, 0x04u, 0x08u, 0x10u };

static int F7_AutoIdxFromMask(uint32_t mask) {
    mask &= 0x01FFFFFFu;
    for (int i = 0; i < 8; ++i) if (F7_AUTO_MASKS[i] == mask) return i;
    return 0;
}
static int F7_ElemIdxFromMask(uint8_t mask) {
    for (int i = 0; i < 6; ++i) if (F7_ELEM_MASKS[i] == mask) return i;
    return 0;
}

// F7 Difficulty multi-column state (2026-08-02, Jarvis-HOOK).
enum F7DiffCol { F7DC_PRESETS = 0, F7DC_BASE, F7DC_AUTO, F7DC_WEAK, F7DC_RESIST, F7DC_ABSORB, F7DC_ACTIONS, F7DC_COUNT };
static int g_f7Col = F7DC_PRESETS;      // Active column.
static int g_f7ColRow = 0;              // Active row within the column.
static int g_f7EditActive = 0;          // Numeric edit mode for the BASE column.
static int g_f7EditValue = 0;           // Value entered so far.
static int g_f7EditDigits = 0;          // Number of digits entered.
static int g_f7StatusTicks = 0;         // Remaining frames for the status message.
static char g_f7StatusMsg[56] = {};     // Apply/Save/Reset/preview feedback.
static const char* const F7_BASE_NAMES[9] = { "HP","STR","DEF","MAG","MDF","AGI","ACC","EVA","LCK" };
static const char* const F7_ELEM_NAMES[5] = { "Fire","Ice","Thunder","Water","Holy" };
static const int F7_BASE_MIN[9] = { 100, 100, 100, 100, 100, 100, 100, 100, 100 };
static const int F7_BASE_MAX[9] = { 10000, 5000, 5000, 5000, 5000, 5000, 5000, 5000, 5000 };  // HP supports up to 10x.

static int F7DiffColRows(int col) {
    switch (col) {
        case F7DC_PRESETS: return 4;
        case F7DC_BASE:    return 9;
        case F7DC_AUTO:    return F7_STATUS_COUNT;   // 25
        case F7DC_WEAK: case F7DC_RESIST: case F7DC_ABSORB: return 5;
        case F7DC_ACTIONS: return 3;
        default: return 1;
    }
}

static F7MouseInputResult F7DifficultyMouseTick() {
    using namespace NativeMenu;
    const F7PointerSnapshot pointer = F7CapturePointer();
    if (!pointer.valid) return {};

    const float chipWidth = NW(0.175f);
    const float chipGap = NW(0.024f);
    const FfxHooks::F7Ui::TabGeometry presets{
        NX(0.10f), NY(0.205f), chipWidth * 4.0f + chipGap * 3.0f,
        NH(0.055f), chipGap, 4};
    const FfxHooks::F7Ui::Hit preset =
        FfxHooks::F7Ui::HitTestTabs(pointer.x, pointer.y, presets);
    if (preset.kind == FfxHooks::F7Ui::HitKind::Tab) {
        const FfxHooks::F7Ui::ListPointerResolution mouse =
            FfxHooks::F7Ui::ResolveListPointerInput(g_f7ColRow, preset, pointer.decision);
        if (pointer.decision.applyHover) {
            g_f7Col = F7DC_PRESETS;
            g_f7ColRow = mouse.selection;
        }
        return F7MouseInputResult{mouse.confirm, mouse.ownsDirectionalFrame};
    }

    static const float columnX[F7DC_COUNT] = {
        0.0f, 0.045f, 0.235f, 0.465f, 0.580f, 0.695f, 0.800f};
    static const float columnWidth[F7DC_COUNT] = {
        0.0f, 0.180f, 0.215f, 0.105f, 0.105f, 0.095f, 0.145f};
    for (int column = F7DC_BASE; column <= F7DC_ACTIONS; ++column) {
        const FfxHooks::F7Ui::ListGeometry rows{
            NX(columnX[column]), NY(0.300f), NW(columnWidth[column]),
            NH(0.022f), NH(0.020f), F7DiffColRows(column)};
        const FfxHooks::F7Ui::Hit hit = FfxHooks::F7Ui::HitTestRows(
            pointer.x, pointer.y, rows, 0, F7DiffColRows(column));
        if (hit.kind != FfxHooks::F7Ui::HitKind::Row) continue;
        const FfxHooks::F7Ui::ListPointerResolution mouse =
            FfxHooks::F7Ui::ResolveListPointerInput(g_f7ColRow, hit, pointer.decision);
        if (pointer.decision.applyHover) {
            g_f7Col = column;
            g_f7ColRow = mouse.selection;
        }
        if (pointer.decision.applyHover && pointer.wheelSteps != 0) {
            int firstVisible = 0;
            FfxHooks::F7Ui::ScrollList(
                pointer.wheelSteps, F7DiffColRows(column), F7DiffColRows(column),
                g_f7ColRow, firstVisible);
        }
        return F7MouseInputResult{mouse.confirm, mouse.ownsDirectionalFrame};
    }
    return {};
}

static void F7DiffSetStatus(const char* msg) {
    FfxHooks::F7Ui::CopyBoundedStatus(msg, g_f7StatusMsg, sizeof(g_f7StatusMsg));
    g_f7StatusTicks = 150;
}

static bool F7_SaveConfigWithFeedback(const char* successMessage) {
    const bool saved = FfxHooks::F7_SaveConfig();
    F7DiffSetStatus(saved ? successMessage :
        "Config save failed; changes remain in memory");
    NativeMenu::PlaySfx(saved ? 4 : 3);
    return saved;
}

static void F7DiffToggleBit(int valIdx, int bit) {
    using namespace NativeMenu;
    g_f7Vals[valIdx] ^= (1 << bit);   // Multi-select checkbox.
    if (valIdx >= 11 && valIdx <= 13 && (g_f7Vals[valIdx] & (1 << bit)) != 0) {
        // One affinity per element; other elements keep their own selections.
        for (int other = 11; other <= 13; ++other)
            if (other != valIdx) g_f7Vals[other] &= ~(1 << bit);
    }
    g_f7DiffPresetIdx = -1;
    PlaySfx(1);
}

static void F7_DiffPresetFill(int preset) {
    // Difficulty values (multi-column layout, 2026-08-02):
    //   [0]=preset [1..9]=hp,str,def,mag,mdf,agi,acc,eva,lck (permille)
    //   [10]=autoStatusMask (bits 0..24) [11]=elemWeak [12]=elemResist [13]=elemAbsorb (bits 0..4)
    g_f7DiffPresetIdx = preset;
    g_f7DifficultyEnabled = preset != 0;
    g_f7Vals[0] = preset;
    switch (preset) {
        case 0:  // Off
            g_f7Vals[1]=1000; g_f7Vals[2]=1000; g_f7Vals[3]=1000; g_f7Vals[4]=1000;
            g_f7Vals[5]=1000; g_f7Vals[6]=1000; g_f7Vals[7]=1000; g_f7Vals[8]=1000; g_f7Vals[9]=1000;
            g_f7Vals[10]=0; g_f7Vals[11]=0; g_f7Vals[12]=0; g_f7Vals[13]=0;
            break;
        case 1:  // Hunter
            g_f7Vals[1]=1500; g_f7Vals[2]=1250; g_f7Vals[3]=1200; g_f7Vals[4]=1100;
            g_f7Vals[5]=1100; g_f7Vals[6]=1200; g_f7Vals[7]=1000; g_f7Vals[8]=1000; g_f7Vals[9]=1000;
            g_f7Vals[10]=0; g_f7Vals[11]=0; g_f7Vals[12]=0; g_f7Vals[13]=0;
            break;
        case 2:  // Sombra de Sin: HP 2x, stats +50%, auto Haste+Protect, resist Holy
            g_f7Vals[1]=2000; g_f7Vals[2]=1500; g_f7Vals[3]=1500; g_f7Vals[4]=1300;
            g_f7Vals[5]=1300; g_f7Vals[6]=1400; g_f7Vals[7]=1000; g_f7Vals[8]=1000; g_f7Vals[9]=1000;
            g_f7Vals[10] = (1u << 23) | (1u << 16);   // Haste(23) + Protect(16)
            g_f7Vals[11]=0; g_f7Vals[12]=0x10; g_f7Vals[13]=0;
            break;
        default: // True Nightmare: HP 3x, stats about 2x, auto Protect+Shell, no elemental modifiers.
            g_f7Vals[1]=3000; g_f7Vals[2]=2200; g_f7Vals[3]=2200; g_f7Vals[4]=2000;
            g_f7Vals[5]=2000; g_f7Vals[6]=2000; g_f7Vals[7]=1500; g_f7Vals[8]=1500; g_f7Vals[9]=1500;
            g_f7Vals[10] = (1u << 23) | (1u << 22) | (1u << 16) | (1u << 15);   // Haste+Regen+Protect+Shell
            g_f7Vals[11]=0; g_f7Vals[12]=0; g_f7Vals[13]=0;
            break;
    }
}

static void F8RefreshScalarLabel(int row) {
    if (row < 0 || row >= g_f7FlagCount || row >= 32 ||
        g_f7Rows[row].type != F7RT_SCALAR || !g_f7FlagSpecs[row] ||
        !g_f7FlagSpecs[row]->scalar) {
        return;
    }

    const FfxHooks::F8FlagSpec& flag = *g_f7FlagSpecs[row];
    const bool editing = g_f8ScalarEditor.Active() && g_f8ScalarEditor.Row() == row;
    const FfxHooks::F8ScalarResult configured = FfxHooks::ResolveF8Scalar(flag);
    const bool valid = editing || configured.state != FfxHooks::F8ScalarState::Invalid;
    const int value = editing ? g_f8ScalarEditor.Draft() : configured.value;
    const bool itemCap = strcmp(flag.gate.canonicalKey, "labs.item_stack_cap") == 0;
    const char* rateName = itemCap ? "Item Cap" :
        strcmp(flag.gate.canonicalKey, "cheats.ap_100x") == 0 ? "AP Rate" : "Gil Rate";
    if (valid) {
        _snprintf_s(g_f8ScalarLabels[row], sizeof(g_f8ScalarLabels[row]), _TRUNCATE,
                    "%s [%d%s%s]", rateName, value, itemCap ? "" : "x", editing ? "_" : "");
    } else {
        _snprintf_s(g_f8ScalarLabels[row], sizeof(g_f8ScalarLabels[row]), _TRUNCATE,
                    "%s [INVALID]", rateName);
    }
    // Re-encode the one existing row label instead of drawing a second value string: the game's
    // native text pool corrupts beyond its small per-frame budget.
    NativeMenu::EncodeLabel(
        g_f8ScalarLabels[row], g_f7Labels[row], static_cast<int>(sizeof(g_f7Labels[row])));
}

#include "hooks/NativeSettingsUi.inl"

static void F7_BuildRows(int kind) {
    g_f7MenuKind = kind;
    g_f7RowCount = 0;
    const FfxHooks::F7Config cfg = FfxHooks::F7_GetConfigSnapshot().config;
    if (kind == F7_MENU_MUSIC) {
        g_f7Vals[0] = cfg.music.lockTrack;  g_f7Vals[1] = cfg.music.battleTrack;
        g_f7Vals[2] = cfg.music.randomizer ? 1 : 0; g_f7Vals[3] = cfg.music.fadeFrames;
        g_f7Rows[g_f7RowCount++] = { "Music Lock",   F7RT_STEPPER, -1, 181, 1 };
        g_f7Rows[g_f7RowCount++] = { "Battle Entry",F7RT_STEPPER, -1, 181, 1 };
        g_f7Rows[g_f7RowCount++] = { "Randomizer",   F7RT_TOGGLE,   0,   1, 1 };
        g_f7Rows[g_f7RowCount++] = { "Fade",         F7RT_STEPPER,  0, 600, 5 };
        g_f7Rows[g_f7RowCount++] = { "Save + Open Custom Mix", F7RT_ACTION, 0, 0, 0 };
        g_f7Rows[g_f7RowCount++] = { "Save",         F7RT_ACTION,   0,   0, 0 };
        g_f7Rows[g_f7RowCount++] = { "Reset",        F7RT_ACTION,   0,   0, 0 };
        g_f7Rows[g_f7RowCount++] = { "Back",         F7RT_BACK,     0,   0, 0 };
    } else if (kind == F7_MENU_FORCE) {
        // Row 1 owns Repeat. Keeping the draft at index 0 made the visible
        // stepper edit one value while Save read a different, unchanged slot.
        g_f7Vals[1] = cfg.force.repeatCount;
        g_f7Rows[g_f7RowCount++] = { "Force Last Battle", F7RT_ACTION, 0, 0, 0 };
        g_f7Rows[g_f7RowCount++] = { "Repeat",            F7RT_STEPPER, 1, 9, 1 };
        g_f7Rows[g_f7RowCount++] = { "Last Encounter",    F7RT_INFO, 0, 0, 0 };
        g_f7Rows[g_f7RowCount++] = { "Back",              F7RT_BACK, 0, 0, 0 };
    } else if (kind == F7_MENU_AI) {
        // The old editor exposed status writers as an AI swap. This page is deliberately named
        // as an observer until a compatible, independently validated script pair exists.
        g_f7Rows[g_f7RowCount++] = { "Monster AI Observer - Read-only", F7RT_INFO, 0, 0, 0 };
        g_f7Rows[g_f7RowCount++] = { FfxHooks::F7AiSwap_StatusName(), F7RT_INFO, 0, 0, 0 };
        g_f7Rows[g_f7RowCount++] = { FfxHooks::F7AiSwap_DetailText(), F7RT_INFO, 0, 0, 0 };
        g_f7Rows[g_f7RowCount++] = { "Back", F7RT_BACK, 0, 0, 0 };
    } else if (kind == F7_MENU_FLAGS) {
        const size_t tabCount = FfxHooks::F8TabCount();
        if (g_f7Tab < 0 || static_cast<size_t>(g_f7Tab) >= tabCount) g_f7Tab = 0;
        const char* tabName = tabCount > 0 ? FfxHooks::F8TabName(static_cast<size_t>(g_f7Tab)) : "";
        g_f7RowCount = 0;
        g_f7FlagCount = 0;
        memset(g_f7FlagSpecs, 0, sizeof(g_f7FlagSpecs));
        memset(g_f8ScalarLabels, 0, sizeof(g_f8ScalarLabels));
        memset(g_f8BulkStatus, 0, sizeof(g_f8BulkStatus));
        memset(g_f8RowVerdicts, 0, sizeof(g_f8RowVerdicts));
        int editableCount = 0;
        for (size_t i = 0; i < FfxHooks::F8FlagCount(); ++i) {
            const FfxHooks::F8FlagSpec& flag = FfxHooks::F8FlagAt(i);
            if (strcmp(flag.tab, tabName) == 0 &&
                flag.activation != FfxHooks::F8Activation::NotWired && flag.activation != FfxHooks::F8Activation::ReadOnly) {
                ++editableCount;
            }
        }
        // WHY: only multi-flag editable tabs need bulk rows; Plugins and Input
        // stay uncluttered because they contain zero or one supported setting.
        if (editableCount > 1) {
            g_f7Rows[g_f7RowCount++] = {
                "Enable Supported", F7RT_BULK, 1, 1, 1,
                "Enable all available options in this tab"};
            g_f7Rows[g_f7RowCount++] = {
                "Disable Supported", F7RT_BULK, 0, 0, 1,
                "Disable all available options in this tab"};
            g_f7FlagCount = g_f7RowCount;
        }
        for (size_t i = 0; i < FfxHooks::F8FlagCount() && g_f7RowCount < 31; ++i) {
            const FfxHooks::F8FlagSpec* flag = &FfxHooks::F8FlagAt(i);
            if (strcmp(flag->tab, tabName) != 0) continue;
            const int idx = g_f7RowCount;
            g_f7FlagSpecs[idx] = flag;
            g_f7Vals[idx] = FfxHooks::ResolveF8Flag(*flag).value ? 1 : 0;
            g_f7Rows[g_f7RowCount++] = { flag->label,
                flag->activation == FfxHooks::F8Activation::ReadOnly ? F7RT_INFO : F7RT_TOGGLE, 0, 1, 1, flag->help };
            g_f7FlagCount = g_f7RowCount;
            if (flag->scalar) {
                const int scalarRow = g_f7RowCount;
                const FfxHooks::F8ScalarResult configured = FfxHooks::ResolveF8Scalar(*flag);
                g_f7FlagSpecs[scalarRow] = flag;
                g_f7Vals[scalarRow] = configured.state == FfxHooks::F8ScalarState::Invalid
                    ? flag->scalar->defaultValue : configured.value;
                g_f7Rows[g_f7RowCount++] = {
                    g_f8ScalarLabels[scalarRow], F7RT_SCALAR,
                    flag->scalar->minimum, flag->scalar->maximum, 1,
                    flag->activation == FfxHooks::F8Activation::RestartRequired
                        ? "RESTART REQUIRED - Set item limit (1-255)."
                        : "Configure this reward multiplier; Confirm saves, Back cancels."};
                g_f7FlagCount = g_f7RowCount;
                F8RefreshScalarLabel(scalarRow);
            }
        }
        if(strcmp(tabName,"System")==0)
            g_f7Rows[g_f7RowCount++]={"Audio languages",F7RT_OPTIONS,static_cast<int>(NativeSettingsPage::Languages),0,0,"Choose voice, battle sound and movie audio languages."};
        if (strcmp(tabName, "Input") == 0) {
            g_f7Rows[g_f7RowCount++]={"Keyboard shortcuts",F7RT_OPTIONS,static_cast<int>(NativeSettingsPage::Keyboard),0,0,"Choose shortcuts for menus and existing native actions."};
            g_f7Rows[g_f7RowCount++]={"Gamepad shortcuts",F7RT_OPTIONS,static_cast<int>(NativeSettingsPage::Gamepad),0,0,"Assign your own physical button combinations."};
            g_f7Rows[g_f7RowCount++]={"Gamepad settings",F7RT_OPTIONS,static_cast<int>(NativeSettingsPage::Controller),0,0,"Choose a controller or remap its buttons."};
            g_f7FlagCount = g_f7RowCount;
        }
        g_f7Rows[g_f7RowCount++] = { "Back", F7RT_BACK, 0, 0, 0, "Close the flags menu and return to the game" };
    } else {  // Difficulty columns: presets + BASE + AUTO + WEAK + RESIST + ABSORB + ACTIONS.
        const FfxHooks::F7DifficultyPreset p = cfg.diffGlobal;
        g_f7DiffPresetIdx = -1;   // Custom preset derived from the current values.
        g_f7DifficultyEnabled = p.enabled;
        g_f7Vals[1] = p.hpMul;  g_f7Vals[2] = p.strMul;  g_f7Vals[3] = p.defMul;
        g_f7Vals[4] = p.magMul; g_f7Vals[5] = p.mdfMul;  g_f7Vals[6] = p.agiMul;
        g_f7Vals[7] = p.accMul; g_f7Vals[8] = p.evaMul;  g_f7Vals[9] = p.lckMul;
        g_f7Vals[10] = (int)(p.autoStatusMask & 0x01FFFFFFu);
        g_f7Vals[11] = p.elemWeak;
        g_f7Vals[12] = p.elemResist;
        g_f7Vals[13] = p.elemAbsorb;
        g_f7RowCount = 0;   // Difficulty owns its multi-column input/draw path instead of 1D rows.
        g_f7Col = F7DC_PRESETS; g_f7ColRow = 0; g_f7EditActive = 0;
    }
    for (int i = 0; i < g_f7RowCount; ++i)
        NativeMenu::EncodeLabel(g_f7Rows[i].label, g_f7Labels[i], (int)sizeof(g_f7Labels[i]));
}

// â”€â”€ F7 submenu: formatted row value â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static const char* F7RowValueText(int row) {
    const F7SubRow& R = g_f7Rows[row];
    switch (R.type) {
        case F7RT_TOGGLE:  return F7BoolName(g_f7Vals[row]);
        case F7RT_STEPPER:
            if (g_f7MenuKind == F7_MENU_MUSIC)      return F7TrackName(g_f7Vals[row]);
            if (g_f7MenuKind == F7_MENU_FORCE)      { _snprintf_s(g_f7FmtBuf, sizeof(g_f7FmtBuf), _TRUNCATE, "%d", g_f7Vals[row]); return g_f7FmtBuf; }
            if (g_f7MenuKind == F7_MENU_DIFF) {
                switch (row) {
                    case 0:  return F7PresetName(g_f7Vals[0]);
                    case 8:  return F7AutoName(g_f7Vals[8]);
                    case 9:  case 10: case 11: return F7ElemName(g_f7Vals[row]);
                    default: return F7MulName(g_f7Vals[row]);
                }
            }
            break;
        case F7RT_INFO:
            if (g_f7MenuKind == F7_MENU_FLAGS && g_f7FlagSpecs[row]) {
                const char* key = g_f7FlagSpecs[row]->gate.canonicalKey;
                if (strcmp(key,"plugins.dinput8")==0 || strcmp(key,"plugins.ffx_probe")==0) return "Built in";
                if (strcmp(key,"plugins.unx")==0) return FpsScoutUnxDetected()?"Loaded externally":"Not loaded";
                if (strcmp(key,"plugins.dxgi")==0) return FpsScoutSpecialKDetected()?"Special K loaded":"Native renderer";
                return "Read only";
            }
            if (g_f7MenuKind == F7_MENU_AI) return "";
            if (g_f7MenuKind == F7_MENU_FORCE && FfxHooks::F7_HasLastEncounter()) {
                _snprintf_s(g_f7FmtBuf, sizeof(g_f7FmtBuf), _TRUNCATE, "field %d / group %d",
                    FfxHooks::F7_LastEncounterField(), FfxHooks::F7_LastEncounterGroup());
                return g_f7FmtBuf;
            }
            return "-";
        case F7RT_BINDING:
            return FfxHooks::NativePorts::BindingText(static_cast<FfxHooks::NativeBindings::Action>(g_f7Rows[row].min));
        default: break;
    }
    return "";
}

// â”€â”€ F7 submenu: input (up/down navigate Â· left/right adjust Â· confirm/cancel) â”€
static const char* F8EditCodeName(FfxHooks::F8EditCode code) {
    switch (code) {
        case FfxHooks::F8EditCode::Saved: return "SAVED";
        case FfxHooks::F8EditCode::RejectedNotWired: return "REJECTED NOT WIRED";
        case FfxHooks::F8EditCode::RejectedUnavailable: return "REJECTED UNAVAILABLE";
        case FfxHooks::F8EditCode::RejectedInvalidParameter: return "REJECTED INVALID RATE";
        case FfxHooks::F8EditCode::PersistFailed: return "PERSIST FAILED";
        default: return "UNKNOWN";
    }
}

static const char* F8ScalarEditCodeName(FfxHooks::F8ScalarEditCode code) {
    switch (code) {
        case FfxHooks::F8ScalarEditCode::Saved: return "SAVED";
        case FfxHooks::F8ScalarEditCode::RejectedNotApplicable: return "REJECTED NOT APPLICABLE";
        case FfxHooks::F8ScalarEditCode::RejectedInvalid: return "REJECTED INVALID";
        case FfxHooks::F8ScalarEditCode::RejectedUnavailable: return "REJECTED UNAVAILABLE";
        case FfxHooks::F8ScalarEditCode::PersistFailed: return "PERSIST FAILED";
        default: return "UNKNOWN";
    }
}

static void F7_AdjustValue(int delta, int sel) {
    const F7SubRow& R = g_f7Rows[sel];
    if (R.type != F7RT_STEPPER && R.type != F7RT_TOGGLE) return;
    int nv;
    if (R.type == F7RT_TOGGLE) {
        /* Toggle FLIPS (0<->1). FIX 2026-08-16: the old `nv = val + delta*step` with delta=1
         * on an ON toggle computed 1+1=2 -> clamped to max(1) -> could NEVER turn OFF. */
        nv = (g_f7Vals[sel] == 0) ? 1 : 0;
    } else {
        const int step = R.step;
        nv = g_f7Vals[sel] + delta * step;
        if (nv < R.min) nv = R.min;
        if (nv > R.max) nv = R.max;
    }
    if (g_f7MenuKind == F7_MENU_FLAGS) {
        if (sel < 0 || sel >= g_f7FlagCount || !g_f7FlagSpecs[sel]) return;
        const FfxHooks::F8FlagSpec* flag = g_f7FlagSpecs[sel];
        const FfxHooks::F8EditResult result = FfxHooks::SetF8FlagValue(*flag, nv != 0);
        /* Effective authority always owns the row after save, override, rejection, or failure. */
        g_f7Vals[sel] = result.effective.value ? 1 : 0;
        g_f7LastEditSpec = flag;
        g_f7LastEdit = result;
        g_f7HasLastEdit = true;
        Log("[ffx-hooks] F8 edit key=%s edit=%s requested=%d effective=%d source=%s\n",
            flag->gate.canonicalKey,
            F8EditCodeName(result.code),
            result.requestedValue ? 1 : 0,
            result.effective.value ? 1 : 0,
            FfxHooks::Config::BoolSourceName(result.effective.source));
        return;
    }
    g_f7Vals[sel] = nv;
    if (g_f7MenuKind == F7_MENU_DIFF) {
        if (sel == 0) { F7_DiffPresetFill(g_f7Vals[0]); return; }   // A preset fills every value.
        g_f7DiffPresetIdx = -1;                                     // Manual edit selects Custom.
    }
}

static void F8ApplyTabBulk(bool requestedValue, int selectedRow) {
    const char* tab = FfxHooks::F8TabName(static_cast<size_t>(g_f7Tab));
    const FfxHooks::F8BulkEditResult result =
        FfxHooks::SetF8TabValues(tab, requestedValue);
    for (int row = 0; row < g_f7FlagCount; ++row) {
        if (g_f7Rows[row].type == F7RT_TOGGLE && g_f7FlagSpecs[row]) {
            g_f7Vals[row] = FfxHooks::ResolveF8Flag(*g_f7FlagSpecs[row]).value ? 1 : 0;
        }
    }
    if (result.persistFailed) {
        strcpy_s(g_f8BulkStatus, "Atomic tab save failed; no settings changed");
    } else {
        _snprintf_s(g_f8BulkStatus, sizeof(g_f8BulkStatus), _TRUNCATE,
                    "%s: %zu changed, %zu already",
                    requestedValue ? "Enabled" : "Disabled",
                    result.changed, result.already);
        size_t used = strlen(g_f8BulkStatus);
        if (result.unavailable) {
            _snprintf_s(g_f8BulkStatus + used, sizeof(g_f8BulkStatus) - used,
                        _TRUNCATE, ", %zu unavailable", result.unavailable);
            used = strlen(g_f8BulkStatus);
        }
        if (result.externalOverride) {
            _snprintf_s(g_f8BulkStatus + used, sizeof(g_f8BulkStatus) - used,
                        _TRUNCATE, ", %zu external", result.externalOverride);
            used = strlen(g_f8BulkStatus);
        }
        if (result.invalidParameter) {
            _snprintf_s(g_f8BulkStatus + used, sizeof(g_f8BulkStatus) - used,
                        _TRUNCATE, ", %zu invalid", result.invalidParameter);
            used = strlen(g_f8BulkStatus);
        }
        if (result.effectiveMismatch) {
            _snprintf_s(g_f8BulkStatus + used, sizeof(g_f8BulkStatus) - used,
                        _TRUNCATE, ", %zu mismatch", result.effectiveMismatch);
        }
    }
    if (selectedRow >= 0 && selectedRow < g_f7FlagCount) {
        g_f7Rows[selectedRow].desc = g_f8BulkStatus;
    }
    Log("[ffx-hooks] F8 bulk tab=%s requested=%d eligible=%zu changed=%zu already=%zu "
        "unavailable=%zu external=%zu invalid=%zu mismatch=%zu persist_failed=%d\n",
        tab, requestedValue ? 1 : 0, result.eligible, result.changed,
        result.already, result.unavailable, result.externalOverride,
        result.invalidParameter, result.effectiveMismatch,
        result.persistFailed ? 1 : 0);
    for (size_t row = 0; row < result.rowCount; ++row) {
        const FfxHooks::F8BulkRowResult& entry = result.rows[row];
        if (entry.code == FfxHooks::F8BulkRowCode::Changed ||
            entry.code == FfxHooks::F8BulkRowCode::Already) {
            continue;
        }
        const char* detail = entry.flag
            ? FfxHooks::F8GateSourceDetail(*entry.flag, entry.source) : nullptr;
        Log("[ffx-hooks] F8 bulk row %s: %s source=%s%s%s\n",
            entry.flag ? entry.flag->gate.canonicalKey : "?",
            FfxHooks::F8BulkRowCodeName(entry.code),
            FfxHooks::Config::BoolSourceName(entry.source),
            detail ? " artifact=" : "", detail ? detail : "");
        // WHY (R8-U1): the per-row verdict previously lived only in this log line —
        // the user pressed Enable Supported, saw "0 changed", and could not tell the
        // run was truthful. Writing the verdict onto the row's own desc makes each
        // blocked row explain itself in the header help when selected, without new UI.
        if (!entry.flag) continue;
        for (int r = 0; r < g_f7FlagCount && r < 32; ++r) {
            if (g_f7FlagSpecs[r] != entry.flag) continue;
            _snprintf_s(g_f8RowVerdicts[r], sizeof(g_f8RowVerdicts[r]), _TRUNCATE,
                        "bulk: %s%s%s", FfxHooks::F8BulkRowCodeName(entry.code),
                        detail ? " — " : "", detail ? detail : "");
            g_f7Rows[r].desc = g_f8RowVerdicts[r];
            break;
        }
    }
    // Expected skips (quarantined rows) keep the success tone; external overrides and
    // invalid scalars warn, and persistence failures or true mismatches error — the
    // status text above carries which of the two the shared warning tone meant.
    const bool clean = !result.persistFailed && result.externalOverride == 0 &&
        result.invalidParameter == 0 && result.effectiveMismatch == 0;
    NativeMenu::PlaySfx(clean ? 4 : 3);
}

// REQ is requested/configured state, EFF is source-resolved state, and APPLIED is readback.
static void F8BuildSelectedStatus(int sel, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (sel < 0 || sel >= g_f7RowCount) return;
    if (g_f7Rows[sel].type == F7RT_BINDING) {
        _snprintf_s(out,outSize,_TRUNCATE,"%s",FfxHooks::NativePorts::BindingCaptureActive()
            ? "Press a shortcut | Esc: cancel | Backspace/Delete: unassign"
            : (g_f8BindingFeedback[0]?g_f8BindingFeedback:"Enter: choose a shortcut | Existing shortcuts are protected"));
        return;
    }
    // WHY (R7-UX-B2): bulk rows carry no flag spec, but the last bulk result is exactly
    // the per-row truth the user is looking for right after pressing Enable/Disable
    // Supported — keep it visible on the technical line instead of showing nothing.
    if (!g_f7FlagSpecs[sel]) {
        if (sel < g_f7FlagCount && g_f7Rows[sel].type == F7RT_BULK &&
            g_f8BulkStatus[0]) {
            _snprintf_s(out, outSize, _TRUNCATE, "%s", g_f8BulkStatus);
        }
        return;
    }
    if (sel >= g_f7FlagCount) return;

    const FfxHooks::F8FlagSpec& flag = *g_f7FlagSpecs[sel];
    if(strcmp(flag.gate.canonicalKey,"boosters.speed_hack_fmv")==0){_snprintf_s(out,outSize,_TRUNCATE,"%s",FfxHooks::FmvSpeed::Detail());return;}
    if (flag.activation == FfxHooks::F8Activation::ReadOnly) {
        _snprintf_s(out,outSize,_TRUNCATE,"Runtime information - read only");
        return;
    }
    const FfxHooks::Config::BoolGateResult effective = FfxHooks::ResolveF8Flag(flag);
    const FfxHooks::F8RuntimeStatus runtime = FfxHooks::GetF8RuntimeStatus(flag);
    if (g_f7Rows[sel].type == F7RT_SCALAR && flag.scalar) {
        const bool nextBoot = flag.activation == FfxHooks::F8Activation::RestartRequired;
        const FfxHooks::F8ScalarResult configured = FfxHooks::ResolveF8Scalar(flag);
        const bool hasLastScalarEdit = g_f7HasLastScalarEdit &&
            g_f7LastScalarEditSpec == &flag;
        const char* edit = hasLastScalarEdit
            ? F8ScalarEditCodeName(g_f7LastScalarEdit.code) : nullptr;
        char config[24] = {};
        char live[40] = {};
        char applied[24] = {};
        char editState[40] = {};
        if (configured.state == FfxHooks::F8ScalarState::Invalid) {
            strcpy_s(config, "CFG INVALID");
        } else {
            _snprintf_s(config, sizeof(config), _TRUNCATE, "CFG %d%s", configured.value, nextBoot ? "" : "x");
        }
        if (nextBoot) {
            strcpy_s(live, "RESTART REQUIRED");
            if (runtime.hasAppliedScalar) {
                _snprintf_s(applied, sizeof(applied), _TRUNCATE, "STARTUP %d", runtime.appliedScalar);
            } else if (runtime.availability == FfxHooks::F8RuntimeAvailability::ProducerUnavailable) {
                strcpy_s(applied, "STARTUP FAILED");
            }
        } else {
            _snprintf_s(live, sizeof(live), _TRUNCATE, "LIVE %s",
                FfxHooks::F8AvailabilityName(runtime.availability));
            if (runtime.hasAppliedScalar) {
                _snprintf_s(applied, sizeof(applied), _TRUNCATE, "APPLIED %dx", runtime.appliedScalar);
            } else {
                strcpy_s(applied, "APPLIED UNKNOWN");
            }
        }
        if (edit) {
            _snprintf_s(editState, sizeof(editState), _TRUNCATE, "EDIT %s", edit);
        }
        const FfxHooks::F8Ui::TechnicalStatusParts parts = {
            config, live, applied[0] ? applied : nullptr, edit ? editState : nullptr,
        };
        if (!FfxHooks::F8Ui::BuildTechnicalStatus(parts, out, outSize)) {
            _snprintf_s(out, outSize, _TRUNCATE, "STATUS UNAVAILABLE");
        }
        return;
    }
    const bool hasLastEdit = g_f7HasLastEdit && g_f7LastEditSpec == &flag;
    bool configuredValue = effective.value;
    FfxHooks::Config::TryGetBoolExact(flag.gate.canonicalKey, &configuredValue);
    const bool requested = hasLastEdit ? g_f7LastEdit.requestedValue : configuredValue;
    const char* edit = hasLastEdit ? F8EditCodeName(g_f7LastEdit.code) : nullptr;

    char requestEffective[64] = {};
    char live[40] = {};
    char applied[24] = {};
    char editState[40] = {};
    // A gate held by an env var or .off marker names that artifact instead of a bare
    // EFF, so "External OFF: arena_plus_music.flag.off" replaces an unexplained state.
    const char* sourceDetail =
        FfxHooks::F8GateSourceDetail(flag, effective.source);
    if (sourceDetail) {
        // WHY: the artifact name must fit inside TechnicalStatusCharacterBudget
        // alongside LIVE/ARMED/EDIT tokens, so the EXT token stays compact —
        // a blown budget would collapse the whole line to STATUS UNAVAILABLE.
        _snprintf_s(requestEffective, sizeof(requestEffective), _TRUNCATE,
            "EXT %s: %s", effective.value ? "ON" : "OFF", sourceDetail);
    } else {
        _snprintf_s(requestEffective, sizeof(requestEffective), _TRUNCATE,
            "REQ %s / EFF %s", requested ? "ON" : "OFF",
            effective.value ? "ON" : "OFF");
    }
    if (strcmp(flag.gate.canonicalKey, "development.fastload_autosave") == 0) {
        const auto fastload = FfxHooks::Fastload::GetRuntimeSnapshot();
        _snprintf_s(live, sizeof(live), _TRUNCATE, "RUN %s",
            FfxHooks::Fastload::RuntimeDetail(fastload, effective.value));
        const FfxHooks::F8Ui::TechnicalStatusParts parts = {requestEffective, live, nullptr, nullptr};
        if (!FfxHooks::F8Ui::BuildTechnicalStatus(parts, out, outSize)) {
            _snprintf_s(out, outSize, _TRUNCATE, "%s", live);
        }
        return;
    }
    if (flag.activation == FfxHooks::F8Activation::Live) {
        _snprintf_s(live, sizeof(live), _TRUNCATE, "LIVE %s",
            FfxHooks::F8AvailabilityName(runtime.availability));
        if (flag.applyMode == FfxHooks::F8ApplyMode::ConfigPolled) {
            _snprintf_s(applied, sizeof(applied), _TRUNCATE, "ARMED %s",
                effective.value ? "ON" : "OFF");
        } else if (flag.applyMode == FfxHooks::F8ApplyMode::RuntimeAcknowledged) {
            if (strcmp(flag.gate.canonicalKey, "boosters.playable_seymour") == 0 &&
                effective.value &&
                runtime.availability == FfxHooks::F8RuntimeAvailability::Pending &&
                !runtime.hasAppliedValue) {
                // Playable Seymour is an entry-triggered battle roster, so Pending while ON is
                // a truthful armed state rather than an unknown RAM readback.
                strcpy_s(applied, "ARMED BATTLE");
            } else if (runtime.hasAppliedValue) {
                _snprintf_s(applied, sizeof(applied), _TRUNCATE, "APPLIED %s",
                    runtime.appliedValue ? "ON" : "OFF");
            } else {
                strcpy_s(applied, "APPLIED UNKNOWN");
            }
        }
    } else if (flag.activation == FfxHooks::F8Activation::RestartRequired) {
        if (runtime.availability == FfxHooks::F8RuntimeAvailability::NotApplicable) {
            strcpy_s(live, "RESTART REQUIRED");
        } else {
            // A restart-required row can publish the result of this process's startup attempt.
            // Surface that evidence so a skipped/conflicted hook is never implied to be active.
            _snprintf_s(live, sizeof(live), _TRUNCATE, "STARTUP %s",
                FfxHooks::F8AvailabilityName(runtime.availability));
        }
    } else {
        strcpy_s(live, "NOT WIRED");
    }
    if (edit) {
        _snprintf_s(editState, sizeof(editState), _TRUNCATE, "EDIT %s", edit);
    }
    const FfxHooks::F8Ui::TechnicalStatusParts parts = {
        requestEffective, live, applied[0] ? applied : nullptr, edit ? editState : nullptr,
    };
    if (!FfxHooks::F8Ui::BuildTechnicalStatus(parts, out, outSize)) {
        _snprintf_s(out, outSize, _TRUNCATE, "STATUS UNAVAILABLE");
    }
}

static bool g_f8MouseWasDown = false;
// ShowCursor is counter-based, so teardown must reverse every increment made while acquiring
// visibility without disturbing a balance owned by the game or another overlay.
static FfxHooks::F8Ui::CursorVisibilityBalance g_f8CursorBalance;

static int F8ShowCursorAdapter(bool show, void*) noexcept {
    return ShowCursor(show ? TRUE : FALSE);
}

static void F8AcquireCursorVisibility() {
    (void)g_f8CursorBalance.Acquire(F8ShowCursorAdapter, nullptr);
}

static void F8ReleaseCursorOwnership() {
    F8NativeSettingsReset();
    (void)g_f8CursorBalance.Release(F8ShowCursorAdapter, nullptr);
}

static void F8RollbackRejectedDirectOpen() {
    FfxHooks::F8Ui::RollbackRejectedDirectOpen(
        &g_f8ScalarEditor, &g_f8MouseWasDown, &g_f7CloseLatch);
    F8ReleaseCursorOwnership();
    // Release-publish after local cleanup so the next Present acquire cannot
    // observe a phantom close edge or stale Pump-local pre-open state.
    g_f8MenuOpen.Store(false);
}

static void F8ReturnFlagsToGame() {
    g_f8ScalarEditor.Cancel();
    F8ReleaseCursorOwnership();
    g_f8MenuOpen.Store(false);
    g_f8MouseWasDown = false;
    InterlockedExchange(&g_nativeWantSpawn, 0);
    InterlockedExchange(&g_nativeWantClose, 0);
    InterlockedExchange(&g_f7WantOpenKind, -1);
    InterlockedExchange(&g_forceSubsystem, 0);
    NativeMenuForceGateClear();
}

static void F8MouseTabHitTest(int obj) {
    using namespace NativeMenu;
    HWND hwnd = g_ingameMenuInputHwnd;
    if (!hwnd || !IsWindow(hwnd)) hwnd = GetForegroundWindow();
    if (!hwnd) return;

    POINT pt = {};
    RECT client = {};
    if (!GetCursorPos(&pt) || !GetClientRect(hwnd, &client)) return;
    POINT clientOrigin = {client.left, client.top};
    if (!ClientToScreen(hwnd, &clientOrigin)) return;
    float px = 0.0f;
    float py = 0.0f;
    if (!FfxHooks::F8Ui::ConvertClientScreenPointToMenu(
            pt.x, pt.y, clientOrigin.x, clientOrigin.y,
            client.right - client.left, client.bottom - client.top,
            MenuPhysW(), MenuPhysH(), &px, &py)) {
        return;
    }
    const bool mouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool mouseEdge = mouseDown && !g_f8MouseWasDown;
    g_f8MouseWasDown = mouseDown;

    const float vLeft = NX(FfxHooks::F8Ui::Layout::TabLeft);
    const float vWidth = NW(FfxHooks::F8Ui::Layout::TabWidth);
    const float tabY = NY(FfxHooks::F8Ui::Layout::TabTop);
    const float tabH = NH(FfxHooks::F8Ui::Layout::TabHeight);
    const float tabGap = NW(FfxHooks::F8Ui::Layout::TabGap);
    const int tabCount = static_cast<int>(FfxHooks::F8TabCount());
    if (!mouseEdge || tabCount <= 0 || px < vLeft || px > vLeft + vWidth ||
        py < tabY || py >= tabY + tabH) return;

    const float tabW = (vWidth - tabGap * (tabCount - 1)) / static_cast<float>(tabCount);
    const float cellWidth = tabW + tabGap;
    const float relativeX = px - vLeft;
    const int tab = static_cast<int>(relativeX / cellWidth);
    const float offsetInCell = relativeX - static_cast<float>(tab) * cellWidth;
    if (offsetInCell >= tabW) return;
    if (tab < 0 || tab >= tabCount || tab == g_f7Tab) return;
    const char* tabName = FfxHooks::F8TabName(static_cast<size_t>(tab));
    if (!tabName || !tabName[0]) return;

    g_f7Tab = tab;
    F7_BuildRows(F7_MENU_FLAGS);
    WrW(obj, O_COUNT, static_cast<int16_t>(g_f7RowCount));
    WrW(obj, O_SELECTED, 0);
    WrW(obj, O_TOP, 0);
    g_f7EasedRowY = -1.0f;
    PlaySfx(1);
}

static int __cdecl F7Sub_InputCb(int obj) {
    using namespace NativeMenu;
    const bool directF8Flags =
        g_f7MenuKind == F7_MENU_FLAGS && g_f8MenuOpen.Load();
    // A direct-F8 FLAGS menu owns no modal input block, so AdmitInput cannot judge
    // it — but focus loss must still close it exactly like every other F-key menu.
    const bool inputAdmitted = directF8Flags
        ? F7IsForegroundWindow()
        : FfxHooks::F7Ui::AdmitInput(
            true, F7IsForegroundWindow(), g_f7Menu.obj == obj);
    if (!inputAdmitted) {
        g_f7LastEdge = 0;
        F7RequestClose(FfxHooks::F7Ui::CloseSource::FocusLost);
        return obj;
    }
    if(FfxHooks::NativePorts::MenuOpeningPadHeld())return obj;
    if(g_f7MenuKind==F7_MENU_FLAGS && F8NativeSettingsActive()){F8NativeSettingsInput(obj);return obj;}
    if (g_f7MenuKind == F7_MENU_FLAGS) {
        if (!g_f8ScalarEditor.Active()) F8MouseTabHitTest(obj);
    }
    if (g_f7ConfirmTimer > 0) --g_f7ConfirmTimer;
    int dir = PadDir();
    const int edge = PadEdge();
    bool confirmPressed = (edge & 0x20) && !(g_f7LastEdge & 0x20) && (g_f7ConfirmTimer == 0);
    const bool cancelPressed  = (edge & 0x40) && !(g_f7LastEdge & 0x40) && (g_f7ConfirmTimer == 0);   // cooldown no cancel tb
    g_f7LastEdge = edge;

    // â”€â”€ DIFF: multi-column (presets + BASE + AUTO + WEAK + RESIST + ABSORB + ACTIONS) â”€â”€
    if (g_f7MenuKind == F7_MENU_DIFF) {
        if (g_f7EditActive) {
            // The game's PadDir stream already merges keyboard/controller repeat.
            // Fine and coarse adjustments therefore share one clamped policy.
            int numericDirection = 0;
            if (dir & 0x8000) numericDirection = -1;
            else if (dir & 0x2000) numericDirection = 1;
            else if (dir & 0x1000) numericDirection = -2;
            else if (dir & 0x4000) numericDirection = 2;
            if (numericDirection != 0) {
                const int row = g_f7ColRow;
                g_f7EditValue = FfxHooks::F7Ui::AdjustNumeric(
                    g_f7EditValue, numericDirection, 25, 100,
                    F7_BASE_MIN[row], F7_BASE_MAX[row]);
                PlaySfx(1);
            }
            // Direct digits are foreground-gated above and replace, rather than
            // append to, the value copied into the draft when editing began.
            for (int k = '0'; k <= '9'; ++k) {
                if (GetAsyncKeyState(k) & 1) {
                    if (g_f7EditDigits == 0) g_f7EditValue = 0;
                    if (g_f7EditDigits < 6) {
                        ++g_f7EditDigits;
                        g_f7EditValue = g_f7EditValue * 10 + (k - '0');
                        if (g_f7EditValue > 99999) g_f7EditValue = 99999;
                    }
                    PlaySfx(1);
                }
            }
            if (GetAsyncKeyState(VK_BACK) & 1) {
                if (g_f7EditDigits > 0) { --g_f7EditDigits; g_f7EditValue /= 10; }
                PlaySfx(1);
            }
            if (confirmPressed) {
                const int row = g_f7ColRow;
                int v = g_f7EditValue;
                if (v < F7_BASE_MIN[row]) v = F7_BASE_MIN[row];
                if (v > F7_BASE_MAX[row]) v = F7_BASE_MAX[row];
                g_f7Vals[1 + row] = v;
                g_f7DiffPresetIdx = -1;
                g_f7EditActive = 0;
                F7DiffSetStatus("Custom value staged; use Apply Now or Save");
                PlaySfx(1);
            } else if (cancelPressed) {
                g_f7EditActive = 0;
                PlaySfx(1);
            }
            return obj;
        }
        const F7MouseInputResult mouse = F7DifficultyMouseTick();
        dir = FfxHooks::F7Ui::ResolveDirectionalInput(dir, mouse.ownsDirectionalFrame);
        if (mouse.confirm) confirmPressed = true;
        if (dir & 0x8000) { if (g_f7Col > 0) { --g_f7Col; if (g_f7ColRow >= F7DiffColRows(g_f7Col)) g_f7ColRow = F7DiffColRows(g_f7Col) - 1; PlaySfx(1); } }
        else if (dir & 0x2000) { if (g_f7Col + 1 < F7DC_COUNT) { ++g_f7Col; if (g_f7ColRow >= F7DiffColRows(g_f7Col)) g_f7ColRow = F7DiffColRows(g_f7Col) - 1; PlaySfx(1); } }
        if (dir & 0x1000) { if (g_f7ColRow > 0) { --g_f7ColRow; PlaySfx(1); } }
        else if (dir & 0x4000) { if (g_f7ColRow + 1 < F7DiffColRows(g_f7Col)) { ++g_f7ColRow; PlaySfx(1); } }
        WrW(obj, O_SELECTED, static_cast<int16_t>(g_f7Col * 16 + g_f7ColRow));
        if (confirmPressed) {
            switch (g_f7Col) {
                case F7DC_PRESETS:
                    F7_DiffPresetFill(g_f7ColRow);
                    F7DiffSetStatus(F7PresetName(g_f7ColRow));
                    PlaySfx(1);
                    break;
                case F7DC_BASE:
                    g_f7EditActive = 1;
                    g_f7EditValue = g_f7Vals[1 + g_f7ColRow];
                    g_f7EditDigits = 0;
                    PlaySfx(4);
                    break;
                case F7DC_AUTO:   F7DiffToggleBit(10, g_f7ColRow); break;
                case F7DC_WEAK:   F7DiffToggleBit(11, g_f7ColRow); break;
                case F7DC_RESIST: F7DiffToggleBit(12, g_f7ColRow); break;
                case F7DC_ABSORB: F7DiffToggleBit(13, g_f7ColRow); break;
                case F7DC_ACTIONS:
                    if (g_f7ColRow == 0) {
                        F7_CommitValsToConfig();
                        FfxHooks::F7DifficultyRuntimeStatus status =
                            FfxHooks::F7_DifficultyStatus();
                        if (!status.infrastructureInstalled ||
                            !status.callbackAdmissionOpen) {
                            char message[56] = {};
                            _snprintf_s(message, sizeof(message), _TRUNCATE,
                                "Apply unavailable: %s",
                                !status.infrastructureInstalled
                                    ? FfxHooks::F7_DifficultyGateName(status.infrastructureGate)
                                    : "admission closed");
                            F7DiffSetStatus(message);
                            PlaySfx(3);
                        } else {
                            FfxHooks::F7_DifficultyApplyNow();
                            status = FfxHooks::F7_DifficultyStatus();
                            char message[56] = {};
                            if (status.last.code ==
                                FfxHooks::F7Difficulty::ResultCode::NoActors) {
                                // WHY: applying outside battle is not a failure — the
                                // validated preset composes on the next actor init.
                                _snprintf_s(message, sizeof(message), _TRUNCATE,
                                    "Armed for next battle");
                            } else {
                                _snprintf_s(message, sizeof(message), _TRUNCATE,
                                    "%s: a%zu w%zu r%zu l%zu f%zu j%zu",
                                    FfxHooks::F7_DifficultyResultName(status.last.code),
                                    status.last.actorsSeen, status.last.fieldsWritten,
                                    status.last.fieldsRestored, status.last.ownershipLost,
                                    status.last.faults, status.pointersRejected);
                            }
                            F7DiffSetStatus(message);
                            PlaySfx(status.last.code == FfxHooks::F7Difficulty::ResultCode::Applied ||
                                    status.last.code == FfxHooks::F7Difficulty::ResultCode::Restored
                                ? 4 : 3);
                        }
                    } else if (g_f7ColRow == 1) {
                        F7_CommitValsToConfig();
                        F7_SaveConfigWithFeedback("Config saved to f7_inlive.json");
                    } else {
                        F7_CommitValsToConfig();
                        if (F7_SaveConfigWithFeedback("Config saved to f7_inlive.json")) {
                            g_f7CloseLatch.RequestCancel();
                            WrB(obj, 65, 1);
                        }
                    }
                    break;
            }
        } else if (cancelPressed) {
            g_f7CloseLatch.RequestCancel();
            WrB(obj, 65, 1);
        }
        if (confirmPressed || cancelPressed) g_f7ConfirmTimer = 12;
        return obj;
    }

    // â”€â”€ MUSIC: steppers with preview on confirm; actions execute without closing â”€â”€
    if (g_f7MenuKind == F7_MENU_MUSIC) {
        const F7MouseInputResult mouse = F7ListMouseTick(
                obj, NX(0.271f), NY(0.215f), NW(0.458f), NH(0.063f),
                NH(0.056f), RdW(obj, O_COUNT), RdW(obj, O_PAGE));
        dir = FfxHooks::F7Ui::ResolveDirectionalInput(dir, mouse.ownsDirectionalFrame);
        if (mouse.confirm) confirmPressed = true;
        int selM = RdW(obj, O_SELECTED);
        const int countM = RdW(obj, O_COUNT);
        int topM = RdW(obj, O_TOP);
        const int pageM = RdW(obj, O_PAGE);
        if (dir & 0x1000) { if (selM > 0) { --selM; PlaySfx(1); } }
        else if (dir & 0x4000) { if (selM + 1 < countM) { ++selM; PlaySfx(1); } }
        if (dir & 0x8000) { F7_AdjustValue(-1, selM); PlaySfx(1); }
        else if (dir & 0x2000) { F7_AdjustValue(+1, selM); PlaySfx(1); }
        if (selM < topM) topM = selM;
        else if (selM >= topM + pageM) topM = selM - pageM + 1;
        WrW(obj, O_SELECTED, static_cast<int16_t>(selM));
        WrW(obj, O_TOP, static_cast<int16_t>(topM));
        if (confirmPressed) {
            const F7SubRow& R = g_f7Rows[selM];
            if (R.type == F7RT_BACK) {
                F7_CommitValsToConfig();
                if (F7_SaveConfigWithFeedback("Config saved to f7_inlive.json")) {
                    g_f7CloseLatch.RequestCancel();
                    WrB(obj, 65, 1);
                }
            } else if (R.type == F7RT_ACTION) {
                if (selM == 4) {
                    F7_CommitValsToConfig();
                    if (F7_SaveConfigWithFeedback("Config saved; opening Arena+...")) {
                        g_f7CloseLatch.RequestCancel();
                        WrB(obj, 65, 1);
                        InterlockedExchange(&g_nativeWantSpawn, 1);
                    }
                }
                else if (selM == 5) {
                    F7_CommitValsToConfig();
                    F7_SaveConfigWithFeedback("Config saved to f7_inlive.json");
                }
                else if (selM == 6) {
                    const bool resetSaved = FfxHooks::F7_ResetMusic();
                    F7_BuildRows(F7_MENU_MUSIC);
                    WrW(obj, O_COUNT, static_cast<int16_t>(g_f7RowCount));
                    WrW(obj, O_SELECTED, 0);
                    WrW(obj, O_TOP, 0);
                    F7DiffSetStatus(resetSaved ? "Music reset to defaults" :
                        "Config save failed; changes remain in memory");
                    PlaySfx(resetSaved ? 4 : 3);
                }
            } else if (R.type == F7RT_STEPPER && (selM == 0 || selM == 1)) {
                F7_CommitValsToConfig();
                F7_SaveConfigWithFeedback("Track saved to f7_inlive.json");
            } else {
                PlaySfx(1);
            }
        } else if (cancelPressed) {
            g_f7CloseLatch.RequestCancel();
            WrB(obj, 65, 1);
        }
        if (confirmPressed || cancelPressed) g_f7ConfirmTimer = 12;
        return obj;
    }

    /* ── FLAGS with sub-tabs (2026-08-16, Luna): L/R switches TAB, Up/Down navigates,
     * Confirm toggles the flag, Back/Cancel exits. ── */
    if (g_f7MenuKind == F7_MENU_FLAGS) {
        int sel = RdW(obj, O_SELECTED);
        const int count = RdW(obj, O_COUNT);
        int top = RdW(obj, O_TOP);
        const int page = RdW(obj, O_PAGE);
        const int tabCount = static_cast<int>(FfxHooks::F8TabCount());

        if (FfxHooks::NativePorts::BindingCaptureActive()) {
            FfxHooks::NativeBindings::BindResult result{};bool cancelled=false;
            if (FfxHooks::NativePorts::ConsumeBindingCapture(&result,&cancelled)) {
                using R=FfxHooks::NativeBindings::BindResult;
                const char* text=cancelled?"Shortcut edit cancelled":result==R::Ok?"Shortcut saved":
                    result==R::Protected?"Reserved for the existing SpeedHack shortcut":
                    result==R::Duplicate?"This shortcut already belongs to another action":
                    result==R::Reserved?"This key combination is reserved":"Unable to save this shortcut";
                strncpy_s(g_f8BindingFeedback,text,_TRUNCATE);
                PlaySfx(!cancelled && result==R::Ok?4:3);g_f7ConfirmTimer=12;
            } else if (cancelPressed) {
                FfxHooks::NativePorts::CancelBindingCapture();
                strncpy_s(g_f8BindingFeedback,"Shortcut edit cancelled",_TRUNCATE);
            }
            return obj;
        }

        if (g_f8ScalarEditor.Active()) {
            const int editRow = g_f8ScalarEditor.Row();
            const auto pointer=F7CapturePointer();
            const FfxHooks::F7Ui::ListGeometry geometry{NX(0.271f),NY(FfxHooks::F8Ui::Layout::RowTop),NW(0.458f),
                NH(FfxHooks::F8Ui::Layout::RowStep),NH(FfxHooks::F8Ui::Layout::RowHeight),page};
            const auto hit=FfxHooks::F7Ui::HitTestRows(pointer.x,pointer.y,geometry,top,count);
            bool mouseCancel=false;
            if(pointer.valid){
                if(pointer.wheelSteps){g_f8ScalarEditor.Adjust(-pointer.wheelSteps);F8RefreshScalarLabel(editRow);dir=0;PlaySfx(1);}
                if(pointer.decision.pressAdmitted && g_f7ConfirmTimer==0){
                    if(hit.index==editRow)confirmPressed=true;
                    else if(hit.index<0 || (hit.index<g_f7RowCount && g_f7Rows[hit.index].type==F7RT_BACK))mouseCancel=true;
                }
            }
            if (cancelPressed || mouseCancel) {
                g_f8ScalarEditor.Cancel();
                F8RefreshScalarLabel(editRow);
                PlaySfx(1);
            } else if (confirmPressed) {
                int savedValue = 0;
                const FfxHooks::F8FlagSpec* flag =
                    editRow >= 0 && editRow < g_f7FlagCount ? g_f7FlagSpecs[editRow] : nullptr;
                if (flag && g_f8ScalarEditor.Confirm(&savedValue)) {
                    const FfxHooks::F8ScalarEditResult result =
                        FfxHooks::SetF8ScalarValue(*flag, savedValue);
                    g_f7LastScalarEditSpec = flag;
                    g_f7LastScalarEdit = result;
                    g_f7HasLastScalarEdit = true;
                    Log("[ffx-hooks] F8 scalar edit key=%s edit=%s requested=%d configured=%d\n",
                        flag->scalar->canonicalKey, F8ScalarEditCodeName(result.code),
                        result.requestedValue, result.configured.value);
                    F8RefreshScalarLabel(editRow);
                    PlaySfx(result.code == FfxHooks::F8ScalarEditCode::Saved ? 4 : 1);
                }
            } else if (dir & 0x8000) {
                g_f8ScalarEditor.Adjust(-1);
                F8RefreshScalarLabel(editRow);
                PlaySfx(1);
            } else if (dir & 0x2000) {
                g_f8ScalarEditor.Adjust(1);
                F8RefreshScalarLabel(editRow);
                PlaySfx(1);
            } else if (dir & 0x1000) {
                g_f8ScalarEditor.Adjust(-10);
                F8RefreshScalarLabel(editRow);
                PlaySfx(1);
            } else if (dir & 0x4000) {
                g_f8ScalarEditor.Adjust(10);
                F8RefreshScalarLabel(editRow);
                PlaySfx(1);
            }
            if (confirmPressed || cancelPressed || mouseCancel) g_f7ConfirmTimer = 12;
            return obj;
        }

        const F7MouseInputResult mouse = F7ListMouseTick(
            obj, NX(0.271f), NY(FfxHooks::F8Ui::Layout::RowTop), NW(0.458f),
            NH(FfxHooks::F8Ui::Layout::RowStep), NH(FfxHooks::F8Ui::Layout::RowHeight),count,page);
        dir=FfxHooks::F7Ui::ResolveDirectionalInput(dir,mouse.ownsDirectionalFrame);
        if(mouse.confirm && g_f7ConfirmTimer==0)confirmPressed=true;
        sel=RdW(obj,O_SELECTED);top=RdW(obj,O_TOP);

        if ((dir & 0x8000) && tabCount > 0) {   /* LEFT: previous tab (wrap) */
            g_f7Tab = (g_f7Tab - 1 + tabCount) % tabCount;
            F7_BuildRows(F7_MENU_FLAGS);
            WrW(obj, O_COUNT, static_cast<int16_t>(g_f7RowCount));
            WrW(obj, O_SELECTED, 0); WrW(obj, O_TOP, 0);
            g_f7EasedRowY = -1.0f;
            PlaySfx(1);
            return obj;
        } else if ((dir & 0x2000) && tabCount > 0) {   /* RIGHT: next tab (wrap) */
            g_f7Tab = (g_f7Tab + 1) % tabCount;
            F7_BuildRows(F7_MENU_FLAGS);
            WrW(obj, O_COUNT, static_cast<int16_t>(g_f7RowCount));
            WrW(obj, O_SELECTED, 0); WrW(obj, O_TOP, 0);
            g_f7EasedRowY = -1.0f;
            PlaySfx(1);
            return obj;
        }

        if (dir & 0x1000) { if (sel > 0) { --sel; PlaySfx(1); } }
        else if (dir & 0x4000) { if (sel + 1 < count) { ++sel; PlaySfx(1); } }
        if (sel < top) top = sel;
        else if (sel >= top + page) top = sel - page + 1;
        WrW(obj, O_SELECTED, static_cast<int16_t>(sel));
        WrW(obj, O_TOP, static_cast<int16_t>(top));

        if (confirmPressed) {
            const F7SubRow& R = g_f7Rows[sel];
            if (R.type == F7RT_BACK) {
                g_f7CloseLatch.RequestConfirm(sel);
                WrB(obj, 65, 1);
            } else if (R.type == F7RT_BULK) {
                F8ApplyTabBulk(R.min != 0, sel);
            } else if (R.type == F7RT_BINDING) {
                g_f8BindingFeedback[0]=0;
                FfxHooks::NativePorts::BeginBindingCapture(static_cast<FfxHooks::NativeBindings::Action>(R.min));
                PlaySfx(1);
            } else if (R.type == F7RT_OPTIONS) {
                g_nativeSettingsNotice[0]=0;
                F8NativeSettingsPush(obj,static_cast<NativeSettingsPage>(R.min));
            } else if (R.type == F7RT_TOGGLE) {
                F7_AdjustValue(1, sel);        // toggle immediately through the catalog transaction
                PlaySfx(4);
            } else if (R.type == F7RT_SCALAR && g_f7FlagSpecs[sel] &&
                       g_f7FlagSpecs[sel]->scalar) {
                const FfxHooks::F8FlagSpec& flag = *g_f7FlagSpecs[sel];
                const FfxHooks::F8ScalarResult configured = FfxHooks::ResolveF8Scalar(flag);
                const int initial = configured.state == FfxHooks::F8ScalarState::Invalid
                    ? flag.scalar->defaultValue : configured.value;
                if (g_f8ScalarEditor.Begin(
                        sel, initial, flag.scalar->minimum, flag.scalar->maximum)) {
                    F8RefreshScalarLabel(sel);
                    PlaySfx(4);
                }
            }
        } else if (cancelPressed) {
            g_f7CloseLatch.RequestCancel();
            WrB(obj, 65, 1);
        }
        if (confirmPressed || cancelPressed) g_f7ConfirmTimer = 12;
        return obj;
    }

    const F7MouseInputResult mouse = F7ListMouseTick(
            obj, NX(0.271f), NY(0.215f), NW(0.458f), NH(0.063f),
            NH(0.056f), RdW(obj, O_COUNT), RdW(obj, O_PAGE));
    dir = FfxHooks::F7Ui::ResolveDirectionalInput(dir, mouse.ownsDirectionalFrame);
    if (mouse.confirm) confirmPressed = true;
    int sel = RdW(obj, O_SELECTED);
    const int count = RdW(obj, O_COUNT);
    int top = RdW(obj, O_TOP);
    const int page = RdW(obj, O_PAGE);

    if (dir & 0x1000) { if (sel > 0) { --sel; PlaySfx(1); } }
    else if (dir & 0x4000) { if (sel + 1 < count) { ++sel; PlaySfx(1); } }
    // The observer page has no editable value. Ignore horizontal input instead of playing an
    // adjustment sound that would falsely imply a mutation or persisted selection.
    if (g_f7MenuKind != F7_MENU_AI) {
        if (dir & 0x8000) { F7_AdjustValue(-1, sel); PlaySfx(1); }
        else if (dir & 0x2000) { F7_AdjustValue(+1, sel); PlaySfx(1); }
    }

    if (sel < top) top = sel;
    else if (sel >= top + page) top = sel - page + 1;
    WrW(obj, O_SELECTED, static_cast<int16_t>(sel));
    WrW(obj, O_TOP, static_cast<int16_t>(top));

    if (confirmPressed) {
        const F7SubRow& R = g_f7Rows[sel];
        if (R.type == F7RT_BACK || R.type == F7RT_ACTION ||
            (g_f7MenuKind == F7_MENU_FORCE && sel == 1)) {
            g_f7CloseLatch.RequestConfirm(sel);
            WrB(obj, 65, 1);                    // close flag -> PollMenu ve o confirm
        } else if (g_f7MenuKind != F7_MENU_AI) {
            // Information-only observer rows deliberately produce no selection feedback.
            if (R.type == F7RT_TOGGLE) {
                F7_AdjustValue(1, sel);
            }
            PlaySfx(1);
        }
    } else if (cancelPressed) {
        g_f7CloseLatch.RequestCancel();
        WrB(obj, 65, 1);
    }
    if (confirmPressed || cancelPressed) g_f7ConfirmTimer = 12;   // ~200ms anti-spam
    return obj;
}

// â”€â”€ F7 submenu: draw (SIN style: glass + neon + value on the right) â”€â”€â”€â”€â”€â”€â”€â”€
// â”€â”€ F7 DIFF draw: prominent presets + side-by-side columns + checkboxes â”€â”€â”€â”€
static void F7Diff_Draw(int F) {
    using namespace NativeMenu;
    const FfxHooks::F7DifficultyRuntimeStatus runtime =
        FfxHooks::F7_DifficultyStatus();
    char runtimeText[144] = {};
    _snprintf_s(runtimeText, sizeof(runtimeText), _TRUNCATE,
        "CFG %s | INFRA %s | ADMISSION %s | LAST %s",
        !runtime.difficultyValid ? "INVALID" : (runtime.configured ? "ON" : "OFF"),
        runtime.infrastructureInstalled
            ? "Installed" : FfxHooks::F7_DifficultyGateName(runtime.infrastructureGate),
        runtime.callbackAdmissionOpen ? "Open" : "Closed",
        FfxHooks::F7_DifficultyResultName(runtime.last.code));
    unsigned char runtimeLabel[144] = {};
    EncodeLabel(runtimeText, runtimeLabel, static_cast<int>(sizeof(runtimeLabel)));
    DrawStringSub(runtimeLabel, NX(0.073f), NY(0.181f));
    // Presets (chips proeminentes no topo)
    const float py = NY(0.205f), ph = NH(0.055f);
    const float chipW = NW(0.175f), gap = NW(0.024f);
    const float px0 = NX(0.10f);
    for (int i = 0; i < 4; ++i) {
        const float cx = px0 + (float)i * (chipW + gap);
        const bool active = (g_f7DiffPresetIdx == i);
        const bool focused = (g_f7Col == F7DC_PRESETS && g_f7ColRow == i);
        DrawSolidRect(cx, py, chipW, ph,
                      active ? 0xC02A5A3Au : (focused ? 0xA0243A4Cu : 0x80181E28u),
                      active ? 0xA0102018u : 0x60101A20u);
        if (focused) DrawSolidRect(cx, py, chipW, ph, 0x40FFFFFFu, 0x20FFFFFFu);
        const float bl = MenuBorderPx() * 0.25f;
        DrawSolidRect(cx, py, chipW, bl, active ? 0xC05AFF9Au : 0x8040AA68u, 0u);
        unsigned char label[32] = {};
        EncodeLabel(F7PresetName(i), label, (int)sizeof(label));
        DrawStringSub(label, cx + chipW * 0.5f - NX(0.050f), py + ph * 0.5f - NH(0.016f));
    }

    // Colunas de dados (BASE AUTO WEAK RESIST ABSORB ACTIONS)
    // WHY: short headers restore the pre-b219b11 readability — the quarantine
    // suffixes overflowed their 0.10-wide columns and made the grid unreadable.
    // Column headers describe the runtime controls; draft edits apply explicitly.
    static const char* const colName[F7DC_COUNT] = {
        "", "STATS", "AUTO", "WEAK", "RESIST", "ABSORB", "ACTIONS" };
    static const float colX[F7DC_COUNT] = { 0.0f, 0.045f, 0.235f, 0.465f, 0.580f, 0.695f, 0.800f };
    static const float colW[F7DC_COUNT] = { 0.0f, 0.180f, 0.215f, 0.105f, 0.105f, 0.095f, 0.145f };
    const float rowY0 = NY(0.300f), rowH = NH(0.020f), rowGap = NH(0.002f);   // 25 rows AUTO cabem (0.300+24*0.022=0.828<0.875)
    for (int c = F7DC_BASE; c <= F7DC_ACTIONS; ++c) {
        const float cx = NX(colX[c]), cw = NW(colW[c]);
        unsigned char hdr[48] = {};
        EncodeLabel(colName[c], hdr, (int)sizeof(hdr));
        DrawStringSub(hdr, cx, NY(0.272f));
        const int rows = F7DiffColRows(c);
        for (int r = 0; r < rows; ++r) {
            const float ry = rowY0 + (float)r * (rowH + rowGap);
            if (ry + rowH > NY(0.875f)) break;
            const bool focused = (g_f7Col == c && g_f7ColRow == r);
            if (focused) {
                // Pulsing highlight + neon edge restore the interactive emphasis
                // the flat 0x38/0x18 tint lost.
                const unsigned int a0 = 0x30u + (unsigned int)(Osc01(F, 45) * 0x28u);
                const unsigned int a1 = 0x18u + (unsigned int)(Osc01(F, 45) * 0x18u);
                DrawSolidRect(cx, ry, cw, rowH, (a0 << 24) | 0x0010FF40u, (a1 << 24) | 0x0010FF40u);
                const float el = MenuBorderPx() * 0.35f;
                DrawSolidRect(cx, ry + rowH - el, cw, el, kMenuNeonGreenLine, kMenuNeonGreenLineLo);
            }
            if (c == F7DC_BASE) {
                char asc[48] = {};
                _snprintf_s(asc, sizeof(asc), _TRUNCATE, "%s %s", F7_BASE_NAMES[r], F7MulName(g_f7Vals[1 + r]));
                unsigned char buf[48] = {};
                EncodeLabel(asc, buf, (int)sizeof(buf));
                DrawStringSub(buf, cx, ry);
            } else if (c >= F7DC_AUTO && c <= F7DC_ABSORB) {
                const int valIdx = (c == F7DC_AUTO) ? 10 : (c == F7DC_WEAK) ? 11 : (c == F7DC_RESIST) ? 12 : 13;
                const bool on = ((g_f7Vals[valIdx] >> r) & 1) != 0;
                const float bs = NX(0.011f);
                const float bx = cx + NX(0.004f), by = ry + (rowH - bs) * 0.5f;
                DrawSolidRect(bx, by, bs, bs, on ? 0xC050FF90u : 0xA0182028u, on ? 0xC028C058u : 0x90080810u);
                unsigned char buf[48] = {};
                const char* nm = (c == F7DC_AUTO) ? FfxHooks::F7_StatusName(r) : F7_ELEM_NAMES[r];
                EncodeLabel(nm, buf, (int)sizeof(buf));
                DrawStringSub(buf, bx + NX(0.020f), ry);
            } else {
                // WHY: Apply needs infrastructure AND open admission. With the preset OFF
                // the same row is the restore path when fields remain owned.
                const bool applyReady = runtime.infrastructureInstalled &&
                    runtime.callbackAdmissionOpen;
                const char* label = (r == 0)
                    ? (!applyReady ? "Apply Unavailable"
                        : runtime.difficultyBehaviorEnabled ? "Apply Now"
                        : runtime.ownedFieldsPresent ? "Restore Stats"
                        : "No Restore Needed")
                    : (r == 1) ? "Save" : "Back";
                unsigned char buf[48] = {};
                EncodeLabel(label, buf, (int)sizeof(buf));
                DrawStringSub(buf, cx, ry);
            }
        }
    }

    // Numeric edit mode: value typed with the cursor
    if (g_f7EditActive) {
        char asc[96] = {};
        _snprintf_s(
            asc, sizeof(asc), _TRUNCATE, ">> %d_  Bounds %d..%d  L/R 25  U/D 100",
            g_f7EditValue,
            F7_BASE_MIN[g_f7ColRow], F7_BASE_MAX[g_f7ColRow]);
        unsigned char buf[96] = {};
        EncodeLabel(asc, buf, (int)sizeof(buf));
        DrawStringSub(buf, NX(0.18f), NY(0.875f));
    }

    // Footer controls: native pad/keyboard glyphs + bounded status inside the panel.
    {
        float hintX = NX(0.073f); const float hintY = NY(0.900f);
        hintX = DrawInputHint(hintX, hintY, PC_PAD_UP, PC_PAD_DOWN, PC_KB_UP, PC_KB_DOWN, "Navigate", 0xFFFFFFFFu);
        hintX = DrawInputHint(hintX, hintY, PC_PAD_LEFT, PC_PAD_RIGHT, PC_KB_LEFT, PC_KB_RIGHT, "Adjust", 0xFFFFFFFFu);
        hintX = DrawInputHint(hintX, hintY, PC_PAD_FACE_D, PC_SKIP, PC_KB_ENTER, PC_SKIP, "Edit/Select", 0xFFFFFFFFu);
        hintX = DrawInputHint(hintX, hintY, PC_PAD_FACE_R, PC_SKIP, PC_KB_BACKSPACE, PC_SKIP, "Back", 0xFFFFFFFFu);
        DrawInputHint(hintX, hintY, PC_SKIP, PC_SKIP, PC_KB_F7, PC_SKIP, "Exit", 0xFFFFFFFFu);
    }
    if (g_f7StatusTicks > 0) {
        --g_f7StatusTicks;
        unsigned char st[64] = {};
        EncodeLabel(g_f7StatusMsg, st, (int)sizeof(st));
        DrawStringSub(st, NX(0.72f), NY(0.911f));
    }
}

static int __cdecl F7Sub_DrawCb(int obj) {
    using namespace NativeMenu;
    static int s_drawCalls = 0;
    const int F = ++s_drawCalls;
    if(g_f7MenuKind==F7_MENU_FLAGS && F8NativeSettingsActive()){F8NativeSettingsDraw(obj,F);return obj;}
    const int sel = RdW(obj, O_SELECTED);
    const int top = RdW(obj, O_TOP);
    const int page = RdW(obj, O_PAGE);

    const float pulse1 = Osc01(F, 45);
    const float neonStr = 0.55f + 0.45f * pulse1;

    char title[64] = {}, sub[64] = {}, foot[96] = {};
    const char* titleTxt = (g_f7MenuKind == F7_MENU_MUSIC) ? "F7 - Music"
                          : (g_f7MenuKind == F7_MENU_FORCE) ? "F7 - Force Battle"
                          : (g_f7MenuKind == F7_MENU_AI) ? "F7 - Monster AI Observer"
                          : (g_f7MenuKind == F7_MENU_FLAGS) ? "F8 - Settings"
                          : "F7 - Difficulty";
    const char* subTxt = (g_f7MenuKind == F7_MENU_MUSIC) ? "Lock / battle / randomizer / fade / preview / save / reset"
                        : (g_f7MenuKind == F7_MENU_FORCE) ? "Last encounter + force with 1 click"
                        : (g_f7MenuKind == F7_MENU_AI) ? "Read-only registration evidence; no game-data writes"
                        : (g_f7MenuKind == F7_MENU_FLAGS) ? "Toggle catalog flags -- effective authority wins"
                        : "Enemy stats, auto-status and elemental affinities";
    if (g_f7MenuKind == F7_MENU_FLAGS && sel >= 0 && sel < g_f7RowCount &&
        g_f7Rows[sel].desc && g_f7Rows[sel].desc[0]) {
        subTxt = g_f7Rows[sel].desc;
    }
    const bool isFlags = g_f7MenuKind == F7_MENU_FLAGS;
    const bool scalarRow = isFlags && sel >= 0 &&
        sel < g_f7FlagCount && g_f7Rows[sel].type == F7RT_SCALAR;
    const bool optionsRow=isFlags && sel>=0 && sel<g_f7RowCount && g_f7Rows[sel].type==F7RT_OPTIONS;
    const bool bulkRow = isFlags && sel >= 0 &&
        sel < g_f7FlagCount && g_f7Rows[sel].type == F7RT_BULK;
    const FfxHooks::F8Ui::FooterMode footerMode = g_f8ScalarEditor.Active()
        ? FfxHooks::F8Ui::FooterMode::ScalarEdit
        : (scalarRow || optionsRow) ? FfxHooks::F8Ui::FooterMode::Configure
        : bulkRow ? FfxHooks::F8Ui::FooterMode::Bulk
                  : FfxHooks::F8Ui::FooterMode::Toggle;
    const char* footTxt = isFlags
        ? FfxHooks::F8Ui::FooterText(footerMode)
        : (g_f7MenuKind == F7_MENU_FORCE)
            ? "Arrows/Mouse Navigate   L/R Repeat   Confirm Run/Save   Cancel Back"
            : (g_f7MenuKind == F7_MENU_AI)
                ? "Arrows/Mouse Navigate   Back Exit   F7 Exit"
                : "Arrows/Mouse Navigate   L/R Adjust   Confirm Select   Cancel Back   F7 Exit";
    EncodeLabel(titleTxt, (unsigned char*)title, (int)sizeof(title));
    EncodeLabel(subTxt, (unsigned char*)sub, (int)sizeof(sub));
    EncodeLabel(footTxt, (unsigned char*)foot, (int)sizeof(foot));

    DrawMenuBackdrop();
    DrawMenuNeonFrame(F);

    if (g_f7MenuKind == F7_MENU_DIFF) {
        const float hx = NX(0.047f), hy = NY(0.054f), hw = NW(0.906f), hh = NH(0.126f);
        DrawMenuGlassPanel(hx, hy, hw, hh, F, 0);
        DrawString((unsigned char*)title, NX(0.073f), NY(0.080f));
        DrawString((unsigned char*)sub, NX(0.073f), NY(0.132f));
        F7Diff_Draw(F);
        return obj;
    }

    const float hx = NX(0.047f), hy = NY(0.054f), hw = NW(0.906f), hh = NH(0.126f);
    DrawMenuGlassPanel(hx, hy, hw, hh, F, 0);
    DrawString((unsigned char*)title, NX(0.071f), NY(0.081f));
    DrawString((unsigned char*)sub,   NX(0.071f), NY(0.137f));
    const float mainPanelTop = isFlags ? FfxHooks::F8Ui::Layout::MainPanelTop : 0.20f;
    const float mainPanelHeight = isFlags ? FfxHooks::F8Ui::Layout::MainPanelHeight : 0.62f;
    DrawMenuGlassPanel(
        NX(0.047f), NY(mainPanelTop), NW(0.906f), NH(mainPanelHeight), F, 1);

    const float vLeft = NX(0.271f);
    const float vTop = NY(isFlags ? FfxHooks::F8Ui::Layout::RowTop : 0.215f);
    const float vWidth = NW(0.458f);
    const float vStep = NH(isFlags ? FfxHooks::F8Ui::Layout::RowStep : 0.063f);
    const float vBarH = NH(isFlags ? FfxHooks::F8Ui::Layout::RowHeight : 0.056f);
    const float vPadX = NW(0.015f);
    const int visibleRows = isFlags ? FfxHooks::F8Ui::Layout::VisibleRows : page;
    const float selLine = MenuBorderPx() * 0.45f;
    const float cursorOff = NW(0.020f);
    const float valX = (g_f7MenuKind == F7_MENU_FLAGS) ? (vLeft + vWidth - NW(0.17f)) : (vLeft + NW(0.175f));

    /* FLAGS tab bar (2026-08-16, Luna): L/R switches, active #18344F ~90% / inactive
     * #102237 ~65%, neon bottom border on the active one. Geometry mirrored in F8MouseTabHitTest. */
    if (isFlags) {
        const float tabY = NY(FfxHooks::F8Ui::Layout::TabTop);
        const float tabH = NH(FfxHooks::F8Ui::Layout::TabHeight);
        const float tabLeft = NX(FfxHooks::F8Ui::Layout::TabLeft);
        const float tabWidth = NW(FfxHooks::F8Ui::Layout::TabWidth);
        const float tabGap = NW(FfxHooks::F8Ui::Layout::TabGap);
        const int tabCount = static_cast<int>(FfxHooks::F8TabCount());
        const float tabW = tabCount > 0
            ? (tabWidth - tabGap * (tabCount - 1)) / static_cast<float>(tabCount)
            : tabWidth;
        for (int t = 0; t < tabCount; ++t) {
            const float x = tabLeft + t * (tabW + tabGap);
            const bool active = (t == g_f7Tab);
            const unsigned int c0 = active ? 0x00183C5Cu : 0x00102237u;
            const unsigned int c1 = active ? 0x00102A3Cu : 0x000A1826u;
            const unsigned int a0 = active ? 0xE6u : 0xA6u;
            DrawSolidRect(x, tabY, tabW, tabH, (a0 << 24) | c0, (a0 << 24) | c1);
            if (active) DrawSolidRect(x, tabY + tabH - MenuBorderPx() * 0.5f, tabW, MenuBorderPx() * 0.5f, kMenuNeonGreenLine, kMenuNeonGreenLineLo);
            unsigned char tlab[16] = {};
            const char* tabName=FfxHooks::F8TabName(static_cast<size_t>(t));
            EncodeLabel(tabName, tlab, (int)sizeof(tlab));
            const float labelWidth=NW(0.0086f)*static_cast<float>(strlen(tabName));
            DrawStringSub(tlab, x + (tabW-labelWidth)*0.5f, tabY + NH(0.010f));
        }
    }

    const float selVisY = vTop + (float)(sel - top) * vStep;
    // Every description/value is keyed to the newly selected row immediately. Snap the
    // shared highlight as well so neither FLAGS nor another F7 page displays stale focus.
    g_f7EasedRowY = FfxHooks::F8Ui::ResolveSelectionRowY(true, g_f7EasedRowY, selVisY);

    if (sel >= top && sel < top + page) {
        const float ey = g_f7EasedRowY;
        const unsigned int a = 0x44u + (unsigned int)(Osc01(F, 44) * 32.0f);
        const unsigned int lift0 = (a << 24) | 0x00305068u;
        const unsigned int lift1 = (a << 24) | 0x00182038u;
        DrawSolidRect(vLeft, ey, vWidth, vBarH, lift0, lift1);
        DrawSolidRect(vLeft, ey + vBarH - selLine, vWidth, selLine, kMenuNeonGreenLine, kMenuNeonGreenLineLo);
        DrawCursor(vLeft - cursorOff, ey + NH(0.002f));
    }

    for (int r = 0; r < page && r < visibleRows && top + r < g_f7RowCount; ++r) {
        const int row = top + r;
        if (g_f7Rows[row].type == F7RT_SCALAR) F8RefreshScalarLabel(row);
        const float vy = vTop + r * vStep;
        unsigned int c0 = kMenuRowGlassTop, c1 = kMenuRowGlassBot;
        if (g_f7Rows[row].type == F7RT_BACK) { c0 = kMenuGlassBorder; c1 = kMenuGlassBorderLo; }
        unsigned int a0 = ((c0 >> 24) & 0xFFu), a1 = ((c1 >> 24) & 0xFFu);
        a0 = (unsigned int)(a0 * neonStr); a1 = (unsigned int)(a1 * neonStr);
        if (a0 > 0xFF) a0 = 0xFF; if (a1 > 0xFF) a1 = 0xFF;
        c0 = (c0 & 0x00FFFFFFu) | (a0 << 24);
        c1 = (c1 & 0x00FFFFFFu) | (a1 << 24);
        DrawSolidRect(vLeft, vy, vWidth, vBarH, c0, c1);
        const char* valTxt = F7RowValueText(row);
        const bool compactInfo=g_f7MenuKind==F7_MENU_FLAGS &&
            (g_f7Rows[row].type==F7RT_INFO || g_f7Rows[row].type==F7RT_BINDING);
        if(compactInfo){
            char combined[96]{};
            _snprintf_s(combined,sizeof(combined),_TRUNCATE,"%s  %s",g_f7Rows[row].label,valTxt?valTxt:"");
            unsigned char compactLabel[96]{};EncodeLabel(combined,compactLabel,sizeof(compactLabel));DrawStringSub(compactLabel,vLeft+vPadX,vy+NH(0.021f));
        }else { unsigned char _lbl[64]; memcpy(_lbl, g_f7Labels[row], sizeof(_lbl)); DrawString(_lbl, vLeft + vPadX, vy + NH(0.016f)); }
        if (g_f7Rows[row].type == F7RT_TOGGLE) {
            /* 2026-08-17 (heap-corruption fix): draw ON/OFF as a colored DOT instead of text.
             * The menus draw ~28 texts/frame and the game's text pool corrupts (cache-hit without
             * bounds) -> heap corruption crash (0xc0000374). A dot saves 1 text per row. */
            const bool on = g_f7Vals[row] != 0;
            unsigned int c = on ? 0xE040D060u : 0xE0405058u;
            if (g_f7MenuKind==F7_MENU_FLAGS && g_f7FlagSpecs[row]) {
                const char* key=g_f7FlagSpecs[row]->gate.canonicalKey;
                const bool nativePort=strncmp(key,"window.",7)==0 || strncmp(key,"camera.",7)==0 ||
                    strcmp(key,"diagnostics.performance")==0 || strcmp(key,"input.block_windows_key")==0 ||
                    strcmp(key,"input.fix_background_input")==0 || strcmp(key,"input.filter_ime")==0;
                if(nativePort){
                    const auto state=FfxHooks::GetF8RuntimeStatus(*g_f7FlagSpecs[row]);
                    const bool applied=state.hasAppliedValue && state.appliedValue;
                    if(on!=applied)c=0xE0D6AB61u;
                }
            }
            const float d = NH(0.020f);
            DrawSolidRect(valX, vy + NH(0.021f), d, d, c, c);
        } else if (!compactInfo && valTxt && valTxt[0]) {
            unsigned char vlab[48] = {};
            EncodeLabel(valTxt, vlab, (int)sizeof(vlab));
            DrawStringSub(vlab, valX, vy + NH(0.023f));
        }
    }

    /* The header owns player help; this lower line is reserved for compact technical state. */
    if (isFlags && sel >= 0 && sel < g_f7FlagCount) {
        char status[FfxHooks::F8Ui::TechnicalStatusCharacterBudget + 1] = {};
        F8BuildSelectedStatus(sel, status, sizeof(status));
        unsigned char encoded[FfxHooks::F8Ui::TechnicalStatusCharacterBudget + 1] = {};
        EncodeLabel(status, encoded, static_cast<int>(sizeof(encoded)));
        DrawStringSub(encoded, vLeft + vPadX, NY(FfxHooks::F8Ui::Layout::DetailTop));
    }

    const float fx = NX(0.047f);
    const float fy = NY(isFlags ? FfxHooks::F8Ui::Layout::FooterTop : 0.887f);
    const float fw = NW(0.906f), fh = NH(0.070f);
    DrawMenuGlassPanel(fx, fy, fw, fh, F, 1);
    // WHY: native pad/keyboard glyphs restore the button-icon hints the menus had
    // before the text-only footers. footTxt/foot stay encoded as the semantic
    // record — the F8Ui footer contract keys off FooterText, not the draw call.
    (void)foot;
    {
        float hintX = NX(0.071f); const float hintY = fy + NH(0.018f);
        if (isFlags) {
            if (g_f8ScalarEditor.Active()) {
                hintX = DrawInputHint(hintX, hintY, PC_PAD_LEFT, PC_PAD_RIGHT, PC_KB_LEFT, PC_KB_RIGHT, "+/-1", 0xFFFFFFFFu);
                hintX = DrawInputHint(hintX, hintY, PC_PAD_UP, PC_PAD_DOWN, PC_KB_UP, PC_KB_DOWN, "+/-10", 0xFFFFFFFFu);
                hintX = DrawInputHint(hintX, hintY, PC_PAD_FACE_D, PC_SKIP, PC_KB_ENTER, PC_SKIP, "Save", 0xFFFFFFFFu);
                hintX = DrawInputHint(hintX, hintY, PC_PAD_FACE_R, PC_SKIP, PC_KB_BACKSPACE, PC_SKIP, "Cancel", 0xFFFFFFFFu);
                DrawInputHint(hintX, hintY, PC_SKIP, PC_SKIP, PC_KB_F8, PC_SKIP, "Exit", 0xFFFFFFFFu);
            } else {
                hintX = DrawInputHint(hintX, hintY, PC_PAD_UP, PC_PAD_DOWN, PC_KB_UP, PC_KB_DOWN, "Navigate", 0xFFFFFFFFu);
                hintX = DrawInputHint(hintX, hintY, PC_PAD_LEFT, PC_PAD_RIGHT, PC_KB_LEFT, PC_KB_RIGHT, "Tabs", 0xFFFFFFFFu);
                hintX = DrawInputHint(hintX, hintY, PC_PAD_FACE_D, PC_SKIP, PC_KB_ENTER, PC_SKIP,
                                      bulkRow ? "Apply Tab" : scalarRow ? "Configure" : "Toggle",
                                      0xFFFFFFFFu);
                hintX = DrawInputHint(hintX, hintY, PC_PAD_FACE_R, PC_SKIP, PC_KB_BACKSPACE, PC_SKIP, "Back", 0xFFFFFFFFu);
                DrawInputHint(hintX, hintY, PC_SKIP, PC_SKIP, PC_KB_F8, PC_SKIP, "Exit", 0xFFFFFFFFu);
            }
        } else if (g_f7MenuKind == F7_MENU_FORCE) {
            hintX = DrawInputHint(hintX, hintY, PC_PAD_UP, PC_PAD_DOWN, PC_KB_UP, PC_KB_DOWN, "Navigate", 0xFFFFFFFFu);
            hintX = DrawInputHint(hintX, hintY, PC_PAD_LEFT, PC_PAD_RIGHT, PC_KB_LEFT, PC_KB_RIGHT, "Repeat", 0xFFFFFFFFu);
            hintX = DrawInputHint(hintX, hintY, PC_PAD_FACE_D, PC_SKIP, PC_KB_ENTER, PC_SKIP, "Run/Save", 0xFFFFFFFFu);
            hintX = DrawInputHint(hintX, hintY, PC_PAD_FACE_R, PC_SKIP, PC_KB_BACKSPACE, PC_SKIP, "Back", 0xFFFFFFFFu);
            DrawInputHint(hintX, hintY, PC_SKIP, PC_SKIP, PC_KB_F7, PC_SKIP, "Exit", 0xFFFFFFFFu);
        } else if (g_f7MenuKind == F7_MENU_AI) {
            hintX = DrawInputHint(hintX, hintY, PC_PAD_UP, PC_PAD_DOWN, PC_KB_UP, PC_KB_DOWN, "Navigate", 0xFFFFFFFFu);
            hintX = DrawInputHint(hintX, hintY, PC_PAD_FACE_R, PC_SKIP, PC_KB_BACKSPACE, PC_SKIP, "Back", 0xFFFFFFFFu);
            DrawInputHint(hintX, hintY, PC_SKIP, PC_SKIP, PC_KB_F7, PC_SKIP, "Exit", 0xFFFFFFFFu);
        } else {
            hintX = DrawInputHint(hintX, hintY, PC_PAD_UP, PC_PAD_DOWN, PC_KB_UP, PC_KB_DOWN, "Navigate", 0xFFFFFFFFu);
            hintX = DrawInputHint(hintX, hintY, PC_PAD_LEFT, PC_PAD_RIGHT, PC_KB_LEFT, PC_KB_RIGHT, "Adjust", 0xFFFFFFFFu);
            hintX = DrawInputHint(hintX, hintY, PC_PAD_FACE_D, PC_SKIP, PC_KB_ENTER, PC_SKIP, "Select", 0xFFFFFFFFu);
            hintX = DrawInputHint(hintX, hintY, PC_PAD_FACE_R, PC_SKIP, PC_KB_BACKSPACE, PC_SKIP, "Back", 0xFFFFFFFFu);
            DrawInputHint(hintX, hintY, PC_SKIP, PC_SKIP, PC_KB_F7, PC_SKIP, "Exit", 0xFFFFFFFFu);
        }
    }
    return obj;
}

// â”€â”€ F7 submenu: spawn / poll / close / confirm â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static NativeMenu::Menu F7Sub_SpawnMenu(int kind) {
    const bool isFlags = kind == F7_MENU_FLAGS;
    const bool isDirectF8Flags = isFlags && g_f8MenuOpen.Load();
    if (kind == F7_MENU_FLAGS) {
        g_f7Tab = 0;                        // Settings starts on the System tab.
        g_f8ScalarEditor.Cancel();
    }
    F7_BuildRows(kind);
    int obj = NativeMenu::Alloc();
    if (!obj) {
        if (isDirectF8Flags) F8ReturnFlagsToGame();
        return NativeMenu::Menu{ 0 };
    }
    if (!isDirectF8Flags) {
        F7SeedPointerForDestination();
        F7AcquireCursorOwnership();
        g_f7UiModalState.open = true;
        g_f7UiModalState.inputBlockOwned = true;
        g_f7UiModalState.nativeGateOwned = true;
        g_f7UiModalState.forceGateOwned = true;
        g_f7UiModalState.draftActive = false;
        g_f7UiModalState.selection = 0;
        g_f7UiModalState.firstVisible = 0;
        g_f7UiModalState.submenu = kind;
        InterlockedExchange(&g_f7MouseWheelDelta, 0);
    }
    if (isFlags) {
        F7SeedPointerForDestination();
        InterlockedExchange(&g_f7MouseWheelDelta,0);
        g_f8MouseWasDown=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
        F8AcquireCursorVisibility();
    }
    // Difficulty supplies a positive compatibility count so the vanilla draw tick invokes our
    // multi-column renderer; a zero count caused the historical black-screen return path.
    NativeMenu::WrW(obj, NativeMenu::O_COUNT, static_cast<int16_t>(g_f7RowCount > 0 ? g_f7RowCount : 8));
    NativeMenu::WrW(
        obj, NativeMenu::O_PAGE,
        (g_f7MenuKind == F7_MENU_FLAGS) ? FfxHooks::F8Ui::Layout::VisibleRows : 6);
    NativeMenu::WrW(obj, NativeMenu::O_TOP,   0);
    NativeMenu::WrW(obj, NativeMenu::O_SELECTED, 0);
    NativeMenu::WrB(obj, NativeMenu::O_SLOTS, 1);
    NativeMenu::WrB(obj, NativeMenu::O_CANCEL, 1);
    NativeMenu::WrB(obj, NativeMenu::O_GROUP62, 2);
    NativeMenu::WrB(obj, NativeMenu::O_GROUP63, 1);
    NativeMenu::WrP(obj, NativeMenu::O_ENTER, (void*)0);
    NativeMenu::WrP(obj, NativeMenu::O_UPDATE, (void*)(uintptr_t)&F7Sub_InputCb);
    NativeMenu::WrP(obj, NativeMenu::O_DRAW, (void*)(uintptr_t)&F7Sub_DrawCb);
    NativeMenu::WrP(obj, NativeMenu::O_AUX, (void*)(uintptr_t)&NativeMenu::OurAux);
    NativeMenu::WrP(obj, NativeMenu::O_VALIDATOR, (void*)0);
    NativeMenu::ClaimModal(obj);  // Prevent the game FSM from processing the same pad edge twice.
    g_f7CloseLatch.Reset();
    g_f7LastEdge = 0;
    g_f7EasedRowY = -1.0f;
    g_f7Col = F7DC_PRESETS; g_f7ColRow = 0; g_f7EditActive = 0; g_f7StatusTicks = 0;
    NativeMenu::Register(obj);
    return NativeMenu::Menu{ obj };
}

static NativeMenu::Poll F7Sub_PollMenu(const NativeMenu::Menu& m) {
    (void)m;
    const FfxHooks::F8Ui::CloseEvent event = g_f7CloseLatch.Consume();
    if (event.kind == FfxHooks::F8Ui::CloseKind::Confirm) {
        return NativeMenu::Poll{ NativeMenu::POLL_CONFIRM, event.row };
    }
    if (event.kind == FfxHooks::F8Ui::CloseKind::Cancel) {
        return NativeMenu::Poll{ NativeMenu::POLL_CANCEL, 0 };
    }
    return NativeMenu::Poll{ NativeMenu::POLL_NAV, 0 };
}

static void F7Sub_CloseMenu() {
    const int closingObj = g_f7Menu.obj;
    if (closingObj) {
        NativeMenu::WrB(closingObj, 65, 1);
        NativeMenu::ReleaseModalIfOwned(closingObj);
        g_f7Menu.obj = 0;
    }
    F8ReleaseCursorOwnership();
    g_f8ScalarEditor.Cancel();
    g_f8MenuOpen.Store(false);
    g_f8MouseWasDown = false;
    g_f7CloseLatch.Reset();
}

static const char* F7CloseSourceName(FfxHooks::F7Ui::CloseSource source) {
    switch (source) {
        case FfxHooks::F7Ui::CloseSource::BackRow: return "back";
        case FfxHooks::F7Ui::CloseSource::Cancel: return "cancel";
        case FfxHooks::F7Ui::CloseSource::Hotkey: return "hotkey";
        case FfxHooks::F7Ui::CloseSource::FocusLost: return "focus_lost";
        case FfxHooks::F7Ui::CloseSource::Stop: return "stop";
        default: return "unknown";
    }
}

static void ArenaPlus_UltraCancelForClose(FfxHooks::F7Ui::CloseSource source) {
    ArenaMixRenameAbort();
    using namespace FfxHooks::CustomMixUltra::Runtime;
    const StatusSnapshot status = ProductionStatus();
    if (g_arenaPlusMenuKind != ArenaPlusMenuKind::Ultra &&
        !ArenaPlus_IsUltraChild(g_arenaPlusMenuKind) &&
        g_arenaPlusMenuKind != ArenaPlusMenuKind::Scenery &&
        g_arenaPlusMenuKind != ArenaPlusMenuKind::Positions &&
        g_arenaPlusMenuKind != ArenaPlusMenuKind::Library &&
        g_arenaPlusMenuKind != ArenaPlusMenuKind::LibraryItem &&
        g_arenaPlusMenuKind != ArenaPlusMenuKind::Rename && status.code != StatusCode::Queued) {
        return;
    }
    CancelReason reason = CancelReason::Close;
    switch (source) {
    case FfxHooks::F7Ui::CloseSource::BackRow: reason = CancelReason::Back; break;
    case FfxHooks::F7Ui::CloseSource::Cancel: reason = CancelReason::Cancel; break;
    case FfxHooks::F7Ui::CloseSource::FocusLost: reason = CancelReason::FocusLoss; break;
    case FfxHooks::F7Ui::CloseSource::Stop: reason = CancelReason::Stop; break;
    default: reason = CancelReason::Close; break;
    }
    ProductionCancel(reason);
    g_arenaPlusUltraSelection = {};
    g_arenaPositionDraft = {};
}

static void F7CloseTransition(
    FfxHooks::F7Ui::CloseSource source,
    FfxHooks::F7Ui::CloseDestination destination) {
    ArenaPlus_UltraCancelForClose(source);
    const bool directF8Flags =
        g_f8MenuOpen.Load() && g_f7Menu.obj && g_f7MenuKind == F7_MENU_FLAGS;
    if (directF8Flags) {
        // Direct-F8 owns its lifecycle outside the F7 modal mirror; drain it through
        // the F8 return path so a focus-lost close cannot leave a background menu.
        F7Sub_CloseMenu();
        F8ReturnFlagsToGame();
        Log("[ffx-hooks] F8: direct FLAGS menu closed source=%s\n",
            F7CloseSourceName(source));
        return;
    }

    // Refresh the portable ownership mirror from the concrete adapter before
    // deciding effects. This keeps cleanup correct after a partially completed
    // hub/submenu handoff and makes repeated close requests harmless.
    g_f7UiModalState.open = F7OwnsVisibleUi() || g_f7UiModalState.open;
    g_f7UiModalState.inputBlockOwned = g_f7Menu.obj != 0;
    g_f7UiModalState.cursorOwned = g_f7CursorShowIncrements > 0;
    g_f7UiModalState.nativeGateOwned =
        InterlockedCompareExchange(&g_forceSubsystem, 0, 0) != 0;
    g_f7UiModalState.forceGateOwned = g_f7UiModalState.nativeGateOwned;
    g_f7UiModalState.draftActive =
        g_f7EditActive != 0 || g_sinDraftActive || g_sinMenu.obj != 0 ||
        (g_arenaPlusMenu.obj != 0 && g_arenaPlusMenuKind == ArenaPlusMenuKind::Ultra) ||
        (g_arenaPlusMenu.obj != 0 && ArenaPlus_IsUltraChild(g_arenaPlusMenuKind)) ||
        (g_f7Menu.obj != 0 && g_f7MenuKind != F7_MENU_FLAGS);
    if (g_f7Menu.obj) {
        g_f7UiModalState.selection = NativeMenu::RdW(g_f7Menu.obj, NativeMenu::O_SELECTED);
        g_f7UiModalState.firstVisible = NativeMenu::RdW(g_f7Menu.obj, NativeMenu::O_TOP);
        g_f7UiModalState.submenu = g_f7MenuKind;
    } else if (g_sinMenu.obj) {
        g_f7UiModalState.selection = NativeMenu::RdW(g_sinMenu.obj, NativeMenu::O_SELECTED);
        g_f7UiModalState.firstVisible = NativeMenu::RdW(g_sinMenu.obj, NativeMenu::O_TOP);
        g_f7UiModalState.submenu = -2;
    } else if (g_nativeMenu.obj) {
        g_f7UiModalState.selection = NativeMenu::RdW(g_nativeMenu.obj, NativeMenu::O_SELECTED);
        g_f7UiModalState.firstVisible = NativeMenu::RdW(g_nativeMenu.obj, NativeMenu::O_TOP);
        g_f7UiModalState.submenu = -1;
    }

    const FfxHooks::F7Ui::CloseEffects effects = FfxHooks::F7Ui::CloseModal(
        g_f7UiModalState, source, destination);
    if (!effects.changed) return;

    if (g_nativeMenu.obj) {
        const int closingObject = g_nativeMenu.obj;
        NativeMenu::CloseMenu(g_nativeMenu);
        NativeMenu::ReleaseModalIfOwned(closingObject);
        NativeMenuQueueHubCloseDrain(
            closingObject, source != FfxHooks::F7Ui::CloseSource::Stop);
    }
    ArenaPlus_CloseMenu(g_arenaPlusMenu);
    SinCurse_CloseMenu();
    EquipmentMenu::Close();
    F7Sub_CloseMenu();
    ArenaPlusComposePick_Close();
    g_nativeHeldAction = -1;

    if (effects.releaseCursor) F7ReleaseCursorOwnership();
    if (effects.cancelDraft) {
        g_f7EditActive = 0;
        g_f7EditValue = 0;
        g_f7EditDigits = 0;
        g_sinDraft = {};
        g_sinDraftActive = false;
        g_sinSeedEditing = false;
        ArenaMixRenameAbort();
        g_arenaPlusUltraSelection = {};
        SinRam_ClearSaveFeedback();
        g_f8ScalarEditor.Cancel();
    }
    g_f7ConfirmTimer = 0;
    g_f7LastEdge = 0;
    g_f7EasedRowY = -1.0f;
    InterlockedExchange(&g_f7MouseWheelDelta, 0);
    InterlockedExchange(&g_nativeWantClose, 0);
    InterlockedExchange(&g_arenaPlusWantOpen, 0);
    InterlockedExchange(&g_sinWantOpen, 0);
    InterlockedExchange(&g_f7WantOpenKind, -1);
    const bool keepCleanupPump = NativeMenuHubCloseDrainPending() || EquipmentMenu::NeedsPump();
    InterlockedExchange(&g_forceSubsystem, keepCleanupPump ? 1 : 0);
    if (!keepCleanupPump) {
        NativeMenuForceGateClear();
    }

    if (effects.returnToHub) {
        // Reacquire the force gate only after all old modal ownership has been
        // released. The next pump allocation starts from a clean object/draft.
        InterlockedExchange(&g_forceSubsystem, 1);
        InterlockedExchange(&g_nativeWantSpawn, 1);
        g_f7UiModalState.nativeGateOwned = true;
        g_f7UiModalState.forceGateOwned = true;
    } else {
        InterlockedExchange(&g_nativeWantSpawn, 0);
    }
    Log("[ffx-hooks] F7 close source=%s destination=%s generation=%u\n",
        F7CloseSourceName(source), effects.returnToHub ? "hub" : "game",
        static_cast<unsigned>(g_f7UiModalState.cleanupGeneration));
}

// Commit the current UI values into Music/Force/Difficulty configuration.
static void F7_CommitValsToConfig() {
    if (g_f7MenuKind == F7_MENU_MUSIC) {
        FfxHooks::F7_SetMusicLock(g_f7Vals[0]);
        FfxHooks::F7_SetMusicBattleTrack(g_f7Vals[1]);
        FfxHooks::F7_SetMusicRandomizer(g_f7Vals[2] != 0);
        FfxHooks::F7_SetMusicFade(g_f7Vals[3]);
    } else if (g_f7MenuKind == F7_MENU_FORCE) {
        FfxHooks::F7_SetRepeatCount(g_f7Vals[1]);
    } else if (g_f7MenuKind == F7_MENU_AI) {
        // The observer page has no mutable draft. Returning here also prevents
        // observer navigation from falling through to the Difficulty writer.
        return;
    } else if (g_f7MenuKind == F7_MENU_FLAGS) {
        return; // FLAGS persistence is transactional per row in SetF8FlagValue.
    } else {
        FfxHooks::F7DifficultyPreset p =
            FfxHooks::F7_GetConfigSnapshot().config.diffGlobal;
        p.enabled = g_f7DifficultyEnabled;
        p.hpMul = g_f7Vals[1];  p.strMul = g_f7Vals[2];  p.defMul = g_f7Vals[3];
        p.magMul = g_f7Vals[4]; p.mdfMul = g_f7Vals[5];  p.agiMul = g_f7Vals[6];
        p.accMul = g_f7Vals[7]; p.evaMul = g_f7Vals[8];  p.lckMul = g_f7Vals[9];
        p.autoStatusMask = (uint32_t)g_f7Vals[10] & 0x01FFFFFFu;
        p.elemWeak = (uint8_t)(g_f7Vals[11] & 0x1F);
        p.elemResist = (uint8_t)(g_f7Vals[12] & 0x1F);
        p.elemAbsorb = (uint8_t)(g_f7Vals[13] & 0x1F);
        FfxHooks::F7_SetDifficultyGlobal(p);
    }
}

static void F7Sub_HandleConfirm(int row) {
    // Music and Difficulty execute Preview/Save/Reset/Apply in their dedicated input paths and
    // close through POLL_CANCEL. Only Force Last Battle consumes CONFIRM here.
    if (g_f7MenuKind == F7_MENU_FORCE) {
        if (row == 0) { FfxHooks::F7_ForceLastBattle(); }
        else if (row == 1) {
            F7_CommitValsToConfig();
            FfxHooks::F7_SaveConfig();
            Log("[ffx-hooks] F7 Force: Repeat save requested value=%d; verify f7_inlive.json/log\n",
                g_f7Vals[1]);
        }
    }
}

static PLH::x86Detour* g_nativeTextOutlineDetour = nullptr;
static uint64_t g_nativeTextOutlineTramp = 0;
// WHY (native text overflow, 2026-08-03/2026-09-15 RT2): RVA 0x4FAE40 is the
// eight-neighbor glyph-outline emitter called by the generic text loop at RVA 0x501700.
// Custom menus already consume a large 2D batch, so omit their outline pass while concrete menu
// or close-drain ownership exists. Resume the trampoline after drain so later vanilla text keeps
// its normal outline/color and the battle-reward renderer receives no process-lifetime suppression.
using FnNativeTextOutline = int(__cdecl*)(void* renderState, void* glyphMetrics, float scale);
static int __cdecl NativeTextOutline_MenuGuard(
    void* renderState, void* glyphMetrics, float scale) {
    const bool menuActive = EquipmentMenu::Active() || g_nativeMenu.obj || g_arenaPlusMenu.obj || g_sinMenu.obj || g_f7Menu.obj ||
        ArenaPlusComposePick_IsActive() || FfxHooks::Maechen_MenuOwned();
    if (menuActive || NativeMenuHubCloseDrainPending()) return 0;
    return reinterpret_cast<FnNativeTextOutline>(g_nativeTextOutlineTramp)(
        renderState, glyphMetrics, scale);
}

static bool StartNativeTextOutlineGuard() {
    if (g_nativeTextOutlineDetour) return true;
    if (!g_base) return false;
    PLH::x86Detour* detour = nullptr;
    try {
        detour = new PLH::x86Detour(
            (uint64_t)(g_base + 0x4FAE40u),
            (uint64_t)&NativeTextOutline_MenuGuard, &g_nativeTextOutlineTramp);
        const bool ok = detour->hook();
        Log("[ffx-hooks] Native text-outline guard hook ok=%d target_rva=0x004FAE40\n",
            ok ? 1 : 0);
        if (!ok) {
            delete detour;
            return false;
        }
        g_nativeTextOutlineDetour = detour;
        return true;
    } catch (const std::exception& ex) {
        delete detour;
        Log("[ffx-hooks] ERROR native text-outline guard exception: %s\n", ex.what());
    } catch (...) {
        delete detour;
        Log("[ffx-hooks] ERROR native text-outline guard unknown exception\n");
    }
    return false;
}

static bool StartNativeMenuIfEnabled() {
    // Arm with FFXHOOKS_ENABLE_NATIVE_MENU=1 or modules/native_menu.flag (Steam-safe).
    // F7 is detected on Present (NativeMenu_PresentTick) — StartAuroraOverlayIfEnabled enables the D3D11
    // when native_menu.flag is armed (no separate aurora_overlay_d3d11.flag needed). Lane IFRIT/ARENA.
    InterlockedExchange(&g_nativeMenuProducerReady, 0);
    InterlockedExchange(&g_nativeOtherOwnerPublished, 0);
    const bool armed = NativeMenuArmedFromConfig();
    if (!armed) {
        Log("[ffx-hooks] NativeMenu: disabled (env FFXHOOKS_ENABLE_NATIVE_MENU=1 OR modules/native_menu.flag to arm)\n");
        return false;
    }
    if (!g_base) { Log("[ffx-hooks] NativeMenu: g_base not resolved - abort\n"); return false; }
    NativeMenu::SetBridge(NativeMenu::PhotoModeBridge{ &NativeMenu_OnEdge, &NativeMenu_OnHeldEnter });
    NativeMenu::SetInputAdmission(&F7RootInputAdmitted);
    PhotoMode::g_base = g_base;
    ArenaPlusComposePick_SetLog(&Log);
    ArenaPlusComposePick_SetModule(g_module);
    ArenaPlusComposePick_SetValidateOnly(g_runtimeValidateOnly);
    const int hk = EnvInt("FFXHOOKS_NATIVE_MENU_HOTKEY", VK_F7);
    const FfxHooks::F7Ui::HotkeyResolution hotkey =
        FfxHooks::F7Ui::ResolveNativeMenuHotkey(hk);
    g_nativeMenuHotkey = hotkey.effectiveVirtualKey;
    // Preserve the adapter-level Maechen ownership invariant even if the
    // portable collision table is extended or refactored independently.
    g_nativeMenuHotkey = g_nativeMenuHotkey == VK_F9 ? VK_F7 : g_nativeMenuHotkey;
    NativeMenu::SetNotice(FfxHooks::F7Ui::HotkeyReasonText(hotkey.reason));
    if (hotkey.reason != FfxHooks::F7Ui::HotkeyReason::None) {
        Log("[ffx-hooks] NativeMenu hotkey remapped requested=0x%02X effective=F7 reason=%s\n",
            hk, FfxHooks::F7Ui::HotkeyReasonText(hotkey.reason));
    }
    const uintptr_t pumpVa = g_base + (0x8A9C50u - 0x400000u); // FFX_Menu_PerFramePump int __cdecl(uint) [IDA d091ab12]
    try {
        g_nativeMenuPumpDetour = new PLH::x86Detour(
            static_cast<uint64_t>(pumpVa),
            reinterpret_cast<uint64_t>(&NativeMenu_PumpHook),
            &g_nativeMenuPumpTramp);
        const bool ok = g_nativeMenuPumpDetour->hook();
        Log("[ffx-hooks] NativeMenu pump hook ok=%d hotkey=0x%02X (menu remains OFF until the hotkey)\n", ok ? 1 : 0, g_nativeMenuHotkey);
        if (!ok) { delete g_nativeMenuPumpDetour; g_nativeMenuPumpDetour = nullptr; g_nativeMenuPumpTramp = 0; }
        if (ok) { InterlockedExchange(&g_nativeMenuProducerReady, 1); }
    } catch (const std::exception& ex) {
        Log("[ffx-hooks] ERROR NativeMenu pump hook exception: %s\n", ex.what());
        g_nativeMenuPumpDetour = nullptr; g_nativeMenuPumpTramp = 0;
    } catch (...) {
        Log("[ffx-hooks] ERROR NativeMenu pump hook unknown exception\n");
        g_nativeMenuPumpDetour = nullptr; g_nativeMenuPumpTramp = 0;
    }
    return g_nativeMenuPumpDetour != nullptr;
}

static void StopNativeMenu() {
    InterlockedExchange(&g_nativeMenuProducerReady, 0);
    __try {
        F7CloseTransition(
            FfxHooks::F7Ui::CloseSource::Stop,
            FfxHooks::F7Ui::CloseDestination::Game);
        // Direct F8 FLAGS is intentionally outside F7 ownership; retain the
        // existing stop fallback for any non-F7 modal still alive here.
        if (g_nativeMenu.obj) NativeMenu::CloseMenu(g_nativeMenu);
        if (g_arenaPlusMenu.obj) ArenaPlus_CloseMenu(g_arenaPlusMenu);
        SinCurse_CloseMenu();
        F7Sub_CloseMenu();
        ArenaPlusComposePick_Close();
        if (PhotoMode::g_pm.on) PhotoMode::Exit();   // Restore actors and camera to vanilla state.
        g_nativeHeldAction = -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[ffx-hooks] WARN NativeMenu stop exception\n");
    }
    __try {
        NativeMenuAbortHubCloseDrainForStop();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_forceSubsystem, 0);
        Log("[ffx-hooks] WARN NativeMenu stop close-drain abort exception\n");
    }
    if (g_nativeMenuPumpDetour) {
        g_nativeMenuPumpDetour->unHook();   // PolyHook2: o destrutor tambem desfaz (belt-and-suspenders)
        delete g_nativeMenuPumpDetour;
        g_nativeMenuPumpDetour = nullptr;
        g_nativeMenuPumpTramp = 0;
    }
    NativeMenu::SetInputAdmission(nullptr);
}
#endif // FFXHOOKS_HAVE_POLYHOOK

/* â”€â”€ Hook install / remove â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
static void StartNovaPoolEarlyIfRequested();
static void InstallHooks() {
    StartupTiming("install-enter");
    Log("[ffx-hooks] InstallHooks enter\n");
    Log("[ffx-hooks] before GetModuleHandleA(FFX.exe)\n");
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleA("FFX.exe"));
    if (!g_base) {
        Log("[ffx-hooks] WARN FFX.exe base not found\n");
        return;
    }
    Log("[ffx-hooks] FFX.exe base = 0x%08X\n", static_cast<unsigned>(g_base));
#ifdef FFXHOOKS_HAVE_POLYHOOK
    // MinHook owns one process-global heap. Initialize it before any feature start so Seymour,
    // F7, and FieldScout never depend on another feature's gate or relative install order.
    const FfxHooks::MinHookBatch::InitializationResult minHookInitialization =
        FfxHooks::MinHookBatch::EnsureProcessInitialized();
    const bool minHookReady = minHookInitialization ==
        FfxHooks::MinHookBatch::InitializationResult::Ready;
    Log("[ffx-hooks] process-global MinHook initialization=%u\n",
        static_cast<unsigned>(minHookInitialization));
    if (!minHookReady) {
        Log("[ffx-hooks] MinHook-dependent features skipped; "
            "process-global initialization is not ready\n");
    }
#else
    constexpr bool minHookReady = false;
#endif
    const bool arenaResolverLogStartupRequested = ArenaPlus_ResolverLogEnabled();
    const bool f7DifficultyStartupRequested = FfxHooks::Config::CheckEnabled(
        "f7.inlive", "FFXHOOKS_ENABLE_F7", "f7_inlive.flag", false);
    // WHY: a saved Difficulty preset requests shared infrastructure by itself — the broad
    // F7 master gates behavior inside the callbacks, never whether the batch exists.
#ifdef FFXHOOKS_HAVE_POLYHOOK
    const bool f7DifficultyConfigRequested = FfxHooks::F7_DifficultyRequestedFromDisk();
    const bool f7SinConfigRequested = FfxHooks::F7_SinRequestedFromDisk();
#else
    constexpr bool f7DifficultyConfigRequested = false;
    constexpr bool f7SinConfigRequested = false;
#endif
    const bool sharedBattleRuntimeRequested =
        FfxHooks::SharedBattleRuntime::AnyConsumerRequiresRuntime({
            f7DifficultyStartupRequested || f7DifficultyConfigRequested,
            FfxHooks::SharedBattleRuntime::kSeymourLiveProducerRequiresInfrastructure,
            f7SinConfigRequested,
            f7DifficultyStartupRequested || F8CatalogGateEnabled("arena_plus.master"),
        });
    // WHY: F7 currently retains ResolveEncounter, InitScene, and ActorInit as one exact
    // process-lifetime batch. Even when only the inert Seymour LIVE producer needs InitScene,
    // the shared owner necessarily reserves ResolveEncounter; allowing Arena ResolverLog to
    // install there would create a second MinHook owner. This explicit startup conflict remains
    // until a separately reviewed partial-batch owner can preserve the same lifecycle proofs.
    const FfxHooks::ResolverOwner::SharedResolverStartupPlan sharedResolverStartupPlan =
        FfxHooks::ResolverOwner::PlanSharedResolverStartup(
            arenaResolverLogStartupRequested, sharedBattleRuntimeRequested);
    // WHY: these immutable startup values arbitrate the shared ResolveEncounter prologue. A
    // later flag-file change may wait for restart, but it cannot create a second runtime owner.
    Log("[ffx-hooks] shared resolver startup resolver_log=%d f7=%d battle_runtime=%d owner_reason=%s\n",
        arenaResolverLogStartupRequested ? 1 : 0,
        f7DifficultyStartupRequested ? 1 : 0,
        sharedBattleRuntimeRequested ? 1 : 0,
        FfxHooks::ResolverOwner::SharedResolverStartupReasonName(
            sharedResolverStartupPlan.reason));
    Log("[ffx-hooks] F8 catalog rows=%zu (Arena+ progression bypass available)\n", FfxHooks::F8FlagCount());
    PublishResolvedF8Status("arena_plus.unlock_all", FfxHooks::F8RuntimeAvailability::Available, true);
    const bool f8RuntimeAdapterReady = FfxHooks::StartUnXBoosterHook(g_base, LogLine);
    LogF8CatalogGate("field_scout.master", "FieldScout startup");
    LogF8CatalogGate("field_scout.heavy", "FieldScout startup");
    LogF8CatalogGate("field_scout.max", "FieldScout startup");
    LogF8CatalogGate("field_scout.ultra", "FieldScout startup");
    LogF8CatalogGate("arena_plus.master", "ArenaPlus startup");
    LogF8CatalogGate("arena_plus.victory_hook", "ArenaPlus startup");
    LogF8CatalogGate("arena_plus.resolver_log", "ArenaPlus startup");
    LogF8CatalogGate("arena_plus.music", "ArenaPlus startup");

    /* Compose is config-polled. The UnX adapter owns its ten runtime statuses from Start. */
    PublishResolvedF8Status(
        "arena_plus.compose_f7", FfxHooks::F8RuntimeAvailability::ProducerUnavailable, false);

    AddVectoredExceptionHandler(1, FfxFaultProbeVeh);
    Log("[ffx-hooks] FaultProbe VEH armed (global fault diagnosis)\n");

    // CreateBlock is deferred â€” only created below when a hook that needs shared memory (MusicHook) is active.
    // Unconditional CreateBlock (pre-fix) caused heap corruption (0xc0000374) by changing the game's memory layout.
    g_mmf = NULL;
    g_block = nullptr;

    /* Phase 1: Music Swap hook. */
#ifdef FFXHOOKS_HAVE_POLYHOOK
    const bool enableMusic = MusicHookEnabledFromConfig();
    const bool validateOnly = EnvFlagEnabled("FFXHOOKS_VALIDATE_ONLY");
    g_runtimeValidateOnly = validateOnly;
    const FfxHooks::MusicHookTarget musicTarget = MusicHookTargetFromEnv();
    const bool enableFpsScout = FpsScoutEnabledFromConfig();
    const int initialOverride = EnvInt("FFXHOOKS_MUSIC_OVERRIDE_TRACK", -1);
    if (g_block && initialOverride >= 0 && initialOverride <= 0xB5) {
        g_block->musicOverrideTrackIndex = initialOverride;
        Log("[ffx-hooks] initial music override armed from env: %d\n", initialOverride);
    } else if (initialOverride != -1) {
        Log("[ffx-hooks] WARN ignoring out-of-range FFXHOOKS_MUSIC_OVERRIDE_TRACK=%d\n", initialOverride);
    }

    Log("[ffx-hooks] PolyHook build active (MusicHookEnabled=%d, FFXHOOKS_VALIDATE_ONLY=%d, target=%s)\n",
        enableMusic ? 1 : 0,
        validateOnly ? 1 : 0,
        FfxHooks::GetMusicHookTargetName(musicTarget));

    const MusicTargetValidation validation = ValidateMusicTargets();
    const bool selectedTargetOk =
        (musicTarget == FfxHooks::MusicHookTarget::SwitchCrossfade)
            ? validation.switchCrossfadeOk
            : validation.playTrackOk;
    if (!enableMusic) {
        Log("[ffx-hooks] MusicHook compiled/validated but not installed (set FFXHOOKS_ENABLE_MUSIC=1, music.flag, or arena_plus_music.flag to arm)\n");
    } else if (validateOnly) {
        Log("[ffx-hooks] MusicHook install blocked by FFXHOOKS_VALIDATE_ONLY=1\n");
    } else if (!selectedTargetOk) {
        Log("[ffx-hooks] MusicHook install skipped: selected target validation failed (%s)\n",
            FfxHooks::GetMusicHookTargetName(musicTarget));
    } else {
        // 2026-08-02 (Jarvis-HOOK): the heartbeat gate no longer blocks MusicHook - the override via
        // FFXHooksBlock + the battle-entry hooks (Prep/PlayTrackWithPreload/SwitchCrossfade) work
        // WITHOUT the probe; only the extra SOUNDCMD (trigger lab) degrades. The probe is still waited for 10s.
        const bool probeAlive = EnvFlagEnabled("FFXHOOKS_SKIP_PROBE_WAIT") || WaitForProbeHeartbeat(10000);
        if (!probeAlive) {
            Log("[ffx-hooks] WARN ffx-probe heartbeat not ready - MusicHook installed WITHOUT soundcmd (override/battle-entry via hook OK)\n");
        }
        // Shared memory (CreateBlock) is only created here, when a hook that needs it is actually active.
        // Do NOT move this to the unconditional path above â€” doing so caused heap corruption (0xc0000374)
        // by shifting the game's address space layout.
        if (!g_block) {
            if (!CreateBlock()) {
                Log("[ffx-hooks] WARN failed to create shared memory '%s' (err=%u)\n",
                    FFXHOOKS_MMF_NAME, GetLastError());
            } else {
                Log("[ffx-hooks] shared memory '%s' ready (%u bytes)\n",
                    FFXHOOKS_MMF_NAME, (unsigned)sizeof(FFXHooksBlock));
            }
        }
        FfxHooks::SetMusicHookMinFadeFrames(ArenaPlus_MusicFadeFrames());
        const bool arenaDualMusic = ArenaPlusMusicFlagEnabledRaw();
        if (arenaDualMusic) {
            const bool arenaTargetsOk =
                validation.playTrackWithPreloadOk &&
                validation.switchCrossfadeOk;
            if (!arenaTargetsOk) {
                Log("[ffx-hooks] MusicHook Arena+ install skipped: battle-entry target validation failed (prep=%d preload=%d switch=%d)\n",
                    validation.prepBattleTrackOk ? 1 : 0,
                    validation.playTrackWithPreloadOk ? 1 : 0,
                    validation.switchCrossfadeOk ? 1 : 0);
                Log("[ffx-hooks] installing MusicHook dual PlayTrack+SwitchCrossfade fallback for Arena+ OST\n");
                const FfxHooks::MusicHookInstallResult fallback =
                    FfxHooks::InstallMusicHookDual(g_base, g_block, LogLine);
                g_musicHookArmed = fallback.ok;
                if (fallback.ok && probeAlive) {
                    FfxHooks::SetArenaBattleMusicSoundCmdFn(ArenaPlus_MusicHookProbeSoundCmd);
                }
                Log("[ffx-hooks] MusicHook dual fallback install result ok=%d trampoline=0x%llX fadeFrames=%d\n",
                    fallback.ok ? 1 : 0,
                    static_cast<unsigned long long>(fallback.trampoline),
                    ArenaPlus_MusicFadeFrames());
            } else {
                Log("[ffx-hooks] installing MusicHook Arena+ v5 Prep+PlayTrackWithPreload+SwitchCrossfade (FSM case-8 path; prep=%d)\n",
                    validation.prepBattleTrackOk ? 1 : 0);
                const FfxHooks::MusicHookInstallResult result =
                    FfxHooks::InstallMusicHookArenaBattle(g_base, g_block, LogLine);
                g_musicHookArmed = result.ok;
                if (result.ok && probeAlive) {
                    FfxHooks::SetArenaBattleMusicSoundCmdFn(ArenaPlus_MusicHookProbeSoundCmd);
                }
                Log("[ffx-hooks] MusicHook Arena battle install result ok=%d trampoline=0x%llX fadeFrames=%d\n",
                    result.ok ? 1 : 0,
                    static_cast<unsigned long long>(result.trampoline),
                    ArenaPlus_MusicFadeFrames());
            }
        } else {
            const uintptr_t installRva =
                (musicTarget == FfxHooks::MusicHookTarget::SwitchCrossfade)
                    ? RVA_FMOD_SWITCH_CROSSFADE
                    : RVA_FMOD_PLAY_TRACK;
            Log("[ffx-hooks] installing MusicHook at VA 0x%08X (RVA 0x%08X)\n",
                static_cast<unsigned>(rva(installRva)),
                static_cast<unsigned>(installRva));
            const FfxHooks::MusicHookInstallResult result =
                FfxHooks::InstallMusicHook(g_base, g_block, musicTarget, LogLine);
            g_musicHookArmed = result.ok;
            if (result.ok && ArenaPlusMusicFlagEnabledRaw() && probeAlive) {
                FfxHooks::SetArenaBattleMusicSoundCmdFn(ArenaPlus_MusicHookProbeSoundCmd);
            }
            Log("[ffx-hooks] MusicHook install result ok=%d trampoline=0x%llX\n",
                result.ok ? 1 : 0, static_cast<unsigned long long>(result.trampoline));
        }
    }
    LogF8CatalogGate("labs.nova_super_damage", "Lab startup");
    LogF8CatalogGate("labs.kimahri_ronso_mana", "Lab startup");
    LogF8CatalogGate("labs.grid_teach", "Lab startup");
    LogF8CatalogGate("labs.kimahri_lancet_dual_grant", "Lab startup");
    LogF8CatalogGate("labs.item_stack_cap", "Lab startup");
    LogF8CatalogGate("labs.double_triple_drop", "Lab startup");
    const bool enableNovaBypass = NovaSuperDamageFlagEnabled();
    // The early worker owns Nova/pool publication before any automatic save read.
    StartNovaPoolEarlyIfRequested();
    const bool enableNulWard = NulWardFlagEnabled() || NulWardApplyEnabled();
    const bool enableNulWardApply = NulWardApplyEnabled();
    const bool enableNulWardLog = NulWardLogFlagEnabled();
    if (enableNulWard) {
        if (validateOnly) {
            Log("[ffx-hooks] NulWard install blocked by FFXHOOKS_VALIDATE_ONLY=1\n");
        } else {
            if (enableNulWardApply && enableNovaBypass) {
                Log("[ffx-hooks] NulWard WARN: NovaClamp bypass active â€” writeback bytes may conflict; disable nova_super_damage.flag for full apply\n");
            }
            FfxHooks::NulWardInstallOptions nulOpts = {};
            nulOpts.nativeSlots = NulWardNativeSlotsEnabled();
            nulOpts.experimentP16 = NulWardP16Enabled() || NulWardP16ApplyEnabled();
            nulOpts.p16Apply = NulWardP16ApplyEnabled();
            const FfxHooks::NulWardInstallResult nulWardResult =
                FfxHooks::InstallNulWardHook(
                    g_base,
                    enableNulWardApply,
                    enableNulWardLog,
                    LogLine,
                    &nulOpts);
            Log("[ffx-hooks] NulWard install result ok=%d stub=0x%08X aftermath=0x%08X hitLoop=0x%08X precheck=0x%08X apply=%d log=%d native=%d p16=%d\n",
                nulWardResult.ok ? 1 : 0,
                static_cast<unsigned>(nulWardResult.stubWriteback),
                static_cast<unsigned>(nulWardResult.detourAftermath),
                static_cast<unsigned>(nulWardResult.detourHitLoop),
                static_cast<unsigned>(nulWardResult.detourPrecheck),
                enableNulWardApply ? 1 : 0,
                enableNulWardLog ? 1 : 0,
                nulOpts.nativeSlots ? 1 : 0,
                nulOpts.experimentP16 ? 1 : 0);
        }
    } else {
        Log("[ffx-hooks] NulWard not armed (nul_ward.flag / nul_ward_apply.flag)\n");
    }
    if (GridTeachEnabled()) {
        if (validateOnly) {
            Log("[ffx-hooks] GridTeach install blocked by FFXHOOKS_VALIDATE_ONLY=1\n");
        } else {
            const FfxHooks::GridTeachInstallResult gridResult =
                FfxHooks::InstallGridTeachHook(g_base, LogLine);
            Log("[ffx-hooks] GridTeach v4.5 install ok=%d menuPatch=0x%08X patched=%d (extMenu Kimahri #322 / Yuna #366 post-BuildMenu)\n",
                gridResult.ok ? 1 : 0,
                static_cast<unsigned>(gridResult.menuBoundPatchVa),
                gridResult.menuBoundPatched ? 1 : 0);
        }
    } else if (NulWardTeachEnabled()) {
        if (validateOnly) {
            Log("[ffx-hooks] NulWardTeach install blocked by FFXHOOKS_VALIDATE_ONLY=1\n");
        } else {
            const FfxHooks::NulWardTeachInstallResult teachResult =
                FfxHooks::InstallNulWardTeachHook(g_base, NulWardTeachGrantEnabled(), LogLine);
            Log("[ffx-hooks] NulWardTeach install ok=%d menuPatch=0x%08X patched=%d grant=%d (legacy â€” prefer grid_teach.flag)\n",
                teachResult.ok ? 1 : 0,
                static_cast<unsigned>(teachResult.menuBoundPatchVa),
                teachResult.menuBoundPatched ? 1 : 0,
                NulWardTeachGrantEnabled() ? 1 : 0);
        }
    }
    if (KimahriLancetDualGrantEnabled()) {
        if (validateOnly) {
            Log("[ffx-hooks] KimahriLancetDualGrant install blocked by FFXHOOKS_VALIDATE_ONLY=1\n");
        } else if (!GridTeachEnabled()) {
            Log("[ffx-hooks] KimahriLancetDualGrant WARN armed but grid_teach.flag off â€” dual grant needs GridTeach grant shim\n");
        } else {
            const FfxHooks::KimahriLancetDualGrantInstallResult dualResult =
                FfxHooks::InstallKimahriLancetDualGrantHook(g_base, true, nullptr, LogLine);
            Log("[ffx-hooks] KimahriLancetDualGrant install ok=%d armed=%d (Lancet rage 104-115 -> Blue 323-334 + menu 322)\n",
                dualResult.ok ? 1 : 0,
                dualResult.armed ? 1 : 0);
        }
    } else {
        Log("[ffx-hooks] KimahriLancetDualGrant not armed (kimahri_lancet_dual_grant.flag)\n");
    }
    if (ItemStackCapFlagEnabled()) {
        if (validateOnly) {
            Log("[ffx-hooks] ItemStackCap install blocked by FFXHOOKS_VALIDATE_ONLY=1\n");
        } else {
            int capRequested = g_itemStackCapStartupValue;
            if (capRequested < 1) capRequested = 1;
            if (capRequested > 255) capRequested = 255;
            const uint8_t cap = static_cast<uint8_t>(capRequested);
            const FfxHooks::ItemStackCapInstallResult capResult =
                FfxHooks::InstallItemStackCapHook(
                    g_base,
                    cap,
                    ItemStackCapLogFlagEnabled(),
                    LogLine);
            FfxHooks::PublishF8RuntimeScalarStatus("labs.item_stack_cap",
                capResult.ok ? FfxHooks::F8RuntimeAvailability::Available
                             : FfxHooks::F8RuntimeAvailability::ProducerUnavailable,
                true, capResult.ok, capResult.ok, static_cast<int>(cap));
            Log("[ffx-hooks] ItemStackCap install result ok=%d cap=%u stub_new=0x%08X stub_exist=0x%08X\n",
                capResult.ok ? 1 : 0,
                static_cast<unsigned>(cap),
                static_cast<unsigned>(capResult.stubNew),
                static_cast<unsigned>(capResult.stubExist));
        }
    } else {
        Log("[ffx-hooks] ItemStackCap not armed (item_stack_cap_255.flag)\n");
    }
    if (DoubleTripleDropEnabled()) {
        if (validateOnly) {
            Log("[ffx-hooks] DoubleTripleDrop install blocked by FFXHOOKS_VALIDATE_ONLY=1\n");
        } else {
            const FfxHooks::DoubleTripleDropInstallResult dropResult =
                FfxHooks::InstallDoubleTripleDropHook(
                    g_base,
                    true,
                    DoubleTripleDropLogEnabled(),
                    LogLine);
            Log("[ffx-hooks] DoubleTripleDrop install ok=%d reason=%u hits=%ld\n",
                dropResult.ok ? 1 : 0,
                static_cast<unsigned>(dropResult.reasonCode),
                FfxHooks::DoubleTripleDropHookHitCount());
        }
    } else {
        Log("[ffx-hooks] DoubleTripleDrop not armed (FFXHOOKS_ENABLE_DOUBLE_TRIPLE_DROP)\n");
    }
    if (ElementScanDarkEnabled()) {
        FfxHooks::InstallElementHook(g_base, LogLine);
    }
    const bool enableAbilitySfxLog = AbilitySfxLogFlagEnabled();
    if (enableAbilitySfxLog) {
        if (validateOnly) {
            Log("[ffx-hooks] AbilitySfx install blocked by FFXHOOKS_VALIDATE_ONLY=1\n");
        } else {
            const FfxHooks::AbilitySfxInstallResult sfxResult =
                FfxHooks::InstallAbilitySfxHook(g_base, true, LogLine);
            Log("[ffx-hooks] AbilitySfx install result ok=%d play=0x%08X handoff=0x%08X\n",
                sfxResult.ok ? 1 : 0,
                static_cast<unsigned>(sfxResult.playBattleStreamingTrampoline),
                static_cast<unsigned>(sfxResult.handoffTrampoline));
        }
    } else {
        Log("[ffx-hooks] AbilitySfx not armed (ability_sfx.flag)\n");
    }
    InstallArenaResolverLogFromStartupPlan(sharedResolverStartupPlan, validateOnly);
    {
        const bool enableFieldScout = FieldScoutFlagEnabled() || FieldScoutMapOnlyFlagEnabled();
        if (enableFieldScout) {
            if (validateOnly) {
                Log("[ffx-hooks] FieldScout install blocked by FFXHOOKS_VALIDATE_ONLY=1\n");
            } else if (!minHookReady) {
                Log("[ffx-hooks] FieldScout install blocked; shared MinHook is not ready\n");
            } else {
                const bool mapOnly = FieldScoutMapOnlyFlagEnabled() && !FieldScoutFlagEnabled();
                const bool heavy = FieldScoutHeavyFlagEnabled();
                const FfxHooks::FieldScoutUltraOptions ultra = FieldScoutBuildUltraOptions();
                const bool maxMode = FieldScoutMaxFlagEnabled() && heavy && ultra.master;
                const FfxHooks::FieldScoutInstallResult scoutResult =
                    FfxHooks::InstallFieldScoutHook(g_base, g_module, mapOnly, heavy, ultra, maxMode, LogLine);
                Log("[ffx-hooks] FieldScout install ok=%d mapOnly=%d heavy=%d ultra=%d max=%d session=%s "
                    "ultra(fl=%d col=%d enc=%d env=%d pipe=%d)\n",
                    scoutResult.ok ? 1 : 0,
                    mapOnly ? 1 : 0,
                    heavy ? 1 : 0,
                    ultra.master ? 1 : 0,
                    maxMode ? 1 : 0,
                    scoutResult.sessionPath[0] ? scoutResult.sessionPath : "(none)",
                    ultra.fieldLogic ? 1 : 0,
                    ultra.collision ? 1 : 0,
                    ultra.encounters ? 1 : 0,
                    ultra.sceneEnv ? 1 : 0,
                    ultra.pipelineHints ? 1 : 0);
                // InstallFieldScoutHook owns and applies one complete process-global MinHook batch
                // for every mode. No pending queue is allowed to escape into another subsystem.
            }
        } else {
            Log("[ffx-hooks] FieldScout not armed (field_scout.flag)\n");
        }
    }
    {
        const bool enableFieldProbe =
            FieldProbeRt2FlagEnabled() ||
            FieldProbeEncounterOnlyFlagEnabled() ||
            FieldProbeTextureOnlyFlagEnabled();
        const bool logEncounter =
            FieldProbeRt2FlagEnabled() || FieldProbeEncounterOnlyFlagEnabled();
        const bool logTexture =
            (FieldProbeRt2FlagEnabled() || FieldProbeTextureOnlyFlagEnabled()) &&
            !FieldScoutFlagEnabled() && !FieldScoutMapOnlyFlagEnabled();
        if (enableFieldProbe) {
            if (validateOnly) {
                Log("[ffx-hooks] FieldProbe install blocked by FFXHOOKS_VALIDATE_ONLY=1\n");
            } else {
                const FfxHooks::FieldProbeInstallResult probeResult =
                    FfxHooks::InstallFieldProbeHook(g_base, logEncounter, logTexture, LogLine);
                Log("[ffx-hooks] FieldProbe install ok=%d hooks=%u encounter=%d texture=%d\n",
                    probeResult.ok ? 1 : 0,
                    probeResult.hookedCount,
                    logEncounter ? 1 : 0,
                    logTexture ? 1 : 0);
            }
        } else {
            Log("[ffx-hooks] FieldProbe not armed (field_probe_rt2.flag)\n");
        }
    }
    ArenaPlus_LoadCatalogOverlay();
    ArenaPlus_LoadCustomTokenRedirects();

    if (ArenaPlus_VictoryHookEnabled()) {
        if (validateOnly) {
            Log("[ffx-hooks] BattleEnd install blocked by FFXHOOKS_VALIDATE_ONLY=1\n");
        } else {
            const FfxHooks::BattleEndInstallResult endResult =
                FfxHooks::InstallBattleEndHook(g_base, LogLine);
            Log("[ffx-hooks] BattleEnd install ok=%d reason=%u\n",
                endResult.ok ? 1 : 0,
                static_cast<unsigned>(endResult.reasonCode));
            if (endResult.ok) {
                FfxHooks::SetBattleEndCallback(&ArenaPlus_OnBattleEnd);
                Log("[ffx-hooks] BattleEnd callback wired -> ArenaPlus_OnBattleEnd (sidecar bridge: scaffold-only)\n");
            }
        }
    } else {
        Log("[ffx-hooks] BattleEnd not armed (arena_plus_victory_hook.flag)\n");
    }
    Log("[ffx-hooks] PhaseTurnEdge disabled in this build\n");
    {
        const FfxHooks::SinCurseInstallResult sinCurseResult =
            FfxHooks::InstallSinCurseHook(g_base, (void*)LogLine);
        if (sinCurseResult.ok) {
            Log("[ffx-hooks] SinCurseHook installed (hooked=%u)\n", sinCurseResult.hookedCount);
        } else {
            Log("[ffx-hooks] SinCurseHook unavailable: %s\n",
                sinCurseResult.reason ? sinCurseResult.reason : "legacy writer quarantined");
        }
    }
    {
        struct ArenaProgressLogShim {
            static void Emit(const char* msg) { Log("%s", msg ? msg : ""); }
        };
        FfxHooks::ArenaProgress_Initialize(&ArenaProgressLogShim::Emit);
        if (FfxHooks::ArenaProgress_Enabled()) {
            for (int dark = 0; dark < ARENA_DARK_FLAG_LEN; ++dark) {
                const ArenaPlusBossRoute& r = ArenaPlus_GetRoute(dark);
                const char* flag = ArenaPlus_ProgressFlagForSlot(dark);
                ArenaPlusTierLockState state = ArenaPlus_GetTierLockState(dark);
                Log("[ffx-hooks] ArenaPlus tier-lock slot=%d battleId=%s flag=%s state=%s\n",
                    dark,
                    r.battleId ? r.battleId : "?",
                    flag ? flag : "?",
                    ArenaPlus_TierLockStateLabel(state));
            }
        }
    }
    StartLabMenuIfEnabled();
    bool f8PresentProducerArmed = StartAuroraOverlayIfEnabled();
    if (enableFpsScout) {
        if (FpsScoutStart()) {
            if (InstallAuroraD3D11Overlay()) {
                StartAuroraD3DLatePresentFallback();
            } else {
                Log("[ffx-hooks] WARN FPS Scout could not install Present hook\n");
            }
        }
    }
    if (f8RuntimeAdapterReady) {
        if (InstallAuroraD3D11Overlay()) {
            f8PresentProducerArmed = true;
            InterlockedExchange(&g_auroraD3DRenderEnabled, 1);
            if (!AuroraD3DPresentReady()) {
                StartAuroraD3DLatePresentFallback();
            }
        } else {
            TryPublishAuroraD3DPresentTerminal();
        }
    }
    StartArenaTraceIfEnabled();
    // The native glyph-outline guard bounds custom-menu text batch usage at RVA 0x4FAE40.
    const bool nativeMenuRequested = NativeMenuArmedFromConfig();
    const bool nativeTextOutlineGuardReady =
        nativeMenuRequested && StartNativeTextOutlineGuard();
    bool f8NativeMenuProducerReady = false;
    if (!nativeMenuRequested) {
        Log("[ffx-hooks] NativeMenu: disabled (no runtime gate requested)\n");
    } else if (!nativeTextOutlineGuardReady) {
        Log("[ffx-hooks] WARN NativeMenu disabled: native text-outline guard unavailable\n");
    } else {
        f8NativeMenuProducerReady = StartNativeMenuIfEnabled();
    }
    const bool dashboardEnabled = FfxHooks::Config::GetBool("dashboard.enabled", false);
    const bool f8PresentProducerOperational = f8PresentProducerArmed &&
        InterlockedCompareExchange(&g_auroraD3DPresentTerminal, 0, 0) == 0;
    const bool menuProducerArmed =
        f8PresentProducerOperational && f8NativeMenuProducerReady;
    const bool maechenEnabled = FfxHooks::Config::GetBool("maechen.enabled", false);
    InterlockedExchange(&g_maechenConfigEnabledPublished, maechenEnabled ? 1 : 0);
    InterlockedExchange(&g_maechenNativePumpReadyPublished,
                        f8NativeMenuProducerReady ? 1 : 0);
    TryInstallMaechenWhenReady();
    if (!maechenEnabled) {
        Log("[ffx-hooks] Maechen disabled (maechen.enabled=0)\n");
    } else if (!f8NativeMenuProducerReady) {
        Log("[ffx-hooks] WARN Maechen native pump unavailable; install failed closed\n");
    } else if (!AuroraD3DPresentReady()) {
        Log("[ffx-hooks] Maechen waiting for operational Present producer\n");
    }
    bool dashboardReady = false;
    if(!dashboardEnabled&&FfxHooks::EquipmentWorkshop::Requested())FfxHooks::NativePorts::Start(g_base,LogLine,static_cast<unsigned>(g_nativeMenuHotkey));
    if (dashboardEnabled && menuProducerArmed) {
        dashboardReady = FfxHooks::Dash_Install(LogLine);
        if (dashboardReady && FfxHooks::NativePorts::Start(g_base,LogLine,static_cast<unsigned>(g_nativeMenuHotkey))) {
            FfxHooks::Dash_SetShortcutReader(&F8ConfiguredShortcut);
            FfxHooks::SpeedHackSetShortcutReader(&F8ConfiguredSpeedShortcut);
        }
        if (dashboardReady) {
            Log("[ffx-hooks] F8 dashboard armed (dashboard.enabled=1; open with F8)\n");
            StartupTiming("f8-ready");
        } else {
            Log("[ffx-hooks] WARN F8 dashboard edge adapter install failed\n");
        }
    } else if (dashboardEnabled) {
        Log("[ffx-hooks] WARN F8 dashboard producer unavailable present=%d native_menu=%d; "
            "Field Scout requires explicit native_menu.flag\n",
            f8PresentProducerOperational ? 1 : 0,
            f8NativeMenuProducerReady ? 1 : 0);
    } else {
        Log("[ffx-hooks] F8 dashboard disabled (dashboard.enabled=0)\n");
    }

    PublishResolvedF8Status(
        "arena_plus.compose_f7", FfxHooks::F8RuntimeAvailability::ProducerUnavailable, false);

    // Dialog Skip owns the voice entry independently of the dashboard. Speed 8x composes with
    // that one owner; 2x/4x remain available even when the corrected voice target fails closed.
    FfxHooks::DialogSkipInstallStatus dialogSkipStatus =
        FfxHooks::DialogSkipInstallStatus::PolyHookUnavailable;
    const bool dialogSkipReady =
        FfxHooks::InstallDialogSkipHook(g_base, LogLine, &dialogSkipStatus);
    FfxHooks::F8RuntimeAvailability dialogSkipAvailability =
        FfxHooks::F8RuntimeAvailability::ProducerUnavailable;
    switch (dialogSkipStatus) {
    case FfxHooks::DialogSkipInstallStatus::Installed:
    case FfxHooks::DialogSkipInstallStatus::AlreadyInstalled:
        dialogSkipAvailability = FfxHooks::F8RuntimeAvailability::Available;
        break;
    case FfxHooks::DialogSkipInstallStatus::UnsupportedProfile:
    case FfxHooks::DialogSkipInstallStatus::TargetOutOfRange:
        dialogSkipAvailability = FfxHooks::F8RuntimeAvailability::UnsupportedBuild;
        break;
    case FfxHooks::DialogSkipInstallStatus::SignatureMismatch:
    case FfxHooks::DialogSkipInstallStatus::DetourLikePrefix:
        dialogSkipAvailability = FfxHooks::F8RuntimeAvailability::SignatureMismatch;
        break;
    case FfxHooks::DialogSkipInstallStatus::DetourFailed:
    case FfxHooks::DialogSkipInstallStatus::PolyHookUnavailable:
        dialogSkipAvailability = FfxHooks::F8RuntimeAvailability::ProducerUnavailable;
        break;
    }
    FfxHooks::SpeedHackInstallStatus speedHackStatus =
        FfxHooks::SpeedHackInstallStatus::PolyHookUnavailable;
    const bool speedHackReady =
        FfxHooks::InstallSpeedHackHook(g_base, dialogSkipReady, LogLine, &speedHackStatus);
    if(speedHackReady && FfxHooks::FmvSpeed::Ready())FfxHooks::SpeedHackSetMovieBridge(&g_f8MovieBridge);
    FfxHooks::F8RuntimeAvailability speedHackAvailability =
        FfxHooks::F8RuntimeAvailability::ProducerUnavailable;
    switch (speedHackStatus) {
    case FfxHooks::SpeedHackInstallStatus::Installed:
    case FfxHooks::SpeedHackInstallStatus::AlreadyInstalled:
        speedHackAvailability = FfxHooks::F8RuntimeAvailability::Available;
        break;
    case FfxHooks::SpeedHackInstallStatus::UnsupportedProfile:
    case FfxHooks::SpeedHackInstallStatus::NativeStateOutOfRange:
    case FfxHooks::SpeedHackInstallStatus::NativeAvailabilityOutOfRange:
    case FfxHooks::SpeedHackInstallStatus::GlobalTargetOutOfRange:
        speedHackAvailability = FfxHooks::F8RuntimeAvailability::UnsupportedBuild;
        break;
    case FfxHooks::SpeedHackInstallStatus::GlobalSignatureMismatch:
    case FfxHooks::SpeedHackInstallStatus::GlobalDetourLikePrefix:
        speedHackAvailability = FfxHooks::F8RuntimeAvailability::SignatureMismatch;
        break;
    case FfxHooks::SpeedHackInstallStatus::UnXModuleConflict:
        speedHackAvailability = FfxHooks::F8RuntimeAvailability::Conflict;
        break;
    case FfxHooks::SpeedHackInstallStatus::GlobalDetourFailed:
    case FfxHooks::SpeedHackInstallStatus::PolyHookUnavailable:
        speedHackAvailability = FfxHooks::F8RuntimeAvailability::ProducerUnavailable;
        break;
    }
    const bool dialogSkipOperational = dialogSkipReady && f8PresentProducerOperational;
    const bool speedHackOperational = speedHackReady && f8PresentProducerOperational;
    if (dialogSkipReady && !f8PresentProducerOperational) {
        dialogSkipAvailability = FfxHooks::F8RuntimeAvailability::ProducerUnavailable;
    }
    if (speedHackReady && !f8PresentProducerOperational) {
        speedHackAvailability = FfxHooks::F8RuntimeAvailability::ProducerUnavailable;
    }
    PublishResolvedF8Status(
        "input.dialog_skip",
        dialogSkipAvailability,
        dialogSkipOperational);
    PublishResolvedF8Status(
        "boosters.speed_hack",
        speedHackAvailability,
        speedHackOperational);
    /* Terminal publication is sticky but may race this worker's earlier producer snapshot. The
     * terminal callback corrects states that publish before it; this post-publication readback
     * corrects the inverse ordering and re-closes hooks that installed after an early terminal. */
    if (InterlockedCompareExchange(&g_auroraD3DPresentTerminal, 0, 0) != 0) {
        FfxHooks::RequestSpeedHackStop();
        FfxHooks::RequestDialogSkipStop();
        PublishResolvedF8Status(
            "input.dialog_skip",
            FfxHooks::F8RuntimeAvailability::ProducerUnavailable,
            false);
        PublishResolvedF8Status(
            "boosters.speed_hack",
            FfxHooks::F8RuntimeAvailability::ProducerUnavailable,
            false);
    }
#else
    if (f8RuntimeAdapterReady) {
        FfxHooks::NotifyUnXBoosterPresentProducer(false, true);
    }
    PublishResolvedF8Status(
        "arena_plus.compose_f7", FfxHooks::F8RuntimeAvailability::ProducerUnavailable, false);
    PublishResolvedF8Status(
        "input.dialog_skip", FfxHooks::F8RuntimeAvailability::ProducerUnavailable, false);
    PublishResolvedF8Status(
        "boosters.speed_hack", FfxHooks::F8RuntimeAvailability::ProducerUnavailable, false);
    Log("[ffx-hooks] non-PolyHook compatibility build loaded - no active detours\n");
#endif
    if (minHookReady) {
        if (FfxHooks::F7Difficulty::ShouldInstallAtStartup(minHookReady, validateOnly) &&
            sharedBattleRuntimeRequested) {
            const bool sharedBattleInstalled = FfxHooks::F7_InstallHooks(
                g_base, g_block, LogLine, sharedBattleRuntimeRequested,
                F8CatalogGateEnabled("arena_plus.master"));
            const auto sinConfig=FfxHooks::F7_GetConfigSnapshot();
            if(sharedBattleInstalled && sinConfig.sinRamValid && sinConfig.sinRam.enabled){
                wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
                if(auto* slash=wcsrchr(path,L'\\')){*slash=0;wcscat_s(path,L"\\modules\\config\\_sin-ai-v1.bin");
                    FfxHooks::SinAi::Start(g_base,path,&FfxHooks::F7_SinAiContext,&FfxHooks::F7_SinAiRegistered,&FfxHooks::F7_SinObserveNaturalEncounter);}
                Log("[ffx-hooks] S.I.N. AI startup: %s\n",FfxHooks::SinAi::Detail());
            }
            ArenaPlus_PublishMixAvailability();
            FfxHooks::SeymourBattleInstallStatus seymourStatus =
                FfxHooks::SeymourBattleInstallStatus::SharedRuntimeUnavailable;
            const bool seymourInstalled = sharedBattleInstalled &&
                FfxHooks::StartSeymourBattleHook(g_base, LogLine, &seymourStatus);
            Log("[ffx-hooks] shared battle install owner=%d seymour=%d status=%s\n",
                sharedBattleInstalled ? 1 : 0, seymourInstalled ? 1 : 0,
                FfxHooks::SeymourBattleInstallStatusName(seymourStatus));
            if (!sharedBattleInstalled) {
                PublishResolvedF8Status(
                    "boosters.playable_seymour",
                    FfxHooks::F8RuntimeAvailability::ProducerUnavailable,
                    false);
            }
        } else if (sharedBattleRuntimeRequested && validateOnly) {
            // WHY: validation-only is an evidence pass, never authorization to create/enable
            // shared battle hooks, open callback admission, or expose either RAM behavior.
            Log("[ffx-hooks] validation-only: shared battle runtime and Seymour skipped (no hooks, admission, or RAM writes)\n");
            FfxHooks::CustomMixUltra::Runtime::StartProduction(
                g_base, false, true);
            PublishResolvedF8Status(
                "boosters.playable_seymour",
                FfxHooks::F8RuntimeAvailability::ProducerUnavailable,
                false);
        }
        // f7.aiswap now requests evidence only. The adapter validates both lifecycle targets and
        // installs its paired no-write observer transaction only on the exact supported profile.
        // Its own validation-only branch returns before creating a hook.
        FfxHooks::F7AiSwap_Install(g_base, LogLine);
    } else {
        // MinHook setup already failed process-wide. Publish observer truth without retrying the
        // shared initializer or entering any feature-local hook path.
        FfxHooks::F7AiSwap_ReportSetupFailure(LogLine);
        PublishResolvedF8Status(
            "boosters.playable_seymour",
            FfxHooks::F8RuntimeAvailability::ProducerUnavailable,
            false);
    }
    ArenaPlus_RestorePendingComposeOnBoot(validateOnly);

    /* Phase 2 placeholder: FfxHooks::InstallElementHook(g_base, g_block); */
    Log("[ffx-hooks] InstallHooks leave\n");
    StartupTiming("install-complete");
}

/* Full teardown is reserved for a future explicit normal-context owner.
 * Process termination discards process-owned state. Dynamic FreeLibrary is unsupported until that owner first stops
 * the Present producer, drains admitted frames, and restores every owned byte outside DllMain. */
static void RemoveHooks() {
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if(!EquipmentMenu::StopReady()){
        Log("[ffx-hooks] Workshop UI close queued on owner thread; hook teardown deferred\n");
        return;
    }
#endif
    FfxHooks::EquipmentWorkshop::RequestStop();
    FfxHooks::Fastload::RemoveFastloadHook();
    Log("[ffx-hooks] RemoveHooks enter\n");
#ifdef FFXHOOKS_HAVE_POLYHOOK
    FpsScoutStop();
    StopAuroraOverlay();
    StopLabMenu();
    StopArenaTrace();
    StopNativeMenu();             // step 5.1 — closes menu + restores (Exit) + removes detour
    const bool seymourRetired = FfxHooks::RemoveSeymourBattleHook();
    if (seymourRetired) {
        // WHY: Seymour composes into F7's process-lifetime InitScene detour. F7 may retire its
        // shared batch only after Seymour proves both its unique exit target and composer slot
        // inert; a Busy/Poisoned retry must preserve every still-reachable trampoline.
        FfxHooks::F7_RemoveHooks();   // F7 In-Live: remove detours + clears music override
    } else {
        Log("[ffx-hooks] Seymour teardown deferred; shared F7 battle runtime retained\n");
    }
    FfxHooks::F7AiSwap_Remove();  // disable observer; applied trampolines stay process-lifetime
    FfxHooks::RemoveSpeedHackHook();  // F8 speed: neutralize both timing backends first.
    FfxHooks::RemoveDialogSkipHook(); // Shared manual/8x voice owner follows Speed teardown.
    FfxHooks::Dash_Uninstall();       // F8 dashboard edge adapter: close and clear state.

#endif
    FfxHooks::RemoveMusicHook(LogLine);
    FfxHooks::RemoveNovaSuperDamageHook(LogLine);
    FfxHooks::RemoveNulWardHook(LogLine);
    FfxHooks::RemoveGridTeachHook(LogLine);
    FfxHooks::RemoveKimahriLancetDualGrantHook(LogLine);
    FfxHooks::RemoveNulWardTeachHook(LogLine);
    FfxHooks::RemoveElementHook();
    FfxHooks::RemoveAbilitySfxHook(LogLine);
    FfxHooks::RemoveFieldScoutHook(LogLine);
    FfxHooks::RemoveFieldProbeHook(LogLine);
    FfxHooks::RemoveResolverLogHook();
    FfxHooks::RemoveItemStackCapHook(LogLine);
    FfxHooks::RemoveDoubleTripleDropHook(LogLine);
    /* Phase 2 placeholder: FfxHooks::RemoveElementHook(); */
    DestroyBlock();
    Log("[ffx-hooks] RemoveHooks leave\n");
}

/* â”€â”€ DllMain â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
static void StartNovaPoolEarlyIfRequested() {
    static LONG attempted = 0;
    const bool enableNovaBypass = NovaSuperDamageFlagEnabled();
    const bool enableNovaLog = NovaSuperDamageLogFlagEnabled();
    const bool enableRonsoMana = RonsoManaFlagEnabled();
    const bool compatibility = FfxHooks::RonsoPool::HasPersistentOwnership();
    if (!enableNovaBypass && !enableNovaLog && !enableRonsoMana && !compatibility && !FfxHooks::NativeSaveEvents::Requested()) return;
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA("FFX.exe"));
    if (!base || InterlockedCompareExchange(&attempted, 1, 0) != 0) return;
    LogF8CatalogGate("labs.nova_super_damage", "Nova/pool early startup");
    LogF8CatalogGate("labs.kimahri_ronso_mana", "Nova/pool early startup");
    if (EnvFlagEnabled("FFXHOOKS_VALIDATE_ONLY")) {
        Log("[ffx-hooks] Nova/pool startup blocked by FFXHOOKS_VALIDATE_ONLY=1\n");
        return;
    }
    const auto installed = FfxHooks::InstallNovaSuperDamageHook(
        base, enableNovaBypass, enableNovaLog, enableRonsoMana, LogLine);
    Log("[ffx-hooks] Nova/pool early startup ok=%d bypass=%d log=%d ronso=%d compatibility=%d\n",
        installed.ok ? 1 : 0, enableNovaBypass ? 1 : 0, enableNovaLog ? 1 : 0,
        enableRonsoMana ? 1 : 0, compatibility ? 1 : 0);
}

static void StartFastloadEarlyIfRequested() {
    LogF8CatalogGate("development.fastload_autosave", "Fastload startup");
    FfxHooks::Fastload::InstallOptions options{};
    options.gateEnabled = F8CatalogGateEnabled("development.fastload_autosave");
    if (!options.gateEnabled) return;
    options.validateOnly = EnvFlagEnabled("FFXHOOKS_VALIDATE_ONLY");
    options.startupShiftHeld = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0 ||
        (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
    options.observeOnly = EnvFlagEnabled("FFXHOOKS_FASTLOAD_OBSERVE_ONLY");
    const uintptr_t fastloadBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("FFX.exe"));
    const auto installed = FfxHooks::Fastload::InstallFastloadHook(fastloadBase, LogLine, options);
    const auto runtime = FfxHooks::Fastload::GetRuntimeSnapshot();
    Log("[ffx-hooks] Fastload startup code=%u failure=%s validateOnly=%d shift=%d observe=%d sceneReady=%d openingReady=%d attempts=%u\n",
        static_cast<unsigned>(installed.code), FfxHooks::Fastload::FailureName(installed.failure),
        options.validateOnly ? 1 : 0, options.startupShiftHeld ? 1 : 0, runtime.observeOnly ? 1 : 0,
        runtime.sceneTickReady ? 1 : 0, runtime.openingSkipReady ? 1 : 0, runtime.actionGeneration);
    FfxHooks::Fastload::FlushFastloadTelemetry();
}

// The worker owns file-backed logging, delay, config I/O, and hook installation outside DllMain.
static DWORD WINAPI HooksWorkerThread(LPVOID) {
    OpenLog();
    g_startupBegin=GetTickCount64();
    EarlyLogLine("[ffx-hooks] early worker thread start\r\n");
    int defaultDelayMs = 2000;
#ifdef FFXHOOKS_HAVE_POLYHOOK
    char configMode[32] = {};
    const bool configD3D =
        AuroraConfigExists() &&
        AuroraConfigInt("enabled", 0) != 0 &&
        AuroraConfigString("mode", configMode, sizeof(configMode)) &&
        (configMode[0] == 'd' || configMode[0] == 'D');
    if (ModuleFlagEnabled("aurora_overlay_d3d11.flag") ||
        ModuleFlagEnabled("config\\aurora_overlay_d3d11.flag") ||
        configD3D) {
        defaultDelayMs = 500;
    }
#endif
    // Load publishes the exact INI snapshot only. Consumers separately resolve environment,
    // INI authority, legacy INI, and flag precedence after this point.
    if (FfxHooks::Config::Load()) {
        Log("[ffx-hooks] Config loaded from %s\n", FfxHooks::Config::GetLoadedPath());
    } else {
        Log("[ffx-hooks] WARN Config::Load failed (built-in defaults)\n");
    }
    CaptureF8StartupGates();
    StartupTiming("config-ready");
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if(!EnvFlagEnabled("FFXHOOKS_VALIDATE_ONLY")){
        if(F8CatalogGateEnabled("boosters.speed_hack_fmv")){
            FfxHooks::FmvSpeed::Start(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)));
            Log("[ffx-hooks] FMV startup: %s\n",FfxHooks::FmvSpeed::Detail());
        }
        using namespace FfxHooks::NativeLanguage;
        const Settings languages{static_cast<Choice>(FfxHooks::Config::GetInt("language.voice",0)),
            static_cast<Choice>(FfxHooks::Config::GetInt("language.sfx",0)),static_cast<Choice>(FfxHooks::Config::GetInt("language.video",0))};
        if(languages.voice!=Choice::GameDefault || languages.sfx!=Choice::GameDefault || languages.video!=Choice::GameDefault){
            Start(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),languages);
            Log("[ffx-hooks] Audio languages startup: %s\n",StatusText());
        }
    }
#endif
    FfxHooks::EquipmentWorkshop::PrimeSaveIo(F8CatalogGateEnabled("labs.equipment_workshop"),EnvFlagEnabled("FFXHOOKS_VALIDATE_ONLY"));
    StartupTiming("early-audio-ready");
    StartNovaPoolEarlyIfRequested();
    StartupTiming("nova-save-io-ready");
    // Nova validates the original damage frame before Workshop owns its entry.
    // Both keep their independent damage behavior; the save imports have one owner.
    FfxHooks::EquipmentWorkshop::Start(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),
        F8CatalogGateEnabled("labs.equipment_workshop"),EnvFlagEnabled("FFXHOOKS_VALIDATE_ONLY"),LogLine);
    Log("[ffx-hooks] Workshop startup: %s\n",FfxHooks::EquipmentWorkshop::Detail());
    StartupTiming("workshop-ready");
    StartFastloadEarlyIfRequested();
    StartupTiming("fastload-ready");
    int delayMs = EnvInt("FFXHOOKS_INSTALL_DELAY_MS", defaultDelayMs);
    if (delayMs < 0) delayMs = 0;
    if (delayMs > 60000) delayMs = 60000;
    Log("[ffx-hooks] worker thread start; sleeping %dms before install path\n", delayMs);
    if (delayMs > 0) {
        Sleep(static_cast<DWORD>(delayMs));
    }
    EarlyLogLine("[ffx-hooks] early worker calling InstallHooks\r\n");
    Log("[ffx-hooks] worker thread woke; calling InstallHooks\n");
    InstallHooks();
    // The existing worker is the only telemetry consumer. No callback needs a logger,
    // renderer, file handle, or additional background thread. The observer owns deadlines.
    while (FfxHooks::Fastload::FastloadNeedsPump()) {
        FfxHooks::Fastload::FlushFastloadTelemetry();
        Sleep(25);
    }
    FfxHooks::Fastload::FlushFastloadTelemetry();
    Log("[ffx-hooks] worker thread leave\n");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_module = hMod;
            OutputDebugStringA("[ffx-hooks] DllMain DLL_PROCESS_ATTACH enter\n");
            /* Lab build: keep thread notifications; some injected loader stacks are touchy here. */
            // Under loader lock, defer hook installation and waits to the worker; never wait here.
            {
                DWORD tid = 0;
                HANDLE thread = CreateThread(nullptr, 0, HooksWorkerThread, nullptr, 0, &tid);
                if (thread) {
                    CloseHandle(thread);
                } else {
                    OutputDebugStringA("[ffx-hooks] DllMain worker thread create failed\n");
                }
            }
            break;

        // Close the Speed producer before its shared Dialog owner. Otherwise an already-running
        // Present frame could publish 8x during the tiny interval after Dialog admission closed.
        // Full teardown still needs a normal-context owner and callback drain.
        case DLL_PROCESS_DETACH:
            FfxHooks::EquipmentWorkshop::RequestStop();
            FfxHooks::NativePorts::RequestStop();
            FfxHooks::NativeLanguage::RequestStop();
            FfxHooks::SinAi::RequestStop();
            FfxHooks::FmvSpeed::RequestStop();
            FfxHooks::Fastload::RequestFastloadStop();
            FfxHooks::RequestNovaSuperDamageStop();
            FfxHooks::RequestSeymourBattleStop();
            FfxHooks::F7_RequestStop();
            FfxHooks::F7AiSwap_RequestStop();
            FfxHooks::RequestSpeedHackStop();
            FfxHooks::RequestDialogSkipStop();
            FfxHooks::RequestUnXBoosterStop();
            break;
    }
    return TRUE;
}
