#!/usr/bin/env python3
"""Cavedog HPI archives (Total Annihilation and its expansions: .hpi, .ufo, .ccx, .gp3).

Layout (all little-endian): a 20-byte header ('HAPI', version 0x00010000, directory size, header key, directory
start). A non-zero header key turns on a byte cipher keyed by the absolute file position
(plain = (pos ^ key) ^ ~byte, key = ~((k * 4) | (k >> 6)) & 0xFF). The directory is a tree of (count, list offset)
headers with 9-byte entries (name offset, data offset, flag 1 = directory). A file record holds (data offset, size,
compression 0 none / 1 LZ77 / 2 zlib); compressed files are split into 64 KiB 'SQSH' chunks after a table of chunk
sizes, each chunk optionally scrambled ((b - i) ^ i) and checked by a byte sum. Saved-game 'BANK' archives are not
supported. Bytes are kept as they are (3DO, GAF, FBI, TDF, TNT ... are read by tools/importers/ta)."""
import os
import struct
import sys
import zlib

sys.dont_write_bytecode = True  # keep the plugin folder clean
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from aiverseunpack import run, Entry  # noqa: E402

MARKER, VERSION, SQSH = 0x49504148, 0x00010000, 0x48535153


class Hpi:
    def __init__(self, data):
        if len(data) < 20:
            raise ValueError('not an HPI archive: too short')
        marker, version, dsize, hkey, start = struct.unpack_from('<5I', data, 0)
        if marker != MARKER or version != VERSION:
            raise ValueError('not an HPI archive (or a saved-game BANK archive)')
        if not (20 <= start <= dsize <= len(data)):
            raise ValueError('HPI: bad directory bounds')
        self.data = data
        self.key = (~((hkey * 4) | (hkey >> 6))) & 0xFF if hkey else 0
        self.dir = bytearray(data[:dsize])
        self.dir[start:dsize] = self.read(start, dsize - start)
        self.root = start

    def read(self, pos, n):
        if pos < 0 or n < 0 or pos + n > len(self.data):
            raise ValueError('HPI: read past the end of the archive')
        b = self.data[pos:pos + n]
        if not self.key:
            return bytes(b)
        k = self.key
        return bytes(((((pos + i) ^ k) & 0xFF) ^ (~c & 0xFF)) for i, c in enumerate(b))

    def _u32(self, off):
        if off < 0 or off + 4 > len(self.dir):
            raise ValueError('HPI: directory offset out of range')
        return struct.unpack_from('<I', self.dir, off)[0]

    def _name(self, off):
        end = self.dir.find(b'\0', off)
        if off < 0 or end < 0:
            raise ValueError('HPI: bad name offset')
        return self.dir[off:end].decode('latin-1')

    def files(self):
        """[(path, data offset, size, compression)] in directory order."""
        out, stack, seen = [], [(self.root, '')], set()
        while stack:
            off, prefix = stack.pop(0)
            if off in seen:
                raise ValueError('HPI: directory loop')
            seen.add(off)
            count, lst = self._u32(off), self._u32(off + 4)
            if count > 100000:
                raise ValueError('HPI: implausible directory size')
            for i in range(count):
                e = lst + i * 9
                if e + 9 > len(self.dir):
                    raise ValueError('HPI: entry out of range')
                name_off, data_off = struct.unpack_from('<2I', self.dir, e)
                flag = self.dir[e + 8]
                path = prefix + self._name(name_off)
                if flag == 1:
                    stack.append((data_off, path + '/'))
                else:
                    if data_off + 9 > len(self.dir):
                        raise ValueError('HPI: file record out of range')
                    doff, size = struct.unpack_from('<2I', self.dir, data_off)
                    out.append((path, doff, size, self.dir[data_off + 8]))
        return out

    def extract(self, doff, size, comp):
        if comp == 0:
            return self.read(doff, size)
        if comp not in (1, 2):
            raise ValueError('HPI: unknown compression %d' % comp)
        chunks = (size + 65535) // 65536
        sizes = struct.unpack('<%dI' % chunks, self.read(doff, 4 * chunks))
        pos, out = doff + 4 * chunks, bytearray()
        for csize in sizes:
            out += self.chunk(self.read(pos, csize))
            pos += csize
        if len(out) != size:
            raise ValueError('HPI: file size mismatch (%d, expected %d)' % (len(out), size))
        return bytes(out)

    @staticmethod
    def chunk(b):
        if len(b) < 19:
            raise ValueError('HPI: truncated chunk')
        marker, _, method, scrambled, csize, dsize, checksum = struct.unpack_from('<IBBBIII', b, 0)
        if marker != SQSH:
            raise ValueError('HPI: bad chunk marker')
        body = bytearray(b[19:19 + csize])
        if len(body) != csize:
            raise ValueError('HPI: truncated chunk data')
        if (sum(body) & 0xFFFFFFFF) != checksum:
            raise ValueError('HPI: chunk checksum mismatch')
        if scrambled:
            for i in range(csize):
                body[i] = ((body[i] - i) ^ i) & 0xFF
        if method == 1:
            data = lz77(bytes(body))
        elif method == 2:
            data = zlib.decompress(bytes(body))
        else:
            raise ValueError('HPI: unknown chunk compression %d' % method)
        if len(data) != dsize:
            raise ValueError('HPI: chunk size mismatch')
        return data


def lz77(src):
    """Cavedog LZ77: a flag byte per 8 items; a clear bit = literal byte, a set bit = u16 (window offset << 4 |
    length - 2) into a 4096-byte window that starts writing at 1; offset 0 ends the stream."""
    out, win, w, i = bytearray(), bytearray(4096), 1, 0
    n = len(src)
    if n == 0:
        return b''
    flags, bit = src[0], 1
    i = 1
    while i < n:
        if not (flags & bit):
            c = src[i]
            i += 1
            out.append(c)
            win[w] = c
            w = (w + 1) & 0xFFF
        else:
            if i + 2 > n:
                break
            v = src[i] | (src[i + 1] << 8)
            i += 2
            p = v >> 4
            if p == 0:
                break
            for _ in range((v & 0xF) + 2):
                c = win[p]
                out.append(c)
                win[w] = c
                p = (p + 1) & 0xFFF
                w = (w + 1) & 0xFFF
        bit <<= 1
        if bit & 0x100:
            bit = 1
            if i >= n:
                break
            flags = src[i]
            i += 1
    return bytes(out)


def probe(path):
    with open(path, 'rb') as f:
        head = f.read(8)
    return len(head) == 8 and struct.unpack('<2I', head) == (MARKER, VERSION)


def entries(path):
    with open(path, 'rb') as f:
        h = Hpi(f.read())
    for name, doff, size, comp in h.files():
        yield Entry(name, size, lambda a=(doff, size, comp): h.extract(*a))


if __name__ == '__main__':
    run('hpi', '1.0.0', probe, entries)
