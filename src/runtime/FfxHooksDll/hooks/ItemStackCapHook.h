#pragma once
// ItemStackCapHook — raises per-slot inventory count cap from 99 (vanilla) to a
// configurable ceiling (default 255, byte ceiling). Patches the two clamp sites
// inside FFX_Inventory_AddItem (PE RVA 0x003905A0) with 5-byte jmp trampolines
// to heap-allocated stubs that swap `push 63h` (imm8) for `push 0FFh` (imm32).
//
// Status: LAB — gated by item_stack_cap_255.flag.
// Evidence: docs/reverse/FFX_ITEM_STACK_CAP_99_RESEARCH_2026-06-16.md

#include <stdint.h>

namespace FfxHooks {

    typedef void (*ItemStackCapLogFn)(const char* message);

    struct ItemStackCapInstallResult {
        bool ok;
        uintptr_t stubNew;     // stub for "new slot" clamp site #1
        uintptr_t stubExist;   // stub for "existing slot" clamp site #2
    };

    /*
     * cap = desired ceiling (1..255). Default 255. Values clamped to [1, 255].
     * logHits enables a one-line install log only; runtime stub stays minimal.
     */
    ItemStackCapInstallResult InstallItemStackCapHook(
        uintptr_t base,
        uint8_t cap,
        bool logInstall,
        ItemStackCapLogFn log);

    bool RemoveItemStackCapHook(ItemStackCapLogFn log);
    bool IsItemStackCapHookInstalled();
    uint8_t GetItemStackCapInstalledCap();

} // namespace FfxHooks
