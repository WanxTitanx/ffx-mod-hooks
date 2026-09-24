# F7 In-Live

Gate: `[f7] inlive=1`, `modules\config\f7_inlive.flag`,
`modules\f7_inlive.flag`, or `FFXHOOKS_ENABLE_F7=1`.
The feature is OFF by default. The default hotkey is F7 (overridable with
`FFXHOOKS_NATIVE_MENU_HOTKEY`).

## Custom Mix and Ultra (offline candidate)

Custom Mix x3/x4/x5 and Custom Mix Ultra share the RAM-only Arena+ editor. Turn Arena+ Master
ON in F8 and restart once; Compose F7 then controls the editor live. The broad `f7.inlive` gate
may remain OFF. The shared Difficulty owner must still accept the exact executable and signature
set. Validation-only mode, stopped admission, or a restore conflict prevents launch. Turning
Compose OFF cancels pending work at the menu producer and again at battle consumption.

Each Dark Aeon is selectable only after its defeat flag is present in the current in-memory save.
The F8 **Bypass Progression** toggle (`arena_plus.unlock_all`, default OFF) makes all eight choices
available without changing save flags. It also applies to rematches and preset gauntlets. It does
not bypass executable signatures, capacity, carrier ownership, or restoration checks. Legacy
`arena_plus_unlock_all.flag` and `FFXHOOKS_ARENAPLUS_UNLOCK_ALL` remain compatible with the normal
F8 authority rules. The editor never reads another disk save to infer progression.

Fixed Mix modes require exactly 3, 4, or 5 expanded positions; Ultra accepts 1 through 8.
Standard Mix entry fees retain the configured sum of selected bosses (Magus is one entry);
Ultra retains free entry. Failed queue/arm does not charge Gil. Arena+ Music, when enabled at
startup, applies the Arena soundtrack to this path too. It respects `.off` overrides.

The native submenu contains exactly eight symbolic choices: Valefor, Ifrit, Ixion, Shiva, Bahamut,
Yojimbo, Anima, and Magus. Repeats are allowed. The portable `BuildSelection` allowlist is the only
expansion authority; Magus becomes its three reviewed slots there, and the UI accepts no raw monster
ID. The editor has nineteen rows: Arena, Camera, eight choices, position controls, Remove Last, Clear, export/library, Launch and Back. Its
bounded eight-slot preview and status use `EMPTY`, `FULL`, `READY`, `QUEUED`, `CONSUMED`, `FAILED`,
`RESTORE CONFLICT`, `EXPIRED`, or `UNAVAILABLE`. Back and Cancel return to the parent Mix list or Arena+ hub; F7 close,
focus loss, and stop return control to the game and cancel an outstanding request.

Launch first queues only the exact vanilla carrier `dome02_00` (field `517`, group `0`, formation
`0`, token `0x02050000`, transition `2`) through the existing `0x00381D60` helper. The CustomMix
request is armed only after the helper reports call success, return `-1`, and an armed game queue.
Requests are RAM-only, one-shot, monotonically generated, and expire after 30 seconds. Requests stay in memory. Explicit Export/Import actions use the separate versioned battle library;
no active game battle file is modified. See [the library guide](ARENA_MIX_LIBRARY.md).

The first **Arena** row restores the former x3/x4/x5 scenery choices. Saved JSONs
retain that choice. An admitted launch optionally overrides only the native
battlefield uint16 at RVA `0x00D2C254`; the upper field-routing word is preserved.
This selector survives InitScene because its caller still needs it for resource
loading. Owned values are compared/restored on known nonbattle transitions or
drained teardown; a new native battle takes ownership. Old presets default to
the original carrier scene. No additional hook or active battle-file write is used.

The allocation/launch carrier now supplies storage only. A verified normal arena
view is loaned to the admitted native InitScene. Its script, mapping, formation
and position references remain on the normal view for later consumers, including
all selected IDs. The engine allocation root/size and borrowed bytes are restored.
Views stay pinned for native workers; next native initialization resets its own
references. No inherited per-monster movement overrides remain. Camera defaults
to Arena with optional Tactical framing; new v2 exports contain normal battle data.

