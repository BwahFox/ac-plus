/* pc_nlr_outside.c - the extra rooms seen from outside: a wing on the house for each. See pc_nlrooms.h.
 *
 * The biggest house's model (obj_s/w_myhome4) is drawn in its own units, 100 to the world's, its front (the door)
 * towards +x, and turned 45 degrees in the world: the left column's houses face south-east, the right column's
 * south-west. A wing is a low box against the house, the side walls' texture (t2) on its walls and the roof's (t1) on
 * its lean-to roof, in the house's colours (segment 8). The left room's wing is on the house's +z side, the right
 * room's on its -z side (as seen walking in), the back room's behind it; all inside the yard's fence.
 *
 * The ground under a wing is solid, as the house's is. The wings are within the house's own plot (reserved for it
 * in the town's items), but should anything be on the ground there when a room's built it goes: weeds and holes are
 * cleared, items and flowers sent to the dump; a tree or a rock (or the dump being full) holds the build up until
 * it's cleared. */
#include "pc_nlrooms.h"

#include "m_all_grow_ovl.h"
#include "m_collision_bg.h"
#include "m_common_data.h"
#include "m_field_info.h"
#include "m_name_table.h"
#include "m_random_field_h.h"
#include "m_rcp.h"
#include "graph.h"
#include "sys_matrix.h"
#include <dolphin/os.h>
#include "libforest/gbi_extensions.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern u8 obj_s_myhome4_t1_tex_txt[];
extern u8 obj_s_myhome4_t2_tex_txt[];
extern u8 obj_w_myhome4_t1_tex_txt[];
extern u8 obj_w_myhome4_t2_tex_txt[];

#define PC_NLW_WALL_HIGH 5600.0f /* the wing's outer wall */
#define PC_NLW_ROOF_IN 7600.0f   /* its roof against the house, under the house's eaves (9000) */
#define PC_NLW_OVER 350.0f       /* the roof's overhang */
#define PC_NLW_ROOF_THICK 250.0f /* and how thick it is */
#define PC_NLW_GLOW_IN 60.0f     /* the light behind the windows, inside the walls */
#define PC_NLW_HEIGHT 14         /* the ground under it, raised as the house's is */

typedef struct {
    f32 x0, x1, z0, z1; /* the wing's floor, in the house model's units */
    s8 out_x, out_z;    /* the way it goes out from the house */
} pc_nlw_wing_c;

/* left, right, back (PC_NLR_LEFT...) */
static const pc_nlw_wing_c l_wing[PC_NLR_ROOMS] = {
    { -3800.0f, 0.0f, 6543.0f, 9543.0f, 0, 1 },
    { -3800.0f, 0.0f, -5543.0f, -2543.0f, 0, -1 },
    { -9000.0f, -6000.0f, 500.0f, 3500.0f, -1, 0 },
};

/* The wing for `room` on a house in column `side`. The one on the house's north side (the right room's in the left
 * column, the left room's in the right) is behind the house as the camera looks, and stops short of the front corner,
 * which would otherwise show past the house's edge */
static pc_nlw_wing_c pc_nlw_wing_of(int side, int room) {
    pc_nlw_wing_c w = l_wing[room];

    if (room == (side == 0 ? PC_NLR_RIGHT : PC_NLR_LEFT)) {
        w.x1 = -1500.0f;
    }
    return w;
}

/* The house model's origin, from its actor's position, and its axes in the world (per unit), for the left (0) and
 * right (1) column */
static const f32 l_origin[2][2] = { { 20.0f, -8.3f }, { 8.3f, 20.0f } };
static const f32 l_axis_x[2][2] = { { 0.00707f, 0.00707f }, { -0.00707f, 0.00707f } };
static const f32 l_axis_z[2][2] = { { -0.00707f, 0.00707f }, { -0.00707f, -0.00707f } };

/* --- where a wing is in the world --- */

