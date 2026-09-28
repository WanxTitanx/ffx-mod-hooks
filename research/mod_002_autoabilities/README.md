# MOD-002 hook-only auto-ability data authoring

**Hooks repo/worktree:** `/home/wanderson/.codex/worktrees/mod-002-parts/ffx-hooks`, branch `codex/mod-002-parts-20260927`. The [Part 2 MOD-002 document](<../../docs/mod-ideas/PARTE 2 MOD 002.md>) assigns default ability IDs 135–147 and requires a future Hook menu to remap them safely. No gameplay handler or ID-remapping menu is implemented here.

**Source roots:** Steam `/mnt/nvme-samsung/SteamLibrary/steamapps/common/FINAL FANTASY FFX&FFX-2 HD Remaster/data/mods/ffx_ps2/ffx/master/`; extracted data `/home/wanderson/Documents/ffx-editor-main/docs/history/DOSSIÊ FFX 01-06-2026/ffx-editor-pt29__PT29_DOSSIE_COMPLETO_CHAT/dependencies/D/FFX Extracted/FFX/ffx_ps2/ffx/master/`; Spira Reforge `/home/wanderson/Documents/ffx-editor-main/mods/Spira Reforge/data/mods/ffx_ps2/ffx/master/`. The Editor checkout was on `codexclaudiocodeffxeditor` during the 27/09/2026 snapshot. [`source_inventory.json`](source_inventory.json) pins the 25 original target paths, hashes, sizes and maximum IDs; it contains no game bytes.

[`default_ids.json`](default_ids.json) provides stable effect keys and default IDs 135–147 for the future Hook menu. `author_hook_only_abilities.py` reads this manifest and appends 13 rows to `a_ability.bin` in each of 10 Steam locales, extracted `new_uspc` and two Spira locales. It also extends the matching `arms_rate.bin` tables with zero prices. Gameplay fields `+0x10..+0x6B` are zero in each new row, so **the Hook must implement every new effect**. Latin locales have provisional English text; Japanese, Chinese and Korean rows have numeric temporary labels. The extracted baseline ends at ID 133, so it gets a neutral ID 134 bridge; existing Steam/Spira ID 134 remains byte-for-byte intact.

The authoring script has three modes:

```sh
python3 research/mod_002_autoabilities/author_hook_only_abilities.py
python3 research/mod_002_autoabilities/author_hook_only_abilities.py --apply
python3 research/mod_002_autoabilities/author_hook_only_abilities.py --verify /path/to/backup/manifest.json
```

The default is a dry plan that refuses any source hash drift. `--apply` creates verified backups before any replacement, writes each file atomically, reads back all 25 and records an external manifest. A failed transaction attempts rollback. Manual rollback is available only when each current hash is the planned before/after value:

```sh
python3 research/mod_002_autoabilities/author_hook_only_abilities.py --rollback /path/to/backup/manifest.json
```

**Actual local application on 27/09/2026:** backup manifest `/home/wanderson/.codex/backups/mod002-autoabilities-20260927T162559Z/manifest.json`; `--verify` passed 25/25. The FFX Editor `--autoability-rt0` parser/readback from clean detached checkout `/home/wanderson/.codex/worktrees/mod-ideas-precode/ffx-editor` at `e5f05554426f27a83d4ee70f7be32695431ac66b` passed 13/13 installed ability/rate pairs with `DOTNET_ROLL_FORWARD=Major`. No game launch or RT2 occurred. Binaries and backups are intentionally excluded from this Git branch; the two extracted and three Spira files in the Editor checkout are locally modified and must not be staged as proprietary assets without a separate release decision.
