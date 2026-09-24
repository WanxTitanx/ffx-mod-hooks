# RT2 protocol — in-game validation

RT2 is a reproducible live observation, not a synonym for a passing build. No source scan,
offline test, manifest, or successful DLL build promotes a feature to Production.

## Evidence levels

| Level | Meaning | Exit gate |
|---|---|---|
| RT0 | Offline structural/byte behavior | deterministic tests and static gates |
| RT1 | Isolated harness or layer without the game | family-specific harness result |
| RT2 | Reproducible behavior observed in the running game | case log + observations + restoration evidence |
| Production | Explicitly promoted RT2 result with release safety gates | separate promotion decision |

An RT2 evidence bundle uses an **integrity-bounded manifest plus SHA-256** for the recorded
files and log boundary. This is **not a cryptographic signature** and proves neither authorship
nor whole-machine state.

## Universal prerequisites

1. Obtain separate authorization for the live RT2 session and for any required deploy.
2. Close FFX before deploy. Deploy is a separate, reviewed action; a protocol script must not
   silently copy a DLL.
3. Use a disposable save and explicitly confirm it. That confirmation is human attestation, not technical proof that the main save or every game-owned file is untouched.
4. Close FFX Mod Studio / FFXProjectEditor and verify the process is absent; a confirmation
   checkbox never replaces the process check.
5. Arm **exactly one feature** (and, where applicable, one finite case) for the session.
6. Create a **case-specific touched-file inventory** before the run. Hash exact leaves, never
   broad directories, and state which runtime-only observations cannot be represented by hashes.
7. Capture timestamps, executable identity, DLL identity, config identity, and the starting
   byte offset/prefix hash of the relevant log.

A probe heartbeat is **family-specific**, not a universal F8 gate. Require it only for a family
whose diagnostic contract actually uses the probe (for example probe READ/CALL or Force Battle).
F8 uses its own catalog/edit/runtime anchors; SIN and other families use their own diagnostics.

## Session shape

1. Verify the case starts OFF after the real resolver (environment, INI, compatibility flags,
   then default), not merely after reading one INI line.
2. Launch the game manually and exercise only the selected case.
3. Record the case-specific observation window. Examples: battle turns for AI, area transitions
   for SIN, or the selected F8 edit/apply/off/restore sequence.
4. Close the game manually.
5. Reject log rotation, truncation, prefix drift, extra sessions, crash/exception/access-violation,
   ownership conflict, or restore-pending evidence.
6. Restore the inventoried vanilla scope and verify exact bytes/hashes. Describe that scope
   precisely; never generalize it to “the whole game is vanilla.”
7. Preserve the raw log slice, parsed verdict, before/after hashes, restoration verdict, and
   human observation attestations.
8. Promote only in a separate decision after the evidence is reviewed.

## Fastload Autosave startup RT2 slice (pending)

The automatic candidate has offline evidence only. The earlier observer's user log is
diagnostic evidence, not acceptance of this executor. Follow the universal prerequisites,
including separate deploy/RT2 authorization, a confirmed disposable save, and one feature
per session. The generic F8 LIVE case table does not include this restart-required feature.

1. With the game closed, record the exact DLL/executable hashes, `_isolated/ffx-hooks.ini`,
   `modules/config/f7_inlive.json`, relevant environment/marker resolver inputs, the exact
   `ffx_000` save leaf and any files the game can modify during the chosen window. Record
   the log's starting byte offset and prefix hash. A missing/ambiguous autosave path blocks
   the case; do not substitute a manual slot or choose by mtime.
2. Record an OFF control process first. Confirm the startup resolver reports Fastload OFF,
   no Fastload hooks/actions are installed, and normal title behavior remains available.
   Close that process and preserve its own log boundary.
3. For the separately recorded ON process, arm `development.fastload_autosave` through the
   existing resolver. Confirm the research observer override and global validate-only mode
   are absent/false, and Difficulty/other experimental cases are not armed. Launch manually;
   use no input to load a save. Do not attach a memory writer or force game states.
