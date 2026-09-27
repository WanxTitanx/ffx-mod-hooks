from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
from server import Workshop
from bridge import WorkshopError, Policy
from store import Store, native_gil, GIL_BASE, GEAR_BASE, QTY_BASE
import struct
import test_store as fixtures

class ControllerTests(unittest.TestCase):
    def setUp(self):
        fixtures.StoreTests.setUp(self);self.app=Workshop(self.store)
    def tearDown(self):fixtures.StoreTests.tearDown(self)
    def request(self,op='mode',**extra):
        state=self.app.view();p=state['pieces'][0]
        return dict(op=op,slot=p['slot'],piece=p['id'],revision=state['revision'],**extra)
    def confirm(self,request):
        result=self.app.preview(request)
        return self.app.confirm({'confirmation':result['confirmation']})
    def test_preview_then_restart_no_debit(self):
        before=self.store.load();self.app.preview(self.request(value=2));self.app=Workshop(self.store)
        self.assertEqual(self.store.load()[:2],before[:2])
    def test_confirm_once(self):
        result=self.app.preview(self.request(value=2));token={'confirmation':result['confirmation']}
        self.app.confirm(token)
        with self.assertRaises(WorkshopError):self.app.confirm(token)
        self.assertEqual(self.store.load()[2].revision,1)
    def test_rng_persists_between_hosts(self):
        self.confirm(self.request(value=2));self.confirm(self.request('refine'))
        self.app=Workshop(self.store);state=self.app.view();self.assertEqual(state['pieces'][0]['total'],1)
        self.confirm(self.request('refine'));self.assertEqual(self.store.load()[2].rolls,2)
    def test_random_preview_hides_winner_and_has_known_cost(self):
        self.confirm(self.request(value=2));preview=self.app.preview(self.request('refine'))
        self.assertIsNone(preview['after']);self.assertTrue(preview['random'])
        self.assertEqual({c['id']:c['amount'] for c in preview['costs']},{70:1,77:1})
        self.assertTrue(preview['requirements_only'])
    def test_new_preview_invalidates_old_confirmation(self):
        a=self.app.preview(self.request(value=1));b=self.app.preview(self.request(value=2))
        with self.assertRaises(WorkshopError):self.app.confirm({'confirmation':a['confirmation']})
        self.app.confirm({'confirmation':b['confirmation']});self.assertEqual(self.store.load()[2].pieces[0].mode,2)
    def test_client_cannot_inject_native_template(self):
        with self.assertRaises(WorkshopError):self.app.preview(self.request('reforge',template='foreign',gearTemplate=[0]*22))
    def test_bounds_and_lifecycle_not_exposed(self):
        for value in (-1,65536,True,'2',None):
            with self.assertRaises(WorkshopError):self.app.preview(self.request(value=value))
        for op in ('create','swap','retire','unknown'):
            with self.assertRaises(WorkshopError):self.app.preview(self.request(op))
    def test_foreign_revision_rejected(self):
        r=self.request(value=1);r['revision']=99
        with self.assertRaises(WorkshopError):self.app.preview(r)
    def test_fifth_ui_uses_same_core_and_persistence(self):
        raw=bytearray(fixtures.synthetic());raw[GEAR_BASE+5]=1
        raw[QTY_BASE+57]=150
        source=self.root/'filled-armor';source.write_bytes(raw)
        self.store=Store(self.root/'fifth-workspace',self.core);self.store.create(source,123);self.app=Workshop(self.store)
        self.confirm(self.request('unlock_fifth'));self.confirm(self.request('set_fifth',value=0x8055))
        state=self.app.view();self.assertEqual(state['pieces'][0]['abilities'][4]['name'],'Auto-Protect')
        self.assertEqual(self.store.load()[2].pieces[0].native[11],4)

    def test_generic_refinement_and_complete_fifth_catalog(self):
        piece=self.store.load()[2].pieces[0]
        piece.native[14]=0;piece.native[15]=0x80
        self.assertTrue(self.app.piece(piece,0)['abilities'][0]['supported'])
        piece.native[14]=131
        self.assertFalse(self.app.piece(piece,0)['abilities'][0]['supported'])
        choices={p['id'] for p in self.app.view()['fifth_choices']}
        self.assertEqual(len(choices),125)
        self.assertIn(0x8000,choices)

    def mixed(self,seed=1,gil=100000,fusion=False):
        raw=bytearray(fixtures.synthetic())
        struct.pack_into('<I',raw,GIL_BASE,gil)
        for slot in range(2):
            for index,word in enumerate((0x8000,0x8064,0x8055,0x8075)):
                struct.pack_into('<H',raw,GEAR_BASE+22*slot+14+2*index,word)
        if fusion:
            raw[GEAR_BASE+5]=raw[GEAR_BASE+22+5]=1
            struct.pack_into('<H',raw,GEAR_BASE+14+2*2,0x8054)
        source=self.root/('mixed-source-'+str(seed));source.write_bytes(raw)
        self.store=Store(self.root/('mixed-'+str(seed)),self.core);self.store.create(source,seed)
        self.app=Workshop(self.store)

    def test_random_preview_does_not_leak_winner_through_ingredient(self):
        requirements=None
        for seed in (1,2,3,4,9):
            self.mixed(seed);view=self.app.preview(self.request('refine'))
            self.assertIsNone(view['after']);self.assertTrue(view['requirements_only'])
            self.assertNotIn('chosenAbility',view)
            costs={c['id']:c['amount'] for c in view['costs']}
            self.assertEqual(costs,{70:1,73:1,77:1,57:7,67:1})
            if requirements is not None:self.assertEqual(costs,requirements)
            requirements=costs

    def test_winning_material_only_is_debited_and_saved(self):
        self.mixed(fusion=True);before=self.store.load()[2]
        self.confirm(self.request('refine'));after=self.store.load()[2]
        self.assertEqual(before.items[70]-after.items[70],1)
        changed=[i for i in (73,77,57,67) if before.items[i]!=after.items[i]]
        self.assertEqual(len(changed),1)
        self.assertEqual(sum(after.pieces[0].ranks),1)
        self.assertEqual(native_gil(self.store.load()[0]),99000)

    def test_fusion_gil_and_ingredient_debit_recover_once(self):
        self.mixed(fusion=True);before=self.store.load()
        request=self.request('fuse',other=1,transfers=[{'from':2,'to':3}])
        quote=self.app.preview(request)
        self.assertEqual(quote['gil_cost'],10000)
        self.assertEqual({c['id']:c['amount'] for c in quote['costs']},{57:24})
        def fault(at):
            if at=='native_written':raise OSError('private interruption after native Gil write')
        self.store.fault=fault
        with self.assertRaises(OSError):self.app.confirm({'confirmation':quote['confirmation']})
        raw,_,after,_=self.store.load()
        self.assertEqual(native_gil(raw),90000);self.assertEqual(after.items[57],75)
        self.assertEqual(after.pieces[1].id,0);self.assertEqual(after.pieces[1].native[2],0)
        self.assertEqual(self.store.load()[0],raw)
        self.assertEqual(native_gil(before[0]),100000)

    def test_insufficient_gil_leaves_native_pair_unchanged(self):
        self.mixed(gil=9999);before=self.store.load()[:2]
        with self.assertRaises(WorkshopError):self.app.preview(self.request('fuse',other=1,transfers=[{'from':2,'to':3}]))
        self.assertEqual(self.store.load()[:2],before)

    def test_policy_change_invalidates_confirm_without_spending(self):
        self.mixed(fusion=True);before=self.store.load()[:2]
        quote=self.app.preview(self.request('refine'));self.app.policy=Policy(mode=1)
        with self.assertRaises(WorkshopError):self.app.confirm({'confirmation':quote['confirmation']})
        self.assertEqual(self.store.load()[:2],before)

    def test_global_a_refines_uneven_b_without_erasing_ranks(self):
        self.mixed();self.confirm(self.request('refine'));before=self.store.load()[2]
        self.app.policy=Policy(mode=1);self.confirm(self.request('refine'));after=self.store.load()[2]
        self.assertEqual(list(after.pieces[0].ranks)[:4],[r+1 for r in list(before.pieces[0].ranks)[:4]])
        self.assertEqual(after.rolls,before.rolls)

if __name__=='__main__':unittest.main()
