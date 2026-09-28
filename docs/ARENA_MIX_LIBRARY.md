# Custom Mix positions and battle library

The F7 Arena+ menu contains **Custom Mix** (exactly three, four or five expanded
monster slots), **Custom Mix Ultra** (one through eight), and **Saved Battles**
inside both editors/lists. Arena+ Master must be enabled at startup and Compose F7
must be ON to launch. Boss defeat requirements remain active unless F8 Bypass
Progression is enabled. Importing a preset does not change that setting or launch
anything automatically.

## Battle arena

The editor's first row, **Arena: ...**, shows the selected destination. Open it to
choose scenery before Launch. New x3 mixes offer Macalania Forest, Open, Open2
and Mushroom Rock Road; x4 offers Calm Lands Cavern, Bikanel and Mushroom Rock;
x5 offers Cavern wide, Calm Lands alternate and Mushroom Rock. Ultra also exposes
these choices and the original Zanarkand Dome.

**Camera** is the second row: **Arena default** uses normal arena framing;
**Tactical (overhead)** is optional. Both use normal battle programs without the
old boss-specific per-monster overrides.

Scenery and camera travel with the saved/exported JSON. Auto Arrange adapts its spacing to
the arena preset; a manual layout is preserved when changing scenery. Some presets
share terrain and differ in automatic spacing. Party anchors come from the selected normal arena. Camera mode chooses its
normal or tactical profile. Selecting an arena does not launch a battle.
Older presets without arena metadata retain their original Zanarkand scene; use
the first row to change it before exporting a new copy.

## Positions

- Select bosses, then choose **Auto Arrange** for a complete 1–8 slot layout.
- **Edit Positions** opens a top view and controls for the selected slot, X and Z.
  Up/Down selects a control, Left/Right changes it; Shift uses a finer step.
- **Reset This Slot**, **Auto Arrange All**, **Apply Positions**, and **Cancel**
  operate on a separate draft. Apply affects the next explicit Launch.
- **Use Native Positions** disables the optional position override.
- Changing the monster selection regenerates an enabled layout, so old coordinates
  cannot silently attach to a different monster. Magus occupies three distinct slots.

The preview uses coordinates relative to the party, not a rendered 3D
scene or a mesh collision simulation. X is limited to -160..160 and Z to 80..200;
spawn centers must be at least 24 units apart and clear of the known party anchors.
Height, facing/rotation and animation remain native. The game retains
ownership of animation and scripted movement; a native position setter takes back
its affected slot. Compose OFF resumes native reads, and every new battle clears
the previous override.

## Save, import and rename

**Export Battle** creates a new, uniquely identified bundle. It never overwrites an
existing battle. Saved Battles lists three built-in presets, saved/imported JSON
files, and the old `modules/arena_formations` entries. Refresh Library rescans the
folders. Up to 64 file entries are displayed in addition to the built-ins.

The new folder is relative to the installed Hooks DLL:

```text
modules/config/arena-mixes/
  _template.bin                         old companion-file compatibility
  _battle-profiles-v1.bin               verified normal arena/camera profiles
  mix-<stable-id>.json                   editable preset configuration
  mix-<stable-id>/<arena>/<arena>.bin    normal Editor-readable battle
```

Copy the JSON and its matching folder together to transfer a complete bundle.
A JSON alone can be imported as settings. Keep identifiers/filenames to letters,
digits, `_` and `-` (maximum 48 characters); use the name field for the visible title.

In a saved battle's action menu:

- **Load JSON for Editing** reads the saved configuration into the Mix editor.
- **Import Edited Battle (.bin)** explicitly reads changes made to the companion
  native file. This is separate from JSON loading, so one file cannot silently
  override edits to the other.
- **Export a Copy** preserves the original and generates another bundle.
- **Rename Battle** changes the visible name only. Type up to 40 supported ASCII
  characters; Ctrl+A selects all, Backspace/Delete edits, Enter saves and Esc cancels.
  The stable identifier, binary filename and binary bytes stay unchanged. Unknown
  JSON metadata is preserved by this name-only edit.

Built-ins and legacy entries must be exported as a copy before renaming. For old
`arena-layout-export-v1` files, **Convert to Current Arena** imports the bosses and
regenerates positions for the current arena. The menu warns that old scenery and
camera settings are not imported. The original file remains untouched.

## Editor compatibility and versioning

The binary uses the game's existing battle format. No new Editor feature is needed
to open the native `.bin`. Existing `Battle_File.Read` and `BattleArenaAnchors_File`
readers were exercised against an actual Hooks export and recognized the expected
monster IDs and positions.

A returning binary may change supported monster slots and monster X/Z values.
Changes to structure, scripts, scenery, camera, party, height/rotation, unsupported
monster IDs, sparse slots, incomplete Magus groups, or positions outside the
supported bounds are rejected. When the edited roster no longer matches a fixed
Mix size, the imported draft uses the corresponding supported size or Ultra.

