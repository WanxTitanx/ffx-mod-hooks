# Vanguard / Elemental / Aeon integration finalization

Jarvis-HOOK — 2026-09-28. Continue the inherited implementation in
`codex/elemental-native-consumers-20260928`; prompts 00, 01, 03 and 04 are
requirements, not renewed permission to merge or deploy. MOD-006 is a separate
concurrent integration.

## Execution plan and gates

- [x] Connect Spira to the verified post-roll item accumulator. Preserve native
  quantity bounds, party maximum, unrelated buffers, AP/Gil and non-item awards.
  Reproduce the existing six failures before changing production code.
- [x] Move Radiant/Umbral Nul ownership onto the shared native Nul, action,
  damage and actor lifecycle services. Use external charges, full element
  coverage, result reservations and exactly-once settlement. Test both startup
  orders, cancellation, actor reincarnation, mixed elements and stop behavior.
- [x] Review all 81 inherited dirty files against the selected requirements;
  complete Workshop/save-flow/Nova/presentation and combined ON/OFF validation.
- [x] Build MSVC x86 Release, replay applicable native harnesses in isolated
  Proton, run offline project checks and prepare the Editor/RT2 handoffs.
- [x] Reconcile the completed language integration from current `origin/main`
  into a reviewable integration candidate without changing the user's main
  checkout. Revalidate affected surfaces and check the final comparison.
- [ ] Inspect an exact source-only staged allowlist, commit, push and confirm
  remote SHA/CI. Ask the user before opening the final PR or merging into main.

An independent review remains a distinct gate: this task has one authorized
agent. Author inspection is not an independent review. DLL deployment, a live
game session and Production promotion are outside this authorization.

## Evidence ledger

- Initial HEAD `17efc7313cdcf6be20439e57db43825096da5f54`; index empty.
  Preserved all inherited dirty files and their hashes under the ignored
  `.superpowers/finalization-20260928/` directory before editing.
- Spira RED: `finalization-spira-red-ffx-mod007-975af8d405b7`, standalone
  **52/58**, paid caps **363/363**, overall exit 1. The six failures reproduce
  the attachment; the reward multiplier has no post-roll caller.
- Verified private fixture SHA-256
  `78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced`, PE32.
  RVA `0x398AD0` is cdecl `(unsigned item, int quantity, void* rewards)`;
  native records at `+0xD4` (WORD IDs), `+0xE4` (BYTE quantities), count
  `+0xD0`, eight entries, quantity ceiling 99. Source of proof: private fixture
  disassembly plus the original native accumulator harness, not guessed bytes.
  Canonical reward buffer is module-relative `0x1F10EA0`.

- Spira GREEN: `finalization-spira-green-ffx-mod007-246c6b58b557`, **58/58**
  plus paid caps **363/363**. Root cause fixed at the original accumulator,
  with a verified entry signature and bounded positive quantity argument.
- Nul ownership RED: `finalization-nul-red3-ffx-mod007-d6ad531bc517`;
  old Nul-first installation blocked Elemental, Nova and shared action admission.
  Shared Nul/action/actor subscriptions replace the old overlapping PolyHook
  detours, writeback stub and timer-byte storage. NativeSlots/P16 legacy options
  retain their opt-in entry but use the same external charge implementation.
- Nul duplicate mixed-hit RED: `finalization-nul-mixed-red-ffx-mod007-30062c22d5fb`,
  **32/33 in both orders**. A native Fire charge was spent on initial computation
  and needed its existing result reservation reused on duplicate calculation.
  GREEN: `finalization-nul-build-ffx-mod007-5a1d7d52b41e`, **33/33 in each order**
  plus all eight Elemental modes. Cancellation, miss override, external ninth
  element, actor reconstruction, load reset and teardown are covered.
- Detach RED: `finalization-nul-detach-red-ffx-mod007-091a81f1be61`, one of
  4530 checks failed. GREEN: `finalization-nul-native-ffx-mod007-4350a26d135e`,
  **4530/4530**. DllMain closes the Nul gate with one lock-free boolean store.
- Transaction RED: `finalization-transaction-red2-ffx-mod007-b9b319d2108f`,
  **65/67**. The inherited Ascension wrapper discarded rejected preview costs.
  The wrapper now preserves diagnostics and restores the original after-state;
  the extraction harness includes the actual shared commit implementation.
- Remaining GREEN: `finalization-remaining-green-ffx-mod007-b424312a5164`:
  transaction **67**, store **97**, save flow **45 OFF/45 ON**, Ascension core
  **2604**, receipt store **29**, menu **29456**, presentation **3/61758/58167/64830**,
  numerical Scan **23473**, and all eight Nova modes. All passed; no check removed.
- Shared regression GREEN: `finalization-shared-regressions-ffx-mod007-fb8fddd4893d`:
  core/catalog suites, Vanguard **83**, shared affinity **13 in both orders**, actual
  Vanguard affinity **14 in both orders**, actions **25 in both orders**, damage
  composition **14 in both orders**. The final native Nul owner is additionally
  tested by the subsequent Elemental/Nul packet above.
