#!/usr/bin/env python3
"""Extract what ac-plus borrows from your own copy of Animal Crossing: City Folk:
the sky textures (clouds, stars, moon) for the curved-world sky (pc/src/pc_sky.c),
the museum's observatory and café (pc/src/pc_cf_room.c), Celeste's lines and
textures (pc/src/pc_celeste.c), Brewster's lines and model (pc/src/pc_brewster.c), the
Roost's music (pc/tools/cf_music.py), and City Folk's animations for the Roost: Brewster's and
the player's at the counter (pc/src/pc_cf_anim.c).

Usage:
    extract_cf_sky.py <City Folk .wbfs/.iso, or its extracted files folder> <bin dir>

Writes <bin dir>/cf_sky/{cloud,star,moon}.tga, cf_sky/rooms/*.cfroom,
cf_sky/celeste.{msg,tex}, cf_sky/brewster.msg, cf_sky/chars/*.cfchar,
cf_sky/anims/{roost.cfanim,player.cfpanm}, cf_sky/music/roost.wav and cf_sky/sfx/bite.wav (both
need numpy). The sky works without them, just without clouds, stars
and moon; Celeste only comes to the museum when her lines are there. Her textures
are fitted to Blathers' model, read from the GameCube disc in <bin dir> (rom/).
Reading a City Folk disc image needs `wit` (Wiimms ISO Tools).
"""
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # cf_rooms.py


def u16(b, o):
    return struct.unpack_from('>H', b, o)[0]


def u32(b, o):
    return struct.unpack_from('>I', b, o)[0]


def s32(b, o):
    return struct.unpack_from('>i', b, o)[0]


# --- BRRES archive ---

def brres_name(b, o):
    return b[o:o + u32(b, o - 4)].decode('ascii', 'replace')


def brres_group(b, go):
    """Index group at go -> {name: data offset}."""
    out = {}
    for i in range(1, u32(b, go + 4) + 1):
        e = go + 8 + 16 * i
        out[brres_name(b, go + s32(b, e + 8))] = go + s32(b, e + 12)
    return out


def brres_folders(b):
    if b[0:4] != b'bres':
        raise ValueError('not a BRRES file')
    root = u16(b, 0xc)
    return {name: brres_group(b, off) for name, off in brres_group(b, root + 8).items()}


# --- GX texture formats ---

