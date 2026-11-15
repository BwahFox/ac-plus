/* pc_cf_char.c - City Folk characters drawn in the GameCube game. See pc_cf_char.h. */
#include "pc_cf_char.h"
#include "pc_acplus_gx.h"
#include "pc_platform.h"

#include "m_play.h"
#include "graph.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PC_CF_CHAR_MAX 12
#define PC_CF_CHAR_BONES 32
#define PC_CF_CHAR_VERT_FLOATS 9 /* bone 1, pos 3, normal 3, uv 2 */

typedef struct {
    char name[16];
    int parent;
    float t[3];
    float rest[3][3];
} pc_cf_bone_c;

typedef struct {
    char name[32];
    int tex;
    u8 xlu, cull, alpha_test, wrap_s, wrap_t, eyes;
    int vert_num;
    float* verts;
} pc_cf_char_mesh_c;

typedef struct {
    char name[32];
    int loaded; /* 1 ok, -1 missing */
    int bone_num;
    pc_cf_bone_c bone[PC_CF_CHAR_BONES];
    int tex_num;
    int* tex_w;
    int* tex_h;
    u8** tex_px;
    PCGXHTex* tex_gx;
    int eye_tex[8]; /* e.0-e.7, or -1 */
    int mesh_num;
    pc_cf_char_mesh_c* mesh;
} pc_cf_char_c;

static pc_cf_char_c l_chars[PC_CF_CHAR_MAX];

/* what the game asked to draw this frame */
static struct {
    pc_cf_char_c* chr;
    float bones[PC_CF_CHAR_BONES][3][4];
    int eye;
    float light;
} l_inst[PC_CF_CHAR_INSTANCES];
static int l_inst_next;
static u32 l_inst_frame;

static int pc_cf_read(FILE* f, void* dst, size_t n) {
    return fread(dst, 1, n, f) == n;
}

static void pc_cf_char_load(pc_cf_char_c* c) {
    char path[96];
    char magic[4];
    u32 version, n, i;
    int k;
    FILE* f;

    snprintf(path, sizeof(path), "cf_sky/chars/%s.cfchar", c->name);
    c->loaded = -1;
    for (k = 0; k < 8; k++) {
        c->eye_tex[k] = -1;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return;
    }
    if (!pc_cf_read(f, magic, 4) || memcmp(magic, "CFCH", 4) != 0 || !pc_cf_read(f, &version, 4) || version != 1 ||
        !pc_cf_read(f, &n, 4) || n > PC_CF_CHAR_BONES) {
        fclose(f);
        return;
    }
    c->bone_num = (int)n;
    for (i = 0; i < n; i++) {
        pc_cf_bone_c* b = &c->bone[i];
        s32 parent;

        if (!pc_cf_read(f, b->name, 16) || !pc_cf_read(f, &parent, 4) || !pc_cf_read(f, b->t, 12) ||
            !pc_cf_read(f, b->rest, 36)) {
            goto bad;
        }
        b->name[15] = '\0';
        b->parent = parent < (s32)i ? parent : -1; /* parents come first */
    }
    if (!pc_cf_read(f, &n, 4)) {
        goto bad;
    }
    c->tex_num = (int)n;
    c->tex_w = calloc(n, sizeof(int));
    c->tex_h = calloc(n, sizeof(int));
    c->tex_px = calloc(n, sizeof(u8*));
    c->tex_gx = calloc(n, sizeof(PCGXHTex));
    for (i = 0; i < n; i++) {
        char name[32];
        u16 wh[2];

        if (!pc_cf_read(f, name, 32) || !pc_cf_read(f, wh, 4)) {
            goto bad;
        }
        name[31] = '\0';
        if (name[0] == 'e' && name[1] == '.' && name[2] >= '0' && name[2] <= '7' && name[3] == '\0') {
            c->eye_tex[name[2] - '0'] = (int)i;
        }
        c->tex_w[i] = wh[0];
        c->tex_h[i] = wh[1];
        c->tex_px[i] = malloc((size_t)wh[0] * wh[1] * 4);
        if (c->tex_px[i] == NULL || !pc_cf_read(f, c->tex_px[i], (size_t)wh[0] * wh[1] * 4)) {
            goto bad;
        }
    }
    if (!pc_cf_read(f, &n, 4)) {
        goto bad;
    }
    c->mesh_num = (int)n;
    c->mesh = calloc(n, sizeof(pc_cf_char_mesh_c));
    for (i = 0; i < n; i++) {
        pc_cf_char_mesh_c* m = &c->mesh[i];
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
        m->wrap_s = flags[3] & 3;
        m->wrap_t = flags[3] >> 2 & 3;
        m->eyes = tex >= 0 && tex == c->eye_tex[0];
        m->vert_num = (int)count;
        m->verts = malloc((size_t)count * PC_CF_CHAR_VERT_FLOATS * sizeof(float));
        for (v = 0; m->verts != NULL && v < count; v++) {
            float* d = &m->verts[v * PC_CF_CHAR_VERT_FLOATS];
            u8 bone[4];

            if (!pc_cf_read(f, bone, 4) || !pc_cf_read(f, &d[1], 8 * sizeof(float))) {
                goto bad;
            }
            d[0] = bone[0] < c->bone_num ? bone[0] : 0;
        }
    }
    fclose(f);
    c->loaded = 1;
    printf("[CFChar] %s: %d bones, %d textures, %d meshes\n", c->name, c->bone_num, c->tex_num, c->mesh_num);
    return;
bad:
    fclose(f);
    printf("[CFChar] %s is cut short\n", path);
}

