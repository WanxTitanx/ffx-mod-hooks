# Battle return, full fifth catalog and extra-element corrections

Jarvis-HOOK, 2026-09-26. Base main166f479; work branch
`fix/workshop-battle-elements-20260926`. This is source and isolated validation,
not a new deployment or live visual acceptance record.

## F10 after battle: root cause, not a replacement inventory

The user reported an empty Workshop after entering and leaving battle. The
inspected log had `opened rows=1 inventory=0` while the inventory runtime remained
ready, without a corresponding new identity/storage fault. Capture read four
bytes at RVA D2A8E0; the actual game state flag occupies one BYTE. Adjacent battle
state may remain nonzero after exit, incorrectly keeping the Workshop blocked.

Pinned executable SHA256:
`78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced`,
preferred base00400000. VA783693 sets BYTE[112A8E0]; VA7816F1 and VA782744 clear
that BYTE. Their equivalent state RVA is D2A8E0. Native references also access
E1/E2/E3 independently. The implementation now reads exactly one byte. No
fallback reimport, identity relaxation, metadata reset or save write was added.

The regression leaves neighboring bytes nonzero, denies Capture during battle,
then clears only the phase byte and requires Capture and the exact prior State.
MSVC RED69/70 became GREEN70/70. Other battle mutation restrictions remain.

## Complete fifth-ability catalog

The old SupportedFifth predicate also served as a bespoke-refinement classifier,
which limited the picker to 26 IDs. Creation now admits all 131 known ability
IDs, filtered by equipment type for the 125 stock Customize recipes. The six
entries with no stock recipe use the pre-existing explicit mod-only recipe for
either type. They are not represented as six new vanilla recipes. The default
fallback becomes 45 Ability Spheres after the fifth multiplier; the 200000 Gil
fee remains. F10 labels and confirmation disclose the mod recipe.

The 26 bespoke refinement effects remain explicitly classified, independent of
creation eligibility, so broadening the picker does not erase generic STR/MAG
refinement behavior. Four open slots still suffice for unlock; four filled native
abilities are required before fifth placement. Owner binding, sphere substitution,
Fusion restrictions, existing ranks and native 22-byte equipment records remain.

The Python host obtains type/cost eligibility from the additive `ws_fifth_cost`
export; its picker filters the returned equipment types. ABI3 structures and v1
persistent layouts do not grow. Older builds that do not recognize a newly
installed fifth ability can reject that state; no backward gameplay support is
claimed for an older DLL.

Core tests cover every ID for both types, fees, fallback, identity/rank preservation
and rejection outside the catalog: RED990/1323, GREEN1323/1323. Native effects
compare the original aggregation output of each of the 131 abilities in a native
slot versus the fifth. This verifies native aggregation, not every downstream
ability-specific interaction or an external mod's feature gate.

## Difficulty: masks and an OFF custom draft

The inspected `modules/config/f7_inlive.json` held `diff_elemAbsorb=16` but
`diff_enabled=false`. Holy was already representable in the old five-bit mask;
manual edits had preserved OFF, so saving the checkbox alone did not enable its
custom preset. Independently, the parser and desired-value path truncated or
rejected masks above0x1F, excluding Darkness and both Custom bits.

F7 now presents Fire, Ice, Thunder, Water, Holy, Darkness and Custom. Row positions
map to explicit bits rather than 1<<row: Holy10, Darkness80, Custom20 or40. The
Custom choice comes from `element_scan.extra_bit`, not a guessed element name.
The core parser accepts only BYTE values0..255, and Apply/Save retain those bits.
Existing conflict precedence and exact native restoration remain unchanged.

Manual element/status and numeric edits stage enabled Custom without committing
from the toggle function itself. Apply Now commits/applies the draft. Save and
the pre-existing save-and-return action persist it; physical cancel remains a
cancel. Explicit Off still restores owned changes. The Apply label no longer
hides the action merely because the previous runtime configuration was OFF.

Actor fields are absorb5DA, null5DB, resist5DC, weak5DD. Scan getter RVA4975C0
reads those same bytes through the native category dispatch. The actual damage
function RVA38A420 consumes them, including all high bits, without a new damage
formula hook. Private native tests verify 1000 damage becomes1500/500/-1000/0
for weak/resist/absorb/null for Holy, Darkness and both Custom bits; unrelated
attacks remain1000. F7 transaction tests exercise writes and exact restoration.
Scan visibility settings do not enable Difficulty, and Difficulty does not
implicitly enable Scan rendering. Nul gameplay was not modified.

