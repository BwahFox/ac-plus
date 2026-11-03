/* pc_cf_ftr.c - City Folk's furniture: the pieces the GameCube has none of the same name of, converted from the
 * player's disc (pc/tools/cf_furniture.py -> cf_sky/furniture.cfftr). Each is a furniture item from
 * PC_CF_FTR_ITEM_FIRST up and a furniture index past the GameCube's, with a profile made here as the GameCube's
 * furniture has one: display lists drawing its baked model (RGB5A3 textures, lit by normals) as the game draws its
 * own furniture, its shape, and the chair, sofa or bed it is. Names, prices, the HRA series and Nook's shop group
 * come from the file too. What moves (a dresser's drawer or doors, a clock's hands, what City Folk animates) is a part
 * of its own, drawn by the piece's own draw at where City Folk's animation has it (pc_cf_ftr_parts below). */

#include "pc_cf_ftr.h"
#include "pc_tv.h"
#include "ac_furniture.h"
#include "m_common_data.h"
#include "m_private.h"
#include "m_collision_bg.h"
#include "m_font.h"
#include "libforest/gbi_extensions.h"
#include "ac_my_room.h"
#include "c_keyframe.h"
#include "graph.h"
#include "sys_matrix.h"
#include "m_rcp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PC_CF_FTR_VTX_BATCH 63 /* vertices per gSPVertex: 21 triangles (emu64 holds 128) */
#define PC_CF_FTR_PARTS 16      /* moving parts a piece can have */
#define PC_CF_FTR_ANIMS 2

/* A moving part: animated (its move per frame), or a clock's hand (turned about pivot and axis by the time) */
enum { PC_CF_PART_ANIM, PC_CF_PART_HOUR, PC_CF_PART_MINUTE };
/* How an animation plays: a storage's opening (the game's own open/close), always, while switched on (A), once
 * when switched, once when switched on before the next one loops */
enum { PC_CF_MODE_STORAGE, PC_CF_MODE_LOOP, PC_CF_MODE_SWITCH_LOOP, PC_CF_MODE_ONCE, PC_CF_MODE_SWITCH_START };

/* What a piece does when you press A, as the GameCube's of its kind */
enum { PC_CF_DOES_NOTHING, PC_CF_DOES_TV, PC_CF_DOES_STEREO, PC_CF_DOES_INST = 0x100 };

typedef struct {
    u8 kind;
    f32 pivot[3];
    f32 axis[3];
    Gfx* opa;
    Gfx* xlu;
} pc_cf_ftr_part_c;

typedef struct {
    u8 mode;
    int frames;
    f32* mtx; /* [frames + 1][parts][3][4] */
} pc_cf_ftr_anim_c;

typedef struct {
    mActor_name_t item;
    u8 size;   /* 0 1x1, 1 2x1, 2 2x2 */
    u8 kind;   /* 0 plain, 1 chair, 2 sofa, 3 bed */
    u8 series; /* HRA series, 0xFF none */
    u8 group;  /* 0 A, 1 B, 2 C */
    u32 price;
    f32 height;
    char name[32];
    aFTR_PROFILE profile;
    u8 storage; /* aFTR_INTERACTION_STORAGE_*, 0 none */
    u16 does;   /* PC_CF_DOES_*, or PC_CF_DOES_INST + the instrument */
    Vtx screen[4]; /* a TV's: where its programs go (pc_tv_screen) */
    int nparts;
    pc_cf_ftr_part_c parts[PC_CF_FTR_PARTS];
    int nanims;
    pc_cf_ftr_anim_c anims[PC_CF_FTR_ANIMS];
} pc_cf_ftr_c;

static aFTR_vtable_c pc_cf_ftr_func;

static pc_cf_ftr_c* l_ftr[PC_CF_FTR_SLOTS];
static mActor_name_t l_list[PC_CF_FTR_SLOTS];
static int l_count;
static int l_loaded;