F7 remains the sole owner of InitScene RVA `0x00383ED0`; the position accessor has its own coordinator owner and does
not occupy Seymour's composer slot. Only the classified battle-state caller enters the CustomMix
wrapper. `ExecuteTransaction` temporarily changes the exact 16-byte formation window, synchronously
runs Seymour-before, guarded vanilla exactly once, and Seymour-after while those bytes are visible,
then compare-restores the original bytes. Explicit position layouts also use an exact, separately
signature-gated accessor at RVA003AC000 for later native reads. That reader preserves native Y/W and
return values, follows battle/thread/actor ownership, yields to native setters, and is cleared on
new battles/OFF/stop. Bootstrap, Sphere Grid startup, and unknown callers bypass
the carrier probe and execute the existing vanilla-only shared path. A restore conflict closes
CustomMix admission for the rest of the process session.

The Windows adapter performs only the three fixed typed reads at the reviewed carrier RVAs and maps
one `VirtualQuery` result. The complete `0x4428` borrowed carrier must be committed, writable,
unguarded, and contained in that one region. The old disk composer remains disabled and is not
entered by the Mix menu. Current evidence is an **offline candidate** only; a
separately authorized user-run RT2 and explicit promotion are still pending.

Difficulty modifies process RAM only. Its sole persistence surface is the bounded
`modules\config\f7_inlive.json` auxiliary document, written by a same-directory atomic
replacement. It does not open `.bin`, battle-data, executable, or save files for writing.

## Supported executable and seams

The Difficulty hook installs only when every gate below matches:

- SHA-256: `78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED`
- PE32/I386, timestamp `0x55D2F3CC`, preferred image base `0x00400000`,
  `SizeOfImage 0x0237D000`
- original actor-table initializer (validated, not detoured) VA `0x0079C130` / RVA `0x0039C130`, whose exact prefix starts
  `55 8B EC 51 53 56 57 6A 60 68 00 60 13 01 E8`; the `push abs32` operand at prefix
  offset 10 / relocation RVA `0x0039C13A` targets RVA `0x00D36000`
- actor accessor VA `0x00795AB0` / RVA `0x00395AB0`,
  `int __cdecl(uint8_t)`, with the exact 21-byte body validated before hook creation; its
  `add eax,[abs32]` operand at signature offset 15 / relocation RVA `0x00395ABF` targets
  RVA `0x00D34460`
- natural encounter resolver VA `0x007828B0` / RVA `0x003828B0`, with exact 21-byte loaded
  prefix `55 8B EC 51 8B 45 08 53 0F B7 D8 56 C1 F8 10 25 FF FF 00 00 57`; the supported image
  has exactly four direct call sites at VAs `0x00781D87`, `0x00782A0C`, `0x00782C0E`, and
  `0x00782C6E`, and only the natural transition call returns at RVA `0x00381D8C`
- system-scene/actor-table initializer VA `0x00783ED0` / RVA `0x00383ED0`, with exact 24-byte
  loaded prefix `8B 0D A8 A9 12 01 56 8B 41 04 0F BE 35 D9 C9 12 01 03 C1 A3 AC A9 12 01`
  at the preferred base; its three `abs32` operands at prefix offsets 2, 13, and 20 target RVAs
  `0x00D2A9A8`, `0x00D2C9D9`, and `0x00D2A9AC`

Read-only IDA query `63d55a9e` confirms that `0x00783ED0` is
`FFX_Battle_InitSystemSceneAndActorTable` (size `0x13B`) and contains the only code xref to the
actor initializer, at VA `0x00783FB1`. The previously considered VA `0x0077B7C0` is the unrelated
movie IPU macroblock decoder and is not a Difficulty target.

The third batch target is now native actor population at VA `0x00784010` / RVA `0x00384010`,
`int __cdecl()`. Its full 89-byte body is validated, with four HIGHLOW operands at offsets
4/10/67/80 targeting RVAs `0x00D2A929`, `0x00D2A8E0`, `0x00D2A8E0`, `0x00D2A929`.
The shared native InitScene original returns 8/10 on the branch that actually called the
actor initializer. Only that return from the known battle caller arms a new generation;
bootstrap, Sphere Grid, unknown callers and its non-battle return 0 do not arm Difficulty.
Generation setup consumes the correlated field/S.I.N. tickets inside the shared original
boundary, before Seymour's after phase and before CustomMix restores its temporary carrier.
It reads/writes no actor data and does not hold the actor-runtime lock across vanilla.

Population calls vanilla exactly once. A resumable return -1 performs no phase/actor read;
only return 0 followed by readable phase `0x13`, pending generation, open admission and the
exact initializer thread may service the writer. Its existing validated accessor and ratio
transactions are unchanged. Present is no longer a producer: the rejected user run had game
thread 460 and renderer 580. The native completion avoids both that mismatch and the menu
pump's late return. NoActors keeps the bounded retry armed; stop/teardown close it, and
Apply Now cannot enter during initialization or the pending first capture.

