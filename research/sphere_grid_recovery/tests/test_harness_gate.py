"""Runner/admission tests. These never count as native executable test passes."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.append(str(Path(__file__).resolve().parents[1]))
import native_harness as n
import emulator_patches as p

class HarnessGateTests(unittest.TestCase):
    def test_unrelated_emulator_fault_is_not_counted_as_expected_overrun(self):
        def fail():raise n.NativeFailure({'kind':'unmodelled_native_callee'})
        with self.assertRaises(n.NativeFailure):n.expect_fault(fail,'allocation_bounds')

    def test_expected_fault_must_actually_happen(self):
        with self.assertRaises(n.NativeFailure):n.expect_fault(lambda:None,'allocation_bounds')

    def test_exact_negative_fault_is_retained(self):
        def fail():raise n.NativeFailure({'kind':'allocation_bounds','eip':'0x7f5622'})
        self.assertEqual(n.expect_fault(fail,'allocation_bounds')['eip'],'0x7f5622')

    def test_case_matrix_is_bounded_unique_and_covers_requested_boundaries(self):
        names=n.case_names(n.CAPACITIES)
        self.assertEqual(len(names),46)
        self.assertEqual(len(set(names)),46)
        for name in ('writer:16384:negative','fixed:1024','state:861','logical_nodes',
                     'logical_links','use_after_free','reused_generation','producer:1024',
                     'node_index_upper','node_index_negative'):
            self.assertIn(name,names)

    def test_state_sizes_do_not_confuse_decimal_or_file_header(self):
        self.assertEqual(n.PAYLOAD_SIZE,26816)
        self.assertEqual(n.FILE_SIZE,26880)
        self.assertEqual(64+n.STATE_OFFSET+2*860,10468)
        self.assertLess(n.STATE_OFFSET+n.STATE_SIZE,n.PAYLOAD_SIZE)
        self.assertEqual(n.CURSORS[0],69836)
        self.assertEqual(n.CURSORS[-1],70316)

    def test_menu_regions_are_adjacent_not_one_expandable_node_allocation(self):
        self.assertEqual(n.NODE_BASE+1024*n.NODE_STRIDE,n.LINK_BASE)
        self.assertEqual(n.LINK_BASE+1024*n.LINK_STRIDE,0xf808)

    def test_missing_executable_never_starts_workers(self):
        with patch.object(n.subprocess,'run') as worker:
            code=n.main(['/a/nonexistent/private/FFX.exe','--capacities','861'])
        self.assertEqual(code,2)
        worker.assert_not_called()

    def test_wrong_executable_hash_is_rejected_before_native_dependencies(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'FFX.exe';path.write_bytes(b'MZ'+b'\0'*128)
            result=subprocess.run([sys.executable,str(Path(n.__file__)),str(path)],
                                  capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,2)
            evidence=json.loads(result.stderr)
            self.assertFalse(evidence['production_ready'])
            self.assertEqual(evidence['status'],'not_validated')
            self.assertIn('SHA-256',evidence['error']['detail'])

    def test_duplicate_or_excessive_capacities_refused(self):
        for capacities in ('861,861',','.join(map(str,range(1,18))),'16385','-1','abc'):
            with self.subTest(capacities=capacities):
                with patch.object(n,'read_exact') as reader:
                    code=n.main(['irrelevant.exe','--capacities='+capacities])
                self.assertEqual(code,2);reader.assert_not_called()

    def test_unsupported_worker_mode_cannot_fall_back_to_another_test(self):
        with self.assertRaises(ValueError):n.run_case(None,'writer:861:unrecognised')

    def test_invalid_patch_record_is_a_controlled_preflight_failure(self):
        with self.assertRaises(p.PatchError):p.apply_private_image(bytearray(100),(object(),))

    def test_unverified_menu_scopes_are_explicit(self):
        joined=' '.join(n.UNVERIFIED).lower()
        for term in ('capture','activation','gpu','destructor','rt2','1024'):
            self.assertIn(term,joined)

if __name__=='__main__':unittest.main()
