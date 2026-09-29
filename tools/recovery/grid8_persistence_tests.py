#!/usr/bin/env python3
"""Source-bound x86 Grid8 consumer tests in a fresh VMTasks folder. Never deploys."""
from pathlib import Path
import argparse
import base64
import hashlib
import io
import json
import re
import secrets
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[2]
MOD = ROOT / "src/runtime/FfxHooksDll"
CASES = ("normal off invalid validate master-off profile pin unready source-absent "
         "source-unready corrupt stop-capture abort reset load revoked stop session layout image thread").split()


def run(host: str, output: Path) -> int:
    output.mkdir(parents=True, exist_ok=False)
    remote = "C:/VMTasks/grid8-persistence-" + secrets.token_hex(6)

    def ps(script: str, name: str):
        encoded = base64.b64encode(script.encode("utf-16le")).decode()
        result = subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", host,
            "powershell", "-NoProfile", "-NonInteractive", "-EncodedCommand", encoded],
            capture_output=True, timeout=300)
        (output / name).write_bytes(result.stdout + result.stderr)
        return result

    preflight = ("$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';"
        "$busy=@(Get-Process -ErrorAction Stop|Where-Object {$_.ProcessName -in @('cl','link','MSBuild')});"
        "if($busy){exit 75};")
    inspected = ps(preflight + "exit 0", "preflight.log")
    if inspected.returncode:
        return inspected.returncode
    tests = ["SphereGridProgress8CommitRt0", "SphereGridProgress8SaveServiceRt0",
             "SphereGridProgress8RuntimeRt1", "SphereGridProgress8StoreRt1", "SphereGridProgress8StoreFaultRt1"]
    payload = {}
    for folder in ("hooks", "shared"):
        for path in sorted((MOD / folder).rglob("*.h")):
            if path.is_symlink():
                raise ValueError("symlink in source headers")
            payload[path.relative_to(MOD).as_posix()] = path.read_bytes()
    sources = ["SphereGridProgress8Runtime", "SphereGridProgress8Store", "RonsoPoolSave",
               "RonsoPoolStore", "RonsoPoolCore", "F8RuntimeCore"]
    for stem in sources:
        payload[f"hooks/{stem}.cpp"] = (MOD / f"hooks/{stem}.cpp").read_bytes()
    for stem in tests:
        payload[f"tests/{stem}.cpp"] = (MOD / f"tests/{stem}.cpp").read_bytes()
    commands = ["@echo off",
        'call "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x86 >environment.log 2>&1',
        "if errorlevel 1 exit /b %errorlevel%"]
    extras = {
        tests[0]: "", tests[1]: " hooks/RonsoPoolSave.cpp",
        tests[2]: " " + " ".join(f"hooks/{stem}.cpp" for stem in sources) + " bcrypt.lib",
        tests[3]: " hooks/SphereGridProgress8Store.cpp", tests[4]: "",
    }
    for stem in tests:
        commands += [f"cl /nologo /EHsc /std:c++17 /utf-8 /W4 /WX /MT /O2 /DFFXHOOKS_TESTING tests/{stem}.cpp{extras[stem]} /Fe:{stem}.exe >{stem}-build.log 2>&1",
                     "if errorlevel 1 exit /b %errorlevel%"]
    commands.append("exit /b 0")
    payload["build.cmd"] = ("\r\n".join(commands) + "\r\n").encode()
    manifest = {name: hashlib.sha256(data).hexdigest() for name, data in payload.items()}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    payload["manifest.json"] = json.dumps(manifest).encode()
    archive = io.BytesIO()
    with tarfile.open(fileobj=archive, mode="w") as tar:
        for name, data in payload.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
    prepared = ps(f"$ErrorActionPreference='Stop';if(Test-Path '{remote}'){{throw 'task exists'}};New-Item -ItemType Directory '{remote}'|Out-Null;exit 0", "prepare.log")
    if prepared.returncode:
        return prepared.returncode
    (output / "task.json").write_text(json.dumps({"host": host, "directory": remote, "game": False}, indent=2))
    copied = subprocess.run(["ssh", "-o", "BatchMode=yes", host, f'tar -xf - -C "{remote}"'],
        input=archive.getvalue(), capture_output=True, timeout=90)
    (output / "copy.log").write_bytes(copied.stdout + copied.stderr)
    if copied.returncode:
        return copied.returncode
    verify = (f"Set-Location '{remote}';$m=Get-Content manifest.json -Raw|ConvertFrom-Json;"
              "foreach($p in $m.PSObject.Properties){if((Get-FileHash -LiteralPath $p.Name -Algorithm SHA256).Hash -ne $p.Value){throw 'source hash mismatch'}};")
    built = ps(preflight + verify + "& $env:ComSpec /d /c build.cmd;exit $LASTEXITCODE", "build.log")
    for stem in tests:
        result = ps(f"Get-Content -LiteralPath '{remote}/{stem}-build.log' -ErrorAction SilentlyContinue", stem + "-build.log")
        if built.returncode:
            print(result.stdout.decode(errors="replace"), flush=True)
    if built.returncode:
        return built.returncode
    entries = [(tests[0], "core", ""), (tests[1], "service", ""),
               (tests[3], "store", f"'{remote}/store-data'"),
               (tests[4], "faults", f"'{remote}/fault-data'")]
    entries += [(tests[2], case, f"'{remote}/case-{case}' '{case}'") for case in CASES]
    results = []
    for stem, case, args in entries:
        result = ps(f"& '{remote}/{stem}.exe' {args};exit $LASTEXITCODE", case + ".log")
        text = result.stdout.decode(errors="replace")
        match = re.search(r": (\d+)/(\d+) passed", text)
        passed, total = map(int, match.groups()) if match else (0, 0)
        ok = result.returncode == 0 and total > 0 and passed == total
        results.append({"case": case, "exit_code": result.returncode, "checks": total, "passed": passed, "ok": ok})
        print(case, result.returncode, f"{passed}/{total}", flush=True)
        if not ok:
            print(text, flush=True)
    report = {"results": results, "all_passed": all(item["ok"] for item in results),
              "source_manifest": manifest, "scope": "real consumer, registry, hashing and Win32 disk; explicit native-state/profile/session endpoints", "game": False}
    (output / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0 if report["all_passed"] else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="windows11-dev-next")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    raise SystemExit(run(args.host, args.output.resolve()))
