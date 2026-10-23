#!/usr/bin/env python3
"""City Folk room models -> .cfroom files for pc/src/pc_cf_room.c.

Part of extract_cf_sky.py: reads a room's BRRES (files/BgData/BgModel/*.brres on the
City Folk disc), and writes its textures and triangles in a simple form the game draws
with its own shader. Only what rooms use is handled: one bone, float or fixed-point
vertex arrays, CI4/CI8/I4/I8/IA4/IA8/RGB565/RGB5A3 textures.

.cfroom, little-endian:
    "CFRM" u32 version (3)
    u32 texture count, then each: name (32 bytes), u16 width, u16 height, RGBA8 pixels
    u32 mesh count, then each: name (32 bytes), i32 texture (-1 none), u8 xlu, u8 cull,
        u8 alpha_test, u8 wrap (s | t << 2: 0 clamp, 1 repeat, 2 mirror), u32 vertex count, then vertices (triangle list):
        f32 x y z, f32 nx ny nz, f32 u v, u8 r g b a
    u8 grid[16][16] (z rows, x columns of 40-unit tiles): 1 floor, 0 wall or furniture
"""
import struct

from extract_cf_sky import brres_folders, brres_group, palette, u16, u32, s32, rgb5a3


def s16(b, o):
    return struct.unpack_from('>h', b, o)[0]


# --- textures ---

