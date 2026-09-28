$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$repo=(Resolve-Path (Join-Path $here '..\..\..')).Path
$obj=Join-Path $here 'obj\equipment-workshop-transaction-rt1'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
$runtime=Get-Content -LiteralPath (Join-Path $here 'hooks\EquipmentWorkshopRuntime.cpp') -Raw
$inc=''
foreach($needle in @('workshop::Error ReadEconomy(','const AeonAscension::Ledger* ReceiptSnapshot(',
    "template<class Stage>`nbool CommitInventory(",'workshop::Error Preview(','bool Commit(')){
    $start=$runtime.IndexOf($needle)
    if($start -lt 0){throw ('Missing production block: '+$needle)}
    $body=$runtime.IndexOf('{',$start);$depth=1;$end=$body+1
    while($depth -gt 0 -and $end -lt $runtime.Length){
        if($runtime[$end] -eq '{'){$depth++}
        if($runtime[$end] -eq '}'){$depth--}
        $end++
    }
    if($depth -ne 0){throw 'Unbalanced production transaction block'}
    $inc += $runtime.Substring($start,$end-$start)+"`n"
}
Set-Content -LiteralPath (Join-Path $obj 'WorkshopTransaction.inc') -Value $inc -Encoding utf8
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$exe=Join-Path $obj 'EquipmentWorkshopTransactionRt1.exe'
$cmd='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /O2 /DFFXHOOKS_TESTING /I"{1}\research\equipment_workshop\include" /I"{2}" "{3}\tests\EquipmentWorkshopTransactionRt1.cpp" "{3}\shared\Config.cpp" "{3}\hooks\RonsoPoolSave.cpp" "{1}\research\equipment_workshop\src\workshop.cpp" /Fe"{4}" user32.lib' -f $vcvars,$repo,$obj,$here,$exe
Push-Location $obj
try{
    & $env:ComSpec /d /s /c $cmd
    if($LASTEXITCODE -ne 0){throw 'Workshop transaction build failed'}
    & $exe
    if($LASTEXITCODE -ne 0){throw 'Workshop transaction RT1 failed'}
}finally{Pop-Location}
