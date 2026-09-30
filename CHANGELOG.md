# v0.6.0-beta.3 — 2026-09-29

- Add nine selectable F8 interface languages with Unicode captions and independent persistence. Failed saves preserve the prior locale. Non-FLAGS F7 pages remain native English.
- Publish optional Fahrenheit V2 bridge/provider for shared hooks, frame/input ownership, managed saves and paired text/font resources, plus complete corresponding modified provider source. Stock V1 remains restricted.
- Correct navigation, confirmation, rejection and Back sounds according to the actual F7/F8 action outcome; retain persistence drafts on failure.
- Harden focus loss, render ownership, provider cleanup and source verification across all 501 pinned upstream files.
- Bind the public source to merged runtime c18fb5602a688dd0de5e7e85677c5d5102402788 and the 4,683,264-byte validated DLL. Eleven Windows suites and isolated CLR transport passed; live RT2 remains pending.

# v0.6.0-beta.2 — 2026-09-29

- Integrate the published recovery, Seymour and Sphere Grid work with the current runtime.
- Add NulHoly/NulShadow/NulEarth/NulWind/NulPoison/NulGravity, a separate default-OFF gate, save-bound learning and hash-pinned private FX authoring recipes.
- Include built-in element names and preserved aliases, Arcana elemental major-card effects and independent Holy/Shadow weapon visuals.
- Preserve general/per-monster AP/Gil composition, previous gameplay options and native widths/defaults.
- Ship the matching tested DLL and complete public source, English/PT-BR documentation and donation links.
- New visuals and full live save/battle acceptance remain pending; no proprietary game assets or personal-save unlocks are distributed.

# Changelog

All notable changes to ffx-hooks are documented here. The project uses independent SemVer
(`MAJOR.MINOR.PATCH[-pre]`) and will stay below `1.0.0` while in beta. Versioning rules:
MINOR = new capability; PATCH = fix/reuse/polish; REVISION (4th component, doc-only) = notes
that do not change behavior.

## [Unreleased]


## [0.6.0-beta.1] — 2026-09-29

- Restore the original PayPal support section and donation badge in the English README.
- Add a complete `README.pt-BR.md`, covering the same DLL inventory, installation, configuration, build, validation limits, credits, license and support information; link both languages prominently.
- Include both READMEs in the binary ZIP and matching tagged source archive. Keep the original v0.6.0-beta tag and archives intact.
- Documentation/package revision only: the DLL and all 300 bound production inputs are unchanged from v0.6.0-beta.


## [0.6.0-beta] — 2026-09-28

- Consolidate Vanguard (31 controls), Elemental Dominion, Spira Reforge, paid Aeon Ascension, Equipment Workshop, text-language support and Arcana (78 cards) in one DLL.
- Reorganize F8 into seven tabs and 88 canonical boolean controls; group Additional mods, FieldScout and AP/Gil Multipliers into native submenus.
- Show eight native plus two Hook-only elements in F7/Scan, with stable identities, applied-affinity ownership, colors and visibility.
- Add independent per-monster AP/Gil factors (1–1000), general factors (1–100), original/individual/total previews and atomic TSV persistence. Wide bounded arithmetic runs before vanilla bonuses without widening monster WORDs.
- Include Arcana's runtime artwork, OFF settings examples, installation guides, licenses and source/binary hashes in the full binary package; publish the corresponding complete source tree.
- Refresh README and checked roadmap without erasing completion history; document actual Editor gaps, inactive Spira definitions and current RT2 limits.
- Repair a test-only GCC misleading-indentation build error; production DLL inputs remain byte-identical to the validated source checkpoint.
- Windows Release/RT0/RT1 and 22 same-binary Proton cases validate the distributed DLL. Current-candidate gameplay/visual acceptance remains pending; this release is beta, not a Production promotion.


## [0.5.0-beta] — 2026-09-27

- Integrated the Equipment Workshop's native equipment and Customize views,
  inventory refresh, sound feedback, priced fusion/refinement, comparison
  panels, and recoverable fifth-ability sidecar. Native equipment records keep
  their original four ability slots.
- Completed bounded Customize eligibility, material costs, fifth-ability
  ordering, and the 131-entry ability catalog (125 native recipes and six
  disclosed mod recipes). Development shortcuts remain separate and OFF by
  default.
