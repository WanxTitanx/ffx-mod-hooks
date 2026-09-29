# Install FFX Hooks v0.6.0-beta.1

The package targets the supported Steam **FFX.exe (Windows x86)**. FFX-2 support
is not implied. A legal game installation and a working FFX DINPUT8 module loader
are required; game files and the loader are not included. Under Proton, use the
same Windows DLL and the same paths relative to the game directory.

## Download and verify

Download `ffx-hooks-release-v0.6.0-beta.1.zip` and `ffx-hooks-v0.6.0-beta.1.sha256`
from the [release](https://github.com/WanxTitanx/ffx-mod-hooks/releases/tag/v0.6.0-beta.1).
The matching `ffx-hooks-source-v0.6.0-beta.1.tar.gz` contains the public tagged source.
Verify the downloaded file hashes against the checksum file. The ZIP also has
`CHECKSUMS.sha256`, `SOURCE.md` and `release-manifest.json` for its contents.

The supplied DLL is 3,398,144 bytes, SHA-256
`734a0bf94b56157648e5391ca06dfb1c60aad2c6bb27748762a67315c98b8ffc`.
Its PE resource says 0.2.0.0; the public package/tag is v0.6.0-beta.1. A separately
rebuilt DLL can have another hash and is not this tested binary.

## Install with the game closed

1. Close FFX. Locate the directory containing `FFX.exe` and `dinput8.dll`.
2. Back up the previous Hooks DLL, configuration and the saves/sidecars you use.
3. Copy `ffx-hooks.dll` from the ZIP into `<game>/modules/`.
4. Copy the ZIP's `mods/` directory into `<game>/modules/`. Arcana images must land
   in `<game>/modules/mods/arcana/cards/` and `shared/`.
5. Preserve your existing INI. Merge desired entries from
   `examples/ffx-hooks.ini.example` into the active Hooks INI, normally
   `<game>/_isolated/ffx-hooks.ini`. The examples are OFF; do not blindly replace
   your existing configuration or install lab flag directories from the source.
6. Restart FFX after enabling a restart-required option. F8 opens the dashboard;
   Extras contains Additional mods and Vanguard, Dev contains FieldScout, and
   Cheats contains AP/Gil Multipliers. Installed/admitted status is separate from
   the saved checkbox value.

```text
<game>/
  FFX.exe
  dinput8.dll                         existing loader
  _isolated/ffx-hooks.ini             preserve/merge your active settings
  modules/
    ffx-hooks.dll
    mods/arcana/cards/                78 selected images
    mods/arcana/shared/               icon and card back
```

The active INI may be selected by your existing installation. Put
`monster-rewards-v1.tsv` beside that INI, not beside the DLL by assumption. F8
creates/saves the table; external edits require restart. The supplied TSV example
contains only the version header, so missing monster rows retain neutral factors.

## Enable only the features you choose

Every editable F8 boolean defaults OFF; the dashboard defaults ON. External OFF
flags/environment settings can override an INI choice. F8 reports these blockers.
For the legacy F7 gate, create `modules/config/f7_inlive.flag` with FFX closed or
use `FFXHOOKS_ENABLE_F7=1`; individual runtime families still have their own gates.
The `f7_aiswap` compatibility name enables an observer, not general AI mutation.

For Arcana, merge `examples/Arcana-settings.ini.example`, set `[arcana] enabled=1`
and restart. Open **Main Menu > Equip > Tarot**. The separate Dev full-deck option
is an explicit instant grant; normal acquisition is in `Arcana-reference/`.

The package includes Arcana's original runtime art. It does not contain a complete
PT-BR translation, game-derived ability tables, authored Elemental packages or
private S.I.N. AI packs. Their loaders/contracts are implemented; obtain or author
compatible packages under the documented source/hash/owner rules. No game asset
or proprietary executable is required to build the DLL itself. Some native RT1
fixtures require your own exact PE/save/kernel inputs and are not redistributed.

## Verify and troubleshoot

Check `%TEMP%/ffx-hooks.log` in the game's Windows/Proton environment for the
selected feature's requested, installed and effective states. Do not infer
success solely from an ON checkbox. Unsupported executable/signature, absent
packages, wrong paths and external OFF markers can keep a feature unavailable.

Use a disposable save for validation. The [roadmap](ROADMAP.md) lists remaining
live acceptance work. Follow [RT2 protocol](RT2_PROTOCOL.md) for reproducible
cases; build/loader checks are not gameplay acceptance. Other DINPUT8, Special K
or UnX owners can conflict. Dynamic hot-unload is unsupported.

## Update, rollback and uninstall

Close FFX before replacing or removing the DLL. Preserve your INI, native saves
and matching Workshop/Aeon/Arcana/Ronso sidecars; they may carry persistent paid
upgrades or collection state. Restore the backed-up DLL and matching package
assets for rollback. Do not assume that deleting metadata refunds purchases or
that every feature is RAM-only. Do not delete the shared loader or another mod's
files as part of uninstalling Hooks.

## Build and package from source

Use the exact tag and follow the [README build instructions](../README.md#build).
Windows, Visual Studio C++ x86 tools and the pinned static dependencies are needed
for the DLL. The separate offline SIN tool uses .NET 8. Portable checks cover
Workshop, Arcana and text-language contracts; optional authoring dependencies are
listed with each tool.

`tools/package_release.py` recreates the version-bound binary ZIP from the
recorded validated DLL and a clean source commit. It rejects source or DLL drift.
`git archive` of the public release tag provides the corresponding source tree.
Historical research receipts may name private local fixtures; default package
creation does not read those paths or copy proprietary data.
