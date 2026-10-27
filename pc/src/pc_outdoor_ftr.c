/* pc_outdoor_ftr.c - furniture placed outdoors in the town, New Horizons style.
 *
 * The town gets the same furniture actor (ac_my_room.c) as a room, run in "field mode": instead of the room's
 * one 16 x 16 block of fg, its grid is the whole town map, kept here and saved as OFTR in the ac-plus save.
 * Every unit a piece stands on is RSV_NO ("reserved, no collision") in the town's own fg, so nothing grows,
 * drops or gets buried under it; the piece brings its own collision. A save opened by a build without this
 * keeps the chunk (pc_ext_save.c keeps unknown chunks) and just shows those units as empty ground.
 * Only in the home town: the island and visited towns have their own fg. */
#include "pc_outdoor_ftr.h"
#include "m_common_data.h"
#include "m_field_info.h"
#include "m_field_make.h"
#include "m_collision_bg.h"
#include "m_name_table.h"
#include "m_room_type.h"
#include "m_ftr_def.h"
#include "m_land.h"
#include "m_event.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The most an acre holds; PC_OFTR_ACRE_MAX overrides it (dev: stress tests) */
#define OFTR_ACRE_MAX 128 /* half an acre's units; the Deck keeps 60 fps with the acres around full */

typedef struct {
    u16 ut;
    u8 layer;
    u8 pad;
    u16 item;
    u16 pad2;
} oftr_entry_c;

static mActor_name_t l_fg[PC_OFTR_LAYER_NUM][PC_OFTR_UT_TOTAL_NUM];
/* What the furniture actor sees outdoors anywhere else (the island, a visited town) */
static mActor_name_t l_none[PC_OFTR_LAYER_NUM][PC_OFTR_UT_TOTAL_NUM];
static u16 l_place[2][PC_OFTR_UT_TOTAL_NUM];
/* Which way items lying on furniture face (a room keeps this in its shop goods actor; not saved there either) */
static s16 l_angle[PC_OFTR_UT_TOTAL_NUM];

s16 pc_oftr_angle(int ut) {
    return ut >= 0 && ut < PC_OFTR_UT_TOTAL_NUM ? l_angle[ut] : 0;
}

void pc_oftr_set_angle(int ut, s16 angle) {
    if (ut >= 0 && ut < PC_OFTR_UT_TOTAL_NUM) {
        l_angle[ut] = angle;
    }
}

int pc_oftr_here(void) {
    return Common_Get(field_type) == mFI_FIELDTYPE2_FG && mFI_GetFieldId() == mFI_FIELD_FG &&
           !mFI_CheckInIsland() && !mLd_PlayerManKindCheck() && mEv_CheckTitleDemo() == mEv_TITLEDEMO_NONE;
}

int pc_oftr_acre_max(void) {
    static int max = 0;

    if (max == 0) {
        const char* env = getenv("PC_OFTR_ACRE_MAX");

        max = env != NULL && atoi(env) > 0 ? atoi(env) : OFTR_ACRE_MAX;
    }
    return max;
}

int pc_oftr_loaded_max(void) {
    return 9 * pc_oftr_acre_max() + 16; /* and a few for pieces on top of others placed past the count */
}

int pc_oftr_acre_count(int ut) {
    int x0 = (ut % PC_OFTR_UT_X_NUM) & ~(UT_X_NUM - 1);
    int z0 = (ut / PC_OFTR_UT_X_NUM) & ~(UT_Z_NUM - 1);
    int count = 0;
    int layer;
    int x;
    int z;

    if (ut < 0 || ut >= PC_OFTR_UT_TOTAL_NUM) {
        return 0;
    }
    for (layer = 0; layer < 2; layer++) {
        for (z = z0; z < z0 + UT_Z_NUM; z++) {
            for (x = x0; x < x0 + UT_X_NUM; x++) {
                count += ITEM_IS_FTR(l_fg[layer][x + z * PC_OFTR_UT_X_NUM]);
            }
        }
    }
    return count;
}

int pc_oftr_acre_full(int ut) {
    return pc_oftr_acre_count(ut) >= pc_oftr_acre_max();
}

mActor_name_t* pc_oftr_fg(int layer) {
    if (layer < 0 || layer >= PC_OFTR_LAYER_NUM) {
        return NULL;
    }
    if (!pc_oftr_here()) {
        return l_none[layer];
    }
    return l_fg[layer];
}

mActor_name_t* pc_oftr_home_fg(int layer) {
    return layer >= 0 && layer < PC_OFTR_LAYER_NUM ? l_fg[layer] : NULL;
}

mActor_name_t* pc_oftr_grid(int home, int layer) {
    if (layer < 0 || layer >= PC_OFTR_LAYER_NUM) {
        return NULL;
    }
    return home ? l_fg[layer] : l_none[layer];
}

