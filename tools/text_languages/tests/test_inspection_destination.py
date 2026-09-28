"""Reject installation destinations before reading or exporting font assets."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_asset_io import archive_bytes


class InspectionDestinationTests(unittest.TestCase):
    def test_all_installation_subdirectories_reject_before_extraction(self):
        with tempfile.TemporaryDirectory() as temporary:
            install = Path(temporary) / 'game'
            (install / 'data').mkdir(parents=True)
            (install / 'FFX.exe').write_bytes(b'installation marker')
            archive = install / 'data/FFX_Data.vbf'
            original = archive_bytes(b'unchanged source')
            archive.write_bytes(original)
            script = Path(__file__).resolve().parents[1] / 'inspect_assets.py'
            for relative in ('font-inspection', '_isolated/font-inspection', 'data/font-inspection'):
                output = install / relative
                with self.subTest(destination=relative):
                    result = subprocess.run([sys.executable, str(script), '--vbf', str(archive),
                                             '--output', str(output)], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertIn('outside', result.stderr)
                    self.assertFalse(output.exists())
                    self.assertEqual(archive.read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
