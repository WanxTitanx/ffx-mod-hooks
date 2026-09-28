# MOD-008 menu audio, native Status typography and MP balance

Jarvis-HOOK, 2026-09-28. Base `aa64b7480f1f917f2a993b0c45ae4b5289afbffb`;
branch `codex/mod-008-audio-mp-status-20260928`. Author review and RT0/RT1;
new audio/visual/gameplay acceptance remains a user retest.

The user accepted the prior interface and supplied screenshots of the working
Equip picker, locked slot and Status effects. The selected follow-up adds native
menu sounds, reduces easily repeated MP restoration, applies the native Status
typography to the title/effects, and shows equipped Tarot slots below Armor on
the main Status overview. Existing interface geometry outside those additions,
card identities, artwork and acquisition rules remain unchanged.

## Native audio feedback

The known menu sound dispatcher at RVA `0x486B00` receives the same IDs already
used by the native menu shell and Workshop: **1 move/confirm, 3 error, 4 cancel**.
The dispatcher preserves the game's normal mixer/volume path. No sound file or
external mod asset is added.

Feedback is chosen after one consumed private input and its transaction result.
Movement requires an actual row/page change; ignored modal navigation and idle
frames are silent. Picker entry, transfers, mode review and committed changes
confirm. Locked slots, unacquired cards, stale intent and rejected transactions
error. A valid retry clears an old error before opening its new prompt. Back
uses cancel. Delegated Weapon/Armor, native Back and character controls retain
their original audio ownership. Draw callbacks never emit feedback.

The private PE fixture executes the original dispatcher and observes its audio
backend at RVA `0x41E5F0`: channel 0, selected cue, pan63, volume127. It verifies
one cue per consumed input, cancel, locked error, acknowledgement, and no replay
from Draw. This is call/argument evidence, not a claim that speakers were heard.

## Native typography and overview row

Status effects now use the real ability-label renderer `0x505AB0` and its width
measurement `0x505290`, with native proportions and outline behavior. A uniform
fit factor per section accommodates long descriptions/lists; short lists keep
the native 0.78/1.0 scale. These calls do not enter the legacy small-font outline
suppression scope. The existing outline guard hooks `0x4FAE40`, so it does not
conflict with the new font evidence spans.

The original Auto-Abilities title is artwork in native atlas `0x2ECC`, rather
than a freely typeset string. The extension reuses its current caption ID from
context DWORD `+0x68`, through `0x4F8D50`, at the original 430x36 size. An
**Arcana** identifier beside it uses the native ability font. The heading band
is 48 high; effects start below it and remain inside logical Y1030.

The separate main Status overview draw is RVA `0x4D26E0`, registered by native
constructor `0x4D30D0`. It renders the original screen once, then appends a
read-only row of two or three slots according to the current deck mode. Labels
retain each card's traditional name while omitting the longer Spira subtitle;
Empty and capacity-Locked states are explicit and update with the character.

- Native equipment renderer `0x4D4510` places Armor at Y422, height60: bottom482.
- Native attributes renderer `0x4D5150` begins at Y552.
- Tarot row: **Y490, height44**, ending at534, inside that existing gap.
- It shares equipment's X470/width1240 region, split into equal columns with
  12-pixel gaps. Existing equipment/attributes are not moved.
- Context WORD `+0x52` and native animation helper `0x4D3090` provide the same
  integer horizontal offset, truncating `animation * 341 / 4096` toward zero.
- Names and small card marks use immediate native drawing; no retained artwork
  overlay can outlive the overview. Save-owner thread capture and actor bounds
  guard the addition; logical stop restores the native-only overview.

All addresses are x86 RVAs in the supported PE32 FFX.exe, preferred base
`0x400000`, image size `0x237D000`, SHA-256
`78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced`.
Signatures/relocations are generated from those exact bytes. Assembly and
private native execution support the calling conventions and geometry; final
appearance during live transitions/resolution changes is still a player check.

## MP balance revision 4

| Cards | Previous | Current |
| --- | --- | --- |
| Hierophant, Hanged Man, Nine of Cups | Defend restores10% MP | **3%** |
| Four of Wands, Two of Cups | Defend restores5% MP | **1%** |
| Hermit, Star | Restore5% MP per turn | **1%** |
| Queen of Wands | Restore3% MP per turn | **1%** |
| Death, Nine of Swords, Six of Pentacles | Kill restores10% MP once/action | **Unchanged** |

The native restoration formula, integer rounding, limits, KO checks and event
deduplication are unchanged. Same-kind restoration uses the strongest equipped
value instead of adding copies. MP cost reductions, item effects and all other
card effects are preserved, including Fool and World. Descriptions, gallery,
typed catalog and Status values derive from the same updated definitions.

All three deployed prior hashes are explicitly compatible: original v1
`53b3c692d78b846e0d2556d22717c0039f01f4d551fbcb3a56412724da4e163e`, v2
`b31ad58a8c3ee8e8989c3dea3487b418f11b3a3006974bb902a8e3134c489e75`, and v3
`c9a3b6349b274d2137e2b317eed244590e1920752849fbc72ce16a1bac51ce37`.
The identity digest, native-save hash, CRC and ownership/resource checks remain
required. Each native fixture retains the collection and equipped cards while
applying the new MP values, then saves the current pack identity normally.

## Verification and delivery

The [machine-readable receipt](../../research/mod_008_arcana/audio-status-validation.json)
pins source inputs, logs and DLL identity. Logs are retained under the worktree's
ignored `work/mod008/audio-*` paths.

- Portable normal and ASan/UBSan: **76,467 each**, including input sound policy,
  read-only slot summaries, complete labels and all legal long-list layouts.
- Native UI: **92 standalone / 94 with Workshop**, including the real sound
  dispatcher, native font/caption call arguments, overview geometry/animation,
  actor changes and stop behavior.
- Native combat72, assets12, runtime26/15/27/21/21/21, CRT24/19 all pass. Defend
  raises the100-MP fixture from50 to53; a turn raises it from50 to51; duplicate
  consumption/turn events and KO cases cannot repeat the reward.
- F8/Seymour/CustomMix regressions: **3,988/88/89**. The unchanged Workshop
  presentation implementation retains its preceding47,631-check result; this
  follow-up separately exercises the shared bridge in its94 native UI checks.
- Release Win32 MSBuild, direct MSVC build and actual DLL validation-only
  worker/loader smoke pass. No game entrypoint is launched by these fixtures.

All mod dependencies remain tracked in `ffx-hooks`; fonts, title artwork and
menu audio use the base game's own facilities, with no Spira Reforge dependency.
Both module/development defaults remain OFF in distributed settings. Author
review only, no subagents or Production claim. Earlier explicit instructions
authorize normal PR/merge and redeploy for user testing; deployment separately
requires a closed game, backup and readback while preserving user files.
