# Research provenance — Jarvis-HOOK, 23 September 2026

## Adapted behavior

- Origin: https://github.com/EvelynTSMG/ffx-mods-fantasia
- Source: src/balance/elemental_affinities.cs, commit 64b03ae915c32f47d4a84aefffcd3130cb13ddce.
- License: MIT; the upstream license text is copied to licenses/Fantasia-MIT.txt.
- Adapted part: the order and arithmetic of Favorable, Balanced, Unfavorable, and Extra Mean affinity modes in the standalone C++ reference model. The C++ source is independently written and has no dependency on Fahrenheit or Fantasia at build time.
- Validation: source inspection and pure-model tests. No FFX gameplay validation or release of the Fantasia mod was inferred.

## Read-only references

- Fahrenheit, local commit c149c847b3a24a66114956f87f1b008599736f75, LGPL-3.0-or-later: generated FFX call table and ElementFlags were read to cross-check the four-argument cdecl affinity hook. No Fahrenheit code was copied.
- FFX Editor, local commit e5f05554426f27a83d4ee70f7be32695431ac66b: ability, gear, Mix, player and save source were inspected. The save diagnostic compiles FfxSaveEquipment.cs directly from a user-supplied Editor worktree at test time; that source is not copied into this branch.
- FFX Customizable Battle Tweaks, local commit cb48450850e5: README and ATEL/data scripts were inspected as examples. No top-level license was located; no code was adapted.
- FFX The Challenge HD, local commit dd33b940a708: documentation and data-file strategy were inspected. No top-level license was located; no code was adapted.
- FFXDataParser, local commit 13607aec9bd0: file-format reference only. No top-level license was located; no code was adapted.

Game files, executables, saves, VBF archives, DLLs, IDA databases, sidecars, and derived binaries remain outside Git.