/* House `house`'s actor position (its structure's unit, moved as aMHS_actor_ct does) */
static int pc_nlw_house_pos(int house, xyz_t* pos) {
    int bx, bz, ux, uz;

    if (!mFI_BlockKind2BkNum(&bx, &bz, mRF_BLOCKKIND_PLAYER) ||
        !mFI_SearchFGInBlock(&ux, &uz, (mActor_name_t)(HOUSE0 + house), bx, bz)) {
        return FALSE;
    }
    mFI_BkandUtNum2Wpos(pos, bx, bz, ux, uz);
    pos->x += (house & 1) == 0 ? 20.0f : -20.0f;
    pos->z += 20.0f;
    return TRUE;
}

/* The units under house `house`'s wing for `room`, the house's actor at `pos`: up to `max` of them */
static int pc_nlw_units(int house, int room, const xyz_t* pos, int* ux, int* uz, int max) {
    int side = house & 1, n = 0, i;
    pc_nlw_wing_c wing = pc_nlw_wing_of(side, room);
    const pc_nlw_wing_c* w = &wing;
    f32 x, z;

    for (x = w->x0 + 300.0f; x <= w->x1 - 300.0f + 1.0f; x += 400.0f) {
        for (z = w->z0 + 300.0f; z <= w->z1 - 300.0f + 1.0f; z += 400.0f) {
            xyz_t wp;
            int px, pz;

            wp.x = pos->x + l_origin[side][0] + x * l_axis_x[side][0] + z * l_axis_z[side][0];
            wp.y = pos->y;
            wp.z = pos->z + l_origin[side][1] + x * l_axis_x[side][1] + z * l_axis_z[side][1];
            if (!mFI_Wpos2UtNum(&px, &pz, wp)) {
                continue;
            }
            for (i = 0; i < n && (ux[i] != px || uz[i] != pz); i++) {
            }
            if (i == n && n < max) {
                ux[n] = px, uz[n] = pz, n++;
            }
        }
    }
    return n;
}

/* --- building one: clearing the ground --- */

static int pc_nlw_to_dump(mActor_name_t item) {
    mFI_unit_c dump;
    mActor_name_t* slot;
    int row, col;

    mAGrw_SearchDump(&dump);
    if (dump.block_data == NULL) {
        return FALSE;
    }
    /* the dump's 4x4 is above and left of its marker */
    slot = dump.block_data + (dump.unit_x - 4) + (dump.unit_z - 4) * UT_X_NUM;
    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            if (slot[row * UT_X_NUM + col] == EMPTY_NO) {
                slot[row * UT_X_NUM + col] = item;
                return TRUE;
            }
        }
    }
    return FALSE;
}

int pc_nlr_wing_ground(int house, int room, int clear) {
    xyz_t pos;
    int ux[32], uz[32], n, i;

    if (!pc_nlw_house_pos(house, &pos)) {
        return TRUE; /* no house to put it on: nothing in the way */
    }
    n = pc_nlw_units(house, room, &pos, ux, uz, 32);
    for (i = 0; i < n; i++) {
        mActor_name_t* item = mFI_UtNum2UtFG(ux[i], uz[i]);

        /* the house's own plot (reserved, its structure and its props) is where the wings mostly are */
        if (item == NULL || *item == EMPTY_NO || *item == RSV_NO || ITEM_NAME_GET_TYPE(*item) == NAME_TYPE_STRUCT ||
            ITEM_NAME_GET_TYPE(*item) == NAME_TYPE_PROPS) {
            continue;
        }
        if (IS_ITEM_GRASS(*item) || ITEM_IS_HOLE(*item)) {
            if (clear) {
                *item = EMPTY_NO;
            }
        } else if (ITEM_NAME_GET_TYPE(*item) == NAME_TYPE_ITEM1 || ITEM_IS_FTR(*item) || IS_ITEM_FLOWER(*item)) {
            if (clear) {
                if (!pc_nlw_to_dump(*item)) {
                    return FALSE; /* the dump's full */
                }
                OSReport("[NLRooms] house %d: %04X at unit %d,%d sent to the dump\n", house, *item, ux[i], uz[i]);
                *item = EMPTY_NO;
            }
        } else {
            OSReport("[NLRooms] house %d: %04X at unit %d,%d is in the way of room %d\n", house, *item, ux[i], uz[i],
                     room);
            return FALSE; /* a tree, a rock, something built */
        }
    }
    return TRUE;
}

