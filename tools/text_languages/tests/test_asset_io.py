import hashlib
import io
from pathlib import Path
import struct
import sys
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from asset_io import VbfArchive, AssetError, phyre_layout, ftc_layout


def archive_bytes(payload, name='ffx_ps2/demo.bin', compressed=False):
    blocks = [payload[i:i+65536] for i in range(0, len(payload), 65536)]
    stored = [zlib.compress(b) if compressed and len(zlib.compress(b)) < len(b) else b for b in blocks]
    names = name.encode() + b'\0'
    header_size = 16 + 16 + 32 + 4 + len(names) + 2 * len(blocks)
    header = bytearray(b'SRYK' + struct.pack('<IQ', header_size, 1))
    header += hashlib.md5(name.lower().encode()).digest()
    header += struct.pack('<IIQQQ', 0, 0, len(payload), header_size, 0)
    header += struct.pack('<I', len(names) + 4) + names
    header += b''.join(struct.pack('<H', len(b) % 65536) for b in stored)
    return bytes(header) + b''.join(stored) + hashlib.md5(header).digest()


class ArchiveTests(unittest.TestCase):
    def test_raw_short_final_and_compressed_blocks(self):
        for payload, compressed in [(b'ab', False), (bytes(range(256))*256+b'end', False), (b'abc'*50000, True), (b'', False)]:
            with self.subTest(size=len(payload), compressed=compressed):
                with VbfArchive(io.BytesIO(archive_bytes(payload, compressed=compressed))) as archive:
                    self.assertEqual(archive.read('FFX_PS2/DEMO.BIN'), payload)
                    self.assertEqual(archive.names, ('ffx_ps2/demo.bin',))

    def test_absent_resource(self):
        with VbfArchive(io.BytesIO(archive_bytes(b'a'))) as archive:
            with self.assertRaises(AssetError):
                archive.read('missing')

    def test_header_integrity(self):
        data = bytearray(archive_bytes(b'hello')); data[30] ^= 1
        with self.assertRaises(AssetError):
            VbfArchive(io.BytesIO(data))

    def test_truncation(self):
        data = archive_bytes(b'hello')
        for cut in (0, 3, 15, 40, len(data)-1):
            with self.subTest(cut=cut), self.assertRaises(AssetError):
                VbfArchive(io.BytesIO(data[:cut]))

    def test_inflation_limit(self):
        with VbfArchive(io.BytesIO(archive_bytes(b'hello'))) as archive:
            with self.assertRaises(AssetError):
                archive.read('ffx_ps2/demo.bin', limit=4)

    def test_forged_offset(self):
        data = bytearray(archive_bytes(b'hello'))
        struct.pack_into('<Q', data, 32+16, 2**60)
        size = struct.unpack_from('<I', data, 4)[0]
        data[-16:] = hashlib.md5(data[:size]).digest()
        with VbfArchive(io.BytesIO(data)) as archive:
            with self.assertRaises(AssetError):
                archive.read('ffx_ps2/demo.bin')

    def test_decompression_corruption(self):
        data = bytearray(archive_bytes(b'a'*70000, compressed=True))
        start = struct.unpack_from('<I', data, 4)[0]
        data[start+3] ^= 255
        with VbfArchive(io.BytesIO(data)) as archive:
            with self.assertRaises(AssetError):
                archive.read('ffx_ps2/demo.bin')


class FormatTests(unittest.TestCase):
    def test_ftc_uses_existing_counts(self):
        data = bytearray(304)
        data[:4] = b'FTCX'
        struct.pack_into('<H', data, 4, 200); struct.pack_into('<H', data, 8, 4)
        struct.pack_into('<IHH', data, 16, 230, 14, 18)
        struct.pack_into('<IIHH', data, 32, 64, 0, 128, 234)
        struct.pack_into('<II', data, 48, 64, 234)
        self.assertEqual(ftc_layout(data).count, 230)
        struct.pack_into('<I', data, 16, 10000)
        with self.assertRaises(AssetError):
            ftc_layout(data)

    def test_phyre_requires_unique_supported_instance(self):
        data = bytearray(256+32*32*4)
        data[:5] = b'RYHPT'
        pidx = 150
        struct.pack_into('<II', data, pidx-88, 32, 32)
        data[pidx:pidx+17] = b'PTexture2D\0ARGB8\0'
        layout = phyre_layout(data)
        self.assertEqual((layout.width,layout.height,layout.format), (32,32,'ARGB8'))
        with self.assertRaises(AssetError):
            phyre_layout(data[:200])


if __name__ == '__main__':
    unittest.main()
