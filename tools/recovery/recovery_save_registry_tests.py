#!/usr/bin/env python3
"""Compile the actual GridTeach observer initializer with the merged save ABI."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / "src/runtime/FfxHooksDll/hooks"


class RecoveryRegistryTests(unittest.TestCase):
    def test_grid_read_start_is_not_a_projection_callback(self):
        source = (HOOKS / "GridLearnedRuntime.h").read_text()
        start = source.index("inline const NativeSaveEvents::Observer observer")
        declaration = source[start:source.index("struct LoadAttempt", start)]
        unit = r'''
#include "NativeSaveEvents.h"
#include <cstdio>
namespace FfxHooks::GridLearned::Runtime {
unsigned starts=0;
void ReadEvent(const wchar_t*,const unsigned char*,const unsigned char*,std::size_t) noexcept {}
void WriteEvent(const wchar_t*,const unsigned char*,std::size_t) noexcept {}
void ResetEvent() noexcept {}
void ReadStartingEvent(const unsigned char*) noexcept {++starts;}
''' + declaration + r'''
}
int main(){
 namespace E=FfxHooks::NativeSaveEvents;
 namespace G=FfxHooks::GridLearned::Runtime;
 if(!E::ValidObserver(&G::observer)||G::observer.project||G::observer.prepare||G::observer.finish)return 1;
 if(G::observer.readStarting!=G::ReadStartingEvent||!E::SubscribeAdditional(&G::observer))return 2;
 unsigned char data=1;E::ReadStarting(&data);
 if(G::starts!=1)return 3;
 E::UnsubscribeAdditional(&G::observer);E::ReadStarting(&data);
 if(G::starts!=1||E::Requested())return 4;
 std::puts("RecoverySaveRegistry: actual initializer routing passed");return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="recovery-registry-") as directory:
            path = Path(directory)
            (path / "test.cpp").write_text(unit)
            build = subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-O2",
                "-Wall", "-Wextra", "-Werror", "-pthread", "-I" + str(HOOKS),
                str(path / "test.cpp"), "-o", str(path / "test")], capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            result = subprocess.run([str(path / "test")], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout, end="")


if __name__ == "__main__":
    unittest.main()