/* --- the ground under the wings --- */

void pc_nlr_wing_collision(int house, const xyz_t* pos) {
    mCoBG_OffsetTable_c ofs;
    int ux[32], uz[32], n, i, r;

    if (house < 0 || house >= PLAYER_NUM) {
        return;
    }
    ofs.unit_attribute = mCoBG_ATTRIBUTE_NONE;
    ofs.centerRight_offset = ofs.leftUp_offset = ofs.leftDown_offset = ofs.rightDown_offset = ofs.rightUp_offset =
        PC_NLW_HEIGHT;
    ofs.shape = 0;
    for (r = 0; r < PC_NLR_ROOMS; r++) {
        if (pc_nlr_room_size(house, r) == 0) {
            continue;
        }
        n = pc_nlw_units(house, r, pos, ux, uz, 32);
        for (i = 0; i < n; i++) {
            mCoBG_Ut2SetPluss5PointOffset_file(ux[i], uz[i], ofs, __FILE__, __LINE__);
            if (getenv("PC_NLR_LOG") != NULL) {
                mActor_name_t* fg = mFI_UtNum2UtFG(ux[i], uz[i]);

                printf("[NLRooms] house %d: room %d's wing on unit %d,%d (%04X)\n", house, r, ux[i], uz[i], fg != NULL ? *fg : 0xFFFF);
            }
        }
    }
}

/* --- drawing them --- */

typedef struct {
    Vtx* v;
    int n;
} pc_nlw_build_c;

static void pc_nlw_vtx(pc_nlw_build_c* b, f32 x, f32 y, f32 z, f32 s, f32 t, f32 nx, f32 ny, f32 nz) {
    Vtx* v = &b->v[b->n++];

    v->n.ob[0] = (s16)x, v->n.ob[1] = (s16)y, v->n.ob[2] = (s16)z;
    v->n.flag = 0;
    v->n.tc[0] = (s16)s, v->n.tc[1] = (s16)t;
    v->n.n[0] = (s8)(nx * 120.0f), v->n.n[1] = (s8)(ny * 120.0f), v->n.n[2] = (s8)(nz * 120.0f);
    v->n.a = 255;
}

/* The house's texture state, as its own models set it; `opaque` draws the texture's see-through parts (the window
 * panes) in their colour instead */
static Gfx* pc_nlw_texture(Gfx* g, u8* tex, int w, int h, int wrap_s, int wrap_t, int opaque) {
    gDPPipeSync(g++);
    gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_ON);
    if (opaque) {
        gDPSetRenderMode(g++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2);
    } else {
        gDPSetRenderMode(g++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_TEX_EDGE2);
    }
    gDPSetCombineLERP(g++, TEXEL0, 0, SHADE, 0, 0, 0, 0, TEXEL0, PRIMITIVE, 0, COMBINED, 0, 0, 0, 0, COMBINED);
    gDPLoadTLUT_Dolphin(g++, 15, 16, 1, anime_1_txt);
    gDPSetTextureImage_Dolphin(g++, G_IM_FMT_CI, G_IM_SIZ_4b, h, w, tex); /* this one takes the height first */
    gDPSetTile_Dolphin(g++, G_DOLPHIN_TLUT_DEFAULT_MODE, 0, 15, wrap_s, wrap_t, 0, 0);
    gDPSetPrimColor(g++, 0, 128, 255, 255, 255, 255);
    gSPLoadGeometryMode(g++, G_ZBUFFER | G_SHADE | G_CULL_BACK | G_FOG | G_LIGHTING | G_SHADING_SMOOTH);
    return g;
}

