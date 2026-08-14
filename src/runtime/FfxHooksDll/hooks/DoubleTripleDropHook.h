#pragma once
// DoubleTripleDropHook — battle-only item qty multiplier (×2 / ×3) on FFX_Inventory_AddItem.
//
// Scope (by design):
//   - Only callers from the four battle grant paths (drop end-screen, steal, commands).
//   - Only item namespace (itemId & 0xF000 == 0x2000).
//   - Party-wide MAX stack: any member with Triple → ×3; else any Double → ×2; never stacks per head.
//
// Ability bits (a_ability entry +0x64 → actor Auto_abilities_2 @ +0x6BE):
//   Double Drop  = 0x1000  (recommended spare id 129 / equip 0x8081)
//   Triple Drop  = 0x2000  (recommended spare id 130 / equip 0x8082)
//
// Gate: env-only for now (no flag files yet — other lane may touch config/):
//   FFXHOOKS_ENABLE_DOUBLE_TRIPLE_DROP=1
//   FFXHOOKS_DOUBLE_TRIPLE_DROP_LOG=1  (optional hit log, capped)
//
// Evidence: docs/reverse/FFX_DOUBLE_TRIPLE_DROP_HOOK_2026-06-23.md

#include <stdint.h>

namespace FfxHooks {

    typedef void (*DoubleTripleDropLogFn)(const char* message);

    struct DoubleTripleDropInstallResult {
        bool     ok;
        uint32_t reasonCode; // 0=ok, 2=already_installed, 3=no_polyhook, 4=detour_failed
    };

    DoubleTripleDropInstallResult InstallDoubleTripleDropHook(
        uintptr_t base,
        bool apply,
        bool logHits,
        DoubleTripleDropLogFn log);

    void RemoveDoubleTripleDropHook(DoubleTripleDropLogFn log);
    bool IsDoubleTripleDropHookInstalled();
    long DoubleTripleDropHookHitCount();

} // namespace FfxHooks
