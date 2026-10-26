/* ac-plus: Brewster, behind the counter of the Roost.
 *
 * He is City Folk's Brewster: his model (pc_cf_char.c) posed by Blathers' skeleton, and his
 * lines (pc_cf_lines.c). It goes the City Folk way: A by a stool sits the player at the
 * counter, and he greets them and offers a cup for 200 Bells, once a day (from 6 AM) ("Care for a cup of
 * joe?", pigeon milk or not). The cup comes on the counter (City Folk's own); "Thank you!"
 * drinks it, "It's gotta cool" gets him telling the player to drink it while it's hot, and then
 * the player says what they think of it. Talked to standing up, he points to the stools.
 *
 * With City Folk's animations extracted (pc_cf_anim.h) it all moves as there: he polishes a cup while
 * he waits, brews the coffee (and the pigeon milk) and pushes it over, and the player lifts it, sips,
 * says what they think and sets it down. When they get up he takes the cup back.
 *
 * He warms up over the days a player has a cup (pc_brewster_familiarity): plain greetings for a
 * stranger, then "Good to see you", then their name and "The usual, right?". */
#include "ac_npc_brewster.h"

#include "m_common_data.h"
#include "m_msg.h"
#include "m_choice.h"
#include "m_demo.h"
#include "m_private.h"
#include "m_player_lib.h"
#include "m_player.h"
#include "pc_brewster.h"
#include "ac_npc_kk_roost.h"
#include "pc_cf_anim.h"
#include "pc_cf_char.h"
#include "pc_cf_lines.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void aBRW_actor_ct(ACTOR* actorx, GAME* game);
static void aBRW_actor_dt(ACTOR* actorx, GAME* game);
static void aBRW_actor_move(ACTOR* actorx, GAME* game);
static void aBRW_actor_draw(ACTOR* actorx, GAME* game);
static void aBRW_actor_init(ACTOR* actorx, GAME* game);
static void aBRW_actor_save(ACTOR* actorx, GAME* game);

ACTOR_PROFILE Npc_Brewster_Profile = {
    mAc_PROFILE_NPC_BREWSTER,
    ACTOR_PART_NPC,
    ACTOR_STATE_NONE,
    SP_NPC_BREWSTER,
    ACTOR_OBJ_BANK_KEEP,
    sizeof(NPC_BREWSTER_ACTOR),
    &aBRW_actor_ct,
    &aBRW_actor_dt,
    &aBRW_actor_init,
    mActor_NONE_PROC1,
    &aBRW_actor_save,
};

/* His lines, numbered as in City Folk's NPC_master.bmg: first and count */
#define aBRW_MSG_GREET 2   /* 2-9 "Welcome to the Roost." ... */
#define aBRW_MSG_GREET_N 8
#define aBRW_MSG_SEAT 10   /* 10-15 "Counter seat's open..." */
#define aBRW_MSG_SEAT_N 6
#define aBRW_MSG_OFFER 17  /* 17-22 "Care for a cup of joe?" One, please / No, thanks */
#define aBRW_MSG_OFFER_N 6
#define aBRW_MSG_NO 23     /* 23-26 "Suit yourself." */
#define aBRW_MSG_NO_N 4
#define aBRW_MSG_BROKE 27  /* "You don't have enough Bells on you" */
#define aBRW_MSG_THANKS 28 /* 28-29 */
#define aBRW_MSG_THANKS_N 2
#define aBRW_MSG_SERVE 30  /* 30-33 */
#define aBRW_MSG_SERVE_N 4
#define aBRW_MSG_MILK 34   /* "a splash of pigeon milk?" Yes, please! / Bleh, no! */
#define aBRW_MSG_MILK_YES 35
#define aBRW_MSG_MILK_NO 36
#define aBRW_MSG_DRINK 37  /* 37-42 "Drink up." Thank you! / It's gotta cool. */
#define aBRW_MSG_DRINK_N 6
#define aBRW_MSG_COOL 43   /* 43-48 "What?!" I'll drink! / Still too hot. */
#define aBRW_MSG_COOL_N 6
#define aBRW_MSG_TASTE 49  /* 49-60 the player's own words about the coffee */
#define aBRW_MSG_TASTE_N 12
#define aBRW_MSG_THANK_YOU 61
#define aBRW_MSG_HAD 69    /* 69-74 after today's cup */
#define aBRW_MSG_HAD_N 6

/* Greeting, offer and serving lines warm up as he gets to know the player (pc_brewster_familiarity):
 * first and count for a stranger, someone he knows, and a regular */
static const u8 aBRW_greet_lines[3][2] = { { 2, 3 }, { 5, 2 }, { 7, 3 } };   /* "Hi." / "Good to see you." / by name */
static const u8 aBRW_offer_lines[3][2] = { { 17, 2 }, { 19, 2 }, { 21, 2 } }; /* .../ "What's your blend?" / "The usual" */
static const u8 aBRW_serve_lines[3][2] = { { 30, 2 }, { 30, 3 }, { 32, 2 } }; /* .../ "just the way you like it" */

static int aBRW_pick_known(const u8 lines[3][2]) {
    const u8* l = lines[pc_brewster_familiarity(Common_Get(player_no))];

    return l[0] + RANDOM(l[1]);
}

