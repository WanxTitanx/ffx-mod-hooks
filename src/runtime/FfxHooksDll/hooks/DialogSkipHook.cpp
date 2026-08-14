#include "DialogSkipHook.h"
#include "../shared/ffx_addresses.h"
#include "../shared/Config.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <polyhook2/Detour/x86Detour.hpp>
#endif

namespace FfxHooks {

namespace {

static bool g_installed = false;
static uintptr_t g_base = 0;
static void (*g_logFn)(const char*) = nullptr;

static void HookLog(const char* fmt, ...) {
    if (!g_logFn) return;
    char line[512] = {};
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    g_logFn(line);
}

/* FFX_FmodVoice_ReadEventData @ 0x70B040 (RVA 0x30B040), __thiscall(this, evtId, a3) -> int.
 * O UnX legado escrevia `C2 08 00` (ret 8) no inicio, SEM gate de versao. Porte seguro:
 * return 0 imediato quando input.dialog_skip=1 (mesmo efeito: evento tratado como ausente). */
using ReadEventDataFn = int(__fastcall*)(void* this_, void* edx, unsigned int evtId, int a3);
static ReadEventDataFn g_readEventOriginal = nullptr;

static int __fastcall ReadEventData_DialogSkipHook(void* this_, void* edx, unsigned int evtId, int a3) {
    if (Config::GetBool("input.dialog_skip", false)) {
        return 0;  // skip: mesmo efeito do ret 8 do UnX, reversivel e com gate
    }
    return g_readEventOriginal(this_, edx, evtId, a3);
}

} // namespace

bool InstallDialogSkipHook(uintptr_t moduleBase, void* logFn) {
    if (g_installed) return true;
    g_base = moduleBase;
    g_logFn = (void (*)(const char*))logFn;

#ifdef FFXHOOKS_HAVE_POLYHOOK
    uint64_t tramp = 0;
    auto detour = new PLH::x86Detour(
        (uint64_t)(moduleBase + RVA_FFX_FMODVOICE_READ_EVENT_DATA),
        (uint64_t)&ReadEventData_DialogSkipHook,
        &tramp);
    if (!detour->hook()) {
        HookLog("[ffx-hooks] DialogSkipHook FAIL detour @ 0x%llX\n",
            (unsigned long long)(moduleBase + RVA_FFX_FMODVOICE_READ_EVENT_DATA));
        delete detour;
        return false;
    }
    g_readEventOriginal = (ReadEventDataFn)tramp;
    g_installed = true;
    HookLog("[ffx-hooks] DialogSkipHook installed (FmodVoice_ReadEventData; gate input.dialog_skip)\n");
    return true;
#else
    (void)moduleBase; (void)logFn;
    return false;
#endif
}

} // namespace FfxHooks