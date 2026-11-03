#!/usr/bin/env python3
"""City Folk's fish and bugs the GameCube has none of (cf_creatures.tsv) -> cf_sky/creatures.cfcr: GameCube-style
flat pictures, as the GameCube has of its own: a fish's held up or hooked, a bug's in town (seen from above), and each
one's pocket icon. City Folk has only 3D models, so each is rendered here, flat-lit, from its model at rest, and each is its pocket icon too.

    cf_creatures.py <City Folk files dir> <out.cfcr>        the pictures
    cf_creatures.py --table <out.h>                         pc/include/pc_cf_creature_table.h: the game's table

The file: "CFCR", u32 version, u32 count, then for each one: u16 item, u16 pad, the picture (PIC x PIC RGB5A3, GameCube
tiled).

And cf_sky/creatures.cfln, what's said about them, from City Folk's messages as the GameCube's message bytes: "CFLN",
u32 count, then for each one: u16 item, u16 catch length, u16 Chip length (0 for a bug), u16 Blathers length, the catch
line (a whole message), Chip's line and Blathers' (each the middle of one of his)."""
import os
import struct
import sys

import numpy as np

from cf_furniture import rgb5a3_tiled
from cf_sprites import render, world_tris
from extract_cf_sky import GC_NEWLINE, GC_PAGE, bmg_messages, brres_folders, convert_lines

VERSION = 2
PIC = 128
RENDER = 512  # rendered at this size, then brought down
TABLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'cf_creatures.tsv')


def read_rows(path=TABLE):
    lines = [l.rstrip('\n') for l in open(path) if not l.startswith('#')]
    head = lines[0].split('\t')
    return [dict(zip(head, l.split('\t'))) for l in lines[1:] if l]


def month_mask(s):
    m = 0
    for part in s.split(','):
        a, _, b = part.partition('-')
        a = int(a)
        b = int(b) if b else a
        i = a
        while True:
            m |= 1 << (i - 1)
            if i == b:
                break
            i = i % 12 + 1
    return m


def hour_mask(s):
    if s == 'all':
        return 0xFFFFFF
    m = 0
    for part in s.split(','):
        a, b = (int(x) for x in part.split('-'))
        i = a
        while i != b:
            m |= 1 << i
            i = (i + 1) % 24
    return m


def write_table(out):
    rows = read_rows()
    with open(out, 'w') as f:
        f.write('/* Made by pc/tools/cf_creatures.py --table from pc/tools/cf_creatures.tsv */\n')
        f.write('/* name, sell, months, hours, place, weight, base, size, placed file number, n (furniture 0x3C00 + 4 * n) */\n')
        for kind in ('fish', 'bug'):
            f.write('#define PC_CF_%s_NUM %d\n' % (kind.upper(), sum(r['kind'] == kind for r in rows)))
        n = 0
        for kind, macro, area, base in (('fish', 'FISH', 'aSOG_SPAWN_AREA_', 'aGYO_SIZE_'),
                                        ('bug', 'BUG', 'aSOI_SPAWN_AREA_', 'aINS_INSECT_TYPE_')):
            f.write('#define PC_CF_%s_TABLE(X) \\\n' % macro)
            for r in rows:
                if r['kind'] == kind:
                    f.write('    X("%s", %s, 0x%03X, 0x%06X, %s%s, %s, %s%s, %s, %s, %d) \\\n' % (
                        r['name'], r['sell'], month_mask(r['months']), hour_mask(r['hours']), area, r['place'],
                        r['weight'], base, r['base'], r['size'], r['placed'], n))
                    n += 1
            f.write('\n')


def item_of(r):
    return (0x2330 if r['kind'] == 'fish' else 0x2D30) + int(r['slot'])


def vis_hidden(b, anim):
    """The bones the animation's VIS0 hides at its first frame: City Folk's bugs have a resting body and a flying one
    in one model, and show one or the other."""
    folder = brres_folders(b).get('AnmVis(NW4R)', {})
    if anim not in folder:
        return set()
    off = folder[anim]
    be32 = lambda o: struct.unpack('>I', b[o:o + 4])[0]
    group = off + be32(off + 0x10)
    count = be32(group + 4)
    hidden = set()
    for i in range(1, count + 1):
        e = group + 8 + 16 * i
        data = group + struct.unpack('>i', b[e + 12:e + 16])[0]
        name_at = group + struct.unpack('>i', b[e + 8:e + 12])[0]
        name = b[name_at:b.index(0, name_at)].decode('ascii', 'replace')
        flags = be32(data + 4)
        if flags & 1:  # the same all through
            shown = bool(flags & 2)
        else:
            shown = bool(be32(data + 8) >> 31)
        if not shown:
            hidden.add(name)
    return hidden


