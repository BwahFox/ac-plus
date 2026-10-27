/* pc_mp_town.c - the host's town, sent to a visitor (multiplayer, pc_mp.c).
 *
 * Not a save file: the host writes only what a visitor needs to walk around the town, field by field, in a
 * layout of our own (version below). The visitor reads it with the bounded reader (mp_wire.h) and builds a town
 * from it on top of a copy of its own save: every value the game would use as an index or an id is checked, and
 * anything wrong is replaced by something harmless (an empty unit, a default villager, ...) instead of being used.
 * Villagers are made new from their id with the game's own defaults; their memories, mail and the residents'
 * private data are not sent at all.
 *
 * Sent: town name and id, residents' names, the acres, the items on the ground, buried-item flags, house outsides
 * and rooms (what stands in them), villagers (who, where they live, shirt, umbrella, catchphrase), the shop's level
 * and stock, the bridge, and a few town settings (station, grass, fruit, tune, weather). Then ac-plus's own: the
 * patterns laid on the ground (with their designs) and the furniture standing outdoors; the visitor puts those in
 * after arriving (pc_mp_town_apply_extras), as the swap of towns resets them. Of the residents' patterns only the
 * one on each house's door is sent, kept apart from the visitor's own patterns (pc_mp_town_door). */
#include "pc_mp_town.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m_actor.h"
#include "m_common_data.h"
#include "m_field_make.h"
#include "m_home_h.h"
#include "m_house.h"
#include "m_land.h"
#include "m_mail.h"
#include "m_name_table.h"
#include "m_npc.h"
#include "m_private.h"
#include "m_random_field.h"
#include "m_shop.h"
#include "m_kankyo.h"
#include "pc_ext_save.h"
#include "pc_outdoor_ftr.h"
#include "pc_pattern.h"

#include "mp/mp_wire.h"

#define TOWN_VERSION 4

extern void mNpc_pc_SetDefAnimal(Animal_c* animal, mActor_name_t npc_id); /* m_npc.c */

/* --- checks --- */

/* text in the game's charset, cleaned (mp_text_clean) */
static void rd_text(mp_rd* r, u8* out, int n) {
    mp_rd_bytes(r, out, (size_t)n);
    mp_text_clean(out, (size_t)n);
}

static void wr_pid(mp_wr* w, const PersonalID_c* id) {
    mp_wr_bytes(w, id->player_name, PLAYER_NAME_LEN);
    mp_wr_bytes(w, id->land_name, LAND_NAME_SIZE);
    mp_wr_u16(w, id->player_id);
    mp_wr_u16(w, id->land_id);
}

static void rd_pid(mp_rd* r, PersonalID_c* id) {
    rd_text(r, id->player_name, PLAYER_NAME_LEN);
    rd_text(r, id->land_name, LAND_NAME_SIZE);
    id->player_id = mp_rd_u16(r);
    id->land_id = mp_rd_u16(r);
}

/* --- host --- */

/* While buildings, mailboxes or signs are on screen, their units hold placeholders and the real items come back
 * only when the scene ends (restore_fgdata_all), so the town is written from a restored copy, as quick save does
 * (pc_quick_save.c), and the live data is put back after. */
static int town_pack(u8* out, int cap);

int pc_mp_town_pack(GAME_PLAY* play, u8* out, int cap) {
    Save_t* live = Common_GetPointer(save.save);
    Save_t* snapshot = (Save_t*)malloc(sizeof(Save_t));
    int n;

    if (snapshot == NULL) {
        return 0;
    }
    memcpy(snapshot, live, sizeof(Save_t));
    if (play != NULL) {
        restore_fgdata_all(play);
    }
    n = town_pack(out, cap);
    memcpy(live, snapshot, sizeof(Save_t));
    free(snapshot);
    return n;
}

/* --- ac-plus extras: ground patterns, outdoor furniture --- */

#define EXTRA_PATTERNS_MAX 256
#define EXTRA_GROUND_MAX (PC_EXT_GROUND_BLOCK_X * PC_EXT_GROUND_BLOCK_Z * PC_EXT_UNITS * PC_EXT_UNITS)
#define EXTRA_FTR_MAX (PC_OFTR_LAYER_NUM * PC_OFTR_UT_TOTAL_NUM)

static u32 l_pat_ids[EXTRA_PATTERNS_MAX];
static int l_pat_n;

