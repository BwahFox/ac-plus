/* pc_mp_world.c - keeping the town the same in every game during a visit (multiplayer, pc_mp.c).
 *
 * Each game compares the town's items (Save fg) and buried-item flags (Save deposit) with a copy from the last
 * frame. The host's own changes go to the visitors as they are. A visitor's change is only a request: the host
 * checks it (the visitor's permissions, the ids, and that what the visitor saw there is still there), then
 * applies it and tells everyone, or tells the visitor what is really there, which puts the unit back (and takes
 * back an item the visitor picked up that wasn't theirs to take).
 *
 * Buildings, signs and mailboxes on screen put placeholders on their units (DUMMY_*, RSV_NO) until the scene
 * ends; those differ between games, so a unit showing a placeholder keeps its last real value in the copy and is
 * never sent. Changes that came from the network are written into the copy too, so they aren't sent back.
 * The shop's goods are kept the same the same way: a sale turns an item into a "sold" mark (candy and grab bags
 * aren't goods: the shop counts how many are left, and a sale takes one from the count). So is the museum,
 * though only the host's players can donate (foreigners can't, as on the GameCube), and the houses' furniture,
 * which only the host rearranges (visitors leave it alone, ac_my_room.c aMR_CheckRoomOwner). */
#include "pc_mp_world.h"

#include <string.h>

#include "m_common_data.h"
#include "m_field_info.h"
#include "m_field_make.h"
#include "m_name_table.h"
#include "m_private.h"
#include "m_shop.h"
#include "m_house.h"
#include "m_museum_display.h"
#include "pc_mp_town.h"

#define FG_UNITS (FG_BLOCK_Z_NUM * FG_BLOCK_X_NUM * UT_TOTAL_NUM)
#define DEP_ROWS (FG_BLOCK_X_NUM * FG_BLOCK_Z_NUM * UT_Z_NUM)

static mActor_name_t l_fg_copy[FG_UNITS];
static u16 l_dep_copy[DEP_ROWS];
static mActor_name_t l_shop_copy[mSP_GOODS_COUNT + 2];
static u8 l_museum_copy[sizeof(mMmd_info_c)];
static int l_copy_valid;

#define MUSEUM_BYTES ((int)sizeof(mMmd_info_c))

static u8* museum_byte(int i) {
    return (u8*)&Save_Get(museum_display) + i;
}

/* every furniture unit of every house: (home, floor, layer main/secondary, unit) */
#define ROOM_LAYERS (PLAYER_NUM * mHm_ROOM_NUM * 2)
#define ROOM_UNITS (ROOM_LAYERS * UT_TOTAL_NUM)
static mActor_name_t l_room_copy[ROOM_UNITS];

static mActor_name_t* room_unit(int i) {
    int layer = i / UT_TOTAL_NUM, k = i % UT_TOTAL_NUM;
    mHm_flr_c* f = &Save_Get(homes[layer / (mHm_ROOM_NUM * 2)]).floors[(layer / 2) % mHm_ROOM_NUM];
    return (layer & 1) ? &f->layer_secondary.items[k / UT_X_NUM][k % UT_X_NUM]
                       : &f->layer_main.items[k / UT_X_NUM][k % UT_X_NUM];
}

/* unit i <-> the two numbers it's sent as (each under 128) */
static void room_ab(int i, int* a, int* b) {
    *a = (i / UT_TOTAL_NUM) * 2 + (i % UT_TOTAL_NUM) / 128;
    *b = i % 128;
}

static int room_index(int a, int b) {
    if (a < 0 || b < 0 || a >= ROOM_LAYERS * 2 || b >= 128) {
        return -1;
    }
    return (a / 2) * UT_TOTAL_NUM + (a % 2) * 128 + b;
}

#define SHOP_SLOTS (mSP_GOODS_COUNT + 2)
_Static_assert(PC_MP_SHOP_COUNTED == mSP_GOODS_COUNT + 1, "the counted slot follows the rare item");

