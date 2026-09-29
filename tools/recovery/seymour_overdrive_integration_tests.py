#!/usr/bin/env python3
"""Wiring checks only; native behavior and adapter execution have separate gates."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[2]/'src/runtime/FfxHooksDll'
def read(path):
    p=ROOT/path
    return p.read_text(encoding='utf-8-sig') if p.exists() else ''
class Integration(unittest.TestCase):
    maxDiff=1000
    def test_actual_native_bodies_are_guarded(self):
        text=read('hooks/SeymourOverdriveHook.cpp')
        for marker in ('Relocate(','CounterProxy','GaugeProxy','CaptureSeymourCompatibilityScope(',
                       'scope.actorTable','FrameCurrent(','g_batch.Publish('):
            self.assertIn(marker,text)
    def test_battle_owner_wires_start_stop(self):
        text=read('hooks/SeymourBattleHook.cpp')
        for marker in ('SeymourOverdrive::Start(','SeymourOverdrive::PresentTick(',
                       'SeymourOverdrive::RequestStop(','SeymourOverdrive::Remove('):
            self.assertIn(marker,text)
    def test_build_and_default_configuration(self):
        self.assertIn('overdrive_events = 0',read('ffx-hooks.ini'))
        self.assertIn('SeymourOverdriveHook.cpp',read('build_hooks.ps1'))
        self.assertIn('SeymourOverdriveHook.cpp',read('FfxHooksDll.vcxproj'))
    def test_native_settings_has_control_and_diagnostic(self):
        text=read('hooks/NativeSettingsUi.inl')
        for marker in ('SeymourOverdrive::MenuLabel(', 'SeymourOverdrive::MenuAction(', 'SeymourOverdrive::Detail('):
            self.assertIn(marker,text)
    def test_no_save_or_balance_replacement(self):
        text=read('hooks/SeymourOverdriveHook.cpp')
        for forbidden in ('WriteFile(', 'CreateFile(', 'SetSavePlyJoin', 'MsNewGame', 'level_up_exp', 'FFXGLR'):
            self.assertNotIn(forbidden,text)
        self.assertIn('N::SealCode(',text)
        self.assertIn('No learned state is rolled back',text)
if __name__=='__main__':unittest.main()