static pc_cf_char_c* pc_cf_char_get(const char* name) {
    int i;

    for (i = 0; i < PC_CF_CHAR_MAX; i++) {
        if (l_chars[i].name[0] != '\0' && strcmp(l_chars[i].name, name) == 0) {
            return &l_chars[i];
        }
    }
    for (i = 0; i < PC_CF_CHAR_MAX; i++) {
        if (l_chars[i].name[0] == '\0') {
            snprintf(l_chars[i].name, sizeof(l_chars[i].name), "%s", name);
            pc_cf_char_load(&l_chars[i]);
            return &l_chars[i];
        }
    }
    return NULL;
}

int pc_cf_char_exists(const char* name) {
    pc_cf_char_c* c = pc_cf_char_get(name);

    return c != NULL && c->loaded == 1;
}

/* --- posing --- */

static void pc_cf_rot_mul(float out[3][3], const float a[3][3], const float b[3][3]) {
    int r, k;

    for (r = 0; r < 3; r++) {
        for (k = 0; k < 3; k++) {
            out[r][k] = a[r][0] * b[0][k] + a[r][1] * b[1][k] + a[r][2] * b[2][k];
        }
    }
}

int pc_cf_char_play_draw(GAME_PLAY* play, const char* name, const pc_cf_mtx34 actor, int joint_num,
                         const char* const* joint_names, const pc_cf_mtx34* joints, int eye, float light) {
    pc_cf_char_c* c = pc_cf_char_get(name);
    GRAPH* graph = play->game.graph;
    float rot[PC_CF_CHAR_BONES][3][3];
    float pos[PC_CF_CHAR_BONES][3];
    int slot, b, j, r, k;

    if (c == NULL || c->loaded != 1) {
        return FALSE;
    }
    if (l_inst_frame != play->game_frame) {
        l_inst_frame = play->game_frame;
        l_inst_next = 0;
    }
    if (l_inst_next >= PC_CF_CHAR_INSTANCES) {
        return FALSE;
    }
    slot = l_inst_next++;

    for (b = 0; b < c->bone_num; b++) {
        const pc_cf_bone_c* bone = &c->bone[b];
        const float(*src)[4] = NULL;

        for (j = 0; j < joint_num; j++) {
            if (joint_names[j] != NULL && strcmp(joint_names[j], bone->name) == 0) {
                src = joints[j];
                break;
            }
        }
        if (bone->parent < 0) {
            /* the root: where the actor stands */
            for (r = 0; r < 3; r++) {
                for (k = 0; k < 3; k++) {
                    rot[b][r][k] = actor[r][k];
                }
                pos[b][r] = actor[r][3];
            }
            continue;
        }
        /* its own length, from the parent as posed */
        for (r = 0; r < 3; r++) {
            pos[b][r] = pos[bone->parent][r] + PC_CF_CHAR_SCALE * (rot[bone->parent][r][0] * bone->t[0] +
                                                                    rot[bone->parent][r][1] * bone->t[1] +
                                                                    rot[bone->parent][r][2] * bone->t[2]);
        }
        if (src != NULL) {
            /* its joint's turn */
            for (r = 0; r < 3; r++) {
                for (k = 0; k < 3; k++) {
                    rot[b][r][k] = src[r][k];
                }
            }
        } else {
            pc_cf_rot_mul(rot[b], rot[bone->parent], bone->rest);
        }
    }
    for (b = 0; b < c->bone_num; b++) {
        for (r = 0; r < 3; r++) {
            for (k = 0; k < 3; k++) {
                l_inst[slot].bones[b][r][k] = rot[b][r][k] * PC_CF_CHAR_SCALE;
            }
            l_inst[slot].bones[b][r][3] = pos[b][r];
        }
    }
    l_inst[slot].chr = c;
    l_inst[slot].eye = eye & 7;
    l_inst[slot].light = light;
    OPEN_DISP(graph);
    gDPNoOpTag(NEXT_POLY_OPA_DISP, PC_NOOP_CF_CHAR + slot);
    CLOSE_DISP(graph);
    return TRUE;
}