/* slot i of the shop's goods; then the rare item; then PC_MP_SHOP_COUNTED, how many candies or grab bags are left */
static mActor_name_t* shop_slot(int i) {
    return i < mSP_GOODS_COUNT ? &Save_Get(shop).items[i] : &Save_Get(shop).rare_item;
}

static mActor_name_t shop_get(int i) {
    if (i == PC_MP_SHOP_COUNTED) {
        s8 n = Save_Get(shop).flowers_candy_grab_bag_count;
        return n > 0 ? (mActor_name_t)n : 0;
    }
    return *shop_slot(i);
}

static void shop_set(int i, mActor_name_t value) {
    if (i == PC_MP_SHOP_COUNTED) {
        Save_Get(shop).flowers_candy_grab_bag_count = (s8)value;
    } else {
        *shop_slot(i) = value;
    }
}

static mActor_name_t* fg_unit(int i) {
    return &Save_Get(fg[0][0]).items[0][0] + i; /* fg is one array of FG_UNITS ids */
}

static u16* dep_row(int i) {
    return &Save_Get(deposit[0][0]) + i;
}

/* index into fg of town unit (ux, uz), -1 outside the part of the town with items (the edges) */
static int unit_index(int ux, int uz) {
    int bx = ux / UT_X_NUM - 1, bz = uz / UT_Z_NUM - 1;
    if (ux < 0 || uz < 0 || bx < 0 || bz < 0 || bx >= FG_BLOCK_X_NUM || bz >= FG_BLOCK_Z_NUM) {
        return -1;
    }
    return ((bz * FG_BLOCK_X_NUM + bx) * UT_Z_NUM + uz % UT_Z_NUM) * UT_X_NUM + ux % UT_X_NUM;
}

static void index_unit(int i, int* ux, int* uz) {
    int block = i / UT_TOTAL_NUM, in = i % UT_TOTAL_NUM;
    *ux = (block % FG_BLOCK_X_NUM + 1) * UT_X_NUM + in % UT_X_NUM;
    *uz = (block / FG_BLOCK_X_NUM + 1) * UT_Z_NUM + in / UT_X_NUM;
}

static int is_placeholder(mActor_name_t n) {
    return n == RSV_NO || (n >= 0xF000 && n <= 0xF147);
}

void pc_mp_world_reset(void) {
    int i;
    for (i = 0; i < FG_UNITS; i++) {
        l_fg_copy[i] = *fg_unit(i);
    }
    memcpy(l_dep_copy, dep_row(0), sizeof(l_dep_copy));
    for (i = 0; i < SHOP_SLOTS; i++) {
        l_shop_copy[i] = shop_get(i);
    }
    memcpy(l_museum_copy, museum_byte(0), sizeof(l_museum_copy));
    for (i = 0; i < ROOM_UNITS; i++) {
        l_room_copy[i] = *room_unit(i);
    }
    l_copy_valid = TRUE;
}

void pc_mp_world_scan(pc_mp_world_change_fn fn, void* user) {
    int i;

    if (!l_copy_valid) {
        pc_mp_world_reset();
        return;
    }
    for (i = 0; i < FG_UNITS; i++) {
        mActor_name_t now = *fg_unit(i);
        if (now != l_fg_copy[i] && !is_placeholder(now) && !is_placeholder(l_fg_copy[i])) {
            int ux, uz;
            index_unit(i, &ux, &uz);
            fn(user, MP_WORLD_SET, ux, uz, l_fg_copy[i], now);
            l_fg_copy[i] = *fg_unit(i); /* (what's there now: fn may have put it back) */
        } else if (now != l_fg_copy[i] && !is_placeholder(now)) {
            l_fg_copy[i] = now; /* a placeholder went (scene change): nothing to send */
        }
    }
    for (i = 0; i < DEP_ROWS; i++) {
        u16 now = *dep_row(i);
        if (now != l_dep_copy[i]) {
            fn(user, MP_WORLD_DEPOSIT, i / UT_Z_NUM, i % UT_Z_NUM, l_dep_copy[i], now);
            l_dep_copy[i] = now;
        }
    }
    for (i = 0; i < SHOP_SLOTS; i++) {
        mActor_name_t now = shop_get(i);
        if (now != l_shop_copy[i]) {
            fn(user, MP_WORLD_SHOP, i, 0, l_shop_copy[i], now);
            l_shop_copy[i] = now;
        }
    }
    for (i = 0; i < MUSEUM_BYTES; i++) {
        u8 now = *museum_byte(i);
        if (now != l_museum_copy[i]) {
            fn(user, MP_WORLD_MUSEUM, i, 0, l_museum_copy[i], now);
            l_museum_copy[i] = now;
        }
    }
    for (i = 0; i < ROOM_UNITS; i++) {
        mActor_name_t now = *room_unit(i);
        if (now != l_room_copy[i]) {
            int a, b;
            room_ab(i, &a, &b);
            fn(user, MP_WORLD_ROOM, a, b, l_room_copy[i], now);
            l_room_copy[i] = now;
        }
    }
}

