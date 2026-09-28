# Plan: F8 "Lab" tab — surfacing the six wired lab hooks in the dashboard

**Date:** 2026-09-16 · execution checkpoint 2026-09-19
**Lane:** Jarvis-HOOK · worktree `ffx-hooks-fastload-autosave-20260916`
**Status:** SELECTED by the user on 2026-09-19 for the same candidate as the Difficulty correction. Local implementation and offline validation complete; deployment and live RT2 remain separate.
**Current lane:** execute inline after the already deployed Fastload success, preserving that implementation. The user selected this Lab slice before integrating the prepared Difficulty correction. No subagents or parallel writers are authorized.

---

## 1. Scope

Surface six already-wired lab hooks as restart-required rows inside F8. The following baseline source findings are from 2026-09-16; the 2026-09-19 corrections below govern the implementation. They are not live-game acceptance.

| Feature | Canonical key | Flag file | Env | Mechanism | Activation |
|---|---|---|---|---|---|
| NovaSuperDamage | `labs.nova_super_damage` | `nova_super_damage.flag` | `FFXHOOKS_ENABLE_NOVA_SUPER_DAMAGE` | Inline patch @ `FFX_Battle_DamageCap_ClampJle` VA `0x0078EDD5` / RVA `0x0038EDD5` — removes the 99,999 single-hit clamp for Kimahri Nova (cmd `0x3073`). `labs.nova_super_damage_log` = observe-only variant. | Boot install → RestartRequired |
| RonsoMana | `labs.kimahri_ronso_mana` (+`_apply`) | `kimahri_ronso_mana.flag` (+`_apply.flag`) | `FFXHOOKS_ENABLE_RONSO_MANA` / `FFXHOOKS_RONSO_MANA_APPLY` | Three detours (gate / greyout / drain) give Kimahri a partial Overdrive pool. Master flag installs **log-only** detours; `_apply` flag makes them write. | Boot install → RestartRequired |
| GridTeach v4.5 | `labs.grid_teach` | `grid_teach.flag` | `FFXHOOKS_GRID_TEACH` | BuildMenu detour + menu-bound patch (bound 367); sphere-grid node activation teaches commands; shadow sidecar words 16-17 hold ids 352-383, actor-seeded. | Boot install → RestartRequired |
| KimahriLancetDualGrant | `labs.kimahri_lancet_dual_grant` | `kimahri_lancet_dual_grant.flag` | `FFXHOOKS_KIMAHRI_LANCET_DUAL_GRANT` | Lancet Ronso-Rage learn (104-115) also grants the Blue Mage clone (323-334). **Chains through the GridTeach grant shim** — warns and stays inert when `grid_teach.flag` is off. | Boot install → RestartRequired |
| ItemStackCap | `labs.item_stack_cap` | `item_stack_cap_255.flag` | `FFXHOOKS_ENABLE_ITEM_STACK_CAP` | Two 5-byte jmp trampolines inside `FFX_Inventory_AddItem` (RVA `0x003905A0`) swap `push 63h` (99) for `push 0FFh`. Cap value read from **`FFXHOOKS_ITEM_STACK_CAP` env only**, default 255, clamped [1,255]. | Boot install → RestartRequired |
| DoubleTripleDrop | `labs.double_triple_drop` | `double_triple_drop.flag` | `FFXHOOKS_ENABLE_DOUBLE_TRIPLE_DROP` | PolyHook detour on the four battle `FFX_Inventory_AddItem` grant paths (drop end-screen, steal, commands); item namespace `0x2000` only; party-wide ×3 if any member has Triple Drop (`Auto_abilities_2 & 0x2000`), else ×2 for Double (`0x1000`); never stacks per head. `labs.double_triple_drop_log` = capped hit log. | Boot install → RestartRequired |

**Explicitly out of scope:** NulWard family (own conflict surface with NovaClamp — roadmap K-21), NulWardTeach legacy, ElementScanDark, AbilitySfx, field probes, the retired SphereGrid entries (`true_new_node.flag`, `sg_full_grid_compiler.flag` — never port). Log-only sub-flags (`*_log`) stay INI/flag-only, not menu rows.

## 2026-09-19 source corrections

- Preserve both legacy opt-ins: the existing `CheckEnabled` accepts unmarked INI true or a flag.
  A plain authority-bearing catalog spec would ignore INI-only true. Add the narrowly opted-in
  `BoolGateSpec::unmarkedTrueIsLegacyEnable` policy to these seven rows: unmarked true enables,
  unmarked false still permits a flag, marked F8 OFF overrides the flag. Other rows retain their
  resolver semantics. This adds no INI key, environment variable or disable-marker name.