static u32 pc_cf_ftr_u32(const u8* p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

static f32 pc_cf_ftr_f32(const u8* p) {
    u32 v = pc_cf_ftr_u32(p);
    f32 f;

    memcpy(&f, &v, 4);
    return f;
}

typedef struct {
    u16 w, h;
    u8 ws, wt;
    u8* data;
} pc_cf_ftr_tex_c;

/* One mesh's commands into gfx (NULL: just count them): its texture, its modes, its vertices and triangles */
static int pc_cf_ftr_mesh_gfx(Gfx* gfx, const pc_cf_ftr_tex_c* tex, int xlu, int cull, int alpha, Vtx* vtx, int n) {
    static u8 white[4 * 4 * 2] ATTRIBUTE_ALIGN(32);
    Gfx* g = gfx;
    int i, k, cmds = 0;
    int tw = tex != NULL ? tex->w : 4, th = tex != NULL ? tex->h : 4;
    u32 geo = G_ZBUFFER | G_SHADE | G_FOG | G_LIGHTING | G_SHADING_SMOOTH | G_DECAL_LEQUAL;

    if (white[0] == 0) {
        memset(white, 0xFF, sizeof(white));
    }
    if (cull == 2) {
        geo |= G_CULL_BACK;
    } else if (cull == 1) {
        geo |= G_CULL_FRONT;
    }
#define CMD(x) (gfx != NULL ? (void)(x) : (void)0), cmds++
    CMD(gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_ON));
    if (xlu) {
        CMD(gDPSetRenderMode(g++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_SURF2));
    } else if (alpha) {
        CMD(gDPSetRenderMode(g++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_TEX_EDGE2));
    } else {
        CMD(gDPSetRenderMode(g++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2));
    }
    if (xlu && tex == NULL) {
        /* untextured see-through: a machine's glass (City Folk shines a reflection on it). Faint, to see inside */
        CMD(gDPSetCombineLERP(g++, TEXEL0, 0, SHADE, 0, 0, 0, 0, TEXEL0, PRIMITIVE, 0, COMBINED, 0, COMBINED, 0, PRIMITIVE, 0));
    } else {
        CMD(gDPSetCombineLERP(g++, TEXEL0, 0, SHADE, 0, 0, 0, 0, TEXEL0, PRIMITIVE, 0, COMBINED, 0, 0, 0, 0, COMBINED));
    }
    /* (the runtime macro takes the height before the width, and its arguments unbracketed: plain values only) */
    CMD(gDPSetTextureImage_Dolphin(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, th, tw, tex != NULL ? tex->data : white));
    CMD(gDPSetTile_Dolphin(g++, G_DOLPHIN_TLUT_DEFAULT_MODE, 0, 0, tex != NULL ? tex->ws : GX_REPEAT,
                           tex != NULL ? tex->wt : GX_REPEAT, 0, 0));
    CMD(gDPSetPrimColor(g++, 0, 128, 255, 255, 255, xlu && tex == NULL ? 48 : 255));
    CMD(gSPLoadGeometryMode(g++, geo));
    for (i = 0; i < n; i += PC_CF_FTR_VTX_BATCH) {
        int m = n - i < PC_CF_FTR_VTX_BATCH ? n - i : PC_CF_FTR_VTX_BATCH;

        CMD(gSPVertex(g++, vtx + i, m, 0));
        for (k = 0; k + 2 < m; k += 3) {
            /* City Folk's (GX) triangles wind the other way round from the display lists' */
            CMD(gSP1Triangle(g++, k, k + 2, k + 1, 0));
        }
    }
#undef CMD
    return cmds;
}

