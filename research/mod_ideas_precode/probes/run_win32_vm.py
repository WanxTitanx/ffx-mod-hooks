#!/usr/bin/env python3
"""Upload, run and remove the pure Win32 model harness through QEMU guest agent."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import re
import struct
import subprocess
import uuid
from pathlib import Path

VM = "windows11-dev-next"


def qga(command: dict[str, object]) -> object:
    process = subprocess.run(
        ["virsh", "-c", "qemu:///system", "qemu-agent-command", VM,
         json.dumps(command, separators=(",", ":"))],
        capture_output=True, text=True, check=True,
    )
    result = json.loads(process.stdout)
    if "error" in result:
        raise RuntimeError(f"QGA: {result['error']}")
    return result["return"]


def vm_exec(wrapper: Path, command: str) -> str:
    process = subprocess.run(["bash", str(wrapper), command, "20"],
                             capture_output=True, text=True, check=True)
    return process.stdout + process.stderr


def check_win32_exe(data: bytes) -> None:
    if not 1024 <= len(data) <= 16 * 1024 * 1024 or data[:2] != b"MZ":
        raise ValueError("Expected bounded PE32 executable")
    pe_at = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_at : pe_at + 4] != b"PE\0\0":
        raise ValueError("Missing PE signature")
    machine = struct.unpack_from("<H", data, pe_at + 4)[0]
    optional_magic = struct.unpack_from("<H", data, pe_at + 24)[0]
    if machine != 0x014C or optional_magic != 0x10B:
        raise ValueError("Expected Intel i386 PE32 executable")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--vm-exec", type=Path, required=True,
                        help="tracked research_tools/Vm/vm_exec.sh from a clean Editor worktree")
    args = parser.parse_args()
    if not args.vm_exec.is_file():
        parser.error("VM execution wrapper is missing")
    expected_exe = (Path(__file__).resolve().parents[1] / "build" /
                    "mod_ideas_win32.exe").resolve()
    if args.exe.resolve() != expected_exe:
        parser.error("Only the local precode build/mod_ideas_win32.exe may run in the VM")
    data = args.exe.read_bytes()
    check_win32_exe(data)
    local_sha = hashlib.sha256(data).hexdigest()
    guest_path = rf"C:\Users\Public\Documents\mod_ideas_precode_{uuid.uuid4().hex}.exe"
    handle: int | None = None
    removed = False
    output = ""
    print(f"guest_temp_path={guest_path}")
    try:
        handle = int(qga({"execute": "guest-file-open",
                          "arguments": {"path": guest_path, "mode": "w"}}))
        for at in range(0, len(data), 49152):
            chunk = data[at : at + 49152]
            result = qga({
                "execute": "guest-file-write",
                "arguments": {
                    "handle": handle,
                    "buf-b64": base64.b64encode(chunk).decode("ascii"),
                },
            })
            if not isinstance(result, dict) or result.get("count") != len(chunk):
                raise RuntimeError(f"Short QGA write at offset {at}: {result}")
        qga({"execute": "guest-file-close", "arguments": {"handle": handle}})
        handle = None

        verify = vm_exec(args.vm_exec, f"certutil -hashfile {guest_path} SHA256")
        if local_sha not in verify.lower():
            raise RuntimeError("Guest copy SHA-256 differs from local executable")
        output = vm_exec(args.vm_exec, guest_path)
        match = re.search(r"MODEL_PASS ([0-9]+)/([0-9]+) checks", output)
        if not match or match.group(1) != match.group(2) or int(match.group(1)) < 116:
            raise RuntimeError(f"Win32 harness did not report all checks: {output[:500]}")
        print(f"RT1_WIN32_MODEL_PASS sha256={local_sha} output={output.strip()}")
        return 0
    finally:
        if handle is not None:
            qga({"execute": "guest-file-close", "arguments": {"handle": handle}})
        vm_exec(args.vm_exec, f"del /q {guest_path}")
        check = vm_exec(args.vm_exec,
                        f"if exist {guest_path} (echo PRESENT) else (echo ABSENT)")
        removed = "ABSENT" in check and "PRESENT" not in check
        print(f"guest_temp_removed={removed}")
        if not removed:
            raise RuntimeError(f"Temporary harness remained in VM: {guest_path}")


if __name__ == "__main__":
    raise SystemExit(main())