def rgb565(v):
    return ((v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31, 255)


def palette_any(b, off):
    fmt, n = u32(b, off + 0x18), u16(b, off + 0x1c)
    data = off + u32(b, off + 0x10)
    if fmt == 0:  # IA8
        return [(b[data + 2 * i + 1],) * 3 + (b[data + 2 * i],) for i in range(n)]
    if fmt == 1:
        return [rgb565(u16(b, data + 2 * i)) for i in range(n)]
    return [rgb5a3(u16(b, data + 2 * i)) for i in range(n)]


BLOCKS = {0: (8, 8, 4), 1: (8, 4, 8), 2: (8, 4, 8), 3: (4, 4, 16), 4: (4, 4, 16), 5: (4, 4, 16), 8: (8, 8, 4), 9: (8, 4, 8)}


def decode_texture(b, off, pal=None):
    """TEX0 at off -> (w, h, bytes RGBA)."""
    w, h, fmt = u16(b, off + 0x1c), u16(b, off + 0x1e), u32(b, off + 0x20)
    data = off + u32(b, off + 0x10)
    if fmt not in BLOCKS:
        raise ValueError('texture format %d not handled' % fmt)
    bw, bh, bpp = BLOCKS[fmt]
    out = bytearray(w * h * 4)
    p = data * 8  # bit position
    for by in range(0, h, bh):
        for bx in range(0, w, bw):
            for y in range(by, by + bh):
                for x in range(bx, bx + bw):
                    i = p // 8
                    if bpp == 4:
                        v = b[i] >> 4 if p % 8 == 0 else b[i] & 15
                    elif bpp == 8:
                        v = b[i]
                    else:
                        v = u16(b, i)
                    p += bpp
                    if fmt == 0:
                        c = (v * 17,) * 3 + (255,)
                    elif fmt == 1:
                        c = (v, v, v, 255)
                    elif fmt == 2:  # IA4
                        c = ((v & 15) * 17,) * 3 + ((v >> 4) * 17,)
                    elif fmt == 3:  # IA8
                        c = (v & 255,) * 3 + (v >> 8,)
                    elif fmt == 4:
                        c = rgb565(v)
                    elif fmt == 5:
                        c = rgb5a3(v)
                    else:
                        if pal is None:
                            raise ValueError('no palette')
                        c = pal[v] if v < len(pal) else (0, 0, 0, 0)
                    if x < w and y < h:
                        o = (y * w + x) * 4
                        out[o:o + 4] = bytes(c)
    return w, h, bytes(out)


# --- the model ---

COMP = {0: ('B', 1), 1: ('b', 1), 2: ('H', 2), 3: ('h', 2), 4: ('f', 4)}


def read_array(b, ho, count_comp, has_bounds):
    """A vertex/normal/uv array header at ho -> list of tuples."""
    data = ho + s32(b, ho + 0x08)
    fmt, div, stride, n = u32(b, ho + 0x18), b[ho + 0x1c], b[ho + 0x1d], u16(b, ho + 0x1e)
    code, size = COMP[fmt]
    scale = 1.0 / (1 << div) if fmt != 4 else 1.0
    out = []
    for i in range(n):
        o = data + i * stride
        out.append(tuple(struct.unpack_from('>' + code, b, o + k * size)[0] * scale for k in range(count_comp)))
    return out


def read_colors(b, ho):
    data = ho + s32(b, ho + 0x08)
    fmt, stride, n = u32(b, ho + 0x18), b[ho + 0x1c], u16(b, ho + 0x1e)
    out = []
    for i in range(n):
        o = data + i * stride
        if fmt == 0:
            c = rgb565(u16(b, o))
        elif fmt == 1:
            c = (b[o], b[o + 1], b[o + 2], 255)
        elif fmt == 2:
            c = (b[o], b[o + 1], b[o + 2], 255)
        elif fmt == 3:
            v = u16(b, o)
            c = ((v >> 12) * 17, (v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17)
        elif fmt == 4:
            v = b[o] << 16 | b[o + 1] << 8 | b[o + 2]
            c = ((v >> 18) * 255 // 63, (v >> 12 & 63) * 255 // 63, (v >> 6 & 63) * 255 // 63, (v & 63) * 255 // 63)
        else:
            c = (b[o], b[o + 1], b[o + 2], b[o + 3])
        out.append(c)
    return out


def section(b, mo, i):
    off = s32(b, mo + 0x10 + 4 * i)
    return brres_group(b, mo + off) if off else {}


def draw_list(b, off):
    """DrawOpa/DrawXlu bytecode -> [(material, object)]."""
    out = []
    p = off
    while True:
        op = b[p]
        if op == 0x01:
            break
        if op == 0x04:
            out.append((u16(b, p + 1), u16(b, p + 3)))
            p += 8
        elif op == 0x02:
            p += 5
        elif op == 0x03:
            p += 4 + 6 * b[p + 3]
        elif op == 0x05:
            p += 5
        elif op == 0x06:
            p += 5
        else:
            raise ValueError('draw list opcode %02x' % op)
    return out


def triangles(prims):
    """Strips/fans/quads/triangles -> triangle index lists."""
    out = []
    for kind, verts in prims:
        if kind == 0x90:
            for i in range(0, len(verts) - 2, 3):
                out.append(verts[i:i + 3])
        elif kind == 0x98:
            for i in range(len(verts) - 2):
                t = verts[i:i + 3]
                out.append(t if i % 2 == 0 else [t[1], t[0], t[2]])
        elif kind == 0xA0:
            for i in range(1, len(verts) - 1):
                out.append([verts[0], verts[i], verts[i + 1]])
        elif kind == 0x80:
            for i in range(0, len(verts) - 3, 4):
                q = verts[i:i + 4]
                out.append([q[0], q[1], q[2]])
                out.append([q[0], q[2], q[3]])
    return out


def read_object(b, oo):
    vcd_lo, vcd_hi = u32(b, oo + 0x0C), u32(b, oo + 0x10)
    prim_size = u32(b, oo + 0x28)
    prim = oo + 0x24 + s32(b, oo + 0x2C)
    ids = {'pos': s16(b, oo + 0x48), 'nrm': s16(b, oo + 0x4A), 'clr': s16(b, oo + 0x4C),
           'uv': s16(b, oo + 0x50)}
    # attribute order in a vertex: PNMTXIDX, TEX0-7MTXIDX, POS, NRM, CLR0, CLR1, TEX0-7
    attrs = [('pnmtx', vcd_lo & 1)]
    for t in range(8):
        attrs.append(('texmtx', vcd_lo >> (1 + t) & 1))
    attrs += [('pos', vcd_lo >> 9 & 3), ('nrm', vcd_lo >> 11 & 3), ('clr', vcd_lo >> 13 & 3), ('clr1', vcd_lo >> 15 & 3)]
    for t in range(8):
        attrs.append(('uv' if t == 0 else 'uv%d' % t, vcd_hi >> (2 * t) & 3))
    prims = []
    p, end = prim, prim + prim_size
    while p < end:
        op = b[p]
        if op == 0:
            p += 1
            continue
        kind = op & 0xF8
        if kind not in (0x80, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8):
            break
        n = u16(b, p + 1)
        p += 3
        verts = []
        for _ in range(n):
            v = {}
            for name, typ in attrs:
                if typ == 0:
                    continue
                if name in ('pnmtx', 'texmtx'):
                    p += 1
                    continue
                if typ == 2:
                    v[name] = b[p]
                    p += 1
                elif typ == 3:
                    v[name] = u16(b, p)
                    p += 2
                else:
                    raise ValueError('direct vertex data not handled')
            verts.append(v)
        prims.append((kind, verts))
    return ids, triangles(prims)


def convert_room(b, model_name=None):
    """-> (textures [(name, w, h, rgba)], meshes [(name, tex, xlu, cull, alpha_test, verts)])"""
    fo = brres_folders(b)
    models = fo['3DModels(NW4R)']
    mname = model_name or next(iter(models))
    mo = models[mname]
    if u32(b, mo + 8) != 11:
        raise ValueError('MDL0 version %d not handled' % u32(b, mo + 8))
    verts = {i: (n, read_array(b, o, 3, True)) for i, (n, o) in enumerate(section(b, mo, 2).items())}
    nrms = {i: read_array(b, o, 3, False) for i, (n, o) in enumerate(section(b, mo, 3).items())}
    clrs = {i: read_colors(b, o) for i, (n, o) in enumerate(section(b, mo, 4).items())}
    uvs = {i: read_array(b, o, 2, True) for i, (n, o) in enumerate(section(b, mo, 5).items())}
    mats = list(section(b, mo, 8).items())
    objs = list(section(b, mo, 10).items())
    # material -> texture, from the texture links
    mat_off = {o: i for i, (n, o) in enumerate(mats)}
    mat_tex = {}
    for tname, lo in section(b, mo, 11).items():
        for k in range(u32(b, lo)):
            moff = lo + s32(b, lo + 4 + 8 * k)
            if moff in mat_off:
                mat_tex.setdefault(mat_off[moff], tname)
    # textures
    pals = fo.get('Palettes(NW4R)', {})
    tex_list = []
    tex_index = {}
    for tname, to in fo.get('Textures(NW4R)', {}).items():
        pname = next((n for n in ('pl_' + tname, 'pl_' + tname.replace('tex_', '')) if n in pals), None)
        pal = palette_any(b, pals[pname]) if pname else None
        try:
            w, h, px = decode_texture(b, to, pal)
        except ValueError:
            continue
        tex_index[tname] = len(tex_list)
        tex_list.append((tname, w, h, px))
    # draw order: opaque, then translucent
    defs = section(b, mo, 0)
    order = [(m, o, 0) for m, o in draw_list(b, defs['DrawOpa'])] if 'DrawOpa' in defs else []
    order += [(m, o, 1) for m, o in draw_list(b, defs['DrawXlu'])] if 'DrawXlu' in defs else []
    meshes = []
    for mat, obj, xlu in order:
        oname, oo = objs[obj]
        ids, tris = read_object(b, oo)
        pos = verts[ids['pos']][1] if ids['pos'] >= 0 else []
        nrm = nrms.get(ids['nrm'], [])
        clr = clrs.get(ids['clr'], [])
        uv = uvs.get(ids['uv'], [])
        mo_ = mats[mat][1]
        cull = u32(b, mo_ + 0x18)
        wrap = 1 | 1 << 2
        if u32(b, mo_ + 0x2C) > 0:  # the first texture layer's S and T wrap modes
            layer = mo_ + s32(b, mo_ + 0x30)
            wrap = min(u32(b, layer + 0x18), 2) | min(u32(b, layer + 0x1C), 2) << 2
        tname = mat_tex.get(mat)
        tex = tex_index.get(tname, -1)
        alpha = 1 if tex >= 0 and any(px < 255 for px in tex_list[tex][3][3::4]) else 0
        out = []
        for t in tris:
            for v in t:
                x, y, z = pos[v['pos']]
                n = nrm[v['nrm']] if 'nrm' in v and v['nrm'] < len(nrm) else (0.0, 1.0, 0.0)
                st = uv[v['uv']] if 'uv' in v and v['uv'] < len(uv) else (0.0, 0.0)
                c = clr[v['clr']] if 'clr' in v and v['clr'] < len(clr) else (255, 255, 255, 255)
                out.append((x, y, z) + tuple(n) + tuple(st) + tuple(c))
        meshes.append((mats[mat][0] + '/' + oname, tex, xlu, cull, alpha | wrap << 4, out))
    return tex_list, meshes


# --- walkable tiles, for the game's collision ---

def _covers(t, x, z):
    """Is (x, z) inside triangle t's xz projection; if so its height there"""
    (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = t
    d = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2)
    if abs(d) < 1e-6:
        return None
    a = ((z1 - z2) * (x - x2) + (x2 - x1) * (z - z2)) / d
    b = ((z2 - z0) * (x - x2) + (x0 - x2) * (z - z2)) / d
    c = 1 - a - b
    if a < -1e-4 or b < -1e-4 or c < -1e-4:
        return None
    return a * y0 + b * y1 + c * y2


def walk_grid(meshes, floor_max=3.0, block_min=10.0, block_share=0.35, reach=40.0):
    """16x16 tiles: floor where at least half the tile is the room's floor, unless furniture covers
    enough of it. Furniture is anything above ankle height that comes down within `reach` of the
    floor, so the dome or a hanging lamp doesn't count."""
    floor, solid = [], []
    for name, tex, xlu, cull, alpha, verts in meshes:
        if xlu or 'shadow' in name:
            continue
        for i in range(0, len(verts) - 2, 3):
            t = [v[:3] for v in verts[i:i + 3]]
            ys = [p[1] for p in t]
            if max(ys) <= floor_max and min(ys) >= -floor_max:
                floor.append(t)
            elif max(ys) > block_min and min(ys) < reach and 'wall' not in name:
                solid.append(t)
    grid = [[0] * 16 for _ in range(16)]
    samples = [(sx + 0.5) * 10 for sx in range(4)]
    for uz in range(16):
        for ux in range(16):
            on_floor = sum(1 for sx in samples for sz in samples
                           if any(_covers(t, ux * 40 + sx, uz * 40 + sz) is not None for t in floor))
            if on_floor < 8:
                continue
            hit = 0
            for sx in samples:
                for sz in samples:
                    x, z = ux * 40 + sx, uz * 40 + sz
                    if any((h := _covers(t, x, z)) is not None and h > block_min for t in solid):
                        hit += 1
            grid[uz][ux] = 0 if hit >= block_share * 16 else 1
    return grid


# City Folk rooms are laid out on 32-unit tiles, the GameCube's on 40
CF_TO_GC = 40.0 / 32.0


def scale_meshes(meshes, k):
    return [(name, tex, xlu, cull, alpha, [(v[0] * k, v[1] * k, v[2] * k) + tuple(v[3:]) for v in verts])
            for name, tex, xlu, cull, alpha, verts in meshes]


def center_meshes(meshes, floor_max=3.0):
    """Move the room so its floor is in the middle of the 640x640 block, on a 40 tile edge"""
    xs, zs = [], []
    for name, tex, xlu, cull, alpha, verts in meshes:
        if not xlu and 'shadow' not in name:
            for v in verts:
                if abs(v[1]) <= floor_max:
                    xs.append(v[0])
                    zs.append(v[2])
    if not xs:
        return meshes
    dx = round((320 - (min(xs) + max(xs)) / 2) / 40) * 40
    dz = round((320 - (min(zs) + max(zs)) / 2) / 40) * 40
    return [(name, tex, xlu, cull, alpha, [(v[0] + dx, v[1], v[2] + dz) + tuple(v[3:]) for v in verts])
            for name, tex, xlu, cull, alpha, verts in meshes]


def write_cfroom(path, textures, meshes):
    meshes = center_meshes(scale_meshes(meshes, CF_TO_GC))
    grid = walk_grid(meshes)
    with open(path, 'wb') as f:
        f.write(b'CFRM' + struct.pack('<I', 3))
        f.write(struct.pack('<I', len(textures)))
        for name, w, h, px in textures:
            f.write(name.encode()[:31].ljust(32, b'\0') + struct.pack('<HH', w, h) + px)
        f.write(struct.pack('<I', len(meshes)))
        for name, tex, xlu, cull, alpha, verts in meshes:
            f.write(name.encode()[:31].ljust(32, b'\0') +
                    struct.pack('<iBBBBI', tex, xlu, cull, alpha & 15, alpha >> 4, len(verts)))
            for v in verts:
                f.write(struct.pack('<8f4B', *v))
        for row in grid:
            f.write(bytes(row))
