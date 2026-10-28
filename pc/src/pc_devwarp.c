/* pc_devwarp.c - dev: PC_WARP_AT=F walks the player into a scene's door at frame F, and PC_TELEPORT_AT=F puts
 * them at PC_TELEPORT_POS=x,z[,degrees] in the scene they're in
 * (PC_WARP_SCENE: 1 = museum entrance, the default; 2 = observatory, 3 = café, 4-7 = house basement S/M/L/LL,
 * 8-11 = the house's main floor S/M/L/LL, 12 = the Nook shop,
 * PC_BASEMENT_TEST=wallpaper,carpet dressing them, PC_ROOM_TEST=wallpaper,carpet the main floors;
 * PC_WARP_POS=x,z: where to
 * arrive instead of the door) */
#include "m_play.h"
#include "m_scene.h"
#include "m_house.h"
#include "m_shop.h"
#include "m_museum_display.h"
#include "m_private.h"
#include "m_scene_table.h"
#include "m_common_data.h"
#include "m_player_lib.h"
#include "m_field_info.h"
#include "m_collision_bg.h"
#include "ac_my_room.h"
#include "m_room_type.h"
#include <stdio.h>

#include <stdlib.h>
#include <string.h>

void pc_dev_warp(void* playv, int which) {
    static Door_data_c museum = {
        SCENE_MUSEUM_ENTRANCE, mSc_DIRECT_NORTH, FALSE, 0, { 240, 0, 440 }, EMPTY_NO, 1, { 0, 0, 0 },
    };
    GAME_PLAY* play = (GAME_PLAY*)playv;
    const char* scene = getenv("PC_WARP_SCENE");
    Door_data_c door = museum;

    (void)which;
    if (scene != NULL && atoi(scene) == 2) {
        door.next_scene_id = SCENE_MUSEUM_OBSERVATORY;
        door.exit_position.x = 320, door.exit_position.z = 420;
    } else if (scene != NULL && atoi(scene) >= 4 && atoi(scene) <= 7) {
        const char* look = getenv("PC_BASEMENT_TEST"); /* wallpaper,carpet: put in every house's basement */

        door.next_scene_id = SCENE_MY_ROOM_BASEMENT_S + (atoi(scene) - 4);
        if (look != NULL && strchr(look, ',') != NULL) {
            extern void pc_basement_set_custom(int house, int what);
            int h;

            for (h = 0; h < PLAYER_NUM; h++) {
                Save_Get(homes[h]).floors[2].wall_floor.wallpaper_idx = (u8)atoi(look);
                Save_Get(homes[h]).floors[2].wall_floor.flooring_idx = (u8)atoi(strchr(look, ',') + 1);
                pc_basement_set_custom(h, 3);
            }
        }
        door.exit_orientation = mSc_DIRECT_SOUTH;
        door.exit_position.x = 160, door.exit_position.z = 160;
    } else if (scene != NULL && atoi(scene) >= 8 && atoi(scene) <= 11) {
        static const int floors[] = { SCENE_MY_ROOM_S, SCENE_MY_ROOM_M, SCENE_MY_ROOM_L, SCENE_MY_ROOM_LL1 };

        const char* look = getenv("PC_ROOM_TEST"); /* wallpaper,carpet: put on every house's main floor */

        door.next_scene_id = floors[atoi(scene) - 8];
        if (look != NULL && strchr(look, ',') != NULL) {
            int h;

            for (h = 0; h < PLAYER_NUM; h++) {
                Save_Get(homes[h]).floors[0].wall_floor.wallpaper_idx = (u8)atoi(look);
                Save_Get(homes[h]).floors[0].wall_floor.flooring_idx = (u8)atoi(strchr(look, ',') + 1);
            }
        }
        Common_Set(house_owner_name, mHS_get_arrange_idx(0)); /* the first player's house, as its front door says */
        door.exit_orientation = mSc_DIRECT_NORTH;
        door.exit_position.x = 160, door.exit_position.z = 200;
    } else if (scene != NULL && atoi(scene) == 12) {
        /* the town's Nook shop, whichever it is now, just inside its door */
        static const int shops[] = { SCENE_SHOP0, SCENE_CONVENI, SCENE_SUPER, SCENE_DEPART };
        static const s16 width[] = { 320, 440, 440, 480 };
        int level = mSP_GetShopLevel() & 3;

        door.next_scene_id = shops[level];
        door.exit_orientation = mSc_DIRECT_NORTH;
        door.exit_position.x = width[level] / 2, door.exit_position.z = width[level] - 80;
    } else if (scene != NULL && atoi(scene) == 13 && GET_PLAYER_ACTOR(play) != NULL) {
        /* the town again, right where the player stands (the scene is built anew) */
        door.next_scene_id = SCENE_FG;
        door.exit_orientation = mSc_DIRECT_SOUTH;
        door.exit_position.x = (s16)GET_PLAYER_ACTOR(play)->actor_class.world.position.x;
        door.exit_position.z = (s16)GET_PLAYER_ACTOR(play)->actor_class.world.position.z;
    } else if (scene != NULL && atoi(scene) == 3) {
        door.next_scene_id = SCENE_MUSEUM_CAFE;
        door.exit_orientation = mSc_DIRECT_WEST;
        door.exit_position.x = 460, door.exit_position.z = 400;
    }
    if ((scene = getenv("PC_WARP_POS")) != NULL && strchr(scene, ',') != NULL) {
        door.exit_position.x = (s16)atoi(scene);
        door.exit_position.z = (s16)atoi(strchr(scene, ',') + 1);
    }
    goto_other_scene(play, &door, TRUE);
}

