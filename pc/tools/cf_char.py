#!/usr/bin/env python3
"""City Folk characters -> .cfchar files for pc/src/pc_cf_char.c.

Part of extract_cf_sky.py: reads a character's model (files/Npc/Special/Model/*.brres) and writes
its bones, textures and triangles. The game poses it with a GameCube skeleton's animation: City
Folk's bones carry the same names and point the same way as the GameCube's joints, so each bone
takes its joint's rotation and keeps its own length (pc_brewster.c).

.cfchar, little-endian:
    "CFCH" u32 version (1)
    u32 bone count, then each: name (16 bytes), s32 parent (-1 for none), f32 translation[3] (in the
        parent's frame), f32 rest rotation[3][3] (relative to the parent, row-major)
    u32 texture count, then each: name (32 bytes), u16 width, u16 height, RGBA8 pixels
    u32 mesh count, then each: name (32 bytes), i32 texture (-1 none), u8 xlu, u8 cull,
        u8 alpha_test, u8 wrap (s | t << 2: 0 clamp, 1 repeat, 2 mirror), u32 vertex count, then
        vertices (triangle list): u8 bone, u8 pad[3], f32 x y z, f32 nx ny nz (in the bone's
        frame), f32 u v
Positions are in City Folk units; the GameCube's are 148.8 times bigger.
"""
import struct

from cf_rooms import section, read_array, s16, triangles, draw_list, palette_any, decode_texture
from extract_cf_sky import brres_folders, u16, u32, s32
from celeste_tex import read_object, mat34

# The characters and props ac-plus borrows: file, model name, output name
CHARS = [
    ('Npc/Special/Model/23.brres', 'pge', 'brewster'),
    # the Roost's cups: black, with pigeon milk, and empty
    ('Rmobj/obj_cafe3.brres', 'obj_cafe3', 'cup_coffee'),
    ('Rmobj/obj_cafe6.brres', 'obj_cafe6', 'cup_milk'),
    # the empty cup is the full one without its coffee: obj_cafe1 only stands up through its animation
    ('Rmobj/obj_cafe3.brres', 'obj_cafe3', 'cup_empty', ('m_cff',)),
    # what Brewster handles in City Folk's animations (cf_roost_anim.py): the saucer, the coffee pot, and the
    # cup and cloth he polishes while he waits
    ('Rmobj/obj_cafe4.brres', 'obj_cafe4', 'saucer'),
    ('Rmobj/obj_cafe5.brres', 'obj_cafe5', 'pot'),
    ('Rmobj/obj_cafe1.brres', 'obj_cafe1', 'polish_cup'),
    ('Rmobj/obj_cafe2.brres', 'obj_cafe2', 'duster'),
]


def cstr(b, o):
    return b[o:b.index(b'\0', o)].decode()


def mat_rot(m):
    return [list(m[r][:3]) for r in range(3)]


def mat_mul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(3)) for c in range(3)] for r in range(3)]


def mat_t(a):
    return [[a[c][r] for c in range(3)] for r in range(3)]


