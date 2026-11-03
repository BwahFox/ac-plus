/* pc_cf_creature.c - City Folk's fish and bugs the GameCube has none of (see pc_cf_creature.h). Everything but their
 * pictures is in pc_cf_creature_table.h (pc/tools/cf_creatures.tsv); the pictures, made from the player's disc, are in
 * cf_sky/creatures.cfcr. Without that file there are none: they don't come out, and an item of theirs is unknown. */
#include "pc_cf_creature.h"
#include "ac_gyoei.h"
#include "ac_insect_h.h"
#include "ac_set_ovl_gyoei.h"
#include "ac_set_ovl_insect.h"
#include "libforest/gbi_extensions.h"
#include "m_common_data.h"
#include "m_font.h"
#include "m_lib.h"
#include "m_collision_bg.h"
#include "m_rcp.h"
#include "sys_matrix.h"
#include "pc_cf_ftr.h"
#include "game.h"

#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIC 128

typedef struct {
    const char* name;
    u32 sell;
    u16 months;
    u32 hours;
    u8 area;
    u8 weight;
    s16 base; /* a fish's size; a bug's GameCube type */
    u8 size;
    u8 placed; /* City Folk's file number (for the tool) */
    u8 n;      /* furniture 0x3C00 + 4 * n */
} pc_cf_creature_c;

typedef struct {
    u8* pic;  /* RGB5A3 */
    Gfx* dl[3]; /* a fish's bends, a bug's wings */
    Vtx* vtx;
} pc_cf_creature_gfx_c;

#define X(name, sell, months, hours, area, weight, base, size, placed, n) \
    { name, sell, months, hours, area, weight, base, size, placed, n },
static const pc_cf_creature_c l_fish[PC_CF_FISH_NUM] = { PC_CF_FISH_TABLE(X) };
static const pc_cf_creature_c l_bug[PC_CF_BUG_NUM] = { PC_CF_BUG_TABLE(X) };
#undef X

static pc_cf_creature_gfx_c l_fish_gfx[PC_CF_FISH_NUM];
static pc_cf_creature_gfx_c l_bug_gfx[PC_CF_BUG_NUM];
static Gfx l_nothing[1];
static int l_loaded;
static u8 l_donor[PC_CF_CREATURE_NUM];

static const pc_cf_creature_c* pc_cf_of_item(mActor_name_t item, pc_cf_creature_gfx_c** gfx) {
    if (PC_CF_ITEM_IS_FISH(item)) {
        if (gfx != NULL) {
            *gfx = &l_fish_gfx[item - PC_CF_FISH_ITEM];
        }
        return &l_fish[item - PC_CF_FISH_ITEM];
    }
    if (PC_CF_ITEM_IS_BUG(item)) {
        if (gfx != NULL) {
            *gfx = &l_bug_gfx[item - PC_CF_BUG_ITEM];
        }
        return &l_bug[item - PC_CF_BUG_ITEM];
    }
    return NULL;
}

int pc_cf_creature_item(mActor_name_t item) {
    pc_cf_creature_gfx_c* g;

    pc_cf_creature_load();
    return pc_cf_of_item(item, &g) != NULL && g->pic != NULL;
}

int pc_cf_creature_name(mActor_name_t item, u8* dst, int len) {
    const pc_cf_creature_c* c = pc_cf_of_item(item, NULL);
    int i, n;

    if (c == NULL) {
        return FALSE;
    }
    n = (int)strlen(c->name);
    for (i = 0; i < len; i++) {
        dst[i] = i < n ? (u8)c->name[i] : CHAR_SPACE;
    }
    return TRUE;
}

u32 pc_cf_creature_price(mActor_name_t item) {
    const pc_cf_creature_c* c = pc_cf_of_item(item, NULL);

    return c != NULL ? c->sell * 4 : 0; /* (the price lists' are what Nook sells at: he buys at a quarter) */
}

u8* pc_cf_creature_icon(mActor_name_t item) {
    pc_cf_creature_gfx_c* g;

    if (!pc_cf_creature_item(item)) {
        return NULL;
    }
    pc_cf_of_item(item, &g);
    return g->pic;
}

