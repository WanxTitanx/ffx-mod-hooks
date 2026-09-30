# Jarvis-HOOK: Fahrenheit coexistence

This ledger records the initial V1 checkpoint. The later optional provider,
implemented save/resource transport and F7/F8 fixes are documented in the
[V2 ledger](FAHRENHEIT_SERVICES_F7_F8_2026_09_29.md). V1 restrictions below
remain applicable to stock Fahrenheit; they do not describe the paired V2 build.

**Status, 2026-09-29:** restricted coexistence implemented and tested at RT0/RT1.
Shared hook ownership, startup coordination, input capture, graphics state and
resize are integrated. Full functional compatibility is **not** established:
managed saves and native text-pack redirection still require cooperative adapters.
Blocking conflicting features is not the same as integrating their functionality.
No game launch, deployment, save migration or upstream modification occurred.

## Scope and identities

Worktree: `ffx-hooks-fahrenheit-compat-20260929`, branch
`jarvis/fahrenheit-compat-20260929`, based on main
`3af0291e19af90a9ddad1c35306dba77544405d5`. Reference:
<https://github.com/fahrenheit-crew/fahrenheit> at
`cdb145d93295c1c6e2bf4766fda5a12877369f54` (`1.0.0-alpha12`), 2026-09-29.

Goal: detect peer runtimes, avoid duplicate ownership and bootstrap races,
preserve foreign hooks during teardown, and do not admit persistent extensions
when their save protocol is bypassed. No deployment, RT2 or Production
promotion is authorized in this lane.

## Initial findings at the base main

- Our coordinator already accepts MH_ERROR_ALREADY_INITIALIZED. Our MinHook is
  statically linked; Fahrenheit uses minhook.x32.dll or minhook.x32d.dll. These
  are independent hook lists/locks, not a shared process-wide instance.
- Fahrenheit Stage1 is fhstage1.dll (its project TargetName). A MinHook DLL
  alone does not identify Fahrenheit. Its managed hook registry owns its own
  trampoline chains and commits them after module initialization.
- The base MinHook enabled/restored code without checking that the entry
  still matches the bytes owned by this instance. Another library may have
  changed it after creation or activation.
- Our dummy D3D swapchain goes through the public creation export. Fahrenheit
  platform.cs binds successful creations to its renderer. A dummy creation can
  therefore publish objects which our fallback immediately destroys.
- RonsoPool observes only FFX CRT imports and exact game caller RVAs. Fahrenheit
  save_impl.cs uses managed FileStream and bypasses that owner. Native I/O
  installation cannot prove Workshop/Arcana/save projection compatibility.

## Implemented review scope

1. Reproduce two-provider MinHook conflicts in an isolated x86 harness.
2. Enforce ownership before code writes and suspended-thread IP relocation;
   retain foreign bytes and still-reachable originals on rejection.
3. Add explicit peer detection, capability/lifecycle reporting and render/save
   admission. Inventory actual upstream registrations, not all FhCall symbols.
4. Review PolyHook, WndProc, bootstrap and raw-patch interoperability.
5. Run applicable RT0/RT1 regressions and the native build; record the remaining
   live-validation requirement independently.

## Provenance and evidence

Fahrenheit is inspected as LGPL-3.0-or-later reference; no upstream assets or
implementation have been copied. The existing MinHook vendor is BSD-2-Clause;
retain its copyright/license headers when modifying it.

## Implemented contracts

The [managed module](../../integrations/fahrenheit/README.md) uses public
Fahrenheit APIs and targets FFX x86. The
[source reference](../../integrations/fahrenheit/upstream-reference.json) pins
36 critical files plus the runtime source-file set. The
[inventory](../../integrations/fahrenheit/upstream-hooks.json) contains 23 actual
runtime registrations with source locations. Upstream declarations sometimes
contain both FFX and X-2 RVAs; this does not establish X-2 bridge support or cover
every third-party mod.

