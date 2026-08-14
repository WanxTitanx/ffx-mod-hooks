# Installation guide

This guide covers three things: **installing from a release zip**, **building
from source**, and **uninstalling**. Read it all before you start.

> **BETA WARNING:** This project just left alpha. Use a **disposable save**.
> All hooks are OFF by default. Expect mod incompatibilities. See
> [KNOWN_BUGS.md](KNOWN_BUGS.md) and [ROADMAP.md](ROADMAP.md).

---

## Prerequisites

- **FINAL FANTASY X/X-2 HD Remaster** (Steam, PC)
- The **FFX module loader** — a `dinput8.dll` proxy in the game root that loads
  DLLs from a `modules\` folder. If you already use mods that load via
  `modules\`, you have this. If not, you need it (see
  [ffgriever's ff10-file-loader](https://github.com/ffgriever)).
- **.NET 8 desktop runtime** — only if you want to use the SIN injector
  (`SinScaleInject.exe`). Download from
  [dotnet.microsoft.com](https://dotnet.microsoft.com/download/dotnet/8.0).

---

## Option A: Install from a release zip (recommended)

### Step 1 — Download

Go to the [Releases page](https://github.com/WanxTitanx/ffx-mod-hooks/releases)
and download the zip from the latest release (e.g.
`ffx-hooks-release-v0.1.0-beta.1.zip`).

### Step 2 — Find your game directory

Your game is typically at:

```
D:\SteamLibrary\steamapps\common\FINAL FANTASY X&FFX-2 HD Remaster\
```

Or wherever your Steam library is. The folder must contain `FFX.exe` and a
`modules\` subfolder.

### Step 3 — Back up

Before touching anything, back up your existing `modules\` folder:

```powershell
Copy-Item "D:\SteamLibrary\steamapps\common\FINAL FANTASY X&FFX-2 HD Remaster\modules" `
          "D:\SteamLibrary\steamapps\common\FINAL FANTASY X&FFX-2 HD Remaster\modules.backup_$(Get-Date -Format yyyyMMdd)" `
          -Recurse -Force