Disk identity alone is insufficient: all nine evidenced HIGHLOW operands are reconstructed
at the actual loaded base, and all five code sequences must match before hook creation.
The installed 14CFF834 candidate logged five phase-0x13 applications with four actors and
20/21 writes, and the user reported changed stats on 2026-09-19. Exact multipliers and the
complete OFF/restore protocol remain unverified. The newer status/element additions were
deployed as 8141BA9D on 2026-09-19 under explicit authorization; their live result remains pending.
See [the population producer evidence](reverse/DIFFICULTY_POPULATION_PRODUCER_2026-09-19.md).

Natural resolution and every explicit Force, Arena, and supported Custom Mix success route publish
a dedicated one-shot current-battle field. Explicit launches begin a request token, publish only
when that same request succeeds, and cancel it on failure; a stale completion cannot overwrite a
newer natural or explicit route. Natural capture is suppressed only inside the synchronous
explicit call on that same thread, so a failed or early explicit route cannot suppress the next
natural encounter. The admitted InitScene completion consumes the immutable ticket for one generation. A missing
ticket is distinct from the last Force history and selects only an explicit `fieldRow=-1`
fallback rule when one exists.

S.I.N. uses the same request publication but accepts only the natural Resolver return RVA
`0x00381D8C` and the full DWORD Resolver token. Its bounded ticket is staged before the natural
BattleField commit; a failed commit cancels it, while Stage failure does not block Difficulty.
Suppressed and explicit Force/Arena/Custom Mix routes never receive a S.I.N. ticket. The admitted InitScene completion
clears prior S.I.N. state, consumes both publications for the same request, and accepts only a
matched natural ticket whose nonzero generation equals the new actor generation and is strictly
newer than the last accepted generation.

Each accessor result must pass `VirtualQuery`: the complete `0xF90` record is committed,
writable, not guarded, and has a formation ID other than the `0xFFFF` empty sentinel. Heap
records are not required to lie inside the executable image.


## Supported Difficulty fields

All supported fields have an exact width and RT0 canary/readback coverage:

| Field | Actor offset and width |
|---|---|
| Formation identity | `u16 +0x00E` (read only) |
| Maximum HP / MP | `u32 +0x594`, `u32 +0x598` |
| Current HP / MP | `u32 +0x5D0`, `u32 +0x5D4` |
| Overkill threshold | `u32 +0x5A4` |
| STR/DEF/MAG/MDF/AGI/LCK/EVA/ACC | `u8 +0x5A8..+0x5AF` |
| Absorb / ignore / resist / weak | `u8 +0x5DA..+0x5DD` |
| Innate AUTO, first 12 / remaining 13 statuses | `u16 +0x630`, `u16 +0x632` |
| Status resistance, same 25-item order as AUTO | `u8 +0x641..+0x659` |

Current HP/MP are dynamic gameplay state. Initial apply preserves their vanilla percentage with
checked 64-bit, half-up arithmetic; after damage, healing, or MP spend, an edit or OFF preserves
the live gameplay ratio against the edited or restored maximum and clamps current to that maximum.
Each maximum/current pair is one ratio transaction: the denominator and gameplay current observed
before the maximum write remain immutable until the paired current write commits. If the maximum
write succeeds but the current write fails, retry uses that retained ratio rather than recomputing
from the partially updated actor. If a maximum write reports failure and an immediate readable
canary proves the exact prevalue, the pending pair is discarded so the next call captures fresh
damage, healing, or MP spend before retrying. A current write that reports failure with an exact
readable prevalue retains the immutable transaction; retry proceeds only while live current still
matches that evidence. Before the first current attempt after a maximum change, current must still
equal the captured numerator (or already equal the desired result), so gameplay cannot be
overwritten between the paired writes. A reported-success write that reads back as the prevalue is
interference, not retry evidence. An unavailable maximum or current post-write readback is
permanently ambiguous and relinquishes both fields immediately; a later ABA back to the prevalue
can never authorize a stale write.
All structural edits recompute from one immutable baseline per battle generation; area presets
replace the global preset and never compound with it. Structural fields use compare-before-write,
so a third-party identity change causes per-field `OwnershipLost`, not an overwrite. OFF
also rechecks the read-only formation identity before any restore, so a reused actor slot is never
treated as the captured actor merely because some structural bytes happen to match.