void pc_dev_teleport(void* playv, const char* pos) {
    GAME_PLAY* play = (GAME_PLAY*)playv;
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);
    const char* c;

    if (player == NULL || pos == NULL || (c = strchr(pos, ',')) == NULL) {
        return;
    }
    player->actor_class.world.position.x = (f32)atof(pos);
    player->actor_class.world.position.z = (f32)atof(c + 1);
    if ((c = strchr(c + 1, ',')) != NULL) {
        player->actor_class.world.angle.y = DEG2SHORT_ANGLE((f32)atof(c + 1));
        player->actor_class.shape_info.rotation.y = player->actor_class.world.angle.y;
    }
}

/* The dev field commands only make sense in a play scene with its player (PC_CTL lines can come during boot) */
static PLAYER_ACTOR* dev_field_player(void* playv) {
    GAME* game = (GAME*)playv;

    if (game == NULL || game->exec != play_main || !mFI_CheckFieldData()) {
        return NULL;
    }
    return GET_PLAYER_ACTOR((GAME_PLAY*)game);
}

/* PC_CTL fg: put field item `item` on the unit (dx, dz) units from the player's, and log the units */
void pc_dev_fg_set(void* playv, int dx, int dz, int item) {
    PLAYER_ACTOR* player = dev_field_player(playv);
    int ut_x;
    int ut_z;

    if (player == NULL || !mFI_Wpos2UtNum(&ut_x, &ut_z, player->actor_class.world.position)) {
        return;
    }
    if (item >= 0) {
        mFI_UtNumtoFGSet_common((mActor_name_t)item, ut_x + dx, ut_z + dz, TRUE);
    }
    printf("[fg] player unit %d,%d; unit %d,%d holds %04X\n", ut_x, ut_z, ut_x + dx, ut_z + dz,
           mFI_UtNum2UtFG(ut_x + dx, ut_z + dz) != NULL ? *mFI_UtNum2UtFG(ut_x + dx, ut_z + dz) : 0xFFFF);
}

