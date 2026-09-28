# Jarvis-HOOK — native Ultra catalog, Arena progression and glass restoration

## Selected scope

The user confirmed the previous overhead camera worked but was too high, supplied
screenshots showing opaque menu rows and the F8 music OFF override, and explicitly
selected bringing Ultra categories and unlocks into the game's F7 menu.

This native step includes the categorized roster, current-save progression,
soundtrack choices, formation management, and the existing arena/position/library
flows. The browser prototype's 91-map expansion and arbitrary per-monster stat
editor are not native runtime features in this step. No new hook or save writer
was introduced. Existing signature, owner-thread, generation, capacity, restore
and teardown boundaries remain active.

## Catalog and progression

`ArenaMonsterCatalog.h` contains 344 choices representing 346 dictionary entries.
260 choices are admitted; 83 special/uncertain actors and Penance remain displayed
as **Encounter only**. That label does not mean a progression lock. Admission is
not a claim that every monster's native AI works in every scene or combination.
Dark Magus Sisters remain one three-slot choice. Eight expanded slots is the limit.

Six native categories retain the draft when returning to the Ultra editor. The
formation screen removes an activation, including a complete Magus group. Ordinary
monsters share the existing normal-Mix readiness rule: a current-save Dark defeat
or explicit Bypass Progression. They do not require individual capture counts.
The 35 Arena creations additionally require their own actual Arena unlock byte.
Dark Aeons retain their individual defeat checks. Bypass Progression intentionally
overrides these progression requirements, but cannot admit unsupported actors.

| Data | Address form and width | Behavior |
| --- | --- | --- |
| Captures | RVA `0x00D30C9C`, `uint8[104]` | Read for the `C:` count beside catalog entries; never written or used to lock ordinary fiends. |
| Creations | RVA `0x00D30D04`, `uint8[35]` | One nonzero byte unlocks only its mapped creation; unreadable bytes remain locked. |
| Monster identity | native `0x1000 + dictionary index`, uint16 | Closed catalog translates symbolic tokens; arbitrary native IDs are rejected. |

Executable identity: FFX.exe PE32/I386, preferred base `0x00400000`, SHA-256
`78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced`.
The current in-memory save is sampled when building the menu and again before
launch; no Dark Rematch disk cache contributes to Ultra unlocks. A new save cannot
inherit a stored unlock mask. Capture thresholds are not guessed: a creation opens
when the game sets its actual unlock flag, including any Arena NPC progression.

Claim → all 35 creation mappings have a distinct source-backed byte index.
Evidence → `arena-unlock-audit.json`, the Editor's ArenaTracker bindings and Monster
dictionary, plus 574 portable policy checks including missing reads, unrelated bits,
shared unlocks, bypass and invalid tokens. Confidence → high for metadata/policy;
live progression across save loads remains user acceptance work. Next → test with
Bypass Progression OFF and compare one locked and one unlocked Arena creation.

## Visuals and music

Glass role fills return to alpha `0x68/0x38`; selected fills use `0x88/0x58`.
Categories have distinct subdued tints, while the static cursor/rail/underline and
reduced-motion-aware 2.4-second glow remain. Native visual acceptance is pending.

Tactical opening distance changes from 450 to 340 units, elevation from -65 to -55
degrees; polar distances are bounded to 340–600. All nine Arena-default profiles
remain byte-identical. The user's supplied overhead screenshot supersedes the
previous uncertainty about whether the candidate produced an overhead view, but
does not validate this closer framing.

Ultra exposes the 92 existing named soundtrack IDs. A Challenge (145) is default.
Launch passes the selected ID through the existing MusicHook pending mechanism.
The music gate still applies and the editor labels it `[OFF]` when disabled.

