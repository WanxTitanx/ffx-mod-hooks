# [DERIVADO DE MOD-006] Text-language package contract

Jarvis-HOOK, 2026-09-28. This is the implementation contract for future translation packs. A pack must pass the compiled validator and its own live acceptance before release. The sample is a sentinel demonstrator, not a complete translation.

## Product boundary

Editor projects remain vanilla by default. An explicit OnlyMod translation project exports a separate `pt-BR` directory; it never changes the original regional assets, vanilla serializers, native locale enumeration, save header, or audio settings. Label every dependent option `[DERIVADO DE MOD-006]`. There are no Editor repository changes in this Hooks delivery.

The supported game image is PE32/i386, SHA-256 `78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced`. Runtime capability is `ffx.text-locale`, API 2. Schema/API `(1,1)` remains accepted for the original menu/battle contract. Schema/API `(2,2)` adds field event/dialogue and scene-subtitle tables. Other version pairs fail admission.

The first selectable virtual locale is `pt-BR`, displayed in F8 as `Portuguese (Brazil)`. Its native base is English ID 1. The player must select that base through the original game/launcher controls; the mod does not coerce the global locale. Other native languages keep their original routes. Configuration uses `language.text_locale=native|pt-BR`, separate from `language.voice`, `language.sfx`, and `language.video`.

The F8 System tab contains `Text languages`, with the original text option, PT-BR, current runtime status, and Back. Selection persists atomically and takes effect after restarting FFX. Unknown config is diagnosed, not silently interpreted as PT-BR. Saving a choice does not mean that a pack has already been admitted.

## Package layout and admission

Future deployment layout, relative to the directory containing `FFX.exe`:

```text
_isolated/
  ffx-hooks.ini
  languages/
    pt-BR/
      manifest.json
      text/...
      font/base.ftc
      font/font_0_0.dds.phyre
      font/font_0_1.dds.phyre
      font/shadow_0_0.dds.phyre
      font/shadow_0_1.dds.phyre
```

This is a layout contract, not an instruction executed against the installation. Keep the package and private source snapshot outside the installation during authoring and validation. The source snapshot is a validator input; it is not installed with the translation.

Required manifest fields: `schema_version`, `capability`, `hook_api`, `locale`, `display_name`, `pack_version`, `base_locale`, `fallback`, `activation`, `executable_sha256`, `coverage`, `resources`, and `fonts`. Fallback is `native`; activation is `restart`. Resource entries contain a unique stable `id`, `family`, canonical virtual `request`, safe package-relative `path`, source/output sizes and SHA-256 values; text entries also bind `font`. The schema files under `contracts/text-languages.*.schema.json` describe authoring structure. The C++ parser/admitter remains authoritative for semantic and byte-level checks.

Limits include a 1 MiB manifest, at most 4,096 resources, at most 64 MiB per resource and a 256 MiB combined source/output admission working set. Absolute paths, traversal, alternate streams, Windows reserved device names, ambiguous separators, duplicate resource requests/paths/IDs and filesystem reparse redirects are rejected. The runtime pins admitted files and directories against replacement/writes and opens independent read cursors. SHA-256 identifies bytes; it does not establish a distributor's identity or trustworthiness.

At startup, the hook verifies the executable identity, target signatures, unchanged ownership and a not-yet-published native Western font. At the native font-registration boundary it checks the complete source/output pack and font profile before publishing translated resources. Concurrent stream readers wait for that transaction. Missing/corrupt/incompatible packs, source changes, unsupported locale, early resource use, or late hook installation retain the original path with a diagnostic. No text is intentionally published while its required glyphs are still unprepared.

After publication, resources and font remain paired for the process lifetime. Hot replacement or dynamic unload is unsupported; changing or removing a pack requires a restart. The DLL/trampolines are retained appropriately. A stop request before publication prevents activation. A stop request after publication closes future changes but does not swap a native font underneath already-cached translated strings.

## Resource families

| Family | Admitted requests | Coverage meaning |
|---|---|---|
| `menu` | `new_uspc/battle/kernel/{menu_txt,mmain_txt,config_txt,save_txt}.bin` | Only the supplied validated tables/entries are translated. `mmain_txt.bin` contains the main menu. |
| `battle` | `new_uspc/battle/kernel/{arms_txt,btl_txt,btlend_txt,build_txt,item_txt,name_txt,status_txt,summon_txt}.bin` | Kernel text, battle notices and related labels/descriptions within these families. |
| `events` | `new_uspc/event/{obj_ps3,obj_psv}/<two-letter group>/<event>/<event>.bin` | Eight-byte field-string tables, including the demonstrated dialogue/scene captions. These are text resources, not event-script executables. |
| `font_metrics`, `font_atlas` | The complete fixed Western PT-BR profile | Original glyphs plus `ã/õ/Ã/Õ`; exact source/output hashes are pinned. |
| `texture_text` | Unavailable | Original regional textures remain native. Burned-in video text and arbitrary texture translations are outside API 2. |

Virtual requests include the `/FFX_Data/ffx_ps2/ffx/master/` prefix, or the `/FFX_Data/GameData/PS3Data/menu_us/base_ftc/D3D11/` atlas prefix. Canonicalization permits the observed `../../../FFX_Data/` spelling and ASCII case/slash normalization, not arbitrary relative path traversal. Event IDs and repeated leaf names must match. Unlisted families are not inferred from an extension.