/* The counter stools (the Roost's room, pc_museum_rooms.c): seat centres and the counter top */
#define aBRW_STOOL_X 295.0f
#define aBRW_SEAT_X 308.75f /* where the seated player stands: City Folk's, from the café's room */
#define aBRW_HOME_X 390.0f  /* where City Folk's Brewster stands, across from the seat by z 300 */
#define aBRW_STOOL_NUM 3
#define aBRW_SIT_STOOL 2 /* the one City Folk seats the player on, across from Brewster */
static const f32 aBRW_stool_z[aBRW_STOOL_NUM] = { 215.0f, 255.0f, 295.0f };
#define aBRW_CUP_X 332.0f
#define aBRW_COUNTER_Y 70.0f
#define aBRW_CUP_SCALE (1.25f / PC_CF_CHAR_SCALE) /* City Folk units, as the room */
#define aBRW_DRINK_FRAMES 240 /* lift, two slow sips, set it down */
#define aBRW_MILK_FRAME 100.0f /* pge_milk1: the milk's in once the cup's down out of sight */

enum {
    aBRW_CUP_NONE,
    aBRW_CUP_COFFEE,
    aBRW_CUP_MILK,
    aBRW_CUP_EMPTY,
};

static void aBRW_talk_wait_line(NPC_BREWSTER_ACTOR* brw, GAME_PLAY* play);

static int aBRW_msg(int n, int last) {
    return PC_CF_MSG_BASE + (last ? PC_CF_MSG_LAST : 0) + n;
}

static int aBRW_pick(int first, int count) {
    return first + RANDOM(count);
}

static void aBRW_name_plate(NPC_BREWSTER_ACTOR* brw, int taste) {
    brw->taste = taste;
    mMsg_Get_base_window_p()->show_actor_name = !taste;
}

/* Next line, from the end of the one showing */
static void aBRW_say(NPC_BREWSTER_ACTOR* brw, int n) {
    mMsg_Set_continue_msg_num(mMsg_Get_base_window_p(), aBRW_msg(n, FALSE));
    brw->wait_msg = n;
    brw->talk_proc = &aBRW_talk_wait_line;
}

/* A last line: the window closes after it and the talk ends */
static void aBRW_say_last(NPC_BREWSTER_ACTOR* brw, int n) {
    mMsg_Set_continue_msg_num(mMsg_Get_base_window_p(), aBRW_msg(n, TRUE));
    brw->wait_msg = -1;
    brw->talk_proc = (aBRW_TALK_PROC)&none_proc1;
}

/* "guy"/"gal" in the player's words */
static void aBRW_set_strings(void) {
    static const u8 guy[16] = "guy             ";
    static const u8 gal[16] = "gal             ";

    mMsg_Set_free_str(mMsg_Get_base_window_p(), mMsg_FREE_STR1,
                      (u8*)(Now_Private->gender == mPr_SEX_FEMALE ? gal : guy), 16);
}

/* --- City Folk's animations --- */

static int aBRW_cf(void) {
    static int ok = -1;

    if (ok < 0) {
        ok = pc_cf_anim_frames("pge_wait1") > 0 && pc_cf_anim_frames("cafe_drink1") > 0 &&
             pc_cf_anim_player_available(PC_CF_PANIM_CAFE_DRINK1) && pc_cf_anim_player_available(PC_CF_PANIM_CAFE_DRINK2);
    }
    return ok;
}

static int aBRW_is_idle(const char* anim) {
    return anim == NULL || strcmp(anim, "pge_wait1") == 0 || strcmp(anim, "pge_wait2") == 0;
}

static void aBRW_body(NPC_BREWSTER_ACTOR* brw, const char* name, f32 speed, int loop) {
    int frames = pc_cf_anim_frames(name);

    brw->body_anim = name;
    brw->body_speed = speed;
    brw->body_loop = loop;
    brw->body_frame = speed < 0.0f && frames > 0 ? (f32)(frames - 1) : 0.0f;
    if (strcmp(name, "pge_pour1") == 0 || strcmp(name, "pge_milk1") == 0 ||
        (strcmp(name, "pge_hand1") == 0 && speed > 0.0f)) {
        brw->cup_anim = NULL; /* the cup's in his hands */
    }
}

/* The player's City Folk animation on the stool (m_player_main_sitdown_wait.c_inc), or -1 */
static void aBRW_player_anim(int which) {
    extern int pc_player_seated_anim;

    pc_player_seated_anim = which >= 0 ? PC_CF_PANIM_FIRST + which : -1;
}

static int aBRW_player_anim_done(GAME_PLAY* play, NPC_BREWSTER_ACTOR* brw) {
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);
    cKF_FrameControl_c* fc;

    if (player == NULL || player->animation0_idx != PC_CF_PANIM_FIRST + brw->player_anim) {
        return FALSE;
    }
    fc = &player->keyframe0.frame_control;
    return fc->current_frame >= fc->end_frame;
}

/* He gets on with it while the window's away, and then says line msg */
static void aBRW_talk_act_wait(NPC_BREWSTER_ACTOR* brw, GAME_PLAY* play);

static void aBRW_act(NPC_BREWSTER_ACTOR* brw, const char* a0, const char* a1, int msg, int last) {
    mMsg_request_main_disappear_wait_type1(mMsg_Get_base_window_p());
    brw->queue_num = 0;
    if (a1 != NULL) {
        brw->queue[brw->queue_num++] = a1;
    }
    aBRW_body(brw, a0, 1.0f, FALSE);
    brw->player_anim = -1;
    brw->after_msg = msg;
    brw->after_last = last;
    brw->wait_msg = -1;
    brw->talk_proc = &aBRW_talk_act_wait;
}

