"""A passing writer matrix cannot hide a failed native activation strand."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.append(str(Path(__file__).resolve().parents[1]))
import verify

class RequiredStrandTests(unittest.TestCase):
    def test_original_native_failure_blocks_full_verification(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)/'report'
            def run(command, log, **kwargs):
                if 'test_*.py' in command:
                    return {'exit_code': 1, 'log': log.name, 'stdout': '',
                            'stderr': 'Ran 30 tests\nFAILED (failures=1)'}
                if any('native_harness.py' in arg for arg in command):
                    (output/'native.json').write_text(json.dumps({'unit_cases_passed': 46,
                        'unit_cases_total': 46, 'unit_matrix_passed': True}))
                return {'exit_code': 0, 'log': log.name,
                        'stdout': json.dumps({'detected': 10, 'mutations': 10}),
                        'stderr': 'Ran 116 tests\nOK'}
            with patch.object(verify, '_run', side_effect=run), patch.object(
                    verify, 'audit_corpus', return_value={'status': 'passed'}):
                code = verify.main(['--output', str(output), '--executable', 'exact.exe',
                                    '--corpus', 'original-pairs'])
            report = json.loads((output/'verification.json').read_text())
            self.assertIn('original_native', report, 'native activation was omitted')
            self.assertEqual(report['original_native']['status'], 'failed')
            self.assertFalse(report['requested_checks_passed'])
            self.assertEqual(code, 1)

if __name__ == '__main__':
    unittest.main()
