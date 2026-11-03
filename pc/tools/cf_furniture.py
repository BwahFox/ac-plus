#!/usr/bin/env python3
"""City Folk furniture -> cf_sky/furniture.cfftr for pc/src/pc_cf_ftr.c.

Part of extract_cf_sky.py. The pieces are the ones pc/tools/cf_furniture.tsv lists (City Folk's that the GameCube
has none of the same name of); their names and prices come from the disc's item.bin, their models from
files/Item/Ftr/FtrN.brres (paintings: files/Item/Picture/PictureN.brres). Each model is posed as it stands (its
bones' rest pose) and baked into one set of triangles in the GameCube furniture's model units (a tenth of a world
unit, City Folk's units being 3/4 of the GameCube's: one tile is 30 there and 40 here), with its textures in the
GameCube's RGB5A3 (16-bit, 4x4 tiles, big-endian), ready to be drawn as the game draws its own furniture.

.cfftr, little-endian:
    "CFFT" u32 version (1) u32 piece count, then each piece:
        u16 item, u8 size (0 1x1, 1 2x1, 2 2x2), u8 kind (0 plain, 1 chair, 2 sofa, 3 bed), u8 series (the HRA
        series index, 0xFF none), u8 group (0 A, 1 B, 2 C), u16 does (0 nothing, 1 TV, 2 stereo, 0x100 + n instrument n), u32 price, f32 height (world units, 0: nothing
        goes on it), f32 top (world units: the model's highest point), name (32 bytes, ASCII, 0 after)
        u32 texture count, then each: u16 width, u16 height, u8 wrap s, u8 wrap t (0 clamp 1 repeat 2 mirror),
            u16 pad, RGB5A3 pixels (width * height * 2)
        u32 mesh count, then each: i32 texture (-1 none), u8 xlu, u8 cull (GX: 0 none 1 front 2 back 3 all),
            u8 alpha test, u8 part (0: the piece itself, n: moving part n - 1), u32 vertex count, then vertices
            (a triangle list): s16 x y z (model units), s8 nx ny nz, u8 pad, f32 s t (in texels)
        (version 2) its moving parts, if any: u8 storage (0 none, else the GameCube's: 1 drawers, 2 double doors,
            4 one door), u8 pad[3], u32 part count, then each: u8 kind (0 animated, 1 hour hand, 2 minute hand),
            u8 pad[3], f32 pivot[3], f32 axis[3] (a hand turns about these, model units); u32 animation count, then
            each: u8 mode (0 the storage opening, 1 always looping, 2 looping while switched on, 3 once when
            switched, 4 once when switched on, before the next one loops), u8 pad[3], u32 frames, then frames + 1
            times, for each part, its move from where it stands: f32 3x4 (rows; model units)
"""
import os
import struct

from cf_char import convert_char
from cf_decor import u8_files, item_names_all

VERSION = 2
SCALE = 40.0 / 30.0 * 10.0  # City Folk units -> GameCube furniture model units (0.1 world unit)

SIZES = {'1x1': 0, '2x1': 1, '2x2': 2}
KINDS = {'plain': 0, 'chair': 1, 'sofa': 2, 'bed': 3}
GROUPS = {'A': 0, 'B': 1, 'C': 2}
# The GameCube's HRA series (m_mark_room.h mMkRm_SERIES_*) the table may name
SERIES = ['EXOTIC', 'LOVELY', 'CLASSIC', 'RANCH', 'CABANA', 'BLUE', 'MODERN', 'REGAL', 'GREEN', 'CABIN', 'SPOOKY',
          'JINGLE', 'KIDDIE', 'GARDEN', 'ROCKGARDEN', 'JAPANESE', 'CONSTRUCTION', 'SPACE', 'PUBLICBATH', 'SCHOOL',
          'CHESS', 'BONSAI', 'WRITING', 'VASE', 'VENDING', 'NINTENDO', 'DARUMA', 'CACTUS', 'BEAR', 'FIGURE',
          'TOTEMPOLE', 'GUITAR', 'DRUM', 'STRINGS', 'OFFICE', 'TRICERA', 'TREX', 'BRONT', 'STEGO', 'PTERA', 'PLESIO',
          'MAMMOTH', 'ROBOT', 'APPLE', 'CITRUS', 'MELON', 'PEAR', 'LUCKY', 'PINE', 'FROGGY', 'IRIS', 'TULIP',
          'DAFFODIL', 'OTHER', 'SNOWMAN', 'WESTERN', 'BACKYARD', 'HARVEST', 'BOXING', 'MARIO']


