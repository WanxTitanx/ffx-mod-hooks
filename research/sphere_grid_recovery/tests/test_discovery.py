"""Exercise discovery in fresh interpreters; never run the private/native strand."""
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest

LAB = Path(__file__).resolve().parents[1]
PORTABLE = LAB / 'tests'
REPO = LAB.parents[1]
DISCOVER = r'''
import json
from pathlib import Path
import sys
import unittest

directory = Path(sys.argv[1]).resolve()
suite = unittest.TestLoader().discover(str(directory))
def leaves(tests):
    for test in tests:
        if isinstance(test, unittest.TestSuite):
            yield from leaves(test)
        else:
            yield test
tests = list(leaves(suite))
origins = {type(test).__module__: str(Path(sys.modules[type(test).__module__].__file__).resolve())
           for test in tests}
print(json.dumps({'ids': [test.id() for test in tests], 'origins': origins}))
'''


class DiscoveryTests(unittest.TestCase):
    def check_directory(self, cwd):
        environment = os.environ.copy()
        environment['PYTHONDONTWRITEBYTECODE'] = '1'
        for optimized in (False, True):
            with self.subTest(cwd=str(cwd), optimized=optimized):
                command = [sys.executable] + (['-O'] if optimized else [])
                result = subprocess.run(command + ['-c', DISCOVER, str(PORTABLE)],
                                        cwd=cwd, env=environment, capture_output=True,
                                        text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                data = json.loads(result.stdout)
                self.assertGreaterEqual(len(data['ids']), 113)
                self.assertIn('test_corpus.CorpusTests.test_all_three_pairs_roundtrip_without_modification',
                              data['ids'])
                self.assertEqual(set(data['origins']),
                                 {path.stem for path in PORTABLE.glob('test_*.py')})
                for origin in data['origins'].values():
                    self.assertEqual(Path(origin).parent, PORTABLE,
                                     'A prototype or unrelated global module was discovered')

    def test_repository_root_discovers_only_portable_modules(self):
        self.check_directory(REPO)

    def test_laboratory_root_discovers_only_portable_modules(self):
        self.check_directory(LAB)

    def test_tests_directory_discovers_only_portable_modules(self):
        self.check_directory(PORTABLE)


if __name__ == '__main__':
    unittest.main()
