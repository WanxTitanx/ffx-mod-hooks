# Consolidated main and local deployment — Jarvis-HOOK

## Source and reconciliation

The user authorized finishing the selected integration, publishing it, merging
it into main, resolving the principal checkout's remaining changes, and deploying
the consolidated DLL. PR #17 merged at
`14ae96bf9ad2921464880cd6be64607162082b8a`; its tree is identical to validated
source `74162cbc9691570dbcbaba5f3975e04c30e5b490`. The three post-merge workflows
(build, context tools and text languages) succeeded.

The principal checkout started at `b6dd994c264f0df2451dceba1cf011017dcbea3c`
with three modified tracked documents, fourteen untracked files and an empty
index. All seventeen leaves were copied byte-for-byte before reconciliation;
the private `work/consolidated-deploy-20260928/initial-state.json` records their
hashes, and `initial.patch` preserves the tracked diff.

- `SESSION_HANDOFF.md` preserves both local and published historical entries.
  All three overlapping insertion blocks retain both sides.
- `MOD_IDEAS_BACKLOG.md` keeps main's newer MOD-004 refinement and MOD-005
  fifth-slot specifications. The stale local deletions remain recoverable from
  the private backup rather than removing completed design work.
- `CURRENT_STATE.md` points to the consolidated source and current evidence.
- The historical documents and eight original feedback images below are now
  versioned. Their wording is preserved, with trailing whitespace normalized in
  four Markdown files; the byte-exact originals remain in the private backup.
  These records do not supersede the
  implemented contracts or authorize additional work.
- `.omo/` remains in place and is ignored as local orchestration state.

Other worktrees retain their own changes. Obsolete interrupted merges and
separate recovery experiments were not added to this selected integration.

## Preserved historical inputs

- [External hook assessment](../ai/EXTERNAL_COMPARE_HOOK_OPPORTUNITIES_2026-09-23.md)
- [MOD-002 expanded concept](MOD_002_COMBAT_ENGINE_EXPANDED_SPEC.md)
- [MOD-002 original F8 prompt](MOD_002_COMBAT_ENGINE_F8_PROMPT.md)
- [MOD-002 original system specification](MOD_002_COMPLETE_SYSTEM_SPEC.md)
- [Workshop, Scan and status feedback with screenshots](../ui-reviews/2026-09-26-workshop-scan-status-feedback.md)

These records contain proposals and historical future-tense wording. Current
implementation and evidence are described by the [finalization record](INTEGRATION_FINALIZATION_2026_09_28.md)
and [main review packet](INTEGRATION_MAIN_REVIEW_PACKET_2026_09_28.md).

## Consolidated artifact and deployment

Candidate: MSVC x86 Release, 3,336,192 bytes, SHA-256
`86370879d932adf0eac8f4b2ab9202f965dd3db9ee64ac4b1d138167ab63a74b`.
The Windows matrix and the same recovered binaries on Proton passed 56 cases,
plus Arcana combat 72/72. Validation is RT0/RT1, not live RT2.

Deployment target is the Steam library's
`FINAL FANTASY FFX&FFX-2 HD Remaster/modules/ffx-hooks.dll`.
Deployment completed at **2026-09-28 20:16:47 UTC** from consolidation source
`3632f0dcb4cc33040243d3f7313826daa73a5e18`. Its `src/`, `research/` and `tools/`
trees are identical to the validated source. All **457/457 native inputs** match
the build receipt byte-for-byte. Twenty-seven unchanged checkout files used
CRLF instead of LF; after proving those differences were exclusively line
endings, they were normalized to the validated packet. Git reports no source
changes from that normalization.

The installed predecessor was 2,006,528 bytes, SHA-256
`a27431819328bf9a680dc70a28292bb89914460411faaa8137ddf2f2f8e4938e`.
Its verified backup is
`work/consolidated-deploy-20260928/deployment-backup/ffx-hooks.dll`.
The new DLL was staged beside the target, flushed, hash-checked and atomically
replaced only after a second process check. FFX and the Editor were absent.
Destination readback confirmed the candidate hash and 3,336,192-byte size.

**178 inventoried installed files are unchanged**, including configuration,
Workshop/Ronso sidecars, Arcana assets, game executable and loader dependencies.
The game executable SHA-256 is
`78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced`, matching
the isolated native-test fixture. Existing feature switches were not changed.
No live game was started; player RT2 acceptance and Production promotion remain
pending.

Private evidence: `work/consolidated-deploy-20260928/source-verification.json`,
`deployment-prepared.json`, and `deployment.json`. These receipts contain the
complete input/protected-file identities and the verified rollback path.

## Documentation validation

`python3 -W error::ResourceWarning -m unittest discover -s tools/context/tests -v`
passes **114/114** after restoring the required historical DLL anchor in the
short current-state entry point. Native checks are reused from the identical
validated inputs; no new runtime code or configuration was introduced.
After the completed deployment record was added, all 12 policy checks passed
again. Context retrieval passed 14/14 cases, and the bounded `DrawPromptGlyph`
bundle smoke check completed successfully. Cached-diff whitespace checks pass.
