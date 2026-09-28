#!/usr/bin/env python3
"""Jarvis-HOOK: hash-verified disposable Windows lanes for selected RT1 scripts.

The Windows host must be explicitly selected. This copies source into a new
directory, copies the private fixtures, and never deploys a DLL or runs FFX.exe.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import re
import subprocess
import sys
import time
import uuid
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ALLOWED_TESTS = {
    "monster_rewards_rt1.ps1",
    "f7_runtime_rt0.ps1",
    "f7_runtime_rt1.ps1",
    "f7_ui_rt0.ps1",
    "f7_config_rt0.ps1",
    "f8_runtime_rt0.ps1",
    "equipment_workshop_menu_rt1.ps1",
    "spira_runtime_rt1.ps1",
    "arcana_spira_composition_rt1.ps1",
    "arcana_combat_rt1.ps1",
    "ronso_pool_io_rt1.ps1",
    "text_languages_rt1.ps1",
    "mod_composition_rt1.ps1",
    "nul_ward_composition_rt1.ps1",
    "aeon_ascension_store_rt1.ps1",
    "shared_action_runtime_rt1.ps1",
    "vanguard_action_runtime_rt1.ps1",
    "vanguard_cast_runtime_rt1.ps1",
    "vanguard_formation_rt1.ps1",
    "vanguard_overdrive_rt1.ps1",
    "elemental_runtime_rt1.ps1",
    "shared_element_runtime_rt1.ps1",
    "elemental_core_rt0.ps1",
    "mod007_candidate_build.ps1",
    "shared_damage_composition_rt1.ps1",
    "vanguard_runtime_rt1.ps1",
    "equipment_workshop_transaction_rt1.ps1",
    "equipment_workshop_store_rt1.ps1",
    "equipment_workshop_save_flow_rt1.ps1",
    "nova_super_damage_rt1.ps1",
    "native_presentation_rt1.ps1",
    "equipment_workshop_runtime_rt1.ps1",
}
SOURCE_PREFIXES = (
    "README.md",
    "docs/F7_INLIVE.md",
    "docs/KNOWN_BUGS.md",
    "docs/ROADMAP.md",
    "src/runtime/FfxHooksDll/",
    "src/runtime/FfxDinput8Probe/",
    "src/runtime/NativeMenuShell/",
    "src/runtime/BattlePhotoMode/",
    "research/equipment_workshop/",
    "tools/text_languages/",
)


def powershell_literal(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def checked(arguments: list[str], **options: object) -> subprocess.CompletedProcess[str]:
    return subprocess.run(arguments, text=True, check=True, **options)


def snapshot(destination: Path, ability_kernel: Path | None) -> list[dict[str, object]]:
    result = checked(["git", "ls-files", "-z", "-co", "--exclude-standard"],
                     cwd=ROOT, capture_output=True)
    entries = []
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as archive:
        for relative in sorted(set(result.stdout.split("\0"))):
            if not relative.startswith(SOURCE_PREFIXES):
                continue
            path = ROOT / relative
            if not path.is_file() or path.is_symlink():
                continue
            if any(part in {"bin", "obj", "vcpkg_installed", ".superpowers", "__pycache__"}
                   for part in Path(relative).parts):
                continue
            # Include build projects and the tracked default INI required by
            # source-contract tests. Never sweep local runtime configurations.
            template = relative == "src/runtime/FfxHooksDll/ffx-hooks.ini"
            contract = relative in {"README.md", "docs/F7_INLIVE.md", "docs/KNOWN_BUGS.md", "docs/ROADMAP.md"}
            if not template and not contract and path.suffix.lower() not in {".h", ".hpp", ".c", ".cpp", ".inl", ".inc", ".ps1", ".vcxproj",
                                         ".cmake", ".txt", ".json", ".def", ".rc", ".py"}:
                continue
            data = path.read_bytes()
            if len(data) > 8 * 1024 * 1024:
                raise RuntimeError("Unexpectedly large source file: " + relative)
            archive.writestr(relative, data)
            entries.append({"path": relative, "bytes": len(data),
                            "sha256": hashlib.sha256(data).hexdigest()})
        if ability_kernel is not None:
            data = ability_kernel.read_bytes()
            if not 20 + 131 * 108 <= len(data) <= 4 * 1024 * 1024:
                raise RuntimeError("The private ability kernel has an unsupported size")
            relative = "sin-fixtures/a_ability.bin"
            archive.writestr(relative, data)
            entries.append({"path": relative, "bytes": len(data), "kind": "private_fixture",
                            "sha256": hashlib.sha256(data).hexdigest()})
        archive.writestr("source-manifest.json", json.dumps(entries, indent=2) + "\n")
    if not entries:
        raise RuntimeError("No eligible source files were captured")
    return entries


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--windows-host", required=True)
    parser.add_argument("--test", action="append", required=True, choices=sorted(ALLOWED_TESTS))
    parser.add_argument("--label", default="validation")
    parser.add_argument("--ability-kernel", type=Path,
                        help="Explicit local private a_ability.bin fixture; required outside the Nova-only harness")
    parser.add_argument("--fixture-root", default=r"C:\VMTasks\ffx-hooks-jarvis-20260918\native-fixtures")
    parser.add_argument("--dependency-root", default=r"C:\VMTasks\ffx-hooks-jarvis-20260918\src\runtime\FfxHooksDll\vcpkg_installed")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,100}", args.windows_host):
        parser.error("Use a configured SSH alias or a plain hostname")
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,50}", args.label):
        parser.error("The evidence label must be a short alphanumeric filename component")
    for test in args.test:
        if not (ROOT / "src/runtime/FfxHooksDll" / test).is_file():
            parser.error("The selected checked-in runner is missing: " + test)
    if any(test != "nova_super_damage_rt1.ps1" for test in args.test) and args.ability_kernel is None:
        parser.error("These native tests require an explicit --ability-kernel fixture")
    if args.ability_kernel is not None and not args.ability_kernel.is_file():
        parser.error("The explicit ability kernel is not a readable local file")
    lane = "ffx-mod007-" + uuid.uuid4().hex[:12]
    evidence = ROOT / ".superpowers/mod007" / (args.label + "-" + lane)
    evidence.mkdir(parents=True)
    archive = evidence / "source.zip"
    entries = snapshot(archive, args.ability_kernel)
    remote = "C:/VMTasks/" + lane
    receipt: dict[str, object] = {
        "producer": "Jarvis-HOOK", "level": "RT1-isolated-Windows-harness",
        "host": args.windows_host, "remote_lane": remote, "tests": args.test,
        "ability_kernel_source": str(args.ability_kernel.resolve()) if args.ability_kernel else None,
        "source_manifest": entries, "source_zip_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
        "started_unix": time.time(), "completed": False,
        "limitations": ["No game session", "No installed DLL or asset was changed",
                        "No independent review or Production promotion"],
    }
    (evidence / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print("EVIDENCE", evidence, flush=True)
    print("SOURCE", len(entries), "files", archive.stat().st_size, "zip bytes", flush=True)
    checked(["scp", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10",
             str(archive), args.windows_host + ":" + remote + ".zip"], timeout=180)
    tests = ",".join(powershell_literal(test) for test in args.test)
    script = r"""
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$lane = __LANE__
$archive = $lane + '.zip'
$fixtureSource = __FIXTURES__
$dependencySource = __DEPENDENCIES__
if (Test-Path -LiteralPath $lane) { throw 'Disposable lane already exists; refusing replacement' }
if ((Get-PSDrive C).Free -lt 2GB) { throw 'Insufficient free disk space for the isolated lane' }
if (-not (Test-Path -LiteralPath $fixtureSource)) { throw 'Private native fixtures are unavailable' }
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne __ZIP_HASH__) { throw 'Source ZIP fingerprint mismatch' }
Expand-Archive -LiteralPath $archive -DestinationPath $lane
$manifest = Get-Content -LiteralPath (Join-Path $lane 'source-manifest.json') -Raw | ConvertFrom-Json
foreach ($entry in $manifest) {
    $sourcePath = Join-Path $lane $entry.path
    if ((Get-Item -LiteralPath $sourcePath).Length -ne $entry.bytes -or
        (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) {
        throw ('Transferred source fingerprint mismatch: ' + $entry.path)
    }
}
Copy-Item -LiteralPath $fixtureSource -Destination (Join-Path $lane 'native-fixtures') -Recurse
$runtime = Join-Path $lane 'src\runtime\FfxHooksDll'
if (Test-Path -LiteralPath $dependencySource) {
    New-Item -ItemType Junction -Path (Join-Path $runtime 'vcpkg_installed') -Target $dependencySource | Out-Null
}
$results = @()
foreach ($test in @(__TESTS__)) {
    Write-Output ('RUN ' + $test)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    & powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path $runtime $test)
    $code = $LASTEXITCODE
    $watch.Stop()
    $results += [PSCustomObject]@{ test=$test; exit_code=$code; seconds=$watch.Elapsed.TotalSeconds }
    $results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $lane 'results.json') -Encoding UTF8
    if ($code -ne 0) { throw ('Isolated test failed: ' + $test + ' exit=' + $code) }
}
Write-Output 'ISOLATED_NATIVE_CHECKS_PASS'
"""
    substitutions = {
        "__LANE__": powershell_literal(remote),
        "__FIXTURES__": powershell_literal(args.fixture_root),
        "__DEPENDENCIES__": powershell_literal(args.dependency_root),
        "__ZIP_HASH__": powershell_literal(str(receipt["source_zip_sha256"])),
        "__TESTS__": tests,
    }
    for placeholder, value in substitutions.items():
        script = script.replace(placeholder, value)
    (evidence / "run.ps1").write_text(script, encoding="utf-8")
    encoded = base64.b64encode(script.encode("utf-16le")).decode("ascii")
    arguments = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", args.windows_host,
                 "powershell.exe", "-NoLogo", "-NoProfile", "-NonInteractive", "-EncodedCommand", encoded]
    exit_code = 1
    with (evidence / "windows.log").open("w", encoding="utf-8") as log:
        process = subprocess.Popen(arguments, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, encoding="utf-8", errors="replace")
        assert process.stdout is not None
        try:
            for line in process.stdout:
                log.write(line);log.flush();print(line, end="", flush=True)
            exit_code = process.wait(timeout=30)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
    receipt.update(completed=True, exit_code=exit_code, ended_unix=time.time())
    (evidence / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print("RESULT", exit_code, "EVIDENCE", evidence, flush=True)
    return exit_code


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print("VALIDATION_ERROR", str(error), file=sys.stderr)
        raise SystemExit(1) from error