/* Quads, four corners each, as triangles, loaded seven at a time (the vertex cache takes 32) */
static Gfx* pc_nlw_quads(Gfx* g, Vtx* v, int n) {
    int first, i, k;

    for (first = 0; first < n; first += 28) {
        k = n - first < 28 ? n - first : 28;
        gSPVertex(g++, &v[first], k, 0);
        for (i = 0; i < k; i += 4) {
            /* one each: the runtime gSPNTrianglesInit_5b gets the second triangle's last index wrong past 7 */
            gSPNTrianglesInit_5b(g++, 1, i, i + 1, i + 2, 0, 0, 0, 0, 0, 0);
            gSPNTrianglesInit_5b(g++, 1, i, i + 2, i + 3, 0, 0, 0, 0, 0, 0);
        }
    }
    return g;
}

/* A quad, wound to face `n` (the walls' way: counterclockwise seen from in front) */
static void pc_nlw_quad(pc_nlw_build_c* b, f32 q[4][3], f32 st[4][2], const f32 n[3]) {
    f32 e1[3], e2[3], cr[3];
    int order[4] = { 0, 1, 2, 3 }, k;

    for (k = 0; k < 3; k++) {
        e1[k] = q[1][k] - q[0][k];
        e2[k] = q[2][k] - q[0][k];
    }
    cr[0] = e1[1] * e2[2] - e1[2] * e2[1];
    cr[1] = e1[2] * e2[0] - e1[0] * e2[2];
    cr[2] = e1[0] * e2[1] - e1[1] * e2[0];
    if (cr[0] * n[0] + cr[1] * n[1] + cr[2] * n[2] < 0.0f) {
        order[1] = 3, order[3] = 1;
    }
    for (k = 0; k < 4; k++) {
        const f32* p = q[order[k]];

        pc_nlw_vtx(b, p[0], p[1], p[2], st[order[k]][0], st[order[k]][1], n[0], n[1], n[2]);
    }
}

