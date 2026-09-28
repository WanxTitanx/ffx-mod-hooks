# Editor handoff: Equipment Workshop — Jarvis-HOOK

## Branch, scope and status

Hooks branch: `codex/mod-ideas-precode-20260923`, base commit
`c582ae53f77dc5de951048c5e870d3bdc49a17e1`. Implementation commit is recorded in
the delivery checkpoint below. Worktree:
`/home/wanderson/.codex/worktrees/mod-ideas-precode/ffx-hooks`.

The separate Editor worktree was **read only**. No Editor change is required to
run this standalone experimental host or the native RT1 harness. A future Editor
integration needs explicit extension UI/format handling and the existing native
save-writer offset fix below; do not silently add a fifth vanilla field.

This is functionality of **Spira Reforge**, not a vanilla FFX capability.
There is no installed equipment DLL, in-game menu, native save callback, RT2,
independent review, PR, merge or Production promotion in this delivery.

## [DERIVADO DE MOD-004] Operations and refinement

The C++ implementation is `research/equipment_workshop/src/workshop.cpp` with
wire structs in `include/workshop.h`. The dedicated UI and Python host call that
same library, not a separate reimplementation of the rules.

- Reforge changes owner/type/name/model and template-dependent formula/power/crit
  using a template from the imported native inventory. Unknown bytes, piece ID,
  abilities and ranks are retained. Prototype price: Master Sphere#80 ×1.
- Fusion transfers up to two ability instances into available slots and consumes
  the donor. Source instance IDs and B ranks follow the actual transferred
  abilities; overwritten identities retire. Celestial/Brotherhood donors are
  blocked. They may receive without clearing special flags. Both pieces must
  have the same refinement mode. Price: Ability Sphere#73 ×number of transfers,
  plus A catch-up when applicable. Duplicate ability IDs are allowed as distinct
  instances; they are not mistaken for shared metadata.
- Slot expansion stays≤4. Kari recipe uses one Key Sphere per new slot; Dawn
  uses quantities1/2/3/4. Item IDs81–84 are Lv.1–4 Key Spheres.
