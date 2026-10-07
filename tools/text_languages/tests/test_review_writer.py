import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from review_writer import append_review


class ReviewWriterTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.source=self.root/'source.jsonl';self.decisions=self.root/'decisions.jsonl';self.progress=self.root/'progress.json'
        rows=[{'uid':'u0','en':'Hello.','pt':'Olá.'},
              {'uid':'u1','en':'{CTRL:09:30}Use 2 items.','pt':'{CTRL:09:30}Use 2 itens.'}]
        self.source.write_text(''.join(json.dumps(r)+'\n' for r in rows))
        self.first={'uid':'u0','decision':'accept_legacy','source_anchor':'Hello.'}
        self.second={'uid':'u1','decision':'revise','source_anchor':'Use 2 items.',
                     'proposed_pt_br':'{CTRL:09:30}Use 2 itens.'}

    def tearDown(self):self.temp.cleanup()

    def write(self,batch,count,limit=None):
        return append_review(self.source,self.decisions,self.progress,batch,count,limit)

    def test_replayed_append_is_idempotent_and_repairs_a_missing_checkpoint(self):
        self.assertEqual(self.write([self.first],0)['status'],'appended')
        before=self.decisions.read_bytes();self.progress.unlink()
        self.assertEqual(self.write([self.first],0)['status'],'already_applied')
        self.assertEqual(self.decisions.read_bytes(),before)
        self.assertEqual(json.loads(self.progress.read_text())['reviewed_count'],1)

    def test_invalid_anchor_uid_or_controls_leave_both_outputs_unchanged(self):
        self.write([self.first],0)
        before=(self.decisions.read_bytes(),self.progress.read_bytes())
        for changes in [{'uid':'u0'},{'source_anchor':'Unrelated sentence'},
                        {'proposed_pt_br':'Use 2 itens.'},{'proposed_pt_br':'{CTRL:09:30}Use 3 itens.'}]:
            with self.subTest(changes=changes):
                with self.assertRaises(ValueError):self.write([{**self.second,**changes}],1)
                self.assertEqual((self.decisions.read_bytes(),self.progress.read_bytes()),before)

    def test_stale_count_cannot_relabel_a_batch_as_the_next_source_rows(self):
        self.write([self.first],0)
        with self.assertRaises(ValueError):self.write([self.second],0)
        self.assertEqual(len(self.decisions.read_text().splitlines()),1)

    def test_malformed_checkpoint_is_rejected_before_decisions_change(self):
        self.write([self.first],0);before=self.decisions.read_bytes()
        self.progress.write_text('{broken')
        with self.assertRaises(ValueError):self.write([self.second],1)
        self.assertEqual(self.decisions.read_bytes(),before)

    def test_assigned_prefix_limit_and_accepted_legacy_shape_are_enforced(self):
        self.write([self.first],0)
        with self.assertRaises(ValueError):self.write([self.second],1,limit=1)
        rows=[json.loads(r) for r in self.source.read_text().splitlines()]
        rows[1]['pt']='Use 2 itens.'
        self.source.write_text(''.join(json.dumps(r)+'\n' for r in rows))
        with self.assertRaises(ValueError):self.write([{'uid':'u1','decision':'accept_legacy','source_anchor':'Use 2 items.'}],1)


if __name__=='__main__':unittest.main()
