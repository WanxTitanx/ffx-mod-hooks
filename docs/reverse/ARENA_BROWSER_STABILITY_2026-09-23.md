# Jarvis-HOOK — Ultra Back crash, native search and complete battle catalog

## User evidence and root cause

The user reports a crash immediately after Back from Arena Creations with a varied
roster. The latest `%TEMP%/ffx-hooks.log` was preserved before any new deployment.
It has no fatal PC/stack. The `CoreDump.dmp` beside the game is dated September20
and is not evidence for this crash.

The actual `ArenaPlus_BuildUltraPreview` appended every full monster name and a
status suffix to a 64-byte buffer with `strcat_s`. Returning from a category rebuilt
that subtitle. An isolated MSVC harness compiling the exact production function
reproduced six CRT invalid-parameter calls and12/24 failed assertions for varied
Arena rosters. Default CRT handling can terminate the process without an access
violation, which fits the absent AV log and the reported Back trigger.

The replacement prints a bounded slot/status/layout summary. Names are displayed
individually in a permanent formation pane. The production-function regression
passes27/27 on Windows and27/27 on Proton, with zero CRT invalid-parameter calls.
Confidence is high for this reproduced defect; absence of a crash stack means it
is not proof that every possible crash is eliminated. Native interaction acceptance
remains a user-run step.

## Native interface

- Arena selection uses the same44-frame glass lift and thin green edge as the F7
  hub. Opaque vertical/accent rails were removed. The existing reduced-motion guard
  remains; both isolated Windows and Proton queries returned animation enabled.
- Ultra uses a left browsing column and a persistent right formation pane with
  eight slots, position overview, arena and music. This is native FFX rendering.
- Search All Monsters and category-local Search accept typed ASCII, match words
  case-insensitively across names/keys/categories, and preserve the roster.
  Enter applies; Esc cancels; Ctrl+A selects the query for replacement.
- Arena and Battle Presets lists have search. Battles can be found by battle ID,
  arena label or opponent name. Selecting a battle opens a roster detail view with
  **Use This Lineup**, **Use This Arena Only**, and Back. Nothing launches on browse.
- Add/Back breadcrumbs record choice IDs and activation counts, not search text.

## Actual game catalog

The old preview was based on a partial mods directory. The new builder reads the
owned `data/FFX_Data.vbf` index and `ffx_ps2/ffx/master/jppc/battle/kernel/btl.bin`.
It finds863 canonical battle files and90 kernel battlefield selectors; zero is a
system sentinel, so89 are selectable. Preserving older named spacing variants
produces93 arena options and186 arena/camera profiles.

The existing18 profiles are byte-identical. New arenas use the already reviewed
normal main/camera program, with no donor boss script or per-monster worker map.
Their first-area geometry comes from an encounter with the same battlefield ID;
the produced manifest reports84/84 new choices as `area-zero`, with no regional or
generic geometry fallback used. First-area normalization limits party anchors to
seven and monster positions to eight. New standard camera framing is adapted to
the area's basis; tactical framing retains the closer preset.

Battle Presets load supported opponent lineups into the normal Mix composer.
They do not replay the original story script. Unsupported actor groups remain
**Encounter Only**, and unresolved preset terrain explicitly keeps the current
arena. Importing a lineup does not bypass Arena/Dark progression. Ordinary monster
AI, special environmental behavior and every visual arena fit are not proven by
the offline catalog or pointer tests.

Private bundle format `ARPROG02` adds bounded encounter records after the profiles.
Its existing filename is retained. The whole bundle is SHA-256 pinned, limited to
8 MiB and1024 encounter records; only validated catalog symbols can become writer
input. No proprietary battle payload is committed. The native shader/resource
selector remains the previously proven low uint16 at RVA`0x00D2C254`; no new hook,
arbitrary selector or original-asset rewrite was introduced.

Executable: FFX.exe PE32/I386, preferred base`0x00400000`, SHA-256
`78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced`.

## Validation

- Actual summary/search harness:27 Windows +27 Proton; RED12/24 before the fix.
- Core907, Windows adapter57, runtime89, F73618, F83575, UI59+contracts pass.
- Scenery transactions/layouts:1485 checks across all93 options pass.
- Native mapped-executable consumers:18474 Windows +18474 Proton checks pass,
  in four fresh processes so the256 published-frame lifetime bound is respected.
  An initial harness run rejected its new batch arguments; the harness contract
  was corrected and all four batches rerun. This was not a game-runtime failure.
- Library/catalog:720 Windows;725 Proton including five additional old-camera
  fixture assertions. Supported lineups preserve music and remain within eight
  slots. Unsupported groups are not silently dropped.
- The first18 profiles compare byte-for-byte with the installed predecessor.
  Regeneration from the new pack reproduces both pack and scene registry exactly.
- Release and exact DLL loader pass. No game launch or RT2 was performed by Jarvis.
  Visual behavior, live Back/search navigation, expanded-arena gameplay and
  independent review remain pending. No Production promotion.

## Provenance and packet

`tools/arena_profiles/catalog.py` imports the existing read-only GPL-3.0
`ffx-editor-main/research_tools/Ps2/vbf_reader.py` supplied explicitly on its CLI.
Its reader hash is recorded in the generated private manifest. Kernel layout was
checked against GPL-3.0 `FfxLib/Battle/EncounterTable_File.cs`; no external runtime
implementation was copied. Names/selectors are factual metadata; scripts,
formation payloads and captured game logs remain in the private packet.

Packet: `.superpowers/sdd/2026-09-23-ultra-browser-stability-030041Z/`.
It contains the pre-change source baseline, preserved logs, RED/GREEN phases,
private corpus/bundle manifests, native batch logs and deployment readback.

## Installed readback

Paired deployment completed2026-09-23T04:26:08Z in the existing authorized testing
workflow. DLL SHA-256`fa5530226f288c658ba700b82614600e8d0f47cc290a2f4c5fb75f6b987a3c3d`,
1,626,624 bytes; bundle SHA-256
`9bb12c51f886df68c059b2834c6b7053e97e60fdfd9d3c742905bdaa278c1c2a`,4,576,340 bytes.
Backups of both previous files are adjacent to their targets with suffix
`.pre-ultra-browser-20260923T042608Z.bak`. Four absence checks passed;314 runtime
inputs matched and44 other protected file states were unchanged. Settings were not
changed, and no game was started by Jarvis. See the packet's deployment receipt.
