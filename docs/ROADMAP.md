# Roadmap — what's done, what's not, what's pending

This is the honest, complete status of every feature in ffx-mod-hooks. Nothing
is hidden. If it says "not ready", it's not ready.

## Legend

| Tag | Meaning |
|---|---|
| **In-game** | Ported, deployed, and observed working in a real game session (RT2 user evidence) |
| **Wired** | Code exists and is connected — but may still need in-game testing (RT2) |
| **Fixed in dev** | Corrected on the development branch; pending merge, deploy, and RT2 revalidation |
| **Toggle only** | The dashboard item exists and persists to INI, but no runtime effect yet |
| **RE pending** | Needs reverse engineering (IDA) before it can be implemented — no guessing |
| **Never port** | Will NOT be implemented — registered as out of scope (includes retired lab entries) |
| **RT2 needed** | Wired but not yet validated in-game with a reproducible test |

---

## F7 In-Live menu

| Feature | Status | Detail |
|---|---|---|
| F7 menu shell (open, navigate, render, glyph footers) | **In-game** | Observed 2026-09-16; Back footer glyph corrected to Backspace (**fixed in dev**) |
| Difficulty: Max/current HP/MP, Overkill, STR/DEF/MAG/MDF/AGI/LCK/EVA/ACC | **Fixed in dev, RT2 needed** | Exact-width, RAM-only transforms with transactional max/current ratios across partial failures, immutable baselines, indeterminate-write retry, and compare-before-restore. 2026-09-16: preset admission decoupled from the broad F7 master gate — the `Apply unavailable: Installed` contradiction is fixed, awaiting redeploy + RT2 |
| Difficulty: Elemental weak/resist/absorb | **Quarantined** | Configuration-only until exact executable xrefs and widths are proved |
| Difficulty: Auto-statuses | **Quarantined** | Configuration-only until exact executable xrefs and widths are proved |
| Difficulty: Status immunities | **Quarantined** | Configuration-only until exact executable xrefs and widths are proved |
| Difficulty: Per-area presets (N2) | **Configuration only in UI** | Bounded JSON plus runtime replacement semantics exist; no native menu editor |
| Difficulty: Per-monster presets (N3) | **Toggle only** | Config + apply code exists, no UI in F7 menu |
| Force Last Battle | **Wired, RT2 needed** | MsBattleEncountExe on main thread, 1-9 reps |
| Music: Track lock | **Wired, RT2 needed** | Via FFXHooksBlock_v1 musicOverrideTrackIndex |
| Music: Battle-entry override | **Wired, RT2 needed** | Single-consumption pending, 45s expiry |
| Music: Randomizer | **Wired, RT2 needed** | Playlist + GetTickCount |
| Music: Fade control | **Wired, RT2 needed** | Min fade frames |
| Music: Playlist edit UI | **Toggle only** | Config supports 8 slots, no UI in MUSIC submenu |
| Music: Track name crosswalk | **Partial** | Not all 181 tracks (0..0xB5) have confirmed names |
| Monster AI observer | **Wired, RT2 needed** | Default-OFF, zero mutation, three-target registration/cleanup/dispatcher observer with value-only lifecycle and dispatch telemetry |
| Monster AI Swap mutation | **RE pending** | Empty compatibility whitelist; closest corpus pair m342/m343 is rejected |

### F7 bugs to fix

- [ ] K-01: Add actual machine-detour scheduler RT1, then run the separately authorized
      baseline/ON/edit/OFF/next-battle RT2
      sequence. Production config-API/runtime concurrency, paired partial-write retry, and the
      conservative paused-before-first-increment retention policy pass isolated RT1; the exact
      three-target Difficulty batch is composed with the process-global coordinator at RT0/build.
- [ ] K-02: Prove exact element and status field xrefs/widths before enabling any runtime write.
- [ ] K-03: Add a native menu editor for per-area (N2) replacement rules.
- [ ] K-04: Music playlist edit — add UI
- [ ] K-05: Complete track name crosswalk (181 tracks)
- [ ] K-06: Validate the observe-only lifecycle plus normal/force/death dispatch families,
      repeated-event preservation, unchanged ABI/readback, and zero mutation via the dedicated
      read-only RT2 slice; keep mutation blocked.
- [ ] K-07: Revalidate Difficulty preset Apply (admission/status split, fixed in dev) and the
      Backspace footer hint in the next authorized RT2 after release.

## F8 Dashboard (shell + LIVE rows In-game; known gaps listed)

