# S.I.N. system — RAM-only runtime and legacy containment

The S.I.N. (Spira Instinct Network) research combines bounded monster scaling and offline ATEL
experiments. The reviewed runtime path is RAM-only and remains an offline candidate pending RT2.

## Reviewed RAM-only runtime (C++, F7 Difficulty)

S.I.N. is independently OFF by default. Its only persisted member is inside the existing bounded
`modules\config\f7_inlive.json` document:

```json
"sinRam":{"enabled":false,"threatLevel":0}
```

The member is parsed independently. An invalid S.I.N. object publishes `sinRamValid=false` plus
OFF/T0 without invalidating Difficulty, Music, or Force. The existing F7 atomic saver is the only
writer and always emits the canonical member; there is no S.I.N. flag, environment gate, process,
sidecar, or `.bin` surface.

No new detour is installed. The existing Difficulty Resolver at RVA `0x003828B0` captures the full
DWORD encounter token and admits S.I.N. evidence only from natural return RVA `0x00381D8C`.
It stages a request/generation ticket before the correlated natural BattleField commit. A failed
commit cancels the ticket, while a failed S.I.N. Stage never blocks Difficulty. Force, Arena,
Ultra, Custom Mix, and synchronously suppressed Resolver routes receive no S.I.N. ticket.

The existing actor initializer at RVA `0x0039C130` consumes the BattleField and S.I.N. tickets for
the same request after clearing prior evidence. Only matched, natural, nonzero, strictly newer
evidence for the current generation becomes a closed `SinRam::RuntimeRequest`; mismatch, corrupt,
and stale observations are consumed and cleared. `F7Difficulty::Runtime::UpdateComposed` remains
the sole physical memory writer. It composes Difficulty first, performs one HP ratio calculation,
and leaves MP entirely under Difficulty control. Apply Now can replace only the configuration on
the already-current natural request/generation.

The exact catalog is field `340` formation IDs `4, 12, 19, 37` and field `310` IDs
`3, 26, 33, 81, 217`, using raw u16 IDs without aliases or masking. Threat T0 is neutral; T1 uses
110% HP/overkill and `105% + 1` stats; T2 uses 120% HP/overkill and `110% + 2` stats.

The native `S.I.N. RAM` submenu has exactly seven rows: Enabled, Threat, Apply Now, Status, Scope,
Save, and Back. Status is one of `INVALID`, `OFF`, `UNAVAILABLE`, `WAIT NATURAL`, or
`CURRENT NATURAL`; scope is `Natural only; fields 310/340; 9 catalog IDs`.

## Legacy runtime boundary (C++, SinCurseHook)

The legacy field-transition writer is unavailable on every supported profile:

- `InstallSinCurseHook` returns an unavailable result before detour setup;
- no field-load callback, subprocess, wait, or installed battle-data writer is reachable;
- stale flags, sidecars, and environment values are not read; they create neither requested nor
  effective state;
- the legacy implementation has no menu or configuration authority;
- teardown is idempotent because this path owns no detour, worker, file, or process.

Exact player-facing reason:
`Unavailable: legacy disk writer is quarantined; no S.I.N. areas are supported.`

No runtime area is supported by the legacy writer. It remains quarantined and is not used by the
separate F7 Difficulty RAM-only implementation above.

## Offline injection research (C#, SinScaleInject)

CLI: `--area`, `--seed`, `--t`, `--intensity`, `--dry-run`, `--restore`, `--restore-area`,
`--save-clean`, `--mod-base <path>`.

Per monster in the area roster (CSV):

1. roll RNG: does this monster receive SIN? (intensity %);
2. roll UNI preset (1-8) per allowlist;
3. scale HP/stats (threat multiplier);
4. inject an ATEL guarded action into the AiFile (entrypoint 0/2/3);
5. modify the monster name in the kernel bins.

UNI presets implemented: 1 Gloom (writeChrProperty + forcePerformCommand), 2 March
(CounterAttack onHit), 3 Rush (Haste when HP < 50%), 4 Ward (Shell + Regen + NulBlaze +
NulShock). 5-8 reserved.

## Files

| Component | Path |
|---|---|
| Hook | `src/runtime/FfxHooksDll/hooks/SinCurseHook.cpp/.h` |
| Injector | `src/sin/SinScaleInject/Program.cs` |
| Codecs | `src/sin/SinCoreLib/` (ATEL bytecode, monster structs, FFX encodings) |
| Rosters | `mods/Spira Reforge/arena/spira-sin-area-rosters/*.csv` (external) |

The offline tool is not copied or launched by `ffx-hooks.dll`. Running it manually is outside the
runtime hook contract and must use isolated inputs, immutable preimage hashes, backups, and a
reviewed rollback procedure.

## Known limits

- thunder_plains out of execution by design (cap=1, waits for a T1 preset).
- Chimera/Xiphos multi-worker monsters not implemented.
- There is no supported hook-to-injector runtime flow.