/* PC_CTL mtree: mark the unit (dx, dz) from the player's as a 90,000 Bells money tree, as burying the bag does */
void pc_dev_mtree(void* playv, int dx, int dz) {
    extern void pc_money_tree_plant(xyz_t pos);
    PLAYER_ACTOR* player = dev_field_player(playv);
    int ut_x;
    int ut_z;
    xyz_t pos;

    if (player != NULL && mFI_Wpos2UtNum(&ut_x, &ut_z, player->actor_class.world.position)) {
        mFI_UtNum2CenterWpos(&pos, ut_x + dx, ut_z + dz);
        pc_money_tree_plant(pos);
        printf("[fg] marked unit %d,%d as a 90,000 Bells tree\n", ut_x + dx, ut_z + dz);
    }
}

/* PC_CTL oftr: furniture item ftr outdoors on the unit (dx, dz) from the player's (pc_outdoor_ftr.c) */
void pc_dev_oftr(void* playv, int dx, int dz, int ftr) {
    extern int aMR_pc_dev_put(int ut_x, int ut_z, mActor_name_t ftr);
    PLAYER_ACTOR* player = dev_field_player(playv);
    int ut_x;
    int ut_z;

    if (player != NULL && mFI_Wpos2UtNum(&ut_x, &ut_z, player->actor_class.world.position)) {
        printf("[oftr] %04X on unit %d,%d: %s\n", ftr, ut_x + dx, ut_z + dz,
               aMR_pc_dev_put(ut_x + dx, ut_z + dz, (mActor_name_t)ftr) ? "ok" : "can't go there");
    }
}

/* PC_CTL place: furniture item ftr put down in front of the player the way Place from the pockets does it */
void pc_dev_place(void* playv, int ftr) {
    GAME* game = (GAME*)playv;
    int ux;
    int uz;
    int sq_ofs;
    int layer;
    u16 rot;
    u16 ftr_idx = mRmTp_FtrItemNo2FtrIdx((mActor_name_t)ftr);
    int idx;

    if (aMR_CLIP == NULL) {
        printf("[place] no furniture actor here\n");
        return;
    }
    idx = aMR_CLIP->judge_breed_new_ftr_proc(game, ftr_idx, &ux, &uz, &rot, &sq_ofs, &layer);
    printf("[place] %04X: judge %d -> unit %d,%d rot %d layer %d\n", ftr, idx, ux, uz, rot, layer);
    if (idx >= 0) {
        aMR_CLIP->reserve_ftr_proc(game, ftr_idx, idx, ux, uz, rot, sq_ofs, layer);
    }
}

/* PC_CTL open: the plain trees with the most flat, dry, empty ground around them (5 units each way), best first */
void pc_dev_open_trees(void) {
    int ut_x;
    int ut_z;

    if (!mFI_CheckFieldData()) {
        return;
    }
    for (ut_z = UT_Z_NUM; ut_z < mFI_GetBlockZMax() * UT_Z_NUM; ut_z++) {
        for (ut_x = UT_X_NUM; ut_x < mFI_GetBlockXMax() * UT_X_NUM; ut_x++) {
            mActor_name_t* fg = mFI_UtNum2UtFG(ut_x, ut_z);
            xyz_t c;
            f32 y0;
            int free_n = 0;
            int dx;
            int dz;

            if (fg == NULL || *fg != TREE) {
                continue;
            }
            mFI_UtNum2CenterWpos(&c, ut_x, ut_z);
            y0 = mCoBG_GetBgY_OnlyCenter_FromWpos2(c, 0.0f);
            for (dz = -5; dz <= 5; dz++) {
                for (dx = -5; dx <= 5; dx++) {
                    mActor_name_t* f = mFI_UtNum2UtFG(ut_x + dx, ut_z + dz);
                    xyz_t p;

                    if ((dx == 0 && dz == 0) || f == NULL || *f != EMPTY_NO ||
                        !mCoBG_Unit2CheckNpc(ut_x + dx, ut_z + dz)) {
                        continue;
                    }
                    mFI_UtNum2CenterWpos(&p, ut_x + dx, ut_z + dz);
                    if (ABS(mCoBG_GetBgY_OnlyCenter_FromWpos2(p, 0.0f) - y0) < 10.0f) {
                        free_n++;
                    }
                }
            }
            printf("[open] tree %d,%d free %d/120\n", ut_x, ut_z, free_n);
        }
    }
}

