#include "NovaSuperDamageHook.h"
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

constexpr size_t   kPatchLen      = 5;

static const uint8_t kExpectedPatches[][kPatchLen] = {
    { 0x7E, 0x02, 0x8B, 0xC3, 0x89 }, /* Steam PC HD build (jle +2) */
    { 0x7E, 0x04, 0x8B, 0xC3, 0x89 }, /* alt layout (jle +4) */
};

static uint8_t              g_savedPatch[kPatchLen] = {};
static uint8_t*             g_stub                  = nullptr;
static size_t               g_stubLen               = 0;
static uintptr_t            g_patchVa                 = 0;
static uintptr_t            g_resumeVa                = 0;
static bool                 g_installed               = false;
static bool                 g_bypassActive            = false;
static bool                 g_logHits                 = false;
static NovaSuperDamageLogFn g_logFn                   = nullptr;
static volatile LONG        g_hitLogCount             = 0;

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

extern "C" void __cdecl NovaSuperDamage_LogPrecClamp(
    int32_t cmdId,
    int32_t eaxDmg,
    int32_t ebxCap,
    int32_t bypassed) {
    const LONG n = InterlockedIncrement(&g_hitLogCount);
    if (!g_logFn || n > 128) return;
    char line[256] = {};
    snprintf(
        line,
        sizeof(line),
        "[ffx-hooks] NovaClamp #%ld cmd=0x%04X eax_pre=%d ebx_cap=%d bypass=%d",
        static_cast<long>(n),
        static_cast<unsigned>(cmdId & 0xFFFF),
        eaxDmg,
        ebxCap,
        bypassed);
    g_logFn(line);
}

static void PatchRel32(std::vector<uint8_t>& code, size_t at, int32_t rel) {
    memcpy(&code[at], &rel, sizeof(rel));
}

static void PatchU32(std::vector<uint8_t>& code, size_t at, uint32_t value) {
    memcpy(&code[at], &value, sizeof(value));
}

