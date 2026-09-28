# Named auto-ability expansion — Jarvis-HOOK

Worktree: `/home/wanderson/.codex/worktrees/aeon-exclusive-breaks/ffx-hooks`.
Branch: `codex/aeon-exclusive-breaks-plan-20260927`, based on `3b010845`.
Runtime source reference: `/home/wanderson/Documents/ffx-hooks`, main `f2308ddd`.
Editor: `/home/wanderson/Documents/ffx-editor-main`, `codexclaudiocodeffxeditor`,
source snapshot `39916423`. This is data authoring, not a runtime implementation.

- `definitions.json` freezes the 27 new records148–174 and their exact payloads.
- `registry.json` consolidates the40 managed IDs135–174, including existing MOD-002.
- `source_inventory.json` pins the25 files **before** this append.
- `staging_validation.json` records13 headless Editor reader checks on staged files.
- `application_report.json` records the actual-file readbacks/readers and external backup.
- `author.py` appends records and text, preserves previous rows/pool, and expands rates.
- `test_author.py` uses synthetic/local temporary files; it never writes game assets.

Applied backup:
`/home/wanderson/.codex/backups/autoability-expansion-20260927T203501Z/manifest.json`.
Game binaries, extracted staging files and backups are deliberately outside Git.

```bash
python3 -m unittest discover -s research/autoability_expansion -p 'test_*.py' -v
python3 research/autoability_expansion/author.py --verify '/home/wanderson/.codex/backups/autoability-expansion-20260927T203501Z/manifest.json'
# Only when rollback is intended; later edits are rejected:
python3 research/autoability_expansion/author.py --rollback '/home/wanderson/.codex/backups/autoability-expansion-20260927T203501Z/manifest.json'
```

The default dry-run and `--apply` intentionally reject the already changed inputs.
They are not a general installer for arbitrary revisions. A future append needs a
new reviewed inventory/ID range and its own backup; never bypass the identity gate.

Exclusive/undefined effects remain neutral. Drop155/156 use markers0x1000/0x2000
for the existing Hook. Four172/173 have a native elemental base, with status riders
pending. Native rows carry only declared fields. CJK names are numeric placeholders;
Latin names/descriptions use the inspected FFX single-byte character map.

Owner, gear category and Workshop-only fields are future Hook contracts. They are
not native restrictions supplied by a text record. The current Workshop catalog
must be extended before these records can be offered safely there.

See [applied report](../../docs/research/AUTOABILITY_SPIRA_AEON_CREATION_2026-09-27.md)
and [the complete ability document](<../../docs/mod-ideas/PARTE 2 MOD 002.md>).