/* PC_CTL fgfind: log the units holding field item `item` */
void pc_dev_fg_find(int item) {
    int ut_x;
    int ut_z;

    if (!mFI_CheckFieldData()) {
        return;
    }

    for (ut_z = 0; ut_z < mFI_GetBlockZMax() * UT_Z_NUM; ut_z++) {
        for (ut_x = 0; ut_x < mFI_GetBlockXMax() * UT_X_NUM; ut_x++) {
            mActor_name_t* fg = mFI_UtNum2UtFG(ut_x, ut_z);

            if (fg != NULL && *fg == (mActor_name_t)item) {
                printf("[fg] %04X at unit %d,%d (world %d,%d)\n", item, ut_x, ut_z, ut_x * 40 + 20, ut_z * 40 + 20);
            }
        }
    }
}

/* PC_CTL loan: the player's house loan */
void pc_devtest_set_loan(int bells) {
    if (Now_Private != NULL) {
        Now_Private->inventory.loan = (u32)bells;
    }
}

/* PC_CTL shopbuy SLOT (multiplayer tests): what buying the shop's item SLOT does to the town and the player - the
 * slot marked sold (mSP_ShopSaleReport), the item into the pockets, the price out of the wallet */
void pc_dev_shop_buy(int slot) {
    Private_c* priv = Now_Private;
    mActor_name_t item;
    int idx;

    if (slot < 0 || slot >= mSP_GOODS_COUNT || priv == NULL) {
        return;
    }
    item = Save_Get(shop).items[slot];
    idx = mPr_GetPossessionItemIdx(priv, EMPTY_NO);
    if (item == EMPTY_NO || item >= 0xFE00 || idx < 0) {
        printf("[shop] can't buy slot %d (%04X)\n", slot, item);
        return;
    }
    mSP_ShopSaleReport(item, Save_Get(shop).items, mSP_GOODS_COUNT, RSV_SHOP_SOLD_FTR);
    mPr_SetPossessionItem(priv, idx, item, mPr_ITEM_COND_NORMAL);
    priv->inventory.wallet -= mSP_ItemNo2ItemPrice(item) <= priv->inventory.wallet ? mSP_ItemNo2ItemPrice(item) : 0;
    printf("[shop] bought slot %d (%04X), wallet %u\n", slot, item, priv->inventory.wallet);
}

/* ctl shopcandy: buy a candy or a grab bag (whichever the shop has), as its shelf does (ac_shop_design.c) */
void pc_dev_shop_counted(void) {
    Private_c* priv = Now_Private;
    mActor_name_t item =
        Common_Get(tanuki_shop_status) == mSP_TANUKI_SHOP_STATUS_HALLOWEEN ? ITM_FOOD_CANDY : ITM_HUKUBUKURO_BAG;
    int idx;

    if (priv == NULL || (idx = mPr_GetPossessionItemIdx(priv, EMPTY_NO)) < 0 ||
        Save_Get(shop).flowers_candy_grab_bag_count <= 0) {
        printf("[shop] none to buy (%d left)\n", Save_Get(shop).flowers_candy_grab_bag_count);
        return;
    }
    mSP_ShopSaleReport(item, Save_Get(shop).items, mSP_GOODS_COUNT, RSV_SHOP_SOLD_PLANT);
    mPr_SetPossessionItem(priv, idx, item, mPr_ITEM_COND_NORMAL);
    priv->inventory.wallet -= mSP_ItemNo2ItemPrice(item) <= priv->inventory.wallet ? mSP_ItemNo2ItemPrice(item) : 0;
    printf("[shop] bought a %04X, %d left, wallet %u\n", item, Save_Get(shop).flowers_candy_grab_bag_count,
           priv->inventory.wallet);
}

