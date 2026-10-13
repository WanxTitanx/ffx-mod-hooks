"""Bounded asset-transformation checks; no game or shared Editor files are written."""
import struct
import unittest
from repair_mana_storm import repair, row_offset

class ManaStormRepair(unittest.TestCase):
    def fixture(self):
        data=bytearray(20+266*92+64)
        struct.pack_into('<H',data,0,1)
        struct.pack_into('<HHHHI',data,8,0,265,92,266*92,20)
        start=20+265*92
        data[start+21]=3
        struct.pack_into('<hh',data,start+16,862,863)
        data[-64:]=bytes(range(64))
        return data
    def test_preserves_gameplay_other_rows_and_text_pool(self):
        before=self.fixture();result=repair(before);start=row_offset(before,265)
        self.assertEqual((86,87),struct.unpack_from('<hh',result,start+16))
        self.assertEqual(before[:start+16],result[:start+16])
        self.assertEqual(before[start+20:],result[start+20:])
        self.assertEqual((862,863),struct.unpack_from('<hh',before,start+16))
        self.assertEqual(result,repair(result))
    def test_rejects_unreviewed_animation_or_caster(self):
        for offset in (16,21):
            data=self.fixture();data[row_offset(data,265)+offset]^=1
            with self.assertRaises(ValueError):repair(data)
    def test_rejects_short_or_missing_native_command(self):
        for size in (0,19,20,20+265*92):
            with self.assertRaises(ValueError):repair(self.fixture()[:size])
        data=self.fixture();struct.pack_into('<H',data,10,264)
        with self.assertRaises(ValueError):repair(data)

if __name__=='__main__':unittest.main()
