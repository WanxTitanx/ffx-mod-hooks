# Six Nuls and large integration — Jarvis-HOOK

User-authorized scope: inspect/reuse existing commands, create all six Nuls and
private animation clones in Steam, Spira Reforge and FFX Extracted, integrate the
large published branch into main through PR/merge, and deploy the combined fixes.
No automatic save unlock, game launch or Production promotion is implied.

## Source composition

- Main baseline: `8cb2c7556311f97d36935c064625cb221c0b03ad`.
- Published integration: `feba2609f12f2bd4bc596ecbf810fd886175a4b8` on
  `jarvis/hooks-main-integration-20260928`; it contains the older modular lane.
- Before composition: 17 main-only and 88 integration-only commits; 686 changed
  files, including 505 research/evidence files and 126 runtime files.
- Seven textual conflicts were resolved. Main's hook-owner and native-page
  ordinals, Arcana elemental strikes, element aliases, rewards and weapon VFX are
  preserved; recovery owners and Photo/Seymour pages append without reindexing.
- The other worktree has uncommitted Grid8 restoration work. It is not copied,
  staged, committed or represented as tested here. The published persistence
  groundwork is included; this is not full live eight-character acceptance.

## Delivered checks

- [x] Inspect all six active `jppc`/`new_uspc` banks in the three requested roots.
- [x] Reuse 320/321 and append 370–373 without altering other command records.
- [x] Create six private DLL/texture clones at verified-free magic IDs 870–875.
- [x] Implement default-OFF Nul gate, save-bound menu learning and private charges.
- [x] Validate native/extra mixed coverage, consumption, OFF and lifecycle behavior.
- [x] Resolve and validate shared recovery/main ownership and UI integration.
- [x] Build the complete DLL and verify all 355 production inputs against its manifest.
- [x] Install/read back 169 leaves with exact backups; preserve configuration/saves.
- [x] Update English/PT-BR README, checked roadmap and complete Editor contract.
- [x] Publish and merge PR #24 after all three hosted checks passed.
- [ ] Record live visuals, learning/load, party casts and battle/save transitions.

## Command and animation identity

The reviewed input has 370 96-byte rows, 52,297 bytes, SHA-256
`9df18bbe9d1b2744e875987bf1cdb29d22c595c282689b66250b597d1b002ca5`.
The result has 374 rows, 53,185 bytes, SHA-256
`fdf70cbaa6a5c43948931e089328a0b4601fe9845b626afa60fe5bbd43a0802d`.
Only old rows 320/321 change; 370–373 append. The other 368 records and entire old
text-pool prefix are unchanged. White Magic+ 366 and Aero/Aerora/Aeroga 367–369
remain intact. Other locales, backups and FFX-2 are not targets.

| Command | ID | Magic | Protected identity |
|---|---:|---:|---|
| NulHoly | 320 | 870 | Holy `0x10` |
| NulShadow | 321 | 871 | Darkness `0x80` |
| NulEarth | 370 | 872 | Earth `0x20` |
| NulWind | 371 | 873 | Wind `0x40` |
| NulPoison | 372 | 874 | `hook.custom03` / `spira.poison` |
| NulGravity | 373 | 875 | `hook.custom04` / `spira.gravity` |

Yuna-only, White Magic+, party target, 2 MP, one hit, no native damage/status
payload. `elemental.nul_spells` is independently OFF by default, restart required.
Learning uses the existing save/character-bound sidecar. No Grid node or save is
edited; commands must be learned through authored nodes. The submenu root is
derived from learned children, not persisted as an automatic unlock.

Each spell grants one private charge per affected ally. Native BYTE element
masks and Nul/Haste/Slow status counters retain their widths and ownership. Two
high masks exist only inside the hook. Mixed attacks need full coverage before
charges are reserved; partial coverage spends none. Poison means the elemental
identity, not the Poison status. Aliases do not rewrite commands or item abilities.

