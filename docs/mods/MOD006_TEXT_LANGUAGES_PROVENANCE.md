# MOD-006 text and font provenance

Jarvis-HOOK, 2026-09-28. This ledger covers implementation and isolated evidence, not a live-game acceptance or permission to redistribute game assets.

## Native executable and resources

The reviewed executable is PE32/i386, preferred ImageBase `0x400000`, SHA-256 `78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced`. Addresses below are RVAs in that executable. All resource inspection used a user's existing installation read-only; generated payloads remain in private `work/mod006/` directories.

| Surface | Address / width / ABI | Evidence and confidence |
|---|---|---|
| Native locale | getter `0x4AC2A0` -> `0x241290`; singleton pointer `0x8DED48`, `u32` at object `+4` | Exact PE instructions and isolated relocated-native calls; high confidence for this image. |
| Stream open | `0x208100`, x86 thiscall, five stack arguments, `ret 0x14`, zero on success | Exact PE instructions and real MinHook/OS-file RT1; high confidence. |
| Stream state | eight bytes: OS handle `+0`, archive pointer `+4` | Open/read/size/close consumers; high confidence. |
| Read / seek / size / close | `0x208250` / `0x2082A0` / `0x207F80` / `0x207F40` | Exact relocated native consumers in the isolated harness. `0x207FC0` is path existence, not seek; the probe label was corrected. |
| Western font registration | `0x4AC0E0`, cdecl pointer; slot 4, 230 metrics, native width pointer `0x1441DA4` | Exact PE plus real registrar RT1. No native font pool or count grows. |
| Save locale | getter call `0x4B3EDA`; byte store to row `+12` at `0x4B3EE3`; mismatch `0x387430` | Instructions and unchanged-code/native-comparison RT1. MOD-006 never patches these locations or writes the locale object. |
| Encoding loader | `0x246AC0` delegates to `0x246AD0` | Exact-byte research only; not detoured by MOD-006. |

VBF members use `ffx_ps2/ffx/master/new_uspc/battle/kernel/` and `ffx_ps2/ffx/master/new_uspc/event/{obj_ps3,obj_psv}/`. Menu/kernel tables use a `0x14` header and 16-byte records, except `btl_txt.bin` with 8-byte records. Field tables have 8-byte records; their first `u16` is also the implicit header length. Runtime and producer therefore use separate validators. Translation never edits field flags, choice counts, native locale IDs, audio paths, or executable event scripts.

The private sentinel pack is generated from original resources with `pack.py`. Source and output hashes in its manifest identify the exact bytes; the VBF header MD5 is an existing archive-integrity check, not a signature of trust.

## Source references and licenses

Hooks is GPL-3.0. The following GPL-3.0 Editor files were consulted read-only at repository `https://github.com/WanxTitanx/ffx-editor-main`, reference commit `2b9f0e0e0827a55d0de444f11f85f6598945eaff`:

- `research_tools/Ps2/vbf_reader.py`: SRYK/VBF block and directory layout. `asset_io.py` adds explicit bounds, checked decompression, header integrity, and selected-member reads.
- `FFXProjectEditor/FfxLib/Ps3/Ps3MagicTextureWriter.cs`: Phyre texture-instance dimensions and mip-zero location. `asset_io.py` validates a unique bounded instance; the PT-BR authoring tool changes only previously empty BC3 blocks.
- `FFXProjectEditor/FfxLib/Text/NameDescriptionTextTable_File.cs`, `BtlTextTable_File.cs`, and `TextTable_File.cs`: resource families, offset widths, suffix sharing and implicit field header. The MOD-006 implementation has its own bounded replacement validation.
- `FFXProjectEditor/FfxLib/Encoding/FfxEncoding.us.cs` and `FfxEncoding.control.cs`: byte mappings and control vocabulary. Current PE/resource bytes take precedence over labels in these references.

Research reports `FFX_MISLABEL_AUDIT_R2_2026-09-16.md` and `FFX_FONT_RUNTIME_2026-09-18.md` supplied investigation starting points, then required addresses and resource bytes were rechecked. `Karifean/FFXDataParser` was an authoring reference only; its code was not copied. The existing NativeLanguage audio service retains its existing UnX provenance and behavior.

## Font profile

`TextLanguagePack.cpp` records exact source/output fingerprints for the complete Western metric and four atlas/shadow resources. An arbitrary self-declared atlas hash is not accepted. Glyphs `ã/õ/Ã/Õ` use `F2/F3/F4/F5`, with advances `31/33/42/42`. `F0/F1` contain native ink and were not reused. Native glyphs, texture allocation sizes, the 230-entry metric count and opaque container bytes stay intact.

The native atlas inspection `work/mod006/cedilla-proof.png` confirms uppercase `Ç` at byte `A7` and lowercase `ç` at `BE`. This supersedes the Editor table's duplicated lowercase label for `A7`. The C++ and Python codecs now map both explicitly; no new atlas output is required for `Ç`.

No game executable, extracted table, font/atlas payload, personal save, image preview, or private reference snapshot is included in the source delivery. A translation distributor must separately resolve rights to its content and use the declared package contract; this implementation does not grant asset redistribution rights.


## API 4 update

See the [public language-pack contract](../TEXT_LANGUAGE_PACKS.md) for bounded
text growth, source-bound controls and the closed UI texture/font profiles.
The public source contains path/size/hash metadata and synthetic tests only.
The game translation and native font/image payloads are not distributed.