static void note_pattern(u32 id) {
    int i;
    for (i = 0; i < l_pat_n; i++) {
        if (l_pat_ids[i] == id) {
            return;
        }
    }
    if (l_pat_n < EXTRA_PATTERNS_MAX && pc_pattern_get(id) != NULL) {
        l_pat_ids[l_pat_n++] = id;
    }
}

static void count_ftr(void* user, int ut, int layer, mActor_name_t item) {
    (*(u32*)user)++;
}

static void write_ftr(void* user, int ut, int layer, mActor_name_t item) {
    mp_wr* w = (mp_wr*)user;
    mp_wr_u16(w, (u16)ut);
    mp_wr_u8(w, (u8)layer);
    mp_wr_u16(w, item);
}

static void pack_extras(mp_wr* w) {
    int i, bx, bz, ux, uz;
    u32 n = 0;

    l_pat_n = 0;
    pc_ext_ground_each_id(note_pattern);
    mp_wr_u16(w, (u16)l_pat_n);
    for (i = 0; i < l_pat_n; i++) {
        const mNW_original_design_c* d = pc_pattern_get(l_pat_ids[i]);
        mp_wr_u32(w, l_pat_ids[i]);
        mp_wr_bytes(w, d->name, mNW_ORIGINAL_DESIGN_NAME_LEN);
        mp_wr_u8(w, d->palette);
        mp_wr_bytes(w, d->design.data, mNW_DESIGN_TEX_SIZE);
    }
    for (bz = 0; bz < PC_EXT_GROUND_BLOCK_Z; bz++) {
        for (bx = 0; bx < PC_EXT_GROUND_BLOCK_X; bx++) {
            for (uz = 0; uz < PC_EXT_UNITS; uz++) {
                for (ux = 0; ux < PC_EXT_UNITS; ux++) {
                    n += pc_ext_ground_get(bx, bz, ux, uz) != 0;
                }
            }
        }
    }
    mp_wr_u32(w, n);
    for (bz = 0; bz < PC_EXT_GROUND_BLOCK_Z; bz++) {
        for (bx = 0; bx < PC_EXT_GROUND_BLOCK_X; bx++) {
            for (uz = 0; uz < PC_EXT_UNITS; uz++) {
                for (ux = 0; ux < PC_EXT_UNITS; ux++) {
                    u32 id = pc_ext_ground_get(bx, bz, ux, uz);
                    if (id != 0) {
                        mp_wr_u8(w, (u8)bx), mp_wr_u8(w, (u8)bz), mp_wr_u8(w, (u8)ux), mp_wr_u8(w, (u8)uz);
                        mp_wr_u32(w, id);
                    }
                }
            }
        }
    }
    n = 0;
    pc_oftr_export(count_ftr, &n);
    mp_wr_u32(w, n);
    pc_oftr_export(write_ftr, w);
}

/* the visitor's checked copy, applied after arriving */
static struct {
    int pat_n;
    u32 pat_id[EXTRA_PATTERNS_MAX];
    mNW_original_design_c pat[EXTRA_PATTERNS_MAX];
    int ground_n;
    struct {
        u8 bx, bz, ux, uz;
        u32 id;
    } ground[EXTRA_GROUND_MAX];
    int ftr_n;
    struct {
        u16 ut;
        u8 layer;
        u16 item;
    } ftr[EXTRA_FTR_MAX];
} l_extras;

