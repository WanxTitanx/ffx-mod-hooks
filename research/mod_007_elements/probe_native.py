#!/usr/bin/env python3
"""Hash-gated native x86 affinity probe. Reads an EXE; executes only a local isolated copy."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

import capstone
import pefile

EXE_SHA = "78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced"
CODE_SHA = "3ec1a5447679ce745684028ac1e25f4708e33f8d4a7b79b8667c6ae211ede36f"
RVA, SIZE, BASE = 0x38A420, 1045, 0x400000


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.exe.resolve() == args.output.resolve():
        parser.error("The output must not replace the source executable")
    raw = args.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest() != EXE_SHA:
        raise SystemExit("Unsupported executable SHA-256; no code executed")
    pe = pefile.PE(data=raw)
    if pe.FILE_HEADER.Machine != 0x14C or pe.OPTIONAL_HEADER.ImageBase != BASE:
        raise SystemExit("Unsupported PE identity")
    code = pe.get_data(RVA, SIZE)
    if hashlib.sha256(code).hexdigest() != CODE_SHA:
        raise SystemExit("Unexpected routine bytes")
    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    cs.detail = True
    instructions = list(cs.disasm(code, BASE + RVA))
    if sum(i.size for i in instructions) != SIZE:
        raise SystemExit("Incomplete disassembly")
    for ins in instructions:
        if ins.mnemonic.startswith("call"):
            raise SystemExit("External call: relocation review required")
        if ins.group(capstone.CS_GRP_JUMP):
            if len(ins.operands) != 1 or ins.operands[0].type != capstone.x86.X86_OP_IMM:
                raise SystemExit("Indirect branch: relocation review required")
            if not BASE + RVA <= ins.operands[0].imm < BASE + RVA + SIZE:
                raise SystemExit("External branch: relocation review required")
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == 0:
                raise SystemExit("Absolute memory operand: relocation review required")
    source = Path(__file__).with_name("native_probe.c")
    with tempfile.TemporaryDirectory(prefix="ffx-mod007-native-") as tmp:
        root = Path(tmp)
        (root / "routine.bin").write_bytes(code)
        (root / "routine.S").write_text('.text\n.globl native_element\n.type native_element,@function\nnative_element:\n.incbin "routine.bin"\n.section .note.GNU-stack,"",@progbits\n')
        compile_args = ["gcc", "-m32", "-O2", "-ffreestanding", "-fno-builtin", "-fno-pie",
                        "-no-pie", "-fno-stack-protector", "-nostdlib", "-Wl,-e,_start",
                        str(source), "routine.S", "-o", "native_probe"]
        subprocess.run(compile_args, cwd=root, check=True, capture_output=True, text=True)
        result = subprocess.run([str(root / "native_probe")], cwd=root, capture_output=True,
                                text=True, timeout=30, check=False)
        if result.returncode:
            raise SystemExit(f"Native probe failed ({result.returncode}): {result.stdout!r} {result.stderr!r}")
        outcomes = json.loads(result.stdout)
    report = {"producer": "Jarvis-HOOK", "date": "2026-09-27", "level": "RT1-isolated-native-routine",
              "scope": "ApplyElementResist only; no process injection, no game session, no MOD-007 implementation",
              "exe_path": str(args.exe.resolve()), "exe_sha256": EXE_SHA,
              "machine": "PE32-i386", "image_base": hex(BASE), "rva": hex(RVA), "flat": hex(BASE+RVA),
              "routine_size": SIZE, "routine_sha256": CODE_SHA,
              "instructions": len(instructions), "external_calls": 0,
              "harness_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
              "compiler": subprocess.check_output(["gcc", "--version"], text=True).splitlines()[0],
              "compile_args": compile_args, "result": outcomes,
              "limitations": ["No complete hit pipeline, Nul/counter/Reflect/save/UI validation",
                              "High bits are passed to a DWORD argument, not serialized into a native BYTE",
                              "No 9th/10th element runtime adapter exists in this experiment"]}
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"comparisons": outcomes["comparisons"], "failures": outcomes["failures"],
                      "output": str(args.output)}))


if __name__ == "__main__":
    main()
