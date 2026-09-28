"""Synthetic ATEL camera regression; contains no game-derived payload."""
import math
import struct
import unittest
import build


def fixture():
    # One descriptor, shared float pool, code followed by the entry table.
    b = bytearray(0x80)
    struct.pack_into('<H', b, 0x36, 1)
    build.set32(b, 0x38, 0x40)
    struct.pack_into('<H', b, 0x48, 1)
    build.set32(b, 0x5c, 0x68)
    b[0x68:0x80] = struct.pack('<6f', 0., -10., 20., 30., 5., 120.)
    code = bytearray()
    for call, args in ((0x6020, (0, 1, 2)), (0x6002, (3, 1, 5)),
                       (0x6004, (3, 4, 5)), (0x1234, (3, 4, 5))):
        for arg in args:
            code.extend(struct.pack('<BH', 0xaf, arg))
        code.extend(struct.pack('<BH', 0xd8, call))
    build.set32(b, 0, len(code))
    build.set32(b, 0x30, len(b))
    build.set32(b, 0x10, len(b) + len(code))
    build.set32(b, 0x60, len(b) + len(code))
    b.extend(code)
    b.extend(bytes(4))
    return bytes(b)


def calls(b):
    pool = build.u32(b, build.workers(b)[0][0] + 0x1c)
    start = build.u32(b, 0x30)
    values, result = [], []
    for p in range(start, start + build.u32(b, 0), 3):
        op, arg = b[p], build.u16(b, p + 1)
        if op == 0xaf:
            values.append(struct.unpack_from('<f', b, pool + arg * 4)[0])
        else:
            result.append((arg, values))
            values = []
    return result


class TacticalCameraTests(unittest.TestCase):
    def test_cartesian_opening_is_above_reference(self):
        out, _ = build.tactical(fixture())
        entries = dict(calls(out))
        ref, eye = entries[0x6020], entries[0x6002]
        # FFX's native world positions use negative Y above the ground.
        self.assertLess(eye[1], ref[1] - 200)
        self.assertGreater(eye[1], ref[1] - 320)
        self.assertTrue(300 <= math.dist(eye, ref) <= 380)

    def test_polar_elevation_uses_same_up_direction(self):
        out, _ = build.tactical(fixture())
        self.assertTrue(-60 <= dict(calls(out))[0x6004][1] <= -45)

    def test_other_calls_and_original_pool_are_unchanged(self):
        before = fixture()
        out, _ = build.tactical(before)
        self.assertEqual(dict(calls(before))[0x1234], dict(calls(out))[0x1234])
        self.assertEqual(out[0x68:0x80], before[0x68:0x80])
        self.assertEqual(build.u32(out, 0), build.u32(before, 0))
        self.assertEqual(build.u32(out, 0x60), build.u32(out, 0x30) + build.u32(out, 0))


if __name__ == '__main__':
    unittest.main()