def shown_anim(row):
    """The animation a fish or bug is shown with: its flying one only for what's always on the wing"""
    if row['kind'] == 'fish':
        return 'swim'
    return 'fly' if 'BUTTERFLY' in row['base'] or 'DRAGONFLY' in row['base'] else 'wait'


def smooth_textures(textures):
    """Each texture 4 times bigger, smoothly, so the render below doesn't show its pixels"""
    from PIL import Image
    out = []
    for name, w, h, px in textures:
        im = Image.frombytes('RGBA', (w, h), bytes(px)).resize((w * 4, h * 4), Image.BICUBIC)
        out.append((name, w * 4, h * 4, im.tobytes()))
    return out


def fish_view(tris, name):
    allp = np.array([p for t in tris for p in t[0]])
    ext = allp.max(0) - allp.min(0)
    if name in LENGTHWISE:  # its length up the model's y: the two longest ways, lying along
        order = sorted(range(3), key=lambda i: -ext[i])
        return (order[0], order[1], order[2])
    return 'top' if ext[1] < min(ext[0], ext[2]) * 0.45 else 'side'


def finish(big, stretch):
    """A PIC x PIC picture from a big render: cut to what's in it, filling the square; thin ones made less thin (a
    long thin fish would be a line in a pocket's circle); bright as the menus' are; and outlined as the GameCube's are"""
    from PIL import Image, ImageFilter
    ys, xs = np.nonzero(big[..., 3])
    if len(xs) == 0:
        return np.zeros((PIC, PIC, 4), np.uint8)
    cut = big[ys.min():ys.max() + 1, xs.min():xs.max() + 1].astype(np.float32)
    cut[..., :3] *= cut[..., 3:4] / 255.0  # (premultiplied, so edges don't bring in the black around them)
    h, w = cut.shape[:2]
    im = Image.fromarray(cut.astype(np.uint8), 'RGBA')
    if stretch and min(w, h) < 0.45 * max(w, h):
        if h < w:
            h = int(0.45 * w)
        else:
            w = int(0.45 * h)
    inner = PIC - 12
    k = inner / max(w, h)
    im = im.resize((max(1, int(w * k)), max(1, int(h * k))), Image.LANCZOS)
    canvas = Image.new('RGBA', (PIC, PIC), (0, 0, 0, 0))
    canvas.paste(im, ((PIC - im.width) // 2, (PIC - im.height) // 2))
    a = np.array(canvas).astype(np.float32)
    alpha = a[..., 3:4] / 255.0
    rgb = np.where(alpha > 0, a[..., :3] / np.maximum(alpha, 1e-3), 0)
    rgb = np.clip(rgb * 1.15 + 6.0, 0, 255)
    solid = alpha[..., 0] >= 0.5
    grown = np.array(Image.fromarray((solid * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(5))) > 0
    line = grown & ~solid
    out = np.zeros((PIC, PIC, 4), np.uint8)
    out[solid, :3] = rgb[solid].astype(np.uint8)
    out[solid, 3] = 255
    out[line, :3] = (40, 34, 44)
    out[line, 3] = 255
    return out


# Fish whose model lies along its height, or that make more sense from above: turned to lie along, head to the left
FROM_ABOVE = {'lobster'}
TURN = {'lobster', 'dab', 'olive flounder', 'squid'}
LENGTHWISE = {'moray eel'}


SAY = bytes((0x7F, 0x05, 0x1E, 0x3C, 0x1E))  # the player's green: the GameCube's catch lines start each line with it
LAST = bytes((GC_NEWLINE, 0x7F, 0x01))
CHIP_LINES = 100  # his line on a fish is this many on from its catch line


def catch_message(toks):
    """A catch line as the player says it: every line in their green; its parts (blank lines between them) kept
    whole, on as many windows of four lines as they need"""
    parts, part = [], []
    for i, t in enumerate(toks):
        if t == '\n' and i + 1 < len(toks) and toks[i + 1] == '\n':
            parts.append(part)
            part = []
        elif t == '\n' and not part:
            continue
        else:
            part.append(t)
    parts.append(part)
    page = []
    pages = [page]
    for part in parts:
        lines = [SAY + l if l else b'' for l in convert_lines(part)[0].split(bytes((GC_NEWLINE,)))]
        lines = [l for l in lines if l]
        if not lines:
            continue
        if page and len(page) + 1 + len(lines) > 4:
            page = []
            pages.append(page)
        page += ([b''] if page else []) + lines
    newline = bytes((GC_NEWLINE,))
    return GC_PAGE.join(newline.join(p) for p in pages) + LAST


def write_lines(root, out):
    script = os.path.join(root, 'Script', 'ENG')
    read = lambda *path: bmg_messages(open(os.path.join(script, *path), 'rb').read())
    fish, bugs = read('sys_MESS', 'SYS_Get_fish.bmg'), read('sys_MESS', 'SYS_Get_bug.bmg')
    chip = read('npc_SP', 'NPC_uomasa.bmg')
    fish_talk, bug_talk = read('npc_SP', 'NPC_hu-ta_fish.bmg'), read('npc_SP', 'NPC_hu-ta_bug.bmg')
    rows = read_rows()
    with open(out, 'wb') as f:
        f.write(b'CFLN' + struct.pack('<I', len(rows)))
        for r in rows:
            n = int(r['line'])
            caught = catch_message((fish if r['kind'] == 'fish' else bugs)[n])
            said = convert_lines(chip[n + CHIP_LINES])[0] if r['kind'] == 'fish' else b''
            talk = convert_lines((fish_talk if r['kind'] == 'fish' else bug_talk)[n])[0]
            if len(talk) > 1300:
                sys.exit('cf_creatures: what Blathers says of %s is too long (%d)' % (r['name'], len(talk)))
            f.write(struct.pack('<HHHH', item_of(r), len(caught), len(said), len(talk)) + caught + said + talk)
    print('wrote %s (%d)' % (out, len(rows)))


def main(root, out, sheet=None):
    import PIL  # noqa: F401 (needed below: missing, it must stop the conversion, not leave every picture out)
    rows = read_rows()
    blob = bytearray()
    pics = []
    n = 0
    for r in rows:
        sub = 'Fish' if r['kind'] == 'fish' else 'Insect'
        path = os.path.join(root, sub, r['file'] + '.brres')
        try:
            b = open(path, 'rb').read()
            model = next(iter(brres_folders(b)['3DModels(NW4R)']))
            tris, textures = world_tris(b, model, vis_hidden(b, shown_anim(r)))
            textures = smooth_textures(textures)
            view = 'top' if r['kind'] == 'bug' or r['name'] in FROM_ABOVE else fish_view(tris, r['name'])
            big = render(tris, textures, RENDER, view)
            if r['name'] in TURN:
                big = np.rot90(big, -1).copy()
            pic = finish(big, r['kind'] == 'fish')
        except Exception as e:  # leave it out rather than the whole conversion
            print('cf_creatures: %s left out (%s)' % (r['name'], e))
            continue
        blob += struct.pack('<HH', item_of(r), 0) + rgb5a3_tiled(PIC, PIC, pic.tobytes())
        pics.append(pic)
        n += 1
    with open(out, 'wb') as f:
        f.write(b'CFCR' + struct.pack('<II', VERSION, n) + blob)
    print('cf_creatures: %d' % n)
    if sheet:
        from PIL import Image
        cols = 10
        s = Image.new('RGBA', (cols * 100, ((n + cols - 1) // cols) * 100), (60, 70, 90, 255))
        for i, p in enumerate(pics):
            s.alpha_composite(Image.fromarray(p, 'RGBA').resize((96, 96), Image.LANCZOS), (i % cols * 100 + 2, i // cols * 100 + 2))
        s.save(sheet)


if __name__ == '__main__':
    if sys.argv[1] == '--table':
        write_table(sys.argv[2])
    else:
        main(*sys.argv[1:4])