The fixed-schema parser clamps HP, MP, and overkill multipliers to 100..10000 permille and stat
multipliers to 100..5000. It rejects out-of-range masks and resistance bytes, duplicate known
keys, excess array/rule items, malformed input, and documents larger than 16,384 bytes. Unknown
keys are ignored so the same bounded document can retain the Force and Music configuration.
Parsing, serialization, and filesystem I/O occur outside the runtime lock. Only a complete,
immutable validated snapshot is published through the mutex-backed production config store.
Apply Now and the initializer copy one generation from that store before entering the separate
runtime lock; concurrent UI edits and reloads therefore cannot expose references or torn values.

## Monster AI observer

The separate legacy `f7.aiswap` gate now requests observation only. On the exact supported
PE32/I386 profile, the adapter validates the script-registration target at RVA `0x00384120`,
its sole direct caller at RVA `0x003839C4`, cleanup at RVA `0x00381660`, and the Monster AI
dispatcher at RVA `0x003AC9E0` before transactionally installing one three-target
registration/cleanup/dispatcher observer batch. Registration emits bounded before/after snapshots
for eight slots, cleanup retires the matching generation, and dispatcher events correlate the
generation, thread, nonzero serial, actor slot, monster ID, low-u16 command, target mask, force,
queue readback, decision reason, and proposal metadata. This is a value-only boundary: logs retain
no actor/script pointer or script bytes, and proposal metadata grants zero mutation authority.

Loaded-prefix validation is ASLR-aware without weakening any signature. The registration
`HIGHLOW` relocation at RVA `0x00384127` reconstructs only its `abs32` operand as loaded image base
plus target RVA `0x008613D8`; cleanup does the same at relocation RVA `0x00381662` for target RVA
`0x00D2A8E0`. The dispatcher's exact 66-byte prefix has four evidenced `HIGHLOW` operands at RVAs
`0x003AC9E8`, `0x003AC9F6`, `0x003ACA06`, and `0x003ACA1E`. It must be unique in executable
memory and have exactly three direct-call return RVAs: normal `0x003A454E`, force `0x003A4A60`,
and death override `0x003A4B8C`. Every other prefix byte remains exact, including the registration
and cleanup trailing `E8 rel32` calls. All three loaded prefixes, the sole registration caller, and
the three dispatcher callers are validated before MinHook initialization or hook creation.

The old status injection, direct script-byte stepper, and JSON Save/Reload UI are removed. The
mutation whitelist is empty (including explicit rejection of corpus pair `m342`/`m343`), so the
menu truthfully reports `Monster AI Observer - Read-only`. `FFXHOOKS_VALIDATE_ONLY=1` performs
profile validation without installing any target. The physical x86 cdecl shim forwards all five
cdecl DWORD arguments unchanged, calls the original exactly once, and returns the exact queue
readback; it has no effective-command or replacement-command channel. Repeated dispatcher calls
remain distinct events rather than being deduplicated. Source/build results remain an offline
observer candidate until the separately authorized RT2 slice in `RT2_PROTOCOL.md` is completed;
live lifecycle, ABI, and zero mutation observations are still pending. A request enabled after a
healthy startup reports `OBSERVE PENDING RESTART`; if process-global MinHook setup failed, a
requested observer reports `UNAVAILABLE` instead. Each shim leases callback lifetime before any
other shared access, and normal teardown closes admission, drains, queue-disables and applies the
exact owned batch, exact-disables it, and drains once more. Once any queued apply has been
attempted, all three disabled trampolines and their contexts remain allocated for process lifetime:
a CPU can already be paused in the machine prologue before the first C++ lease increment, so a
zero callback count cannot prove that a trampoline is freeable. The page reports `INERT (RESTART
REQUIRED)` in this state. Only create failures that never touched the process-global queue remain
exactly removable. A single-flight coordinator is the sole owner of `MH_QueueEnableHook`,
`MH_QueueDisableHook`, and `MH_ApplyQueued`; it also initializes MinHook process-wide before any
feature starts, independent of FieldScout or Seymour gates. Initialization and neutralization
failures poison further applies until restart. FieldScout uses the same coordinator, applies its
complete batch in every mode, checks one process-sticky shutdown admission at all eleven detour
entries, prevents path transitions from reopening capture, drains in-flight trace-thread creation,
and retains applied trampolines plus runtime context until process exit. Any failed queued apply is
absorbing `Poisoned` even if compensating disables make targets inert. MinHook stays initialized
process-wide, and dynamic DLL unload remains unsupported.