static int unpack_extras(mp_rd* r) {
    u32 n, i;
    int j;

    l_extras.pat_n = 0, l_extras.ground_n = 0, l_extras.ftr_n = 0;
    n = mp_rd_u16(r);
    if (n > EXTRA_PATTERNS_MAX) {
        return FALSE;
    }
    for (i = 0; i < n && !r->bad; i++) {
        mNW_original_design_c* d = &l_extras.pat[l_extras.pat_n];
        u32 id = mp_rd_u32(r);
        memset(d, 0, sizeof(*d));
        rd_text(r, d->name, mNW_ORIGINAL_DESIGN_NAME_LEN);
        d->palette = mp_rd_u8(r) % mNW_PALETTE_NUM;
        mp_rd_bytes(r, d->design.data, mNW_DESIGN_TEX_SIZE); /* pixels: any value is a colour */
        d->flag_design_set = TRUE;
        if (id != 0) {
            l_extras.pat_id[l_extras.pat_n++] = id;
        }
    }
    n = mp_rd_u32(r);
    if (n > EXTRA_GROUND_MAX) {
        return FALSE;
    }
    for (i = 0; i < n && !r->bad; i++) {
        u8 bx = mp_rd_u8(r), bz = mp_rd_u8(r), ux = mp_rd_u8(r), uz = mp_rd_u8(r);
        u32 id = mp_rd_u32(r);
        int known = FALSE;
        for (j = 0; j < l_extras.pat_n && !known; j++) {
            known = l_extras.pat_id[j] == id;
        }
        if (known && bx < PC_EXT_GROUND_BLOCK_X && bz < PC_EXT_GROUND_BLOCK_Z && ux < PC_EXT_UNITS &&
            uz < PC_EXT_UNITS) {
            l_extras.ground[l_extras.ground_n].bx = bx, l_extras.ground[l_extras.ground_n].bz = bz;
            l_extras.ground[l_extras.ground_n].ux = ux, l_extras.ground[l_extras.ground_n].uz = uz;
            l_extras.ground[l_extras.ground_n].id = id;
            l_extras.ground_n++;
        }
    }
    n = mp_rd_u32(r);
    if (n > EXTRA_FTR_MAX) {
        return FALSE;
    }
    for (i = 0; i < n && !r->bad; i++) {
        u16 ut = mp_rd_u16(r);
        u8 layer = mp_rd_u8(r);
        u16 item = mp_rd_u16(r);
        /* checked again by pc_oftr_visit_set, like the save's own furniture */
        if (ut < PC_OFTR_UT_TOTAL_NUM && layer < PC_OFTR_LAYER_NUM && pc_mp_room_item_ok(item)) {
            l_extras.ftr[l_extras.ftr_n].ut = ut;
            l_extras.ftr[l_extras.ftr_n].layer = layer;
            l_extras.ftr[l_extras.ftr_n].item = item;
            l_extras.ftr_n++;
        }
    }
    return !r->bad;
}

void pc_mp_town_apply_extras(void) {
    int i;
    for (i = 0; i < l_extras.pat_n; i++) {
        pc_pattern_visit_add(l_extras.pat_id[i], &l_extras.pat[i]);
    }
    for (i = 0; i < l_extras.ground_n; i++) {
        pc_ext_ground_set(l_extras.ground[i].bx, l_extras.ground[i].bz, l_extras.ground[i].ux, l_extras.ground[i].uz,
                          l_extras.ground[i].id);
    }
    pc_oftr_visit_clear();
    for (i = 0; i < l_extras.ftr_n; i++) {
        pc_oftr_visit_set(l_extras.ftr[i].ut, l_extras.ftr[i].layer, l_extras.ftr[i].item);
    }
    printf("[mp] town extras: %d patterns, %d pattern units, %d furniture units\n", l_extras.pat_n,
           l_extras.ground_n, l_extras.ftr_n);
}

/* the visited town's door designs: drawn on its houses' doors, nothing more (the visitor's patterns stay theirs) */
static struct {
    int has;
    u8 palette;
    u8 tex[mNW_DESIGN_TEX_SIZE];
} l_doors[PLAYER_NUM];

int pc_mp_town_door(int home, u8* palette, u8** tex) {
    if (home < 0 || home >= PLAYER_NUM || !l_doors[home].has) {
        return FALSE;
    }
    *palette = l_doors[home].palette;
    *tex = l_doors[home].tex;
    return TRUE;
}

