#!/usr/bin/env python3
"""City Folk fish and insects -> GameCube-style flat sprites (a prototype, not used by the game yet).

The GameCube draws a fish held up or swimming off the hook, and every insect in town, as small flat textured
pictures, and each species' pocket icon as a 2D picture; only the ones put in the house (and the museum's) are 3D.
City Folk has only 3D models, so each is rendered here: its model at rest (its bones' rest pose), seen from the
side for a fish and from above for an insect, flat-lit as the GameCube's pictures are, onto a small transparent
picture.

    cf_sprites.py <City Folk files dir> <cf-fish-bugs-list.csv> <out dir> [size]
writes <out>/<name>.png for each species the GameCube doesn't have, and <out>/sheet.png with all of them.
"""
import csv
import os
import sys

import numpy as np

from cf_char import convert_char
from extract_cf_sky import brres_folders


def world_tris(b, model, hidden=()):
    """The model's triangles at rest: [(3 points, 3 uvs, texture index)], and its textures. hidden: bones' names to
    leave out."""
    bones, textures, meshes = convert_char(b, model)
    tris = []
    for name, tex, xlu, cull, alpha, wrap, verts in meshes:
        pts = []
        verts = [v for v in verts if bones[v[0]]['name'] not in hidden] if hidden else verts
        for bone, x, y, z, nx, ny, nz, s, t in verts:
            w = bones[bone]['world']
            pts.append(([w[i][0] * x + w[i][1] * y + w[i][2] * z + w[i][3] for i in range(3)], (s, t)))
        for i in range(0, len(pts) - 2, 3):
            tri = pts[i:i + 3]
            tris.append(([p for p, _ in tri], [uv for _, uv in tri], tex, wrap))
    return tris, textures


def render(tris, textures, size, view):
    """A size x size RGBA picture: view 'side' (x right, y up) or 'top' (x right, z down)."""
    if not tris:
        return np.zeros((size, size, 4), np.uint8)
    allp = np.array([p for t in tris for p in t[0]])
    lo, hi = allp.min(0), allp.max(0)
    if isinstance(view, tuple):
        ax = view  # screen x, screen y, depth
    elif view == 'side':
        # along the body: City Folk's fish point along z, some along x
        ax = (2, 1, 0) if hi[2] - lo[2] > hi[0] - lo[0] else (0, 1, 2)
    else:
        ax = (0, 2, 1)  # screen x, screen y, depth
    span = max(hi[ax[0]] - lo[ax[0]], hi[ax[1]] - lo[ax[1]]) or 1.0
    scale = (size - 2) / span
    cx, cy = (lo[ax[0]] + hi[ax[0]]) / 2, (lo[ax[1]] + hi[ax[1]]) / 2
    img = np.zeros((size, size, 4), np.float32)
    depth = np.full((size, size), -1e9, np.float32)
    texs = [np.frombuffer(px, np.uint8).reshape(h, w, 4).astype(np.float32) for _, w, h, px in textures]
    light = np.array([0.3, 0.8, 0.5])
    light /= np.linalg.norm(light)
    for pts, uvs, tex, wrap in tris:
        p = np.array(pts)
        sx = (p[:, ax[0]] - cx) * scale + size / 2
        up = view != 'top'  # (a side view: screen y is the model's up)
        sy = (cy - p[:, ax[1]]) * scale + size / 2 if up else (p[:, ax[1]] - cy) * scale + size / 2
        sz = p[:, ax[2]] if up else p[:, 1]
        if view == 'side' and ax[0] == 2:
            sx = size - sx  # head to the left, as the GameCube's
        n = np.cross(p[1] - p[0], p[2] - p[0])
        ln = np.linalg.norm(n)
        shade = 0.65 + 0.35 * abs(float(n @ light) / ln) if ln > 0 else 1.0
        x0, x1 = int(max(0, np.floor(sx.min()))), int(min(size - 1, np.ceil(sx.max())))
        y0, y1 = int(max(0, np.floor(sy.min()))), int(min(size - 1, np.ceil(sy.max())))
        if x1 < x0 or y1 < y0:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        d = (sy[1] - sy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (sy[0] - sy[2])
        if abs(d) < 1e-9:
            continue
        w0 = ((sy[1] - sy[2]) * (gx - sx[2]) + (sx[2] - sx[1]) * (gy - sy[2])) / d
        w1 = ((sy[2] - sy[0]) * (gx - sx[2]) + (sx[0] - sx[2]) * (gy - sy[2])) / d
        w2 = 1 - w0 - w1
        inside = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
        if not inside.any():
            continue
        z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2]
        u = w0 * uvs[0][0] + w1 * uvs[1][0] + w2 * uvs[2][0]
        v = w0 * uvs[0][1] + w1 * uvs[1][1] + w2 * uvs[2][1]
        if 0 <= tex < len(texs):
            t = texs[tex]
            th, tw = t.shape[:2]
            # (wrap: s in its low 2 bits, t in the next two; 2 is mirrored, as half a symmetrical body is)
            iu = np.floor(u * tw).astype(int)
            iv = np.floor(v * th).astype(int)
            if (wrap & 3) == 2:
                iu = np.where(iu % (2 * tw) >= tw, 2 * tw - 1 - iu % (2 * tw), iu % (2 * tw))
            if (wrap >> 2 & 3) == 2:
                iv = np.where(iv % (2 * th) >= th, 2 * th - 1 - iv % (2 * th), iv % (2 * th))
            col = t[iv % th, iu % tw]
        else:
            col = np.full(gx.shape + (4,), 255.0)
        ok = inside & (col[..., 3] > 96) & (z > depth[y0:y1 + 1, x0:x1 + 1])
        sub = img[y0:y1 + 1, x0:x1 + 1]
        sub[ok, :3] = col[ok, :3] * shade
        sub[ok, 3] = 255
        depth[y0:y1 + 1, x0:x1 + 1][ok] = z[ok]
    return np.clip(img, 0, 255).astype(np.uint8)


def main(root, table, out, size=32):
    from PIL import Image
    os.makedirs(out, exist_ok=True)
    rows = [r for r in csv.DictReader(open(table)) if r['gc_has'] != 'yes' and r['cf_world_model']]
    pics = []
    for r in rows:
        path = os.path.join(root, *r['cf_world_model'].split('/'))
        if not os.path.isfile(path):
            continue
        b = open(path, 'rb').read()
        model = next(iter(brres_folders(b)['3DModels(NW4R)']))
        try:
            tris, textures = world_tris(b, model)
        except Exception as e:
            print('%s: %s' % (r['name'], e))
            continue
        pic = render(tris, textures, size, 'side' if r['type'] == 'fish' else 'top')
        Image.fromarray(pic, 'RGBA').save(os.path.join(out, r['name'].replace(' ', '_') + '.png'))
        pics.append((r['name'], pic))
    cols = 10
    sheet = Image.new('RGBA', (cols * (size * 3 + 8), ((len(pics) + cols - 1) // cols) * (size * 3 + 8)), (60, 70, 90, 255))
    for i, (name, pic) in enumerate(pics):
        im = Image.fromarray(pic, 'RGBA').resize((size * 3, size * 3), Image.NEAREST)
        sheet.alpha_composite(im, ((i % cols) * (size * 3 + 8) + 4, (i // cols) * (size * 3 + 8) + 4))
    sheet.save(os.path.join(out, 'sheet.png'))
    print('rendered %d' % len(pics))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 32)
