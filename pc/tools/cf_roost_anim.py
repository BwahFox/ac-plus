#!/usr/bin/env python3
"""The Roost's City Folk animations -> cf_sky/anims/roost.cfanim (pc/src/pc_cf_anim.c).

Brewster's own animations (pouring, the pigeon milk, handing the cup over, polishing a cup while he
waits) and the cups, saucer, pot and cloth that go with them, and the cup and saucer while the
player drinks (the player's own body is a GameCube animation: cf_player_anim.py).

City Folk keeps the props in two ways. While a character holds one, its root stays put and its
model's offset is from the hand: Brewster's left hand holds the cup and saucer, his right the pot
and the cloth. Set down, the prop's root is in the café's own space. Everything here is baked
into one of two frames, so the game only places those:
  - Brewster's: where he stands, facing the player (City Folk: (408, 0, 304), facing -x)
  - the seat's: where the player sits, facing him (City Folk: (343, 0, 304), facing +x; the
    player stands 7 behind it, see cf_player_anim.SEAT_Z)
In City Folk he serves from right across the counter; ac-plus has three stools, so the cup slides
from his seat (seat_w 0) to the player's (1) while he pushes it over.

.cfanim, little-endian, City Folk units:
    "CFAN" u32 version (1), f32 seat_in_owner[3][4] (the seat's frame in Brewster's),
    u32 animation count, then each:
        name (32 bytes), u32 frames, f32 seat_w[frames], u32 track count, then each:
            model (16 bytes: a cf_sky/chars name, or "cup" for whichever cup is out),
            u32 bone count, bone names (16 bytes each),
            u8 anchor[frames] (0 hidden, 1 Brewster's frame, 2 the seat's), padded to 4,
            f32 matrix[frames][bones][3][4] (from the anchor; row-major, translation last)
"""
import math
import os
import struct

from cf_anim import read_chr0, sample, anims
from cf_char import convert_char
from extract_cf_sky import brres_folders

BREWSTER_POS, BREWSTER_YAW = (408.0, 0.0, 304.0), -90.0
SEAT_POS, SEAT_YAW = (343.0, 0.0, 304.0), 90.0

PROPS = {  # model name: (file, model in it)
    'polish_cup': ('obj_cafe1', 'obj_cafe1'),
    'duster': ('obj_cafe2', 'obj_cafe2'),
    'cup': ('obj_cafe3', 'obj_cafe3'),
    'saucer': ('obj_cafe4', 'obj_cafe4'),
    'pot': ('obj_cafe5', 'obj_cafe5'),
}

# animation: (Brewster's or None, [(prop, its animation, hand)], seat slide (first, last) or None)
ROOST = [
    ('pge_wait1', True, [('polish_cup', 'obj_cafe1_0', 'Lhand'), ('duster', 'obj_cafe2_0', 'Rhand')], None),
    ('pge_wait2', True, [], None),
    ('pge_walk1', True, [], None),
    ('pge_pour1', True, [('pot', 'obj_cafe5_0', 'Rhand'), ('cup', 'obj_cafe3_3', 'Lhand'),
                         ('saucer', 'obj_cafe4_3', 'Lhand')], None),
    ('pge_milk1', True, [('cup', 'obj_cafe3_10', 'Lhand'), ('saucer', 'obj_cafe4_7', 'Lhand')], None),
    ('pge_hand1', True, [('cup', 'obj_cafe3_6', 'Lhand'), ('saucer', 'obj_cafe4_4', 'Lhand')], (50, 138)),
    ('pge_cls1', True, [('cup', 'obj_cafe3_0', 'Lhand'), ('saucer', 'obj_cafe4_0', 'Lhand')], None),
    ('cafe_drink1', False, [('cup', 'obj_cafe3_7', None), ('saucer', 'obj_cafe4_5', None)], 'seat'),
    ('cafe_drink2', False, [('cup', 'obj_cafe3_8', None), ('saucer', 'obj_cafe4_6', None)], 'seat'),
]
ROOST_BY_NAME = [(r[0], r[1]) for r in ROOST]