```

### Step 4 — Extract and copy

Open the zip. You will see:

```
ffx-hooks.dll          -> copy to <game>\modules\
ffx-probe.dll          -> copy to <game>\modules\
ffx-hooks.ini          -> copy to <game>\ (NEXT TO FFX.exe, not in modules\)
SinScaleInject\        -> copy to <game>\modules\tools\SinScaleInject\
INSTALL.md             -> reference (don't need to copy anywhere)
```

After copying, your game folder should look like:

```
<game>\
  FFX.exe
  dinput8.dll              (the module loader proxy — must already exist)
  ffx-hooks.ini            (our config)
  modules\
    ffx-hooks.dll          (our hook layer)
    ffx-probe.dll          (our probe — off by default)
    tools\
      SinScaleInject\
        SinScaleInject.exe
        SinCoreLib.dll
        Xe.BinaryMapper.dll
        ...
```

### Step 5 — Arm a feature (game CLOSED)

All hooks are OFF by default. You arm a feature by creating a flag file:

```powershell
# F7 In-Live menu (the functional core)
New-Item -ItemType File "<game>\modules\config\f7_inlive.flag"
```

Other useful flags:

```powershell
# F7 Monster AI Swap
New-Item -ItemType File "<game>\modules\config\f7_aiswap.flag"

# Music hook
New-Item -ItemType File "<game>\config\music.flag"

# Nova Super Damage (lab — 99999 damage cap bypass)
New-Item -ItemType File "<game>\config\nova_super_damage.flag"
```

You can also arm via environment variable instead of flag files:

```powershell
$env:FFXHOOKS_ENABLE_F7 = "1"   # then launch FFX from this shell
```

### Step 6 — Launch and verify

1. Close the game if it's open.
2. Launch FFX (from Steam, or from a shell with the env var set).
3. Check the log appeared: open `%TEMP%\ffx-hooks.log` — you should see:

```
[ffx-hooks] DLL_PROCESS_ATTACH enter
[ffx-hooks] FFX.exe base = 0x...
[ffx-hooks] InstallHooks enter
[ffx-hooks] F7: instalado ok=1 (difficulty=ON, ...)
[ffx-hooks] InstallHooks leave
```

4. Press **F7** in-game — the In-Live menu should open.

### Step 7 — Use the F7 menu

| Submenu | What it does |
|---|---|
| **DIFF** | Difficulty presets + stat multipliers + auto-status + Apply Now / Save |
| **FORCE** | Force Last Battle (re-triggers the last natural encounter) |
| **MUSIC** | Track lock / battle-entry override / randomizer / fade |

Navigate with arrow keys (or D-pad). Toggle with Enter. Back with Esc.

## Option B: Build from source

### Prerequisites

- Windows 10/11
- [Visual Studio 2022](https://visualstudio.microsoft.com/) with the
  "Desktop development with C++" workload (MSVC x86/x64 tools)
- [vcpkg](https://github.com/microsoft/vcpkg) — the C++ package manager
- PowerShell
- .NET 8 SDK (for the SIN injector)

### Step 1 — Clone

```powershell
git clone https://github.com/WanxTitanx/ffx-mod-hooks.git
cd ffx-mod-hooks
```

### Step 2 — Install vcpkg dependencies (one-time)

```powershell
# If you don't have vcpkg yet:
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat

# Install the static x86 libraries:
C:\vcpkg\vcpkg install --triplet x86-windows-static polyhook2 zydis minhook
```

### Step 3 — Copy vcpkg_installed into the project

The build script expects `vcpkg_installed\` inside the FfxHooksDll folder:

```powershell
Copy-Item C:\vcpkg\installed\x86-windows-static .\src\runtime\FfxHooksDll\vcpkg_installed -Recurse -Force
```

### Step 4 — Build the hooks DLL

```powershell
.\src\runtime\FfxHooksDll\build_hooks.ps1 -WithPolyHook -Release
```

Output: `src\runtime\FfxHooksDll\bin\Release\ffx-hooks.dll`

> **Note:** You can also open `FfxHooksDll.vcxproj` in Visual Studio and build
> there (the IDE build is the reference — 0 errors). A new hook must be
> registered in **both** the vcxproj and `build_hooks.ps1`.

### Step 5 — Build the probe

```powershell
.\src\runtime\FfxDinput8Probe\build.ps1
```

Output: `src\runtime\FfxDinput8Probe\ffx-probe.dll`

### Step 6 — Build the SIN injector

```powershell
dotnet build src\sin\SinScaleInject\SinScaleInject.csproj -c Release
```

Output: `src\sin\SinScaleInject\bin\Release\net8.0\` (exe + deps)

### Step 7 — Deploy

Copy the built files to your game as described in Option A, Step 4. Or use
the deploy flag (lab deploy to a disposable copy):

```powershell
.\src\runtime\FfxHooksDll\build_hooks.ps1 -WithPolyHook -Release -Deploy -LabDeploy -GameRoot "D:\path\to\game-copy"
```

---

## Uninstall

### Quick uninstall (remove hooks, keep DLLs)

Delete the flag files — hooks go dormant on next game restart:

```powershell
Remove-Item "<game>\modules\config\f7_inlive.flag" -Force
Remove-Item "<game>\modules\config\f7_aiswap.flag" -Force
Remove-Item "<game>\config\music.flag" -Force
```

Restart FFX. The DLLs are still loaded but do nothing (all gates OFF).

### Full uninstall (remove everything)

1. Close FFX.
2. Delete the DLLs and config:

```powershell
Remove-Item "<game>\modules\ffx-hooks.dll" -Force
Remove-Item "<game>\modules\ffx-probe.dll" -Force
Remove-Item "<game>\ffx-hooks.ini" -Force
Remove-Item "<game>\modules\tools\SinScaleInject" -Recurse -Force
```

3. Delete any flag files you created.
4. Restore your `modules\` backup if you made one.
5. Restart FFX — the game runs vanilla.

Everything is RAM-only and reversible. No `.bin` files are modified by the
hooks (the SIN injector does modify `.bin` files, but only when explicitly
spawned — and it has `--restore` and `--restore-area` to undo changes).

---

## Troubleshooting

### F7 doesn't open

- Check `%TEMP%\ffx-hooks.log` — did the DLL load? Did InstallHooks run?
- Verify the flag file exists: `Test-Path "<game>\modules\config\f7_inlive.flag"`
- Verify the DLL is in `modules\`, not in the game root.
- Verify `dinput8.dll` (the proxy loader) exists in the game root.

### Game crashes on launch

- Check if `ffx-hooks-polyhook-lab.dll` is in the game directory — **delete it**
  (lab artifact that crashes the menu — K-20).
- Check `%TEMP%\ffx-hooks.log` for FAULT lines (crash address).
- Remove all flag files and try again (clean boot with hooks dormant).

### Hook disappears after a game update

- A Steam update can change `FFX.exe` bytes, breaking signature validation
  (K-23). The hook logs "unexpected bytes" and stays off. The RVAs need to be
  re-verified against the new exe. Open an issue on the repo.

### Conflicts with other mods

- If you use Special K (`dxgi.dll`), UnX (`unx.dll`), or any mod that hooks
  `GetDeviceState` (vtable slot 9), expect conflicts (K-24). UnX crashed 3/3
  with our probe. Try running without those mods first.
