# Equipment Workshop native evidence — Jarvis-HOOK

This records the initial isolated adapter. The subsequent production-source
native integration and its current limits are documented in
[main integration evidence](MAIN_WORKSHOP_SIN_2026_09_24.md).

## Identity and scope

PC PE32/i386 `FFX.exe`, SHA-256
`78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced`,
preferred ImageBase0x400000. All addresses below are **hexadecimal RVAs**; add0x400000 only for
the historical flat VA. Runtime relocation is applied when the private PE image
is mapped. Kernel `a_ability.bin` SHA-256
`d0610e7d37cde6e65298da116f6dc05a69236126d9ff157c2db65d17a97729aa`.
Actor offsets and bit masks below are hexadecimal; counts and IDs are decimal.
The first data section starts at20 (0x14), stride108 (0x6C), IDs0–133. Only named numeric
98–121 and Auto-Shell84/Auto-Protect85 are selected for extension effects.

The RT1 executable verifies both full hashes before mapping. It never starts the
game, loads the production Hooks DLL, modifies the installed PE or writes saves.
Its callsite/loop patches exist only in its private image and vanish with it.

## Consumers exercised

| RVA / ABI | Actual evidence | Limit |
|---|---|---|
| 39C610, cdecl actor index | Entire original actor equipment aggregator executes, including its native helpers. Private gear/row views enter at callsites39C77D/39C8D4. Its loop count39C8A4 becomes5 only in the harness. Auto-Protect sets actor+632 bit10; Shell sets bit08. | No installed detour. KO/revive, menu, battle start/end and live cache coverage still require integration. |
| 386786..386850, native frame locals | Original numerical gear loop, row field55, flags56 and percent arithmetic execute. A small entry bridge initializes its frame; a controlled branch skips the other-piece loop and returns one result before unrelated writes. Loop immediate386787 is5 privately. Every ability98–121 at rank0–10 is checked with base100. | This is an isolated **consumer region**, not execution of the entire field/grid producer3861B0. The bridge is test-only. |
| 38AE00, cdecl `(command, flags*, divisor*, status, damage)` | Entire native Protect leaf executes: physical type1, active status byteB, damage1000→500, flag40/divisor2. Adapter then yields450 at rank10. | It establishes leaf ABI/composition, not the complete live damage call graph. |
| 38AE80, same five args | Entire native Shell leaf executes: magic type2, active status byteA, damage1000→500, flag20. Adapter then yields450 at rank10. | Same live call-coverage limit as Protect. |

Numeric refinement changes field55 in a private108-byte row view by +rank
percentage points, bounded255. Global kernel bytes remain exact. Duplicate
abilities have distinct per-occurrence views. Status flags are never multiplied;
the optional reduction applies after native Protect/Shell only when that status
actually applied and the matching refined piece is equipped by the actor. The
maximum matching rank is used, so duplicates do not stack beyond10% reduction.

## Native inventory producers exercised

| RVA / ABI | Verified behavior |
|---|---|
| 3AB930, cdecl `(const22byte*)` | Finds the first vacant regular slot, copies22 bytes, sets exists=1 and equipped_by=FF, returns5000+index; returns0 when full. |
| 3ABA10, cdecl `(gearCodeA,gearCodeB)` | Swaps full22-byte records and adjusts equipped-slot references/owner bytes. The RT1 swaps two byte-identical records with different sidecar ranks. |
| 3AB990, cdecl `(owner,kind,newGearCode)` | Clears old equipped_by, sets new owner and the native PlySave equipment index. |
| 3ABCC0, cdecl `(gearCode)` | Clears exists and releases matching native equipped indices. The observer retires the extension identity. |
| 3ABBF0, cdecl `(gearCode,outUnknown*)` | Regular slot accessor uses22-byte stride and200 slots. Families7/B are outside this prototype's identity observer. |

The lifecycle adapter receives a completed producer event plus exact before/after
inventory images on its admitted owner thread. Created pieces receive new IDs;
swapped IDs move even if native bytes compare equal; removed IDs retire; equip
updates do not reset ranks. Unexpected changes quarantine all extension effects
instead of deriving identities from fingerprints. No automatic event detours or
native save callbacks are installed yet. A byte-identical replacement that occurs
**without an observed producer event cannot be detected by a later snapshot**;
this is an explicit integration gate, not a solved fingerprint problem.

## Results, failures and remaining confidence limits

`research/equipment_workshop/run_checks.py`:

- Linux C++17 `/Wall` equivalent `-Wall -Wextra -Werror`:165/165.
- ASan/UBSan with `halt_on_error=1`:165/165, no remaining diagnostics.
- Python host/persistence/controller:19 tests, including the real private save
  fixture SHA6e2a617b…e77af0b3 and unchanged-source readback.
- MSVC x86 `/std:c++17 /W4 /WX /MT`:165/165 core,595/595 native.
- Same MSVC native executable under isolated Proton:595/595.

Early sanitizer testing caught a packed-field reference passed to STL; aligned
local copies fixed it and the final sanitizer gate fails on diagnostics. An
early native swap fixture zeroed all18 PlySave equipment indices. Zero means
slot0, so native swap legitimately reassigned equipped_by to the last character.
The fixture now initializes both equipment indices toFF for all18 characters
before testing an unequipped inventory. The final native producer assertions pass;
the failed run is not counted as acceptance. The browser also caught malformed
dialog markup; final confirmation/cancellation and reload flows were retested.

Local logs/manifests are under `research/equipment_workshop/build/`, excluded from
Git with all PE/kernel/save bytes. Evidence confidence is high for these isolated
format, transaction and native leaf/consumer behaviors. There is **no RT2,
independent review or Production claim**. Complete in-game save/lifecycle/UI
integration remains pending and must stay OFF until those contracts are complete.

## Saved UI state reaches the native consumer

The browser authored an Auto-Protect fifth slot at rank1 on piece41, committing
through the shared core and the recovery journal. A read-only packed snapshot
from the validated sidecar was then supplied via `--snapshot` to the Windows
harness and the same MSVC executable under Proton. Both reported
`WORKSHOP_HOST_SNAPSHOT piece=41 rank=1 damage=495` and595/595 assertions.
The native actor aggregator admitted the fifth flag; native Protect produced500
from1000; the saved refinement rank then produced495. Only the isolated test's
in-memory equip context changed; the host snapshot and original save stayed read
only. This closes the authoring→persistence→native-consumer prototype path while
leaving the installed game lifecycle/render/save gates explicit.
