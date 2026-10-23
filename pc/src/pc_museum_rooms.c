/* pc_museum_rooms.c - the museum's City Folk rooms: the observatory upstairs and the
 * café downstairs (SCENE_MUSEUM_OBSERVATORY / SCENE_MUSEUM_CAFE, src/data/scene/museum_pc_rooms.c).
 *
 * The entrance hall gets stairs in its side walls (the doorways are in rom_museum1.c): this
 * makes their corridors walkable, sloping down on the left and up on the right, and puts the
 * doors to the new rooms at their far ends. The new rooms borrow the hall's block; their own
 * floor plan comes from the City Folk room file (pc_cf_room.c) and the hall's model is dropped,
 * so only the City Folk room is drawn, raised to the GameCube's floor height. */
#include "m_play.h"
#include "m_scene.h"
#include "m_scene_table.h"
#include "m_field_make.h"
#include "m_field_info.h"
#include "m_collision_bg.h"
#include "m_common_data.h"
#include "m_name_table.h"
#include "m_player_lib.h"
#include "m_player.h"
#include "pc_cf_room.h"
#include "ac_npc_kk_roost.h"
#include "pc_music.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The GameCube floor is 40 up (collision 4); City Folk's is at 0 */
#define PC_MR_FLOOR 4
#define PC_MR_WALL 31
#define PC_MR_FLOOR_Y 40.0f
#define PC_MR_CAFE_MUSIC "cf_sky/music/roost.wav"

typedef struct {
    int scene;
    const char* room;
    int exit_ux[2], exit_uz[2]; /* the doorway's two tiles, the door back to the hall */
    float camera_center_x;      /* middle of the room, for the camera */
} pc_museum_room_c;

/* Tiles as cf_rooms.py lays the rooms out (scaled to 40 tiles, floor centred in the block):
 * the observatory's way out is its corridor at the front, the café's the stairs on its right */
static const pc_museum_room_c l_rooms[] = {
    { SCENE_MUSEUM_OBSERVATORY, "idr_ms_astro", { 7, 8 }, { 11, 11 }, 320.0f },
    { SCENE_MUSEUM_CAFE, "idr_cafe", { 12, 12 }, { 9, 10 }, 300.0f },
};

/* The hall's new doors (entries 4 and 5 of its door table) */
static Door_data_c l_entrance_doors[6];
/* The scene actually loaded. Not Save_Get(scene_no): that keeps the scene the game was saved in, café
 * included, while the title screen's demo plays */
static int l_scene = -1;

void pc_museum_rooms_scene_set(int scene) {
    l_scene = scene;
}

int pc_museum_rooms_scene(void) {
    return l_scene;
}

static const pc_museum_room_c* pc_mr_room(int scene) {
    int i;

    for (i = 0; i < (int)(sizeof(l_rooms) / sizeof(l_rooms[0])); i++) {
        if (l_rooms[i].scene == scene) {
            return &l_rooms[i];
        }
    }
    return NULL;
}

int pc_museum_rooms_is_room(int scene) {
    return pc_mr_room(scene) != NULL;
}

int pc_museum_rooms_available(void) {
    return pc_cf_room_exists("idr_ms_astro") && pc_cf_room_exists("idr_cafe");
}

static void pc_mr_set_unit(mFM_bg_info_c* bg, int ux, int uz, int tl, int tr, int bl, int br) {
    mCoBG_Collision_u* c = &bg->collision[uz][ux];

    c->data.slate_flag = 0;
    c->data.top_left = tl, c->data.top_right = tr;
    c->data.bot_left = bl, c->data.bot_right = br;
    c->data.center = (tl + tr + bl + br) / 4;
    c->data.unit_attribute = mCoBG_ATTRIBUTE_FLOOR;
    bg->keep_h[uz][ux] = c->data.center;
}

