param(
    [Parameter(Mandatory=$true)][string]$NativeDll,
    [Parameter(Mandatory=$true)][string]$ExecutableFixture,
    [Parameter(Mandatory=$true)][string]$SaveFixture,
    [Parameter(Mandatory=$true)][string]$ReferenceMinHook,
    [string]$OutputRoot="$PSScriptRoot/work"
)
# Jarvis-HOOK: dedicated x86 CLR process; FFX is mapped with NO entrypoint call.
$ErrorActionPreference='Stop'
if((Get-FileHash $ExecutableFixture).Hash -ne '78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED'){throw 'Exact private FFX fixture required'}
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$owned=Join-Path $OutputRoot ('transport-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $owned 'modules/config') -Force | Out-Null
$owned=(Resolve-Path $owned).Path
Copy-Item -LiteralPath $NativeDll -Destination (Join-Path $owned 'modules/ffx-hooks.dll')
Copy-Item -LiteralPath $ReferenceMinHook -Destination (Join-Path $owned 'minhook.x32.dll')
& dotnet publish (Join-Path $PSScriptRoot 'transport/TransportTests.csproj') -c Release -r win-x86 --self-contained true -p:UseAppHost=true -p:NuGetAudit=false -o $owned
if($LASTEXITCODE){throw 'Self-contained transport harness build failed'}
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC x86 required for the marker fixture'}
$vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
$marker=Join-Path $repo 'src/runtime/FfxHooksDll/tests/FahrenheitMarkerFixture.c'
$compilerArguments='/nologo /LD /O2 /MT "'+$marker+'" /Fo"'+$owned+'/" /Fe:"'+$owned+'/fhstage1.dll"'
$rsp=Join-Path $owned 'marker.rsp'
[IO.File]::WriteAllText($rsp,$compilerArguments,[Text.Encoding]::Unicode)
& cmd /d /c ('"'+$vcvars+'" x86 >nul 2>&1 && cl @"'+$rsp+'"')
if($LASTEXITCODE){throw 'Marker fixture build failed'}
& (Join-Path $owned 'TransportTests.exe') (Join-Path $owned 'modules/ffx-hooks.dll') $ExecutableFixture $SaveFixture $owned
if($LASTEXITCODE){throw "Transport test failed; inspect native log in $owned"}
Get-FileHash (Join-Path $owned 'modules/ffx-hooks.dll')
Write-Output "CLR/native transport RT1 passed; evidence in $owned"