unsigned short* pc_oftr_place_table(int layer) {
    return l_place[layer & 1];
}

static mActor_name_t* town_fg(int ut) {
    int ut_x = ut % PC_OFTR_UT_X_NUM;
    int ut_z = ut / PC_OFTR_UT_X_NUM;

    /* Only the town's own blocks have fg (not the border ring) */
    if (ut_x < UT_X_NUM || ut_x >= (BLOCK_X_NUM - 1) * UT_X_NUM || ut_z < UT_Z_NUM ||
        ut_z >= (BLOCK_Z_NUM - 1) * UT_Z_NUM) {
        return NULL;
    }
    return mFI_UtNum2UtFG(ut_x, ut_z);
}

void pc_oftr_unit_changed(int ut) {
    mActor_name_t* fg;

    if (ut < 0 || ut >= PC_OFTR_UT_TOTAL_NUM || !pc_oftr_here() || (fg = town_fg(ut)) == NULL) {
        return;
    }
    if (l_fg[0][ut] != EMPTY_NO) {
        if (*fg == EMPTY_NO) {
            *fg = RSV_NO;
        }
    } else if (*fg == RSV_NO) {
        *fg = EMPTY_NO;
    }
}

int pc_oftr_unit_free(int ut_x, int ut_z) {
    int ut = ut_x + ut_z * PC_OFTR_UT_X_NUM;
    mActor_name_t* fg;
    xyz_t center;

    if (!pc_oftr_here() || ut_x < 0 || ut_z < 0 || ut_x >= PC_OFTR_UT_X_NUM || ut_z >= PC_OFTR_UT_Z_NUM || (fg = town_fg(ut)) == NULL) {
        return FALSE;
    }
    if (!(*fg == EMPTY_NO || (*fg == RSV_NO && l_fg[0][ut] != EMPTY_NO))) {
        return FALSE;
    }
    /* Not next to a spot kept for a villager's house: building one clears the 3 x 3 around it */
    {
        int dx;
        int dz;

        for (dz = -1; dz <= 1; dz++) {
            for (dx = -1; dx <= 1; dx++) {
                mActor_name_t* near_fg = mFI_UtNum2UtFG(ut_x + dx, ut_z + dz);

                /* the spot's sign, or its stand-in while the sign is out (DUMMY_RESERVE) */
                if (near_fg != NULL && (mNT_IS_RESERVE(*near_fg) || *near_fg == DUMMY_RESERVE)) {
                    return FALSE;
                }
            }
        }
    }
    mFI_UtNum2CenterWpos(&center, ut_x, ut_z);
    center.y = 0.0f;
    /* The ground pattern rule (pc_ground_pattern.c): no water, waves, cliffs or river edges */
    return mCoBG_CheckPlace(center) && !mCoBG_CheckWave(&center) && !mCoBG_CheckGrassX(&center);
}

int pc_oftr_unit_walkable(int ut_x, int ut_z, f32 y) {
    mActor_name_t* fg;
    xyz_t center;

    if (ut_x < 0 || ut_z < 0 || ut_x >= PC_OFTR_UT_X_NUM || ut_z >= PC_OFTR_UT_Z_NUM ||
        (fg = town_fg(ut_x + ut_z * PC_OFTR_UT_X_NUM)) == NULL || !mCoBG_Unit2CheckNpc(ut_x, ut_z)) {
        return FALSE;
    }
    /* Nothing standing there: bare ground, flowers, weeds and things lying on the ground are fine */
    if (!(*fg == EMPTY_NO || *fg == RSV_NO || IS_ITEM_FLOWER(*fg) || IS_ITEM_GRASS(*fg) ||
          ITEM_NAME_GET_TYPE(*fg) == NAME_TYPE_ITEM1)) {
        return FALSE;
    }
    mFI_UtNum2CenterWpos(&center, ut_x, ut_z);
    return ABS(mCoBG_GetBgY_OnlyCenter_FromWpos2(center, 0.0f) - y) < 10.0f && !mCoBG_CheckWave(&center);
}

