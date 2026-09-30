# Fahrenheit coexistence bridge (Jarvis-HOOK)

This bridge lets the native FFX Hooks runtime start after Fahrenheit commits its
initial hook chains, borrow its MinHook provider, and receive frames through the
public Fahrenheit module API. It targets FFX and the source revision recorded in
`upstream-reference.json`. It is an RT0/RT1 candidate, not an in-game acceptance
or a guarantee for arbitrary third-party mods.

Two variants are available. Stock V1 provides restricted shared-hook/render
coexistence. The optional V2 provider adds implemented managed-save and paired
text/font-resource services. See [V2 build and packaging](provider/README.md)
and the [current audio/hardening evidence](../../docs/research/F8_F7_AUDIO_FAHRENHEIT_V2_2026_09_29.md).

## Building the stock V1 variant

Use .NET 10, Python 3 and an unmodified checkout of Fahrenheit at
`cdb145d93295c1c6e2bf4766fda5a12877369f54` (alpha12). From this repository root:

```sh
python3 integrations/fahrenheit/verify_upstream.py /absolute/path/to/fahrenheit
dotnet build integrations/fahrenheit/FfxHooks.Fahrenheit.csproj -c Release -p:FahrenheitSourceRoot=/absolute/path/to/fahrenheit
dotnet run --project integrations/fahrenheit/tests/BridgeLifecycleTests.csproj -c Release
python3 integrations/fahrenheit/validate_package.py
```

The managed build also runs the 501-file source-reference check automatically;
it rejects a changed bootstrap, hook, save or SDK contract. An already-restored
checkout can be built with --no-restore only when its dependency assets exist.

The native DLL must also be built from this branch with `build_hooks.ps1
-WithPolyHook -Release`. Do not use the phase-0 stub. No build command above
installs a DLL or starts a game. The normal production source list defines
`FFXHOOKS_COEXISTENCE`; isolated legacy harnesses can retain the original
PolyHook adapter. The dedicated Fahrenheit harness exercises the shared adapter.

## Package layout for a separately authorized test installation

- Keep the matching native DLL at `<game>/modules/ffx-hooks.dll` with its existing
  configuration and assets. The bridge reuses it when already loaded, or loads
  that exact path through `NativeLibrary.Load`.
- Place the validated contents of `bin/publish` in Fahrenheit's
  `mods/ffxhooks_fahrenheit` directory and add `ffxhooks_fahrenheit` to its
  `mods/loadorder`, preserving existing entries. Fahrenheit supplies `fhr` first.
- Never copy another `fh.dll`, `fhr.dll`, MinHook DLL, or DINPUT8 proxy into this
  package. The package validator rejects extra DLLs.

An existing native DLL loaded without this matching managed bridge waits when
`fhstage1.dll` is present. It does not guess readiness from a timeout. Restart
after changing load order, provider, or native DLL. Hot unload is unsupported.

## Ownership

| Surface | Behavior with Fahrenheit |
|---|---|
| MinHook initialization | Accepts `MH_OK` and `MH_ERROR_ALREADY_INITIALIZED`. Borrows the already loaded release or debug provider, never both. |
| Hook targets | Uses one MinHook registry. An existing foreign target is rejected, not chained by guessing its ABI or original pointer. |
| Queued operations | Applies exact targets owned by this client. Never flushes Fahrenheit's pending queue, disables all of its hooks, or uninitializes its provider. |
| PolyHook call sites | Route through the shared provider in peer mode. Standalone mode preserves the original PolyHook implementation. |
| Rendering | Receives the real swapchain from the managed module. Native dummy-device and competing Present installation paths are closed. |
| Graphics state | Uses an isolated D3D11.1 context state and restores the peer pipeline even after a native exception. Unsupported isolation skips native drawing. |
| Resize | A ticket protects the entire downstream ResizeBuffers call. Contention rejects that resize attempt without forwarding it; cached native backbuffer references are released. |
| Input | Honors Fahrenheit's keyboard/mouse capture. Retains WndProc continuations that another subclass may still call. |
| Retirement | Keeps native continuations and shared callback state for process lifetime where a cross-framework drain is unavailable. Rejected physical removal is not successful restoration. |
| Save transport | V1 blocks save consumers. Negotiated V2 delivers real transaction events through the existing registry without native CRT import hooks. |

