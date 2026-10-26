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