def rgb5a3(v):
    if v & 0x8000:
        return ((v >> 10 & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v & 31) * 255 // 31, 255)
    return ((v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17, (v >> 12 & 7) * 255 // 7)


def palette(b, off):
    fmt, n = u32(b, off + 0x18), u16(b, off + 0x1c)
    data = off + u32(b, off + 0x10)
    if fmt != 2:
        raise ValueError('only RGB5A3 palettes are handled')
    return [rgb5a3(u16(b, data + 2 * i)) for i in range(n)]


def decode(b, off, pal=None):
    """TEX0 at off -> (width, height, rows of RGBA tuples)."""
    w, h, fmt = u16(b, off + 0x1c), u16(b, off + 0x1e), u32(b, off + 0x20)
    data = off + u32(b, off + 0x10)
    blocks = {0: (8, 8), 1: (8, 4), 8: (8, 8)}  # I4, I8, CI4
    if fmt not in blocks:
        raise ValueError('texture format %d not handled' % fmt)
    bw, bh = blocks[fmt]
    px = [[(0, 0, 0, 0)] * w for _ in range(h)]
    nib = 0  # position in 4-bit units
    for by in range(0, h, bh):
        for bx in range(0, w, bw):
            for y in range(by, by + bh):
                for x in range(bx, bx + bw):
                    if fmt == 1:
                        v = b[data + nib // 2]
                        nib += 2
                        c = (v, v, v, 255)
                    else:
                        v = b[data + nib // 2]
                        v = v >> 4 if nib % 2 == 0 else v & 15
                        nib += 1
                        c = (v * 17, v * 17, v * 17, 255) if fmt == 0 else pal[min(v, len(pal) - 1)]
                    if x < w and y < h:
                        px[y][x] = c
    return w, h, px


def write_tga(path, w, h, px):
    with open(path, 'wb') as f:
        # uncompressed true colour, 32 bpp, 8 alpha bits, top-left origin
        f.write(struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, w, h, 32, 0x28))
        for row in px:
            for r, g, bl, a in row:
                f.write(bytes((bl, g, r, a)))


def texture(path, name, palette_name=None):
    b = open(path, 'rb').read()
    folders = brres_folders(b)
    pal = palette(b, folders['Palettes(NW4R)'][palette_name]) if palette_name else None
    return decode(b, folders['Textures(NW4R)'][name], pal)


WANTED = [
    # output, brres, texture, palette
    ('cloud.tga', 'bg_cloud.brres', 'tex_cloud', None),
    ('star.tga', 'bg_star.brres', 'tex_star', None),
    ('moon.tga', 'bg_moon.brres', 'tex_moon', 'pl_moon'),
]


# --- Celeste's messages: City Folk BMG -> the GameCube game's message codes ---

GC_NEWLINE = 0xCD
GC_PAGE = bytes((0x7F, 0x04, GC_NEWLINE, 0x7F, 0x02))   # wait for A, clear
GC_PLAYER = bytes((0x7F, 0x1A))
GC_TOWN = bytes((0x7F, 0x2F))
GC_FREE = [bytes((0x7F, 0x24 + i)) for i in range(6)]  # PUT_STRING_FREE0..: 0 name, 1 day, 2 month, 3 hour, 4 minute, 5 am/pm
CHAR_MAP = {'\u2019': "'", '\u2018': "'", '\u201c': '"', '\u201d': '"', '\u2026': '...', '\u00e9': 'e',
            '\u00e8': 'e', '\u2013': '-', '\u2014': '-'}
CHOICE_LEN = 16
GENDER_FREE = 1  # free string the speaker fills with the player's "guy"/"gal" word


def bmg_messages(b):
    """BMG (UTF-16) -> list of token lists: str characters, ('code', group, type, args) tuples."""
    if b[:8] != b'MESGbmg1':
        raise ValueError('not a BMG file')
    sections = {}
    o = 0x20
    for _ in range(u32(b, 0xC)):
        sections[b[o:o + 4]] = o
        o += u32(b, o + 4)
    inf, dat = sections[b'INF1'], sections[b'DAT1']
    count, entry = u16(b, inf + 8), u16(b, inf + 10)
    out = []
    for i in range(count):
        p = dat + 8 + u32(b, inf + 16 + i * entry)
        toks = []
        while True:
            c = u16(b, p)
            if c == 0:
                break
            if c == 0x1A:
                n = b[p + 2]
                toks.append(('code', b[p + 3], u16(b, p + 4), b[p + 6:p + n]))
                p += n
                continue
            toks.append(chr(c))
            p += 2
        out.append(toks)
    return out


def celeste_free_slot(args):
    """Which free string a City Folk text variable becomes, by the label in it."""
    raw = args[1:]  # a length byte, then the label in UTF-16
    label = raw[:len(raw) // 2 * 2].decode('utf-16-be', 'replace')
    for key, slot in (('MONTH', 2), ('DAY', 1), ('AM', 5)):
        if key in label:
            return slot
    return 0


def convert_lines(toks):
    """-> (body bytes, [choice labels]) in the GameCube message format."""
    choice_num = 0
    lines = [[]]
    for t in toks:
        if isinstance(t, str):
            if t == '\n':
                lines.append([])
            else:
                for ch in CHAR_MAP.get(t, t):
                    lines[-1].append(bytes((ord(ch),)) if 32 <= ord(ch) < 127 else b'?')
            continue
        _, group, typ, args = t
        if group == 5:
            lines[-1].append(GC_TOWN if typ == 5 else GC_PLAYER)
        elif group == 6 and typ == 0 and len(args) >= 2:      # pause
            lines[-1].append(bytes((0x7F, 0x03, min(args[1], 0x30))))
        elif group == 7:                                      # a choice: the labels are the lines after it
            choice_num = (len(args) // 2) if args else typ - 2
            lines.append(['CHOICES'])
        elif group == 4:
            lines[-1].append(GC_FREE[celeste_free_slot(args)])
        elif group == 3 and typ == 5:                         # Brewster's "176.<decimals> degrees": none
            if lines[-1] and lines[-1][-1] == b'.':
                lines[-1].pop()
        elif group == 3:                                      # a number: hours, then minutes
            lines[-1].append(GC_FREE[4 if args[:1] == b'\x01' else 3])
        elif group == 12 and len(args) > 4:                   # "guy"/"gal" by the player: the actor fills it
            lines[-1].append(GC_FREE[GENDER_FREE])
    body_lines, labels = lines, []
    for i, line in enumerate(lines):
        if line == ['CHOICES']:
            body_lines = lines[:i]
            labels = [b''.join(l).decode('ascii', 'replace').strip() for l in lines[i + 1:]][:choice_num or 4]
            break
    # City Folk's window shows four lines; blank lines pad each page out
    pages = []
    for k in range(0, len(body_lines), 4):
        page = [b''.join(l) for l in body_lines[k:k + 4]]
        while page and page[-1] == b'':
            page.pop()
        while page and page[0] == b'':
            page.pop(0)
        if page:
            pages.append(bytes((GC_NEWLINE,)).join(page))
    return GC_PAGE.join(pages), [l for l in labels if l]


def write_lines(bmg_path, out_path):
    msgs = bmg_messages(open(bmg_path, 'rb').read())
    with open(out_path, 'wb') as f:
        f.write(b'CELE' + struct.pack('<H', len(msgs)))
        for toks in msgs:
            body, labels = convert_lines(toks)
            f.write(struct.pack('<H', len(body)) + body + bytes((len(labels),)))
            for l in labels:
                f.write(l.encode('ascii', 'replace')[:CHOICE_LEN].ljust(CHOICE_LEN, b' '))
    print('wrote %s (%d messages)' % (out_path, len(msgs)))


# The rooms ac-plus borrows: the observatory and the Roost (pc/src/pc_cf_room.c)
ROOMS = ['idr_ms_astro', 'idr_cafe', 'idr_entrance']


def write_rooms(bgmodel, out):
    import cf_rooms
    if not os.path.isdir(bgmodel):
        print('no %s: no City Folk rooms' % bgmodel)
        return
    os.makedirs(out, exist_ok=True)
    for name in sorted(os.listdir(bgmodel)):
        b = open(os.path.join(bgmodel, name), 'rb').read()
        try:
            models = list(brres_folders(b).get('3DModels(NW4R)', {}))
        except ValueError:
            continue
        for room in ROOMS:
            if room in models:
                textures, meshes = cf_rooms.convert_room(b, room)
                cf_rooms.write_cfroom(os.path.join(out, room + '.cfroom'), textures, meshes)
                print('wrote %s (%d meshes)' % (os.path.join(out, room + '.cfroom'), len(meshes)))


def find_files_root(path):
    """The disc's files folder (DATA/files or files), or path itself."""
    for cand in (os.path.join(path, 'DATA', 'files'), os.path.join(path, 'files'), path):
        if os.path.isdir(os.path.join(cand, 'Sky')) or os.path.isdir(os.path.join(cand, 'Script')):
            return cand
    return path


# The game checks cf_sky/version.txt against this (PC_CF_DATA_VERSION in pc/include/pc_cf_data.h) and asks
# for a fresh extraction when they differ: raise both whenever what's written here changes.
CF_DATA_VERSION = 1

# What a complete extraction leaves in cf_sky/
REQUIRED = ['cloud.tga', 'star.tga', 'moon.tga', 'rooms/idr_ms_astro.cfroom', 'rooms/idr_cafe.cfroom',
            'rooms/idr_entrance.cfroom', 'celeste.msg', 'celeste.tex', 'brewster.msg', 'chars/brewster.cfchar',
            'chars/cup_coffee.cfchar', 'chars/cup_milk.cfchar', 'chars/cup_empty.cfchar', 'chars/saucer.cfchar',
            'chars/pot.cfchar', 'chars/polish_cup.cfchar', 'chars/duster.cfchar', 'anims/roost.cfanim',
            'anims/player.cfpanm', 'music/roost.wav', 'sfx/bite.wav']


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, bin_dir = sys.argv[1], sys.argv[2]
    try:
        import numpy  # noqa: F401 (the music and sound effects need it)
    except ImportError:
        sys.exit('numpy is needed: install it (pip install numpy), or use ./ac-plus.sh extract-cf')
    import gc_disc
    if gc_disc.find_disc(bin_dir) is None:
        sys.exit('Put your Animal Crossing (GameCube) disc image in %s first: Celeste is fitted to its Blathers'
                 % os.path.join(bin_dir, 'rom'))
    version_file = os.path.join(bin_dir, 'cf_sky', 'version.txt')
    if os.path.exists(version_file):
        os.remove(version_file)  # a failed run must not look complete

    tmp = None
    if os.path.isfile(src):
        tmp = tempfile.TemporaryDirectory()
        disc = os.path.join(tmp.name, 'disc')  # wit wants a folder that doesn't exist yet
        subprocess.run(['wit', 'extract', '-q', '--files', '+/files/Sky/', '--files',
                        '+/files/Script/ENG/npc_SP/NPC_hu-ko.bmg', '--files',
                        '+/files/Script/ENG/npc_SP/NPC_master.bmg', '--files', '+/files/BgData/BgModel/',
                        '--files', '+/files/Npc/Special/Model/14.brres', '--files', '+/files/Npc/Special/Model/23.brres',
                        '--files', '+/files/Rmobj/obj_cafe1.brres', '--files', '+/files/Rmobj/obj_cafe2.brres',
                        '--files', '+/files/Rmobj/obj_cafe3.brres', '--files', '+/files/Rmobj/obj_cafe4.brres',
                        '--files', '+/files/Rmobj/obj_cafe5.brres', '--files', '+/files/Anm/Anm.brcha',
                        '--files', '+/files/Rmobj/obj_cafe6.brres', '--files', '+/files/Sound/rv_forest.brsar',
                        src, disc], check=True)
        root = find_files_root(disc)
    else:
        root = find_files_root(src)
    sky = os.path.join(root, 'Sky') if os.path.isdir(os.path.join(root, 'Sky')) else root

    out = os.path.join(bin_dir, 'cf_sky')
    os.makedirs(out, exist_ok=True)
    for name, brres, tex, pal in WANTED:
        w, h, px = texture(os.path.join(sky, brres), tex, pal)
        write_tga(os.path.join(out, name), w, h, px)
        print('wrote %s (%dx%d)' % (os.path.join(out, name), w, h))
    write_rooms(os.path.join(root, 'BgData', 'BgModel'), os.path.join(out, 'rooms'))
    bmg = os.path.join(root, 'Script', 'ENG', 'npc_SP', 'NPC_hu-ko.bmg')
    if os.path.isfile(bmg):
        write_lines(bmg, os.path.join(out, 'celeste.msg'))
        import celeste_tex
        celeste_tex.write_celeste_tex(root, bin_dir, os.path.join(out, 'celeste.tex'))
    master = os.path.join(root, 'Script', 'ENG', 'npc_SP', 'NPC_master.bmg')
    if os.path.isfile(master):
        write_lines(master, os.path.join(out, 'brewster.msg'))
    import cf_char
    cf_char.write_chars(root, os.path.join(out, 'chars'))
    import cf_roost_anim
    cf_roost_anim.write_roost_anims(root, os.path.join(out, 'anims'))
    brsar = os.path.join(root, 'Sound', 'rv_forest.brsar')
    if os.path.isfile(brsar):
        import cf_music
        cf_music.write_music(open(brsar, 'rb').read(), os.path.join(out, 'music'))
        cf_music.write_sfx(open(brsar, 'rb').read(), os.path.join(out, 'sfx'))

    missing = [f for f in REQUIRED if not os.path.isfile(os.path.join(out, f))]
    if missing:
        sys.exit('This doesn\'t look like Animal Crossing: City Folk (USA, RUUE01): nothing was made for %s'
                 % ', '.join(missing))
    with open(version_file, 'w') as f:
        f.write('%d\n' % CF_DATA_VERSION)
    print('City Folk files ready (version %d)' % CF_DATA_VERSION)


if __name__ == '__main__':
    main()