/* A picture on a strip, along x, its tail bent back and forth in z as the GameCube's fish's are */
#define FISH_COLS 5
static void pc_cf_fish_strip(pc_cf_creature_gfx_c* g, int size) {
    static const int half[] = { 1500, 1700, 2000, 2300, 2900 }; /* XS S M L XL */
    int w = half[size], f, c, r;

    g->vtx = (Vtx*)calloc(3 * (FISH_COLS + 1) * 2, sizeof(Vtx));
    if (g->vtx == NULL) {
        return;
    }
    for (f = 0; f < 3; f++) {
        Vtx* v = g->vtx + f * (FISH_COLS + 1) * 2;
        Gfx* d = (Gfx*)malloc(sizeof(Gfx) * 32);
        Gfx* gp = d;

        for (c = 0; c <= FISH_COLS; c++) {
            for (r = 0; r < 2; r++) {
                Vtx* p = &v[c * 2 + r];
                int bend = f == 0 ? 0 : (f == 1 ? 1 : -1) * (c * c * w) / (FISH_COLS * FISH_COLS * 2);

                /* held by the tail: the picture's head (left) up */
                p->n.ob[0] = (s16)(r == 0 ? -w : w);
                p->n.ob[1] = (s16)(w - 2 * w * c / FISH_COLS);
                p->n.ob[2] = (s16)bend;
                p->n.tc[0] = (s16)((PIC * c / FISH_COLS) * 32);
                p->n.tc[1] = (s16)((r == 0 ? PIC : 0) * 32); /* (its top to the viewer's right, as the GameCube's) */
                p->n.n[2] = 127;
                p->n.a = 255;
            }
        }
        if (d == NULL) {
            return;
        }
        gSPTexture(gp++, 0, 0, 0, G_TX_RENDERTILE, G_ON);
        gDPSetRenderMode(gp++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_TEX_EDGE2);
        gDPSetCombineLERP(gp++, TEXEL0, 0, SHADE, 0, 0, 0, 0, TEXEL0, PRIMITIVE, 0, COMBINED, 0, 0, 0, 0, COMBINED);
        gDPSetTextureImage_Dolphin(gp++, G_IM_FMT_RGBA, G_IM_SIZ_16b, PIC, PIC, g->pic);
        gDPSetTile_Dolphin(gp++, G_DOLPHIN_TLUT_DEFAULT_MODE, 0, 0, GX_CLAMP, GX_CLAMP, 0, 0);
        gDPSetPrimColor(gp++, 0, 128, 255, 255, 255, 255);
        gSPLoadGeometryMode(gp++, G_ZBUFFER | G_SHADE | G_FOG | G_LIGHTING | G_SHADING_SMOOTH);
        gSPVertex(gp++, v, (FISH_COLS + 1) * 2, 0);
        for (c = 0; c < FISH_COLS; c++) {
            gSP2Triangles(gp++, c * 2, c * 2 + 1, c * 2 + 3, 0, c * 2, c * 2 + 3, c * 2 + 2, 0);
        }
        gSPEndDisplayList(gp++);
        g->dl[f] = d;
    }
}

/* A picture on one quad, flat as the GameCube's bugs' (x and z in its own terms are the display lists' x and y);
 * wings closing: narrower */
