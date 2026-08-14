// ElementHook — Scan UI Holy/Dark element-affinity icons (Jarvis-RE-SCANUI 2026-06-16).
//
// The Scan target panel draws element affinity as 4 hardcoded balls (Fire/Ice/
// Thunder/Water) via inverse-masking: a baked 4-element strip is drawn, then a
// "mask" sub-tile is stamped over every element the target does NOT have. There
// is no slot for Holy (0x10) or Dark (0x80), even though the damage math and the
// editor already accept Holy/Dark weak/absorb/null/resist data.
//
// This hook detours the per-row drawer (FFX_BtlUI_DrawScanElementResistRow,
// PE RVA 0x494AB0), lets the vanilla row render untouched, then appends up to two
// POSITIVE-logic ball draws — Holy and Dark — sampling two balls authored into
// atlas 16128 (battle.dds.phyre) by FFXProjectEditor/Tools/ScanWardBallInjectRt0.cs.
//
// The vanilla affinity box (sub_8939A0) is only 385 design-px wide, so the two
// appended balls (design x+379 / x+442) spill past its right edge. So the install
// also data-patches the box-width constant (flt_ScanInfoPanelWidth385, sole xref =
// the panel frame draw) from 385f to 490f, stretching the rounded 9-slice box so
// both new balls sit INSIDE it — in both the Sensor "Info" and full "Scan data"
// panels (they are the same function, differing only by the immunities flag).
//
// Full RE (ABI, exact draw recipe, authored UVs, scaling proof, box-width xref):
//   docs/reverse/FFX_SCAN_WEAKNESS_UI_RENDER_LOOP_2026-06-16.md (§14)
//
// Gated by element_scan_dark.flag / FFXHOOKS_ELEMENT_SCAN_DARK (see dllmain.cpp).
// Without PolyHook the install is a logging no-op so the DLL stays link-safe.

#include "ElementHook.h"
#include "../shared/ffx_addresses.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <polyhook2/Detour/x86Detour.hpp>
#include <exception>
#include <stdint.h>
#endif

