# SIN system — SinScaleInject + SinCurseHook

The SIN (Spira Instinct Network) system infects monsters with difficulty presets and ATEL
behavior handlers. Two layers:

## Layer 1 — Runtime hook (C++, SinCurseHook)

Hook on `GraphicFieldMapLoad` (area transition):

1. resolve the area from the path prefix (`kAreaTable`, e.g. `map/mcyt` → Macalania Woods);
2. deterministic seed (hash of map path);
3. roll threat T via XorShift32 within the area cap;
4. if T > 0, spawn `SinScaleInject.exe --area <id> --seed N --t N` (`CreateProcessA`,
   `CREATE_NO_WINDOW`, 1 s wait timeout; exit code 0 = success, 1 = partial, 2 = failed).

Gate: `sin_curse.flag` + `sin_f7_intensity.flag` (internal gate, default off).

## Layer 2 — Offline injection (C#, SinScaleInject)

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

Deploy: copy the full publish folder (exe + SinCoreLib.dll + Xe.BinaryMapper.dll) to
`modules\tools\SinScaleInject\`.

## Known limits

- thunder_plains out of execution by design (cap=1, waits for a T1 preset).
- Chimera/Xiphos multi-worker monsters not implemented.
- Full hook → injector RT2 flow pending.
