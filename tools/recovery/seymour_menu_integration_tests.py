#!/usr/bin/env python3
"""Supplementary wiring checks; native semantics and adapters have separate tests."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2] / "src/runtime/FfxHooksDll"


def source(name):
    return (ROOT / name).read_text(encoding="utf-8-sig")


class Integration(unittest.TestCase):
    def test_complete_build_inputs(self):
        for path in ("build_hooks.ps1", "FfxHooksDll.vcxproj"):
            self.assertEqual(source(path).count("SeymourMenuListHook.cpp"), 1, path)
        for header in ("Core", "Service", "Hook"):
            self.assertIn("SeymourMenuList" + header + ".h", source("FfxHooksDll.vcxproj"))

    def test_independent_default_off_control(self):
        self.assertIn("menu_list = 0", source("ffx-hooks.ini"))
        self.assertIn('"seymour.menu_list"', source("hooks/SeymourSessionRuntime.cpp"))
        menu = source("hooks/NativeSettingsUi.inl")
        for method in ("MenuLabel", "MenuAction", "Detail"):
            self.assertIn("SeymourMenuList::" + method, menu)

    def test_owner_lifecycle(self):
        owner = source("hooks/SeymourBattleHook.cpp")
        for method in ("Start", "PresentTick", "RequestStop", "Remove"):
            self.assertIn("SeymourMenuList::" + method, owner)
        dll = source("dllmain.cpp")
        self.assertIn("SeymourMenuList::PumpTick()", dll)
        self.assertIn("SeymourMenuList::RequestStop()", dll)
        teardown = dll[dll.index("static void RemoveHooks()") :]
        self.assertLess(teardown.index("SeymourMenuList::Remove()"),
                        teardown.index("SeymourPersistentRoster::Remove()"))
        self.assertLess(teardown.index("SeymourMenuList::Remove()"),
                        teardown.index("StopNativeMenu()"))
        self.assertLess(teardown.index("SeymourMenuList::Remove()"),
                        teardown.index("SeymourSession::Remove()"))


if __name__ == "__main__":
    unittest.main()
