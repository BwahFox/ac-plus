/* pc_cf_anim.c - City Folk's animations in the GameCube game. See pc_cf_anim.h. */
#include "pc_cf_anim.h"

#include "m_play.h"
#include "c_keyframe.h"
#include "sys_math.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PC_CF_ANIM_SCALE 1.25f /* GameCube world units per City Folk unit, as the café's room */

/* --- roost.cfanim --- */

typedef struct {
    char model[16];
    int bone_num;
    char (*bone_names)[16];
    const char** names; /* bone_names as pointers, for pc_cf_char_play_draw_mtx */
    u8* anchor;         /* per frame: 0 hidden, 1 Brewster's, 2 the seat's */
    float* mtx;         /* [frame][bone][3][4] */
} pc_cf_track_c;

typedef struct {
    char name[32];
    int frames;
    float* seat_w;
    int track_num;
    pc_cf_track_c* track;
} pc_cf_anim_c;

static int l_loaded; /* 0 not tried, 1 ok, -1 missing */
static pc_cf_mtx34 l_seat_in_owner;
static int l_anim_num;
static pc_cf_anim_c* l_anims;

static int pc_cf_anim_read(FILE* f, void* dst, size_t n) {
    return fread(dst, 1, n, f) == n;
}

static void pc_cf_anim_load(void) {
    FILE* f;
    char magic[4];
    u32 version, n, i, t, b;

    l_loaded = -1;
    f = fopen("cf_sky/anims/roost.cfanim", "rb");
    if (f == NULL) {
        return;
    }
    if (!pc_cf_anim_read(f, magic, 4) || memcmp(magic, "CFAN", 4) != 0 || !pc_cf_anim_read(f, &version, 4) ||
        version != 1 || !pc_cf_anim_read(f, l_seat_in_owner, sizeof(l_seat_in_owner)) || !pc_cf_anim_read(f, &n, 4) ||
        n > 64) {
        goto bad;
    }
    l_anims = calloc(n, sizeof(pc_cf_anim_c));
    l_anim_num = (int)n;
    for (i = 0; i < n; i++) {
        pc_cf_anim_c* a = &l_anims[i];
        u32 frames, tn;

        if (!pc_cf_anim_read(f, a->name, 32) || !pc_cf_anim_read(f, &frames, 4) || frames == 0 || frames > 4096) {
            goto bad;
        }
        a->name[31] = '\0';
        a->frames = (int)frames;
        a->seat_w = malloc(frames * sizeof(float));
        if (a->seat_w == NULL || !pc_cf_anim_read(f, a->seat_w, frames * sizeof(float)) || !pc_cf_anim_read(f, &tn, 4) ||
            tn > 16) {
            goto bad;
        }
        a->track_num = (int)tn;
        a->track = calloc(tn, sizeof(pc_cf_track_c));
        for (t = 0; t < tn; t++) {
            pc_cf_track_c* tr = &a->track[t];
            u32 bn;
            size_t mtx_size;

            if (!pc_cf_anim_read(f, tr->model, 16) || !pc_cf_anim_read(f, &bn, 4) || bn == 0 || bn > 32) {
                goto bad;
            }
            tr->model[15] = '\0';
            tr->bone_num = (int)bn;
            tr->bone_names = calloc(bn, 16);
            tr->names = calloc(bn, sizeof(char*));
            if (tr->bone_names == NULL || tr->names == NULL || !pc_cf_anim_read(f, tr->bone_names, bn * 16)) {
                goto bad;
            }
            for (b = 0; b < bn; b++) {
                tr->bone_names[b][15] = '\0';
                tr->names[b] = tr->bone_names[b];
            }
            tr->anchor = malloc(frames + 3);
            mtx_size = (size_t)frames * bn * 12 * sizeof(float);
            tr->mtx = malloc(mtx_size);
            if (tr->anchor == NULL || tr->mtx == NULL || !pc_cf_anim_read(f, tr->anchor, (frames + 3) & ~3u) ||
                !pc_cf_anim_read(f, tr->mtx, mtx_size)) {
                goto bad;
            }
        }
    }
    fclose(f);
    l_loaded = 1;
    printf("[CFAnim] roost.cfanim: %d animations\n", l_anim_num);
    return;
bad:
    fclose(f);
    l_anim_num = 0; /* what was read stays allocated: it's tried once */
    printf("[CFAnim] roost.cfanim is cut short\n");
}

