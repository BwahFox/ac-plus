/* pc_ground_pattern.c - lay your patterns on the ground, Wild World style.
 *
 * A pattern's Use menu outdoors in town gets "Lay on Ground": the pattern goes
 * on the tile the player stands on. Doing it again with the same pattern on
 * that tile picks it back up; a different pattern replaces it. The tiles live
 * in the ac-plus companion save (pc_ext_save.c) as pattern IDs (pc_pattern.c),
 * so an edited pattern shows its new design on the ground too, and a pattern
 * moved to storage or replaced in its slot stays on the ground.
 *
 * Drawn by the town's field-draw actor right after the terrain, into BG_OPA,
 * so they are lit and fogged like the ground and follow the curved world. */
#include "m_common_data.h"
#include "m_play.h"
#include "m_player.h"
#include "m_player_lib.h"
#include "m_field_info.h"
#include "m_field_make.h"
#include "m_collision_bg.h"
#include "m_needlework.h"
#include "m_name_table.h"
#include "m_rcp.h"
#include "m_event.h"
#include "m_lib.h"
#include "sys_matrix.h"
#include "graph.h"
#include "pc_ext_save.h"
#include "pc_text_draw.h"
#include "pc_pattern.h"
#include "m_font.h"

#include <stdio.h>

/* Frames a request may wait, after the menus close, for the player to be free. */
#define REQUEST_TIMEOUT 180
/* Lift above the terrain, against z-fighting where the drawn ground bulges. */
#define GROUND_LIFT 2.0f
/* Each tile is a GRID x GRID mesh following the ground height. */
#define GRID 4
/* Corner heights are sampled this far inside the tile, so a cliff next to it
 * doesn't pull a corner up. */
#define CORNER_INSET 2.0f

void pc_quick_save_show_message(const char* msg);

static int l_request_image = -1;
static int l_request_wait = 0;

/* F4 tile readout: the play scene seen by this frame's tick (NULL outside play). */
static int l_debug_on = FALSE;
static GAME_PLAY* l_debug_play = NULL;

static int in_town(void) {
    /* The title screen's background demo also runs in a town. */
    return Common_Get(field_type) == mFI_FIELDTYPE2_FG && mFI_GetFieldId() == mFI_FIELD_FG &&
           mEv_CheckTitleDemo() == mEv_TITLEDEMO_NONE;
}

/* m_tag_ovl.c: "Lay on Ground" picked for my_org[image]. */
void pc_ground_pattern_request(int image) {
    l_request_image = image & 7;
    l_request_wait = 0;
}

/* Town acre (0-based FG block) and unit under a world position. */
static int town_unit(xyz_t pos, int* bx, int* bz, int* ux, int* uz) {
    if (!mFI_Wpos2BkandUtNuminBlock(bx, bz, ux, uz, pos)) {
        return FALSE;
    }
    *bx -= 1; /* world blocks have a border ring around the town's 5 x 6 */
    *bz -= 1;
    return *bx >= 0 && *bx < PC_EXT_GROUND_BLOCK_X && *bz >= 0 && *bz < PC_EXT_GROUND_BLOCK_Z;
}

static int tile_takes_pattern(xyz_t center) {
    mActor_name_t* fg = mFI_GetUnitFG(center);

    /* Not the signboard rule: that also keeps house fronts and the paths
     * between acres clear, which only matters for something solid. */
    if (!mCoBG_CheckPlace(center) || mCoBG_CheckWave(&center) || mCoBG_CheckGrassX(&center)) {
        return FALSE; /* water, waves, cliff and river edges */
    }
    /* Bare ground or a dropped item; not trees, flowers, signs, holes. */
    return fg == NULL || *fg == EMPTY_NO || ITEM_NAME_GET_TYPE(*fg) == NAME_TYPE_ITEM1;
}

static void lay_pattern(GAME_PLAY* play, int image) {
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);
    int player_no = Common_Get(player_no);
    int bx, bz, ux, uz;
    xyz_t center;
    u32 value;
    u32 existing;

    if (player == NULL || player_no >= PLAYER_NUM || !in_town() ||
        !town_unit(player->actor_class.world.position, &bx, &bz, &ux, &uz)) {
        pc_quick_save_show_message("Patterns can only be laid on the ground in town.");
        return;
    }

    mFI_BkandUtNum2CenterWpos(&center, bx + 1, bz + 1, ux, uz);
    value = pc_pattern_slot_id(player_no, image);
    if (value == 0) {
        pc_quick_save_show_message("Too many patterns in use.");
        return;
    }
    existing = pc_ext_ground_get(bx, bz, ux, uz);

    if (existing == value) {
        pc_ext_ground_set(bx, bz, ux, uz, 0);
        pc_quick_save_show_message("Picked up the pattern.");
    } else if (!tile_takes_pattern(center)) {
        pc_quick_save_show_message("A pattern can't go here.");
    } else {
        pc_ext_ground_set(bx, bz, ux, uz, value);
        pc_quick_save_show_message(existing != 0 ? "Replaced the pattern on the ground." : "Laid the pattern on the ground.");
    }
}