/* The player does something (the cup going with it), then line msg */
static void aBRW_act_player(NPC_BREWSTER_ACTOR* brw, int which, const char* cup_anim, int msg, int last) {
    mMsg_request_main_disappear_wait_type1(mMsg_Get_base_window_p());
    brw->queue_num = 0;
    aBRW_player_anim(which);
    brw->player_anim = which;
    brw->cup_anim = cup_anim;
    brw->cup_frame = 0.0f;
    brw->after_msg = msg;
    brw->after_last = last;
    brw->wait_msg = -1;
    brw->talk_proc = &aBRW_talk_act_wait;
}

static void aBRW_talk_act_wait(NPC_BREWSTER_ACTOR* brw, GAME_PLAY* play) {
    mMsg_Window_c* msg_p = mMsg_Get_base_window_p();
    int n = brw->after_msg;

    if (getenv("PC_BREWSTER_LOG") != NULL && play->game_frame % 60 == 0) {
        PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);

        printf("[Brewster] waiting: window %d (asked %d prio %d), queue %d, body %s, player anim %d (now %d, %.1f/%.1f, "
               "morph %.2f)\n",
               msg_p->main_index, msg_p->requested_main_index, msg_p->requested_priority, brw->queue_num, brw->body_anim, brw->player_anim,
               player != NULL ? player->animation0_idx : -1,
               player != NULL ? player->keyframe0.frame_control.current_frame : 0.0f,
               player != NULL ? player->keyframe0.frame_control.end_frame : 0.0f,
               player != NULL ? player->keyframe0.morph_counter : 0.0f);
    }
    if (mMsg_Check_not_series_main_wait(msg_p)) {
        mMsg_request_main_disappear_wait_type1(msg_p); /* not taken the first time: again */
        return;
    }
    if (mMsg_Check_main_wait(msg_p) != TRUE || brw->queue_num > 0 || !aBRW_is_idle(brw->body_anim)) {
        return;
    }
    if (brw->player_anim >= 0 && !aBRW_player_anim_done(play, brw)) {
        return;
    }
    if (n >= aBRW_MSG_TASTE && n < aBRW_MSG_TASTE + aBRW_MSG_TASTE_N) {
        aBRW_set_strings();
        aBRW_name_plate(brw, TRUE); /* the player's own words */
    }
    mMsg_ChangeMsgData(msg_p, aBRW_msg(n, brw->after_last));
    mMsg_Set_ForceNext(msg_p);
    if (!mMsg_request_main_appear_wait_type1(msg_p)) {
        return; /* again next frame */
    }
    if (getenv("PC_BREWSTER_LOG") != NULL) {
        printf("[Brewster] done; line %d%s\n", n, brw->after_last ? " (last)" : "");
    }
    if (brw->after_last) {
        brw->wait_msg = -1;
        brw->talk_proc = (aBRW_TALK_PROC)&none_proc1;
    } else {
        brw->wait_msg = n;
        brw->talk_proc = &aBRW_talk_wait_line;
    }
}

/* Each frame: his animation goes on, and what follows it */
static void aBRW_cf_move(NPC_BREWSTER_ACTOR* brw, GAME* game) {
    f32 step = (f32)game->graph->dt_num_60fps_frames;
    const char* idle = mDemo_IS_ACTOR_TALKING((ACTOR*)brw) ? "pge_wait2" : "pge_wait1";
    int frames;

    if (brw->body_anim == NULL || (aBRW_is_idle(brw->body_anim) && strcmp(brw->body_anim, idle) != 0)) {
        aBRW_body(brw, idle, 1.0f, TRUE);
    }
    frames = pc_cf_anim_frames(brw->body_anim);
    brw->body_frame += brw->body_speed * step;
    if (brw->body_loop) {
        while (brw->body_frame >= (f32)(frames - 1) && frames > 1) {
            brw->body_frame -= (f32)(frames - 1);
        }
    } else if ((brw->body_speed > 0.0f && brw->body_frame >= (f32)(frames - 1)) ||
               (brw->body_speed < 0.0f && brw->body_frame <= 0.0f)) {
        /* done */
        if (strcmp(brw->body_anim, "pge_hand1") == 0) {
            if (brw->body_speed > 0.0f) {
                brw->cup_anim = "pge_hand1"; /* the cup stays where he pushed it */
                brw->cup_frame = (f32)(frames - 1);
            } else {
                brw->cup = aBRW_CUP_NONE; /* taken back */
                brw->cup_anim = NULL;
            }
        }
        if (brw->queue_num > 0) {
            const char* next = brw->queue[0];

            brw->queue[0] = brw->queue[1];
            brw->queue[1] = brw->queue[2];
            brw->queue_num--;
            aBRW_body(brw, next, 1.0f, FALSE);
        } else {
            aBRW_body(brw, idle, 1.0f, TRUE);
        }
    }
    if (brw->body_anim != NULL && strcmp(brw->body_anim, "pge_milk1") == 0 && brw->body_frame >= aBRW_MILK_FRAME &&
        brw->cup == aBRW_CUP_COFFEE) {
        brw->cup = aBRW_CUP_MILK;
    }
    if (getenv("PC_BREWSTER_LOG") != NULL) {
        static const char* last;
        static u32 since;

        if (brw->body_anim != last) {
            printf("[Brewster] anim %s (speed %.0f) at frame %u, %s took %u\n", brw->body_anim, brw->body_speed,
                   ((GAME_PLAY*)game)->game_frame, last != NULL ? last : "-", ((GAME_PLAY*)game)->game_frame - since);
            last = brw->body_anim;
            since = ((GAME_PLAY*)game)->game_frame;
        }
    }
    if (brw->player_anim >= 0) {
        /* the cup goes with the player's animation (keys from 1) */
        PLAYER_ACTOR* player = GET_PLAYER_ACTOR((GAME_PLAY*)game);

        if (player != NULL && player->animation0_idx == PC_CF_PANIM_FIRST + brw->player_anim) {
            brw->cup_frame = player->keyframe0.frame_control.current_frame - 1.0f;
        }
    }
}

