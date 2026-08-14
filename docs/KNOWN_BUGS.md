# Known bugs and honest limitations

This list is the single source of truth for known issues. It is mirrored into the release
notes of every public release. Items move out only after a passing RT2 session.

## F7 In-Live

| # | Issue | Severity | Detail |
|---|---|---|---|
| K-01 | Force Last Battle stutters briefly | Low | `Sleep(~120 ms)` runs on the main thread between repetitions (1-9 reps). Acceptable up to 9 reps; visible hitch in field. |
| K-02 | Current HP/MP not rescaled on difficulty apply | Medium | Difficulty multipliers change Max HP/MP; the monster keeps its current % of the new max. Intentional-but-not-yet-decided behavior. |
| K-03 | Per-area (N2) and per-monster (N3) difficulty presets have no UI | Medium | They exist in config + apply code, but the F7 menu does not expose them yet. |
| K-04 | Music playlist edit has no UI | Low | Config supports 8 playlist slots; the MUSIC submenu does not expose editing. |
| K-05 | Track name crosswalk incomplete | Low | Not all 181 music tracks (0..0xB5) have confirmed names; some show partial names. |
| K-06 | Monster AI Swap targets are hints | Medium | `Stat_action` (+0xDD6) and `Seck_target_id` (+0x438) are strong hints, not proven causal fields. The write is flag-gated; semantics still need an RT2 verdict. |

## F8 Dashboard (implemented, disabled)

| # | Issue | Severity | Detail |
|---|---|---|---|
| K-10 | Never passed a full RT2 | High | The dashboard has not been tested in-game with the editor closed (`run_f8_rt2.ps1` exists but is pending). |
| K-11 | Flicker risk | Medium | The F7 menu had a flicker bug (force-gate `0x13407E4` staying on after confirm). The dashboard uses the same Present-hook + force-subsystem pattern and must be checked for the same failure mode. |
| K-12 | Playable Seymour wired (RT2 pending) | Info | Party struct validated 2026-08-14 (RVA 0xD32088, 18x148B, slot 7, bit0). Wired in UnXBoosterHook; OFF by default. |
| K-13 | Speed hack unimplemented | Info | Requires RE of the game tick seam first; not ported. |

## General / cross-cutting

| # | Issue | Severity | Detail |
|---|---|---|---|
| K-20 | `ffx-hooks-polyhook-lab.dll` orphan crashes the menu | High | If a lab build artifact is left in the game directory, opening any menu can crash. Always delete it after lab testing. |
| K-21 | NulWard apply conflicts with NovaClamp | Medium | Both touch the damage writeback path; keep only one armed per session. |
| K-22 | Sphere Grid TrueNewNode vs FullGridCompiler | Medium | Mutually exclusive lab hooks; arming both is invalid. |
| K-23 | Signature mismatch = hook silently off | Info | Hooks install only when the target bytes match the expected signature. On a game update (exe hash change), hooks stop installing and only log — feature "disappears" until RVAs/signatures are re-verified. |
| K-24 | Beta-grade mod incompatibilities | High | See README Compatibility: mods hooking vtable[9] `GetDeviceState`, `dxgi.dll` proxies (Special K), UnX, or custom `dinput8.dll` loaders can conflict. UnX crashed 3/3 combined with the probe and was removed from deploy. |

## Out of scope / never ported

Soft Reset / KillMeNow byte patches, WININET auto-update, Special K texture injection,
Cheat Engine-style sig-scans, and hardcoded `.text` patches from the legacy UnX switchboard.
These are registered as "never port" in the project archive.
