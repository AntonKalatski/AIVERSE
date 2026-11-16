#!/usr/bin/env python3
"""Unit tests for tools/visual/compare.py (PPM/PNG reading, PNG writing, change detection)."""
import importlib.util
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('compare', Path(__file__).with_name('compare.py'))
cmp = importlib.util.module_from_spec(spec); spec.loader.exec_module(cmp)


def ppm(w, h, rgb):
    return f'P6\n# test\n{w} {h}\n255\n'.encode() + rgb


class CompareTest(unittest.TestCase):
    def test_roundtrip_and_diff(self):
        w, h = 4, 3
        a = bytes(range(36))
        b = bytearray(a); b[15:18] = b'\xff\xff\xff'   # pixel 5 (x=1, y=1)
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            (p / 'a.ppm').write_bytes(ppm(w, h, a))
            cmp.write_png(p / 'b.png', w, h, bytes(b))
            self.assertEqual(cmp.read_image(p / 'b.png'), (w, h, bytes(b)))
            self.assertEqual(cmp.read_image(p / 'a.ppm'), (w, h, a))
            stats, diff = cmp.compare(cmp.read_image(p / 'a.ppm'), cmp.read_image(p / 'b.png'), 16)
            self.assertEqual((stats['changed'], stats['box']), (1, [1, 1, 1, 1]))
            self.assertEqual(diff[15:18], b'\xff\x00\x00')
            self.assertEqual(cmp.main([str(p / 'a.ppm'), str(p / 'a.ppm')]), 0)
            self.assertEqual(cmp.main([str(p / 'a.ppm'), str(p / 'b.png'), '--diff', str(p / 'd.png')]), 1)
            self.assertTrue((p / 'd.png').exists())
            self.assertEqual(cmp.main([str(p / 'a.ppm'), str(p / 'b.png'), '--max-ratio', '0.1']), 0)

    def test_size_mismatch(self):
        with self.assertRaises(ValueError):
            cmp.compare((1, 1, b'\0\0\0'), (2, 1, b'\0' * 6), 0)


if __name__ == '__main__':
    unittest.main()
