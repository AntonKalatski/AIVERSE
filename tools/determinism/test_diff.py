#!/usr/bin/env python3
"""Unit tests for tools/determinism/diff.py (log comparison, state byte diff; no engine binary needed)."""
import importlib.util
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('diff', Path(__file__).with_name('diff.py'))
diff = importlib.util.module_from_spec(spec); spec.loader.exec_module(diff)


class DiffTest(unittest.TestCase):
    def test_divergence(self):
        a = [(20, 'aa'), (40, 'bb'), (60, 'cc')]
        self.assertEqual(diff.first_divergence(a, list(a)), (60, None, None))
        self.assertEqual(diff.first_divergence(a, [(20, 'aa'), (40, 'xx'), (60, 'cc')]), (20, 40, 'checksum'))
        self.assertEqual(diff.first_divergence(a, a[:2]), (40, 40, 'length'))

    def test_logs_and_bytes(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            (p / 'a').write_text('# tickRate 30\n20 00000000000000aa\n40 00000000000000bb\n')
            (p / 'b').write_text('# tickRate 30\n20 00000000000000aa\n40 00000000000000cc\n')
            rate, rows = diff.read_log(p / 'a')
            self.assertEqual((rate, rows[1]), (30, (40, '00000000000000bb')))
            self.assertEqual(diff.main(['logs', str(p / 'a'), str(p / 'a')]), 0)
            self.assertEqual(diff.main(['logs', str(p / 'a'), str(p / 'b')]), 1)
            (p / 's1').write_bytes(b'abcdef'); (p / 's2').write_bytes(b'abXdef')
            self.assertEqual(diff.first_byte(p / 's1', p / 's2'), (2, 6, 6))
            self.assertEqual(diff.first_byte(p / 's1', p / 's1')[0], None)


if __name__ == '__main__':
    unittest.main()