static void pc_cf_bug_quads(pc_cf_creature_gfx_c* g, int half) {
    static const int width[3] = { 100, 60, 80 };
    int f;

    g->vtx = (Vtx*)calloc(3 * 4, sizeof(Vtx));
    if (g->vtx == NULL) {
        return;
    }
    for (f = 0; f < 3; f++) {
        Vtx* v = g->vtx + f * 4;
        Gfx* d = (Gfx*)malloc(sizeof(Gfx) * 16);
        Gfx* gp = d;
        int w = half * 100 * width[f] / 100, h = half * 100, k;

        if (d == NULL) {
            return;
        }
        for (k = 0; k < 4; k++) {
            v[k].n.ob[0] = (s16)((k == 0 || k == 3) ? -w : w);
            v[k].n.ob[1] = (s16)(k < 2 ? h : -h);
            v[k].n.tc[0] = (s16)(((k == 0 || k == 3) ? 0 : PIC) * 32);
            v[k].n.tc[1] = (s16)((k < 2 ? 0 : PIC) * 32);
            v[k].n.n[2] = 127;
            v[k].n.a = 255;
        }
        gSPTexture(gp++, 0, 0, 0, G_TX_RENDERTILE, G_ON);
        gDPSetCombineLERP(gp++, TEXEL0, 0, PRIMITIVE, 0, TEXEL0, 0, ENVIRONMENT, 0, COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED);
        gDPSetPrimColor(gp++, 0, 128, 255, 255, 255, 255);
        gDPSetRenderMode(gp++, G_RM_FOG_SHADE_A, G_RM_ZB_XLU_SURF2);
        gDPSetTextureImage_Dolphin(gp++, G_IM_FMT_RGBA, G_IM_SIZ_16b, PIC, PIC, g->pic);
        gDPSetTile_Dolphin(gp++, G_DOLPHIN_TLUT_DEFAULT_MODE, 0, 0, GX_CLAMP, GX_CLAMP, 0, 0);
        gSPLoadGeometryMode(gp++, G_ZBUFFER | G_SHADE | G_FOG | G_LIGHTING | G_SHADING_SMOOTH);
        gSPVertex(gp++, v, 4, 0);
        gSP2Triangles(gp++, 0, 1, 2, 0, 0, 2, 3, 0);
        gSPEndDisplayList(gp++);
        g->dl[f] = d;
    }
}

void pc_cf_creature_load(void) {
    FILE* f;
    u8 head[12];
    u32 n, i;

    if (l_loaded) {
        return;
    }
    l_loaded = TRUE;
    gSPEndDisplayList(l_nothing);
    f = fopen("cf_sky/creatures.cfcr", "rb");
    if (f == NULL) {
        return;
    }
    if (fread(head, 1, 12, f) == 12 && memcmp(head, "CFCR", 4) == 0 && (head[4] | head[5] << 8) == 2) {
        n = head[8] | head[9] << 8;
        for (i = 0; i < n; i++) {
            u8 ih[4];
            mActor_name_t item;
            const pc_cf_creature_c* c;
            pc_cf_creature_gfx_c* g = NULL;
            pc_cf_creature_gfx_c got;
            size_t pic_size = PIC * PIC * 2;

            if (fread(ih, 1, 4, f) != 4) {
                break;
            }
            item = (mActor_name_t)(ih[0] | ih[1] << 8);
            got.pic = (u8*)malloc(pic_size);
            if (got.pic == NULL || fread(got.pic, 1, pic_size, f) != pic_size) {
                break;
            }
            c = pc_cf_of_item(item, &g);
            if (c == NULL) {
                continue;
            }
            *g = got;
            g->vtx = NULL;
            g->dl[0] = g->dl[1] = g->dl[2] = NULL;
            if (PC_CF_ITEM_IS_FISH(item)) {
                pc_cf_fish_strip(g, c->base == aGYO_SIZE_XS   ? 0
                                    : c->base == aGYO_SIZE_S  ? 1
                                    : c->base == aGYO_SIZE_M  ? 2
                                    : c->base == aGYO_SIZE_L  ? 3
                                                              : 4);
            } else {
                pc_cf_bug_quads(g, c->size);
            }
        }
    }
    fclose(f);
}

Gfx** pc_cf_fish_dl(int type) {
    int i = type - aGYO_TYPE_PC_CF;
    pc_cf_creature_load();
    return i >= 0 && i < PC_CF_FISH_NUM && l_fish_gfx[i].dl[0] != NULL ? l_fish_gfx[i].dl : NULL;
}

int pc_cf_fish_in_sea(int type) {
    int i = type - aGYO_TYPE_PC_CF;

    return i >= 0 && i < PC_CF_FISH_NUM && l_fish[i].area == aSOG_SPAWN_AREA_SEA;
}

