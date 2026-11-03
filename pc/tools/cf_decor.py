#!/usr/bin/env python3
"""City Folk wallpaper and carpets -> cf_sky/decor.cfdecor for pc/src/pc_cf_decor.c.

Part of extract_cf_sky.py. City Folk's wallpaper (files/Item/Wall/WallN.brres, 64x128) and carpets
(files/Item/Carpet/CarpetN.brres, 128x128) are laid out as the GameCube's: a wall is its upper and lower
64x64 halves, a floor the four 64x64 quarters of a corner (top left, top right, bottom left, bottom right),
mirrored around the room. Each is cut into those pieces and brought down to the GameCube's 16 colours (CI4),
in the layout mRmTp_CopyWallData / CopyFloorData give the room. Only the ones the GameCube doesn't have are
kept, with their names and prices (files/Item/item.arc's item.bin, English).

.cfdecor, little-endian:
    "CFDC" u32 version (1) u32 walls u32 floors
    each wall:  name (16 bytes, spaces after), u32 price, u16 palette[16] (RGB5A3), CI4 64x64 x2 (GameCube tiles)
    each floor: the same with x4
"""
import os
import struct
import unicodedata

import numpy as np

from extract_cf_sky import brres_folders
from cf_rooms import decode_texture, palette_any

VERSION = 1

# The GameCube's own wallpaper and carpets (its item names, lowercase): City Folk's of the same name are left out
GC_WALLS = {
    'ancient wall', 'arched window', 'backgammon wall', 'backyard fence', 'bamboo wall', 'basement wall',
    'blue tarp', 'blue wall', 'blue-trim wall', 'cabana wall', 'cabin wall', 'chic wall', 'citrus wall',
    'classic wall', 'classroom wall', 'concrete wall', 'desert vista', 'exotic wall', 'exquisite wall',
    'floral wall', 'garden wall', 'gold screen wall', 'green wall', 'groovy wall', 'harvest wall', 'imperial wall',
    'industrial wall', 'ivy wall', 'jingle wall', 'kiddie wall', 'kitchen wall', 'kitschy wall', 'lattice wall',
    'library wall', 'lovely wall', 'lunar horizon', 'manor wall', 'meadow vista', 'mod wall', 'modern screen',
    'modern wall', 'mortar wall', 'mosaic wall', 'mushroom mural', 'music room wall', 'office wall',
    'old brick wall', 'ornate wall', 'parlor wall', 'plaster wall', 'playroom wall', 'plaza wall', 'ranch wall',
    'regal wall', 'ringside seating', 'rose wall', 'sandlot wall', 'shanty wall', 'snowman wall', 'spooky wall',
    'stately wall', 'stone wall', 'tea room wall', 'tree-lined wall', 'tropical vista', 'western vista',
    'wood paneling',
}
GC_FLOORS = {
    '18 mat tatami', '8 mat tatami', 'ancient tile', 'backyard lawn', 'bamboo flooring', 'basement floor',
    'birch flooring', 'block flooring', 'blue flooring', 'boxing ring mat', 'cabana flooring', 'cabin rug',
    'ceramic tile', 'charcoal tile', 'checkered tile', 'chessboard rug', 'citrus carpet', 'classic carpet',
    'classroom floor', 'closed road', 'concrete floor', 'cowhide rug', 'daisy meadow', 'diner tile', 'exotic rug',
    'exquisite rug', 'fancy carpet', 'green rug', 'harvest rug', 'imperial tile', 'jingle carpet', 'kiddie carpet',
    'kitchen flooring', 'kitchen tile', 'kitschy tile', 'lovely carpet', 'lunar surface', 'modern tile',
    'mosaic tile', 'mossy carpet', 'music room floor', 'office flooring', 'old flooring', 'opulent rug',
    'ornate rug', 'palace tile', 'parquet floor', 'playroom rug', 'plaza tile', 'plush carpet', 'ranch flooring',
    'red tile', 'regal carpet', "saharah's desert", 'sand garden', 'sandlot', 'shanty mat', 'sidewalk',
    'slate flooring', 'snowman carpet', 'spooky carpet', 'steel flooring', 'stone tile', 'tartan rug',
    'tatami floor', 'tropical floor', 'western desert',
}


def u8_files(path):
    """A Wii U8 archive -> {name: bytes} (files only, any folder)."""
    d = open(path, 'rb').read()
    if d[:4] != b'\x55\xaa\x38\x2d':
        raise ValueError('not a U8 archive')
    root = struct.unpack_from('>I', d, 4)[0]
    n = struct.unpack_from('>I', d, root + 8)[0]
    strtab = root + n * 12
    out = {}
    for i in range(1, n):
        if d[root + i * 12] == 0:  # a file (1: a folder)
            name_off = strtab + (struct.unpack_from('>I', d, root + i * 12)[0] & 0xffffff)
            a, b = struct.unpack_from('>II', d, root + i * 12 + 4)
            out[d[name_off:d.index(b'\0', name_off)].decode()] = d[a:a + b]
    return out