- OFF/ON matrix GREEN: `finalization-mod-matrix2-ffx-mod007-d6969d193c15`:
  both OFF **4**, Elemental-only **14**, Spira-only **14**, both ON **16**;
  standalone Spira **58**, paid native caps **363**. The real adapters and shared
  producer/clamp execute; a controlled formula endpoint supplies the input damage.
- Linux `research/equipment_workshop/run_checks.py` with the explicit private
  save/PE passed normal and ASan/UBSan suites plus **48 Python tests**.
- Pre-main MSVC x86 Release candidate: **3123712 bytes**, SHA-256
  `5f1c9898422b9fe8c33b43b01c56c1dd9f6b5a78327ccf6021b3b4ccee9e95e7`.
  Three existing C4996 warnings remain. This is a build candidate, not an installed
  DLL, RT2 receipt, independent review or Production promotion.
- Main reconciliation target: language PR13 is MERGED at
  `b6dd994c264f0df2451dceba1cf011017dcbea3c`. Main import and affected revalidation
  follow the source checkpoint; the final main PR/merge still needs user approval.

## Reconciliation with current main

Main `b6dd994c264f0df2451dceba1cf011017dcbea3c` (language PR13) was imported
into this task branch after source checkpoint `5bbf216b77531d3e221de3311aebf15104990058`.
Fourteen textual conflicts were reconciled without discarding either lane's
features. The imported Arcana work was already on main; this task only composes
its existing consumers with Vanguard/Spira and the paid save pipeline.

- The integer clamp became `SharedClampRuntime`; MP cost, critical and native HP
  application became `SharedCombatRuntime`. Both use exact owned-byte/profile
  admission and stable original-call paths. Field/load/damage/turn notifications
  retain the new main observers and the existing Vanguard/Elemental scopes.
- `main-arcana-spira-red-ffx-mod007-348b6d757d91` and
  `main-vanguard-arcana-red-ffx-mod007-39c4b34c7d41` exposed the old competing
  producers. Fixture corrections supplied the native CRT imports and authoritative
  F8 choices; they did not weaken the native profile or behavior checks.
- `main-shared-only-red-ffx-mod007-e3a54c487ed3` showed that a shared provider
  must supply field/aggregate events even when Workshop editing is OFF. The
  producer now honors separately requested native gameplay observers.
- Paid checkpoint projection RED: `main-checkpoint-projection-red-ffx-mod007-e96985e6a624`,
  **72/74**. Checkpoints now project a private native image, seal it, prepare the
  other save owners, publish once, and finish their metadata transaction. Failed
  projection preparation rolls back the paid operation before acceptance.
- Ronso ownership RED: `main-ronso-projection-owner-red-ffx-mod007-80294474d7fe`,
  **32/33 ON**. Ownership previously hashed the pre-projection image. It now
  hashes exactly the successfully written bytes, preserving dormant charge.
- Multi-observer save dispatch retains one checkpoint selector. That selector
  receives the original disk anchor; other observers receive the chosen canonical
  checkpoint image. Linux boundary regressions pass **29/29**, projection **15/15**,
  gameplay events **5/5**. No store schema or native save record was expanded.

## Final source validation

Commands are checked-in runners invoked through:

```text
python3 tools/run_mod_runtime_checks.py --windows-host windows11-dev-next
  --test <runner> [--test <runner> ...]
  --ability-kernel <explicit-private-a_ability.bin> --label <receipt-label>
```

Final native source packets have identical per-file manifests:

- `final-main-combat-build-ffx-mod007-0fdb417271d6`: Arcana/Vanguard/Spira **37/37**
  in both orders; editing-OFF shared provider **34/34**; Nul **33/33** in both
  orders; Elemental OFF/Magic/Core/Tactics/Monster/Gravity/override/Equipment
  **4/27/34/86/20/23/23/29**; four independent mod combinations **4/14/14/16**;
  Spira **58**, paid caps **363**, Vanguard **83**, casting **16/18/18/19**,
  formation **29/29/39/39**, actions/buffs/follow/threaten/energy/shared-first
  **27/22/36/18/29/28**, OD **39/46**, shared damage **14 in each order**, all
  eight Nova modes, and full MSVC x86 Release. All passed.
- `final-main-save-ui-ffx-mod007-d6ff5472d902`: Ronso I/O **33 ON/27 OFF**,
  transaction **74**, store **97**, save flow **48 OFF/48 ON**, standalone/composed
  producer **39/37**, Workshop **82**, Aeon Workshop **55**, paid core/store
  **2604/29**, menu **29470**, presentation **3/61762/58171/64834**, numerical Scan
  **23477**, F8 **4584**, and the text-language synthetic/native runner. All passed.
- `final-main-independent-consumers-ffx-mod007-1a9597fee3c1`: the unchanged Arcana
  native combat acceptance suite passes **72/72** after shared-hook composition;
  shared action **25 in each order**, shared element **13 in each order** and
  actual Vanguard affinity **14 in each order** also pass.