static pc_cf_anim_c* pc_cf_anim_find(const char* name) {
    int i;

    if (l_loaded == 0) {
        pc_cf_anim_load();
    }
    for (i = 0; i < l_anim_num; i++) {
        if (strcmp(l_anims[i].name, name) == 0) {
            return &l_anims[i];
        }
    }
    return NULL;
}

int pc_cf_anim_frames(const char* name) {
    pc_cf_anim_c* a = pc_cf_anim_find(name);

    return a != NULL ? a->frames : 0;
}

/* out = a * b, 3x4 (column vectors, translation in column 3) */
static void pc_cf_mtx_mul(pc_cf_mtx34 out, const pc_cf_mtx34 a, const pc_cf_mtx34 b) {
    pc_cf_mtx34 m;
    int r, c;

    for (r = 0; r < 3; r++) {
        for (c = 0; c < 4; c++) {
            m[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c] + (c == 3 ? a[r][3] : 0.0f);
        }
    }
    memcpy(out, m, sizeof(m));
}

void pc_cf_anim_place(pc_cf_mtx34 out, float x, float y, float z, short angle_y) {
    float s = sin_s(angle_y), c = cos_s(angle_y);

    out[0][0] = c, out[0][1] = 0.0f, out[0][2] = s, out[0][3] = x;
    out[1][0] = 0.0f, out[1][1] = 1.0f, out[1][2] = 0.0f, out[1][3] = y;
    out[2][0] = -s, out[2][1] = 0.0f, out[2][2] = c, out[2][3] = z;
}

void pc_cf_anim_owner_seat(pc_cf_mtx34 out, const pc_cf_mtx34 owner) {
    pc_cf_mtx34 scaled;
    int r;

    if (l_loaded == 0) {
        pc_cf_anim_load();
    }
    /* the seat's frame is in City Folk units from Brewster: its place scales, its turn doesn't */
    memcpy(scaled, l_seat_in_owner, sizeof(scaled));
    for (r = 0; r < 3; r++) {
        scaled[r][3] *= PC_CF_ANIM_SCALE;
    }
    pc_cf_mtx_mul(out, owner, scaled);
}

int pc_cf_anim_draw(GAME_PLAY* play, const char* name, float frame, const pc_cf_mtx34 owner, const pc_cf_mtx34 seat,
                    const char* cup, int eye, float light, int parts) {
    pc_cf_anim_c* a = pc_cf_anim_find(name);
    pc_cf_mtx34 places[3], across, bones[32];
    int f0, f1, t, b, r, c;
    float k, w;

    if (a == NULL) {
        return FALSE;
    }
    if (frame < 0.0f) {
        frame = 0.0f;
    }
    if (frame > (float)(a->frames - 1)) {
        frame = (float)(a->frames - 1);
    }
    f0 = (int)frame;
    f1 = f0 + 1 < a->frames ? f0 + 1 : f0;
    k = frame - (float)f0;

    /* the two places, with City Folk units made the room's */
    pc_cf_anim_owner_seat(across, owner);
    w = a->seat_w[f0] + (a->seat_w[f1] - a->seat_w[f0]) * k;
    memcpy(places[1], owner, sizeof(pc_cf_mtx34));
    for (r = 0; r < 3; r++) {
        for (c = 0; c < 4; c++) {
            places[2][r][c] = seat != NULL ? across[r][c] + (seat[r][c] - across[r][c]) * w : across[r][c];
        }
    }
    for (t = 1; t < 3; t++) {
        for (r = 0; r < 3; r++) {
            for (c = 0; c < 3; c++) {
                places[t][r][c] *= PC_CF_ANIM_SCALE;
            }
        }
    }

    for (t = 0; t < a->track_num; t++) {
        pc_cf_track_c* tr = &a->track[t];
        int anchor = tr->anchor[k < 0.5f ? f0 : f1];
        int body = strcmp(tr->model, "brewster") == 0;
        int part = body ? PC_CF_ANIM_BODY
                        : strcmp(tr->model, "cup") == 0 || strcmp(tr->model, "saucer") == 0 ? PC_CF_ANIM_CUP
                                                                                          : PC_CF_ANIM_PROPS;
        const char* model = strcmp(tr->model, "cup") == 0 ? cup : tr->model;
        const float* m0 = &tr->mtx[(size_t)f0 * tr->bone_num * 12];
        const float* m1 = &tr->mtx[(size_t)f1 * tr->bone_num * 12];

        if (anchor == 0 || anchor > 2 || model == NULL || tr->bone_num > 32 ||
            !(parts & part)) {
            continue;
        }
        if (tr->anchor[f0] != tr->anchor[f1]) {
            m1 = m0 = k < 0.5f ? m0 : m1; /* a hand-over: no blending across it */
        }
        for (b = 0; b < tr->bone_num; b++) {
            pc_cf_mtx34 m;

            for (r = 0; r < 3; r++) {
                for (c = 0; c < 4; c++) {
                    int i = b * 12 + r * 4 + c;

                    m[r][c] = m0[i] + (m1[i] - m0[i]) * k;
                }
            }
            pc_cf_mtx_mul(bones[b], places[anchor], m);
        }
        pc_cf_char_play_draw_mtx(play, model, tr->bone_num, tr->names, (const pc_cf_mtx34*)bones,
                                 body ? eye : 0, light);
    }
    return TRUE;
}