/* Dev test harness: write which units of the player's acre take a pattern, and why not. */
void pc_ground_pattern_probe(void* game, const char* path) {
    GAME_PLAY* play = (GAME_PLAY*)game;
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);
    int bx, bz, ux, uz, pux, puz;
    FILE* f;

    if (player == NULL || !town_unit(player->actor_class.world.position, &bx, &bz, &pux, &puz)) {
        return;
    }
    f = fopen(path, "w");
    if (f == NULL) {
        return;
    }
    fprintf(f, "acre %d,%d player unit %d,%d  (. ok  s edge or water  p place  f fg  * player)\n", bx, bz, pux, puz);
    for (uz = 0; uz < PC_EXT_UNITS; uz++) {
        for (ux = 0; ux < PC_EXT_UNITS; ux++) {
            xyz_t c;
            mActor_name_t* fg;
            char ch = '.';

            mFI_BkandUtNum2CenterWpos(&c, bx + 1, bz + 1, ux, uz);
            fg = mFI_GetUnitFG(c);
            if (mCoBG_CheckWave(&c) || mCoBG_CheckGrassX(&c)) {
                ch = 's';
            } else if (!mCoBG_CheckPlace(c)) {
                ch = 'p';
            } else if (!(fg == NULL || *fg == EMPTY_NO || ITEM_NAME_GET_TYPE(*fg) == NAME_TYPE_ITEM1)) {
                ch = 'f';
            }
            if (ux == pux && uz == puz) {
                ch = '*';
            }
            fputc(ch, f);
        }
        fprintf(f, "   attr");
        for (ux = 0; ux < PC_EXT_UNITS; ux++) {
            xyz_t c;

            mFI_BkandUtNum2CenterWpos(&c, bx + 1, bz + 1, ux, uz);
            fprintf(f, " %2d", (int)mFI_GetUnitCol(c)->data.unit_attribute);
        }
        fputc('\n', f);
    }
    fprintf(f, "ground pattern id under player: %u\n", pc_ext_ground_get(bx, bz, pux, puz));
    fclose(f);
}

/* m_play.c, every frame before the submenu system runs. */
void pc_ground_pattern_tick(GAME_PLAY* play) {
    int index;

    l_debug_play = play;

    if (l_request_image < 0) {
        return;
    }
    if (mEv_CheckTitleDemo() != mEv_TITLEDEMO_NONE) {
        l_request_image = -1;
        return;
    }
    /* Wait for the menus to close... */
    if (play->submenu.open_flag || play->submenu.process_status != mSM_PROCESS_WAIT) {
        return;
    }
    /* ...then for the player to stand free (not forever: a scene change or event drops it). */
    if (++l_request_wait > REQUEST_TIMEOUT) {
        l_request_image = -1;
        return;
    }
    if (play->fb_wipe_mode != WIPE_MODE_NONE || play->fb_fade_type != FADE_TYPE_NONE) {
        return;
    }
    index = mPlib_get_player_actor_main_index((GAME*)play);
    if (index != mPlayer_INDEX_WAIT && index != mPlayer_INDEX_WALK && index != mPlayer_INDEX_RUN &&
        index != mPlayer_INDEX_DASH) {
        return;
    }

    lay_pattern(play, l_request_image);
    l_request_image = -1;
}

/* --- drawing --- */

static Gfx l_ground_pattern_setup[] = {
    gsDPPipeSync(),
    gsSPTexture(0, 0, 0, G_TX_RENDERTILE, G_ON),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_TEX_EDGE2),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0, 0, 0, 0, TEXEL0, PRIMITIVE, 0, COMBINED, 0, 0, 0, 0, COMBINED),
    gsDPSetPrimColor(0, 255, 255, 255, 255, 255),
    /* No back-face culling: the quads are only ever seen from above anyway. */
    gsSPLoadGeometryMode(G_ZBUFFER | G_SHADE | G_FOG | G_LIGHTING | G_SHADING_SMOOTH | G_DECAL_LEQUAL),
    gsSPEndDisplayList(),
};

static f32 ground_y(f32 x, f32 z) {
    s_xyz angle;
    xyz_t pos;

    pos.x = x;
    pos.y = 0.0f;
    pos.z = z;
    return mCoBG_GetBgY_AngleS_FromWpos(&angle, pos, 0.0f) + GROUND_LIFT;
}

