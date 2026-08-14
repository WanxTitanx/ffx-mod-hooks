# Builds ffx-hooks.dll and optionally deploys to the game modules\ directory.
#
#   .\build_hooks.ps1                # Fase 0: simple cl.exe build (no PolyHook2)
#   .\build_hooks.ps1 -Release       # Release config instead of Debug
#   .\build_hooks.ps1 -WithMSBuildNoPolyHook # comparator: MSBuild path, no PolyHook link
#   .\build_hooks.ps1 -WithPolyHook  # Fase 1+: cl.exe + vcpkg static libs (PolyHook2 required)
#   .\build_hooks.ps1 -Deploy        # also copy ffx-hooks.dll to game modules\
#   .\build_hooks.ps1 -WithPolyHook -Release -Deploy -LabDeploy -GameRoot <copy>
#
# Prerequisites (Fase 0):
#   - Visual Studio 2022 Build Tools with "Desktop development with C++"
#   - winget install Microsoft.VisualStudio.2022.BuildTools
#       --override '--add Microsoft.VisualStudio.Workload.VCTools'
#
# Additional prerequisites (Fase 1+ / -WithPolyHook):
#   - vcpkg installed (https://github.com/microsoft/vcpkg)
#   - vcpkg install --triplet x86-windows-static
#   - vcpkg integrate install (adds MSBuild props globally)

param(
    [switch]$Deploy,
    [switch]$WithPolyHook,
    [switch]$WithMSBuildNoPolyHook,
    [switch]$Release,
    [switch]$LabDeploy,
    [string]$GameRoot
)

$ErrorActionPreference = "Stop"
$here  = Split-Path -Parent $MyInvocation.MyCommand.Path
$cfg   = if ($Release) { "Release" } else { "Debug" }
$steam = "D:\SteamLibrary\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster"
$gameRoot = if ($GameRoot) { $GameRoot } else { $steam }

if ($LabDeploy -and -not $WithPolyHook) {
    throw "-LabDeploy is only valid with -WithPolyHook."
}
if ($WithPolyHook -and $WithMSBuildNoPolyHook) {
    throw "Use either -WithPolyHook or -WithMSBuildNoPolyHook, not both."
}
if ($Deploy -and $WithPolyHook -and -not $LabDeploy) {
    throw "Refusing to deploy PolyHook lab build without -LabDeploy. Keep the installed game on Fase 0, or pass -LabDeploy -GameRoot <disposable copy>."
}

$processMusicFlag = [Environment]::GetEnvironmentVariable("FFXHOOKS_ENABLE_MUSIC", "Process")
$userMusicFlag = [Environment]::GetEnvironmentVariable("FFXHOOKS_ENABLE_MUSIC", "User")
$machineMusicFlag = [Environment]::GetEnvironmentVariable("FFXHOOKS_ENABLE_MUSIC", "Machine")
if ($processMusicFlag -or $userMusicFlag -or $machineMusicFlag) {
    Write-Warning "FFXHOOKS_ENABLE_MUSIC is set in the environment. Safe boots should clear it before launching FFX."
}

# Locate MSVC (x86)
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere not found. Install VS2022 Build Tools: winget install Microsoft.VisualStudio.2022.BuildTools --override '--add Microsoft.VisualStudio.Workload.VCTools'"
}
$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw "MSVC VC Tools not found - install 'Desktop development with C++' in VS2022." }
$vcvars = "$vsRoot\VC\Auxiliary\Build\vcvarsall.bat"

$outDir = "$here\bin\$cfg"
$objDir = "$here\obj\$cfg"
New-Item -ItemType Directory -Force $outDir | Out-Null
New-Item -ItemType Directory -Force $objDir | Out-Null
$dll = "$outDir\ffx-hooks.dll"
Remove-Item -LiteralPath $dll -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath "$outDir\ffx-hooks.lib" -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath "$outDir\ffx-hooks.exp" -Force -ErrorAction SilentlyContinue

