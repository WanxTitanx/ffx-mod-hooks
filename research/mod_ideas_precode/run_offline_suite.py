#!/usr/bin/env python3
"""Run the bounded offline evidence suite without changing game or source inputs."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKET = Path(__file__).resolve().parent
BUILD = PACKET / "build"
EXPECTED_EDITOR_COMMIT = "e5f05554426f27a83d4ee70f7be32695431ac66b"
EXPECTED_MONMAGIC_SHA = "85402f76af8b0bacdfb3850a1c8907ef76f825b43ad22af3a0bfddc05983707a"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(editor_root: Path, *args: str) -> str:
    process = subprocess.run(
        ["git", "-C", str(editor_root), *args],
        capture_output=True, text=True, check=True,
    )
    return process.stdout.strip()


def run_step(name: str, command: list[str], *, cwd: Path, env: dict[str, str],
             required: str | None = None) -> dict[str, object]:
    started = time.monotonic()
    process = subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True)
    output = process.stdout + process.stderr
    log = BUILD / f"{name}.log"
    log.write_text(output)
    ok = process.returncode == 0 and (required is None or required in output)
    if name == "monmagic_grow":
        selected = [line for line in output.splitlines() if line.strip().startswith("checks")]
    else:
        selected = [
            line for line in output.splitlines()
            if any(key in line for key in ("PASS", "Passed!", "Failed:", "REPRODUCED_RT0", "PE32 executable"))
        ]
    print(f"{'PASS' if ok else 'FAIL'} {name}: exit={process.returncode} "
          f"duration={time.monotonic() - started:.1f}s "
          f"summary={selected[-1][:180] if selected else '(see log)'}")
    return {
        "name": name,
        "pass": ok,
        "exitCode": process.returncode,
        "requiredMarker": required,
        "durationSeconds": round(time.monotonic() - started, 2),
        "log": str(log),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--editor-root", required=True, type=Path)
    parser.add_argument("--ffx-exe", required=True, type=Path)
    parser.add_argument("--skip-sanitized", action="store_true")
    args = parser.parse_args()
    editor = args.editor_root.resolve()
    exe = args.ffx_exe.resolve()
    if git(editor, "rev-parse", "HEAD") != EXPECTED_EDITOR_COMMIT:
        parser.error("Editor commit differs from the pinned source identity")
    if git(editor, "status", "--porcelain"):
        parser.error("Use a clean isolated FFX Editor worktree")

    battle = editor / "FFXProjectEditor.Tests" / "Fixtures" / "Battle"
    save = editor / "FFXProjectEditor.Tests" / "Fixtures" / "Save" / "user_ffx_000"
    command = battle / "command.bin"
    monmagic = battle / "monmagic2.bin"
    pinned_inputs = {path: sha256(path) for path in (exe, save, command, monmagic)}
    if pinned_inputs[monmagic] != EXPECTED_MONMAGIC_SHA:
        parser.error("monmagic2.bin fixture differs from the pinned source identity")

    BUILD.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, DOTNET_ROLL_FORWARD="Major")
    results = [
        run_step(
            "ledger", [sys.executable, str(PACKET / "generate_ledger.py"), "--check"],
            cwd=ROOT, env=environment, required="PASS 97/97",
        ),
        run_step(
            "cpp_model", [str(PACKET / "run_tests.sh")],
            cwd=ROOT, env=environment, required="MODEL_PASS 116/116",
        ),
    ]
    if not args.skip_sanitized:
        results.append(run_step(
            "cpp_sanitized", [str(PACKET / "run_sanitized.sh")],
            cwd=ROOT, env=environment, required="MODEL_PASS 116/116",
        ))
    results.append(run_step(
        "win32_compile", [str(PACKET / "run_win32_compile.sh")],
        cwd=ROOT, env=environment, required="PE32 executable for MS Windows",
    ))
    results.extend((
        run_step(
            "pe_signatures", [sys.executable, str(PACKET / "probes" / "pe_signature_probe.py"), str(exe)],
            cwd=ROOT, env=environment, required="PASS signatures=11/11",
        ),
        run_step(
            "save_layout_defect",
            ["dotnet", "run", "--project", str(PACKET / "probes" / "save_layout" / "save_layout_probe.csproj"),
             f"-p:EditorRoot={editor}", "--", str(save)],
            cwd=ROOT, env=environment, required="REPRODUCED_RT0",
        ),
        run_step(
            "command_fields",
            ["dotnet", "run", "--project", str(PACKET / "probes" / "aeon_elements" / "aeon_element_probe.csproj"),
             f"-p:EditorRoot={editor}", "--", str(command)],
            cwd=ROOT, env=environment, required="COMMAND_OWNER_RT0_PASS",
        ),
        run_step(
            "editor_writers",
            ["dotnet", "test", str(editor / "FFXProjectEditor.Tests" / "FFXProjectEditor.Tests.csproj"),
             "--filter", "FullyQualifiedName~WeaponGearRoundTripTests|FullyQualifiedName~AbilityCommandRoundTripTests",
             "--verbosity", "quiet"],
            cwd=editor, env=environment, required="Passed:    15",
        ),
    ))

    with tempfile.TemporaryDirectory(prefix="modideas-monmagic-") as directory:
        result = run_step(
            "monmagic_grow",
            ["dotnet", str(editor / "FFXProjectEditor" / "bin" / "Debug" / "net8.0"
                            / "FFXProjectEditor.dll"),
             "--monmagic-grow-rt0", str(monmagic), directory],
            cwd=editor, env=environment, required="VERDICT: PASS",
        )
        manifest = Path(directory) / "monster_magic_grow_rt0.json"
        if manifest.exists():
            value = json.loads(manifest.read_text())
            fields = ("Pass", "ExistingRecordPayloadPreserved",
                      "ExistingTextPoolPrefixPreserved", "RereadCountOk",
                      "RereadNewTextOk", "PreserveWriteOk")
            result["pass"] = bool(result["pass"]) and all(value.get(field) is True for field in fields)
            result["entryCount"] = [value.get("OriginalEntryCount"), value.get("NewEntryCount")]
            result["outputSha256"] = sha256(Path(directory) / "monmagic2.bin")
        else:
            result["pass"] = False
        results.append(result)

    integrity = all(sha256(path) == before for path, before in pinned_inputs.items())
    results.append({"name": "input_integrity", "pass": integrity})
    print(f"{'PASS' if integrity else 'FAIL'} input_integrity: four pinned inputs unchanged")
    summary = {
        "generatedAtUtc": datetime.now(timezone.utc).isoformat(),
        "hooksCommit": git(ROOT, "rev-parse", "HEAD"),
        "editorCommit": git(editor, "rev-parse", "HEAD"),
        "inputs": {str(path): value for path, value in pinned_inputs.items()},
        "steps": results,
        "passed": sum(bool(item["pass"]) for item in results),
        "total": len(results),
    }
    summary["verdict"] = "PASS_RT0_AND_MODEL" if summary["passed"] == summary["total"] else "FAIL"
    (BUILD / "offline_suite_summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"{summary['verdict']} steps={summary['passed']}/{summary['total']}")
    return 0 if summary["verdict"] == "PASS_RT0_AND_MODEL" else 1


if __name__ == "__main__":
    raise SystemExit(main())