/* PC_CTL shoplog: the shop's goods and the wallet */
void pc_dev_shop_log(void) {
    int i;
    printf("[shop] wallet %u, %d candies/grab bags left, goods", Now_Private != NULL ? Now_Private->inventory.wallet : 0,
           Save_Get(shop).flowers_candy_grab_bag_count);
    for (i = 0; i < 8; i++) {
        printf(" %04X", Save_Get(shop).items[i]);
    }
    printf("\n");
    if (Now_Private != NULL) {
        printf("[shop] pockets");
        for (i = 0; i < mPr_POCKETS_SLOT_COUNT; i++) {
            printf(" %04X", Now_Private->inventory.pockets[i]);
        }
        printf("\n");
    }
}

/* PC_CTL museumlog: the museum's fish (who donated each, 4 bits per fish) */
void pc_dev_museum_log(void) {
    int i;
    printf("[museum] fish");
    for (i = 0; i < mMmd_FISH_BIT_NUM; i++) {
        printf(" %02X", Save_Get(museum_display).fish_bit[i]);
    }
    printf("\n");
}

/* PC_CTL roomset HOME FLOOR UNIT ITEM / roomlog HOME FLOOR UNIT: a house's furniture unit (main layer) */
void pc_dev_room(int home, int floor, int unit, int item) {
    mActor_name_t* it;
    if (home < 0 || home >= PLAYER_NUM || floor < 0 || floor >= mHm_ROOM_NUM || unit < 0 || unit >= UT_TOTAL_NUM) {
        return;
    }
    it = &Save_Get(homes[home]).floors[floor].layer_main.items[unit / UT_X_NUM][unit % UT_X_NUM];
    if (item >= 0) {
        *it = (mActor_name_t)item;
    }
    printf("[room] home %d floor %d unit %d = %04X\n", home, floor, unit, *it);
}

/* ctl wear: put on a shirt, as the pockets do (m_hand_ovl.c) */
void pc_dev_wear(int item) {
    if (Now_Private == NULL || gamePT == NULL) {
        return;
    }
    mPlib_change_player_cloth_info_lv2(Now_Private, (mActor_name_t)item);
    mPlib_change_player_cloth(gamePT, Now_Private->cloth.idx);
}

/* ctl npcgone: villager idx at home in this game, their actor gone */
void pc_dev_npc_gone(int idx) {
    GAME_PLAY* play = (GAME_PLAY*)gamePT;
    ACTOR* a;
    if (play == NULL || idx < 0 || idx >= ANIMAL_NUM_MAX) {
        return;
    }
    Save_Get(animals[idx]).is_home = TRUE;
    Common_Get(npclist)[idx].position = Common_Get(npclist)[idx].house_position; /* elsewhere, as far as we know */
    for (a = play->actor_info.list[ACTOR_PART_NPC].actor; a != NULL; a = a->next_actor) {
        if (a->npc_id == Save_Get(animals[idx]).id.npc_id) {
            Actor_delete(a);
            printf("[devtest] villager %d removed\n", idx);
        }
    }
}

/* ctl talkto: in front of villager idx, facing them */
void pc_dev_talkto(int idx) {
    GAME_PLAY* play = (GAME_PLAY*)gamePT;
    ACTOR* a;
    char pos[48];
    if (play == NULL || idx < 0 || idx >= ANIMAL_NUM_MAX) {
        return;
    }
    for (a = play->actor_info.list[ACTOR_PART_NPC].actor; a != NULL; a = a->next_actor) {
        if (a->npc_id == Save_Get(animals[idx]).id.npc_id) {
            snprintf(pos, sizeof(pos), "%d,%d,180", (int)a->world.position.x, (int)a->world.position.z + 36);
            pc_dev_teleport(play, pos);
            printf("[devtest] in front of villager %d\n", idx);
            return;
        }
    }
    printf("[devtest] villager %d isn't here\n", idx);
}

