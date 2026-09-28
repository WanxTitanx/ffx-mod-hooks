# F8 groups, ten-element controls and per-monster rewards

Jarvis-HOOK — 2026-09-28. Implemented directly on `main`, starting from
`a7c7836f5bbf6981192571aa56d335008779d0ad`, as requested by the user.

## Delivered behavior

- `F8 > Extras > Additional mods` contains the six Elemental Dominion,
  Spira Reforge and Aeon Ascension controls. Vanguard retains its separate menu.
- `F8 > Dev > FieldScout` contains Master, Heavy, Max and Ultra.
- `F8 > Cheats > AP/Gil Multipliers` contains the existing global controls,
  the default-OFF per-monster mode, a monster browser and direct monster-ID
  selection. The previous catalog indices are preserved; the new gate is appended.
- F7 shows all eight native element bits and two external elements. Both native
  Custom bits (`0x20` and `0x40`) remain independent of Scan's display order.
- Scan settings expose both external colors and visibility switches. F7 and Scan
  use the same registered identities and labels. An absent package is shown as
  unavailable, rather than assigning a fictional ninth native bit.

Opening a submenu changes no configuration. Back restores its parent cursor;
Cancel discards a staged multiplier/color edit. A failed save retains the prior
setting. Existing independent gates and tab-wide authority operations remain.

## F7 and elemental contracts

The original `elemWeak`, `elemResist` and `elemAbsorb` fields remain BYTE masks.
The optional `diff_elemExtra` global field and `elemExtra` area field contain at
most two entries:

```json
{"key":"mod.aether","affinity":1}
```

`affinity` is `0` unchanged, `1` weak (15000 basis points), `2` resist (5000),
or `3` absorb (-10000). Keys retain the package's stable, case-sensitive identity.
Duplicates, invalid keys/ranges, malformed arrays and excess entries are rejected.
No external element is stored in the game's native element BYTEs.

External selections are published only after a successful native Difficulty
apply/restore transaction. Merely editing or saving requested configuration does
not bypass that boundary. Publication is bound to the battle generation, actor
address and raw formation identity. Failed transactions, stale generations and
reused identities cannot expose a partially applied selection. The callback is
owner-thread gated and uses a nonblocking shared-lock attempt.

Elemental damage and numerical Scan consume the same applied selection. Explicit
package locks remain authoritative; Imperil/Ward calculations follow the selected
base. OFF/retirement removes the overlay without modifying monster files. Native
and external visibility/color settings do not alter gameplay affinity.

External Scan preferences use the flattened keys
`element_scan.hook.<stable-element-key>.rgb` (RGB24) and `.enabled` (0/1).
They are preferences for a declared identity, not a registry or gameplay enable.
The native palette retains its existing keys and migration defaults.

## Per-monster AP/Gil

The master is `cheats.monster_rewards`, with F8 authority marker
`f8_authority.monster_rewards`. Default is OFF. Enabling through F8 requires a
restart for installation. Once installed, saved individual rates apply on the
next reward calculation; disabling closes logical admission. General AP/Gil
controls keep their existing canonical keys and 1–100 range.

Each monster has an independent integer AP and Gil multiplier from 1 to 1000;
both default to 1. AP applies to the normal or Overkill amount selected by the
native routine. Every actor of the same monster file ID shares that rate. Direct
ID selection supports `m000` through `m4095`, including IDs absent from the name
catalog. Native identity must have the `0x1000` monster tag: for example `m342`
corresponds to raw WORD `0x1156` / decimal 4438. Player IDs and untagged aliases
cannot receive these overrides. The adapter covers the eight validated enemy
slots 18–25 and verifies the canonical actor address.

The preview shows original, individual and configured total, including a separate
Overkill AP line. Native character bonuses are explicitly outside that preview.
The original values come from an installed unpacked monster file or an observed
actor/reward view; an unavailable base is identified as such. Transient upstream
profiles may supply a different live reward view. The runtime always uses that
actual view, not the preview cache.

### Arithmetic and executable evidence

Supported PE32/i386 executable SHA-256:
`78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced`.
Addresses below are RVAs, preferred image base `0x00400000`.

- Native parent `0x3990E0` reads unsigned WORDs: Gil at loot `+0`, normal AP
  at `+2`, Overkill AP at `+4`.
- Existing global AP/Gil multiplier sites remain `0x399123` and `0x39913E`.
- The new, separately owned seam is `0x399144`, after widening/global calculation
  and before the per-character AP loop (`0x398A10`) and Gillionaire stage.
- The six-byte native span is `8B 7D FC 56 33 F6`. A 32-byte signature
  and executable identity gate installation. The pinned trampoline is published
  before enabling the hook. The shim preserves flags/registers and replays the
  covered instructions.