4. Require startup `observe=0`, scene/opening hooks ready, and an ordered, lossless
   `Fastload trace` sequence. Action mask `1` means opening Finish (optional if the callback
   was missed), `2` means publication of the native title Load choice, `4` means autosave handoff; `result=1` means
   accepted. Request and handoff must each occur exactly once, with increasing generation,
    after bootstrap scene 348 returns to title 23. The request must additionally
    show `activeScene=23`, `messageBank=0`, `choiceState=2` and bit `0x20` set in
    `choiceFlags`: scene23 alone does not prove the title startup worker has
    reached its input wait. Require the admitted answer buffer (`answerRva=0x01467940`
    or `0x01468300`), `windowPhase=3`, `answerState=1` and `answerCount=0` at action2.
    Subsequent edges must show the committed Load answer1/state2/count1 and closure
    of the title dialog before its script starts the native load screen. No direct
    out-of-band Load request is permitted. The handoff must still show `activeScene=23`, screen 2,
   UI 12, direction/dialog/selectLoad 0, and slot/page/row 0. Reject combined action masks.
5. Require subsequent vanilla read/close/transition and `phase=SUCCEEDED observe=0`, zero
   dropped edges, a non-title scene with `activeScene` equal to the saved scene,
   screen/selectLoad0 and a nonzero controlled character. A new load after the first
   read closed must end in `LoadInterrupted`, never credit a later manual load.
   Attest that field
   rendering, audio, movement and menu input work. Compare save bytes at field arrival
   before any natural autosave; stop before an action that can overwrite the disposable
   save. A hash captured only after closing the game cannot prove this earlier boundary.
6. Reject timeout, fallback, wrong slot, duplicate request/handoff, signature/thread
   conflict, crash/exception/access violation, missing/reordered edges, truncated/rotated
   log, additional process sessions, or executable/DLL/config drift. UI 16 must preserve
   vanilla's visible invalid-save dialog; do not manufacture corruption in the user's save.
7. In a separate approved Shift case, keep the gate ON, hold Shift at launch and attest
   that normal title is usable with zero request/handoff actions. Preserve and restore the
   exact inventoried configuration/save scope, then hash/read back each restored leaf.

Preserve raw log slices, observations, manifests and verdicts. An OFF/Shift pass does not
clear automatic load; a working load does not clear opening skip, Difficulty, other F8
features or Production. See [native request evidence](reverse/FASTLOAD_NATIVE_REQUEST_2026-09-19.md).

## CustomMix Ultra v1 user-run RT2 slice (pending)

CustomMix Ultra currently has RT0/RT1 and source/compile evidence only. This slice is intentionally
manual and remains pending; do not deploy or run it without the separate authorizations required
above. It validates only the RAM-only `dome02_00` transaction on the exact supported executable and
also covers the fixed x3/x4/x5 editor modes; it does not promote Seymour, Difficulty, or the DLL
as a whole. The old disk composer remains out of scope.

1. Record executable, DLL, F7 configuration, log-boundary, and touched-file identities. Require the
   Arena+ Master ON at startup, Compose F7 ON, and the Ultra hub row operational even with
   `f7.inlive=0`. Separately verify Compose OFF, Master OFF at startup, and validation-only
   cannot queue a Mix. Verify each undefeated boss shows OFF, defeat unlocks only that choice,
   and Bypass Progression can be turned ON/OFF without writing defeat flags. Fixed x3/x4/x5
   require their exact expanded count; Ultra still caps at eight. Record the two-line hub labels
   and matching mouse hit areas, selected-row retention, parent Back/Cancel, optional standard
   entry fees, and Music ON/OFF with its documented override precedence.
2. In the Ultra submenu, verify Auto Arrange, the per-slot X/Z editor and top view,
   Shift fine adjustment, Apply/Cancel, Native reset, library browsing and refresh,
   explicit JSON versus edited-binary import, export without overwrite, and keyboard
   rename/Cancel/focus loss. Exercise a legacy import and attest that the scenery warning
   is shown. Record the exact saved-bundle leaves touched by these explicit authoring actions.
   Verify mouse, arrows, scrolling, confirm, Remove Last, Clear, Back, Cancel,
   F7 close, and focus-loss cleanup. Record that Back/Cancel return to Arena+ without a black screen
   and that close/focus loss returns to the game with no later CustomMix consumption.
