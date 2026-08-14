#pragma once
// ElementHook — Scan UI Holy/Dark element-affinity icons.
// Detours FFX_BtlUI_DrawScanElementResistRow (PE RVA 0x494AB0) and appends two
// positive-logic ball draws (Holy 0x10, Dark 0x80) sampling atlas 16128
// (battle.dds.phyre). Gated by element_scan_dark.flag / FFXHOOKS_ELEMENT_SCAN_DARK.
// RE: docs/reverse/FFX_SCAN_WEAKNESS_UI_RENDER_LOOP_2026-06-16.md (§14).

#include <stdint.h>

namespace FfxHooks {
    void InstallElementHook(uintptr_t base, void (*log)(const char* message));
    void RemoveElementHook();
    bool IsElementHookInstalled();
}
