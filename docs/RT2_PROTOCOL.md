# RT2 protocol — in-game validation

In-game behavior is validated by a reproducible protocol. Nothing is promoted to "working"
without a signed RT2 log.

## Levels

| Level | Meaning | Exit gate |
|---|---|---|
| RT0 | byte-identity offline (codec/writer round-trip) | CLI gates + tests |
| RT1 | isolated layer (probe/harness without game) | n/a for most in-process hooks |
| RT2 | observed in-game behavior, reproducible log | **this protocol** |
| Production | RT2 log + safety gates | explicit promotion |

## Prerequisites (every RT2 session)

1. Steam FFX HD with the built DLLs deployed (game CLOSED before deploy).
2. Probe heartbeat up (`ffxprobectl mon` → heartbeat — proof of main-thread life).
3. **Disposable save** — copy the save; never write over the main save.
4. **Editor CLOSED** during hook RT2 (historical softlocks came from the editor being open).
5. Log: `work/rt2_<family>_<date>.log` with timestamps — the log IS the evidence.

## Session shape

1. Record hashes of every file touched (before/after).
2. Arm exactly one feature per session (one flag).
3. Reproduce the behavior: for F7 difficulty → enter battle, observe stats; for force →
   natural encounter → force → battle appears; for music → lock track, listen.
4. Observe 5+ battle turns (AI/rotation features) or 3+ area transitions (SIN).
5. Read-only diagnostics via the probe (`read` of the relevant RVA) between turns.
6. Restore vanilla state + verify hashes match.
7. Promotion only with: complete log + hashes OK + observed behavior.

## Rules (never)

- Never promote "needs testing" → production without a signed RT2 log.
- Never claim "works" with only RT0 (structural gate ≠ in-game behavior).
- Never run RT2 on the main save. Never `CreateRemoteThread`.
- Corpus drifts are annotated, not silently fixed.

## Per-family checklist (state at repo birth)

| Family | Diagnostic mode | RT2 status |
|---|---|---|
| Probe DINPUT8 (READ/CALL) | `ffxprobectl mon`/`read` | Proven (2026-06-03) |
| Force Battle | `forcebattle` (disposable save) | Proven 1 scenario; adversarial (menu/FMV/mid-battle) pending |
| F7 difficulty / music | apply in battle, mutate-observe | Needs testing |
| F7 Monster AI Swap | read counter vars | Needs testing |
| F8 dashboard | `run_f8_rt2.ps1` | Pending |
| SIN (hook → injector) | `--dry-run` + exit codes | Pending |
