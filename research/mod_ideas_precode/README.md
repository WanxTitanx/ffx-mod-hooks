# MOD-001..004 precode packet — Jarvis-HOOK

Start with docs/research/MOD_IDEAS_PRECODE_REVALIDATION_2026-09-23.md for MOD-001..004, docs/research/MOD_005_FIFTH_EQUIPMENT_ABILITY_SLOT_2026-09-23.md for MOD-005, docs/research/MOD_004_EQUIPMENT_REFINEMENT_2026-09-23.md for refinement, and docs/research/MOD_006_ADDITIONAL_TEXT_LANGUAGE_2026-09-27.md for the new language idea. Historical backlog SHA-256 values were 2c56bf4813bb13b11007d3ee67fcd51da976cb6522dbfa90e880d09c21c39044 (Luna), a20d679996c4cf68a60d64bd76f9ed6bc8a02bdcdc519a48e659090cff68a276 (after MOD-005) and f71f396b351b91342ad3781e7f75e30062820466e5ade37a5c985541acc3ec9f (refinement). Current docs/MOD_IDEAS_BACKLOG.md is pinned at SHA-256 241a87e5fe3dbc3c414ca4024134cdcd9171c35abbe285cd3edcb07bb3f24ef4. feature_ledger.tsv provides one row per current checkbox. Source binaries and fixtures are not part of this branch.

Current MOD-007 worktree: `/home/wanderson/.codex/worktrees/mod-007-elemental-dominion/ffx-hooks`, branch `codex/mod-007-elemental-dominion-20260927`. See `docs/research/MOD_007_ELEMENT_REGISTRY_FEASIBILITY_2026-09-27.md` for elements beyond the native byte. Runtime references there are pinned to current main separately from this older documentation base.

Current Aeon addendum: `/home/wanderson/.codex/worktrees/aeon-exclusive-breaks/ffx-hooks`, branch `codex/aeon-exclusive-breaks-plan-20260927`, based on documentation commit `39ceb198`. See `docs/mod-ideas/AEON ASCENSION - MOD 002 004 005.md`; recipes are proposals, native ingredient facts were read from kaizou.bin, and no new effect/ID was installed.

Current named Spira/Aeon data creation: `research/autoability_expansion/registry.json`,40 IDs135..174;27 new rows applied locally. Report: `docs/research/AUTOABILITY_SPIRA_AEON_CREATION_2026-09-27.md`. Same documentary worktree/branch as the Aeon addendum; no runtime implementation or binary publication.

## Ledger columns

- id and source_line: stable ordinal and line in the pinned current backlog; source lines may shift when ideas are added.
- route: DATA uses an existing file field; HOOK needs new runtime logic; DATA_HOOK needs both; ATEL_OR_HOOK is an event-script candidate; UI_HOOK needs a renderer/menu path; ASSET_GATE needs model/animation proof; DECISION and RESEARCH are pre-implementation work; TEMPLATE is not an idea.
- evidence: short source/probe code, joined by plus when more than one source supports a row.
- confidence: HIGH for exact bytes/source plus a reproducible probe, MEDIUM for source with untested consumers, LOW for a concept without a mapped implementation.
- verdict: RT0_PASS is a file-format/data probe; MODEL_PASS and MODEL_PARTIAL are pure-rule checks; SOURCE_ONLY is inspected code; STATIC_PASS is a completed classification; CORRECTED records a Luna interpretation changed by the source; BLOCKED_SAVE is the reproduced Editor writer defect; NEEDS_SPEC/NEEDS_RE and META are literal.
- next_gate: the concrete work needed before a gameplay claim. The codes are explained below.

## Evidence keys

