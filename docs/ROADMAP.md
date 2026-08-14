# Roadmap — what's done, what's not, what's pending

This is the honest, complete status of every feature in ffx-mod-hooks. Nothing
is hidden. If it says "not ready", it's not ready.

## Legend

| Tag | Meaning |
|---|---|
| **Wired** | Code exists and is connected — but may still need in-game testing (RT2) |
| **Toggle only** | The dashboard item exists and persists to INI, but no runtime effect yet |
| **RE pending** | Needs reverse engineering (IDA) before it can be implemented — no guessing |
| **Never port** | Will NOT be implemented — registered as out of scope |
| **RT2 needed** | Wired but not yet validated in-game with a reproducible test |

---

## F7 In-Live menu (functional core)

| Feature | Status | Detail |
|---|---|---|
| Difficulty: HP/MP/STR/DEF/MAG/MDF/AGI/LCK/EVA/ACC | **Wired, RT2 needed** | RAM-only multipliers, presets Off/Hunter/Sombra de Sin/True Nightmare |
| Difficulty: Elemental weak/resist/absorb | **Wired, RT2 needed** | OR-masked into actor bytes (+0x5DA/5DC/5DD) |
| Difficulty: Auto-statuses | **Wired, RT2 needed** | OR into 3x u16 at +0x630 |
| Difficulty: Status immunities | **Wired, RT2 needed** | Direct byte writes at +0x641 (25 bytes) |
| Difficulty: Per-area presets (N2) | **Toggle only** | Config + apply code exists, no UI in F7 menu |
| Difficulty: Per-monster presets (N3) | **Toggle only** | Config + apply code exists, no UI in F7 menu |
| Force Last Battle | **Wired, RT2 needed** | MsBattleEncountExe on main thread, 1-9 reps |
| Music: Track lock | **Wired, RT2 needed** | Via FFXHooksBlock_v1 musicOverrideTrackIndex |
| Music: Battle-entry override | **Wired, RT2 needed** | Single-consumption pending, 45s expiry |
| Music: Randomizer | **Wired, RT2 needed** | Playlist + GetTickCount |
| Music: Fade control | **Wired, RT2 needed** | Min fade frames |
| Music: Playlist edit UI | **Toggle only** | Config supports 8 slots, no UI in MUSIC submenu |
| Music: Track name crosswalk | **Partial** | Not all 181 tracks (0..0xB5) have confirmed names |
| Monster AI Swap | **Wired, RT2 needed** | Per-monster/per-ability status injection (JSON config) |
| Monster AI Swap: target semantics | **RE pending** | Stat_action (+0xDD6) and Seck_target_id (+0x438) are hints, not proven causal |

### F7 bugs to fix

- [ ] K-01: Force Last Battle stutters (Sleep ~120ms on main thread between reps)
- [ ] K-02: Current HP/MP not rescaled on difficulty apply (monster keeps % of new max)
- [ ] K-03: Per-area (N2) and per-monster (N3) difficulty presets — add UI
- [ ] K-04: Music playlist edit — add UI
- [ ] K-05: Complete track name crosswalk (181 tracks)
- [ ] K-06: Validate AI Swap target semantics via RT2

## F8 Dashboard (implemented, DISABLED — `[dashboard] enabled=0`)

The dashboard has 6 tabs and 30 items. Here is every single one.

### Tab 1: Plugins (4 items)

| Item | Default | Status | Detail |
|---|---|---|---|
| dinput8.dll (hook proxy) | ON | **Display only** | The proxy loads our DLLs — toggle is informational |
| dxgi.dll (Special K) | OFF | **Display only** | Special K removed from deploy (INC-002 crash). No runtime effect |
| unx.dll (UnX loader) | OFF | **Display only** | UnX removed from deploy (crashed 3/3 with probe). Reimplemented safe concepts |
| ffx-probe.dll (probe tool) | OFF | **Toggle only** | Probe deployed as .RT2OFF. Toggle should rename file — not yet wired to file ops |

### Tab 2: Boosters (4 items)

| Item | Default | Status | Detail |
|---|---|---|---|
| Entire Party Earns AP | OFF | **Wired, RT2 needed** | 30Hz timer writes participation=2 + earn=1 at 0x1F10EA0/0x1F10EC4 |
| Permanent Sensor | OFF | **Wired, RT2 needed** | Debug byte 0xD2A8F8+0x15 = 1 at 30Hz |
| Playable Seymour | OFF | **Wired, RT2 needed** | Party struct VALIDATED 2026-08-14 (base RVA 0xD32088, 18 slots x 148B, Seymour slot 7, in_party bit0). Wired in UnXBoosterHook (30Hz) |
| Speed Hack (F2 hold) | OFF | **RE pending** | Needs RE of game tick seam. Not implemented |