def convert_char(b, model):
    fo = brres_folders(b)
    mo = fo['3DModels(NW4R)'][model]

    # bones, in file order (parents come first)
    bone_off = list(section(b, mo, 1).items())
    by_off = {o: i for i, (n, o) in enumerate(bone_off)}
    by_node = {}
    bones = []
    for i, (name, o) in enumerate(bone_off):
        p = s32(b, o + 0x5C)
        parent = by_off.get(o + p, -1) if p else -1
        world = mat34(b, o + 0x70)
        by_node[u32(b, o + 0x10)] = i
        bones.append({'name': name, 'parent': parent, 't': struct.unpack_from('>3f', b, o + 0x38),
                      'world': world})
    for bn in bones:
        r = mat_rot(bn['world'])
        if bn['parent'] >= 0:
            # rotations are orthonormal: the inverse is the transpose
            r = mat_mul(mat_t(mat_rot(bones[bn['parent']]['world'])), r)
        bn['rest'] = r

    # textures, each with its material's palette
    tex_pal = {}
    wraps = {}
    for mname, o in section(b, mo, 8).items():
        if not u32(b, o + 0x2C):
            continue
        lo = o + s32(b, o + 0x30)
        wraps[mname] = (min(u32(b, lo + 0x18), 2), min(u32(b, lo + 0x1C), 2))
        tname = cstr(b, lo + s32(b, lo))
        # eye frames e.0-e.7 share the palette of the one the material names
        tex_pal[tname.split('.')[0]] = cstr(b, lo + s32(b, lo + 4)) if s32(b, lo + 4) else None
        wraps[mname] += (tname,)
    pals = fo.get('Palettes(NW4R)', {})
    textures, tex_index = [], {}
    for tname, to in fo.get('Textures(NW4R)', {}).items():
        pname = tex_pal.get(tname.split('.')[0])
        pal = palette_any(b, pals[pname]) if pname in pals else palette_any(b, next(iter(pals.values()))) if pals else None
        w, h, px = decode_texture(b, to, pal)
        tex_index[tname] = len(textures)
        textures.append((tname, w, h, bytes(px)))

    verts = {i: read_array(b, o, 3, True) for i, (n, o) in enumerate(section(b, mo, 2).items())}
    nrms = {i: read_array(b, o, 3, False) for i, (n, o) in enumerate(section(b, mo, 3).items())}
    uvs = {i: read_array(b, o, 2, True) for i, (n, o) in enumerate(section(b, mo, 5).items())}
    mats = list(section(b, mo, 8).items())
    objs = list(section(b, mo, 10).items())
    defs = section(b, mo, 0)
    order = [(m, ob, 0) for m, ob in draw_list(b, defs['DrawOpa'])] if 'DrawOpa' in defs else []
    order += [(m, ob, 1) for m, ob in draw_list(b, defs['DrawXlu'])] if 'DrawXlu' in defs else []
    meshes = []
    for mat, obj, xlu in order:
        oname, oo = objs[obj]
        ids, tris, node, _table = read_object(b, oo)
        pos, nrm = verts[ids['pos']], nrms.get(ids['nrm'], [])
        uv = uvs.get(ids['uv'], [])
        mname, mo_ = mats[mat]
        cull = u32(b, mo_ + 0x18)
        ws, wt, tname = wraps.get(mname, (1, 1, None))
        tex = tex_index.get(tname, -1)
        alpha = 1 if tex >= 0 and any(px < 255 for px in textures[tex][3][3::4]) else 0
        out = []
        for t in tris:
            for v in t:
                nd = v['pnmtx'] if 'pnmtx' in v else node
                n = nrm[v['nrm']] if 'nrm' in v and v['nrm'] < len(nrm) else (0.0, 1.0, 0.0)
                st = uv[v['uv']] if 'uv' in v and v['uv'] < len(uv) else (0.0, 0.0)
                out.append((by_node.get(nd, 0),) + tuple(pos[v['pos']]) + tuple(n) + tuple(st))
        meshes.append((mname + '/' + oname, tex, xlu, cull, alpha, ws | wt << 2, out))
    return bones, textures, meshes


def write_cfchar(path, bones, textures, meshes):
    with open(path, 'wb') as f:
        f.write(b'CFCH' + struct.pack('<I', 1))
        f.write(struct.pack('<I', len(bones)))
        for bn in bones:
            f.write(bn['name'].encode()[:15].ljust(16, b'\0') + struct.pack('<i3f', bn['parent'], *bn['t']))
            f.write(struct.pack('<9f', *[x for row in bn['rest'] for x in row]))
        f.write(struct.pack('<I', len(textures)))
        for name, w, h, px in textures:
            f.write(name.encode()[:31].ljust(32, b'\0') + struct.pack('<HH', w, h) + px)
        f.write(struct.pack('<I', len(meshes)))
        for name, tex, xlu, cull, alpha, wrap, verts in meshes:
            f.write(name.encode()[:31].ljust(32, b'\0') + struct.pack('<iBBBBI', tex, xlu, min(cull, 3), alpha, wrap,
                                                                     len(verts)))
            for v in verts:
                f.write(struct.pack('<B3x8f', *v))


def write_chars(root, out_dir):
    import os
    os.makedirs(out_dir, exist_ok=True)
    for rel, model, name, *drop in CHARS:
        path = os.path.join(root, *rel.split('/'))
        if not os.path.isfile(path):
            print('no %s: no %s' % (path, name))
            continue
        bones, textures, meshes = convert_char(open(path, 'rb').read(), model)
        if drop:
            meshes = [m for m in meshes if m[0].split('/')[0] not in drop[0]]
        out = os.path.join(out_dir, name + '.cfchar')
        write_cfchar(out, bones, textures, meshes)
        print('wrote %s (%d bones, %d meshes, %d triangles)' % (out, len(bones), len(meshes),
                                                                 sum(len(m[6]) for m in meshes) // 3))


if __name__ == '__main__':
    import sys
    write_chars(sys.argv[1], sys.argv[2])