3. Exercise repeated choices and Magus. Before Launch, record the bounded eight-slot preview and the
   expected stable first-activation order. Reject an overflowing add and an empty Launch; verify the
   prior valid draft remains intact and failure feedback is audible.
4. Launch one finite selection. Capture the exact queue evidence in order: carrier helper call,
   return `-1`, game queue armed, then nonzero CustomMix generation armed. A missing or reordered
   fact rejects the run. The editor must close without cancelling that newly armed generation before
   battle-state consumption. Do not use a second launch, a raw ID, Penance, a scenario file, or a
   fallback writer.
5. At battle-state InitScene, use a non-mutating debugger/watchpoint capture on the borrowed carrier's
   exact 16-byte formation window. Prove the selected candidate is visible through Seymour-before
   (when Seymour is separately enabled for this check), the one vanilla call, and Seymour-after;
   prove the original game int return is preserved and the original 16 bytes are restored afterward.
   For the normal-program path, also record the temporary file-root/size loan,
   restoration of native allocation ownership, and the retained normal script,
   worker, formation and area references consumed after InitScene. Verify the
   scene map has no per-monster overrides and late formation reads contain every
   selected ID. In the user-facing battle, explicitly cycle through every visible
   boss and confirm each can be selected and damaged. Compare Arena-default and
   optional Tactical camera only in separately bounded cases. Inventory/hash the
   private normal-profile bundle alongside the DLL.
   Bootstrap, Sphere Grid startup, and unknown callers must show no CustomMix carrier probe or patch.
6. Repeat with one expired/cancelled request and prove the next battle is vanilla-only. Treat any
   restore conflict, crash, exception, access violation, duplicate vanilla call, missing vanilla
   call, stale-generation consumption, or post-restore byte mismatch as a rejection requiring a
   fresh process before further testing.
7. Close the game manually. Verify the exact touched-file inventory is unchanged; this RAM-only path
   must create no scenario, compose cache or save member. Explicit library actions may create only
   the selected preset JSON/native bundle, or replace its name metadata; inventory those leaves
   separately from the battle-launch case. Preserve raw logs,
   debugger evidence, UI observations, hashes, and the restoration verdict.

Queue-failure and queue-success/arm-failure branch forcing is **injection-only RT1**. The isolated
x86 adapter harness supplies deterministic queue results and proves respectively that the first
branch preserves and reopens the symbolic editor, while the second closes the editor and leaves the
already queued carrier vanilla. It also proves that the success branch closes without invoking the
request-cancel callback. Do not manipulate live queue gates, return values, runtime admission, or
timing between the helper and arm merely to manufacture these failures in RT2; such intervention
would be unsafe and would invalidate the observation. An organic live failure rejects that RT2 run
and may be retained only as diagnostic evidence, not as branch-coverage proof.

A passing slice is still RT2, not Production. Promotion requires a separate reviewed decision.

## F7 Monster AI observer RT2 slice

This slice validates observation only. `f7.aiswap` is OFF by default, the compatibility whitelist
is empty, and neither a proposal nor a dispatcher event is permission to alter a command, target,
actor, script, file, or save. Live observations remain pending until the user completes this slice;
the current profile, RT0/RT1, and build results are not RT2 or Production evidence.

The supported dispatcher is RVA `0x003AC9E0`. Preflight must retain the exact 66-byte signature,
its `HIGHLOW` relocation RVAs `0x003AC9E8`, `0x003AC9F6`, `0x003ACA06`, and `0x003ACA1E`, and
exactly these direct-call return RVAs: normal `0x003A454E`, force `0x003A4A60`, and death override
`0x003A4B8C`. A mismatch, extra caller, missing caller, non-unique signature, unavailable shared
MinHook coordinator, or status other than `OBSERVING` rejects the enabled run.

Run the following bounded sequence with a disposable save and only this gate selected:

1. With `f7.aiswap` resolved OFF, launch manually and capture a baseline battle. Require status
   `OFF`, no new `MonsterAiObserver OBSERVING` or `MonsterAiObserver dispatch` line, and the exact
   starting log/file inventory. Close the game manually.
2. Enable only `f7.aiswap`, relaunch manually, and require the profile-validation line followed by
   `OBSERVING (registration/cleanup/dispatch telemetry; mutation whitelist=0)`. Record one coherent
   registration generation before exercising dispatch.