The peer path selects and pins one already-loaded MinHook provider before
initialization. Direct MinHook and converted PolyHook call sites use its
registry. Private client records contain only successfully created targets;
queued operations use exact client targets without flushing the peer queue,
disabling all peer hooks or uninitializing its heap.

The local vendor verifies entry and trampoline ownership, including the five
bytes preceding an x86 hotpatch. Standalone verification occurs after suspension
and before thread instruction-pointer adjustment. Shared operations also check
their postconditions: a postflight conflict can follow a partial transition.
Callers retain their may-have-run fences and reachable executable storage.

CompatibleDetour preserves PolyHook in standalone mode. Its peer relay initially
routes to the original continuation, publishes that pointer before activation,
then switches to the replacement atomically. Delayed callback continuations and
ambiguous failures retain process-lifetime storage. Removal is refused where a
cross-framework drain is unavailable; it is not reported as successful
restoration. AbilitySfx now uses the continuation published before activation,
instead of a typed alias assigned after hook() returns.

The native worker waits for the managed handshake after Fahrenheit's initial
hook commit and successful resize registration. Startup executes once; failure
is terminal. A MinHook DLL alone does not identify Fahrenheit. Late attachment,
provider switching and hot unload remain unsupported.

Fahrenheit supplies the real swapchain through render_imgui. Native dummy-device
creation and competing Present hooks are closed. Peer drawing switches to a
separate D3D11.1 context state, restores the saved pipeline even after an SEH
exception and clears the isolated state before restoring the peer. This prevents
cached native backbuffer references from obstructing a later resize. Unsupported
context-state isolation skips native drawing. Asynchronous queries and video
state are outside this pipeline-isolation claim.

A nonblocking ticket covers the entire downstream ResizeBuffers call. A busy
resize returns a DXGI failure without forwarding an unprotected operation.
Completion is bound to its ticket and initiating thread. A skipped first frame
cannot claim renderer ownership while resize holds the gate. The obsolete
one-shot resize entrypoint and duplicate export aliases were removed. The five
C ABI entrypoints each have one undecorated exported name.

Keyboard/mouse capture gates native polling and WndProc handling. Saved WndProc
continuations remain callable when another subclass may still reach them.

## Persistent and resource features

ABI 1 advertises capabilities **3** (shared hooks and frames) and reports
**managedSaveCompatible=0**. A capabilities-7 handshake is rejected. F8 reports
FAHRENHEIT: ADAPTER REQUIRED, rejects enabling blocked features and preserves
stored preferences. Startup APIs and the actual NativeSaveEvents registry enforce
the same boundary. Legacy NulWardTeach flags are rejected before menu patching
or persistent command grants.

| Surface | Current peer behavior | Required next work |
|---|---|---|
| Hook ownership | Shared provider, exact client operations, foreign-target rejection | Selected in-game feature and load-order acceptance |
| Frame delivery/input/resize | Managed callback, capture gate, full resize scope, isolated pipeline | In-game focus, menus, device and resize observations |
| Independent gameplay hooks | Existing gates, executable signatures and target checks still apply | Per-feature acceptance with actual selected Fahrenheit mods |
| Workshop, Ronso/Nova, Arcana, Vanguard, Seymour, Grid8, GridTeach, Aeon/Nul persistence | Unavailable under managed save ownership | Cooperative read/write/close adapter and consumer integration |
| Fastload/autosave selection | Unavailable under peer bootstrap/save ownership | Provider-aware selection and acceptance |
| Native text-pack redirection | Unavailable under peer file-loader ownership | Resource-loader integration and language validation |

Fahrenheit uses managed FileStream, bypassing the game's CRT import observer.
Its local-state callback follows the primary write and cannot supply pre-write
projections, paid-checkpoint selection or successful-close verification. The
[save protocol requirements](../../integrations/fahrenheit/SAVE_PROTOCOL.md)
describe the missing boundaries; they are a specification, not a shipped API.
The current native save layout and extension stores are not rewritten here.

## Validation ledger

