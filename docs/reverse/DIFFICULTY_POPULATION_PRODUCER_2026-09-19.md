# Jarvis-HOOK — Difficulty population producer

## Rejected producer and supported executable

The failed user session loaded DLL `A318D870...F1BE74` from the correct Steam module directory.
Difficulty was ON with HP multiplier 7000. Its actor initializer recorded thread 460; Present
recorded thread 580, so the thread guard correctly refused the render callback. The owner-thread
menu pump returned only after cleanup and the retry expired with NoActors. Do not remove the guard.

Raw disassembly was verified against FFX.exe SHA-256
`78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED`, PE32/I386,
timestamp `0x55D2F3CC`, preferred base `0x00400000`, SizeOfImage `0x0237D000`.
Addresses below distinguish preferred VA and RVA. No external code was copied.

## Native completion contract

- InitScene: VA `0x00783ED0`, RVA `0x00383ED0`, cdecl int. Its battle branch invokes the original
  actor initializer at the unique callsite VA `0x00783FB1`; it then stores phase 8 or 10 and
  returns that value. Its other branch stores phase zero and returns zero. Only the known battle
  caller (return RVA `0x0038321C`) plus original result 8/10 arms the new generation.
- Actor initializer VA `0x0079C130` / RVA `0x0039C130` remains validated but is no longer detoured.
  Generation metadata is armed at the shared original boundary, inside the existing
  Seymour/CustomMix bracketing. Prior S.I.N. state is cleared, correlated field/ticket evidence
  is consumed, then the generation and retry are published together under the runtime lock.
  No actor enumeration/read/write occurs there; the lock is not held across vanilla.
- Population: VA `0x00784010`, RVA `0x00384010`, cdecl int, complete 89-byte body. It starts with
  phase byte `0x12`, resumes the 18-combatant loop with signed byte at VA `0x0112A929`, and calls
  VA `0x007841D0` to populate eligible records. Incomplete work returns -1. The only zero return
  follows the phase-byte store `0x13` at VA `0x00784051`.
- Population HIGHLOW operands are at body offsets 4, 10, 67 and 80. Their targets are respectively
  RVAs `0x00D2A929`, `0x00D2A8E0`, `0x00D2A8E0`, `0x00D2A929`; both data fields are one byte.
  Pin the entire body, including relative calls and both return paths, before creating any hook.

The existing three-target MinHook batch becomes ResolveEncounter / InitScene / ActorPopulate.
Default-OFF behavior, shared infrastructure admission, exact executable/signature gates,
rollback-before-Apply, retained gateways, stop, and callback draining remain in force. The
population shim always calls vanilla once; only a zero return plus fresh phase 0x13 and the
matching pending generation/thread may run the existing Difficulty writer. Nested native
initialization, foreign threads, stop, faulted phase reads and completed retries are pass-through.
The rendering callback is removed as a Difficulty producer. The menu pump retains only a guarded
fallback/expiry service, with phase zero still rejected as cleanup.

## Validation and remaining observation

RT0 uses the actual core writer behind the injected native-completion seam. Its owner 460 fixture
populates vanilla HP 1500/750, then proves 7x HP 10500/5250 with no Present or menu call. It rejects
foreign thread 580, incomplete return, bad phase, re-entry, stop and read faults. Full-signature
mutation checks cover every bit at preferred and relocated bases. Existing S.I.N., Seymour,
CustomMix, lifecycle, ownership and ratio tests must remain green.

This proves offline behavior, not game effectiveness. After separate deployment/RT2 authorization,
use an explicitly disposable save and a fresh log boundary, one feature/case, and a new battle.
Require native completion followed by a timely phase-0x13 Difficulty result with valid actors and
nonzero writes; compare actual enemy HP/stats to a vanilla baseline and verify subsequent battles
do not compound the multiplier. Include OFF/restore and exit/stop cases. Preserve executable,
DLL, configuration, exact touched-file hashes and the raw log. No live session or promotion is
authorized by this document.
