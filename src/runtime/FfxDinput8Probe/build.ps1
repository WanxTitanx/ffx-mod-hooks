# Builds the FFX DINPUT8 in-process probe (module + external ctl) and optionally deploys the module.
#   .\build.ps1            # build module + ctl
#   .\build.ps1 -Deploy    # also copy ffx-probe.dll into the game's modules\ (game must be CLOSED)
param([switch]$Deploy)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

# locate MSVC (x86) via vswhere
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vc = "$vs\VC\Auxiliary\Build\vcvarsall.bat"
if (-not (Test-Path $vc)) { throw "MSVC (VC Tools) nao encontrado. Instale: winget install Microsoft.VisualStudio.2022.BuildTools --override '--add Microsoft.VisualStudio.Workload.VCTools'" }

# module (x86, static CRT -> self-contained DLL)
$dll = "$here\ffx-probe.dll"
Remove-Item -LiteralPath $dll -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath "$here\ffx-probe.lib" -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath "$here\ffx-probe.exp" -Force -ErrorAction SilentlyContinue
cmd /c "`"$vc`" x86 >nul 2>&1 && cd /d `"$here`" && cl /nologo /LD /O2 /MT ffx-probe.c /Fe:ffx-probe.dll"
if ($LASTEXITCODE -ne 0) { throw "cl.exe ffx-probe build falhou (exit $LASTEXITCODE)" }
if (-not (Test-Path "$here\ffx-probe.dll")) { throw "build do modulo falhou" }
Write-Host "modulo: $here\ffx-probe.dll"

# ctl (.NET console)
dotnet build "$here\ctl\Ctl.csproj" -c Release -o "$here\ctl\bin" | Out-Null
Write-Host "ctl: $here\ctl\bin\ffxprobectl.exe"

if ($Deploy) {
    $steam = "D:\SteamLibrary\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster"
    if (Get-Process FFX -ErrorAction SilentlyContinue) { throw "Feche o FFX antes de deployar (o .dll fica travado em uso)." }
    Copy-Item "$here\ffx-probe.dll" "$steam\modules\ffx-probe.dll" -Force
    Write-Host "deployado em $steam\modules\ffx-probe.dll"
}
