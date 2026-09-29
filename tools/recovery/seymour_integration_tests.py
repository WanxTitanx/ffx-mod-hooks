#!/usr/bin/env python3
"""Source integration contracts, not proof of native hooks or in-game behavior."""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[2]/'src/runtime/FfxHooksDll'
def text(path):
    p=ROOT/path
    return p.read_text() if p.is_file() else ''

class Integration(unittest.TestCase):
    maxDiff=800
    def test_draw_does_not_erase_persistence_failure(self):
        source=text('hooks/NativeSettingsUi.inl')
        draw=source[source.index('static void F8NativeSettingsDraw'):]
        self.assertNotIn('SeymourCompatibility::Detail(g_nativeSettingsNotice',draw)
    def test_real_adapter_is_connected_to_builds(self):
        self.assertIn('ServiceQuery(',text('hooks/SeymourCompatibilityHook.cpp'))
        for path in ('build_hooks.ps1','FfxHooksDll.vcxproj'):
            self.assertIn('SeymourCompatibilityHook.cpp',text(path))
    def test_controls_are_default_off_and_have_native_page(self):
        ini=text('ffx-hooks.ini')
        self.assertIn('[seymour]',ini)
        self.assertIn('command_safety = 0',ini)
        self.assertIn('gear_visibility = 0',ini)
        ui=text('hooks/NativeSettingsUi.inl')
        for call in ('SeymourCompatibility::MenuLabel','SeymourCompatibility::MenuAction','SeymourCompatibility::Detail'):
            self.assertIn(call,ui)
        self.assertIn('NativeSettingsPage::Seymour',text('dllmain.cpp'))
    def test_start_boundary_and_nonblocking_stop_are_wired(self):
        source=text('hooks/SeymourBattleHook.cpp')
        for name in ('SeymourCompatibility::Start','SeymourCompatibility::RestoreAtBattleBoundary',
                     'SeymourCompatibility::RequestStop','SeymourCompatibility::Remove','CaptureSeymourCompatibilityScope'):
            self.assertIn(name,source)
    def test_cleanup_is_separate_from_new_work(self):
        source=text('hooks/SeymourCompatibilityHook.cpp')
        self.assertIn('g_lease.Restore(',source)
        self.assertIn('g_lease.Apply(',source)
        self.assertIn('Owner::SeymourCompatibility',source)
        self.assertNotIn('MH_ALL_HOOKS',source)
        self.assertNotIn('MH_Uninitialize',source)
    def test_license_is_present(self):
        license=text('third_party/licenses/playable-seymour-mod.txt')
        self.assertIn('Copyright (c) 2026 cxldalyy',license)
        self.assertIn('MIT License',license)

if __name__=='__main__':unittest.main()
