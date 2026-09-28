# Jarvis-HOOK — first-ready-title Fastload

> Superseded by the 2026-09-20 user-run trace. Load suspends field processing only
> while its screen is active; it does not finish the pending native startup
> script. The trace loads scene 164, then startup switches to 348 and returns to
> title 23. Its later SUCCEEDED was reached after a second/manual load. The early
> admission claim below is invalidated; see
> [the regression and save investigation](FASTLOAD_NOVA_SAVE_2026-09-20.md).

## Claim and identity

The old Fastload policy waited for a scene 348/349 roundtrip after the opening
loader had already been skipped. The new candidate requests native Load at the
first idle, ready scene 23, while no scene transition is pending.

Executable: `FFX.exe` SHA-256
`78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED`,
PE32/I386, preferred base `0x00400000`. Addresses below are preferred VAs;
subtract `0x00400000` for RVAs. Evidence was read from these exact disk bytes
with pefile/Capstone. No external implementation was adapted.

## Observed delay

The user-run 8141BA9D session reached a ready scene 23 at tick **1342347**,
then entered 348 at **1342419**, 349 at **1348292**, and only requested Load on
returning to 23 at **1359008**. The old history guard therefore delayed that
request by **16661 ms** after its first otherwise-ready title sample.

This is an observed old-run delay, not a promised startup benchmark for the
new DLL. The captured old trace does not contain the new transition field.
Evidence directory:
`.superpowers/sdd/2026-09-16-f8-development-fastload-autosave/early-fastload-20260919T231530Z/`.
Full snapshot: 616672 bytes, SHA-256
`FA80AF2E1AC674CC00482D66E90949043F1F022A3DD37772F83DBB1F7C65214B`.
Its first 549972 bytes match the predeploy `AAD63645...` boundary.

## Native control-flow proof

| Evidence | Bytes / width | Meaning |
|---|---|---|
| `0x00820D71` / RVA `0x00420D71` | `E8 1A F3 FF FF` | Outer field tick calls the existing inner `0x00820090` observer before the Load gate |
| `0x00820DFC` / RVA `0x00420DFC` | `E8 AF 74 E2 FF 85 C0 0F 84 D0 09 00 00` | Call idle predicate `0x006482B0`; when false, branch to `0x008217D9` and bypass ordinary field processing |
| `0x006482B0` | 12-byte body, DWORD comparison | Returns whether screen DWORD `0x00CCB994` equals zero |
| `0x0133080C` / RVA `0x00F3080C` | DWORD | Pending scene transition; set to 1 at `0x0088EA36` and cleared at `0x0088E24C` |
| `0x0112CA90` | WORD | Current scene |
| `0x0112CA92` | WORD | Previous scene, copied from the former current scene at `0x0088EA0F..0x0088EA16`; not a pending-scene ID |

The native Load dispatcher `0x00821870` still receives only `0x40100000`.
It sets screen 1, clears its request flag and selects load direction 0.
Those existing scalar helpers remain fully signature/relocation checked.
The original inner tick services Load; the subsequent outer gate sees active
Load and suppresses ordinary field work. Fastload neither rewrites scene IDs
nor cancels transitions, and does not detour the externally owned outer tick.

Both new code excerpts are read-only admission evidence, checked at installation
and before Load/handoff. The two original detours remain unchanged.
Request admission requires scene 23, no controlled character, an entered inner
tick, no pending transition, idle screen/dialog/direction/selectLoad, UI 0/10
and command fields -1/1. Fresh admission rechecks those fields. Slot/page/row
zero, the one UI compare-exchange 12 -> 14, vanilla checksum/language validation,
deadlines, Shift bypass and no post-handoff rollback remain intact.

## Validation and confidence

The new first-title regression failed against the prior policy: **1/7840 RED**.
The revised suite passes **8326 checks**, including all-bit mutation rejection
for the two extra native excerpts, active/unknown transition denial, one-shot
early request, late-window fallback and the existing load/stop/ownership cases.

An isolated x86 fixture executes the exact native Load dispatcher and its
relocated helpers, followed by the exact native call/test/branch above. Only
the two branch destinations are test return markers. Before Load the branch
permits field processing; afterward it selects the Load bypass. The fixture
uses the production sampler, policy and action executor, proves no manual
scene/transition mutation, and rejects a duplicate request. No game process
or save is involved.

Confidence is high for the executable control flow and isolated native behavior.
The exact DEF1A87E candidate was deployed under explicit authorization at
2026-09-19 23:56:11 UTC. Cold-start timing and visible intro suppression still
await user-run observation. This is not a live RT2 result or Production
promotion. The previous 8141BA9D DLL is preserved as a verified backup.
