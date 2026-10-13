import struct
import unittest
from tools.capture_cascade.author import clone_monster, formation_slots


def monster(source_id=334):
    data=bytearray(320)
    struct.pack_into('<9I',data,0,8,0x34,0x50,0x80,0x110,0x120,0x128,0x130,len(data))
    struct.pack_into('<III',data,0x94,800000,9999,99999)
    struct.pack_into('<HH',data,0xF2,source_id,0x3001)
    data[0xF8]=255
    return bytes(data)


def battle():
    data=bytearray(256)
    struct.pack_into('<9I',data,0,8,0x24,0x40,0x60,0x80,0xA0,0xB0,0xC0,len(data))
    struct.pack_into('<8H',data,0x6C,0x1001,0x1002,0x1003,0x1004,0x1005,0xFFFF,0xFFFF,0xFFFF)
    return bytes(data)


class AuthoringTests(unittest.TestCase):
    def test_vanilla_hp_and_identity_only(self):
        source=monster();after=clone_monster(source,334,900,85)
        self.assertEqual(struct.unpack_from('<I',after,0x94)[0],680000)
        self.assertEqual(struct.unpack_from('<H',after,0xF2)[0],900)
        self.assertEqual(struct.unpack_from('<H',after,0xF4)[0],0x3001)
        allowed=set(range(0x94,0x98))|{0xF2,0xF3}
        self.assertTrue(all(a==b or i in allowed for i,(a,b) in enumerate(zip(source,after))))
        self.assertEqual(source,monster())
        self.assertEqual(after[0xF8],255, 'patrol does not enter the native Arena counter')

    def test_refuses_wrong_origin_and_bad_identity(self):
        for source_id,destination,percent in [(335,900,85),(334,334,85),(334,1024,85),(334,900,0),(334,900,101)]:
            with self.subTest(source_id=source_id,destination=destination,percent=percent):
                with self.assertRaises(ValueError):clone_monster(monster(),source_id,destination,percent)

    def test_absent_optional_spoils_section_uses_next_real_boundary(self):
        source=bytearray(monster());struct.pack_into('<I',source,16,0)
        after=clone_monster(bytes(source),334,900,85)
        self.assertEqual(struct.unpack_from('<I',after,0x94)[0],680000)
        self.assertEqual(after[16:20],b'\0'*4)

    def test_refuses_unknown_layout_without_output(self):
        for offset,value in [(12,319),(32,319),(0xF8,0),(0xF9,1)]:
            bad=bytearray(monster())
            if offset in (12,32):struct.pack_into('<I',bad,offset,value)
            else:bad[offset]=value
            with self.subTest(offset=offset):
                with self.assertRaises(ValueError):clone_monster(bytes(bad),334,900,85)

    def test_preserves_five_actor_count_and_empty_slots(self):
        offset,slots=formation_slots(battle())
        self.assertEqual(offset,0x6C)
        self.assertEqual(slots,(0x1001,0x1002,0x1003,0x1004,0x1005,0xFFFF,0xFFFF,0xFFFF))

    def test_rejects_truncated_or_overlapping_formation(self):
        for source in [battle()[:120],battle()[:15]]:
            with self.assertRaises(ValueError):formation_slots(source)
        bad=bytearray(battle());struct.pack_into('<I',bad,12,0x7F)
        with self.assertRaises(ValueError):formation_slots(bytes(bad))


if __name__=='__main__':unittest.main()
