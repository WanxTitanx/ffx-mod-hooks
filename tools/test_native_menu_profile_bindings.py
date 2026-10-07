"""Compile the actual menu consumer bindings, not only the address ledger."""

from pathlib import Path
import hashlib
import os
import re
import shutil
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
DLL = ROOT / "src/runtime/FfxHooksDll"


class NativeMenuProfileBindings(unittest.TestCase):
    def binding(self, function, variable):
        source = (DLL / "dllmain.cpp").read_text(encoding="utf-8")
        start = source.index(function)
        end = source.index("\nstatic ", start + len(function))
        matches = re.findall(
            rf"const uintptr_t {variable}\s*=\s*([^;]+);", source[start:end]
        )
        self.assertEqual(len(matches), 1, "The production consumer must have one binding")
        return matches[0]

    def check_profile(self, steam):
        compiler = shutil.which("c++")
        if not compiler:
            self.skipTest("A C++17 compiler is required for the portable binding check")
        pump = self.binding("static bool StartNativeMenuIfEnabled() {", "pumpVa")
        pool = self.binding("static void ArenaTrace_MenuPoolTick(const char* source) {", "pool")
        # Expected RVAs are the independently inspected PE targets. Compiling
        # expressions from dllmain also catches consumers that bypass a correct
        # ledger, which a test of the constants alone would miss.
        pump_rva, pool_rva = (0x4A9CA0, 0x1440900) if steam else (0x4A9C50, 0x14408C0)
        code = f'''#include <cstdint>
#include <cstdio>
#include "shared/ffx_addresses.h"
int main() {{
    unsigned failures = 0;
    for (const uintptr_t g_base : {{uintptr_t(0x400000), uintptr_t(0x640000), uintptr_t(0x18000000)}}) {{
        const uintptr_t pump = {pump};
        const uintptr_t pool = {pool};
        if (pump != g_base + {pump_rva}u) {{ ++failures; std::puts("wrong menu pump target"); }}
        if (pool != g_base + {pool_rva}u) {{ ++failures; std::puts("wrong menu pool target"); }}
    }}
    return failures ? 1 : 0;
}}
'''
        with tempfile.TemporaryDirectory(prefix="ffx-menu-binding-") as directory:
            source = Path(directory) / "binding.cpp"
            binary = Path(directory) / "binding"
            source.write_text(code, encoding="utf-8")
            command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(DLL)]
            if steam:
                command.append("-DFFXHOOKS_TARGET_STEAM_20261001")
            subprocess.run(command + [str(source), "-o", str(binary)], check=True, capture_output=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_legacy_consumers_at_three_image_bases(self):
        self.check_profile(False)

    def test_steam_consumers_at_three_image_bases(self):
        self.check_profile(True)

    def check_sort_witnesses(self, steam):
        name = "FFX_STEAM_EXE_FIXTURE" if steam else "FFX_LEGACY_EXE_FIXTURE"
        fixture = os.environ.get(name)
        if not fixture:
            self.skipTest(f"{name} must name the private executable fixture")
        compiler = shutil.which("c++")
        if not compiler:
            self.skipTest("A C++17 compiler is required")
        image = Path(fixture).read_bytes()
        expected = ("0537b2a1047f3266e73495cd4e35f63f0777f4231d417699f979954686da686d" if steam else
                    "78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced")
        self.assertEqual(hashlib.sha256(image).hexdigest(), expected, "Exact reviewed PE required")
        pe = struct.unpack_from("<I", image, 0x3C)[0]
        count = struct.unpack_from("<H", image, pe + 6)[0]
        optional = struct.unpack_from("<H", image, pe + 20)[0]
        sections = pe + 24 + optional

        def native_bytes(rva, length):
            for i in range(count):
                va, size, raw = struct.unpack_from("<III", image, sections + i * 40 + 12)
                if va <= rva and rva + length <= va + size:
                    return image[raw + rva - va:raw + rva - va + length]
            self.fail(f"Witness RVA 0x{rva:X} is not backed by PE bytes")

        source = (DLL / "hooks/SeymourGearSortHook.cpp").read_text(encoding="utf-8")
        start = source.index("constexpr std::uint32_t SwapRva=")
        end = source.index("enum class Phase:", start)
        # Emit the same conditional declarations and proof tables that Start()
        # uses. No synthetic signature acceptance or copied expected byte array.
        code = '''#include <cstdio>
#include "hooks/RecoveryEvidence.generated.h"
namespace FfxHooks::SeymourGearSort {
''' + source[start:end] + '''
}
int main() {
    using namespace FfxHooks::SeymourGearSort;
    const auto emit = [](const FfxHooks::RecoveryEvidence::Proof& p) {
        std::printf("%x ", p.rva);
        for (size_t i=0;i<p.size;++i) std::printf("%02x", p.bytes[i]);
        std::puts("");
    };
    for (const auto& p : HelperProofs) emit(p);
    for (const auto& p : Proofs) emit(p);
}
'''
        with tempfile.TemporaryDirectory(prefix="ffx-sort-witness-") as directory:
            cpp, binary = Path(directory) / "witness.cpp", Path(directory) / "witness"
            cpp.write_text(code, encoding="utf-8")
            command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(DLL)]
            if steam:
                command.append("-DFFXHOOKS_TARGET_STEAM_20261001")
            subprocess.run(command + [str(cpp), "-o", str(binary)], check=True, capture_output=True)
            lines = subprocess.check_output([str(binary)], text=True).splitlines()
        self.assertEqual(len(lines), 5)
        for line in lines:
            rva, hex_bytes = line.split()
            expected_bytes = bytes.fromhex(hex_bytes)
            self.assertEqual(native_bytes(int(rva, 16), len(expected_bytes)), expected_bytes,
                             f"The actual sorting admission witness at RVA 0x{rva} must match the selected PE")

    def test_legacy_sort_admission_witnesses_match_native_pe(self):
        self.check_sort_witnesses(False)

    def test_steam_sort_admission_witnesses_match_native_pe(self):
        self.check_sort_witnesses(True)


if __name__ == "__main__":
    unittest.main()
