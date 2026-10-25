/* pc_skin.c - skin tones, as Animal Crossing Deluxe has them.
 *
 * The player's skin is entries 1-5 of the face palette (the arms, legs and the pocket portrait use the same
 * palette). Eight tones replace those five colours, light to dark, tone 1 being the GameCube's own; tanning,
 * when the setting has it on, darkens and reddens whichever tone the player has, the further the higher the
 * sunburn rank. Rover asks for it on the train, and checking a mirror at home changes it.
 *
 * The tones are kept per town player as SKIN in the ac-plus save, the GameCube save having no room for them. */
#include "pc_skin.h"
#include "pc_settings.h"
#include "pc_menu_util.h"
#include "pc_platform.h"
#include "pc_text_draw.h"
#include "m_common_data.h"
#include "m_msg.h"
#include "m_controller.h"
#include "m_rcp.h"
#include "game.h"
#include "graph.h"

#include <stdio.h>
#include <string.h>

/* Face palette entries 1-5 per tone, RGB, darkest first (Deluxe's values) */
static const u8 l_tone_rgb[PC_SKIN_TONE_NUM][5][3] = {
    { { 90, 57, 33 }, { 165, 107, 57 }, { 232, 176, 137 }, { 255, 201, 168 }, { 255, 221, 196 } },
    { { 90, 57, 33 }, { 165, 107, 57 }, { 222, 156, 107 }, { 255, 189, 148 }, { 255, 214, 165 } },
    { { 90, 57, 33 }, { 165, 107, 57 }, { 212, 146, 102 }, { 255, 169, 138 }, { 255, 203, 169 } },
    { { 90, 57, 33 }, { 165, 107, 57 }, { 202, 136, 97 }, { 225, 160, 127 }, { 255, 193, 136 } },
    { { 90, 57, 33 }, { 165, 107, 57 }, { 187, 126, 87 }, { 215, 152, 122 }, { 255, 176, 123 } },
    { { 90, 57, 33 }, { 165, 107, 57 }, { 175, 115, 80 }, { 200, 127, 86 }, { 221, 143, 95 } },
    { { 90, 57, 33 }, { 165, 107, 57 }, { 170, 110, 70 }, { 180, 115, 80 }, { 195, 130, 90 } },
    { { 90, 57, 33 }, { 120, 80, 40 }, { 126, 90, 60 }, { 140, 100, 79 }, { 173, 116, 89 } },
};

static u8 l_tone[PLAYER_NUM] = { PC_SKIN_TONE_DEFAULT, PC_SKIN_TONE_DEFAULT, PC_SKIN_TONE_DEFAULT,
                                  PC_SKIN_TONE_DEFAULT };

int pc_skin_now_player(void) {
    int i;

    for (i = 0; i < PLAYER_NUM; i++) {
        if (Common_Get(now_private) == Save_GetPointer(private_data[i])) {
            return i;
        }
    }
    return -1;
}

int pc_skin_tone_of(int player) {
    return player >= 0 && player < PLAYER_NUM && l_tone[player] < PC_SKIN_TONE_NUM ? l_tone[player] : PC_SKIN_TONE_DEFAULT;
}

int pc_skin_tone(void) {
    return pc_skin_tone_of(pc_skin_now_player());
}

void pc_skin_set_tone(int player, int tone) {
    if (player >= 0 && player < PLAYER_NUM && tone >= 0 && tone < PC_SKIN_TONE_NUM) {
        l_tone[player] = (u8)tone;
    }
}

int pc_skin_sunburn_rank(void) {
    return g_pc_settings.tanning && Common_Get(now_private) != NULL ? Common_Get(now_private)->sunburn.rank : 0;
}

static u8 l_channel(int c, f32 m) {
    f32 v = (f32)c * m;

    return v <= 0.0f ? 0 : v >= 255.0f ? 255 : (u8)v;
}

static u16 l_vanilla[16];
static u16 l_applied[16]; /* l_vanilla with the tone: what the face's palette holds wherever it's copied to */