int pc_mp_world_get(int kind, int a, int b, u16* value) {
    int i;
    if (kind == MP_WORLD_SET) {
        i = unit_index(a, b);
        if (i < 0) {
            return FALSE;
        }
        *value = l_fg_copy[i]; /* the real item, not a placeholder */
        return TRUE;
    }
    if (kind == MP_WORLD_SHOP) {
        if (a < 0 || a >= SHOP_SLOTS || b != 0) {
            return FALSE;
        }
        *value = l_shop_copy[a];
        return TRUE;
    }
    if (kind == MP_WORLD_MUSEUM) {
        if (a < 0 || a >= MUSEUM_BYTES || b != 0) {
            return FALSE;
        }
        *value = l_museum_copy[a];
        return TRUE;
    }
    if (kind == MP_WORLD_ROOM) {
        i = room_index(a, b);
        if (i < 0) {
            return FALSE;
        }
        *value = l_room_copy[i];
        return TRUE;
    }
    if (a < 0 || b < 0 || a >= FG_BLOCK_X_NUM * FG_BLOCK_Z_NUM || b >= UT_Z_NUM) {
        return FALSE;
    }
    *value = l_dep_copy[a * UT_Z_NUM + b];
    return TRUE;
}

int pc_mp_world_apply(int kind, int a, int b, u16 value) {
    int i;
    if (kind == MP_WORLD_SET) {
        mActor_name_t* unit;
        i = unit_index(a, b);
        if (i < 0 || !pc_mp_fg_item_ok(value) || is_placeholder(value)) {
            return FALSE;
        }
        unit = fg_unit(i);
        l_fg_copy[i] = value;
        if (!is_placeholder(*unit)) {
            *unit = value; /* a unit under a building on screen gets it when the scene ends */
        }
        if (mFI_CheckFieldData()) {
            mFI_SetFGUpData();
        }
        return TRUE;
    }
    if (kind == MP_WORLD_SHOP) {
        if (a < 0 || a >= SHOP_SLOTS || b != 0 ||
            (a == PC_MP_SHOP_COUNTED ? value > 127 : !pc_mp_shop_item_ok(value))) {
            return FALSE;
        }
        {
            extern void pc_aSD_remote_sale(mActor_name_t item, int was_rare); /* ac_shop_design.c */
            mActor_name_t was = l_shop_copy[a];
            l_shop_copy[a] = value;
            shop_set(a, value); /* the shelves show it the next time someone goes in, */
            /* and the shop someone is in right now at once */
            if (a == PC_MP_SHOP_COUNTED) {
                for (; was > value; was--) {
                    pc_aSD_remote_sale(ITM_FOOD_CANDY, FALSE);
                }
            } else if (was != EMPTY_NO && pc_mp_item_ok(was) && pc_mp_shop_sold(value)) {
                pc_aSD_remote_sale(was, a == mSP_GOODS_COUNT);
            }
        }
        return TRUE;
    }
    if (kind == MP_WORLD_MUSEUM) {
        if (a < 0 || a >= MUSEUM_BYTES || b != 0 || value > 0xFF || !pc_mp_museum_byte_ok((u8)value)) {
            return FALSE;
        }
        l_museum_copy[a] = (u8)value;
        *museum_byte(a) = (u8)value;
        return TRUE;
    }
    if (kind == MP_WORLD_ROOM) {
        extern void pc_aMR_room_live(int home, int floor, int layer, int unit, mActor_name_t old_item,
                                     mActor_name_t new_item);
        mActor_name_t old_item;
        int layer;
        i = room_index(a, b);
        if (i < 0 || !pc_mp_room_item_ok(value)) {
            return FALSE;
        }
        old_item = *room_unit(i);
        l_room_copy[i] = value;
        *room_unit(i) = value; /* a room shows it the next time someone walks in, */
        layer = i / UT_TOTAL_NUM;
        if (old_item != value) {
            /* and the room someone is in right now at once */
            pc_aMR_room_live(layer / (mHm_ROOM_NUM * 2), (layer / 2) % mHm_ROOM_NUM, layer & 1, i % UT_TOTAL_NUM,
                             old_item, value);
        }
        return TRUE;
    }
    if (a < 0 || b < 0 || a >= FG_BLOCK_X_NUM * FG_BLOCK_Z_NUM || b >= UT_Z_NUM) {
        return FALSE;
    }
    i = a * UT_Z_NUM + b;
    l_dep_copy[i] = value;
    *dep_row(i) = value;
    return TRUE;
}

