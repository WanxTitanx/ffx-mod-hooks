# Holy / Shadow native verification

This is offline tooling. It never starts FFX or writes an input asset. Supply
your own supported game executable; extracted game files are not distributed.

```bash
c++ -std=c++17 -Wall -Wextra -Werror \
  src/runtime/FfxHooksDll/tests/WeaponStrikeVfxRt0.cpp -o /your/output/strike-core
/your/output/strike-core
/your/output/strike-core --dump > /your/output/programs.json
python3 tools/weapon_strike_vfx/verify_native.py \
  --exe /your/FFX.exe --programs /your/output/programs.json \
  --report /your/output/new-native-report.json
```

The optional Python probe uses `pefile` and `unicorn` (verified with
`pefile==2024.8.26`, `unicorn==2.1.4`). Install them in an isolated environment.
The report path must not exist. The native constructor is compared with the C++
ring bytes, and the authored bytecode executes in real FFX handlers. Projection,
graphics, weapon geometry and a live battle are not simulated or certified.

On Windows, from a disposable source copy:

```powershell
src\runtime\FfxHooksDll\weapon_strike_vfx_rt1.ps1 -FixtureRoot C:\your\private-fixtures
```

The fixture directory must contain the exact `FFX.exe` and `et_battle.bin`
identified in the [research and implementation record](../../docs/research/HOLY_SHADOW_WEAPON_VFX_2026_09_29.md).
The test runner verifies both hashes before compiling. The real PE is mapped
with `DONT_RESOLVE_DLL_REFERENCES`; its game entry point is never called.

For a configured Windows SSH host, `tools/run_mod_runtime_checks.py` accepts
`--test weapon_strike_vfx_rt1.ps1`, explicit `--fixture-root` and
`--dependency-root`. It copies and hashes sources into its own directory.
Full DLL compilation uses `build_hooks.ps1 -WithPolyHook -Release`, without any
deploy switch. Builds and harness results are not live RT2 acceptance.