| Key | Source or probe |
|---|---|
| AEON7 | probes/aeon_elements/AeonElementProbe.cs: seven Aeon element edits, OD cost and owner field in-memory. |
| ATEL_REF | lawhsia_ffx-customizable-battle-tweaks/apply_buffs.py lines 191–211, 366–380: battle-start OD reset by ATEL. Read only; no license for adaptation found. |
| BACKLOG, FORUM | User-pasted forum text, including the attachment in /home/wanderson/.codex/attachments/505444d3-58a5-4fb0-82f6-02619866f090/Texto colado.txt. |
| EDITOR_CMD | FFX Editor Ability_Command.cs, command/item/monmagic2 round-trip tests. |
| EDITOR_GEAR | FFX Editor WeaponGear_File.cs and WeaponGearRoundTripTests.cs; synthetic weapon.bin only. |
| EDITOR_MIX | FFX Editor MixTable_File.cs and MixTableEditor_DataModel.cs; writer source, no local prepare.bin fixture. |
| EDITOR_TEXT | FFX Editor text authoring source; not exercised in this packet. |
| FANTASIA | Fantasia src/balance/elemental_affinities.cs at commit 64b03ae..., MIT; see THIRD_PARTY_NOTICE.md. |
| HOOKS_KIM | Existing GridTeachHook.cpp, RonsoManaHook.cpp, KimahriLancetDualGrantHook.cpp in Hooks. No new OD was installed. |
| LEDGER | generate_ledger.py verifies 97/97 checkbox coverage (two template rows). |
| SPIRA_AA_SNAPSHOT | `research/spira_autoabilities/catalog_snapshot.json`: historical designs versus pre-creation JP/US files and Drop consumer. |
| AA_EXPANSION | `research/autoability_expansion/application_report.json`: 27 new rows148..174 across13 ability/12 rate tables; native/marker/neutral modes explicit, no gameplay proof. |
| AEON_BREAK_RECIPE | `research/aeon_exclusive_breaks/evidence.json`: native BHP/BMP/BDL ingredient rows and proposed-recipe identity/arithmetic audit. |
| AEON_WORKSHOP | Main f2308ddd: `IsAeon`, `AeonAccess`, double Gil policy, immutable 0x807B and canonical gear checks. |
| AEON_CAP_PE | Pinned PE: field HP/MP caps, battle Double HP/MP, signed clamp helper and damage clamp; static observations only. |
| PE_MOD007 | `research/mod_007_elements/probe_native.py`: original affinity routine, eight bits and ignored high bits; 403809 RT1 comparisons, not a new Hook. |
| ELEMENT_SOURCE | Current runtime Scan/Difficulty/Workshop source and Editor u8 serializers; hashes in `research/mod_007_elements/source_inventory.json`. |
| MODEL_ELEMENTS | `research/mod_007_elements/validate_design.py`: proposed registry/affinity/Gravity rules only; no runtime adapter. |
| PE_MOD005 | Snapshot IDA RE of FFX.exe SHA-256 78ce3439...: fixed four-ability consumers and 22-byte inventory operations; detailed addresses in the MOD-005 report. |
| LOCALE_RE | Editor docs/reverse/FFX_MISLABEL_AUDIT_R2_2026-09-16.md: current locale id from sLanguageManager+4 and save mismatch caller, pinned PE. |
| ENCODING_CORPUS | Steam override ffxsjistbl_us.bin SHA-256 9f96e3b9... and Editor FfxEncoding.us.cs: common accents present, ã/õ absent. |
| EDITOR_LOCALE | Editor encoding/text writers and Karifean FFXDataParser localization reference; no new locale writer or game menu integration proved. |
| SAVE_FIXTURE | research/mod_005_fifth_slot/probe_save_overlap.py: pinned PC save, 200 × 22-byte records and fifth-u16 overlap, RT0 only. |
| MODEL_REFINEMENT | research/mod_004_refinement/probe_refinement_design.py: 131 named abilities, 112 item ids, candidate costs and +10/+40/+50 invariants; no runtime effects. |
| IDA_CUSTOMIZE | FFX CustomizeMenu_KaizouStateMachine flat 0x8D5800, one recipe/item debit at 0x8D5BF0 for the pinned PE; see MOD-004 refinement report. |
| LUNA_IDA | IDA observations captured in the preceding Luna audit. Sol checked matching PE bytes without reopening the changing IDB. |
| MODEL_CPP | include/mod_ideas.hpp and src/mod_ideas.cpp, 116 pure C++17 assertions; Linux, sanitizers, MinGW x86 VM and MSVC x86 VM. |
| MONMAGIC | Editor --monmagic-grow-rt0: monmagic2 fixture 247→248 and five preservation checks. |
| PE11 | probes/pe_signature_probe.py: 11 exact RVA byte signatures on FFX.exe SHA-256 78ce3439... |
| PLY_SAVE | FFX Editor PlayerKernel_File.cs, SphereLevelsAvailable +0x3B of a 0x94-byte row; no local ply_save.bin fixture. |
| SAVE_CODE | FFX Editor FfxSaveEquipment.cs/SaveEditor_EquipmentBindings.cs source. |
| SAVE4 | probes/save_layout/Program.cs linked to actual FfxSaveEquipment.cs and a pinned real save fixture: four defect checks. |

## Next-gate catalog