/* What an untrusted visitor may change: pick up, put down or swap items and furniture on open ground. Not trees,
 * flowers, holes or anything else of the town's (digging and chopping need the host's trust). */
/* a tree a guest may shake, and what it becomes: fruit, and the drops a town's trees get by chance - not money
 * trees, which the owner planted (bg_item_common.c_inc drop_fruit's table) */
static const u16 l_guest_shake[][2] = {
    { TREE_APPLE_FRUIT, TREE_APPLE_NOFRUIT_0 }, { TREE_ORANGE_FRUIT, TREE_ORANGE_NOFRUIT_0 },
    { TREE_PEACH_FRUIT, TREE_PEACH_NOFRUIT_0 }, { TREE_PEAR_FRUIT, TREE_PEAR_NOFRUIT_0 },
    { TREE_CHERRY_FRUIT, TREE_CHERRY_NOFRUIT_0 }, { TREE_PALM_FRUIT, TREE_PALM_NOFRUIT_0 },
    { TREE_FTR, TREE }, { TREE_BEES, TREE }, { TREE_PRESENT, TREE }, { TREE_BELLS, TREE },
    { CEDAR_TREE_BELLS, CEDAR_TREE }, { CEDAR_TREE_FTR, CEDAR_TREE }, { CEDAR_TREE_BEES, CEDAR_TREE },
    { GOLD_TREE_BELLS, GOLD_TREE }, { GOLD_TREE_FTR, GOLD_TREE }, { GOLD_TREE_BEES, GOLD_TREE },
};

int pc_mp_guest_may_shake(u16 tree) {
    int i;
    for (i = 0; i < (int)(sizeof(l_guest_shake) / sizeof(l_guest_shake[0])); i++) {
        if (l_guest_shake[i][0] == tree) {
            return TRUE;
        }
    }
    return FALSE;
}

/* a trusted visitor may do what playing does, nothing more (pc_mp_fg_natural_ok) */
/* is something buried at town unit (ux, uz) (Save deposit: a row of bits per acre, mFI_GetLineDeposit) */
static int buried(int ux, int uz) {
    int i = unit_index(ux, uz);
    if (i < 0) {
        return FALSE;
    }
    return (l_dep_copy[(i / UT_TOTAL_NUM) * UT_Z_NUM + (i % UT_TOTAL_NUM) / UT_X_NUM] >> (i % UT_X_NUM)) & 1;
}

