#!/usr/bin/env python3
"""Read files from the player's GameCube disc image (ISO/GCM/CISO), as pc/src/pc_disc.c does.

find_disc(game_dir) looks where the game does (., orig, rom); read_file(disc, name) returns a
file's bytes, Yaz0-decompressed if it's an .szs.
"""
import os
import struct

CISO_HDR_SIZE = 0x8000


class Disc:
    def __init__(self, path):
        self.f = open(path, 'rb')
        hdr = self.f.read(CISO_HDR_SIZE)
        self.block_phys = None
        if hdr[:4] == b'CISO':
            self.block_size = struct.unpack_from('<I', hdr, 4)[0]
            phys, self.block_phys = 0, []
            for present in hdr[8:]:
                self.block_phys.append(phys if present else -1)
                phys += 1 if present else 0

    def read(self, offset, size):
        if self.block_phys is None:
            self.f.seek(offset)
            return self.f.read(size)
        out = bytearray()
        while size > 0:
            bi, bo = divmod(offset, self.block_size)
            chunk = min(self.block_size - bo, size)
            if bi >= len(self.block_phys) or self.block_phys[bi] < 0:
                out += bytes(chunk)
            else:
                self.f.seek(CISO_HDR_SIZE + self.block_phys[bi] * self.block_size + bo)
                out += self.f.read(chunk)
            offset += chunk
            size -= chunk
        return bytes(out)

    def files(self):
        """{path: (offset, size)} from the FST"""
        fst_off, fst_size = struct.unpack('>II', self.read(0x424, 8))
        fst = self.read(fst_off, fst_size)
        count = struct.unpack_from('>I', fst, 8)[0]
        names = count * 12
        out = {}
        dirs = [(count, '')]  # (end index, path prefix)
        for i in range(1, count):
            while dirs and i >= dirs[-1][0]:
                dirs.pop()
            word, a, b = struct.unpack_from('>III', fst, i * 12)
            end = fst.index(b'\0', names + (word & 0xFFFFFF))
            name = fst[names + (word & 0xFFFFFF):end].decode('ascii', 'replace')
            prefix = dirs[-1][1] if dirs else ''
            if word >> 24:
                dirs.append((b, prefix + name + '/'))
            else:
                out[prefix + name] = (a, b)
        return out


def yaz0(src):
    if src[:4] != b'Yaz0':
        return src
    size = struct.unpack_from('>I', src, 4)[0]
    dst = bytearray(size)
    s, d = 16, 0
    while d < size:
        code = src[s]
        s += 1
        for bit in range(8):
            if d >= size:
                break
            if code & (0x80 >> bit):
                dst[d] = src[s]
                d += 1
                s += 1
            else:
                b1, b2 = src[s], src[s + 1]
                s += 2
                back = d - ((b1 & 15) << 8 | b2) - 1
                n = b1 >> 4
                if n == 0:
                    n = src[s] + 0x12
                    s += 1
                else:
                    n += 2
                for k in range(n):
                    dst[d + k] = dst[back + k]
                d += n
    return bytes(dst)


def find_disc(game_dir):
    for sub in ('.', 'orig', 'rom'):
        d = os.path.join(game_dir, sub)
        if os.path.isdir(d):
            for name in sorted(os.listdir(d)):
                path = os.path.join(d, name)
                if name.lower().endswith('.ciso'):
                    return path
                if name.lower().endswith(('.iso', '.gcm')):
                    with open(path, 'rb') as f:
                        head = f.read(0x20)
                    if head[:3] == b'GAF' and head[0x1C:0x20] == b'\xc2\x33\x9f\x3d':  # the GameCube game
                        return path
    return None


def read_file(disc_path, name):
    disc = Disc(disc_path)
    off, size = disc.files()[name]
    return yaz0(disc.read(off, size))
