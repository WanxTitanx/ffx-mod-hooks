# Main integration and test candidate — Jarvis-HOOK

## Scope and source ownership

The runtime lane (`fastload-autosave-20260916`, `dc5ad38`), equipment lane
(`codex/mod-ideas-precode-20260923`, `e867317`) and main (`a70a354`) are
consolidated in the main checkout. Main's context tools, scoped agent policy and
retirement of the unsupported Sphere Grid entries are preserved. Existing local
changes to CURRENT_STATE, SESSION_HANDOFF, MOD_IDEAS_BACKLOG, the external hook
comparison and `.omo/` are not part of this implementation.

The new code is compiled by `src/runtime/FfxHooksDll/build_hooks.ps1
-WithPolyHook -Release`. The Workshop is now part of the native DLL, rather than
only the separately served research UI. No game executable, kernel, save,
generated DLL or private fixture belongs in Git.

## Equipment Workshop integration

- F8 / Reforge / Equipment Workshop defaults OFF and requires restart. The
  independent menu action is `Open Equipment Workshop` under Input bindings;
  keyboard and gamepad bindings default unassigned. No new fixed key is taken.
- The native menu has character filtering, scroll, keyboard/gamepad navigation,
  glass surfaces, selection animation and material confirmation. It has its own
  modal object, focus-loss closure, owner-thread close drain and cursor ownership.
  This first native menu does not implement the browser prototype's text search.
- Native records remain 22 bytes, original slots remain at most four. Piece IDs,
  ranks, fifth abilities and RNG state are stored in versioned binary sidecars
  under `modules/config/equipment-workshop-v1/`, bound to the actual native save
  path and full saved-file SHA-256. Save normally in FFX to persist a transaction.
- Existing Ronso-owned CRT read/write hooks publish passive completed I/O events.
  Workshop does not install another fread/fwrite hook, retry a native save or
  alter the native save format. A corrupt matching sidecar closes admission.
- Native load/create/swap/free/equip events preserve instance identities.
  Unexpected inventory changes close admission; reload a save to recover.
  Confirmations are invalidated by inventory revision, including another load.
- Mutations require the native owner thread, field state, current identities,
  sufficient real inventory materials and a fresh identical preview. Partial
  write failure rolls back only bytes still equal to the transaction's own image.
- Implemented refinement effects remain the 24 numeric percent abilities
  (98–121), Auto-Shell (84) and Auto-Protect (85). Other effect families are
  refused before payment. The 131-row research table is not a claim that all
  effects are implemented. Reforge, fusion, original-slot expansion, removal,
  supported evolution, A/B refinement and fifth-slot selection use the same
  transaction core as the research tests.
- Safe 24-byte views serve the two patched consumers, including disabled,
  unknown/special-equipment and stop paths. Global kernel rows are never edited.
  The new hooks are pinned for process lifetime. Stop removes admission and
  yields vanilla views; it does not physically unhook or support DLL unloading.

Exact PC PE SHA-256:
`78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced`.
All following addresses are hexadecimal RVAs at preferred ImageBase 0x400000:

| Surface | RVA | Width / evidence |
|---|---|---|
| Native load | 4B5450 | cdecl destination/source; exact profile and 32-byte relocated prefix |
| Create / swap / free / equip | 3AB930 / 3ABA10 / 3ABCC0 / 3AB990 | Native 22-byte records; actual native calls in RT1 |
| Field / battle aggregation | 3861B0 / 39C610 | Scoped private Gear/Row views; full battle aggregator in RT1 |
| Gear / ability row / direct-ID query | 3ABBF0 / 3AB890 / 3A0C40 | Exact consumer return addresses; four native WORDs plus private fifth WORD |
| Protect / Shell | 38AE00 / 38AE80 | Original leaves execute before bounded rank reduction |
| Damage producer | 38E680 | cdecl 11 arguments; target ID is argument 3, DamageInfo is argument 7 |
| Loop immediates | 386787 / 39C8A4 | One byte 4 to 5 after helpers; retained safe views on stop |

The damage leaf's status argument is **DamageInfo**, not an actor status pointer.
Actor ownership is carried through the exact producer's thread-local frame.
Complete combat/KO/revive/cache coverage still needs player RT2. Native tests are
not evidence that every game inventory entry point has been observed live.

