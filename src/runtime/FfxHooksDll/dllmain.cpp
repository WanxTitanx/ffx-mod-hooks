/* ffx-hooks.dll â€” FFX engine hook layer (C++, PolyHook2).
 *
 * Fase 0 skeleton: DLL loads, creates shared memory, writes a log.
 *   No real hooks installed â€” gate: game opens without crash.
 * Fase 1+: MusicHook, ElementHook, etc. activated as IDA confirms RVAs.
 *
 * Loaded by dinput8.dll Module Loader from modules\ (same as ffx-probe.dll).
 * Does NOT replace or modify ffx-probe.dll.
 *
 * Build without PolyHook2 (Fase 0):
 *   cl /nologo /LD /O2 /MT /std:c++17 dllmain.cpp hooks\MusicHook.cpp
 *      hooks\ElementHook.cpp /I. /Fe:ffx-hooks.dll kernel32.lib user32.lib
 *
 * Build with PolyHook2 (Fase 1+):
 *   Add /DFFXHOOKS_HAVE_POLYHOOK and link against vcpkg polyhook2:x86-windows.
 *   See build_hooks.ps1 -WithPolyHook.
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

/* PolyHook2 â€” only included when the library is available (Fase 1+) */
#ifdef FFXHOOKS_HAVE_POLYHOOK
#  include <polyhook2/Detour/x86Detour.hpp>
#  include <polyhook2/MemProtector.hpp>
#  include <d3d11.h>
#  include <dxgi.h>
#  include <d3dcompiler.h>
#endif

#include "shared/ffx_addresses.h"
#include "shared/ffx_hooks_block.h"
#include "shared/Config.h"             // F8 dashboard gate ([dashboard] enabled) — Operacao Demonio
#include "../FfxDinput8Probe/ffx_probe_block.h"
#include "hooks/MusicHook.h"
#include "hooks/NovaSuperDamageHook.h"
#include "hooks/RonsoManaHook.h"
#include "hooks/NulWardHook.h"
#include "hooks/NulWardTeachHook.h"
#include "hooks/GridTeachHook.h"
#include "hooks/KimahriLancetDualGrantHook.h"
#include "hooks/PhaseTurnEdgeHook.h"
#include "hooks/PhaseTurnEdgeSidecar.h"
#include "hooks/ElementHook.h"
#include "hooks/AbilitySfxHook.h"
#include "hooks/ResolverLogHook.h"
#include "hooks/FieldProbeHook.h"
#include "hooks/FieldScoutHook.h"
#include "hooks/BattleEndHook.h"
#include "hooks/ArenaProgressSidecar.h"
#include "hooks/ItemStackCapHook.h"
#include "hooks/DoubleTripleDropHook.h"
#include "hooks/SinCurseHook.h"
#include "hooks/ArenaPlusComposePick.h"
#include "hooks/ArenaPlusGil.h"
#include "hooks/F7InLive.h"
#include "hooks/F7AiSwap.h"
#include "hooks/InGameMenuDashboard.h"   // F8 dashboard (Operacao Demonio 2026-08-02)
#include "hooks/UnXBoosterHook.h"        // F8: boosters/cheats 30Hz (mesmo gate)
#include "hooks/DialogSkipHook.h"        // Onda 3: dialog voice skip (gate input.dialog_skip)
#ifdef FFXHOOKS_HAVE_POLYHOOK
#include "../NativeMenuShell/NativeMenuShell.h"   // step 5.1: casca de menu nativo (ref header)
#include "../BattlePhotoMode/PhotoModeActions.h"  // step 5.1: acoes de RAM da Aurora (ref header)
#endif
/* Hook modules included when their RVA is confirmed: */
/* #include "hooks/ElementHook.h" â€” now active for element_scan_dark.flag */

extern "C" __declspec(dllexport) const char* FF10HgetName(void) {
    return "ffx-hooks (Jarvis PolyHook2 engine hook layer)";
}

extern "C" __declspec(dllexport) const char* FF10HgetVer(void) {
    return "0.2-phase1-validate";
}

/* â”€â”€ Address helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
static HMODULE g_module = NULL;
static uintptr_t g_base = 0;
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
    return EnvFlagEnabled("FFXHOOKS_ENABLE_FPS_SCOUT") ||
           ModuleFlagEnabled("fps_scout.flag") ||
           ModuleFlagEnabled("config\\fps_scout.flag");
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
    return GetModuleHandleA("SpecialK.dll") != NULL ||
           GetModuleHandleA("SpecialK32.dll") != NULL ||
           GetModuleHandleA("SpecialK64.dll") != NULL;
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

static void FpsScoutOnPresent(UINT syncInterval, UINT flags) {
    if (InterlockedCompareExchange(&g_fpsScoutRunning, 1, 1) != 1 ||
        !g_fpsScoutPresentCsv) {
        return;
    }

    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    double dtMs = 0.0;
    if (g_fpsScoutLastPresentQpc.QuadPart != 0 && g_fpsScoutQpcFreq.QuadPart > 0) {
        dtMs = static_cast<double>(now.QuadPart - g_fpsScoutLastPresentQpc.QuadPart) * 1000.0 /
               static_cast<double>(g_fpsScoutQpcFreq.QuadPart);
    }
    g_fpsScoutLastPresentQpc = now;

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

static bool ArenaPlusMusicFlagEnabledRaw() {
    /* FIX 2026-08-02: o .flag.off agora desativa (o ModuleFlagEnabled so via a existencia da flag). */
    if (EnvFlagEnabled("FFXHOOKS_DISABLE_ARENA_PLUS_MUSIC")) return false;
    if (ModuleFileExists("arena_plus_music.flag.off") ||
        ModuleFileExists("config\\arena_plus_music.flag.off")) return false;
    return EnvFlagEnabled("FFXHOOKS_ARENAPLUS_MUSIC") ||
           ModuleFlagEnabled("arena_plus_music.flag") ||
           ModuleFlagEnabled("config\\arena_plus_music.flag");
}

static bool MusicHookEnabledFromConfig() {
    if (ModuleFileExists("music.flag.off") ||
        ModuleFileExists("config\\music.flag.off")) return false;
    return EnvFlagEnabled("FFXHOOKS_ENABLE_MUSIC") ||
           ModuleFlagEnabled("music.flag") ||
           ModuleFlagEnabled("config\\music.flag") ||
           ArenaPlusMusicFlagEnabledRaw();
}

static bool NovaSuperDamageFlagEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ENABLE_NOVA_SUPER_DAMAGE") ||
           ModuleFlagEnabled("nova_super_damage.flag") ||
           ModuleFlagEnabled("config\\nova_super_damage.flag");
}

static bool NovaSuperDamageLogFlagEnabled() {
    return EnvFlagEnabled("FFXHOOKS_NOVA_SUPER_DAMAGE_LOG") ||
           ModuleFlagEnabled("nova_super_damage_log.flag") ||
           ModuleFlagEnabled("config\\nova_super_damage_log.flag");
}

static bool RonsoManaFlagEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ENABLE_RONSO_MANA") ||
           ModuleFlagEnabled("kimahri_ronso_mana.flag") ||
           ModuleFlagEnabled("config\\kimahri_ronso_mana.flag");
}

static bool RonsoManaApplyEnabled() {
    return EnvFlagEnabled("FFXHOOKS_RONSO_MANA_APPLY") ||
           ModuleFlagEnabled("kimahri_ronso_mana_apply.flag") ||
           ModuleFlagEnabled("config\\kimahri_ronso_mana_apply.flag");
}

static bool NulWardFlagEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ENABLE_NUL_WARD") ||
           ModuleFlagEnabled("nul_ward.flag") ||
           ModuleFlagEnabled("config\\nul_ward.flag");
}

static bool NulWardApplyEnabled() {
    return EnvFlagEnabled("FFXHOOKS_NUL_WARD_APPLY") ||
           ModuleFlagEnabled("nul_ward_apply.flag") ||
           ModuleFlagEnabled("config\\nul_ward_apply.flag");
}

static bool NulWardLogFlagEnabled() {
    return NulWardFlagEnabled() ||
           EnvFlagEnabled("FFXHOOKS_NUL_WARD_LOG") ||
           ModuleFlagEnabled("nul_ward_log.flag") ||
           ModuleFlagEnabled("config\\nul_ward_log.flag") ||
           NulWardApplyEnabled();
}

static bool NulWardNativeSlotsEnabled() {
    return EnvFlagEnabled("FFXHOOKS_NUL_WARD_NATIVE_SLOTS") ||
           ModuleFlagEnabled("nul_ward_native_slots.flag") ||
           ModuleFlagEnabled("config\\nul_ward_native_slots.flag");
}

static bool NulWardP16Enabled() {
    return EnvFlagEnabled("FFXHOOKS_NUL_WARD_P16") ||
           ModuleFlagEnabled("nul_ward_p16.flag") ||
           ModuleFlagEnabled("config\\nul_ward_p16.flag");
}

static bool NulWardP16ApplyEnabled() {
    return EnvFlagEnabled("FFXHOOKS_NUL_WARD_P16_APPLY") ||
           ModuleFlagEnabled("nul_ward_p16_apply.flag") ||
           ModuleFlagEnabled("config\\nul_ward_p16_apply.flag");
}

static bool NulWardTeachEnabled() {
    return EnvFlagEnabled("FFXHOOKS_NUL_WARD_TEACH") ||
           ModuleFlagEnabled("nul_ward_teach.flag") ||
           ModuleFlagEnabled("config\\nul_ward_teach.flag");
}

static bool NulWardTeachGrantEnabled() {
    // Explicit opt-in ONLY â€” do NOT tie to nul_ward_teach.flag (that caused born-with grant for all chars).
    return EnvFlagEnabled("FFXHOOKS_NUL_WARD_TEACH_GRANT") ||
           ModuleFlagEnabled("nul_ward_teach_grant.flag") ||
           ModuleFlagEnabled("config\\nul_ward_teach_grant.flag");
}

static bool GridTeachEnabled() {
    return EnvFlagEnabled("FFXHOOKS_GRID_TEACH") ||
           ModuleFlagEnabled("grid_teach.flag") ||
           ModuleFlagEnabled("config\\grid_teach.flag");
}

static bool KimahriLancetDualGrantEnabled() {
    return EnvFlagEnabled("FFXHOOKS_KIMAHRI_LANCET_DUAL_GRANT") ||
           ModuleFlagEnabled("kimahri_lancet_dual_grant.flag") ||
           ModuleFlagEnabled("config\\kimahri_lancet_dual_grant.flag");
}

static bool ItemStackCapFlagEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ENABLE_ITEM_STACK_CAP") ||
           ModuleFlagEnabled("item_stack_cap_255.flag") ||
           ModuleFlagEnabled("config\\item_stack_cap_255.flag");
}

static bool ItemStackCapLogFlagEnabled() {
    return ItemStackCapFlagEnabled() ||
           EnvFlagEnabled("FFXHOOKS_ITEM_STACK_CAP_LOG") ||
           ModuleFlagEnabled("item_stack_cap_log.flag") ||
           ModuleFlagEnabled("config\\item_stack_cap_log.flag");
}

/* Env-only gate â€” no flag files yet (coordination with parallel DLL lane). */
static bool DoubleTripleDropEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ENABLE_DOUBLE_TRIPLE_DROP");
}

static bool DoubleTripleDropLogEnabled() {
    return DoubleTripleDropEnabled() ||
           EnvFlagEnabled("FFXHOOKS_DOUBLE_TRIPLE_DROP_LOG");
}

static bool ElementScanDarkEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ELEMENT_SCAN_DARK") ||
           ModuleFlagEnabled("element_scan_dark.flag") ||
           ModuleFlagEnabled("config\\element_scan_dark.flag");
}

static bool AbilitySfxFlagEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ENABLE_ABILITY_SFX") ||
           ModuleFlagEnabled("ability_sfx.flag") ||
           ModuleFlagEnabled("config\\ability_sfx.flag");
}

static bool AbilitySfxLogFlagEnabled() {
    return AbilitySfxFlagEnabled() ||
           EnvFlagEnabled("FFXHOOKS_ABILITY_SFX_LOG") ||
           ModuleFlagEnabled("ability_sfx_log.flag") ||
           ModuleFlagEnabled("config\\ability_sfx_log.flag");
}

static bool FieldProbeRt2FlagEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ENABLE_FIELD_PROBE_RT2") ||
           ModuleFlagEnabled("field_probe_rt2.flag") ||
           ModuleFlagEnabled("config\\field_probe_rt2.flag");
}

static bool FieldProbeEncounterOnlyFlagEnabled() {
    return EnvFlagEnabled("FFXHOOKS_FIELD_PROBE_ENCOUNTER") ||
           ModuleFlagEnabled("field_probe_encounter.flag") ||
           ModuleFlagEnabled("config\\field_probe_encounter.flag");
}

static bool FieldProbeTextureOnlyFlagEnabled() {
    return EnvFlagEnabled("FFXHOOKS_FIELD_PROBE_TEXTURE") ||
           ModuleFlagEnabled("field_probe_texture.flag") ||
           ModuleFlagEnabled("config\\field_probe_texture.flag");
}

static bool FieldScoutFlagEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ENABLE_FIELD_SCOUT") ||
           ModuleFlagEnabled("field_scout.flag") ||
           ModuleFlagEnabled("config\\field_scout.flag");
}

static bool FieldScoutMapOnlyFlagEnabled() {
    return EnvFlagEnabled("FFXHOOKS_FIELD_SCOUT_MAP_ONLY") ||
           ModuleFlagEnabled("field_scout_map_only.flag") ||
           ModuleFlagEnabled("config\\field_scout_map_only.flag");
}

static bool FieldScoutHeavyFlagEnabled() {
    return EnvFlagEnabled("FFXHOOKS_FIELD_SCOUT_HEAVY") ||
           ModuleFlagEnabled("field_scout_heavy.flag") ||
           ModuleFlagEnabled("config\\field_scout_heavy.flag");
}

static bool FieldScoutUltraFlagEnabled() {
    return EnvFlagEnabled("FFXHOOKS_FIELD_SCOUT_ULTRA") ||
           ModuleFlagEnabled("field_scout_ultra.flag") ||
           ModuleFlagEnabled("config\\field_scout_ultra.flag");
}

static bool FieldScoutUltraSubFlagEnabled(const char* flagName, const char* configFlagName, const char* envName) {
    if (!FieldScoutHeavyFlagEnabled() || !FieldScoutUltraFlagEnabled()) return false;
    return EnvFlagEnabled(envName) ||
           ModuleFlagEnabled(flagName) ||
           ModuleFlagEnabled(configFlagName);
}

