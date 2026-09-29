#!/usr/bin/env python3
"""Structural integration gate; native behavior has separate executable tests."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[2]/'src/runtime/FfxHooksDll'
def read(path):
    p=ROOT/path
    return p.read_text(encoding='utf-8-sig') if p.exists() else ''
class Integration(unittest.TestCase):
    maxDiff=1000
    def test_adapter_and_existing_swap_route(self):
        source=read('hooks/SeymourGearSortHook.cpp')
        self.assertIn('Apply(',source)
        self.assertIn('SwapRva=0x3aba10',source)
        self.assertNotIn('g_batch.Add(g_base+SwapRva',source)
        for path in ('build_hooks.ps1','FfxHooksDll.vcxproj'):
            self.assertIn('SeymourGearSortHook.cpp',read(path))
    def test_default_off_and_menu(self):
        self.assertIn('gear_sorting = 0',read('ffx-hooks.ini'))
        for name in ('MenuLabel','MenuAction','Detail'):
            self.assertIn('SeymourGearSort::'+name,read('hooks/NativeSettingsUi.inl'))
    def test_shared_save_producer_and_lifecycle(self):
        self.assertIn('SeymourSession::PrimeSaveIo',read('dllmain.cpp'))
        for name in ('Start','PresentTick','RequestStop','Remove'):
            self.assertIn('SeymourGearSort::'+name,read('hooks/SeymourBattleHook.cpp'))
        source=read('hooks/SeymourGearSortHook.cpp')
        self.assertIn('RonsoPool::IsSaveIoReady()',source)
        self.assertIn('SeymourSession::Capture()',source)
        self.assertIn('SeymourSession::Current(',source)
        self.assertNotIn('SubscribeAdditional',source)
        self.assertNotIn('g_ownerThread.compare_exchange',source)
if __name__=='__main__':unittest.main()