def item_names(item_bin):
    """item.bin -> {kind: [(price, English name)]} in the table's order, which is the texture files' (WallN is the
    N-th wallpaper listed, not item 1000 + N): 0x133 wallpaper, 0x134 carpets."""
    out = {}
    rs, base = 0x18c, 0x20
    for k in range(struct.unpack_from('>I', item_bin, 0)[0]):
        r = item_bin[base + k * rs:base + (k + 1) * rs]
        if r[:4] != b'BITM':
            continue
        price, num, kind = struct.unpack_from('>IHH', r, 4)
        name = r[52:84].decode('utf-16-be', 'replace').split('\0')[0]
        out.setdefault(kind, []).append((price, unicodedata.normalize('NFKD', name).encode('ascii', 'ignore').decode()))
    return out


def item_names_all(item_bin):
    """item.bin -> {record number: (price, English name)}"""
    out = {}
    rs, base = 0x18c, 0x20
    for k in range(struct.unpack_from('>I', item_bin, 0)[0]):
        r = item_bin[base + k * rs:base + (k + 1) * rs]
        if r[:4] != b'BITM':
            continue
        price = struct.unpack_from('>I', r, 4)[0]
        name = r[52:84].decode('utf-16-be', 'replace').split('\0')[0]
        out[k] = (price, unicodedata.normalize('NFKD', name).encode('ascii', 'ignore').decode())
    return out


def quantize16(px):
    """RGBA pixels (h, w, 4) -> (16 RGB colours, (h, w) indices): a few rounds of k-means from a spread of the
    image's own colours."""
    rgb = px[:, :, :3].reshape(-1, 3).astype(np.float32)
    uniq = np.unique(rgb, axis=0)
    if len(uniq) <= 16:
        pal = np.zeros((16, 3), np.float32)
        pal[:len(uniq)] = uniq
    else:
        order = np.argsort(uniq @ np.array([0.299, 0.587, 0.114], np.float32))
        pal = uniq[order[np.linspace(0, len(uniq) - 1, 16).astype(int)]].copy()
        for _ in range(12):
            idx = ((rgb[:, None, :] - pal[None, :, :]) ** 2).sum(2).argmin(1)
            for c in range(16):
                m = rgb[idx == c]
                if len(m):
                    pal[c] = m.mean(0)
    idx = ((rgb[:, None, :] - pal[None, :, :]) ** 2).sum(2).argmin(1)
    return pal, idx.reshape(px.shape[0], px.shape[1])


def rgb5a3(c):
    r, g, b = (int(round(v)) for v in c)
    return 0x8000 | (min(r, 255) >> 3) << 10 | (min(g, 255) >> 3) << 5 | (min(b, 255) >> 3)


def ci4_tile(idx):
    """64x64 colour indices -> the GameCube's CI4 layout (8x8 blocks, high nibble first)."""
    out = bytearray()
    for by in range(0, 64, 8):
        for bx in range(0, 64, 8):
            for y in range(by, by + 8):
                for x in range(bx, bx + 8, 2):
                    out.append(int(idx[y, x]) << 4 | int(idx[y, x + 1]))
    return bytes(out)


def piece(path):
    b = open(path, 'rb').read()
    f = brres_folders(b)
    tex = list(f['Textures(NW4R)'].values())[0]
    pal = list(f['Palettes(NW4R)'].values())[0] if 'Palettes(NW4R)' in f else None
    w, h, px = decode_texture(b, tex, palette_any(b, pal) if pal is not None else None)
    return np.frombuffer(px, np.uint8).reshape(h, w, 4)


def record(name, price, px, tiles):
    pal, idx = quantize16(px)
    out = name.encode('ascii', 'replace')[:16].ljust(16, b' ') + struct.pack('<I', price)
    out += struct.pack('<16H', *(rgb5a3(c) for c in pal))
    for ty, tx in tiles:
        out += ci4_tile(idx[ty:ty + 64, tx:tx + 64])
    return out


def write_decor(root, out_path):
    item = os.path.join(root, 'Item')
    names = item_names(u8_files(os.path.join(item, 'item.arc'))['item.bin'])
    walls, floors = [], []
    for n, (price, name) in enumerate(names.get(0x133, [])):
        p = os.path.join(item, 'Wall', 'Wall%d.brres' % n)
        if price != 0 and name.lower() not in GC_WALLS and os.path.isfile(p):  # (price 0: the shops' own)
            px = piece(p)
            if px.shape[:2] == (128, 64):
                walls.append(record(name, price, px, [(0, 0), (64, 0)]))
    for n, (price, name) in enumerate(names.get(0x134, [])):
        p = os.path.join(item, 'Carpet', 'Carpet%d.brres' % n)
        if price != 0 and name.lower() not in GC_FLOORS and os.path.isfile(p):
            px = piece(p)
            if px.shape[:2] == (128, 128):
                floors.append(record(name, price, px, [(0, 0), (0, 64), (64, 0), (64, 64)]))
    with open(out_path, 'wb') as f:
        f.write(b'CFDC' + struct.pack('<III', VERSION, len(walls), len(floors)))
        for r in walls + floors:
            f.write(r)
    print('City Folk wallpaper and carpets: %d and %d' % (len(walls), len(floors)))
