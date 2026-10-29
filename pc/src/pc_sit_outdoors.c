/* pc_sit_outdoors.c - sit on a tree stump or a rock outdoors, as on a chair: A while facing one, with nothing in
 * hand that A would use (a tool still chops, digs or swings). Getting up goes the way the stick points, if there's
 * room there (pc_sit_outdoor_unit_free, m_player_main_standup.c_inc). */

#include "m_play.h"
#include "m_player_lib.h"
#include "m_field_info.h"
#include "m_collision_bg.h"
#include "m_common_data.h"
#include "m_name_table.h"
#include "m_demo.h"
#include "m_controller.h"
#include "m_private.h"
#include "m_lib.h"
#include "sys_math.h"

#include <stdio.h>
#include <stdlib.h>

static int pc_sit_seat(mActor_name_t item) {
    return IS_ITEM_TREE_STUMP(item) || IS_ITEM_ROCK(item);
}

/* A field unit the player can stand on after getting up: nothing standing in it, not water, about the same height */
int pc_sit_outdoor_unit_free(xyz_t pos, f32 y) {
    mActor_name_t* fg = mFI_GetUnitFG(pos);
    s8 cant_dig;
    u32 attr;
    f32 ground;

    if (fg == NULL) {
        return FALSE;
    }
    if (*fg != EMPTY_NO && ITEM_NAME_GET_TYPE(*fg) != NAME_TYPE_ITEM1) {
        return FALSE; /* trees, stumps, rocks, signs, structures: anything planted there (items lying there are fine) */
    }
    attr = mCoBG_Wpos2Attribute(pos, &cant_dig);
    if (mCoBG_CheckWaterAttribute(attr) || attr == mCoBG_ATTRIBUTE_HOLE) {
        return FALSE;
    }
    ground = mCoBG_GetBgY_OnlyCenter_FromWpos2(pos, 0.0f);
    return ground - y < 10.0f && y - ground < 10.0f;
}

#define PC_SIT_ROCK_LIFT 14.0f /* a rock's top is higher than a chair seat or a stump */

int pc_player_seat_raised; /* sitting on a rock: Player_actor_move puts the player at pc_player_seat_y */
f32 pc_player_seat_y;

/* Called each frame outdoors (m_play.c) */
void pc_sit_outdoors_tick(GAME* game) {
    GAME_PLAY* play = (GAME_PLAY*)game;
    PLAYER_ACTOR* player;
    int idx;
    mActor_name_t equip;
    mActor_name_t* fg;
    xyz_t front;
    xyz_t seat;
    s16 rot;
    int dir;

    if (!mFI_CheckFieldData() || (player = GET_PLAYER_ACTOR(play)) == NULL) {
        pc_player_seat_raised = FALSE;
        return;
    }
    idx = mPlib_get_player_actor_main_index(game);
    if (idx != mPlayer_INDEX_SITDOWN && idx != mPlayer_INDEX_SITDOWN_WAIT) {
        pc_player_seat_raised = FALSE;
    }
    if (!chkTrigger(BUTTON_A) || mDemo_Get_talk_actor() != NULL) {
        return;
    }
    if (idx != mPlayer_INDEX_WAIT && idx != mPlayer_INDEX_WALK && idx != mPlayer_INDEX_RUN) {
        return;
    }
    equip = Common_Get(now_private) != NULL ? Common_Get(now_private)->equipment : EMPTY_NO;
    if (equip != EMPTY_NO && !ITEM_IS_UMBRELLA2(equip)) {
        return; /* A is the tool's */
    }

    /* the unit in front, along the nearest of the four ways the player faces */
    rot = player->actor_class.shape_info.rotation.y;
    dir = ((u16)(rot + 0x2000) >> 14) & 3; /* 0 south (+z), 1 east (+x), 2 north, 3 west */
    front = player->actor_class.world.position;
    front.x += 40.0f * sin_s((s16)(dir << 14));
    front.z += 40.0f * cos_s((s16)(dir << 14));
    fg = mFI_GetUnitFG(front);
    if (fg == NULL || !pc_sit_seat(*fg) || !mFI_Wpos2UtCenterWpos(&seat, front)) {
        return;
    }
    seat.y = player->actor_class.world.position.y;
    /* sitting down backs the player 35 onto the seat (as at the Roost's stools): start that far out, on the side they
     * came from, which is also the way they end up facing */
    seat.x -= 35.0f * sin_s((s16)(dir << 14));
    seat.z -= 35.0f * cos_s((s16)(dir << 14));
    if (getenv("PC_SIT_LOG") != NULL) {
        printf("[sit] player %.0f,%.0f rot %04X dir %d front %.0f,%.0f item %04X seat %.0f,%.0f\n",
               player->actor_class.world.position.x, player->actor_class.world.position.z, (u16)rot, dir, front.x,
               front.z, *fg, seat.x, seat.z);
    }
    /* as for a chair: the direction given is the seat's back, the far side from the player, who sits facing back the
     * way they came (wall angles: 0 north, 1 west, 2 south, 3 east) */
    {
        static const int back_of[4] = { 2, 3, 0, 1 }; /* facing south, the back is south... */
        if (mPlib_request_main_sitdown_type1(game, back_of[dir], &seat, -1)) {
            pc_player_seat_raised = IS_ITEM_ROCK(*fg);
            pc_player_seat_y = player->actor_class.world.position.y + PC_SIT_ROCK_LIFT;
        }
    }
}
