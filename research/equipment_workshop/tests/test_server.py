from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
from server import Workshop
from bridge import WorkshopError
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
        self.assertEqual({c['id']:c['amount'] for c in preview['costs']},{72:1,73:1})
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
        self.confirm(self.request('unlock_fifth'));self.confirm(self.request('set_fifth',value=0x8055))
        state=self.app.view();self.assertEqual(state['pieces'][0]['abilities'][4]['name'],'Auto-Protect')
        self.assertEqual(self.store.load()[2].pieces[0].native[11],4)

if __name__=='__main__':unittest.main()
