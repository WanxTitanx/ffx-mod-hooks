"""Memory instrumentation and render protocol tests, not a model of all FFX."""
from __future__ import annotations
import random
import sys
import unittest
from pathlib import Path
sys.path.append(str(Path(__file__).resolve().parents[1]))
try:
    import memory_contract as M
except ModuleNotFoundError:
    M=None


class MemoryTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(M,'The generation-aware allocation ledger is missing')
        self.ledger=M.Ledger(byte_budget=1024*1024,record_budget=128)
        self.h=self.ledger.register(0x10001000,64,owner='nodes.colors',generation=1)

    def test_exact_last_byte_is_accepted(self):
        self.ledger.access(self.h,63,1,write=True)
        self.assertEqual(self.ledger.stats(self.h).maximum_end,64)

    def test_one_byte_overrun_is_detected_before_recording_a_store(self):
        with self.assertRaisesRegex(M.MemoryViolation,'overrun'):
            self.ledger.access(self.h,63,2,write=True)
        self.assertEqual(self.ledger.stats(self.h).writes,0)

    def test_underrun_is_detected(self):
        with self.assertRaisesRegex(M.MemoryViolation,'underrun'):
            self.ledger.access(self.h,-1,4,write=True)

    def test_address_wrap_is_rejected(self):
        with self.assertRaises(M.MemoryViolation):
            self.ledger.register(0xfffffff0,32,owner='bad',generation=1)

    def test_zero_and_negative_allocations_are_rejected(self):
        for size in (0,-1,True,1.5):
            with self.subTest(size=size),self.assertRaises(M.MemoryViolation):
                self.ledger.register(0x10002000,size,owner='bad',generation=1)

    def test_overlap_is_rejected(self):
        with self.assertRaisesRegex(M.MemoryViolation,'overlap'):
            self.ledger.register(0x10001020,64,owner='other',generation=1)

    def test_adjacent_buffers_do_not_extend_each_other(self):
        second=self.ledger.register(0x10001040,64,owner='nodes.uv',generation=1)
        with self.assertRaisesRegex(M.MemoryViolation,'overrun'):
            self.ledger.access(self.h,64,4,write=True)
        self.ledger.access(second,0,4,write=True)

    def test_read_after_free_is_rejected(self):
        self.ledger.release(self.h,owner='nodes.colors',generation=1)
        with self.assertRaisesRegex(M.MemoryViolation,'released'):
            self.ledger.access(self.h,0,4,write=False)

    def test_write_after_free_is_rejected(self):
        self.ledger.release(self.h,owner='nodes.colors',generation=1)
        with self.assertRaisesRegex(M.MemoryViolation,'released'):
            self.ledger.access(self.h,0,4,write=True)

    def test_double_free_is_rejected(self):
        self.ledger.release(self.h,owner='nodes.colors',generation=1)
        with self.assertRaisesRegex(M.MemoryViolation,'released'):
            self.ledger.release(self.h,owner='nodes.colors',generation=1)

    def test_borrowed_owner_cannot_free(self):
        with self.assertRaisesRegex(M.MemoryViolation,'owner'):
            self.ledger.release(self.h,owner='other',generation=1)
        self.ledger.access(self.h,0,4,write=True)

    def test_wrong_generation_cannot_free(self):
        with self.assertRaisesRegex(M.MemoryViolation,'generation'):
            self.ledger.release(self.h,owner='nodes.colors',generation=2)

    def test_reused_address_does_not_revive_an_old_handle(self):
        self.ledger.release(self.h,owner='nodes.colors',generation=1)
        new=self.ledger.register(0x10001000,64,owner='nodes.colors',generation=2)
        with self.assertRaisesRegex(M.MemoryViolation,'stale'):
            self.ledger.access(self.h,0,4,write=True)
        self.ledger.access(new,0,4,write=True)

    def test_different_capacity_at_reused_address_is_revalidated(self):
        self.ledger.release(self.h,owner='nodes.colors',generation=1)
        new=self.ledger.register(0x10001000,32,owner='nodes.colors',generation=2)
        with self.assertRaisesRegex(M.MemoryViolation,'overrun'):
            self.ledger.access(new,32,1,write=True)

    def test_memory_budget_is_enforced_before_registration(self):
        before=self.ledger.live_bytes
        with self.assertRaisesRegex(M.MemoryViolation,'budget'):
            self.ledger.register(0x20000000,1024*1024,owner='huge',generation=1)
        self.assertEqual(self.ledger.live_bytes,before)

    def test_thousands_of_reopen_cycles_have_bounded_metadata(self):
        self.ledger.release(self.h,owner='nodes.colors',generation=1)
        for generation in range(2,10002):
            h=self.ledger.register(0x10001000,64,owner='nodes.colors',generation=generation)
            self.ledger.access(h,60,4,write=True)
            self.ledger.release(h,owner='nodes.colors',generation=generation)
        self.assertEqual(self.ledger.record_count,1)
        self.assertEqual(self.ledger.live_bytes,0)

    def test_store_statistics_are_bounded_not_a_per_write_log(self):
        for i in range(50000):self.ledger.access(self.h,(i%16)*4,4,write=True)
        stats=self.ledger.stats(self.h)
        self.assertEqual(stats.writes,50000)
        self.assertEqual(stats.maximum_end,64)
        self.assertEqual(self.ledger.record_count,1)

    def test_unknown_addresses_are_not_silently_claimed_as_ours(self):
        self.assertIsNone(self.ledger.containing(0x20000000,4))
        self.assertEqual(self.ledger.containing(0x10001004,4),self.h)
        with self.assertRaises(M.MemoryViolation):
            self.ledger.containing(0x1000103f,4)

    def test_stale_generation_is_rejected_for_address_only_callback(self):
        with self.assertRaisesRegex(M.MemoryViolation,'generation'):
            self.ledger.access_address(0x10001000,4,write=True,owner='nodes.colors',generation=2)

    def test_record_budget_bounds_distinct_tombstones(self):
        ledger=M.Ledger(byte_budget=10000,record_budget=2)
        for addr in (0x1000,0x2000):
            h=ledger.register(addr,16,owner='x',generation=1)
            ledger.release(h,owner='x',generation=1)
        with self.assertRaisesRegex(M.MemoryViolation,'record'):
            ledger.register(0x3000,16,owner='x',generation=2)


