#!/usr/bin/env python3
"""Hermetic tests for the actual binary PPM review code."""
import json
from pathlib import Path
import tempfile
import unittest
from review_frames import NAMES, analyze, load_ppm, write_ppm


def uniform_frame(w, h, rgb):
    return bytes(rgb) * (w * h)


class ReviewFramesTests(unittest.TestCase):
    def test_binary_rgb_starting_with_whitespace_byte_is_preserved(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "first.ppm"
            pix = b" \x00\xff" + b"\x01\x02\x03" * 3
            write_ppm(p, 2, 2, pix)
            self.assertEqual(load_ppm(p), (2, 2, pix))

    def test_rejects_short_frame(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "broken.ppm"
            p.write_bytes(b"P6\n2 2\n255\n\x00\x01")
            with self.assertRaisesRegex(ValueError, "expected 12 RGB bytes"):
                load_ppm(p)

    def test_generates_report_and_contact_sheet_from_real_inputs(self):
        with tempfile.TemporaryDirectory() as d:
            prefix = str(Path(d) / "spectrafx")
            w = h = 32
            base = uniform_frame(w, h, (0, 0, 0))
            fx_a = bytearray(base)
            fx_b = bytearray(base)
            for y in range(10, 22):
                for x in range(16, 27):
                    fx_a[(y * w + x) * 3] = 180
                for x in range(9, 20):
                    fx_b[(y * w + x) * 3] = 180
            sources = {
                "baseline-a": base, "effect-a": bytes(fx_a),
                "baseline-b": base, "effect-b": bytes(fx_b),
                "after-reset-a": base, "prisma-baseline-a": base,
                "prisma-effect-a": bytes(fx_a),
            }
            for suffix, px in sources.items():
                write_ppm(f"{prefix}-{suffix}.ppm", w, h, px)
            output = str(Path(d) / "report")
            self.assertEqual(analyze(prefix, output, strict=False), 0)
            report = json.loads(Path(output + ".json").read_text())
            self.assertEqual(len(NAMES), 7)
            self.assertTrue(report["checks"]["camera_changes_screen_position"])
            self.assertEqual(report["metrics"]["reset"]["maximum_absolute_red_delta"], 0)
            sheet = load_ppm(output + ".ppm")
            self.assertEqual(sheet[:2], (4 * w + 5 * 4, 3 * h + 4 * 4))

    def test_missing_capture_is_not_faked(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(FileNotFoundError):
                analyze(str(Path(d)/"missing"), str(Path(d)/"out"))


if __name__ == "__main__":
    unittest.main()
