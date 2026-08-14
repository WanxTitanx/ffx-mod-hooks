# F8 Dashboard — status and roadmap

The F8 dashboard (InGameMenuDashboard) is the project's replacement for the legacy UnX
switchboard: one menu, 6 tabs (Plugins, Boosters, Cheats, Field, Arena+, Input), 56 rows,
GDI-rendered via the Aurora D3D Present hook.

**Current state: implemented but DISABLED** (`ffx-hooks.ini` → `[dashboard] enabled=0`).
Focus stayed on F7 first. This is the next release candidate.

## Key arbitration (already implemented)

| Key | Owner (dashboard on) | Owner (dashboard off) |
|---|---|---|
| F7 | NativeMenu (RAM) | NativeMenu |
| F8 | **Dashboard** | InGameMenu (legacy switchboard) |
| INSERT | InGameMenu fallback | InGameMenu |
| F9/F10 | Aurora overlay | Aurora overlay |

Rules: one owner per key; render priority Dashboard > NativeMenu > InGameMenu > Aurora labels;
a higher-priority open menu skips the input of lower ones. The legacy GDI "Lab Menu" is
retired (kept only as gated compat code).

## What the dashboard already wires

- **Plugins:** `dinput8` (loader), `dxgi` (off), `unx` (off — legacy removed from deploy
  after a 3/3 crash with the probe), `ffx_probe` (off).
- **Boosters (UnXBoosterHook, 30 Hz):** entire party earns AP, permanent sensor.
  **Playable Seymour stays OFF** — party struct not validated in IDA (K-12).
- **Cheats:** debug-flag bytes (`0xD2A8F8` + 0x04..0x0B) — always overdrive/critical,
  damage value, always rare drop, AP/Gil 100x.
- **Input:** background input fix, block Windows key, IME filter, **dialog voice skip**
  (DialogSkipHook — safe port of the legacy `ret 8`: a reversible behavior hook on
  `FFX_FmodVoice_ReadEventData` @ RVA `0x30B040`, gated by `input.dialog_skip`).
- **Field/Arena+:** scout toggles and arena compose/music/resolver log items.

## Reactivation steps (when we return)

1. `shared/Config.cpp` → `kDefaultIni`: `[dashboard] enabled = 0` → `1`.
2. `dllmain.cpp` → 2x `GetBool("dashboard.enabled", false)` → `true`.
3. Build Release + deploy with named backups (Steam + isolated copy).
4. RT2: `powershell -File run_f8_rt2.ps1` (editor CLOSED, disposable save, hashes before/after).
   Checks: F8 opens the dashboard, closes cleanly with movement keys restored, F7 untouched,
   no F7/F8 dispute, no flicker (K-11), no crash in log.

## Roadmap beyond F8

1. `ffx-probe` Tier 2 return — remove the `.RT2OFF` suffix, heartbeat `hooked=1`, real toggle
   in the dashboard, editor wiring (`FfxProbe_Service` + `Local\FFXProbeBlock_v1`).
2. Safe UnX ports only (never: soft reset bytes, WININET auto-update, texture inject,
   sig-scans, hardcoded `.text` patches — registered as "never port").
3. Speed hack — only after RE of the game tick seam (not to be confused with 60 fps).
4. FFX Mod Studio (editor/launcher/site) integration.