- It verifies the observed general factor, then recomputes
  `original × individual × general` using 64-bit intermediates. It replaces only
  the two 32-bit stack locals (AP at EBP-4, Gil at EBP-8). Monster reward WORDs,
  loot data and actor records remain unchanged. Unknown foreign transformations
  are left on their original route.
- Before vanilla bonuses, AP is bounded to **382,494,549**, Gil to **573,741,824**.
  These bounds preserve signed arithmetic with the existing 999,999,999
  accumulator and maximum native factors 3/2. The preview marks capped results.
- The per-monster seam is installed during worker preparation before the F8
  booster adapter starts. Its code page is not repatched while global multiplier
  updates run. Stop closes lock-free gates; the pinned original gateway remains
  safe for process lifetime.

Arcana's existing post-native AP/Gil adjustments and S.I.N.'s upstream reward
view are preserved. Item-drop quantities, rewards outside the monster routine,
save formats and native inventory records are not rewritten by this feature.

### Dedicated settings file

Individual rates live in `monster-rewards-v1.tsv`, beside the active Hooks INI.
The main INI's 256-key limit cannot hold the entire monster catalog, so the rate
table has its own bounded format:

```text
ffx.monster-rewards.v1
1	2	3
342	5	1
```

Columns are decimal monster file ID, AP multiplier, Gil multiplier; separators
are literal tabs and each line ends in a newline. LF and CRLF are accepted.
All 4096 IDs fit the 65,535-byte limit. Duplicate IDs, malformed rows, invalid
ranges and truncated records reject the entire table. Missing rows are neutral.

Worker preparation loads the table. Explicit F8 Save compares the original file
snapshot, writes and flushes an exclusively created sibling temporary, checks
the snapshot again, atomically replaces the file and verifies readback. Foreign
edits and failed replacement are preserved. No reward callback or frame pump
performs file I/O. External edits require a restart; the Editor must not write a
native WORD to represent an amount exceeding 65535.

## Provenance

The factual 347-entry ID/name table was adapted from the current Editor file
`FFXProjectEditor/FfxLib/Dictionaries/Monster_Dictionary.cs` on 2026-09-28.
Both repository licenses are GNU GPL version 3. Only ID/name pairs were adapted;
the generated include records the exact source SHA-256. Monster container offsets
were cross-checked against `Monster_Structs.cs`, `Monster_File.cs`, `Monster_Loot.cs`
and the actual supported executable. No proprietary binary enters Git.

## Validation and artifact

Candidate DLL: **3,398,144 bytes**, SHA-256
`734a0bf94b56157648e5391ca06dfb1c60aad2c6bb27748762a67315c98b8ffc`.
All **472 native source inputs** match its immutable build packet.

Windows evidence includes reward arithmetic/settings 45/45; native reward modes
OFF 6/6, invalid profile 4/4, enabled 25/25; native menu 29536/29536; F8 4595;
F7 core 4020 and F7 native 176. Elemental native cases cover eight modes, including
actual damage and Scan consumers, generation/ownership retirement and independent
external identities. Presentation cases pass 3 / 61762 / 58171 / 64834 / 23477.
The same recovered binaries pass **22 Proton cases**, including DLL loader/worker
smoke. Tool packet tests pass 2/2. Three pre-existing MSVC C4996 warnings remain.

Commands: `tools/run_mod_runtime_checks.py --windows-host windows11-dev-next`
with the checked-in F7, F8, menu, Elemental, presentation, monster-reward and full
candidate build runners; `g++` reward-core checks; `python3 -m unittest discover
-s tools/tests -v`; and the hash-verified private Proton replay script.

Private receipts and logs are under `work/f8-f7-elements-20260928/` and
`.superpowers/mod007/`. This is source, RT0 and RT1 evidence. Deployment and the
current installed hash are recorded separately after replacement; live RT2/player
acceptance and Production promotion are not established by these tests.


## Completed deployment

Source `dda5cb45305448d761e5412f3b265e58213d8ce2` was committed and pushed
straight to main. At **2026-09-28 22:28:57 UTC**, the candidate above replaced
`modules/ffx-hooks.dll` in the Linux Steam installation. Both process checks were
clear; atomic replacement and hash/size readback passed. All **178 protected
installed files** were unchanged. All **605 packet source inputs** (472 native
inputs) were also verified against the current checkout before installation.

The predecessor SHA-256
`86370879d932adf0eac8f4b2ab9202f965dd3db9ee64ac4b1d138167ab63a74b`
is backed up at `work/f8-f7-elements-20260928/deployment-backup/ffx-hooks.dll`.
The complete private deployment receipt is
`work/f8-f7-elements-20260928/deployment.json`. Existing configuration was
preserved; the new per-monster gate was not silently enabled. No live game was
launched by this task. The requested complete Editor dossier follows this
successful deployment.