/* ctl neartree: in front of the nearest grown tree with open ground south of it, facing it */
void pc_dev_near_tree(void) {
    GAME_PLAY* play = (GAME_PLAY*)gamePT;
    PLAYER_ACTOR* player;
    int ut_x, ut_z, best_x = -1, best_z = -1;
    f32 best = 1e9f;
    char pos[48];

    if (play == NULL || !mFI_CheckFieldData()) {
        return;
    }
    player = GET_PLAYER_ACTOR(play);
    for (ut_z = 0; ut_z + 1 < mFI_GetBlockZMax() * UT_Z_NUM; ut_z++) {
        for (ut_x = 0; ut_x < mFI_GetBlockXMax() * UT_X_NUM; ut_x++) {
            mActor_name_t* fg = mFI_UtNum2UtFG(ut_x, ut_z);
            mActor_name_t* below = mFI_UtNum2UtFG(ut_x, ut_z + 1);
            f32 dx, dz;
            if (fg == NULL || below == NULL || !IS_ITEM_GROWN_TREE(*fg) || *below != EMPTY_NO) {
                continue;
            }
            dx = ut_x * 40 + 20 - player->actor_class.world.position.x;
            dz = ut_z * 40 + 20 - player->actor_class.world.position.z;
            if (dx * dx + dz * dz < best) {
                best = dx * dx + dz * dz, best_x = ut_x, best_z = ut_z;
            }
        }
    }
    if (best_x < 0) {
        printf("[devtest] no tree near\n");
        return;
    }
    snprintf(pos, sizeof(pos), "%d,%d,180", best_x * 40 + 20, (best_z + 1) * 40 + 20);
    pc_dev_teleport(play, pos);
    printf("[devtest] at tree %04X, unit %d,%d\n", *mFI_UtNum2UtFG(best_x, best_z), best_x, best_z);
}

/* ctl switchnear */
static int l_sw_best_on, l_sw_best_x, l_sw_best_z, l_sw_best_layer, l_sw_best_name;
static f32 l_sw_best;
static int l_sw_only;

static void sw_near(void* user, int idx, u16 name, int ut_x, int ut_z, int layer, int on) {
    PLAYER_ACTOR* player = (PLAYER_ACTOR*)user;
    f32 dx = ut_x * 40 + 20 - player->actor_class.world.position.x;
    if (l_sw_only >= 0 && name != l_sw_only) {
        return;
    }
    f32 dz = ut_z * 40 + 20 - player->actor_class.world.position.z;
    if (dx * dx + dz * dz < l_sw_best) {
        l_sw_best = dx * dx + dz * dz;
        l_sw_best_on = on, l_sw_best_x = ut_x, l_sw_best_z = ut_z, l_sw_best_layer = layer, l_sw_best_name = name;
    }
}

void pc_dev_switch_near(int only) {
    extern void pc_aMR_switch_each(void (*fn)(void* user, int idx, u16 name, int ut_x, int ut_z, int layer, int on),
                                   void* user);
    extern int pc_aMR_switch_set(u16 name, int ut_x, int ut_z, int layer, int on);
    GAME_PLAY* play = (GAME_PLAY*)gamePT;
    if (play == NULL) {
        return;
    }
    l_sw_best = 1e9f;
    l_sw_best_name = -1;
    l_sw_only = only;
    pc_aMR_switch_each(sw_near, GET_PLAYER_ACTOR(play));
    if (l_sw_best_name < 0) {
        printf("[devtest] no furniture here\n");
        return;
    }
    pc_aMR_switch_set((u16)l_sw_best_name, l_sw_best_x, l_sw_best_z, l_sw_best_layer, !l_sw_best_on);
    printf("[devtest] furniture %d at %d,%d switched %s\n", l_sw_best_name, l_sw_best_x, l_sw_best_z,
           !l_sw_best_on ? "on" : "off");
}