/* Bones already posed (pc_cf_anim.c): bone_names[i]'s world matrix is bones[i], scale included.
 * Bones not given keep their rest pose under their parent. */
int pc_cf_char_play_draw_mtx(GAME_PLAY* play, const char* name, int bone_num, const char* const* bone_names,
                             const pc_cf_mtx34* bones, int eye, float light) {
    pc_cf_char_c* c = pc_cf_char_get(name);
    GRAPH* graph = play->game.graph;
    int slot, b, j, r, k;

    if (c == NULL || c->loaded != 1) {
        return FALSE;
    }
    if (l_inst_frame != play->game_frame) {
        l_inst_frame = play->game_frame;
        l_inst_next = 0;
    }
    if (l_inst_next >= PC_CF_CHAR_INSTANCES) {
        return FALSE;
    }
    slot = l_inst_next++;

    for (b = 0; b < c->bone_num; b++) {
        const pc_cf_bone_c* bone = &c->bone[b];
        float(*out)[4] = l_inst[slot].bones[b];

        for (j = 0; j < bone_num; j++) {
            if (strcmp(bone_names[j], bone->name) == 0) {
                break;
            }
        }
        if (j < bone_num) {
            memcpy(out, bones[j], sizeof(pc_cf_mtx34));
        } else if (bone->parent >= 0) {
            const float(*p)[4] = l_inst[slot].bones[bone->parent];

            for (r = 0; r < 3; r++) {
                for (k = 0; k < 3; k++) {
                    out[r][k] = p[r][0] * bone->rest[0][k] + p[r][1] * bone->rest[1][k] + p[r][2] * bone->rest[2][k];
                }
                out[r][3] = p[r][0] * bone->t[0] + p[r][1] * bone->t[1] + p[r][2] * bone->t[2] + p[r][3];
            }
        } else {
            memset(out, 0, sizeof(pc_cf_mtx34)); /* nothing to place it by: not drawn */
        }
    }
    l_inst[slot].chr = c;
    l_inst[slot].eye = eye & 7;
    l_inst[slot].light = light;
    OPEN_DISP(graph);
    gDPNoOpTag(NEXT_POLY_OPA_DISP, PC_NOOP_CF_CHAR + slot);
    CLOSE_DISP(graph);
    return TRUE;
}

/* --- render side --- */

static void pc_cf_char_upload(pc_cf_char_c* c) {
    int i;

    for (i = 0; i < c->tex_num; i++) {
        if (!c->tex_gx[i].ready && c->tex_px[i] != NULL) {
            pc_gxh_tex_init(&c->tex_gx[i], c->tex_px[i], c->tex_w[i], c->tex_h[i], 1, 1, TRUE);
        }
    }
}