int pc_oftr_unit_still_ok(int ut) {
    int ut_x = ut % PC_OFTR_UT_X_NUM;
    int ut_z = ut / PC_OFTR_UT_X_NUM;
    mActor_name_t* fg = town_fg(ut);
    int dx;
    int dz;

    if (fg == NULL || !(*fg == EMPTY_NO || *fg == RSV_NO)) {
        return FALSE;
    }
    for (dz = -1; dz <= 1; dz++) {
        for (dx = -1; dx <= 1; dx++) {
            mActor_name_t* near_fg = mFI_UtNum2UtFG(ut_x + dx, ut_z + dz);

            if (near_fg != NULL && (mNT_IS_RESERVE(*near_fg) || *near_fg == DUMMY_RESERVE ||
                                    ITEM_IS_NPC_HOUSE(*near_fg))) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

void pc_oftr_unit_released(int ut) {
    mActor_name_t* fg;

    if (ut < 0 || ut >= PC_OFTR_UT_TOTAL_NUM || !pc_oftr_here() || l_fg[0][ut] != EMPTY_NO ||
        (fg = town_fg(ut)) == NULL || *fg != RSV_NO) {
        return;
    }
    if (pc_oftr_unit_still_ok(ut)) { /* nothing of a house's around it */
        *fg = EMPTY_NO;
    }
}

int pc_oftr_house_spot_taken(int bx, int bz, int ut_x, int ut_z) {
    int dx;
    int dz;

    for (dz = -1; dz <= 1; dz++) {
        for (dx = -1; dx <= 1; dx++) {
            int x = bx * UT_X_NUM + ut_x + dx;
            int z = bz * UT_Z_NUM + ut_z + dz;

            if (x >= 0 && z >= 0 && x < PC_OFTR_UT_X_NUM && z < PC_OFTR_UT_Z_NUM &&
                l_fg[0][x + z * PC_OFTR_UT_X_NUM] != EMPTY_NO) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

void pc_oftr_mark_town(void) {
    int ut;

    for (ut = 0; ut < PC_OFTR_UT_TOTAL_NUM; ut++) {
        if (l_fg[0][ut] != EMPTY_NO) {
            pc_oftr_unit_changed(ut);
        }
    }
}

void pc_oftr_reset(void) {
    memset(l_fg, 0, sizeof(l_fg));
    memset(l_angle, 0, sizeof(l_angle));
}

/* What a unit of the grid may hold: a piece of furniture the game has, a piece's other units, or an item on top.
 * Anything else (a damaged or hand-edited file, RSV_NO left by an item that was in the air) is dropped */
static int oftr_item_ok(mActor_name_t item) {
    if (ITEM_IS_FTR(item)) {
        int idx = mRmTp_FtrItemNo2FtrIdx(item);

        return idx >= 0 && idx < FTR_NUM;
    }
    return item == RSV_FE1F || ITEM_IS_ITEM1(item);
}

int pc_oftr_read_chunk(const char tag[4], void* fp, unsigned int size) {
    FILE* f = (FILE*)fp;
    oftr_entry_c e;
    unsigned int i;

    if (memcmp(tag, "OFTR", 4) != 0) {
        return FALSE;
    }
    pc_oftr_reset();
    if (size % sizeof(e) != 0) {
        fseek(f, (long)size, SEEK_CUR);
        return TRUE;
    }
    for (i = 0; i < size / sizeof(e); i++) {
        if (fread(&e, sizeof(e), 1, f) != 1) {
            pc_oftr_reset();
            return TRUE;
        }
        if (e.ut < PC_OFTR_UT_TOTAL_NUM && e.layer < PC_OFTR_LAYER_NUM && oftr_item_ok(e.item)) {
            l_fg[e.layer][e.ut] = e.item;
        }
    }
    return TRUE;
}

static int write_chunk(FILE* f);

int pc_oftr_write_chunk(void* fp) {
    extern void aMR_pc_items_to_fg(int on);
    int ok;

    extern int aSG_pc_field_dropping(void);
    extern void aSG_pc_field_flush(void);

    if (aSG_pc_field_dropping()) {
        aSG_pc_field_flush(); /* quitting mid-toss (pc_quick_save.c waited as long as it would): land it now */
    }
    aMR_pc_items_to_fg(TRUE); /* what the loaded pieces hold (a stereo's disc) */
    ok = write_chunk((FILE*)fp);
    aMR_pc_items_to_fg(FALSE);
    return ok;
}

static int write_chunk(FILE* f) {
    oftr_entry_c e;
    u32 count = 0;
    u32 size;
    int layer;
    int ut;

    for (layer = 0; layer < PC_OFTR_LAYER_NUM; layer++) {
        for (ut = 0; ut < PC_OFTR_UT_TOTAL_NUM; ut++) {
            count += l_fg[layer][ut] != EMPTY_NO && l_fg[layer][ut] != RSV_NO;
        }
    }
    size = count * sizeof(e);
    if (fwrite("OFTR", 1, 4, f) != 4 || fwrite(&size, 4, 1, f) != 1) {
        return FALSE;
    }
    memset(&e, 0, sizeof(e));
    for (layer = 0; layer < PC_OFTR_LAYER_NUM; layer++) {
        for (ut = 0; ut < PC_OFTR_UT_TOTAL_NUM; ut++) {
            if (l_fg[layer][ut] != EMPTY_NO && l_fg[layer][ut] != RSV_NO) { /* RSV_NO: an item still in the air */
                e.ut = (u16)ut;
                e.layer = (u8)layer;
                e.item = l_fg[layer][ut];
                if (fwrite(&e, sizeof(e), 1, f) != 1) {
                    return FALSE;
                }
            }
        }
    }
    return TRUE;
}

int pc_oftr_units_level(const int* uts, int count) {
    f32 y0 = 0.0f;
    int i;

    for (i = 0; i < count; i++) {
        xyz_t center;
        f32 y;

        if (uts[i] < 0 || uts[i] >= PC_OFTR_UT_TOTAL_NUM) {
            return FALSE;
        }
        mFI_UtNum2CenterWpos(&center, uts[i] % PC_OFTR_UT_X_NUM, uts[i] / PC_OFTR_UT_X_NUM);
        y = mCoBG_GetBgY_OnlyCenter_FromWpos2(center, 0.0f);
        if (i == 0) {
            y0 = y;
        } else if (ABS(y - y0) >= 10.0f) {
            return FALSE;
        }
    }
    return TRUE;
}

int pc_oftr_put(const int* uts, int count, mActor_name_t ftr) {
    int i;

    if (!ITEM_IS_FTR(ftr) || count <= 0 || !pc_oftr_units_level(uts, count)) {
        return FALSE;
    }
    for (i = 0; i < count; i++) {
        if (!pc_oftr_unit_free(uts[i] % PC_OFTR_UT_X_NUM, uts[i] / PC_OFTR_UT_X_NUM) || l_fg[0][uts[i]] != EMPTY_NO) {
            return FALSE;
        }
    }
    for (i = 0; i < count; i++) {
        l_fg[0][uts[i]] = i == 0 ? ftr : RSV_FE1F;
        pc_oftr_unit_changed(uts[i]);
    }
    return TRUE;
}

/* Online visits (pc_mp_town.c). The host: every unit of its outdoor furniture, as the save has it */
void pc_oftr_export(void (*fn)(void* user, int ut, int layer, mActor_name_t item), void* user) {
    extern void aMR_pc_items_to_fg(int on);
    int layer, ut;

    aMR_pc_items_to_fg(TRUE); /* what the loaded pieces hold (a stereo's disc) */
    for (layer = 0; layer < PC_OFTR_LAYER_NUM; layer++) {
        for (ut = 0; ut < PC_OFTR_UT_TOTAL_NUM; ut++) {
            if (l_fg[layer][ut] != EMPTY_NO && l_fg[layer][ut] != RSV_NO) {
                fn(user, ut, layer, l_fg[layer][ut]);
            }
        }
    }
    aMR_pc_items_to_fg(FALSE);
}

/* The visitor: the host's furniture goes in the grid another town's furniture actor uses */
void pc_oftr_visit_clear(void) {
    memset(l_none, 0, sizeof(l_none));
}

int pc_oftr_visit_set(int ut, int layer, mActor_name_t item) {
    if (ut < 0 || ut >= PC_OFTR_UT_TOTAL_NUM || layer < 0 || layer >= PC_OFTR_LAYER_NUM ||
        (item != EMPTY_NO && !oftr_item_ok(item))) {
        return FALSE;
    }
    l_none[layer][ut] = item;
    return TRUE;
}

/* online host: what changed in the town's outdoor furniture since the last look (placed, moved, taken away, put in
 * or taken out of a piece); the first look only takes note. A piece's other units (RSV_NO) count as empty. */
static mActor_name_t l_seen[PC_OFTR_LAYER_NUM][PC_OFTR_UT_TOTAL_NUM];
static int l_seen_valid;

void pc_oftr_changes(void (*fn)(void* user, int ut, int layer, mActor_name_t old_item, mActor_name_t new_item),
                     void* user) {
    extern void aMR_pc_items_to_fg(int on);
    int layer, ut;

    aMR_pc_items_to_fg(TRUE); /* what the loaded pieces hold (a stereo's disc) is still the town's: not a change */
    for (layer = 0; layer < PC_OFTR_LAYER_NUM; layer++) {
        for (ut = 0; ut < PC_OFTR_UT_TOTAL_NUM; ut++) {
            mActor_name_t now = l_fg[layer][ut] == RSV_NO ? EMPTY_NO : l_fg[layer][ut];
            if (l_seen_valid && now != l_seen[layer][ut]) {
                fn(user, ut, layer, l_seen[layer][ut], now);
            }
            l_seen[layer][ut] = now;
        }
    }
    aMR_pc_items_to_fg(FALSE);
    l_seen_valid = TRUE;
}

void pc_oftr_changes_reset(void) {
    l_seen_valid = FALSE;
}