/* Right after the field is made (mFM_FieldInit) */
void pc_museum_rooms_field_made(void) {
    int scene = l_scene;
    const pc_museum_room_c* room = pc_mr_room(scene);
    mFM_block_info_c* block;
    int ux, uz;

    if (g_fdinfo == NULL || g_fdinfo->block_info == NULL || !pc_museum_rooms_available()) {
        return;
    }
    block = &g_fdinfo->block_info[0];
    if (getenv("PC_CF_ROOM_LOG") != NULL) {
        printf("[MuseumRooms] scene %d\n", scene);
    }

    if (scene == SCENE_MUSEUM_ENTRANCE) {
        /* the stair corridors: x 1-2 (down, west) and 9-10 (up, east), z 9-10 */
        for (uz = 9; uz <= 10; uz++) {
            pc_mr_set_unit(&block->bg_info, 2, uz, 3, 4, 3, 4);
            pc_mr_set_unit(&block->bg_info, 1, uz, 2, 3, 2, 3);
            pc_mr_set_unit(&block->bg_info, 9, uz, 4, 5, 4, 5);
            pc_mr_set_unit(&block->bg_info, 10, uz, 5, 6, 5, 6);
            if (block->fg_info.items_p != NULL) {
                block->fg_info.items_p[uz * UT_X_NUM + 1] = DOOR_START + 5; /* down to the café */
                block->fg_info.items_p[uz * UT_X_NUM + 10] = DOOR_START + 4; /* up to the observatory */
            }
        }
        return;
    }
    if (room == NULL) {
        return;
    }

    /* a City Folk room: its floor plan, one door back, and none of the hall's look */
    {
        const u8* grid = pc_cf_room_grid(room->room);

        for (uz = 0; uz < UT_Z_NUM; uz++) {
            for (ux = 0; ux < UT_X_NUM; ux++) {
                int floor = grid != NULL && grid[uz * 16 + ux] != 0;
                int h = floor ? PC_MR_FLOOR : PC_MR_WALL;

                pc_mr_set_unit(&block->bg_info, ux, uz, h, h, h, h);
                if (block->fg_info.items_p != NULL) {
                    block->fg_info.items_p[uz * UT_X_NUM + ux] = EMPTY_NO;
                }
            }
        }
        for (ux = 0; ux < 2; ux++) {
            pc_mr_set_unit(&block->bg_info, room->exit_ux[ux], room->exit_uz[ux], PC_MR_FLOOR, PC_MR_FLOOR,
                           PC_MR_FLOOR, PC_MR_FLOOR);
            if (block->fg_info.items_p != NULL) {
                block->fg_info.items_p[room->exit_uz[ux] * UT_X_NUM + room->exit_ux[ux]] = DOOR_START + 0;
            }
        }
    }
    block->bg_info.opaque_gfx = NULL;
    block->bg_info.translucent_gfx = NULL;
    block->bg_info.animation = NULL;
    block->bg_info.animation_count = 0;
}

/* Scene_Proc_Door_Data_Ptr: the hall gets two more doors */
Door_data_c* pc_museum_rooms_door_data(int scene, Door_data_c* doors, int* num) {
    if (scene != SCENE_MUSEUM_ENTRANCE || *num != 4 || !pc_museum_rooms_available()) {
        return doors;
    }
    memcpy(l_entrance_doors, doors, sizeof(Door_data_c) * 4);
    l_entrance_doors[4] = l_entrance_doors[0];
    l_entrance_doors[4].next_scene_id = SCENE_MUSEUM_OBSERVATORY;
    l_entrance_doors[4].exit_orientation = mSc_DIRECT_NORTH;
    l_entrance_doors[4].exit_position.x = 320;
    l_entrance_doors[4].exit_position.y = 0;
    l_entrance_doors[4].exit_position.z = 420;
    l_entrance_doors[4].door_actor_name = DOOR_START + 4;
    l_entrance_doors[5] = l_entrance_doors[0];
    l_entrance_doors[5].next_scene_id = SCENE_MUSEUM_CAFE;
    l_entrance_doors[5].exit_orientation = mSc_DIRECT_WEST;
    l_entrance_doors[5].exit_position.x = 460;
    l_entrance_doors[5].exit_position.y = 0;
    l_entrance_doors[5].exit_position.z = 400;
    l_entrance_doors[5].door_actor_name = DOOR_START + 5;
    *num = 6;
    return l_entrance_doors;
}