/* ctl talkprof: in front of the first actor of a profile, facing it */
void pc_dev_talk_profile(int profile) {
    GAME_PLAY* play = (GAME_PLAY*)gamePT;
    int part;
    char pos[48];
    if (play == NULL) {
        return;
    }
    for (part = 0; part < ACTOR_PART_NUM; part++) {
        ACTOR* a;
        for (a = play->actor_info.list[part].actor; a != NULL; a = a->next_actor) {
            if (a->id == profile) {
                snprintf(pos, sizeof(pos), "%d,%d,180", (int)a->world.position.x, (int)a->world.position.z + 40);
                pc_dev_teleport(play, pos);
                printf("[devtest] in front of profile %d at %d,%d\n", profile, (int)a->world.position.x,
                       (int)a->world.position.z);
                return;
            }
        }
    }
    printf("[devtest] no profile %d here\n", profile);
}

/* ctl nearwater: the dry unit south of the nearest river or pond water unit, facing the water */
void pc_dev_near_water(void) {
    GAME_PLAY* play = (GAME_PLAY*)gamePT;
    PLAYER_ACTOR* player;
    int ut_x, ut_z, best_x = -1, best_z = -1;
    f32 best = 1e9f;
    char pos[48];

    if (play == NULL || !mFI_CheckFieldData()) {
        return;
    }
    player = GET_PLAYER_ACTOR(play);
    for (ut_z = UT_Z_NUM; ut_z + 1 < mFI_GetBlockZMax() * UT_Z_NUM; ut_z++) {
        for (ut_x = UT_X_NUM; ut_x < (mFI_GetBlockXMax() - 1) * UT_X_NUM; ut_x++) {
            xyz_t w = { ut_x * 40.0f + 20.0f, 0.0f, ut_z * 40.0f + 20.0f };
            xyz_t d = { ut_x * 40.0f + 20.0f, 0.0f, (ut_z + 1) * 40.0f + 20.0f };
            f32 dx, dz;
            if (!mCoBG_CheckWaterAttribute_OutOfSea(mCoBG_Wpos2BgAttribute_Original(w)) ||
                mCoBG_CheckWaterAttribute(mCoBG_Wpos2BgAttribute_Original(d))) {
                continue;
            }
            dx = d.x - player->actor_class.world.position.x;
            dz = d.z - player->actor_class.world.position.z;
            if (dx * dx + dz * dz < best) {
                best = dx * dx + dz * dz, best_x = ut_x, best_z = ut_z + 1;
            }
        }
    }
    if (best_x < 0) {
        printf("[devtest] no water\n");
        return;
    }
    snprintf(pos, sizeof(pos), "%d,%d,180", best_x * 40 + 20, best_z * 40 + 20);
    pc_dev_teleport(play, pos);
    printf("[devtest] by the water at unit %d,%d (%d,%d)\n", best_x, best_z, best_x * 40 + 20, best_z * 40 + 20);
}

/* ctl npcaway: villager idx out but elsewhere in this game, their actor here gone */
void pc_dev_npc_away(int idx) {
    pc_dev_npc_gone(idx);
    if (idx >= 0 && idx < ANIMAL_NUM_MAX) {
        xyz_t* p = &Common_Get(npclist)[idx].position;
        Save_Get(animals[idx]).is_home = FALSE;
        /* a different acre from where they were: two acres over */
        p->x = p->x < 3 * 640.0f ? p->x + 2 * 640.0f : p->x - 2 * 640.0f;
        printf("[devtest] villager %d is out at %d,%d\n", idx, (int)p->x, (int)p->z);
    }
}

/* ctl wallet BELLS */
void pc_dev_wallet(int bells) {
    if (Now_Private != NULL) {
        Now_Private->inventory.wallet = (u32)bells;
    }
}
