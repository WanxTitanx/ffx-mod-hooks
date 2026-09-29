"""PE mapper negative tests use a synthetic image, never a private executable."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import sys
import unittest
sys.path.append(str(Path(__file__).resolve().parents[1]))


def image():
    data=bytearray(0x600);data[:2]=b'MZ';struct.pack_into('<I',data,0x3c,0x80)
    data[0x80:0x84]=b'PE\0\0';struct.pack_into('<HHIIIHH',data,0x84,0x14c,2,0,0,0,0xe0,0x102)
    optional=0x98
    struct.pack_into('<H',data,optional,0x10b)
    struct.pack_into('<I',data,optional+28,0x400000)
    struct.pack_into('<I',data,optional+56,0x4000)
    struct.pack_into('<I',data,optional+60,0x200)
    sections=optional+0xe0
    for i,(name,va,raw,flags) in enumerate([(b'.text',0x1000,0x200,0x60000020),
                                          (b'.data',0x2000,0x400,0xc0000040)]):
        offset=sections+i*40;data[offset:offset+len(name)]=name
        struct.pack_into('<IIII',data,offset+8,0x300,va,0x200,raw)
        struct.pack_into('<I',data,offset+36,flags)
    data[0x200:0x400]=bytes([0x90])*0x200;data[0x400:0x600]=bytes([0xA5])*0x200
    return data


class PeTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(importlib.util.find_spec('pe_image'),'Bounded private PE mapper is not implemented')
        import pe_image
        self.p=pe_image

    def map(self,data):
        return self.p.map_verified(bytes(data),expected_sha256=hashlib.sha256(data).hexdigest(),
                                   expected_image_size=0x4000)

    def test_sections_are_mapped_and_virtual_tails_zeroed(self):
        mapped=self.map(image())
        self.assertEqual(mapped.base,0x400000)
        self.assertEqual(mapped.data[0x1000:0x1200],b'\x90'*0x200)
        self.assertEqual(mapped.data[0x1200:0x1300],b'\0'*0x100)
        self.assertEqual(mapped.data[0x2000:0x2200],b'\xA5'*0x200)
        self.assertFalse(mapped.sections[0].writable)
        self.assertTrue(mapped.sections[0].executable)
        self.assertTrue(mapped.sections[1].writable)

    def test_hash_mismatch_refuses_before_mapping(self):
        with self.assertRaises(self.p.ImageError):self.p.map_verified(bytes(image()))

    def test_truncated_headers_refused(self):
        for length in (0,1,63,64,0x80,0x84,0x100,0x190):
            with self.subTest(length=length):
                with self.assertRaises(self.p.ImageError):self.map(image()[:length])

    def test_wrong_architecture_magic_and_base_refused(self):
        for offset,format,value in [(0x84,'H',0x8664),(0x98,'H',0x20b),(0x98+28,'I',0x500000)]:
            data=image();struct.pack_into('<'+format,data,offset,value)
            with self.assertRaises(self.p.ImageError):self.map(data)

    def test_section_raw_past_file_refused(self):
        data=image();struct.pack_into('<I',data,0x178+20,0x100000)
        with self.assertRaises(self.p.ImageError):self.map(data)

    def test_section_virtual_past_image_refused(self):
        data=image();struct.pack_into('<I',data,0x178+12,0xffffff00)
        with self.assertRaises(self.p.ImageError):self.map(data)

    def test_overlapping_virtual_sections_refused(self):
        data=image();struct.pack_into('<I',data,0x178+40+12,0x1100)
        with self.assertRaises(self.p.ImageError):self.map(data)

    def test_section_overlapping_headers_refused(self):
        data=image();struct.pack_into('<I',data,0x178+12,0x80)
        with self.assertRaises(self.p.ImageError):self.map(data)

    def test_excessive_section_count_refused(self):
        data=image();struct.pack_into('<H',data,0x86,65535)
        with self.assertRaises(self.p.ImageError):self.map(data)

    def test_bad_dos_or_pe_signature_refused(self):
        for offset in (0,0x80):
            data=image();data[offset]=0
            with self.assertRaises(self.p.ImageError):self.map(data)

    def test_large_image_declaration_refused(self):
        data=image();struct.pack_into('<I',data,0x98+56,0x80000000)
        with self.assertRaises(self.p.ImageError):self.map(data)

    def test_repository_executable_identity_is_pinned(self):
        self.assertEqual(self.p.EXPECTED_SHA256,'78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced')
        self.assertEqual(self.p.EXPECTED_IMAGE_SIZE,0x237d000)

if __name__=='__main__':unittest.main()