### Tab 3: Cheats (6 items)

| Item | Default | Status | Detail |
|---|---|---|---|
| Always Overdrive | OFF | **Wired, RT2 needed** | Debug byte 0xD2A8F8+0x04 = 1 at 30Hz |
| Always Critical | OFF | **Wired, RT2 needed** | Debug byte 0xD2A8F8+0x05 = 1 at 30Hz |
| Damage 99999 | OFF | **Wired, RT2 needed** | Debug byte 0xD2A8F8+0x08 = 1 at 30Hz |
| Always Rare Drop | OFF | **Wired, RT2 needed** | Debug byte 0xD2A8F8+0x09 = 1 at 30Hz |
| 100x AP | OFF | **Wired, RT2 needed** | Debug byte 0xD2A8F8+0x0A = 1 at 30Hz |
| 100x Gil | OFF | **Wired, RT2 needed** | Debug byte 0xD2A8F8+0x0B = 1 at 30Hz |

### Tab 4: Field (7 items)

| Item | Default | Status | Detail |
|---|---|---|---|
| FieldScout Master | OFF | **Wired, RT2 needed** | FieldScout hooks installed |
| Heavy Mode | OFF | **Wired, RT2 needed** | FieldScout sub-mode |
| Max Mode | OFF | **Wired, RT2 needed** | FieldScout sub-mode |
| Ultra Mode | OFF | **Wired, RT2 needed** | FieldScout sub-mode |
| Ultra: Materials | OFF | **Wired, RT2 needed** | Ultra sub-toggle |
| Ultra: Sound | OFF | **Wired, RT2 needed** | Ultra sub-toggle |
| Ultra: Encounters | OFF | **Wired, RT2 needed** | Ultra sub-toggle |

### Tab 5: Arena+ (5 items)

| Item | Default | Status | Detail |
|---|---|---|---|
| Arena+ Master | OFF | **Wired, RT2 needed** | ArenaPlus hooks installed |
| Compose F7 (boss rush) | OFF | **Wired, RT2 needed** | Compose engine + F7 menu integration |
| Victory Hook | OFF | **Wired, RT2 needed** | BattleEndHook |
| Resolver Log | OFF | **Wired, RT2 needed** | ResolverLogHook |
| Arena+ Music | OFF | **Wired, RT2 needed** | Music hook with Arena+ battle-entry pending |

### Tab 6: Input (4 items)

| Item | Default | Status | Detail |
|---|---|---|---|
| Block Windows Key | ON | **Wired** | Prevents Windows key from stealing focus |
| Fix Background Input | ON | **Wired** | Keeps game receiving input when window loses focus |
| Filter IME (crashes) | ON | **Wired** | Prevents IME composition strings from crashing |
| Dialog Skip (voices) | OFF | **Wired, RT2 needed** | Reversible hook on FmodVoice_ReadEventData (RVA 0x30B040) |

### F8 bugs to fix before re-activation

- [ ] K-10: Never passed a full RT2 — run_f8_rt2.ps1 exists but was never run
- [ ] K-11: Flicker risk — same Present-hook + force-subsystem pattern as F7
- [ ] K-12: Playable Seymour — party struct validated + wired (2026-08-14); RT2 pending
- [ ] K-13: Speed Hack — needs RE of game tick seam before implementation
- [ ] ffx-probe toggle — dashboard item should rename .RT2OFF file to activate
- [ ] Key arbitration stress test — verify F7 and F8 never fight over same key

### F8 re-activation steps (when ready)

1. Config.cpp kDefaultIni: [dashboard] enabled = 0 -> 1
2. dllmain.cpp: 2x GetBool("dashboard.enabled", false) -> true
3. Build Release + deploy with named backups
4. RT2: run_f8_rt2.ps1 (editor CLOSED, disposable save, hashes before/after)
5. Checks: F8 opens, closes cleanly, F7 untouched, no dispute, no flicker, no crash

---

## Lab hooks (gated OFF, not in any menu)

