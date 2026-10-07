# Jarvis-HOOK: actual Scan text boundary, including SEH retirement; no game.
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$obj=Join-Path $here 'obj\scan-text-rt1'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
function Get-Function([string]$source,[string]$name){
    $match=[regex]::Match($source,'(?m)^[^\r\n;]*\b'+$name+'\([^;]*?\)\s*\{')
    if(-not $match.Success){throw "Missing production function: $name"}
    $depth=1;$end=$match.Index+$match.Length
    while($depth -gt 0){
        if($source[$end] -eq '{'){$depth++}
        if($source[$end] -eq '}'){$depth--}
        $end++
    }
    return $source.Substring($match.Index,$end-$match.Index)
}
$main=Get-Content -Raw (Join-Path $here 'dllmain.cpp')
$draw=Get-Content -Raw (Join-Path $here 'hooks\ElementalScanDraw.inl')
$adapter=(Get-Function $main 'NativeTextOutline_MenuGuard')+"`n"+(Get-Function $draw 'NumericalText')+"`n"+(Get-Function $main 'ScanPresentationTextReady')
[IO.File]::WriteAllText((Join-Path $obj 'ScanTextAdapter.inc'),$adapter)
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$exe=Join-Path $obj 'ScanTextRt1.exe'
$cmd='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /O2 /W4 /WX /utf-8 /MT /I"{1}" /I"{2}\hooks" "{2}\tests\ScanTextRt0.cpp" /Fe"{3}"' -f $vcvars,$obj,$here,$exe
Push-Location $obj
try{& $env:ComSpec /d /c $cmd;if($LASTEXITCODE -ne 0){throw 'Scan text compile failed'}}finally{Pop-Location}
& $exe
if($LASTEXITCODE -ne 0){throw 'Scan text boundary failed'}