static void aBRW_cf_draw(NPC_BREWSTER_ACTOR* brw, GAME_PLAY* play) {
    static const char* const models[] = { "cup_coffee", "cup_coffee", "cup_milk", "cup_empty" };
    ACTOR* actorx = (ACTOR*)brw;
    pc_cf_mtx34 owner;
    const char* cup = models[brw->cup & 3];
    int eye = brw->npc_class.draw.tex_anim[aNPC_TEX_ANIM_EYE].pattern;
    int parts = PC_CF_ANIM_BODY | PC_CF_ANIM_PROPS | (brw->cup != aBRW_CUP_NONE ? PC_CF_ANIM_CUP : 0);

    pc_cf_anim_place(owner, aBRW_HOME_X, actorx->world.position.y, actorx->world.position.z, DEG2SHORT_ANGLE(-90.0f));
    pc_cf_anim_draw(play, brw->body_anim != NULL ? brw->body_anim : "pge_wait1", brw->body_frame, owner,
                    brw->stool >= 0 ? (const float(*)[4])brw->seat : NULL, cup, eye, aKKR_room_light(), parts);
    if (brw->cup_anim != NULL && brw->cup != aBRW_CUP_NONE) {
        pc_cf_anim_draw(play, brw->cup_anim, brw->cup_frame, owner, brw->stool >= 0 ? (const float(*)[4])brw->seat : NULL,
                        cup, eye, aKKR_room_light(), PC_CF_ANIM_CUP);
    }
}

/* The cup goes up while the window is away, then the player says what they think */
static void aBRW_talk_drink_wait(NPC_BREWSTER_ACTOR* brw, GAME_PLAY* play) {
    mMsg_Window_c* msg_p = mMsg_Get_base_window_p();
    int n;

    if (mMsg_Check_main_wait(msg_p) != TRUE) {
        return;
    }
    if (++brw->cup_timer < aBRW_DRINK_FRAMES) {
        return;
    }
    brw->cup = aBRW_CUP_EMPTY;
    n = aBRW_pick(aBRW_MSG_TASTE, aBRW_MSG_TASTE_N);
    aBRW_set_strings();
    aBRW_name_plate(brw, TRUE);
    mMsg_ChangeMsgData(msg_p, aBRW_msg(n, FALSE));
    mMsg_Set_ForceNext(msg_p);
    mMsg_request_main_appear_wait_type1(msg_p);
    brw->wait_msg = n;
    brw->talk_proc = &aBRW_talk_wait_line;
}

static void aBRW_drink(NPC_BREWSTER_ACTOR* brw) {
    pc_brewster_set_coffee_today(Common_Get(player_no));
    if (aBRW_cf()) {
        /* City Folk's: up, sip, and a turn to say what they think, cup in hand */
        aBRW_act_player(brw, PC_CF_PANIM_CAFE_DRINK1, "cafe_drink1", aBRW_pick(aBRW_MSG_TASTE, aBRW_MSG_TASTE_N), FALSE);
        return;
    }
    mMsg_request_main_disappear_wait_type1(mMsg_Get_base_window_p());
    brw->cup_timer = 0;
    brw->wait_msg = -1;
    brw->talk_proc = &aBRW_talk_drink_wait;
}

