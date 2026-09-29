# MOD-008 — Spira: Arcana of the Fayth

Jarvis-HOOK. Native runtime candidate implemented on the current Hooks base; RT0/RT1 verified, RT2 pending; author review only. See [runtime contracts and validation](../../docs/research/MOD_008_RUNTIME_2026-09-28.md).

- [Design and settled decisions](<../../docs/mod-ideas/MOD 008 - ARCANA OF THE FAYTH.md>)
- [All 78 cards and current effects](<../../docs/mod-ideas/MOD 008 - CATALOGO DAS 78 CARTAS.md>)
- [Local gallery](../../assets/mod-008-arcana/gallery.html)
- [Native integration plan](../../docs/research/MOD_008_IMPLEMENTATION_PLAN_2026-09-27.md)
- [Recovery and delivery review](../../docs/research/MOD_008_DELIVERY_REVIEW_2026-09-27.md)

The active art set is selected by `cards.proposed.json` and `assets-manifest.json`:
78 individual cards, the original frame, a card back and a transparent icon.
The native preview decodes these selected PNG masters with WIC and a bounded D3D11 cache. Generation receipts preserve
their origins. Nine of Cups uses `044-cups-nine-v2.png`; its original extra-cup
concept remains local and is excluded from version control.

## Reproduce offline checks

Install the optional authoring/check dependencies with
`python3 -m pip install -r research/mod_008_arcana/requirements-dev.txt`.
The runtime ZIP builder uses only Python's standard library and Git.

Run from the repository root with Python 3, Pillow and Node.js available:

```sh
python3 research/mod_008_arcana/probe_contracts.py
python3 research/mod_008_arcana/verify_delivery.py
python3 research/mod_008_arcana/run_checks.py
python3 research/mod_008_arcana/run_checks.py --sanitize
```

`probe_contracts.py` rewrites its deterministic model report. `verify_delivery.py`
only reads files and checks JavaScript syntax; it does not open a browser.
Its optional `--local-evidence` also rehashes the exact recorded research inputs,
original generated images, cached Fandom API snapshot and FFX executable. Those
external paths belong to the original research machine and are not distributed.

To rebuild derived catalog/prompts/gallery/manifest after an intentional source
change, run `build_catalog.py`, then `build_artifacts.py`, then the checks above.
Those builders do not generate or edit images. Rebuilding does not prove their
text, symbolism or visual quality; review the selected images separately.

## Native Windows fixtures and packaging

Use `src/runtime/FfxHooksDll/arcana_rt1.ps1 -GameExecutable <private-FFX.exe> -NativeSaveFixture <disposable-native-save> -AssetsRoot <mods/arcana> -CandidateDll <candidate-dll>` with MSVC x86. The CRT fixture requires the exact game `msvcr110.dll` beside the private PE. This maps the PE without running its entrypoint and never launches or deploys the game.

Create a private package with `python3 research/mod_008_arcana/package_runtime.py --dll <candidate-dll> --output <candidate.zip>`. It includes the DLL, all 78 selected cards, icon/back, OFF examples, effect/acquisition references, licenses and SHA-256 identities. The original frame master remains in the art delivery.

## Standalone installation and repository completeness

MOD-008 is an FFX Hooks module. It does not require Spira Reforge, its launcher,
its data packs, FFX-2, or an online card download. The name and artwork retain the
Spira: Arcana of the Fayth identity. Every Arcana runtime dependency is tracked
in this repository: native code and compiled catalog under
`src/runtime/FfxHooksDll/`, the 78 PNGs and shared art under
`assets/mod-008-arcana/`, and acquisition rules, source catalogs, generators,
package scripts and license notices under `research/mod_008_arcana/`.

The normal FFX Hooks loader and a supported copy of FFX are still required.
The game executable and private save fixtures are not redistributable module
dependencies. A developer may supply them for optional signature regeneration
and native RT1 checks. Historical research receipts contain external paths only
as provenance; runtime, packaging and default offline checks do not read them.

