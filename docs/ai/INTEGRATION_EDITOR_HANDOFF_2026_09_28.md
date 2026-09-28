# Integrated Hook / Editor contract

Jarvis-HOOK — 2026-09-28. This is the source/RT0/RT1 handoff for the integrated
Vanguard, Elemental Dominion, Spira and paid Aeon consumers. It does not edit the
Editor, install a DLL or establish live-game/Production acceptance.

## Authoring and admission

- Keep vanilla mode available and default. Hook-dependent data is explicit
  OnlyMod output, with the applicable `[DERIVADO DE MOD-002]`,
  `[DERIVADO DE MOD-004]`, `[DERIVADO DE MOD-005]`, `[DERIVADO DE MOD-007]`
  and `[SPIRA REFORGE]` labels. A catalog entry or capability is not runtime proof.
- Native equipment remains **22 bytes**, with four WORD abilities at decimal
  offsets **14, 16, 18, 20**. The fifth ability is a validated logical sidecar
  slot; never write a fifth WORD at offset 22 or invent a sixth slot.
- Default Vanguard IDs are 135–147; Aeon/Spira defaults are 148–174.
  `AutoAbilitySlots.h` reserves these ranges against cross-module remaps.
  Explicit remaps start at 175, remain bounded by the real loaded kernel and
  must preserve the effect identity, payload, kind and canonical owner.
  The configurable ID ceiling does not enlarge the native WORD-sized bank.
- `SpiraAbilityCatalog.h` verifies every gameplay byte of each 108-byte row and
  its expected Latin label or explicitly supported numeric placeholder.
  A changed bank, name, header, locale identity or mapping revokes admission.
  Supplying the paid Aeon WORD alone never grants its effect.
- Arcane Focus, Spell Spring and Foolstrike/Fooltouch effects remain undefined
  and inactive. Fourstrike/Fourtouch retain their declared four-element native
  base only; unspecified status riders are not implemented or advertised.

## Elemental pack

The schema remains `ffx.mod007.elements.v1`; see `ElementPackCore.h`,
`ElementPackAdmission.h` and the checked-in native/RT0 fixtures for exact examples.
The implemented capability vocabulary is registry, context, affinity, spell-cap,
tactics, gravity and equipment (`mod007.<name>.v1`). Activation still requires
the independent feature gates and a Ready runtime with matching data.

- Native element fields remain BYTE. External descriptors do not borrow an
  unverified native bit. Commands, loaded banks and rows require exact bindings
  and fingerprints; the declared fallback is native-unmodified.
- Affinity uses signed basis points, 10000 = 100%, in 2500-point steps between
  -10000 and 25000. The preview and Scan consume the same resolver. Policies
  remain native-exact, highest-exposure, split-weighted and lowest-exposure.
- Equipment bindings support explicit `kind` (`weapon`, `armor`, `either`),
  canonical `owners`, native SOS admission and external-element deltas.
  Native equipment masks are not applied a second time.
- Monster profiles bind a raw native monster WORD, complete-file fingerprint,
  actor allocation/incarnation and bounded header/stat views. Names, apparent
  species and unusually high HP are not admission evidence.
- Per-element profile affinity accepts `imperil_immune` and
  `imperil_resist_bp` independently from `locked` and `base_bp`.
- Gravity uses only explicit admitted profiles, preserves immunity unless the
  profile explicitly overrides it, and applies the nonlethal final bound.
- Numerical Scan exposes base/effective percentages, equipment delta,
  Imperil/Ward/Nul stacks and remaining actions, locks and immunity/resistance.
  Ten descriptors fit one page; the engineering limit is 32 over four pages.

## Paid Aeon upgrades

`AeonAscensionCore.h` is the authoritative value contract. Owners 8–17 must be
acquired allies with the existing progression and canonical equipment gates.
Owner 7, ordinary humans, enemies and apparent Aeon shapes are excluded.