/* One piece from f; FALSE when the file's cut short */
static int pc_cf_ftr_read(FILE* f) {
    u8 head[52];
    u8 n4[4];
    pc_cf_ftr_c* c;
    pc_cf_ftr_tex_c* tex = NULL;
    u32 ntex, nmesh, t, m;
    long mesh_at;
    int slot;

    if (fread(head, 1, sizeof(head), f) != sizeof(head) || fread(n4, 1, 4, f) != 4) {
        return FALSE;
    }
    c = (pc_cf_ftr_c*)calloc(1, sizeof(pc_cf_ftr_c));
    if (c == NULL) {
        return FALSE;
    }
    c->item = (mActor_name_t)(head[0] | head[1] << 8);
    c->size = head[2];
    c->kind = head[3];
    c->series = head[4];
    c->group = head[5];
    c->does = (u16)(head[6] | head[7] << 8);
    c->price = pc_cf_ftr_u32(head + 8);
    c->height = pc_cf_ftr_f32(head + 12);
    memcpy(c->name, head + 20, 31);
    ntex = pc_cf_ftr_u32(n4);
    if (ntex > 64 || (ntex > 0 && (tex = (pc_cf_ftr_tex_c*)calloc(ntex, sizeof(*tex))) == NULL)) {
        free(c);
        return FALSE;
    }
    for (t = 0; t < ntex; t++) {
        u8 th[8];
        u32 bytes;

        if (fread(th, 1, 8, f) != 8) {
            return FALSE;
        }
        tex[t].w = (u16)(th[0] | th[1] << 8);
        tex[t].h = (u16)(th[2] | th[3] << 8);
        tex[t].ws = th[4] > 2 ? GX_REPEAT : th[4];
        tex[t].wt = th[5] > 2 ? GX_REPEAT : th[5];
        bytes = (u32)tex[t].w * tex[t].h * 2;
        tex[t].data = (u8*)malloc(bytes);
        if (tex[t].data == NULL || fread(tex[t].data, 1, bytes, f) != bytes) {
            return FALSE;
        }
    }
    if (fread(n4, 1, 4, f) != 4) {
        return FALSE;
    }
    nmesh = pc_cf_ftr_u32(n4);

    /* first pass: how many commands, per part; second: the vertices and commands */
    mesh_at = ftell(f);
    {
        int cmds[PC_CF_FTR_PARTS + 1][2];
        Gfx* dl[PC_CF_FTR_PARTS + 1][2];
        Gfx* at[PC_CF_FTR_PARTS + 1][2];
        int any[PC_CF_FTR_PARTS + 1][2];
        int pt, x;

        for (pt = 0; pt <= PC_CF_FTR_PARTS; pt++) {
            cmds[pt][0] = cmds[pt][1] = 1;
            any[pt][0] = any[pt][1] = FALSE;
        }
        for (m = 0; m < nmesh; m++) {
            u8 mh[12];
            u32 nv;

            if (fread(mh, 1, 12, f) != 12) {
                return FALSE;
            }
            nv = pc_cf_ftr_u32(mh + 8);
            pt = mh[7] <= PC_CF_FTR_PARTS ? mh[7] : 0;
            x = mh[4] != 0;
            cmds[pt][x] += pc_cf_ftr_mesh_gfx(NULL, NULL, x, mh[5], mh[6], NULL, (int)nv);
            any[pt][x] = TRUE;
            fseek(f, (long)nv * 18, SEEK_CUR);
        }
        for (pt = 0; pt <= PC_CF_FTR_PARTS; pt++) {
            for (x = 0; x < 2; x++) {
                dl[pt][x] = at[pt][x] = NULL;
                if ((pt == 0 || any[pt][x]) && (dl[pt][x] = at[pt][x] = (Gfx*)malloc(sizeof(Gfx) * cmds[pt][x])) == NULL) {
                    return FALSE;
                }
            }
        }
        fseek(f, mesh_at, SEEK_SET);
        for (m = 0; m < nmesh; m++) {
            u8 mh[12];
            s32 ti;
            u32 nv, v;
            Vtx* vtx;

            if (fread(mh, 1, 12, f) != 12) {
                return FALSE;
            }
            ti = (s32)pc_cf_ftr_u32(mh);
            nv = pc_cf_ftr_u32(mh + 8);
            pt = mh[7] <= PC_CF_FTR_PARTS ? mh[7] : 0;
            x = mh[4] != 0;
            vtx = (Vtx*)malloc(sizeof(Vtx) * (nv ? nv : 1));
            if (vtx == NULL) {
                return FALSE;
            }
            for (v = 0; v < nv; v++) {
                u8 r[18];
                f32 s, tt;

                if (fread(r, 1, 18, f) != 18) {
                    return FALSE;
                }
                vtx[v].n.ob[0] = (s16)(r[0] | r[1] << 8);
                vtx[v].n.ob[1] = (s16)(r[2] | r[3] << 8);
                vtx[v].n.ob[2] = (s16)(r[4] | r[5] << 8);
                vtx[v].n.flag = 0;
                s = pc_cf_ftr_f32(r + 10) * 32.0f;
                tt = pc_cf_ftr_f32(r + 14) * 32.0f;
                vtx[v].n.tc[0] = (s16)(s > 32767.0f ? 32767 : s < -32768.0f ? -32768 : s);
                vtx[v].n.tc[1] = (s16)(tt > 32767.0f ? 32767 : tt < -32768.0f ? -32768 : tt);
                vtx[v].n.n[0] = (s8)r[6];
                vtx[v].n.n[1] = (s8)r[7];
                vtx[v].n.n[2] = (s8)r[8];
                vtx[v].n.a = 255;
            }
            at[pt][x] += pc_cf_ftr_mesh_gfx(at[pt][x], ti >= 0 && ti < (s32)ntex ? &tex[ti] : NULL, x, mh[5], mh[6],
                                            vtx, (int)nv);
        }
        for (pt = 0; pt <= PC_CF_FTR_PARTS; pt++) {
            for (x = 0; x < 2; x++) {
                if (at[pt][x] != NULL) {
                    gSPEndDisplayList(at[pt][x]++);
                }
            }
        }
        c->profile.opaque0 = any[0][0] ? dl[0][0] : NULL;
        c->profile.translucent0 = any[0][1] ? dl[0][1] : NULL;

        /* its moving parts */
        {
            u8 ph[8];
            u32 np, na, a;

            if (fread(ph, 1, 8, f) != 8) {
                return FALSE;
            }
            c->storage = ph[0];
            np = pc_cf_ftr_u32(ph + 4);
            if (np > PC_CF_FTR_PARTS) {
                return FALSE;
            }
            c->nparts = (int)np;
            for (pt = 0; pt < (int)np; pt++) {
                u8 r[28];
                int k;

                if (fread(r, 1, 28, f) != 28) {
                    return FALSE;
                }
                c->parts[pt].kind = r[0];
                for (k = 0; k < 3; k++) {
                    c->parts[pt].pivot[k] = pc_cf_ftr_f32(r + 4 + 4 * k);
                    c->parts[pt].axis[k] = pc_cf_ftr_f32(r + 16 + 4 * k);
                }
                c->parts[pt].opa = any[pt + 1][0] ? dl[pt + 1][0] : NULL;
                c->parts[pt].xlu = any[pt + 1][1] ? dl[pt + 1][1] : NULL;
            }
            if (fread(ph, 1, 4, f) != 4) {
                return FALSE;
            }
            na = pc_cf_ftr_u32(ph);
            for (a = 0; a < na; a++) {
                u32 frames;
                size_t n;
                f32* mtx;

                if (fread(ph, 1, 8, f) != 8 || (frames = pc_cf_ftr_u32(ph + 4)) > 4096) {
                    return FALSE;
                }
                n = (size_t)(frames + 1) * np * 12;
                mtx = (f32*)malloc(sizeof(f32) * (n ? n : 1));
                if (mtx == NULL || fread(mtx, sizeof(f32), n, f) != n) {
                    return FALSE;
                }
                if (a < PC_CF_FTR_ANIMS) {
                    c->anims[a].mode = ph[0];
                    c->anims[a].frames = (int)frames;
                    c->anims[a].mtx = mtx;
                    c->nanims = (int)a + 1;
                } else {
                    free(mtx);
                }
            }
        }
    }
    {
        u32 nscreen;
        int v;

        if (fread(n4, 1, 4, f) != 4 || (nscreen = pc_cf_ftr_u32(n4)) > 4) {
            return FALSE;
        }
        for (v = 0; v < (int)nscreen; v++) {
            u8 r[6];

            if (fread(r, 1, 6, f) != 6) {
                return FALSE;
            }
            c->screen[v].v.ob[0] = (s16)(r[0] | r[1] << 8);
            c->screen[v].v.ob[1] = (s16)(r[2] | r[3] << 8);
            c->screen[v].v.ob[2] = (s16)(r[4] | r[5] << 8);
            c->screen[v].v.cn[0] = c->screen[v].v.cn[1] = c->screen[v].v.cn[2] = c->screen[v].v.cn[3] = 255;
        }
        if (nscreen != 4 && c->does == PC_CF_DOES_TV) {
            c->does = PC_CF_DOES_NOTHING;
        }
    }
    free(tex); /* (the textures themselves stay: the display lists use them) */

    c->profile.height = c->height > 0.0f ? c->height : 0.0f;
    c->profile.scale = 0.1f;
    c->profile.shape = c->size == 2 ? aFTR_SHAPE_TYPEC : c->size == 1 ? aFTR_SHAPE_TYPEB_0 : aFTR_SHAPE_TYPEA;
    c->profile.move_bg_type = c->size == 2 ? mCoBG_FTR_TYPEC : c->size == 1 ? mCoBG_FTR_TYPEB_0 : mCoBG_FTR_TYPEA;
    c->profile.kankyo_map = aFTR_KANKYO_MAP_NONE;
    c->profile.contact_action = c->kind == 1   ? aFTR_CONTACT_ACTION_CHAIR_UNIDIRECTIONAL
                                : c->kind == 2 ? aFTR_CONTACT_ACTION_CHAIR_SOFA
                                : c->kind == 3 ? (c->size == 2 ? aFTR_CONTACT_ACTION_BED_DOUBLE : aFTR_CONTACT_ACTION_BED_SINGLE)
                                               : aFTR_CONTACT_ACTION_NONE;
    if (c->storage != 0 || c->nparts > 0 || c->does != PC_CF_DOES_NOTHING) {
        c->profile.interaction_type = c->storage | (c->does == PC_CF_DOES_TV ? aFTR_INTERACTION_START_DISABLED
                                                    : c->does == PC_CF_DOES_STEREO ? aFTR_INTERACTION_MUSIC_DISK
                                                                                   : 0);
        c->profile.vtable = &pc_cf_ftr_func;
    }
    slot = (c->item - PC_CF_FTR_ITEM_FIRST) >> 2;
    if (slot < 0 || slot >= PC_CF_FTR_SLOTS || l_ftr[slot] != NULL) {
        free(c);
        return TRUE;
    }
    l_ftr[slot] = c;
    l_list[l_count++] = c->item;
    return TRUE;
}

