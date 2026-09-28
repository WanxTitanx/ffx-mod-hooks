# Main PR review packet

Jarvis-HOOK — source integration complete, publication authorized, final main
PR/merge awaiting the user's requested approval.

## Identity and scope

- Branch: `codex/elemental-native-consumers-20260928`.
- Reconciled main: `b6dd994c264f0df2451dceba1cf011017dcbea3c` (language PR13).
- Integration source commit: `a3b1407` (merge), preceded by `5bbf216` (inherited
  implementation plus Spira/Nul/Workshop corrections).
- Candidate DLL: 3,336,192 bytes, SHA-256
  `86370879d932adf0eac8f4b2ab9202f965dd3db9ee64ac4b1d138167ab63a74b`.
- All 457 captured native source/test/build inputs match the candidate manifest.
  The rejected-read follow-up and final receipt supersede the initial candidate; see the evidence record.
- Existing PR17 remains a draft against the intermediate integration branch.
  Retargeting/opening the main PR and merging are separate authorized-next-step
  decisions; pushing this branch does not promote or deploy the DLL.

## Suggested main PR title

Complete Vanguard, Elemental Dominion and paid Aeon/Spira integration

## Suggested description

Spira's Double/Triple Drop multiplier did not reach the post-roll reward
accumulator, and the legacy NulWard hooks competed with shared action/damage
ownership. Connect rewards at the verified native accumulator and move Nul
coverage/reservations onto shared Nul, action and actor lifecycle services.

Consolidate the existing Vanguard increments and complete the defined Elemental,
Spira and paid Aeon implementation: exact loaded-data admission, external
affinities/tactics/equipment/monster profiles, nonlethal Gravity, independent Magic
BDL, numerical Scan, dedicated fixed-price Aeon purchases and versioned receipts.
Keep every new gate OFF by default and preserve the native four-slot save layout.
Undefined Spira effects and unspecified Fourstrike/Fourtouch riders remain inactive.

Reconcile the current main language and Arcana work without competing hooks.
Share clamp, MP cost, critical and HP application; preserve temporary-stat save
projection, paid checkpoint atomicity, Ronso ownership and existing UI controls.
Rejected Workshop previews retain their cost/ingredient diagnostics.

Validation: full MSVC x86 Release, the final Windows native/offline matrices,
56 same-binary Proton cases including DLL loader/worker smoke, separate Arcana
combat72/72, Linux normal/sanitizer suites, language/authoring checks and context
tooling. Exact runners, counts and private receipt identities are in
`docs/research/INTEGRATION_FINALIZATION_2026_09_28.md`.

This is RT0/RT1 evidence. No installed DLL, game assets, personal save, Editor
checkout, live RT2 session or Production promotion is part of this PR. Independent
review is still required before merge; the author's inspection is not that review.

## Review focus

1. Shared callback ordering and exactly-once native calls in clamp, MP, critical,
   HP, Nul and action/result paths, including stop and in-flight callbacks.
2. Save projection/checkpoint order: paid state is durable before acceptance;
   other save observers receive the selected canonical image; Ronso binds the
   actual written bytes; failures cannot refund accepted costs or persist buffs.
3. Loaded bank/name/payload/remap proof, canonical owner/kind, logical fifth slot
   and paid receipt checks. Config or data drift must close only its admission.
4. Preservation of main's language/Arcana consumers, all previous F8 identities,
   old saves and independent feature controls.
5. Evidence boundaries: native fixtures isolate formula/graphics dependencies;
   live presentation, scene transitions, reflected/countered actions and player
   save acceptance require the separately authorized RT2 procedure.

The source-only audit found no new game/save binaries, private fixtures, native
DLLs, archives or credential patterns in the 273-path integration diff. Fixtures,
artifacts, replay drivers and logs stay under ignored `.superpowers/` directories.

## Reviewer commands

```sh
git diff b6dd994c264f0df2451dceba1cf011017dcbea3c...HEAD -- src/runtime/FfxHooksDll
python3 -m unittest discover -s tools/tests -v
python3 -W error::ResourceWarning -m unittest discover -s tools/context/tests -v
```

The native runners need the explicitly selected isolated Windows host and private
PE/kernel/save fixtures described in the evidence record. Those fixtures are not
repository dependencies or distributable release assets.
