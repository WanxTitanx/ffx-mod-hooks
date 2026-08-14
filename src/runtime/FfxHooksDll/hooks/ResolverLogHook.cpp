// ResolverLogHook — Arena+ Multi Dark Aeon spike (Fase 4): read-only logger detour
// on FFX_Field_ResolveEncounterToken@0x7828B0. See ResolverLogHook.h for behavior and
// docs/reverse/FFX_ARENA_PLUS_CUSTOM_TOKEN_RESOLVER_HOOK_SPIKE.md for the RE.
//
// Build path: dllmain.cpp links this conditionally on FFXHOOKS_HAVE_POLYHOOK. Without the
// polyhook stack the install function returns reasonCode=3 (no_polyhook) and the no-op stub
// keeps the DLL valid.

#include "ResolverLogHook.h"
#include "../shared/ffx_addresses.h"

#ifdef FFXHOOKS_HAVE_POLYHOOK
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <polyhook2/Detour/x86Detour.hpp>
#include <exception>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#endif

namespace FfxHooks {

#ifdef FFXHOOKS_HAVE_POLYHOOK

namespace {

constexpr long kDefaultMaxLoggedCalls = 256;
constexpr size_t kMaxRedirects = 64;

static PLH::x86Detour* g_detour = nullptr;
static uint64_t        g_trampoline = 0;
static ResolverLogFn   g_logFn = nullptr;
static bool            g_installed = false;
static volatile LONG   g_callCount = 0;
static long            g_maxLoggedCalls = kDefaultMaxLoggedCalls;

// Redirect table state (Opcao A). Updated under SetCustomTokenRedirects. The shim
// reads g_redirectCount with acquire semantics so a publisher (dllmain at boot) can
// race-safely populate it once. Mutation after boot is allowed but discouraged.
static CustomTokenRedirect g_redirects[kMaxRedirects] = {};
static volatile LONG       g_redirectCount = 0;
static volatile LONG       g_redirectEnabled = 0;
static volatile LONG       g_redirectHits = 0;

// Signature mirrors the IDA decompilation in docs/reverse/FFX_ARENA_PLUS_CUSTOM_TOKEN_RESOLVER_HOOK_SPIKE.md
//   unsigned __int8 *__cdecl FFX_Field_ResolveEncounterToken(int token, int *outField, _DWORD *outGroup, _DWORD *outEntry)
typedef unsigned char* (__cdecl *ResolveToken_t)(int token, int* outField, unsigned int* outGroup, unsigned int* outEntry);

static void HookLog(const char* fmt, ...) {
    if (!g_logFn) return;
    char line[512] = {};
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    g_logFn(line);
}

// Resolve a custom token -> alias token lookup. Returns 0 if no redirect applies.
// Linear scan; the table is tiny (kMaxRedirects=64) and only consulted on cache miss.
static uint32_t LookupRedirect(uint32_t token) {
    if (!InterlockedCompareExchange(&g_redirectEnabled, 0, 0)) return 0;
    const unsigned int hi = (token >> 16) & 0xFFFF;
    if (hi < 0xA001u || hi > 0xAFFFu) return 0;
    const LONG count = InterlockedCompareExchange(&g_redirectCount, 0, 0);
    for (LONG i = 0; i < count; ++i) {
        if (g_redirects[i].customToken == token) {
            return g_redirects[i].aliasToken;
        }
    }
    return 0;
}

// The shim must use __cdecl to match the target ABI exactly; PolyHook is told to detour
// the VA directly. Two paths:
//   - Custom token in the range with a redirect entry => swap the token before trampoline.
//   - Otherwise => call trampoline unchanged.
// Either way we log the call.
static unsigned char* __cdecl ResolverLog_Shim(int token, int* outField, unsigned int* outGroup, unsigned int* outEntry) {
    const uint32_t incoming = static_cast<uint32_t>(token);
    const uint32_t alias = LookupRedirect(incoming);
    const int effective = alias ? static_cast<int>(alias) : token;

    unsigned char* result = ((ResolveToken_t)g_trampoline)(effective, outField, outGroup, outEntry);

    const LONG n = InterlockedIncrement(&g_callCount);
    const bool didRedirect = alias != 0;
    if (didRedirect) InterlockedIncrement(&g_redirectHits);

    if (n <= g_maxLoggedCalls || didRedirect) {
        const int field = outField ? *outField : -1;
        const unsigned int group = outGroup ? *outGroup : 0xFFFFFFFFu;
        const unsigned int entry = outEntry ? *outEntry : 0xFFFFFFFFu;
        const unsigned int hi = (incoming >> 16) & 0xFFFF;
        const unsigned int lo = incoming & 0xFFFF;
        const bool customRange = (hi >= 0xA001u && hi <= 0xAFFFu);
        if (didRedirect) {
            HookLog(
                "[ffx-hooks] ResolverLog #%ld token=0x%08X (hi=0x%04X lo=0x%04X CUSTOM) REDIRECT -> alias=0x%08X "
                "result=%s outField=%d outGroup=%u outEntry=%u",
                static_cast<long>(n),
                incoming, hi, lo, alias,
                result ? "MATCH" : "MISS",
                field, group, entry);
        } else {
            HookLog(
                "[ffx-hooks] ResolverLog #%ld token=0x%08X (hi=0x%04X lo=0x%04X%s) -> %s "
                "outField=%d outGroup=%u outEntry=%u",
                static_cast<long>(n),
                incoming, hi, lo,
                customRange ? " CUSTOM" : "",
                result ? "MATCH" : "MISS",
                field, group, entry);
        }
    }
    return result;
}

static int ReadEnvInt(const char* name, int defaultValue) {
    char buf[32] = {};
    DWORD len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (!len || len >= sizeof(buf)) return defaultValue;
    char* end = nullptr;
    long v = strtol(buf, &end, 10);
    if (!end || end == buf) return defaultValue;
    return static_cast<int>(v);
}

} // namespace

ResolverLogInstallResult InstallResolverLogHook(uintptr_t base, ResolverLogFn log) {
    ResolverLogInstallResult result = { false, 0 };
    g_logFn = log;

    if (g_installed) {
        result.reasonCode = 2;
        return result;
    }

    const int envMax = ReadEnvInt("FFXHOOKS_ARENAPLUS_RESOLVER_LOG_MAX", static_cast<int>(kDefaultMaxLoggedCalls));
    g_maxLoggedCalls = envMax > 0 ? envMax : kDefaultMaxLoggedCalls;

    const uint64_t targetVa = static_cast<uint64_t>(base + RVA_FFX_FIELD_RESOLVE_ENCOUNTER_TOKEN);
    HookLog("[ffx-hooks] ResolverLog installing detour at VA=0x%08X (RVA=0x%08X) maxLogs=%ld",
        static_cast<unsigned>(targetVa),
        static_cast<unsigned>(RVA_FFX_FIELD_RESOLVE_ENCOUNTER_TOKEN),
        g_maxLoggedCalls);

    try {
        g_detour = new PLH::x86Detour(
            targetVa,
            reinterpret_cast<uint64_t>(&ResolverLog_Shim),
            &g_trampoline);
        const bool hooked = g_detour->hook();
        if (!hooked) {
            HookLog("[ffx-hooks] ResolverLog hook() returned false; aborting install");
            delete g_detour;
            g_detour = nullptr;
            g_trampoline = 0;
            result.reasonCode = 4;
            return result;
        }
        g_installed = true;
        result.ok = true;
        HookLog("[ffx-hooks] ResolverLog detour installed; trampoline=0x%llX",
            static_cast<unsigned long long>(g_trampoline));
    } catch (const std::exception& ex) {
        HookLog("[ffx-hooks] ResolverLog install exception: %s", ex.what());
        delete g_detour;
        g_detour = nullptr;
        g_trampoline = 0;
        result.reasonCode = 4;
    } catch (...) {
        HookLog("[ffx-hooks] ResolverLog install unknown exception");
        delete g_detour;
        g_detour = nullptr;
        g_trampoline = 0;
        result.reasonCode = 4;
    }
    return result;
}

void RemoveResolverLogHook() {
    if (!g_installed) return;
    if (g_detour) {
        g_detour->unHook();
        delete g_detour;
        g_detour = nullptr;
    }
    g_trampoline = 0;
    g_installed = false;
}

bool IsResolverLogHookInstalled() { return g_installed; }
long ResolverLogHookCallCount() { return static_cast<long>(InterlockedCompareExchange(&g_callCount, 0, 0)); }

size_t SetCustomTokenRedirects(const CustomTokenRedirect* table, size_t count) {
    // Atomically replace the table. While the publisher writes, the shim may read inconsistent
    // entries — but we always publish g_redirectCount LAST so the worst case is the shim sees
    // an old entry briefly. For this lab use (boot-time publish) that's acceptable.
    if (count > kMaxRedirects) count = kMaxRedirects;

    InterlockedExchange(&g_redirectCount, 0);
    size_t accepted = 0;
    for (size_t i = 0; i < count && table != nullptr; ++i) {
        const uint32_t hi = (table[i].customToken >> 16) & 0xFFFF;
        if (hi < 0xA001u || hi > 0xAFFFu) continue;
        g_redirects[accepted].customToken = table[i].customToken;
        g_redirects[accepted].aliasToken = table[i].aliasToken;
        ++accepted;
    }
    InterlockedExchange(&g_redirectCount, static_cast<LONG>(accepted));
    HookLog("[ffx-hooks] ResolverLog redirect table updated: accepted=%zu / submitted=%zu",
            accepted, count);
    return accepted;
}

bool SetCustomTokenRedirectEnabled(bool enabled) {
    const LONG prev = InterlockedExchange(&g_redirectEnabled, enabled ? 1 : 0);
    HookLog("[ffx-hooks] ResolverLog redirect %s (prev=%s)",
            enabled ? "ENABLED" : "DISABLED",
            prev ? "on" : "off");
    return prev != 0;
}

bool IsCustomTokenRedirectEnabled() {
    return InterlockedCompareExchange(&g_redirectEnabled, 0, 0) != 0;
}

long ResolverRedirectHitCount() {
    return static_cast<long>(InterlockedCompareExchange(&g_redirectHits, 0, 0));
}

#else // !FFXHOOKS_HAVE_POLYHOOK

// Without PolyHook we cannot install a detour. The stub keeps callers (dllmain) link-safe and
// reports reasonCode=3 so the parent DLL can log "spike requested but no detour stack".
ResolverLogInstallResult InstallResolverLogHook(uintptr_t /*base*/, ResolverLogFn /*log*/) {
    return { false, 3 };
}
void RemoveResolverLogHook() {}
bool IsResolverLogHookInstalled() { return false; }
long ResolverLogHookCallCount() { return 0; }

size_t SetCustomTokenRedirects(const CustomTokenRedirect* /*table*/, size_t /*count*/) { return 0; }
bool SetCustomTokenRedirectEnabled(bool /*enabled*/) { return false; }
bool IsCustomTokenRedirectEnabled() { return false; }
long ResolverRedirectHitCount() { return 0; }

#endif // FFXHOOKS_HAVE_POLYHOOK

} // namespace FfxHooks
