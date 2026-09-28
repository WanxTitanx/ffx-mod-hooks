"""Exercise the C++ Aeon contracts through the production C ABI."""
from pathlib import Path
import ctypes as C
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'host'))
from bridge import Core, Economy, Request, OPS, ability


class AeonRules(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.core = Core()

    def fixture(self, owner=8, kind=0):
        records = bytearray(4400)
        records[2:7] = bytes([1, 3 if kind else 7, owner, kind, owner])
        records[11] = 4
        words = (0x8000, 0x8017, 0x8018, 255) if kind else (0x807B, 0x8019, 0x8062, 255)
        struct.pack_into('<4H', records, 14, *words)
        state = self.core.import_records(bytes(records), [99] * 112, 123)
        raw = bytearray(26880)
        raw[64+0xC6C] = 127
        ply = 64+0x55CC+owner*0x94
        raw[ply+0x2C] = 0x11
        raw[ply+0x2D+kind] = 0
        return state, Economy(10000000, customize_unlocked=True, aeons=self.core.aeon_progress(bytes(raw))), raw

    def request(self, state, name, value=0):
        r = Request(); r.op = OPS[name]; r.slot = 0; r.pieceId = state.pieces[0].id
        r.revision = state.revision; r.value = value
        return r

    def test_all_aeon_crests_and_authentic_equipped_slots(self):
        for owner, mask in enumerate([2, 16, 8, 32, 0, 0, 4, 0, 0, 0], 8):
            for kind in (0, 1):
                s, e, raw = self.fixture(owner, kind)
                r = self.request(s, 'unlock_fifth')
                for crest in range(128):
                    e.aeons.crests = crest
                    code, plan = self.core.quote(s, r, e)
                    self.assertEqual(code == 0, not mask or bool(crest & mask), (owner, kind, crest, code))
                    if code: self.assertEqual(bytes(s), bytes(plan.after))
                raw[64+0xC39] = raw[64+0xC6D] = 127; raw[64+0xC6C] = 0
                self.assertEqual(self.core.aeon_progress(bytes(raw)).crests, 0)
                e.aeons.obtained = 0; e.policy.devIgnoreProgression = 1
                self.assertNotEqual(self.core.quote(s, r, e)[0], 0)
                s, e, _ = self.fixture(owner, kind)
                e.aeons.gear[2*(owner-8)+kind] = 1
                self.assertNotEqual(self.core.quote(s, r, e)[0], 0)

    def test_four_per_type_and_cumulative_master_substitution(self):
        for owner in range(8, 18):
            s, e, _ = self.fixture(owner); r = self.request(s, 'unlock_fifth')
            self.assertEqual(list(self.core.preview(s, r, e).costs)[75:81], [4, 4, 4, 4, 4, 0])
            s.items[75:80] = [0, 1, 2, 3, 4]; s.items[80] = 10
            p = self.core.preview(s, r, e)
            self.assertEqual(list(p.costs)[75:81], [0, 1, 2, 3, 4, 10])
            self.assertEqual(list(p.after.items)[75:81], [0]*6)
            s.items[80] = 9; code, p = self.core.quote(s, r, e)
            self.assertNotEqual(code, 0); self.assertEqual(bytes(p.after), bytes(s))
            s.items[75:80] = [0]*5; s.items[80] = 20
            self.assertEqual(self.core.preview(s, r, e).costs[80], 20)

    def test_immunity_protection_and_double_gil(self):
        s, e, _ = self.fixture()
        code, p = self.core.quote(s, self.request(s, 'clear', 0), e)
        self.assertNotEqual(code, 0); self.assertEqual(bytes(s), bytes(p.after))
        self.assertFalse(self.core.can_customize(s.pieces[0], 0, 0x8001))
        p = self.core.preview(s, self.request(s, 'clear', 2), e)
        self.assertEqual(ability(p.after.pieces[0], 0), 0x807B)
        for mode, price in ((1, 6000), (2, 2000)):
            e.policy.mode = mode; p = self.core.preview(s, self.request(s, 'refine'), e)
            self.assertEqual(p.after.pieces[0].ranks[0], 0)
            self.assertEqual(p.after.pieces[0].abilities[0], s.pieces[0].abilities[0])
            self.assertEqual((p.gilCost, p.gilDebit), (price, price))
        e.policy.mode = 1; e.policy.devFreeGil = 1
        p = self.core.preview(s, self.request(s, 'refine'), e)
        self.assertEqual((p.gilCost, p.gilDebit), (6000, 0))
        e.policy.devFreeGil = 0
        r = self.request(s, 'evolve', 0x8063); r.to[0] = 2
        self.assertEqual(self.core.preview(s, r, e).gilCost, 50000)
        self.assertNotEqual(self.core.quote(s, self.request(s, 'retire'), e)[0], 0)
        r = self.request(s, 'reforge'); r.gearTemplate[:] = s.pieces[0].native[:]; r.gearTemplate[4] = 0
        self.assertNotEqual(self.core.quote(s, r, e)[0], 0)

    def test_old_abi_does_not_write_a_larger_plan(self):
        s, e, _ = self.fixture(); r = self.request(s, 'unlock_fifth')
        canary = (C.c_ubyte * 32)(*([0xA5]*32))
        result = self.core.dll.ws_plan_economy_v3(C.byref(s), C.byref(r), C.byref(e), C.byref(canary))
        self.assertNotEqual(result, 0); self.assertEqual(bytes(canary), bytes([0xA5]*32))
        result = self.core.dll.ws_plan_economy_v4(C.byref(s), C.byref(r), C.byref(e), C.byref(canary))
        self.assertNotEqual(result, 0); self.assertEqual(bytes(canary), bytes([0xA5]*32))


if __name__ == '__main__':
    unittest.main()
