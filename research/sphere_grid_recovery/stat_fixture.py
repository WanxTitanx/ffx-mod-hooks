"""Original A54860 aggregation; isolated state/count experiment, not a full grid.

State pointer, panel data, summary destinations, command sink and downstream
apply are explicit fixtures. The native sum itself executes original code.
"""
import struct
from emulator_patches import Patch, apply_private_image
from native_machine import Machine
import panel_fixture

class StatFixture:
    ROWS = {35: (0x100, 0, 6), 5: (1, 0, 4)}

    def __init__(self, mapped, count):
        if type(count) is not int or not 1 <= count <= 16384:
            raise ValueError("stat count outside [1,16384]")
        self.node_count=count
        image=apply_private_image(mapped.data,(Patch("stat_count",0xa549ac-0x400000,
            bytes.fromhex("81fe00040000"),2,count),))
        self.m=Machine(mapped,private_image=image);m=self.m
        self.state=m.alloc(count*2,"stat state")
        m.put(self.state.address,b"\xff\0"*count)
        self.summary=m.alloc(7*16,"seven-character summary")
        data=panel_fixture.encode(self.ROWS)
        self.panel=m.alloc(len(data),"synthetic panel")
        m.put(self.panel.address,data);m.set32(0x1a860e0,self.panel.address)
        self.downstream_apply_calls=0;self.grants=[]
        def reset():
            m.put(self.summary.address,bytes(self.summary.length));self.grants.clear();m.returning()
        def summary():
            (character,)=m.arguments(1)
            if not 0<=character<7:raise ValueError("summary character outside roster")
            m.returning(self.summary.address+character*16)
        def grant():
            character,command=m.arguments(2)
            if not 0<=character<7:raise ValueError("command character outside roster")
            if command:self.grants.append((character,command))
            m.returning()
        def downstream():
            self.downstream_apply_calls+=1;m.returning()
        m.external.update({0x785000:lambda:m.returning(self.state.address),0x798830:reset,
            0x798800:summary,0x798850:grant,0x7869c0:downstream,0x949240:lambda:m.returning()})
        m.allowed_code=[(0xa54860,0xa54aa3),(0x7ab890,0x7ab90e)]
        m.dependencies.update({"synthetic panel and state pointer","summary and command sink",
            "downstream player-stat application observed, not executed","private stat-count experiment"})

    def state_bytes(self):return self.m.get(self.state.address,self.state.length)

    def activate(self,node,character,content):
        # Fixture seeding is distinct from the separately executed A48910.
        if type(node) is not int or not 0<=node<self.node_count:raise ValueError("invalid node")
        if type(character) is not int or not 0<=character<7:raise ValueError("invalid character")
        if type(content) is not int or content not in self.ROWS:raise ValueError("unknown panel row")
        old=self.m.get(self.state.address+node*2,2)
        if old[1] and old[0]!=content:raise ValueError("cannot replace activated type")
        self.m.put(self.state.address+node*2,bytes((content,old[1]|(1<<character))))

    def recompute(self):
        with self.m.borrowed((self.state,self.summary,self.panel)):
            self.m.call(0xa54860,instructions=4000000)
        return [struct.unpack("<II8B",self.m.get(self.summary.address+i*16,16)) for i in range(7)]