- Clear costs Clear Sphere#95 ×1 and retires that ability's identity/rank.
  Evolution supports the explicit24 numerical sequences and eight
  Touch→Strike pairs (e.g. Darktouch#71→Darkstrike#70). It creates a new ability
  identity; B rank resets0. Price: Attribute Sphere#75 ×1 plus A catch-up.
- A: global rank0–10; each occupied eligible ability contributes its own base
  ingredient `1+floor((rank-1)/3)` and one milestone ingredient at4/7/10.
  Catch-up includes every missing rank when adding/replacing an ability.
- B: per-instance rank0–10; one eligible ability is selected uniformly with
  rejection sampling. Empty/maxed abilities are excluded. Generator state and
  successful-roll count persist. Costs are known before revealing the result.
  Four/five occupied abilities cap at+40/+50. Owner catalyst IDs for
  Tidus/Yuna/Auron/Kimahri/Wakka/Lulu/Rikku:72/78/87/73/93/79/76, plus73.
  Kimahri's two identical material IDs aggregate into quantity2.
- A/B mode selection is permanent for the piece in this prototype; no conversion
  silently grants ranks. Equipped ordinary gear cannot be edited. Event adapters
  separately track the actual native equip/swap/free operations.

Executable effect slice: numerical abilities98–121 gain one percentage point per
rank in a private kernel row; Auto-Shell84/Auto-Protect85 retain the native flag
and gain a bounded post-status reduction of1% per rank, highest matching rank,
max10%. No AP20, No Encounters29, Aeon Immunities123 and all other unimplemented
effect families block refinement **before payment**. The131-row ingredient
catalog is still proposal data, not131 implemented effects or vanilla recipes.

## [DERIVADO DE MOD-005] Logical fifth ability

Four native slots are required before unlocking the fifth. Prototype unlock cost
is Lv.4 Key Sphere#84 ×1. Selection uses an eligible ability's base rank1 recipe,
plus A catch-up if the piece is already refined. The fifth has an independent
ability-instance ID and B rank. It can be displayed, replaced, cleared and used
as a fusion destination/source while preserving piece identity.

The fifth **never** becomes native capacity5 and is never written at native+22.
Only a temporary24-byte consumer view contains that fifth WORD. Original gear
records, source kernel rows and the following equipment/PlySave remain intact.
RT1 proves the native numerical consumer and full actor aggregation can consume
the additional word with the appropriate bounded loop changes. Full installed
consumer coverage, native menus/previews/names/prices and direct-ID queries are
still integration gates.

## [DERIVADO DE MOD-004] [DERIVADO DE MOD-005] Formats and contracts

Native gear:22 bytes; exists+2, flags+3, owner+4, type+5, equipped_by+6,
formula+8, power+9, critical+10, capacity+11, modelu16+12,
abilitiesu16 at14/16/18/20. Empty native ability00FF or0000; logical empty00FF; known native ability code8000+ID.
PC file fixture:0x6900 (26,880) bytes,0x40 (64)-byte header, equipment payload0x449C/file0x44DC,
200×22 bytes, then PlySave payload0x55CC/file0x560C. Native inventory types are
payload0x3ECC/file0x3F0C (256u16 words), quantities payload0x40CC/file0x410C (256u8);
the host recognizes item words0x2000–0x206F and preserves all other bytes.

**Existing Editor defect remains separate:**
`FFXProjectEditor/FfxLib/Save/FfxSaveEquipment.cs` in the inspected Editor branch
reads/writes abilities at15/17/19/21. Those offsets are one byte late for this
22-byte native layout. Fix and test that writer independently before using it
for a real-save Workshop integration. No silent Editor patch was made here.

The v1 mod bridge is little-endian, packed and independent of the native layout:

| Type | Size | Fields/offsets |
|---|---:|---|
| Piece |80| native22@0; pieceIDu64@22; modeu8@30; globalRanku8@31; fifthUnlockedu8@32; ranks5×u8@33; abilityIDs5×u64@38; fifthu16@78 |
| State |16260| versionu32@0; revisionu64@4; nextIDu64@12; rngu64@20; successfulRollsu64@28; pieces200×80@36; materialCounts112×u16@16036 |
| Request |62| opu32@0; revisionu64@4; pieceIDu64@12; otherIDu64@20; slot/other/valueu16@28/30/32; from2×u8@34; to2×u8@36; count/policyu8@38/39; verifiedTemplate22@40 |
| Plan |16492| afterState@0; aggregatedCosts112×u16@16260; chosenAbilityIDu64@16484 |

Sidecar JSON envelope: `payload` plus SHA-256 of canonical compact sorted-key
ASCII JSON. Payload fields: `schema=1`, `save_id` (random32-char identity),
`workspace_key` (SHA-256 of resolved workspace path bytes), `native_sha256`,
`state` (Base64 of exactly16260 bytes). SHA is integrity detection, not a signature.
The reader must validate all dimensions, unique IDs, ranges and byte association;
it must not recover identity by matching equipment names or fingerprints.

Host files: `origin.json`, `native.bin`, `sidecar.json`, paired backups and
transient `pending.json`. A durable journal resolves only an exact before/after
pair. A changed/corrupt/foreign save, copied workspace, stale confirmation or
unknown inventory producer quarantines metadata. `native.bin` is an experimental
working snapshot **without supported game-save export/checksum integration**;
never install it as a player's game save.

Recommended future Editor UI: a separate explicitly labeled Spira Reforge
extension inspector/importer with association/version status, immutable piece
and ability identity, refinement mode/ranks, fifth ability and recovery state.
Do not insert a fifth vanilla field in `weapon.bin` or the existing save record.
Never write partial metadata for one piece without the sidecar revision and
transaction contract. A game-side lifecycle provider is required before the
Editor can promise that a loaded extension still belongs to the same live piece.

## Verification and remaining gates

Core165 assertions: Linux, ASan/UBSan and MSVC x86. Host/controller19 tests with
real-save unchanged-source check. Native595 assertions: Windows and the same
MSVC binary in isolated Proton. Native proof includes full actor aggregator,
Protect/Shell leaves, all24 numerical abilities/ranks via a documented consumer
bridge, and actual native create/swap/equip/free producers. Read the limitations
in `docs/reverse/EQUIPMENT_WORKSHOP_NATIVE_2026_09_24.md`.

Next gates: all producer and save/load coverage, game-side owner-thread admission,
the standalone native menu/render/focus adapter, remaining effect families and
all affected consumers, independent review, then separately authorized deploy
and reproducible RT2 with a disposable save. No additional new vanilla item IDs,
automatic keybind or production enablement is part of this prototype.

## Main runtime follow-up

The Hooks main checkout now provides the native save/lifecycle/menu adapter.
See `docs/reverse/MAIN_WORKSHOP_SIN_2026_09_24.md`. Its save extension is binary
`FFXWKS01` plus path, full native image and state hashes and the packed v1 state.
The earlier research host JSON is not that game's storage format. Any Editor
writer must implement this exact association contract and validate the current
native gear layout before editing it. No Editor source has been changed here.
The existing [DERIVADO DE MOD-004] / [DERIVADO DE MOD-005] scope tags still apply.
Player RT2 and independent review are pending.

## Delivery checkpoint

Implementation commit: `f480708eae1ac32322428bff3bb66f48c713e8a9`.
Base research commit: `c582ae53f77dc5de951048c5e870d3bdc49a17e1`.
The subsequent documentation checkpoint records these identities; its pushed
HEAD is reported in the task delivery. Both remain on the isolated branch.
