$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$repo=(Resolve-Path (Join-Path $here '..\..\..')).Path
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $here 'obj\equipment-workshop-menu-rt1'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
$source=Get-Content -LiteralPath (Join-Path $here 'dllmain.cpp') -Raw
$start=$source.LastIndexOf('static void F7CloseTransition(')
$body=$source.IndexOf('{',$start);$depth=1;$end=$body+1
while($depth -gt 0 -and $end -lt $source.Length){if($source[$end] -eq '{'){$depth++};if($source[$end] -eq '}'){$depth--};$end++}
if($start -lt 0 -or $depth -ne 0){throw 'Cannot locate actual F7 close adapter'}
Set-Content -LiteralPath (Join-Path $obj 'WorkshopCloseTransition.inc') -Value $source.Substring($start,$end-$start) -Encoding utf8
$pump=$source.Substring($source.IndexOf('static int __cdecl NativeMenu_PumpHook('))
$steps=@()
foreach($needle in @('if(InterlockedCompareExchange(&EquipmentMenu::wantOpen,0,0)', 'const LONG f7CloseSource = InterlockedExchange(&g_f7CloseSourcePending, -1);')){
 $at=$pump.IndexOf($needle);if($at -lt 0){throw ('Missing actual adapter step: '+$needle)}
 $brace=$pump.IndexOf('{',$at);$depth=1;$end=$brace+1
 while($depth -gt 0 -and $end -lt $pump.Length){if($pump[$end] -eq '{'){$depth++};if($pump[$end] -eq '}'){$depth--};$end++}
 if($depth -ne 0){throw 'Unbalanced native adapter step'}
 $steps+=@{Position=$at;Code=$pump.Substring($at,$end-$at)}
}
$ordered=($steps | Sort-Object {$_.Position} | ForEach-Object {$_.Code}) -join "`n"
Set-Content -LiteralPath (Join-Path $obj 'WorkshopOpenCloseOrder.inc') -Value $ordered -Encoding utf8
$exe=Join-Path $obj 'EquipmentWorkshopMenuRt1.exe'
$cmd='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /I"{1}" /I"{2}\research\equipment_workshop\include" "{3}\tests\EquipmentWorkshopMenuRt1.cpp" "{3}\hooks\F7UiCore.cpp" "{2}\research\equipment_workshop\src\workshop.cpp" /Fe"{4}" user32.lib' -f $vcvars,$obj,$repo,$here,$exe
Push-Location $obj
try {& $env:ComSpec /d /s /c $cmd;if($LASTEXITCODE -ne 0){throw 'Menu harness build failed'};& $exe;if($LASTEXITCODE -ne 0){throw 'Menu regression failed'}}finally{Pop-Location}