The composed offline candidate now gives Difficulty its distinct `Owner::Difficulty` batch on the
same coordinator. This composition is still not a deployment or live-game claim; observer and
Difficulty RT2 remain separately pending.

## S.I.N. RAM composition

S.I.N. adds no MinHook target and does not change the shared InitScene composer. The existing
admitted InitScene completion creates a closed `SinRam::RuntimeRequest` containing only validated
configuration plus token, origin, caller, request, and generation evidence. The Difficulty runtime
then derives the raw u16 formation ID, field row, and after-Difficulty structural values itself and
uses `UpdateComposed` as the sole memory writer. Difficulty and S.I.N. share exactly one final HP
ratio calculation. S.I.N. has no MP write surface; MP remains Difficulty-only.

The allowlist is exact: field `340` IDs `4, 12, 19, 37`, and field `310` IDs
`3, 26, 33, 81, 217`. Threat T0 is neutral; T1 applies 110% HP/overkill and `105% + 1` stats; T2
applies 120% HP/overkill and `110% + 2` stats. No masked ID, alias, other field, or explicit battle
origin is admitted. Apply Now may reuse only the current natural request and current generation,
replacing only its configuration snapshot.

## Auto-status and elemental runtime candidate

The new source candidate uses exact executable xrefs and widths, recorded in
[the status/element evidence](reverse/DIFFICULTY_STATUS_ELEMENTS_2026-09-19.md).
It was deployed under explicit authorization on 2026-09-19; it is not a live RT2/Production result.

AUTO ORs the selected 25 bits into the two captured innate words. It does not write a guessed
32-bit status block, erase an actor's existing innate effects, or use the extra/SOS words as
configuration storage. A change calls vanilla's `void __cdecl(actor*)` remove/apply pair at
RVAs `0x0039B1B0` and `0x0039B2A0`, on the admitted battle thread. Both complete, relocation-free
bodies (239/470 bytes) must match before batch installation and before native refresh. Vanilla
restores temporary effects from its own backup fields, recomputes full innate/SOS masks, and
installs selected duration effects with `0xFF`. Vanilla interactions, such as Haste dispelling
finite Slow, remain intact. Unchanged Apply performs no extra native refresh.

Both innate words are checked before either changes. Missing native admission performs no
AUTO writes. Unknown readback, a successful write reverted to its prevalue, or a native exception
closes AUTO for that actor generation; OFF never blindly retries uncertain native backup state.
Formation replacement drops the old snapshot. `auto=N` in the Difficulty result log counts
successful native refreshes; it does not replace visible in-game evidence.

Choosing weak/resist/absorb replaces competing affinities (including native ignore) only for
the selected elements. Unselected elements and unknown high bits retain their captured values.
The UI keeps one chosen affinity per element. Existing overlapping JSON masks resolve
absorb, then resist, then weak. Status-resistance JSON bytes set minimums; zero preserves
native resistance rather than making immune monsters vulnerable. OFF compare-restores the
owned structural fields and refreshes AUTO through the same native pair.

The Difficulty/Music hub rows only open their respective editors. Numeric adjustments and
music previews remain inside the corresponding submenu.

## Quarantined scratch fields

These scratch fields remain quarantined from runtime writes.

`+0x6E4/+0x6E8` are per-hit scratch snapshots copied from authoritative current HP/MP at
`+0x5D0/+0x5D4`; Difficulty never writes the scratch pair. `+0x5A0` is base maximum MP, not
overkill. Overkill is the evidenced dword at `+0x5A4`.

## Runtime outcomes

The UI reports configured state, hook-install state, and the last structured result separately:
`Unavailable`, `NoActors`, `Applied`, `Restored`, `OwnershipLost`, `InvalidConfig`, or `Fault`.
When profile/signature installation is unavailable, configuration editing and saving remain
available while Apply is disabled. `FFXHOOKS_VALIDATE_ONLY=1` skips the Difficulty runtime batch
entirely: it cannot create or enable these hooks, open Difficulty admission, or expose its RAM
writer.