- The existing scalar transaction/labels assumed LIVE AP/Gil. Restart scalars must be editable
  while enabled, preserve current startup evidence and render Item Cap as a count. Capture its
  INI/env value once with startup gates, and report the actual installed ceiling separately.
- Catalog totals are now **37 rows / 8 tabs / 14 LIVE / 16 RESTART REQUIRED / 7 NOT WIRED**;
  Dev/Fastload remains unchanged. The generic 14-case LIVE protocol changes its catalog anchor
  and documentation assertions only; Lab remains a separate per-feature live validation.
- Shorten the example help text below to the existing 63-character subtitle budget. Do not
  change rendering geometry or reintroduce a second per-row value draw.

## 2. Architecture findings that shape the design

### F1 — F8 authority vs install-site split-brain (must fix)
F8 rows resolve through `ResolveBoolGate` (disableEnv > .off markers > env > `f8_authority.*`+canonical > legacy key > flag file). The lab install sites instead call `CheckEnabled` (`dllmain.cpp:876-952`), which reads env > canonical INI > **flag file unconditionally**. Consequence: if a `*.flag` file sits in the game dir and the user flips the F8 row OFF (writes `labs.x=0` + authority marker), the install site still sees the flag and arms the hook — the menu lies.
**Design:** migrate the six gate readers to resolve through the catalog (`ResolveF8Flag(*FindF8Flag("labs.x")).value`). With no authority marker the flag file still wins → zero behavior change for existing users; once F8 owns the row, F8 wins.

### F2 — Two-stage RonsoMana gate
`kimahri_ronso_mana.flag` alone = install log-only. `_apply` without master installs **nothing** (master gates the whole install block). Options:
- **A (chosen):** two rows — `Ronso Mana` (master, log-only) and `Ronso Mana Apply` (`_apply`). Change the install condition to `master || apply` so the Apply row alone arms the full path; `logOnly = !apply` unchanged. Honest, each row maps to one real flag.
- B: one row that writes both keys — hides the log-only observability stage the lab workflow needs.

### F3 — Lancet hard dependency on GridTeach
`InstallKimahriLancetDualGrantHook` warns and skips when GridTeach is off. Do NOT auto-arm GridTeach (its BuildMenu detour changes menu behavior on its own — implicit enablement violates least-surprise). Row gets help text `REQUIRES Grid Teach`; the existing WARN log already tells the truth. Documented as RT2 check.

### F4 — ItemStackCap scalar needs a config read path
The cap comes from env only. To make it an F8 scalar row (digit editor already exists — `g_f8ScalarEditor`, Backspace digit input), the install site must read `Config::GetInt("labs.item_stack_cap_value", 255)` with env as the override. New default key lands in `kDefaultIni`. Scalar spec: `{"labs.item_stack_cap_value", 255, 1, 255}` attached to the boolean row (pattern: `kApMultiplier`/`kGilMultiplier`). Scalar persists at edit; effect applies next boot — same RestartRequired contract.

### F5 — Catalog/budget mechanics
- `kTabNames` grows `7 → 8` (`"Lab"` appended last — least-important rightmost, matches importance ordering). Tab bar auto-sizes (`tabW` divides by count); `F8MouseTabHitTest` reads `F8TabCount()` — verify it needs no hardcoded 6.
- `kFlagCount` static_assert `30 → 37`; `kF8BulkRowResultMax` `30 → 37` (the R6 static_assert now couples them — compile-time enforced).
- Per-tab row arrays `g_f7FlagSpecs[32]` etc. — Lab has 7 boolean rows plus a scalar, two bulk actions and Back: 11 visible/menu rows use the existing nine-row scrolling viewport.
- `tlab[16]` label buffer — `"Lab"` trivially fits.
- Bulk buttons (`Enable Supported`/`Disable Supported`) appear on every editable tab — Lab included. RestartRequired rows can't fail admission, so Lab bulk-enable arms all seven at once including the dependent pair. **Decision:** keep bulk (per-row truth reporting already names skipped/overridden rows), but add a hard-stop test asserting Lancet+Apply survive a bulk-enable without master rows and still resolve honestly. Alternative considered: suppress bulk on Lab — rejected, inconsistent chrome.

### F6 — All six are RestartRequired
No live toggle path exists for any of them (boot-time patch/detour install). Rows: `F8Activation::RestartRequired`, `F8ApplyMode::None` — identical contract to the Scout/Arena+ rows. Help strings start `RESTART REQUIRED - ` (existing convention), plus `LAB` marker for honest expectation-setting.

## 3. Implemented catalog rows