/* Nook's furniture lists A, B and C (data/item/ftr_list.c) with City Folk's of each group after the GameCube's:
 * the shop sells them, and what checks those lists (the catalog's prices, a secret code's) finds them there */
extern mActor_name_t* mSP_ftr_list[];

static void pc_cf_ftr_shop_lists(void) {
    int g;

    for (g = 0; g < 3; g++) {
        mActor_name_t* old = mSP_ftr_list[g];
        mActor_name_t* list;
        int n = 0, m = 0, i;

        while (old != NULL && old[n] != EMPTY_NO) {
            n++;
        }
        for (i = 0; i < l_count; i++) {
            m += l_ftr[(l_list[i] - PC_CF_FTR_ITEM_FIRST) >> 2]->group == g;
        }
        if (m == 0 || (list = (mActor_name_t*)malloc(sizeof(mActor_name_t) * (n + m + 1))) == NULL) {
            continue;
        }
        if (n > 0) {
            memcpy(list, old, sizeof(mActor_name_t) * n);
        }
        for (i = 0; i < l_count; i++) {
            if (l_ftr[(l_list[i] - PC_CF_FTR_ITEM_FIRST) >> 2]->group == g) {
                list[n++] = l_list[i];
            }
        }
        list[n] = EMPTY_NO;
        mSP_ftr_list[g] = list;
    }
}