`coverage` contains `menu`, `battle`, `events`, `subtitles`, and `texture_text` in schema 2. A supplied family is `partial`, otherwise `unavailable`; no field claims a complete translation. Scene subtitles share the demonstrated event-text resource route. A separate generic movie-subtitle format has not been admitted. Schema 1 omits `subtitles` and requires events/texture_text unavailable.

## Text and glyph authoring

Use `tools/text_languages/pt-BR.demo.json` as a complete recipe example. Each resource maps to edits with native `row`, `slot`, and UTF-8 `text`. Kernel slots are 0..3; field slots are 0..1. Unknown rows/slots, duplicate edits, malformed UTF-8, raw control bytes and unmapped characters are errors.

The codec includes the verified native Portuguese accent mappings and uppercase `Ç=A7`, lowercase `ç=BE`. Tilde additions are `ã=F2`, `õ=F3`, `Ã=F4`, `Õ=F5`; their metric advances are 31, 33, 42 and 42. The native atlas had ink at F0/F1, so those bytes were not reused. The full font/atlas/shadow group is required; arbitrary substituted fonts or expanded Unicode ranges are not accepted merely because their manifest has hashes. Normalize source Unicode consistently; unmapped combining characters must be resolved during authoring rather than dropped.

Supported recipe markup includes `\n`, `{TIDUS}`, `{YUNA}`, `{VAR:0}` through `{VAR:9}`, `{WARN}`, `{NORMAL}`, and explicit `{CTRL:XX:YY}` for supported native control/argument pairs. Literal braces are doubled. The validator preserves the original control sequence, arguments, placeholder identities and line count. Native controls 09/0B/19 have bounded nonzero arguments; 0A accepts 41/43/52/B1, 12 accepts 30..39, 13 accepts 30..41, and 03 is newline. Unknown control/bank sequences survive unchanged originals but are not authorable edits in this contract.

A translated string must fit both the original encoded byte capacity and each original measured line width. A visually narrow but longer byte sequence is still rejected. Variables and numbers are never truncated to make text fit. Translators must shorten/rephrase an over-limit line; larger buffer or layout support requires a separately validated capability change.

Kernel replacement preserves the original header, keys, opaque data and suffix-shared string pool; changed references point only into an appended pool. Field replacement preserves its implicit header length, entry count, flags and dialogue choice counts, while rebuilding bounded string offsets. Native offsets are `u16`; no offset or allocation range is silently widened. A no-edit write preserves bytes. Examples `{TIDUS}\nMaldito velho...` and `{CTRL:09:30}Ataque inicial!` exercise real source controls in the sentinel recipe.

## Reproducible authoring and preflight

Python 3 is required; font generation additionally uses Pillow. Read-only PE probes additionally use pefile/capstone. The runtime has no Python/Pillow dependency.

```text
python tools/text_languages/pack.py --vbf "<original data/FFX_Data.vbf>" --edits tools/text_languages/pt-BR.demo.json --output "<new isolated directory>/pt-BR" --reference "<new isolated directory>/reference"

python tools/text_languages/reference.py --vbf "<original data/FFX_Data.vbf>" --pack "<received pt-BR package>" --output "<new private reference directory>"

TextLanguageValidate.exe "<received pt-BR package>" "<private reference directory>"
```

`TextLanguageValidate` is built by `src/runtime/FfxHooksDll/text_languages_rt1.ps1` on Windows, or `tools/text_languages/run_checks.py --output <build directory>` on Linux. The Windows build outputs it under `obj/text-languages-rt1/`. Require exit 0 and `ADMITTED RT0`; an extracted reference or a JSON-schema pass alone is not acceptance. The game checks sources again at activation, so modifications between preflight and launch fail admission.

Recipient preflight validates the package path before canonicalization; symlink/junction roots are rejected. Windows preflight uses the runtime `Files` service, holding verified files open throughout admission. The regression command `python tools/text_languages/check_received_pack.py --validator <compiled-validator> --pack <received-pack> --reference <source-snapshot> --output <isolated-test-output>` exercises ten acceptance/rejection cases on temporary copies, including an unsupported locale and a redirected root. Both private-fixture runners include it. Keep the original pack and private reference outside its output directory.

For isolated native validation, pass `-ExecutablePath`, `-PackageDirectory`, and `-ReferenceDirectory` together to `text_languages_rt1.ps1`. `-BuildDll` additionally builds the Release DLL; neither option deploys it or launches the game. The runner compiles both native adapters even when private fixtures are absent, but explicitly records that private native execution was not performed. Linux `run_checks.py --sanitizers --pack ... --reference ...` exercises complete pack admission and negative IO/hash cases.

## Migration, missing Hook and future release acceptance

Existing vanilla projects require no migration. Converting to OnlyMod creates a separate recipe/package, never repurposes English or changes a native save ID. Old schema-1 packs remain schema 1 until re-exported; adding events requires schema/API 2, not relabeling incompatible bytes. With the Hook absent or text selection `native`, the original game routes remain available. With another native base language selected, the PT-BR package does not override it.

A future translation pack requires this preflight on its exact bytes, review of its coverage/unsupported glyphs, and live checks of each modified resource family, menus, dialogue options, variables, save/load and unchanged voices. Use `docs/mods/MOD006_RT2_CHECKLIST.md` with separately authorized deployment/live testing. A passing demonstration or source build does not automatically certify later translation content, third-party hooks, a different executable, or an arbitrary existing translation ZIP.
