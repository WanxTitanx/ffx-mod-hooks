# Jarvis-HOOK: isolated x86 locale/configuration/GDI tests; no game or deploy.
param([string]$NativeFontFixture='')
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
    $cases=@('UiLanguageRt0','UiCaptionRt0','UiFormatRt0','UiLanguageSettingsRt1','UiPaintRt1')
    if($NativeFontFixture){
        $env:FFXHOOKS_TEST_NATIVE_FONT_VBF=Join-Path $NativeFontFixture 'native-fonts.vbf'
        $env:FFXHOOKS_TEST_NATIVE_TYPOGRAPHY='1'
        $env:FFXHOOKS_TEST_UI_PREVIEWS=Join-Path $obj 'previews'
        $cases+='UiNativeFontRt0'
    }
    foreach($name in $cases){
        $source='"'+(Join-Path $here ('tests\'+$name+'.cpp'))+'"'
        if($name -eq 'UiLanguageSettingsRt1'){$source+=' "'+(Join-Path $here 'shared\Config.cpp')+'"'}
        if($name -in @('UiPaintRt1','UiNativeFontRt0')){$source+=' "'+(Join-Path $here 'hooks\UiNativeFont.cpp')+'"'}
        $exe=Join-Path $obj ($name+'.exe')
        $command='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /O2 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING {1} /Fe"{2}" user32.lib gdi32.lib bcrypt.lib' -f $vcvars,$source,$exe
        & $env:ComSpec /d /s /c $command
        if($LASTEXITCODE -ne 0){throw ('UI compilation failed: '+$name)}
        if($name -eq 'UiNativeFontRt0'){& $exe (Join-Path $NativeFontFixture 'native-fonts.vbf') $NativeFontFixture}else{& $exe}
        if($LASTEXITCODE -ne 0){throw ('UI test failed: '+$name)}
    }
} finally {Pop-Location}
Write-Output 'UI_LANGUAGE_RT1_PASS'
