$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$installation){throw 'Visual Studio C++ tools were not found'}
$vcvars=Join-Path $installation 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $PSScriptRoot 'obj\native-prototype-core-rt0'
New-Item -ItemType Directory -Force $obj|Out-Null
$cases=@(
    @{Name='NativePortsRt0'; Sources=@('tests\NativePortsRt0.cpp')},
    @{Name='NativeGamepadRt0'; Sources=@('tests\NativeGamepadRt0.cpp')},
    @{Name='NativeLanguageRt0'; Sources=@('tests\NativeLanguageRt0.cpp')},
    @{Name='FmvSpeedRt0'; Sources=@('tests\FmvSpeedRt0.cpp')},
    @{Name='SinSpreadRt0'; Sources=@('tests\SinSpreadRt0.cpp')},
    @{Name='SinMetadataRt0'; Sources=@('tests\SinMetadataRt0.cpp')},
    @{Name='SinSpreadConfigRt0'; Sources=@('tests\SinSpreadConfigRt0.cpp','hooks\SinRamConfigCore.cpp','hooks\SinRamScalingCore.cpp')}
)
Push-Location $obj
try {
    foreach($case in $cases) {
        $sources=$case.Sources | ForEach-Object {'"'+(Join-Path $PSScriptRoot $_)+'"'}
        $exe=Join-Path $obj ($case.Name+'.exe')
        $command='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /O2 /W4 /WX /utf-8 /MT {1} /Fe"{2}"' -f $vcvars,($sources -join ' '),$exe
        & $env:ComSpec /d /c $command
        if($LASTEXITCODE -ne 0){throw ($case.Name+' build failed')}
        & $exe
        if($LASTEXITCODE -ne 0){throw ($case.Name+' failed')}
    }
} finally {Pop-Location}
