#!/usr/bin/env python3
"""Read-only MOD-006 PE/font evidence for the exact supported executable."""
import argparse
import hashlib
import json
from pathlib import Path

EXE_SHA = "78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced"
FUNCTIONS = {"stream_construct": (0x207D80, 0x48),
             "stream_open": (0x208100, 0x140),
             "stream_read_region": (0x208230, 0xC0),
             "stream_exists": (0x207FC0, 0x140), "stream_seek": (0x2082A0, 0x40),
             "stream_close": (0x207F40, 0x40), "stream_size": (0x207F80, 0x40),
             "font_register": (0x4AC0E0, 0x1C0), "font_page_path": (0x4ACF00, 0x400),
             "locale_get": (0x241290, 0x30), "save_mismatch": (0x387430, 0x100)}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    exe, assets, output = args.exe.resolve(strict=True), args.assets.resolve(strict=True), args.output.resolve()
    if output.exists() or output == exe or output == assets or assets in output.parents:
        parser.error("Use a new isolated evidence directory outside the assets")
    digest = hashlib.sha256(exe.read_bytes()).hexdigest()
    if digest != EXE_SHA:
        parser.error("Executable profile mismatch: " + digest)
    import pefile
    import capstone
    pe = pefile.PE(str(exe))
    if pe.FILE_HEADER.Machine != 0x14C or pe.OPTIONAL_HEADER.ImageBase != 0x400000:
        parser.error("Expected the profiled PE32/i386 image")
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoder.detail = True
    output.mkdir(parents=True)
    report = {"sha256": digest, "image_base": 0x400000, "functions": {}, "assets": []}
    for name, (rva, length) in FUNCTIONS.items():
        code = pe.get_data(rva, length)
        lines, strings = [], {}
        for ins in decoder.disasm(code, 0x400000 + rva):
            lines.append(f"{ins.address:08x} {ins.bytes.hex():24s} {ins.mnemonic} {ins.op_str}")
            for operand in ins.operands:
                if operand.type != capstone.x86.X86_OP_IMM or not 0xB00000 <= operand.imm < 0xC60000:
                    continue
                raw = pe.get_data(operand.imm - 0x400000, 256).split(b"\0", 1)[0]
                if len(raw) > 3 and all(32 <= c < 127 for c in raw):
                    strings[hex(operand.imm)] = raw.decode("ascii")
        (output / (name + ".asm.txt")).write_text("\n".join(lines), encoding="utf-8")
        report["functions"][name] = {"rva": rva, "bytes": code.hex(), "strings": strings}
    for path in assets.rglob("*"):
        if not path.is_file() or path.is_symlink():
            continue
        relative = path.relative_to(assets).as_posix()
        if "base_ftc" not in relative.lower() and path.name.lower() not in ("base.ftc", "ffxsjistbl_us.bin"):
            continue
        if path.stat().st_size > 64 * 1024 * 1024:
            continue
        raw = path.read_bytes()
        report["assets"].append({"path": relative, "size": len(raw), "sha256": hashlib.sha256(raw).hexdigest(), "prefix": raw[:160].hex()})
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"output": str(output), "functions": len(FUNCTIONS), "font_assets": len(report["assets"]), "sha256": digest}))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
