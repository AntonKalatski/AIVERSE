"""CPU-only tests for the standard-library virtual texture page packer."""
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[2] / "tools" / "virtual_texture_pack.py"
spec = importlib.util.spec_from_file_location("aiverse_vt_pack", str(SCRIPT))
packer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packer)


class VirtualTexturePackTests(unittest.TestCase):
    def test_multi_layer_mips_and_gutters(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = []
            for layer in (0, 1):
                pixels = bytearray()
                for y in range(4):
                    for x in range(4):
                        pixels.extend((x * 30 + layer, y * 40, layer * 100, 255))
                name = root / ("layer{}.rgba".format(layer))
                name.write_bytes(pixels)
                files.append(name)
            out = root / "pages"
            manifest = packer.pack_pages(files, 4, 4, 4, 1, out)
            self.assertEqual(manifest["schema"], "aiverse.vt.disk-pages.v1")
            self.assertEqual(manifest["tilesX"], 2)
            self.assertEqual(manifest["tilesY"], 2)
            self.assertEqual(manifest["mipLevels"], 2)
            self.assertEqual(manifest["virtualLayers"], 2)
            self.assertEqual(len(manifest["sourceSha256"]), 2)
            self.assertEqual(json.loads((out / "manifest.json").read_text()),
                             manifest)
            self.assertEqual(len(list(out.rglob("*.rgba"))), 10)
            page = (out / "m0/l0/x0_y0.rgba").read_bytes()
            self.assertEqual(len(page), 4 * 4 * 4)
            self.assertEqual(tuple(page[:4]), (0, 0, 0, 255))
            self.assertEqual(tuple(page[4:8]), (0, 0, 0, 255))
            self.assertEqual(tuple(page[8:12]), (30, 0, 0, 255))
            self.assertEqual(tuple(page[4 * (2 * 4 + 2):4 * (2 * 4 + 3)]),
                             (30, 40, 0, 255))
            page2 = (out / "m0/l1/x1_y1.rgba").read_bytes()
            self.assertEqual(tuple(page2[-4:]), (91, 120, 100, 255))
            mip = (out / "m1/l0/x0_y0.rgba").read_bytes()
            # Mip1 texel (0,0) averages source (0,0),(1,0),(0,1),(1,1).
            self.assertEqual(tuple(mip[(4 + 1) * 4:(4 + 2) * 4]),
                             (15, 20, 0, 255))
            self.assertEqual(manifest["physicalPageSize"], 4)

    def test_invalid_sources_are_transactional(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            short = root / "short.rgba"
            short.write_bytes(bytes(63))
            target = root / "pages"
            with self.assertRaises(ValueError):
                packer.pack_pages([short], 4, 4, 4, 1, target)
            self.assertFalse(target.exists())
            self.assertFalse(list(root.glob(".aiverse-vt-pack-*")))
            with self.assertRaises(ValueError):
                packer.pack_pages([short], 4, 4, 4, 2, target)
            with self.assertRaises(ValueError):
                packer.pack_pages([short], 4, 4, 4, 1, target, max_pixels=8)
            target.mkdir()
            with self.assertRaises(FileExistsError):
                packer.pack_pages([short], 4, 4, 4, 1, target)

    def test_odd_dimensions_and_determinism(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            src = root / "raw.rgba"
            src.write_bytes(bytes([17, 36, 92, 255] * (7 * 5)))
            a = root / "out-a"
            b = root / "out-b"
            info_a = packer.pack_pages([src], 7, 5, 6, 1, a)
            info_b = packer.pack_pages([src], 7, 5, 6, 1, b)
            self.assertEqual(info_a, info_b)
            self.assertEqual(info_a["tilesX"], 2)
            self.assertEqual(info_a["tilesY"], 2)
            self.assertEqual(info_a["mipLevels"], 2)
            entries_a = {p.relative_to(a): p.read_bytes()
                         for p in a.rglob("*") if p.is_file()}
            entries_b = {p.relative_to(b): p.read_bytes()
                         for p in b.rglob("*") if p.is_file()}
            self.assertEqual(entries_a, entries_b)


if __name__ == "__main__":
    unittest.main()
