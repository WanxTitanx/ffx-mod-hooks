# MOD-004 refinement RT0 model

The [refinement report](../../docs/research/MOD_004_EQUIPMENT_REFINEMENT_2026-09-23.md) separates game evidence from proposed costs and bonuses. This pure Python probe does not edit or load game data into FFX and does not implement a hook.

Run with the pinned FFX Editor checkout and optional locally extracted `a_ability.bin`:

```sh
python3 research/mod_004_refinement/probe_refinement_design.py \
  --editor-root /path/to/ffx-editor \
  --ability-bin /path/to/a_ability.bin \
  --check-catalog
```

It verifies 131 named auto-ability IDs, 112 vanilla item IDs, one candidate ingredient family for every named ability, explicit unresolved IDs 20/29/123, example global +1/+10 receipts, replacement catch-up cost, stack bound, random eligible-slot selection, insufficient-material no-op, and four/five-slot maxima of +40/+50. With the optional pinned kernel it checks Auto-Protect/Auto-Haste bits and a numeric stat ability. The extracted binary is not part of this branch. Proposed ingredient associations and effect gates are not vanilla customization recipes or live gameplay results.

[`ability_ingredient_candidates.tsv`](ability_ingredient_candidates.tsv) is generated from the pinned Editor dictionaries plus the proposal map in the probe. Run `--write-catalog` only after deliberately changing that map; review its complete diff before committing. `--check-catalog` confirms the checked-in TSV matches those sources and rules.
