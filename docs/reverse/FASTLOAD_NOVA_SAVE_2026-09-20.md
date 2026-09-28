# Jarvis-HOOK — startup interruption and Kimahri Nova save investigation

## Current evidence and authority

User reports that first-menu Fastload returns to the initial menu, and Kimahri's
Nova stays still/fails to launch. The user confirmed the tested save is the
Fastload autosave. This investigation reads the actual save, installed assets,
executable and existing log; it does not start FFX or change live memory/save.

- Installed DLL: `38DC7822A3A7E5424BBA63B495DA3329DA64157E8AF4AFC18FA9E59F159067BF`.
- Executable: `78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED`,
  PE32/I386, preferred base `00400000`, timestamp `55D2F3CC`, image `0237D000`.
- Autosave `ffx_000`: 26880 bytes,
  `6E2A617B58CC058A72526F20A31BF00B4D79847F7784CE5845935538E77AF0B3`.
- New log rotated before this run: 24398 bytes,
  `6DD1A322FF1B7797CBAD25A05AE6CB5ED51336A9D2D045546EC6A55421CD6722`.
  It is a full independent snapshot; the old 847356-byte boundary cannot slice it.

Evidence directory:
`.superpowers/sdd/2026-09-16-f8-development-fastload-autosave/fastload-nova-followup-20260920T044542Z/`.

## Fastload: invalidated assumption and repair

The trace proves an initial native load at tick 21169435, save read at 21169475,
scene **164** at 21170610, then startup scene **348** at 21170710, and title **23**
at 21173580. A second load sequence begins at 21176116; only at 21178428 does
scene164 gain player control. SUCCEEDED therefore credited a later load to the
earlier automatic attempt. The one-shot actions themselves were not repeated.

The earlier fixture correctly proved the native outer tick bypass while Load
was active. It did not cover the startup script resuming after Load closes.
The early-admission behavioral claim is invalidated by the actual trace.

The repair restores the prior admission requirement: observe startup scene348,
then request Load only upon a ready return to title23 with no pending transition.
The existing opening skip remains, but this repair does not promise loading
before all introductory scenes. No new detour, scene write or boot-script patch
is introduced. A handoff which reaches a field scene and then returns to title,
or enters startup348/349, now ends with `LOAD INTERRUPTED`, without another load.
Success also requires player control, an idle load screen, no selected load and
no pending scene transition. A later manual load cannot revive a failed attempt.

New tests against the installed source failed **11/8336**. After the two-file
policy/test correction, **8336 checks PASS** in the private Windows x86 harness,
including the observed interruption sequence and the actual native Load branch.
The native branch fixture now requires the safe policy state; it no longer
claims that pausing startup is equivalent to completing it.

## Autosave: Nova learned, normal charge capacity

The current Studio/FFXED field registry and fixed 148-byte character stride give:

| Field | File offset / width | Value |
|---|---|---|
| Kimahri activation | 22516 / 1 byte | `0x11`, enabled |
| Current OD | 22529 / 1 byte | 100 |
| Maximum OD | 22530 / 1 byte | 100 |
| Nova learned | 15790 / bit3 | set (`0xFD`) |

Eleven of twelve native Ronso overdrives are learned; Bad Breath is the only
missing bit. This does not remove Nova. The save's header/trailer CRC values both
equal `0x8FC6`, matching computation after zeroing the stored trailer CRC in a
detached buffer as required by FfxSaveCore. An initial raw-buffer computation
included that stored CRC; that intermediate mismatch was not treated as save
corruption. The live save was never written.

## Nova: incompatible modified cost, before the damage hook

The actual loose `new_uspc/battle/kernel/command.bin` has 367 rows and SHA-256
`B368BE9CB7CACDD25F452236CEF0B90DF3A9D2F47E217095799E092E06EE41FB`.
The canonical VBF original was extracted with the unchanged GPLv3 Studio reader,
with archive header/footer validation and provenance recorded. Its 320-row table
hashes to `CE1C294F83597676DC9F6A50D0803FFD963660D55C64634ADA068FFCD2E67D8F`.
The older file named backup-vanilla has 322 rows and is not used as the original.

Nova is row115 / encoded `0x3073`. Only three row bytes differ from the actual
original: damage flags `04 -> 06`, cost `100 -> 200`, and formula `15 -> 3`.
Animation IDs (422/0), caster animation3, actor3, menu categories4/4, targeting,
MP0, power70 and hit count1 are unchanged. The cost is at row offset **0x26**,
file offset **11098 / 0x2B5A**, width **1 byte**. It exceeds the saved 100 maximum.

Exact native consumer: **VA0078ABE0 / RVA0038ABE0**, 242 bytes through 0078ACD1.
SHA-256 `BC3F909279D670DE972A1E4D8253F7E2E001B7F342BC66939ADA6FFA8AC57DEE`.
At VA0078AC77 it reads unsigned `command+0x26` into EDX. At VA0078ACA6 it reads
the actor's byte charge at `+0x5BC`, compares at 0078ACAD, and rejects at 0078ACAF
when charge is lower. Failure returns0 at 0078ACCA; success returns-1. A native
no-cost flag at VA0112A90C can bypass cost. Its live value was not read.

`NativeNovaCostRt1.c` executes that exact full function in a private x86 process.
Only external actor/command/MP providers and the no-cost byte reference are
relocated to fixture data. Real save charge and real command rows produce:

- Installed Nova, charge100/cost200: **rejected (0)**.
- Original and repaired Nova, charge100/cost100: **accepted (-1)**.
- Repaired cost100 with charge99: rejected; installed cost200 with synthetic
  charge200: accepted; native no-cost branch remains accepted.

All **six RT1 cases PASS** under the isolated Proton prefix. This demonstrates
the data incompatibility before any damage-cap code executes. The actual
in-battle no-cost state and full animation path are not inferred from this test.
IDA MCP was unavailable (404); the evidence uses the exact disk executable with
pefile/Capstone and native fixture execution, not an unavailable decompiler.

## Prepared asset and limits

The candidate command table changes **only cost200 -> 100 at offset11098**.
All other 51866 bytes, including extended rows, modified formulas and text, are
preserved. Candidate SHA-256:
`C94383F7BC1F5856D9D305E6BB7F2CE1E8F4C083A8C3EBA10FC9F14C944362A1`.
The Nova cap hook remains scoped to Kimahri + Nova + HP and is unchanged here.
No skills, save progress or OD capacity are edited; F9 remains disabled.

This is a prepared correction, not a deployed asset or observed live cast fix.
The new DLL and one-byte command-table replacement require an explicit, concrete
deployment approval with verified backups. User battle/boot confirmation and
the pre-existing independent-review/Production gates remain pending.