def read_table(path):
    rows = []
    head = None
    for line in open(path, encoding='utf-8'):
        if line.startswith('#') or not line.strip():
            continue
        cols = line.rstrip('\n').split('\t')
        if head is None:
            head = cols
            continue
        rows.append(dict(zip(head, cols)))
    return rows


def rgb5a3_tiled(w, h, px):
    out = bytearray()
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            for y in range(by, by + 4):
                for x in range(bx, bx + 4):
                    o = (min(y, h - 1) * w + min(x, w - 1)) * 4
                    r, g, b, a = px[o:o + 4]
                    if a >= 0xE0:
                        v = 0x8000 | (r >> 3) << 10 | (g >> 3) << 5 | b >> 3
                    else:
                        v = (a >> 5) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | b >> 4
                    out += struct.pack('>H', v)
    return bytes(out)


def glass_alpha(textures, meshes):
    """A grey texture without alpha that's only drawn see-through (a tank's glass, in some of City Folk's files): its
    brightness is how solid it is, or it would be a dark opaque pane"""
    out = list(textures)
    for i, (name, w, h, px) in enumerate(textures):
        used = [m for m in meshes if m[1] == i]
        if not used or not all(m[2] for m in used):
            continue
        rgba = bytes(px)
        if all(a == 255 for a in rgba[3::4]) and rgba[0::4] == rgba[1::4] == rgba[2::4]:
            fixed = bytearray(rgba)
            fixed[3::4] = rgba[0::4]
            out[i] = (name, w, h, bytes(fixed))
    return out


def bake(b, model, shift_x=0.0):
    """The model as it stands: (bones, textures, meshes) with vertices in City Folk units, moved shift_x along x,
    each with its bone"""
    bones, textures, meshes = convert_char(b, model)
    textures = glass_alpha(textures, meshes)
    out_meshes = []
    for name, tex, xlu, cull, alpha, wrap, verts in meshes:
        vs = []
        for bone, x, y, z, nx, ny, nz, s, t in verts:
            w = bones[bone]['world']
            p = [w[i][0] * x + w[i][1] * y + w[i][2] * z + w[i][3] for i in range(3)]
            p[0] += shift_x
            n = [w[i][0] * nx + w[i][1] * ny + w[i][2] * nz for i in range(3)]
            ln = max(1e-6, sum(c * c for c in n) ** 0.5)
            vs.append((p, [c / ln for c in n], s, t, bone))
        out_meshes.append((tex, xlu, cull, alpha, wrap, vs))
    return bones, textures, out_meshes


def rebind_blended(bones, meshes):
    """A fish's body is skinned: vertices blended between its spine's bones come out here on the skeleton's root, which
    doesn't move. Each goes with the nearest bone of its mesh, so it moves with the fish."""
    out = []
    for m in meshes:
        spine = sorted({v[4] for v in m[5]} - {0})
        if len(spine) < 2 or not any(v[4] == 0 for v in m[5]) or bones[0]['parent'] != -1:
            out.append(m)
            continue
        at = {i: [bones[i]['world'][k][3] for k in range(3)] for i in spine}
        vs = []
        for p, n, s, t, bone in m[5]:
            if bone == 0:
                bone = min(spine, key=lambda i: sum((p[k] - at[i][k]) ** 2 for k in range(3)))
            vs.append((p, n, s, t, bone))
        out.append(m[:5] + (vs,))
    return out


# --- Moving parts: dressers' drawers and doors, clocks' hands, what City Folk animates ---