The native exported query is `FfxHooks_FahrenheitQueryV1`, using the 24-byte
`BridgeStatusV1` layout in `hooks/FahrenheitBridge.h`. ABI 1 advertises
capabilities `3` (shared hooks and frame delivery) and reports
`managedSaveCompatible=0` in stock V1. Active negotiated V2 reports 1 in that
field while preserving the V1 ABI/capability values. These are independent of a
passing build or frame counter. `ReadyV1` accepts exactly that supported contract.
The remaining exports are FrameV1, ResizeBeginV1 and ResizeEndV1, all prefixed
FfxHooks_Fahrenheit. The former one-shot ResizeV1 notification is deliberately
absent; use a matching managed/native pair.

## Persistent features and provider variants

The restrictions below describe **stock V1**. The optional cooperative V2
provider integrates the existing save consumers and text/font loader, and admits
those features through their ordinary configuration/signature/readiness gates.
It does not change saved preferences or claim that every third-party mod was tested.

Equipment Workshop, Ronso pool/Nova persistence, Arcana, Vanguard, Seymour and
Sphere Grid persistence, GridTeach, Aeon Ascension, and the save-dependent Nul
bundle remain unavailable under this bridge. F8 reports a peer-owned surface
and rejects enabling it while preserving the user's saved configuration. The
legacy early-start paths and the shared save-observer registry enforce the same
boundary. Independent features are still subject to their normal signatures
and target-ownership checks.

Text-pack file redirection and fastload/autosave selection also yield to
Fahrenheit's file-loader and bootstrap ownership. This does not certify every
other option as functional: a target conflict or unsupported executable can
still make a specific feature unavailable. See `SAVE_PROTOCOL.md` for the
remaining cooperative persistence contract.

## Validation

`fahrenheit_minhook_rt1.ps1` builds independent native providers and tests
ownership conflicts, hotpatch spans, trampoline retention, and queue isolation.
`fahrenheit_detour_rt1.ps1` tests the real x86 relay and publication failures.
`fahrenheit_bridge_rt1.ps1` checks the exported ABI, startup, thread/swapchain
ownership, and terminal states without a game. The managed lifecycle tests
exercise failures, concurrent callbacks, stop-versus-start races and whole-call
resize scope. The native bridge suite also rejects first-frame ownership races
and duplicate C ABI export names.

The D3D11 WARP harness, fahrenheit_d3d_state_rt1.ps1, verifies pipeline restoration,
SEH cleanup and device-threading flags without a game. Its generated executable
can additionally run with --swapchain from an interactive Windows desktop to test
real ResizeBuffers operations. The default headless run does not claim those
graphical checks. The tested graphical run passed 69 total assertions, including
four actual resizes. A temporary interactive test task was removed afterward.
From `src/runtime/FfxHooksDll` on Windows, run:

```powershell
.\fahrenheit_d3d_state_rt1.ps1
.\fahrenheit_d3d_state_rt1.ps1 -WithSwapChain
.\ability_sfx_publication_rt1.ps1
.\nul_ward_teach_coexistence_rt1.ps1
```

Use the second command in an interactive desktop session: DXGI returns
`0x887A0022` when this swapchain is created from SSH/service session 0. It uses
a hidden test window, never a game process. The audio test enters the actual
shims before the installer returns; the Nul test operates on private synthetic
memory and rejects admission before any legacy menu write.

The SFX earliest-publication and legacy Nul writer harnesses exercise native
callback and save-admission regressions. See the
[full ledger](../../docs/research/FAHRENHEIT_COMPATIBILITY_2026_09_29.md) for
commands, results, limitations and exact artifact identities.

The initial V1 checks above are extended by the linked V2 ledger. The
source-reference check proves identity, not runtime correctness. In-game
rendering, device resize, input focus, gameplay, and save acceptance need a
separately authorized disposable-save RT2 session.
