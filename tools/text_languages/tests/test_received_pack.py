"""Recipient regression fixtures never trust mutation paths from rejected input."""
from pathlib import Path
from contextlib import redirect_stdout
import io
import json
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_received_pack


class ReceivedPackTests(unittest.TestCase):
    def test_rejected_manifest_cannot_select_mutation_targets(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package = root/'pack'; package.mkdir()
            reference = root/'reference'; reference.mkdir()
            victim = root/'unrelated.bin'; victim.write_bytes(b'Preserve outside fixture')
            validator = root/'validator'; validator.write_bytes(b'Private process boundary')
            (package/'manifest.json').write_text(json.dumps({'resources': [{'path': str(victim)}]}))
            # Only the external process boundary is substituted. The runner's
            # temporary copy, path interpretation and filesystem writes are real.
            rejected = subprocess.CompletedProcess([], 1, stdout='', stderr='REJECTED: unsafe path\n')
            with patch.object(check_received_pack.subprocess, 'run', return_value=rejected), redirect_stdout(io.StringIO()):
                try:
                    result = check_received_pack.run(validator, package, reference, root/'results')
                except (ValueError, RuntimeError, OSError):
                    result = 1
            self.assertEqual(result, 1)
            self.assertTrue(victim.is_file(), 'Rejected metadata must not delete an unrelated file')
            self.assertEqual(victim.read_bytes(), b'Preserve outside fixture')


if __name__ == '__main__':
    unittest.main()
