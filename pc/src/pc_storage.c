/* pc_storage.c - City Folk style item storage behind the house's dressers.
 *
 * Opening a dresser, wardrobe or closet in your own house shows the gyroid's
 * screen in "storage mode": the gyroid's 4 slots become a 5 x 3 page of
 * storage, L/R turn through its 6 pages, and items move between it and the
 * pockets the way they do with a gyroid, minus the prices. A picks an item
 * straight up.
 *
 * The haniwa code reaches its slots through pc_storage_slot(), which hands out
 * the shown page as Haniwa_Item_c while storage is open. That page is copied
 * back into the companion save (pc_ext_save.c) every frame and on page turns.
 *
 * Whatever the GameCube kept inside a piece of furniture (2-3 items) moves into
 * storage the first time its owner opens it. */
#include "pc_storage.h"
#include "pc_ext_save.h"

#include "m_common_data.h"
#include "m_play.h"
#include "m_event.h"
#include "m_player_lib.h"
#include "m_submenu.h"
#include "m_submenu_ovl.h"
#include "m_tag_ovl.h"
#include "m_name_table.h"
#include "m_font.h"
#include "m_rcp.h"
#include "sys_matrix.h"
#include "graph.h"
#include "audio.h"

#include <stdio.h>

#define PAGE_SIZE PC_EXT_STORAGE_PAGE_SIZE
#define PAGES PC_EXT_STORAGE_PAGES

static int l_active = FALSE;
static int l_page = 0;
static PCExtStoredItem* l_store = NULL;
static Haniwa_Item_c l_page_items[PAGE_SIZE];
static int l_requested = FALSE;

static PCExtStoredItem* resident_storage(void) {
    int player_no = Common_Get(player_no);

    if (player_no < 0 || player_no >= PLAYER_NUM || Now_Private == NULL) {
        return NULL;
    }
    return pc_ext_storage(player_no, &Now_Private->player_ID);
}

static void load_page(void) {
    int i;

    for (i = 0; i < PAGE_SIZE; i++) {
        l_page_items[i].item = l_store[l_page * PAGE_SIZE + i].item;
        l_page_items[i].exchange_type = 0;
        l_page_items[i].extra_data = 0;
    }
}

void pc_storage_sync(void) {
    int i;

    if (!l_active || l_store == NULL) {
        return;
    }
    for (i = 0; i < PAGE_SIZE; i++) {
        l_store[l_page * PAGE_SIZE + i].item = l_page_items[i].item;
        l_store[l_page * PAGE_SIZE + i].cond = mPr_ITEM_COND_NORMAL;
    }
}

int pc_storage_begin(void) {
    l_store = resident_storage();
    if (l_store == NULL) {
        return FALSE;
    }
    l_active = TRUE;
    l_page = 0;
    load_page();
    return TRUE;
}

void pc_storage_take_furniture_items(mActor_name_t* items, int count) {
    PCExtStoredItem* store = resident_storage();
    int i;
    int slot = 0;

    if (store == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        if (items[i] == EMPTY_NO) {
            continue;
        }
        while (slot < PC_EXT_STORAGE_COUNT && store[slot].item != EMPTY_NO) {
            slot++;
        }
        if (slot >= PC_EXT_STORAGE_COUNT) {
            break; /* full: it stays in the furniture until there is room */
        }
        store[slot].item = items[i];
        store[slot].cond = mPr_ITEM_COND_NORMAL;
        items[i] = EMPTY_NO;
    }
    if (l_active) {
        load_page();
    }
}

int pc_storage_active(void) {
    return l_active;
}

void pc_storage_end(void) {
    pc_storage_sync();
    l_active = FALSE;
    l_store = NULL;
}

Haniwa_Item_c* pc_storage_slot(int home, int idx) {
    if (l_active) {
        return &l_page_items[(u32)idx < PAGE_SIZE ? idx : 0];
    }
    return &Save_Get(homes[home]).haniwa.items[idx];
}

