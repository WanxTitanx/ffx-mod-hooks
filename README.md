# ffx-mod-hooks

**English** | [Português (Brasil)](README.pt-BR.md)

<div align="center">

![ffx-hooks logo](assets/logo.png)

**Runtime engine hooks for FINAL FANTASY X HD Remaster (Steam, PC)**

[![Status](https://img.shields.io/badge/status-BETA-red)](#beta-status)
[![Build](https://img.shields.io/badge/build-consolidated%20main-informational)](#current-dll)
[![License](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20x86-lightgrey)](#compatibility)
[![Donate](https://img.shields.io/badge/Donate-PayPal-blue)](https://www.paypal.com/cgi-bin/webscr?cmd=_donations&business=wandersonwpires%40hotmail.com&currency_code=USD)

</div>

`ffx-hooks` is the behavior layer of FFX Mod Studio. It combines native game
menus, optional combat rules, equipment systems, languages, diagnostics and
runtime configuration in one `ffx-hooks.dll`. This inventory describes the
consolidated **FFX** build; it does not claim equivalent FFX-2 support.

- [Current DLL](#current-dll)
- [F8 menus and controls](#f8-dashboard)
- [Combat, equipment and cards](#combat-equipment-and-cards)
- [F7 In-Live](#f7-in-live-status)
- [Other runtime systems](#other-runtime-systems)
- [Configuration and gates](#gates)
- [Build](#build), [deploy](#deploy) and [validation](#testing-rt2)
- [Checked roadmap](docs/ROADMAP.md)
- [Complete Editor integration dossier](docs/ai/EDITOR_INTEGRATION_COMPLETE_HANDOFF_2026_09_28.md)
- [Support the project](#support)

## Beta status

The 2026-09-29 integration joins the published recovery/Seymour/Sphere Grid lane
with main's current elements, Arcana, rewards, languages and weapon effects.
It also adds six learned **Elemental Nul spells**. See the
[integration and asset receipt](docs/ai/NUL_ELEMENTS_INTEGRATION_2026_09_29.md)
for tested source, installation identity and remaining live checks.

Features are opt-in. Every editable boolean in the current F8 catalog defaults
OFF; the dashboard itself defaults ON. A package, compatible executable, restart
or additional gate may be required before an enabled option becomes effective.
`LIVE` in F8 describes how a setting is applied, not a claim of completed RT2.

The consolidated candidate has Windows build, RT0 and isolated RT1 evidence.
Earlier user observations remain attached to their original DLLs. Full gameplay,
visual and save-lifecycle acceptance of this exact candidate is still pending;
deployment alone does not promote it to Production. Known issues are tracked in
[KNOWN_BUGS](docs/KNOWN_BUGS.md) and the [current roadmap](docs/ROADMAP.md).

## Current DLL

Jarvis-HOOK checkpoint: **2026-09-29**, implemented and deployed from `main`.

| Identity | Recorded value |
|---|---|
| Runtime source commit | `2787386a7144af04c16f194029a4a35616bf8dea` (development integration) |
| Installed module | `<game>/modules/ffx-hooks.dll` |
| DLL size | 4,011,008 bytes |
| DLL SHA-256 | `5f6a76f3260a1aa0f5a36e0ff6e9f641f7d92e0dda969910d0d66a2debc3016e` |
| Deployment | 2026-09-29 10:28:24 UTC; verified backup and atomic replacement |
| Source binding | 363 code/resource inputs match the recorded release build |
| Protected installation scope | 1,116 inventoried files unchanged |
| Supported executable | PE32/i386 `FFX.exe`, preferred image base `0x00400000` |
| Executable SHA-256 | `78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced` |

This local deployment includes the six Nuls, private animations and published
recovery/Seymour/Sphere Grid integration alongside the existing main features.
See the [source/build/deployment evidence](docs/ai/NUL_ELEMENTS_INTEGRATION_2026_09_29.md).
The public [v0.6.0-beta.2](https://github.com/WanxTitanx/ffx-mod-hooks/releases/tag/v0.6.0-beta.2)
archives identify the preceding DLL and remain unchanged. The PE resource still
reports `0.2.0.0`; use the source and artifact hashes above for this deployment.
Live RT2 acceptance and Production promotion remain separate.

## What this is

| Component | What it does | Status |
|---|---|---|
| `ffx-hooks.dll` (FfxHooksDll) | Engine hooks, native UI, shared combat/save consumers and the optional systems below | Consolidated beta candidate, deployed |
| F7 In-Live menu | Difficulty, S.I.N. RAM, Force Last Battle, music, Monster AI observer and Arena+ CustomMix | Offline candidate: source/RT0/build and isolated runtime/policy RT1 pass; live machine-callback RT1 and user-run RT2 pending for the Difficulty acceptance matrix |
| F8 Dashboard | Seven tabs, nested settings and 90 canonical boolean controls | 25 LIVE, 61 RESTART REQUIRED, 4 READ ONLY; counts describe activation, not gameplay acceptance |
| F9 Maechen | Native question/answer UI and bounded service client | Earlier transport/UI observations; current-build acceptance and service grounding remain separate |
| `ffx-probe.dll` (FfxDinput8Probe) | Separate main-thread READ / WRITE / CALL probe through the DINPUT8 seam | Optional, default OFF; not included by enabling a Hooks gameplay feature |
| `SinScaleInject` + `SinCoreLib` | Offline S.I.N. research/materialization tools | Legacy disk-writing runtime route remains quarantined; the current S.I.N. RAM route is separate |

## F8 Dashboard

The current tabs are **System, Boosters, Cheats, Extras, Input, Dev, Reforge**.
Keyboard, mouse, wheel and controller navigation share the native menu shell.
Submenus preserve their parent selection; staged edits support confirm/cancel.
Bulk actions retain each control's authority and report unavailable, externally
overridden or failed changes instead of silently claiming success.

| Tab | Boolean identities | Included controls and submenus |
|---|---:|---|
| System | 10 | Borderless window, cursor confinement/idle hiding, performance display, free battle camera, field freeze; four informational module rows; Audio languages and Text languages |
| Boosters | 5 | Permanent Sensor, experimental Playable Seymour, Speed Hack, optional FMV acceleration, Entire Party Earns AP |
| Cheats | 9 | Invincible Party/Enemies, Always Overdrive/Critical, Damage 99999, Always Rare Drop; AP/Gil Multipliers submenu |
| Extras | 38 | Additional mods: Elemental Core/Tactics/Gravity/Magic BDL, Spira Reforge, Aeon Ascension and optional Holy/Shadow weapon effects; separate Vanguard Combat Engine hierarchy with 31 controls |
| Input | 4 | Windows-key blocking, background-input fix, IME filter, Dialog Skip; keyboard/gamepad shortcuts and controller/button mapping |
| Dev | 6 | FieldScout submenu (Master/Heavy/Max/Ultra), Fastload Autosave, Arcana full-deck development option; Equipment Workshop development settings |
| Reforge | 17 | Arcana, Nova Super Damage, Ronso Mana, Equipment Workshop/native equipment details, Scan settings, Grid Teach, Lancet Dual Grant, item-stack cap, Double/Triple Drop, Arena+ submenu |

The counts include controls moved into submenus. Navigation rows, colors,
language selectors, mappings and numeric editors are additional settings, not
additional boolean flags. The [complete 89-entry catalog](docs/ROADMAP.md#complete-f8-control-inventory)
records every label, canonical key, default and activation class.

### AP/Gil Multipliers

`F8 > Cheats > AP/Gil Multipliers` provides independent general AP/Gil controls
(1–100, default configured rate 100 when enabled) and **Per-monster AP/Gil**.
The per-monster hook defaults OFF and requires a restart for initial installation.

Choose a named monster or enter its file ID (`m000`–`m4095`), then set independent
AP and Gil factors from **1 to 1000**, neutral at 1. The AP setting covers the
normal or Overkill value chosen by the game. All instances of that monster ID
share the setting. Previews show the original value, the individual result and
the configured total; missing base data and saturation are explicit.

```text
base reward -> monster multiplier -> general mod multiplier -> vanilla bonuses
```

Calculation uses wide intermediates after the native 16-bit reward read, so the
result can exceed **65,535** without changing monster-file WORDs. Before vanilla
bonuses, the safe limits are AP **382,494,549** and Gil **573,741,824**; this is not
an unlimited-reward patch. The runtime uses the actual reward view, including an
admitted upstream S.I.N. view; a disk preview cannot predict every live modifier.

Individual rates persist atomically in `monster-rewards-v1.tsv` beside the active
Hooks INI. External file edits require restart. Item-drop multipliers are a
separate system. See the [format and native consumer evidence](docs/research/F8_F7_ELEMENTS_MONSTER_REWARDS_2026_09_28.md).

### Ten elements in F7 and Scan

Both interfaces use the same element identities: **eight native bits plus two
Hook-only slots**. Fire, Ice, Thunder, Water, Holy, Darkness and both native Custom
bits remain distinct; external elements use stable keys rather than invented
bits in the game's native BYTE mask. Core now includes **Poison** and
**Gravity** when no external pack is selected or present. Enable Core and
restart to register them. An explicitly selected missing/invalid pack still
reports an error; it is never silently replaced. Existing valid packs retain
all bindings and gain any missing unused external slot.

`F8 > Reforge > Scan settings` includes expanded stats/MP, native extra-element
colors, both external colors, and per-element visibility. F7 weak/resist/absorb
controls cover all ten slots. External gameplay affinities publish only after a
successful Difficulty Apply/Restore transaction; Save alone does not apply them.
Scan visibility and colors change presentation only.

`F8 > Reforge > Scan settings > Element names` edits **Holy, Darkness, Earth,
Wind, Poison and Gravity**. Use keyboard entry or the controller-friendly
character picker, then Save; Cancel/focus loss preserves the prior name. Reset
inherits the canonical label. Names accept up to 32 supported ASCII characters;
empty, duplicate and unsafe names are rejected.

Valid saved aliases take priority over the four newly named defaults; a colliding
default keeps its previous numbered Custom label. Aliases follow native bits or stable external keys and update the hook's F7,
Scan, color, visibility and order labels. **Item/equipment attributes, ability
and spell names, status names and native data remain unchanged.** Affinity and
color preferences survive renaming. Core/Tactics plus Scan Extra Elements now
turn on the numerical element view by default, without a second hidden option;
an explicit `elemental.numeric_scan` preference still takes precedence.

New external elements start neutral and unassigned. Authored command/profile/
equipment bindings are still needed to make an attack use a new element; naming
an element does not silently convert any native attack.


## Combat, equipment and cards

### Vanguard Combat Engine — MOD-002

All **31** independently gated rules have native consumers, organized under
Damage, Magic, Status, Formation, Weapons, Armor, Equipment and Mapping:

- Universal stat percentages, effective-HP defense, healing through Shell,
  additive Armor/Mental Break, action-end Auto-Crit/MP0 consumption,
  opposite-element weakness and configurable single/multi-hit scaling.
- Status-duration refresh, enemy duration resistance, once-per-incarnation
  Threaten and guaranteed-hit policy.
- Quickcast, White Magic in Double/Quickcast and current-MP magic scaling.
- Turn-cost party switching and Eject/Shatter auto-reinforcement.
- Thirteen equipped abilities: Hero's Bravery, Energy Boost, Energy Burst,
  Efficiency, Vampirism, Follow Up, P-Trade, M-Trade, Hero's Caution, MP Regen,
  Elude, Energy Wall and Energy Barrier.
- Equipped active-command bindings and partial Overdrive fees, with native
  display/selection/debit consumers and configurable ability mappings.

Default custom ability IDs are 135–147, separate from Spira/Aeon IDs 148–174.
The [Editor dossier](docs/ai/EDITOR_INTEGRATION_COMPLETE_HANDOFF_2026_09_28.md)
details IDs, payloads, owners, command encoding and validation requirements.

### Elemental Nul spells

`F8 > Extras > Additional mods > Elemental Nul spells` enables the independent
`elemental.nul_spells` gate (OFF by default; restart required). The authored
commands cost 2 MP, target the party and belong to Yuna's White Magic+ menu:

| Command | ID | Private animation | Protected identity |
|---|---:|---:|---|
| NulHoly | 320 | 870 | Holy `0x10` |
| NulShadow | 321 | 871 | Darkness `0x80` |
| NulEarth | 370 | 872 | Earth `0x20` |
| NulWind | 371 | 873 | Wind `0x40` |
| NulPoison | 372 | 874 | `hook.custom03` / `spira.poison` |
| NulGravity | 373 | 875 | `hook.custom04` / `spira.gravity` |

Each spell grants one private protection charge per affected ally. Mixed attacks
require coverage of every participating element; incomplete coverage spends no
custom charge. Native Nuls, status timers and item ability names are preserved.
Poison means the elemental identity, not immunity to the Poison status.

Commands must be learned through authored Sphere Grid nodes. The feature starts
the save-bound learning infrastructure; it never grants commands or changes a
personal save by itself. The White Magic+ root is derived from learned children.
Renaming an element in F8 changes display aliases, not these canonical spell IDs
or names. All six animation DLLs and texture trees are private clones, with no
vanilla replacement. Authoring/install tools require local game assets; proprietary
DLLs/textures are not distributed as source. Live visual/battle acceptance remains
pending. See the [asset recipe and rollback contract](tools/nul_elements/README.md).

### Elemental Dominion — MOD-007

Four independent gates expose Core, Tactics, Gravity and Magic Break Damage
Limit. The admitted `ffx.mod007.elements.v1` pack supplies element definitions,
command bindings, actor profiles and equipment deltas. Native consumers cover:

- Eight native element identities and stable external identities; percentage
  affinities, mixed-element policies and numerical Scan.
- Imperil, Ward and Nul interactions, action timers and consumption/lifecycle
  rules shared with the existing combat owners.
- Profile-gated Gravity, including the defined nonlethal boss HP fraction.
- Magic damage up to 999,999 HP per hit when the relevant gate/cap applies.
- Fingerprinted monster profiles and equipment affinity deltas, including
  owner/type/SOS admission and retirement.

The registry supports more identities than the ten slots exposed by the current
F7/Scan settings UI; these are different limits. A declared capability does not
make every planned presentation consumer available. Pack details and limits are
in the [complete integration contract](docs/ai/EDITOR_INTEGRATION_COMPLETE_HANDOFF_2026_09_28.md).

### Spira Reforge and paid Aeon Ascension

The shared catalog reserves **27 identities, IDs 148–174**. Implemented Spira
effects include Mana Spring, Break Limits, Devil's Bargain, Auron's Warden's Oath,
Double/Triple Drop, Element Eater, HP/MP percentages, all-stat percentages,
STR/MAG and DEF/MDEF combinations. Drop factors use the party maximum, not a
product across equipped copies, and do not multiply AP/Gil.

**Arcane Focus, Spell Spring, Foolstrike and Fooltouch remain inactive pending
definitions.** Fourstrike/Fourtouch have their defined native four-element base;
unspecified extra behavior is not implemented. Catalog presence is not a claim
that every reserved effect is playable.

Aeon Ascension adds paid equipment upgrades for acquired canonical Aeons:
armor HP up to **999,999** and MP up to **9,999**, or weapon damage up to
**999,999**. Authorization requires the correct piece/owner and a persisted paid
receipt, not just an ability WORD. Purchases consume Gil/materials, preserve
immunity and do not refill current HP/MP. Removal does not refund the purchase.

### Equipment Workshop — MOD-004/005

The native Workshop includes a logical fifth ability, refinement modes A/B,
generic refinement bonuses, expansion/fusion, recipe/economy previews, native
equipment comparison/details, navigation/audio fixes and Aeon upgrades. Dev
settings expose explicit cost/progression overrides without turning a paid
Ascension receipt into a free unlock.

Native equipment remains **22 bytes with four ability WORDs**. The fifth slot,
ranks and paid authorizations live in versioned sidecars. Transaction journals,
save fingerprints, checkpoint recovery and native-save projection prevent
custom metadata from being written into the next native equipment record.
See [Workshop economy](docs/WORKSHOP_ECONOMY.md) and the
[Editor/save contract](docs/ai/EDITOR_INTEGRATION_COMPLETE_HANDOFF_2026_09_28.md).

### Spira: Arcana Fayth — MOD-008

Arcana adds **78 cards**, collection/acquisition, seven character loadouts and
up to three card slots, Twin/Constellation modes, combat consumers, native
**Main Menu > Equip** integration, Status/Auto-Abilities presentation, artwork,
menu audio and persistent collection/loadouts. Temporary stat projection is
composed with the shared save layer. The full-deck Dev option is independently
gated and defaults OFF, as does Arcana itself.

The catalog is compiled into the DLL; concept JSON is not a dynamic gameplay
pack contract. Arcana works without Spira Reforge. Use the
[Arcana installation and acquisition guide](research/mod_008_arcana/README.md)
for required artwork and package contents.

The September 29 source candidate adds bonuses without removing existing ones:
The Moon gains **Shadowstrike/Shadow Ward**, The Empress **Earthstrike/Earth Ward**,
The Chariot **Aerostrike/Wind Ward**, Death **Biostrike/Poison Ward**, and The Hanged
Man **Gravitystrike/Gravity Ward**. The Sun retains Holystrike/Holy Ward.
Shadow is Darkness, not blindness; Bio uses the Poison element, not its status.
Poison/Gravity require Elemental Dominion **Core ON** and Arcana. Their strikes
retain normal weapon damage: Gravitystrike does not cast Demi. External card
Wards halve positive exposure and preserve immunity, absorption and locked profiles.
Holy/Shadow particles use the separate Weapon Strike VFX gate. Card capacity is
now ten effects; save/card identities and custom names remain compatible.
See the [integration contract](docs/ai/ARCANA_ELEMENTAL_STRIKES_2026_09_29.md).
These source additions are separate from the older deployment identity above.

### Text languages — MOD-006

`F8 > System > Text languages` selects native text or a separate compatible PT-BR
pack, with restart required. The runtime supports admitted menu/battle/event/
scene-subtitle resources, font metrics/atlas/shadows, exact source hashes,
capacity/line-width validation and native fallback. API/schema 2 retain the
documented older-version compatibility.

The DLL does not itself contain a complete translation or a new dub. Independent
voice, battle-sound and movie-audio selectors are under **Audio languages**.
See the [MOD-006 Editor handoff](docs/ai/MOD006_EDITOR_HANDOFF.md) for exporting a
usable text-and-font package rather than just a language toggle.

## F7 In-Live status

- **Difficulty:** RAM-only maximum/current HP/MP, Overkill, STR/DEF/MAG/MDF/AGI/
  LCK/EVA/ACC scaling with immutable baselines, transactional ratios and owned
  restoration. Element and status controls have exact field/width evidence and
  isolated tests; current-build live acceptance remains pending. Native
  weak/resist/absorb and two keyed external affinities share the Apply boundary.
- **Presets:** Off, Hunter, Sombra de Sin and True Nightmare; explicit Apply Now,
  Save and Back. Per-area replacement configuration exists; its native editor is
  still a roadmap item. Status-resistance configuration is also JSON-based.
- **S.I.N. RAM:** seeded/visit-based natural-encounter selection, admitted curse
  scripts, bounded names, shared Difficulty stat scaling and Threat reward
  views for the reviewed Macalania pilot. The legacy disk materializer is not
  launched. Gameplay confirmation of the repaired curse targeting remains open.
- **Force Last Battle:** main-thread encounter requeue with bounded repetitions;
  complete lifecycle/adversarial acceptance remains pending.
- **Music:** track lock, battle-entry override, playlist randomizer and fade
  controls. Full playlist editing and the complete track-name crosswalk remain
  pending.
- **Monster AI observer:** default-OFF, profile-gated, read-only registration,
  cleanup and dispatch telemetry. General AI-swap mutation remains blocked.
- **Arena+ / CustomMix:** native catalogs/browser, progression bypass, saved mix
  library, actor/battlefield selection and bounded RAM-only Ultra formation
  requests. Legacy disk-backed composition remains a separate quarantined route.

[F7 contracts and executable evidence](docs/F7_INLIVE.md) distinguish each
consumer, field width, lifecycle and evidence level.

## Other runtime systems

| System | Included behavior and boundary |
|---|---|
| Speed Hack | Configurable Ctrl+Shift+K default cycle 1x/2x/4x/8x; native 2x/4x and custom field-service 8x; effective/paused/conflict display. F12 remains available for screenshots. |
| FMV acceleration | Separate default-OFF restart gate; movie picture and its owned FMOD audio channel follow the requested speed. Pitch rises with speed; complete playback/subtitle/transition acceptance remains pending. |
| Native ports/input | Window/cursor/performance/camera controls, keyboard and gamepad bindings, XInput/Steam Input selection and digital-button remapping. A host compositor can still intercept the Windows/Super key. |
| Dialog Skip | Independently gated native voice consumer, composed with Speed Hack rather than a second owner of the same seam. |
| Fastload Autosave | Startup autosave loading and native load/save-lifecycle integration; prior player confirmation is recorded for the earlier artifact, not blanket acceptance of this DLL. |
| FieldScout | Master/Heavy/Max/Ultra diagnostics and capture; all four controls are now in Dev > FieldScout. |
| Maechen F9 | Bounded native question/answer modal and service transport; client implementation does not prove service answer quality or protocol-v2 local-context support. |
| Ronso/Nova | Optional Ronso Mana pool, native command-cost/save integration and Nova Super Damage gate. |
| Nul/teaching | Shared Nul composition plus legacy NulWard/teaching controls, Grid Teach and Lancet Dual Grant. Separate experimental writeback modes retain their own gates and conflicts. |
| Inventory/rewards | Configurable item-stack cap up to 255; Double/Triple Drop consumers distinct from AP/Gil; runtime ownership and restoration are family-specific. |
| Aurora/lab diagnostics | Default-OFF actor/detail diagnostics; Aurora uses Ctrl+Alt+F9/F10, not plain F9/F10. Disabled or retired experiments are recorded in the roadmap. |
| Shared infrastructure | Composed damage/turn/Nul/action consumers, hook-batch coordination, native presentation and multi-observer save events; paid checkpoint projection preserves Arcana/Ronso ownership. |

## Roadmap

The [roadmap](docs/ROADMAP.md) keeps completed checkboxes and separates delivered
source/deployment work from pending RT2, Editor authoring and release work.
Current priorities are exact-candidate gameplay acceptance, the Editor exporters
and dependency validation, remaining Spira definitions, and unresolved F7/probe
work. The [complete Editor dossier](docs/ai/EDITOR_INTEGRATION_COMPLETE_HANDOFF_2026_09_28.md)
contains all 40 custom ability identities, 31 Vanguard rules, 89 F8 controls,
78 Arcana cards, formats/dependencies and 62 concrete Editor tasks. It is a
handoff, not a claim that those Editor tasks are already implemented.

## Architecture

```text
FFX.exe (x86)
  dinput8.dll -> FF10 module loader
    modules/ff10-file-loader.dll -> optional external data/packages
    modules/ffx-hooks.dll -> native menus + gated runtime systems
    modules/ffx-probe.dll -> optional, separately armed diagnostics

FFX Mod Studio / Launcher
  authoring + package/install front ends
  versioned manifests, sidecars and contracts (integration status in roadmap)
```

See [architecture](docs/ARCHITECTURE.md) and the
[current Editor contract](docs/ai/EDITOR_INTEGRATION_COMPLETE_HANDOFF_2026_09_28.md).

## Repo layout

```text
contracts/       Hooks/probe MMF and interoperability contracts
src/runtime/    FfxHooksDll, FfxDinput8Probe, NativeMenuShell, BattlePhotoMode
src/sin/        Offline SinCoreLib / SinScaleInject
docs/           Runtime guides, checked roadmap, evidence, Editor handoffs
research/       Mod catalogs, packaging inputs and research
assets/         Logo and licensed/recorded mod artwork
tools/          Context, package, runtime-check and export tooling
```

## Gates

Every editable F8 boolean defaults OFF; `[dashboard] enabled=1` controls menu
availability only. Configuration load failure keeps the dashboard OFF.
Compatibility precedence is fail-closed, highest first:

```text
true disable env > local/global off flags > positive env (true or false) >
authority marker + canonical INI > legacy INI > positive flags in
modules, config, modules\config, then root > unmarked canonical (only without an authority key) > default
```

| Family | Primary control |
|---|---|
| F7 | `modules\config\f7_inlive.flag` or `FFXHOOKS_ENABLE_F7=1`; family-specific admission still applies |
| F7 AI observer | `f7_aiswap.flag` / `FFXHOOKS_ENABLE_F7_AISWAP=1`, observe-only, restart required |
| F8 | `[dashboard] enabled`; per-control keys/defaults in the complete roadmap catalog |
| Vanguard | Independent `vanguard.*` gates and `vanguard_ids.*` / `vanguard_commands.*` mappings |
| Elemental | `elemental.core`, `.tactics`, `.gravity`, `.magic_bdl`, `.nul_spells`; admitted `elemental.pack` |
| Spira / Aeon | `spira.enabled`, `aeon_ascension.enabled`; identity/payload/owner and paid-receipt checks |
| Arcana | `arcana.enabled`; separate `development.arcana_full_deck` development option |
| Per-monster AP/Gil | `cheats.monster_rewards`, F8 authority and `monster-rewards-v1.tsv` |
| Dialog Skip / FMV | `input.dialog_skip` / `boosters.speed_hack_fmv` |
| Lab families | Independent flags/settings; enabling the dashboard does not arm them |

F8 preserves dominating external OFF markers. A saved ON value, an installed
detour and an effective runtime operation are different states.

## Build

Prerequisites: Windows, Visual Studio 2022 (Desktop C++ workload), PowerShell,
[vcpkg](https://github.com/microsoft/vcpkg), .NET 8 SDK (for the SIN injector).

```powershell
# One-time: vcpkg static deps (x86)
vcpkg install --triplet x86-windows-static polyhook2 zydis minhook

# Build the hooks DLL (PolyHook build, Release)
.\src\runtime\FfxHooksDll\build_hooks.ps1 -WithPolyHook -Release

# Build the probe
.\src\runtime\FfxDinput8Probe\build.ps1

# Build the SIN injector
dotnet build src\sin\SinScaleInject\SinScaleInject.csproj -c Release
```

Notes:
- The Visual Studio IDE build of `FfxHooksDll.vcxproj` is the reference (0 errors); the CLI
  script is the deploy path. A new hook must be registered in **both**.
- The PolyHook lab artifact `ffx-hooks-polyhook-lab.dll` must **never** be left in the game
  directory — it crashes the menu. `build_hooks.ps1 -Deploy` refuses lab deploys without
  `-LabDeploy -GameRoot <disposable copy>`.

CI runs one native build for relevant source changes, skips ordinary documentation,
and keeps full context matrices and large Arcana packages manual. See the
[CI execution and cost controls](.github/CI.md).

## Deploy

```powershell
# Lab deploy (disposable game copy — NEVER the installed game)
.\src\runtime\FfxHooksDll\build_hooks.ps1 -WithPolyHook -Release -Deploy -LabDeploy -GameRoot D:\path\to\game-copy

# Arm a feature (game closed)
New-Item -ItemType File "<game>\modules\config\f7_inlive.flag"
```

Rules: close FFX before deploying, always back up the previous DLL (named backups),
deploy to every target you own, verify `%TEMP%\ffx-hooks.log` after boot.

## Testing (RT2)

In-game behavior is validated by a reproducible protocol: game closed, disposable save,
hashes before/after, log as evidence. See [docs/RT2_PROTOCOL.md](docs/RT2_PROTOCOL.md).
The editor (FFX Mod Studio) must be closed during hook RT2 sessions.
`run_f8_rt2.ps1` is a manual Preflight/Verify gate: it never launches or stops either process,
copies/deploys a DLL, or waits for boot. It produces an integrity-bounded manifest and verifies
only the selected case after the human-run session. No F8 case is promoted by that script alone.

## Compatibility

- The current engine integrations target the exact supported FFX executable.
  Signature/profile guarantees apply to the reviewed sites, not every future
  game build or other mod's patches.
- Other DINPUT8/GetDeviceState owners, proxy loaders, Special K or UnX can
  conflict with the same seams. The native ports here do not require loading
  UnX or Special K alongside Hooks.
- Custom field speed, optional FMV speed, Dialog Skip and shared combat/save
  hooks have separate ownership/admission paths. FMV audio speed changes pitch;
  it is not pitch-preserving time stretching.
- Playable Seymour covers the experimental battle roster, not Sphere Grid
  support. General AI swap and the legacy S.I.N. disk writer remain unavailable.
- Dynamic `FreeLibrary`/hot unload is unsupported. `DLL_PROCESS_DETACH` only
  closes admission with non-blocking stop requests; full teardown runs outside
  loader lock.
- Use disposable saves for RT2. Follow the family-specific protocol, retain
  hashes/logs and verify restoration before making a Production claim.

## Versioning

Independent SemVer, starting at `0.1.0-beta.1`. This project will stay below `1.0.0` while in
beta. `MAJOR.MINOR.PATCH` with pre-release suffix; REVISION bumps are doc-only. See
[CHANGELOG.md](CHANGELOG.md).

## Credits

This project builds on open source and community work — full list in [NOTICE](NOTICE):
PolyHook2 (stevemk14ebr), MinHook (TsudaKageyu), Zydis/Zycore (zyantific), asmjit/asmtk,
Xe.BinaryMapper (Xeeynamo), the FFX module loader / DINPUT8 proxy concept (ffgriever).
The DINPUT8 main-thread seam (`GetDeviceState` vtable hook) was discovered and proven
in-house (see docs). ATEL/monster codecs in SinCoreLib were extracted from the
FFX Mod Studio editor codebase (our own code).

## Support

🇧🇷 This project is made by a Brazilian developer. If it helps you, consider
supporting its development:

[![Donate](https://img.shields.io/badge/Donate-PayPal-blue)](https://www.paypal.com/cgi-bin/webscr?cmd=_donations&business=wandersonwpires%40hotmail.com&currency_code=USD)

**Any amount helps.** Even US$ 2 contributes to maintenance. FFX Mod Studio is
bigger than the hooks: it includes the Editor, Launcher, download website and
other tools. Your support helps keep that work going. Donations are optional.

## License

GPL-3.0 — see [LICENSE](LICENSE). Game assets and `FFX.exe` are NOT included and remain property
of Square Enix. This project is a fan-made tooling layer.