static void set_vtx(Vtx* v, f32 x, f32 y, f32 z, int s, int t) {
    v->n.ob[0] = (s16)x;
    v->n.ob[1] = (s16)y;
    v->n.ob[2] = (s16)z;
    v->n.flag = 0;
    v->n.tc[0] = (s16)(s << 5); /* 10.5 fixed point texels */
    v->n.tc[1] = (s16)(t << 5);
    v->n.n[0] = 0;
    v->n.n[1] = 127; /* facing up, lit like the ground */
    v->n.n[2] = 0;
    v->n.a = 255;
}

static void draw_tile(GRAPH* graph, Gfx** gfx_p, int bx, int bz, int ux, int uz,
                      const mNW_original_design_c* design) {
    Vtx* vtx = (Vtx*)GRAPH_ALLOC_TYPE(graph, Vtx, (GRID + 1) * (GRID + 1));
    Gfx* gfx = *gfx_p;
    xyz_t c;
    f32 size = mFI_UT_WORLDSIZE_X_F;
    f32 x0, z0;
    int i, j;

    if (vtx == NULL) {
        return;
    }
    mFI_BkandUtNum2CenterWpos(&c, bx + 1, bz + 1, ux, uz);
    x0 = c.x - size * 0.5f;
    z0 = c.z - size * 0.5f; /* north edge: the pattern's top */

    for (j = 0; j <= GRID; j++) {
        for (i = 0; i <= GRID; i++) {
            f32 x = x0 + size * i / GRID;
            f32 z = z0 + size * j / GRID;
            /* Heights at the tile's edges are read just inside it, so a cliff next door doesn't pull them up. */
            f32 sx = (i == 0) ? x + CORNER_INSET : (i == GRID) ? x - CORNER_INSET : x;
            f32 sz = (j == 0) ? z + CORNER_INSET : (j == GRID) ? z - CORNER_INSET : z;

            set_vtx(&vtx[j * (GRID + 1) + i], x, ground_y(sx, sz), z, 32 * i / GRID, 32 * j / GRID);
        }
    }

    gDPLoadTLUT_Dolphin(gfx++, 15, 16, 1, mNW_PaletteIdx2Palette(design->palette));
    gDPSetTextureImage_Dolphin(gfx++, G_IM_FMT_CI, G_IM_SIZ_4b, 32, 32, design->design.data);
    gDPSetTile_Dolphin(gfx++, G_DOLPHIN_TLUT_DEFAULT_MODE, 0, 15, GX_CLAMP, GX_CLAMP, 0, 0);
    gSPVertex(gfx++, vtx, (GRID + 1) * (GRID + 1), 0);
    for (j = 0; j < GRID; j++) {
        for (i = 0; i < GRID; i++) {
            int v = j * (GRID + 1) + i;

            gSP2Triangles(gfx++, v, v + 1, v + GRID + 2, 0, v, v + GRID + 2, v + GRID + 1, 0);
        }
    }
    *gfx_p = gfx;
}

