/* pc_devwarp.c - dev: PC_WARP_AT=F walks the player into a scene's door at frame F, and PC_TELEPORT_AT=F puts
 * them at PC_TELEPORT_POS=x,z[,degrees] in the scene they're in
 * (PC_WARP_SCENE: 1 = museum entrance, the default; 2 = observatory, 3 = café, 4-7 = house basement S/M/L/LL,
 * PC_BASEMENT_TEST=wallpaper,carpet dressing them;
 * PC_WARP_POS=x,z: where to
 * arrive instead of the door) */
#include "m_play.h"
#include "m_scene.h"
#include "m_scene_table.h"
#include "m_common_data.h"
#include "m_player_lib.h"

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

void pc_dev_teleport(void* playv) {
    GAME_PLAY* play = (GAME_PLAY*)playv;
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);
    const char* pos = getenv("PC_TELEPORT_POS");
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
