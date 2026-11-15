/* pc_cf_room.c - City Folk rooms drawn in the GameCube game. See pc_cf_room.h. */
#include "pc_cf_room.h"
#include "pc_acplus_gx.h"
#include "pc_platform.h"

#include "m_play.h"
#include "graph.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PC_CF_ROOM_MAX 4
#define PC_CF_ROOM_NAME 32

typedef struct {
    char name[32];
    int tex;
    u8 xlu, cull, alpha_test;
    u8 wrap_s, wrap_t; /* 0 clamp, 1 repeat, 2 mirror, as the material's GX wrap modes */
    int vert_num;
    float* verts; /* x y z nx ny nz u v, then rgba as u8 packed into the last float slot */
    float* shade; /* per vertex: the light from its normal (0.82-1.0), worked out once */
} pc_cf_mesh_c;

typedef struct {
    char name[PC_CF_ROOM_NAME];
    int loaded; /* 1 ok, -1 missing */
    int tex_num;
    int* tex_w;
    int* tex_h;
    u8** tex_px;
    PCGXHTex* tex_gx;
    int mesh_num;
    pc_cf_mesh_c* mesh;
    int has_grid;
    u8 grid[16 * 16]; /* walkable units, from version 2 files */
} pc_cf_room_c;

static pc_cf_room_c l_rooms[PC_CF_ROOM_MAX];

/* set by the game for the frame being drawn */
static struct {
    pc_cf_room_c* room;
    float light;
    float spot[4]; /* pc_cf_room_set_spot: x, z, radius, strength */
    float y_offset;
} l_frame;

#define PC_CF_VERT_FLOATS 9 /* pos 3, normal 3, uv 2, colour (4 bytes) 1 */

static int pc_cf_read(FILE* f, void* dst, size_t n) {
    return fread(dst, 1, n, f) == n;
}

static void pc_cf_room_load(pc_cf_room_c* r) {
    char path[96];
    char magic[4];
    u32 version, n, i;
    FILE* f;

    snprintf(path, sizeof(path), "cf_sky/rooms/%s.cfroom", r->name);
    r->loaded = -1;
    f = fopen(path, "rb");
    if (f == NULL) {
        return;
    }
    if (!pc_cf_read(f, magic, 4) || memcmp(magic, "CFRM", 4) != 0 || !pc_cf_read(f, &version, 4) || version < 1 || version > 3 ||
        !pc_cf_read(f, &n, 4)) {
        fclose(f);
        return;
    }
    r->tex_num = (int)n;
    r->tex_w = calloc(n, sizeof(int));
    r->tex_h = calloc(n, sizeof(int));
    r->tex_px = calloc(n, sizeof(u8*));
    r->tex_gx = calloc(n, sizeof(PCGXHTex));
    for (i = 0; i < n; i++) {
        char name[32];
        u16 wh[2];

        if (!pc_cf_read(f, name, 32) || !pc_cf_read(f, wh, 4)) {
            goto bad;
        }
        r->tex_w[i] = wh[0];
        r->tex_h[i] = wh[1];
        r->tex_px[i] = malloc((size_t)wh[0] * wh[1] * 4);
        if (r->tex_px[i] == NULL || !pc_cf_read(f, r->tex_px[i], (size_t)wh[0] * wh[1] * 4)) {
            goto bad;
        }
    }
    if (!pc_cf_read(f, &n, 4)) {
        goto bad;
    }
    r->mesh_num = (int)n;
    r->mesh = calloc(n, sizeof(pc_cf_mesh_c));
    for (i = 0; i < n; i++) {
        pc_cf_mesh_c* m = &r->mesh[i];
        s32 tex;
        u8 flags[4];
        u32 count, v;

        if (!pc_cf_read(f, m->name, 32) || !pc_cf_read(f, &tex, 4) || !pc_cf_read(f, flags, 4) ||
            !pc_cf_read(f, &count, 4)) {
            goto bad;
        }
        m->tex = tex;
        m->xlu = flags[0];
        m->cull = flags[1];
        m->alpha_test = flags[2];
        m->wrap_s = version >= 3 ? flags[3] & 3 : 1;
        m->wrap_t = version >= 3 ? flags[3] >> 2 & 3 : 1;
        m->vert_num = (int)count;
        m->verts = malloc((size_t)count * PC_CF_VERT_FLOATS * sizeof(float));
        for (v = 0; m->verts != NULL && v < count; v++) {
            float* d = &m->verts[v * PC_CF_VERT_FLOATS];
            u8 rgba[4];

            if (!pc_cf_read(f, d, 8 * sizeof(float)) || !pc_cf_read(f, rgba, 4)) {
                goto bad;
            }
            memcpy(&d[8], rgba, 4);
        }
    }
    if (version >= 2) {
        if (!pc_cf_read(f, r->grid, sizeof(r->grid))) {
            goto bad;
        }
        r->has_grid = TRUE;
    }
    fclose(f);
    r->loaded = 1;
    printf("[CFRoom] %s: %d textures, %d meshes\n", r->name, r->tex_num, r->mesh_num);
    return;
bad:
    fclose(f);
    printf("[CFRoom] %s is cut short\n", path);
}

