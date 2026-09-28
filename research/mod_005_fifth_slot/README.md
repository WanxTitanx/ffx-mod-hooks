# MOD-005 structural RT0 probe

See [`docs/research/MOD_005_FIFTH_EQUIPMENT_ABILITY_SLOT_2026-09-23.md`](../../docs/research/MOD_005_FIFTH_EQUIPMENT_ABILITY_SLOT_2026-09-23.md) for the executable identity, IDA observations and implementation scope.

Run against the pinned genuine PC FFX save fixture in the separate FFX Editor checkout:

```sh
python3 research/mod_005_fifth_slot/probe_save_overlap.py \
  /path/to/ffx-editor/FFXProjectEditor.Tests/Fixtures/Save/user_ffx_000
```

The probe verifies fixture size/hash, the 64-byte PC save header, 200 records of 22 bytes, the four existing ability IDs, and the fact that writing a hypothetical fifth `u16` in a **bytearray copy** changes the next equipment name or the first `PlySave` word. It does not write the fixture or implement the mod. No game binary, save or IDA database is included in this branch.