The six animation DLLs clone safe Holy 146, already used by the disabled Wards;
only three UTF-16 identity strings change and `.text` stays identical. Each has
eight private DXT5 texture files tinted through every mip. Older Editor evidence
records a crash path with status-less native Nul donors, so those are not reused.
See [recipes/provenance/install/rollback](../../tools/nul_elements/README.md).
No proprietary game binaries enter the source commit.

## Fresh validation

| Surface | Command/result | Evidence limit |
|---|---|---|
| Authoring and install | `python3 -m unittest discover -s tools/nul_elements -p 'test_*.py' -v`: 12 tests | Drift, ID collision, rollback and byte preservation |
| Actual menu bodies | Same suite: 43/43 | Real learned core; simulated native resolver |
| Canonical Nul rows | `NulElementCommandsRt0`: 42/42 | Portable C++ |
| Native Nul | `nul_ward_composition_rt1.ps1`: 40/40 in both installation orders | Isolated Windows x86/PE |
| F8 | `f8_runtime_rt0.ps1`: 4,621 | Catalog/config contracts |
| Workshop/F8 menus | `equipment_workshop_menu_rt1.ps1`: 29,614/29,614 | Actual menus, simulated rendering; recovery endpoints tested separately |
| F7 | `f7_runtime_rt0.ps1`: 4,020; `f7_config_rt0.ps1`: 25 | Core and config |
| Elements | Full `elemental_runtime_rt1.ps1` mode matrix; composition 4/14/14/16 | Isolated consumers |
| Ronso I/O | `ronso_pool_io_rt1.ps1`: 33 ON + 27 OFF | Actual adapter |
| Arcana/Spira | `arcana_spira_composition_rt1.ps1`: 43/43/40/39 | Four modes |
| Recovery portable | 19 selected suites; read-start follow-up 32/32; startup 21/21 | Actual bodies with explicit boundary doubles |
| Seymour | `run_seymour_adapters.py --adapters gear overdrive session menu sort`: 172/172 cases | Actual x86 adapters, simulated native endpoints |
| Grid8 | `grid8_persistence_tests.py`: 25 cases / 2,146 checks | Real registry, hashing and Win32 disk; native state endpoints simulated |
| Grid store | `sphere_store_disk_tests.py`: 88/88 + 33/33 | Private disk and injected faults |
| Native machine | `native_machine_tests.py`; `sphere_draw_tests.py`: pass | Exact PE, synthetic capture counters; 861-element bounds |
| Context | `python3 -W error::ResourceWarning -m unittest discover -s tools/context/tests -v`: 114 | Offline repo policy |
| Editor oracle | Reads all 374 commands and six animation roots | All records/referenced scripts round-trip; writer zeros only 149 orphan text bytes, not installed |
| Proton | Same MSVC Nul binary in both orders; same menu binary; final DLL loader/worker | Private prefix and mapped PE; no live game |

Superseded diagnostics remain retained: the raw append probe reproduces the
constructor-count overflow used as a negative control by `sphere_draw_tests.py`;
its capture-aware positive cases pass. The fresh Proton prefix initially lacked
three matching vkd3d dependencies (loader error 126); copying those into the
private harness directory resolved it without changing the production DLL.

Full build: `mod007_candidate_build.ps1`, lane
`C:/VMTasks/ffx-mod007-4adab6ca347b`. DLL: **4,011,008 bytes**, SHA-256
`5f6a76f3260a1aa0f5a36e0ff6e9f641f7d92e0dda969910d0d66a2debc3016e`.
All 355 production inputs match `.superpowers/mod007/nul-final-candidate-ffx-mod007-4adab6ca347b/receipt.json`.

## Installation — 2026-09-29 10:28:24 UTC

The game and Editor were closed. One backed-up transaction published/read back
169 leaves: six command banks, 162 new FX files across Steam/Spira/Extracted, and
`<Steam game>/modules/ffx-hooks.dll`. All **1,116** inventoried protected leaves
matched before/after. Saves, configuration, vanilla donor and existing Aero FX
were preserved. The old DLL SHA-256 was
`7cda5bb76128a45e6063da0c6bb238d1c78528c461b67858c1e65d1ec7cedbbd`.

