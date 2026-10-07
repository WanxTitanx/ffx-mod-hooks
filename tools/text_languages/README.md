# MOD-006 text-language tools

Jarvis-HOOK. Prepare and validate an independent PT-BR text package without editing the original installation. Runtime selection is opt-in and restart-only. This directory provides infrastructure and a small sentinel recipe, not a complete game translation.

Read the complete [OnlyMod contract](../../docs/ai/MOD006_EDITOR_HANDOFF.md), [provenance](../../docs/mods/MOD006_TEXT_LANGUAGES_PROVENANCE.md), and [future live acceptance procedure](../../docs/mods/MOD006_RT2_CHECKLIST.md).

## A future translation package

The received package must implement `ffx.text-locale` API 1, 2, 3 or 4 with its matching schema and the exact compiled executable/font profile. API 3 adds the verified structured kernel, battle-bank, main-menu and macro-dictionary resources; earlier contracts remain accepted for their original resource families. An arbitrary pre-existing translation ZIP is not automatically compatible. Work outside the game installation.

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
  --exe "<exact supported FFX.exe>" \
  --edits tools/text_languages/pt-BR.demo.json \
  --output "<new isolated directory>/pt-BR" \
  --reference "<new isolated directory>/reference"
```

The recipe demonstrates menu, battle and PS3/PSV event/caption resources, accents and native controls. The producer verifies the executable's size and SHA-256; `--exe` may be omitted only when the supported executable is beside the source installation's `data` directory. The unchanged VBF does not identify an executable version.

Translation edits preserve native row identity, choice flags, control/placeholder sequence, line count, encoded capacity and measured width. An over-limit translation is rejected instead of truncated. No-edit resources remain byte-identical. Fonts use the fixed, source-verified Western extension; arbitrary new glyph profiles need a separate capability change. Recipes are bounded to 32 MiB; emitted runtime manifests remain bounded to 1 MiB.

The closed catalogue in `text_layouts.py` gives each resource's family, minimum API, stride and slots. Weapon names have 14 slots. Compact `btl_txt.bin` records expose two offset/metadata pairs at +0/+4, preserving +2/+6; old raw-word slot 2 becomes normalized slot 1, and raw-word slots 1/3 are not editable. Macro authoring uses `row = chunk * 65536 + row_in_chunk`, with two variants. Missing field/macro variants remain missing. Kernel offset zero is a valid first string. Appending text cannot activate an invalid native pointer, and identical replacements share an appended string. `battle/kernel/item.bin` and `item.bin` identify the same resource and cannot both appear in a recipe.

The producer selects API 3 when a resource, preserved null field variant or supported control requires it. API 3 preserves pause `01`, choice `10`, dictionary controls `14`–`23`, additional documented color arguments and the two dummy character names. Existing controls must stay in the exact original sequence; these capabilities do not permit creating new control flow. Mixed-encoding `GameData/PS3Data/lockit/ffx_loc_kit_ps3_us.bin`, alternate text banks and Flash remain outside the runtime catalogue. API 4 admits only the separately examined UI texture pairs listed below.

Schema files under `contracts/` document the authoring structure; the C++ validator enforces the semantic and binary requirements. Neither schema validation nor a pack hash is a signature of trust or a live-game test.

## Recording an editorial pass

`corpus.py` exports immutable English/legacy locations with raw bytes and reversible previews. Keep the received assets, corpus and editorial decisions outside Git. An opaque preview, a reference match from another game or a control check is not semantic approval.

For reviewed JSONL shards with `uid`, `en` and `pt`, `review_writer.py` accepts a JSON batch on standard input. Every new decision includes a short, literal `source_anchor` copied from the English that the reviewer actually read. The writer checks the assigned source prefix, UID, anchor, placeholders, named markers, line breaks and numeric digits before modifying either output. It rejects stale counts and recognizes identical replays without appending duplicate decisions; replay also repairs a missing checkpoint. A missing/malformed source or checkpoint is an error, not permission to discard previous work.

```sh
python3 tools/text_languages/review_writer.py \
  --source "<immutable shard.jsonl>" \
  --decisions "<owned decisions.jsonl>" \
  --progress "<owned progress.json>" \
  --expected-count 120 --limit 5000 < "<reviewed batch.json>"
```

Give each output one reviewer. Use small, fully visible reading batches; truncated tool output must be re-read before review. The writer's validation proves association and structure only. Ambiguous context and terminology remain explicit, and the final semantic/byte-width pass is separate. `nonlinguistic` means preserve the original source instead of importing a potentially different legacy placeholder.

## Verification

```sh
python3 tools/text_languages/run_checks.py --output work/mod006-checks --sanitizers
python3 tools/text_languages/run_checks.py --output work/mod006-steam-checks \
  --profile steam-20261001 --sanitizers

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

Keep generated fonts, extracted game resources, executable/save fixtures and reference snapshots outside Git. A future pack revision needs its own validator result and affected live tests. Painted texture text and generic video subtitle formats are not advertised by API 3; unlisted resources stay native. The [public release validation](../../docs/releases/v0.6.0-beta.4-validation.json) records the private test-pack checks and their limits.

## Explicit API 4 authoring

Use `pack.py --hook-api 4` for bounded container-backed growth, native position
`07`, style `0E`, extended variable `12` and the observed `0B:20` argument.
Every argument stays bound to its source. Choices and positioned layouts retain
exact control/newline order; other lines may reflow within the same pause page.
The 2,048-byte script bound, u16 pools and total memory limits remain enforced.
Width/byte growth is reported as `layout_review_required`, not proof of live fit.
The optional `ffx-western-v2` profile adds º/ª at F6/F7 with advances 21/20;
existing glyphs and the v1 profile remain supported. `{GLYPH:8F}`, `90`, `9C` and
`D4` preserve examined native symbols without guessing Unicode names.

Sphere records and `btlend_txt`, `build_txt`, `name_txt`, `save_txt` have two
editable references at +0/+4. Their tails remain immutable; this corrects older
four-offset extraction without assigning semantics to the auxiliary halfwords.

`graphics.py --vbf SOURCE --images REVIEWED_PNG_DIRECTORY --output NEW_DIRECTORY
--encoder ispc` compiles reviewed containers without changing headers or extents.
The selected authoring dependency is `ispc_texcomp==1.0.1` (MIT), with Pillow and
NumPy. The compiler handles alpha separately and preserves untouched compressed
blocks/channels. The runtime has none of these Python dependencies.
The input directory supplies `resource-sources.jsonl` with verified source/PNG
hashes. A closed catalogue is generated with `graphics_catalog.py --compiled DIR
--output TextLanguageGraphicsCatalog.h --profiles graphics_profiles.json`.
Changing that catalogue requires a new reviewed source build. Compilation alone
cannot authorize new graphics in an existing DLL.

Supply `--graphics COMPILED_DIRECTORY` with `--hook-api 4` to package the current
466 examined UI resources. All four native font pages stay owned by the paired
font transaction. See [public language-pack contract](../../docs/TEXT_LANGUAGE_PACKS.md)
for exact input identities, full candidate checks and outstanding live limits.