The native FLAGS catalog has six tabs — Plugins, Boosters, Cheats, Scout, Arena+, Input — and
29 rows: 14 LIVE, 8 RESTART REQUIRED, 7 NOT WIRED. The tracked and built-in templates set
`[dashboard] enabled=1`, but all 14 LIVE targets default false. Config-load failure is still OFF.
The dashboard shell, tab navigation, LIVE row effects, and bulk actions were observed in-game
on 2026-09-16.

### Tab 1: Plugins (4 items — none wired)

| Item | Default | Status | Detail |
|---|---|---|---|
| dinput8 - hook proxy | ON | **Not wired — display only** | The proxy loads our DLLs; the menu cannot load it |
| dxgi - Special K | OFF | **Not wired — display only** | Special K removed from deploy (INC-002 crash); no runtime effect |
| unx - UnX loader | OFF | **Not wired — display only** | UnX removed from deploy (crashed with probe); safe concepts reimplemented |
| ffx-probe | OFF | **Not wired — display only** | Probe deployed as `.RT2OFF`; toggle is not wired to file ops |

### Tab 2: Boosters (4 items — LIVE)

| Item | Default | Status | Detail |
|---|---|---|---|
| Permanent Sensor | OFF | **In-game** | Debug byte `0xD2A8F8+0x15` — enemy details without Sensor gear |
| Playable Seymour | OFF | **In-game (experimental)** | Party struct `0x00D32060`, slot 7, `in_party` bit0; Sphere Grid unsupported; selection/switch/exit cleanup still needs strict RT2 |
| Speed Hack | OFF | **In-game** | `boosters.speed_hack` arms native 2x/4x; custom 8x on reviewed field-scene tick (Ctrl+Shift+K cycles); dialogue/rendered-scene coverage RT2-pending; FMV unsupported |
| Entire Party Earns AP | OFF | **In-game** | Participation `0x01F10EA0` + earned `0x01F10EC4` (seven-byte arrays) |

### Tab 3: Cheats (8 items — LIVE)

| Item | Default | Status | Detail |
|---|---|---|---|
| Invincible Party | OFF | **In-game** | Debug-byte family — party HP hold |
| Invincible Enemies | OFF | **In-game** | Enemy HP hold |
| Always Overdrive | OFF | **In-game** | Overdrive gauges held full in battle |
| Always Critical | OFF | **In-game** | Eligible attacks forced critical |
| Damage 99999 | OFF | **In-game** | Supported damage set to 99999 |
| Always Rare Drop | OFF | **In-game** | Rare drops forced where supported |
| AP Multiplier | OFF (rate 100x) | **In-game** | Scalar `cheats.ap_multiplier` 1-100 — supported AP rewards multiplied by configured rate |
| Gil Multiplier | OFF (rate 100x) | **In-game** | Scalar `cheats.gil_multiplier` 1-100 — supported Gil rewards multiplied by configured rate |

### Tab 4: Scout (4 items — RESTART REQUIRED)

| Item | Default | Status | Detail |
|---|---|---|---|
| FieldScout Master | OFF | **Wired — arms next launch; RT2 needed** | Basic field capture; gates `field_scout.flag` / env |
| FieldScout Heavy | OFF | **Wired — arms next launch; RT2 needed** | Extra field details |
| FieldScout Max | OFF | **Wired — arms next launch; RT2 needed** | Requires Heavy + Ultra |
| FieldScout Ultra | OFF | **Wired — arms next launch; RT2 needed** | Requires Heavy |

### Tab 5: Arena+ (5 items)

| Item | Default | Status | Detail |
|---|---|---|---|
| Arena+ Master | OFF | **Wired — arms next launch; RT2 needed** | Gates `arena_plus.flag` / `FFXHOOKS_ENABLE_ARENA_PLUS` |
| Arena+ Compose F7 | OFF | **Unavailable — quarantined** | LIVE/ConfigPolled but compose is quarantined; row reports Unavailable, no recovery |
| Arena+ Victory Hook | OFF | **Wired — arms next launch; RT2 needed** | Victory log; rewards unchanged (scaffold/log-only) |
| Arena+ Resolver Log | OFF | **Wired — arms next launch; RT2 needed** | Logs match choices |
| Arena+ Music | OFF | **Wired — arms next launch; externally blocked** | `arena_plus_music.flag.off` present in the install dominates the row (`ExternalOverride`); bulk never deletes it silently |

### Tab 6: Input (4 items)