/* --- player.cfpanm --- */

static int l_player_loaded; /* 0 not tried, 1 tried */
static cKF_Animation_R_c l_player[PC_CF_PANIM_NUM];
static int l_player_ok[PC_CF_PANIM_NUM];
static const char* const l_player_names[PC_CF_PANIM_NUM] = {
    "cafe_drink1",
    "cafe_drink2",
};

static void pc_cf_anim_player_load(void) {
    FILE* f;
    char magic[4];
    u32 version, n, i;

    l_player_loaded = 1;
    f = fopen("cf_sky/anims/player.cfpanm", "rb");
    if (f == NULL) {
        return;
    }
    if (!pc_cf_anim_read(f, magic, 4) || memcmp(magic, "CFPA", 4) != 0 || !pc_cf_anim_read(f, &version, 4) ||
        version != 1 || !pc_cf_anim_read(f, &n, 4)) {
        fclose(f);
        return;
    }
    for (i = 0; i < n; i++) {
        char name[32];
        u16 frames, joints, fixed_num, chan_num;
        u32 data_num;
        u8* flags;
        s16 *fixed, *keys, *data;
        int w;

        if (!pc_cf_anim_read(f, name, 32) || !pc_cf_anim_read(f, &frames, 2) || !pc_cf_anim_read(f, &joints, 2) ||
            joints != 26) {
            break;
        }
        name[31] = '\0';
        flags = malloc(joints);
        if (flags == NULL || !pc_cf_anim_read(f, flags, joints) || !pc_cf_anim_read(f, &fixed_num, 2)) {
            break;
        }
        fixed = malloc((fixed_num + 1) * sizeof(s16));
        if (fixed == NULL || !pc_cf_anim_read(f, fixed, fixed_num * sizeof(s16)) || !pc_cf_anim_read(f, &chan_num, 2)) {
            break;
        }
        keys = malloc((chan_num + 1) * sizeof(s16));
        if (keys == NULL || !pc_cf_anim_read(f, keys, chan_num * sizeof(s16)) || !pc_cf_anim_read(f, &data_num, 4) ||
            data_num > 1000000) {
            break;
        }
        data = malloc((data_num + 1) * sizeof(s16));
        if (data == NULL || !pc_cf_anim_read(f, data, data_num * sizeof(s16))) {
            break;
        }
        for (w = 0; w < PC_CF_PANIM_NUM; w++) {
            if (strcmp(name, l_player_names[w]) == 0) {
                l_player[w].flag_table = flags;
                l_player[w].data_table = data;
                l_player[w].key_table = keys;
                l_player[w].fixed_table = fixed;
                l_player[w].pad = -1;
                l_player[w].frames = (s16)frames;
                l_player_ok[w] = TRUE;
            }
        }
    }
    fclose(f);
    printf("[CFAnim] player.cfpanm: %u animations\n", (unsigned)n);
}

struct animation_s* pc_cf_anim_player(int which) {
    if (!l_player_loaded) {
        pc_cf_anim_player_load();
    }
    return which >= 0 && which < PC_CF_PANIM_NUM && l_player_ok[which] ? &l_player[which] : NULL;
}

int pc_cf_anim_player_available(int which) {
    return pc_cf_anim_player(which) != NULL;
}
