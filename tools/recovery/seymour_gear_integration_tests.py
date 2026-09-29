#!/usr/bin/env python3
"""Structural wiring contracts. Behavioral adapter/native tests are separate."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[2]/'src/runtime/FfxHooksDll'
def text(name):
    p=ROOT/name
    return p.read_text(encoding='utf-8-sig') if p.exists() else ''
class Integration(unittest.TestCase):
    maxDiff=1024
    def test_native_adapter_is_built(self):
        self.assertIn('SeymourGearPresentationHook.cpp',text('build_hooks.ps1'))
        self.assertIn('SeymourGearPresentationHook.cpp',text('FfxHooksDll.vcxproj'))
        self.assertIn('Service(',text('hooks/SeymourGearPresentationHook.cpp'))
    def test_explicit_default_off_and_native_page(self):
        self.assertIn('gear_presentation = 0',text('ffx-hooks.ini'))
        ui=text('hooks/NativeSettingsUi.inl')
        self.assertIn('SeymourGearPresentation::MenuLabel',ui)
        self.assertIn('SeymourGearPresentation::MenuAction',ui)
        self.assertIn('SeymourGearPresentation::Detail',ui)
    def test_lifecycle_owned_by_existing_seymour_adapter(self):
        source=text('hooks/SeymourBattleHook.cpp')
        for action in ('Start','PresentTick','RequestStop','Remove'):
            self.assertIn('SeymourGearPresentation::'+action,source)
    def test_independent_coordinator_owner(self):
        self.assertIn('SeymourGearPresentation',text('hooks/MinHookBatchCoordinator.h'))
        self.assertIn('Owner::SeymourGearPresentation',text('hooks/MinHookBatchCoordinator.cpp'))
if __name__=='__main__':unittest.main()