int pc_storage_slot_count(void) {
    return l_active ? PAGE_SIZE : HANIWA_ITEM_HOLD_NUM;
}

void pc_storage_turn_page(int dir) {
    if (!l_active) {
        return;
    }
    pc_storage_sync();
    l_page = (l_page + dir + PAGES) % PAGES;
    load_page();
}

int pc_storage_accepts(mActor_name_t item) {
    /* Like a gyroid, but Bell bags are fine. */
    return item != EMPTY_NO && !(item >= ITM_EXCERCISE_CARD00 && item <= ITM_EXCERCISE_CARD12) &&
           item != ITM_KNIFE_AND_FORK && !(item >= ITM_SPIRIT0 && item <= ITM_SPIRIT4);
}

/* --- drawing --- */

/* Slot centres, matching m_tag_ovl.c's storage table (the pockets' columns). */
static const s16 l_col_pos[] = { -77, -53, -29, -5, 19 };
static const s16 l_row_pos[] = { 84, 60, 36 };
/* Item icons are drawn from their slot position this far to the lower right. */
#define ICON_OFS_X 0.0f
#define ICON_OFS_Y 0.0f
#define SLOT_HALF 11.0f

static Gfx l_panel_mode[] = {
    gsDPPipeSync(),
    gsSPTexture(0, 0, 0, G_TX_RENDERTILE, G_OFF),
    gsSPLoadGeometryMode(G_SHADE | G_SHADING_SMOOTH),
    gsDPSetOtherMode(G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                         G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                     G_AC_NONE | G_ZS_PIXEL | G_RM_XLU_SURF | G_RM_XLU_SURF2),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
    gsSPEndDisplayList(),
};

static void set_v(Vtx* v, f32 x, f32 y, const u8* rgba) {
    v->v.ob[0] = (s16)x;
    v->v.ob[1] = (s16)y;
    v->v.ob[2] = 0;
    v->v.flag = 0;
    v->v.tc[0] = 0;
    v->v.tc[1] = 0;
    v->v.cn[0] = rgba[0];
    v->v.cn[1] = rgba[1];
    v->v.cn[2] = rgba[2];
    v->v.cn[3] = rgba[3];
}

static void quad(GRAPH* graph, Gfx** gfx_p, f32 x0, f32 y0, f32 x1, f32 y1, const u8* top, const u8* bottom) {
    Vtx* v = (Vtx*)GRAPH_ALLOC_TYPE(graph, Vtx, 4);
    Gfx* gfx = *gfx_p;

    if (v == NULL) {
        return;
    }
    set_v(&v[0], x0, y1, top);
    set_v(&v[1], x1, y1, top);
    set_v(&v[2], x1, y0, bottom);
    set_v(&v[3], x0, y0, bottom);
    gSPVertex(gfx++, v, 4, 0);
    gSP2Triangles(gfx++, 0, 1, 2, 0, 0, 2, 3, 0);
    *gfx_p = gfx;
}

static int ascii_to_msg(u8* out, int size, const char* s) {
    int n = 0;

    while (*s != '\0' && n < size) {
        out[n++] = (u8)*s++;
    }
    return n;
}

