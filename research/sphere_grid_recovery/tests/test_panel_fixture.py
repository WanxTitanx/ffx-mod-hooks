"""Synthetic panel records use the independently observed native table ABI."""
import importlib.util
from pathlib import Path
import struct
import sys
import unittest
sys.path.append(str(Path(__file__).resolve().parents[1]))

class PanelFixtureTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(importlib.util.find_spec('panel_fixture'), 'Panel fixture missing')
        import panel_fixture
        self.p = panel_fixture

    def test_native_header_and_record_offsets(self):
        data = self.p.encode({35:(0x100,0,6)}, count=130)
        self.assertEqual(struct.unpack_from('<4HI',data,8),(0,129,24,3120,20))
        self.assertEqual(self.p.lookup(data,35),(0x100,0,6))

    def test_invalid_records_do_not_wrap(self):
        for rows in ({130:(0,0,0)},{35:(0x800,0,1)},{35:(1,65536,1)},
                     {35:(1,0,256)},{True:(1,0,1)}):
            with self.assertRaises(ValueError):self.p.encode(rows,count=130)

    def test_invalid_input_does_not_fallback(self):
        data = self.p.encode({35:(0x100,0,6)},count=130)
        for bad in (b'',data[:-1],data+b'\0'):
            with self.assertRaises(ValueError):self.p.lookup(bad,35)
        for index in (-1,130,True):
            with self.assertRaises(ValueError):self.p.lookup(data,index)

    def test_native_ability_bit_is_a_known_panel_target(self):
        data=self.p.encode({80:(0x400,0x3020,0)},count=130)
        self.assertEqual(self.p.lookup(data,80),(0x400,0x3020,0))

    def test_native_text_pointer_has_owned_terminator(self):
        data=self.p.encode({35:(0x100,0,6)},count=130)
        _,_,_,text_offset,base=struct.unpack_from('<4HI',data,8)
        self.assertLess(base+text_offset,len(data),'resource text pointer is outside its allocation')
        self.assertEqual(data[base+text_offset:],bytes([0]))

if __name__ == '__main__':unittest.main()