static bool BuildStub(uintptr_t resumeVa, bool bypass, bool logHits, uint8_t** outStub, size_t* outLen) {
    std::vector<uint8_t> code;
    std::vector<size_t> jmpFixups;
    const auto emit = [&](std::initializer_list<uint8_t> bytes) {
        code.insert(code.end(), bytes.begin(), bytes.end());
    };
    const auto emitJmpResume = [&]() {
        emit({ 0xE9 });
        jmpFixups.push_back(code.size());
        emit({ 0x00, 0x00, 0x00, 0x00 });
    };
    const auto emitCallLog = [&](int32_t bypassedFlag) {
        const uintptr_t logVa = reinterpret_cast<uintptr_t>(&NovaSuperDamage_LogPrecClamp);
        emit({ 0x6A }); /* push imm8 bypassed */
        emit({ static_cast<uint8_t>(bypassedFlag & 0xFF) });
        emit({ 0x53 }); /* push ebx (cap) */
        emit({ 0x50 }); /* push eax (pre-clamp dmg) */
        emit({ 0xFF, 0x75, static_cast<uint8_t>(FFX_BATTLE_COMPUTE_HIT_DAMAGE_ARG_N12320) }); /* push n12320 */
        emit({ 0xE8 });
        const size_t relSite = code.size();
        emit({ 0x00, 0x00, 0x00, 0x00 });
        const int32_t rel = static_cast<int32_t>(logVa - (code.size()));
        PatchRel32(code, relSite, rel);
        emit({ 0x83, 0xC4, 0x10 }); /* add esp, 16 */
    };
    const auto emitCallLogPreserveEax = [&](int32_t bypassedFlag) {
        emit({ 0x50 }); /* push eax */
        emitCallLog(bypassedFlag);
        emit({ 0x58 }); /* pop eax */
    };

    /* Gate: cmp dword ptr [ebp+1Ch], 0x3073 — must not reuse those flags for jle below. */
    emit({ 0x81, 0x7D, static_cast<uint8_t>(FFX_BATTLE_COMPUTE_HIT_DAMAGE_ARG_N12320),
           0x73, 0x30, 0x00, 0x00 });
    const size_t jeNovaBypass = code.size();
    emit({ 0x0F, 0x84, 0x00, 0x00, 0x00, 0x00 }); /* je nova_bypass */

    const size_t vanillaClamp = code.size();
    /* Re-run cmp eax,ebx — hijacked jle used those flags; gate cmp clobbered them. */
    emit({ 0x3B, 0xC3 }); /* cmp eax, ebx */
    emit({ 0x7E });
    const size_t jleWriteback = code.size();
    emit({ 0x00 });
    emit({ 0x8B, 0xC3 }); /* mov eax, ebx */
    const size_t writeback = code.size();
    code[jleWriteback] = static_cast<uint8_t>(writeback - (jleWriteback + 1));
    emit({ 0x89, 0x06 }); /* mov [esi], eax */
    if (logHits && bypass) {
        emitCallLog(0); /* non-Nova hits only */
    }
    emitJmpResume();

    const size_t novaBypass = code.size();
    PatchRel32(code, jeNovaBypass + 2, static_cast<int32_t>(novaBypass - (jeNovaBypass + 6)));

    if (bypass) {
        emit({ 0x89, 0x06 }); /* mov [esi], eax — skip clamp for Nova */
        if (logHits) {
            emitCallLog(1);
        }
        emitJmpResume();
    } else if (logHits) {
        emitCallLogPreserveEax(0);
        emit({ 0xE9 });
        const size_t jmpVanilla = code.size();
        emit({ 0x00, 0x00, 0x00, 0x00 });
        const int32_t rel = static_cast<int32_t>(vanillaClamp - (jmpVanilla + 4));
        PatchRel32(code, jmpVanilla, rel);
    }

    uint8_t* stub = static_cast<uint8_t*>(VirtualAlloc(
        nullptr,
        code.size(),
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (!stub) return false;
    memcpy(stub, code.data(), code.size());
    for (size_t relSite : jmpFixups) {
        const int32_t rel = static_cast<int32_t>(resumeVa - (reinterpret_cast<uintptr_t>(stub) + relSite + 4));
        memcpy(stub + relSite, &rel, sizeof(rel));
    }
    FlushInstructionCache(GetCurrentProcess(), stub, code.size());

    *outStub = stub;
    *outLen = code.size();
    return true;
}

static bool BytesMatch(const uint8_t* actual, const uint8_t* expected, size_t len) {
    return memcmp(actual, expected, len) == 0;
}

static bool ResolveClampPatchSite(
    uintptr_t base,
    uintptr_t* patchVaOut,
    uintptr_t* resumeVaOut,
    uint8_t savedPatchOut[kPatchLen]) {
    if (!patchVaOut || !resumeVaOut || !savedPatchOut) return false;

    const uintptr_t defaultPatch = base + RVA_FFX_BATTLE_DAMAGE_CAP_CLAMP_JLE;
    uint8_t actual[kPatchLen] = {};
    memcpy(actual, reinterpret_cast<const void*>(defaultPatch), kPatchLen);
    for (size_t i = 0; i < sizeof(kExpectedPatches) / sizeof(kExpectedPatches[0]); ++i) {
        if (BytesMatch(actual, kExpectedPatches[i], kPatchLen)) {
            *patchVaOut = defaultPatch;
            *resumeVaOut = base + RVA_FFX_BATTLE_DAMAGE_POST_WRITEBACK;
            memcpy(savedPatchOut, kExpectedPatches[i], kPatchLen);
            return true;
        }
    }

    /* Fallback: anchor on mov ebx, 99999 then locate cmp/jle/mov clamp ahead. */
    const uint8_t bdlAnchor[] = { 0xBB, 0x9F, 0x86, 0x01, 0x00 };
    const uintptr_t scanStart = base + 0x380000u;
    const uintptr_t scanEnd = base + 0x3A0000u;
    for (uintptr_t p = scanStart; p + 64 < scanEnd; ++p) {
        if (!BytesMatch(reinterpret_cast<const uint8_t*>(p), bdlAnchor, sizeof(bdlAnchor))) {
            continue;
        }
        for (uintptr_t q = p; q + 16 < p + 0x120; ++q) {
            const uint8_t* b = reinterpret_cast<const uint8_t*>(q);
            if (b[0] == 0x3B && b[1] == 0xC3 && b[2] == 0x7E &&
                b[4] == 0x8B && b[5] == 0xC3 && b[6] == 0x89 && b[7] == 0x06) {
                const uintptr_t patchVa = q + 2;
                memcpy(actual, reinterpret_cast<const void*>(patchVa), kPatchLen);
                for (size_t i = 0; i < sizeof(kExpectedPatches) / sizeof(kExpectedPatches[0]); ++i) {
                    if (BytesMatch(actual, kExpectedPatches[i], kPatchLen)) {
                        *patchVaOut = patchVa;
                        *resumeVaOut = q + 8; /* after mov [esi], eax */
                        memcpy(savedPatchOut, kExpectedPatches[i], kPatchLen);
                        HookLog(
                            "[ffx-hooks] NovaClamp resolved via BDL anchor patch@0x%08X resume@0x%08X",
                            static_cast<unsigned>(patchVa),
                            static_cast<unsigned>(*resumeVaOut));
                        return true;
                    }
                }
            }
        }
    }

    HookLog(
        "[ffx-hooks] ERROR NovaClamp unexpected bytes @0x%08X: %02X %02X %02X %02X %02X",
        static_cast<unsigned>(defaultPatch),
        actual[0], actual[1], actual[2], actual[3], actual[4]);
    return false;
}

} // namespace

NovaSuperDamageInstallResult InstallNovaSuperDamageHook(
    uintptr_t base,
    bool bypass,
    bool logHits,
    NovaSuperDamageLogFn log) {
    NovaSuperDamageInstallResult result = { false, 0 };
    if (g_installed) {
        result.ok = true;
        result.stub = reinterpret_cast<uintptr_t>(g_stub);
        return result;
    }
    if (!bypass && !logHits) {
        HookLog("[ffx-hooks] NovaClamp install skipped: neither bypass nor log requested");
        return result;
    }

    g_logFn = log;
    g_bypassActive = bypass;
    g_logHits = logHits;
    g_hitLogCount = 0;

    if (!ResolveClampPatchSite(base, &g_patchVa, &g_resumeVa, g_savedPatch)) {
        return result;
    }

    if (!BuildStub(g_resumeVa, bypass, logHits, &g_stub, &g_stubLen)) {
        HookLog("[ffx-hooks] ERROR NovaClamp stub alloc failed");
        return result;
    }

    memcpy(g_savedPatch, reinterpret_cast<const void*>(g_patchVa), kPatchLen);

    const int32_t rel = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(g_stub) - (g_patchVa + 5));
    uint8_t jmpPatch[kPatchLen] = {
        0xE9,
        static_cast<uint8_t>(rel & 0xFF),
        static_cast<uint8_t>((rel >> 8) & 0xFF),
        static_cast<uint8_t>((rel >> 16) & 0xFF),
        static_cast<uint8_t>((rel >> 24) & 0xFF),
    };

    if (!MemWrite(reinterpret_cast<void*>(g_patchVa), jmpPatch, kPatchLen)) {
        HookLog("[ffx-hooks] ERROR NovaClamp patch write failed @0x%08X", static_cast<unsigned>(g_patchVa));
        VirtualFree(g_stub, 0, MEM_RELEASE);
        g_stub = nullptr;
        return result;
    }

    g_installed = true;
    result.ok = true;
    result.stub = reinterpret_cast<uintptr_t>(g_stub);
    HookLog(
        "[ffx-hooks] NovaClamp installed patch@0x%08X (rva 0x%08X) resume@0x%08X bypass=%d log=%d stub=0x%08X",
        static_cast<unsigned>(g_patchVa),
        static_cast<unsigned>(g_patchVa - base),
        static_cast<unsigned>(g_resumeVa),
        bypass ? 1 : 0,
        logHits ? 1 : 0,
        static_cast<unsigned>(result.stub));
    return result;
}

bool RemoveNovaSuperDamageHook(NovaSuperDamageLogFn log) {
    if (!g_installed) return true;

    bool restored = false;
    if (g_patchVa != 0 && g_savedPatch[0] != 0) {
        restored = MemWrite(reinterpret_cast<void*>(g_patchVa), g_savedPatch, kPatchLen);
        if (log) {
            char line[128] = {};
            snprintf(
                line,
                sizeof(line),
                "[ffx-hooks] NovaClamp patch restore %s @0x%08X",
                restored ? "ok" : "FAILED",
                static_cast<unsigned>(g_patchVa));
            log(line);
        }
    }

    if (g_stub) {
        VirtualFree(g_stub, 0, MEM_RELEASE);
        g_stub = nullptr;
        g_stubLen = 0;
    }

    g_installed = false;
    g_bypassActive = false;
    g_logHits = false;
    g_logFn = nullptr;
    g_patchVa = 0;
    g_resumeVa = 0;
    memset(g_savedPatch, 0, sizeof(g_savedPatch));
    return restored;
}

bool IsNovaSuperDamageHookInstalled() {
    return g_installed;
}

} // namespace FfxHooks
