#!/usr/bin/env python3
"""Compare two engine screenshots (PPM from AIVERSE_SCREENSHOT, or 8-bit PNG) and write a difference image.

  python3 tools/visual/compare.py before.ppm after.png [--diff diff.png] [--threshold 16] [--max-ratio 0.0]

Reports size, changed pixel ratio (a pixel changed when any channel differs by more than --threshold), mean absolute
channel error and the bounding box of the change. The diff image shows the 'after' frame dimmed with changed pixels
in red. Exit 0 when the changed ratio <= --max-ratio, 1 otherwise, 2 on unreadable input. No third-party packages.
Use it for before/after checks with the same camera, map, seed and frame count (skills/verification/aiverse-visual-qa).
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path


def read_ppm(data):
    parts, i = [], 2
    while len(parts) < 3:
        while data[i:i + 1].isspace():
            i += 1
        if data[i:i + 1] == b'#':
            while data[i:i + 1] not in (b'\n', b''):
                i += 1
            continue
        j = i
        while not data[j:j + 1].isspace():
            j += 1
        parts.append(int(data[i:j])); i = j
    w, h, maxval = parts
    if maxval != 255:
        raise ValueError('only 8-bit PPM is supported')
    pix = data[i + 1:i + 1 + w * h * 3]
    if len(pix) != w * h * 3:
        raise ValueError('truncated PPM')
    return w, h, pix


def read_png(data):
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG')
    i, idat, w = 8, b'', None
    while i < len(data):
        n, kind = struct.unpack('>I4s', data[i:i + 8])
        body = data[i + 8:i + 8 + n]
        if kind == b'IHDR':
            w, h, depth, color, _, _, interlace = struct.unpack('>IIBBBBB', body)
            if depth != 8 or interlace or color not in (0, 2, 6):
                raise ValueError('only 8-bit non-interlaced gray / RGB / RGBA PNG is supported')
        elif kind == b'IDAT':
            idat += body
        elif kind == b'IEND':
            break
        i += 12 + n
    if w is None:
        raise ValueError('PNG without IHDR')
    ch = {0: 1, 2: 3, 6: 4}[color]
    raw, stride = zlib.decompress(idat), w * ch
    out, prev, pos = bytearray(), bytearray(stride), 0
    for _ in range(h):
        f = raw[pos]; line = bytearray(raw[pos + 1:pos + 1 + stride]); pos += 1 + stride
        for x in range(stride):
            a = line[x - ch] if x >= ch else 0
            b = prev[x]
            c = prev[x - ch] if x >= ch else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        for x in range(w):
            px = line[x * ch:x * ch + ch]
            out += bytes((px[0], px[0], px[0])) if ch == 1 else px[:3]
    return w, h, bytes(out)


def read_image(path):
    data = Path(path).read_bytes()
    if data[:2] == b'P6':
        return read_ppm(data)
    return read_png(data)


def write_png(path, w, h, rgb):
    raw = b''.join(b'\x00' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def compare(a, b, threshold):
    (wa, ha, pa), (wb, hb, pb) = a, b
    if (wa, ha) != (wb, hb):
        raise ValueError(f'different sizes: {wa}x{ha} vs {wb}x{hb}')
    changed, total_err = 0, 0
    box = [wa, ha, -1, -1]
    diff = bytearray(len(pb))
    for i in range(0, len(pa), 3):
        d0, d1, d2 = abs(pa[i] - pb[i]), abs(pa[i + 1] - pb[i + 1]), abs(pa[i + 2] - pb[i + 2])
        total_err += d0 + d1 + d2
        if max(d0, d1, d2) > threshold:
            changed += 1
            p = i // 3; x, y = p % wa, p // wa
            box = [min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y)]
            diff[i:i + 3] = b'\xff\x00\x00'
        else:
            diff[i:i + 3] = bytes((pb[i] // 3, pb[i + 1] // 3, pb[i + 2] // 3))
    n = wa * ha
    return dict(width=wa, height=ha, changed=changed, ratio=changed / n if n else 0.0,
                meanError=total_err / (3 * n) if n else 0.0, box=box if changed else None), bytes(diff)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('before'); ap.add_argument('after')
    ap.add_argument('--diff', help='write the difference image (PNG)')
    ap.add_argument('--threshold', type=int, default=16)
    ap.add_argument('--max-ratio', type=float, default=0.0)
    a = ap.parse_args(argv)
    try:
        stats, diff = compare(read_image(a.before), read_image(a.after), a.threshold)
    except (OSError, ValueError, zlib.error, struct.error) as e:
        print(f'error: {e}', file=sys.stderr); return 2
    if a.diff:
        write_png(a.diff, stats['width'], stats['height'], diff)
    box = stats['box']
    where = f" box x={box[0]}..{box[2]} y={box[1]}..{box[3]}" if box else ''
    print(f"{stats['width']}x{stats['height']}: changed {stats['changed']} px ({stats['ratio'] * 100:.3f}%), "
          f"mean error {stats['meanError']:.2f}{where}")
    return 0 if stats['ratio'] <= a.max_ratio else 1


if __name__ == '__main__':
    sys.exit(main())