```cpp
    {"Lab", "Nova Super Damage",
     {"labs.nova_super_damage", "f8_authority.lab_nova_super_damage", nullptr,
      "FFXHOOKS_ENABLE_NOVA_SUPER_DAMAGE", "nova_super_damage.flag", nullptr, nullptr, nullptr, false, true},
     "RESTART REQUIRED - LAB: uncap Kimahri Nova damage.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Lab", "Ronso Mana",
     {"labs.kimahri_ronso_mana", "f8_authority.lab_kimahri_ronso_mana", nullptr,
      "FFXHOOKS_ENABLE_RONSO_MANA", "kimahri_ronso_mana.flag", nullptr, nullptr, nullptr, false, true},
     "RESTART REQUIRED - LAB: observe Kimahri Overdrive; no writes.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Lab", "Ronso Mana Apply",
     {"labs.kimahri_ronso_mana_apply", "f8_authority.lab_kimahri_ronso_mana_apply", nullptr,
      "FFXHOOKS_RONSO_MANA_APPLY", "kimahri_ronso_mana_apply.flag", nullptr, nullptr, nullptr, false, true},
     "RESTART REQUIRED - LAB: apply Kimahri Overdrive changes.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Lab", "Grid Teach",
     {"labs.grid_teach", "f8_authority.lab_grid_teach", nullptr,
      "FFXHOOKS_GRID_TEACH", "grid_teach.flag", nullptr, nullptr, nullptr, false, true},
     "RESTART REQUIRED - LAB: Sphere Grid nodes teach commands.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Lab", "Lancet Dual Grant",
     {"labs.kimahri_lancet_dual_grant", "f8_authority.lab_kimahri_lancet_dual_grant", nullptr,
      "FFXHOOKS_KIMAHRI_LANCET_DUAL_GRANT", "kimahri_lancet_dual_grant.flag", nullptr, nullptr, nullptr, false, true},
     "RESTART REQUIRED - LAB: Blue skills; REQUIRES Grid Teach.",
     F8Activation::RestartRequired, F8ApplyMode::None},
    {"Lab", "Item Stack Cap",
     {"labs.item_stack_cap", "f8_authority.lab_item_stack_cap", nullptr,
      "FFXHOOKS_ENABLE_ITEM_STACK_CAP", "item_stack_cap_255.flag", nullptr, nullptr, nullptr, false, true},
     "RESTART REQUIRED - LAB: set per-slot item limit (1-255).",
     F8Activation::RestartRequired, F8ApplyMode::None, &kItemStackCapValue},
    {"Lab", "Double/Triple Drop",
     {"labs.double_triple_drop", "f8_authority.lab_double_triple_drop", nullptr,
      "FFXHOOKS_ENABLE_DOUBLE_TRIPLE_DROP", "double_triple_drop.flag", nullptr, nullptr, nullptr, false, true},
     "RESTART REQUIRED - LAB: battle drops x2/x3 with drop abilities.",
     F8Activation::RestartRequired, F8ApplyMode::None},
```

## 4. Task breakdown

### Task L0 — RED contract tests
- [x] Pin the 7-row Lab tab in `F8RuntimeRt0.cpp`: `F8TabCount()==8`, `F8TabName(7)=="Lab"`, each `labs.*` canonical key present with `RestartRequired`+`None`, correct env/flag names, `f8_authority.lab_*` markers.
- [x] Pin the resolver migration: each `XxxFlagEnabled()` body in `dllmain.cpp` resolves via `ResolveF8Flag`/`FindF8Flag` (source-pin), never bare `CheckEnabled`, for the six master keys.
- [x] Pin `RonsoMana` install gate as `master || apply` and `logOnly = !apply` unchanged.
- [x] Pin Lancet warning path preserved (`grid_teach.flag off` WARN).
- [x] Pin ItemStackCap install reading `labs.item_stack_cap_value` via `Config::GetInt` with env override retained.
- [x] Behavioral: fake-provider test — F8 OFF (authority+canonical 0) with `nova_super_damage.flag` present resolves `false` (split-brain regression); untouched row with flag present resolves `true` (back-compat).
- [x] Pin static_asserts: `kFlagCount==37`, `kFlagCount <= kF8BulkRowResultMax`, `kTabCount==8`.
- [x] Run F8 RT0 → expect RED on every new pin.

### Task L1 — Catalog + defaults (GREEN)
- [x] `F8FlagCatalog.cpp`: append `"Lab"` to `kTabNames`, append 7 rows, add `kItemStackCapValue` scalar spec, bump static_asserts.
- [x] `Config.cpp` `kDefaultIni`: add `item_stack_cap_value = 255` under `[labs]`.
- [x] Run F8 RT0 → catalog pins GREEN.