static bool FieldScoutMaxFlagEnabled() {
    return EnvFlagEnabled("FFXHOOKS_FIELD_SCOUT_MAX") ||
           ModuleFlagEnabled("field_scout_max.flag") ||
           ModuleFlagEnabled("config\\field_scout_max.flag");
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
static volatile LONG g_auroraOverlayVisible = 1;
static volatile LONG g_auroraOverlayDetail = 0;
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
static volatile LONG g_auroraActorOverlayEnabled = 1;

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

static bool InGameMenuConfigEnabled() {
    return EnvFlagEnabled("FFXHOOKS_INGAME_MENU") ||
           EnvFlagEnabled("FFXHOOKS_ENABLE_INGAME_MENU") ||
           ModuleFlagEnabled("ingame_menu.flag") ||
           ModuleFlagEnabled("config\\ingame_menu.flag") ||
           (AuroraConfigExists() && AuroraConfigInt("ingame_menu", 0) != 0);
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

static int InGameMenuRowCount() {
    return 3 + g_ingameMenuPluginCount;
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
    if (row == 0) {
        const LONG enabled = InterlockedCompareExchange(&g_auroraActorOverlayEnabled, 0, 0) ? 0 : 1;
        InterlockedExchange(&g_auroraActorOverlayEnabled, enabled);
        InterlockedExchange(&g_auroraOverlayVisible, enabled);
        InGameMenuSetStatus("Aurora actor labels %s", enabled ? "ligado" : "desligado");
        return;
    }
    if (row == 1) {
        const LONG detail = InterlockedCompareExchange(&g_auroraOverlayDetail, 0, 0) ? 0 : 1;
        InterlockedExchange(&g_auroraOverlayDetail, detail);
        InGameMenuSetStatus("Aurora detail labels %s", detail ? "ligado" : "desligado");
        return;
    }
    if (row == 2) {
        InGameMenuRefreshPlugins();
        InGameMenuSetStatus("Plugin list refreshed: %d item(s)", g_ingameMenuPluginCount);
        return;
    }

    const int pluginIndex = row - 3;
    if (pluginIndex >= 0 && pluginIndex < g_ingameMenuPluginCount) {
        const InGameMenuPlugin& item = g_ingameMenuPlugins[pluginIndex];
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

    if (vk == VK_F8 || vk == VK_INSERT) {
        // WAVE 2 (2026-08-02): key arbitration -- with dashboard active it OWNS
        // do F8/INSERT (o DWP subclasseado do dashboard consome antes). O InGameMenu
        // so processa F8/INSERT com dashboard.enabled=0 (fallback).
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

    if (AuroraKeyPressed(VK_F8)) InGameMenuProcessKey(VK_F8, "poll");
    if (AuroraKeyPressed(VK_INSERT)) InGameMenuProcessKey(VK_INSERT, "poll");

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

static LRESULT CALLBACK InGameMenuWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
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
    if (!InterlockedCompareExchange(&g_ingameMenuEnabled, 1, 1) || !hwnd) return;
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

enum InGameMenuIconType {
    InGameMenuIconEye = 0,
    InGameMenuIconList = 1,
    InGameMenuIconRefresh = 2,
    InGameMenuIconPuzzle = 3,
    InGameMenuIconShield = 4,
    InGameMenuIconGear = 5
};

static int InGameMenuIconForRow(int row, const char* file) {
    if (row == 0) return InGameMenuIconEye;
    if (row == 1) return InGameMenuIconList;
    if (row == 2) return InGameMenuIconRefresh;
    if (file && _stricmp(file, "ffx-probe.dll") == 0) return InGameMenuIconShield;
    if (file && (_stricmp(file, "dxgi.dll") == 0 || _stricmp(file, "unx.dll") == 0)) return InGameMenuIconGear;
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
    if (g_ingameMenuLastScanTick == 0 || now - g_ingameMenuLastScanTick > 1500) {
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
    InGameDrawSoftText(hdc, "JARVIS FFX IN-GAME MENU", title,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
        RGB(70, 222, 207), true);

    SelectObject(hdc, smallFont);
    RECT subtitle = { panel.left + 28, panel.top + 38, panel.right - 28, panel.top + 56 };
    InGameDrawSoftText(hdc, "Runtime switchboard / loaded DLLs / safe live controls", subtitle,
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
        if (row == 0) {
            lstrcpynA(label, "Aurora actor labels", static_cast<int>(sizeof(label)));
            if (InterlockedCompareExchange(&g_auroraActorOverlayEnabled, 1, 1) &&
                InterlockedCompareExchange(&g_auroraOverlayVisible, 1, 1)) {
                state = "Ligado";
                stateColor = RGB(94, 220, 185);
            } else {
                state = "Desligado";
                stateColor = RGB(255, 64, 87);
            }
        } else if (row == 1) {
            lstrcpynA(label, "Aurora detail labels", static_cast<int>(sizeof(label)));
            if (InterlockedCompareExchange(&g_auroraOverlayDetail, 1, 1)) {
                state = "Ligado";
                stateColor = RGB(94, 220, 185);
            } else {
                state = "Desligado";
                stateColor = RGB(255, 64, 87);
            }
        } else if (row == 2) {
            lstrcpynA(label, "Refresh plugin list", static_cast<int>(sizeof(label)));
            state = "Scan";
            stateColor = RGB(246, 196, 92);
        } else {
            const int pluginIndex = row - 3;
            if (pluginIndex >= 0 && pluginIndex < g_ingameMenuPluginCount) {
                const InGameMenuPlugin& item = g_ingameMenuPlugins[pluginIndex];
                lstrcpynA(label, item.label, static_cast<int>(sizeof(label)));
                file = item.file;
                state = InGameMenuPluginState(item, &stateColor);
            }
        }

        RECT iconRect = { rowRect.left + 18, rowRect.top + 10, rowRect.left + 36, rowRect.top + 28 };
        InGameDrawIcon(hdc, InGameMenuIconForRow(row, file), iconRect, selected ? RGB(161, 255, 236) : RGB(190, 205, 210));

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
    InGameDrawSoftText(hdc, "Selecionar", label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
        RGB(197, 210, 214), false);

    hintX = label.right + 13;
    key = { hintX, hintY, hintX + 42, hintY + 17 };
    InGameDrawKeycap(hdc, key, "Enter");
    label = { key.right + 6, hintY, key.right + 49, hintY + 17 };
    InGameDrawSoftText(hdc, "Ativar", label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
        RGB(197, 210, 214), false);

    hintX = label.right + 13;
    key = { hintX, hintY, hintX + 31, hintY + 17 };
    InGameDrawKeycap(hdc, key, "Esc");
    label = { key.right + 6, hintY, key.right + 50, hintY + 17 };
    InGameDrawSoftText(hdc, "Fechar", label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
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
static bool            g_auroraD3DHooked = false;
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
static void NativeMenu_PresentTick();   // wire (def. abaixo): F7 + force-gate; roda toda frame ate no field
#endif
static void AuroraD3DRender(IDXGISwapChain* swapChain) {
    if (!swapChain) return;
#ifdef FFXHOOKS_HAVE_POLYHOOK
    // [AURORA-OWNED / coordinate â€” ver blueprint sec 9] held-override LISO: runs AFTER the
    // update do ator (contrato Aurora sec 1). RT2-PENDING: confirmar thread/ordem ao vivo.
    if (PhotoMode::g_pm.on) { __try { PhotoMode::Tick(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    // FORCE-GATE: F7 + reescrita de dword_13407E4=1 toda frame -> o pump roda ATE no field (sem menu concorrente).
    __try { NativeMenu_PresentTick(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
#endif
    const bool menuOpen = InGameMenuHandleInput();
    if (AuroraKeyPressed(VK_F9)) {
        const LONG visible = InterlockedCompareExchange(&g_auroraOverlayVisible, 0, 0) ? 0 : 1;
        InterlockedExchange(&g_auroraOverlayVisible, visible);
        InterlockedExchange(&g_auroraActorOverlayEnabled, visible);
        Log("[ffx-hooks] AuroraD3D visible=%d\n", visible ? 1 : 0);
    }
    if (AuroraKeyPressed(VK_F10)) {
        const LONG detail = InterlockedCompareExchange(&g_auroraOverlayDetail, 0, 0) ? 0 : 1;
        InterlockedExchange(&g_auroraOverlayDetail, detail);
        Log("[ffx-hooks] AuroraD3D detail=%d\n", detail ? 1 : 0);
    }
    const bool actorVisible =
        InterlockedCompareExchange(&g_auroraActorOverlayEnabled, 1, 1) &&
        InterlockedCompareExchange(&g_auroraOverlayVisible, 1, 1);
    if (!actorVisible && !menuOpen) return;

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

static bool AuroraD3DHookPresentFromVtable(void** vtable, const char* reason) {
    if (g_auroraD3DPresentDetour) {
        return g_auroraD3DHooked;
    }

    if (!vtable || !vtable[8]) {
        Log("[ffx-hooks] WARN AuroraD3D swapchain vtable missing\n");
        return false;
    }

    const uint64_t presentVa = reinterpret_cast<uint64_t>(vtable[8]);
    const uint64_t shimVa = reinterpret_cast<uint64_t>(AuroraD3DPresentShim);
    Log("[ffx-hooks] AuroraD3D installing Present hook from real swapchain VA=0x%08X reason=%s\n",
        static_cast<unsigned>(presentVa), reason ? reason : "unknown");

    try {
        g_auroraD3DPresentDetour = new PLH::x86Detour(presentVa, shimVa, &g_auroraD3DPresentTrampoline);
        g_auroraD3DHooked = g_auroraD3DPresentDetour->hook();
        Log("[ffx-hooks] AuroraD3D Present hook result ok=%d trampoline=0x%llX\n",
            g_auroraD3DHooked ? 1 : 0,
            static_cast<unsigned long long>(g_auroraD3DPresentTrampoline));
        if (!g_auroraD3DHooked) {
            delete g_auroraD3DPresentDetour;
            g_auroraD3DPresentDetour = nullptr;
            g_auroraD3DPresentTrampoline = 0;
        }
    } catch (const std::exception& ex) {
        Log("[ffx-hooks] ERROR AuroraD3D Present hook exception: %s\n", ex.what());
        delete g_auroraD3DPresentDetour;
        g_auroraD3DPresentDetour = nullptr;
        g_auroraD3DPresentTrampoline = 0;
        g_auroraD3DHooked = false;
    } catch (...) {
        Log("[ffx-hooks] ERROR AuroraD3D Present hook unknown exception\n");
        delete g_auroraD3DPresentDetour;
        g_auroraD3DPresentDetour = nullptr;
        g_auroraD3DPresentTrampoline = 0;
        g_auroraD3DHooked = false;
    }
    return g_auroraD3DHooked;
}

static bool AuroraD3DHookPresentFromSwapChain(IDXGISwapChain* swapChain, const char* reason) {
    if (!swapChain) return false;
    void** vtable = *reinterpret_cast<void***>(swapChain);
    return AuroraD3DHookPresentFromVtable(vtable, reason);
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
           !g_auroraD3DHooked) {
        Sleep(250);
        waited += 250;
    }

    if (InterlockedCompareExchange(&g_auroraD3DFallbackRunning, 1, 1) == 1 &&
        !g_auroraD3DHooked) {
        void** vtable = nullptr;
        Log("[ffx-hooks] AuroraD3D late Present fallback probing dummy swapchain\n");
        if (AuroraD3DCreateDummySwapChain(&vtable) && vtable) {
            AuroraD3DHookPresentFromVtable(vtable, "late-dummy-swapchain");
        } else {
            Log("[ffx-hooks] WARN AuroraD3D late Present fallback could not create dummy swapchain\n");
        }
    }

    InterlockedExchange(&g_auroraD3DFallbackRunning, 0);
    g_auroraD3DFallbackThread = NULL;
    return 0;
}

static void StartAuroraD3DLatePresentFallback() {
    if (InterlockedCompareExchange(&g_auroraD3DFallbackRunning, 1, 0) != 0) {
        return;
    }

    DWORD tid = 0;
    g_auroraD3DFallbackThread = CreateThread(
        nullptr, 0, AuroraD3DLatePresentFallbackThreadProc, nullptr, 0, &tid);
    if (g_auroraD3DFallbackThread) {
        Log("[ffx-hooks] AuroraD3D late Present fallback thread created tid=%u\n",
            static_cast<unsigned>(tid));
        CloseHandle(g_auroraD3DFallbackThread);
    } else {
        InterlockedExchange(&g_auroraD3DFallbackRunning, 0);
        Log("[ffx-hooks] WARN AuroraD3D late Present fallback thread create failed (err=%u)\n",
            GetLastError());
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
    InGameMenuRestoreWndProc();
    AuroraD3DRemoveContextSniffer();

    if (g_auroraD3DPresentDetour) {
        try {
            if (g_auroraD3DHooked) {
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
    g_auroraD3DHooked = false;
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
    Log("[ffx-hooks] AuroraOverlay window ready; F9 toggles, F10 detail\n");

    MSG msg = {};
    while (InterlockedCompareExchange(&g_auroraOverlayRunning, 1, 1) == 1) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (AuroraKeyPressed(VK_F9)) {
            const LONG visible = InterlockedCompareExchange(&g_auroraOverlayVisible, 0, 0) ? 0 : 1;
            InterlockedExchange(&g_auroraOverlayVisible, visible);
            ShowWindow(hwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
            Log("[ffx-hooks] AuroraOverlay visible=%d\n", visible ? 1 : 0);
        }
        if (AuroraKeyPressed(VK_F10)) {
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
    const bool fileFlag =
        ModuleFlagEnabled("native_menu.flag") ||
        ModuleFlagEnabled("config\\native_menu.flag") ||
        ModuleFlagEnabled("f7_inlive.flag") ||          // F7 In-Live tambem arma o menu nativo (F7)
        ModuleFlagEnabled("config\\f7_inlive.flag");
    const bool envFlag = EnvFlagEnabled("FFXHOOKS_ENABLE_NATIVE_MENU");
    /* Field Scout walk: env-only NativeMenu + Aurora Present hook soft-locks titl00 boot.
     * Require explicit native_menu.flag when field_scout* is armed. */
    if (FieldScoutFlagEnabled() || FieldScoutMapOnlyFlagEnabled()) {
        return fileFlag;
    }
    return envFlag || fileFlag;
}
#endif

static void StartAuroraOverlayIfEnabled() {
    const bool envEnabled = EnvFlagEnabled("FFXHOOKS_ENABLE_AURORA_OVERLAY");
    const bool inGameMenuEnabled = InGameMenuConfigEnabled();
#ifdef FFXHOOKS_HAVE_POLYHOOK
    const bool nativeMenuEnabled = NativeMenuArmedFromConfig();
#else
    const bool nativeMenuEnabled = false;
#endif
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
        ModuleFlagEnabled("aurora_overlay.flag") ||
        ModuleFlagEnabled("config\\aurora_overlay.flag") ||
        configEnabled ||
        inGameMenuEnabled ||
        nativeMenuEnabled ||
        d3dFileEnabled ||
        sniffFileEnabled;
    if (!envEnabled && !fileEnabled) {
        return;
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
            return;
        }
        InterlockedExchange(&g_auroraD3DRenderEnabled, 0);
        Log("[ffx-hooks] WARN AuroraD3D install failed; falling back to GDI overlay window\n");
    }

    if (InterlockedCompareExchange(&g_auroraOverlayRunning, 1, 0) != 0) {
        return;
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
    // Legacy LabMenu moved to F6 â€” sem colisÃ£o de tecla entre os trÃªs menus.
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

/* â”€â”€ NATIVE MENU SHELL WIRE (step 5.1) â€” OFF by default â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 *  Liga NativeMenuShell.h (menu nativo, texto NOSSO) no jogo e roteia as linhas
 *  pras acoes da Aurora (PhotoModeActions.h, contrato sec 4). Gate: env
 *  FFXHOOKS_ENABLE_NATIVE_MENU=1. Hotkey: env FFXHOOKS_NATIVE_MENU_HOTKEY (VK) ou
 *  F7 default. Seam: detour em FFX_Menu_PerFramePump (VA 0x8A9C50 =
 *  int __cdecl(unsigned int), IDA-verificado) â€” MAIN THREAD, so com subsistema de
 *  menu vivo (dword_13407E4). Reversivel. Tudo SEH-guarded. */
#ifdef FFXHOOKS_HAVE_POLYHOOK
// PhotoModeActions.h declares g_base/g_pm as extern
namespace PhotoMode { uintptr_t g_base = 0; State g_pm; }

static PLH::x86Detour*   g_nativeMenuPumpDetour = nullptr;
static uint64_t          g_nativeMenuPumpTramp  = 0;
static NativeMenu::Menu  g_nativeMenu           = { 0 };
static NativeMenu::Menu  g_arenaPlusMenu        = { 0 };
static int               g_nativeMenuHotkey     = VK_F7;
static int               g_nativeHeldAction     = -1;   // -1 = nenhum; senao ActionId em modo "segurar"
static volatile LONG     g_nativeMenuInHook     = 0;
static volatile LONG     g_forceSubsystem       = 0;   // 1 = forca dword_13407E4=1 toda frame (Present) -> pump roda no field
static volatile LONG     g_nativeWantSpawn      = 0;   // pedido de spawn (consumido pelo pump hook quando o pump roda)
static volatile LONG     g_nativeWantClose      = 0;   // pedido de close
static volatile LONG     g_arenaPlusWantOpen    = 0;   // pedido de abrir a tela Arena+ propria no pump
static volatile LONG     g_arenaNpcInNowWhat    = 0;   // Common.013B Now what? dialog ativo (bloqueia overlap)
static volatile LONG     g_arenaNpcPendingOpen  = 0;   // abrir Arena+ apos dialogo vanilla fechar
static volatile LONG     g_arenaNpcOpenDelay    = 0;   // frames de pump antes de abrir (evita overlap)

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
static const int ARENA_PLUS_MAX_MENU_ROWS = 12;
static const int ARENA_PLUS_VISIBLE_PAGE = 8;

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
};

static const int ARENA_PLUS_HUB_ROW_SAFE = 0;
static const int ARENA_PLUS_HUB_ROW_DARK = 1;
static const int ARENA_PLUS_HUB_ROW_GAUNTLET = 2;
static const int ARENA_PLUS_HUB_ROW_MIX = 3;
static const int ARENA_PLUS_HUB_ROW_ULTRA = 4;
static const int ARENA_PLUS_HUB_ROW_BACK = 5;
static const int ARENA_PLUS_HUB_ROW_COUNT = 6;

static ArenaPlusMenuKind g_arenaPlusMenuKind = ArenaPlusMenuKind::Hub;
static int g_arenaPlusActiveRowCount = ARENA_PLUS_HUB_ROW_COUNT;

// â”€â”€ SIN Curse submenu state â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
#define SIN_CURSE_ROW_TOGGLE      0
#define SIN_CURSE_ROW_INTENSITY   1
#define SIN_CURSE_ROW_REGION      2
#define SIN_CURSE_ROW_BACK        3
#define SIN_CURSE_ROW_COUNT       4

static NativeMenu::Menu  g_sinMenu             = { 0 };
static int               g_sinMenuResult       = 0;
static bool              g_sinMenuClosed       = false;
static volatile LONG     g_sinWantOpen         = 0;
static bool              g_sinCurseOn          = false;
static int               g_sinCurseIntensity   = 5; // index 5 = 60% (default)
static char              g_sinCurseRegion[32]  = {};
static int               g_sinCurseThreatCap   = 0;
static int               g_sinLastEdge         = 0; // edge detection (NUNCA resetar â€” persiste entre spawns)
static int               g_sinConfirmTimer     = 0; // confirm cooldown (sÃ³ Enter, navegaÃ§Ã£o livre)
static int               g_sinLastRow          = SIN_CURSE_ROW_TOGGLE;
static float             g_sinEasedRowY        = -1.0f;

static unsigned char     g_sinLabels[SIN_CURSE_ROW_COUNT][64] = {};
static unsigned char     g_sinSubLabels[SIN_CURSE_ROW_COUNT][64] = {};

// 10 intensity levels: 10%..100%
static const char* kSinIntensityNames[] = {
    "10%", "20%", "30%", "40%", "50%",
    "60%", "70%", "80%", "90%", "100%"
};
static const char* kSinIntensityFlags[] = {
    "10", "20", "30", "40", "50",
    "60", "70", "80", "90", "100"
};
static const char* kSinIntensityDesc[] = {
    "Trace", "Faint", "Mild", "Moderate", "Notable",
    "Default", "Severe", "Intense", "Extreme", "Total"
};

// Row colors (ARGB)
static const unsigned int kSinToggleOnTop  = 0xE6B33CFFu;
static const unsigned int kSinToggleOnBot  = 0xE63A0A6Eu;
static const unsigned int kSinToggleOffTop = 0xD06B382Eu;
static const unsigned int kSinToggleOffBot = 0xD0271110u;
static const unsigned int kSinIntensityTop = 0xE04A3A72u;
static const unsigned int kSinIntensityBot = 0xE0182048u;
static const unsigned int kSinRegionTop    = 0x68283850u;
static const unsigned int kSinRegionBot    = 0x48182028u;
static const unsigned int kSinBackTop      = 0xC0222A34u;
static const unsigned int kSinBackBot      = 0xC00A1018u;

// â”€â”€ SIN flag I/O â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

static void SinCurse_ReadFlags() {
    g_sinCurseOn = ModuleFileExists("config\\sin_curse.flag");

    char path[MAX_PATH] = {};
    if (!ModuleRelativePath("config\\sin_f7_intensity.flag", path, sizeof(path))) return;
    char content[32] = {};
    if (!InGameReadSmallTextFile(path, content, sizeof(content))) return;

    for (int i = 0; content[i]; ++i) {
        if (content[i] == '\r' || content[i] == '\n' || content[i] == ' ') {
            content[i] = '\0';
            break;
        }
    }

    int pct = atoi(content);
    if (pct >= 10 && pct <= 100)
        g_sinCurseIntensity = (pct / 10) - 1; // 10â†’0, 20â†’1, ..., 100â†’9
    else
        g_sinCurseIntensity = 5; // default 60%
}

static void SinCurse_WriteIntensity() {
    char path[MAX_PATH] = {};
    if (!ModuleRelativePath("config\\sin_f7_intensity.flag", path, sizeof(path))) return;

    int i = g_sinCurseIntensity;
    if (i < 0) i = 0;
    if (i > 9) i = 9;
    const char* text = kSinIntensityFlags[i];

    HANDLE hFile = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        Log("[ffx-hooks] SinCurse: failed to write intensity flag\n");
        return;
    }
    DWORD written = 0;
    WriteFile(hFile, text, (DWORD)strlen(text), &written, nullptr);
    CloseHandle(hFile);
    Log("[ffx-hooks] SinCurse: intensity flag set to '%s'\n", text);
}

static void SinCurse_ToggleOnOff() {
    char onPath[MAX_PATH]  = {};
    char offPath[MAX_PATH] = {};
    ModuleRelativePath("config\\sin_curse.flag",     onPath,  sizeof(onPath));
    ModuleRelativePath("config\\sin_curse.flag.off", offPath, sizeof(offPath));

    if (g_sinCurseOn) {
        if (MoveFileA(onPath, offPath) == 0)
            Log("[ffx-hooks] SinCurse: MoveFileA .flag->.flag.off failed %lu\n", GetLastError());
        g_sinCurseOn = false;
        Log("[ffx-hooks] SinCurse: toggled OFF\n");
    } else {
        if (MoveFileA(offPath, onPath) == 0) {
            HANDLE hFile = CreateFileA(onPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
        }
        g_sinCurseOn = true;
        Log("[ffx-hooks] SinCurse: toggled ON\n");
    }
}

static void SinCurse_CycleIntensity() {
    g_sinCurseIntensity = (g_sinCurseIntensity + 1) % 10;
    SinCurse_WriteIntensity();
    Log("[ffx-hooks] SinCurse: intensity set to %s\n", kSinIntensityNames[g_sinCurseIntensity]);
}

// â”€â”€ SIN menu draw â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

static int __cdecl SinCurse_DrawCb(int obj) {
    using namespace NativeMenu;
    static int s_drawCalls = 0;
    const int F = ++s_drawCalls;
    const int sel = RdW(obj, O_SELECTED);
    const int top = RdW(obj, O_TOP);
    const int page = RdW(obj, O_PAGE);

    // Pulsing: Osc01 gives smooth 0..1..0 triangle with smoothstep
    const float pulse1 = Osc01(F, 45);                    // neon pulse (slow)
    const float neonStr = 0.55f + 0.45f * pulse1;        // 0.55..1.0

    char title[64] = {}, sub[64] = {}, foot[96] = {};
    EncodeLabel("S.I.N. - Spira Instinct Network", (unsigned char*)title, (int)sizeof(title));
    EncodeLabel("Runtime curse control", (unsigned char*)sub, (int)sizeof(sub));
    EncodeLabel("Arrows Navigate   Confirm Select   Cancel Back   F7 Exit", (unsigned char*)foot, (int)sizeof(foot));

    DrawMenuBackdrop();
    DrawMenuNeonFrame(F);

    // header
    const float hx = NX(0.047f), hy = NY(0.054f), hw = NW(0.906f), hh = NH(0.126f);
    DrawMenuGlassPanel(hx, hy, hw, hh, F, 0);
    DrawString((unsigned char*)title, NX(0.071f), NY(0.081f));
    DrawString((unsigned char*)sub,   NX(0.071f), NY(0.137f));

    // rows â€” same layout as main F7 menu
    const float vLeft   = NX(0.271f);
    const float vTop    = NY(0.215f);
    const float vWidth  = NW(0.458f);
    const float vStep   = NH(0.063f);
    const float vBarH   = NH(0.056f);
    const float vPadX   = NW(0.015f);
    const float selLine = MenuBorderPx() * 0.45f;
    const float cursorOff = NW(0.020f);

    for (int r = 0; r < page && top + r < SIN_CURSE_ROW_COUNT; ++r) {
        const int row = top + r;
        const float vy = vTop + r * vStep;

        unsigned int c0 = kSinRegionTop, c1 = kSinRegionBot;
        if (row == SIN_CURSE_ROW_TOGGLE) {
            c0 = g_sinCurseOn ? kSinToggleOnTop  : kSinToggleOffTop;
            c1 = g_sinCurseOn ? kSinToggleOnBot  : kSinToggleOffBot;
        } else if (row == SIN_CURSE_ROW_INTENSITY) {
            c0 = kSinIntensityTop;
            c1 = kSinIntensityBot;
        } else if (row == SIN_CURSE_ROW_BACK) {
            c0 = kSinBackTop;
            c1 = kSinBackBot;
        }

        // Neon pulse on interactive rows
        if (row == SIN_CURSE_ROW_TOGGLE || row == SIN_CURSE_ROW_INTENSITY) {
            unsigned int a0 = ((c0 >> 24) & 0xFFu), a1 = ((c1 >> 24) & 0xFFu);
            a0 = (unsigned int)(a0 * neonStr);
            a1 = (unsigned int)(a1 * neonStr);
            if (a0 > 0xFF) a0 = 0xFF; if (a1 > 0xFF) a1 = 0xFF;
            c0 = (c0 & 0x00FFFFFFu) | (a0 << 24);
            c1 = (c1 & 0x00FFFFFFu) | (a1 << 24);
        }

        DrawSolidRect(vLeft, vy, vWidth, vBarH, c0, c1);
        DrawString(g_sinLabels[row], vLeft + vPadX, vy + NH(0.016f));

        // Sub-label (intensity description)
        if (g_sinSubLabels[row][0] != 0)
            DrawStringSub(g_sinSubLabels[row], vLeft + NW(0.175f), vy + NH(0.023f));
    }

    // ===== SELECAO (eased Y + lift overlay, matching main F7 menu style) =====
    const float selVisY = vTop + (float)(sel - top) * vStep;
    if (g_sinEasedRowY < 0.0f) g_sinEasedRowY = selVisY;
    g_sinEasedRowY += (selVisY - g_sinEasedRowY) * 0.30f;

    if (sel >= top && sel < top + page) {
        const float ey = g_sinEasedRowY;
        // Lift overlay: pulsing alpha, blue-steel tint
        const unsigned int a = 0x44u + (unsigned int)(Osc01(F, 44) * 32.0f);
        const unsigned int lift0 = (a << 24) | 0x00305068u;
        const unsigned int lift1 = (a << 24) | 0x00182038u;
        DrawSolidRect(vLeft, ey, vWidth, vBarH, lift0, lift1);
        DrawSolidRect(vLeft, ey + vBarH - selLine, vWidth, selLine, kMenuNeonGreenLine, kMenuNeonGreenLineLo);
        DrawCursor(vLeft - cursorOff, ey + NH(0.002f));
    }

    // footer
    const float fx = NX(0.047f), fy = NY(0.887f), fw = NW(0.906f), fh = NH(0.070f);
    DrawMenuGlassPanel(fx, fy, fw, fh, F, 1);
    DrawString((unsigned char*)foot, NX(0.071f), NY(0.911f));

    return obj;
}

// â”€â”€ SIN menu input â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

static int __cdecl SinCurse_InputCb(int obj) {
    // Confirm cooldown: decrementa todo frame, sÃ³ bloqueia Enter (navegaÃ§Ã£o livre)
    if (g_sinConfirmTimer > 0) --g_sinConfirmTimer;

    const int dir = NativeMenu::PadDir();
    const int edge = NativeMenu::PadEdge();
    const int confirmEdge = edge & 0x20;
    const int cancelEdge   = edge & 0x40;
    // Rising-edge only â€” g_sinLastEdge NUNCA Ã© resetado (persiste entre spawns)
    const bool confirmPressed = (confirmEdge != 0) && !(g_sinLastEdge & 0x20) && (g_sinConfirmTimer == 0);
    const bool cancelPressed  = (cancelEdge != 0) && !(g_sinLastEdge & 0x40);
    g_sinLastEdge = edge & 0x60;

    int sel = NativeMenu::RdW(obj, NativeMenu::O_SELECTED);
    const int count = NativeMenu::RdW(obj, NativeMenu::O_COUNT);
    int top = NativeMenu::RdW(obj, NativeMenu::O_TOP);
    const int page = NativeMenu::RdW(obj, NativeMenu::O_PAGE);
    if (count > 0) {
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
            if (sel == SIN_CURSE_ROW_REGION) {
                NativeMenu::PlaySfx(1);
            } else {
                NativeMenu::PlaySfx(1);
                g_sinLastRow = sel;
                g_sinConfirmTimer = 10; // ~167ms â€” sÃ³ bloqueia Enter, navegaÃ§Ã£o livre
                g_sinMenuResult = sel;
                g_sinMenuClosed = true;
            }
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
    char toggleLabel[64] = {};
    _snprintf_s(toggleLabel, sizeof(toggleLabel), _TRUNCATE, "Curse: %s", g_sinCurseOn ? "ON" : "OFF");
    NativeMenu::EncodeLabel(toggleLabel, g_sinLabels[SIN_CURSE_ROW_TOGGLE], 64);
    g_sinSubLabels[SIN_CURSE_ROW_TOGGLE][0] = 0;

    char intLabel[64] = {};
    _snprintf_s(intLabel, sizeof(intLabel), _TRUNCATE, "%s", kSinIntensityNames[g_sinCurseIntensity]);
    NativeMenu::EncodeLabel(intLabel, g_sinLabels[SIN_CURSE_ROW_INTENSITY], 64);
    NativeMenu::EncodeLabel(kSinIntensityDesc[g_sinCurseIntensity], g_sinSubLabels[SIN_CURSE_ROW_INTENSITY], 64);

    char regLabel[64] = {};
    if (g_sinCurseRegion[0] != '\0') {
        _snprintf_s(regLabel, sizeof(regLabel), _TRUNCATE, "Zone: %s [T%d]", g_sinCurseRegion, g_sinCurseThreatCap);
    } else {
        _snprintf_s(regLabel, sizeof(regLabel), _TRUNCATE, "Zone: (none)");
    }
    NativeMenu::EncodeLabel(regLabel, g_sinLabels[SIN_CURSE_ROW_REGION], 64);
    g_sinSubLabels[SIN_CURSE_ROW_REGION][0] = 0;

    NativeMenu::EncodeLabel("Back", g_sinLabels[SIN_CURSE_ROW_BACK], 64);
    g_sinSubLabels[SIN_CURSE_ROW_BACK][0] = 0;
}

static NativeMenu::Menu SinCurse_SpawnMenu() {
    SinCurse_ReadFlags();

    g_sinCurseRegion[0] = '\0';
    g_sinCurseThreatCap = 0;
    if (FfxHooks::IsSinCurseHookInstalled()) {
        const char* r = FfxHooks::GetCurrentRegion();
        if (r && r[0]) lstrcpynA(g_sinCurseRegion, r, (int)sizeof(g_sinCurseRegion));
        g_sinCurseThreatCap = FfxHooks::GetCurrentThreatCap();
    } else {
        _snprintf_s(g_sinCurseRegion, sizeof(g_sinCurseRegion), _TRUNCATE, "(hook not loaded)");
    }

    SinCurse_BuildLabels();
    g_sinLastEdge = 0;
    g_sinEasedRowY = -1.0f;

    int obj = NativeMenu::Alloc();
    if (!obj) return NativeMenu::Menu{ 0 };

    int initSel = g_sinLastRow;
    if (initSel < 0 || initSel >= SIN_CURSE_ROW_COUNT) initSel = SIN_CURSE_ROW_TOGGLE;

    NativeMenu::WrW(obj, NativeMenu::O_COUNT,    static_cast<int16_t>(SIN_CURSE_ROW_COUNT));
    NativeMenu::WrW(obj, NativeMenu::O_PAGE,     6);
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

    return NativeMenu::Menu{ obj };
}

static void SinCurse_HandleConfirm(int row) {
    if (row == SIN_CURSE_ROW_TOGGLE) {
        SinCurse_ToggleOnOff();
        SinCurse_BuildLabels();
        g_sinMenu = SinCurse_SpawnMenu();
        if (!g_sinMenu.obj) g_nativeMenu = NativeMenu::SpawnMenu();
        return;
    }
    if (row == SIN_CURSE_ROW_INTENSITY) {
        SinCurse_CycleIntensity();
        SinCurse_BuildLabels();
        g_sinMenu = SinCurse_SpawnMenu();
        if (!g_sinMenu.obj) g_nativeMenu = NativeMenu::SpawnMenu();
        return;
    }
    if (row == SIN_CURSE_ROW_BACK) {
        g_nativeMenu = NativeMenu::SpawnMenu();
        if (!g_nativeMenu.obj) g_forceSubsystem = 0;
        Log("[ffx-hooks] SinCurse: back to NativeMenu\n");
        return;
    }
    // Read-only rows: re-open SIN menu without state change
    g_sinMenu = SinCurse_SpawnMenu();
    if (!g_sinMenu.obj) g_nativeMenu = NativeMenu::SpawnMenu();
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

/* Spira Reforge Arena+ catalog v2 overlay (Fase 5).
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

/* Per-slot progress_flag pulled from the catalog (Fase 6).
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
static volatile LONG g_arenaPlusBattle7002TemplateReady = 0;
static volatile LONG g_arenaPlusBattle7002TemplateCtx = 0;
static volatile LONG g_arenaPlusBattle7002TemplateA2 = 0;
static volatile LONG g_arenaPlusBattle7002TemplateTick = 0;
static volatile LONG g_arenaPlusBattle7002TemplateCount = 0;
static uint32_t g_arenaPlusBattle7002TemplateStack[8] = {};

// g_FFX_MenuSubsystemActive (VA 0x13407E4): so spawnar com ele setado.
static bool NativeMenu_SubsystemLive() {
    if (!g_base) return false;
    return *reinterpret_cast<volatile int*>(g_base + (0x13407E4u - 0x400000u)) != 0;
}

static bool ArenaPlus_IsEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ENABLE_ARENA_PLUS") ||
           ModuleFlagEnabled("arena_plus.flag") ||
           ModuleFlagEnabled("config\\arena_plus.flag");
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
    return EnvFlagEnabled("FFXHOOKS_ARENAPLUS_UNLOCK_ALL") ||
           ModuleFlagEnabled("arena_plus_unlock_all.flag") ||
           ModuleFlagEnabled("config\\arena_plus_unlock_all.flag");
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

/* Arena+ Multi Dark Aeon spike (Fase 4 â€” RE doc:
   docs/reverse/FFX_ARENA_PLUS_CUSTOM_TOKEN_RESOLVER_HOOK_SPIKE.md).
   Default-OFF read-only detour on FFX_Field_ResolveEncounterToken@0x7828B0 that logs
   every (token -> result) call so we can measure real-world resolver traffic before
   spec'ing the custom-token redirect path. Safe to keep around: the hook never mutates
   the token nor the return value. */
static bool ArenaPlus_ResolverLogEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ENABLE_ARENA_PLUS_RESOLVER_LOG") ||
           ModuleFlagEnabled("arena_plus_resolver_log.flag") ||
           ModuleFlagEnabled("config\\arena_plus_resolver_log.flag");
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
    return EnvFlagEnabled("FFXHOOKS_ENABLE_ARENA_PLUS_VICTORY_HOOK") ||
           ModuleFlagEnabled("arena_plus_victory_hook.flag") ||
           ModuleFlagEnabled("config\\arena_plus_victory_hook.flag");
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

/* Arena+ catalog v2 reader (Fase 5). When this flag + JSON file are present, the catalog
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

// Fase 6: tier-lock state per Arena+ slot.
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
           ArenaPlus_LabRoutesEnabled() ||
           ArenaPlus_UnlockAllEnabled();
}

static bool ArenaPlus_ComboRouteAllowed(int combo) {
    return ArenaPlus_ComboRouteMapped(combo) && ArenaPlus_ComboBattlesEnabled();
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

static bool ArenaPlus_ReadDarkAeonDefeated(int index, uint8_t* rawByte, bool* defeated) {
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
    if (g_arenaPlusDiskDark.valid) {
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
    return okFfxed || okRuntime || g_arenaPlusDiskDark.valid;
}

static int ArenaPlus_MenuRowCount(ArenaPlusMenuKind kind) {
    switch (kind) {
    case ArenaPlusMenuKind::Hub: return ARENA_PLUS_HUB_ROW_COUNT;
    case ArenaPlusMenuKind::DarkRematch: return ARENA_DARK_FLAG_LEN + 1;
    case ArenaPlusMenuKind::AeonGauntlet: return ARENA_PLUS_PRESET_COMBO_COUNT + 1;
    case ArenaPlusMenuKind::CustomMix: return ARENA_PLUS_CUSTOM_MIX_COMBO_COUNT + 1;
    case ArenaPlusMenuKind::Ultra: return 2;
    default: return ARENA_PLUS_HUB_ROW_COUNT;
    }
}

static int ArenaPlus_SubMenuBackRow(ArenaPlusMenuKind kind) {
    switch (kind) {
    case ArenaPlusMenuKind::DarkRematch: return ARENA_DARK_FLAG_LEN;
    case ArenaPlusMenuKind::AeonGauntlet: return ARENA_PLUS_PRESET_COMBO_COUNT;
    case ArenaPlusMenuKind::CustomMix: return ARENA_PLUS_CUSTOM_MIX_COMBO_COUNT;
    case ArenaPlusMenuKind::Ultra: return 1;
    default: return -1;
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
    if (ArenaPlusComposePick_IsCustomMixCombo(combo) && mapped && comboEnabled &&
        ArenaPlusComposePick_IsEnabled()) {
        _snprintf_s(statusBuf, sizeof(statusBuf), _TRUNCATE,
            ArenaPlus_IsChargeGilEnabled() ? "PICK SUM OF BOSSES" : "PICK+FIGHT");
        status = statusBuf;
    } else if (mapped && comboEnabled && gilCost > 0) {
        _snprintf_s(statusBuf, sizeof(statusBuf), _TRUNCATE, "COST %dG", gilCost);
        status = statusBuf;
    }
    _snprintf_s(g_arenaPlusLabels[labelRow], ARENA_PLUS_LABEL_CAP, _TRUNCATE,
        "%-18s %s", kArenaPlusComboNames[combo], status);
}

static void ArenaPlus_BuildRowsForKind(ArenaPlusMenuKind kind) {
    ArenaPlus_RefreshDiskSaveDarkCache();
    g_arenaPlusActiveRowCount = ArenaPlus_MenuRowCount(kind);

    if (kind == ArenaPlusMenuKind::Ultra) {
        _snprintf_s(g_arenaPlusLabels[0], ARENA_PLUS_LABEL_CAP, _TRUNCATE, "Build + Launch");
        _snprintf_s(g_arenaPlusLabels[1], ARENA_PLUS_LABEL_CAP, _TRUNCATE, "Back");
        return;
    }
    if (kind == ArenaPlusMenuKind::Hub) {
        _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_HUB_ROW_SAFE], ARENA_PLUS_LABEL_CAP, _TRUNCATE,
            "RT2 Current Battle");
        NativeMenu::EncodeLabel("The battle calls â€” answer now",
            g_arenaPlusHubDescBytes[ARENA_PLUS_HUB_ROW_SAFE], ARENA_PLUS_LABEL_CAP);
        _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_HUB_ROW_DARK], ARENA_PLUS_LABEL_CAP, _TRUNCATE,
            "Dark Aeon Rematch");
        NativeMenu::EncodeLabel("Face the fallen guardians alone",
            g_arenaPlusHubDescBytes[ARENA_PLUS_HUB_ROW_DARK], ARENA_PLUS_LABEL_CAP);
        _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_HUB_ROW_GAUNTLET], ARENA_PLUS_LABEL_CAP, _TRUNCATE,
            "Aeon Gauntlet");
        NativeMenu::EncodeLabel("Trials where champions rise as one",
            g_arenaPlusHubDescBytes[ARENA_PLUS_HUB_ROW_GAUNTLET], ARENA_PLUS_LABEL_CAP);
        _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_HUB_ROW_MIX], ARENA_PLUS_LABEL_CAP, _TRUNCATE,
            "Custom Mix");
        NativeMenu::EncodeLabel("Forge the gauntlet of your choosing",
            g_arenaPlusHubDescBytes[ARENA_PLUS_HUB_ROW_MIX], ARENA_PLUS_LABEL_CAP);
        g_arenaPlusHubDescBytes[ARENA_PLUS_HUB_ROW_BACK][0] = 0;
        _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_HUB_ROW_ULTRA], ARENA_PLUS_LABEL_CAP, _TRUNCATE, "CustomMix Ultra");
    _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_HUB_ROW_BACK], ARENA_PLUS_LABEL_CAP, _TRUNCATE,
            "Back");
    } else if (kind == ArenaPlusMenuKind::DarkRematch) {
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
        _snprintf_s(g_arenaPlusLabels[ARENA_PLUS_CUSTOM_MIX_COMBO_COUNT], ARENA_PLUS_LABEL_CAP, _TRUNCATE, "Back");
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

static int __cdecl ArenaPlus_Draw(int obj) {
    using namespace NativeMenu;
    ++g_arenaPlusDrawCalls;
    const int F = g_arenaPlusDrawCalls;

    static unsigned char s_title[64], s_sub[64], s_foot[64];
    static bool s_enc = false;
    if (!s_enc) {
        EncodeLabel("Confirm Select   Cancel Back   F7 Close", s_foot, 64);
        s_enc = true;
    }

    const char* titleText = "Arena+";
    const char* subText = "Choose your trial upon the Calm Lands";
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
    const float vLeft = NX(0.271f), vTop = NY(0.215f), vWidth = NW(0.458f);
    const float vStep = NH(0.063f), vBarH = NH(0.056f);
    const float selLine = MenuBorderPx() * 0.45f;
    const float cursorOff = NW(0.020f);

    for (int r = 0; r < page; ++r) {
        const int row = top + r;
        if (row >= count || row >= g_arenaPlusActiveRowCount) break;
        unsigned int c0 = kMenuRowGlassTop;
        unsigned int c1 = kMenuRowGlassBot;
        const int backRow = ArenaPlus_SubMenuBackRow(g_arenaPlusMenuKind);

        if (g_arenaPlusMenuKind == ArenaPlusMenuKind::Hub) {
            if (row == ARENA_PLUS_HUB_ROW_SAFE) {
                c0 = 0xD02D5C42u;
                c1 = 0xD00D241Au;
            } else if (row >= ARENA_PLUS_HUB_ROW_DARK && row <= ARENA_PLUS_HUB_ROW_ULTRA) {
                c0 = 0xD04A3A72u;
                c1 = 0xD0182048u;
            } else if (row == ARENA_PLUS_HUB_ROW_BACK) {
                c0 = 0xC0222A34u;
                c1 = 0xC00A1018u;
            }
        } else if (row == backRow) {
            c0 = 0xC0222A34u;
            c1 = 0xC00A1018u;
        } else if (g_arenaPlusMenuKind == ArenaPlusMenuKind::DarkRematch) {
            const int dark = row;
            if (!g_arenaPlusDarkReadOk[dark]) {
                c0 = 0xD06B382Eu;
                c1 = 0xD0271110u;
            } else if (ArenaPlus_BossRouteAllowed(dark)) {
                if (ArenaPlus_BossDefeatedFlagSet(dark)) {
                    c0 = 0xD03F654Au;
                    c1 = 0xD0102A1Bu;
                } else {
                    c0 = 0xD04A4F88u;
                    c1 = 0xD017203Fu;
                }
            } else if (!ArenaPlus_BossRouteMapped(dark)) {
                c0 = 0xD05A4630u;
                c1 = 0xD0201710u;
            } else {
                c0 = 0xD01C2730u;
                c1 = 0xD0080D13u;
            }
        } else {
            const int combo = (g_arenaPlusMenuKind == ArenaPlusMenuKind::CustomMix)
                ? ArenaPlus_CustomMixComboIndex(row)
                : row;
            if (!ArenaPlus_ComboRouteMapped(combo)) {
                c0 = 0xD05A4630u;
                c1 = 0xD0201710u;
            } else if (ArenaPlus_ComboRouteAllowed(combo)) {
                c0 = 0xD05A3A72u;
                c1 = 0xD0241848u;
            } else {
                c0 = 0xD03A2838u;
                c1 = 0xD0141018u;
            }
        }

        const float vy = vTop + (float)r * vStep;
        if (row == sel) {
            c0 = ColorLerp(c0, kMenuNeonGreenHi, 0.26f + Osc01(F, 46) * 0.10f);
            c1 = ColorLerp(c1, kMenuNeonGreenLo, 0.22f);
        }
        DrawSolidRect(vLeft, vy, vWidth, vBarH, c0, c1);
        if (row == sel) {
            DrawSolidRect(vLeft, vy + vBarH - selLine, vWidth, selLine, kMenuNeonGreenLine, kMenuNeonGreenLineLo);
            DrawCursor(vLeft - cursorOff, vy + NH(0.001f));
        }
        const bool hubDescRow = (g_arenaPlusMenuKind == ArenaPlusMenuKind::Hub &&
            row >= ARENA_PLUS_HUB_ROW_SAFE && row <= ARENA_PLUS_HUB_ROW_MIX);
        DrawString(g_arenaPlusLabelBytes[row], vLeft + NW(0.015f), vy + NH(0.016f));
        if (hubDescRow && g_arenaPlusHubDescBytes[row][0] != 0) {
            DrawStringSub(g_arenaPlusHubDescBytes[row], vLeft + NW(0.224f), vy + NH(0.024f));
        }
    }

    const float fx = NX(0.047f), fy = NY(0.887f), fw = NW(0.906f), fh = NH(0.070f);
    DrawMenuGlassPanel(fx, fy, fw, fh, F, 1);
    DrawString(s_foot, NX(0.071f), NY(0.911f));
    return obj;
}

static int __cdecl ArenaPlus_InputCb(int obj) {
    const int dir = NativeMenu::PadDir();
    const int edge = NativeMenu::PadEdge();
    const int confirmEdge = edge & 0x20;
    const int cancelEdge = edge & 0x40;
    const bool confirmPressed = confirmEdge && !(g_arenaPlusLastConfirmEdge & 0x20);
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

    const bool confirmBlocked = g_arenaPlusInputCooldown > 0;
    if (!g_arenaPlusClosed) {
        if (confirmPressed) {
            if (!confirmBlocked) {
                NativeMenu::PlaySfx(1);
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

static void ArenaPlus_Ultra_Launch();  // fwd — defined after 781D60 functions

// === CustomMix Ultra: SpawnMenu + HandleConfirm (Jarvis-HOOK 2026-08-05) ===

static NativeMenu::Menu ArenaPlus_Ultra_SpawnMenu() {
    Log("[ffx-hooks] Ultra: spawn 2-row menu (Build+Launch, Back)\n");
    g_arenaPlusMenuKind = ArenaPlusMenuKind::Ultra;
    g_arenaPlusActiveRowCount = 2;
    _snprintf_s(g_arenaPlusLabels[0], ARENA_PLUS_LABEL_CAP, _TRUNCATE, "Build + Launch");
    _snprintf_s(g_arenaPlusLabels[1], ARENA_PLUS_LABEL_CAP, _TRUNCATE, "Back");
    int obj = NativeMenu::Alloc();
    if (!obj) { Log("[ffx-hooks] Ultra: Alloc FAILED\n"); return NativeMenu::Menu{ 0 }; }
    NativeMenu::WrW(obj, NativeMenu::O_COUNT,    2);
    NativeMenu::WrW(obj, NativeMenu::O_PAGE,     2);
    NativeMenu::WrW(obj, NativeMenu::O_TOP,      0);
    NativeMenu::WrW(obj, NativeMenu::O_SELECTED, 0);
    NativeMenu::WrB(obj, NativeMenu::O_SLOTS,    1);
    NativeMenu::WrB(obj, NativeMenu::O_CANCEL,   1);
    NativeMenu::WrB(obj, NativeMenu::O_GROUP62,  2);
    NativeMenu::WrB(obj, NativeMenu::O_GROUP63,  1);
    NativeMenu::WrP(obj, NativeMenu::O_ENTER,    (void*)0);
    NativeMenu::WrP(obj, NativeMenu::O_UPDATE,   (void*)(uintptr_t)&ArenaPlus_InputCb);
    NativeMenu::WrP(obj, NativeMenu::O_DRAW,     (void*)(uintptr_t)&ArenaPlus_Draw);
    NativeMenu::WrP(obj, NativeMenu::O_AUX,      (void*)(uintptr_t)&NativeMenu::OurAux);
    NativeMenu::WrP(obj, NativeMenu::O_VALIDATOR,(void*)0);
    NativeMenu::g_ourClosed = 0; NativeMenu::g_ourResult = 0;
    g_arenaPlusClosed = 0; g_arenaPlusResult = 0;
    g_arenaPlusInputCooldown = 10;
    g_arenaPlusLastConfirmEdge = NativeMenu::PadEdge() & 0x60;
    NativeMenu::Register(obj);
    Log("[ffx-hooks] Ultra: menu spawned obj=0x%08X\n", (unsigned)obj);
    return NativeMenu::Menu{ obj };
}

static void ArenaPlus_Ultra_HandleConfirm(int row) {
    Log("[ffx-hooks] Ultra: confirm row=%d\n", row);
    if (row == 0) {
        Log("[ffx-hooks] Ultra: Build+Launch selected\n");
        ArenaPlus_CloseMenu(g_arenaPlusMenu);
        ArenaPlus_Ultra_Launch();
        return;
    }
    // row 1 = Back to hub
    Log("[ffx-hooks] Ultra: Back -> hub\n");
    ArenaPlus_CloseMenu(g_arenaPlusMenu);
    g_arenaPlusMenu = ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::Hub);
    if (!g_arenaPlusMenu.obj) { g_forceSubsystem = 0; Log("[ffx-hooks] Ultra: hub reopen FAILED\n"); }
}

// === CustomMix Ultra: Launch (compose + deploy + 781D60) ===


static NativeMenu::Menu ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind kind) {
    g_arenaPlusMenuKind = kind;
    ArenaPlus_BuildRowsForKind(kind);

    // Ultra: simple 2-row list menu (Build+Launch, Back)
    if (kind == ArenaPlusMenuKind::Ultra) {
        int obj = NativeMenu::Alloc();
        if (!obj) return NativeMenu::Menu{ 0 };
        NativeMenu::WrW(obj, NativeMenu::O_COUNT, 2);
        NativeMenu::WrW(obj, NativeMenu::O_PAGE, 2);
        NativeMenu::WrW(obj, NativeMenu::O_TOP, 0);
        NativeMenu::WrW(obj, NativeMenu::O_SELECTED, 0);
        NativeMenu::WrB(obj, NativeMenu::O_SLOTS, 1);
        NativeMenu::WrB(obj, NativeMenu::O_CANCEL, 1);
        NativeMenu::WrB(obj, NativeMenu::O_GROUP62, 2);
        NativeMenu::WrB(obj, NativeMenu::O_GROUP63, 1);
        NativeMenu::WrP(obj, NativeMenu::O_ENTER, (void*)0);
        NativeMenu::WrP(obj, NativeMenu::O_UPDATE, (void*)(uintptr_t)&ArenaPlus_InputCb);
        NativeMenu::WrP(obj, NativeMenu::O_DRAW, (void*)(uintptr_t)&ArenaPlus_Draw);
        NativeMenu::WrP(obj, NativeMenu::O_AUX, (void*)(uintptr_t)&NativeMenu::OurAux);
        NativeMenu::WrP(obj, NativeMenu::O_VALIDATOR, (void*)0);
        NativeMenu::g_ourClosed = 0;
        NativeMenu::g_ourResult = 0;
        g_arenaPlusClosed = 0;
        g_arenaPlusResult = 0;
        g_arenaPlusInputCooldown = 10;
        g_arenaPlusLastConfirmEdge = NativeMenu::PadEdge() & 0x60;
        NativeMenu::Register(obj);
        return NativeMenu::Menu{ obj };
    }


    int obj = NativeMenu::Alloc();
    if (!obj) return NativeMenu::Menu{ 0 };

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
    NativeMenu::Register(obj);
    return NativeMenu::Menu{ obj };
}

static NativeMenu::Menu ArenaPlus_SpawnMenu() {
    return ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::Hub);
}

static NativeMenu::Menu ArenaPlus_ReopenMenu() {
    return ArenaPlus_SpawnMenuKind(g_arenaPlusMenuKind);
}

static void ArenaPlus_HandleMenuConfirm(int row) {
    const int backRow = ArenaPlus_SubMenuBackRow(g_arenaPlusMenuKind);

    // CustomMix Ultra sub-menu: Build+Launch or Back
    if (g_arenaPlusMenuKind == ArenaPlusMenuKind::Ultra) {
        ArenaPlus_Ultra_HandleConfirm(row);
        return;
    }

    if (g_arenaPlusMenuKind == ArenaPlusMenuKind::Hub) {
        if (row == ARENA_PLUS_HUB_ROW_SAFE) {
            const bool queued = ArenaPlus_LaunchSafeBattleFromPump();
            if (queued) {
                g_nativeHeldAction = -1;
                g_forceSubsystem = 0;
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
            g_arenaPlusMenu = ArenaPlus_Ultra_SpawnMenu();
            Log("[ffx-hooks] ArenaPlus: hub -> CustomMix Ultra\n");
            return;
        }
        if (row == ARENA_PLUS_HUB_ROW_BACK) {
            g_nativeMenu = NativeMenu::SpawnMenu();
            if (!g_nativeMenu.obj) g_forceSubsystem = 0;
            Log("[ffx-hooks] ArenaPlus: hub back to NativeMenu obj=0x%08X\n", static_cast<unsigned>(g_nativeMenu.obj));
            return;
        }
        return;
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
                g_forceSubsystem = 0;
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
    if (ArenaPlusComposePick_IsEnabled() && ArenaPlusComposePick_IsCustomMixCombo(combo)) {
        if (!ArenaPlusComposePick_Open(combo)) {
            Log("[ffx-hooks] ArenaPlus: compose pick open failed combo=%d; reopening Custom Mix\n", combo);
            g_arenaPlusMenu = ArenaPlus_SpawnMenuKind(ArenaPlusMenuKind::CustomMix);
        } else {
            Log("[ffx-hooks] ArenaPlus: compose pick opened combo=%d\n", combo);
        }
        return;
    }
    const bool queued = ArenaPlus_LaunchComboBattleFromPump(combo);
    if (queued) {
        g_nativeHeldAction = -1;
        g_forceSubsystem = 0;
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

/* Deferred file restore: after cross-map compose overwrites a scene bin (e.g. mcfr00_00.bin),
   restore the .spiraforge.bak after ~30s so random encounters don't find Dark Aeons. */
static char g_pendingRestoreDeployPath[MAX_PATH] = {};
static char g_pendingRestoreBakPath[MAX_PATH] = {};
static int g_pendingRestoreFrames = 0;

static void ArenaPlus_ArmDeferredFileRestore(const char* deployPath, int frames) {
    if (!deployPath || !deployPath[0]) return;
    lstrcpynA(g_pendingRestoreDeployPath, deployPath, MAX_PATH);
    _snprintf_s(g_pendingRestoreBakPath, _TRUNCATE, "%s.spiraforge.bak", deployPath);
    g_pendingRestoreFrames = frames;
    Log("[ffx-hooks] ArenaPlus: armed deferred restore for %s (%d frames)\n", deployPath, frames);
}

static void ArenaPlus_TickDeferredFileRestore() {
    if (g_pendingRestoreFrames <= 0 || !g_pendingRestoreDeployPath[0]) return;
    g_pendingRestoreFrames--;
    if (g_pendingRestoreFrames > 0) return;
    /* Timer expired â€” restore the backup over the composed bin. */
    DWORD bakAttr = GetFileAttributesA(g_pendingRestoreBakPath);
    if (bakAttr == INVALID_FILE_ATTRIBUTES) {
        Log("[ffx-hooks] ArenaPlus: deferred restore SKIP â€” no backup at %s\n", g_pendingRestoreBakPath);
    } else if (CopyFileA(g_pendingRestoreBakPath, g_pendingRestoreDeployPath, FALSE)) {
        Log("[ffx-hooks] ArenaPlus: deferred restore OK â€” %s restored from backup\n", g_pendingRestoreDeployPath);
    } else {
        Log("[ffx-hooks] ArenaPlus: deferred restore FAILED â€” CopyFile(%s -> %s) err=%u\n",
            g_pendingRestoreBakPath, g_pendingRestoreDeployPath, GetLastError());
    }
    g_pendingRestoreDeployPath[0] = '\0';
    g_pendingRestoreBakPath[0] = '\0';
}

/* Boot restore: sis aa sessao anterior fechou antes do restore deferido (~30s) rodar, o bin composto
   ficaria no mod-root para sempre (sobrepondo o canonico). No boot, restauramos do .spiraforge.bak
   (o compose NUNCA fica permanente — fix 2026-08-02). */
static bool ArenaPlus_ReadManifestBattleId(const char* manifestPath, char* out, size_t outSize) {
    if (!manifestPath || !out || outSize == 0) return false;
    char buf[16384] = {};
    FILE* f = nullptr;
    if (fopen_s(&f, manifestPath, "rb") != 0 || !f) return false;
    const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    const char* p = strstr(buf, "\"battle_id\"");
    if (!p) return false;
    p = strchr(p, ':');
    if (!p) return false;
    p = strchr(p, '"');
    if (!p) return false;
    ++p;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < outSize) out[i++] = *p++;
    out[i] = '\0';
    return i > 0;
}

static void ArenaPlus_RestorePendingComposeOnBoot() {
    char manifestPath[MAX_PATH * 2] = {};
    /* Paths do manifest real (mesma ordem do ResolveManifestPath do ComposePick):
       1) modules\compose_last.json — o caso real (o lab escreve relativo ao modulo) */
    {
        char modulePath[MAX_PATH] = {};
        if (g_module && GetModuleFileNameA(g_module, modulePath, sizeof(modulePath)) > 0) {
            char* slash = strrchr(modulePath, '\\');
            if (slash) {
                *(slash + 1) = '\0';
                _snprintf_s(manifestPath, sizeof(manifestPath), _TRUNCATE, "%scompose_last.json", modulePath);
                if (GetFileAttributesA(manifestPath) != INVALID_FILE_ATTRIBUTES) goto have_manifest;
            }
        }
    }
    /* 2) gameRoot\compose_last.json · 3) gameRoot\mods\Spira Reforge\arena\compose_last.json */
    {
        char gameRoot[MAX_PATH] = {};
        if (GameRootDirectoryPath(gameRoot, sizeof(gameRoot))) {
            _snprintf_s(manifestPath, sizeof(manifestPath), _TRUNCATE, "%scompose_last.json", gameRoot);
            if (GetFileAttributesA(manifestPath) != INVALID_FILE_ATTRIBUTES) goto have_manifest;
            _snprintf_s(manifestPath, sizeof(manifestPath), _TRUNCATE,
                "%smods\\Spira Reforge\\arena\\compose_last.json", gameRoot);
            if (GetFileAttributesA(manifestPath) != INVALID_FILE_ATTRIBUTES) goto have_manifest;
        }
    }
    return;
have_manifest:
    char battleId[64] = {};
    if (!ArenaPlus_ReadManifestBattleId(manifestPath, battleId, sizeof(battleId))) return;
    char modBtlRoot[MAX_PATH] = {};
    if (!ResolveModBtlRoot(modBtlRoot, sizeof(modBtlRoot))) return;
    char deployPath[MAX_PATH] = {};
    _snprintf_s(deployPath, _TRUNCATE, "%s\\%s\\%s.bin", modBtlRoot, battleId, battleId);
    char bakPath[MAX_PATH] = {};
    _snprintf_s(bakPath, _TRUNCATE, "%s.spiraforge.bak", deployPath);
    if (GetFileAttributesA(bakPath) == INVALID_FILE_ATTRIBUTES) return;
    if (CopyFileA(bakPath, deployPath, FALSE))
        Log("[ffx-hooks] ArenaPlus: boot restore OK — %s restaurado do backup (sessao anterior)\n", deployPath);
    else
        Log("[ffx-hooks] ArenaPlus: boot restore FAILED — CopyFile(%s -> %s) err=%u\n",
            bakPath, deployPath, GetLastError());
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

static bool ArenaPlus_ForceBattleDirect(int field, int group, int formation, int32_t* outRet, uint32_t* outErr) {
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

    __try {
        *script0 = 0u;
        *script1 = 0u;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!err) err = GetExceptionCode();
    }

    if (outRet) *outRet = ret;
    if (outErr) *outErr = err;
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

static bool ArenaPlus_LaunchBattle7002Exact(const ArenaPlusBossRoute& route, int dark, int32_t* outRet, uint32_t* outErr) {
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
    Log("[ffx-hooks] ArenaPlus: Battle.7002 exact try row=%d name=%s battleId=%s token=0x%08X transition=%u stack0=0x%08X stack1=0x%08X swap=%d\n",
        dark,
        (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
        route.battleId ? route.battleId : "?",
        route.battleToken,
        route.transition,
        stack[0],
        stack[1],
        swapArgs ? 1 : 0);

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
    return ok && ret == -1;
}

static bool ArenaPlus_LaunchBattle781D60Request(const ArenaPlusBossRoute& route, int dark, int32_t* outRet, uint32_t* outErr) {
    if (outRet) *outRet = 0;
    if (outErr) *outErr = 0;
    if (!g_base || route.battleToken == 0 || !ArenaPlus_DirectRequest781D60Enabled()) return false;

    typedef int (__cdecl* FnBattleRequest781D60)(int, char, char);
    ArenaPlus_LogBattleQueueState("direct-request pre", &route, dark);
    ArenaPlus_CallCommonSetBattleFlags0200("direct-request prelaunch", route, dark);
    ArenaPlus_LogBattleQueueState("direct-request after-flags", &route, dark);

    int32_t ret = 0;
    uint32_t err = 0;
    bool ok = false;
    const char transition = static_cast<char>(route.transition & 0xFFu);
    Log("[ffx-hooks] ArenaPlus: direct request 781D60 try row=%d name=%s battleId=%s token=0x%08X args=[token,1,%u]\n",
        dark,
        (dark >= 0 && dark < ARENA_DARK_FLAG_LEN) ? kArenaPlusDarkNames[dark] : "?",
        route.battleId ? route.battleId : "?",
        route.battleToken,
        static_cast<unsigned>(route.transition & 0xFFu));
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

    ArenaPlus_LogBattleQueueState("direct-request post", &route, dark);
    if (outRet) *outRet = ret;
    if (outErr) *outErr = err;
    const bool queueArmed = ArenaPlus_IsBattleQueueArmed();
    Log("[ffx-hooks] ArenaPlus: direct request 781D60 result row=%d battleId=%s -> ok=%d ret=0x%08X %s err=0x%08X queueArmed=%d\n",
        dark,
        route.battleId ? route.battleId : "?",
        ok ? 1 : 0,
        static_cast<unsigned>(ret),
        LabForceRetMeaning(ret),
        err,
        queueArmed ? 1 : 0);
    return ok && ret == -1 && queueArmed;
}

// === CustomMix Ultra: Launch (compose + deploy + 781D60 carrier) ===

static void ArenaPlus_Ultra_Launch() {
    Log("[ffx-hooks] Ultra: ====== LAUNCH START ======\n");
    const char* ultraJson =
        "{\"schema\":\"ultra-v1\",\"name\":\"F7_Ultra\",\"monsters\":["
        "{\"monster_id\":44238,\"label\":\"Dark Valefor\"},"
        "{\"monster_id\":44239,\"label\":\"Dark Ifrit\"},"
        "{\"monster_id\":44240,\"label\":\"Dark Ixion\"}],\"music_track\":145}";
    Log("[ffx-hooks] Ultra: manifest (Valefor+Ifrit+Ixion)\n");

    char gameRoot[MAX_PATH] = {};
    {
        char modRoot[MAX_PATH] = {};
        if (!ResolveModBtlRoot(modRoot, sizeof(modRoot))) {
            Log("[ffx-hooks] Ultra: FAILED — mod root\n"); return;
        }
        char* p = modRoot + strlen(modRoot);
        for (int up = 0; up < 5 && p > modRoot; up++) {
            while (p > modRoot && *p != '\\') p--;
            if (p > modRoot) *p = 0;
        }
        if (!modRoot[0]) { Log("[ffx-hooks] Ultra: FAILED — empty root\n"); return; }
        _snprintf_s(gameRoot, sizeof(gameRoot), _TRUNCATE, "%s", modRoot);
    }
    Log("[ffx-hooks] Ultra: gameRoot=%s\n", gameRoot);

    char manifestPath[MAX_PATH] = {};
    _snprintf_s(manifestPath, sizeof(manifestPath), _TRUNCATE, "%s\\modules\\config\\ultra_manifest.json", gameRoot);
    FILE* mf = nullptr;
    if (fopen_s(&mf, manifestPath, "wb") != 0 || !mf) {
        Log("[ffx-hooks] Ultra: FAILED — manifest write err=%d\n", errno); return;
    }
    fwrite(ultraJson, 1, strlen(ultraJson), mf); fclose(mf);
    Log("[ffx-hooks] Ultra: manifest written (%zu B)\n", strlen(ultraJson));

    char cmdLine[2048] = {};
    _snprintf_s(cmdLine, sizeof(cmdLine), _TRUNCATE,
        "\"%s\\data\\modules\\tools\\ArenaMultiBossLab\\ArenaMultiBossLab.exe\" --ultra \"%s\""
        " --vanilla-root \"D:\\FFX Extracted\\FFX\\ffx_ps2\\ffx\\master\\jppc\\battle\\btl\""
        " --mod-root \"%s\\data\\mods\\ffx_ps2\\ffx\\master\\jppc\\battle\\btl\"",
        gameRoot, manifestPath, gameRoot);
    Log("[ffx-hooks] Ultra: spawning lab...\n");

    STARTUPINFOA si = { sizeof(si) }; PROCESS_INFORMATION pi = {};
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    if (!CreateProcessA(nullptr, cmdLine, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        Log("[ffx-hooks] Ultra: FAILED — CreateProcess err=%lu\n", GetLastError()); return;
    }
    Log("[ffx-hooks] Ultra: lab pid=%lu (20s timeout)\n", (unsigned long)pi.dwProcessId);
    DWORD waitRc = WaitForSingleObject(pi.hProcess, 20000);
    DWORD exitCode = 1; GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    Log("[ffx-hooks] Ultra: lab done wait=%lu exit=%lu\n", (unsigned long)waitRc, (unsigned long)exitCode);
    if (waitRc != WAIT_OBJECT_0 || exitCode != 0) {
        Log("[ffx-hooks] Ultra: FAILED — lab exit=%lu\n", (unsigned long)exitCode); return;
    }

    char outBin[MAX_PATH] = {};
    _snprintf_s(outBin, sizeof(outBin), _TRUNCATE,
        "%s\\data\\mods\\ffx_ps2\\ffx\\master\\jppc\\battle\\btl\\F7_Ultra\\F7_Ultra.bin", gameRoot);
    if (GetFileAttributesA(outBin) == INVALID_FILE_ATTRIBUTES) {
        Log("[ffx-hooks] Ultra: FAILED — bin missing: %s\n", outBin); return;
    }
    Log("[ffx-hooks] Ultra: output bin OK: %s\n", outBin);

    const char* battleId = "F7_Ultra";
    ArenaPlus_PinEncounterName(battleId, false);
    Log("[ffx-hooks] Ultra: name pinned -> %s\n", battleId);
    FfxHooks::F7_SetSkipForceCapture(true);

    ArenaPlusBossRoute route = { 2, 0, 0, 0x00F000F0u, 2, "F7_Ultra", "Ultra Besaid" };
    int32_t rret = 0; uint32_t rerr = 0;
    bool launched = ArenaPlus_LaunchBattle781D60Request(route, -1, &rret, &rerr);
    Log("[ffx-hooks] Ultra: 781D60=%s ret=%d err=0x%08X\n", launched ? "OK" : "FAIL", (int)rret, (unsigned)rerr);
    Log(launched ? "[ffx-hooks] Ultra: ====== LAUNCHED ======\n" : "[ffx-hooks] Ultra: ====== FAILED ======\n");
}

static bool ArenaPlus_LaunchBattle7002Template(const ArenaPlusBossRoute& route, int dark, int32_t* outRet, uint32_t* outErr) {
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
    return ok && ret == -1 && ArenaPlus_TemplateReplayClaimSuccessEnabled();
}

static void ArenaPlus_ClearPendingBattle7002Override() {
    InterlockedExchange(&g_arenaPlusPendingBattle7002, 0);
    InterlockedExchange(&g_arenaPlusPendingBattleToken, 0);
    InterlockedExchange(&g_arenaPlusPendingTransition, 0);
    InterlockedExchange(&g_arenaPlusPendingDark, -1);
    InterlockedExchange(&g_arenaPlusPendingGilCost, 0);
    InterlockedExchange(&g_arenaPlusPendingExpireTick, 0);
}

static bool ArenaPlus_ArmBattle7002Override(const ArenaPlusBossRoute& route, int dark, uint32_t expireMs) {
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
    const bool ok = ArenaPlus_ForceBattleDirect(field, group, formation, &ret, &err);
    Log("[ffx-hooks] ArenaPlus: launch safe battle route=%s field=%d group=%d formation=%d -> ok=%d ret=%d %s err=0x%08X\n",
        source, field, group, formation, ok ? 1 : 0, ret, LabForceRetMeaning(ret), err);
    return ok && ret == -1;
}

static bool ArenaPlus_LaunchBossBattleFromPump(int dark) {
    FfxHooks::F7_SetSkipForceCapture(true);  // prevent Force Battle from capturing Dark Aeons/Arena/Custom Mix
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
    const bool directRequestOk = ArenaPlus_LaunchBattle781D60Request(route, dark, &ret, &err);
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

    const bool templateOk = ArenaPlus_LaunchBattle7002Template(route, dark, &ret, &err);
    if (templateOk) return true;

    if (!ArenaPlus_DirectBattle7002Enabled()) {
        const bool autoCarrier = ArenaPlus_AutoCarrierEnabled();
        const bool unprovenDirectAttempt = (ret == -1 && err == 0) &&
            (ArenaPlus_DirectRequest781D60Enabled() || ArenaPlus_TemplateReplayEnabled());
        const uint32_t ttlMs = autoCarrier
            ? ArenaPlus_AutoCarrierTtlMs()
            : (unprovenDirectAttempt ? ArenaPlus_UnprovenDirectFallbackTtlMs() : 0);
        const bool armed = ArenaPlus_ArmBattle7002Override(route, dark, ttlMs);
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
    const bool exactOk = ArenaPlus_LaunchBattle7002Exact(route, dark, &ret, &err);
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
    const bool ok = ArenaPlus_ForceBattleDirect(route.field, route.group, route.formation, &ret, &err);
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
    FfxHooks::F7_SetSkipForceCapture(true);  // prevent Force Battle from capturing Custom Mix/combos
    if (!ArenaPlus_ComboRouteMapped(combo)) {
        Log("[ffx-hooks] ArenaPlus: combo route missing row=%d name=%s\n",
            combo,
            (combo >= 0 && combo < ARENA_PLUS_COMBO_COUNT) ? kArenaPlusComboNames[combo] : "?");
        return false;
    }
    if (!ArenaPlus_ComboBattlesEnabled()) {
        Log("[ffx-hooks] ArenaPlus: combo battles disabled row=%d name=%s (need arena_plus_combo_battles.flag or lab/unlock_all)\n",
            combo,
            kArenaPlusComboNames[combo]);
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
    /* Deferred restore do bin composto (caminho nao-cross-map): ~30s depois, restaura o canonico do .bak
       para os encontros aleatorios nao acharem os Dark Aeons (mesmo contrato do caminho 781D60 — fix 2026-08-02). */
    if (scenarioRouteApplied && scenarioBackdropBattleId[0] && !scenarioCrossMap) {
        char modBtlRoot[MAX_PATH] = {};
        if (ResolveModBtlRoot(modBtlRoot, sizeof(modBtlRoot))) {
            char deployPath[MAX_PATH] = {};
            _snprintf_s(deployPath, _TRUNCATE, "%s\\%s\\%s.bin",
                modBtlRoot, scenarioBackdropBattleId, scenarioBackdropBattleId);
            ArenaPlus_ArmDeferredFileRestore(deployPath, 1800);
        }
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
            &ret,
            &err);
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
            {
                char modBtlRoot[MAX_PATH] = {};
                if (ResolveModBtlRoot(modBtlRoot, sizeof(modBtlRoot))) {
                    char deployPath[MAX_PATH] = {};
                    _snprintf_s(deployPath, _TRUNCATE, "%s\\%s\\%s.bin",
                        modBtlRoot, scenarioBackdropBattleId, scenarioBackdropBattleId);
                    ArenaPlus_ArmDeferredFileRestore(deployPath, 1800); /* ~30s at 60fps */
                }
            }
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
    const bool directRequestOk = ArenaPlus_LaunchBattle781D60Request(route, comboRowId, &ret, &err);
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

    const bool templateOk = ArenaPlus_LaunchBattle7002Template(route, comboRowId, &ret, &err);
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
        const bool armed = ArenaPlus_ArmBattle7002Override(route, comboRowId, ttlMs);
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
    const bool exactOk = ArenaPlus_LaunchBattle7002Exact(route, comboRowId, &ret, &err);
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
    const bool ok = ArenaPlus_ForceBattleDirect(route.field, route.group, route.formation, &ret, &err);
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
        Log("[ffx-hooks] ArenaTrace: g_base nao resolvido - abort\n");
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

// Bridge EDGE: liga cada linha confirmada numa acao da Aurora. SO chama PhotoMode::*.
/* â”€â”€ F7 In-Live submenu: declarations (definitions in sections below) â”€â”€â”€â”€â”€â”€â”€â”€ */
enum F7MenuKind { F7_MENU_MUSIC = 0, F7_MENU_FORCE, F7_MENU_DIFF, F7_MENU_AI };
enum F7RowType { F7RT_INFO = 0, F7RT_TOGGLE, F7RT_STEPPER, F7RT_ACTION, F7RT_BACK };

struct F7SubRow {
    const char* label;
    F7RowType   type;
    int         min, max, step;
};

static NativeMenu::Menu g_f7Menu         = { 0 };
static volatile LONG    g_f7WantOpenKind = -1;
static int              g_f7MenuKind     = F7_MENU_MUSIC;
static int              g_f7Vals[16]     = {};
static F7SubRow         g_f7Rows[16]     = {};
static int              g_f7RowCount     = 0;
static unsigned char    g_f7Labels[16][64]    = {};
static unsigned char    g_f7SubLabels[16][48] = {};
static int              g_f7ConfirmTimer = 0;
static int              g_f7LastEdge     = 0;
static int              g_f7ClosedFlag   = 0;
static int              g_f7ConfirmRow   = -1;
static float            g_f7EasedRowY    = -1.0f;
static int              g_f7DrawCalls    = 0;
static int              g_f7DiffPresetIdx = -1;   // DIFF: preset ativo (-1 = custom)
static char             g_f7FmtBuf[48]   = {};

static NativeMenu::Menu F7Sub_SpawnMenu(int kind);
static NativeMenu::Poll F7Sub_PollMenu(const NativeMenu::Menu& m);
static void F7Sub_CloseMenu();
static void F7Sub_HandleConfirm(int row);
static void F7_LeverApply(NativeMenu::ActionId act, int val);   // KEYSTONE B (2026-08-02): fwd — acoes diretas (OnEdge)
static void F7_CommitValsToConfig();   // fwd (def. abaixo) â€” usada pelo F7Sub_InputCb

static void NativeMenu_OnEdge(NativeMenu::ActionId a) {
    using namespace NativeMenu;
    // Lane IFRIT: menu repurposed as in-live editor. For now ONLY the NAMES are in the menu
    // fiada ainda (ver docs/ai/IFRIT_F7_INLIVE_EDITOR_ROADMAP_2026-06-10.md). PhotoMode segue ENCERRADO (nao chamar).
    switch (a) {
        case ACT_BATTLE_CHEATS: {
            // KEYSTONE B (2026-08-02): direct actions WITHOUT submenu
            const int row = NativeMenu::g_ourResult;
            const int val = (row >= 0 && row < NativeMenu::kRowCount) ? NativeMenu::g_rowValue[row] : 0;
            F7_LeverApply(a, val);
            break;
        }
        case ACT_AI_SWAP:
            Log("[ffx-hooks] NativeMenu: MONSTER AI SWAP selected, opening F7 ai-swap submenu\n");
            InterlockedExchange(&g_f7WantOpenKind, F7_MENU_AI);
            break;
        case ACT_EXIT: break;   // o caller fecha o objeto (CloseMenu); nada a fazer aqui
        case ACT_ARENA:
            if (!ArenaPlus_IsEnabled()) {
                Log("[ffx-hooks] ArenaPlus: disabled (set FFXHOOKS_ENABLE_ARENA_PLUS=1 ou crie modules\\arena_plus.flag)\n");
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
            Log("[ffx-hooks] NativeMenu: item %d selecionado (nao-fiado ainda; so o nome)\n", (int)a);
            break;
    }
}
// Bridge HELD: entra no sub-modo "segurar" (o pump aplica por frame em NativeMenu_TickHeld).
static void NativeMenu_OnHeldEnter(NativeMenu::ActionId a) { g_nativeHeldAction = (int)a; }

// Sub-modo HELD por frame: le deltas (teclado por ora) + chama a acao continua da Aurora.
// (Pad nativo: trocar GetAsyncKeyState pelos bits de 0x8BE440 num passo futuro.)
static void NativeMenu_TickHeld() {
    // Lane IFRIT: nenhuma row e HELD por ora (todas EDGE) -> dormente. Stub mantido p/ quando uma acao continua
    // (ex.: Camera/Actor nudge, que precisa de re-poke por frame) for fiada. ESC sai do sub-modo por seguranca.
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { g_nativeHeldAction = -1; g_forceSubsystem = 0; }
}

// Runs on PRESENT (every frame, ALL contexts -- field/dialogue too).
// F7 liga/desliga o "force". Enquanto ligado, reescreve dword_13407E4=1 -> o tick do field passa a CHAMAR
// o pump no field -> nosso menu desenha/ticka SEM menu do jogo concorrente. (O field zera o gate sozinho a
// cada frame; por isso reescrevemos todo frame.) Prova: forcar o gate por ~frames nao crashou (probe RT2).
static void NativeMenu_PresentTick() {
    if (!g_base) return;
    static bool s_hk = false;
    static DWORD s_hkLastEdge = 0;
    const bool down = (GetAsyncKeyState(g_nativeMenuHotkey) & 0x8000) != 0;
    const bool edge = down && !s_hk; s_hk = down;
    if (edge) {
        // FIX 2026-08-02 (user RT2): F7 toggle debounce (250ms)
        // o F7 (ou key stuck) alterna o menu a cada frame = flicker. Mesmo padrao do EKey.
        const DWORD now = GetTickCount();
        if (now - s_hkLastEdge >= 250) {
            s_hkLastEdge = now;
        if (!g_forceSubsystem) {
            // BLOQUEIO anti-double-input: se o subsistema JA esta ativo sem ser pela nossa forca, ha um MENU DO
            // JOGO aberto -> NAO abrir o nosso em cima (senao os dois navegam = caos). Abre so com o jogo "limpo".
            const bool gameMenuOpen =
                *reinterpret_cast<volatile int*>(g_base + (0x13407E4u - 0x400000u)) != 0;
            if (gameMenuOpen) {
                Log("[ffx-hooks] NativeMenu: F7 BLOQUEADO (menu do jogo ja aberto) - feche o menu do jogo primeiro\n");
            } else {
                g_forceSubsystem = 1; g_nativeWantSpawn = 1;   // 1o F7 num contexto limpo: liga force + pede spawn
            }
        } else {
            g_nativeWantClose = 1;                              // 2o F7: pede close
        }
        }
    }
    if (g_forceSubsystem) {
        *reinterpret_cast<volatile int*>(g_base + (0x13407E4u - 0x400000u)) = 1;  // FORCA o gate -> pump roda no field
        // RENDER-ON-TOP: o pump JA desenha nosso menu (OurDraw roda), mas o render do field/batalha COBRE.
        // Redesenhamos AQUI no Present (depois do frame do jogo renderizar) p/ ficar POR CIMA. (Experimento.)
        if (g_nativeMenu.obj) { __try { NativeMenu::OurDraw(g_nativeMenu.obj); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
        if (g_arenaPlusMenu.obj) { __try { ArenaPlus_Draw(g_arenaPlusMenu.obj); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    }
}

// O detour do pump: MAIN THREAD, 1x/frame com o subsistema vivo.
// KEYSTONE B (2026-08-02): the F7 levers — immediate RAM effect (roadmap IFRIT_F7_INLIVE).
static void F7_LeverApply(NativeMenu::ActionId act, int val) {
    using namespace FfxHooks;
    switch (act) {
        case NativeMenu::ACT_MUSIC: {
            F7_MusicPreview(val);   // toca a faixa agora (override + soundcmd, sem persist)
            Log("[ffx-hooks] F7 lever: music track=%d (preview)\n", val);
            break;
        }
        case NativeMenu::ACT_BATTLE_CHEATS: {   // Debug Invincible 0xD2A8F8+1 (byte)
            if (g_base)
                *reinterpret_cast<volatile uint8_t*>(g_base + (0xD2A8F8u - 0x400000u + 1u)) = val ? 1 : 0;
            Log("[ffx-hooks] F7 lever: debug invincible=%d\n", val ? 1 : 0);
            break;
        }
        case NativeMenu::ACT_FORCE_BATTLE:      // force com o field do stepper (F7 tick-based)
            F7_ForceFieldBattle(val, 0);
            Log("[ffx-hooks] F7 lever: force battle field=%d group=0 (queued)\n", val);
            break;
        case NativeMenu::ACT_AI_SWAP: {         // opcode byte-local +0xF78 do ator atual (battle)
            if (g_base) {
                uint32_t* list = reinterpret_cast<uint32_t*>(g_base + (0xD37634u - 0x400000u));
                uint32_t enemyList = *list;
                if (enemyList) {
                    for (int s = 0; s < 8; s++) {
                        uint8_t* entry = reinterpret_cast<uint8_t*>(enemyList + 0xF90u * s);
                        if (*(uint16_t*)(entry + 0x0E) != 0xFFFF) { entry[0xF78] = (uint8_t)val; break; }
                    }
                }
            }
            Log("[ffx-hooks] F7 lever: ai opcode=%d (slot 0)\n", val);
            break;
        }
        case NativeMenu::ACT_DIFFICULTY:        // lever real (2026-08-02): seta o preset global 0..5
            F7_SetDifficultyLevel(val);         // aplica no proximo battle start (auto-apply do F7InLive)
            Log("[ffx-hooks] F7 lever: difficulty=%d (auto-apply on battle start)\n", val);
            break;
        default:
            break;
    }
}

static int __cdecl NativeMenu_PumpHook(unsigned int a1) {
    // RENDER-VISIBILITY (RE re-menu-render-visibility, alta confianca): ANTES do pump, nao kill-switchar o 2D
    // (o enqueue+flush do batch do menu 2D rodam DENTRO do pump; 0x12FB790=hard kill-switch, 0x12FB798=pula upload).
    if (g_forceSubsystem && g_base) {
        *reinterpret_cast<volatile int*>(g_base + (0x12FB790u - 0x400000u)) = 0;  // g_Render2D_Disabled = 0
        *reinterpret_cast<volatile int*>(g_base + (0x12FB798u - 0x400000u)) = 0;  // upload do batch nao pulado
    }
    // 1) roda o pump ORIGINAL (jogo atualiza+desenha; OurDraw ENFILEIRA os quads do menu no batch)
    int r = reinterpret_cast<int(__cdecl*)(unsigned int)>(g_nativeMenuPumpTramp)(a1);
    // DEPOIS do pump (ANTES do check 0x820de2 do field tick sub_820C00): manter unk_13407E4=1 -> o jogo PULA o
    // render 3D (igual ao menu LEGITIMO) -> nosso menu (ja desenhado pelo pump) fica POR CIMA em vez de coberto.
    if (g_forceSubsystem && g_base)
        *reinterpret_cast<volatile int*>(g_base + (0x13407E4u - 0x400000u)) = 1;
    // 2) nosso trabalho (reentrancia + SEH)
    if (InterlockedCompareExchange(&g_nativeMenuInHook, 1, 0) != 0) return r;
    __try {
        ArenaTrace_MenuPoolTick("NativeMenu_PumpHook");
        ArenaPlus_TickScenarioBackdropPending();
        ArenaPlus_TickEncounterPinPending();
        ArenaPlus_TickDeferredFileRestore();
        FfxHooks::F7_TickMainThread();   // F7 In-Live: auto-apply difficulty + music battle (main thread)
        FfxHooks::F7AiSwap_Tick();        // F7 AI Swap: status-on-ability (gate f7_aiswap.flag)
        // F7 agora roda no Present (NativeMenu_PresentTick), que FORCA o gate; aqui so CONSUMIMOS os pedidos.
        // (O pump so chega aqui porque o gate foi forcado -> agora roda ate no field, sem menu concorrente.)
        if (g_nativeWantSpawn) {
            g_nativeWantSpawn = 0;
            // estamos DENTRO do pump -> ele rodou -> o subsistema esta vivo agora; aceita force como prova
            if (g_nativeMenu.obj == 0 && g_arenaPlusMenu.obj == 0 && g_sinMenu.obj == 0 && g_f7Menu.obj == 0 && !ArenaPlusComposePick_IsActive() &&
                g_nativeHeldAction < 0 && (NativeMenu_SubsystemLive() || g_forceSubsystem)) {
                g_nativeMenu = NativeMenu::SpawnMenu();
                Log("[ffx-hooks] NativeMenu open obj=0x%08X (force-gate)\n", (unsigned)g_nativeMenu.obj);
            }
        }
        if (g_nativeWantClose) {
            g_nativeWantClose = 0;
            NativeMenu::CloseMenu(g_nativeMenu); g_nativeHeldAction = -1;
            ArenaPlus_CloseMenu(g_arenaPlusMenu);
            SinCurse_CloseMenu();
            F7Sub_CloseMenu();
            ArenaPlusComposePick_Close();
            InterlockedExchange(&g_arenaPlusWantOpen, 0);
            InterlockedExchange(&g_sinWantOpen, 0);
            InterlockedExchange(&g_f7WantOpenKind, -1);
            g_forceSubsystem = 0;   // para de forcar -> field volta ao normal
            if (g_base) *reinterpret_cast<volatile int*>(g_base + (0x13407E4u - 0x400000u)) = 0;  // zera gate direto (fix tela preta)
            Log("[ffx-hooks] NativeMenu close (force-gate off)\n");
        }
        if (!g_nativeMenu.obj && !g_arenaPlusMenu.obj && !g_sinMenu.obj && !g_f7Menu.obj && !ArenaPlusComposePick_IsActive() &&
            InterlockedCompareExchange(&g_arenaNpcInNowWhat, 0, 0) == 0) {
            LONG delay = InterlockedCompareExchange(&g_arenaNpcOpenDelay, 0, 0);
            if (delay > 0) {
                InterlockedDecrement(&g_arenaNpcOpenDelay);
            } else if (InterlockedCompareExchange(&g_arenaNpcPendingOpen, 0, 0) != 0) {
                InterlockedExchange(&g_arenaNpcPendingOpen, 0);
                ArenaPlus_RequestOpen("NpcNowWhat");
            }
        }
        if (!g_nativeMenu.obj && !g_arenaPlusMenu.obj && !g_sinMenu.obj && !ArenaPlusComposePick_IsActive() &&
            InterlockedCompareExchange(&g_arenaPlusWantOpen, 0, 0) != 0 &&
            InterlockedCompareExchange(&g_arenaNpcInNowWhat, 0, 0) == 0) {
            InterlockedExchange(&g_arenaPlusWantOpen, 0);
            if (!ArenaPlus_OpenMenuFromRequest()) {
                g_nativeMenu = NativeMenu::SpawnMenu();
                if (!g_nativeMenu.obj) g_forceSubsystem = 0;
            }
        }
        if (g_nativeHeldAction >= 0) {
            NativeMenu_TickHeld();
        } else if (ArenaPlusComposePick_IsActive()) {
            ArenaPlusComposePick_Tick();
            const ArenaPlusComposePollResult cp = ArenaPlusComposePick_PollMenu();
            if (cp.what == ArenaPlusComposePollKind::Launch) {
                ArenaPlusComposePick_Close();
                // FIX 2026-08-02 (crash do compose): desliga o force-gate ANTES do MsBattleEncountExe.
                // O launch rodava com 0x13407E4=1 (modo menu) -> o battle start crashava no UpdateWindowTitle.
                g_forceSubsystem = 0;
                if (g_base) *reinterpret_cast<volatile int*>(g_base + (0x13407E4u - 0x400000u)) = 0;  // zera o gate direto
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
                g_forceSubsystem = 0;   // FIX 2026-08-02: mesma correcao do Launch (relaunch limpo)
                if (g_base) *reinterpret_cast<volatile int*>(g_base + (0x13407E4u - 0x400000u)) = 0;
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
                if (!g_arenaPlusMenu.obj) g_forceSubsystem = 0;
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
                g_nativeMenu = NativeMenu::SpawnMenu();
                if (!g_nativeMenu.obj) g_forceSubsystem = 0;
                Log("[ffx-hooks] ArenaPlus: cancel/back to NativeMenu obj=0x%08X\n", static_cast<unsigned>(g_nativeMenu.obj));
            }
        } else if (g_sinMenu.obj) {
            NativeMenu::Poll p = SinCurse_PollMenu(g_sinMenu);
            if (p.what == NativeMenu::POLL_CONFIRM) {
                const int row = p.row;
                SinCurse_CloseMenu();
                SinCurse_HandleConfirm(row);
            } else if (p.what == NativeMenu::POLL_CANCEL) {
                SinCurse_CloseMenu();
                g_nativeMenu = NativeMenu::SpawnMenu();
                if (!g_nativeMenu.obj) g_forceSubsystem = 0;
                Log("[ffx-hooks] SinCurse: cancel/back to NativeMenu\n");
            }
        } else if (g_f7Menu.obj) {
            NativeMenu::Poll p = F7Sub_PollMenu(g_f7Menu);
            if (p.what == NativeMenu::POLL_CONFIRM) {
                const int row = p.row;
                F7Sub_CloseMenu();
                F7Sub_HandleConfirm(row);
                if (row >= 0) {
                    if (g_f7MenuKind == F7_MENU_FORCE && row == 0) {
                        g_f7Menu = F7Sub_SpawnMenu(F7_MENU_FORCE);   // Force: mantem aberto p/ repetir
                        if (!g_f7Menu.obj) g_forceSubsystem = 0;
                    } else {
                        g_nativeWantSpawn = 1;   // volta ao hub pelo caminho do pump (fix tela preta 2026-08-02)
                    }
                }
            } else if (p.what == NativeMenu::POLL_CANCEL) {
                F7Sub_CloseMenu();
                g_nativeWantSpawn = 1;   // fix tela preta 2026-08-02 (caminho provado do pump)
            }
        } else if (g_nativeMenu.obj) {
            // WHY (2026-08-02, RT2): this is the CORRECT confirm flow of the native menu — o pump
            // POLLA o menu e, no confirm, faz CloseMenu + DispatchConfirm (que chama o NativeMenu_OnEdge
            // -> abre submenus/reabre/force-off). Um wire anterior consumia o g_ourClosed ANTES deste
            // block and "stole" o confirm (NOTHING entered — submenus never opened). DO NOT re-introduce
            // consumo de g_ourClosed aqui.
            NativeMenu::Poll p = NativeMenu::PollMenu(g_nativeMenu);
            if (p.what == NativeMenu::POLL_CONFIRM) {
                const bool inRange = (p.row >= 0 && p.row < NativeMenu::kRowCount);
                const NativeMenu::ActionId act = inRange ? NativeMenu::g_rows[p.row].action : NativeMenu::ACT_EXIT;
                const bool held = inRange && (NativeMenu::g_rows[p.row].kind == NativeMenu::HELD);
                NativeMenu::CloseMenu(g_nativeMenu);     // o input generico fecha no confirm (+66=1)
                NativeMenu::DispatchConfirm(p.row);      // chama o bridge (edge/held)
                const bool arenaRequested = InterlockedExchange(&g_arenaPlusWantOpen, 0) != 0;
                if (arenaRequested) {
                    if (!ArenaPlus_OpenMenuFromRequest()) {
                        g_nativeMenu = NativeMenu::SpawnMenu();
                        if (!g_nativeMenu.obj) g_forceSubsystem = 0;
                    }
                } else {
                    const bool sinRequested = InterlockedExchange(&g_sinWantOpen, 0) != 0;
                    if (sinRequested) {
                        g_sinMenu = SinCurse_SpawnMenu();
                        if (!g_sinMenu.obj) {
                            g_nativeMenu = NativeMenu::SpawnMenu();
                            if (!g_nativeMenu.obj) g_forceSubsystem = 0;
                        }
                        Log("[ffx-hooks] SinCurse: submenu opened obj=0x%08X\n", static_cast<unsigned>(g_sinMenu.obj));
                    } else {
                        const LONG f7Kind = InterlockedExchange(&g_f7WantOpenKind, -1);
                        if (f7Kind >= 0) {
                            g_f7Menu = F7Sub_SpawnMenu((int)f7Kind);
                            if (!g_f7Menu.obj) {
                                g_nativeMenu = NativeMenu::SpawnMenu();
                                if (!g_nativeMenu.obj) g_forceSubsystem = 0;
                            }
                            Log("[ffx-hooks] F7: submenu %d opened obj=0x%08X\n", (int)f7Kind, static_cast<unsigned>(g_f7Menu.obj));
                        } else if (act != NativeMenu::ACT_EXIT && !held)
                            g_nativeMenu = NativeMenu::SpawnMenu();  // re-abre (menu persistente p/ proxima escolha)
                        else if (act == NativeMenu::ACT_EXIT) { g_forceSubsystem = 0;                    // EXIT: para de forcar (held MANTEM o pump vivo p/ TickHeld)
                            if (g_base) *reinterpret_cast<volatile int*>(g_base + (0x13407E4u - 0x400000u)) = 0; }  // fix tela preta
                    }
                }
            } else if (p.what == NativeMenu::POLL_CANCEL) {
                NativeMenu::CloseMenu(g_nativeMenu);
                g_forceSubsystem = 0;                        // cancelou: para de forcar
                if (g_base) *reinterpret_cast<volatile int*>(g_base + (0x13407E4u - 0x400000u)) = 0;  // fix tela preta
            }
        }
        static int s_drawDbg = 0;
        if (g_nativeMenu.obj && ((++s_drawDbg) % 120) == 0)
            Log("[ffx-hooks] NativeMenu DBG OurDraw calls=%d (sobe=desenha mas coberto; parado=NAO desenha)\n", NativeMenu::g_ourDrawCalls);
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
    const char* n = LabMusicRuntimeName(t);   // crosswalk completo (10..181) â€” 2026-08-02
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

// mapas valor -> mask (auto-status bits 15..23; elem bits 0..4)
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

// â”€â”€ F7 DIFF multi-column (2026-08-02, Jarvis-HOOK) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
enum F7DiffCol { F7DC_PRESETS = 0, F7DC_BASE, F7DC_AUTO, F7DC_WEAK, F7DC_RESIST, F7DC_ABSORB, F7DC_ACTIONS, F7DC_COUNT };
static int g_f7Col = F7DC_PRESETS;      // coluna ativa
static int g_f7ColRow = 0;              // linha ativa na coluna
static int g_f7EditActive = 0;          // modo edicao numerica (coluna BASE)
static int g_f7EditValue = 0;           // valor digitado
static int g_f7EditDigits = 0;          // qtd de digitos digitados
static int g_f7StatusTicks = 0;         // frames restantes da msg de status
static char g_f7StatusMsg[56] = {};     // msg de feedback (Apply/Save/Reset/preview)
static const char* const F7_BASE_NAMES[9] = { "HP","STR","DEF","MAG","MDF","AGI","ACC","EVA","LCK" };
static const char* const F7_ELEM_NAMES[5] = { "Fire","Ice","Thunder","Water","Holy" };
static const int F7_BASE_MIN[9] = { 100, 100, 100, 100, 100, 100, 100, 100, 100 };
static const int F7_BASE_MAX[9] = { 10000, 5000, 5000, 5000, 5000, 5000, 5000, 5000, 5000 };  // HP ate 10x

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

static void F7DiffSetStatus(const char* msg) {
    _snprintf_s(g_f7StatusMsg, sizeof(g_f7StatusMsg), _TRUNCATE, "%s", msg ? msg : "");
    g_f7StatusTicks = 150;
}

static void F7DiffToggleBit(int valIdx, int bit) {
    using namespace NativeMenu;
    g_f7Vals[valIdx] ^= (1 << bit);   // multi-select (checkbox)
    g_f7DiffPresetIdx = -1;
    g_f7Vals[0] = 0;                  // ajuste manual = custom
    PlaySfx(1);
}

static void F7_DiffPresetFill(int preset) {
    // vals DIFF (multi-coluna, 2026-08-02):
    //   [0]=preset [1..9]=hp,str,def,mag,mdf,agi,acc,eva,lck (permille)
    //   [10]=autoStatusMask (bits 0..24) [11]=elemWeak [12]=elemResist [13]=elemAbsorb (bits 0..4)
    g_f7DiffPresetIdx = preset;
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
        default: // True Nightmare: HP 3x, stats ~2x, auto Protect+Shell, sem elementos (stats puros + survives)
            g_f7Vals[1]=3000; g_f7Vals[2]=2200; g_f7Vals[3]=2200; g_f7Vals[4]=2000;
            g_f7Vals[5]=2000; g_f7Vals[6]=2000; g_f7Vals[7]=1500; g_f7Vals[8]=1500; g_f7Vals[9]=1500;
            g_f7Vals[10] = (1u << 23) | (1u << 22) | (1u << 16) | (1u << 15);   // Haste+Regen+Protect+Shell
            g_f7Vals[11]=0; g_f7Vals[12]=0; g_f7Vals[13]=0;
            break;
    }
}

static void F7_BuildRows(int kind) {
    g_f7MenuKind = kind;
    g_f7RowCount = 0;
    const FfxHooks::F7Config& cfg = FfxHooks::F7_GetConfig();
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
        g_f7Vals[0] = cfg.force.repeatCount;
        g_f7Rows[g_f7RowCount++] = { "Force Last Battle", F7RT_ACTION, 0, 0, 0 };
        g_f7Rows[g_f7RowCount++] = { "Repeat",            F7RT_STEPPER, 1, 9, 1 };
        g_f7Rows[g_f7RowCount++] = { "Last Encounter",    F7RT_INFO, 0, 0, 0 };
        g_f7Rows[g_f7RowCount++] = { "Back",              F7RT_BACK, 0, 0, 0 };
    } else if (kind == F7_MENU_AI) {
        // Monster AI Swap: enable + per-entry summary rows (read) + actions.
        const FfxHooks::F7AiSwapConfig& ac = FfxHooks::F7AiSwap_GetConfig();
        g_f7Vals[0] = ac.enabled ? 1 : 0;
        g_f7Vals[1] = ac.entryCount;
        g_f7Rows[g_f7RowCount++] = { "Monster AI Swap",   F7RT_TOGGLE, 0, 1, 1 };
        g_f7Rows[g_f7RowCount++] = { "Configs",           F7RT_INFO, 0, 0, 0 };
        g_f7Rows[g_f7RowCount++] = { "Reload from JSON",  F7RT_ACTION, 0, 0, 0 };
        g_f7Rows[g_f7RowCount++] = { "Save",              F7RT_ACTION, 0, 0, 0 };
        g_f7Rows[g_f7RowCount++] = { "Back",              F7RT_BACK, 0, 0, 0 };
    } else {  // DIFF (multi-coluna: presets + BASE + AUTO + WEAK + RESIST + ABSORB + ACTIONS)
        const FfxHooks::F7DifficultyPreset p = cfg.diffGlobal;
        g_f7DiffPresetIdx = -1;   // custom (deriva dos valores atuais)
        g_f7Vals[0] = 0;
        g_f7Vals[1] = p.hpMul;  g_f7Vals[2] = p.strMul;  g_f7Vals[3] = p.defMul;
        g_f7Vals[4] = p.magMul; g_f7Vals[5] = p.mdfMul;  g_f7Vals[6] = p.agiMul;
        g_f7Vals[7] = p.accMul; g_f7Vals[8] = p.evaMul;  g_f7Vals[9] = p.lckMul;
        g_f7Vals[10] = (int)(p.autoStatusMask & 0x01FFFFFFu);
        g_f7Vals[11] = p.elemWeak;
        g_f7Vals[12] = p.elemResist;
        g_f7Vals[13] = p.elemAbsorb;
        g_f7RowCount = 0;   // DIFF nao usa rows 1D (input/draw multi-coluna proprios)
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
            if (g_f7MenuKind == F7_MENU_FORCE && FfxHooks::F7_HasLastEncounter()) {
                _snprintf_s(g_f7FmtBuf, sizeof(g_f7FmtBuf), _TRUNCATE, "field %d / group %d",
                    FfxHooks::F7_LastEncounterField(), FfxHooks::F7_LastEncounterGroup());
                return g_f7FmtBuf;
            }
            return "-";
        default: break;
    }
    return "";
}

// â”€â”€ F7 submenu: input (up/down navigate Â· left/right adjust Â· confirm/cancel) â”€
static void F7_AdjustValue(int delta, int sel) {
    const F7SubRow& R = g_f7Rows[sel];
    if (R.type != F7RT_STEPPER && R.type != F7RT_TOGGLE) return;
    const int step = (R.type == F7RT_TOGGLE) ? 1 : R.step;
    int nv = g_f7Vals[sel] + delta * step;
    if (nv < R.min) nv = R.min;
    if (nv > R.max) nv = R.max;
    g_f7Vals[sel] = nv;
    if (g_f7MenuKind == F7_MENU_DIFF) {
        if (sel == 0) { F7_DiffPresetFill(g_f7Vals[0]); return; }   // preset aplica nos valores
        g_f7DiffPresetIdx = -1;                                      // ajuste manual = custom
        g_f7Vals[0] = 0;
    }
}

static int __cdecl F7Sub_InputCb(int obj) {
    using namespace NativeMenu;
    if (g_f7ConfirmTimer > 0) --g_f7ConfirmTimer;
    const int dir  = PadDir();
    const int edge = PadEdge();
    const bool confirmPressed = (edge & 0x20) && !(g_f7LastEdge & 0x20) && (g_f7ConfirmTimer == 0);
    const bool cancelPressed  = (edge & 0x40) && !(g_f7LastEdge & 0x40) && (g_f7ConfirmTimer == 0);   // cooldown no cancel tb
    g_f7LastEdge = edge;

    // â”€â”€ DIFF: multi-column (presets + BASE + AUTO + WEAK + RESIST + ABSORB + ACTIONS) â”€â”€
    if (g_f7MenuKind == F7_MENU_DIFF) {
        if (g_f7EditActive) {
            // modo edicao numerica (coluna BASE): digitar 0-9 / Backspace / Confirm aplica / Cancel cancela
            for (int k = '0'; k <= '9'; ++k) {
                if (GetAsyncKeyState(k) & 1) {
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
                int v = (g_f7EditDigits > 0) ? g_f7EditValue : g_f7Vals[1 + row];
                if (v < F7_BASE_MIN[row]) v = F7_BASE_MIN[row];
                if (v > F7_BASE_MAX[row]) v = F7_BASE_MAX[row];
                g_f7Vals[1 + row] = v;
                g_f7DiffPresetIdx = -1; g_f7Vals[0] = 0;
                g_f7EditActive = 0;
                F7DiffSetStatus("valor aplicado (custom)");
                PlaySfx(4);
            } else if (cancelPressed) {
                g_f7EditActive = 0;
                PlaySfx(1);
            }
            return obj;
        }
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
                        FfxHooks::F7_DifficultyApplyNow();
                        F7DiffSetStatus("Apply Now executado");
                        PlaySfx(4);
                    } else if (g_f7ColRow == 1) {
                        F7_CommitValsToConfig();
                        FfxHooks::F7_SaveConfig();
                        F7DiffSetStatus("Config salva em f7_inlive.json");
                        PlaySfx(4);
                    } else {
                        F7_CommitValsToConfig();
                        FfxHooks::F7_SaveConfig();
                        g_f7ConfirmRow = -2;          // volta ao hub (pump trata como cancel)
                        WrB(obj, 65, 1);
                    }
                    break;
            }
        } else if (cancelPressed) {
            g_f7ConfirmRow = -2;
            WrB(obj, 65, 1);
        }
        if (confirmPressed || cancelPressed) g_f7ConfirmTimer = 12;
        return obj;
    }

    // â”€â”€ MUSIC: steppers with preview on confirm; actions execute without closing â”€â”€
    if (g_f7MenuKind == F7_MENU_MUSIC) {
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
                g_f7ConfirmRow = -2;
                WrB(obj, 65, 1);
            } else if (R.type == F7RT_ACTION) {
                if (selM == 4) {
                    // FIX 2026-08-02 (RT2): Preview used to crash (via probe). Now:
                    // 1) salva o config (mesmo do Save) 2) reabre o menu nativo p/ Arena+ → Custom Mix.
                    F7_CommitValsToConfig(); FfxHooks::F7_SaveConfig();
                    F7DiffSetStatus("Config salva — abrindo Arena+...");
                    g_f7ConfirmRow = -2; WrB(obj, 65, 1);  // fecha o submenu
                    InterlockedExchange(&g_nativeWantSpawn, 1);  // reabre o menu nativo
                    PlaySfx(4);
                }
                else if (selM == 5) { F7_CommitValsToConfig(); FfxHooks::F7_SaveConfig(); F7DiffSetStatus("Config salva em f7_inlive.json"); PlaySfx(4); }
                else if (selM == 6) {
                    FfxHooks::F7_ResetMusic();
                    F7_BuildRows(F7_MENU_MUSIC);
                    WrW(obj, O_COUNT, static_cast<int16_t>(g_f7RowCount));
                    WrW(obj, O_SELECTED, 0);
                    WrW(obj, O_TOP, 0);
                    F7DiffSetStatus("Musica restaurada ao padrao");
                    PlaySfx(4);
                }
            } else if (R.type == F7RT_STEPPER && (selM == 0 || selM == 1)) {
                // FIX 2026-08-02 (RT2): confirm no stepper = SALVA (o preview via probe crashava — probe OFF).
                F7_CommitValsToConfig(); FfxHooks::F7_SaveConfig();
                F7DiffSetStatus("Faixa salva em f7_inlive.json");
                PlaySfx(4);
            } else {
                PlaySfx(4);
            }
        } else if (cancelPressed) {
            g_f7ConfirmRow = -2;
            WrB(obj, 65, 1);
        }
        if (confirmPressed || cancelPressed) g_f7ConfirmTimer = 12;
        return obj;
    }

    int sel = RdW(obj, O_SELECTED);
    const int count = RdW(obj, O_COUNT);
    int top = RdW(obj, O_TOP);
    const int page = RdW(obj, O_PAGE);

    if (dir & 0x1000) { if (sel > 0) { --sel; PlaySfx(1); } }
    else if (dir & 0x4000) { if (sel + 1 < count) { ++sel; PlaySfx(1); } }
    if (dir & 0x8000) { F7_AdjustValue(-1, sel); PlaySfx(1); }
    else if (dir & 0x2000) { F7_AdjustValue(+1, sel); PlaySfx(1); }

    if (sel < top) top = sel;
    else if (sel >= top + page) top = sel - page + 1;
    WrW(obj, O_SELECTED, static_cast<int16_t>(sel));
    WrW(obj, O_TOP, static_cast<int16_t>(top));

    if (confirmPressed) {
        const F7SubRow& R = g_f7Rows[sel];
        if (R.type == F7RT_BACK || R.type == F7RT_ACTION) {
            g_f7ConfirmRow = sel;
            WrB(obj, 65, 1);                    // close flag -> PollMenu ve o confirm
        } else {
            PlaySfx(4);
        }
    } else if (cancelPressed) {
        g_f7ConfirmRow = -2;                    // cancel
        WrB(obj, 65, 1);
    }
    if (confirmPressed || cancelPressed) g_f7ConfirmTimer = 12;   // ~200ms anti-spam
    return obj;
}

// â”€â”€ F7 submenu: draw (SIN style: glass + neon + value on the right) â”€â”€â”€â”€â”€â”€â”€â”€
// â”€â”€ F7 DIFF draw: prominent presets + side-by-side columns + checkboxes â”€â”€â”€â”€
static void F7Diff_Draw(int F) {
    using namespace NativeMenu;
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
    static const char* const colName[F7DC_COUNT] = { "", "BASE STATUS", "AUTO STATUS", "WEAK", "RESIST", "ABSORB", "ACTIONS" };
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
            if (focused) DrawSolidRect(cx, ry, cw, rowH, 0x3810FF40u, 0x1810FF40u);
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
                const char* label = (r == 0) ? "Apply Now" : (r == 1) ? "Save" : "Back";
                unsigned char buf[48] = {};
                EncodeLabel(label, buf, (int)sizeof(buf));
                DrawStringSub(buf, cx, ry);
            }
        }
    }

    // Modo edicao numerica: valor digitado com cursor
    if (g_f7EditActive) {
        char asc[48] = {};
        _snprintf_s(asc, sizeof(asc), _TRUNCATE, ">> %d_", g_f7EditDigits > 0 ? g_f7EditValue : g_f7Vals[1 + g_f7ColRow]);
        unsigned char buf[48] = {};
        EncodeLabel(asc, buf, (int)sizeof(buf));
        DrawStringSub(buf, NX(0.24f), NY(0.890f));
    }

    // Footer: dicas + status
    unsigned char foot[96] = {};
    EncodeLabel("L/R colunas  U/D linha  X ativar  Esc voltar", foot, (int)sizeof(foot));
    DrawStringSub(foot, NX(0.073f), NY(0.911f));
    if (g_f7StatusTicks > 0) {
        --g_f7StatusTicks;
        unsigned char st[64] = {};
        EncodeLabel(g_f7StatusMsg, st, (int)sizeof(st));
        DrawStringSub(st, NX(0.50f), NY(0.911f));
    }
}

static int __cdecl F7Sub_DrawCb(int obj) {
    using namespace NativeMenu;
    static int s_drawCalls = 0;
    const int F = ++s_drawCalls;
    const int sel = RdW(obj, O_SELECTED);
    const int top = RdW(obj, O_TOP);
    const int page = RdW(obj, O_PAGE);

    const float pulse1 = Osc01(F, 45);
    const float neonStr = 0.55f + 0.45f * pulse1;

    char title[64] = {}, sub[64] = {}, foot[96] = {};
    const char* titleTxt = (g_f7MenuKind == F7_MENU_MUSIC) ? "F7 - Music"
                          : (g_f7MenuKind == F7_MENU_FORCE) ? "F7 - Force Battle"
                          : "F7 - Difficulty";
    const char* subTxt = (g_f7MenuKind == F7_MENU_MUSIC) ? "Lock / battle / randomizer / fade / preview / save / reset"
                        : (g_f7MenuKind == F7_MENU_FORCE) ? "Last encounter + force with 1 click"
                        : "Monster status control - live RAM";
    EncodeLabel(titleTxt, (unsigned char*)title, (int)sizeof(title));
    EncodeLabel(subTxt, (unsigned char*)sub, (int)sizeof(sub));
    EncodeLabel("Arrows Navigate   L/R Adjust   Confirm Select   Cancel Back   F7 Exit", (unsigned char*)foot, (int)sizeof(foot));

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
    DrawMenuGlassPanel(NX(0.047f), NY(0.20f), NW(0.906f), NH(0.62f), F, 1);

    const float vLeft = NX(0.271f), vTop = NY(0.215f), vWidth = NW(0.458f);
    const float vStep = NH(0.063f), vBarH = NH(0.056f), vPadX = NW(0.015f);
    const float selLine = MenuBorderPx() * 0.45f;
    const float cursorOff = NW(0.020f);

    for (int r = 0; r < page && top + r < g_f7RowCount; ++r) {
        const int row = top + r;
        const float vy = vTop + r * vStep;
        unsigned int c0 = kMenuRowGlassTop, c1 = kMenuRowGlassBot;
        if (g_f7Rows[row].type == F7RT_BACK) { c0 = kMenuGlassBorder; c1 = kMenuGlassBorderLo; }
        unsigned int a0 = ((c0 >> 24) & 0xFFu), a1 = ((c1 >> 24) & 0xFFu);
        a0 = (unsigned int)(a0 * neonStr); a1 = (unsigned int)(a1 * neonStr);
        if (a0 > 0xFF) a0 = 0xFF; if (a1 > 0xFF) a1 = 0xFF;
        c0 = (c0 & 0x00FFFFFFu) | (a0 << 24);
        c1 = (c1 & 0x00FFFFFFu) | (a1 << 24);
        DrawSolidRect(vLeft, vy, vWidth, vBarH, c0, c1);
        { unsigned char _lbl[64]; memcpy(_lbl, g_f7Labels[row], sizeof(_lbl)); DrawString(_lbl, vLeft + vPadX, vy + NH(0.016f)); }
        const char* valTxt = F7RowValueText(row);
        if (valTxt && valTxt[0]) {
            unsigned char vlab[48] = {};
            EncodeLabel(valTxt, vlab, (int)sizeof(vlab));
            DrawStringSub(vlab, vLeft + NW(0.175f), vy + NH(0.023f));
        }
    }

    const float selVisY = vTop + (float)(sel - top) * vStep;
    if (g_f7EasedRowY < 0.0f) g_f7EasedRowY = selVisY;
    g_f7EasedRowY += (selVisY - g_f7EasedRowY) * 0.30f;

    if (sel >= top && sel < top + page) {
        const float ey = g_f7EasedRowY;
        const unsigned int a = 0x44u + (unsigned int)(Osc01(F, 44) * 32.0f);
        const unsigned int lift0 = (a << 24) | 0x00305068u;
        const unsigned int lift1 = (a << 24) | 0x00182038u;
        DrawSolidRect(vLeft, ey, vWidth, vBarH, lift0, lift1);
        DrawSolidRect(vLeft, ey + vBarH - selLine, vWidth, selLine, kMenuNeonGreenLine, kMenuNeonGreenLineLo);
        DrawCursor(vLeft - cursorOff, ey + NH(0.002f));
    }

    const float fx = NX(0.047f), fy = NY(0.887f), fw = NW(0.906f), fh = NH(0.070f);
    DrawMenuGlassPanel(fx, fy, fw, fh, F, 1);
    DrawString((unsigned char*)foot, NX(0.071f), NY(0.911f));
    return obj;
}

// â”€â”€ F7 submenu: spawn / poll / close / confirm â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static NativeMenu::Menu F7Sub_SpawnMenu(int kind) {
    F7_BuildRows(kind);
    int obj = NativeMenu::Alloc();
    if (!obj) return NativeMenu::Menu{ 0 };
    NativeMenu::WrW(obj, NativeMenu::O_COUNT, static_cast<int16_t>(g_f7RowCount > 0 ? g_f7RowCount : 8));  // DIFF multi-coluna: count fake >0 p/ o draw tick do jogo chamar nosso draw (fix tela preta 2026-08-02)
    NativeMenu::WrW(obj, NativeMenu::O_PAGE,  6);
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
    NativeMenu::ClaimModal(obj);  // block game FSM from double-processing pad (fix tela preta)
    g_f7ClosedFlag = 0;
    g_f7ConfirmRow = -1;
    g_f7LastEdge = 0;
    g_f7EasedRowY = -1.0f;
    g_f7Col = F7DC_PRESETS; g_f7ColRow = 0; g_f7EditActive = 0; g_f7StatusTicks = 0;
    NativeMenu::Register(obj);
    return NativeMenu::Menu{ obj };
}

static NativeMenu::Poll F7Sub_PollMenu(const NativeMenu::Menu& m) {
    if (g_f7ClosedFlag) {
        g_f7ClosedFlag = 0;
        if (g_f7ConfirmRow >= 0) return NativeMenu::Poll{ NativeMenu::POLL_CONFIRM, g_f7ConfirmRow };
        return NativeMenu::Poll{ NativeMenu::POLL_CANCEL, 0 };
    }
    return NativeMenu::Poll{ NativeMenu::POLL_NAV, 0 };
}

static void F7Sub_CloseMenu() {
    if (g_f7Menu.obj) {
        NativeMenu::WrB(g_f7Menu.obj, 65, 1);
        g_f7Menu.obj = 0;
    }
    g_f7ClosedFlag = 0;
}

// Aplica vals -> config do F7 (Music/Force/Difficulty).
static void F7_CommitValsToConfig() {
    FfxHooks::F7Config& cfg = const_cast<FfxHooks::F7Config&>(FfxHooks::F7_GetConfig());
    if (g_f7MenuKind == F7_MENU_MUSIC) {
        FfxHooks::F7_SetMusicLock(g_f7Vals[0]);
        FfxHooks::F7_SetMusicBattleTrack(g_f7Vals[1]);
        FfxHooks::F7_SetMusicRandomizer(g_f7Vals[2] != 0);
        FfxHooks::F7_SetMusicFade(g_f7Vals[3]);
    } else if (g_f7MenuKind == F7_MENU_FORCE) {
        FfxHooks::F7_SetRepeatCount(g_f7Vals[0]);
    } else if (g_f7MenuKind == F7_MENU_AI) {
        FfxHooks::F7AiSwapConfig& ac = const_cast<FfxHooks::F7AiSwapConfig&>(FfxHooks::F7AiSwap_GetConfig());
        ac.enabled = g_f7Vals[0] != 0;
        FfxHooks::F7AiSwap_SaveConfig();
    } else {
        FfxHooks::F7DifficultyPreset p = cfg.diffGlobal;
        p.enabled = g_f7Vals[0] != 0;
        p.hpMul = g_f7Vals[1];  p.strMul = g_f7Vals[2];  p.defMul = g_f7Vals[3];
        p.magMul = g_f7Vals[4]; p.mdfMul = g_f7Vals[5];  p.agiMul = g_f7Vals[6];
        p.accMul = g_f7Vals[7]; p.evaMul = g_f7Vals[8];  p.lckMul = g_f7Vals[9];
        p.autoStatusMask = (uint32_t)g_f7Vals[10] & 0x01FFFFFFu;
        p.elemWeak = (uint8_t)(g_f7Vals[11] & 0x1F);
        p.elemResist = (uint8_t)(g_f7Vals[12] & 0x1F);
        p.elemAbsorb = (uint8_t)(g_f7Vals[13] & 0x1F);
        cfg.diffGlobal = p;
    }
}

static void F7Sub_HandleConfirm(int row) {
    // 2026-08-02 (Jarvis-HOOK): MUSIC/DIFF executam as acoes no proprio input
    // (Preview/Save/Reset/Apply) e saem via POLL_CANCEL; aqui so o FORCE usa CONFIRM.
    if (g_f7MenuKind == F7_MENU_FORCE) {
        if (row == 0) { FfxHooks::F7_ForceLastBattle(); }
        else if (row == 3) { F7_CommitValsToConfig(); }
    } else if (g_f7MenuKind == F7_MENU_AI) {
        if (row == 2) {   // Reload from JSON
            FfxHooks::F7AiSwap_Reload();
            Log("[ffx-hooks] F7 AI: config reloaded (entries=%d)\n",
                FfxHooks::F7AiSwap_GetConfig().entryCount);
        } else if (row == 3) {   // Save
            F7_CommitValsToConfig();   // persiste enabled
        }
    }
}

static PLH::x86Detour* g_updateWindowTitleDetour = nullptr;
static uint64_t g_updateWindowTitleTramp = 0;
// WHY (crash geral do menu, 2026-08-03, RT2): o nosso menu nativo (F7/submenus/compose pick) desenha
// MUITOS textos no menu 2D do jogo todo frame e corrompe o pool/cache de texto do jogo (cache-hit ->
// pool[cursor-1] sem bound check). Quando o jogo chama o FFX_System_UpdateWindowTitle (o draw do texto
// do SAVE no titulo — ex. ao sair dis aa batalha), o DrawUITextElement usa o pool corrompido ->
// AV WRITE 0xBD (rva 0x4FB05E). Fix: hook no UpdateWindowTitle — com o nosso menu ativo, retorna 0
// (the title does not update — cosmético) and the game does NOT draw the text with corrupted pool.
using FnUpdateWindowTitle = int(__cdecl*)(void* a, __int16* title, float e);
static int __cdecl UpdateWindowTitle_MenuGuard(void* a, __int16* title, float e) {
    const bool menuActive = g_nativeMenu.obj || g_arenaPlusMenu.obj || g_sinMenu.obj || g_f7Menu.obj ||
        ArenaPlusComposePick_IsActive();
    if (menuActive) return 0;
    return reinterpret_cast<FnUpdateWindowTitle>(g_updateWindowTitleTramp)(a, title, e);
}

static void StartNativeMenuIfEnabled() {
    // ARM: env FFXHOOKS_ENABLE_NATIVE_MENU=1 OU modules/native_menu.flag (Steam-safe).
    // F7 e detectado no Present (NativeMenu_PresentTick) â€” StartAuroraOverlayIfEnabled enables the D3D11
    // quando native_menu.flag esta armado (nao precisa aurora_overlay_d3d11.flag separado). Lane IFRIT/ARENA.
    const bool armed = NativeMenuArmedFromConfig();
    if (!armed) {
        Log("[ffx-hooks] NativeMenu: disabled (env FFXHOOKS_ENABLE_NATIVE_MENU=1 OU modules/native_menu.flag p/ armar)\n");
        return;
    }
    if (!g_base) { Log("[ffx-hooks] NativeMenu: g_base nao resolvido â€” abort\n"); return; }
    NativeMenu::SetBridge(NativeMenu::PhotoModeBridge{ &NativeMenu_OnEdge, &NativeMenu_OnHeldEnter });
    PhotoMode::g_base = g_base;
    ArenaPlusComposePick_SetLog(&Log);
    ArenaPlusComposePick_SetModule(g_module);
    const int hk = EnvInt("FFXHOOKS_NATIVE_MENU_HOTKEY", VK_F7);
    g_nativeMenuHotkey = (hk > 0 && hk < 256) ? hk : VK_F7;
    const uintptr_t pumpVa = g_base + (0x8A9C50u - 0x400000u); // FFX_Menu_PerFramePump int __cdecl(uint) [IDA d091ab12]
    try {
        g_nativeMenuPumpDetour = new PLH::x86Detour(
            static_cast<uint64_t>(pumpVa),
            reinterpret_cast<uint64_t>(&NativeMenu_PumpHook),
            &g_nativeMenuPumpTramp);
        const bool ok = g_nativeMenuPumpDetour->hook();
        Log("[ffx-hooks] NativeMenu pump hook ok=%d hotkey=0x%02X (menu OFF ate a hotkey)\n", ok ? 1 : 0, g_nativeMenuHotkey);
        if (!ok) { delete g_nativeMenuPumpDetour; g_nativeMenuPumpDetour = nullptr; g_nativeMenuPumpTramp = 0; }
    } catch (const std::exception& ex) {
        Log("[ffx-hooks] ERROR NativeMenu pump hook exception: %s\n", ex.what());
        g_nativeMenuPumpDetour = nullptr; g_nativeMenuPumpTramp = 0;
    } catch (...) {
        Log("[ffx-hooks] ERROR NativeMenu pump hook unknown exception\n");
        g_nativeMenuPumpDetour = nullptr; g_nativeMenuPumpTramp = 0;
    }
}

static void StopNativeMenu() {
    __try {
        if (g_nativeMenu.obj) NativeMenu::CloseMenu(g_nativeMenu);
        if (g_arenaPlusMenu.obj) ArenaPlus_CloseMenu(g_arenaPlusMenu);
        SinCurse_CloseMenu();
        F7Sub_CloseMenu();
        ArenaPlusComposePick_Close();
        if (PhotoMode::g_pm.on) PhotoMode::Exit();   // restaura atores + camera ao original
        g_nativeHeldAction = -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[ffx-hooks] WARN NativeMenu stop exception\n");
    }
    if (g_nativeMenuPumpDetour) {
        g_nativeMenuPumpDetour->unHook();   // PolyHook2: o destrutor tambem desfaz (belt-and-suspenders)
        delete g_nativeMenuPumpDetour;
        g_nativeMenuPumpDetour = nullptr;
        g_nativeMenuPumpTramp = 0;
    }
}
#endif // FFXHOOKS_HAVE_POLYHOOK

/* â”€â”€ Hook install / remove â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
static void InstallHooks() {
    Log("[ffx-hooks] InstallHooks enter\n");
    Log("[ffx-hooks] before GetModuleHandleA(FFX.exe)\n");
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleA("FFX.exe"));
    if (!g_base) {
        Log("[ffx-hooks] WARN FFX.exe base not found\n");
        return;
    }
    Log("[ffx-hooks] FFX.exe base = 0x%08X\n", static_cast<unsigned>(g_base));

    AddVectoredExceptionHandler(1, FfxFaultProbeVeh);
    Log("[ffx-hooks] FaultProbe VEH armed (global fault diagnosis)\n");

    // CreateBlock is deferred â€” only created below when a hook that needs shared memory (MusicHook) is active.
    // Unconditional CreateBlock (pre-fix) caused heap corruption (0xc0000374) by changing the game's memory layout.
    g_mmf = NULL;
    g_block = nullptr;

    /* â”€â”€ Fase 1: Music Swap hook â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
#ifdef FFXHOOKS_HAVE_POLYHOOK
    const bool enableMusic = MusicHookEnabledFromConfig();
    const bool validateOnly = EnvFlagEnabled("FFXHOOKS_VALIDATE_ONLY");
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
        // 2026-08-02 (Jarvis-HOOK): o gate do heartbeat NAO bloqueia mais o MusicHook — o override via
        // FFXHooksBlock + os hooks de battle-entry (Prep/PlayTrackWithPreload/SwitchCrossfade) funcionam
        // SEM o probe; so o SOUNDCMD extra (trigger lab) degrada. O probe continua sendo esperado 10s.
        const bool probeAlive = EnvFlagEnabled("FFXHOOKS_SKIP_PROBE_WAIT") || WaitForProbeHeartbeat(10000);
        if (!probeAlive) {
            Log("[ffx-hooks] WARN ffx-probe heartbeat not ready — MusicHook instalado SEM soundcmd (override/battle-entry via hook OK)\n");
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
    const bool enableNovaBypass = NovaSuperDamageFlagEnabled();
    const bool enableNovaLog = NovaSuperDamageLogFlagEnabled();
    if (enableNovaBypass || enableNovaLog) {
        if (validateOnly) {
            Log("[ffx-hooks] NovaClamp install blocked by FFXHOOKS_VALIDATE_ONLY=1\n");
        } else {
            const FfxHooks::NovaSuperDamageInstallResult novaResult =
                FfxHooks::InstallNovaSuperDamageHook(
                    g_base,
                    enableNovaBypass,
                    enableNovaLog,
                    LogLine);
            Log("[ffx-hooks] NovaClamp install result ok=%d stub=0x%08X bypass=%d log=%d\n",
                novaResult.ok ? 1 : 0,
                static_cast<unsigned>(novaResult.stub),
                enableNovaBypass ? 1 : 0,
                enableNovaLog ? 1 : 0);
        }
    } else {
        Log("[ffx-hooks] NovaClamp not armed (nova_super_damage.flag / nova_super_damage_log.flag)\n");
    }
    if (RonsoManaFlagEnabled()) {
        if (validateOnly) {
            Log("[ffx-hooks] RonsoMana install blocked by FFXHOOKS_VALIDATE_ONLY=1\n");
        } else {
            const bool ronsoLogOnly = !RonsoManaApplyEnabled();
            const FfxHooks::RonsoManaInstallResult ronsoResult =
                FfxHooks::InstallRonsoManaHook(
                    g_base,
                    true, /* G1: temp max spoof only inside gate shim */
                    true,
                    true,
                    ronsoLogOnly,
                    LogLine);
            Log("[ffx-hooks] RonsoMana install result ok=%d gate=0x%08X grey=0x%08X drain=0x%08X logOnly=%d\n",
                ronsoResult.ok ? 1 : 0,
                static_cast<unsigned>(ronsoResult.stubGate),
                static_cast<unsigned>(ronsoResult.stubGreyout),
                static_cast<unsigned>(ronsoResult.stubDrain),
                ronsoLogOnly ? 1 : 0);
        }
    } else {
        Log("[ffx-hooks] RonsoMana not armed (kimahri_ronso_mana.flag)\n");
    }
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
            int capRequested = EnvInt("FFXHOOKS_ITEM_STACK_CAP", FFX_ITEM_STACK_CAP_EXTENDED);
            if (capRequested < 1) capRequested = 1;
            if (capRequested > 255) capRequested = 255;
            const uint8_t cap = static_cast<uint8_t>(capRequested);
            const FfxHooks::ItemStackCapInstallResult capResult =
                FfxHooks::InstallItemStackCapHook(
                    g_base,
                    cap,
                    ItemStackCapLogFlagEnabled(),
                    LogLine);
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
    if (ArenaPlus_ResolverLogEnabled()) {
        if (validateOnly) {
            Log("[ffx-hooks] ResolverLog install blocked by FFXHOOKS_VALIDATE_ONLY=1\n");
        } else {
            const FfxHooks::ResolverLogInstallResult resolverResult =
                FfxHooks::InstallResolverLogHook(g_base, LogLine);
            Log("[ffx-hooks] ResolverLog install ok=%d reason=%u\n",
                resolverResult.ok ? 1 : 0,
                static_cast<unsigned>(resolverResult.reasonCode));
        }
    } else {
        Log("[ffx-hooks] ResolverLog not armed (arena_plus_resolver_log.flag)\n");
    }
    {
        const bool enableFieldScout = FieldScoutFlagEnabled() || FieldScoutMapOnlyFlagEnabled();
        if (enableFieldScout) {
            if (validateOnly) {
                Log("[ffx-hooks] FieldScout install blocked by FFXHOOKS_VALIDATE_ONLY=1\n");
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
                /* Apply queued MinHook hooks after boot settles (10s delay â€” after intro/title) */
                /* Apply queued hooks immediately (worker thread already slept 500ms-2s + boot time).
                   MH_ApplyQueued suspends ALL threads atomically â€” safe to call. Game is on title screen. */
                if (scoutResult.ok && heavy) {
                    FfxHooks::ApplyFieldScoutQueuedHooks(LogLine);
                    if (LogLine) LogLine("[ffx-hooks] FieldScout heavy hooks activated\n");
                }
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
        }
    }
    // WIRE-ME: BootSkipHook â€” uncomment when RT2 approves (Jarvis-MAGIC BootSkipLab).
    // Requires #include "hooks/BootSkipHook.h" at top and FastBootSkipEnabledFromConfig() helper.
    // if (FastBootSkipEnabledFromConfig()) {
    //     if (validateOnly) {
    //         Log("[ffx-hooks] BootSkip install blocked by FFXHOOKS_VALIDATE_ONLY=1\n");
    //     } else {
    //         const FfxHooks::BootSkipConfig bsCfg = FfxHooks::BootSkipConfigFromEnvironment();
    //         const FfxHooks::BootSkipInstallResult bsResult =
    //             FfxHooks::InstallBootSkipHook(g_base, LogLine, bsCfg);
    //         Log("[ffx-hooks] BootSkip install ok=%d reason=%u\n",
    //             bsResult.ok ? 1 : 0, static_cast<unsigned>(bsResult.reasonCode));
    //     }
    // } else {
    //     Log("[ffx-hooks] BootSkip not armed (fast_boot_skip.flag)\n");
    // }
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
    StartAuroraOverlayIfEnabled();
    if (enableFpsScout) {
        if (FpsScoutStart()) {
            if (InstallAuroraD3D11Overlay()) {
                StartAuroraD3DLatePresentFallback();
            } else {
                Log("[ffx-hooks] WARN FPS Scout could not install Present hook\n");
            }
        }
    }
    StartArenaTraceIfEnabled();
    StartNativeMenuIfEnabled();   // step 5.1 â€” OFF ate FFXHOOKS_ENABLE_NATIVE_MENU=1
    // UpdateWindowTitle guard (menu 2D pool crash fix, 2026-08-03): detour no 0x4FAE40 — com o nosso
    // menu ativo, o jogo NAO desenha o titulo do save (o pool de texto corrompido nao crasha mais).
    if (!g_updateWindowTitleDetour && g_base) {
        g_updateWindowTitleDetour = new PLH::x86Detour(
            (uint64_t)(g_base + (0x4FAE40u - 0x400000u)),
            (uint64_t)&UpdateWindowTitle_MenuGuard, &g_updateWindowTitleTramp);
        const bool ok = g_updateWindowTitleDetour->hook();
        Log("[ffx-hooks] UpdateWindowTitle guard hook ok=%d (menu 2D pool crash fix)\n", ok ? 1 : 0);
        if (!ok) { delete g_updateWindowTitleDetour; g_updateWindowTitleDetour = nullptr; }
    }
    // F8 dashboard (Operacao Demonio 2026-08-02): gate [dashboard] enabled (default on).
    // O dashboard vira o DONO do F8/INSERT; o InGameMenu so processa F8 com dashboard off
    // (arbitragem na InGameMenuProcessKey).
    if (FfxHooks::Config::GetBool("dashboard.enabled", false)) {
        if (FfxHooks::StartInGameMenuDashboard()) {
            Log("[ffx-hooks] F8 dashboard started (dashboard.enabled=1)\n");
        } else {
            Log("[ffx-hooks] WARN F8 dashboard failed to start (no game window?)\n");
        }
        if (FfxHooks::StartUnXBoosterHook()) {
            Log("[ffx-hooks] UnXBoosterHook started (30Hz booster timer)\n");
        } else {
            Log("[ffx-hooks] WARN UnXBoosterHook failed to start\n");
        }
        FfxHooks::InstallDialogSkipHook(g_base, LogLine);   // Onda 3: dialog voice skip (gate input.dialog_skip)
    } else {
        Log("[ffx-hooks] F8 dashboard disabled (dashboard.enabled=0) -> InGameMenu keeps F8\n");
    }
#else
    Log("[ffx-hooks] Fase 0 skeleton loaded â€” no active hooks\n");
#endif
    FfxHooks::F7_InstallHooks(g_base, g_block, LogLine);   // F7 In-Live: difficulty/force/music (gate f7_inlive.flag)
    FfxHooks::F7AiSwap_Install(g_base, LogLine);           // F7 AI Swap: status-on-ability (gate f7_aiswap.flag)
    ArenaPlus_RestorePendingComposeOnBoot();   // restaura o bin composto dis aa sessao anterior (o compose nunca fica)

    /* Fase 2: FfxHooks::InstallElementHook(g_base, g_block);           */
    Log("[ffx-hooks] InstallHooks leave\n");
}

static void RemoveHooks() {
    Log("[ffx-hooks] RemoveHooks enter\n");
#ifdef FFXHOOKS_HAVE_POLYHOOK
    FpsScoutStop();
    StopAuroraOverlay();
    StopLabMenu();
    StopArenaTrace();
    StopNativeMenu();             // step 5.1 â€” fecha menu + restaura (Exit) + remove detour
    FfxHooks::F7_RemoveHooks();   // F7 In-Live: remove detours + limpa override de musica
    FfxHooks::F7AiSwap_Remove();  // F7 AI Swap: limpa estado (gate f7_aiswap.flag)

#endif
    FfxHooks::RemoveMusicHook(LogLine);
    FfxHooks::RemoveNovaSuperDamageHook(LogLine);
    FfxHooks::RemoveRonsoManaHook(LogLine);
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
    /* Fase 2: FfxHooks::RemoveElementHook(); */
    DestroyBlock();
    Log("[ffx-hooks] RemoveHooks leave\n");
}

/* â”€â”€ DllMain â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
static DWORD WINAPI HooksWorkerThread(LPVOID) {
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
    Log("[ffx-hooks] worker thread leave\n");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_module = hMod;
            EarlyLogLine("[ffx-hooks] early DLL_PROCESS_ATTACH enter\r\n");
            OutputDebugStringA("[ffx-hooks] DllMain DLL_PROCESS_ATTACH enter\n");
            /* Lab build: keep thread notifications; some injected loader stacks are touchy here. */
            EarlyLogLine("[ffx-hooks] early skip DisableThreadLibraryCalls\r\n");
            OpenLog();
            EarlyLogLine("[ffx-hooks] early after OpenLog\r\n");
            Log("[ffx-hooks] DLL_PROCESS_ATTACH enter\n");
            /* Do NOT hook directly in DllMain â€” create a thread and wait. */
            {
                DWORD tid = 0;
                HANDLE thread = CreateThread(nullptr, 0, HooksWorkerThread, nullptr, 0, &tid);
                if (thread) {
                    EarlyLogLine("[ffx-hooks] early worker thread created\r\n");
                    Log("[ffx-hooks] worker thread created tid=%u\n", static_cast<unsigned>(tid));
                    CloseHandle(thread);
                } else {
                    EarlyLogLine("[ffx-hooks] early worker thread create failed\r\n");
                    Log("[ffx-hooks] WARN failed to create worker thread (err=%u)\n", GetLastError());
                }
            }
            break;

        case DLL_PROCESS_DETACH:
            Log("[ffx-hooks] DLL_PROCESS_DETACH enter\n");
            RemoveHooks();
            if (g_log) { fclose(g_log); g_log = nullptr; }
            OutputDebugStringA("[ffx-hooks] DllMain DLL_PROCESS_DETACH leave\n");
            break;
    }
    return TRUE;
}