## Original sphere artwork, native colored-sprite ABI

The procedural sixteen-band circles were a different shape/style from the game.
The new adapter samples the existing silver fourth sphere at the original native
mask footprint (32.7 design pixels), uses the existing navy inactive mask, and
extends the connector with a crop from the same strip. Independent element
visibility, configured RGB colors and dynamic panel widths remain unchanged.

Native row geometry: strip(125,2,225,35.7), UV(230,972,477,1012)/1024.
The silver sphere crop is derived at(316,4,32.7,32.7); the inactive tile uses
UV(557,974,594,1010)/1024. Asset inspection used menu_us/d3d11/battle.dds.phyre,
SHA256 `fc952b3c0352c7c1928267e17f4364cf7284bf2e249a9df07acf7a813facc213`.
No source asset or extracted preview is committed or installed.

RVA503EE0 is __cdecl(tileId, x,y,w,h,u0,v0,u1,v1,colorA,colorB): eleven arguments,
not the plain wrapper's nine. RVA50417D reads stack+2C and RVA5041EE reads+30,
unpacking ABGR into DWORD channels in two 32-byte corner records. New rebased
byte signatures guard the callee and both color consumers. The private test
executes this actual wrapper, replacing only clip context, texture lookup and
GPU submission endpoints. Profile/thread/OFF/hidden-data/stop behavior remains.
The native sprite regression was RED113/128 before implementation; final native
presentation plus real affinity arithmetic passes256/256.

## Final validation

All final results are for the current sources, not the preceding installed DLL.

| Surface | Result |
|---|---:|
| Core / economy / progression / fifth order / fifth catalog | 1283 / 2660 / 62 / 116 / 1323, normal and ASan/UBSan |
| Python host and save tests with fixtures | 37 |
| Actual F7 draft functions, portable harness | 44 |
| Presentation / element pure cores | 15 / 3115, normal and ASan/UBSan |
| Native effects, including all 131 aggregation comparisons | 7025 |
| MSVC and identical-binary Proton native presentation | 256 |
| MSVC and Proton Workshop runtime / menu / settings / transaction | 70 / 11746 / 29 / 34 |
| MSVC and Proton store / native save flow | 12 / 38 OFF and 38 ON |
| Ronso I/O | 18 ON / 13 OFF |
| F7 runtime / UI; F8 runtime | 3997 / 59; 3884 |
| Release MSVC + PolyHook2; Windows and Proton DLL loaders | PASS |

The first full round exposed two obsolete F8 source assertions requiring manual
edits to preserve OFF and hiding Apply while OFF. They were updated to the new
staged-Custom contract while retaining independent configuration/preset identity,
explicit apply admission, and no commit from a checkbox toggle. Earlier failure
logs are retained. Three pre-existing C4996 warnings remain. Review is inline,
not independent. A direct Linux compile of the Windows F7 harness is unsupported
by its MSVC CRT calls; its supported MSVC/Proton runs passed instead.

Commands: `research/equipment_workshop/run_checks.py --pe <fixture> --save <fixture>`;
`tools/run_f7_element_draft_checks.py --output <private-output>`; the native
PowerShell RT0/RT1 runners; `build_hooks.ps1 -WithPolyHook -Release`; and private
recovered-binary replay. Evidence is under `.superpowers/battle-elements-fix-20260926/`.

## Candidate and remaining boundary

Candidate: `.superpowers/native-ui-20260925/vm-native-battle-final-green-c49b0027/candidate/ffx-hooks.dll`
in `/home/wanderson/Documents/ffx-hooks-native-ui-20260925`.
SHA256 `e974b0be33b3698491e4d1c11a58003b32bc17453d00c129bde51f459b47e363`;
size1852928 bytes. Windows lane ends `battle-final-green-c49b0027`.
422 source inputs and17 recovered artifacts were hash-verified. All14 isolated
Proton invocations passed; receipt `resume-verification-6b64d711b8/receipt.json`
is in the native-ui evidence directory.

No new deploy, installed INI/save modification, game launch/termination or
Production promotion occurred. The initial installed-file observation was
ed920390...; the user's running process was not inspected. Live visual positioning,
localization and every downstream equipment ability still require in-game
acceptance. Preserve unrelated main documentation and all private fixtures.