### Task L2 — Install-site resolver migration (GREEN)
- [x] `dllmain.cpp`: `NovaSuperDamageFlagEnabled`, `RonsoManaFlagEnabled`, `RonsoManaApplyEnabled`, `GridTeachEnabled`, `KimahriLancetDualGrantEnabled`, `ItemStackCapFlagEnabled`, `DoubleTripleDropEnabled` resolve via catalog spec. Log-flag readers (`*_LogFlagEnabled`) keep `CheckEnabled` (not menu rows).
- [x] RonsoMana install condition `master || apply`.
- [x] ItemStackCap cap read: `EnvInt("FFXHOOKS_ITEM_STACK_CAP", GetInt("labs.item_stack_cap_value", 255))` clamped [1,255].
- [x] Run F8 RT0 + F7 RT0 → all GREEN; behavioral split-brain test passes.

### Task L3 — Mutations
- [x] M1: revert one gate reader to `CheckEnabled` → split-brain test must fail.
- [x] M2: drop `apply` from RonsoMana install condition → apply-row-alone test fails.
- [x] M3: restore `kF8BulkRowResultMax`/`kFlagCount` mismatch → static_assert fires (compile check).
- [x] M4: Lancet WARN deleted → pin fails.

### Task L4 — Integrated verification

Review was performed inline under the no-subagent policy. Final artifact/evidence is recorded in SESSION_HANDOFF; underlying Lab behavior and the new Difficulty producer still need their own live observations.
- [x] Full RT0 sweep (F8, F7, UI, config, protocol PS).
- [x] Release x86 build; hash + exports recorded.
- [x] `requesting-code-review` on the Lab-tab diff; resolve Critical/Important.
- [x] Update `docs/ROADMAP.md` lab rows → `Lane-fixed` (menu surface), keep `Wired, RT2 needed` evidence column honest.

### Task L5 — RT2 (authorization-gated, consumes the user's in-flight battery)
- [ ] Deploy with hash-verified rollback only after explicit authorization.
- [ ] Per-feature matrix: row OFF→ON persists, restart arms hook (install log line), behavior observed per existing lab RT2 evidence; row ON→OFF with a stale `.flag` file present really disarms (the F1 regression).
- [ ] Lancet-without-GridTeach produces WARN and no dual grant.
- [ ] ItemStackCap scalar edit persists and installs with chosen cap.
- [ ] Lab bulk-enable/disable reports per-row truth; game stable on next boot.
- [ ] Record accepted/rejected/partial per item; never collapse into PASS.

## 5. Source allowlist

```
src/runtime/FfxHooksDll/hooks/F8FlagCatalog.{h,cpp}
src/runtime/FfxHooksDll/shared/Config.{h,cpp}    (defaults + opt-in legacy true compatibility)
src/runtime/FfxHooksDll/ffx-hooks.ini            (item cap scalar default)
src/runtime/FfxHooksDll/dllmain.cpp              (seven gate readers + startup snapshot + Ronso gate + item-cap scalar UI/status)
src/runtime/FfxHooksDll/tests/F8RuntimeRt0.cpp
src/runtime/FfxHooksDll/run_f8_rt2_lib.psm1      (catalog anchor only)
src/runtime/FfxHooksDll/tests/run_f8_rt2_rt0.ps1 (catalog/docs assertions only)
README.md, docs/{F8_DASHBOARD,ARCHITECTURE,KNOWN_BUGS,ROADMAP}.md (current catalog/scope)
docs/superpowers/plans/2026-09-16-f8-lab-tab-integration.md
```

## 6. Hard stops

- No live-toggle semantics for lab rows — everything RestartRequired.
- No implicit enablement: Lancet never arms GridTeach; Apply never silently rewrites the master row (install gate OR is install-side only, rows stay independent).
- No `.off` marker names invented for lab rows (no new resolver surface).
- Log-only flags stay out of the menu.
- F9 UI, transport, Maechen, Fastload surfaces untouched.
- No deploy/RT2 without separate explicit authorization; the user's RT2 battery results feed Task L5, not assumptions.

## 7. Definition of done

- Lab tab visible, navigable, mouse-hit-testable with 8 tabs.
- All 7 rows persist through F8 with authority markers; effective state truthful under flag-file, env, and authority combinations.
- Split-brain closed: F8 OFF wins over a stale `.flag` for the six features.
- ItemStackCap scalar editable 1-255 and consumed at install.
- All RT0/RT1 GREEN, Release build clean, review findings resolved.
- RT2 matrix executed under authorization; roadmap updated to match observed truth.

## 8. Selected implementation choices

The user selected this plan for implementation on 2026-09-19. Use the recommended two
Ronso rows, retain Lab bulk controls, keep log-only subflags outside the menu, and expose the
Item Cap scalar. The earlier R6 scheduling note does not reopen those local choices; actual
Lab gameplay validation and deployment remain authorization-gated.