if ($WithPolyHook) {
    # Fase 1+: direct cl.exe build + vcpkg static PolyHook libs
    # Keep this close to the known-good Fase 0 PE shape. The MSBuild artifact
    # crashes under the FFX module loader before our DllMain log.
    Write-Host "Building with PolyHook2 (direct cl.exe + vcpkg x86-windows-static)..."

    $vcpkgRoot = Join-Path $here "vcpkg_installed\x86-windows-static"
    $vcpkgInclude = Join-Path $vcpkgRoot "include"
    $vcpkgLib = Join-Path $vcpkgRoot "lib"
    if (-not (Test-Path $vcpkgInclude) -or -not (Test-Path $vcpkgLib)) {
        throw "vcpkg x86-windows-static install not found under $vcpkgRoot. Run: vcpkg install --triplet x86-windows-static"
    }

    $sources = @(
        "`"$here\dllmain.cpp`"",
        "`"$here\hooks\MusicHook.cpp`"",
        "`"$here\hooks\NovaSuperDamageHook.cpp`"",
        "`"$here\hooks\RonsoManaHook.cpp`"",
        "`"$here\hooks\NulWardHook.cpp`"",
        "`"$here\hooks\NulWardTeachHook.cpp`"",
        "`"$here\hooks\GridTeachHook.cpp`"",
        "`"$here\hooks\KimahriLancetDualGrantHook.cpp`"",
        "`"$here\hooks\AbilitySfxHook.cpp`"",
        "`"$here\hooks\ElementHook.cpp`"",
        "`"$here\hooks\ResolverLogHook.cpp`"",
        "`"$here\hooks\FieldProbeHook.cpp`"",
        "`"$here\hooks\FieldScoutHook.cpp`"",
        "`"$here\hooks\SinCurseHook.cpp`"",
        "`"$here\hooks\ArenaProgressSidecar.cpp`"",
        "`"$here\hooks\ItemStackCapHook.cpp`"",
        "`"$here\hooks\DoubleTripleDropHook.cpp`"",
        "`"$here\hooks\ArenaPlusComposePick.cpp`"",
        "`"$here\hooks\BattleEndHook.cpp`"",
        "`"$here\hooks\PhaseTurnEdgeHook.cpp`"",
        "`"$here\hooks\PhaseTurnEdgeSidecar.cpp`"",
        "`"$here\hooks\BootSkipHook.cpp`"",
        "`"$here\hooks\F7InLive.cpp`"",
        "`"$here\hooks\F7AiSwap.cpp`"",
        "`"$here\hooks\InGameMenuDashboard.cpp`"",
        "`"$here\hooks\UnXBoosterHook.cpp`"",
        "`"$here\hooks\DialogSkipHook.cpp`"",
        "`"$here\shared\Config.cpp`"",
        "`"$here\third_party\minhook\src\hook.c`"",
        "`"$here\third_party\minhook\src\buffer.c`"",
        "`"$here\third_party\minhook\src\trampoline.c`"",
        "`"$here\third_party\minhook\src\hde\hde32.c`""
    ) -join " "

    # Compile version resource (optional) and embed via cl.exe
    cmd /c "`"$vcvars`" x86 >nul 2>&1 && rc /fo `"$objDir\version.res`" `"$here\version.rc`""
    if ($LASTEXITCODE -ne 0) { Write-Host "rc.exe warning (version resource may be missing)" }
    $resArg = if (Test-Path "$objDir\version.res") { "`"$objDir\version.res`"" } else { "" }

    $clCmd = (
        "cl /nologo /LD /O2 /MT /W3 /EHsc /std:c++17" +
        " /D_WINDOWS /D_USRDLL /DFFXHOOKS_EXPORTS /DFFXHOOKS_HAVE_POLYHOOK" +
        " /I`"$here`" /I`"$here\third_party\minhook\include`" /I`"$vcpkgInclude`"" +
        " $sources" +
        " $resArg" +
        " /Fe:`"$outDir\ffx-hooks.dll`"" +
        " /Fo`"$objDir\\`"" +
        " /link /LIBPATH:`"$vcpkgLib`"" +
        " PolyHook_2.lib Zydis.lib Zycore.lib asmjit.lib asmtk.lib kernel32.lib user32.lib gdi32.lib d3d11.lib dxgi.lib" +
        " /VERSION:0.1"
    )

    cmd /c "`"$vcvars`" x86 >nul 2>&1 && $clCmd"
    if ($LASTEXITCODE -ne 0) { throw "cl.exe PolyHook build failed (exit $LASTEXITCODE)" }
} elseif ($WithMSBuildNoPolyHook) {
    # Comparator: MSBuild path without PolyHook linkage
    Write-Host "Building MSBuild comparator (no PolyHook link)..."
    $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
                          -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -Last 1
    if (-not $msbuild) { throw "MSBuild not found - install Visual Studio 2022." }

    & $msbuild "$here\FfxHooksDll.vcxproj" `
        /t:Rebuild `
        /p:Configuration=$cfg `
        /p:Platform=Win32 `
        /p:FFXHooksEnablePolyHook=false `
        /nologo /verbosity:minimal
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed (exit $LASTEXITCODE)" }
} else {
    # Fase 0: simple cl.exe build (no PolyHook2 needed)
    Write-Host "Building Fase 0 skeleton (cl.exe, x86, no PolyHook2)..."

    $sources = @(
        "`"$here\fase0_stub.cpp`""
    ) -join " "

    $clCmd = (
        "cl /nologo /LD /O2 /GS- /W3 /std:c++17" +
        " /D_WINDOWS /D_USRDLL /DFFXHOOKS_EXPORTS" +
        " /I`"$here`"" +
        " $sources" +
        " /Fe:`"$outDir\ffx-hooks.dll`"" +
        " /Fo`"$objDir\\`"" +
        " /link /NODEFAULTLIB /ENTRY:DllMainCRTStartup kernel32.lib user32.lib"
    )

    # Run cl via vcvarsall to get the x86 environment
    cmd /c "`"$vcvars`" x86 >nul 2>&1 && $clCmd"
    if ($LASTEXITCODE -ne 0) { throw "cl.exe Fase 0 build failed (exit $LASTEXITCODE)" }
}

if (-not (Test-Path $dll)) {
    throw "Build failed - $dll was not created. Check cl.exe output above."
}
if ($WithPolyHook) {
    $dllSize = (Get-Item -LiteralPath $dll).Length
    if ($dllSize -lt 65536) {
        throw "PolyHook build produced a suspiciously small DLL ($dllSize bytes): $dll"
    }
}
Write-Host "built: $dll"
if ($WithPolyHook) {
    $labDll = Join-Path $outDir "ffx-hooks-polyhook-lab.dll"
    Copy-Item $dll $labDll -Force
    Write-Host "lab artifact copy: $labDll"
} elseif ($WithMSBuildNoPolyHook) {
    $comparatorDll = Join-Path $outDir "ffx-hooks-msbuild-no-polyhook.dll"
    Copy-Item $dll $comparatorDll -Force
    Write-Host "comparator artifact copy: $comparatorDll"
}

if ($Deploy) {
    if (-not (Test-Path $gameRoot)) {
        throw "Game root not found: $gameRoot"
    }
    $modulesDir = Join-Path $gameRoot "modules"
    if (-not (Test-Path $modulesDir)) {
        throw "Game modules directory not found: $modulesDir"
    }
    $proc = Get-Process FFX -ErrorAction SilentlyContinue
    if ($proc) {
        throw "FFX is running (PID $($proc.Id)). Close the game before deploying."
    }

    foreach ($dep in @("PolyHook_2.dll", "Zydis.dll", "Zycore.dll", "asmjit.dll", "asmtk.dll")) {
        foreach ($dir in @($gameRoot, $modulesDir)) {
            $staleCopy = Join-Path $dir $dep
            if (Test-Path $staleCopy) {
                Remove-Item -LiteralPath $staleCopy -Force
                Write-Host "removed stale dynamic dependency: $staleCopy"
            }
        }
    }

    $dest = Join-Path $modulesDir "ffx-hooks.dll"
    Copy-Item $dll $dest -Force
    Write-Host "deployed: $dest"
    Write-Host ""
    Write-Host "Gate check: start FFX and verify %TEMP%\ffx-hooks.log appears with:"
    if ($WithPolyHook) {
        Write-Host "  [ffx-hooks] FFX.exe base = 0x..."
        Write-Host "  [ffx-hooks] PolyHook build active (FFXHOOKS_ENABLE_MUSIC=0, FFXHOOKS_VALIDATE_ONLY=0)"
        Write-Host "  [ffx-hooks] MusicHook compiled/validated but not installed (default safe gate)"
        Write-Host "  Optional unsafe lab arm: set FFXHOOKS_ENABLE_MUSIC=1 before launching FFX"
    } else {
        Write-Host "  [ffx-hooks] Fase 0 safe stub log opened"
        Write-Host "  [ffx-hooks] Fase 0 skeleton loaded - no active hooks"
    }
}