# --- 3x4 matrices (rows; translation in column 3) ---

def m_rot(r):
    x, y, z = [math.radians(v) for v in r]
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    # Rz Ry Rx
    return [[cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx, 0.0],
            [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx, 0.0],
            [-sy, cy * sx, cy * cx, 0.0]]


def m_srt(s, r, t):
    m = m_rot(r)
    for i in range(3):
        for k in range(3):
            m[i][k] *= s[k]
        m[i][3] = t[i]
    return m


def m_mul(a, b):
    out = []
    for i in range(3):
        row = [sum(a[i][k] * b[k][c] for k in range(3)) for c in range(4)]
        row[3] += a[i][3]
        out.append(row)
    return out


def m_place(pos, yaw):
    m = m_rot((0.0, yaw, 0.0))
    for i in range(3):
        m[i][3] = pos[i]
    return m


def m_inv_rigid(m):
    r = [[m[c][i] for c in range(3)] for i in range(3)]
    t = [-sum(r[i][k] * m[k][3] for k in range(3)) for i in range(3)]
    return [r[i] + [t[i]] for i in range(3)]


IDENT = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0]]


def is_ident(m, eps=1e-3):
    return all(abs(m[i][k] - IDENT[i][k]) < eps for i in range(3) for k in range(4))


def fk(bones, anim, f, own_lengths=False):
    """bones [(name, parent)] in order -> model-space matrices; bones the animation lacks keep
    their rest translation (unturned). own_lengths: every bone but the root and base keeps the
    model's own offset (the shared animations put Brewster's beak, 'mouth', where it isn't)."""
    out = []
    for name, parent, rest_t in bones:
        s = sample(anim, name, f)
        if s and own_lengths and name not in ('root', 'base'):
            s = (s[0], s[1], rest_t)
        local = m_srt(*s) if s else m_srt((1, 1, 1), (0, 0, 0), rest_t)
        out.append(m_mul(out[parent], local) if parent >= 0 else local)
    return out


def model_bones(path, model):
    b = open(path, 'rb').read()
    bones, _tex, _meshes = convert_char(b, model)
    return b, [(bn['name'], bn['parent'], bn['t']) for bn in bones]