- Added independent F8 controls for Scan Expanded and extra Scan elements.
  Scan Expanded includes eight stats, native MP when present, and attribute
  backgrounds; the extra-element path covers Holy, Darkness, and Custom.
- Fixed post-battle F10 list recovery and native Scan state handling. The
  expanded affinity rows now follow the game's state.
- Release binary is the Windows x86 MSVC/PolyHook candidate built from private
  source commit `2979ca577a284d7e8aeb816cc1b6a74a37f72569`. Windows and
  Proton harnesses and loader checks passed. These results are RT0/RT1;
  the new Workshop/Scan visuals still need player-run RT2 acceptance.

## Historical development notes

### 0.2.0 RT2 candidate

- **F8 Speed cycle**: Ctrl+Shift+K now cycles 1x -> 2x -> 4x -> 8x -> 1x after the
  default-OFF Speed Hack flag is armed. A top-screen indicator reports the effective speed.
  Native 2x/4x and the clean-room 8x backend have separate ownership and restoration checks.
  The 8x route targets the reviewed field-scene service tick and shares the bounded voice-bypass
  owner. Broader dialogue/cutscene coverage is a live acceptance observation, not an offline
  guarantee. Pre-rendered video remains outside this hook, and F12 is never consumed because the
  game keeps it for screenshots.
- **F9 Maechen client**: added a default-OFF native question/answer modal with strict foreground
  admission, bounded UTF-8 protocol validation, 25-second cancellation, cross-modal ownership,
  and focus-epoch handling. The remote route is maintained in the website repository and remains
  independently testable/deployable from the DLL.
- **Playable Seymour**: replaced the old party-bit-only behavior with an exact-profile,
  battle-entry/exit roster transaction composed through the shared InitScene runtime. It is
  reversible, disabled by default, excludes Sphere Grid state, and remains experimental pending
  user-run RT2.
- **F7 RAM-only Difficulty and S.I.N.**: Difficulty is the sole actor writer. S.I.N. composes its
  reviewed structural fields for the bounded natural-encounter catalog through that writer,
  preserves one final HP ratio, leaves MP under Difficulty ownership, persists through the
  atomic F7 JSON saver, and never modifies battle .bin files.
- **Monster AI Observer**: removed the misleading swap claim and every reachable mutation route.
  The default-OFF page now exposes bounded read-only dispatcher telemetry with an empty compatible
  pair whitelist.
- **CustomMix Ultra**: added a session-only, RAM-only symbolic picker for one to eight actors.
  The allowlist contains the reviewed aeon IDs only, supports repetitions and the three-member
  Magus expansion, patches exactly the 16-byte vanilla carrier formation window during InitScene,
  restores compare-safely, and remains isolated from legacy x3/x4/x5 disk composition.
- **Maintenance**: refreshed English lifecycle/RE comments, truthful menu labels, version resources,
  and candidate documentation. All gameplay writers remain default OFF and Production promotion
  still requires the documented user-run RT2 matrix.

### Fixed

- **Monster AI Swap truth/safety correction** (2026-08-23, Jarvis-HOOK): removed the reachable
  frame-polled status injector, direct actor-script byte writer, and legacy JSON Save/Reload UI.
  The existing `f7.aiswap` gate now requests only a default-OFF, exact-profile/signature-gated
  registration/cleanup observer. Its eight-slot output is bounded to IDs, worker counts, and
  deterministic hashes; the compatible-pair whitelist is empty. This is an offline observer
  candidate pending read-only RT2, not a functioning AI swap. Callback-first leases and a bounded
  close/drain/queue-disable/apply/exact-disable/drain transaction neutralize installed hooks; after
  any apply attempt their trampolines remain allocated and truthfully `INERT` until process exit
  because an uncounted CPU may be paused in the machine prologue. A new process-global single-flight
  coordinator is the sole caller of MinHook initialization and queued APIs, preventing Monster AI
  and FieldScout from applying each other's pending hooks. Initialization happens before any feature
  starts instead of depending on the FieldScout gate or install order, and a failed initialization
  now blocks every MinHook consumer. Any failed `MH_ApplyQueued` attempt poisons queue ownership
  until restart even when compensating disables succeed. FieldScout now applies its complete batch
  in every mode, uses one process-sticky admission word across all eleven shims, drains trace-thread
  creation, retains applied trampolines and runtime context until restart, releases unowned pre-hook
  allocations on early failure, and never reopens capture from a delayed path callback. No subsystem
  calls `MH_Uninitialize()`. The shared enum distinctly admits Difficulty and Seymour ownership for
  their separate reviewed lifecycles; this observer lane exercises neither runtime owner. If
  process-global MinHook setup fails while the observer is requested, the worker now publishes
  `UNAVAILABLE` through a status-only path instead of rendering a false pending-restart state.