/* A wing's walls (the three away from the house) and its roof, wound to face out */
static void pc_nlw_wing(pc_nlw_build_c* walls, pc_nlw_build_c* roof, const pc_nlw_wing_c* w) {
    /* the wing seen from the house: "out" away from it, "across" along the house's wall */
    int out_axis = w->out_x != 0 ? 0 : 2, across = out_axis == 0 ? 2 : 0;
    f32 sign = w->out_x != 0 ? w->out_x : w->out_z;
    f32 lo[3], hi[3];
    f32 in_at, out_at, a0, a1, mid, depth, drop, k, nlen;
    /* the side walls' texture: up its s (2048 to the house's 9000), along its t */
    const f32 su = 2048.0f / 9000.0f, tv = 955.0f / 3500.0f;
    int c;

    lo[0] = w->x0, hi[0] = w->x1, lo[2] = w->z0, hi[2] = w->z1;
    in_at = sign > 0 ? lo[out_axis] : hi[out_axis];
    out_at = sign > 0 ? hi[out_axis] : lo[out_axis];
    a0 = lo[across], a1 = hi[across];
    mid = (a0 + a1) * 0.5f;
    depth = (out_at - in_at) * sign;
    drop = (PC_NLW_ROOF_IN - PC_NLW_WALL_HIGH) / depth;

#define PC_NLW_P(o, a, p) ((p)[out_axis] = (o), (p)[across] = (a))
    {
        /* the outer wall, then the two ends (from the house out), each corner: across/out, height */
        f32 q[3][4][3];
        f32 nrm[3][3] = { { 0 } };
        int f, k2;

        for (c = 0; c < 4; c++) {
            f32 a = (c == 0 || c == 3) ? a0 : a1;
            f32 y = c < 2 ? 0.0f : PC_NLW_WALL_HIGH;

            PC_NLW_P(out_at, a, q[0][c]);
            q[0][c][1] = y;
        }
        nrm[0][out_axis] = sign;
        for (f = 1; f < 3; f++) {
            f32 a = f == 1 ? a0 : a1;

            for (c = 0; c < 4; c++) {
                f32 o = (c == 0 || c == 3) ? in_at : out_at;
                f32 y = c < 2 ? 0.0f : (c == 2 ? PC_NLW_WALL_HIGH : PC_NLW_ROOF_IN);

                PC_NLW_P(o, a, q[f][c]);
                q[f][c][1] = y;
            }
            nrm[f][across] = f == 1 ? -1.0f : 1.0f;
        }
        for (f = 0; f < 3; f++) {
            f32 e1[3], e2[3], cr[3];
            int order[4] = { 0, 1, 2, 3 };

            for (k2 = 0; k2 < 3; k2++) {
                e1[k2] = q[f][1][k2] - q[f][0][k2];
                e2[k2] = q[f][2][k2] - q[f][0][k2];
            }
            cr[0] = e1[1] * e2[2] - e1[2] * e2[1];
            cr[1] = e1[2] * e2[0] - e1[0] * e2[2];
            cr[2] = e1[0] * e2[1] - e1[1] * e2[0];
            if (cr[0] * nrm[f][0] + cr[1] * nrm[f][1] + cr[2] * nrm[f][2] < 0.0f) {
                order[1] = 3, order[3] = 1;
            }
            for (k2 = 0; k2 < 4; k2++) {
                const f32* p = q[f][order[k2]];
                /* all as the house's side walls are, a window in the middle of each */
                f32 t = f == 0 ? (p[across] - mid) * tv : ((p[out_axis] - in_at) * sign - depth * 0.5f) * tv;

                pc_nlw_vtx(walls, p[0], p[1], p[2], p[1] * su, t, nrm[f][0], nrm[f][1], nrm[f][2]);
            }
        }
    }
    {
        /* the roof: from the house's wall down past the outer wall, overhanging the ends; a board PC_NLW_ROOF_THICK
         * thick, its top and underside in the roof's texture as the house's roof has it (across its s, up the slope
         * its t from -1536 at the eaves), and its three free edges */
        f32 o_in = in_at, o_out = out_at + sign * PC_NLW_OVER;
        f32 y_in = PC_NLW_ROOF_IN, y_out = PC_NLW_WALL_HIGH - drop * PC_NLW_OVER;
        f32 r0 = a0 - PC_NLW_OVER, r1 = a1 + PC_NLW_OVER;
        f32 len = sqrtf(1.0f + drop * drop) * (depth + PC_NLW_OVER);
        f32 top[4][3], bot[4][3], st[4][2], n[3] = { 0 }, down[3], q[4][3], qst[4][2], en[3];
        int k2, c2;

        PC_NLW_P(o_out, r0, top[0]), top[0][1] = y_out;
        PC_NLW_P(o_out, r1, top[1]), top[1][1] = y_out;
        PC_NLW_P(o_in, r1, top[2]), top[2][1] = y_in;
        PC_NLW_P(o_in, r0, top[3]), top[3][1] = y_in;
        nlen = 1.0f / sqrtf(1.0f + drop * drop);
        n[1] = nlen;
        n[out_axis] = sign * drop * nlen;
        k = 1024.0f / 5625.0f;
        for (k2 = 0; k2 < 4; k2++) {
            for (c2 = 0; c2 < 3; c2++) {
                bot[k2][c2] = top[k2][c2] - n[c2] * PC_NLW_ROOF_THICK;
            }
            st[k2][0] = (top[k2][across] - mid) * (1024.0f / 9586.0f);
            st[k2][1] = k2 < 2 ? -1536.0f : -1536.0f - len * k;
            down[0] = -n[0], down[1] = -n[1], down[2] = -n[2];
        }
        pc_nlw_quad(roof, top, st, n);
        pc_nlw_quad(roof, bot, st, down);
        /* the edges: the eaves (corners 0-1), and the two ends (0-3, 1-2) */
        {
            static const int edge[3][2] = { { 0, 1 }, { 0, 3 }, { 1, 2 } };
            int e;

            for (e = 0; e < 3; e++) {
                int i0 = edge[e][0], i1 = edge[e][1];

                for (c2 = 0; c2 < 3; c2++) {
                    q[0][c2] = top[i0][c2], q[1][c2] = top[i1][c2];
                    q[2][c2] = bot[i1][c2], q[3][c2] = bot[i0][c2];
                    en[c2] = 0.0f;
                }
                if (e == 0) {
                    en[out_axis] = sign;
                } else {
                    en[across] = e == 1 ? -1.0f : 1.0f;
                }
                for (k2 = 0; k2 < 2; k2++) {
                    qst[k2][0] = st[edge[e][k2]][0], qst[k2][1] = st[edge[e][k2]][1];
                    qst[3 - k2][0] = qst[k2][0], qst[3 - k2][1] = qst[k2][1] - 40.0f;
                }
                pc_nlw_quad(roof, q, qst, en);
            }
        }
    }
#undef PC_NLW_P
}

