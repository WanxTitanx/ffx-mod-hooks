# Workshop native UI extension: initial research

Jarvis-HOOK. Research only; no additional drawing hook, native slot-count patch,
or runtime installation is introduced by this document. Validate the progression
implementation before selecting this follow-up for execution.

## Executable and evidence boundary

Supported private PE SHA-256:
`78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced`.
The preferred image base is `0x00400000`. Addresses labelled VA below are
preferred-image virtual addresses; use module base plus RVA at runtime.

The executable-byte tests in
`research/equipment_workshop/tests/test_native_progression.py` were run against
this PE during the integration. Earlier disassembly evidence is retained in
the source worktree's `.superpowers/sdd/2026-09-25-workshop-progression-jarvis/`:
`native-menu-analysis.txt`, `native-customize-dispatch.txt`, and
`native-customize-references.txt`. These are static evidence, not live UI tests.

## Confirmed Customize admission and dispatch

| Location | Width / observation | Confidence and consequence |
|---|---|---|
| VA `0x008E1DF4`, RVA `0x004E1DF4` | 18 bytes `85ff740881ff480400007c0681ce80000000` | Byte-verified admission of native option 7 when story is zero or at least `0x448`. Zero is the native debug/all-menus case. |
| VA `0x0086C400`, RVA `0x0046C400` | 13 bytes `e89b0302000fb780ec0b0000c3` | Reads an unsigned 16-bit story value at save RAM + `0xBEC`. |
| VA `0x0088C7A0`, RVA `0x0048C7A0` | 5-byte jump `e95b8befff` | Leads to the save-RAM getter at VA `0x00785300`. |
| VA `0x00785300`, RVA `0x00385300` | 6 bytes `b890ca1201c3` | Returns preferred VA `0x0112CA90`, hence save-RAM RVA `0x00D2CA90`. Story is RVA `0x00D2D67C`; native file offset is `0xC2C`, including its 64-byte header. |
| VA `0x008E2617` / `0x008E25F8` | BYTE `5` / DWORD `0x008E25CA` | Connects option 7 to the native dispatch case. |
| VA `0x008E25CA` | 9 bytes `6a006a09e8dd7afcff` | Pushes zero and menu ID 9 before calling VA `0x008AA0B0`. |
| VA `0x008AA812` | 7 bytes `68fca7c5006a09` | Registers the `TkMenuKaizou` string reference at VA `0x00C5A7FC` with menu ID 9. This identifies Customize, not its individual row renderer. |

The Workshop now follows this native condition rather than guessing from a map
or location. It reads the story value and never changes it. Profile signatures
include the provenance spans; adding those read-only signatures does not install
hooks at these addresses.

The registration routine at VA `0x008AAA60` stores a menu object in the table at
VA `0x01840848 + 4 * menuId`, reads an object callback at `+0x08`, and initializes
fields at `+0x18` and `+0x1C`. This is a useful route to inspect menu lifecycle.
It is not sufficient evidence that the callback draws abilities, nor permission
to replace the global table.

## Why a global fifth-slot patch is inappropriate

The current model preserves the native equipment record at 22 bytes, native
capacity at byte `+0x0B`, and four native ability words at `+0x0E` through
`+0x14`. Fifth ability, unlock state, identities, and refinement ranks live in
the Workshop extension (`Piece` 80 bytes, `State` 16260 bytes). Raising the native
capacity to five would not create storage for another native ability and could
make a consumer interpret the next record as an ability.

The visual extension therefore needs a presentation adapter, not a change to
the save layout or every native four-iteration loop. Its input must identify
the exact selected inventory slot and match the associated extension identity;
an equipment pointer, owner, model, or identical native bytes alone are not a
sufficient identity after sorting, replacement, or load.

## Per-view investigation still required

| View | Available starting evidence | Missing proof before implementation |
|---|---|---|
| Field Equipment menu | Existing Workshop inventory association and native menu dispatch infrastructure | Resolve the field menu descriptor, selected-slot lookup, actual ability-list renderer, clipping rectangle, and navigation lifecycle. |
| Battle weapon/armor selection | Existing fifth-effect consumers establish that effects and presentation are different surfaces | Trace the battle command submenu to its selected equipment identity and draw callbacks; verify active actor, weapon/armor switching, preview restoration, and battle teardown. Do not reuse a field pointer without proof. |
| Customize | Menu 9 / `TkMenuKaizou` and admission/dispatch are identified above | Locate the list/detail renderer and its four-slot layout. Keep fifth installation in the reviewed Workshop transaction; a visual row must not silently expose vanilla Customize as a free fifth-slot writer. |

## Uncompiled design sketch

This is pseudocode for a later implementation, not a declaration of discovered
native function signatures or calling conventions:

```cpp
// Documentation only: never included by a build target.
ViewModel InspectSelectedEquipment(ViewKind view, NativeSelection selected) {
    ViewModel result = ReadVanillaPresentation(selected);
    if (!WorkshopDisplayEnabled() || !SupportedProfile()) return result;
    auto association = ResolveExactWorkshopIdentity(view, selected);
    if (!association || !association->matchesCurrentNativeRecord) return result;
    result.refinementLabel = SumExistingAbilityRanks(*association);
    if (association->fifthUnlocked) {
        result.fifthRow = FormatFifthAbilityOrEmpty(*association);
        result.requiresFifthRowLayout = true;
    }
    return result;
}
```

A draw adapter should preserve the original callback and ABI, keep per-frame
scratch state private, and use the original four-row layout whenever there is
no admitted fifth slot. A refined four-slot item can receive a refinement label
without reserving an empty fifth row. Each view needs its own measured spacing,
font, clipping, and description-box handling; no guessed coordinates are ready
for production.

Any future hook must default OFF, validate its exact code profile, run on the
owning menu thread, and restore its owned callbacks/patches on disable and
teardown. Unknown identity or profile means vanilla presentation, never an
unbounded dereference or manufactured fifth ability.

## Future acceptance cases

Use separate four-slot and five-slot equipment, an empty fifth, refined and
unrefined abilities, each weapon/armor view, sorting, switching saves, identical
items, donor destruction, repeated open/close, focus loss, and mod disable.
Validate field, battle, and Customize independently. Require keyboard/gamepad
selection correctness and no clipping or neighboring-record reads. Build and
isolated harness passes cannot replace authorized live observations for these
visual claims.
