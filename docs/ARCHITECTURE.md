# Architecture

## Layered view

```
FFX.exe (x86, image base 0x400000, 32-bit)
  └─ dinput8.dll  →  FF10 Module Loader (proxy DINPUT8)
       └─ modules\*.dll
            ├─ ff10-file-loader.dll   (data/ file loader — external)
            ├─ ffx-probe.dll          (main-thread probe — this repo)
            └─ ffx-hooks.dll          (behavior hook layer — this repo)
FFX Mod Studio (editor, Avalonia, external to this repo)
  ├─ RuntimeDllManager  → arms/disarms DLLs on disk, detects runtime
  ├─ FfxProbe_Service   → writes commands into the probe Command Block (MMF)
  └─ LiveBattleLab      → ForceBattle via the probe
```

## The honest main-thread seam (DINPUT8)

The game polls the keyboard every frame on the main thread via
`IDirectInputDevice8::GetDeviceState` (vtable slot 9). The probe hooks that vtable entry
race-free (shared vtable of a disposable device) and executes queued commands there.
`CreateRemoteThread` is retired — it corrupted global state and crashed.

Command Block: MMF `Local\FFXProbeBlock_v1` (580 bytes). Opcodes: NOP, READ, WRITE, CALL
(ABIs 1..5), FORCEBATTLE (atomic, 1 frame), SETINPUT, TEXLOG, SOUNDCMD, KETHRES, U1.

## Hook layer internals (ffx-hooks.dll)

- **Detours:** PolyHook2 `PLH::x86Detour` (most hooks) and MinHook (F7 battle hooks).
- **Inline patches:** signature-validated byte patches with heap stubs (e.g. NovaSuperDamage
  clamp bypass) — install only if expected bytes match, restore on remove.
- **Lifecycle:** `DllMain` never installs hooks or runs full teardown. Process attach publishes
  the existing worker-thread bootstrap; install delay and `InstallHooks()` run on that worker.
  Process detach makes only the lock-free `RequestSpeedHackStop()`, `RequestDialogSkipStop()`,
  and `RequestUnXBoosterStop()` requests, in that order so Speed publication closes before its
  shared Dialog owner. Dynamic
  `FreeLibrary`/hot unload is unsupported because there is not yet a normal-context owner that
  can stop the Present producer, drain admitted frames, and restore every owned resource before
  unload. `RemoveHooks()` is therefore not called under the loader lock. A VEH fault probe logs
  the first 8 access violations with RVA + EBP stack walk (diagnostic only, never swallows them).
- **Memory contracts (MMF):**
  - `Local\FFXHooksBlock_v1` (256 B) — music override track + seq + element flags ext.
    Created lazily (only when a hook needs it — unconditional creation caused heap corruption).
  - `Local\FFXProbeBlock_v1` (580 B) — probe command block.
  Canonical copies: `contracts/` (keep in sync with the headers used at build time).
- **Log:** `%TEMP%\ffx-hooks.log`, rotated every 10 opens (`.old1`..`.old9`).
- **Addresses:** `src/runtime/FfxHooksDll/shared/ffx_addresses.h` — the RVA ledger.
  **Runtime uses PE RVAs** (`GetModuleHandleA("FFX.exe") + rva`), never IDA flat addresses
  (IDA flat = RVA + 0x400000).

## Calling conventions (hard rule)

- Always verify the convention of the hooked function (thiscall / cdecl / stdcall).
  Historical bug: a `retn4` double-pop on a misdeclared shim — conventions are checked on
  every new hook.
- thiscall targets use `__fastcall`-shaped shims that receive `ecx`/`edx` explicitly.

## F8 runtime governance

`F8FlagCatalog.cpp` is the only dashboard row catalog. The native FLAGS menu renders 37 rows in
eight tabs — Plugins, Boosters, Cheats, Scout, Arena+, Input, Dev, Lab — classified 14 LIVE,
16 RESTART REQUIRED, and 7 NOT WIRED. LIVE has two application contracts:

- 11 `RuntimeAcknowledged` rows persist the requested value and require the runtime producer to
  publish applied/restored readback;
- 3 `ConfigPolled` rows (`boosters.speed_hack`, `arena_plus.compose_f7`,
  `input.dialog_skip`) are read dynamically and report their effective resolver source.