/* ac_field_draw.c, after the terrain blocks (texture adjust mode still Dolphin). */
void pc_ground_pattern_draw(GAME* game) {
    GAME_PLAY* play = (GAME_PLAY*)game;
    PLAYER_ACTOR* player;
    GRAPH* graph = game->graph;
    int pbx, pbz, pux, puz;
    int bx, bz;
    int started = FALSE;

    if (!in_town()) {
        return;
    }
    player = GET_PLAYER_ACTOR(play);
    if (player == NULL || !town_unit(player->actor_class.world.position, &pbx, &pbz, &pux, &puz)) {
        return;
    }

    OPEN_DISP(graph);
    /* Only the acres around the player can be on screen. */
    for (bz = pbz - 1; bz <= pbz + 1; bz++) {
        for (bx = pbx - 1; bx <= pbx + 1; bx++) {
            int ux;
            int uz;

            if (!pc_ext_ground_block_used(bx, bz)) {
                continue;
            }
            if (!started) {
                Matrix_push();
                Matrix_translate(0.0f, 0.0f, 0.0f, MTX_LOAD);
                gSPMatrix(NEXT_BG_OPA_DISP, _Matrix_to_Mtx_new(graph), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
                Matrix_pull();
                gSPDisplayList(NEXT_BG_OPA_DISP, l_ground_pattern_setup);
                started = TRUE;
            }
            for (uz = 0; uz < PC_EXT_UNITS; uz++) {
                for (ux = 0; ux < PC_EXT_UNITS; ux++) {
                    const mNW_original_design_c* design = pc_pattern_get(pc_ext_ground_get(bx, bz, ux, uz));

                    if (design != NULL) {
                        Gfx* gfx = NOW_BG_OPA_DISP;
                        draw_tile(graph, &gfx, bx, bz, ux, uz, design);
                        NOW_BG_OPA_DISP = gfx;
                    }
                }
            }
        }
    }
    CLOSE_DISP(graph);
}

/* --- F4: what the game thinks of the tile under the player --- */

void pc_ground_debug_toggle(void) {
    l_debug_on = !l_debug_on;
}

static const char* attr_name(u32 attr) {
    static const char* names[] = {
        "grass", "grass", "grass", "grass", "dirt", "dirt", "dirt", "stone", "floor", "bush", "hole", "waves",
        "water", "waterfall", "river", "river", "river", "river", "river", "river", "river", "river", "sand",
        "wood", "sea",
    };

    if (attr < ARRAY_COUNT(names)) {
        return names[attr];
    }
    if (attr >= 27 && attr <= 31) {
        return "wood bridge";
    }
    if (attr >= 32 && attr <= 35) {
        return "stone bridge";
    }
    if (attr >= 36 && attr <= 38) {
        return "waves";
    }
    if (attr >= 39 && attr <= 42 || attr >= 59 && attr <= 62) {
        return "river bank";
    }
    if (attr >= 43 && attr <= 46) {
        return "grass by river";
    }
    if (attr >= 47 && attr <= 58) {
        return "cliff edge";
    }
    if (attr == 63) {
        return "slope";
    }
    return "?";
}

/* graph.c, after the frame's game logic, next to the save messages. */
void pc_ground_debug_draw(GAME* game) {
    GAME_PLAY* play = l_debug_play;
    PLAYER_ACTOR* player;
    int bx, bz, ux, uz;
    xyz_t c;
    mActor_name_t* fg;
    u32 attr;
    u32 value;
    char line[96];
    f32 y = 40.0f; /* below the save messages */

    l_debug_play = NULL; /* set again by next frame's tick, if still in play */
    if (!l_debug_on || play == NULL || (GAME*)play != game || game->graph == NULL) {
        return;
    }
    player = GET_PLAYER_ACTOR(play);
    if (player == NULL) {
        return;
    }

    mFont_SetMatrix(game->graph, mFont_MODE_FONT);
#define DBG_LINE(...)                                                          \
    do {                                                                       \
        snprintf(line, sizeof(line), __VA_ARGS__);                             \
        pc_text_draw(game, line, 8.0f, y, 255, 255, 160, 255, 0.8f);           \
        y += 12.0f;                                                            \
    } while (0)

    if (!in_town()) {
        DBG_LINE("Not in town (ground patterns are town only)");
    } else if (!town_unit(player->actor_class.world.position, &bx, &bz, &ux, &uz)) {
        DBG_LINE("Outside the town's acres");
    } else {
        mFI_BkandUtNum2CenterWpos(&c, bx + 1, bz + 1, ux, uz);
        fg = mFI_GetUnitFG(c);
        attr = mFI_GetUnitCol(c)->data.unit_attribute;
        value = pc_ext_ground_get(bx, bz, ux, uz);

        DBG_LINE("Acre %c-%d   tile x %d  z %d   (acre %d,%d)", 'A' + bz, bx + 1, ux, uz, bx, bz);
        DBG_LINE("Ground: %s (attribute %u)", attr_name(attr), (unsigned)attr);
        DBG_LINE("On tile: %04X%s", fg != NULL ? *fg : 0, (fg == NULL || *fg == EMPTY_NO) ? " (nothing)" : "");
        DBG_LINE("Drop spot: %s   Edge or water: %s   Tile free: %s", mCoBG_CheckPlace(c) ? "yes" : "NO",
                 (mCoBG_CheckWave(&c) || mCoBG_CheckGrassX(&c)) ? "YES" : "no",
                 (fg == NULL || *fg == EMPTY_NO || ITEM_NAME_GET_TYPE(*fg) == NAME_TYPE_ITEM1) ? "yes" : "NO");
        DBG_LINE("Takes a pattern: %s", tile_takes_pattern(c) ? "yes" : "NO");
        if (value != 0) {
            const mNW_original_design_c* d = pc_pattern_get(value);

            DBG_LINE("Pattern here: id %u%s", (unsigned)value, d != NULL ? "" : " (missing)");
        } else {
            DBG_LINE("Pattern here: none");
        }
        DBG_LINE("Position x %.1f  z %.1f", player->actor_class.world.position.x,
                 player->actor_class.world.position.z);
    }
#undef DBG_LINE
    mFont_UnSetMatrix(game->graph, mFont_MODE_FONT);
}