static pc_cf_room_c* pc_cf_room_get(const char* name) {
    int i;

    for (i = 0; i < PC_CF_ROOM_MAX; i++) {
        if (l_rooms[i].name[0] != '\0' && strcmp(l_rooms[i].name, name) == 0) {
            return &l_rooms[i];
        }
    }
    for (i = 0; i < PC_CF_ROOM_MAX; i++) {
        if (l_rooms[i].name[0] == '\0') {
            snprintf(l_rooms[i].name, sizeof(l_rooms[i].name), "%s", name);
            pc_cf_room_load(&l_rooms[i]);
            return &l_rooms[i];
        }
    }
    return NULL;
}

int pc_cf_room_exists(const char* name) {
    pc_cf_room_c* r = pc_cf_room_get(name);

    return r != NULL && r->loaded == 1;
}

const u8* pc_cf_room_grid(const char* name) {
    pc_cf_room_c* r = pc_cf_room_get(name);

    return r != NULL && r->loaded == 1 && r->has_grid ? r->grid : NULL;
}

static float l_next_spot[4];

void pc_cf_room_set_spot(float x, float z, float r, float strength) {
    l_next_spot[0] = x;
    l_next_spot[1] = z;
    l_next_spot[2] = r > 1.0f ? r : 1.0f;
    l_next_spot[3] = strength;
}

int pc_cf_room_play_draw(GAME_PLAY* play, const char* name, float light, float y_offset) {
    pc_cf_room_c* r = pc_cf_room_get(name);
    GRAPH* graph = play->game.graph;

    if (r == NULL || r->loaded != 1) {
        return FALSE;
    }
    l_frame.room = r;
    l_frame.light = light;
    l_frame.y_offset = y_offset;
    memcpy(l_frame.spot, l_next_spot, sizeof(l_frame.spot));
    l_next_spot[3] = 0.0f; /* this frame only */
    OPEN_DISP(graph);
    if (getenv("PC_CF_ROOM_LATE") != NULL) {
        gDPNoOpTag(NEXT_POLY_XLU_DISP, PC_NOOP_CF_ROOM); /* dev: after everything else */
    } else {
        /* at the head of POLY_OPA, before the actors: BG_OPA is painted over indoors */
        gDPNoOpTag(NEXT_POLY_OPA_DISP, PC_NOOP_CF_ROOM);
    }
    CLOSE_DISP(graph);
    return TRUE;
}

/* --- render side --- */

/* GX textures and the normals' light, the first time a room is drawn */
static void pc_cf_upload(pc_cf_room_c* r) {
    static const float L[3] = { 0.2491f, 0.8305f, 0.4983f }; /* normalize(0.3, 1.0, 0.6) */
    int i, v;

    for (i = 0; i < r->tex_num; i++) {
        if (!r->tex_gx[i].ready && r->tex_px[i] != NULL) {
            pc_gxh_tex_init(&r->tex_gx[i], r->tex_px[i], r->tex_w[i], r->tex_h[i], 1, 1, TRUE);
        }
    }
    for (i = 0; i < r->mesh_num; i++) {
        pc_cf_mesh_c* m = &r->mesh[i];

        if (m->shade != NULL || m->verts == NULL) {
            continue;
        }
        m->shade = malloc((size_t)m->vert_num * sizeof(float));
        for (v = 0; m->shade != NULL && v < m->vert_num; v++) {
            const float* d = &m->verts[v * PC_CF_VERT_FLOATS];
            float len = sqrtf(d[3] * d[3] + d[4] * d[4] + d[5] * d[5]);
            float dot = len > 0.0f ? (d[3] * L[0] + d[4] * L[1] + d[5] * L[2]) / len : 0.0f;

            /* a little shape from the normals, as the room's own lights would */
            m->shade[v] = 0.82f + 0.18f * (dot < 0.0f ? 0.0f : dot > 1.0f ? 1.0f : dot);
        }
    }
}