/* A line finished: what's next */
static void aBRW_line_done(NPC_BREWSTER_ACTOR* brw, int msg, int choice) {
    if (msg >= aBRW_MSG_GREET && msg < aBRW_MSG_GREET + aBRW_MSG_GREET_N) {
        if (!brw->seated_talk) {
            aBRW_say_last(brw, aBRW_pick(aBRW_MSG_SEAT, aBRW_MSG_SEAT_N));
        } else if (pc_brewster_had_coffee_today(Common_Get(player_no))) {
            aBRW_say_last(brw, aBRW_pick(aBRW_MSG_HAD, aBRW_MSG_HAD_N));
        } else {
            aBRW_say(brw, aBRW_pick_known(aBRW_offer_lines));
        }
    } else if (msg >= aBRW_MSG_OFFER && msg < aBRW_MSG_OFFER + aBRW_MSG_OFFER_N) {
        if (choice != 0) {
            aBRW_say_last(brw, aBRW_pick(aBRW_MSG_NO, aBRW_MSG_NO_N));
        } else if (Now_Private->inventory.wallet < PC_BREWSTER_PRICE) {
            aBRW_say_last(brw, aBRW_MSG_BROKE);
        } else {
            Now_Private->inventory.wallet -= PC_BREWSTER_PRICE;
            aBRW_say(brw, aBRW_pick(aBRW_MSG_THANKS, aBRW_MSG_THANKS_N));
        }
    } else if (msg >= aBRW_MSG_THANKS && msg < aBRW_MSG_THANKS + aBRW_MSG_THANKS_N) {
        if (!aBRW_cf()) {
            brw->cup = aBRW_CUP_COFFEE; /* here you go */
        }
        aBRW_say(brw, aBRW_pick_known(aBRW_serve_lines));
    } else if (msg >= aBRW_MSG_SERVE && msg < aBRW_MSG_SERVE + aBRW_MSG_SERVE_N) {
        if (aBRW_cf()) {
            brw->cup = aBRW_CUP_COFFEE;
            aBRW_act(brw, "pge_pour1", NULL, aBRW_MSG_MILK, FALSE); /* he brews it */
        } else {
            aBRW_say(brw, aBRW_MSG_MILK);
        }
    } else if (msg == aBRW_MSG_MILK) {
        if (choice == 0 && !aBRW_cf()) {
            brw->cup = aBRW_CUP_MILK;
        }
        aBRW_say(brw, choice == 0 ? aBRW_MSG_MILK_YES : aBRW_MSG_MILK_NO);
    } else if (msg == aBRW_MSG_MILK_YES || msg == aBRW_MSG_MILK_NO) {
        if (aBRW_cf()) {
            /* the milk (out of sight below the counter), then the cup pushed over */
            aBRW_act(brw, msg == aBRW_MSG_MILK_YES ? "pge_milk1" : "pge_hand1",
                     msg == aBRW_MSG_MILK_YES ? "pge_hand1" : NULL, aBRW_pick(aBRW_MSG_DRINK, aBRW_MSG_DRINK_N), FALSE);
        } else {
            aBRW_say(brw, aBRW_pick(aBRW_MSG_DRINK, aBRW_MSG_DRINK_N));
        }
    } else if ((msg >= aBRW_MSG_DRINK && msg < aBRW_MSG_DRINK + aBRW_MSG_DRINK_N) ||
               (msg >= aBRW_MSG_COOL && msg < aBRW_MSG_COOL + aBRW_MSG_COOL_N)) {
        if (choice == 0) {
            aBRW_drink(brw);
        } else {
            aBRW_say(brw, aBRW_pick(aBRW_MSG_COOL, aBRW_MSG_COOL_N));
        }
    } else if (msg >= aBRW_MSG_TASTE && msg < aBRW_MSG_TASTE + aBRW_MSG_TASTE_N) {
        aBRW_name_plate(brw, FALSE);
        if (aBRW_cf()) {
            brw->cup = aBRW_CUP_EMPTY;
            aBRW_act_player(brw, PC_CF_PANIM_CAFE_DRINK2, "cafe_drink2", aBRW_MSG_THANK_YOU, TRUE); /* set it down */
        } else {
            aBRW_say_last(brw, aBRW_MSG_THANK_YOU);
        }
    } else {
        aBRW_say_last(brw, aBRW_MSG_THANK_YOU);
    }
}

static void aBRW_talk_wait_line(NPC_BREWSTER_ACTOR* brw, GAME_PLAY* play) {
    mMsg_Window_c* msg_p = mMsg_Get_base_window_p();

    if (brw->wait_msg >= 0 && mMsg_Get_msg_num(msg_p) == aBRW_msg(brw->wait_msg, FALSE) &&
        mMsg_Check_MainNormalContinue(msg_p) == TRUE) {
        int choice = mChoice_Get_ChoseNum(mChoice_Get_base_window_p());
        static const char* dev = (const char*)-1;

        /* dev: PC_BREWSTER_PICKS=0,1,... answers his questions in turn instead of the player */
        if (dev == (const char*)-1) {
            dev = getenv("PC_BREWSTER_PICKS");
        }
        if (dev != NULL && *dev != '\0' && pc_cf_lines_has_choices(PC_CF_BREWSTER, brw->wait_msg)) {
            choice = atoi(dev);
            dev = strchr(dev, ',');
            dev = dev != NULL ? dev + 1 : "";
        }
        if (getenv("PC_BREWSTER_LOG") != NULL) {
            printf("[Brewster] line %d done, choice %d, wallet %u\n", brw->wait_msg, choice,
                   (unsigned)Now_Private->inventory.wallet);
            fflush(stdout);
        }
        aBRW_line_done(brw, brw->wait_msg, choice);
    }
}

/* --- talking --- */

static void aBRW_set_talk_info(ACTOR* actorx) {
    NPC_BREWSTER_ACTOR* brw = (NPC_BREWSTER_ACTOR*)actorx;
    int msg = aBRW_pick_known(aBRW_greet_lines);

    pc_cf_lines_set_talking(PC_CF_BREWSTER);
    if (brw->seated_talk) {
        mDemo_Set_talk_change_player(FALSE); /* the player stays on the stool */
    }
    mDemo_Set_msg_num(aBRW_msg(msg, FALSE));
    brw->wait_msg = msg;
}

/* --- the stools --- */