Windows runs use the isolated lane C:/VMTasks/jarvis-fahrenheit-compat-20260929.
Logs below are under work/fahrenheit and intentionally excluded from Git. Counts
are assertions in their own suites, not coverage percentages. All rows are
RT0/RT1 and invoke no game entrypoint or live FFX process.

| Command or harness | Result | Evidence |
|---|---|---|
| fahrenheit_minhook_rt1.ps1 -ReferenceMinHook BASELINE | 82 checks, zero failures across eight scenarios | minhook-verified.log |
| fahrenheit_detour_rt1.ps1 -DependencyRoot DEPS | 12 checks, zero failures | detour-verified.log |
| fahrenheit_bridge_rt1.ps1 | 35 checks, zero failures | bridge-final.log |
| fahrenheit_d3d_state_rt1.ps1 | 38 real WARP checks, zero failures | d3d-headless-verified.log |
| Same D3D executable --swapchain in interactive VM session 1 | 69 total checks including four real resizes, zero failures | d3d-interactive.log |
| ability_sfx_publication_rt1.ps1 | 7 checks, zero failures | sfx-publication-green.log |
| nul_ward_teach_coexistence_rt1.ps1 | 3 checks, zero failures; private memory unchanged | nul-teach-green.log |
| FahrenheitCoexistenceRt0.cpp, g++17 | 37 checks, zero failures, including actual save-registry admission | coexistence-final stdout |
| NativeSaveSlotPathRt0.cpp, g++17 | 10,016 checks, zero failures | save-slot-verified stdout |
| dotnet run --project integrations/fahrenheit/tests/BridgeLifecycleTests.csproj -c Release | 18 checks, zero failures | managed lifecycle stdout |
| f8_runtime_rt0.ps1 / f7_runtime_rt0.ps1 | 4,621 / 4,020 checks passed | f8-verified.log, f7-verified.log |
| monster_ai_observer_rt0.ps1 | 275 native checks and all source/documentation gates passed | coordinator-final.log |
| music_hook_rt1.ps1 / fastload_runtime_rt0.ps1 | 1,254 / 13,219 checks passed | music-verified.log, fastload-verified.log |
| ronso_pool_io_rt1.ps1 | ON 33 and OFF 27 passed | ronso-io-verified.log |
| equipment_workshop_save_flow_rt1.ps1 | ON 48 and OFF 48 passed | workshop-save-verified.log |
| python3 research/mod_008_arcana/run_checks.py | All 12 portable suites passed, including startup, projections and events | arcana-portable-final.log |
| verify_upstream.py SOURCE / validate_package.py | 36 pinned files and four-member managed package passed | verifier stdout |

The 69-check graphical run includes the 38 headless cases; they are not disjoint.
The provider harness uses an independent unmodified MinHook baseline, not the
full Fahrenheit process. WARP is not the game's actual renderer/GPU. Private
fixture regressions use the supported FFX executable identity
78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED.
No proprietary fixtures or compiled dependencies are committed.

### Resolved failures retained in the evidence

The initial vendor failed 12 ownership assertions in six scenarios; the updated
eight-scenario matrix passes. Missing managed resize scope and native pipeline
isolation each failed their first regression, then passed after implementation.
The resize/first-frame race and duplicate export aliases produced 11 failures
in a 33-check run; the corrected 35-check run passes.

The SFX harness first lacked PolyHook link dependencies. With its runner fixed,
it reproduced five behavior failures before the continuation-publication fix;
all seven checks now pass. The legacy Nul test reproduced three forbidden-write
failures and now passes with admission before any write. The Arcana startup
double needed the new adapter namespace alias; the extracted production helper
and its nine startup assertions remain tested, and all 12 portable suites pass.

The observer wrapper first lacked copied documents, then detected stale README
and roadmap descriptions. The complete corrected wrapper passes without weakened
checks. DXGI swapchain creation failed in SSH session 0 with 0x887A0022; the exact
test succeeded in the existing graphical session. Its temporary limited-user
scheduled task was removed, with zero matching tasks left. A managed restore
against a fresh clone lacked dependency assets; the successful build uses an
already-restored copy passing the same 36-file source check.

## Earlier candidate artifacts — superseded

