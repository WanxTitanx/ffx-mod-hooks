#!/usr/bin/env python3
"""Source wiring gate, supplementary to actual-code/core and native tests."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[2]/'src/runtime/FfxHooksDll'

def source(name):
    path=ROOT/name
    return path.read_text() if path.is_file() else ''

class Wiring(unittest.TestCase):
    def test_shared_load_without_enabling_learning(self):
        grid=source('hooks/GridLearnedRuntime.h')
        self.assertIn('inline bool StartLoadEvents(',grid)
        self.assertIn('StartNativeSaveLoadEvents(',source('hooks/GridTeachHook.cpp'))
        self.assertIn('loadEventsReady',grid)
    def test_session_uses_real_copy_completion(self):
        text=source('hooks/SeymourSessionRuntime.cpp')
        for token in ('SeymourActiveLoadCore.h','loadStarting','loadCompleted','IsSaveIoReady','SubscribeAdditional','StartNativeSaveLoadEvents'):
            self.assertIn(token,text)
        self.assertNotIn('MH_CreateHook',text)
        self.assertNotIn('WriteFile',text)
    def test_early_and_pump_lifecycle(self):
        text=source('dllmain.cpp')
        self.assertIn('SeymourSession::PrimeSaveIo(',text)
        self.assertIn('SeymourPersistentRoster::PumpTick()',text)
        self.assertIn('SeymourSession::RequestStop()',text)
        self.assertIn('SeymourPersistentRoster::RequestStop()',text)
    def test_sort_uses_confirmed_session(self):
        text=source('hooks/SeymourGearSortHook.cpp')
        self.assertIn('SeymourSession::Capture(',text)
        self.assertIn('SeymourSession::Current(',text)
        self.assertNotIn('g_ownerThread.compare_exchange',text)

if __name__=='__main__':unittest.main()