namespace FfxHooks {

#ifdef FFXHOOKS_HAVE_POLYHOOK

namespace {

static PLH::x86Detour* g_detour     = nullptr;
static uint64_t        g_trampoline = 0;
static void          (*g_log)(const char*) = nullptr;
static bool            g_installed  = false;
static uintptr_t       g_base       = 0;

// ── Target ABIs (mirror IDA; see ffx_addresses.h) ──────────────────────────
//   FFX_BtlUI_DrawScanElementResistRow @ RVA 0x494AB0
typedef int  (__cdecl *ScanRowFn)(int actor, int cat, int x, int y);
//   FFX_Menu2D_DrawTexQuadSolid @ RVA 0x503BB0 (__cdecl, 9 args, caller cleanup)
//   floats pass as 4-byte stack slots in x86 __cdecl — exactly what the target reads.
typedef int  (__cdecl *DrawTexQuadFn)(unsigned int atlasId, float x, float y,
                                      float w, float h,
                                      float u0, float v0, float u1, float v1);
//   FFX_GetActorElementCategoryMask_Switch4 @ RVA 0x4975C0
typedef char (__cdecl *ElemMaskFn)(int actor, int cat);

// ── Authored ball UVs in atlas 16128 (battle.dds.phyre) — doc §14.1/§14.2 ──
constexpr float HOLY_U0 = 0.1602f, HOLY_V0 = 0.9570f, HOLY_U1 = 0.2070f, HOLY_V1 = 0.9961f;
constexpr float DARK_U0 = 0.5977f, DARK_V0 = 0.9570f, DARK_U1 = 0.6445f, DARK_V1 = 0.9961f;

// ── Vanilla per-slot geometry (design space, fed through the scalers) ──────
constexpr float kBallSize  = 32.700001f;  // flt_B5EE84 — vanilla mask tile size
constexpr float kBallYOff  = 4.0f;         // flt_B922E8 — vanilla mask y offset
constexpr float kHolyXOff  = 379.0f;       // +63 past Blizzard's +316
constexpr float kDarkXOff  = 442.0f;       // +63 past Holy

constexpr unsigned char BIT_HOLY = 0x10u;
constexpr unsigned char BIT_DARK = 0x80u;

// Pure UI coordinate scalers, identical to the game's sub_644990 / sub_6449D0
// (design space 1920x1080 -> render space 512x416). Inlined to avoid extra RVAs;
// double intermediate + float cast matches the vanilla rounding exactly.
static inline float ScaleX(float v) { return (float)((double)v * 512.0 / 1920.0); }
static inline float ScaleY(float v) { return (float)((double)v * 416.0 / 1080.0); }

static void EmitBall(float x, float y, float xOff,
                     float u0, float v0, float u1, float v1) {
    DrawTexQuadFn draw = (DrawTexQuadFn)(g_base + RVA_FFX_MENU2D_DRAW_TEX_QUAD_SOLID);
    draw(FFX_SCAN_WIDGET_ATLAS_ID,
         x + ScaleX(xOff),
         y + ScaleY(kBallYOff),
         ScaleX(kBallSize),
         ScaleY(kBallSize),
         u0, v0, u1, v1);
}

// Detour shim: run the vanilla row, then append the Holy/Dark balls for this
// affinity category. Same __cdecl(int,int,int,int) shape as the target.
static int __cdecl ScanRow_Shim(int actor, int cat, int x, int y) {
    const int result = ((ScanRowFn)g_trampoline)(actor, cat, x, y);

    ElemMaskFn getMask = (ElemMaskFn)(g_base + RVA_FFX_GETACTOR_ELEMENT_CATEGORY_MASK);
    const unsigned char v4 = (unsigned char)getMask(actor, cat);
    const float fx = (float)x;
    const float fy = (float)y;
    if (v4 & BIT_HOLY) EmitBall(fx, fy, kHolyXOff, HOLY_U0, HOLY_V0, HOLY_U1, HOLY_V1);
    if (v4 & BIT_DARK) EmitBall(fx, fy, kDarkXOff, DARK_U0, DARK_V0, DARK_U1, DARK_V1);
    return result;
}

static void Logf(const char* fmt, ...) {
    if (!g_log) return;
    char line[256] = {};
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    g_log(line);
}

// ── Widen the affinity info-panel box so the appended Holy/Dark balls fit ──────
// flt_ScanInfoPanelWidth385 is a read-only float whose ONLY xref is this panel's
// frame draw (proven via IDA xrefs), so patching it in isolation is safe. The
// original 4 bytes are saved and restored so RemoveElementHook fully reverts it.
static unsigned char g_widthSaved[sizeof(float)] = {};
static bool          g_widthPatched = false;

static void PatchScanPanelWidth() {
    if (g_widthPatched) return;
    void* addr = (void*)(g_base + RVA_FFX_SCAN_INFO_PANEL_WIDTH_CONST);
    DWORD oldProt = 0;
    if (!VirtualProtect(addr, sizeof(float), PAGE_READWRITE, &oldProt)) {
        Logf("[ffx-hooks] ElementHook panel-width VirtualProtect failed (err=%lu)\n",
             (unsigned long)GetLastError());
        return;
    }
    memcpy(g_widthSaved, addr, sizeof(float));
    const float widened = FFX_SCAN_INFO_PANEL_WIDTH_WIDENED;
    memcpy(addr, &widened, sizeof(float));
    DWORD restoreProt = 0;
    VirtualProtect(addr, sizeof(float), oldProt, &restoreProt);
    g_widthPatched = true;
    float prev = 0.0f;
    memcpy(&prev, g_widthSaved, sizeof(float));
    Logf("[ffx-hooks] ElementHook widened Scan info-panel box %.1f->%.1f at RVA=0x%08X\n",
         prev, widened, (unsigned)RVA_FFX_SCAN_INFO_PANEL_WIDTH_CONST);
}

static void RestoreScanPanelWidth() {
    if (!g_widthPatched) return;
    void* addr = (void*)(g_base + RVA_FFX_SCAN_INFO_PANEL_WIDTH_CONST);
    DWORD oldProt = 0;
    if (VirtualProtect(addr, sizeof(float), PAGE_READWRITE, &oldProt)) {
        memcpy(addr, g_widthSaved, sizeof(float));
        DWORD restoreProt = 0;
        VirtualProtect(addr, sizeof(float), oldProt, &restoreProt);
    }
    g_widthPatched = false;
}

} // namespace

void InstallElementHook(uintptr_t base, void (*log)(const char*)) {
    g_log  = log;
    g_base = base;
    if (g_installed) return;

    const uint64_t targetVa =
        (uint64_t)(base + RVA_FFX_BTLUI_DRAW_SCAN_ELEMENT_RESIST_ROW);
    Logf("[ffx-hooks] ElementHook installing Scan Holy/Dark detour at VA=0x%08X (RVA=0x%08X)\n",
         (unsigned)targetVa, (unsigned)RVA_FFX_BTLUI_DRAW_SCAN_ELEMENT_RESIST_ROW);

    try {
        g_detour = new PLH::x86Detour(targetVa, (uint64_t)&ScanRow_Shim, &g_trampoline);
        if (!g_detour->hook()) {
            Logf("[ffx-hooks] ElementHook hook() returned false; aborting install\n");
            delete g_detour;
            g_detour = nullptr;
            g_trampoline = 0;
            return;
        }
        g_installed = true;
        Logf("[ffx-hooks] ElementHook detour installed; trampoline=0x%llX\n",
             (unsigned long long)g_trampoline);
        // Balls draw; now stretch the box so they land inside it.
        PatchScanPanelWidth();
    } catch (const std::exception& ex) {
        Logf("[ffx-hooks] ElementHook install exception: %s\n", ex.what());
        delete g_detour;
        g_detour = nullptr;
        g_trampoline = 0;
    } catch (...) {
        Logf("[ffx-hooks] ElementHook install unknown exception\n");
        delete g_detour;
        g_detour = nullptr;
        g_trampoline = 0;
    }
}

void RemoveElementHook() {
    if (!g_installed) return;
    RestoreScanPanelWidth();
    if (g_detour) {
        g_detour->unHook();
        delete g_detour;
        g_detour = nullptr;
    }
    g_trampoline = 0;
    g_installed = false;
}

bool IsElementHookInstalled() { return g_installed; }

#else // !FFXHOOKS_HAVE_POLYHOOK

void InstallElementHook(uintptr_t /*base*/, void (*log)(const char*)) {
    if (log) log("[ffx-hooks] ElementHook: no PolyHook stack; Scan Holy/Dark detour unavailable\n");
}
void RemoveElementHook() {}
bool IsElementHookInstalled() { return false; }

#endif // FFXHOOKS_HAVE_POLYHOOK

} // namespace FfxHooks
