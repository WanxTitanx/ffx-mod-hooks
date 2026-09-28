# Equipment Workshop economy and refinement policy

Jarvis-HOOK — 2026-09-25. This describes the integrated progression source, not
an installed release. Earlier economy validation is recorded separately in
[the economy evidence](reverse/WORKSHOP_ECONOMY_2026_09_25.md); historical test
totals do not validate subsequent progression changes.

## Default B: one guaranteed random improvement

B is the default. Each successful paid refinement increases one eligible ability
by one rank. The maximum remains +10 per ability; maxed abilities leave the pool.
There is no failure roll, equipment breakage, downgrade, or inventory-based
filtering of the eligible abilities.

For a native Customize recipe requiring quantity `Q`, and the next ability rank
`r` (1 through 10), the initial balance is:

```text
Specific ingredient cost = ceil(Q / 10) * r
Base cost                = 1 Power Sphere
Gil cost                 = progressive fee for the next total equipment rank
```

The ingredient ID comes from the real Customize table, not the earlier research
proposal TSV. For example, Auto-Protect requires 70 Light Curtains in the pinned
Customize table: its refinement costs 7 at +1, 14 at +2, and 70 at +10, in addition
to the base sphere for the roll.

Before a roll is allowed, the inventory must cover **every possible next-rank
outcome**. For each item ID, the prerequisite is the largest alternative specific
cost, plus the base cost when that item is also the base sphere. Alternative
recipes sharing an item are not summed. Only the base and the winning ability's
ingredient are actually consumed. For instance, alternative costs of 3 and 7 of
the same item require 7, not 10; if that item is the base sphere, require 8.

The preview shows prerequisites and alternatives, not the selected winner or its
after-image. Canceling or failing a requirement changes no material, Gil, rank,
or stored RNG state. A changed inventory, balance policy, or Gil balance rejects
an old confirmation rather than applying a different price. Native Customize
admission and development-policy changes also invalidate reviewed operations.

### Progressive Gil

Let `n` be the sum of all current individual ability ranks, including the fifth,
plus one. The fee for that next improvement is:

```text
fee(n) = sum(k = 1 .. n, 1000 + 500 * floor((k - 1) / 10))
```

The fee is 1000 at total +1, 10000 at +10, 11500 at +11, 25000 at +20,
27000 at +21, 45000 at +30, 47500 at +31, 70000 at +40, 73000 at +41,
and 100000 at +50. These are per-improvement prices, not lifetime expenditure.
Mode A pays the sequential fees for all ranks it adds: four initial improvements
cost 1000 + 2000 + 3000 + 4000 = 10000 Gil. Individual ability caps remain +10.

## Fifth slot and character binding

Unlock requires an ordinary unequipped piece with four OPEN native slots. They
may be empty, partly filled or fully filled. This is the user's corrected rule
of 2026-09-26, superseding the earlier four-empty interpretation. Unlock keeps
existing native abilities, ranks and identities unchanged.

The price is ten owner spheres. Spend the specific sphere first; Master Sphere
substitutes for any shortage one-for-one, including ten Masters when no specific
spheres are available.

| Character | Specific sphere | Item ID |
|---|---|---:|
| Auron | Attribute Sphere | 75 |
| Kimahri, Rikku | Special Sphere | 76 |
| Tidus, Wakka | Skill Sphere | 77 |
| Yuna | Wht Magic Sphere | 78 |
| Lulu | Blk Magic Sphere | 79 |

Master Sphere is item 80. Seven Special Spheres plus three Masters unlock
Kimahri's fifth slot atomically. Nine combined spheres fail without debit.

Unlock binds the equipment to its existing character, even while the fifth is
empty or after its ability is removed. Inventory-slot movement is not a character
transfer. Reforge cannot change character; it also cannot change equipment type
while a fifth ability is installed.

Installing or replacing a fifth ability additionally requires all four native
abilities to be filled. Zero and 0x00FF both mean empty. Development exemptions
do not bypass this order. Clearing a native ability does not erase an existing
fifth, remove binding, or invalidate an older save, but another fifth placement
is blocked until the four native abilities are filled again.