# Storage: City Folk's chests open with one of three shared animations (Ftr/roomAlways.arc int_anim.brres), unless
# the piece has its own: chestA a drawer (the GameCube's dressers), chestB one door (closets), chestC two (wardrobes)
STORAGE = {'chestA': 1, 'chestB': 4, 'chestC': 2}
HANDS = {'kh_j': 1, 'km_j': 2}
MODE_STORAGE, MODE_LOOP, MODE_SWITCH_LOOP, MODE_ONCE, MODE_SWITCH_START = 0, 1, 2, 3, 4


def m_inv(m):
    import numpy as np
    a = np.eye(4)
    a[:3, :] = np.array(m)
    return np.linalg.inv(a)[:3, :].tolist()


def anim_worlds(bones, b, off, f, absolute=False):
    """Each bone's model-space matrix at frame f of the CHR0 at off: the animation's turn and move since its own
    first frame, put on the model's own rest pose (the shared ones hinge where no model does)"""
    from cf_anim import read_chr0, sample
    from cf_roost_anim import m_srt, m_mul
    a = read_chr0(b, off)
    out = []
    for bn in bones:
        par = bones[bn['parent']]['world'] if bn['parent'] >= 0 else None
        rest = m_mul(m_inv(par), bn['world']) if par else bn['world']
        s, s0 = sample(a, bn['name'], f), sample(a, bn['name'], 0)
        if absolute and s and bn['name'] not in HANDS:
            loc = m_srt(*s)  # where the animation has it, not a move from its first frame (a fish in its tank)
        else:
            loc = m_mul(rest, m_mul(m_inv(m_srt(*s0)), m_srt(*s))) if s and bn['name'] not in HANDS else rest
        out.append(m_mul(out[bn['parent']], loc) if bn['parent'] >= 0 else loc)
    return a, out


def motion(b, model, bones, meshes, shared, shift_x, always=None, absolute=False):
    """(storage, parts [(kind, pivot, axis, bones)], anims [(mode, frames, [[3x4 per part] per frame])]) for the
    piece; the parts' bones: whose vertices move with it"""
    from extract_cf_sky import brres_folders
    from cf_roost_anim import m_mul, is_ident
    own = brres_folders(b).get('AnmChr(NW4R)', {})
    storage = next((v for k, v in STORAGE.items() if model.endswith(k)), 0)
    srcs = []  # (mode, bytes, offset)
    if always is not None:  # that animation, over and over (a fish or bug in the museum)
        storage = 0
        srcs.append((MODE_LOOP, b, own[always] if always in own else next(iter(own.values()))))
    elif storage:
        kind = next(k for k in STORAGE if model.endswith(k))
        if model in own:
            srcs.append((MODE_STORAGE, b, own[model]))
        else:
            srcs.append((MODE_STORAGE, shared[0], shared[1]['int_anim_' + kind]))
    elif own:
        from cf_anim import read_chr0
        names = sorted(own)
        tail = model.rsplit('_', 1)[-1]
        if tail == 'FS' and len(names) >= 2:  # a start, then a loop while switched on
            srcs = [(MODE_SWITCH_START, b, own[names[0]]), (MODE_SWITCH_LOOP, b, own[names[1]])]
        else:
            loop = read_chr0(b, own[names[0]])['loop']
            mode = (MODE_LOOP if tail == 'LP' else MODE_SWITCH_LOOP if tail in ('SW', 'SW2')
                    else MODE_LOOP if loop and not tail.startswith('TR') else MODE_ONCE)
            srcs = [(mode, b, own[names[0]])]
    # the parts: bones some animation moves (and their children), and clock hands
    moved = set()
    baked = []
    for mode, ab, off in srcs:
        a, _ = anim_worlds(bones, ab, off, 0, absolute)
        frames = []
        for f in range(a['frames'] + 1):
            _, ws = anim_worlds(bones, ab, off, f, absolute)
            d = [m_mul(w, m_inv(bn['world'])) for w, bn in zip(ws, bones)]
            moved |= {i for i, m in enumerate(d) if not is_ident(m)}
            frames.append(d)
        baked.append((mode, a['frames'], frames))
    used = {v[4] for m in meshes for v in m[5]}
    parts = []
    for i, bn in enumerate(bones):
        if i not in used:
            continue
        if bn['name'] in HANDS:
            pts = [v[0] for m in meshes for v in m[5] if v[4] == i]
            parts.append((HANDS[bn['name']], hand_pivot(bn, shift_x), hand_axis(pts), i))
        elif i in moved:
            parts.append((0, (0.0, 0.0, 0.0), (0.0, 0.0, 1.0), i))
    if not parts:
        return storage if srcs and storage else 0, [], []
    parts = parts[:16]  # (the game's limit: what's past it stays still)
    anims = []
    for mode, n, frames in baked:
        per = []
        for d in frames:
            per.append([to_model(d[bi], shift_x) if kind == 0 else IDENT34 for kind, _p, _a, bi in parts])
        anims.append((mode, n, per))
    return storage, parts, anims