| Effect | Default ID / kind | Final price | Exact materials |
| --- | --- | --- | --- |
| Aeon Break HP/MP Limit | 148 / armor | 10,000,000 Gil | 60 Wings to Discovery, 60 Three Stars, 30 Underdog's Secret, 2 Master Spheres |
| Aeon Break Damage Limit | 149 / weapon | 15,000,000 Gil | 99 Dark Matter, 30 Winning Formula, 20 Gambler's Spirit, 3 Master Spheres |

The Aeon Gil multiplier is already included. No fifth-slot surcharge or material
multiplier applies. The selected native empty slot, explicit corresponding Break
replacement, or already-unlocked logical fifth is occupied. Aeon Immunity
`0x807B` is never removed. Paid abilities have rank zero and no refinement scale.
Dedicated confirmed removal has no refund and does not restore an old ability.

Receipts use Ledger v1, recipe v1, fixed 680-byte ledger / 32-byte entries,
binding save identity, piece and ability instance IDs, owner, effect, WORD and
recipe version. `FFXASCP1` immutable attachments bind the complete Workshop
State-v1 hash and normalized save path. Native-save padding and ranks carry no
receipt. Old saves without a receipt retain their native behavior.

Purchase/removal uses the existing paid checkpoint commit point. Generic
reforge, fusion, clearing and refinement cannot transfer or manufacture a paid
permission. Cancellation, stale previews, mapping drift, missing funds, failed
durable preparation and save isolation are checked before acceptance. Buying a
cap never restores current HP/MP; removal only clamps excess values downward.

## Shared runtime and integration with main

The merged main includes the completed language work and its existing Arcana
implementation. This task preserves those consumers through shared services:

- `SharedClampRuntime.h` owns RVA `0x39A0D0`. Arcana's field adjustments precede
  the independently gated Spira cap request; the original clamp runs once.
- `SharedCombatRuntime.h` owns MP cost `0x38D030`, critical `0x389750` and HP
  application `0x38E2F0`. Arcana adjusts the request around Vanguard's policy;
  startup order does not alter precedence, native RNG or actual-loss accounting.
- `SharedNulRuntime.h`, `SharedActionRuntime.h` and `SharedActorRuntime.h`
  compose Radiant/Umbral and Elemental charges. A full-coverage result reserves
  charges, duplicate calculation reuses the decision, actual result consumption
  settles it once, and cancellation/reset retires the reservation. The old
  nativeSlots/P16 options use external ownership, not actor timer bytes.
- Spira rewards multiply each positive selected item quantity at native RVA
  `0x398AD0` in the canonical reward buffer only. The party maximum is 2 or 3,
  the native eight-slot / 99-quantity bounds remain authoritative, and AP/Gil,
  unrelated buffers and non-item namespaces are preserved.
- Native save notifications support multiple independent observers but one
  checkpoint selector. Paid checkpoints use temporary-stat projection and
  prepare/finish metadata callbacks. Ronso ownership binds the bytes actually
  written after projection. Other observers receive the selected checkpoint's
  canonical image; its selector retains the original disk anchor. Failed reads
  close admission through `ReadRejected()`; they never dispatch a confirmed
  new-game/reset event or erase the shadows needed for safe serialization.

All addresses are RVAs for the supported PE32 fixture SHA-256
`78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced`
(preferred ImageBase `0x00400000`). They are not portable offsets for another PE.

## Remaining acceptance boundary

See `docs/research/INTEGRATION_FINALIZATION_2026_09_28.md` for fresh commands,
source/artifact receipts and counts. The native harnesses use a private relocated
PE and controlled formula/graphics dependencies; their success is RT1, not RT2.
Live HP loss versus displayed values, summon/dismiss/re-summon, Zanmato/nonnumeric
exclusions, Reflect/counter scene behavior, actor replacement, language assets
and save/reload acceptance still require the separately authorized RT2 run.