void pc_skin_apply(u16* pal, int tone, int rank) {
    f32 t = (rank > 0 ? (f32)rank : 0.0f) / 7.0f;
    f32 d = 1.0f - 0.3f * t;
    f32 mr = (1.0f + 0.2f * t) * d, mg = (1.0f - 0.1f * t) * d, mb = (1.0f - 0.15f * t) * d;
    int i;

    if (tone < 0 || tone >= PC_SKIN_TONE_NUM) {
        tone = PC_SKIN_TONE_DEFAULT;
    }
    for (i = 0; i < 5; i++) {
        const u8* c = l_tone_rgb[tone][i];

        pal[1 + i] = (u16)(0x8000 | ((l_channel(c[0], mr) >> 3) << 10) | ((l_channel(c[1], mg) >> 3) << 5) |
                           (l_channel(c[2], mb) >> 3));
    }
    memcpy(l_applied, pal, sizeof(l_applied));
}


void pc_skin_keep_vanilla(const u16* pal) {
    memcpy(l_vanilla, pal, sizeof(l_vanilla));
}

/* Recognised by its colours, not where it is: the blinking eyes and the face may load it from different copies */
const u16* pc_skin_vanilla_tlut(const void* tlut) {
    if (tlut == NULL || memcmp(tlut, l_applied, sizeof(l_applied)) != 0 ||
        memcmp(l_applied, l_vanilla, sizeof(l_vanilla)) == 0) {
        return NULL;
    }
    return l_vanilla;
}

static void pc_skin_rgb(u16 c, int* rgb) {
    if (c & 0x8000) { /* RGB555 */
        rgb[0] = ((c >> 10) & 31) * 255 / 31, rgb[1] = ((c >> 5) & 31) * 255 / 31, rgb[2] = (c & 31) * 255 / 31;
    } else { /* A3RGB4 */
        rgb[0] = ((c >> 8) & 15) * 17, rgb[1] = ((c >> 4) & 15) * 17, rgb[2] = (c & 15) * 17;
    }
}

void pc_skin_recolor_rgba(u8* rgba, int n_pixels, const u16* from, const u16* to) {
    int src[16][3], shift[16][3];
    int i, k;

    for (i = 0; i < 16; i++) {
        int dst[3];

        pc_skin_rgb(from[i], src[i]);
        pc_skin_rgb(to[i], dst);
        for (k = 0; k < 3; k++) {
            shift[i][k] = (i >= 1 && i <= 5) ? dst[k] - src[i][k] : 0;
        }
    }
    for (; n_pixels > 0; n_pixels--, rgba += 4) {
        int best = 0, best_d = 1 << 30;

        for (i = 0; i < 16; i++) {
            int dr = rgba[0] - src[i][0], dg = rgba[1] - src[i][1], db = rgba[2] - src[i][2];
            int d = dr * dr + dg * dg + db * db;

            if (d < best_d) {
                best_d = d, best = i;
            }
        }
        if (best >= 1 && best <= 5) {
            for (k = 0; k < 3; k++) {
                int v = rgba[k] + shift[best][k];

                rgba[k] = (u8)(v < 0 ? 0 : v > 255 ? 255 : v);
            }
        }
    }
}

void pc_skin_swatch(int tone, u8* r, u8* g, u8* b) {
    const u8* c = l_tone_rgb[tone >= 0 && tone < PC_SKIN_TONE_NUM ? tone : PC_SKIN_TONE_DEFAULT][4];

    *r = c[0], *g = c[1], *b = c[2];
}

/* --- the picker --- */

extern int g_pc_paused;
void pc_pause_menu_open_skin(void);
void mPlib_change_player_face_pallet(GAME* game);

/* pc_pattern_qr.h's action numbers */
enum { ACT_UP = 1, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_CONFIRM, ACT_CANCEL };

#define PICK_COLS 2
#define PICK_ROWS (PC_SKIN_TONE_NUM / PICK_COLS)

static struct {
    int active;
    int player;
    int can_cancel;
    int start; /* the tone it had */
    int sel;
    int done;
} l_pick;