class ProtocolTests(unittest.TestCase):
    def setUp(self):self.assertIsNotNone(M,'The render protocol contract is missing')

    def test_measured_stock_units(self):
        plan=M.RenderPlan(861)
        self.assertEqual(plan.buffer_sizes,{'positions':41328,'colors':55104,'uv':27552,'indices':10332})
        self.assertEqual(plan.maximum_vertex_index,3443)
        self.assertEqual(plan.append_negative,-862)

    def test_float_count_is_not_byte_count(self):
        plan=M.RenderPlan(861)
        self.assertEqual(plan.buffer_sizes['colors'],861*16*4)
        self.assertEqual(plan.buffer_sizes['uv'],861*8*4)
        self.assertNotEqual(plan.buffer_sizes['colors'],861*16)

    def test_roundtrip_all_commands_at_every_scale(self):
        for capacity in (1,860,861,862,1024,1025,2048,4096,16384):
            plan=M.RenderPlan(capacity)
            for node in range(capacity):
                self.assertEqual(plan.decode(plan.positive(node)),('append_first',node))
                self.assertEqual(plan.decode(plan.negative(node)),('fixed_second',node))
                self.assertEqual(plan.decode(node),('fixed_first',node))
            self.assertEqual(plan.decode(plan.append_negative),('append_second',None))
            self.assertEqual(plan.decode(0xffff),('append_both_indices',None))

    def test_triangle_index_limit_is_not_node_storage_limit(self):
        plan=M.RenderPlan(16384)
        self.assertEqual(plan.maximum_vertex_index,65535)
        with self.assertRaisesRegex(M.MemoryViolation,'16-bit'):
            M.RenderPlan(16385)

    def test_invalid_protocol_values_are_rejected(self):
        plan=M.RenderPlan(861)
        for value in (-863,1722,2**31,-2**31-1,True,1.5):
            with self.subTest(value=value),self.assertRaises(M.MemoryViolation):plan.decode(value)
        for value in (-1,861,True):
            with self.subTest(index=value),self.assertRaises(M.MemoryViolation):plan.positive(value)

    def test_explicit_overlay_budget_does_not_count_as_proof(self):
        plan=M.RenderPlan.for_nodes(1024,extra_quads=8)
        self.assertEqual(plan.quads,1032)
        self.assertFalse(plan.proves_native_layout)
        with self.assertRaises(M.MemoryViolation):M.RenderPlan.for_nodes(16384,extra_quads=1)

    def test_append_preflight_rejects_constructor_full_counts(self):
        plan=M.RenderPlan(861)
        with self.assertRaisesRegex(M.MemoryViolation,'full'):
            plan.check_append(vertices=3444,indices=5166)
        self.assertEqual(plan.check_append(vertices=0,indices=0),0)
        self.assertEqual(plan.check_append(vertices=3440,indices=5160),860)

    def test_append_counters_must_have_matching_units(self):
        plan=M.RenderPlan(861)
        for v,i in ((1,6),(4,5),(8,6),(-4,0),(0,-6),(True,0)):
            with self.subTest(v=v,i=i),self.assertRaises(M.MemoryViolation):plan.check_append(vertices=v,indices=i)


if __name__=='__main__':unittest.main()