void pc_storage_draw(Submenu* submenu, GAME* game, f32 pos_x, f32 pos_y) {
    static const u8 border[] = { 140, 95, 55, 255 };
    static const u8 fill_top[] = { 255, 247, 214, 255 };
    static const u8 fill_bottom[] = { 246, 228, 180, 255 };
    static const u8 slot_top[] = { 226, 204, 156, 255 };
    static const u8 slot_bottom[] = { 236, 218, 174, 255 };
    static const u8 dot_off[] = { 205, 180, 130, 255 };
    static const u8 dot_on[] = { 120, 170, 90, 255 };
    GRAPH* graph = game->graph;
    f32 left = l_col_pos[0] - 17.0f;
    f32 right = l_col_pos[4] + 17.0f;
    f32 bottom = l_row_pos[2] - 14.0f;
    f32 top = l_row_pos[0] + 27.0f;
    Gfx* gfx;
    u8 str[24];
    char label[24];
    int len;
    int i;

    Matrix_scale(16.0f, 16.0f, 1.0f, MTX_LOAD);
    Matrix_translate(pos_x, pos_y, -809.0f, MTX_MULT);

    OPEN_POLY_OPA_DISP(graph);
    gfx = POLY_OPA_DISP;

    gSPDisplayList(gfx++, l_panel_mode);
    gSPMatrix(gfx++, _Matrix_to_Mtx_new(graph), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    quad(graph, &gfx, left - 2.0f, bottom - 2.0f, right + 2.0f, top + 2.0f, border, border);
    quad(graph, &gfx, left, bottom, right, top, fill_top, fill_bottom);
    for (i = 0; i < PAGE_SIZE; i++) {
        f32 cx = l_col_pos[i % 5] + ICON_OFS_X;
        f32 cy = l_row_pos[i / 5] + ICON_OFS_Y;

        quad(graph, &gfx, cx - SLOT_HALF, cy - SLOT_HALF, cx + SLOT_HALF, cy + SLOT_HALF, slot_top, slot_bottom);
    }
    /* Page dots, top right. */
    for (i = 0; i < PAGES; i++) {
        f32 x = right - 6.0f - (PAGES - 1 - i) * 8.0f;
        f32 y = top - 9.0f;
        const u8* c = (i == l_page) ? dot_on : dot_off;

        quad(graph, &gfx, x - 2.5f, y - 2.5f, x + 2.5f, y + 2.5f, c, c);
    }

    POLY_OPA_DISP = gfx;
    CLOSE_POLY_OPA_DISP(graph);

    /* Items, as the gyroid draws its own. */
    {
        extern Gfx inv_item_mode[];

        OPEN_POLY_OPA_DISP(graph);
        gSPDisplayList(POLY_OPA_DISP++, inv_item_mode);
        CLOSE_POLY_OPA_DISP(graph);
    }
    for (i = 0; i < PAGE_SIZE; i++) {
        if (l_page_items[i].item != EMPTY_NO) {
            f32 pos[2];

            submenu->overlay->tag_ovl->set_hand_pos_proc(submenu, pos, mTG_TABLE_HANIWA, i);
            submenu->overlay->draw_item_proc(graph, pos_x + pos[0], pos_y + pos[1], 1.0f, l_page_items[i].item, FALSE,
                                             TRUE, 0, FALSE, FALSE);
        }
    }

    /* No slash: the game font has a note there. */
    sprintf(label, "Page %d of %d", l_page + 1, PAGES);
    len = ascii_to_msg(str, sizeof(str), label);
    submenu->overlay->set_char_matrix_proc(graph);
    mFont_SetLineStrings(game, str, len, 160.0f + pos_x + left + 6.0f, 120.0f - (pos_y + top - 3.0f), 90, 60, 30, 255,
                         FALSE, TRUE, 0.875f, 0.875f, mFont_MODE_POLY);
}

/* --- dev test harness --- */

void pc_item_storage_request(void) {
    l_requested = TRUE;
}

void pc_item_storage_tick(GAME_PLAY* play) {
    if (!l_requested) {
        return;
    }
    if (mEv_CheckTitleDemo() != mEv_TITLEDEMO_NONE) {
        l_requested = FALSE;
        return;
    }
    if (play->submenu.process_status != mSM_PROCESS_WAIT || play->submenu.open_flag ||
        play->fb_fade_type != FADE_TYPE_NONE || play->fb_wipe_mode != WIPE_MODE_NONE ||
        !mPlib_able_submenu_type1((GAME*)play)) {
        return;
    }
    l_requested = FALSE;
    if (pc_storage_begin()) {
        mSM_open_submenu(&play->submenu, mSM_OVL_INVENTORY, mSM_IV_OPEN_HANIWA_ENTRUST, 0);
    }
}
