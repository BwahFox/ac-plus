/* pc_mp_items.c - which item ids from another player's game are safe to hold (multiplayer, pc_mp_town.c).
 * The game's own tables index these ids mostly without bounds checks (mNT_get_itemTableNo, bg_item's type tables,
 * the structure and birth-control tables), so each check below is the size of the table that the id indexes.
 * Bounds as of the USA disc's tables:
 *  - items (type 2): per category, the *_NUM counts (not the *_END macros, some run past the tables);
 *  - furniture: types 1 and 3, mRmTp_FtrItemNo2FtrIdx < FTR_NUM;
 *  - on the ground also: field objects 0x0000-0x0082 and 0x0800-0x0869 (bg_item's type tables), signboards,
 *    villager houses 0x5000-0x50ED and structures 0x5800-0x5852 (setupInfo_table), the two doors, move objects
 *    0x8000-0x8015 and props 0xA000-0xA012 (birth control's tables), building footprints 0xF001-0xF147, and the
 *    door and bridge markers RSV_DOOR..RSV_FE1F. */
#include "pc_mp_town.h"

#include "m_ftr_def.h"
#include "m_name_table.h"
#include "m_room_type.h"

static int item1_ok(mActor_name_t n) {
    static const u16 kinds[16] = {
        PAPER_NUM, MONEY_NUM, TOOL_NUM, FISH_NUM, CLOTH_NUM, ETC_NUM, CARPET_NUM, WALL_NUM,
        FRUIT_NUM, PLANT_NUM, MINIDISK_NUM, DIARY_NUM, TICKET_NUM, INSECT_NUM, HUKUBUKURO_NUM, KABU_NUM,
    };
    return ITEM_NAME_GET_TYPE(n) == NAME_TYPE_ITEM1 && (n & 0xFF) < kinds[(n >> 8) & 0xF];
}

static int ftr_ok(mActor_name_t n) {
    if (ITEM_IS_FTR(n)) {
        int idx = mRmTp_FtrItemNo2FtrIdx(n);
        return idx >= 0 && idx < FTR_NUM;
    }
    return FALSE;
}

int pc_mp_item_ok(mActor_name_t n) {
    return n == EMPTY_NO || item1_ok(n) || ftr_ok(n);
}

/* What playing can put on the ground or take off it: items and furniture, weeds, holes and buried pitfalls,
 * stumps, trees, saplings and flowers (and their chance drops). Never buildings, signs, boards, fences, rocks,
 * mailboxes, doors or placeholders - ids like those, put where they don't belong, are what broke towns in Wild
 * World ("brick seeds"). A visitor's change has to be one of these on both sides, even a trusted visitor's. */
int pc_mp_fg_natural_ok(mActor_name_t n) {
    if (pc_mp_item_ok(n)) {
        return TRUE;
    }
    return (n >= TREE_STUMP001 && n <= TREE_STUMP004) || IS_ITEM_GRASS(n) || (n >= HOLE_START && n <= HOLE_END) ||
           (n >= BURIED_PITFALL_HOLE_START && n <= BURIED_PITFALL_HOLE_END) || n == SHINE_SPOT || n == HOLE_SHINE ||
           (n >= TREE_BEES && n <= TREE_PRESENT) || n == FLOWER_SEED || n == TREE_BELLS ||
           (n >= TREE_PALM_STUMP001 && n <= CEDAR_TREE_STUMP004) || (n >= CEDAR_TREE_BELLS && n <= CEDAR_TREE_BEES) ||
           (n >= GOLD_TREE_STUMP001 && n <= GOLD_TREE_STUMP004) || (n >= GOLD_TREE_BELLS && n <= CEDAR_TREE_LIGHTS) ||
           (n >= ENV_START && n <= DEAD_GOLD_SAPLING);
}

/* the museum's exhibits: a 4-bit "who donated it" per exhibit, 0 = not yet, else one of the town's players */
int pc_mp_museum_byte_ok(u8 b) {
    return (b & 0xF) <= PLAYER_NUM && (b >> 4) <= PLAYER_NUM;
}

void pc_mp_museum_clean(u8* bytes, int n) {
    int i;
    for (i = 0; i < n; i++) {
        if (!pc_mp_museum_byte_ok(bytes[i])) {
            bytes[i] = 0;
        }
    }
}

/* the shop's goods: items, or a "sold" mark where one was (mSP_ShopSaleReport) */
int pc_mp_shop_item_ok(mActor_name_t n) {
    return pc_mp_item_ok(n) || pc_mp_shop_sold(n);
}

int pc_mp_shop_sold(mActor_name_t n) {
    return n >= RSV_SHOP_SOLD_PAPER && n <= RSV_SHOP_SOLD_SIGNBOARD;
}

/* rooms also hold their doors and stairs (DOOR0/1, EXIT_DOOR/1, RSV_DOOR) and wall markers */
int pc_mp_room_item_ok(mActor_name_t n) {
    return pc_mp_item_ok(n) || n == DOOR0 || n == DOOR1 || n == EXIT_DOOR || n == EXIT_DOOR1 || n == RSV_DOOR ||
           n == RSV_FE1F || n == RSV_WALL_NO || n == RSV_NO;
}

int pc_mp_fg_item_ok(mActor_name_t n) {
    if (pc_mp_item_ok(n)) {
        return TRUE;
    }
    return (n >= 0x0001 && n <= 0x0082) || (n >= 0x0800 && n <= 0x0869) || (n >= 0x0900 && n <= 0x0920) ||
           (n >= 0x5000 && n <= 0x50ED) || (n >= 0x5800 && n <= 0x5852) || n == DOOR0 || n == DOOR1 || n == EXIT_DOOR || n == EXIT_DOOR1 ||
           (n >= 0x8000 && n <= 0x8015) || (n >= 0xA000 && n <= 0xA012) || (n >= 0xF001 && n <= 0xF147) ||
           (n >= RSV_DOOR && n <= RSV_FE1F) || n == RSV_NO;
}
