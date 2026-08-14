#include "ItemStackCapHook.h"
#include "../shared/ffx_addresses.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <vector>

namespace FfxHooks {

namespace {

constexpr size_t kPatchLen = 5;

/*
 * Expected vanilla bytes at each clamp site. These are the displaced
 * instructions that the stub must replay. We sentinel-check them before
 * writing the jmp trampoline so we fail loud on build divergence.
 */
static const uint8_t kExpectedNew[kPatchLen] = {
    0x6A, 0x63,           /* push 63h        */
    0x6A, 0x00,           /* push 0          */
    0x53                  /* push ebx        */
};

static const uint8_t kExpectedExist[kPatchLen] = {
    0x6A, 0x63,           /* push 63h        */
    0x8D, 0x04, 0x1E      /* lea eax,[ebx+esi] */
};

static uint8_t        g_savedNew[kPatchLen]   = {};
static uint8_t        g_savedExist[kPatchLen] = {};
static uint8_t*       g_stubNew               = nullptr;
static size_t         g_stubNewLen            = 0;
static uint8_t*       g_stubExist             = nullptr;
static size_t         g_stubExistLen          = 0;
static uintptr_t      g_patchVaNew            = 0;
static uintptr_t      g_resumeVaNew           = 0;
static uintptr_t      g_patchVaExist          = 0;
static uintptr_t      g_resumeVaExist         = 0;
static uint8_t        g_capInstalled          = 0;
static bool           g_installed             = false;
static ItemStackCapLogFn g_logFn              = nullptr;

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

static bool BytesMatch(const uint8_t* actual, const uint8_t* expected, size_t len) {
    return memcmp(actual, expected, len) == 0;
}

/*
 * Stub layout for "new slot" site:
 *   68 FF 00 00 00     push imm32 <cap>     ; replaces the original `push 63h`
 *   6A 00              push 0               ; replays displaced byte
 *   53                 push ebx             ; replays displaced byte
 *   E9 ?? ?? ?? ??     jmp resumeNew        ; jump back to instruction after 5-byte window
 *
 * Stub layout for "existing slot" site:
 *   68 FF 00 00 00     push imm32 <cap>     ; replaces the original `push 63h`
 *   8D 04 1E           lea eax,[ebx+esi]    ; replays displaced bytes
 *   E9 ?? ?? ?? ??     jmp resumeExist
 */
static uint8_t* BuildStubNew(uint8_t cap, uintptr_t resumeVa, size_t* outLen) {
    constexpr size_t kStubLen = 5 /*push imm32*/ + 2 /*push 0*/ + 1 /*push ebx*/ + 5 /*jmp rel32*/;
    uint8_t* stub = static_cast<uint8_t*>(VirtualAlloc(
        nullptr, kStubLen, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!stub) return nullptr;

    stub[0] = 0x68;                        /* push imm32 */
    stub[1] = cap;
    stub[2] = 0x00;
    stub[3] = 0x00;
    stub[4] = 0x00;
    stub[5] = 0x6A;                        /* push 0 */
    stub[6] = 0x00;
    stub[7] = 0x53;                        /* push ebx */
    stub[8] = 0xE9;                        /* jmp rel32 */
    const int32_t rel = static_cast<int32_t>(resumeVa - (reinterpret_cast<uintptr_t>(stub) + 8 + 5));
    memcpy(stub + 9, &rel, sizeof(rel));

    FlushInstructionCache(GetCurrentProcess(), stub, kStubLen);
    *outLen = kStubLen;
    return stub;
}

static uint8_t* BuildStubExist(uint8_t cap, uintptr_t resumeVa, size_t* outLen) {
    constexpr size_t kStubLen = 5 /*push imm32*/ + 3 /*lea eax,[ebx+esi]*/ + 5 /*jmp rel32*/;
    uint8_t* stub = static_cast<uint8_t*>(VirtualAlloc(
        nullptr, kStubLen, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!stub) return nullptr;

    stub[0] = 0x68;                        /* push imm32 */
    stub[1] = cap;
    stub[2] = 0x00;
    stub[3] = 0x00;
    stub[4] = 0x00;
    stub[5] = 0x8D;                        /* lea eax,[ebx+esi] = 8D 04 1E */
    stub[6] = 0x04;
    stub[7] = 0x1E;
    stub[8] = 0xE9;                        /* jmp rel32 */
    const int32_t rel = static_cast<int32_t>(resumeVa - (reinterpret_cast<uintptr_t>(stub) + 8 + 5));
    memcpy(stub + 9, &rel, sizeof(rel));

    FlushInstructionCache(GetCurrentProcess(), stub, kStubLen);
    *outLen = kStubLen;
    return stub;
}

static bool PatchSite(
    uintptr_t patchVa,
    uintptr_t stubVa,
    uint8_t savedOut[kPatchLen]) {
    memcpy(savedOut, reinterpret_cast<const void*>(patchVa), kPatchLen);

    const int32_t rel = static_cast<int32_t>(stubVa - (patchVa + 5));
    uint8_t jmpPatch[kPatchLen] = {
        0xE9,
        static_cast<uint8_t>(rel & 0xFF),
        static_cast<uint8_t>((rel >> 8) & 0xFF),
        static_cast<uint8_t>((rel >> 16) & 0xFF),
        static_cast<uint8_t>((rel >> 24) & 0xFF),
    };
    return MemWrite(reinterpret_cast<void*>(patchVa), jmpPatch, kPatchLen);
}

} // namespace

ItemStackCapInstallResult InstallItemStackCapHook(
    uintptr_t base,
    uint8_t cap,
    bool logInstall,
    ItemStackCapLogFn log) {
    ItemStackCapInstallResult result = { false, 0, 0 };
    if (g_installed) {
        result.ok = true;
        result.stubNew = reinterpret_cast<uintptr_t>(g_stubNew);
        result.stubExist = reinterpret_cast<uintptr_t>(g_stubExist);
        return result;
    }

    g_logFn = log;
    if (cap == 0) cap = 1;
    g_capInstalled = cap;

    g_patchVaNew     = base + RVA_FFX_INVENTORY_ADD_ITEM_CLAMP_NEW;
    g_resumeVaNew    = base + RVA_FFX_INVENTORY_ADD_ITEM_CLAMP_NEW_RESUME;
    g_patchVaExist   = base + RVA_FFX_INVENTORY_ADD_ITEM_CLAMP_EXIST;
    g_resumeVaExist  = base + RVA_FFX_INVENTORY_ADD_ITEM_CLAMP_EXIST_RESUME;

    /* Sentinel check: confirm vanilla bytes before patching. Defends against build divergence. */
    if (!BytesMatch(reinterpret_cast<const uint8_t*>(g_patchVaNew), kExpectedNew, kPatchLen)) {
        const uint8_t* a = reinterpret_cast<const uint8_t*>(g_patchVaNew);
        HookLog(
            "[ffx-hooks] ERROR ItemStackCap unexpected bytes @0x%08X (new site): %02X %02X %02X %02X %02X",
            static_cast<unsigned>(g_patchVaNew),
            a[0], a[1], a[2], a[3], a[4]);
        return result;
    }
    if (!BytesMatch(reinterpret_cast<const uint8_t*>(g_patchVaExist), kExpectedExist, kPatchLen)) {
        const uint8_t* a = reinterpret_cast<const uint8_t*>(g_patchVaExist);
        HookLog(
            "[ffx-hooks] ERROR ItemStackCap unexpected bytes @0x%08X (existing site): %02X %02X %02X %02X %02X",
            static_cast<unsigned>(g_patchVaExist),
            a[0], a[1], a[2], a[3], a[4]);
        return result;
    }

    g_stubNew = BuildStubNew(cap, g_resumeVaNew, &g_stubNewLen);
    if (!g_stubNew) {
        HookLog("[ffx-hooks] ERROR ItemStackCap stub#1 alloc failed");
        return result;
    }
    g_stubExist = BuildStubExist(cap, g_resumeVaExist, &g_stubExistLen);
    if (!g_stubExist) {
        HookLog("[ffx-hooks] ERROR ItemStackCap stub#2 alloc failed");
        VirtualFree(g_stubNew, 0, MEM_RELEASE);
        g_stubNew = nullptr;
        return result;
    }

    if (!PatchSite(g_patchVaNew, reinterpret_cast<uintptr_t>(g_stubNew), g_savedNew)) {
        HookLog("[ffx-hooks] ERROR ItemStackCap patch write failed @0x%08X (new)",
            static_cast<unsigned>(g_patchVaNew));
        VirtualFree(g_stubNew, 0, MEM_RELEASE);
        VirtualFree(g_stubExist, 0, MEM_RELEASE);
        g_stubNew = nullptr;
        g_stubExist = nullptr;
        return result;
    }
    if (!PatchSite(g_patchVaExist, reinterpret_cast<uintptr_t>(g_stubExist), g_savedExist)) {
        HookLog("[ffx-hooks] ERROR ItemStackCap patch write failed @0x%08X (existing); rolling back new site",
            static_cast<unsigned>(g_patchVaExist));
        /* Roll back the new-site patch so we don't leave half-installed. */
        MemWrite(reinterpret_cast<void*>(g_patchVaNew), g_savedNew, kPatchLen);
        VirtualFree(g_stubNew, 0, MEM_RELEASE);
        VirtualFree(g_stubExist, 0, MEM_RELEASE);
        g_stubNew = nullptr;
        g_stubExist = nullptr;
        return result;
    }

    g_installed = true;
    result.ok = true;
    result.stubNew = reinterpret_cast<uintptr_t>(g_stubNew);
    result.stubExist = reinterpret_cast<uintptr_t>(g_stubExist);

    if (logInstall) {
        HookLog(
            "[ffx-hooks] ItemStackCap installed cap=%u patch_new@0x%08X (rva 0x%08X) stub_new=0x%08X "
            "patch_exist@0x%08X (rva 0x%08X) stub_exist=0x%08X",
            static_cast<unsigned>(cap),
            static_cast<unsigned>(g_patchVaNew),
            static_cast<unsigned>(g_patchVaNew - base),
            static_cast<unsigned>(result.stubNew),
            static_cast<unsigned>(g_patchVaExist),
            static_cast<unsigned>(g_patchVaExist - base),
            static_cast<unsigned>(result.stubExist));
    }
    return result;
}

bool RemoveItemStackCapHook(ItemStackCapLogFn log) {
    if (!g_installed) return true;

    bool restoredNew = true;
    bool restoredExist = true;

    if (g_patchVaNew != 0) {
        restoredNew = MemWrite(reinterpret_cast<void*>(g_patchVaNew), g_savedNew, kPatchLen);
        if (log) {
            char line[160] = {};
            snprintf(line, sizeof(line),
                "[ffx-hooks] ItemStackCap patch restore %s @0x%08X (new)",
                restoredNew ? "ok" : "FAILED",
                static_cast<unsigned>(g_patchVaNew));
            log(line);
        }
    }
    if (g_patchVaExist != 0) {
        restoredExist = MemWrite(reinterpret_cast<void*>(g_patchVaExist), g_savedExist, kPatchLen);
        if (log) {
            char line[160] = {};
            snprintf(line, sizeof(line),
                "[ffx-hooks] ItemStackCap patch restore %s @0x%08X (existing)",
                restoredExist ? "ok" : "FAILED",
                static_cast<unsigned>(g_patchVaExist));
            log(line);
        }
    }

    if (g_stubNew) {
        VirtualFree(g_stubNew, 0, MEM_RELEASE);
        g_stubNew = nullptr;
        g_stubNewLen = 0;
    }
    if (g_stubExist) {
        VirtualFree(g_stubExist, 0, MEM_RELEASE);
        g_stubExist = nullptr;
        g_stubExistLen = 0;
    }

    g_installed = false;
    g_capInstalled = 0;
    g_logFn = nullptr;
    g_patchVaNew = 0;
    g_resumeVaNew = 0;
    g_patchVaExist = 0;
    g_resumeVaExist = 0;
    memset(g_savedNew, 0, sizeof(g_savedNew));
    memset(g_savedExist, 0, sizeof(g_savedExist));
    return restoredNew && restoredExist;
}

bool IsItemStackCapHookInstalled() {
    return g_installed;
}

uint8_t GetItemStackCapInstalledCap() {
    return g_capInstalled;
}

} // namespace FfxHooks
