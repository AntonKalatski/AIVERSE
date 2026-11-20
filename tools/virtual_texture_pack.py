#!/usr/bin/env python3
"""Pack raw RGBA8 virtual-texture layers into guttered, mipmapped disk pages.

No third-party dependencies. Source pixels are row-major RGBA8, top to bottom.
The output layout matches gfx::VirtualTextureDiskQueue::relativePath:
    <root>/m<MIP>/l<LAYER>/x<X>_y<Y>.rgba
Physical pages include configurable per-axis Clamp/Repeat/MirrorRepeat gutters;
image mips use deterministic 2x2 RGBA8 box filtering with the same addressing.
The default Clamp mode keeps the existing page and manifest format unchanged.
Not GPU feedback/sparse texturing.
"""
import argparse
import hashlib
import json
import shutil
import tempfile
from pathlib import Path


def address_texel(index, extent, mode):
    """Map a texel coordinate using the chosen source image address mode."""
    if mode == "repeat":
        return index % extent
    if mode == "mirror":
        # MirrorRepeat duplicates the edge texel at the turn.
        folded = index % (2 * extent)
        return min(folded, 2 * extent - 1 - folded)
    return min(extent - 1, max(0, index))


def downsample_rgba(image, width, height, address_u="clamp", address_v="clamp"):
    """Deterministic per-axis-addressed 2x2 filter (including alpha)."""
    new_width = max(1, (width + 1) // 2)
    new_height = max(1, (height + 1) // 2)
    result = bytearray(new_width * new_height * 4)
    for y in range(new_height):
        for x in range(new_width):
            total = [0, 0, 0, 0]
            for oy in (0, 1):
                sy = address_texel(y * 2 + oy, height, address_v)
                for ox in (0, 1):
                    sx = address_texel(x * 2 + ox, width, address_u)
                    pos = (sy * width + sx) * 4
                    for c in range(4):
                        total[c] += image[pos + c]
            target = (y * new_width + x) * 4
            for c in range(4):
                result[target + c] = (total[c] + 2) // 4
    return bytes(result), new_width, new_height


def make_page(image, width, height, tile_x, tile_y, page_size, gutter,
              address_u="clamp", address_v="clamp"):
    interior = page_size - 2 * gutter
    result = bytearray(page_size * page_size * 4)
    for y in range(page_size):
        sy = address_texel(tile_y * interior + y - gutter, height, address_v)
        for x in range(page_size):
            sx = address_texel(tile_x * interior + x - gutter, width, address_u)
            src = (sy * width + sx) * 4
            dst = (y * page_size + x) * 4
            result[dst:dst + 4] = image[src:src + 4]
    return result


def pack_pages(inputs, width, height, page_size, gutter, destination,
               max_pixels=32 * 1024 * 1024,
               address_u="clamp", address_v="clamp"):
    """Return manifest; fail transactionally without overwriting a destination."""
    inputs = [Path(path) for path in inputs]
    destination = Path(destination)
    if not inputs or len(inputs) > 65536:
        raise ValueError("expected 1..65536 input RGBA8 layers")
    if width < 1 or height < 1 or width > 1 << 20 or height > 1 << 20:
        raise ValueError("invalid input pixel dimensions")
    if width > max_pixels // height:
        raise ValueError("RGBA8 source exceeds pixel budget")
    if address_u not in ("clamp", "repeat", "mirror") or address_v not in ("clamp", "repeat", "mirror"):
        raise ValueError("texture address modes must be clamp, repeat or mirror")
    if page_size < 4 or page_size > 4096 or gutter < 0 or gutter * 2 >= page_size:
        raise ValueError("physical page size or gutter invalid")
    if page_size * page_size * 4 > 512 * 1024 * 1024:
        raise ValueError("physical tile exceeds staging budget")
    if destination.exists():
        raise FileExistsError("output directory already exists")
    interior = page_size - 2 * gutter
    tiles_x = (width + interior - 1) // interior
    tiles_y = (height + interior - 1) // interior
    max_tiles = max(tiles_x, tiles_y)
    mip_levels = (max_tiles - 1).bit_length() + 1
    manifest = {
        "schema": "aiverse.vt.disk-pages.v1",
        "format": "RGBA8",
        "width": width, "height": height,
        "tilesX": tiles_x, "tilesY": tiles_y,
        "mipLevels": mip_levels,
        "virtualLayers": len(inputs),
        "physicalPageSize": page_size,
        "gutter": gutter,
        "interiorPageSize": interior,
        "sourceSha256": [],
    }
    # Opt-in fields: no change to the manifest for legacy clamp-only assets.
    if address_u != "clamp" or address_v != "clamp":
        manifest["addressU"] = address_u
        manifest["addressV"] = address_v
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".aiverse-vt-pack-",
                                     dir=str(destination.parent)))
    try:
        for layer, src_path in enumerate(inputs):
            expected = width * height * 4
            if src_path.stat().st_size != expected:
                raise ValueError("input layer {} size mismatch: expected {} bytes".format(
                    layer, expected))
            image = src_path.read_bytes()
            manifest["sourceSha256"].append(hashlib.sha256(image).hexdigest())
            mip_w, mip_h = width, height
            for mip in range(mip_levels):
                count_x = (mip_w + interior - 1) // interior
                count_y = (mip_h + interior - 1) // interior
                mip_dir = staging / ("m{}".format(mip)) / ("l{}".format(layer))
                mip_dir.mkdir(parents=True, exist_ok=True)
                for y in range(count_y):
                    for x in range(count_x):
                        page = make_page(image, mip_w, mip_h, x, y, page_size,
                                         gutter, address_u, address_v)
                        (mip_dir / ("x{}_y{}.rgba".format(x, y))).write_bytes(page)
                if mip + 1 < mip_levels:
                    image, mip_w, mip_h = downsample_rgba(
                        image, mip_w, mip_h, address_u, address_v)
        (staging / "manifest.json").write_text(
            json.dumps(manifest, sort_keys=True, indent=2) + "\n",
            encoding="utf-8")
        staging.rename(destination)
        return manifest
    except BaseException:
        shutil.rmtree(staging, ignore_errors=True)
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--layer", dest="layers", type=Path, action="append",
                        required=True, help="packed RGBA8 layer file; repeat for arrays")
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument("--page-size", type=int, default=128)
    parser.add_argument("--gutter", type=int, default=2)
    parser.add_argument("--address-u", choices=("clamp", "repeat", "mirror"),
                        default="clamp", help="U-axis virtual texture address mode")
    parser.add_argument("--address-v", choices=("clamp", "repeat", "mirror"),
                        default="clamp", help="V-axis virtual texture address mode")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    info = pack_pages(args.layers, args.width, args.height, args.page_size,
                      args.gutter, args.output,
                      address_u=args.address_u, address_v=args.address_v)
    print(json.dumps(info, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