static void pc_cf_ftr_load(void) {
    FILE* f;
    u8 head[12];
    u32 n, i;

    if (l_loaded) {
        return;
    }
    l_loaded = TRUE;
    f = fopen("cf_sky/furniture.cfftr", "rb");
    if (f == NULL) {
        return;
    }
    if (fread(head, 1, 12, f) == 12 && memcmp(head, "CFFT", 4) == 0 && pc_cf_ftr_u32(head + 4) == 2) {
        n = pc_cf_ftr_u32(head + 8);
        for (i = 0; i < n; i++) {
            if (!pc_cf_ftr_read(f)) {
                printf("[cf ftr] cut short after %d pieces\n", l_count);
                break;
            }
        }
    }
    fclose(f);
    pc_cf_ftr_shop_lists();
    printf("[cf ftr] %d pieces of furniture\n", l_count);
}

static pc_cf_ftr_c* pc_cf_ftr_of_item(mActor_name_t item) {
    int slot;

    if (item < PC_CF_FTR_ITEM_FIRST || item >= PC_CF_FTR_ITEM_FIRST + PC_CF_FTR_SLOTS * 4) {
        return NULL;
    }
    pc_cf_ftr_load();
    slot = (item - PC_CF_FTR_ITEM_FIRST) >> 2;
    return l_ftr[slot];
}

static pc_cf_ftr_c* pc_cf_ftr_of_idx(int ftr_idx) {
    int slot = ftr_idx - PC_CF_FTR_IDX_FIRST;

    if (slot < 0 || slot >= PC_CF_FTR_SLOTS) {
        return NULL;
    }
    pc_cf_ftr_load();
    return l_ftr[slot];
}

int pc_cf_ftr_item(mActor_name_t item) {
    return pc_cf_ftr_of_item(item) != NULL;
}

int pc_cf_ftr_idx(int ftr_idx) {
    return pc_cf_ftr_of_idx(ftr_idx) != NULL;
}

int pc_cf_ftr_count(void) {
    pc_cf_ftr_load();
    return l_count;
}

mActor_name_t pc_cf_ftr_nth(int n) {
    pc_cf_ftr_load();
    return n >= 0 && n < l_count ? l_list[n] : 0;
}

aFTR_PROFILE* pc_cf_ftr_profile(int ftr_idx) {
    pc_cf_ftr_c* c = pc_cf_ftr_of_idx(ftr_idx);

    return c != NULL ? &c->profile : NULL;
}

int pc_cf_ftr_name(mActor_name_t item, u8* dst, int len) {
    pc_cf_ftr_c* c = pc_cf_ftr_of_item(item);
    int k, n;

    if (c == NULL) {
        return FALSE;
    }
    n = (int)strlen(c->name);
    for (k = 0; k < len; k++) {
        dst[k] = k < n ? (u8)c->name[k] : CHAR_SPACE;
    }
    return TRUE;
}

u32 pc_cf_ftr_price(mActor_name_t item) {
    pc_cf_ftr_c* c = pc_cf_ftr_of_item(item);

    return c != NULL ? c->price : 0;
}

int pc_cf_ftr_series(int ftr_idx) {
    pc_cf_ftr_c* c = pc_cf_ftr_of_idx(ftr_idx);

    return c != NULL && c->series != 0xFF ? c->series : -1;
}

int pc_cf_ftr_group(int ftr_idx) {
    pc_cf_ftr_c* c = pc_cf_ftr_of_idx(ftr_idx);

    return c != NULL ? (c->group > 2 ? 0 : c->group) : -1; /* (a fish or bug's, not Nook's: any) */
}

int pc_cf_ftr_surface(int ftr_idx) {
    pc_cf_ftr_c* c = pc_cf_ftr_of_idx(ftr_idx);

    return c != NULL && c->height > 0.0f;
}

/* --- the catalog: which ones each resident has had (the GameCube's bits stop at its own furniture) ---
 * CFFC in the town's ac-plus data: u32 bits[PLAYER_NUM][PC_CF_FTR_SLOTS / 32], one per slot */
static u32 l_collected[PLAYER_NUM][PC_CF_FTR_CATALOG_SLOTS / 32];

static int pc_cf_ftr_player(void) {
    int i;

    for (i = 0; i < PLAYER_NUM; i++) {
        if (Common_Get(now_private) == Save_GetPointer(private_data[i])) {
            return i;
        }
    }
    return -1;
}