- `main-composition-build-ffx-mod007-ee21fac01d6f`: the extended core runner also
  includes formerly orphaned Spira rules **183/183** and Scan view **29/29**;
  registry, affinity, pack, actor-state, proof and catalog suites all pass.
- Offline language checks: `python3 tools/text_languages/run_checks.py --output
  <private-output> --sanitizers` passes. No new private PT-BR pack acceptance is
  claimed by this integration; main's completed language delivery is preserved.
- Autoability author **13/13**; design-contract validator **124/124**. Linux
  Workshop normal/sanitizer suites and **48 Python tests** passed with the explicit
  private save/PE. Context tooling **114/114** passed before the final doc update;
  it is rerun for the final committed documents.

## Final candidate and same-binary Proton receipt

MSVC x86 Release candidate: **3,335,168 bytes**; SHA-256
`dea6462415f1ac33e8aca4942b2d6bc3a1cb7ff8a7dd36d5b0925cb4e9bf8245`.
Three existing C4996 warnings remain. The private DLL and 20 native test artifacts
were recovered with per-file and bundle SHA-256 verification. The additional
Arcana combat executable was independently recovered and verified.

Windows DLL loader/worker smoke passes with validation-only admission and
unchanged private PE boundaries. Proton 11 replays the same recovered binaries:
**56/56 cases**, including DLL loading/worker completion, plus the separately
recovered **72/72 Arcana combat** case. The final prefix is private to this task.
A direct wineboot prefix lacked three i386 vkd3d libraries; the matching files
from the same installed Proton 11 distribution were copied only into that prefix
and hashed. The candidate DLL was not modified. Two initial replay invocation
errors (missing harness argument and a pre-created directory in the no-I/O
control) were corrected without changing production or dropping checks.

Private receipts, manifests, command scripts and full logs are under
`.superpowers/finalization-20260928/` and `.superpowers/mod007/`. They are ignored
and excluded from commits. The Editor contract is
[the integration handoff](../ai/INTEGRATION_EDITOR_HANDOFF_2026_09_28.md).

## Review / promotion boundary

The complete source diff was inspected by the author, with fixes backed by
failing-then-passing tests. It is **not an independent review**: the current scope
explicitly authorizes one agent. Independent review remains required before main
merge. Opening/retargeting the final PR and merging main await the user's requested
approval. No DLL deployment, installed asset edit, personal-save edit, game launch,
RT2 observation, binary release or Production promotion occurred.

## Source delivery checkpoint

The reconciled source is committed as `a3b1407` after checkpoint `5bbf216`.
A full integration audit against current main covered 273 paths and verified
457 captured native inputs against the final candidate. No new game/save binary,
DLL, archive, private fixture or credential pattern was included.

The final source-packet test initially retained its historical blanket INI ban,
while native F8 source-contract tests now require the checked-in default template.
The test now requires exactly that one template and separately proves that local
and nested runtime INIs and private binaries remain excluded: **2/2 pass**. No
runtime behavior or candidate DLL changed for this test correction.

[The ready main PR description and review focus](INTEGRATION_MAIN_REVIEW_PACKET_2026_09_28.md)
are prepared locally. Git remote readback/CI determine the publication receipt;
main PR/merge still await approval and independent review.

## Rejected-read isolation: final superseding candidate

A final integration review found that the paid-load rejection path inherited
`ResetCompleted()`, while main's Arcana reset callback deliberately creates a new
session. The real private CRT/fread caller reproduced this distinction:
`rejected-load-red-ffx-mod007-df99befc1158` failed session-admission and retained
projection checks after reading an unrecognized save identity.

`NativeSaveEvents::ReadRejected()` now has an explicit optional callback. Ronso
uses it only for failed reads; confirmed native resets retain `ResetCompleted()`.
Workshop closes its admission. Arcana closes ready/bound state and pending reads
without clearing the prior collection or temporary-stat shadows. No failed read
can mint a fresh admitted session, and later serialization can still strip buffs.

GREEN: `rejected-load-green-final-ffx-mod007-ef1c830125c4` passes the real native
Arcana/Vanguard/Spira rejection paths **41/41 in both orders**, editing-OFF **38/38**,
Ronso I/O **33 ON/27 OFF**, save flow **48 OFF/48 ON**, Spira **58**, paid caps **363**,
Arcana combat **72**, and a fresh full x86 Release build. The affected recovered
binaries and updated DLL loader were replayed in the private Proton prefix; the
previously successful unaffected cases were retained, for **56/56 cases**, plus
Arcana combat **72/72**.

This supersedes the preceding DLL identity. Final candidate: **3,336,192 bytes**,
SHA-256 `86370879d932adf0eac8f4b2ab9202f965dd3db9ee64ac4b1d138167ab63a74b`.
Windows and Proton loader/worker smoke use exactly these bytes. No deployment or
live game was performed. The old candidate is retained only as historical evidence.