IDENT34 = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0]]


def to_model(d, shift_x):
    """A move in City Folk units (about their origin) -> in model units, about the piece's (moved shift_x along x)"""
    r = [row[:3] for row in d]
    t = [d[i][3] + shift_x * (1.0 if i == 0 else 0.0) - r[i][0] * shift_x for i in range(3)]
    return [r[i] + [t[i] * SCALE] for i in range(3)]


def hand_pivot(bn, shift_x):
    w = bn['world']
    return ((w[0][3] + shift_x) * SCALE, w[1][3] * SCALE, w[2][3] * SCALE)


def hand_axis(pts):
    """The normal of a hand's flat shape, facing the clock's front (+z)"""
    import numpy as np
    if len(pts) < 3:
        return (0.0, 0.0, 1.0)
    a = np.array(pts)
    a -= a.mean(0)
    n = np.linalg.svd(a)[2][-1]
    if n[2] < 0:
        n = -n
    return tuple(float(c) for c in n / np.linalg.norm(n))


def does_code(does):
    """The table's does column as the file has it: 0 nothing, 1 TV, 2 stereo, 0x100 + n instrument n"""
    if does.startswith('inst:'):
        return 0x100 + int(does[5:])
    return {'': 0, 'tv': 1, 'stereo': 2}[does]


def tv_screen(textures, meshes):
    """A TV's screen is its one untextured quad, facing front, which City Folk draws its programs on. (textures, meshes,
    screen): the quad's corners a hair in front of it, anticlockwise from the bottom left for the picture, and the quad
    itself now dark glass, which is what shows while the TV's off"""
    quad = [m for m in meshes if m[0] < 0 and len(m[5]) == 6]
    if len(quad) != 1:
        raise ValueError('%d screens' % len(quad))
    ps = [v[0] for v in quad[0][5]]
    x0, x1 = min(p[0] for p in ps), max(p[0] for p in ps)
    y0, y1 = min(p[1] for p in ps), max(p[1] for p in ps)
    z = max(p[2] for p in ps) + 0.15
    dark = len(textures)
    textures = list(textures) + [('tv_off', 4, 4, bytes((26, 28, 30, 255)) * 16)]
    meshes = [(dark,) + m[1:] if m is quad[0] else m for m in meshes]
    return textures, meshes, [(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)]