void pc_nlr_draw_wings(GAME* game, int house, int winter, void* palette, const u8 light[3]) {
    GRAPH* graph = game->graph;
    pc_nlw_build_c walls, roof, glow;
    Vtx* vtx;
    Gfx* gfx;
    Gfx* g;
    Mtx* mtx;
    int r, any = FALSE;

    for (r = 0; r < PC_NLR_ROOMS; r++) {
        any |= pc_nlr_room_size(house, r) != 0;
    }
    if (!any) {
        return;
    }
    vtx = (Vtx*)GRAPH_ALLOC_TYPE(graph, Vtx, PC_NLR_ROOMS * (12 + 20 + 12));
    gfx = (Gfx*)GRAPH_ALLOC_TYPE(graph, Gfx, 128);
    mtx = _Matrix_to_Mtx_new(graph);
    if (vtx == NULL || gfx == NULL || mtx == NULL) {
        return;
    }
    walls.v = vtx, walls.n = 0;
    roof.v = vtx + PC_NLR_ROOMS * 12, roof.n = 0;
    for (r = 0; r < PC_NLR_ROOMS; r++) {
        if (pc_nlr_room_size(house, r) != 0) {
            pc_nlw_wing_c w = pc_nlw_wing_of(house & 1, r);

            pc_nlw_wing(&walls, &roof, &w);
        }
    }
    /* behind each wall, just inside, what's seen through its window panes: lit as the house's windows are (its
     * light model, PC_NLW_GLOW_IN in), dark by day or with the lights off */
    glow.v = roof.v + PC_NLR_ROOMS * 20, glow.n = walls.n;
    for (r = 0; r < walls.n; r++) {
        int k;

        glow.v[r] = walls.v[r];
        for (k = 0; k < 3; k++) {
            glow.v[r].n.ob[k] -= (s16)(walls.v[r].n.n[k] * (PC_NLW_GLOW_IN / 120.0f));
        }
    }

    g = gfx;
    gSPSegment(g++, G_MWO_SEGMENT_8, palette); /* the house's colours, as its draw set them before its joints */
    gDPPipeSync(g++);
    gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombineLERP(g++, 0, 0, 0, PRIMITIVE, 0, 0, 0, 1, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED);
    gDPSetRenderMode(g++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2);
    gDPSetPrimColor(g++, 0, 0, light[0], light[1], light[2], 255);
    gSPLoadGeometryMode(g++, G_ZBUFFER | G_CULL_BACK | G_FOG);
    g = pc_nlw_quads(g, glow.v, glow.n);
    g = pc_nlw_texture(g, winter ? obj_w_myhome4_t2_tex_txt : obj_s_myhome4_t2_tex_txt, 128, 32, GX_CLAMP, GX_MIRROR,
                       FALSE);
    g = pc_nlw_quads(g, walls.v, walls.n);
    g = pc_nlw_texture(g, winter ? obj_w_myhome4_t1_tex_txt : obj_s_myhome4_t1_tex_txt, 64, 64, GX_MIRROR, GX_MIRROR,
                       FALSE);
    g = pc_nlw_quads(g, roof.v, roof.n);
    gSPEndDisplayList(g++);

    OPEN_DISP(graph);
    gSPMatrix(NEXT_POLY_OPA_DISP, mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(NEXT_POLY_OPA_DISP, gfx);
    CLOSE_DISP(graph);
}