ResolveEncounter, InitScene, and actor population install as one exact three-target
`Owner::Difficulty` batch. The process-global coordinator is initialized before create, owns every
queued operation and Apply, and opens Difficulty admission only after the whole batch applies.
A create failure before the first Apply boundary rolls back exact create-only targets; once any
Apply is attempted, all three targets, trampolines, the drain event, module base, and runtime state
remain allocated for process lifetime. Normal owner-thread teardown atomically closes admission,
neutralizes the exact batch, and uses reset-before-recheck callback draining. It never removes a
reachable target, closes retained storage, or races a RAM restore against a thread suspended in
the machine prologue before the first C++ callback increment. Disable Difficulty and use Apply Now
on the normal owner thread before teardown; if that restore reports `Fault`, retry it before
teardown. `DllMain` only requests admission close. Dynamic DLL unload is unsupported, and only the
shared coordinator owns MinHook initialization/queued Apply lifetime.

## Evidence level

Parser, transform, profile/signature, transaction, pointer-spy, source-contract, and consolidated
x86 Release build checks are RT0 offline evidence. RT0 also covers all nine evidenced HIGHLOW
operands at both preferred and relocated bases. An isolated x86 RT1 harness passed concurrent edit,
Apply Now, post-original-style initialization, reload publication, and teardown scheduling through
the actual production config APIs without a game process. It also covers deterministic partial
current-write retry for ON/OFF, maximum-failure recapture after intervening damage, healing, and MP
spend, ambiguous maximum/current readback, current-value ABA, and reported-success writes reverted
to their prevalue. The focused harnesses pass RT0 `353/353` and RT1 `60/60`; adjacent gates
pass config `24/24`, UI
`59/59`, observer `128/128`, protocol `76/76`, unsafe containment, and F8 `2949/2949`. The x86
Release DLL and lab copy are byte-identical, 1,296,896 bytes, SHA-256
`29078275E4EE3D9CD56F1A1CEF44420A84E1453B3A40E1308F90247A1EB3956C`. RT1 also models an entrant
paused before the first C++ callback increment to enforce process-lifetime retention.
Actual machine detour/prologue scheduling still needs its own RT1, and manual RT2 gameplay
observation remains pending. No RT2 or Production claim follows from these offline and isolated
results.

## Other F7 functions

### Force Last Battle

The read-only post-original hook on `FFX_Field_ResolveEncounterToken` at RVA `0x003828B0`
captures the field and group for a natural encounter. Requests call `MsBattleEncountExe` at
RVA `0x00380DE0` as `int __cdecl(int field, int group, float walkedDelta)` from the owner
thread. `repeatCount` is clamped to 1..9. Repetitions are scheduled one per menu-pump frame with
ten intervening frames, so this path never blocks the game thread with `Sleep`. A launch made by
another F7-family route uses a same-thread lexical capture scope and a correlated request token;
failure or cancellation leaves no sticky state that could hide the next natural encounter.

### Music

Track lock publishes `musicOverrideTrackIndex` and increments `musicSeq` through the
`FFXHooksBlock_v1` contract. Battle-entry selection prefers a configured playlist randomizer,
using `GetTickCount()` only to select a playlist index, then falls back to the configured battle
track. The pending music interceptor consumes that request once, expires it after 45 seconds,
and prevents it from leaking back into field music. Fade is clamped to 0..600 frames; a battle
entry uses 90 frames when no positive override is configured. These controls require the
separately gated Music hook.

### Configuration

The bounded JSON document stores the complete Difficulty preset and area rules together with
S.I.N., Force, and Music values. S.I.N. defaults to the exact root member
`"sinRam":{"enabled":false,"threatLevel":0}`. It is parsed independently: an invalid S.I.N.
member publishes only `sinRamValid=false` and OFF/T0, leaving valid Difficulty, Music, and Force
state intact. The sole existing F7 atomic saver emits the canonical member; no S.I.N. flag,
environment variable, process, sidecar, or `.bin` file participates. Difficulty presets are Off, Hunter, Sombra de Sin, True
Nightmare, and Custom. The preset identity, enabled bit, and editable values remain independent;
selecting Custom does not silently disable the feature. The eight-slot music playlist and Force
repeat count remain configuration-backed even where the native menu has no dedicated editor.
The `S.I.N. RAM` submenu has exactly Enabled, Threat, Apply Now, Status, Scope, Save, and Back.
Its only statuses are `INVALID`, `OFF`, `UNAVAILABLE`, `WAIT NATURAL`, and `CURRENT NATURAL`, and
its displayed scope is `Natural only; fields 310/340; 9 catalog IDs`.
Native save actions report the real atomic-write result: failure uses failure status/SFX and keeps
the menu open where closing would otherwise imply that the configuration was persisted.