static int town_pack(u8* out, int cap) {
    mp_wr w;
    int i, j, k, x, z;
    const Save_t* s = Common_GetPointer(save.save);

    mp_wr_init(&w, out, (size_t)cap);
    mp_wr_u16(&w, TOWN_VERSION);
    mp_wr_bytes(&w, s->land_info.name, LAND_NAME_SIZE);
    mp_wr_u16(&w, s->land_info.id);

    for (i = 0; i < PLAYER_NUM; i++) {
        mp_wr_u8(&w, s->private_data[i].exists ? 1 : 0);
        wr_pid(&w, &s->private_data[i].player_ID);
        mp_wr_u8(&w, s->private_data[i].gender);
    }

    mp_wr_u8(&w, s->now_npc_max);
    mp_wr_u8(&w, s->station_type);
    mp_wr_u8(&w, s->bg_tex_idx);
    mp_wr_u8(&w, s->weather);
    mp_wr_u16(&w, s->fruit);
    mp_wr_u64(&w, s->melody);
    mp_wr_u8(&w, s->num_statues);
    mp_wr_u8(&w, s->house_arrangement);
    mp_wr_bytes(&w, s->keep_house_size, PLAYER_NUM);
    mp_wr_u8(&w, s->bridge.block_x);
    mp_wr_u8(&w, s->bridge.block_z);
    mp_wr_u8(&w, s->bridge.exists);

    mp_wr_u8(&w, s->shop.shop_info.shop_level);
    mp_wr_u8(&w, s->shop.shop_info.paint_color);
    for (i = 0; i < mSP_GOODS_COUNT; i++) {
        mp_wr_u16(&w, s->shop.items[i]);
    }
    mp_wr_u16(&w, s->shop.rare_item);
    mp_wr_bytes(&w, &s->museum_display, sizeof(s->museum_display));

    for (z = 0; z < BLOCK_Z_NUM; z++) {
        for (x = 0; x < BLOCK_X_NUM; x++) {
            mp_wr_u16(&w, s->combi_table[z][x].combination_type);
            mp_wr_u8(&w, s->combi_table[z][x].height);
        }
    }
    for (z = 0; z < FG_BLOCK_Z_NUM; z++) {
        for (x = 0; x < FG_BLOCK_X_NUM; x++) {
            for (k = 0; k < UT_TOTAL_NUM; k++) {
                mp_wr_u16(&w, s->fg[z][x].items[k / UT_X_NUM][k % UT_X_NUM]);
            }
        }
    }
    for (i = 0; i < FG_BLOCK_X_NUM * FG_BLOCK_Z_NUM; i++) {
        for (k = 0; k < UT_Z_NUM; k++) {
            mp_wr_u16(&w, s->deposit[i][k]);
        }
    }

    for (i = 0; i < PLAYER_NUM; i++) {
        const mHm_hs_c* h = &s->homes[i];
        wr_pid(&w, &h->ownerID);
        mp_wr_u8(&w, h->size_info.size);
        mp_wr_u8(&w, h->size_info.statue_rank);
        mp_wr_u8(&w, h->outlook_pal);
        mp_wr_u8(&w, h->flags.has_basement);
        for (j = 0; j < mHm_ROOM_NUM; j++) {
            const mHm_flr_c* f = &h->floors[j];
            mp_wr_u8(&w, f->wall_floor.wallpaper_idx);
            mp_wr_u8(&w, f->wall_floor.flooring_idx);
            mp_wr_u64(&w, f->layer_main.ftr_switch);
            for (k = 0; k < UT_TOTAL_NUM; k++) {
                mp_wr_u16(&w, f->layer_main.items[k / UT_X_NUM][k % UT_X_NUM]);
            }
            for (k = 0; k < UT_TOTAL_NUM; k++) {
                mp_wr_u16(&w, f->layer_secondary.items[k / UT_X_NUM][k % UT_X_NUM]);
            }
        }
        mp_wr_u32(&w, h->music_box[0]);
        mp_wr_u32(&w, h->music_box[1]);
        {
            /* the door's design, as the house draws it (ac_my_house_draw.c_inc) */
            int pl_no = mHS_get_pl_no_detail(i);
            if (h->door_original == 0xFF || pl_no < 0) {
                mp_wr_u8(&w, 0);
            } else {
                int org = h->door_original & 7;
                u8 pal = s->private_data[pl_no].my_org[org].palette;
                const u8* tex = s->private_data[pl_no].my_org[org].design.data;
                const mNW_original_design_c* kept = pc_pattern_use(PC_PAT_USE_DOOR, (u32)i, -1, org);
                if (kept != NULL) {
                    pal = kept->palette;
                    tex = kept->design.data;
                }
                mp_wr_u8(&w, 1);
                mp_wr_u8(&w, pal);
                mp_wr_bytes(&w, tex, mNW_DESIGN_TEX_SIZE);
                if (getenv("PC_MP_DEBUG") != NULL) {
                    printf("[mpdbg] door %d: player %d design %d (%s) pal %d bytes %02X%02X%02X%02X%02X%02X%02X%02X\n", i,
                           pl_no, org, kept != NULL ? "kept" : "own", pal, tex[0], tex[1], tex[2], tex[3], tex[64],
                           tex[65], tex[128], tex[129]);
                }
            }
        }
    }

    for (i = 0; i < ANIMAL_NUM_MAX; i++) {
        const Animal_c* a = &s->animals[i];
        mp_wr_u16(&w, a->id.npc_id);
        mp_wr_u8(&w, a->home_info.block_x);
        mp_wr_u8(&w, a->home_info.block_z);
        mp_wr_u8(&w, a->home_info.ut_x);
        mp_wr_u8(&w, a->home_info.ut_z);
        mp_wr_u16(&w, a->cloth);
        mp_wr_u8(&w, a->umbrella_id);
        mp_wr_bytes(&w, a->catchphrase, ANIMAL_CATCHPHRASE_LEN);
        mp_wr_u8(&w, a->is_home);
        mp_wr_u8(&w, a->moved_in);
    }
    mp_wr_u8(&w, s->remove_animal_idx);
    pack_extras(&w);
    return w.bad ? 0 : (int)w.pos;
}

