#!/usr/bin/env python3
"""City Folk player animations -> GameCube keyframe animations (pc/src/pc_cf_anim.c).

City Folk's people and the GameCube player share a rig: the same bones, 125 GameCube skeleton
units to a City Folk unit, rotations applied Z then Y then X. The GameCube skeleton (boy_model.c)
splits some bones in two: an offset joint with a fixed turn (the same in every GameCube
animation, below), and the joint the animation moves. So each City Folk bone's turn becomes its
joint's turn after the offset joint's is taken off.

City Folk animates at 60 frames a second and the GameCube at 30, so every City Folk frame is a key
and the game plays them at speed 1 (pc_cf_anim.c).

.cfpanm, little-endian:
    "CFPA" u32 version (1), u32 animation count, then each:
        name (32 bytes), u16 frames, u16 joint count (26), u8 flags[26] (as cKF: bits 5-3 root
        translation x y z, bits 2-0 rotation x y z), u16 fixed count, s16 fixed[], u16 channel
        count, u16 keys[channels], u32 data count, s16 data[] (frame, value, tangent)
"""
import math
import struct

from cf_anim import read_chr0, sample

# GameCube player joint -> City Folk bone (None: an offset joint, or no bone)
GC_JOINTS = [
    None, 'base', None, 'Lfoot1', 'Lfoot2', 'Lfoot3', None, 'Rfoot1', 'Rfoot2', 'Rfoot3', None, 'tail1', 'tail2',
    'chest', None, 'Larm1', 'Larm2', None, 'Rarm1', 'Rarm2', 'Rhand', 'head', None, 'mouth', None, None,
]
# The GameCube's fixed turns (degrees x y z), of joint 0 and the offset joints
GC_FIXED = {0: (0, 0, 90), 2: (0, 0, 180), 6: (0, 0, 180), 10: (0, 90, 0), 14: (0, 0, -90), 17: (0, 0, 90),
            22: (0, -90, 0), 24: (0, 0, 0), 25: (0, 0, 0)}
# joint 0 carries base's fixed turn; the joint after it (the base model) the rest
GC_PARENT_FIXED = {1: 0, 3: 2, 7: 6, 11: 10, 15: 14, 18: 17, 23: 22}
SCALE = 125.0
# City Folk sits 7 units behind where the GameCube seats the player (chair_sitdown_wait1 against
# ply_1_sitdown_wait1)
SEAT_Z = 7.0


def rot(r):
    x, y, z = [math.radians(v) for v in r]
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    rx = [[1, 0, 0], [0, cx, -sx], [0, sx, cx]]
    ry = [[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]]
    rz = [[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]]
    return mul(mul(rz, ry), rx)


def mul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(3)) for c in range(3)] for r in range(3)]


def tr(a):
    return [[a[c][r] for c in range(3)] for r in range(3)]


def euler(m, prev):
    """Rz Ry Rx -> (x, y, z) degrees, the one of the two solutions nearest prev, unwrapped."""
    sy = max(-1.0, min(1.0, -m[2][0]))
    y = math.degrees(math.asin(sy))
    if abs(sy) < 0.99999:
        x = math.degrees(math.atan2(m[2][1], m[2][2]))
        z = math.degrees(math.atan2(m[1][0], m[0][0]))
    else:  # straight up or down: z takes it all
        x = 0.0
        z = math.degrees(math.atan2(-m[0][1], m[1][1]))
    cands = [(x, y, z), (x + 180.0, 180.0 - y, z + 180.0)]
    best = None
    for c in cands:
        if prev is not None:
            c = tuple(v + 360.0 * round((p - v) / 360.0) for v, p in zip(c, prev))
            d = sum((v - p) ** 2 for v, p in zip(c, prev))
        else:
            c = tuple(v - 360.0 * round(v / 360.0) for v in c)
            d = sum(v * v for v in c)
        if best is None or d < best[0]:
            best = (d, c)
    return best[1]


def gc_frames(anim):
    """-> list over City Folk frames of (root translation, [26 joint rotations in degrees])."""
    out = []
    prev = [None] * 26
    for f in range(anim['frames'] + 1):
        root = sample(anim, 'root', f)
        base = sample(anim, 'base', f)
        t = [SCALE * (root[2][i] + base[2][i]) for i in range(3)]
        t[2] -= SCALE * SEAT_Z
        rots = []
        for j, bone in enumerate(GC_JOINTS):
            if bone is None and j in GC_FIXED:
                r = tuple(float(v) for v in GC_FIXED[j])
            else:
                s = sample(anim, bone, f) if bone else None
                m = rot(s[1]) if s else rot((0, 0, 0))
                if j in GC_PARENT_FIXED:
                    m = mul(tr(rot(GC_FIXED[GC_PARENT_FIXED[j]])), m)
                r = euler(m, prev[j])
            prev[j] = r
            rots.append(r)
        out.append((t, rots))
    return out


def to_cKF(frames):
    """GameCube tables: flags, fixed, key counts, data (one key a frame where a value moves)."""
    n = len(frames)
    chans = []  # (joint, axis, kind) in cKF order: root translation x y z, then each joint's x y z
    for a in range(3):
        chans.append(('t', 0, a, [fr[0][a] for fr in frames]))
    for j in range(26):
        for a in range(3):
            chans.append(('r', j, a, [fr[1][j][a] * 10.0 for fr in frames]))
    flags = [0] * 26
    fixed, keys, data = [], [], []
    for kind, j, a, vals in chans:
        q = [int(round(v)) for v in vals]
        if max(q) == min(q):
            fixed.append(q[0])
            continue
        flags[j] |= (1 << (5 - a)) if kind == 't' else (1 << (2 - a))
        keys.append(n)
        for i in range(n):
            lo, hi = max(i - 1, 0), min(i + 1, n - 1)
            tan = (vals[hi] - vals[lo]) / max(hi - lo, 1) * 30.0
            data += [i + 1, q[i], int(round(max(-32768, min(32767, tan))))]
    # the root's translation flags go with joint 0's rotation flags in flags[0]: keep both
    return flags, fixed, keys, data


def write_cfpanm(path, anims):
    with open(path, 'wb') as f:
        f.write(b'CFPA' + struct.pack('<II', 1, len(anims)))
        for name, frames in anims:
            flags, fixed, keys, data = to_cKF(frames)
            f.write(name.encode()[:31].ljust(32, b'\0') + struct.pack('<HH', len(frames), 26))
            f.write(bytes(flags))
            f.write(struct.pack('<H%dh' % len(fixed), len(fixed), *fixed))
            f.write(struct.pack('<H%dH' % len(keys), len(keys), *keys))
            f.write(struct.pack('<I%dh' % len(data), len(data), *data))


# The player's animations ac-plus borrows
PLAYER_ANIMS = ['cafe_drink1', 'cafe_drink2']


def write_player_anims(b, table, path):
    out = []
    for name in PLAYER_ANIMS:
        if name in table:
            out.append((name, gc_frames(read_chr0(b, table[name]))))
    write_cfpanm(path, out)
    print('wrote %s (%d animations)' % (path, len(out)))


if __name__ == '__main__':
    import sys
    from cf_anim import anims
    b, table = anims(sys.argv[1])
    if len(sys.argv) > 3:
        fr = gc_frames(read_chr0(b, table[sys.argv[3]]))
        t, r = fr[int(sys.argv[4]) if len(sys.argv) > 4 else 0]
        print([round(v) for v in t])
        for j, x in enumerate(r):
            print(j, GC_JOINTS[j], ['%.1f' % v for v in x])
    else:
        write_player_anims(b, table, sys.argv[2])