/* The stool the player stands by, or -1: behind it (on the room's side), or beside it facing it, as a chair at
 * home takes the player from its side (ac_my_room_action.c_inc). Facing matters beside it: the middle stool is
 * close, and a player there faces Brewster to talk to him. Only the one across from Brewster seats the player,
 * as in City Folk. */
static int aBRW_stool_near(const PLAYER_ACTOR* player) {
    const xyz_t* p = &player->actor_class.world.position;
    int i;

    for (i = aBRW_SIT_STOOL; i <= aBRW_SIT_STOOL; i++) {
        f32 dz = aBRW_stool_z[i] - p->z;

        if (p->x > aBRW_STOOL_X - 50.0f && p->x < aBRW_STOOL_X + 8.0f && ABS(dz) < 20.0f) {
            return i; /* behind it */
        }
        if (p->x > aBRW_STOOL_X - 22.0f && p->x < aBRW_STOOL_X + 22.0f && ABS(dz) >= 20.0f && ABS(dz) < 45.0f) {
            /* beside it: facing it, within 60 degrees */
            s16 to = (s16)(atan2f(aBRW_STOOL_X - p->x, dz) * (32768.0f / 3.14159265f));
            s16 off = (s16)(player->actor_class.shape_info.rotation.y - to);

            if (ABS(off) < DEG2SHORT_ANGLE(60.0f)) {
                return i;
            }
        }
    }
    return -1;
}

/* The stool the player sits on, or -1 */
static int aBRW_stool_seated(GAME* game) {
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR((GAME_PLAY*)game);
    int idx = mPlib_get_player_actor_main_index(game);
    int i;

    if (player == NULL || (idx != mPlayer_INDEX_SITDOWN_WAIT && idx != mPlayer_INDEX_SITDOWN)) {
        return -1;
    }
    for (i = 0; i < aBRW_STOOL_NUM; i++) {
        if (ABS(player->actor_class.world.position.x - aBRW_SEAT_X) < 14.0f &&
            ABS(player->actor_class.world.position.z - aBRW_stool_z[i]) < 14.0f) {
            return i;
        }
    }
    return -1;
}

static void aBRW_stools(NPC_BREWSTER_ACTOR* brw, GAME* game) {
    GAME_PLAY* play = (GAME_PLAY*)game;
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);
    int idx = mPlib_get_player_actor_main_index(game);
    int seated = aBRW_stool_seated(game);

    if (player == NULL) {
        return;
    }
    /* By the stool, A is the stool's: he's out of reach there (his reach across the counter covers it), and sitting
     * down brings him over anyway */
    ((ACTOR*)brw)->talk_distance = aBRW_stool_near(player) >= 0 ? 0.0f : 110.0f;
    if ((idx == mPlayer_INDEX_WAIT || idx == mPlayer_INDEX_WALK || idx == mPlayer_INDEX_RUN) && chkTrigger(BUTTON_A)) {
        int s = aBRW_stool_near(player);

        if (s >= 0) {
            xyz_t pos = { aBRW_SEAT_X + 35.0f, 0.0f, 0.0f }; /* sitting backs the player 35 onto the seat */

            pos.z = aBRW_stool_z[s];
            pos.y = player->actor_class.world.position.y;
            /* as a chair's: the direction given is the seat's back, and the player turns to face away from it */
            if (mPlib_request_main_sitdown_type1(game, 1, &pos, -1)) {
                brw->sit_frame = play->game_frame;
            }
            if (getenv("PC_BREWSTER_LOG") != NULL) {
                printf("[Brewster] A by stool %d (player main %d)\n", s, idx);
            }
        }
    }
    if (mDemo_IS_ACTOR_TALKING((ACTOR*)brw)) {
        brw->greeted = TRUE;
    }
    {
        /* getting up from a stool: off it backwards, facing the room (pc_player_standup_turn) */
        extern int pc_player_standup_turn;

        pc_player_standup_turn = seated >= 0;
    }
    if (seated >= 0) {
        /* where the cup goes */
        pc_cf_anim_place(brw->seat, player->actor_class.world.position.x, player->actor_class.world.position.y,
                         player->actor_class.world.position.z, player->actor_class.shape_info.rotation.y);
    } else {
        extern int pc_player_seated_anim;

        pc_player_seated_anim = -1;
    }
    if (seated >= 0 && idx == mPlayer_INDEX_SITDOWN_WAIT && !brw->greeted) {
        /* sat down: he comes over (asked each frame until the talk starts) */
        brw->stool = seated;
        brw->seated_talk = TRUE;
        /* he speaks first, as a villager walking up does, and the player stays seated */
        mDemo_Request(mDemo_TYPE_SPEAK, (ACTOR*)brw, &aBRW_set_talk_info);
        if (getenv("PC_BREWSTER_LOG") != NULL && play->game_frame % 60 == 0) {
            printf("[Brewster] the player sits at stool %d\n", seated);
        }
    } else if (seated < 0 && !mDemo_IS_ACTOR_TALKING((ACTOR*)brw)) {
        /* up again: the counter's cleared */
        brw->greeted = FALSE;
        if (!aBRW_cf()) {
            brw->cup = aBRW_CUP_NONE;
        } else if (brw->cup != aBRW_CUP_NONE && brw->cup_anim != NULL && aBRW_is_idle(brw->body_anim)) {
            /* he takes the cup back: the push over, the other way (pc_cf_anim.h) */
            brw->cup = aBRW_CUP_EMPTY;
            brw->cup_anim = NULL;
            aBRW_body(brw, "pge_hand1", -1.0f, FALSE);
        }
    }
}