The JSON contract is UTF-8, maximum 32 KiB, schema `ffx-hooks.arena-mix`, version 2:

```json
{
  "schema": "ffx-hooks.arena-mix",
  "version": 2,
  "name": "Elemental Trio",
  "carrier": "mcyt00_21",
  "battle_program": "normal-v1",
  "program_source": "mcyt00_21",
  "coordinate_space": "party-relative",
  "camera_mode": "arena",
  "required_slots": 3,
  "arena": "macalania_open2",
  "battlefield_id": 1046,
  "choices": ["valefor", "ifrit", "ixion"],
  "position_mode": "auto",
  "positions": [[-56, 140], [0, 140], [56, 140]],
  "editor_binary": "mix-example/mcyt00_21/mcyt00_21.bin"
}
```

`required_slots` is 0 (Ultra), 3, 4 or 5. Choices are symbolic: `valefor`, `ifrit`,
`ixion`, `shiva`, `bahamut`, `yojimbo`, `anima`, `magus`. Repeats are grouped in first
activation order; Magus expands in its native three-slot order. Position pairs use
that expanded order. `native` mode requires an empty positions array; `auto` and
`manual` store explicit coordinates, preserving the authored layout across future
algorithm changes. Unknown additive fields are accepted; unknown major versions,
duplicate keys, malformed values and nonfinite coordinates are rejected. Exporting
a new copy writes the known canonical settings; renaming preserves unknown fields.
`editor_binary` is advisory: Hooks derives file paths from the validated stable
identifier rather than executing or trusting an imported path.

Version1 JSON/native companions remain importable for roster/layout; their boss
program is not retained. New exports are version2 and contain the normal battle
program used by Hooks. JSON X/Z are party-relative; native binaries contain the
transformed world coordinates. Import converts them back. Camera modes are
`arena` and `tactical`; old presets default to `arena`.

`arena` and `battlefield_id` identify the native scenery
selector. Supported keys are `carrier` (0), `macalania_forest` (1044),
`macalania_open`/`macalania_open2` (1046), `cavern`/`cavern_wide`/`cavern_alt` (1080),
`bikanel` (1049), and legacy-compatible `remiem` (1035, Mushroom Rock Road).
An unknown key or inconsistent ID is rejected. Missing fields mean `carrier`.
The companion binary contains formation/positions; editors can read scenery from
this JSON. Importing an edited binary retains its JSON arena selection.

The private `_template.bin` is retained for old companion compatibility: the user's
vanilla `dome02_00` (17,448 bytes,
SHA-256 `DDF8D89343195D3D014630C296A9583EE556EFA839918435802249FE148594D0`). It is not
stored in Git. Export works on a copy; it does not write the game's active battle
files, configuration gates, learned flags or saves.

## Native Ultra catalog update (2026-09-23)

Custom Mix Ultra now opens six monster categories. Back keeps the selected roster;
Your Formation removes an activation, and Magus Sisters stay a three-slot group.
Normal Mix retains its original Dark-only roster. Ultra offers 344 catalog choices,
260 admitted choices and 84 encounter-only entries. The latter are not progression
locks and cannot launch through this composer.

Ordinary monsters share the normal Mix unlock without an extra capture requirement.
Arena creations follow the game's 35 unlock flags; Dark Aeons follow their defeat
flags. `C:` reports captures where mapped. Bypass Progression deliberately overrides
these requirements; turn it OFF when checking real progression.

Ultra's Music row selects among 92 named tracks, default A Challenge. If `[OFF]`
appears, turn Arena+ Music ON in F8 and restart. This explicit ON archives old
Arena-specific OFF markers; a global music OFF marker still overrides it.

New JSON exports use version 3 and add `music_track` plus `monster_NNN` symbolic
keys for catalog entries. Versions 1 and 2 remain importable. The native importer
also recognizes Tactical binaries from the prior two profile revisions and keeps
only their roster/layout; unknown script or other structural edits are rejected.
The existing reviewed scenery list remains in use. Arbitrary per-monster stat
editing and the browser prototype's wider map list are not native controls yet.

See [native Ultra evidence](reverse/ARENA_ULTRA_NATIVE_2026-09-23.md).

## Native browser/search update (2026-09-23)

Ultra now has Search All Monsters, category search and a permanent formation pane.
Enter applies a typed query; Esc cancels; Ctrl+A selects the query. Search also
works in Arena and Battle Presets. All863 canonical battle files are cataloged;
89 non-system terrain selectors produce93 options including retained older variants.
A preset opens its opponent list before explicitly applying the lineup or arena.
The composer uses normal battle programs; original story scripts are not replayed.
Encounter-only actor groups remain unavailable as custom lineups.

The previous full-name subtitle could exceed64 bytes and terminate the process
when returning from a category. It is now a bounded summary; names stay in the
formation pane. Arena selection shares F7's pulsing glass lift and green edge.
See [browser stability evidence](reverse/ARENA_BROWSER_STABILITY_2026-09-23.md).