int pc_cf_ftr_collected(mActor_name_t item) {
    int p = pc_cf_ftr_player();
    int slot = (item - PC_CF_FTR_ITEM_FIRST) >> 2;

    return p >= 0 && slot < PC_CF_FTR_CATALOG_SLOTS && pc_cf_ftr_of_item(item) != NULL && (l_collected[p][slot >> 5] >> (slot & 31) & 1);
}

void pc_cf_ftr_collect(mActor_name_t item) {
    int p = pc_cf_ftr_player();
    int slot = (item - PC_CF_FTR_ITEM_FIRST) >> 2;

    if (p >= 0 && slot < PC_CF_FTR_CATALOG_SLOTS && pc_cf_ftr_of_item(item) != NULL) {
        l_collected[p][slot >> 5] |= 1u << (slot & 31);
    }
}

void pc_cf_ftr_reset(void) {
    memset(l_collected, 0, sizeof(l_collected));
}

int pc_cf_ftr_read_chunk(const char tag[4], void* fp, unsigned int size) {
    FILE* f = (FILE*)fp;

    if (memcmp(tag, "CFFC", 4) != 0) {
        return FALSE;
    }
    if (size != sizeof(l_collected) || fread(l_collected, 1, size, f) != size) {
        pc_cf_ftr_reset();
        if (size != sizeof(l_collected)) {
            fseek(f, (long)size, SEEK_CUR);
        }
    }
    return TRUE;
}

int pc_cf_ftr_write_chunk(void* fp) {
    FILE* f = (FILE*)fp;
    u32 size = sizeof(l_collected);

    return fwrite("CFFC", 1, 4, f) == 4 && fwrite(&size, 4, 1, f) == 1 && fwrite(l_collected, 1, size, f) == size;
}

/* --- Moving parts ---
 * A storage piece opens and closes as the game's own do (open_close_common_move_proc), which plays the piece's
 * keyframe skeleton: a one-joint stand-in that never moves here, its frame telling where the opening is. Other
 * animations keep their place in the same keyframe's frame control, which nothing else touches without a rig:
 * current_frame the frame, mode the animation playing (-1 none), previous_frame the switch last seen (-1 not yet). */

static cKF_Joint_R_c l_pc_cf_joint = { NULL, 0, 0, { 0, 0, 0 } };
static cKF_Skeleton_R_c l_pc_cf_skel = { 1, 0, &l_pc_cf_joint };
static u8 l_pc_cf_flags[1] = { 0 };
static s16 l_pc_cf_fixed[6] = { 0 };
static cKF_Animation_R_c l_pc_cf_anim = { l_pc_cf_flags, NULL, NULL, l_pc_cf_fixed, 0, 64 };

/* the opening's frames in the game's keyframe: from 1, half of City Folk's (it plays them at half a frame a frame) */
static f32 pc_cf_ftr_storage_end(const pc_cf_ftr_c* c) {
    return 1.0f + (c->nanims > 0 ? (f32)c->anims[0].frames : 18.0f) * 0.5f;
}

static void pc_cf_ftr_ct(FTR_ACTOR* ftr_actor, u8* data) {
    pc_cf_ftr_c* c = pc_cf_ftr_of_idx(ftr_actor->name);
    cKF_FrameControl_c* fc = &ftr_actor->keyframe.frame_control;

    if (c != NULL && c->storage != 0) {
        cKF_SkeletonInfo_R_c* keyframe = &ftr_actor->keyframe;

        cKF_SkeletonInfo_R_ct(keyframe, &l_pc_cf_skel, &l_pc_cf_anim, ftr_actor->joint, ftr_actor->morph);
        cKF_SkeletonInfo_R_init_standard_stop(keyframe, &l_pc_cf_anim, NULL);
        keyframe->frame_control.speed = 0.0f;
        cKF_SkeletonInfo_R_play(keyframe);
        return;
    }
    fc->current_frame = 0.0f;
    fc->mode = -1;
    fc->previous_frame = -1.0f;
}