| Item | Default | Status | Detail |
|---|---|---|---|
| Block Windows Key | ON | **Not wired — display only** | Cannot block the Windows key from this menu |
| Fix Background Input | ON | **Not wired — display only** | Background input cannot be changed here |
| Filter IME | ON | **Not wired — display only** | IME filtering unavailable here |
| Dialog Skip | OFF | **In-game** | LIVE/ConfigPolled — skips voiced lines when enabled |

### F8 known gaps

- Plugins tab (4) and three Input rows are display-only — no runtime effect exists.
- Arena+ Compose F7 stays quarantined; Victory/Resolver log-only until RT2.
- Arena+ Music cannot arm while `arena_plus_music.flag.off` (or `music.flag.off`) exists in
  `modules/` — the row now reports the blocking artifact by name instead of a bare "blocked".
- Speed Hack: dialogue/rendered-scene coverage unproven; pre-rendered FMV unsupported.
- Playable Seymour: Sphere Grid unsupported; selection/Switch/turn/exit/next-battle cleanup
  still need strict RT2 evidence.

### Technical notes

The corrected Debug base is RVA `0x00D2A8F8`, with one-byte owned fields. AP participation and
AP-earned are exactly seven-byte arrays at RVAs `0x01F10EA0` and `0x01F10EC4`. The party
structure is RVA `0x00D32060`, with slot-zero `in_party` at `0x00D32088` and a 148-byte stride.
The debug/AP producer runs from Present through `UnXBoosterFrameTick()` at a 33 ms gate; there is
no F8 window timer.

2026-09-16 **fixed-in-dev** updates pending release:

- Bulk actions renamed `Enable Supported` / `Disable Supported` and now classify per row:
  `Changed / Already / Unavailable / ExternalOverride / InvalidParameter / EffectiveMismatch`
  with one atomic INI write; the previous `0 changed, 3 already, 2 blocked` report conflated
  quarantine, external `.off` markers, and unsupported rows.
- External overrides name the dominating artifact (e.g. `arena_plus_music.flag.off`).
  Marker files are never deleted silently.
- Direct-F8 menus now drain on focus loss like every other F-key surface; the close-on-focus-loss
  behavior is a pinned safety contract, and a held F-key cannot reopen a menu on focus return
  (fresh press required).

Next gate: run one separately authorized, manual RT2 case at a time through
`run_f8_rt2.ps1 -Phase Preflight` and `-Phase Verify`. The script never launches/stops FFX or
the editor and never deploys. Dynamic `FreeLibrary`/hot unload remains unsupported. Source,
RT0, and build evidence are not RT2 or Production.

---

## Maechen F9 assistant (in-game transport; answer quality in progress)

| Feature | Status | Detail |
|---|---|---|
| F9 translucent UI (title/input/answer/footer, native shell) | **In-game** | Observed 2026-09-16; layout/protocol frozen — no UI redesign is planned |
| Transport `POST /api/maechen/game/v1` | **In-game** | Protocol v1 (`{locale, question, version}`); Production probe returned HTTP 200 with a valid bounded answer; in-game round-trip observed |
| Answer grounding (native path) | **Fixed in dev (in progress)** | Root cause measured: `allowExternalSearch=false` also blocks the bundled guide (17 areas) and wiki runtime (~700 local articles); location questions can never reach either. Fix lands in server-side arbitration only |
| Protocol v2 local context | **Proposed, not approved** | Opt-in (default OFF) bounded scalar digest — current location, gil, party count, save slot, effective/blocked flag digest, build id — so answers can reference the player's real client state. No save bytes, file paths, or player names |
| Conversation memory | **Future** | Protocol v1 is stateless by design |
| Save-file analysis | **Deferred** | Raw save parsing requires a save-format spec; only a vanilla-decoded slot-header reuse would be reconsidered |

---

## What is still missing (consolidated gaps)

- RT2 validation coverage for every LIVE row (F7 writers, Speed cycle, Dialog Skip, Seymour
  cleanup, Debug/AP, Arena+ Victory/Resolver/Music) — one authorized case at a time via
  `run_f8_rt2.ps1`.
- Maechen native retrieval fix merge + release; protocol v2 local context pending approval.
- Fastload Autosave implementation (approved plan: F8 Dev gate, default OFF, observe-only
  first, newest autosave only, Shift bypass, no save-byte writes).
