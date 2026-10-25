#!/usr/bin/env python3
"""City Folk's TV programs for the TVs in the houses (pc/src/pc_tv.c), from the City Folk files:
Ftr/tv_program*.brres (each a few 32x32 CI4 pictures and a PAT0 that cuts between them) and the
week's schedule in rels/d_ftr_managerNP.rel (dFtrm_c::tvPlayer_c).

Usage: cf_tv.py <City Folk files folder> <out.cftv>     (extract_cf_sky.py calls write_tv)

.cftv, little-endian:
  "CFTV", u32 version, u16 program count, u16 pad
  per program: u16 loop frames (60 a second), u16 pictures, u16 cuts, u16 pad,
               pictures x 32x32 RGB5A3 in GameCube 4x4 tiles, big-endian (2048 bytes each),
               cuts x { u16 frame, u16 picture }
  the week: 7 days from Sunday, each u16 count, count x { s16 program, u8 hour, u8 minute }
Programs 0-10 are City Folk's own numbers (1 = the US colour bars); 11-21 are the weather report with
the forecast panel for WEATHER_KINDS, in that order, City Folk's program 255.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from cf_rooms import decode_texture, palette_any  # noqa: E402
from extract_cf_sky import brres_folders, s32, u16, u32  # noqa: E402

VERSION = 1
SIZE = 32
PROGRAMS = 11
WEATHER = 255
WEATHER_KINDS = ['ff', 'cc', 'rr', 'fc', 'cf', 'fr', 'cr', 'rc', 'ss', 'cs', 'sc']  # f fine, c cloudy, r rain, s snow
SCHEDULE_SECTION, SCHEDULE_OFFSET = 4, 0x490  # in d_ftr_managerNP.rel: 7 x { list pointer, count }


def cstr(b, o):
    return b[o:b.index(b'\0', o)].decode()


def pat0(b, o):
    """A PAT0's length, loop flag and its first material's slot 0 cuts: [(frame, texture name, palette name)]."""
    if b[o:o + 4] != b'PAT0' or u32(b, o + 8) != 4:
        raise ValueError('not a version 4 PAT0')
    group, tex_names, plt_names = (o + s32(b, o + off) for off in (0x10, 0x14, 0x18))
    frames, _, ntex, nplt = struct.unpack_from('>HHHH', b, o + 0x30)
    tex = [cstr(b, tex_names + s32(b, tex_names + 4 * i)) for i in range(ntex)]
    plt = [cstr(b, plt_names + s32(b, plt_names + 4 * i)) for i in range(nplt)]
    mat = group + s32(b, group + 8 + 16 + 12)  # the group's first entry
    flags = u32(b, mat + 4)
    if not flags & 1:
        raise ValueError('PAT0 without a slot 0')
    if flags & 2:  # one fixed picture
        keys = [(0.0,) + struct.unpack_from('>HH', b, mat + 8)]
    else:
        at = mat + s32(b, mat + 8)
        keys = [(struct.unpack_from('>f', b, at + 8 + 8 * k)[0],) + struct.unpack_from('>HH', b, at + 12 + 8 * k)
                for k in range(u16(b, at))]
    return frames, [(f, tex[t], plt[p] if flags & 8 else None) for f, t, p in keys]


def rgb5a3_tiled(px):
    """32x32 RGBA bytes -> GameCube RGB5A3, 4x4 tiles, big-endian."""
    out = bytearray()
    for by in range(0, SIZE, 4):
        for bx in range(0, SIZE, 4):
            for y in range(by, by + 4):
                for x in range(bx, bx + 4):
                    r, g, b, a = px[(y * SIZE + x) * 4:(y * SIZE + x) * 4 + 4]
                    if a >= 0xE0:
                        v = 0x8000 | (r >> 3) << 10 | (g >> 3) << 5 | b >> 3
                    else:
                        v = (a >> 5) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | b >> 4
                    out += struct.pack('>H', v)
    return bytes(out)


def program(path):
    """A program file -> (loop frames, [pictures], [(frame, picture)])."""
    b = open(path, 'rb').read()
    folders = brres_folders(b)
    textures = folders['Textures(NW4R)']
    palettes = folders.get('Palettes(NW4R)', {})
    frames, keys = pat0(b, next(iter(folders['AnmTexPat(NW4R)'].values())))
    pictures, index, cuts = [], {}, []
    for frame, tex, plt in keys:
        if (tex, plt) not in index:
            w, h, px = decode_texture(b, textures[tex], palette_any(b, palettes[plt]) if plt else None)
            if (w, h) != (SIZE, SIZE):
                raise ValueError('%s: %s is %dx%d' % (path, tex, w, h))
            index[(tex, plt)] = len(pictures)
            pictures.append(rgb5a3_tiled(px))
        cuts.append((int(round(frame)), index[(tex, plt)]))
    return max(frames, 1), pictures, cuts


def rel_sections_and_relocs(b):
    """A REL's section offsets and its own relocations: {(section, offset): (target section, addend)}."""
    nsec, sinfo, imp, impsz = u32(b, 0xC), u32(b, 0x10), u32(b, 0x28), u32(b, 0x2C)
    module = u32(b, 0)
    sections = [u32(b, sinfo + 8 * i) & ~1 for i in range(nsec)]
    relocs = {}
    for i in range(impsz // 8):
        if u32(b, imp + 8 * i) != module:
            continue
        o, sec, off = u32(b, imp + 8 * i + 4), 0, 0
        while True:
            delta, kind, tsec, addend = struct.unpack_from('>HBBI', b, o)
            o += 8
            off += delta
            if kind == 203:  # R_DOLPHIN_END
                break
            if kind == 202:  # R_DOLPHIN_SECTION
                sec, off = tsec, 0
            elif kind == 1:  # R_PPC_ADDR32
                relocs[(sec, off)] = (tsec, addend)
    return sections, relocs


def schedule(rel_path):
    """The week from Sunday: [[(program, hour, minute)]]."""
    b = open(rel_path, 'rb').read()
    sections, relocs = rel_sections_and_relocs(b)
    days = []
    for d in range(7):
        at = SCHEDULE_OFFSET + 8 * d
        tsec, addend = relocs[(SCHEDULE_SECTION, at)]
        count = u32(b, sections[SCHEDULE_SECTION] + at + 4)
        days.append([struct.unpack_from('>hBB', b, sections[tsec] + addend + 4 * i) for i in range(count)])
    return days


def write_tv(root, out_path):
    ftr = os.path.join(root, 'Ftr')
    files = ['tv_program1_US.brres' if n == 1 else 'tv_program%d.brres' % n for n in range(PROGRAMS)]
    files += ['tv_program_%s.brres' % k for k in WEATHER_KINDS]
    progs = [program(os.path.join(ftr, f)) for f in files]
    week = schedule(os.path.join(root, 'rels', 'd_ftr_managerNP.rel'))
    for day in week:
        for prog, hour, minute in day:
            if not (0 <= prog < PROGRAMS or prog == WEATHER) or hour > 23 or minute > 59:
                raise ValueError('unexpected schedule entry %d %02d:%02d' % (prog, hour, minute))

    out = bytearray(b'CFTV' + struct.pack('<IHH', VERSION, len(progs), 0))
    for frames, pictures, cuts in progs:
        out += struct.pack('<HHHH', frames, len(pictures), len(cuts), 0)
        out += b''.join(pictures)
        out += b''.join(struct.pack('<HH', f, p) for f, p in cuts)
    for day in week:
        out += struct.pack('<H', len(day)) + b''.join(struct.pack('<hBB', *e) for e in day)
    os.makedirs(os.path.dirname(out_path) or '.', exist_ok=True)
    with open(out_path, 'wb') as f:
        f.write(out)
    print('wrote %s (%d programs, %d bytes)' % (out_path, len(progs), len(out)))
    return week


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    write_tv(sys.argv[1], sys.argv[2])
