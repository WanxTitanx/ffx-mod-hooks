# Jarvis-HOOK: isolated x86 locale/configuration/GDI tests; no game or deploy.
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC is unavailable'}
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $here 'obj\ui-language-rt1'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
Push-Location $obj
try {
    foreach($name in @('UiLanguageRt0','UiCaptionRt0','UiFormatRt0','UiLanguageSettingsRt1','UiPaintRt1')){
        $source='"'+(Join-Path $here ('tests\'+$name+'.cpp'))+'"'
        if($name -eq 'UiLanguageSettingsRt1'){$source+=' "'+(Join-Path $here 'shared\Config.cpp')+'"'}
        $exe=Join-Path $obj ($name+'.exe')
        $command='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING {1} /Fe"{2}" user32.lib gdi32.lib' -f $vcvars,$source,$exe
        & $env:ComSpec /d /s /c $command
        if($LASTEXITCODE -ne 0){throw ('UI compilation failed: '+$name)}
        & $exe
        if($LASTEXITCODE -ne 0){throw ('UI test failed: '+$name)}
    }
} finally {Pop-Location}
Write-Output 'UI_LANGUAGE_RT1_PASS'