static float pc_cf_smoothstep(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);

    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return t * t * (3.0f - 2.0f * t);
}

static u8 pc_cf_u8(float v) {
    return (u8)(v <= 0.0f ? 0 : v >= 255.0f ? 255 : v + 0.5f);
}

static void pc_cf_draw(const float proj[4][4], const float view[3][4]) {
    pc_cf_room_c* r = l_frame.room;
    float v[3][4];
    const float* spot = l_frame.spot;
    int pass, i, k;

    if (r == NULL || r->loaded != 1) {
        return;
    }
    pc_cf_upload(r);
    {
        static int logged;
        if (!logged++ || (getenv("PC_CF_ROOM_LOG") != NULL && logged % 120 == 0)) {
            printf("[CFRoom] view %.2f %.2f %.2f %.1f / %.2f %.2f %.2f %.1f / %.2f %.2f %.2f %.1f proj00 %.3f 11 %.3f\n",
                   view[0][0], view[0][1], view[0][2], view[0][3], view[1][0], view[1][1], view[1][2], view[1][3],
                   view[2][0], view[2][1], view[2][2], view[2][3], proj[0][0], proj[1][1]);
            fflush(stdout);
        }
    }
    for (i = 0; i < 3; i++) {
        for (k = 0; k < 4; k++) {
            v[i][k] = view[i][k];
        }
        v[i][3] += view[i][1] * l_frame.y_offset; /* the room raised by y_offset */
    }
    pc_gxh_camera(proj, (const float(*)[4])v);

    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < r->mesh_num; i++) {
            pc_cf_mesh_c* m = &r->mesh[i];
            const PCGXHTex* tex = NULL;
            GXCullMode cull;
            int n;

            if (m->xlu != pass || m->verts == NULL || m->shade == NULL || m->vert_num <= 0) {
                continue;
            }
            if (m->tex >= 0 && m->tex < r->tex_num && r->tex_gx[m->tex].ready) {
                tex = &r->tex_gx[m->tex];
                pc_gxh_tex_set_wrap(&r->tex_gx[m->tex], m->wrap_s & 3, m->wrap_t & 3);
            }
            /* GX culls by its own winding; the converter keeps the model's, so cull the other side */
            cull = m->cull == 0 ? GX_CULL_NONE : m->cull == 1 ? GX_CULL_FRONT : m->cull == 2 ? GX_CULL_BACK : GX_CULL_ALL;
            pc_gxh_mesh_state(tex, pass == 1 ? 1 : 0, pass == 0 && m->alpha_test, pass == 1 ? 1 : 2, cull);

            GXBegin(GX_TRIANGLES, PC_GXH_VTXFMT, GX_AUTO);
            for (n = 0; n < m->vert_num; n++) {
                const float* d = &m->verts[n * PC_CF_VERT_FLOATS];
                const u8* c = (const u8*)&d[8];
                float lit_r = l_frame.light, lit_g = l_frame.light, lit_b = l_frame.light;
                float shade = m->shade[n] * 0.5f; /* halved: the TEV doubles it back (a spot goes past full) */

                if (spot[3] > 0.0f) {
                    float dx = d[0] - spot[0], dz = d[2] - spot[1];
                    float s = spot[3] * (1.0f - pc_cf_smoothstep(0.55f * spot[2], spot[2], sqrtf(dx * dx + dz * dz)));

                    lit_r += (1.08f - lit_r) * s;
                    lit_g += (1.0f - lit_g) * s;
                    lit_b += (0.86f - lit_b) * s;
                }
                GXPosition3f32(d[0], d[1], d[2]);
                GXColor4u8(pc_cf_u8(c[0] * shade * lit_r), pc_cf_u8(c[1] * shade * lit_g),
                           pc_cf_u8(c[2] * shade * lit_b), c[3]);
                if (tex != NULL) {
                    GXTexCoord2f32(d[6], d[7]);
                }
            }
            GXEnd();
        }
    }
}

void pc_cf_room_noop(unsigned int tag, const float proj[4][4], const float view[3][4]) {
    if (tag == PC_NOOP_CF_ROOM) {
        pc_cf_draw(proj, view);
        l_frame.room = NULL;
    }
}
