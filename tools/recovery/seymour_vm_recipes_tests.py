#!/usr/bin/env python3
"""Runner admission only: fake SSH results, no remote command or game access."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import run_seymour_adapters as runner


class Recipes(unittest.TestCase):
    def test_each_supported_adapter_obeys_busy_preflight(self):
        for adapter in ("gear", "overdrive", "session", "menu", "sort"):
            with self.subTest(adapter=adapter), tempfile.TemporaryDirectory() as directory:
                result = subprocess.CompletedProcess([], 75, b"busy\n", b"")
                with patch.object(runner.subprocess, "run", return_value=result) as transport:
                    code = runner.run("not-a-real-host", Path(directory) / "output", (adapter,))
                self.assertEqual(code, 75)
                self.assertEqual(transport.call_count, 1)
                self.assertEqual(transport.call_args.args[0][0], "ssh")
                self.assertFalse((Path(directory) / "output" / "task.json").exists())

    def test_invalid_selection_never_contacts_windows(self):
        for adapters in ((), ("unknown",), ("menu", "menu")):
            with self.subTest(adapters=adapters), tempfile.TemporaryDirectory() as directory:
                with patch.object(runner.subprocess, "run") as transport:
                    with self.assertRaises(ValueError):
                        runner.run("not-a-real-host", Path(directory) / "output", adapters)
                    transport.assert_not_called()


if __name__ == "__main__":
    unittest.main()