static void pc_cf_ftr_mv(FTR_ACTOR* ftr_actor, ACTOR* my_room_actor, GAME* game, u8* data) {
    pc_cf_ftr_c* c = pc_cf_ftr_of_idx(ftr_actor->name);
    cKF_FrameControl_c* fc = &ftr_actor->keyframe.frame_control;
    int on = ftr_actor->switch_bit != FALSE;

    if (c == NULL) {
        return;
    }
    if (c->does == PC_CF_DOES_TV) {
        if (ftr_actor->switch_changed_flag) { /* (the program's sound comes from pc_tv_screen, when it's drawn) */
            sAdo_OngenTrgStart(on ? 0x16 : 0x17, &ftr_actor->position);
        }
    } else if (c->does == PC_CF_DOES_STEREO) {
        if (Common_Get(clip).my_room_clip != NULL) {
            Common_Get(clip).my_room_clip->mini_disk_common_move_proc(ftr_actor, my_room_actor, game, 0.0f, 0.0f);
        }
    } else if (c->does >= PC_CF_DOES_INST) {
        if (Common_Get(clip).my_room_clip != NULL) {
            Common_Get(clip).my_room_clip->sound_melody_proc(ftr_actor, my_room_actor, c->does - PC_CF_DOES_INST);
        }
    }
    if (c->storage != 0) {
        if (Common_Get(clip).my_room_clip != NULL) {
            Common_Get(clip).my_room_clip->open_close_common_move_proc(ftr_actor, my_room_actor, game, 1.0f,
                                                                       pc_cf_ftr_storage_end(c));
        }
        return;
    }
    if (c->nanims == 0) {
        return;
    }
    switch (c->anims[0].mode) {
        case PC_CF_MODE_LOOP:
            fc->mode = 0;
            fc->current_frame += 1.0f;
            if (fc->current_frame >= (f32)c->anims[0].frames) {
                fc->current_frame = 0.0f;
            }
            break;
        case PC_CF_MODE_SWITCH_LOOP:
        case PC_CF_MODE_SWITCH_START:
            if (!on) {
                fc->mode = -1;
                fc->current_frame = 0.0f;
                break;
            }
            if (fc->mode < 0) {
                fc->mode = 0;
                fc->current_frame = 0.0f;
            }
            fc->current_frame += 1.0f;
            if (fc->current_frame >= (f32)c->anims[fc->mode].frames) {
                fc->current_frame = 0.0f;
                if (c->anims[fc->mode].mode == PC_CF_MODE_SWITCH_START && fc->mode + 1 < c->nanims) {
                    fc->mode++; /* started: the loop now */
                }
            }
            break;
        default: /* once, when switched */
            if (fc->previous_frame < 0.0f) {
                fc->previous_frame = (f32)on; /* as it was placed: nothing to play */
            } else if ((f32)on != fc->previous_frame) {
                fc->previous_frame = (f32)on;
                fc->mode = 0;
                fc->current_frame = 0.0f;
            }
            if (fc->mode >= 0) {
                fc->current_frame += 1.0f;
                if (fc->current_frame >= (f32)c->anims[0].frames) {
                    fc->mode = -1;
                    fc->current_frame = 0.0f;
                }
            }
            break;
    }
}

/* a turn by angle (radians) about axis through pivot, as a matrix */
static void pc_cf_ftr_turn(MtxF* m, const f32 pivot[3], const f32 axis[3], f32 angle) {
    f32 x = axis[0], y = axis[1], z = axis[2];
    f32 cs = cosf(angle), sn = sinf(angle), t = 1.0f - cs;
    f32 r[3][3];
    int i, k;

    r[0][0] = t * x * x + cs, r[0][1] = t * x * y - sn * z, r[0][2] = t * x * z + sn * y;
    r[1][0] = t * x * y + sn * z, r[1][1] = t * y * y + cs, r[1][2] = t * y * z - sn * x;
    r[2][0] = t * x * z - sn * y, r[2][1] = t * y * z + sn * x, r[2][2] = t * z * z + cs;
    for (i = 0; i < 3; i++) {
        for (k = 0; k < 3; k++) {
            m->mf[k][i] = r[i][k];
        }
        m->mf[3][i] = pivot[i] - (r[i][0] * pivot[0] + r[i][1] * pivot[1] + r[i][2] * pivot[2]);
        m->mf[i][3] = 0.0f;
    }
    m->mf[3][3] = 1.0f;
}