- Difficulty quarantined families (elemental, statuses, immunities) — exact xref/width proof.
- Per-area (N2)/per-monster (N3) preset native editors; Music playlist UI; 181-track crosswalk.
- Monster AI Swap mutation RE (corpus whitelist still empty).
- ffx-probe reactivation in deploy, dashboard file-rename wiring, editor `FfxProbe_Service` MMF.
- FFX Mod Studio integration (RuntimeDllManager releases, contracts consumption).

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
| ItemStackCap (99->255) | item_stack_cap_255.flag | Wired, lab |
| DoubleTripleDrop | (battle callers) | Wired, lab |
| ElementHook (Scan Holy/Dark) | element_scan_dark.flag | Wired, RT2 needed |
| SinCurseHook (legacy writer) | — | **Unavailable** — no detour, process launch, or runtime area |
| PhaseTurnEdge | — | DISABLED in build |
| BootSkip | fast_boot_skip.flag | **Superseded** — the commented scaffold is retired by the approved Fastload Autosave plan (default OFF, observe-only first, autosave only, Shift bypass) |

---

## Never port (out of scope)

- Soft Reset / KillMeNow (mem b D2A8E2 2) — byte patch, unsafe
- WININET auto-update — network auto-updater, not our model
- Special K texture injection — not compatible
- Cheat Engine sig-scan — not our approach
- Hardcoded .text patches (`0x392930`, rejected stale interior voice RVA `0x30B040`, `Btl.battle_trigger`) — replaced with validated reversible hooks where supported
- SphereGridTrueNewNode (`true_new_node.flag`) — retired from the project (was Lab, RT2-blocked, excl. FullGrid K-22)
- SphereGridFullGridCompiler (`sg_full_grid_compiler.flag`) — retired from the project (was Lab)

---

## SIN system

The legacy field-transition writer is quarantined. Its old flags, environment values, and sidecars
are ignored; they create neither requested nor effective runtime state and cannot install a detour
or launch the offline materializer.
The F7 row is read-only and reports `Unavailable`. No S.I.N. area is currently supported by this
legacy runtime path. The offline codecs/presets below remain research assets, not live-hook claims.

| Feature | Status |
|---|---|
| Legacy runtime field hook | **Unavailable / quarantined** |
| Runtime-supported areas | **None** |
| Runtime process/materializer launch | **Removed** |
| Future RAM-only threat scaling | Separate design/review lane |
| Offline UNI-001 Gloom | Implemented in tooling only |
| Offline UNI-002 March | Implemented in tooling only |
| Offline UNI-003 Rush | Implemented in tooling only |
| Offline UNI-004 Ward | Implemented in tooling only |
| UNI-005 Frost | Reserved |
| UNI-006 Tide | Reserved |
| UNI-007 Salve | Reserved |
| UNI-008 Mist | Reserved |
| thunder_plains | Out of execution by design (cap=1) |
| Chimera/Xiphos multi-worker | Not implemented |
| Full hook -> injector RT2 | Cancelled for the quarantined writer |

### SIN TODO

- [ ] Implement UNI-005 through UNI-008
- [ ] Implement multi-worker monster support
- [ ] Design and review a bounded RAM-only S.I.N. replacement

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
| In-game (observed in a real session) | F7/F8/F9 shells + transport, focus-loss drain, 13 F8 LIVE rows (Boosters 4, Cheats 8, Dialog Skip; Seymour experimental) |
| Fixed in dev (awaiting release/RT2) | 4 (Backspace glyph, Difficulty admission, F8 bulk truth, direct-F8 drain) |
| Wired + RT2 needed | ~15 (F8 restart-required rows, F7 writers, lab hooks) |
| Toggle only (UI exists, no effect) | 6 |
| RE pending (no guessing) | 1 |
| Display only (informational) | 3 |
| Never port / retired | 7+ |
| Lab / experimental | 10 |
| Proven (RT2 passed) | 5 |
| Reserved / not implemented | 4 |

**Bottom line:** the F7/F8/F9 native menu shells, the Maechen transport, and 13 F8 LIVE rows
(Boosters, Cheats incl. AP/Gil multipliers, Dialog Skip; Seymour experimental) are **In-game**
per user RT2 evidence. Four user-reported defects are **fixed in dev** awaiting release/RT2.
F7 Difficulty writes, the 8 F8 restart-required rows (Scout/Arena+), and lab hooks remain
RT2-pending; Arena+ Compose is quarantined and Arena+ Music is externally blocked by
`arena_plus_music.flag.off`. BootSkip is superseded by the approved Fastload plan; the two
Sphere Grid lab entries are retired. S.I.N. RAM is an offline candidate; its legacy disk writer
remains quarantined. The probe has family-specific prior evidence but is off in deploy.