The earlier shared-lane candidate recorded 719 input checks and a captured
manifest with SHA-256
`fd32c56e6f8ebe774894d765b41e61c4c06ac589198e0068704ac5acdd0e9142`.
This includes the SFX publication fix and the legacy Nul admission guard.

| Candidate | Bytes | SHA-256 | Local path from this worktree |
|---|---:|---|---|
| Native x86 DLL | 4,024,320 | `5b2f318d2f3c2344eaf9c91d9819931691858b68ccde0522c7bc318462b614a1` | `work/fahrenheit/artifacts/ffx-hooks.dll` |
| Managed x86 bridge | 13,824 | `92acd150a6eb4b9ad1065bbaf2f76370fcdb5481eb5f619f19969a6112a97de8` | `integrations/fahrenheit/bin/publish/ffxhooks_fahrenheit.dll` |

Native command: `build_hooks.ps1 -WithPolyHook -Release`. Managed command:
`dotnet build integrations/fahrenheit/FfxHooks.Fahrenheit.csproj -c Release
--no-restore -p:FahrenheitSourceRoot=<pinned-checkout>`; the restored dependency
cache was used, with zero warnings/errors and the source guard passing.

`work/fahrenheit/final-build-receipt.json` records artifact, source-manifest and
evidence-log hashes. It is an unkeyed integrity record, not an authenticity
signature or runtime acceptance. PE inspection confirms x86 and exactly the five
expected bridge exports, with no imported Fahrenheit/MinHook DLL dependency.
The paired managed package passed its JSON/manifest and no-extra-DLL checks.
Neither candidate has been installed or publicly released.

Review was performed inline; no independent reviewer or live game acceptance is
claimed. These identities are historical. Shared output/log paths were reused
by resumed commands; use the unique final artifact receipt below as the
authoritative source/build/readback evidence for this branch.

## Final artifact receipt

The authoritative final build completed at 2026-09-29 20:37:31 UTC in a unique
verification lane, C:/VMTasks/jarvis-fahrenheit-verify-20260929-9c1c8eed.
Its local receipt and logs are in work/fahrenheit/verified-9c1c8eed.
All **719 exact native inputs** match before and after compilation and match the
local source. Source-manifest SHA-256:
1c6e3f225b4e34a667d27a65d3232a7055ecc012b8791dfd6195cfa67df67047.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| ffx-hooks.dll, x86 Release | 4,024,320 | b02132d0d73a4c1dfaab7e024a09de65080abe3c2d5287a33223f39c91eaa8c7 |
| ffxhooks_fahrenheit.dll, managed x86 | 13,824 | 92acd150a6eb4b9ad1065bbaf2f76370fcdb5481eb5f619f19969a6112a97de8 |

The native build command was build_hooks.ps1 -WithPolyHook -Release, with exit 0.
It retains three pre-existing C4996 deprecation warnings in fscanf/fopen callers;
there were no compilation or link errors. The managed build completed with zero
warnings/errors. Native copyback matches the VM hash/length, contains the five
unique bridge exports, and has no load-time imports of a private Fahrenheit or
MinHook DLL. These checks do not prove that the game has loaded either artifact.

Earlier resumed builds shared output/log paths, so their mixed final-build logs
are not used as the authoritative receipt. An earlier inspection also used an
overly restrictive objdump parser that missed names following ordinal metadata.
The unique lane has separate source, command-status, hash and export evidence and
supersedes those candidate identities. A preparation attempt that found another
compiler active stopped before starting a competing build.

## Acceptance boundary

Claim -> restricted coexistence implemented and exercised offline/in isolation.
Evidence -> commands, retained red/green logs and artifact receipt in this ledger.
Confidence -> high for the bounded tested contracts; live gameplay/persistence
compatibility remains unestablished. Conflict -> save/resource protocols are not
integrated, and an unknown provider or third-party mod can still reject a feature.
Next -> implement the cooperative save/resource adapters, then separately authorize
deployment and a disposable-save RT2 matrix for the exact binary and load order.
