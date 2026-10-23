#!/usr/bin/env python3
"""Celeste's textures for the game (cf_sky/celeste.tex), from City Folk's.

She uses Blathers' GameCube model, so her textures must be in his layout: one 4-bit sheet of body
parts and 8 eye frames, sharing a 16-colour palette. The two owls share a
skeleton: City Folk's bones point the same way and the GameCube's model is City Folk's scale x148.8.
So each texel Blathers' model shows is placed on his body (owl_1.c's display lists, his vertices from
the GameCube disc), that point is found on City Folk Celeste's body, facing the same way, and her
texture there is what the texel becomes. The eye frames go the same way, frame by frame.

Her body and face textures use different palettes in City Folk; the colours the texels end up
using are fitted into one.

Her beak, legs and talons stay Blathers' (KEEP), in her colours: his beak sticks out past hers.

celeste.tex, little-endian: "CELT", u32 version (2), 16 u16 palette (RGB5A3), u8 remap[16]
(unused here: identity), u16 painted (bit n: part n is in the sheet; all of them), the sheet (0x640 bytes GX
CI4), 8 eye frames (0x100 bytes each, 32x16 CI4). The game keeps Blathers' see-through texels
see-through (pc_celeste.c), as his model's cut-outs rely on them.
"""
import math
import os
import re
import struct

from cf_rooms import section, read_array, s16, triangles, draw_list, palette_any
from extract_cf_sky import brres_folders, palette, decode, u16, u32, s32

OWL_SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'data', 'npc', 'model',
                       'mdl', 'owl_1.c')
OWL_REL = 'foresta.rel.szs'
OWL_VTX = (0x4AD1C0, 0x1A90)   # owl_1_v, as owl_1.c loads it
OWL_PAL = (0x4AF800, 0x20)     # owl_1_pal (pc_assets.c)
OWL_TMEM = (0x4B0020, 0x640)   # owl_1_tmem_txt
KEEP = (0, 1, 8, 9)  # Blathers' own beak, legs and talons: hers are yellow too, and her beak is smaller
SUBST = {(90, 57, 32): (180, 115, 0)}  # in those, his brown is her dark gold (legs, beak outline)
FACE = {0: {7: (255, 255, 255)}, 1: {7: (255, 255, 255)}}  # round the beak his brown face is her white one
CELESTE_BRRES = 'Npc/Special/Model/14.brres'
SHEET_SIZE = 0x640
EYE_W, EYE_H = 32, 16
TRANSPARENT = 15  # her palette has white twice; the second is the see-through entry


# --- Blathers' GameCube mesh: triangles in joint-local space, with texel coordinates ---

# display matrix slot -> joint name (the skeleton's display order)
SLOT_JOINT = ['base', 'Lfoot1', 'Lfoot2', 'Lfoot3', 'Rfoot1', 'Rfoot2', 'Rfoot3', 'tail1', 'chest',
              'Larm1', 'Larm2', 'Rarm1', 'Rarm2', 'mouth', 'head']


def commands(text):
    """gsFoo(args) calls, with nested parentheses"""
    i = 0
    while True:
        m = re.compile(r'(gs\w+)\(').search(text, i)
        if not m:
            return
        depth, j = 1, m.end()
        while depth:
            depth += {'(': 1, ')': -1}.get(text[j], 0)
            j += 1
        yield m.group(1), text[m.end():j - 1]
        i = j