def piece_blob(textures, meshes, parts=(), storage=0, anims=(), screen=()):
    out = bytearray(struct.pack('<I', len(textures)))
    wraps = {}
    for tex, xlu, cull, alpha, wrap, vs in meshes:
        if tex >= 0:
            wraps.setdefault(tex, (wrap & 3, wrap >> 2 & 3))
    for i, (tname, w, h, px) in enumerate(textures):
        ws, wt = wraps.get(i, (1, 1))
        out += struct.pack('<HHBBH', w, h, ws, wt, 0) + rgb5a3_tiled(w, h, px)
    # each mesh split by part: a triangle goes with its first vertex's bone
    part_of = {bi: k + 1 for k, (_kind, _p, _a, bi) in enumerate(parts)}
    split = []
    for tex, xlu, cull, alpha, wrap, vs in meshes:
        by = {}
        for i in range(0, len(vs) - 2, 3):
            by.setdefault(part_of.get(vs[i][4], 0), []).extend(vs[i:i + 3])
        for part in sorted(by):
            split.append((tex, xlu, cull, alpha, part, by[part]))
    out += struct.pack('<I', len(split))
    for tex, xlu, cull, alpha, part, vs in split:
        tw, th = (textures[tex][1], textures[tex][2]) if tex >= 0 else (1, 1)
        out += struct.pack('<iBBBBI', tex, xlu, min(cull, 3), alpha, part, len(vs))
        for p, n, s, t, _bone in vs:
            out += struct.pack('<3h3bBff', *[max(-32768, min(32767, round(c * SCALE))) for c in p],
                               *[max(-127, min(127, round(c * 127))) for c in n], 0, s * tw, t * th)
    out += struct.pack('<B3xI', storage, len(parts))
    for kind, pivot, axis, _bi in parts:
        out += struct.pack('<B3x6f', kind, *pivot, *axis)
    out += struct.pack('<I', len(anims))
    for mode, n, frames in anims:
        out += struct.pack('<B3xI', mode, n)
        for per in frames:
            for m in per:
                out += struct.pack('<12f', *(c for row in m for c in row))
    out += struct.pack('<I', len(screen))
    for p in screen:
        out += struct.pack('<3h', *[max(-32768, min(32767, round(c * SCALE))) for c in p])
    return bytes(out)