/* Round things the player walks around, as the GameCube's actors' collision cylinders do: the café's
 * counter stools (from the room's model: seats 14 across, plus the player's own reach). Sitting on one
 * (ac_npc_brewster.c) goes past this. */
typedef struct {
    float x, z, r;
} pc_mr_round_c;

static const pc_mr_round_c l_cafe_round[] = {
    { 299.0f, 213.0f, 30.0f },
    { 299.0f, 253.0f, 30.0f },
    { 299.0f, 293.0f, 30.0f },
};

/* Game_play_move, after the actors: push the player out of them */
void pc_museum_rooms_move(GAME_PLAY* play) {
    PLAYER_ACTOR* player;
    int idx, i;

    if (l_scene != SCENE_MUSEUM_CAFE || !pc_museum_rooms_available() || (player = GET_PLAYER_ACTOR(play)) == NULL) {
        return;
    }
    idx = mPlib_get_player_actor_main_index((GAME*)play);
    if (idx == mPlayer_INDEX_SITDOWN || idx == mPlayer_INDEX_SITDOWN_WAIT || idx == mPlayer_INDEX_STANDUP) {
        return;
    }
    for (i = 0; i < (int)(sizeof(l_cafe_round) / sizeof(l_cafe_round[0])); i++) {
        xyz_t* p = &player->actor_class.world.position;
        float dx = p->x - l_cafe_round[i].x, dz = p->z - l_cafe_round[i].z;
        float d2 = dx * dx + dz * dz;

        if (d2 < l_cafe_round[i].r * l_cafe_round[i].r) {
            float d = sqrtf(d2);

            if (d < 0.01f) {
                dx = -1.0f, dz = 0.0f, d = 1.0f; /* dead centre: out towards the room */
            }
            p->x = l_cafe_round[i].x + dx / d * l_cafe_round[i].r;
            p->z = l_cafe_round[i].z + dz / d * l_cafe_round[i].r;
        }
    }
}

/* Game_play_draw: the City Folk room, at the top of the frame's background; and the Roost's
 * music in the café, from City Folk (extract_cf_sky.py), in place of the museum's */
void pc_museum_rooms_draw(GAME_PLAY* play) {
    const pc_museum_room_c* room = pc_mr_room(l_scene);

    {
        /* K.K.'s song, in place of the Roost's music (ac_npc_kk_roost.c) */
        pc_music_set_scene_track(l_scene == SCENE_MUSEUM_CAFE && !aKKR_playing ? PC_MR_CAFE_MUSIC : NULL);
    }

    if (room != NULL) {
        float light = 1.0f;

        if (l_scene == SCENE_MUSEUM_CAFE && aKKR_show > 0.0f) {
            /* K.K.'s show: the lights down, a pool of light on him (ac_npc_kk_roost.c) */
            light = aKKR_room_light();
            pc_cf_room_set_spot(aKKR_STAGE_X, aKKR_STAGE_Z, 75.0f, aKKR_show * aKKR_show * (3.0f - 2.0f * aKKR_show));
        }
        pc_cf_room_play_draw(play, room->room, light, PC_MR_FLOOR_Y);
        if (getenv("PC_CF_ROOM_LOG") != NULL && play->game_frame % 120 == 0) {
            PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);

            if (player != NULL) {
                printf("[MuseumRooms] player %.0f %.0f %.0f\n", player->actor_class.world.position.x,
                       player->actor_class.world.position.y, player->actor_class.world.position.z);
            }
        }
    }
}

/* mRmTp_GetEntranceBasePosition: where the camera's room starts (it centres on the hall's width) */
int pc_museum_rooms_camera_base(xyz_t* pos) {
    const pc_museum_room_c* room = pc_mr_room(l_scene);

    if (room == NULL) {
        return FALSE;
    }
    pos->x = room->camera_center_x - mFI_GetBlockWidth() * 0.5f;
    pos->y = PC_MR_FLOOR_Y;
    pos->z = 0.0f;
    return TRUE;
}