3. Capture at least one normal-return event attributable to `0x003A454E`. Its reason must be a
   normal classification, not `unexpected_caller`, `force_dispatch_caller`, or
   `death_override_caller`. Then trigger and capture one `reason=force_dispatch_caller` event for
   return `0x003A4A60` and one `reason=death_override_caller` event for return `0x003A4B8C`.
   Caller addresses are deliberately absent from the value-only log; attribution therefore
   requires the exact three-caller profile plus its reason classification. If the selected
   encounter cannot reproducibly exercise all three families, record a partial verdict and do not
   promote the observer.
4. Prove repeated-event preservation by recording two dispatcher events with matching
   generation/slot/monster/command/target/force/reason/proposal fields and distinct increasing
   nonzero serials. Retain each queue readback independently; do not collapse or infer a duplicate
   from one line.
5. For one isolated event, use a non-mutating x86 hardware-breakpoint/trace capture to compare the
   physical shim entry with the original trampoline entry. Require the five cdecl DWORD arguments
   unchanged across that boundary: they must match bit-for-bit. Record exactly one original entry
   and return for that event, and require its returned `EAX` to equal the event's queue readback. If
   the trace cannot prove the original exactly once boundary, ABI transparency remains RT1-only and
   the RT2 verdict is partial.
6. Treat `proposal=1 proposed_command=...` as value-only telemetry, never as an applied command.
   Accept zero mutation only when the unchanged-argument trace, vanilla gameplay outcome, bounded
   no-pointer log schema, and exact before/after hashes for the inventoried game/save files all pass.
   This proves only the recorded slice, not that the whole process performed no writes.
7. End the enabled battle through the normal game-owned cleanup path. Require exactly one cleanup line
   for that battle, `[ffx-hooks] MonsterAiObserver teardown generation=<active-generation> thread=<thread-id>`,
   matching the registered active generation from step 2. Reject a zero generation, a mismatch, or
   duplicate cleanup lines for that generation. The cleanup must retire that generation: through the
   remainder of the enabled log, any later stale-generation dispatch must not be correlated or admitted
   as belonging to that retired generation. It must instead fail closed without the retired slot identity
   or belong to a distinct newly registered generation. Preserve the exact cleanup line together with
   its matching registration evidence. A missing, mismatched, or duplicate cleanup line forces a
   `partial/reject` verdict and blocks promotion.
8. Restore `f7.aiswap` to OFF, close the game, and relaunch once because applied trampolines are
   retained for process lifetime. Require status `OFF`, no new observer dispatch event, exact
   inventoried file restoration, and no crash, exception, access violation, ownership conflict, or
   restore-pending line.

Preserve the raw baseline/enabled/restored log slices, the three event-family excerpts, the repeated
event pair, the exact cleanup line and matching active-generation registration, debugger trace,
executable/DLL/config identities, touched-file hashes, and human battle observations. Missing evidence
yields `partial`, never a positive RT2 or Production result.

## F8 manual Preflight/Verify

`src/runtime/FfxHooksDll/run_f8_rt2.ps1` is a non-destructive gate, not an RT2 automator.
It has 14 finite cases and two phases:

- `Preflight` requires explicit disposable-save/editor confirmations, both processes closed,
  all 14 targets effective OFF, dashboard enabled, supported executable identity, matching
  built/installed DLL hashes, and a safe log boundary. It reads the INI once as bytes, writes an
  exact snapshot, and records all resolver sources in an integrity-bounded manifest. The
  PowerShell parser mirrors the runtime loader limits: input is nonempty and at most 65,535 bytes;
  at most 256 parsed pairs count duplicates; flattened keys are under 128 UTF-8 bytes and values
  are under 512 UTF-8 bytes.
- The human launches FFX, toggles only the selected row ON then OFF, records both observations,
  closes FFX, and does not ask the script to manage either process.
- Both phases derive the same default `work/f8_rt2` EvidenceRoot. A custom Preflight root must be repeated explicitly on Verify.
  The manifest records that root, and `EvidenceDirectory` must be
  a direct strict child of it with the exact `<timestamp>_<case>_<session>` leaf; session hex is
  lowercase.
