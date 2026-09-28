from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
from bridge import WorkshopError
from server import Workshop
from store import Store,GEAR_BASE,GIL_BASE,native_gil
import test_store as fixtures


class AeonControllerTests(unittest.TestCase):
    def setUp(self):
        fixtures.StoreTests.setUp(self)
        raw=bytearray(fixtures.synthetic())
        raw[GEAR_BASE+2:GEAR_BASE+7]=bytes([1,7,8,0,8])
        struct.pack_into('<4H',raw,GEAR_BASE+14,0x807B,0x8019,0x8062,0x800B)
        raw[64+0xC6C]=2
        ply=64+0x55CC+8*0x94
        raw[ply+0x2C]=0x11;raw[ply+0x2D]=0;raw[ply+0x2E]=255
        struct.pack_into('<I',raw,GIL_BASE,2000000)
        source=self.root/'aeon-source';source.write_bytes(raw)
        self.store=Store(self.root/'aeon-workspace',self.core);self.store.create(source,123)
        self.app=Workshop(self.store)

    def tearDown(self):
        fixtures.StoreTests.tearDown(self)

    def request(self,op,**extra):
        view=self.app.view();piece=next(p for p in view['pieces'] if p['slot']==0)
        return dict(op=op,slot=0,piece=piece['id'],revision=view['revision'],**extra)

    def test_aeon_view_and_persisted_fifth_transaction(self):
        view=self.app.view();piece=next(p for p in view['pieces'] if p['slot']==0)
        self.assertEqual(piece['owner'],'Valefor')
        self.assertTrue(piece['aeon'] and piece['equipped'] and piece['abilities'][0]['locked'])
        self.assertFalse(piece['protected'])
        before=self.store.load();quote=self.app.preview(self.request('unlock_fifth'))
        self.assertEqual({c['id']:c['amount'] for c in quote['costs']},{i:4 for i in range(75,80)})
        self.app.confirm({'confirmation':quote['confirmation']})
        quote=self.app.preview(self.request('set_fifth',value=0x8066))
        self.assertEqual(quote['gil_cost'],400000)
        self.app.confirm({'confirmation':quote['confirmation']})
        raw,_,state,_=self.store.load()
        self.assertEqual(native_gil(raw),native_gil(before[0])-400000)
        self.assertEqual(state.pieces[0].fifth,0x8066)
        self.assertEqual(state.pieces[0].native[14:16],[0x7B,0x80])
        for i in range(75,80):self.assertEqual(state.items[i],before[2].items[i]-4)
        self.app=Workshop(self.store)
        piece=next(p for p in self.app.view()['pieces'] if p['slot']==0)
        self.assertEqual(piece['abilities'][4]['id'],0x8066)

    def test_immunity_cannot_be_removed_through_the_controller(self):
        before=self.store.load()
        with self.assertRaises(WorkshopError):self.app.preview(self.request('clear',value=0))
        self.assertEqual(self.store.load()[:2],before[:2])

    def test_changed_progress_cannot_reuse_an_old_confirmation(self):
        quote=self.app.preview(self.request('unlock_fifth'))
        raw,_,state,save_id=self.store.load();changed=bytearray(raw);changed[64+0xC6C]=0
        # Simulate an independently verified producer replacing a native pair.
        self.store.write('native.bin',bytes(changed));self.store.write('sidecar.json',self.store.sidecar(bytes(changed),state,save_id))
        before=self.store.load()
        with self.assertRaises(WorkshopError):self.app.confirm({'confirmation':quote['confirmation']})
        self.assertEqual(self.store.load()[:2],before[:2])
        piece=next(p for p in self.app.view()['pieces'] if p['slot']==0)
        self.assertTrue(piece['protected']);self.assertIn('Nirvana',piece['requirement'])


if __name__=='__main__':unittest.main()