static int pc_cf_out_now(const pc_cf_creature_c* c, const pc_cf_creature_gfx_c* g, int* area) {
    lbRTC_time_c* rtc = Common_GetPointer(time.rtc_time);

    if (g->pic == NULL || !(c->months >> (rtc->month - 1) & 1) || !(c->hours >> rtc->hour & 1)) {
        return 0;
    }
    *area = c->area;
    return getenv("PC_CF_CR_FORCE") != NULL ? c->weight * 1000 : c->weight; /* (dev: they're nearly all you find) */
}

int pc_cf_fish_now(int i, int* area) {
    pc_cf_creature_load();
    return i >= 0 && i < PC_CF_FISH_NUM ? pc_cf_out_now(&l_fish[i], &l_fish_gfx[i], area) : 0;
}

int pc_cf_bug_now(int i, int* area) {
    pc_cf_creature_load();
    return i >= 0 && i < PC_CF_BUG_NUM ? pc_cf_out_now(&l_bug[i], &l_bug_gfx[i], area) : 0;
}

int pc_cf_bug_base(int type) {
    int i = type - aINS_TYPE_PC_CF;

    return i >= 0 && i < PC_CF_BUG_NUM ? l_bug[i].base : type;
}

Gfx* pc_cf_bug_dl(int type, int draw_type) {
    int i = type - aINS_TYPE_PC_CF;
    int base;

    if (i < 0 || i >= PC_CF_BUG_NUM || l_bug_gfx[i].dl[0] == NULL || (draw_type & 1) != 0) {
        return l_nothing;
    }
    base = l_bug[i].base;
    if (base == aINS_INSECT_TYPE_COMMON_BUTTERFLY || base == aINS_INSECT_TYPE_YELLOW_BUTTERFLY ||
        base == aINS_INSECT_TYPE_TIGER_BUTTERFLY) {
        return l_bug_gfx[i].dl[(draw_type >> 1) % 3]; /* wings */
    }
    return l_bug_gfx[i].dl[0];
}

mActor_name_t pc_cf_creature_placed(mActor_name_t item) {
    const pc_cf_creature_c* c = pc_cf_of_item(item, NULL);

    return c != NULL ? (mActor_name_t)(PC_CF_PLACED_ITEM + 4 * c->n) : item;
}

mActor_name_t pc_cf_creature_of_placed(mActor_name_t ftr_item) {
    int n;

    if (!PC_CF_ITEM_IS_PLACED(ftr_item)) {
        return 0;
    }
    n = (ftr_item - PC_CF_PLACED_ITEM) >> 2;
    return (mActor_name_t)(n < PC_CF_FISH_NUM ? PC_CF_FISH_ITEM + n : PC_CF_BUG_ITEM + n - PC_CF_FISH_NUM);
}

static u8 l_caught[PLAYER_NUM][(PC_CF_CREATURE_NUM + 7) / 8];

static int pc_cf_player_now(void) {
    int i;

    for (i = 0; i < PLAYER_NUM; i++) {
        if (Common_Get(now_private) == Save_GetPointer(private_data[i])) {
            return i;
        }
    }
    return -1;
}

static int pc_cf_museum_idx(mActor_name_t item) {
    return PC_CF_ITEM_IS_FISH(item)  ? item - PC_CF_FISH_ITEM
           : PC_CF_ITEM_IS_BUG(item) ? PC_CF_FISH_NUM + item - PC_CF_BUG_ITEM
                                     : -1;
}

void pc_cf_creature_set_caught(mActor_name_t item) {
    int i = pc_cf_museum_idx(item), p = pc_cf_player_now();

    if (i >= 0 && p >= 0) {
        l_caught[p][i >> 3] |= (u8)(1 << (i & 7));
    }
}