int pc_mp_world_trusted_may(int kind, int a, int b, u16 old_value, u16 new_value) {
    if (kind == MP_WORLD_MUSEUM || kind == MP_WORLD_ROOM) {
        return FALSE; /* foreigners don't donate */
    }
    if (kind == MP_WORLD_SHOP) {
        return pc_mp_world_visitor_may(kind, a, b, old_value, new_value);
    }
    if (kind == MP_WORLD_DEPOSIT) {
        /* digging up and burying: only where playing can, on units with items, holes and plants (never a
         * building's) */
        int bit;
        if (a < 0 || b < 0 || a >= FG_BLOCK_X_NUM * FG_BLOCK_Z_NUM || b >= UT_Z_NUM) {
            return FALSE;
        }
        for (bit = 0; bit < UT_X_NUM; bit++) {
            if (((old_value ^ new_value) >> bit) & 1 &&
                !pc_mp_fg_natural_ok(l_fg_copy[a * UT_TOTAL_NUM + b * UT_X_NUM + bit])) {
                return FALSE;
            }
        }
        return TRUE;
    }
    return pc_mp_fg_natural_ok(old_value) && pc_mp_fg_natural_ok(new_value);
}

int pc_mp_world_visitor_may(int kind, int a, int b, u16 old_value, u16 new_value) {
    int i;
    if (kind == MP_WORLD_MUSEUM || kind == MP_WORLD_ROOM) {
        return FALSE; /* foreigners don't donate, visitors don't rearrange the host's houses */
    }
    if (kind == MP_WORLD_SHOP && a == PC_MP_SHOP_COUNTED) {
        return old_value > 0 && new_value == old_value - 1; /* buying a candy or a grab bag */
    }
    if (kind == MP_WORLD_SHOP) {
        return old_value != EMPTY_NO && pc_mp_item_ok(old_value) && pc_mp_shop_sold(new_value); /* buying it */
    }
    if (kind != MP_WORLD_SET) {
        return FALSE; /* buried things are dug up or buried: digging */
    }
    if (buried(a, b)) {
        return FALSE; /* taking a buried thing is digging it up */
    }
    if (IS_ITEM_GRASS(old_value) && new_value == EMPTY_NO) {
        return TRUE; /* pulling weeds */
    }
    for (i = 0; i < (int)(sizeof(l_guest_shake) / sizeof(l_guest_shake[0])); i++) {
        if (l_guest_shake[i][0] == old_value && l_guest_shake[i][1] == new_value) {
            return TRUE;
        }
    }
    return pc_mp_item_ok(old_value) && pc_mp_item_ok(new_value);
}

/* the host refused a purchase (someone bought it first): the item back, the bells back */
void pc_mp_world_undo_purchase(u16 item) {
    Private_c* priv = Now_Private;
    int idx;
    u32 price;
    if (priv == NULL || !pc_mp_item_ok(item) || item == EMPTY_NO) {
        return;
    }
    idx = mPr_GetPossessionItemIdx(priv, item);
    if (idx < 0 || idx >= mPr_POCKETS_SLOT_COUNT) {
        return; /* not in the pockets (sent home, say): nothing to give back for */
    }
    mPr_SetPossessionItem(priv, idx, EMPTY_NO, mPr_ITEM_COND_NORMAL);
    price = mSP_ItemNo2ItemPrice(item);
    priv->inventory.wallet = priv->inventory.wallet + price > 99999 ? 99999 : priv->inventory.wallet + price;
}

/* the host refused a pickup: take the item back out of the pockets */
void pc_mp_world_undo_pickup(u16 item) {
    Private_c* priv = Now_Private;
    int idx;
    if (priv == NULL || item == EMPTY_NO) {
        return;
    }
    idx = mPr_GetPossessionItemIdx(priv, item);
    if (idx >= 0 && idx < mPr_POCKETS_SLOT_COUNT) {
        mPr_SetPossessionItem(priv, idx, EMPTY_NO, mPr_ITEM_COND_NORMAL);
    }
}