### MINOR

- **Playable Seymour (F8 booster) — party struct RE + apply wired** (2026-08-14, Jarvis-MAGIC/Shiva):
  party array validated in the canonical DB (base VA 0x1132088 / RVA 0xD32088,
  18 slots x 148B, Seymour = slot 7, in_party byte bit0). `UnXBoosterHook` now
  applies the game's own assign/remove formulas (bit0+bit4 / `&= ~1`), stateful
  so it never fights the temporary Seymour of the Sinspawn Gui battle. Gate:
  Release build with PolyHook 0 errors. **RT2 pending** — item stays OFF by
  default. Doc: research/f8_recovery/PLAYABLE_SEYMOUR_PARTY_RE_2026-08-14.md

- **Config gate unificado (INI > env > legacy .flag) — migração de ~35 gates** (2026-08-14, Jarvis-MAGIC/Shiva):
  `Config::CheckEnabled` agora é público e cobre TODOS os caminhos legados de flag
  (`modules\<flag>`, `config\<flag>`, `modules\config\<flag>`, raiz do jogo). Todos os
  gates de dllmain (labs/music/arena/fps) + F7 (f7.inlive / f7.aiswap) + Arena*
  migrados para o check combinado. Novas seções INI: `[f7]`, `[music]`, `[labs]`
  (todas default 0 = OFF). **Backward-compat total:** env vars e .flag files seguem
  honrados (mesmos nomes/caminhos) — a migração é ADITIVA, nada deixa de funcionar.
  Gate: Release build com PolyHook 0 erros.
- **Speed Hack historical prototype — game tick RE + hook wired** (2026-08-14, Jarvis-MAGIC/Shiva):
  game tick seam validated (frame delta = `FFXField+0x24` float, consumed by the
  scene tick and the game clock). New `SpeedHackHook` inline-hooks
  `FFX_Field_UpdateAndRender` (RVA 0x2F600) and scales the delta by
  `speed_hack.factor` (default 2.0, gate `boosters.speed_hack`). This historical F12
  input prototype was superseded by the current `Ctrl+Shift+K` cycle; F12 is screenshot-only.
  Gate: Release build with PolyHook 0 errors. **RT2 pending** — OFF by default.
  Doc: research/f8_recovery/SPEED_HACK_GAME_TICK_RE_2026-08-14.md


## [0.1.0-beta.1] — 2026-08-13

First independent release. The runtime hook layer leaves the FFX Mod Studio monorepo
(`RuntimeTools/FfxHooksDll`, `FfxDinput8Probe`, `SinCoreLib`, `SinScaleInject`,
`NativeMenuShell`, `BattlePhotoMode`) and becomes its own repository.

### Functional (beta)

- F7 In-Live menu: RAM-only difficulty levers, Force Last Battle, battle music control,
  Monster AI Swap (per-monster/per-ability status injection, JSON config).
- ffx-probe: main-thread DINPUT8 probe (READ/WRITE/CALL, ForceBattle opcode), proven in-game.
- F8 Dashboard: implemented, disabled by default (`[dashboard] enabled=0`) — next release
  candidate. Includes safe UnX-style ports (dialog skip, boosters, debug-flag cheats).
- SIN: SinCurseHook (area-transition detection) + SinScaleInject (.bin injector, UNI 1-4).
- Lab hooks (gated off): NovaSuperDamage clamp, RonsoMana, NulWard, GridTeach, Kimahri
  Lancet dual grant, ItemStackCap 255, Double/Triple Drop, ElementHook (Scan Holy/Dark).

### Notes

- Known bugs: see [docs/KNOWN_BUGS.md](docs/KNOWN_BUGS.md) (K-01..K-24).
- All hooks OFF by default; disposable saves required; expect mod incompatibilities.
- FFX Mod Studio (editor/launcher/site) integration is on the roadmap.