| Hook | Gate flag | Status |
|---|---|---|
| NovaSuperDamage (99999) | nova_super_damage.flag | Wired, RT2 needed |
| RonsoMana | kimahri_ronso_mana.flag | Wired, lab (log-only) |
| NulWard (320/321) | nul_ward.flag | Wired, lab (WARN: conflicts NovaClamp K-21) |
| GridTeach v4.5 | grid_teach.flag | Wired, RT2 needed |
| NulWardTeach (legacy) | nul_ward_teach.flag | Wired, lab (prefer GridTeach) |
| KimahriLancetDualGrant | kimahri_lancet_dual_grant.flag | Wired, lab (needs GridTeach) |
| SphereGridTrueNewNode | true_new_node.flag | Lab, RT2-blocked (excl. FullGrid K-22) |
| SphereGridFullGridCompiler | sg_full_grid_compiler.flag | Lab |
| ItemStackCap (99->255) | item_stack_cap_255.flag | Wired, lab |
| DoubleTripleDrop | (battle callers) | Wired, lab |
| ElementHook (Scan Holy/Dark) | element_scan_dark.flag | Wired, RT2 needed |
| SinCurseHook | sin_curse.flag | Wired, RT2 needed |
| PhaseTurnEdge | — | DISABLED in build |
| BootSkip | fast_boot_skip.flag | WIRE-ME commented (aguarda RT2) |

---

## Never port (out of scope)

- Soft Reset / KillMeNow (mem b D2A8E2 2) — byte patch, unsafe
- WININET auto-update — network auto-updater, not our model
- Special K texture injection — not compatible
- Cheat Engine sig-scan — not our approach
- Hardcoded .text patches (0x392930, 0x30B040, Btl.battle_trigger) — replaced with reversible hooks

---

## SIN system

| Feature | Status |
|---|---|
| Area transition detection | Wired, RT2 needed |
| Threat roll (XorShift32) | Wired |
| HP/stat scaling | Wired, RT2 needed |
| ATEL handler injection | Wired, RT2 needed |
| Kernel name modification | Wired, RT2 needed |
| UNI-001 Gloom | Implemented |
| UNI-002 March | Implemented |
| UNI-003 Rush | Implemented |
| UNI-004 Ward | Implemented |
| UNI-005 Frost | Reserved |
| UNI-006 Tide | Reserved |
| UNI-007 Salve | Reserved |
| UNI-008 Mist | Reserved |
| thunder_plains | Out of execution by design (cap=1) |
| Chimera/Xiphos multi-worker | Not implemented |
| Full hook -> injector RT2 | Pending |

### SIN TODO

- [ ] Implement UNI-005 through UNI-008
- [ ] Implement multi-worker monster support
- [ ] RT2: full hook -> injector flow end-to-end

---

## ffx-probe

| Feature | Status |
|---|---|
| READ (main thread) | Proven (RT2 2026-06-03) |
| WRITE (main thread) | Proven (Gil, items, debug flags) |
| CALL (cdecl) | Proven |
| ForceBattle (atomic) | Proven (battle on screen, no crash) |
| ForceBattle (clean, current field) | Proven (real encounter, no void) |
| CALL (thiscall) | RE pending (probe is cdecl-only) |
| Probe in deploy | OFF (.RT2OFF suffix, needs re-activation) |
| Editor wiring | Pending (FfxProbe_Service exists, not wired) |
| Dashboard toggle (file rename) | Toggle only (no file-op wiring) |
| Adversarial RT2 (menu/FMV/mid-battle) | Pending |

### Probe TODO

- [ ] Re-activate probe in deploy (remove .RT2OFF with backup)
- [ ] Wire dashboard toggle to file rename
- [ ] Wire editor FfxProbe_Service to MMF
- [ ] Implement thiscall CALL (ECX shim)
- [ ] Adversarial RT2: ForceBattle during menu, FMV, active battle

---

## FFX Mod Studio integration (future)

| Feature | Status |
|---|---|
| RuntimeDllManager | Exists in editor, not connected to this repo's releases |
| FfxProbe_Service | Exists in editor, not wired in deploy |
| LiveBattleLab | Should route through probe, not CreateRemoteThread |
| Contracts sync | Canonical copies in contracts/ — editor must consume these |
| Submodule integration | Future — editor can consume via git submodule when stable |

---

## Summary by status

| Status | Count |
|---|---|
| Wired + RT2 needed | ~26 |
| Toggle only (UI exists, no effect) | 5 |
| RE pending (no guessing) | 2 |
| Display only (informational) | 3 |
| Never port | 5+ |
| Lab / experimental | 12 |
| Proven (RT2 passed) | 5 |
| Reserved / not implemented | 4 |

**Bottom line:** F7 is functional but needs RT2. F8 is implemented but disabled
and needs a full RT2 pass. Two items need RE (Speed Hack, probe thiscall);
Playable Seymour is now wired (RE 2026-08-14, RT2 pending). SIN has 4 of 8 UNI
presets. The probe is proven but off in deploy.