mActor_name_t pc_cf_creature_collected_nth(int fish, int n) {
    int i, p = pc_cf_player_now();

    if (p < 0 || n < 0 || n >= (fish ? PC_CF_FISH_NUM : PC_CF_BUG_NUM)) {
        return EMPTY_NO;
    }
    i = fish ? n : PC_CF_FISH_NUM + n;
    if (!(l_caught[p][i >> 3] >> (i & 7) & 1)) {
        return EMPTY_NO;
    }
    return (mActor_name_t)(fish ? PC_CF_FISH_ITEM + n : PC_CF_BUG_ITEM + n);
}

int pc_cf_museum_donor(mActor_name_t item) {
    int i = pc_cf_museum_idx(item);

    return i >= 0 ? l_donor[i] : 0;
}

void pc_cf_museum_donate(mActor_name_t item, int player) {
    int i = pc_cf_museum_idx(item);

    if (i >= 0) {
        l_donor[i] = (u8)(1 + player);
    }
}

void pc_cf_museum_reset(void) {
    memset(l_donor, 0, sizeof(l_donor));
    memset(l_caught, 0, sizeof(l_caught));
}

int pc_cf_museum_read_chunk(const char tag[4], void* fp, unsigned int size) {
    u8 buf[256];

    if (memcmp(tag, "CRCL", 4) == 0) {
        if (size != sizeof(l_caught) || fread(l_caught, 1, size, (FILE*)fp) != size) {
            memset(l_caught, 0, sizeof(l_caught));
            if (size != sizeof(l_caught)) {
                fseek((FILE*)fp, (long)size, SEEK_CUR);
            }
        }
        return TRUE;
    }
    if (memcmp(tag, "CRMU", 4) != 0) {
        return FALSE;
    }
    if (size > sizeof(buf) || fread(buf, 1, size, (FILE*)fp) != size) {
        fseek((FILE*)fp, (long)size, SEEK_CUR);
        return TRUE;
    }
    memcpy(l_donor, buf, size < sizeof(l_donor) ? size : sizeof(l_donor));
    return TRUE;
}

int pc_cf_museum_write_chunk(void* fp) {
    u32 size = sizeof(l_caught);
    FILE* f = (FILE*)fp;
    int i, any = FALSE;

    if (fwrite("CRCL", 1, 4, f) != 4 || fwrite(&size, 4, 1, f) != 1 || fwrite(l_caught, 1, size, f) != size) {
        return FALSE;
    }
    size = sizeof(l_donor);

    for (i = 0; i < (int)sizeof(l_donor); i++) {
        any |= l_donor[i] != 0;
    }
    if (!any) {
        return TRUE;
    }
    return fwrite("CRMU", 1, 4, f) == 4 && fwrite(&size, 4, 1, f) == 1 && fwrite(l_donor, 1, size, f) == size;
}

/* What's said about them: no lines of their own in the game's messages, so these stand in for the one next asked for.
 * The catch lines and Chip's come from City Folk's messages (cf_sky/creatures.cfln) */
#define SAY_MAX 1500
static int l_say_msg = -1;
static u8 l_say[SAY_MAX];
static int l_say_len;
static u8* l_catch_line[PC_CF_CREATURE_NUM];
static u8* l_chip_line[PC_CF_CREATURE_NUM];
static u16 l_catch_len[PC_CF_CREATURE_NUM];
static u16 l_chip_len[PC_CF_CREATURE_NUM];
static u8* l_talk_line[PC_CF_CREATURE_NUM];
static u16 l_talk_len[PC_CF_CREATURE_NUM];

static int pc_cf_creature_index(mActor_name_t item) {
    return PC_CF_ITEM_IS_FISH(item) ? item - PC_CF_FISH_ITEM : PC_CF_FISH_NUM + (item - PC_CF_BUG_ITEM);
}

