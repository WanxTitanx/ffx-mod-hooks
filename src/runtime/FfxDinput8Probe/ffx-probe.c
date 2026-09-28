/* ffx-probe.dll — FFX DINPUT8 in-process probe (Fase 1).
 *
 * Loaded by the "Final Fantasy X Module Loader" (dinput8.dll) from modules\.
 * Honest main-thread seam: the game polls input every frame on its MAIN THREAD via
 * IDirectInputDevice8::GetDeviceState (vtable[9], proven in IDA Fase 0). We patch that
 * shared vtable slot, so our executor runs on the main thread, per frame — NO CreateRemoteThread.
 *
 * Acquisition is race-free: instead of hooking CreateDevice (which the game calls during
 * startup, hard to interleave), we wait for the game's IDirectInput8* to appear, then create
 * our OWN throwaway keyboard device just to grab the SHARED device vtable, patch GetDeviceState,
 * and release it. The game's keyboard device shares that vtable -> its poll is now hooked.
 *
 * STANDBY by default: the executor only runs a command when the editor increments `seq`.
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "ffx_probe_block.h"

/* g_FFX_Input_DirectInput8 — IDA VA 0xCC9CD4, image base 0x400000 -> RVA 0x8C9CD4 */
#define RVA_DINPUT8_PTR 0x008C9CD4u
#define RVA_SOUND_COMMAND_MANAGER 0x008E9000u
#define RVA_SOUND_REGIST_COMMAND_SYNC 0x002FA5E0u

/* GUID_SysKeyboard (dinput) */
static const GUID kGuidSysKeyboard =
    { 0x6F1D2B61, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };

/* IDirectInput8 vtable: [3]=CreateDevice.  IDirectInputDevice8 vtable: [2]=Release,[9]=GetDeviceState. */
typedef long (__stdcall *CreateDevice_t)(void *self, const GUID *rguid, void **out, void *unk);
typedef long (__stdcall *GetDeviceState_t)(void *self, uint32_t cb, void *data);
typedef unsigned long (__stdcall *Release_t)(void *self);

static FFXProbeBlock  *g_block;
static uint8_t        *g_base;
static GetDeviceState_t g_origGetDeviceState;
static volatile long   g_inCmd;   /* re-entrancy guard for CALL */
/* persistent forged-input latch (set by FFXPROBE_OP_SETINPUT, applied every frame in the hook) */
static volatile uint32_t g_finMask;   /* FFXIN_* intents */
static volatile uint32_t g_finKey1;   /* raw DIK keycode to also hold (0 = none) */
static volatile uint32_t g_finKey2;
static volatile uint32_t g_frameCtr;

#define U1_PRO_LEN_VECTOR 10u
#define U1_PRO_LEN_ANGMOVE 9u
#define U1_PRO_LEN_SIMPLE 6u
#define U1_RING_N 64u
typedef struct {
    uint32_t rva;
    uint32_t proLen;
} U1FamilyConfig;

static const U1FamilyConfig kU1Families[] = {
    { 0u, 0u },
    { 0x0035C090u, U1_PRO_LEN_VECTOR },
    { 0x0035B9F0u, U1_PRO_LEN_VECTOR },
    { 0x0035BFE0u, U1_PRO_LEN_ANGMOVE },
    { 0x0035BED0u, U1_PRO_LEN_VECTOR },
    { 0x0035B830u, U1_PRO_LEN_VECTOR },
    { 0x0035B940u, U1_PRO_LEN_SIMPLE },
    { 0x0035C540u, U1_PRO_LEN_VECTOR },
    { 0x0035CF20u, U1_PRO_LEN_VECTOR },
    { 0x0035D0D0u, U1_PRO_LEN_VECTOR },
};
#define U1_FAMILY_MAX ((uint32_t)(sizeof(kU1Families) / sizeof(kU1Families[0]) - 1u))
static FFXProbeU1Record g_u1Ring[U1_RING_N];
static volatile uint32_t g_u1Count;
static volatile uint32_t g_u1On;
static uint32_t g_u1Family;
static uint8_t *g_u1Tramp;
static void *g_u1DetourPtr;
static uint8_t *g_u1TrampBackPtr;
static uint32_t g_u1HookAbs;
static uint32_t g_u1ProLen;
static uint8_t g_u1Orig[U1_PRO_LEN_VECTOR];
static int g_u1Installed;

