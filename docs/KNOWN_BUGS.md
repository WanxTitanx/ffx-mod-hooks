# Known bugs and honest limitations

This list is the single source of truth for known issues. It is mirrored into the release
notes of every public release. Items move out only after a passing RT2 session.

## F7 In-Live

| # | Issue | Severity | Detail |
|---|---|---|---|
| K-01 | Difficulty gameplay proof pending | High | Profile, parser, transform, exact three-target `Owner::Difficulty` transaction, pointer-spy, source-contract, and build checks are RT0. Isolated RT1 covers the actual production config APIs, concurrent runtime operations, partial max/current retry, and the conservative paused-prologue retention policy. Actual machine-detour scheduling RT1 and the baseline/ON/edit/OFF/next-battle RT2 sequence remain pending. Dynamic unload remains unsupported. |
| K-02 | Element and auto-status gameplay acceptance pending | Medium | The 8141BA9D candidate implements exact-width affinity/resistance writes and the exact native auto-status remove/apply pair. It was deployed on 2026-09-19 under explicit authorization; live battle acceptance remains pending. |
| K-03 | Per-area difficulty rules have no menu editor | Medium | Bounded JSON and runtime replacement semantics exist, but the native F7 menu currently edits only the global preset. |
| K-04 | Music playlist edit has no UI | Low | Config supports 8 playlist slots; the MUSIC submenu does not expose editing. |
| K-05 | Track name crosswalk incomplete | Low | Not all 181 music tracks (0..0xB5) have confirmed names; some show partial names. |
| K-06 | Monster AI observer live proof pending | Medium | The former status-injection writer was removed because it was not an AI swap. Its observe-only, default-OFF replacement has a three-target registration/cleanup/dispatcher lifecycle, but live observations remain pending for normal/force/death coverage, repeated-event preservation, five cdecl DWORD arguments unchanged, original exactly once, queue readback, and zero mutation. No compatible swap pair is validated, and mutation remains blocked by an empty whitelist. |

## F8 Dashboard (source/RT0 complete; RT2 pending)

Committed catalog truth is 36 rows: 14 LIVE, 15 RESTART REQUIRED, 7 NOT WIRED, in the eight
Plugins/Boosters/Cheats/Scout/Arena+/Input/Dev/Lab tabs. The tracked and built-in dashboard templates
set `enabled=1`, while all 14 LIVE target defaults remain false. `LIVE` is a source connection,
not a Production claim.

| # | Issue | Severity | Detail |
|---|---|---|---|
| K-10 | No F8 case has passed RT2 | High | `run_f8_rt2.ps1` is now a non-destructive manual Preflight/Verify gate, but it was not run against FFX and no DLL was deployed. |
| K-11 | FLAGS Back/Cancel black-screen regression | Fixed in branch / RT2 pending | Back, Cancel, and direct F8 now converge on one idempotent cleanup in source/RT0. Manual RT2 must still prove scene restoration in the running game. |
| K-12 | Experimental Seymour battle roster RT2 pending | High | The default-OFF RAM-only behavior now composes through the single shared battle-entry owner and one unique exit detour, but it is not full Playable Seymour: Sphere Grid is rejected. Its profile-gated LIVE producer installs inert infrastructure at startup; because F7 retains ResolveEncounter, InitScene, and ActorPopulate as one batch, this also reserves ResolverLog and exposes a truthful startup conflict if both are requested. Selection, controlled turn, legitimate Switch, persistent cleanup, normal exit, next-battle cleanup, and no reintroduction still need strict raw-memory RT2 evidence. |
| K-13 | Speed Hack cycle RT2 pending | Info | The LIVE/ConfigPolled flag arms Ctrl+Shift+K for 1x/2x/4x/8x. Native 2x/4x remains ARMED without a per-scene application claim. Custom 8x targets the reviewed field-scene service tick and becomes APPLIED only after matching callback telemetry; dialogue and rendered-scene coverage requires RT2 observation. Pre-rendered FMV is unsupported. F12 remains screenshot-only. |
| K-14 | Dynamic hot unload unsupported | High | `DLL_PROCESS_DETACH` only publishes lock-free stop requests. Do not use dynamic `FreeLibrary`; full normal-context teardown/drain is not implemented. |
| K-15 | Mouse row navigation deferred | Low | Mouse input remains tab-only. Row selection/clicking is deferred so the stabilized native text/layout path is not broadened before RT2. |

## General / cross-cutting

| # | Issue | Severity | Detail |
|---|---|---|---|
| K-20 | `ffx-hooks-polyhook-lab.dll` orphan crashes the menu | High | If a lab build artifact is left in the game directory, opening any menu can crash. Always delete it after lab testing. |
| K-21 | NulWard apply conflicts with NovaClamp | Medium | Both touch the damage writeback path; keep only one armed per session. |
| K-22 | Sphere Grid TrueNewNode vs FullGridCompiler | Medium | Mutually exclusive lab hooks; arming both is invalid. |
| K-23 | Validated target mismatch = hook off | Info | Only specific supported-profile/expected-byte inline-patch families fail closed on mismatch, and that assurance is scoped to their validated sites. Speed validates its native state/availability ranges and relocated field-service target; corrected Dialog Skip validates its exact target. Their behavior remains beta and RT2-pending. |
| K-24 | Beta-grade mod incompatibilities | High | See README Compatibility: mods hooking vtable[9] `GetDeviceState`, `dxgi.dll` proxies (Special K), UnX, or custom `dinput8.dll` loaders can conflict. UnX crashed 3/3 combined with the probe and was removed from deploy. |

## Out of scope / never ported

Soft Reset / KillMeNow byte patches, WININET auto-update, Special K texture injection,
Cheat Engine-style sig-scans, and hardcoded `.text` patches from the legacy UnX switchboard.
These are registered as "never port" in the project archive.
