# FFX Hooks current-state entry point

## Latest: F8/F7, ten elements and monster AP/Gil

Jarvis-HOOK implemented the user-selected fixes directly on main. The new menus,
keyed external affinities/Scan controls and per-monster AP/Gil settings passed
Windows validation and 22 same-binary Proton cases. Installed DLL: 3,398,144 bytes,
SHA-256 `734a0bf94b56157648e5391ca06dfb1c60aad2c6bb27748762a67315c98b8ffc`.
All 472 native inputs match the build. Source commit `dda5cb4` is published on
main. The DLL was atomically installed at 2026-09-28 22:28:57 UTC with a verified
backup; all 178 protected installed files were unchanged. The
[complete Editor dependency dossier](EDITOR_INTEGRATION_COMPLETE_HANDOFF_2026_09_28.md)
is delivered. See [this round's contracts and evidence](../research/F8_F7_ELEMENTS_MONSTER_REWARDS_2026_09_28.md).
Live RT2 acceptance remains pending. The earlier deployment record below is historical.

## 2026-09-28 — Consolidated main and deployment (Jarvis-HOOK)

PR #17 merged the selected Vanguard, Elemental Dominion, Aeon Ascension and
Spira Reforge work into main `14ae96bf9ad2921464880cd6be64607162082b8a`,
including the existing Arcana and MOD-006 language work. The merge tree matches
validated source `74162cbc9691570dbcbaba5f3975e04c30e5b490`.

The principal checkout's historical documentation and feedback assets are now
reconciled without introducing another runtime variant. See
[local consolidation and deployment](../research/LOCAL_CONSOLIDATION_DEPLOY_2026_09_28.md).

The consolidated x86 Release DLL is 3,336,192 bytes, SHA-256
`86370879d932adf0eac8f4b2ab9202f965dd3db9ee64ac4b1d138167ab63a74b`.
Its native inputs passed the Windows matrix and 56 recovered-binary Proton
cases, plus Arcana combat 72/72. This is RT0/RT1 evidence; live RT2 and Production
promotion remain separate. The user-authorized deployment completed at
2026-09-28 20:16:47 UTC. All 457 native input files match the validated packet;
the installed DLL hash matches, the previous DLL has a verified backup, and
178 protected installed files are unchanged. FFX was closed before the atomic
replacement. Existing feature configuration is preserved; the game was not
launched by this task.

See [final validation evidence](../research/INTEGRATION_FINALIZATION_2026_09_28.md),
[main review packet](../research/INTEGRATION_MAIN_REVIEW_PACKET_2026_09_28.md), and
[Editor contract](INTEGRATION_EDITOR_HANDOFF_2026_09_28.md).

## Historical evidence and navigation

`SESSION_HANDOFF.md` retains both previously local and published checkpoints.
Old prohibitions, approvals, pending implementation lists and deployed hashes
describe their original checkpoints; use current source and deployment receipts
for present status. Archived specifications and screenshots are input evidence,
not additional execution instructions or proof of implemented gameplay.

Historical reference only: the 2026-09-16 glyph/atomic-F8 candidate recorded
SHA-256 `64C27BA60FD089159039325CA47C24F1C385D608C483B996C802C8A8D023BE91`.
That older artifact is not reverified here and does not identify the current
installation. Live RT2 acceptance for the consolidated candidate remains pending.

Use `CONTEXT_MAP.md` for an unfamiliar subsystem and `CONTEXT_GUIDE.md` for
optional retrieval. Private build packets, proprietary fixtures, backups and
local orchestration state remain outside Git.