def write_furniture(root, table_path, out_path, codes_path=None):
    item = os.path.join(root, 'Item')
    from extract_cf_sky import brres_folders
    anim_b = u8_files(os.path.join(root, 'Ftr', 'roomAlways.arc'))['int_anim.brres']
    shared = (anim_b, brres_folders(anim_b)['AnmChr(NW4R)'])
    records = item_names_all(u8_files(os.path.join(item, 'item.arc'))['item.bin'])
    out = bytearray()
    count = 0
    codes = []
    for row in read_table(table_path):
        slot = int(row['slot'])
        rec = records.get(int(row['record']))
        path = os.path.join(item, *row['file'].split('/'))
        if rec is None or not os.path.isfile(path):
            print('cf_furniture: no record %s or no %s' % (row['record'], path))
            continue
        price, name = rec
        b = open(path, 'rb').read()
        model = row['model']
        if not model:  # a painting: the file's one model
            from extract_cf_sky import brres_folders
            model = next(iter(brres_folders(b)['3DModels(NW4R)']))
        try:
            # a 2x1 piece stands on its left tile, as the GameCube's (City Folk's are centred on both): half a tile
            # (15 of City Folk's units) over
            shift = 15.0 if row['size'] == '2x1' else 0.0
            bones, textures, meshes = bake(b, model, shift)
            storage, parts, anims = motion(b, model, bones, meshes, shared, shift)
            does = row.get('does', '')
            screen = ()
            if does == 'tv':
                textures, meshes, screen = tv_screen(textures, meshes)
        except Exception as e:  # leave a piece out rather than the whole conversion
            print('cf_furniture: %s left out (%s)' % (name, e))
            continue
        top = max((v[0][1] for m in meshes for v in m[5]), default=0.0) * SCALE / 10.0
        itm = 0x3700 + slot * 4
        head = struct.pack('<HBBBBHIff', itm, SIZES[row['size']], KINDS[row['kind']],
                           SERIES.index(row['series']) if row['series'] in SERIES else 0xFF, GROUPS[row['group']],
                           does_code(does), price, float(row['height']) * SCALE / 10.0, top)
        head += name.encode('ascii', 'replace')[:31].ljust(32, b'\0')
        blob = piece_blob(textures, meshes, parts, storage, anims, screen)
        out += head + blob
        count += 1
        codes.append({'name': '%s (ac-plus, City Folk)' % name, 'id': '%04X' % itm})
    # City Folk's fish and bugs, put in the house (cf_creatures.tsv): not Nook's (group 3), named after the fish or
    # bug, at 0x3C00 + 4 * n
    from cf_creatures import read_rows, shown_anim, vis_hidden
    sizes = {}
    n = -1
    for row in read_rows():
        n += 1
        sub = 'Fish/Fish%s' if row['kind'] == 'fish' else 'Insect/Insect%s'
        path = os.path.join(item, *(sub % row['placed'] + '.brres').split('/'))
        if not os.path.isfile(path):
            print('cf_furniture: no %s' % path)
            continue
        b = open(path, 'rb').read()
        model = next(iter(brres_folders(b)['3DModels(NW4R)']))
        try:
            # the size of its tank or cage: City Folk's big fish have long ones (a tile is 30 of its units)
            _, _, meshes = bake(b, model, 0.0)
            pts = [v[0] for m in meshes for v in m[5]]
            wide = max(p[0] for p in pts) - min(p[0] for p in pts) > 34.0
            deep = max(p[2] for p in pts) - min(p[2] for p in pts) > 34.0
            size = '2x2' if wide and deep else '2x1' if wide else '1x1'
            shift = 15.0 if size == '2x1' else 0.0
            bones, textures, meshes = bake(b, model, shift)
            meshes = rebind_blended(bones, meshes)
            storage, parts, anims = motion(b, model, bones, meshes, shared, shift, absolute=True)
            sizes[size] = sizes.get(size, 0) + 1
        except Exception as e:
            print('cf_furniture: %s left out (%s)' % (row['name'], e))
            continue
        top = max((v[0][1] for m in meshes for v in m[5]), default=0.0) * SCALE / 10.0
        head = struct.pack('<HBBBBHIff', 0x3C00 + 4 * n, SIZES[size], 0, 0xFF, 3, 0, int(row['sell']) * 4, 0.0, top)
        head += row['name'].encode('ascii', 'replace')[:31].ljust(32, b'\0')
        out += head + piece_blob(textures, meshes, parts, storage, anims)
        count += 1
    print('cf_furniture: tanks and cages %s' % sizes)
    # ... and as themselves, swimming or flying over and over, for the museum: at 0x3D00 + 4 * n
    n = -1
    for row in read_rows():
        n += 1
        path = os.path.join(root, 'Fish' if row['kind'] == 'fish' else 'Insect', row['file'] + '.brres')
        if not os.path.isfile(path):
            continue
        b = open(path, 'rb').read()
        model = next(iter(brres_folders(b)['3DModels(NW4R)']))
        try:
            bones, textures, meshes = bake(b, model, 0.0)
            hidden = vis_hidden(b, shown_anim(row))  # a bug's resting and flying bodies: the one it's showing
            meshes = [m[:5] + ([v for v in m[5] if bones[v[4]]['name'] not in hidden],) for m in meshes]
            meshes = rebind_blended(bones, [m for m in meshes if m[5]])
            storage, parts, anims = motion(b, model, bones, meshes, shared, 0.0, shown_anim(row))
        except Exception as e:
            print('cf_furniture: %s (museum) left out (%s)' % (row['name'], e))
            continue
        # (height: how wide it is, for the game to bring down a bug that would be too big)
        pts = [v[0] for m in meshes for v in m[5]]
        wide = max(max(p[k] for p in pts) - min(p[k] for p in pts) for k in (0, 2)) * SCALE
        head = struct.pack('<HBBBBHIff', 0x3D00 + 4 * n, 0, 0, 0xFF, 3, 0, 0, wide, 0.0)
        head += row['name'].encode('ascii', 'replace')[:31].ljust(32, b'\0')
        out += head + piece_blob(textures, meshes, parts, storage, anims)
        count += 1
    with open(out_path, 'wb') as f:
        f.write(b'CFFT' + struct.pack('<II', VERSION, count) + out)
    print('wrote %s (%d pieces)' % (out_path, count))
    if codes_path:
        import json
        with open(codes_path, 'w') as f:
            json.dump(codes, f, indent=1)
    return count


if __name__ == '__main__':
    import sys
    write_furniture(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else None)
