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
- **Lifecycle:** `DllMain` never hooks. A worker thread sleeps (`FFXHOOKS_INSTALL_DELAY_MS`,
  default 2000 ms, 500 ms with the D3D11 overlay armed) and then calls `InstallHooks()`.
  `RemoveHooks()` runs on detach. A VEH fault probe logs the first 8 access violations with
  RVA + EBP stack walk (diagnostic only, never swallows exceptions).
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

## Hook registry (install order in the factory)

MusicHook (PlayTrack/SwitchCrossfade/PrepBattleTrack/PlayTrackWithPreload) → NovaSuperDamage →
RonsoMana → NulWard → GridTeach → KimahriLancetDualGrant → NulWardTeach → ItemStackCap →
DoubleTripleDrop → ElementHook (Scan UI Holy/Dark) → FieldScout/FieldProbe → AbilitySfx →
ResolverLog → SinCurseHook (GraphicFieldMapLoad) → ArenaPlus (compose pick / gil) →
F7InLive + F7AiSwap → F8 Dashboard (InGameMenuDashboard + UnXBooster + DialogSkip) →
NativeMenu pump + UpdateWindowTitle guard. PhaseTurnEdge and BootSkip are compiled but
not installed in the current build.
