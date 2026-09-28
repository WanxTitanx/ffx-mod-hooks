# MOD-007 research packet — Jarvis-HOOK

Repository: `/home/wanderson/Documents/ffx-hooks`. Packet worktree:
`/home/wanderson/.codex/worktrees/mod-007-elemental-dominion/ffx-hooks`, branch
`codex/mod-007-elemental-dominion-20260927`, documentation base `f53aa3b0c7526a15b667fe317665194004ad6f07`.
Current runtime inspected separately at `main` commit `f2308dddc1833811899c0999c96b5ee25a18bd66`.
Editor source: `/home/wanderson/Documents/ffx-editor-main`, branch `codexclaudiocodeffxeditor`,
snapshot `399164236638bd34d44c4833b02ea3b15a49d771`.

This packet does not install a hook, write a game asset or implement the new combat system.
The native probe executes one hash-pinned original routine in a freestanding ELF i386
process, with synthetic actor buffers and no game process. The design checker tests
proposed rules and example data; it cannot prove a runtime adapter works.

- [Design](<../../docs/mod-ideas/MOD 007 - ELEMENTAL DOMINION.md>)
- [Evidence and constraints](../../docs/research/MOD_007_ELEMENT_REGISTRY_FEASIBILITY_2026-09-27.md)
- [Implementation plan](../../docs/research/MOD_007_IMPLEMENTATION_PLAN_2026-09-27.md)
- `example_manifest.json`: ten stable element keys, eight optional native-bit bindings,
  two external keys, 15 affinity tiers; **not loadable by the existing Hook**.
- `native_validation.json`: actual extracted routine results, 403809 comparisons,
  eight individual bit controls, code/executable/harness hashes and limitations.
- `design_validation.json`: proposed-contract check result, not a gameplay test.
- `source_inventory.json`: inspected source and fixture identities; no binary payloads.
- `spell_cap_validation.json`: static cap bytes/command flags and proposed 9999/999999
  spell-cap policy checks; no replacement runtime clamp is executed. Fury needs explicit
  spell bindings because its native magical-damage flag is unset in the inspected rows.

## Reproduce

Requires Linux x86 with ELF i386 support, GCC, Python with `pefile` and `capstone`.
These were already available on the research host. No dependency installation or game
launch is performed by the commands. Supply your own supported local executable.

```bash
python3 research/mod_007_elements/probe_native.py '/path/to/FFX.exe' --output /tmp/mod007-native.json
python3 research/mod_007_elements/validate_design.py
python3 research/mod_007_elements/probe_spell_cap.py '/path/to/FFX.exe' '/path/to/command.bin' --output /tmp/mod007-spell-cap.json
python3 research/mod_ideas_precode/generate_ledger.py --check
```

The first command rejects an unknown full-EXE SHA or function SHA before executing.
Only the position-independent 1045-byte routine is linked. Temporary extracted bytes
and executable are deleted; they must not be added to Git. The second command uses
Python integer arithmetic; it explicitly demonstrates why a bounded i64 intermediate
and a separate final i32/game cap are required in the future native implementation.

## Provenance

The probe harness and proposed-contract checker are original research support code,
derived from local byte observations and the requested design. No Fantasia/Fahrenheit
implementation was adapted. Fantasia (MIT) and Fahrenheit (LGPL-3.0-or-later) were read
as references; pinned commits, public links and used surfaces are in the evidence report.
Local wiki pages were used for design context, not engine proofs or redistributable assets.
No FFX executable, extracted instruction blob, game data or wiki corpus is in this packet.
