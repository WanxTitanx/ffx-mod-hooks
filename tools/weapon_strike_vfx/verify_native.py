#!/usr/bin/env python3
"""Execute authored particle programs in the exact FFX x86 handlers via Unicorn.

The fixture supplies resource pointers, not a live game or a graphics device.
This verifies VM semantics and draw-input values, not gameplay/visual RT2.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_EDI, UC_X86_REG_ESP

EXE_SHA = "78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced"
BASE = 0x400000
CTX, RECORD, SCRIPT, POOL, RESOURCE = 0x20001000, 0x20002000, 0x20003000, 0x20004000, 0x20006000
PARTICLE, STACK, STOP = 0x20008000, 0x20100000, 0x200FF000


def execute(pe, program):
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    image = pe.get_memory_mapped_image()
    u.mem_map(BASE, (len(image) + 4095) & ~4095)
    u.mem_write(BASE, image)
    u.mem_map(0x20000000, 0x200000)

    def put(address, fmt, *values):
        u.mem_write(address, struct.pack(fmt, *values))

    def get(address, fmt):
        return struct.unpack(fmt, u.mem_read(address, struct.calcsize(fmt)))

    def call(handler, *arguments, budget=50000):
        put(STACK, "<" + "I" * (len(arguments) + 1), STOP, *arguments)
        u.reg_write(UC_X86_REG_ESP, STACK)
        u.emu_start(handler, STOP, count=budget)
        return u.reg_read(UC_X86_REG_EAX)

    put(CTX + 0x220, "<I", RECORD)
    put(POOL + 32, "<I", RECORD)
    put(POOL + 96, "<I", RESOURCE)
    put(RESOURCE + 16, "<I", 0x100)
    for index in range(18):
        put(RESOURCE + 0x100 + 4 * index, "<I", 0x20010000 + index * 0x100)
    # Construct a real native type-9 buffer and compare it byte-for-byte with
    # the C++ adapter's constructor. Native arena setup/allocation also executes.
    arena = call(0x7FF1F0, PARTICLE - 32, 0x60000, budget=1000000)
    assert arena == PARTICLE - 32 + 0x60000
    put(CTX + 0x3A8, "<I", arena)
    put(CTX + 0x320, "<I", RECORD)
    put(CTX + 0x31C, "<I", 0x20005000)
    put(CTX + 0x334, "<H", 512)
    u.mem_write(0x20005000, bytes([255]) * 1024)
    put(RECORD + 0xC8, "<I", 0x80808080)
    put(CTX + 0x21A, "<H", 0x008F)
    put(SCRIPT, "<2H", 0x008F, 16)
    assert call(0x81BCD0, CTX, SCRIPT) == SCRIPT + 4
    buffer = get(RECORD + 0xA8, "<I")[0]
    assert buffer == PARTICLE - 32 and get(RECORD + 0xBC, "<I")[0] == buffer
    assert get(RECORD + 0xBB, "<B")[0] == 9 and get(arena - 2, "<H")[0] == 1
    assert get(0x20005000, "<h")[0] == 0, "Native constructor did not register its side pass"
    native_ring = bytes(u.mem_read(buffer, 0x70 + 16 * 0x50))
    assert native_ring == bytes.fromhex(program["ring_hex"]), "C++ ring differs from native constructor"
    words = program["words"]
    if len(words) > 128 or any(not isinstance(w, int) or not 0 <= w <= 65535 for w in words):
        raise ValueError("Invalid authored program")
    payload = struct.pack("<" + "H" * len(words), *words)
    u.mem_write(SCRIPT, payload)
    # Execute the authored parent until its stop wait, then exercise stop/drain.
    parent = SCRIPT
    for expected in (0x0809, 0x8026, 0x8064, 0x0052, 0x502F):
        assert get(parent, "<H")[0] == expected
        put(CTX + 0x21A, "<H", expected)
        put(CTX + 0x210, "<B", 0)
        handler = struct.unpack("<I", pe.get_data(0x848EC8 + (expected & 0xFF) * 4, 4))[0]
        next_parent = call(handler, CTX, parent)
        if expected == 0x502F:
            assert next_parent == parent and get(CTX + 0x210, "<B")[0] == 1
        else:
            parent = next_parent
    assert get(RECORD + 4, "<I")[0] == SCRIPT + program["child"] * 2
    put(RECORD + 0xB0, "<H", 0x4000)
    parent = call(0x817930, CTX, parent)
    assert get(parent, "<H")[0] == 0x002A
    put(CTX + 0x21A, "<H", 0x002A)
    parent = call(0x80AF20, CTX, parent)
    assert bytes(u.mem_read(RECORD + 4, 12)) == bytes(12)
    assert get(parent, "<H")[0] == 0x4009
    put(CTX + 0x21A, "<H", 0x4009)
    call(0x80C650, CTX, parent)
    assert get(CTX + 0x210, "<B")[0] == 32

    # Native emission selector -256 means SELF. Prove the program lands in the
    # owned ring and records its owner, instead of targeting vanilla batch 0x20.
    emit = SCRIPT + words.index(0x208F, program["child"]) * 2
    put(CTX + 0x21A, "<H", 0x208F)
    put(CTX + 0x210, "<B", 0)
    put(RECORD + 0x40, "<4f", 32.0, 48.0, 64.0, 1.0)
    assert call(0x81BCD0, CTX, emit) == emit + 6
    assert get(PARTICLE, "<I")[0] == SCRIPT + program["particle"] * 2
    assert get(PARTICLE + 0x4C, "<H")[0] == 0 and get(PARTICLE + 9, "<B")[0] == 1
    u.mem_write(PARTICLE + 0x50, bytes([0xA5]) * 32)
    pc = SCRIPT + program["particle"] * 2
    trace = []
    while True:
        if not SCRIPT <= pc <= SCRIPT + len(payload) - 2 or len(trace) >= 64:
            raise ValueError("Particle program did not yield inside its bounded input")
        opcode = get(pc, "<H")[0]
        # Keep the fixture narrow: no actor counters, native calls or arbitrary VM opcodes.
        if (opcode & 0xFF) not in (0, 1, 4, 5, 7, 9, 11):
            raise ValueError(f"Unexpected particle opcode: {opcode:#x}")
        put(CTX + 0x21A, "<H", opcode)
        handler = struct.unpack("<I", pe.get_data(0x849318 + (opcode & 0xFF) * 4, 4))[0]
        next_pc = call(handler, CTX, POOL, pc, PARTICLE)
        trace.append({"opcode": opcode, "handler_va": handler, "consumed": next_pc - pc})
        pc = next_pc
        if get(CTX + 0x210, "<B")[0]:
            break

    lifetime = get(CTX + 0x210, "<B")[0]
    if not 1 <= lifetime < 32:
        raise ValueError("Lifetime exceeds the authored drain contract")
    initial_color = list(get(PARTICLE + 0x1C, "<4B"))
    initial_size = get(PARTICLE + 0xC, "<H")[0]
    texture = (get(PARTICLE + 4, "<I")[0] - 0x20010000) // 0x100
    frames = []
    for frame in range(lifetime):
        u.reg_write(UC_X86_REG_ESP, STACK)
        u.reg_write(UC_X86_REG_EBP, STACK - 0x100)
        u.reg_write(UC_X86_REG_EDI, PARTICLE)
        u.reg_write(UC_X86_REG_EBX, CTX)
        # Actual native motion/size/RGBA integration and copy to draw context.
        # Stop before projection/graphics dispatch: no GPU behavior is simulated.
        u.emu_start(0x812723, 0x8127AD, count=5000)
        color = list(get(PARTICLE + 0x1C, "<4B"))
        assert color == list(get(CTX + 4, "<4B")), "Native draw context lost particle color"
        assert color[:3] == initial_color[:3] and color[3] == initial_color[3] - 4 * (frame + 1)
        assert u.mem_read(PARTICLE + 0x50, 32) == bytes([0xA5]) * 32
        frames.append({"frame": frame, "rgba": color, "position": list(get(PARTICLE + 0x10, "<3f")),
                       "size": get(PARTICLE + 0xC, "<H")[0], "angle": get(PARTICLE + 0x3C, "<h")[0]})

    assert get(pc, "<H")[0] == 0, "Particle does not terminate after its finite wait"
    put(CTX + 0x21A, "<H", 0)
    put(STACK, "<5I", STOP, CTX, POOL, pc, PARTICLE)
    u.reg_write(UC_X86_REG_ESP, STACK)
    u.emu_start(0x810800, STOP, count=5000)
    assert u.reg_read(UC_X86_REG_EAX) == 0 and get(CTX + 0x210, "<B")[0] == 255
    return {"name": program["name"], "program_sha256": hashlib.sha256(payload).hexdigest(),
            "native_ring_matches_cpp": True, "emission_owns_ring": True, "parent_stop_drain_ticks": 32,
            "texture_index": texture, "initial_rgba": initial_color, "initial_size": initial_size,
            "lifetime": lifetime, "native_trace": trace, "frames": frames, "terminated": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--programs", type=Path, required=True, help="JSON emitted by WeaponStrikeVfxRt0 --dump")
    parser.add_argument("--report", type=Path, help="New report path; existing files are refused")
    args = parser.parse_args()
    try:
        data = args.exe.read_bytes()
        if hashlib.sha256(data).hexdigest() != EXE_SHA:
            raise ValueError("FFX.exe identity mismatch")
        programs = json.loads(args.programs.read_text())
        if len(programs) != 2:
            raise ValueError("Expected exactly two authored visuals")
        pe = pefile.PE(data=data)
        results = [execute(pe, program) for program in programs]
        holy, shadow = results
        assert holy["texture_index"] == 4 and shadow["texture_index"] == 1
        assert holy["initial_rgba"] == [128, 110, 52, 112] and shadow["initial_rgba"] == [70, 32, 128, 96]
        assert holy["frames"][-1]["position"][1] < holy["frames"][0]["position"][1]
        assert shadow["frames"][-1]["position"][1] > shadow["frames"][0]["position"][1]
        report = {"exe_sha256": EXE_SHA, "level": "native x86 emulation; no live game or graphics device", "visuals": results}
        payload = json.dumps(report, indent=2) + "\n"
        if args.report:
            with args.report.open("x", encoding="utf-8") as stream:
                stream.write(payload)
        else:
            print(payload, end="")
        print("PASS: both authored visuals, native RGBA/size/motion integration, finite lifetime and canaries")
    except (OSError, ValueError, AssertionError) as error:
        parser.exit(1, f"Native VFX verification refused: {error}\n")


if __name__ == "__main__":
    main()
