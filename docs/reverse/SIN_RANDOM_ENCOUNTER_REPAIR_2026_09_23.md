# S.I.N. random encounter acceptance repair — Jarvis-HOOK

The user reported that Opening Veil did not apply in a random Macalania battle,
and requested native name indicators, Threat-scaled AP/Gil and compatibility with
Difficulty. This supersedes the earlier claim that the label resolver represented
random walking. The preceding test deployment is `F4A1903E`; its acceptance failed
for this feature. Experimental MOD-004/005 work remains queued behind this repair.

Private evidence: `.superpowers/sdd/2026-09-23-sin-runtime-acceptance-211524Z/`.
The user-session log, initial installed identities, actual monster source snapshots,
compiler manifest and validation logs are retained there. No game assets belong in
Git. The user's original monster files are read-only inputs and remain unchanged.

## Findings and implementation

1. **Wrong entry path.** `ResolveEncounter` RVA3828B0/return381D8C belongs to
   `MsBattleLabelExe`, a label/script path. Walking invokes `MsBattleEncountExe`
   RVA380DE0 from the exact call at RVA471CEA, return471CEF. On acceptance it writes
   field-row/group/formation at RVAD2C256..D2C259 and returns -1. It never calls the
   old resolver. The live log had no resolver capture despite random battles.
   The new observer validates the exact PE, 24-byte prefix and five-byte caller,
   preserves the original float/arguments/result, then publishes the selected
   native values through the existing request/generation owner. External direct
   calls, non-start returns and explicit-launch suppression cannot grant authority.
   Input field identity and native lookup-row index remain separate.
2. **Wrong actor ID representation.** The native constructor at VA79C588 copies
   the complete formation word into `Chr+0xE`. Existing Arena fixtures/catalogs and
   the native filename formatter at VA784151 establish the monster family encoding:
   `m003` uses `0x1003`, not 3. The previous bare-ID checks rejected real actors.
   Admission now explicitly decodes only `0x1001..0x13FF`, then checks the closed
   pilot roster. Unrelated families and low-byte aliases remain rejected. No actor
   ID is modified. RED tests reproduced both caller and typed-ID rejection before
   changing the respective gates.
3. **Wrong Opening Veil event.** The exporter used OnHit. The original injector and
   Editor's curated recipe specify the first OnTurn. Its dedicated private latch
   now guards that event. The exporter traces the generated initialization/turn
   wrappers: only the new latch is touched, all three self-property grants occur
   once, the second turn rejoins the original body, and unrelated event entries
   stay unchanged. This is an emitted-code RT0 trace, not a live ATEL execution claim.
4. **Changed source scripts.** The installed m003 and m019 AI had grown since the
   first profile packet; eight other pilot files were identical. The first pack's
   exact-source gate therefore also rejected these changed files. The exporter
   preserves their current custom behavior and compiles new compatible wrappers.
   All 47 profiles are regenerated. Pack size134004, SHA-256
   `838E1433D996975DA5B6B18FF657BED706A707736CBD64025D09AAE6D271B79E`.

## Scaling, labels and rewards

The recovered formulas in `src/sin/SinScaleInject/Program.cs` are:

- HP and overkill threshold: `floor(afterDifficulty × (1 + 0.10 × T))`.
- Each of the eight stats: `floor(afterDifficulty × (1 + 0.05 × T)) + T`, capped255.
- Current HP keeps the existing single final-ratio calculation. The shared
  Difficulty writer remains the sole owner of these fields. Registration rearms
  its bounded retry so a newly admitted script mask is not missed after population.

AP/Gil scaling is a newly completed policy requested by the user: +10% per Threat,
including overkill AP, capped at the native unsigned16 maximum. The old injector
did not establish a separate AP/Gil formula. The original reward consumer at
RVA3990E0 receives a retained 280-byte view with only its first three unsigned16
values changed. Its own normal/overkill selection and existing global F8/vanilla
multipliers run afterward. Source loot bytes, equipment/item/steal data and actor
resource pointers stay intact. Only successfully registered cursed actors in the
same natural generation and owner thread may supply a view.

The 40-byte native name at `Chr+0x540` gets a bounded encoded suffix such as
` [Veil T1]`. Original text is preserved; no truncation or unknown control-payload
rewriting is attempted. Labels can be refreshed after population without appending
twice. Restoration compares actor/whole-file identity and the complete owned name;
foreign renames survive. Field return retires the encounter metadata. Data and
trampolines remain resident; hot DLL unloading is unsupported.

Native reward-layout evidence: the function at VA7990F8 reads overkill AP from
`loot+4`; VA799107 reads normal AP from `loot+2`; VA799130 reads Gil from `loot+0`.
AP/Gil multiplier sites remain the existing F8-owned body sites. The new detour
owns the function entry and argument only. Actor name/RAM layout is crossed against
the PC accesses at VA79B519/VA931AED and the existing Editor/Fahrenheit descriptions.
Fahrenheit is a read-only structure reference under LGPL-3.0-or-later; no external
implementation is copied. All executable evidence targets the fixed PC SHA-256
`78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED`.

## Verification boundaries

The native fixture executes real registration/allocation and the native reward
selector/multiplier prefix in a private mapped PE. Its test-only shortened reward
tail must restore EBX (pushed at VA799133) as well as EDI; omitting EBX caused an
isolated harness AV and invalidated that run. This tail is never part of the DLL.
The corrected fixture additionally checks owner-thread admission, stale generation,
idempotence, original asset/resource preservation, name bounds and owned/foreign
restoration. Final results and installed identity are authoritative in the packet
receipts; intermediate passing counts do not substitute for the final build.

No FFX process was launched by the agent. Native game timing, visible target-name
layout, the enemy's first-turn status behavior and the full victory screen remain
player acceptance checks. Independent review and Production promotion remain
pending. New runtime behavior is controlled by the existing default-OFF S.I.N.
setting and retains profile/signature, natural origin, generation and thread gates.

Final validation:32,919 affected assertions per Windows/Proton environment;400 native AI/metadata assertions,56 metadata core,3,621 F7 core and3,772 F8 core included. Configuration/UI contracts, Release and exact final DLL loaders passed. The corrected native fixture additionally passed three fresh process runs. Deployment completed2026-09-24T00:00:44Z: DLL3A641937…B4DB10 and matching838E1433…271B79E pack, two verified backups and56 protected files unchanged. Player acceptance remains pending.