static void pc_cf_creature_load_lines(void) {
    static int loaded;
    FILE* f;
    u8 head[8];

    if (loaded) {
        return;
    }
    loaded = TRUE;
    f = fopen("cf_sky/creatures.cfln", "rb");
    if (f == NULL) {
        return;
    }
    if (fread(head, 1, 8, f) == 8 && memcmp(head, "CFLN", 4) == 0) {
        int n = head[4] | head[5] << 8, i;

        for (i = 0; i < n; i++) {
            u8 h[8];
            mActor_name_t item;
            int caught, chip, talk;
            u8* a;
            u8* b;
            u8* c;

            if (fread(h, 1, 8, f) != 8) {
                break;
            }
            item = (mActor_name_t)(h[0] | h[1] << 8);
            caught = h[2] | h[3] << 8;
            chip = h[4] | h[5] << 8;
            talk = h[6] | h[7] << 8;
            a = (u8*)malloc(caught + 1);
            b = (u8*)malloc(chip + 1);
            c = (u8*)malloc(talk + 1);
            if (a == NULL || b == NULL || c == NULL || fread(a, 1, caught, f) != (size_t)caught ||
                fread(b, 1, chip, f) != (size_t)chip || fread(c, 1, talk, f) != (size_t)talk) {
                free(a);
                free(b);
                free(c);
                break;
            }
            if (PC_CF_ITEM_IS_FISH(item) || PC_CF_ITEM_IS_BUG(item)) {
                int k = pc_cf_creature_index(item);

                l_catch_line[k] = a;
                l_catch_len[k] = (u16)caught;
                l_chip_line[k] = b;
                l_chip_len[k] = (u16)chip;
                l_talk_line[k] = c;
                l_talk_len[k] = (u16)talk;
            }
        }
    }
    fclose(f);
}

static int pc_cf_creature_short_name(mActor_name_t item, char* out) {
    u8 name[mIN_ITEM_NAME_LEN + 1];
    int n;

    pc_cf_creature_name(item, name, mIN_ITEM_NAME_LEN);
    for (n = mIN_ITEM_NAME_LEN; n > 0 && name[n - 1] == CHAR_SPACE; n--) {
    }
    memcpy(out, name, n);
    out[n] = '\0';
    return n;
}

static const char* pc_cf_article(const char* name) {
    return (name[0] == 'a' || name[0] == 'e' || name[0] == 'i' || name[0] == 'o' || name[0] == 'u') ? "an" : "a";
}

void pc_cf_creature_say_caught(mActor_name_t item, int msg_no) {
    extern int pc_msg_text(const char* t, u8* buf, int buf_size);
    int k = pc_cf_creature_index(item);

    pc_cf_creature_load_lines();
    if (l_catch_line[k] != NULL && l_catch_len[k] <= SAY_MAX) {
        memcpy(l_say, l_catch_line[k], l_catch_len[k]);
        l_say_len = l_catch_len[k];
    } else {
        char name[mIN_ITEM_NAME_LEN + 1];
        char text[128];

        pc_cf_creature_short_name(item, name);
        snprintf(text, sizeof(text), "{7f}{05}{1e}{3c}{1e}I caught %s %s!|{7f}{01}", pc_cf_article(name), name);
        l_say_len = pc_msg_text(text, l_say, SAY_MAX);
    }
    l_say_msg = msg_no;
}

/* Chip at the fishing tourney, as he is about any fish that isn't bass: it's not one, then what he'd cook it in (City
 * Folk's lines), then on to the GameCube's "shall I take it?" */
void pc_cf_creature_say_chip(mActor_name_t item, int msg_no) {
    extern int pc_msg_text(const char* t, u8* buf, int buf_size);
    char name[mIN_ITEM_NAME_LEN + 1];
    char text[320];
    int k = pc_cf_creature_index(item);
    int n;

    pc_cf_creature_load_lines();
    pc_cf_creature_short_name(item, name);
    snprintf(text, sizeof(text),
             "{7f}{72}{7f}{03}{1e}{7f}{09}{00}{00}{04}Hold on! {7f}{03}{06}That's no bass!{7f}{03}{06}|"
             "That's %s %s!{7f}{03}{06}|{7f}{09}{00}{00}{17}It's bass or nothing in this|tourney. {7f}{03}{06}Sorry!{7f}{04}|{7f}{02}{7f}{09}{00}{00}{15}",
             pc_cf_article(name), name);
    n = pc_msg_text(text, l_say, SAY_MAX);
    if (l_chip_line[k] != NULL && n + l_chip_len[k] + 16 <= SAY_MAX) {
        memcpy(l_say + n, l_chip_line[k], l_chip_len[k]);
        n += l_chip_len[k];
    }
    n += pc_msg_text("|{7f}{0e}{11}{0e}{7f}{73}|{7f}{01}", l_say + n, SAY_MAX - n);
    l_say_len = n;
    l_say_msg = msg_no;
}

