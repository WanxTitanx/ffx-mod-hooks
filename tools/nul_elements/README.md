# Six Elemental Nuls — Jarvis-HOOK

This recipe reuses disabled Radiant/Umbral Ward records 320/321 and appends four
commands to the reviewed 370-row Spira command bank. It does not manufacture a
bank from vanilla, overwrite occupied identities, distribute game assets or
grant commands to saves. The user must supply their local licensed game files.

| Name | Command | Encoded command | Magic DLL | Element identity |
|---|---:|---|---:|---|
| NulHoly | 320 | `0x3140` | 870 | native `0x10` |
| NulShadow | 321 | `0x3141` | 871 | native `0x80` |
| NulEarth | 370 | `0x3172` | 872 | native `0x20` |
| NulWind | 371 | `0x3173` | 873 | native `0x40` |
| NulPoison | 372 | `0x3174` | 874 | `hook.custom03` / `spira.poison` |
| NulGravity | 373 | `0x3175` | 875 | `hook.custom04` / `spira.gravity` |

The default recipe is Yuna-only, White Magic+ category 4, party target, 2 MP,
one hit, no damage or native status payload. IDs 366–369 (White Magic+ and Aero
through Aeroga) are preserved. The other 368 existing rows and complete old text
pool prefix stay byte-identical. The resulting bank has 374 96-byte records.
Text offsets remain native little-endian WORDs relative to the shifted pool.

The runtime requires `elemental.nul_spells` through its F8 authority resolver.
It defaults OFF and needs restart. GridTeach infrastructure is requested by this
gate and stores learned commands per save and character in its existing sidecar.
Do not expand or write the game's native 16-WORD learned bank for command 373.
No Sphere Grid node, learned state or personal save is edited by these tools.
An Editor-authored command node can teach the encoded identity normally. Yuna's
White Magic+ root is derived from a learned child without persisting a fake root
unlock. F8 element aliases never rewrite canonical command or item ability names.

## Authoring

Run from this repository; use new staging directories:

```bash
python3 tools/nul_elements/commands.py /path/to/command.bin
python3 tools/nul_elements/commands.py /path/to/command.bin --output work/nuls/commands
python3 tools/nul_elements/animations.py \
  --donor /path/to/game/magicFiles/FFX/magic_0146.dll \
  --textures /path/to/extracted/FFX/ffx_data/gamedata/ps3data/magic/magic_0146 \
  --output work/nuls/animations
python3 -m unittest discover -s tools/nul_elements -p 'test_*.py' -v
```

`holy_donor_recipe.json` pins the donor DLL and all eight texture hashes/layouts.
Older Editor evidence records crashes when a native Nul animation loses its
native status payload. These commands instead clone the compatible one-shot Holy
146 donor already used by the two disabled Wards. Six private DLLs replace only
the three UTF-16 identity strings; `.text` is byte-identical. Each clone has eight
private DXT5 texture files; tinting covers every mip while preserving headers,
dimensions, block indices and alpha interpolation mode. Gold, purple, brown,
teal, green and indigo distinguish the six effects. This is offline structure and
resource evidence; game rendering and appearance require a live observation.

The complete-mip tint algorithm is adapted from
`FFXProjectEditor/FfxLib/Ps3/Ps3MagicWindTextureWriter.cs` in
[`WanxTitanx/ffx-editor-main`](https://github.com/WanxTitanx/ffx-editor-main),
GPL-3.0, inspected 2026-09-29. The receipt records local asset fingerprints, not
redistributable proprietary content.

## Three-root installation and recovery

Close FFX and the Editor. Supply the actual Steam game root, Spira Reforge mod
root and Extracted **FFX** root (not FFX-2). Dry run first:

```bash
python3 tools/nul_elements/install.py \
  --commands work/nuls/commands --animations work/nuls/animations \
  --steam '/path/to/Steam/game' --spira '/path/to/Spira Reforge' \
  --extracted '/path/to/FFX Extracted/FFX'
```

The plan has 168 leaves: two command locales (`jppc`, `new_uspc`) and 54 private
FX files per root. Steam/Spira textures use
`data/mods/FFX_Data/GameData/PS3Data/magic`; Extracted uses
`ffx_data/gamedata/ps3data/magic`. DLLs use `magicFiles/FFX` in all roots.
After review, repeat with `--apply --backup work/nuls/install-backup`.

The installer refuses changed command hashes, occupied clone IDs, symlink targets
and incomplete stages. It backs up existing banks, publishes through local
temporary files, verifies all hashes and records `installation.json`. Identical
assets are an idempotent no-op. On failure, only its own still-matching writes are
restored; newer concurrent edits are preserved and reported as `restore-pending`.
Empty directories may remain after rollback. Keep the receipt and original files.

For later rollback with the game/Editor closed, first compare every current leaf
against the receipt's `after_sha256`. Restore a recorded backup only after that
comparison. Remove a newly created clone only when `before_sha256` is null and
the current hash still matches the installed hash. Never delete a newer file.
The tools do not install the runtime DLL or enable any gameplay setting.

## Runtime and validation boundaries

Charges are private per actor lifetime/action, not native status timer bytes.
External Poison/Gravity masks exist only inside the hook; the native element
field remains one BYTE. Fully covered mixed attacks reserve their required
charges together; partial coverage spends none. Completion and actor/action
retirement own consumption and cleanup. Poison Nul does not remove Poison status.

Coverage includes byte-preserving/idempotent authoring, occupied-ID refusal,
texture/DLL boundaries, installer drift/rollback, actual menu bodies with the
real learned-state core, strict command payload admission, native/shared Nul
composition in both startup orders, and preservation of native Nuls/Haste/Slow.
The Editor parser independently reads all 374 commands and all six animation
roots. Its writer zeros 149 orphaned old text bytes; that rewritten buffer is
not installed. Every referenced script and all command records round-trip.
These results are RT0/isolated RT1, not live rendering or Production promotion.