## S.I.N. UNI-001 through UNI-008

The previous exporter discarded typed command targets: its self-command codec
always emitted FFF3, including hostile and allied-team actions. This is an
exporter defect, not a difficulty-stat defect. The replacement exporter verifies
the actual baked instruction prefix for each profile before publishing a pack.

| Curse | Contract now exported |
|---|---|
| UNI-001 Opening Veil | Existing once-only direct self-status fields retained |
| UNI-002 Counter March | Canonical on-hit guard; native Delay Attack 3006 against FFEF; terminal action |
| UNI-003 | Direct self Haste, then Slow 3038 against one living front-line party member; terminal action |
| UNI-004 | Once-only direct Shell, Regen, one NulBlaze and one NulShock charge; retain native turn |
| UNI-005 Frost-Flood Weave | Hostile team FFF2, terminal action |
| UNI-006 | Hostile team FFF2, terminal action |
| UNI-007 | Self FFF3, terminal action |
| UNI-008 | Allied monster team FFF1, terminal action |

All 47 compatible profiles pass semantic prefix/guard/latch checks and preserve
non-AI sections. Pack SHA-256:
`ddad9d6e460c55a92ec5a26c6f70761f52abb06b450097a251847b1ead711a01`.
The generated proof now depends on custom command rows 268–271; experimental
Counter March row 272 and replaced Ward row 267 are no longer dependencies.

Counter March previously substituted experimental command 6110 for the editor's
explicit native Delay Attack bake. Returning to 3006 is the evidence-backed
candidate. The precise animation/softlock cause was not reproduced live; no claim
that a missing texture or an animation ID alone caused the lock is supported.
The previous user's confirmed stats/name/Opening Veil behavior remains distinct
from the pending Counter March and Frost-Flood gameplay acceptance.

## Startup timing

The fixed 2000 ms worker delay already exists in main a70a354 (500 ms with the
optional D3D overlay). It does not establish the cause of the reported new delay.
Logs now record config, early audio, Ronso I/O, Workshop, Fastload, installation,
F8 readiness and first ready Present elapsed times. Timing begins after logging
initialization. No SpeedHack policy or fixed delay was changed for this diagnosis.

## Verification and acceptance limits

Private command/output/hash evidence is under
`.superpowers/sdd/2026-09-24-main-workshop-021558Z/`. Windows native Workshop RT1
passes 27 checks including real create/free/equip, material debit, fifth-ID lookup,
full native Auto-Protect aggregation, unchanged global kernel, save association,
load revision invalidation and vanilla views after stop. The store passes 12
checks; shared Ronso I/O passes 18 enabled and 13 disabled checks.

The 17 harness cases total 51,361 assertions on Windows and the same MSVC
executables on Proton. DLL loader/export/wait checks also pass on both. Local
shared-core checks pass 165 assertions normally and under ASan/UBSan; 19 host
tests pass. The DLL is 1,791,488 bytes, SHA-256
`7247bff61e00f407eae64222a94984688249da6546b1e3568a39ab55eb8bda4c`.

Two harness-environment failures were corrected, not hidden: Proton's
`LoadLibraryEx(..., DONT_RESOLVE_DLL_REFERENCES)` retained preferred-base
addresses in the private EXE fixture, so the fixture now normalizes its actual
HIGHLOW table before the unchanged runtime profile gate; the OFF Ronso case
must reuse the ON case's directory to exercise the same saved ownership. The
new isolated Proton prefix also required its distribution's default vkd3d DLLs
for the D3D11 loader dependency. All these changes are isolated test setup.

The broader F7/F8, binding, SIN, FMV and Ronso suites are recorded in the packet.
These are RT0/RT1 and build evidence, not RT2 or Production. Independent review
remains pending; self-inspection is not independent review. Deployment and a
disposable-save game session need their separately authorized test steps.

Player test order: enable Workshop and choose its binding; restart and load a
disposable save; edit an unequipped regular piece; inspect its material receipt;
confirm/cancel; save/reload; equip and compare the implemented effects. For SIN,
use natural Woods/Snowfield encounters with the displayed seed, observe the eight
curse families and capture timing logs from process launch until F7/F8 respond.
