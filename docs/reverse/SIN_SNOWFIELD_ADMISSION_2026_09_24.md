# S.I.N. Snowfield admission and command ownership — Jarvis-HOOK

The second player test still showed no effects. Its new runtime log establishes
the exact case: walking field333, native row43, group0, formation1; generation1 was
captured, but no curse registered and the stat writer reported zero changed fields.
The screenshot uses seed1259714269,80%, visit1 and the Snowfield preview.

The prior repair fixed entry and actor identity, but retained a closed field list
containing only preview keys310/340. Thus real maca03 field333 was rejected before
script, name and stat/reward admission. Passing fixtures had used those preview
keys and did not cover the real location. This invalidates the previous readiness
assumption for the full in-game path.

The new mapping is explicit: mcfr00/03 (310/313) use the forest roster; maca00/03
(330/333) and mcyt00(340) use the snowy roster. These are encounter IDs, not terrain
IDs; no low-byte masking or blanket regional range is used. Full field identity
and row43 remain in the encounter evidence. The assignment uses its canonical
roster/seed namespace, so the real room receives exactly the curses shown by the
same preview and visit. Field333 is live-log evidence; the named neighboring IDs
are cross-referenced with the existing encounter ID definitions.

The regression reproduces the player's exact seed/visit: Counter March on Mafdet,
Opening Veil on Snow Wolf, Frost-Flood Weave on Ice Flan, Evil Eye unchanged. It
first failed on unsupported field333, then passed. The shared writer is tested
with native Flan ID1013, field333/row43, both Difficulty OFF and2×: baseline1000HP
becomes1200 or2400; base stat20 becomes24 or46. Repeated application writes no
second multiplier. All snowy native registration fixtures now use actual field333.

Command curses also needed explicit turn ownership. The previous appended action
rejoined the original OnTurn handler, allowing its ordinary move selection to
replace the curse command. Active UNI005–008 now return after their curse action;
a failed conditional guard rejoins the original handler. Frost-Flood selects the
verified custom command268(610C). This implements a replacement attack on that
turn, not a promised extra queued turn. Status-only openers continue into vanilla.
The compiler verifies the active RET, original fallback and Frost-Flood payload
for every compatible generated profile. Original monster files are not changed.

The final information row was also moved inside the glass panel, matching the
player screenshot's clipping report. Exact DLL/pack identities and final results
are recorded in `.superpowers/sdd/2026-09-24-sin-snowfield-002646Z/`. Windows and
Proton fixture validation does not substitute for the next player observation;
no game process was launched by the agent. Independent review remains pending.

## Verification and installation

`run_vm.py gates-snowfield-build` and `validate_proton.py` passed 32,926
assertions per environment, plus F7 UI/config contracts, Release and exact
DLL loaders. Metadata regressions pass59, Difficulty composition35, native
AI/metadata registration400. The candidate's400 build inputs and362 runtime
inputs were frozen and checked; runtime source did not change afterward.

At `2026-09-24T00:47:20Z`, after explicit user authorization, SIGTERM closed
Editor and Launcher. Four deployment checks confirmed the game/Editor/Launcher
were absent. The installed DLL is
`24B833E8F40C1ACF29D0A08B006CA8CA602536F2DFD2888688049433A07E7CEE`
(1,705,984 bytes); the matching private pack is
`84D146B9D29014E154FE44B14E2A15C50AE0220413D0301FE3C503BAC80BC6D0`
(134,004 bytes). The receipt records matching installed readback, verified
backups of the previous DLL/pack and56 unchanged protected files.

This is a test deployment, not a gameplay-success or Production claim.
The previous player failure is preserved in `user-session.log` in the packet.
Next acceptance is a new random encounter, not an already initialized battle.