def gc_mesh(src_c, vb):
    vtx = [struct.unpack_from('>3hH2h4B', vb, i) for i in range(0, len(vb), 16)]
    text = open(src_c).read()
    tris = []
    for body in re.findall(r'static Gfx (\w+)\[\] = \{(.*?)\n\};', text, re.S):
        cache = {}
        mtx = SLOT_JOINT.index(body[0].split('_')[0])  # the joint's own, until loaded
        tex = None
        for cmd, args in commands(re.sub(r'//[^\n]*', '', body[1])):
            if cmd == 'gsSPMatrix':
                m = re.search(r'sizeof\(Mtx\)\*(\d+)', args)
                mtx = int(m.group(1)) if m else 0
            elif cmd == 'gsSPVertex':
                m = re.match(r'\s*&owl_1_v\[(\d+)\],\s*(\d+),\s*(\d+)', args)
                a, n, d = map(int, m.groups())
                for k in range(n):
                    cache[d + k] = (a + k, mtx)
            elif cmd == 'gsDPLoadTextureBlock_4b_Dolphin':
                p = [x.strip() for x in args.split(',')]
                base = p[0]
                off = int(base.split('+')[1].strip(), 16) if '+' in base else 0
                tex = {'img': 'eye' if base.startswith('anime_1') else 'body', 'off': off,
                       'w': int(p[2]), 'h': int(p[3]), 'ws': p[5], 'wt': p[6]}
            elif cmd in ('gsSPNTrianglesInit_5b', 'gsSPNTriangles_5b'):
                nums = [int(x) for x in re.findall(r'-?\d+', args)]
                if cmd == 'gsSPNTrianglesInit_5b':
                    count, nums = nums[0], nums[1:]
                    body_count = [count]
                for t in range(0, len(nums), 3):
                    if body_count[0] <= 0:
                        break
                    body_count[0] -= 1
                    tri = []
                    for s in nums[t:t + 3]:
                        vi, mt = cache[s]
                        x, y, z, _f, s_, t_, *_ = vtx[vi]
                        tri.append(((x, y, z), SLOT_JOINT[mt], (s_ / 32.0, t_ / 32.0)))
                    tris.append((body[0], dict(tex) if tex else None, tri))
    return tris


def mat34(b, o):
    m = struct.unpack_from('>12f', b, o)
    return [m[0:4], m[4:8], m[8:12]]


def apply(m, p):
    return tuple(m[r][0] * p[0] + m[r][1] * p[1] + m[r][2] * p[2] + m[r][3] for r in range(3))


