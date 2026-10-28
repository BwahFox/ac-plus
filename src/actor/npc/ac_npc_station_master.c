#include "ac_npc_station_master.h"
#ifdef TARGET_PC
#include <stdio.h>
#include <stdlib.h>
#endif

#include "libultra/libultra.h"
#include "m_common_data.h"
#include "m_msg.h"
#include "m_font.h"
#include "m_player_lib.h"
#include "m_string.h"
#include "m_bgm.h"
#include "m_train_control.h"
#include "ac_intro_demo.h"
#include "ac_ride_off_demo.h"

enum {
    aSTM_THINK_GET_OFF_WAIT,
    aSTM_THINK_TURN,
    aSTM_THINK_2,
    aSTM_THINK_3,
    aSTM_THINK_4,
    aSTM_THINK_5,
    aSTM_THINK_INTERRUPT_TURN,
    aSTM_THINK_INTERRUPT_MOVE,
    aSTM_THINK_8,
    aSTM_THINK_9,
    aSTM_THINK_MAKE_TRAIN_BF,
    aSTM_THINK_MAKE_TRAIN,
    aSTM_THINK_TRAIN_ARRIVE_WAIT,
    aSTM_THINK_MOVE_TURN_BF,
    aSTM_THINK_14,
    aSTM_THINK_MOVE,
    aSTM_THINK_16,
    aSTM_THINK_17,
    aSTM_THINK_IN_TRAIN,
    aSTM_THINK_IN_TRAIN2,
    aSTM_THINK_SEE_OFF,
    aSTM_THINK_GAME_END,

    aSTM_THINK_NUM
};

static void aSTM_actor_ct(ACTOR* actorx, GAME* game);
static void aSTM_actor_dt(ACTOR* actorx, GAME* game);
static void aSTM_actor_init(ACTOR* actorx, GAME* game);
static void aSTM_actor_save(ACTOR* actorx, GAME* game);
static void aSTM_actor_move(ACTOR* actorx, GAME* game);
static void aSTM_actor_draw(ACTOR* actorx, GAME* game);
static int aSTM_talk_init(ACTOR* actorx, GAME* game);
static int aSTM_talk_end_chk(ACTOR* actorx, GAME* game);
static void aSTM_schedule_proc(NPC_ACTOR*, GAME_PLAY*, int);
static void aSTM_talk_request(ACTOR* actorx, GAME* game);
static int aSTM_change_talk_proc(NPC_STATION_MASTER_ACTOR*, u8);
static void aSTM_setup_think_proc(NPC_STATION_MASTER_ACTOR* actor, GAME_PLAY* play, u8 think_idx);

// clang-format off
ACTOR_PROFILE Npc_Station_Master_Profile = {
    mAc_PROFILE_NPC_STATION_MASTER,
    ACTOR_PART_NPC,
    ACTOR_STATE_NONE,
    SP_NPC_STATION_MASTER,
    ACTOR_OBJ_BANK_KEEP,
    sizeof(NPC_STATION_MASTER_ACTOR),
    aSTM_actor_ct,
    aSTM_actor_dt,
    aSTM_actor_init,
    mActor_NONE_PROC1,
    aSTM_actor_save
};
// clang-format on

static void aSTM_actor_ct(ACTOR* actorx, GAME* game) {
    // clang-format off
    static aNPC_ct_data_c ct_data = {
        aSTM_actor_move,
        aSTM_actor_draw,
        aNPC_CT_SCHED_TYPE_SPECIAL,
        (aNPC_TALK_REQUEST_PROC)none_proc1,
        aSTM_talk_init,
        aSTM_talk_end_chk,
        0
    };
    // clang-format on

    if (NPC_CLIP->birth_check_proc(actorx, game) == TRUE) {
        NPC_STATION_MASTER_ACTOR* actor = (NPC_STATION_MASTER_ACTOR*)actorx;

        actor->npc_class.schedule.schedule_proc = aSTM_schedule_proc;
        NPC_CLIP->ct_proc(actorx, game, &ct_data);
        actor->sound = 0;
        actor->happening_sound = 0;
        actor->melody_save = 0;
        actor->npc_class.palActorIgnoreTimer = -1;
    }
}

static void aSTM_actor_save(ACTOR* actorx, GAME* game) {
    mNpc_RenewalSetNpc(actorx);
}

static void aSTM_actor_dt(ACTOR* actorx, GAME* game) {
    NPC_STATION_MASTER_ACTOR* actor = (NPC_STATION_MASTER_ACTOR*)actorx;

    if (actor->sound != 0) {
        mBGMPsComp_delete_ps_demo(actor->sound, 0x168);
    }

    switch (actor->happening_sound) {
        case BGM_INTRO_ARRIVE:
        case BGM_INTRO_NEW_TOWN:
            mBGMPsComp_delete_ps_happening(actor->happening_sound, 0x168);
            break;
    }

    NPC_CLIP->dt_proc(actorx, game);
}

static void aSTM_actor_init(ACTOR* actorx, GAME* game) {
    NPC_CLIP->init_proc(actorx, game);
}

static void aSTM_actor_draw(ACTOR* actorx, GAME* game) {
    NPC_CLIP->draw_proc(actorx, game);
}

#include "../src/actor/npc/ac_npc_station_master_move.c_inc"
#include "../src/actor/npc/ac_npc_station_master_talk.c_inc"
#include "../src/actor/npc/ac_npc_station_master_schedule.c_inc"

#ifdef TARGET_PC
/* pc_mp.c: an online trip leaves on Porter's train, as the card trip does once it has saved (aSTM_save_talk): the
 * train is called or he walks the player to the one waiting, sees them off, and pc_mp_train_left takes it from
 * there (aSTM_game_end_init). Only while he's at his post with the player close by and no talk going on */
int pc_aSTM_online_board(GAME_PLAY* play) {
    NPC_STATION_MASTER_ACTOR* actor =
        (NPC_STATION_MASTER_ACTOR*)Actor_info_fgName_search(&play->actor_info, SP_NPC_STATION_MASTER, ACTOR_PART_NPC);
    STRUCTURE_ACTOR* train1;

    if (actor == NULL || (actor->think_idx != aSTM_THINK_8 && actor->think_idx != aSTM_THINK_9) ||
        actor->npc_class.actor_class.player_distance_xz > 160.0f || mDemo_Get_talk_actor() != NULL) {
        return FALSE;
    }
    mPlib_request_main_demo_wait_type1((GAME*)play, FALSE, NULL);
    actor->station_check_res = 0;
    train1 = (STRUCTURE_ACTOR*)Actor_info_fgName_search(&play->actor_info, TRAIN1, ACTOR_PART_ITEM);
    if (train1 == NULL || train1->action > 5 || Common_Get(train_action) > 5) {
        actor->train_actor_p = NULL;
        Common_Set(train_coming_flag, 4);
        aSTM_setup_think_proc(actor, play, aSTM_THINK_MAKE_TRAIN_BF);
    } else {
        actor->train_actor_p = (ACTOR*)train1;
        Common_Set(train_coming_flag, 2);
        aSTM_Change_sound(actor, BGM_TRAIN_ARRIVAL_WAIT);
        aSTM_setup_think_proc(actor, play, aSTM_THINK_MOVE_TURN_BF);
    }
    return TRUE;
}
#endif