Installing or replacing a supported fifth ability costs
`ceil(3 * native Customize quantity / 2)` and **200000 Gil**. Its recipe must match
weapon/armor type. New fifth abilities do not inherit old refinement ranks.
Clearing uses the existing clear recipe and preserves binding. Fusion may neither
read nor write the fifth slot; evolution cannot bypass fifth customization fees.
Native capacity remains four and fifth data stays in the extension.

## Fusion: donor, ingredients, and Gil

The initial price for each transferred ability is:

```text
Ingredient cost = ceil(native Customize quantity / 3)
Gil cost        = 10,000 per transferred ability
```

One or two selected abilities can be transferred. Costs for simultaneous
transfers are summed, including shared item IDs. The rounding happens per
ability before aggregation. A one-unit Customize ingredient therefore still
costs one; nothing becomes free through integer truncation.

The donor is destroyed once, after the reviewed operation is admitted. Its
selected ability identities and individual ranks follow the transfer. The
unselected identical equipment, native donor-consumption logic, and menu audio
are preserved. Gear, item quantities, and Gil belong to the same reviewed memory
transaction; readback failure attempts rollback only of bytes still owned by
that transaction and closes admission on conflict.

The division, rounding rule, base sphere, rank formula, and 10,000-Gil starting
price are implementation balance choices. The user's requirement was progressive
Customize-based refinement and fusion costing Gil plus approximately one third
of the Customize materials; these numbers remain adjustable.

## Optional A and existing saves

Use **F8 > Dev > Equipment Workshop > Refinement method** to select A or B. This is a choice page,
not a bulk-toggle flag. Missing configuration selects B. Choosing a mode alone
does not modify any equipment or rank.

A increases every occupied, non-maxed ability by one rank without RNG. It pays
the corresponding next-rank ingredient for each ability and one base-sphere
charge per improved ability, plus sequential progressive Gil fees. An uneven B piece stays uneven; A does not round
it up or discard its higher ranks.

Persisted v1 `Piece` (80 bytes) and `State` (16,260 bytes) remain unchanged. When
refining or fusing a legacy A piece, its piece-wide rank is expanded losslessly
into individual ranks inside the successful transaction. This uses the existing
v1 individual-rank representation; no sidecar-format migration or free regrading
is performed. Failed previews and canceled edits leave the old representation
unchanged. Native Gil remains in the native save, not the sidecar.

Save normally in the game after editing. Backups still need both native saves
and the Workshop sidecar directory. Different-save isolation and existing
path/content binding are not replaced by the global refinement preference.

## Non-customizable and modded abilities

The pinned table contains 125 native recipes. Six supported ability IDs have no
native Customize recipe: 20, 83, 122, 123, 129, and 130 (offset from `0x8000`).
They use an explicit **mod-only Ability Sphere recipe**, with a synthetic
reference quantity of 30 by default. That produces 3 Ability Spheres at +1,
30 at +10, or 10 for a fusion transfer under the default divisors.

This is not claimed to be a vanilla recipe. Native confirmation explicitly
displays `Includes a mod-only recipe.` when an eligible refinement outcome or a
selected fusion transfer uses it. Maxed abilities and unselected donor abilities
do not trigger that disclosure or their costs.

Abilities outside the closed 131-entry catalog remain blocked. Fifth-slot
creation now covers that complete catalog rather than only the 26 bespoke
refinement effects. The 125 stock recipes retain their weapon/armor type;
the six entries without a stock recipe use the explicit mod-only fallback
for either type (45 Ability Spheres by default after the fifth-slot multiplier,
plus 200000 Gil). F10 marks these choices and confirmations as mod recipes.
The generic refinement policy is independent and unchanged. The Python host
queries the same type/cost rules instead of maintaining a second ability list.
A mod changing
`kaizou.bin` must regenerate and review the table; the runtime does not silently
load new recipes from the extracted-files directory.

## Configuration

Ordinary access follows native Customize admission. It reads the unsigned story
value and never writes story progress. The pinned game admits story zero (its
debug/all-menus case) or at least `0x448`; see [native admission evidence and future
visual research](reverse/WORKSHOP_NATIVE_UI_RESEARCH_2026_09_25.md). Access locks
prevent Workshop actions, not effects on previously refined gear.