static void aBRW_talk_request(ACTOR* actorx, GAME* game) {
    NPC_BREWSTER_ACTOR* brw = (NPC_BREWSTER_ACTOR*)actorx;
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR((GAME_PLAY*)game);

    if (((GAME_PLAY*)game)->game_frame - brw->sit_frame < 4 ||
        (player != NULL && aBRW_stool_seated(game) < 0 && aBRW_stool_near(player) >= 0)) {
        return; /* that A was for a stool */
    }
    brw->seated_talk = aBRW_stool_seated(game) >= 0;
    if (getenv("PC_BREWSTER_LOG") != NULL) {
        printf("[Brewster] talk request, seated %d, player main %d\n", brw->seated_talk,
               mPlib_get_player_actor_main_index(game));
    }
    mDemo_Request(mDemo_TYPE_TALK, actorx, &aBRW_set_talk_info);
}

static int aBRW_talk_init(ACTOR* actorx, GAME* game) {
    NPC_BREWSTER_ACTOR* brw = (NPC_BREWSTER_ACTOR*)actorx;

    brw->talk_proc = &aBRW_talk_wait_line;
    mDemo_Set_ListenAble();
    brw->npc_class.head.angle_add_y = DEG2SHORT_ANGLE(5.625f);
    return TRUE;
}

static int aBRW_talk_end_chk(ACTOR* actorx, GAME* game) {
    NPC_BREWSTER_ACTOR* brw = (NPC_BREWSTER_ACTOR*)actorx;

    (*brw->talk_proc)(brw, (GAME_PLAY*)game);
    if (brw->taste) {
        mMsg_Get_base_window_p()->show_actor_name = FALSE; /* the player's own words, whatever reopened the window */
    }
    if (!mDemo_IS_ACTOR_TALKING(actorx)) {
        if (brw->taste) {
            aBRW_name_plate(brw, FALSE);
        }
        pc_cf_lines_set_talking(-1);
        if (getenv("PC_BREWSTER_LOG") != NULL) {
            printf("[Brewster] talk over\n");
        }
        aBRW_player_anim(-1); /* back to sitting still */
        brw->player_anim = -1;
        return TRUE;
    }
    return FALSE;
}

/* --- standing behind the counter (Blathers' awake schedule) --- */

static void aBRW_set_request_act(NPC_BREWSTER_ACTOR* brw) {
    brw->npc_class.request.act_priority = 4;
    brw->npc_class.request.act_idx = 16;
    brw->npc_class.request.act_type = aNPC_ACT_TYPE_SEARCH;
}

static void aBRW_act_proc(NPC_ACTOR* actorx, GAME_PLAY* play, int action) {
    switch (action) {
        case 0: /* init */
            (*Common_Get(clip).npc_clip->animation_init_proc)(actorx, aNPC_ANIM_WAIT1, FALSE);
            actorx->condition_info.demo_flg = 0;
            actorx->head.angle_add_y = DEG2SHORT_ANGLE(5.625f);
            actorx->movement.mv_angl = DEG2SHORT_ANGLE(-90.0f); /* across the counter, to the stools */
            actorx->movement.mv_add_angl = DEG2SHORT_ANGLE(5.625f);
            actorx->action.step = 0;
            break;
        case 1: /* change data */
            actorx->action.act_obj = aNPC_ACT_OBJ_PLAYER;
            break;
    }
}

static void aBRW_think_proc(NPC_ACTOR* actorx, GAME_PLAY* play, int think_idx) {
    if (think_idx == 0) {
        actorx->action.act_proc = &aBRW_act_proc;
    }
    if (think_idx == 0 || actorx->action.step == aNPC_ACTION_END_STEP) {
        aBRW_set_request_act((NPC_BREWSTER_ACTOR*)actorx);
    }
}

static void aBRW_schedule_proc(NPC_ACTOR* actorx, GAME_PLAY* play, int proc_idx) {
    if (proc_idx == 0) {
        actorx->think.think_proc = &aBRW_think_proc;
        actorx->condition_info.hide_request = FALSE;
        (*Common_Get(clip).npc_clip->think_proc)(actorx, play, aNPC_THINK_IN_BLOCK, aNPC_THINK_TYPE_INIT);
    } else if ((*Common_Get(clip).npc_clip->think_proc)(actorx, play, -1, aNPC_THINK_TYPE_CHK_INTERRUPT) == FALSE) {
        (*Common_Get(clip).npc_clip->think_proc)(actorx, play, -1, aNPC_THINK_TYPE_MAIN);
    }
}

static void aBRW_actor_ct(ACTOR* actorx, GAME* game) {
    static aNPC_ct_data_c ct_data = {
        &aBRW_actor_move, &aBRW_actor_draw, 5, &aBRW_talk_request, &aBRW_talk_init, &aBRW_talk_end_chk, 0,
    };
    NPC_BREWSTER_ACTOR* brw = (NPC_BREWSTER_ACTOR*)actorx;

    pc_brewster_skeleton(); /* before the NPC system reads it */
    if ((*Common_Get(clip).npc_clip->birth_check_proc)(actorx, game) == TRUE) {
        brw->npc_class.schedule.schedule_proc = &aBRW_schedule_proc;
        (*Common_Get(clip).npc_clip->ct_proc)(actorx, game, &ct_data);
        actorx->status_data.weight = 255;
        actorx->talk_distance = 110.0f; /* across the counter, as the shops' counters */
        brw->talk_proc = &aBRW_talk_wait_line;
        brw->wait_msg = -1;
        brw->stool = -1;
        brw->player_anim = -1;
        brw->after_msg = -1;
    }
}