/* Show tone `tone` on the player now (the scene keeps drawing them behind the picker) */
static void pc_skin_preview(int tone) {
    pc_skin_set_tone(l_pick.player, tone);
    if (gamePT != NULL && l_pick.player == pc_skin_now_player()) {
        mPlib_change_player_face_pallet(gamePT);
    }
}

void pc_skin_pick_open(int player, int can_cancel) {
    l_pick.active = TRUE;
    l_pick.player = player;
    l_pick.can_cancel = can_cancel;
    l_pick.start = pc_skin_tone_of(player);
    l_pick.sel = l_pick.start;
    l_pick.done = FALSE;
    pc_pause_menu_open_skin();
}

int pc_skin_pick_active(void) {
    return l_pick.active;
}

int pc_skin_pick_done(void) {
    return l_pick.done;
}

void pc_skin_pick_action(int act) {
    int col = l_pick.sel % PICK_COLS, row = l_pick.sel / PICK_COLS;

    if (!l_pick.active) {
        return;
    }
    switch (act) {
        case ACT_UP:
            row = (row + PICK_ROWS - 1) % PICK_ROWS;
            break;
        case ACT_DOWN:
            row = (row + 1) % PICK_ROWS;
            break;
        case ACT_LEFT:
        case ACT_RIGHT:
            col = 1 - col;
            break;
        case ACT_CONFIRM:
            pc_skin_preview(l_pick.sel);
            l_pick.done = TRUE;
            l_pick.active = FALSE;
            return;
        case ACT_CANCEL:
            if (l_pick.can_cancel) {
                pc_skin_preview(l_pick.start);
                l_pick.active = FALSE;
            }
            return;
        default:
            return;
    }
    l_pick.sel = row * PICK_COLS + col;
    pc_skin_preview(l_pick.sel);
}

static void pc_skin_rect(GRAPH* graph, int x, int y, int w, int h, int r, int g, int b, int a) {
    Gfx* gfx;

    OPEN_DISP(graph);
    gfx = NOW_FONT_DISP;
    gDPPipeSync(gfx++);
    gDPSetOtherMode(gfx++,
                    G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_POINT | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PRIM | G_RM_XLU_SURF | G_RM_XLU_SURF2);
    gDPSetCombineMode(gfx++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, a);
    gfx = gfx_gSPTextureRectangle1(gfx, x << 2, y << 2, (x + w) << 2, (y + h) << 2, 0, 0, 0, 0, 0);
    gDPPipeSync(gfx++);
    SET_FONT_DISP(gfx);
    CLOSE_DISP(graph);
}

/* Text centred across the panel */
static void pc_skin_text(struct game_s* game, const char* s, int px, int pw, int y, int r, int g, int b, int a, f32 scale) {
    pc_text_draw(game, s, (f32)px + ((f32)pw - (f32)pc_text_width(s) * scale) * 0.5f, (f32)y, r, g, b, a, scale);
}

void pc_skin_pick_draw(struct game_s* game) {
    static const int sw = 34, sh = 22, gap = 6;
    int pw = PICK_COLS * sw + (PICK_COLS - 1) * gap + 24, ph = PICK_ROWS * sh + (PICK_ROWS - 1) * gap + 56;
    int px = 320 - pw - 20, py = 12; /* to the side, above the clock: the player shows the tone in the middle */
    int i;

    if (!l_pick.active || game == NULL) {
        return;
    }
    pc_skin_rect(game->graph, px, py, pw, ph, 20, 32, 48, 240);
    pc_skin_text(game, "Skin tone", px, pw, py + 6, 255, 235, 120, 255, 1.0f);
    for (i = 0; i < PC_SKIN_TONE_NUM; i++) {
        int x = px + 12 + (i % PICK_COLS) * (sw + gap), y = py + 26 + (i / PICK_COLS) * (sh + gap);
        u8 r, g, b;

        if (i == l_pick.sel) {
            pc_skin_rect(game->graph, x - 3, y - 3, sw + 6, sh + 6, 255, 235, 120, 255);
        }
        pc_skin_swatch(i, &r, &g, &b);
        pc_skin_rect(game->graph, x, y, sw, sh, r, g, b, 255);
    }
    pc_skin_text(game, l_pick.can_cancel ? "A: pick  B: back" : "A: pick", px, pw, py + ph - 18, 200, 200, 200, 220,
                 0.8f);
}

