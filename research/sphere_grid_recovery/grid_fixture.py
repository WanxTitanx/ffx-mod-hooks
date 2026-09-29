"""Native ABMAP data/producer/activation fixtures in private emulated memory.

Resource I/O, sprite artwork, party-stat consumption and effect creation are
explicit synthetic boundaries. No real renderer device, process or save is used.
"""
import struct
import codec
import panel_fixture
from native_fixture import SphereMachine
from unicorn.x86_const import UC_X86_REG_ESP

IDENTITY = struct.pack('<16f', 1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)


class GridMachine(SphereMachine):
    def bind_stat_summary(self):
        # Original A54860 and 7AB890 execute in this same machine. Only the
        # destination/command sinks and downstream player application are doubles.
        self.summary_memory = self.alloc(7*16, 'native stat summary destination')
        self.stat_grants = []
        self.stat_apply_calls = 0
        def reset():
            self.write_bytes(self.summary_memory, bytes(7*16))
            self.stat_grants.clear()
            self.returning()
        def destination():
            sp = self.cpu.reg_read(UC_X86_REG_ESP)
            character = self.read32(sp+4)
            if not 0 <= character < 7:
                raise ValueError('native summary character is outside roster')
            self.returning(self.summary_memory+character*16)
        def command():
            sp = self.cpu.reg_read(UC_X86_REG_ESP)
            character, ability = self.read32(sp+4), self.read32(sp+8)
            if not 0 <= character < 7:
                raise ValueError('native command character is outside roster')
            if ability:
                self.stat_grants.append((character, ability))
            self.returning()
        def apply():
            self.stat_apply_calls += 1
            self.services['downstream player-stat application boundary'] += 1
            self.returning()
        self.special.update({0x798830:reset, 0x798800:destination,
                             0x798850:command, 0x7869c0:apply})

    def read_stat_summary(self):
        return [struct.unpack('<II8B',self.cpu.mem_read(self.summary_memory+i*16,16))
                for i in range(7)]

    def bind_resources(self, layout, contents, mode=0):
        # The original native strand must admit exactly the strict packed pair.
        codec.parse(layout, contents)
        self.menu = self.alloc(0x12fc0, 'logical menu state')
        self.set32(0x2305834, self.menu)
        # Native category refresh reads categories 2..5 at stride 0x340.
        # These are explicitly synthetic, valid empty command lists.
        categories = self.alloc(6*0x340, 'synthetic empty command categories')
        self.set32(0x1a86108, categories)
        # A5AA30 reads panel_bin_ptr, then executes the original 7AB890 table
        # lookup. Supplying the table is not stubbing either native consumer.
        # Only this explicit HP fixture row is currently admitted for activation.
        self.panel_rows = {35: (0x100, 0, 6)}
        panel = panel_fixture.encode(self.panel_rows)
        self.panel = self.alloc(len(panel), 'synthetic panel resource table')
        self.write_bytes(self.panel, panel)
        self.set32(0x1a860e0, self.panel)
        self.bind_stat_summary()
        self.fixture_text = self.alloc(16, 'synthetic localized text')
        self.write_bytes(self.fixture_text, b'fixture\0')
        def text_resource():
            self.services['synthetic localization/character/stat-name resource'] += 1
            self.returning(self.fixture_text)
        # Text catalogs remain declared external data services; activation,
        # mask publication and native panel dispatch are not suppressed.
        self.special.update({0x78fd40:text_resource, 0x8ac800:text_resource,
                             0xa56fc0:text_resource})
        table = self.alloc(0x20, 'synthetic resource interface')
        self.cpu.mem_map(0x3f010000, 0x1000)
        self.set32(table, 0x3f010010)
        self.set32(table+8, 0x3f010020)
        self.set32(table+0x10, 0x3f010030)
        self.special[0x76d1e0] = lambda: self.returning(table)
        self.resource_requests = []

        def load():
            sp = self.cpu.reg_read(UC_X86_REG_ESP)
            resource, destination = self.read32(sp+4), self.read32(sp+8)
            if resource in (9,10,11):
                value = layout
            elif resource in (17,18,19):
                value = contents
            else:
                raise AssertionError('unmodelled native resource ' + str(resource))
            self.resource_requests.append(resource)
            if len(self.resource_requests) > 256:
                raise AssertionError('unbounded native resource loop')
            self.services['synthetic resource read'] += 1
            self.write_bytes(destination, value)
            self.returning(len(value))

        self.special[0x3f010010] = load
        self.special[0x3f010020] = lambda: self.returning()
        self.special[0x3f010030] = lambda: self.returning()
        mode_pointer = self.invoke(0x785350)
        self.set32(mode_pointer, mode << 14)

    def load_layout(self, layout, contents, mode=0):
        self.bind_resources(layout, contents, mode)
        # A5B140 scans all links for each node. The original grid reached node
        # 111 at one million instructions; a finite larger budget covers 1024².
        self.invoke(0xa45570, limit=16000000)
        self.write_bytes(self.menu+0x113e0, IDENTITY)
        self.write_bytes(self.menu+0x11350, struct.pack('<4f', 1,1,1,1))
        self.art = self.sprite()
        for kind in range(130):
            ui = self.menu+0xf828+kind*0x30
            for offset in (0,4,8):
                self.set32(ui+offset, self.art)
            self.write_bytes(ui+12, struct.pack('<hhf',16,16,1))
        return self.menu

    def draw_nodes(self, manager):
        self.reset_sphere(manager)
        self.invoke(0xa51340, limit=2000000)

    def update_node(self, index):
        self.invoke(0xa51560, (index,))

    def activate(self, index, character):
        if type(index) is not int or not 0 <= index < self.read32(self.menu) >> 16:
            raise ValueError('activation index is outside the loaded layout')
        if type(character) is not int or not 0 <= character < 7:
            raise ValueError('activation character is outside the native roster')
        if self.node_word(index) not in self.panel_rows:
            raise ValueError('activation requires an explicitly supplied panel fixture row')
        def service(name):
            def invoke():
                self.services[name] += 1
                self.returning()
            return invoke
        self.special[0x72c570] = service('synthetic effect resource setup')
        self.special[0xa5bad0] = service('synthetic activation effect creation')
        self.invoke(0xa48910, (character,index), limit=2000000)
        self.update_node(index)

    def roundtrip_payload(self):
        self.invoke(0xa5bb70)
        payload = bytes(self.cpu.mem_read(0x112ca90, 0x68c0))
        image = self.alloc(0x6900, 'private synthetic save image')
        self.write_bytes(image, bytes(0x40)+payload)
        self.write_bytes(0x112ca90, bytes(0x68c0))
        self.special[0x787530] = lambda: self.returning()
        self.invoke(0x8b5450, (0x112ca90,image))
        assert bytes(self.cpu.mem_read(0x112ca90,0x68c0)) == payload
        self.release(image)
        self.invoke(0xa49590, limit=2000000)
        return payload

    def node_word(self, index, offset=6):
        return int.from_bytes(self.cpu.mem_read(self.menu+0x808+index*40+offset,2),'little')

    def activated(self, index):
        return self.cpu.mem_read(self.menu+0x808+index*40+0x21,1)[0]