static void __cdecl U1Log(uint32_t context, uint32_t program, uint32_t slot) {
    FFXProbeU1Record *record;
    uint8_t programBytes[32];
    uint32_t index, byteIndex, existingIndex, nonzero;
    if (!g_u1On || !context || !program) return;
    __try {
        if (*(uint32_t *)(uintptr_t)program != *(uint32_t *)(uintptr_t)(context + 0x0Cu)) return;
        memcpy(programBytes, (const void *)(uintptr_t)program, sizeof(programBytes));
        nonzero = 0;
        for (byteIndex = 16u; byteIndex < 32u; byteIndex++) {
            if (programBytes[byteIndex]) { nonzero = 1; break; }
        }
        if (!nonzero) return;
        index = g_u1Count;
        for (existingIndex = 0; existingIndex < index; existingIndex++) {
            record = &g_u1Ring[existingIndex];
            if (record->program == program && memcmp(record->programBytes, programBytes, sizeof(programBytes)) == 0) return;
        }
        if (index >= U1_RING_N) { g_u1On = 0; return; }
        record = &g_u1Ring[index];
        memcpy(record->programBytes, programBytes, sizeof(programBytes));
        record->frame = g_frameCtr;
        record->family = g_u1Family;
        record->context = context;
        record->program = program;
        record->slot = slot;
        g_u1Count = index + 1u;
        if (g_u1Count >= U1_RING_N) g_u1On = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
}

static int __cdecl U1DetourC(uint32_t context, uint32_t program, uint32_t slot) {
    typedef int (__cdecl *U1Fn)(uint32_t, uint32_t, uint32_t);
    U1Log(context, program, slot);
    return ((U1Fn)g_u1Tramp)(context, program, slot);
}

static void UninstallU1Hook(void) {
    DWORD old = 0;
    uint8_t *target;
    if (!g_u1Installed || !g_u1HookAbs) return;
    target = (uint8_t *)(uintptr_t)g_u1HookAbs;
    VirtualProtect(target, g_u1ProLen, PAGE_EXECUTE_READWRITE, &old);
    memcpy(target, g_u1Orig, g_u1ProLen);
    VirtualProtect(target, g_u1ProLen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, g_u1ProLen);
    if (g_u1Tramp) VirtualFree(g_u1Tramp, 0, MEM_RELEASE);
    g_u1Tramp = NULL;
    g_u1HookAbs = 0;
    g_u1ProLen = 0;
    g_u1Installed = 0;
    g_u1On = 0;
}

static int InstallU1Hook(uint32_t family) {
    DWORD old = 0;
    uint8_t *target;
    uint32_t byteIndex;
    const U1FamilyConfig *config;
    if (family < 1u || family > U1_FAMILY_MAX) return 0;
    if (g_u1Installed) return g_u1Family == family;
    config = &kU1Families[family];
    target = g_base + config->rva;
    g_u1ProLen = config->proLen;
    g_u1Tramp = (uint8_t *)VirtualAlloc(NULL, 64u, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_u1Tramp) return 0;
    g_u1Family = family;
    g_u1DetourPtr = (void *)U1DetourC;
    g_u1TrampBackPtr = target + g_u1ProLen;
    g_u1HookAbs = (uint32_t)(uintptr_t)target;
    memcpy(g_u1Orig, target, g_u1ProLen);
    memcpy(g_u1Tramp, target, g_u1ProLen);
    g_u1Tramp[g_u1ProLen] = 0xFF;
    g_u1Tramp[g_u1ProLen + 1u] = 0x25;
    *(uint32_t *)(g_u1Tramp + g_u1ProLen + 2u) = (uint32_t)(uintptr_t)&g_u1TrampBackPtr;
    VirtualProtect(target, g_u1ProLen, PAGE_EXECUTE_READWRITE, &old);
    target[0] = 0xFF;
    target[1] = 0x25;
    *(uint32_t *)(target + 2u) = (uint32_t)(uintptr_t)&g_u1DetourPtr;
    for (byteIndex = 6u; byteIndex < g_u1ProLen; byteIndex++) target[byteIndex] = 0x90;
    VirtualProtect(target, g_u1ProLen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, g_u1ProLen);
    g_u1Installed = 1;
    return 1;
}

__declspec(noinline) static int CallThiscall5(void *fn, void *self, int a0, int a1, int a2, int a3, int a4)
{
    int rv;
    __asm {
        push a4
        push a3
        push a2
        push a1
        push a0
        mov ecx, self
        mov eax, fn
        call eax
        mov rv, eax
    }
    return rv;
}

/* ---- texture-LOAD auto-logger: inline-hook FFX_Ps3Data_BuildTextureSlotRecord_LoadTime (RVA 0x2451F0) ----
 * The UNIVERSAL load-time texture-slot builder (~40 callers = all PS3Data texture-list loaders). Fires ONCE
 * per texture-unit when an area/texture-list loads (NOT per draw). arg5 'Source' (entry [esp+0x18]) = the
 * FULL resolved path C-string, e.g. ".../PS3Data/map/azit/azit03/fp/tex/GCM/<tile>.dds.phyre" -> this covers
 * the 3D MAP textures (the per-draw lane 0x67DAC0 only ever saw the UI sprite). 6-byte reloc-free prologue
 * (push ebp; mov ebp,esp; mov ecx,[ebp+8]) relocated to a trampoline; head patched with FF 25 (abs-indirect,
 * no rel32 range issue). We install/arm from the GetDeviceState op (main thread) -> no patch race. */
#define RVA_TEXLOAD      0x002451F0u
#define TEXLOG_PRO_LEN   6u
#define TEXLOG_RING_N    16384u
typedef struct { uint32_t frame; char name[124]; } TexRec;   /* 128 bytes; name = full texture path */
static TexRec            g_texRing[TEXLOG_RING_N];
static volatile uint32_t g_texCount;     /* records written (<= TEXLOG_RING_N) */
static volatile uint32_t g_texOn;        /* 1 = armed */
static uint8_t          *g_texTramp;     /* [6 orig bytes][FF 25 -> target+6] */
static void             *g_detourPtr;    /* = TexDetour (target of the head FF 25)   */
static uint8_t          *g_trampBackPtr; /* = target+6   (target of the tramp FF 25) */
static int               g_texInstalled;

/* logger: 'name' is the Source path C-string passed straight in (arg5). Log every non-empty name;
 * any filter (e.g. require "/map/") is applied offline on the host. */
__declspec(noinline) static void __cdecl TexLog(const char *name) {
    if (!g_texOn || !name) return;
    __try {
        uint32_t i; TexRec *r; int j;
        i = g_texCount;
        if (i >= TEXLOG_RING_N) { g_texOn = 0; return; }
        r = &g_texRing[i];
        for (j = 0; j < (int)sizeof(r->name) - 1 && name[j]; j++) r->name[j] = name[j];
        r->name[j] = 0;
        if (j == 0) return;                /* empty name: don't count */
        r->frame = g_frameCtr;
        g_texCount = i + 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { /* bad deref: drop this record */ }
}

/* reached via the head FF 25 jmp (NOT a call) -> stack == original entry. Source (name) is arg5 at
 * entry [esp+0x18]; after pushfd(4)+pushad(32)=0x24 it is [esp+0x3C]. Restore all, then run the trampoline. */
static __declspec(naked) void TexDetour(void) {
    __asm {
        pushfd
        pushad
        mov  eax, [esp+0x3C]
        push eax
        call TexLog
        add  esp, 4
        popad
        popfd
        mov  eax, g_texTramp   /* eax is dead at the real fn head (it does mov ecx,[ebp+8] next) */
        jmp  eax
    }
}

static void InstallTexHook(void) {
    uint8_t *target;
    DWORD old = 0;
    uint32_t k;
    if (g_texInstalled) return;
    target = g_base + RVA_TEXLOAD;
    g_texTramp = (uint8_t *)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_texTramp) return;
    g_detourPtr    = (void *)TexDetour;
    g_trampBackPtr = target + TEXLOG_PRO_LEN;
    /* trampoline = [6 live (already relocated) prologue bytes] + [FF 25 &g_trampBackPtr] */
    memcpy(g_texTramp, target, TEXLOG_PRO_LEN);
    g_texTramp[TEXLOG_PRO_LEN + 0] = 0xFF;
    g_texTramp[TEXLOG_PRO_LEN + 1] = 0x25;
    *(uint32_t *)(g_texTramp + TEXLOG_PRO_LEN + 2) = (uint32_t)(uintptr_t)&g_trampBackPtr;
    /* patch head: FF 25 &g_detourPtr (jmp dword ptr [&g_detourPtr]); 6 bytes == prologue, no NOP pad */
    VirtualProtect(target, TEXLOG_PRO_LEN, PAGE_EXECUTE_READWRITE, &old);
    target[0] = 0xFF;
    target[1] = 0x25;
    *(uint32_t *)(target + 2) = (uint32_t)(uintptr_t)&g_detourPtr;
    for (k = 6; k < TEXLOG_PRO_LEN; k++) target[k] = 0x90;   /* none: PRO_LEN==6 */
    VirtualProtect(target, TEXLOG_PRO_LEN, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, TEXLOG_PRO_LEN);
    g_texInstalled = 1;
}

/* ---- KeThRes attach logger: inline-hook FFX_MagicHost_LinkResourceBufferRange (RVA 0x32C570) ---- */
#define RVA_KETHRES_LINK 0x0032C570u
#define KETHRES_PRO_LEN  6u
#define KETHRES_RING_N   4096u
#define KETHRES_SNAP_BYTES 4096u
typedef struct { uint32_t frame; uint32_t buf; uint32_t size; uint32_t handle; } KeThResRec;
static KeThResRec            g_kethresRing[KETHRES_RING_N];
static volatile uint32_t     g_kethresCount;
static volatile uint32_t     g_kethresOn;
static uint8_t               g_kethresSnap[KETHRES_SNAP_BYTES];
static volatile uint32_t     g_kethresSnapBuf;
static volatile uint32_t     g_kethresSnapNz;
static volatile uint32_t     g_kethresSnapCount;
static uint8_t              *g_kethresTramp;
static void                 *g_kethresDetourPtr;
static uint8_t              *g_kethresTrampBackPtr;
static int                   g_kethresInstalled;

static void __cdecl KeThResLog(uint32_t handle, uint32_t buf, uint32_t size);
static void __cdecl KeThResSnapPost(uint32_t handle, uint32_t buf, uint32_t size);

__declspec(noinline) static void __cdecl KeThResSnapPost(uint32_t handle, uint32_t buf, uint32_t size) {
    uint32_t k, nz;
    if (!g_kethresOn || !buf || size != 0x100000u || !handle) return;
    if (buf != handle + 0x3Cu) return;
    __try {
        memcpy(g_kethresSnap, (const void *)(uintptr_t)buf, KETHRES_SNAP_BYTES);
        g_kethresSnapBuf = buf;
        nz = 0;
        for (k = 0; k < KETHRES_SNAP_BYTES; k++) if (g_kethresSnap[k]) nz++;
        g_kethresSnapNz = nz;
        g_kethresSnapCount++;
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
}

/* cdecl thunk: run relocated prologue+rest, THEN snap decoded blob. */
static int __cdecl KeThResDetourC(void *a1, void *a2, int a3) {
    typedef int (__cdecl *Fn)(void *, void *, int);
    int r = ((Fn)g_kethresTramp)(a1, a2, a3);
    KeThResLog((uint32_t)(uintptr_t)a1, (uint32_t)(uintptr_t)a2, (uint32_t)a3);
    KeThResSnapPost((uint32_t)(uintptr_t)a1, (uint32_t)(uintptr_t)a2, (uint32_t)a3);
    return r;
}

__declspec(noinline) static void __cdecl KeThResLog(uint32_t handle, uint32_t buf, uint32_t size) {
    if (!g_kethresOn || !buf) return;
    __try {
        uint32_t i; KeThResRec *r;
        i = g_kethresCount;
        if (i >= KETHRES_RING_N) { g_kethresOn = 0; return; }
        r = &g_kethresRing[i];
        r->frame = g_frameCtr;
        r->buf = buf;
        r->size = size;
        r->handle = handle;
        g_kethresCount = i + 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
}

static void InstallKeThResHook(void) {
    uint8_t *target;
    DWORD old = 0;
    uint32_t k;
    if (g_kethresInstalled) return;
    target = g_base + RVA_KETHRES_LINK;
    g_kethresTramp = (uint8_t *)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_kethresTramp) return;
    g_kethresDetourPtr = (void *)KeThResDetourC;
    g_kethresTrampBackPtr = target + KETHRES_PRO_LEN;
    memcpy(g_kethresTramp, target, KETHRES_PRO_LEN);
    g_kethresTramp[KETHRES_PRO_LEN + 0] = 0xFF;
    g_kethresTramp[KETHRES_PRO_LEN + 1] = 0x25;
    *(uint32_t *)(g_kethresTramp + KETHRES_PRO_LEN + 2) = (uint32_t)(uintptr_t)&g_kethresTrampBackPtr;
    VirtualProtect(target, KETHRES_PRO_LEN, PAGE_EXECUTE_READWRITE, &old);
    target[0] = 0xFF;
    target[1] = 0x25;
    *(uint32_t *)(target + 2) = (uint32_t)(uintptr_t)&g_kethresDetourPtr;
    for (k = 6; k < KETHRES_PRO_LEN; k++) target[k] = 0x90;
    VirtualProtect(target, KETHRES_PRO_LEN, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, KETHRES_PRO_LEN);
    g_kethresInstalled = 1;
}

#if !FFXPROBE_PPPDRAW_RETIRED
/* ---- PPP draw tint hook: sub_71B980 ApplyPppDrawableColors (true entry) ---- */
#define RVA_PPPDRAW_APPLY      0x0031B980u /* sub_71B980 @ EA 0x71B980, image base 0x400000 */
#define RVA_HOST_BIND_SLOT     0x00865810u /* off_C65810 host+2856 — fallback only */
/* prologue 9B (push ebp; mov ebp,esp; sub esp,8Ch) + mov eax,cookie 5B = 14B stolen */
#define PPPDRAW_PRO_LEN    14u
#define PPPDRAW_PRO_STOLEN 14u
#define PPPDRAW_RING_N     4096u
typedef struct {
    uint32_t frame;
    uint32_t a0, a1, a2, a3;
    float    tintR, tintG, tintB;
} PppDrawRec;
static PppDrawRec            g_pppdrawRing[PPPDRAW_RING_N];
static volatile uint32_t     g_pppdrawCount;
static volatile uint32_t     g_pppdrawOn;
static volatile uint32_t     g_pppdrawForce;
static float                 g_pppdrawForceR = 1.0f;
static float                 g_pppdrawForceG = 0.35f;
static float                 g_pppdrawForceB = 0.05f;
static float                 g_pppdrawForceA = 1.0f;
static uint8_t              *g_pppdrawTramp;
static void                 *g_pppdrawDetourPtr;
static uint8_t              *g_pppdrawTrampBackPtr;
static uint32_t              g_pppdrawHookAbs;
static int                   g_pppdrawInstalled;

static int PppDrawIsPtr(uint32_t p) {
    return p >= 0x01000000u && p < 0x7FFE0000u;
}

static int PppDrawLooksEntry(uint8_t *p) {
    return p && p[0] == 0x55 && p[1] == 0x8B && p[2] == 0xEC;
}

static uint8_t               g_pppdrawOrig[PPPDRAW_PRO_LEN];

static int PppDrawInModule(uint32_t addr) {
    return addr >= (uint32_t)(uintptr_t)g_base && addr < (uint32_t)(uintptr_t)g_base + 0x02000000u;
}

static void __cdecl PppDrawTryForceTint(uint32_t ptr) {
    float *f;
    int off;
    if (!g_pppdrawForce || !PppDrawIsPtr(ptr)) return;
    __try {
        for (off = 0; off <= 16; off += 4) {
            f = (float *)(uintptr_t)(ptr + (uint32_t)off);
            if (f[0] >= -0.05f && f[0] <= 1.6f && f[2] >= -0.05f && f[2] <= 1.6f) {
                f[0] = g_pppdrawForceR;
                f[1] = g_pppdrawForceG;
                f[2] = g_pppdrawForceB;
                f[3] = g_pppdrawForceA;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
}

static void __cdecl PppDrawReadTint(uint32_t ptr, float *outR, float *outG, float *outB, float *outA) {
    *outR = *outG = *outB = 0.f; *outA = 1.f;
    if (!PppDrawIsPtr(ptr)) return;
    __try {
        float *f = (float *)(uintptr_t)(ptr + 4u);
        *outR = f[0]; *outG = f[1]; *outB = f[2]; *outA = f[3];
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
}

__declspec(noinline) static void __cdecl PppDrawLogTint(uint32_t tintStruct, uint32_t a1_lo, uint32_t a1_hi, uint32_t a2) {
    uint32_t i; PppDrawRec *r; float ab;
    if (!g_pppdrawOn) return;
    if (!PppDrawIsPtr(tintStruct)) {
        /* a3 not always tint struct — skip log, still call through */
    } else if (g_pppdrawForce) {
        PppDrawTryForceTint(tintStruct + 4u);   /* IDA: [ebx+4] vec4 -> draw cmd +0xC8 */
        PppDrawTryForceTint(tintStruct + 0x10u);
    }
    if (!PppDrawIsPtr(tintStruct)) return;
    __try {
        i = g_pppdrawCount;
        if (i >= PPPDRAW_RING_N) { g_pppdrawOn = 0; return; }
        r = &g_pppdrawRing[i];
        r->frame = g_frameCtr;
        r->a0 = a1_lo; r->a1 = a1_hi; r->a2 = a2; r->a3 = tintStruct;
        PppDrawReadTint(tintStruct + 4u, &r->tintR, &r->tintG, &r->tintB, &ab);
        g_pppdrawCount = i + 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
}

/* sub_71B980(__int64 a1, int *a2, int a3, uint16_t a4) — a3 = tint struct ptr */
static int __cdecl PppDrawDetour71B980(
    uint32_t a1_lo, uint32_t a1_hi, int *a2, int a3, uint16_t a4)
{
    PppDrawLogTint((uint32_t)a3, a1_lo, a1_hi, (uint32_t)(uintptr_t)a2);
    typedef int (__cdecl *Fn)(uint32_t, uint32_t, int *, int, uint16_t);
    return ((Fn)g_pppdrawTramp)(a1_lo, a1_hi, a2, a3, a4);
}

static int __cdecl PppDrawDetourBind(void *a0, void *a1, void *a2, void *a3) {
    /* legacy bind-slot hook — wrong arg layout; kept for fallback logging only */
    PppDrawLogTint((uint32_t)(uintptr_t)a2, (uint32_t)(uintptr_t)a0, (uint32_t)(uintptr_t)a1, 0);
    typedef int (__cdecl *Fn)(void *, void *, void *, void *);
    return ((Fn)g_pppdrawTramp)(a0, a1, a2, a3);
}

static void UninstallPppDrawHook(void) {
    uint8_t *target;
    DWORD old = 0;
    if (!g_pppdrawInstalled || !g_pppdrawTramp) return;
    target = (uint8_t *)(uintptr_t)g_pppdrawHookAbs;
    VirtualProtect(target, PPPDRAW_PRO_LEN, PAGE_EXECUTE_READWRITE, &old);
    memcpy(target, g_pppdrawOrig, PPPDRAW_PRO_LEN);
    VirtualProtect(target, PPPDRAW_PRO_LEN, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, PPPDRAW_PRO_LEN);
    VirtualFree(g_pppdrawTramp, 0, MEM_RELEASE);
    g_pppdrawTramp = NULL;
    g_pppdrawInstalled = 0;
    g_pppdrawHookAbs = 0;
}

#define PPPDRAW_PRO_EPILOG PPPDRAW_PRO_STOLEN

static void InstallPppDrawHookAt(uint8_t *target, void *detour) {
    DWORD old = 0;
    if (g_pppdrawInstalled || !target || !detour) return;
    g_pppdrawTramp = (uint8_t *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_pppdrawTramp) return;
    g_pppdrawDetourPtr = detour;
    g_pppdrawTrampBackPtr = target + PPPDRAW_PRO_STOLEN;
    g_pppdrawHookAbs = (uint32_t)(uintptr_t)target;
    memcpy(g_pppdrawOrig, target, PPPDRAW_PRO_LEN);
    memcpy(g_pppdrawTramp, target, PPPDRAW_PRO_STOLEN);
    g_pppdrawTramp[PPPDRAW_PRO_STOLEN + 0] = 0xFF;
    g_pppdrawTramp[PPPDRAW_PRO_STOLEN + 1] = 0x25;
    *(uint32_t *)(g_pppdrawTramp + PPPDRAW_PRO_STOLEN + 2) = (uint32_t)(uintptr_t)&g_pppdrawTrampBackPtr;
    VirtualProtect(target, PPPDRAW_PRO_LEN, PAGE_EXECUTE_READWRITE, &old);
    target[0] = 0xFF;
    target[1] = 0x25;
    *(uint32_t *)(target + 2) = (uint32_t)(uintptr_t)&g_pppdrawDetourPtr;
    for (DWORD k = 6; k < PPPDRAW_PRO_LEN; k++)
        target[k] = 0x90;
    VirtualProtect(target, PPPDRAW_PRO_LEN, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, PPPDRAW_PRO_LEN);
    g_pppdrawInstalled = 1;
}

static void InstallPppDrawHook(void) {
    uint32_t bindFn = 0;
    uint8_t *target = NULL;
    if (g_pppdrawInstalled) return;
    target = g_base + RVA_PPPDRAW_APPLY;
    if (PppDrawLooksEntry(target)) {
        InstallPppDrawHookAt(target, (void *)PppDrawDetour71B980);
        return;
    }
    /* fallback: host+2856 bind fn (sub_711540) — may log with wrong layout */
    __try {
        bindFn = *(uint32_t *)(g_base + RVA_HOST_BIND_SLOT);
    } __except (EXCEPTION_EXECUTE_HANDLER) { bindFn = 0; }
    if (!PppDrawIsPtr(bindFn) || !PppDrawInModule(bindFn)) return;
    target = (uint8_t *)(uintptr_t)bindFn;
    if (!PppDrawLooksEntry(target)) return;
    InstallPppDrawHookAt(target, (void *)PppDrawDetourBind);
}
#endif /* !FFXPROBE_PPPDRAW_RETIRED */

static void PatchSlot(void **vtbl, int idx, void *hook, void **origOut)
{
    DWORD old = 0;
    VirtualProtect(&vtbl[idx], sizeof(void *), PAGE_EXECUTE_READWRITE, &old);
    if (origOut) *origOut = vtbl[idx];
    vtbl[idx] = hook;
    VirtualProtect(&vtbl[idx], sizeof(void *), old, &old);
}

static void RunCommand(FFXProbeBlock *b)
{
    uint32_t n;
    void *fn;
    float f;
    uint32_t ab;
    void *soundMgr;

    __try {
        switch (b->opcode) {
        case FFXPROBE_OP_READ:
            n = b->len; if (n > sizeof(b->buf)) n = (uint32_t)sizeof(b->buf);
            memcpy((void *)b->buf, (const void *)(uintptr_t)b->addr, n);
            b->status = FFXPROBE_ST_OK;
            break;
        case FFXPROBE_OP_WRITE:
            n = b->len; if (n > sizeof(b->buf)) n = (uint32_t)sizeof(b->buf);
            memcpy((void *)(uintptr_t)b->addr, (const void *)b->buf, n);
            b->status = FFXPROBE_ST_OK;
            break;
        case FFXPROBE_OP_CALL:
            fn = (void *)(uintptr_t)b->addr;
            ab = b->arg2; memcpy(&f, &ab, 4);
            switch (b->abi) {
            case FFXPROBE_ABI_CDECL_I:
                b->ret = ((int(__cdecl *)(int))fn)((int)b->arg0);
                b->status = FFXPROBE_ST_OK; break;
            case FFXPROBE_ABI_CDECL_II:
                b->ret = ((int(__cdecl *)(int, int))fn)((int)b->arg0, (int)b->arg1);
                b->status = FFXPROBE_ST_OK; break;
            case FFXPROBE_ABI_CDECL_III:
                b->ret = ((int(__cdecl *)(int, int, int))fn)((int)b->arg0, (int)b->arg1, (int)b->arg2);
                b->status = FFXPROBE_ST_OK; break;
            case FFXPROBE_ABI_CDECL_IIF:
                b->ret = ((int(__cdecl *)(int, int, float))fn)((int)b->arg0, (int)b->arg1, f);
                b->status = FFXPROBE_ST_OK; break;
            case FFXPROBE_ABI_STDCALL_II:
                b->ret = ((int(__stdcall *)(int, int))fn)((int)b->arg0, (int)b->arg1);
                b->status = FFXPROBE_ST_OK; break;
            default:
                b->ret = 0;
                b->errCode = 0xBADAB1u;
                b->status = FFXPROBE_ST_ERR; break;
            }
            break;
        case FFXPROBE_OP_FORCEBATTLE: {
            /* Atomic within this single main-thread hook frame: the game clears the scripted-encounter
               flag (RVA 0xD2CA20) every frame, so we set it and call MsBattleEncountExe immediately,
               before the game's per-frame reset runs. Takes the immediate "queue battle" branch. */
            uint8_t *base = g_base;
            *(volatile uint32_t *)(base + 0x00D2CA20) = 1u;
            *(volatile uint32_t *)(base + 0x00D2CA24) = 1u;
            *(volatile uint8_t  *)(base + 0x00D2CA28) = (uint8_t)b->arg2;  /* formation */
            b->ret = ((int(__cdecl *)(int, int, float))(base + 0x00380DE0))((int)b->arg0, (int)b->arg1, 0.0f);
            *(volatile uint32_t *)(base + 0x00D2CA20) = 0u;
            *(volatile uint32_t *)(base + 0x00D2CA24) = 0u;
            b->status = FFXPROBE_ST_OK;
            break;
        }
        case FFXPROBE_OP_SETINPUT:
            /* latch forged input; applied every frame by the hook until changed */
            g_finMask = b->arg0;
            g_finKey1 = b->arg1;
            g_finKey2 = b->arg2;
            b->status = FFXPROBE_ST_OK;
            break;
        case FFXPROBE_OP_TEXLOG_START:
            InstallTexHook();
            g_texCount = 0;
            g_texOn = g_texInstalled ? 1u : 0u;
            b->ret = g_texInstalled;
            b->status = g_texInstalled ? FFXPROBE_ST_OK : FFXPROBE_ST_ERR;
            break;
        case FFXPROBE_OP_TEXLOG_STOP:
            g_texOn = 0;
            b->ret = (int)g_texCount;
            b->status = FFXPROBE_ST_OK;
            break;
        case FFXPROBE_OP_TEXLOG_DRAIN: {
            uint32_t start = b->arg0, total = g_texCount, kk = 0;
            while (kk < 4u && (start + kk) < total) {   /* 128-byte records: 4 fit in buf[512] */
                memcpy((void *)(b->buf + kk * sizeof(TexRec)), &g_texRing[start + kk], sizeof(TexRec));
                kk++;
            }
            b->ret = (int)kk;
            b->len = total;
            b->status = FFXPROBE_ST_OK;
            break;
        }
        case FFXPROBE_OP_KETHRES_START:
            InstallKeThResHook();
            g_kethresCount = 0;
            g_kethresSnapCount = 0;
            g_kethresSnapBuf = 0;
            g_kethresSnapNz = 0;
            g_kethresOn = g_kethresInstalled ? 1u : 0u;
            b->ret = g_kethresInstalled;
            b->status = g_kethresInstalled ? FFXPROBE_ST_OK : FFXPROBE_ST_ERR;
            break;
        case FFXPROBE_OP_KETHRES_STOP:
            g_kethresOn = 0;
            b->ret = (int)g_kethresCount;
            b->status = FFXPROBE_ST_OK;
            break;
        case FFXPROBE_OP_KETHRES_DRAIN: {
            uint32_t start = b->arg0, total = g_kethresCount, kk = 0;
            while (kk < 8u && (start + kk) < total) {
                memcpy((void *)(b->buf + kk * sizeof(KeThResRec)), &g_kethresRing[start + kk], sizeof(KeThResRec));
                kk++;
            }
            b->ret = (int)kk;
            b->len = total;
            b->status = FFXPROBE_ST_OK;
            break;
        }
        case FFXPROBE_OP_KETHRES_SNAP: {
            uint32_t off = b->arg0;
            uint32_t n;
            if (off >= KETHRES_SNAP_BYTES) {
                b->ret = 0;
                b->status = FFXPROBE_ST_ERR;
                break;
            }
            n = KETHRES_SNAP_BYTES - off;
            if (n > sizeof(b->buf)) n = (uint32_t)sizeof(b->buf);
            memcpy((void *)b->buf, g_kethresSnap + off, n);
            b->addr = g_kethresSnapBuf;
            b->len = g_kethresSnapCount;
            b->ret = (int)g_kethresSnapNz;
            b->status = g_kethresSnapCount ? FFXPROBE_ST_OK : FFXPROBE_ST_ERR;
            break;
        }
        case FFXPROBE_OP_U1_START:
            if (g_u1Installed && g_u1Family != b->arg0) UninstallU1Hook();
            g_u1Count = 0;
            if (!InstallU1Hook(b->arg0)) {
                b->ret = 0;
                b->errCode = 0x55310001u;
                b->status = FFXPROBE_ST_ERR;
                break;
            }
            g_u1On = 1;
            b->addr = g_u1HookAbs;
            b->ret = (int)g_u1Family;
            b->status = FFXPROBE_ST_OK;
            break;
        case FFXPROBE_OP_U1_STOP:
            g_u1On = 0;
            b->ret = (int)g_u1Count;
            UninstallU1Hook();
            b->status = FFXPROBE_ST_OK;
            break;
        case FFXPROBE_OP_U1_DRAIN: {
            uint32_t start = b->arg0, total = g_u1Count, copied = 0;
            while (copied < (uint32_t)(sizeof(b->buf) / sizeof(FFXProbeU1Record)) && (start + copied) < total) {
                memcpy((void *)(b->buf + copied * sizeof(FFXProbeU1Record)), &g_u1Ring[start + copied], sizeof(FFXProbeU1Record));
                copied++;
            }
            b->ret = (int)copied;
            b->len = total;
            b->status = FFXPROBE_ST_OK;
            break;
        }
#if FFXPROBE_PPPDRAW_RETIRED
        case FFXPROBE_OP_PPPDRAW_START:
        case FFXPROBE_OP_PPPDRAW_STOP:
        case FFXPROBE_OP_PPPDRAW_DRAIN:
            b->ret = 0;
            b->errCode = FFXPROBE_ERR_PPPDRAW_RETIRED;
            b->status = FFXPROBE_ST_ERR;
            break;
#else
        case FFXPROBE_OP_PPPDRAW_START: {
            uint32_t ab;
            if (!g_pppdrawInstalled)
                InstallPppDrawHook();
            g_pppdrawCount = 0;
            g_pppdrawForce = b->arg0 ? 1u : 0u;
            g_pppdrawOn = g_pppdrawInstalled ? 1u : 0u;
            if (g_pppdrawForce) {
                ab = b->arg1; memcpy(&g_pppdrawForceR, &ab, 4);
                ab = b->arg2; memcpy(&g_pppdrawForceG, &ab, 4);
                if (b->len >= 4u)
                    memcpy(&g_pppdrawForceB, (const void *)b->buf, 4);
                else
                    g_pppdrawForceB = 0.05f;
            } else {
                g_pppdrawForceR = 1.0f;
                g_pppdrawForceG = 0.35f;
                g_pppdrawForceB = 0.05f;
            }
            g_pppdrawForceA = 1.0f;
            b->addr = g_pppdrawHookAbs;
            b->ret = g_pppdrawInstalled;
            b->status = g_pppdrawInstalled ? FFXPROBE_ST_OK : FFXPROBE_ST_ERR;
            break;
        }
        case FFXPROBE_OP_PPPDRAW_STOP:
            g_pppdrawOn = 0;
            g_pppdrawForce = 0;
            b->ret = (int)g_pppdrawCount;
            b->status = FFXPROBE_ST_OK;
            break;
        case FFXPROBE_OP_PPPDRAW_DRAIN: {
            uint32_t start = b->arg0, total = g_pppdrawCount, kk = 0;
            while (kk < 16u && (start + kk) < total) {
                memcpy((void *)(b->buf + kk * sizeof(PppDrawRec)), &g_pppdrawRing[start + kk], sizeof(PppDrawRec));
                kk++;
            }
            b->ret = (int)kk;
            b->len = total;
            b->status = FFXPROBE_ST_OK;
            break;
        }
#endif
        case FFXPROBE_OP_SOUNDCMD:
            soundMgr = *(void **)(g_base + RVA_SOUND_COMMAND_MANAGER);
            if (!soundMgr) {
                b->ret = 0;
                b->errCode = 1;
                b->status = FFXPROBE_ST_ERR;
                break;
            }
            b->ret = CallThiscall5(
                (void *)(g_base + RVA_SOUND_REGIST_COMMAND_SYNC),
                soundMgr,
                (int)b->arg0,
                (int)b->arg1,
                (int)b->arg2,
                0,
                0);
            b->status = FFXPROBE_ST_OK;
            break;
        default:
            b->ret = 0;
            b->errCode = 0xBAD0C0DEu;
            b->status = FFXPROBE_ST_ERR;
            break;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        b->status = FFXPROBE_ST_ERR;
        b->errCode = GetExceptionCode();
    }
}

/* Overlay forged input onto a freshly-filled device-state buffer.
   Keyboard: cb==256, data = DIK keystate array (0x80 = pressed). We force MULTIPLE candidate keys
   per direction (arrows + WASD + numpad) so movement works whatever FFX's keyboard mapping is.
   Joystick: cb>=24, data = DIJOYSTATE (lX@0, lY@4 LONG; rgdwPOV@16). Forge axes/POV when FFXIN_JOY set. */
static void ApplyForgedInput(uint32_t cb, void *data)
{
    uint32_t m = g_finMask, k1 = g_finKey1, k2 = g_finKey2;
    if (!m && !k1 && !k2) return;
    if (!data) return;
    if (cb == 256) {
        uint8_t *k = (uint8_t *)data;
        if (k1) k[k1 & 0xFF] = 0x80;
        if (k2) k[k2 & 0xFF] = 0x80;
        if (m & FFXIN_UP)      { k[0xC8] = 0x80; k[0x11] = 0x80; k[0x48] = 0x80; } /* UP, W, Numpad8 */
        if (m & FFXIN_DOWN)    { k[0xD0] = 0x80; k[0x1F] = 0x80; k[0x50] = 0x80; } /* DOWN, S, Numpad2 */
        if (m & FFXIN_LEFT)    { k[0xCB] = 0x80; k[0x1E] = 0x80; k[0x4B] = 0x80; } /* LEFT, A, Numpad4 */
        if (m & FFXIN_RIGHT)   { k[0xCD] = 0x80; k[0x20] = 0x80; k[0x4D] = 0x80; } /* RIGHT, D, Numpad6 */
        if (m & FFXIN_CONFIRM) { k[0x1C] = 0x80; k[0x39] = 0x80; }                 /* ENTER, SPACE */
        if (m & FFXIN_CANCEL)  { k[0x01] = 0x80; k[0x2E] = 0x80; }                 /* ESC, C */
        if (m & FFXIN_MENU)    { k[0x0F] = 0x80; }                                 /* TAB */
    } else if (cb >= 24 && (m & FFXIN_JOY)) {
        /* DIJOYSTATE: assume axes ranged 0..65535, center 32767 (FFX sets DIPROP_RANGE). */
        int32_t *ax = (int32_t *)data;        /* lX@[0], lY@[1] */
        if (m & FFXIN_UP)    ax[1] = 0;
        if (m & FFXIN_DOWN)  ax[1] = 65535;
        if (m & FFXIN_LEFT)  ax[0] = 0;
        if (m & FFXIN_RIGHT) ax[0] = 65535;
        if (cb >= 32) { /* rgdwPOV[0] @ byte 16 = dword[4]; -1 = centered */
            uint32_t *pov = (uint32_t *)((uint8_t *)data + 16);
            if      (m & FFXIN_UP)    pov[0] = 0;      /* 0 deg */
            else if (m & FFXIN_RIGHT) pov[0] = 9000;
            else if (m & FFXIN_DOWN)  pov[0] = 18000;
            else if (m & FFXIN_LEFT)  pov[0] = 27000;
        }
    }
}

static long __stdcall MyGetDeviceState(void *self, uint32_t cb, void *data)
{
    FFXProbeBlock *b = g_block;
    long r;
    if (b) {
        b->heartbeat++;
        g_frameCtr++;   /* per-frame tick; the texlog hook tags records with this */
        if (b->seq != b->ackSeq && InterlockedCompareExchange(&g_inCmd, 1, 0) == 0) {
            RunCommand(b);
            b->ackSeq = b->seq;       /* signal completion to the editor */
            InterlockedExchange(&g_inCmd, 0);
        }
    }
    r = g_origGetDeviceState(self, cb, data);
    if (r >= 0) ApplyForgedInput(cb, data);   /* overlay forged input on the real state */
    return r;
}

static DWORD WINAPI InitThread(LPVOID unused)
{
    HANDLE mmf;
    void *di;
    void **diVtbl;
    CreateDevice_t createDevice;
    void *dev = NULL;
    void **devVtbl;
    int i;

    (void)unused;
    g_base = (uint8_t *)GetModuleHandleW(NULL);

    mmf = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                             0, sizeof(FFXProbeBlock), FFXPROBE_MMF_NAME);
    if (!mmf) return 0;
    g_block = (FFXProbeBlock *)MapViewOfFile(mmf, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FFXProbeBlock));
    if (!g_block) return 0;
    memset((void *)g_block, 0, sizeof(*g_block));
    g_block->version    = FFXPROBE_VERSION;
    g_block->moduleBase = (uint32_t)(uintptr_t)g_base;
    g_block->magic      = FFXPROBE_MAGIC;   /* editor can now see "module attached" */

    /* Wait for the game's IDirectInput8* to be created (FFX_Input_InitDirectInput8). */
    for (i = 0; i < 120000; i++) {
        di = *(void **)(g_base + RVA_DINPUT8_PTR);
        if (di) break;
        Sleep(5);
    }
    if (!di) return 0;

    /* Create a throwaway keyboard device just to reach the SHARED device vtable. */
    diVtbl = *(void ***)di;
    createDevice = (CreateDevice_t)diVtbl[3];
    if (createDevice(di, &kGuidSysKeyboard, &dev, NULL) < 0 || !dev) {
        /* Fallback: retry briefly in case DirectInput finishes initializing. */
        for (i = 0; i < 200 && (!dev); i++) { Sleep(10); createDevice(di, &kGuidSysKeyboard, &dev, NULL); }
        if (!dev) return 0;
    }

    devVtbl = *(void ***)dev;
    PatchSlot(devVtbl, 9, (void *)MyGetDeviceState, (void **)&g_origGetDeviceState); /* GetDeviceState */
    ((Release_t)devVtbl[2])(dev);  /* release our throwaway; the patched vtable persists */

    g_block->hooked = 1;
    return 0;
}

/* ---- FFX Module Loader entry points + DllMain ---- */
__declspec(dllexport) const char *FF10HgetName(void) { return "ffx-probe (Jarvis DINPUT8 main-thread probe)"; }
__declspec(dllexport) const char *FF10HgetVer(void)  { return "0.1"; }

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);
        CreateThread(NULL, 0, InitThread, NULL, 0, NULL);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        g_u1On = 0;
#if !FFXPROBE_PPPDRAW_RETIRED
        UninstallPppDrawHook();
#endif
    }
    return TRUE;
}