The dedicated F8 page exposes three independent development flags, all OFF by
default. Free materials preserves nominal prices and requires at least one of
every required ingredient, or one owner/Master sphere for unlocking. Free Gil
needs no currency and charges zero. Ignore progression bypasses only Customize
admission. Confirmation explicitly reports exemptions. Fusion still destroys its
donor; identity, equipped-item, save, and transaction checks remain effective.

Merge this section into the existing `ffx-hooks.ini` used by F8; do not replace
unrelated sections or create a second configuration authority. No installed INI
was changed by this implementation.

```ini
[equipment_workshop]
refinement_mode=2
expansion_recipe=1
base_sphere_item=70
base_sphere_amount=1
refinement_divisor=10
fusion_divisor=3
fusion_gil_per_ability=10000
mod_recipe_quantity=30
dev_free_materials=0
dev_free_gil=0
dev_ignore_progression=0
```

`refinement_mode` accepts 1 (A) or 2 (B). Base item IDs 70–73 are Power, Mana,
Speed, and Ability Sphere. Base amount accepts 1–99, both divisors 1–100, Gil
per ability 1–100,000,000, and mod recipe quantity 1–255. Invalid values fail
closed; zero is not a free-cost or disable switch. Costs above available stack
capacity are rejected, not clamped or silently discounted.

Development flags accept only 0 or 1. The transient planning ABI is version 3:
Policy 40 bytes, Economy 48 bytes, Plan 16772 bytes. The Python host checks these
sizes and version. Old planning entrypoints reject calls without writing into
older, smaller buffers. Persisted equipment layouts remain unchanged.

## Comparison UI

### Navigation and expansion preference

Back restores the immediate parent, cursor/scroll and partial fusion choices. Leaving transfer two keeps transfer one. Inventory drift discards obsolete history; committed fusion cannot revive its donor menu. Inventory retains the target comparison card.

F8 > Dev > Equipment Workshop > Expansion recipe chooses A (default: one matching Key Sphere per added slot) or B (slot-number quantity 1/2/3/4). Two-to-four costs one Lv3 plus one Lv4 with A, or three Lv3 plus four Lv4 with B. Existing slots are not recharged. The picker shows only the selected recipe; changing it invalidates old confirmations. Fifth-slot recipe and Dev exemptions are separate.

See [native details](reverse/WORKSHOP_NATIVE_UI_IMPLEMENTATION_2026_09_25.md) for optional fifth rows and individual rank suffixes.
The Workshop header now reports whether that native detail hook is actually ON
or OFF. Enabling Workshop alone does not enable its separate native-details
gate; use F8 > Reforge > Native equipment details and restart. See the
[slot-order and Scan follow-up](reverse/WORKSHOP_SLOT_SCAN_FIX_2026_09_26.md).

### Comparison cards

The upper card retains the selected target. The lower card follows the focused
equipment, donor, or model reference during browsing. Source and destination
selection retain both cards. Fusion identifies the donor as consumed on
confirmation. Deterministic edits can show before/after; random B instead shows
eligible outcomes and prerequisites without leaking the winner.

## Recipe provenance

The generated `research/equipment_workshop/include/customize_recipes.h` was
read-back checked against both supplied `jppc` and `inpc` kernel tables under
`FFX Extracted/FFX/ffx_ps2/ffx/master/`. Both `battle/kernel/kaizou.bin` files have
SHA-256 `fea70d34a8567260e5b27da19eb86234ec02c6ab1e59c774f3b799ccd15ed844`.
The parser requires the bounded 20-byte header, eight-byte records, unique
ability IDs, valid item IDs and positive native quantities. The extracted binary
is input-only and is not committed to the repository.

See [battle return, full fifth catalog and extra elements](reverse/WORKSHOP_BATTLE_ELEMENTS_FIX_2026_09_26.md) for the latest runtime corrections and validation.


## Aeon equipment extension

Acquired, progression-unlocked Aeons use one 2x Gil multiplier. Fifth unlock
consumes four of each distinct Attribute, Special, Skill, Wht Magic and Blk
Magic Sphere, with cumulative 1:1 Master substitution. Aeon Immunity is permanent
and never a refinement outcome. See [native gates and validation](reverse/AEON_WORKSHOP_2026_09_27.md).
