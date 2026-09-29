"""Native node producer, activation and in-memory persistence regressions."""
import os
import math
from pathlib import Path
import struct
import unittest
from grid_fixture import GridMachine
from layout import parse_pair
from native_fixture import exact_image
from test_render_plan import apply_plan


class GridNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw = exact_image(os.environ['FFX_SPHERE_EXE'])
        root = Path(os.environ['FFX_SPHERE_ASSETS'])
        cls.base = parse_pair((root/'dat02.dat').read_bytes(), (root/'dat10.dat').read_bytes())

    def expanded(self, count):
        grid = self.base
        degree = [0] * grid.node_count
        for link in grid.links:
            degree[link.first] += 1
            degree[link.second] += 1
        last = next(i for i,value in enumerate(degree) if value < 5)
        while grid.node_count < count:
            node = grid.nodes[last]
            grid = grid.append_node(template=last, x=node.x+8, y=node.y,
                                    content=0x23, connect_to=last)
            last = grid.node_count-1
        return grid

    def test_native_layout_and_two_layer_producer_for_861(self):
        grid = self.expanded(861)
        m = GridMachine(self.raw)
        manager = m.manager(full=True)
        m.load_layout(*grid.encode())
        self.assertEqual(m.node_word(860), 0x23)
        m.draw_nodes(manager)
        for layer in m.sphere_layers(manager)[1]:
            self.assertEqual((m.read32(layer+4),m.read32(layer+8)), (3444,5166))
        m.update_node(860)

    def test_native_activation_and_serialization_of_appended_node(self):
        grid = self.expanded(861)
        m = GridMachine(self.raw)
        manager = m.manager(full=True)
        m.load_layout(*grid.encode())
        m.draw_nodes(manager)
        m.activate(860, 0)
        self.assertTrue(m.activated(860)&1)
        payload = m.roundtrip_payload()
        self.assertEqual(payload[0x21ec+2*860],0x23)
        self.assertTrue(payload[0x21ec+2*860+1]&1)
        self.assertTrue(m.activated(860)&1)

    def test_activation_reaches_original_stat_aggregation(self):
        grid = self.expanded(861)
        m = GridMachine(self.raw)
        self.assertTrue(hasattr(m, 'read_stat_summary'), 'activation still has a stat stub')
        manager = m.manager(full=True)
        m.load_layout(*grid.encode())
        m.draw_nodes(manager)
        m.invoke(0xa5bb70)
        m.invoke(0xa54860)
        before = m.read_stat_summary()
        calls = m.native_calls['0xa54860']
        m.activate(860, 0)
        self.assertNotIn(0xa54860, m.special, 'native stat sum must not be suppressed')
        self.assertEqual(m.native_calls['0xa54860'], calls + 1)
        after = m.read_stat_summary()
        self.assertEqual(after[0][0], before[0][0] + 6)
        self.assertEqual(after[0][1:], before[0][1:])
        self.assertEqual(after[1:], before[1:])
        m.roundtrip_payload()
        m.invoke(0xa54860)
        self.assertEqual(m.read_stat_summary(), after)

    def test_extended_native_producer_862_and_1003_nodes(self):
        for count in (862,1003):
            with self.subTest(count=count):
                grid = self.expanded(count)
                m = GridMachine(self.raw)
                apply_plan(m,1024)
                manager = m.manager(full=True)
                m.load_layout(*grid.encode())
                m.draw_nodes(manager)
                for layer in m.sphere_layers(manager)[1]:
                    self.assertEqual((m.read32(layer+4),m.read32(layer+8)), (count*4,count*6))
                m.update_node(count-1)
                m.invoke(0xa4fe40,limit=2000000)

    def test_appended_1003rd_node_draw_activation_and_save_state(self):
        # Keep all Standard edges: 860/881 plus 143 nodes/links = 1003/1024.
        grid=self.expanded(1003)
        self.assertEqual(len(grid.links),1024)
        m=GridMachine(self.raw);apply_plan(m,1024)
        manager=m.manager(full=True);m.load_layout(*grid.encode());m.draw_nodes(manager)
        # The original loader must build real native links for every new node,
        # not merely increase the counts while leaving disconnected records.
        for node_index in range(860,1003):
            expected={m.menu+0xa808+i*20 for i,edge in enumerate(grid.links)
                      if node_index in (edge.first,edge.second)}
            at=m.menu+0x808+node_index*40+0xc
            actual=set(struct.unpack('<5I',m.cpu.mem_read(at,20)))-{0}
            self.assertEqual(actual,expected)
        for link_index in range(881,1024):
            native=struct.unpack('<3H',m.cpu.mem_read(m.menu+0xa808+link_index*20,6))
            edge=grid.links[link_index]
            self.assertEqual(native,(edge.first,edge.second,0xffff))
        layer=m.sphere_layers(manager)[1][0]
        xyz=struct.unpack('<12f',m.cpu.mem_read(m.read32(layer+12)+1002*48,48))
        uv=struct.unpack('<8f',m.cpu.mem_read(m.read32(layer+24)+1002*32,32))
        self.assertTrue(all(math.isfinite(v) for v in (*xyz,*uv)))
        area=(xyz[3]-xyz[0])*(xyz[7]-xyz[1])-(xyz[6]-xyz[0])*(xyz[4]-xyz[1])
        self.assertGreater(abs(area),1e-6)
        self.assertGreater(max(uv[::2])-min(uv[::2]),0)
        self.assertGreater(max(uv[1::2])-min(uv[1::2]),0)
        before=[(m.node_word(i),m.activated(i)) for i in range(1002)]
        m.activate(1002,6)
        self.assertEqual(m.activated(1002),1<<6)
        self.assertEqual([(m.node_word(i),m.activated(i)) for i in range(1002)],before)
        payload=m.roundtrip_payload()
        self.assertEqual(payload[0x21ec+1002*2:0x21ec+1002*2+2],bytes((35,64)))
        self.assertEqual(m.activated(1002),64)


if __name__ == '__main__':
    unittest.main()