The debug/AP producer is the real D3D11 Present path: `AuroraD3DRender()` calls
`UnXBoosterFrameTick(GetTickCount())`, whose reentrancy-safe gate admits an immediate first tick
and then throttles to 33 ms. There is no F8 window `SetTimer`/`TIMERPROC` producer in committed
HEAD. The owned Debug layout is 32 bytes at RVA `0x00D2A8F8`; persistent fields are one-byte
transactions. AP participation (`0x01F10EA0`) and AP-earned (`0x01F10EC4`) are exactly seven
contiguous bytes each, not `DWORD` arrays.

Speed Hack has one packed, generation-tagged route and two mutually exclusive timing consumers.
Native `SpeedBooster` state at RVA `0x008E82A4` owns 2x/4x states 1/2; the read-only availability
byte at RVA `0x008E82AC` controls whether those states may be armed but is not a scene classifier.
The hook therefore reports native 2x/4x as ARMED and makes no per-scene application claim. Custom
8x requires native state zero and scales the stack delta at field-service-tick RVA `0x420C00`,
reporting APPLIED only after callback telemetry acknowledges the exact route generation. The
supported executable profile and both native image ranges are validated before use; the field-service
target must match its relocated exact signature, and mismatch or a detour-like prefix fails closed.
Dialog Skip is the sole owner of the voice entry at RVA `0x30AEC0`, also profile/signature-gated
and fail closed; its packed publication atomically ORs the manual gate with only the custom 8x
source. Callbacks read no configuration and emit no logs. Custom 8x targets the reviewed field-scene
service tick; the current call graph does not prove a whole-engine timing seam. Dialogue and
rendered-scene coverage is therefore an RT2 observation, while pre-rendered FMV remains outside
this hook. These are offline/build guarantees and remain RT2-pending.

Playable Seymour is connected only as a default-OFF, RAM-only experimental battle-roster
consumer. F7 owns the single shared MinHook target at `InitScene` RVA `0x00383ED0`; its composer
calls vanilla exactly once. Seymour is admitted only for return RVA `0x0038321C` and rejects the
bootstrap `0x00381C76` and startup/Sphere-Grid `0x003821CF` callers. Difficulty remains the sole
post-populate `ActorPopulate` RVA `0x00384010` detour. Seymour owns one unique exit target at
`SyncPartyStatsFromActors` RVA `0x00386080`, filtered to return RVA `0x00390F07`, and calls its
original exactly once before ownership-checked cleanup.

Because the dashboard row is LIVE, the exact profile/signature-gated MinHook infrastructure is
installed at startup even while `boosters.playable_seymour=0`; only that inert producer is live.
The independent OFF command reaches vanilla exactly once and never calls the formation writer.
F7 currently retains ResolveEncounter, InitScene, and ActorPopulate as one process-lifetime batch, so
a Seymour-only producer also reserves ResolveEncounter. Arena ResolverLog is therefore reported
as a startup ownership conflict when requested, rather than being allowed to create a second
detour. Splitting that batch is separate reviewed work, not an implicit relaxation of ownership.

The transaction uses only the official cdecl `AssignToFormation(uint8_t,int)` routine at RVA
`0x00386A70`. It requires the persistent party byte at `0x00D32494` to be exactly `0x10`, adds
slot 7 for vanilla initialization, then removes it from persistent state while battle-local lists
retain the roster member. Cleanup accepts only the owned combined 20-byte membership proof—one
`0xFF` replaced by one actor ID `0x07`—so legitimate Switch order changes compose while duplicate
or foreign membership drift fails closed. Local buffers may remain owned until the next battle
initialization and are reported RestorePending; RT2 must prove selection, one controlled turn,
Switch, persistent cleanup, normal exit, next-battle cleanup, and no reintroduction. Sphere Grid
is unsupported and no historical menu-filter bytes are patched.

## Hook registry (install order in the factory)

MusicHook (PlayTrack/SwitchCrossfade/PrepBattleTrack/PlayTrackWithPreload) → NovaSuperDamage →
RonsoMana → NulWard → GridTeach → KimahriLancetDualGrant → NulWardTeach → ItemStackCap →
DoubleTripleDrop → ElementHook (Scan UI Holy/Dark) → FieldScout/FieldProbe → AbilitySfx →
ResolverLog → SinCurseHook (GraphicFieldMapLoad) → ArenaPlus (compose pick / gil) →
F7InLive + F7AiSwap (legacy name; observe-only) → F8 Dashboard (InGameMenuDashboard + UnXBooster + DialogSkip → SpeedHack) →
NativeMenu pump + UpdateWindowTitle guard. PhaseTurnEdge and BootSkip are compiled but
not installed in the current build.
