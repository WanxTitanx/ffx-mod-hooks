#!/usr/bin/env python3
"""Compile the pure C++17 model with MSVC x86 in a disposable VM directory."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import re
import subprocess
import uuid
from pathlib import Path

VM = "windows11-dev-next"
VCVARSALL = (
    r"C:\Program Files\Microsoft Visual Studio\2022\Community"
    r"\VC\Auxiliary\Build\vcvarsall.bat"
)
PACKET = Path(__file__).resolve().parents[1]


def qga(command: dict[str, object]) -> object:
    process = subprocess.run(
        ["virsh", "-c", "qemu:///system", "qemu-agent-command", VM,
         json.dumps(command, separators=(",", ":"))],
        capture_output=True, text=True, check=True,
    )
    result = json.loads(process.stdout)
    if "error" in result:
        raise RuntimeError(str(result["error"]))
    return result["return"]


def vm_exec(wrapper: Path, command: str) -> str:
    process = subprocess.run(["bash", str(wrapper), command, "55"],
                             capture_output=True, text=True, check=True)
    return process.stdout + process.stderr


def guest_file_exists(path: str) -> bool:
    try:
        handle = qga({"execute": "guest-file-open",
                      "arguments": {"path": path, "mode": "r"}})
    except (RuntimeError, subprocess.CalledProcessError):
        return False
    qga({"execute": "guest-file-close", "arguments": {"handle": handle}})
    return True


def upload(path: str, data: bytes) -> None:
    handle = qga({"execute": "guest-file-open",
                  "arguments": {"path": path, "mode": "w"}})
    try:
        for at in range(0, len(data), 49152):
            chunk = data[at : at + 49152]
            reply = qga({
                "execute": "guest-file-write",
                "arguments": {
                    "handle": handle,
                    "buf-b64": base64.b64encode(chunk).decode("ascii"),
                },
            })
            if not isinstance(reply, dict) or reply.get("count") != len(chunk):
                raise RuntimeError(f"Short source transfer at {at}: {reply}")
    finally:
        qga({"execute": "guest-file-close", "arguments": {"handle": handle}})


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vm-exec", type=Path, required=True)
    parser.add_argument("--vcvarsall", default=VCVARSALL)
    args = parser.parse_args()
    if not args.vm_exec.is_file() or not guest_file_exists(args.vcvarsall):
        parser.error("VM wrapper or MSVC x86 vcvarsall is missing")
    guest = rf"C:\Users\Public\Documents\modideas_msvc_{uuid.uuid4().hex}"
    print(f"guest_temp_dir={guest}")
    created = False
    try:
        vm_exec(args.vm_exec, f"mkdir {guest}")
        created = True
        files = {
            "mod_ideas.hpp": PACKET / "include" / "mod_ideas.hpp",
            "mod_ideas.cpp": PACKET / "src" / "mod_ideas.cpp",
            "test_mod_ideas.cpp": PACKET / "tests" / "test_mod_ideas.cpp",
        }
        for name, local in files.items():
            upload(guest + "\\" + name, local.read_bytes())
        batch = (
            "@echo off\r\n"
            f"cd /d {guest}\r\n"
            f'call "{args.vcvarsall}" x86 >nul\r\n'
            "if errorlevel 1 exit /b 2\r\n"
            "cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT "
            "/I. mod_ideas.cpp test_mod_ideas.cpp /Fe:model_tests.exe\r\n"
            "if errorlevel 1 exit /b 3\r\n"
            "model_tests.exe\r\n"
            "if errorlevel 1 exit /b 4\r\n"
            "exit /b 0\r\n"
        )
        upload(guest + r"\build.cmd", batch.encode("ascii"))
        output = vm_exec(args.vm_exec, guest + r"\build.cmd")
        match = re.search(r"MODEL_PASS ([0-9]+)/([0-9]+) checks", output)
        if not match or match.group(1) != match.group(2) or int(match.group(1)) < 116:
            raise RuntimeError(f"MSVC x86 model result missing: {output[-1200:]}")
        source_hash = hashlib.sha256(
            b"".join(local.read_bytes() for local in files.values())
        ).hexdigest()
        print(f"RT1_MSVC_X86_MODEL_PASS source_sha256={source_hash}")
        print(next(line for line in output.splitlines() if "MODEL_PASS" in line))
        return 0
    finally:
        if created:
            vm_exec(args.vm_exec, f"rmdir /s /q {guest}")
            check = vm_exec(args.vm_exec, f"if exist {guest} (echo PRESENT) else (echo ABSENT)")
            removed = "ABSENT" in check and "PRESENT" not in check
            print(f"guest_temp_removed={removed}")
            if not removed:
                raise RuntimeError(f"Temporary MSVC directory remains: {guest}")


if __name__ == "__main__":
    raise SystemExit(main())