/* Blathers on what's just been given: City Folk's words, between the ways his own lines start and end. (As his go:
 * {7f}{09} his faces, 72 and 73 at a fish's the putting away; a bug he can't be doing with) */
void pc_cf_creature_say_blathers(mActor_name_t item, int msg_no) {
    extern int pc_msg_text(const char* t, u8* buf, int buf_size);
    int k = pc_cf_creature_index(item);
    int bug = PC_CF_ITEM_IS_BUG(item);
    int n;

    pc_cf_creature_load_lines();
    n = pc_msg_text(bug ? "{7f}{09}{00}{00}{ff}" : "{7f}{72}{7f}{09}{00}{00}{ff}", l_say, SAY_MAX);
    if (l_talk_line[k] != NULL && n + l_talk_len[k] + 96 <= SAY_MAX) {
        memcpy(l_say + n, l_talk_line[k], l_talk_len[k]);
        n += l_talk_len[k];
    } else {
        n += pc_msg_text("Hoo! A rare specimen, this!", l_say + n, SAY_MAX - n);
    }
    n += pc_msg_text(bug ? "{7f}{04}|{7f}{02}{7f}{09}{00}{00}{ff}Hooo...{7f}{03}{08} But I digress...{7f}{03}{08}|{7f}{01}"
                         : "{7f}{03}{06}{7f}{09}{01}{00}{0e}{7f}{73}|{7f}{01}",
                     l_say + n, SAY_MAX - n);
    l_say_len = n;
    l_say_msg = msg_no;
}

int pc_cf_creature_msg(int index, u8* buf, int buf_size, int* len) {
    if (l_say_msg < 0 || index != l_say_msg) {
        return FALSE;
    }
    l_say_msg = -1;
    *len = l_say_len < buf_size ? l_say_len : buf_size;
    memcpy(buf, l_say, *len);
    return TRUE;
}

/* --- In the museum: what's been given, as themselves ---
 * Fish swim round the tanks (river fish the four tanks in the room, sea fish the long one at the back); bugs keep to
 * the garden beds, the butterflies and dragonflies above them. */
#define PC_CF_MUSEUM_MODEL 0x3D00
#define PC_CF_MUSEUM_PI 3.14159265f

static f32 pc_cf_bug_scale(int i, f32 scale) {
    f32 width = pc_cf_ftr_model_width((mActor_name_t)(PC_CF_MUSEUM_MODEL + 4 * (PC_CF_FISH_NUM + i)));
    f32 most = width > 0.0f ? (f32)l_bug[i].size * 2.0f / width : scale;

    return most < scale ? most : scale;
}

void pc_cf_museum_draw_fish(struct game_s* game_, f32 scale) {
    GAME* game = (GAME*)game_;
    static const f32 tank[5][4] = {
        /* centre x, z, half x, half z */
        { 220.0f, 220.0f, 55.0f, 55.0f }, { 420.0f, 220.0f, 55.0f, 55.0f }, { 220.0f, 460.0f, 55.0f, 55.0f },
        { 420.0f, 460.0f, 55.0f, 55.0f }, { 320.0f, 15.0f, 225.0f, 10.0f },
    };
    int i;
    f32 t = (f32)game->frame_counter;

    pc_cf_creature_load();
    _texture_z_light_fog_prim(game->graph);
    for (i = 0; i < PC_CF_FISH_NUM; i++) {
        const f32* k;
        f32 a, x, z;

        if (l_donor[i] == 0) {
            continue;
        }
        k = tank[l_fish[i].area == aSOG_SPAWN_AREA_SEA ? 4 : i & 3];
        a = t * 0.004f + (f32)i * 2.4f;
        x = k[0] + cosf(a) * k[2] * (0.6f + 0.4f * (f32)(i % 3) / 2.0f);
        z = k[1] + sinf(a) * k[3] * (0.6f + 0.4f * (f32)(i % 3) / 2.0f);
        Matrix_translate(x, 28.0f + (f32)(i % 4) * 8.0f, z, MTX_LOAD);
        /* along its way round (the models' heads along z) */
        Matrix_RotateY((s16)((-a) * (32768.0f / PC_CF_MUSEUM_PI)), MTX_MULT);
        {
            /* (a shark's a shark, but it still has to fit its tank) */
            f32 s = l_fish[i].base == aGYO_SIZE_XL ? scale * 0.75f : scale;

            Matrix_scale(s, s, s, MTX_MULT);
        }
        pc_cf_ftr_draw_model((mActor_name_t)(PC_CF_MUSEUM_MODEL + 4 * i), (struct game_s*)game, t * 0.5f + (f32)i * 7.0f);
    }
}

