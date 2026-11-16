"""Total Annihilation .tnt maps: header, 32 x 32 px tiles, per-16 px cell height + feature attributes, feature names,
sea level and the minimap. Only what an importer needs: heights, features, sea level, size.

Header (little-endian u32 x 16): id 0x2000, width and height in 16 px cells, offsets of the tile map, the cell
attributes, the tile graphics, tile count, feature count, offset of the feature names, sea level, offset of the
minimap, then padding. Attributes: 4 bytes per cell (height u8, feature u16, pad u8); feature 0xFFFF = none,
0xFFFC / 0xFFFE = void / part of a feature. Feature names: (u32 index, 128-byte name)."""
import struct

NONE = (0xFFFF, 0xFFFC, 0xFFFE)


class Tnt:
    def __init__(self, data):
        if len(data) < 64:
            raise ValueError('TNT: too short')
        (ident, w, h, _map, attr, _gfx, _tiles, nfeat, featnames, sea, _mini) = struct.unpack_from('<11I', data, 0)
        if ident != 0x2000:
            raise ValueError('TNT: not a Total Annihilation map (id %#x)' % ident)
        if not (0 < w <= 4096 and 0 < h <= 4096) or attr + 4 * w * h > len(data):
            raise ValueError('TNT: bad size or attribute table')
        self.w, self.h, self.sea = w, h, sea
        self.heights = [0] * (w * h)
        self.features = []          # (x, y, feature index)
        for i in range(w * h):
            ht, feat = struct.unpack_from('<BH', data, attr + 4 * i)
            self.heights[i] = ht
            if feat not in NONE:
                self.features.append((i % w, i // w, feat))
        self.names = {}
        for k in range(nfeat):
            off = featnames + k * 132
            if off + 132 > len(data):
                raise ValueError('TNT: feature table out of range')
            idx = struct.unpack_from('<I', data, off)[0]
            self.names[idx] = data[off + 4:off + 132].split(b'\0', 1)[0].decode('latin-1')

    def height(self, x, y):
        x = min(max(x, 0), self.w - 1)
        y = min(max(y, 0), self.h - 1)
        return self.heights[y * self.w + x]


def write(w, h, heights, features=(), names=(), sea=0):
    """A minimal .tnt (no tile graphics): for tests and for tools that make maps."""
    head = 64
    attr = head
    feat = attr + 4 * w * h
    body = bytearray()
    fmap = {(x, y): f for x, y, f in features}
    for i in range(w * h):
        body += struct.pack('<BHB', heights[i], fmap.get((i % w, i // w), 0xFFFF), 0)
    names_blob = bytearray()
    for i, n in enumerate(names):
        names_blob += struct.pack('<I', i) + n.encode('latin-1')[:127].ljust(128, b'\0')
    hdr = struct.pack('<11I', 0x2000, w, h, 0, attr, 0, 0, len(names), feat, sea, 0).ljust(head, b'\0')
    return bytes(hdr + body + names_blob)