| Gate | Acceptance needed |
|---|---|
| ASTRA_REVIEW | Inspect evidence, diff, assumptions and coverage before selecting implementation work. |
| RT2_FINITE | Define one reversible game case under docs/RT2_PROTOCOL.md and record actual behavior. |
| NO_ACTION | A wording/template item; no mod code follows from this row. |
| DEFINE_SCOPE, DEFINE_PACKAGE | Choose mod goal, ownership, user-facing options and shared versus game-specific patch scope. |
| DEFINE_RULE, DEFINE_CONDITION, DEFINE_MONSTERS | Specify Blue Mage OD behavior, damage conditions, monster classes, ability IDs and decision rules. |
| DEFINE_CAP, DEFINE_FORMULA, FORMULA_ID | Choose caps with command overrides and accumulator/HUD policy; map stable formula IDs and persist them. |
| DEFINE_ELEMENT_PAIR, DEFINE_ELEMENT_BOOST | Specify opposite element pairs, multielemental precedence and which damage/healing is boosted. |
| DEFINE_RANK, DEFINE_FURY, CONFIRM_MP0, MAP_MAGIC_FORMULA | Choose Quickcast floor/ceil/fixed rank, Fury MP/rotation behavior and derive magic formula from original MP-0 wording. |
| DEFINE_STATUS, DEFINE_KO, MAP_STATUS, MAP_DURATION_FIELD, MAP_THREATEN | Define lifecycle, expiration and persistence; locate status writer/field and Threaten branch before patch. |
| DEFINE_ACCURACY, IDENTIFY_ATTACKS, DEFINE_CRIT | Choose hit/crit formulas and enumerate any allegedly broken always-hit commands. |
| DEFINE_COST_ROUNDING, DEFINE_STACKING, DEFINE_DAMAGE_TYPES | Choose integer rounding, additive versus multiplicative effects and classification of fixed/fractional/reflect/healing hits. |
| DEFINE_SLOT_COST, DEFINE_EVOLUTION | Choose Key Sphere schedule and ability replacement recipe. |
| REFINEMENT_EFFECTS_RT1 | Resolve three unscalable abilities and family-specific effects; prove sidecar/menu/inventory lifecycle in isolated harness before gameplay claim. |
| DEFINE_INSTANT, DEFINE_MIX, IDENTIFY_ITEMS | Explain suitcase timing, Mix overhaul recipes/rules and Customize-only item IDs/effects. |
| MAP_EFFECT, MAP_CTB, MAP_TURN_END, MAP_TURN_START, MAP_STAT_CAPS | Locate the real consumers/writers for autoabilities, delay, one-use flags, regen and attribute caps. |
| MAP_SWITCH_CTB, MAP_AP_AWARD, MAP_KILL_EVENT, MAP_COUNTER_EVENT, MAP_GUARD_EVA | Locate battle flow before implementing swap cost, guaranteed AP, Vampirism, Follow Up or Elude. |
| MAP_EQUIP_MENU, MAP_OD_UI, MAP_SUMMON_MENU, MAP_DOUBLECAST | Identify menu, confirmation, gauge and action dispatch contracts; authoring a command row alone does not cover them. |
| MAP_NEW_GAME, MAP_BATTLE_SAVE, FIX_SAVE_LAYOUT | Prove initial state and battle-save behavior; repair and re-test the currently overlapping Editor gear writer first. |
| SIDECAR_RT1 | Review MOD-005 IDA matrix; specify unlock/cost/special gear, then run isolated inventory and save-sidecar lifecycle harness before any runtime hook. |
| MAP_LANGUAGE_MENU_AND_RESOURCES | Trace native menu, locale manager, resource paths, encoding/font, save and voice before choosing virtual pt-BR versus new engine ID. |
| MAP_BATTLE_HUD, MAP_SUMMON_HUD, MAP_CTB_PREVIEW, MAP_ARENA_FLAGS | Find render functions and saved state for participant/status/Arena/Aeon/preview icons. |
| MAP_SPHERE_MONITOR, MAP_BOSS_SCRIPTS, MAP_LIGHTNING_EVENT, MAP_BUTTERFLY_FLAGS | Enumerate relevant encounters/events and verify ATEL opcodes/flags before authoring. |
| MAP_ABSORB_EVENT, ASSET_COMPATIBILITY, TEXT_RT0 | Identify Fire Eater outcome hook; prove Geosgaeno rig/summon compatibility; stage/localize renamed item text. |
| CONFIRM_CTB_TEXT | The quoted Aeon/CTB exchange has no definite feature; recover the requested behavior before work. |

`ELEMENT_PIPELINE_RT1`: implement and validate the complete contextual element path, including consumers before/after the affinity helper, status lifecycle and OnlyMod schema.

`AEON_HP_CAP_RT1` / `AEON_DAMAGE_CAP_RT1`: implement exclusive Workshop receipts and contextual cap consumers, then prove field/battle/save/UI behavior and human/enemy rejection.

`SPIRA_EFFECTS_AND_OWNER_RT1`: implement unresolved handlers and owner/Workshop restrictions for created IDs; reconcile legacy profile separately.

## Tests

Run the ten-step offline suite and separate Windows x86 probes using probes/README.md and the exact paths in the revalidation report. The pass label is an evidence level, not a gameplay claim. The source executable, saves, command.bin, monmagic2.bin and any generated DLL/executable remain outside Git; build/ is ignored.
