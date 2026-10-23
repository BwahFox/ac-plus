#!/usr/bin/env python3
"""City Folk's character animations (files/Anm/Anm.brcha, CHR0) -> per-frame bone poses.

All of City Folk's people share one skeleton and one animation archive, so an animation made for
the player (cafe_drink1) or for Brewster (pge_pour1) is a list of bone names, each with scale,
rotation (degrees, X then Y then Z) and translation over the frames.

read_chr0(b, off) -> {'frames': n, 'loop': bool, 'bones': {name: sample(frame) -> (s, r, t)}}
"""
import math
import struct

from extract_cf_sky import brres_folders, u16, u32, s32


def f32(b, o):
    return struct.unpack_from('>f', b, o)[0]


def hermite(f0, v0, t0, f1, v1, t1, f):
    span = f1 - f0
    if span <= 0:
        return v0
    t = (f - f0) / span
    t2, t3 = t * t, t * t * t
    return ((2 * t3 - 3 * t2 + 1) * v0 + (t3 - 2 * t2 + t) * span * t0 +
            (-2 * t3 + 3 * t2) * v1 + (t3 - t2) * span * t1)


class Keys:
    """Hermite keys (frame, value, tangent)."""

    def __init__(self, keys):
        self.keys = keys

    def __call__(self, f):
        k = self.keys
        if f <= k[0][0]:
            return k[0][1]
        if f >= k[-1][0]:
            return k[-1][1]
        for i in range(len(k) - 1):
            if k[i][0] <= f <= k[i + 1][0]:
                # a repeated frame is a step: take the later key's value from there on
                return hermite(*k[i], *k[i + 1], f)
        return k[-1][1]


class Linear:
    def __init__(self, values):
        self.values = values

    def __call__(self, f):
        v = self.values
        i = int(math.floor(f))
        if i < 0:
            return v[0]
        if i >= len(v) - 1:
            return v[-1]
        a = f - i
        return v[i] * (1 - a) + v[i + 1] * a


class Const:
    def __init__(self, v):
        self.v = v

    def __call__(self, f):
        return self.v


def read_track(b, o, fmt, frames):
    if fmt == 1:  # interpolated, 4-byte keys
        n = u16(b, o)
        step, base = f32(b, o + 8), f32(b, o + 12)
        keys = []
        for i in range(n):
            d = u32(b, o + 16 + 4 * i)
            tan = ((d & 0xFFF) ^ 0x800) - 0x800
            keys.append((float(d >> 24), base + step * (d >> 12 & 0xFFF), tan / 32.0))
        return Keys(keys)
    if fmt == 2:  # interpolated, 6-byte keys
        n = u16(b, o)
        step, base = f32(b, o + 8), f32(b, o + 12)
        keys = []
        for i in range(n):
            e = o + 16 + 6 * i
            fr, val = u16(b, e), u16(b, e + 2)
            tan = struct.unpack_from('>h', b, e + 4)[0]
            keys.append((fr / 32.0, base + step * val, tan / 256.0))
        return Keys(keys)
    if fmt == 3:  # interpolated, 12-byte keys
        n = u16(b, o)
        keys = [struct.unpack_from('>3f', b, o + 8 + 12 * i) for i in range(n)]
        return Keys([tuple(k) for k in keys])
    if fmt == 4:  # one byte a frame
        step, base = f32(b, o), f32(b, o + 4)
        return Linear([base + step * b[o + 8 + i] for i in range(frames)])
    if fmt == 5:  # two bytes a frame
        step, base = f32(b, o), f32(b, o + 4)
        return Linear([base + step * u16(b, o + 8 + 2 * i) for i in range(frames)])
    if fmt == 6:  # a float a frame
        return Linear([f32(b, o + 4 * i) for i in range(frames)])
    raise ValueError('CHR0 track format %d' % fmt)


def read_chr0(b, off):
    if b[off:off + 4] != b'CHR0':
        raise ValueError('not CHR0')
    version = u32(b, off + 8)
    if version != 5:
        raise ValueError('CHR0 version %d' % version)
    group = off + s32(b, off + 0x10)
    frames = u16(b, off + 0x20)
    loop = u32(b, off + 0x24) != 0
    bones = {}
    for i in range(1, u32(b, group + 4) + 1):
        e = group + 8 + 16 * i
        bo = group + s32(b, e + 12)
        name = b[bo + s32(b, bo):].split(b'\0', 1)[0].decode()
        code = u32(b, bo + 4)
        p = bo + 8
        tracks = []
        # scale, rotation, translation: present?, isotropic?, per-axis fixed bits, format
        for has_bit, iso_bit, fixed0, fmt_shift, fmt_mask, default in (
                (22, 4, 13, 25, 3, 1.0), (23, 5, 16, 27, 7, 0.0), (24, 6, 19, 30, 3, 0.0)):
            if not code >> has_bit & 1:
                tracks.append([Const(default)] * 3)
                continue
            fmt = code >> fmt_shift & fmt_mask
            axes = 1 if code >> iso_bit & 1 else 3
            got = []
            for a in range(axes):
                if code >> (fixed0 + a) & 1:
                    got.append(Const(f32(b, p)))
                else:
                    got.append(read_track(b, bo + s32(b, p), fmt, frames + 1))
                p += 4
            tracks.append(got * 3 if axes == 1 else got)
        bones[name] = tracks
    return {'frames': frames, 'loop': loop, 'bones': bones}


def sample(anim, name, f):
    """(scale, rotation in degrees, translation) of bone name at frame f, or None."""
    tr = anim['bones'].get(name)
    if tr is None:
        return None
    return tuple(tuple(c(f) for c in group) for group in tr)


def anims(path):
    b = open(path, 'rb').read()
    return b, brres_folders(b).get('AnmChr(NW4R)', {})


if __name__ == '__main__':
    import sys
    b, table = anims(sys.argv[1])
    for name in sys.argv[2:]:
        a = read_chr0(b, table[name])
        print(name, 'frames', a['frames'], 'loop', a['loop'])
        for bn in a['bones']:
            print('  %-10s' % bn, ' '.join('(%s)' % ' '.join('%7.2f' % x for x in g)
                                           for g in sample(a, bn, 0)))