- `Verify` requires that exact `EvidenceDirectory`; it never chooses “latest.” It checks the
  manifest/session/snapshot, unchanged DLL/EXE hashes, counter and byte-sliced log, then requires
  `ObservedApplied`, `ObservedRestored`, and explicit `RestoreConfigSnapshot` before a same-dir
  `CreateNew`/`Flush(true)`/atomic-replace restoration. It keeps the early fail-closed process
  gates and repeats the process query after the owned temp is flushed, immediately before
  `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`. A failed final recheck prevents the move,
  preserves the destination, and cleans only that owned temp. Verify then reruns all 14 OFF
  resolutions and verifies the final INI bytes/hash.

The snapshot uses the exact leaf `ffx-hooks.ini.snapshot.bin` directly under the Preflight
evidence directory. Its length and SHA-256 must match manifest `paths.ini` before any restore temp
is created. This unkeyed internal consistency check catches accidental or single-record drift; the
unkeyed SHA-256 sidecar is not authenticity. A malicious coordinated rewrite completed before
Verify can relabel a different INI identity unless an external trusted pin outside this protocol
and interface rejects it.

After restoration succeeds, Verify writes `log-slice.bin`, `log-slice.sha256`,
`parsed-log-verdict.json`, `before-after-hashes.json`, and `restoration-verdict.json`. The Seymour
case additionally writes the validated raw-memory record as `seymour-memory-evidence.json`. It then
writes the versioned `ffx-hooks.f8-rt2-verify-evidence/v1` final manifest with exact path, length,
and SHA-256 records plus the Preflight case/session link. Both Preflight `manifest.json` and
`manifest.sha256` are pinned by exact integrity records immediately after initial validation and
must remain unchanged before any final manifest or sidecar is written. Those records are derived
from the exact byte arrays already validated and parsed by the same read operation; no later path
reread creates the initial pins. The final manifest records both pins, its sidecar is written last,
and both Preflight files plus the exact five-artifact set (six for Seymour) are read back before
success is returned. This is integrity evidence, not a cryptographic signature.

The script never starts or stops FFX/editor, invokes Steam, sleeps for boot, or copies/deploys a
DLL. A directory created before a fail-closed final process recheck can remain incomplete; it is
not valid evidence because Verify requires the exact session suffix, manifest, SHA sidecar, and
snapshot integrity and refuses a missing/partial bundle.

### F8 log anchors

Within the post-offset slice, require in order:

1. `[ffx-hooks] F8 catalog rows=29 live=14 restart=8 not_wired=7`;
2. selected `F8 edit key=<canonical> edit=SAVED requested=1 effective=1 ...`;
3. `RuntimeAcknowledged` only: selected `[f8-runtime] ... state=applied`;
4. selected `F8 edit key=<canonical> edit=SAVED requested=0 effective=0 ...`;
5. `RuntimeAcknowledged` only: selected `[f8-runtime] ... state=restored`.

The 11 RuntimeAcknowledged cases need both runtime anchors and both human observations. The three
ConfigPolled cases (Speed Hack, Arena+ Compose F7, Dialog Skip) need the two edit anchors and
both observations. For Speed Hack, arm `boosters.speed_hack` and press Ctrl+Shift+K repeatedly to
observe the complete 1x → 2x → 4x → 8x → 1x cycle in the top indicator. Native 2x/4x must remain
ARMED and is not a per-scene application claim. Fast field scenes 8x must move from ARMED to
APPLIED only after its matching callback telemetry advances. Observe field gameplay, dialogue,
and a rendered field-scene sequence separately and record which paths accelerate; this observation
must not be generalized into a whole-engine guarantee. Pre-rendered FMV is unsupported and is not
an acceleration acceptance case. Verify restoration to 1x after the gate
is turned OFF and after focus loss. F12 must remain screenshot-only and must not alter speed.
Reject a wrong-case key, another target's ON/applied line, or an
unexpected effective source.

### Experimental Seymour battle-roster evidence