void pc_cf_museum_draw_bugs(struct game_s* game_, f32 scale) {
    GAME* game = (GAME*)game_;
    int i;
    f32 t = (f32)game->frame_counter;

    pc_cf_creature_load();
    _texture_z_light_fog_prim(game->graph);
    for (i = 0; i < PC_CF_BUG_NUM; i++) {
        int base = l_bug[i].base;
        int flies = base == aINS_INSECT_TYPE_COMMON_BUTTERFLY || base == aINS_INSECT_TYPE_YELLOW_BUTTERFLY ||
                    base == aINS_INSECT_TYPE_TIGER_BUTTERFLY || base == aINS_INSECT_TYPE_DARNER_DRAGONFLY;
        /* two beds, front and back: seven across, two deep */
        f32 x = 140.0f + (f32)((i / 2) % 7) * 50.0f;
        f32 z = ((i / 14) & 1 ? 450.0f : 170.0f) + (f32)(i & 1) * 50.0f;
        f32 y;
        xyz_t pos;

        if (l_donor[PC_CF_FISH_NUM + i] == 0) {
            continue;
        }
        pos.x = x;
        pos.y = 0.0f;
        pos.z = z;
        y = mCoBG_GetBgY_OnlyCenter_FromWpos2(pos, 0.0f);
        if (flies) {
            y += 30.0f + 6.0f * sinf(t * 0.03f + (f32)i);
            x += 12.0f * cosf(t * 0.012f + (f32)i);
        }
        Matrix_translate(x, y, z, MTX_LOAD);
        Matrix_RotateY((s16)(i * 7000), MTX_MULT);
        {
            f32 sc = pc_cf_bug_scale(i, scale);

            Matrix_scale(sc, sc, sc, MTX_MULT);
        }
        pc_cf_ftr_draw_model((mActor_name_t)(PC_CF_MUSEUM_MODEL + 4 * (PC_CF_FISH_NUM + i)), (struct game_s*)game,
                             t * 0.5f + (f32)i * 7.0f);
    }
}

void pc_cf_bug_draw_model(int type, struct game_s* game_) {
    GAME* game = (GAME*)game_;
    int i = type - aINS_TYPE_PC_CF;

    if (i < 0 || i >= PC_CF_BUG_NUM) {
        return;
    }
    /* The picture lies in the matrix's x-y plane, its top along y and facing z; the model stands in x-z, its top up y
     * and its tail along z: a quarter turn about x. The bug's actor scale is 0.01, the museum's models 0.08, less if
     * that's bigger than the bug is meant to be (cf_creatures.tsv: size) */
    Matrix_RotateX((s16)0x4000, MTX_MULT);
    {
        f32 s = pc_cf_bug_scale(i, 0.08f) * 100.0f;

        Matrix_scale(s, s, s, MTX_MULT);
    }
    pc_cf_ftr_draw_model((mActor_name_t)(PC_CF_MUSEUM_MODEL + 4 * (PC_CF_FISH_NUM + i)), game_,
                         (f32)game->frame_counter * 0.5f + (f32)i * 7.0f);
}