/* --- Rover's question --- */

#define PC_SKIN_ASK_MSG 0x3D00 /* unused by the game; pc_skin_msg gives his line */

enum { ASK_NONE, ASK_LINE, ASK_CLOSING, ASK_PICK };

static int l_ask;
static int l_ask_next;

void pc_skin_ask_start(int next) {
    mMsg_Window_c* msg_p = mMsg_Get_base_window_p();

    l_ask = ASK_LINE;
    l_ask_next = next;
    mMsg_Set_continue_msg_num(msg_p, PC_SKIN_ASK_MSG);
    mMsg_Set_ForceNext(msg_p);
}

int pc_skin_ask_busy(void) {
    mMsg_Window_c* msg_p = mMsg_Get_base_window_p();

    switch (l_ask) {
        case ASK_LINE:
            /* his line shown in full, and A pressed to go on (the window stops waiting at its end) */
            if (mMsg_Get_msg_num(msg_p) == PC_SKIN_ASK_MSG && mMsg_Check_MainNormalContinue(msg_p) == TRUE &&
                chkTrigger(BUTTON_A)) {
                mMsg_request_main_disappear_wait_type1(msg_p);
                l_ask = ASK_CLOSING;
            }
            break;
        case ASK_CLOSING:
            if (mMsg_Check_main_wait(msg_p) == TRUE) {
                pc_skin_pick_open(pc_skin_now_player(), FALSE);
                l_ask = ASK_PICK;
            }
            break;
        case ASK_PICK:
            if (!l_pick.active && !g_pc_paused) {
                mMsg_ChangeMsgData(msg_p, l_ask_next);
                mMsg_Set_ForceNext(msg_p);
                mMsg_request_main_appear_wait_type1(msg_p);
                l_ask = ASK_NONE;
            }
            break;
    }
    return l_ask != ASK_NONE;
}

int pc_skin_msg(int index, u8* buf, int buf_size, int* len) {
    /* one page: the picker opens when the window waits at its end */
    static const char* line = "Oh, that reminds me! I can't\nreally tell human skin tones\napart. What would you say\nyours is, @?";
    const char* t;
    int i = 0;

    if (index != PC_SKIN_ASK_MSG || l_ask != ASK_LINE) {
        return FALSE;
    }
    for (t = line; *t != '\0' && i < buf_size - 8; t++) {
        if (*t == '\n') {
            buf[i++] = 0xCD;
        } else if (*t == '\f') {
            buf[i++] = 0x7F, buf[i++] = 0x04; /* wait for A */
            buf[i++] = 0xCD;
            buf[i++] = 0x7F, buf[i++] = 0x02; /* clear */
        } else if (*t == '@') {
            buf[i++] = 0x7F, buf[i++] = 0x1A; /* the player's name */
        } else {
            buf[i++] = (u8)*t;
        }
    }
    buf[i++] = 0x7F, buf[i++] = 0x01; /* CONTINUE: the guide carries on after A (pc_skin_ask_busy) */
    *len = i;
    return TRUE;
}

/* --- the save --- */

void pc_skin_reset(void) {
    memset(l_tone, PC_SKIN_TONE_DEFAULT, sizeof(l_tone));
}

int pc_skin_read_chunk(const char tag[4], void* fp, unsigned int size) {
    FILE* f = (FILE*)fp;

    if (memcmp(tag, "SKIN", 4) != 0) {
        return FALSE;
    }
    if (size != sizeof(l_tone) || fread(l_tone, 1, size, f) != size) {
        pc_skin_reset();
        if (size != sizeof(l_tone)) {
            fseek(f, (long)size, SEEK_CUR);
        }
    }
    return TRUE;
}

int pc_skin_write_chunk(void* fp) {
    FILE* f = (FILE*)fp;
    u32 size = sizeof(l_tone);

    return fwrite("SKIN", 1, 4, f) == 4 && fwrite(&size, 4, 1, f) == 1 && fwrite(l_tone, 1, size, f) == size;
}