F8 previously persisted ON while `arena_plus_music.flag.off` continued to override
it. An explicit single-row Arena+ Music ON now archives exact feature-specific OFF
markers in the four existing lookup locations as `.f8-on-*.bak`, then persists the
canonical setting. An INI failure restores the markers; an archival failure reports
failure. Global `music.flag.off` and disable-environment controls retain priority.
Startup and bulk-enable actions do not perform this migration. Restart remains the
advertised activation requirement. Installed settings are not silently enabled by
the deployment itself.

## Saved-file compatibility

New exports use `ffx-hooks.arena-mix` v3, symbolic `monster_009`-style catalog keys
and `music_track`. The original eight Dark keys and v1/v2 JSON still import.
Extended rosters select Ultra rather than silently changing normal Mix's Dark-only
scope. Native edited-binary import preserves the JSON soundtrack.

Older Tactical `.bin` exports initially failed because camera constants/offsets
changed. Regression: 1 failure / 48 library checks before the compatibility fix.
The importer now recognizes the two previously deployed normal-profile revisions
by exact normalized SHA-256. Only roster IDs and monster X/Z are excluded from
that fingerprint; the imported values still pass catalog/order/bounds validation.
The old camera/script is never copied into the runtime. Unrelated native edits
remain rejected. Ordinary profiles need no compatibility exception.

## Validation and limits

- Camera regression: old framing failed two of three checks; new bounds pass 3/3.
- Portable core 907/907 and progression/catalog policy 574/574 passed.
- Windows runtime 89, F7 3618, F8 3575, UI 59 plus adapter contracts, scenery 141,
  containment and Release build passed. Initial F8 failure was an overlong help
  subtitle; the corrected text fits the existing 63-byte limit.
- Native mapped-executable consumers: 1796 Windows and 1796 Proton checks pass.
  The expanded sweep covers every admitted single-monster catalog choice and
  preserves native IDs after initialization. This does not execute whole-game AI.
- Library: 48 Windows and 48 Proton checks pass, including old Tactical binaries,
  tamper rejection, extended roster, soundtrack and v1 JSON compatibility.
- F8 exact-name marker archival/restoration: 27 isolated file-I/O checks pass under
  Proton as well as the full Windows suite. No game prefix/config is used for them.
- Exact candidate DLL loader passes; protected installation leaves are inventoried
  separately for deployment. No game launch or RT2 was performed by Jarvis.

Independent review, native UI/closer-camera acceptance, save-switch progression and
per-monster battle compatibility remain pending. Production is not promoted.

## Provenance

Metadata adapted 2026-09-23 from the user's local GPL-3.0 Editor files:
`FFXProjectEditor/FfxLib/Dictionaries/Monster_Dictionary.cs` and
`FFXProjectEditor/Modules/ArenaTracker/ArenaTracker_Control.axaml`, compatible with
the Hooks GPL-3.0 license. Name/ID, capture-index and unlock-index facts were used;
no Editor runtime implementation was copied. Source hashes and all 35 mappings
are in the packet's `arena-unlock-audit.json`.

Soundtrack labels/IDs come from the existing Hooks `LabMusicRuntimeName` catalog.
Legacy fingerprints derive from exact private bundles `1A272302...35BB3` and
`25650109...ABAA1`; only hashes are compiled, not proprietary script payloads.
All proprietary bundles, screenshots and executable fixtures remain private.

Packet: `.superpowers/sdd/2026-09-23-ultra-native-glass-015429Z/`.

## Installed readback

Authorized paired deployment completed 2026-09-23T02:42:56Z after the user confirmed
both launcher and game closed. DLL SHA-256
`50852fd4e1804a2ec7defc6d39eb2d4b2ce3efd1fd59e000d2864b7b6a465873`,
1,608,704 bytes; profiles
`6421be6420062828049b1d7bce1bd105b6f04530da70e5c73944a81fcdf45820`,
459,492 bytes. Both previous files have verified adjacent backups ending
`.pre-ultra-native-20260923T024256Z.bak`. Four absence checks passed;310 runtime
inputs matched and44 other protected leaves were unchanged. The paired receipt
and protected inventories are in the packet. Music remains user-controlled via F8.