int pc_cf_char_is_tag(unsigned int tag) {
    return tag >= PC_NOOP_CF_CHAR && tag < PC_NOOP_CF_CHAR + PC_CF_CHAR_INSTANCES;
}

void pc_cf_char_noop(unsigned int tag, const float proj[4][4], const float view[3][4]) {
    static const float L[3] = { 0.1543f, 0.7715f, 0.6172f }; /* normalize(0.2, 1.0, 0.8) */
    int slot = (int)(tag - PC_NOOP_CF_CHAR);
    pc_cf_char_c* c;
    float(*bones)[3][4];
    float light;
    int pass, i, n;

    if (!pc_cf_char_is_tag(tag) || (c = l_inst[slot].chr) == NULL || c->loaded != 1) {
        return;
    }
    l_inst[slot].chr = NULL;
    pc_cf_char_upload(c);
    bones = l_inst[slot].bones;
    light = l_inst[slot].light;

    /* posed on the CPU: each vertex follows its one bone into the world, then the camera takes it */
    pc_gxh_camera(proj, view);
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < c->mesh_num; i++) {
            pc_cf_char_mesh_c* m = &c->mesh[i];
            const PCGXHTex* tex = NULL;
            int t = m->tex;
            GXCullMode cull;

            if (m->xlu != pass || m->verts == NULL || m->vert_num <= 0) {
                continue;
            }
            if (m->eyes && c->eye_tex[l_inst[slot].eye] >= 0) {
                t = c->eye_tex[l_inst[slot].eye];
            }
            if (t >= 0 && t < c->tex_num && c->tex_gx[t].ready) {
                tex = &c->tex_gx[t];
                pc_gxh_tex_set_wrap(&c->tex_gx[t], m->wrap_s, m->wrap_t);
            }
            /* GX culls by its own winding; the converter keeps the model's, so cull the other side */
            cull = m->cull == 0 ? GX_CULL_NONE : m->cull == 1 ? GX_CULL_FRONT : m->cull == 2 ? GX_CULL_BACK : GX_CULL_ALL;
            pc_gxh_mesh_state(tex, pass == 1 ? 1 : 0, pass == 0 && m->alpha_test, pass == 1 ? 1 : 2, cull);

            GXBegin(GX_TRIANGLES, PC_GXH_VTXFMT, GX_AUTO);
            for (n = 0; n < m->vert_num; n++) {
                const float* d = &m->verts[n * PC_CF_CHAR_VERT_FLOATS];
                const float(*b)[4] = bones[(int)(d[0] + 0.5f)];
                float x = b[0][0] * d[1] + b[0][1] * d[2] + b[0][2] * d[3] + b[0][3];
                float y = b[1][0] * d[1] + b[1][1] * d[2] + b[1][2] * d[3] + b[1][3];
                float z = b[2][0] * d[1] + b[2][1] * d[2] + b[2][2] * d[3] + b[2][3];
                float nx = b[0][0] * d[4] + b[0][1] * d[5] + b[0][2] * d[6];
                float ny = b[1][0] * d[4] + b[1][1] * d[5] + b[1][2] * d[6];
                float nz = b[2][0] * d[4] + b[2][1] * d[5] + b[2][2] * d[6];
                float len = sqrtf(nx * nx + ny * ny + nz * nz);
                float dot = len > 0.0f ? (nx * L[0] + ny * L[1] + nz * L[2]) / len : 0.0f;
                float h, shade;
                u8 v;

                /* soft light from above and in front, as the game's own characters */
                h = dot * 0.5f + 0.5f;
                h = h < 0.0f ? 0.0f : h > 1.0f ? 1.0f : h;
                shade = (0.62f + 0.38f * h) * light * 0.5f * 255.0f; /* halved: the TEV doubles it back */
                v = (u8)(shade >= 255.0f ? 255 : shade <= 0.0f ? 0 : shade + 0.5f);
                GXPosition3f32(x, y, z);
                GXColor4u8(v, v, v, 255);
                if (tex != NULL) {
                    GXTexCoord2f32(d[7], d[8]);
                }
            }
            GXEnd();
        }
    }
}