static void aBRW_actor_save(ACTOR* actorx, GAME* game) {
    (*Common_Get(clip).npc_clip->save_proc)(actorx, game);
}

static void aBRW_actor_dt(ACTOR* actorx, GAME* game) {
    extern ACTOR* pc_mDemo_seated_speaker;

    if (pc_mDemo_seated_speaker == actorx) {
        pc_mDemo_seated_speaker = NULL;
    }
    aBRW_player_anim(-1);
    pc_cf_lines_set_talking(-1);
    (*Common_Get(clip).npc_clip->dt_proc)(actorx, game);
}

static void aBRW_actor_init(ACTOR* actorx, GAME* game) {
    (*Common_Get(clip).npc_clip->init_proc)(actorx, game);
}

/* The cup on the counter, or on its way to the player's beak and back while they drink */
static void aBRW_draw_cup(NPC_BREWSTER_ACTOR* brw, GAME_PLAY* play) {
    static const char* const models[] = { NULL, "cup_coffee", "cup_milk", "cup_empty" };
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);
    pc_cf_mtx34 m;
    xyz_t pos = { aBRW_CUP_X, aBRW_COUNTER_Y, 0.0f };
    f32 tilt = 0.0f, t, c, sn;
    int cup = brw->cup;

    if (cup == aBRW_CUP_NONE || brw->stool < 0 || brw->stool >= aBRW_STOOL_NUM) {
        return;
    }
    pos.z = aBRW_stool_z[brw->stool];
    if (brw->talk_proc == &aBRW_talk_drink_wait && player != NULL) {
        /* up, sip, down: the empty cup comes back */
        xyz_t lip = player->actor_class.world.position;
        f32 k;

        lip.x += 9.0f;
        lip.y += 46.0f;
        t = brw->cup_timer / (f32)aBRW_DRINK_FRAMES;
        /* a moment, up (0.08-0.3), at the beak (two sips), down (0.8-0.95) */
        k = t < 0.08f ? 0.0f : t < 0.3f ? (t - 0.08f) / 0.22f : t < 0.8f ? 1.0f : t < 0.95f ? (0.95f - t) / 0.15f : 0.0f;
        k = k * k * (3.0f - 2.0f * k);
        pos.x += (lip.x - pos.x) * k;
        pos.y += (lip.y - pos.y) * k;
        pos.z += (lip.z - pos.z) * k;
        if (t > 0.3f && t < 0.8f) {
            f32 sip = (t - 0.3f) / 0.25f; /* two sips: tip, hold, back */

            sip -= floorf(sip);
            tilt = 0.75f * sinf(sip * 3.14159f);
        }
        if (t > 0.55f) {
            cup = aBRW_CUP_EMPTY;
        }
    }
    /* a turn about z (tipping towards the player, who faces +x), at the room's scale */
    c = cosf(tilt) * aBRW_CUP_SCALE;
    sn = sinf(tilt) * aBRW_CUP_SCALE;
    m[0][0] = c, m[0][1] = -sn, m[0][2] = 0.0f, m[0][3] = pos.x;
    m[1][0] = sn, m[1][1] = c, m[1][2] = 0.0f, m[1][3] = pos.y;
    m[2][0] = 0.0f, m[2][1] = 0.0f, m[2][2] = aBRW_CUP_SCALE, m[2][3] = pos.z;
    pc_cf_char_play_draw(play, models[cup], m, 0, NULL, NULL, 0, aKKR_room_light());
}

/* Blathers' skeleton draws nothing (pc_brewster_skeleton); City Folk's Brewster goes where it stood */
static void aBRW_actor_draw(ACTOR* actorx, GAME* game) {
    NPC_BREWSTER_ACTOR* brw = (NPC_BREWSTER_ACTOR*)actorx;

    if (aBRW_cf()) {
        /* his own animations (the skeleton still draws, empty, for his shadow) */
        (*Common_Get(clip).npc_clip->draw_proc)(actorx, game);
        aBRW_cf_draw(brw, (GAME_PLAY*)game);
        return;
    }
    pc_brewster_draw_begin();
    (*Common_Get(clip).npc_clip->draw_proc)(actorx, game);
    pc_brewster_draw_end((GAME_PLAY*)game, actorx, brw->npc_class.draw.tex_anim[aNPC_TEX_ANIM_EYE].pattern);
    aBRW_draw_cup(brw, (GAME_PLAY*)game);
}

static void aBRW_actor_move(ACTOR* actorx, GAME* game) {
    extern ACTOR* pc_mDemo_seated_speaker;

    pc_mDemo_seated_speaker = actorx;
    aBRW_stools((NPC_BREWSTER_ACTOR*)actorx, game);
    (*Common_Get(clip).npc_clip->move_proc)(actorx, game);
    if (aBRW_cf()) {
        aBRW_cf_move((NPC_BREWSTER_ACTOR*)actorx, game);
    }
}