def read_object(b, oo):
    """like cf_rooms.read_object, keeping each vertex's matrix index"""
    vcd_lo, vcd_hi = u32(b, oo + 0x0C), u32(b, oo + 0x10)
    prim_size = u32(b, oo + 0x28)
    prim = oo + 0x24 + s32(b, oo + 0x2C)
    ids = {'pos': s16(b, oo + 0x48), 'nrm': s16(b, oo + 0x4A), 'uv': s16(b, oo + 0x50)}
    attrs = [('pnmtx', vcd_lo & 1)]
    for t in range(8):
        attrs.append(('texmtx', vcd_lo >> (1 + t) & 1))
    attrs += [('pos', vcd_lo >> 9 & 3), ('nrm', vcd_lo >> 11 & 3), ('clr', vcd_lo >> 13 & 3), ('clr1', vcd_lo >> 15 & 3)]
    for t in range(8):
        attrs.append(('uv' if t == 0 else 'uv%d' % t, vcd_hi >> (2 * t) & 3))
    prims = []
    slots = {}
    p, end = prim, prim + prim_size
    while p < end:
        op = b[p]
        if op == 0:
            p += 1
            continue
        if op in (0x20, 0x28, 0x30, 0x38):  # load a matrix into a slot: index (node), address
            if op == 0x20:
                slots[(u16(b, p + 3) & 0xFFF) // 12] = u16(b, p + 1)
            p += 5
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
                    if name == 'pnmtx':
                        v['pnmtx'] = slots.get(b[p] // 3, -1)
                    p += 1
                    continue
                if typ == 2:
                    v[name] = b[p]; p += 1
                else:
                    v[name] = u16(b, p); p += 2
            verts.append(v)
        prims.append((kind, verts))
    table_off = s32(b, oo + 0x64)
    table = [u16(b, oo + table_off + 4 + 2 * i) for i in range(u32(b, oo + table_off))]
    return ids, triangles(prims), s32(b, oo + 8), table


def cf_mesh(b, model):
    fo = brres_folders(b)
    mo = fo['3DModels(NW4R)'][model]
    bones = {}
    for name, o in section(b, mo, 1).items():
        bones[u32(b, o + 0x10)] = (name, mat34(b, o + 0x70), mat34(b, o + 0xA0))
    verts = {i: read_array(b, o, 3, True) for i, (n, o) in enumerate(section(b, mo, 2).items())}
    uvs = {i: read_array(b, o, 2, True) for i, (n, o) in enumerate(section(b, mo, 5).items())}
    mats = list(section(b, mo, 8).items())
    objs = list(section(b, mo, 10).items())
    defs = section(b, mo, 0)
    out = []
    for mat, obj in draw_list(b, defs['DrawOpa']):
        oname, oo = objs[obj]
        ids, tris, node, table = read_object(b, oo)
        pos, uv = verts[ids['pos']], uvs.get(ids['uv'], [])
        for t in tris:
            tri = []
            for v in t:
                nd = v['pnmtx'] if 'pnmtx' in v else node
                tri.append((pos[v['pos']], bones[nd][0], uv[v['uv']] if 'uv' in v else (0, 0), 'pnmtx' in v))
            out.append((mats[mat][0], tri))
    return out, bones


K = 148.8  # GameCube units per City Folk unit
PARTS = [(0x000, 16, 8), (0x040, 16, 16), (0x0C0, 32, 8), (0x140, 32, 8), (0x1C0, 32, 8),
         (0x240, 32, 24), (0x3C0, 32, 24), (0x540, 16, 16), (0x5C0, 16, 8), (0x600, 16, 8)]


def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def normal(pts):
    a, b = sub(pts[1], pts[0]), sub(pts[2], pts[0])
    n = (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
    l = math.sqrt(dot(n, n)) or 1
    return (n[0] / l, n[1] / l, n[2] / l)


def closest_on_tri(p, a, b, c):
    """closest point to p on triangle abc, as barycentric (u, v, w)"""
    ab, ac, ap = sub(b, a), sub(c, a), sub(p, a)
    d1, d2 = dot(ab, ap), dot(ac, ap)
    if d1 <= 0 and d2 <= 0: return (1, 0, 0)
    bp = sub(p, b); d3, d4 = dot(ab, bp), dot(ac, bp)
    if d3 >= 0 and d4 <= d3: return (0, 1, 0)
    vc = d1 * d4 - d3 * d2
    if vc <= 0 and d1 >= 0 and d3 <= 0:
        v = d1 / (d1 - d3); return (1 - v, v, 0)
    cp = sub(p, c); d5, d6 = dot(ab, cp), dot(ac, cp)
    if d6 >= 0 and d5 <= d6: return (0, 0, 1)
    vb = d5 * d2 - d1 * d6
    if vb <= 0 and d2 >= 0 and d6 <= 0:
        w = d2 / (d2 - d6); return (1 - w, 0, w)
    va = d3 * d6 - d5 * d4
    if va <= 0 and (d4 - d3) >= 0 and (d5 - d6) >= 0:
        w = (d4 - d3) / ((d4 - d3) + (d5 - d6)); return (0, 1 - w, w)
    den = 1 / (va + vb + vc)
    v, w = vb * den, vc * den
    return (1 - v - w, v, w)


def mix(bary, pts):
    return tuple(sum(bary[k] * pts[k][i] for k in range(3)) for i in range(len(pts[0])))


def cf_world(cf_tris, bones):
    by_name = {n: w for nd, (n, w, inv) in bones.items()}
    out = []
    for mat, tri in cf_tris:
        pts = [apply(by_name[bn], p) for p, bn, uv, w in tri]
        out.append((mat, pts, [uv for p, bn, uv, w in tri], [bn for p, bn, uv, w in tri], normal(pts)))
    return out, by_name


def texel_points(gc_tris, by_name):
    """{(img, part offset): {(x, y): world point}} for each texel Blathers' model shows"""
    out = {}
    for name, tex, tri in gc_tris:
        if tex is None:
            continue
        w, h = tex['w'], tex['h']
        pts = [apply(by_name[j], (p[0] / K, p[1] / K, p[2] / K)) for p, j, uv in tri]
        uvs = [uv for p, j, uv in tri]
        key = (tex['img'], tex['off'])
        nrm = tuple(-c for c in normal(pts))  # GX winding is the other way round
        dst = out.setdefault(key, {})
        # walk the triangle's texel footprint
        us, vs = [u for u, v in uvs], [v for u, v in uvs]
        (x0, y0), (x1, y1), (x2, y2) = uvs
        den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(den) < 1e-9:
            continue
        for ty in range(math.floor(min(vs)), math.ceil(max(vs))):
            for tx in range(math.floor(min(us)), math.ceil(max(us))):
                px, py = tx + 0.5, ty + 0.5
                a = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / den
                b = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / den
                c = 1 - a - b
                if min(a, b, c) < -0.15:  # a little past the edges, for seams
                    continue
                a, b, c = max(a, 0), max(b, 0), max(c, 0)
                s = a + b + c
                p = mix((a / s, b / s, c / s), pts)
                # fold mirrored/clamped coordinates into the texture
                fx = tx % (2 * w) if tex['ws'] == 'GX_MIRROR' else min(max(tx, 0), w - 1)
                if tex['ws'] == 'GX_MIRROR' and fx >= w:
                    fx = 2 * w - 1 - fx
                fy = min(max(ty, 0), h - 1)
                inside = min(a, b, c) >= 0
                if inside or (fx, fy) not in dst:
                    dst[(fx, fy)] = (p, nrm)
    return out


def nearest(cf_w, pn):
    """the closest City Folk surface facing the same way"""
    p, n = pn
    best = None
    for mat, pts, uvs, bns, cn in cf_w:
        bary = closest_on_tri(p, *pts)
        q = mix(bary, pts)
        d = dot(sub(p, q), sub(p, q))
        facing = dot(n, cn)
        if facing < 0.2:
            d = d * 4 + 4  # facing away: only if nothing else is near
        d /= 0.5 + 0.5 * max(facing, 0)
        if best is None or d < best[0]:
            best = (d, mat, mix(bary, uvs))
    return best


def cstr(b, o):
    return b[o:b.index(b'\0', o)].decode()


def cf_textures(b, model):
    """{texture: (w, h, rows)}, {material: (wrap s, wrap t)}; eye frames are 'e.N'.
    Each texture is decoded with the palette its material names (Celeste's body is 'skin0', with
    the yellow beak and feet; her face 'skin')."""
    fo = brres_folders(b)
    mo = fo['3DModels(NW4R)'][model]
    wraps, tex_pal = {}, {}
    for mname, o in section(b, mo, 8).items():
        if not u32(b, o + 0x2C):
            wraps[mname] = (1, 1)
            continue
        lo = o + s32(b, o + 0x30)
        wraps[mname] = (u32(b, lo + 0x18), u32(b, lo + 0x1C))
        if s32(b, lo + 4):
            tex_pal[cstr(b, lo + s32(b, lo)).split('.')[0]] = cstr(b, lo + s32(b, lo + 4))
    pals = fo['Palettes(NW4R)']
    tex = {}
    for n, o in fo['Textures(NW4R)'].items():
        pname = tex_pal.get(n.split('.')[0], next(iter(pals)))
        tex[n] = decode(b, o, palette_any(b, pals[pname]))
    return tex, wraps


def palette16(counts, n):
    """At most n colours for the counted ones: the closest pair merges, weighted, until they fit"""
    cols = {c: k for c, k in counts.items()}
    while len(cols) > n:
        keys = list(cols)
        best = None
        for i in range(len(keys)):
            for j in range(i + 1, len(keys)):
                a, b = keys[i], keys[j]
                d = sum((x - y) ** 2 for x, y in zip(a, b)) * min(cols[a], cols[b])
                if best is None or d < best[0]:
                    best = (d, a, b)
        _, a, b = best
        ka, kb = cols.pop(a), cols.pop(b)
        m = tuple(round((x * ka + y * kb) / (ka + kb)) for x, y in zip(a, b))
        cols[m] = cols.get(m, 0) + ka + kb
    return list(cols)


def wrap(i, n, mode):
    if mode == 0:
        return min(max(i, 0), n - 1)
    if mode == 2:
        i %= 2 * n
        return 2 * n - 1 - i if i >= n else i
    return i % n


def sample(tex, wraps, mat, uv, frame=0):
    name = mat if mat in tex else '%s.%d' % (mat, frame)
    w, h, rows = tex[name]
    ws, wt = wraps.get(mat, (1, 1))
    return rows[wrap(math.floor(uv[1] * h), h, wt)][wrap(math.floor(uv[0] * w), w, ws)]


# --- the file ---

def to_rgb5a3(r, g, b, a):
    if a >= 0xE0:
        return 0x8000 | (r >> 3) << 10 | (g >> 3) << 5 | b >> 3
    return (a >> 5) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | b >> 4


def encode_ci4(idx, w, h):
    """Row-major indices -> GX CI4 (8x8 tiles)"""
    out = bytearray(w * h // 2)
    i = 0
    for by in range(0, h, 8):
        for bx in range(0, w, 8):
            for y in range(by, by + 8):
                for x in range(bx, bx + 8, 2):
                    out[i] = idx[y * w + x] << 4 | idx[y * w + x + 1]
                    i += 1
    return bytes(out)


def fill(grid, w, h):
    """Texels no triangle shows take their nearest shown neighbour's colour"""
    known = [(x, y) for y in range(h) for x in range(w) if grid[y][x] is not None]
    for y in range(h):
        for x in range(w):
            if grid[y][x] is None and known:
                kx, ky = min(known, key=lambda k: (k[0] - x) ** 2 + (k[1] - y) ** 2)
                grid[y][x] = grid[ky][kx]
    return grid


def rgb5a3(v):
    if v & 0x8000:
        return ((v >> 10 & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v & 31) * 255 // 31, 255)
    return ((v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17, (v >> 12 & 7) * 255 // 7)


def ci4_indices(data, off, w, h):
    """GX CI4 (8x8 tiles) at off -> row-major indices"""
    idx = [0] * (w * h)
    i = off * 2
    for by in range(0, h, 8):
        for bx in range(0, w, 8):
            for y in range(by, by + 8):
                for x in range(bx, bx + 8):
                    b = data[i // 2]
                    idx[y * w + x] = b >> 4 if i % 2 == 0 else b & 15
                    i += 1
    return idx


def build(cf_brres, rel, out_path, preview_path=None):
    """cf_brres: City Folk's Celeste model file; rel: the GameCube game's foresta.rel (Blathers)"""
    sl = lambda r: rel[r[0]:r[0] + r[1]]
    gc = gc_mesh(OWL_SRC, sl(OWL_VTX))
    his_pal = [rgb5a3(struct.unpack_from('>H', sl(OWL_PAL), 2 * i)[0]) for i in range(16)]
    his_pal = [SUBST.get(c[:3], c[:3]) + c[3:] for c in his_pal]
    his_tmem = sl(OWL_TMEM)
    cf_tris, bones = cf_mesh(cf_brres, 'ows')
    cf_w, by_name = cf_world(cf_tris, bones)
    tex, wraps = cf_textures(cf_brres, 'ows')
    maps = {key: {xy: nearest(cf_w, p) for xy, p in texels.items()} for key, texels in texel_points(gc, by_name).items()}

    def paint(key, w, h, frame=0):
        m = maps.get(key, {})
        grid = [[tuple(sample(tex, wraps, m[(x, y)][1], m[(x, y)][2], frame))[:3] if (x, y) in m else None
                 for x in range(w)] for y in range(h)]
        return fill(grid, w, h)

    grids = [(w, h, paint(('body', off), w, h)) for off, w, h in PARTS]
    # her beak's and feet's yellows belong to the parts kept from Blathers: elsewhere (where his face
    # lands on the base of her bigger beak) they take their neighbours' colours
    yellow = lambda c: c[0] > 150 and c[1] > 80 and c[2] < 70
    for k, (w, h, grid) in enumerate(grids):
        if k not in KEEP:
            grids[k] = (w, h, fill([[None if yellow(c) else c for c in row] for row in grid], w, h))
    eyes = [paint(('eye', 0), EYE_W, EYE_H, f) for f in range(8)]
    # her two palettes share the game's one: the colours used, fitted into 15 (one is see-through)
    counts = {}
    for k, (w, h, grid) in enumerate(grids + [(EYE_W, EYE_H, e) for e in eyes]):
        if k in KEEP:
            off = PARTS[k][0]
            cells = [FACE.get(k, {}).get(i, his_pal[i][:3]) for i in ci4_indices(his_tmem, off, w, h)
                     if his_pal[i][3] == 255]
        else:
            cells = [c for row in grid for c in row]
        for c in cells:
            counts[c] = counts.get(c, 0) + 1
    pal = palette16(counts, 15)
    pal += [(0, 0, 0)] * (15 - len(pal))
    near = lambda c: min(range(15), key=lambda i: sum((x - y) ** 2 for x, y in zip(pal[i], c)))
    grids = [(w, h, [[near(c) for c in row] for row in grid]) for w, h, grid in grids]
    eyes = [[[near(c) for c in row] for row in e] for e in eyes]
    nearest_pal = lambda c: min(range(15), key=lambda i: sum((x - y) ** 2 for x, y in zip(pal[i], c[:3])))
    sheet = bytearray(SHEET_SIZE)
    for k, ((off, w, h), (_, _, grid)) in enumerate(zip(PARTS, grids)):
        if k in KEEP:
            # his art, in her colours
            his = ci4_indices(his_tmem, off, w, h)
            grid = [[TRANSPARENT if his_pal[i][3] != 255 else nearest_pal(FACE.get(k, {}).get(i, his_pal[i]))
                     for i in his[y * w:(y + 1) * w]] for y in range(h)]
            grids[k] = (w, h, grid)
        data = encode_ci4([i for row in grid for i in row], w, h)
        sheet[off:off + len(data)] = data
    pal = [c + (255,) for c in pal]
    pal_out = pal + [(255, 255, 255, 0)]  # TRANSPARENT
    with open(out_path, 'wb') as f:
        f.write(b'CELT' + struct.pack('<I', 2) + struct.pack('<16H', *[to_rgb5a3(*c) for c in pal_out]))
        f.write(bytes(range(16)) + struct.pack('<H', (1 << len(PARTS)) - 1) + bytes(sheet))
        for e in eyes:
            f.write(encode_ci4([i for row in e for i in row], EYE_W, EYE_H))
    if preview_path:
        from PIL import Image
        k = 6
        im = Image.new('RGBA', (sum(w + 4 for o, w, h in PARTS) * k + 8 * 36 * k, 26 * k), (40, 40, 40, 255))
        x = 0
        for w, h, grid in grids + [(EYE_W, EYE_H, e) for e in eyes]:
            p = Image.new('RGBA', (w, h))
            p.putdata([pal_out[i] for row in grid for i in row])
            im.paste(p.resize((w * k, h * k), Image.NEAREST), (x, 0))
            x += (w + 4) * k
        im.save(preview_path)


def write_celeste_tex(cf_root, game_dir, out_path):
    """From the City Folk files folder and the game folder (for the GameCube disc)"""
    import gc_disc
    brres = os.path.join(cf_root, *CELESTE_BRRES.split('/'))
    disc = gc_disc.find_disc(game_dir)
    if not os.path.isfile(brres) or not os.path.isfile(OWL_SRC) or disc is None:
        print('no %s, owl_1.c or GameCube disc: Celeste keeps Blathers\' colours' % brres)
        return
    build(open(brres, 'rb').read(), gc_disc.read_file(disc, OWL_REL), out_path)
    print('wrote %s' % out_path)


if __name__ == '__main__':
    import sys
    import gc_disc
    build(open(sys.argv[1], 'rb').read(), gc_disc.read_file(sys.argv[2], OWL_REL), sys.argv[3],
          sys.argv[4] if len(sys.argv) > 4 else None)