This is a battle-roster-only candidate, not full Playable Seymour. Use its dedicated
`seymour_battle_roster` case with a disposable save and capture raw memory snapshots for both
persistent lists (`D307E8` state, 3 bytes; `D307EB` ability, 17 bytes) and both battle-local lists
(`D2C895` state, 7 bytes; `D2C8A3` ability, 17 bytes). Record all four at each exact phase:
`before`, `on` immediately after entry, `afterSwitch` after exactly one legitimate Switch, `off`
after normal battle exit plus OFF, and `nextBattle` after entering the next battle.

Verify for this case requires `-SeymourEvidencePath <json>` in addition to the generic switches.
The JSON schema is exactly `ffx-hooks.seymour-battle-memory/v1`. Its `executable` record contains
the supported executable SHA-256 and the observed `moduleBase`; each leaf contains its declared
RVA, exact byte width, and computed `runtimeVa`, where runtime VA equals module base plus RVA.
Addresses use unambiguous uppercase eight-digit `0x` hex and every raw snapshot uses exact
uppercase two-digit bytes separated by one space. The top-level `captureSequence` is exactly the
five phases above, and every one of the four leaves must contain a value for every phase.

The exact boolean observation fields are `seymourSelected`, `controlledTurnCompleted`,
`switchCompleted`, `normalExitCompleted`, `nextBattleEntered`,
`nextBattleNoReintroduction`, and `sphereGridNotOpened`; every field must be explicitly true.
Missing fields, ambiguous bytes or addresses, an unchanged ON/after-Switch local record, a wrong
membership delta, or a duplicate actor ID 7 fails before the INI restore. At `off`, the persistent
lists must already have baseline membership without actor ID 7. The raw battle-local lists are
still mandatory, but the validator accepts either clean baseline membership or the exact owned
single-`FF`-to-`07` membership and records the latter as
`restore-pending-current-battle-retained`; it does not claim or force immediate local-buffer
cleanup. At `nextBattle`, all four lists must have baseline membership without actor ID 7, proving
that normal reinitialization did not reintroduce Seymour. The validated normalized record is sealed
into the final manifest as `seymour-memory-evidence.json`.

The case must show requested ON as pending until the battle-entry consumer acknowledges
application, actual Seymour selection and one controlled turn, one legitimate Switch, persistent
cleanup, normal exit, OFF restoration, and next-battle cleanup. Sphere Grid must not be opened:
the implementation deliberately rejects its caller and makes no Sphere Grid support claim.

Every selected ON/OFF anchor therefore requires `edit=SAVED`. Any non-SAVED F8 edit or non-`none` runtime `failure=`
record for any target rejects the slice, even when otherwise valid anchors
coexist. Production failure spellings include `read-failed`, `write-failed`, `readback-failed`,
`ownership-conflict`, `battle-gate-closed`, and `memory-span-invalid`.

Arena+ Compose F7 is **gate-only** in this protocol. Do not open/launch Compose or copy/mutate a
battle file. Functional Compose file mutation needs a separate touched-file inventory and RT2.

Current offline evidence: the PowerShell RT0 harness passes `76/76`, and all 16 one-at-a-time
mutation probes are discriminated. These results remain RT0; no F8 RT2 session or deploy occurred.

## Rules that never weaken

- Never run RT2 on the main save and never use `CreateRemoteThread`.
- Never corrupt the installed executable to test an unsupported signature; use an isolated
  harness.
- Never treat header/import/export inspection as proof that Windows loaded the DLL.
- Never use dynamic `FreeLibrary` as restoration evidence; hot unload is unsupported.
- Never relabel RT0/build evidence as RT2 or Production.

## Current family status

| Family | Family-specific diagnostic | RT2 status |
|---|---|---|
| Probe DINPUT8 READ/CALL | `ffxprobectl mon` / read/call contract | historical evidence recorded; refresh per release |
| Force Battle | probe `forcebattle`, disposable save | one scenario recorded; adversarial menu/FMV/battle pending |
| F7 difficulty/music | battle observation + family fields/logs | further RT2 needed |
| F7 Monster AI observer | three-target lifecycle plus normal/force/death dispatch, repeat, ABI/readback, and zero-mutation evidence | dedicated live slice pending |
| F8 dashboard | case-specific Preflight/Verify and F8 anchors | all 14 cases pending |
| SIN hook → injector | dry-run/exit codes plus area-transition evidence | pending |
