/* ffx-hooks.dll Fase 0 safe stub.
 *
 * This file intentionally avoids the C/C++ runtime. It is the deploy-safe
 * module-loader smoke artifact: create the shared block, write a tiny log, and
 * install no hooks.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "shared/ffx_hooks_block.h"

static HANDLE g_log = INVALID_HANDLE_VALUE;
static HANDLE g_mmf = NULL;
static FFXHooksBlock* g_block = NULL;

extern "C" __declspec(dllexport) const char* FF10HgetName(void) {
    return "ffx-hooks (Jarvis Fase 0 safe stub)";
}

extern "C" __declspec(dllexport) const char* FF10HgetVer(void) {
    return "0.2-fase0-safe";
}

static void AppendLogLiteral(const char* text) {
    if (g_log == INVALID_HANDLE_VALUE || !text) {
        return;
    }

    DWORD len = 0;
    while (text[len] != '\0') {
        ++len;
    }

    DWORD written = 0;
    WriteFile(g_log, text, len, &written, NULL);
}

static void ClearBlock(FFXHooksBlock* block) {
    if (!block) {
        return;
    }

    volatile unsigned char* bytes = reinterpret_cast<volatile unsigned char*>(block);
    for (DWORD i = 0; i < sizeof(FFXHooksBlock); ++i) {
        bytes[i] = 0;
    }
}

static void AppendHex32(DWORD value) {
    static const char kHex[] = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; ++i) {
        const DWORD shift = static_cast<DWORD>((7 - i) * 4);
        buf[2 + i] = kHex[(value >> shift) & 0xF];
    }
    buf[10] = '\0';
    AppendLogLiteral(buf);
}

static void OpenLog(void) {
    char tmp[MAX_PATH];
    char path[MAX_PATH];
    tmp[0] = '\0';
    path[0] = '\0';
    GetTempPathA(MAX_PATH, tmp);
    lstrcpynA(path, tmp, MAX_PATH);
    lstrcatA(path, "ffx-hooks.log");

    g_log = CreateFileA(
        path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (g_log != INVALID_HANDLE_VALUE) {
        AppendLogLiteral("[ffx-hooks] Fase 0 safe stub log opened\n");
    }
}

static void CreateBlock(void) {
    g_mmf = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE,
        0,
        sizeof(FFXHooksBlock),
        FFXHOOKS_MMF_NAME);
    if (!g_mmf) {
        AppendLogLiteral("[ffx-hooks] WARN Fase 0 failed CreateFileMappingA err=");
        AppendHex32(GetLastError());
        AppendLogLiteral("\n");
        return;
    }

    g_block = static_cast<FFXHooksBlock*>(
        MapViewOfFile(g_mmf, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FFXHooksBlock)));
    if (!g_block) {
        AppendLogLiteral("[ffx-hooks] WARN Fase 0 failed MapViewOfFile err=");
        AppendHex32(GetLastError());
        AppendLogLiteral("\n");
        CloseHandle(g_mmf);
        g_mmf = NULL;
        return;
    }

    ClearBlock(g_block);
    g_block->magic = FFXHOOKS_MAGIC;
    g_block->version = FFXHOOKS_VERSION;
    g_block->musicOverrideTrackIndex = -1;

    AppendLogLiteral("[ffx-hooks] shared memory 'Local\\FFXHooksBlock_v1' ready (256 bytes)\n");
}

static void DestroyBlock(void) {
    if (g_block) {
        UnmapViewOfFile(g_block);
        g_block = NULL;
    }
    if (g_mmf) {
        CloseHandle(g_mmf);
        g_mmf = NULL;
    }
}

extern "C" BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD reason, LPVOID reserved) {
    (void)hinstDLL;
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        OpenLog();
        AppendLogLiteral("[ffx-hooks] DLL_PROCESS_ATTACH enter\n");
        CreateBlock();
        AppendLogLiteral("[ffx-hooks] Fase 0 skeleton loaded - no active hooks\n");
    } else if (reason == DLL_PROCESS_DETACH) {
        AppendLogLiteral("[ffx-hooks] DLL_PROCESS_DETACH enter\n");
        DestroyBlock();
        if (g_log != INVALID_HANDLE_VALUE) {
            CloseHandle(g_log);
            g_log = INVALID_HANDLE_VALUE;
        }
    }

    return TRUE;
}

extern "C" BOOL WINAPI DllMainCRTStartup(HINSTANCE hinstDLL, DWORD reason, LPVOID reserved) {
    return DllMain(hinstDLL, reason, reserved);
}