static void pc_cf_ftr_draw_parts(const pc_cf_ftr_c* c, GAME* game, const pc_cf_ftr_anim_c* a, f32 frame) {
    int f0 = 0, f1 = 0, p;
    f32 k = 0.0f;

    if (a != NULL) {
        frame = frame < 0.0f ? 0.0f : frame > (f32)a->frames ? (f32)a->frames : frame;
        f0 = (int)frame;
        f1 = f0 < a->frames ? f0 + 1 : f0;
        k = frame - (f32)f0;
    }
    for (p = 0; p < c->nparts; p++) {
        const pc_cf_ftr_part_c* part = &c->parts[p];
        MtxF m;

        if (part->opa == NULL && part->xlu == NULL) {
            continue;
        }
        if ((part->kind == PC_CF_PART_HOUR || part->kind == PC_CF_PART_MINUTE)) {
            /* clockwise, seen from the front: as the GameCube's clocks' hands (their angles from 12) */
            s16 ang = part->kind == PC_CF_PART_HOUR ? Common_Get(time).rad_hour : Common_Get(time).rad_min;

            pc_cf_ftr_turn(&m, part->pivot, part->axis, -(f32)ang * (2.0f * 3.14159265f / 65536.0f));
        } else if (a != NULL) {
            const f32* m0 = a->mtx + ((size_t)f0 * c->nparts + p) * 12;
            const f32* m1 = a->mtx + ((size_t)f1 * c->nparts + p) * 12;
            int i, j;

            for (i = 0; i < 3; i++) {
                for (j = 0; j < 4; j++) {
                    m.mf[j][i] = m0[i * 4 + j] + (m1[i * 4 + j] - m0[i * 4 + j]) * k;
                }
                m.mf[i][3] = 0.0f;
            }
            m.mf[3][3] = 1.0f;
        } else {
            int i, j; /* still: where it stands */

            for (i = 0; i < 4; i++) {
                for (j = 0; j < 4; j++) {
                    m.mf[i][j] = i == j ? 1.0f : 0.0f;
                }
            }
        }
        Matrix_push();
        Matrix_mult(&m, MTX_MULT);
        OPEN_DISP(game->graph);
        if (part->opa != NULL) {
            gSPMatrix(NEXT_POLY_OPA_DISP, _Matrix_to_Mtx_new(game->graph), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(NEXT_POLY_OPA_DISP, part->opa);
        }
        if (part->xlu != NULL) {
            gSPMatrix(NEXT_POLY_XLU_DISP, _Matrix_to_Mtx_new(game->graph), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(NEXT_POLY_XLU_DISP, part->xlu);
        }
        CLOSE_DISP(game->graph);
        Matrix_pull();
    }
}

f32 pc_cf_ftr_model_width(mActor_name_t item) {
    const pc_cf_ftr_c* c = pc_cf_ftr_of_item(item);

    return c != NULL ? c->height : 0.0f;
}

void pc_cf_ftr_draw_model(mActor_name_t item, struct game_s* game_, f32 frame) {
    GAME* game = (GAME*)game_;
    const pc_cf_ftr_c* c = pc_cf_ftr_of_item(item);
    const pc_cf_ftr_anim_c* a;

    if (c == NULL) {
        return;
    }
    a = c->nanims > 0 ? &c->anims[0] : NULL;
    if (a != NULL && a->frames > 0) {
        frame -= (f32)((int)(frame / (f32)a->frames) * a->frames);
    }
    OPEN_DISP(game->graph);
    if (c->profile.opaque0 != NULL) {
        gSPMatrix(NEXT_POLY_OPA_DISP, _Matrix_to_Mtx_new(game->graph), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(NEXT_POLY_OPA_DISP, c->profile.opaque0);
    }
    if (c->profile.translucent0 != NULL) {
        gSPMatrix(NEXT_POLY_XLU_DISP, _Matrix_to_Mtx_new(game->graph), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(NEXT_POLY_XLU_DISP, c->profile.translucent0);
    }
    CLOSE_DISP(game->graph);
    pc_cf_ftr_draw_parts(c, game, a, frame);
}

static void pc_cf_ftr_dw(FTR_ACTOR* ftr_actor, ACTOR* my_room_actor, GAME* game, u8* data) {
    pc_cf_ftr_c* c = pc_cf_ftr_of_idx(ftr_actor->name);
    cKF_FrameControl_c* fc = &ftr_actor->keyframe.frame_control;
    const pc_cf_ftr_anim_c* a = NULL;
    f32 frame = 0.0f;

    if (c != NULL && c->does == PC_CF_DOES_TV && ftr_actor->switch_bit) {
        Gfx* screen = pc_tv_screen(game, c->screen, &ftr_actor->position);

        if (screen != NULL) {
            OPEN_DISP(game->graph);
            gSPMatrix(NEXT_POLY_OPA_DISP, _Matrix_to_Mtx_new(game->graph), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(NEXT_POLY_OPA_DISP, screen);
            CLOSE_DISP(game->graph);
        }
    }
    if (c == NULL || c->nparts == 0) {
        return;
    }
    if (c->storage != 0) {
        if (c->nanims > 0) {
            a = &c->anims[0];
            frame = (fc->current_frame - 1.0f) * 2.0f;
        }
    } else if (fc->mode >= 0 && fc->mode < c->nanims) {
        a = &c->anims[fc->mode];
        frame = fc->current_frame;
    } else if (c->nanims > 0) {
        a = &c->anims[0]; /* at rest: its first frame */
    }
    if (a != NULL && getenv("PC_CF_FTR_POSE") != NULL) { /* dev: every piece at this share of its animation (0-1) */
        frame = (f32)atof(getenv("PC_CF_FTR_POSE")) * (f32)a->frames;
    }
    pc_cf_ftr_draw_parts(c, game, a, frame);
}

static void pc_cf_ftr_dt(FTR_ACTOR* ftr_actor, u8* data) {
}

static aFTR_vtable_c pc_cf_ftr_func = {
    &pc_cf_ftr_ct, &pc_cf_ftr_mv, &pc_cf_ftr_dw, &pc_cf_ftr_dt, NULL,
};
