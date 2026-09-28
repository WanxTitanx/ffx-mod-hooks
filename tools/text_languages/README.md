# MOD-006 text-language tools

Jarvis-HOOK. Prepare and validate an independent PT-BR text package without editing the original installation. Runtime selection is opt-in and restart-only. This directory provides infrastructure and a small sentinel recipe, not a complete game translation.

Read the complete [OnlyMod contract](../../docs/ai/MOD006_EDITOR_HANDOFF.md), [provenance](../../docs/mods/MOD006_TEXT_LANGUAGES_PROVENANCE.md), and [future live acceptance procedure](../../docs/mods/MOD006_RT2_CHECKLIST.md).

## A future translation package

The received package must implement `ffx.text-locale` API 1 or 2 with its matching schema and the supported executable/font profile. An arbitrary pre-existing translation ZIP is not automatically compatible. Work outside the game installation.

```sh
python3 tools/text_languages/reference.py \
  --vbf "<original data/FFX_Data.vbf>" \
  --pack "<received pt-BR directory>" \
  --output "<new private reference directory>"

TextLanguageValidate.exe "<received pt-BR directory>" "<private reference directory>"
```

Require validator exit 0 and `ADMITTED RT0`. The reference extractor alone is not full package validation. Missing/changed files, incompatible versions, incorrect sources, missing glyphs and unsafe text changes must be corrected in the package before deployment. The runtime repeats source verification when admitting its font and text transaction.

The Windows validator is produced at `src/runtime/FfxHooksDll/obj/text-languages-rt1/TextLanguageValidate.exe` by the runner below. Linux `run_checks.py` produces a native `TextLanguageValidate` executable in its output directory. The validator has no Python/Pillow runtime dependency. Use a real package directory, not a symlink or junction: preflight rejects redirected roots just as the runtime does. On Windows, it reuses the runtime file-pinning service for the entire admission check.

The private-fixture runners also execute ten recipient checks on temporary copies: a valid pack, absent/changed payloads, incompatible executable/version/locale, unsafe/voice paths, redirected root, and continued validity after the rejected cases. Run that check independently with `python3 tools/text_languages/check_received_pack.py --validator <compiled-validator> --pack <pt-BR-directory> --reference <source-directory> --output <separate-test-directory>`. The inputs remain unchanged; passing does not install a pack or certify live rendering.

After separately reviewed deployment, the supported layout is `<FFX.exe directory>/_isolated/languages/pt-BR/manifest.json` plus its referenced text/font resources. F8 System > Text languages saves the virtual selection. Native English remains the base selected through the original game controls; voices have separate settings. Restart to change, remove or replace a package. Keep original regional resources and the VBF intact.

## Authoring the demonstration

Python 3 plus Pillow are required for font generation. The source archive is opened read-only. Both output destinations must be new directories outside the installation.

```sh
python3 tools/text_languages/pack.py \
  --vbf "<original data/FFX_Data.vbf>" \
  --edits tools/text_languages/pt-BR.demo.json \
  --output "<new isolated directory>/pt-BR" \
  --reference "<new isolated directory>/reference"
```

The recipe demonstrates menu, battle and PS3/PSV event/caption resources, accents and native controls. Translation edits preserve native row identity, choice flags, control/placeholder sequence, line count, encoded capacity and measured width. An over-limit translation is rejected instead of truncated. No-edit resources remain byte-identical. Fonts use the fixed, source-verified Western extension; arbitrary new glyph profiles need a separate capability change.

Schema files under `contracts/` document the authoring structure; the C++ validator enforces the semantic and binary requirements. Neither schema validation nor a pack hash is a signature of trust or a live-game test.

## Verification

```sh
python3 tools/text_languages/run_checks.py --output work/mod006-checks --sanitizers

python3 tools/text_languages/run_checks.py --output work/mod006-private-checks \
  --pack "<pt-BR directory>" --reference "<reference directory>" --sanitizers
```

```powershell
./src/runtime/FfxHooksDll/text_languages_rt1.ps1

./src/runtime/FfxHooksDll/text_languages_rt1.ps1 `
  -ExecutablePath '<private exact FFX.exe>' `
  -PackageDirectory '<private pt-BR directory>' `
  -ReferenceDirectory '<private reference directory>' `
  -BuildDll
```

The Windows runner uses MSVC x86, compiles the native adapters, and runs file/configuration/F8 tests. Supplying all three private arguments also runs the relocated native stream/font cases and audio invariance tests. `-BuildDll` builds the full Release DLL but never deploys it or launches FFX. Without private fixtures, public CI explicitly reports native execution as not supplied.

Keep generated fonts, extracted game resources, executable/save fixtures and reference snapshots outside Git. A future pack revision needs its own validator result and affected live tests. Painted texture text and generic video subtitle formats are not advertised by API 2; unlisted resources stay native.
