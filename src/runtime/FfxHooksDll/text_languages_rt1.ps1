param(
    [string]$ExecutablePath,
    [string]$PackageDirectory,
    [string]$ReferenceDirectory,
    [switch]$BuildDll
)
# Jarvis-HOOK: isolated harnesses only. No game entry point or deploy is invoked.
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $here '..\..\..')).Path
$obj = Join-Path $here 'obj\text-languages-rt1'
New-Item -ItemType Directory -Force $obj | Out-Null
$free = (Get-PSDrive -Name ([IO.Path]::GetPathRoot($obj).Substring(0,1))).Free
if ($free -lt 1GB) { throw 'At least 1 GB of free build space is required' }
$private = [bool]$ExecutablePath -or [bool]$PackageDirectory -or [bool]$ReferenceDirectory
if ($private -and (!$ExecutablePath -or !$PackageDirectory -or !$ReferenceDirectory)) {
    throw 'Supply ExecutablePath, PackageDirectory and ReferenceDirectory together'
}
if ($private -and (Get-FileHash -Algorithm SHA256 -LiteralPath $ExecutablePath).Hash -ne '78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED') {
    throw 'The private fixture does not match the reviewed FFX executable'
}
$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC x86 Build Tools were not found' }
$vcvars = Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$results = [Collections.Generic.List[object]]::new()
function Record($name, $code) {
    $results.Add(@{name=$name; exit=$code})
    $results | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $obj 'results.json') -Encoding utf8
    if ($code -ne 0) { throw "$name failed ($code)" }
}
function Compile([string]$name, [string[]]$sources, [string[]]$extra=@()) {
    $exe = Join-Path $obj ($name + '.exe')
    $arguments = @('/nologo','/std:c++17','/EHsc','/O2','/MT','/W4','/WX','/utf-8','/DFFXHOOKS_TESTING',
                   ('/I"'+$obj+'"'),('/I"'+(Join-Path $here 'third_party\minhook\include')+'"'))
    $arguments += $sources | ForEach-Object {'"'+(Join-Path $here $_)+'"'}
    $arguments += $extra
    $arguments += @('/Fe"'+$exe+'"', 'kernel32.lib', 'user32.lib', 'bcrypt.lib')
    $response = Join-Path $obj ($name + '.rsp')
    [IO.File]::WriteAllText($response, ($arguments -join "`r`n"), [Text.Encoding]::Unicode)
    & $env:ComSpec /d /c ('call "'+$vcvars+'" x86 >nul && cl @"'+$response+'"') 2>&1 | Tee-Object (Join-Path $obj ($name+'.build.log')) | Write-Host
    Record ($name+'-build') $LASTEXITCODE
    return $exe
}
function Execute([string]$name, [string]$exe, [string[]]$arguments=@()) {
    & $exe @arguments 2>&1 | Tee-Object (Join-Path $obj ($name+'.log')) | Write-Host
    Record $name $LASTEXITCODE
}
Push-Location $obj
try {
    $core = @('hooks\TextLanguageCore.cpp')
    $payload = $core + @('hooks\TextLanguagePayload.cpp')
    foreach ($name in @('Core','Payload','Field')) {
        [string[]]$sources = if ($name -eq 'Core') {$core} else {$payload}
        $exe = Compile ('TextLanguage'+$name+'Rt0') ($sources + @('tests\TextLanguage'+$name+'Rt0.cpp'))
        Execute $name $exe
    }
    $exe = Compile 'TextLanguageSettingsRt0' @('tests\TextLanguageSettingsRt0.cpp','shared\Config.cpp')
    Execute 'Settings' $exe
    $io = $core + @('hooks\TextLanguageFiles.cpp')
    $fileTest = Compile 'TextLanguageFilesRt1' ($io + @('tests\TextLanguageFilesRt1.cpp'))
    $validator = Compile 'TextLanguageValidate' ($payload + @('hooks\TextLanguagePack.cpp','hooks\TextLanguageFiles.cpp','tests\TextLanguageValidate.cpp'))
    if ($private) {
        Execute 'Files-private' $fileTest @($PackageDirectory)
        Execute 'Pack-private' $validator @($PackageDirectory,$ReferenceDirectory)
        & python (Join-Path $repo 'tools\text_languages\check_received_pack.py') --validator $validator `
            --pack $PackageDirectory --reference $ReferenceDirectory --output (Join-Path $obj 'received-pack')
        Record 'Received-pack-private' $LASTEXITCODE
    } else {
        $synthetic = Join-Path $obj ('synthetic-'+[guid]::NewGuid().ToString('N'))
        & python (Join-Path $repo 'tools\text_languages\synthetic_files.py') --output $synthetic
        Record 'Synthetic-fixture' $LASTEXITCODE
        Execute 'Files-synthetic' $fileTest @($synthetic)
        & $validator $synthetic $synthetic
        if ($LASTEXITCODE -ne 1) { throw 'A synthetic IO fixture must never be admitted as a game text package' }
        Write-Host 'Synthetic fixture correctly rejected by full pack admission.'
    }
    $mapper = Get-Content -Raw (Join-Path $here 'tests\ArenaPositionNativeRt1.cpp')
    $begin = $mapper.IndexOf('static std::vector<unsigned char> Read(')
    $end = $mapper.IndexOf('static bool Nested(', $begin)
    if ($begin -lt 0 -or $end -le $begin) { throw 'Reviewed isolated PE mapper not found' }
    [IO.File]::WriteAllText((Join-Path $obj 'TextLanguagePeFixture.inc'), $mapper.Substring($begin,$end-$begin))
    # Keep the dependency's existing warning policy separate from /W4 /WX on our C++.
    $vendorSources = @('hook.c','buffer.c','trampoline.c','hde\hde32.c') |
        ForEach-Object {'"'+(Join-Path $here ('third_party\minhook\src\'+$_))+'"'}
    $vendorArgs = '/nologo /c /O2 /MT /W3 /I"'+(Join-Path $here 'third_party\minhook\include')+'" '+($vendorSources -join ' ')
    & $env:ComSpec /d /c ('call "'+$vcvars+'" x86 >nul && cl '+$vendorArgs) 2>&1 |
        Tee-Object (Join-Path $obj 'MinHook.build.log') | Write-Host
    Record 'MinHook-build' $LASTEXITCODE
    $vendorObjects = @('hook.obj','buffer.obj','trampoline.obj','hde32.obj') |
        ForEach-Object {'"'+(Join-Path $obj $_)+'"'}
    $native = $payload + @('hooks\TextLanguagePack.cpp','hooks\TextLanguageFiles.cpp','hooks\TextLanguageHook.cpp',
        'hooks\F8RuntimeCore.cpp','hooks\MinHookBatchCoordinator.cpp','shared\Config.cpp','tests\TextLanguageNativeRt1.cpp')
    $nativeExe = Compile 'TextLanguageNativeRt1' $native (@('/DFFXHOOKS_HAVE_POLYHOOK') + $vendorObjects)
    if ($private) {
        foreach ($mode in @('active','concurrent','early','stop','stop-validated','stop-reading','stop-committed','bad-font','late')) {
            Execute ('Native-'+$mode) $nativeExe @($ExecutablePath,$PackageDirectory,$ReferenceDirectory,$mode)
        }
    } else { Write-Host 'NATIVE_PRIVATE_FIXTURE=NOT_SUPPLIED; native adapter compiled only.' }
    & (Join-Path $here 'equipment_workshop_menu_rt1.ps1')
    Record 'F8-and-Workshop' $LASTEXITCODE
    if ($private) {
        & (Join-Path $here 'native_language_rt1.ps1') -ExecutablePath $ExecutablePath
        Record 'Audio-languages' $LASTEXITCODE
    }
    if ($BuildDll) {
        & (Join-Path $here 'build_hooks.ps1') -WithPolyHook -Release
        Record 'Full-DLL-Release' $LASTEXITCODE
        Get-FileHash -Algorithm SHA256 (Join-Path $here 'bin\Release\ffx-hooks.dll') | Format-List | Out-Host
    }
} finally { Pop-Location }