Ignored backup directory:
`work/nul-elements-integration-20260929/deployment-20260929T102818Z/`.
`installation.json` binds every original/new leaf and backup. Protected inventories
and `delivery/installed-receipt.json` bind the deployed runtime. Exact rollback
must preserve any newer concurrent file. No feature was enabled, command learned,
save rewritten or game launched by this installation. Visual/battle/save RT2 and
Production promotion remain pending.

## Published merge receipt

[PR #24](https://github.com/WanxTitanx/ffx-hooks/pull/24) merged at
**2026-09-29 10:48:05 UTC**, commit `48243b11f5fafe0be4cfe5c62b7677cd45ea0f84`.
Its head `2787386a7144af04c16f194029a4a35616bf8dea` is the source-bound implementation. The merge tree is
byte-identical to that tested head; both original main and `feba260` are verified
ancestors. The older modular integration is also contained by ancestry.

Hosted checks on the exact PR head: `build-hooks` SUCCESS (6m59s),
`context (ubuntu-latest, 3.13)` SUCCESS (44s), `portable-contracts` SUCCESS (1m7s).
The source and docs pass cached whitespace checks. Seventy-nine raw historical
report files retain their published whitespace unchanged, preserving evidence
hashes. Complete staged content was inspected for conflict markers, credential
patterns and accidentally tracked game/build binaries; none were found.

Primary checkout advanced to main without reset or cleanup. The other task's
dirty Grid8 work remains untouched. Publication receipts are retained beside
the ignored installation/build evidence. This documentation-only follow-up does
not change the installed DLL or its 355 matching production inputs.


## Public release — 2026-09-29 11:18:57 UTC

[v0.6.0-beta.2](https://github.com/WanxTitanx/ffx-mod-hooks/releases/tag/v0.6.0-beta.2) is published as a beta, not Production promotion.
Public source/tag: `0c454b527e731b38aa1270d742908cdcfc8be78c` in
`WanxTitanx/ffx-mod-hooks`. English/PT-BR READMEs and release notes include
support/donation links. The runtime DLL is byte-identical to the installed
candidate. Four uploaded assets were read back through GitHub's SHA-256 digests:

| Asset | Bytes | SHA-256 |
|---|---:|---|
| `ffx-hooks.dll` | 4,011,008 | `5f6a76f3260a1aa0f5a36e0ff6e9f641f7d92e0dda969910d0d66a2debc3016e` |
| `ffx-hooks-release-v0.6.0-beta.2.zip` | 243,948,596 | `04442b47c0284929c8dadbe951e838c0751092887637e66ae211e0afe93d7d1d` |
| `ffx-hooks-source-v0.6.0-beta.2.tar.gz` | 251,997,044 | `f5d8af732970d793b2b927fd4510a8f19f68ce46c029aec86370c7f32f386bd7` |
| `ffx-hooks-v0.6.0-beta.2.sha256` | three archive/DLL entries | GitHub digest verified against the local checksum file |

The binary ZIP has 102 verified members, including 78 cards and two shared
Arcana images. The source archive contains exactly 1,263 tracked public files.
All 363 C/C++/include/resource/definition inputs match the recorded candidate
packet, including the Workshop dependencies; the separate 355-input runtime
count above also includes project files and uses a different scope.

Public-copy checks: normal/sanitized Workshop; 48 Python checks with two optional
private fixtures skipped; all 12 sanitized Arcana cases; language core 2,109,
payload 73 and field 48 plus Python contracts; 12 Nul tests (menu 43/43); 122
Sphere helper tests; two source-packet tests; negative packager checks and complete
archive readback. No private pack or live game session was supplied/run.

The mirror commit deliberately uses `[skip ci]` to reuse byte-identical native
validation and avoid another paid hosted build. No branch protection/ruleset
required extra statuses; public portable/release checks ran locally. Private PR
and main hosted checks completed successfully. This does not waive RT2 or
Production gates. No proprietary command banks, animation DLLs/textures, loaders,
saves, private fixtures or raw development report archive are distributed.
