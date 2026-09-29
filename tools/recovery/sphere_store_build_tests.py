#!/usr/bin/env python3
"""Build-list consistency only. Actual Win32 storage has independent RT1 tests."""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[2]/'src/runtime/FfxHooksDll'

class StoreBuildTests(unittest.TestCase):
    def test_store_is_compiled_once_in_both_build_routes(self):
        script=(ROOT/'build_hooks.ps1').read_text(encoding='utf-8-sig')
        self.assertEqual(script.count('SphereGridProgress8Store.cpp'),1)
        project=ET.parse(ROOT/'FfxHooksDll.vcxproj')
        sources=[entry.attrib['Include'] for entry in project.iter() if entry.tag.endswith('ClCompile') and 'Include' in entry.attrib]
        self.assertEqual(sources.count('hooks\\SphereGridProgress8Store.cpp'),1)

    def test_all_store_and_proof_headers_are_listed(self):
        project=(ROOT/'FfxHooksDll.vcxproj').read_text(encoding='utf-8-sig')
        for name in ('SphereGridProgressCore.h','SphereGridProgress8Core.h','SphereGridProgress8Store.h','FieldProbeEvidence.h'):
            self.assertEqual(len(re.findall(r'ClInclude Include="hooks\\'+re.escape(name)+r'"',project)),1,name)

if __name__=='__main__':unittest.main()
