/* pc_devwarp.c - dev: PC_WARP_AT=F walks the player into a scene's door at frame F
 * (PC_WARP_SCENE: 1 = museum entrance, the default; 2 = observatory, 3 = café) */
#include "m_play.h"
#include "m_scene.h"
#include "m_scene_table.h"
#include "m_common_data.h"

#include <stdlib.h>

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
    } else if (scene != NULL && atoi(scene) == 3) {
        door.next_scene_id = SCENE_MUSEUM_CAFE;
        door.exit_orientation = mSc_DIRECT_WEST;
        door.exit_position.x = 460, door.exit_position.z = 400;
    }
    goto_other_scene(play, &door, TRUE);
}