def bake(files_root):
    brw_b, brw_bones = model_bones(os.path.join(files_root, 'Npc/Special/Model/23.brres'), 'pge')
    anm_b, anm_table = anims(os.path.join(files_root, 'Anm/Anm.brcha'))
    props = {}
    for name, (fname, model) in PROPS.items():
        path = os.path.join(files_root, 'Rmobj', fname + '.brres')
        pb, pbones = model_bones(path, model)
        props[name] = (pb, pbones, brres_folders(pb).get('AnmChr(NW4R)', {}))
    hand = {n: i for i, (n, p, t) in enumerate(brw_bones)}

    room_to_owner = m_inv_rigid(m_place(BREWSTER_POS, BREWSTER_YAW))
    room_to_seat = m_inv_rigid(m_place(SEAT_POS, SEAT_YAW))
    seat_in_owner = m_mul(room_to_owner, m_place(SEAT_POS, SEAT_YAW))

    out = []
    pot_rest = None
    for name, brewster, prop_list, slide in ROOST:
        if name not in anm_table:
            continue
        a = read_chr0(anm_b, anm_table[name])
        n = a['frames'] + 1
        if slide == 'seat':
            seat_w = [1.0] * n
        elif slide:
            lo, hi = slide
            seat_w = [min(1.0, max(0.0, (f - lo) / float(hi - lo))) for f in range(n)]
            seat_w = [w * w * (3 - 2 * w) for w in seat_w]
        else:
            seat_w = [0.0] * n
        brw_frames = [fk(brw_bones, a, f, own_lengths=True) for f in range(n)] if brewster else None
        tracks = []
        if brewster:
            tracks.append(('brewster', [b[0] for b in brw_bones], [1] * n, brw_frames))
        for prop, panim_name, held_by in prop_list:
            pb, pbones, ptable = props[prop]
            if panim_name not in ptable:
                continue
            pa = read_chr0(pb, ptable[panim_name])
            anchors, mats = [], []
            rest = None  # where a prop not in hand waits (the pot, on the counter)
            for f in range(n):
                local = fk(pbones, pa, min(f, pa['frames']))
                if is_ident(local[0]) and held_by:
                    if all(is_ident(m) for m in local) and prop == 'pot':
                        anchors.append(1)
                        mats.append(None)  # filled in below, from where the hand picks it up
                        continue
                    h = brw_frames[f][hand[held_by]]
                    anchors.append(1)
                    mats.append([m_mul(h, m) for m in local])
                    if prop == 'pot' and rest is None:
                        rest = mats[-1]
                elif not is_ident(local[0]) or not held_by:
                    # on the counter: City Folk's room -> the seat's frame
                    if brewster and seat_w[f] < 1.0:
                        anchors.append(1)
                        mats.append([m_mul(room_to_owner, m) for m in local])
                    else:
                        anchors.append(2)
                        mats.append([m_mul(room_to_seat, m) for m in local])
                else:
                    anchors.append(0)
                    mats.append([IDENT] * len(local))
            if prop == 'pot':
                rest = rest or [IDENT] * len(pbones)
                pot_rest = (pbones, rest)
                mats = [m if m is not None else rest for m in mats]
            tracks.append(('cup' if prop == 'cup' else prop, [b[0] for b in pbones], anchors, mats))
        out.append((name, n, seat_w, tracks))
    # the pot waits under the siphon whatever else he's doing
    for name, n, seat_w, tracks in out:
        if pot_rest and dict(ROOST_BY_NAME)[name] and not any(t[0] == 'pot' for t in tracks):
            pbones, rest = pot_rest
            tracks.append(('pot', [b[0] for b in pbones], [1] * n, [rest] * n))
    return seat_in_owner, out


def write_cfanim(path, seat_in_owner, anims_out):
    with open(path, 'wb') as f:
        f.write(b'CFAN' + struct.pack('<I', 1))
        f.write(struct.pack('<12f', *[v for row in seat_in_owner for v in row]))
        f.write(struct.pack('<I', len(anims_out)))
        for name, n, seat_w, tracks in anims_out:
            f.write(name.encode()[:31].ljust(32, b'\0') + struct.pack('<I', n))
            f.write(struct.pack('<%df' % n, *seat_w))
            f.write(struct.pack('<I', len(tracks)))
            for model, names, anchors, mats in tracks:
                f.write(model.encode()[:15].ljust(16, b'\0') + struct.pack('<I', len(names)))
                for bn in names:
                    f.write(bn.encode()[:15].ljust(16, b'\0'))
                f.write(bytes(anchors) + b'\0' * (-n % 4))
                for frame in mats:
                    for m in frame:
                        f.write(struct.pack('<12f', *[v for row in m for v in row]))


def write_roost_anims(files_root, out_dir):
    import cf_player_anim
    os.makedirs(out_dir, exist_ok=True)
    anm = os.path.join(files_root, 'Anm', 'Anm.brcha')
    if not os.path.isfile(anm):
        print('no %s: the Roost keeps its simple moves' % anm)
        return
    seat_in_owner, out = bake(files_root)
    path = os.path.join(out_dir, 'roost.cfanim')
    write_cfanim(path, seat_in_owner, out)
    print('wrote %s (%d animations)' % (path, len(out)))
    b, table = anims(anm)
    cf_player_anim.write_player_anims(b, table, os.path.join(out_dir, 'player.cfpanm'))


if __name__ == '__main__':
    import sys
    write_roost_anims(sys.argv[1], sys.argv[2])
