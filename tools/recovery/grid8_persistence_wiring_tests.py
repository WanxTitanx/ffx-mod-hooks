#!/usr/bin/env python3
"""Routing checks; behavioral and filesystem acceptance are separate."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[2]/'src/runtime/FfxHooksDll'
class WiringTests(unittest.TestCase):
    def test_build(self):
        self.assertEqual((ROOT/'build_hooks.ps1').read_text(encoding='utf-8-sig').count('SphereGridProgress8Runtime.cpp'),1)
        project=(ROOT/'FfxHooksDll.vcxproj').read_text(encoding='utf-8-sig')
        for name in ('SphereGridProgress8Runtime.cpp','SphereGridProgress8Runtime.h','SphereGridProgress8CommitCore.h','SphereGridProgress8SaveService.h'):
            self.assertEqual(project.count(name),1,name)
    def test_lifecycle(self):
        source=(ROOT/'dllmain.cpp').read_text()
        worker=source[source.index('static DWORD WINAPI HooksWorkerThread('):]
        prime='FfxHooks::SphereGridProgress8Runtime::PrimeSaveIo('
        self.assertIn(prime,worker)
        self.assertLess(worker.index('FfxHooks::SeymourSession::PrimeSaveIo('),worker.index(prime))
        self.assertLess(worker.index(prime),worker.index('StartNovaPoolEarlyIfRequested();'))
        remove=source[source.index('static void RemoveHooks() {'):source.index('static void StartNovaPoolEarlyIfRequested() {')]
        self.assertLess(remove.index('SphereGridProgress8Runtime::Remove()'),remove.index('SeymourSession::Remove()'))
        detach=source[source.index('case DLL_PROCESS_DETACH:'):]
        self.assertIn('SphereGridProgress8Runtime::RequestStop();',detach)
        self.assertNotIn('SphereGridProgress8Runtime::Remove()',detach)
    def test_control(self):
        self.assertIn('grid8_persistence = 0',(ROOT/'ffx-hooks.ini').read_text())
        self.assertIn('seymour.grid8_persistence',(ROOT/'hooks/SeymourSessionRuntime.cpp').read_text())
        ui=(ROOT/'hooks/NativeSettingsUi.inl').read_text()
        for token in ('SeymourCompatibility::MenuCount()+13','SphereGridProgress8Runtime::MenuAction()','SphereGridProgress8Runtime::MenuLabel(out,capacity)'):
            self.assertIn(token,ui)
if __name__=='__main__':unittest.main(verbosity=2)