1. Build the DLL using the repository's normal Windows build, or obtain the
   `ffx-hooks-arcana` artifact from the repository's **build** Actions workflow.
   That workflow creates the complete ZIP alongside the existing DLL artifact.
2. With FFX closed, extract `ffx-hooks.dll` and `mods/` from the ZIP into the
   game's `modules/` folder. The art must land in `modules/mods/arcana/`.
3. Merge `Arcana-settings.ini.example` into `_isolated/ffx-hooks.ini` while
   preserving other settings. Both options ship OFF. Set `[arcana] enabled=1`
   and restart FFX to use **Main Menu > Equip > Tarot**.
4. Normal play awards cards from the documented story/challenge milestones.
   **F8 > Dev > Arcana: full deck** grants all 78 immediately. Disabling this
   development option keeps the collection. Preserve each save's Arcana
   sidecars when copying saves.

Balance v4 recognizes the three pinned previous catalogs, retains the 78 card
identities and equipped slots, and writes the upgraded extension on the next
normal native save. Unknown catalog hashes remain rejected. The full validation
and remaining user test cases are in
[the menu audio, Status and MP record](../../docs/research/MOD_008_AUDIO_STATUS_2026-09-28.md).

**Status > Auto-Abilities** shows the equipped character's Arcana effects below
the native equipment list. Shared effects from cards are aggregated; conditional
bonuses and their limits are named. This is a read-only view and does not create
native equipment abilities or require Workshop. The Fool now grants First
Strike and 35% less recovery after the first eligible action, retaining Evasion
+20 and Sensor. Existing saves retain their collection and loadouts.

Arcana inputs use the game's move/confirm, cancel and error sounds. Repeatable
MP recovery is now 3% or 1% on Defend and 1% per turn; the three kill-triggered
MP rewards remain 10%. The Status effects use the native ability font and the
original Auto-Abilities caption artwork, with an Arcana label alongside it.
The main Status overview also shows the equipped Tarot slots side by side in
the existing gap below Armor, including Empty/Locked states.

[Acquisition rules](acquisition-v1.md) implement story/sidequest/challenge reconciliation. The separate Development option grants all cards immediately and is OFF by default. Native Equip contains the picker; it is not an F7 equipment replacement.

The dated September 27 reports describe concept/research evidence. `cards.proposed.json` intentionally remains a design source, not a runtime parser contract. The compiled typed catalog and [runtime validation record](runtime-validation.json) describe this candidate. No independent review or live visual/gameplay result is claimed; source integration follows the user's later explicit PR/merge instruction. RT2 and Production promotion remain separate.


## Elemental major-card additions (2026-09-29 source candidate)

| Major card | Added effects |
| --- | --- |
| III — The Empress | Earthstrike, Earth Ward |
| VII — The Chariot | Aerostrike, Wind Ward |
| XII — The Hanged Man | Gravitystrike, Gravity Ward |
| XIII — Death | Biostrike, Poison Ward |
| XVIII — The Moon | Shadowstrike, Shadow Ward |
| XIX — The Sun | Existing Holystrike and Holy Ward retained |

Every previous bonus is retained. Cards now have a bounded ten-effect capacity;
IDs, acquisition conditions, collection/loadouts and the stable save identity
remain unchanged. Poison and Gravity need Elemental Dominion Core. Gravitystrike
uses regular weapon damage, not fractional HP. Poison is an elemental affinity,
not a poison-status proc. External Wards halve positive resolved exposure;
locked profiles, immunity and absorption remain authoritative. The two extra
strikes use the same F7/Scan affinities and mixed-element resolver as authored
commands, without expanding native item/weapon fields. Holy/Shadow visuals are
controlled by the independent Weapon Strike VFX setting. This source candidate
requires its matching DLL; older packaged releases do not acquire it from JSON.

[Implementation, validation and integration ledger](../../docs/ai/ARCANA_ELEMENTAL_STRIKES_2026_09_29.md).