/* --- visitor --- */

int pc_mp_town_unpack(const u8* in, int len, Save_t* s) {
    mp_rd r;
    int i, j, k, x, z;
    int bad_fg = 0, bad_room = 0;
    u16 land_id;
    u8 land_name[LAND_NAME_SIZE];

    mp_rd_init(&r, in, (size_t)len);
    if (mp_rd_u16(&r) != TOWN_VERSION) {
        return 0;
    }
    rd_text(&r, land_name, LAND_NAME_SIZE);
    land_id = mp_rd_u16(&r);
    if (r.bad || !mLd_CHECK_LAND_ID(land_id)) {
        return 0;
    }
    memcpy(s->land_info.name, land_name, LAND_NAME_SIZE);
    s->land_info.id = land_id;
    s->land_info.exists = TRUE;
    s->save_check.land_id = land_id;

    /* residents: names only */
    for (i = 0; i < PLAYER_NUM; i++) {
        Private_c* p = &s->private_data[i];
        u8 exists = mp_rd_u8(&r);
        mPr_ClearPrivateInfo(p);
        rd_pid(&r, &p->player_ID);
        p->gender = mp_rd_u8(&r) ? mPr_SEX_FEMALE : mPr_SEX_MALE;
        p->exists = exists ? TRUE : FALSE;
        if (!exists) {
            mPr_ClearPrivateInfo(p);
        }
    }

    s->now_npc_max = mp_rd_u8(&r);
    if (s->now_npc_max > ANIMAL_NUM_MAX) {
        s->now_npc_max = ANIMAL_NUM_MAX;
    }
    s->station_type = mp_rd_u8(&r) % 15;
    s->bg_tex_idx = mp_rd_u8(&r) % mFM_BG_TEX_NUM;
    s->weather = mp_rd_u8(&r);
    if (mEnv_SAVE_GET_WEATHER_TYPE(s->weather) >= mEnv_WEATHER_NUM ||
        mEnv_SAVE_GET_WEATHER_INTENSITY(s->weather) >= mEnv_WEATHER_INTENSITY_NUM) {
        s->weather = 0; /* type (high nibble) and intensity (low) as the game has them; else clear */
    }
    s->fruit = mp_rd_u16(&r);
    if (s->fruit < ITM_FOOD_START || s->fruit >= ITM_FOOD_START + FRUIT_NUM) {
        s->fruit = ITM_FOOD_APPLE;
    }
    s->melody = mp_rd_u64(&r); /* 16 notes, any value is a note */
    s->num_statues = mp_rd_u8(&r);
    if (s->num_statues > 3) {
        s->num_statues = 3; /* ac_npc_shop_common.c never counts past 3 */
    }
    s->house_arrangement = mp_rd_u8(&r);
    {
        /* which house each player has: four 2-bit fields, each house once (mHS_get_pl_no), else in order */
        int seen = 0;
        for (i = 0; i < PLAYER_NUM; i++) {
            seen |= 1 << ((s->house_arrangement >> (i * 2)) & 3);
        }
        if (seen != 0xF) {
            s->house_arrangement = 0 | (1 << 2) | (2 << 4) | (3 << 6);
        }
    }
    mp_rd_bytes(&r, s->keep_house_size, PLAYER_NUM);
    for (i = 0; i < PLAYER_NUM; i++) {
        /* size | next size << 3 | renew << 6 (m_home.c) */
        u8 k = s->keep_house_size[i];
        if ((k & 7) > mHm_HOMESIZE_UPPER || ((k >> 3) & 7) > mHm_HOMESIZE_STATUE || (k >> 6) > 1) {
            s->keep_house_size[i] = 0;
        }
    }
    s->bridge.block_x = mp_rd_u8(&r);
    s->bridge.block_z = mp_rd_u8(&r);
    s->bridge.exists = mp_rd_u8(&r) ? 1 : 0;
    s->bridge.pending = 0;
    if (s->bridge.block_x >= BLOCK_X_NUM || s->bridge.block_z >= BLOCK_Z_NUM) {
        s->bridge.exists = 0, s->bridge.block_x = 0, s->bridge.block_z = 0;
    }

    s->shop.shop_info.shop_level = mp_rd_u8(&r) & 3;
    s->shop.shop_info.paint_color = mp_rd_u8(&r) % 12;
    for (i = 0; i < mSP_GOODS_COUNT; i++) {
        mActor_name_t it = mp_rd_u16(&r);
        s->shop.items[i] = pc_mp_shop_item_ok(it) ? it : EMPTY_NO;
    }
    s->shop.rare_item = mp_rd_u16(&r);
    if (!pc_mp_shop_item_ok(s->shop.rare_item)) {
        s->shop.rare_item = EMPTY_NO;
    }
    mp_rd_bytes(&r, &s->museum_display, sizeof(s->museum_display));
    pc_mp_museum_clean((u8*)&s->museum_display, sizeof(s->museum_display));

    for (z = 0; z < BLOCK_Z_NUM; z++) {
        for (x = 0; x < BLOCK_X_NUM; x++) {
            u16 type = mp_rd_u16(&r);
            u8 height = mp_rd_u8(&r);
            if (type >= (u16)data_combi_table_number || height > 2) {
                return 0; /* a broken map can't be fixed up */
            }
            s->combi_table[z][x].combination_type = type;
            s->combi_table[z][x].height = height;
        }
    }
    for (z = 0; z < FG_BLOCK_Z_NUM; z++) {
        for (x = 0; x < FG_BLOCK_X_NUM; x++) {
            for (k = 0; k < UT_TOTAL_NUM; k++) {
                mActor_name_t it = mp_rd_u16(&r);
                if (!pc_mp_fg_item_ok(it)) {
                    if (bad_fg++ < 8) {
                        printf("[mp] town: ground item %04X replaced\n", it);
                    }
                    it = EMPTY_NO;
                }
                s->fg[z][x].items[k / UT_X_NUM][k % UT_X_NUM] = it;
            }
        }
    }
    for (i = 0; i < FG_BLOCK_X_NUM * FG_BLOCK_Z_NUM; i++) {
        for (k = 0; k < UT_Z_NUM; k++) {
            s->deposit[i][k] = mp_rd_u16(&r); /* bit flags, any value */
        }
    }

    for (i = 0; i < PLAYER_NUM; i++) {
        mHm_hs_c* h = &s->homes[i];
        memset(h, 0, sizeof(*h));
        mMl_clear_mail_box(h->mailbox, HOME_MAILBOX_SIZE); /* empty (all-zero letters read as mail) */
        rd_pid(&r, &h->ownerID);
        h->size_info.size = mp_rd_u8(&r);
        if (h->size_info.size > mHm_HOMESIZE_UPPER) {
            h->size_info.size = mHm_HOMESIZE_UPPER; /* STATUE is only ever a next size; the house actor has 4 */
        }
        h->size_info.next_size = h->size_info.size;
        h->size_info.statue_rank = mp_rd_u8(&r) & 3;
        h->outlook_pal = mp_rd_u8(&r) % mHm_OUTLOOK_PAL_NUM;
        h->ordered_outlook_pal = h->outlook_pal;
        h->next_outlook_pal = h->outlook_pal;
        h->flags.has_basement = mp_rd_u8(&r) ? 1 : 0;
        for (j = 0; j < mHm_ROOM_NUM; j++) {
            mHm_flr_c* f = &h->floors[j];
            f->wall_floor.wallpaper_idx = mp_rd_u8(&r);
            f->wall_floor.flooring_idx = mp_rd_u8(&r);
            if (f->wall_floor.wallpaper_idx >= WALL_NUM) {
                f->wall_floor.wallpaper_idx = 0;
            }
            if (f->wall_floor.flooring_idx >= CARPET_NUM) {
                f->wall_floor.flooring_idx = 0;
            }
            f->layer_main.ftr_switch = mp_rd_u64(&r);
            for (k = 0; k < UT_TOTAL_NUM; k++) {
                mActor_name_t it = mp_rd_u16(&r);
                if (!pc_mp_room_item_ok(it) && bad_room++ < 8) {
                    printf("[mp] town: room item %04X replaced\n", it);
                }
                f->layer_main.items[k / UT_X_NUM][k % UT_X_NUM] = pc_mp_room_item_ok(it) ? it : EMPTY_NO;
            }
            for (k = 0; k < UT_TOTAL_NUM; k++) {
                mActor_name_t it = mp_rd_u16(&r);
                if (!pc_mp_room_item_ok(it) && bad_room++ < 8) {
                    printf("[mp] town: room item %04X replaced\n", it);
                }
                f->layer_secondary.items[k / UT_X_NUM][k % UT_X_NUM] = pc_mp_room_item_ok(it) ? it : EMPTY_NO;
            }
        }
        h->music_box[0] = mp_rd_u32(&r);
        h->music_box[1] = mp_rd_u32(&r);
        l_doors[i].has = mp_rd_u8(&r) != 0;
        if (l_doors[i].has) {
            l_doors[i].palette = mp_rd_u8(&r) % mNW_PALETTE_NUM;
            mp_rd_bytes(&r, l_doors[i].tex, mNW_DESIGN_TEX_SIZE); /* pixels: any value is a colour */
        }
        h->door_original = l_doors[i].has ? 0 : 0xFF; /* a design or the plain mark; the design is l_doors' */
        if (getenv("PC_MP_DEBUG") != NULL && l_doors[i].has) {
            printf("[mpdbg] door %d received: pal %d bytes %02X%02X%02X%02X%02X%02X%02X%02X\n", i, l_doors[i].palette,
                   l_doors[i].tex[0], l_doors[i].tex[1], l_doors[i].tex[2], l_doors[i].tex[3], l_doors[i].tex[64],
                   l_doors[i].tex[65], l_doors[i].tex[128], l_doors[i].tex[129]);
        }
    }

    for (i = 0; i < ANIMAL_NUM_MAX; i++) {
        Animal_c* a = &s->animals[i];
        mActor_name_t npc = mp_rd_u16(&r);
        u8 bx = mp_rd_u8(&r), bz = mp_rd_u8(&r), ux = mp_rd_u8(&r), uz = mp_rd_u8(&r);
        mActor_name_t cloth = mp_rd_u16(&r);
        u8 umbrella = mp_rd_u8(&r);
        u8 phrase[ANIMAL_CATCHPHRASE_LEN];
        u8 is_home, moved_in;

        rd_text(&r, phrase, ANIMAL_CATCHPHRASE_LEN);
        is_home = mp_rd_u8(&r);
        moved_in = mp_rd_u8(&r);
        mNpc_ClearAnimalInfo(a);
        printf("[mp] town: villager %d = %04X at %d,%d %d,%d\n", i, npc, bx, bz, ux, uz);
        if (ITEM_NAME_GET_TYPE(npc) != NAME_TYPE_NPC || (npc & 0xFFF) >= NPC_NUM || bx >= BLOCK_X_NUM ||
            bz >= BLOCK_Z_NUM || ux >= UT_X_NUM || uz >= UT_Z_NUM) {
            continue; /* an empty house slot, or something we won't use */
        }
        mNpc_pc_SetDefAnimal(a, npc);
        a->id.land_id = land_id;
        memcpy(a->id.land_name, land_name, LAND_NAME_SIZE);
        a->home_info.block_x = bx, a->home_info.block_z = bz;
        a->home_info.ut_x = ux, a->home_info.ut_z = uz;
        if (ITEM_IS_CLOTH(cloth)) {
            a->cloth = cloth;
        }
        a->cloth_original_id = 0xFF;
        if (umbrella < UMBRELLA_NUM) {
            a->umbrella_id = umbrella;
        }
        memcpy(a->catchphrase, phrase, ANIMAL_CATCHPHRASE_LEN);
        a->is_home = is_home ? TRUE : FALSE;
        a->moved_in = moved_in ? TRUE : FALSE;
    }
    s->remove_animal_idx = mp_rd_u8(&r);
    if (s->remove_animal_idx >= ANIMAL_NUM_MAX) {
        s->remove_animal_idx = 0xFF;
    }
    printf("[mp] town %.8s: %d ground and %d room items replaced\n", land_name, bad_fg, bad_room);
    return unpack_extras(&r) && !r.bad && r.pos == r.len;
}
