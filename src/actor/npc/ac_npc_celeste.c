/* ac-plus: Celeste, who minds the telescope in the museum's entrance hall.
 *
 * She stands and turns to whoever talks to her; her lines are City Folk's (pc_celeste.c)
 * and her talk goes the City Folk way: "Where am I?", "Constellations!" (make one, change
 * one), "Stargazing!" (look, or when one is best seen), "Erasing!". Making, looking and
 * choosing happen in her telescope (pc_constellation_edit.c); naming in the game's own
 * name editor. */
#include "ac_npc_celeste.h"

#include "m_common_data.h"
#include "m_msg.h"
#include "m_choice.h"
#include "m_demo.h"
#include "m_submenu.h"
#include "m_ledit_ovl.h"
#include "pc_celeste.h"
#include "pc_constellation.h"
#include "pc_constellation_edit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_pc_paused;

static void aCEL_actor_ct(ACTOR* actorx, GAME* game);
static void aCEL_actor_dt(ACTOR* actorx, GAME* game);
static void aCEL_actor_move(ACTOR* actorx, GAME* game);
static void aCEL_actor_draw(ACTOR* actorx, GAME* game);
static void aCEL_actor_init(ACTOR* actorx, GAME* game);
static void aCEL_actor_save(ACTOR* actorx, GAME* game);

ACTOR_PROFILE Npc_Celeste_Profile = {
    mAc_PROFILE_NPC_CELESTE,
    ACTOR_PART_NPC,
    ACTOR_STATE_NONE,
    SP_NPC_CELESTE,
    ACTOR_OBJ_BANK_KEEP,
    sizeof(NPC_CELESTE_ACTOR),
    &aCEL_actor_ct,
    &aCEL_actor_dt,
    &aCEL_actor_init,
    mActor_NONE_PROC1,
    &aCEL_actor_save,
};

/* Her lines, numbered as in City Folk's NPC_hu-ko.bmg */
enum {
    aCEL_MSG_GREET_A = 4,     /* "Welcome to (town)'s observatory!" */
    aCEL_MSG_GREET_SLEEPY = 5,
    aCEL_MSG_GREET_B = 6,
    aCEL_MSG_MENU = 11,       /* Where am I? / Constellations! / You're adorable! / Never mind. */
    aCEL_MSG_WHERE = 12,
    aCEL_MSG_ADORE_A = 14,
    aCEL_MSG_ADORE_B = 15,
    aCEL_MSG_BYE = 16,
    aCEL_MSG_FIRST = 17,      /* first visit */
    aCEL_MSG_SCOPE = 22,      /* Constellations! / Stargazing! / Forget it. */
    aCEL_MSG_SCOPE_ERASE = 23, /* ... / Erasing! / Forget it. */
    aCEL_MSG_FORGET = 26,
    aCEL_MSG_ENJOY = 27,
    aCEL_MSG_MAKE_CHANGE = 28, /* Make a new one! / Change one! / Never mind. */
    aCEL_MSG_FULL = 29,
    aCEL_MSG_CHANGED_MIND = 30,
    aCEL_MSG_WHICH_CHANGE = 31,
    aCEL_MSG_RENAME = 33,     /* New name! / It's fine. */
    aCEL_MSG_NAME_IT = 34,
    aCEL_MSG_REGISTER = 35,
    aCEL_MSG_LOG = 36,        /* best on (day) of (month) at 8:00... Yes, please! / Nah, never mind. */
    aCEL_MSG_SAME_NAME = 38,
    aCEL_MSG_LOGGED = 39,
    aCEL_MSG_DO_ENJOY = 42,
    aCEL_MSG_ALL_DONE = 43,
    aCEL_MSG_LOOK_TIMES = 44, /* Look at them! / Check times! / Never mind. */
    aCEL_MSG_SHAME = 45,
    aCEL_MSG_WHICH_TIMES = 46,
    aCEL_MSG_WHICH_ERASE = 48,
    aCEL_MSG_KEEP = 49,
    aCEL_MSG_ERASE = 50,      /* Erase it. / No, wait! */
    aCEL_MSG_ERASED = 51,
    aCEL_MSG_AGAIN = 52,
    aCEL_MSG_AGAIN_ERASE = 53,
    aCEL_MSG_TONIGHT = 56,
    aCEL_MSG_RIGHT_NOW = 57,
    aCEL_MSG_BEST_DATE = 59,
    aCEL_MSG_RULES = 80,
};

/* What to open once her window has closed */
enum {
    aCEL_OPEN_NONE,
    aCEL_OPEN_MAKE,
    aCEL_OPEN_VIEW,
    aCEL_OPEN_PICK,
    aCEL_OPEN_NAME,
};

/* Why the list was opened */
enum {
    aCEL_PICK_CHANGE,
    aCEL_PICK_ERASE,
    aCEL_PICK_TIMES,
};

#define aCEL_FLAG_MET 0
#define aCEL_FLAG_RULES 1

static void aCEL_talk_wait_line(NPC_CELESTE_ACTOR* cel, GAME_PLAY* play);

static int aCEL_msg(int n, int last) {
    return PC_CELESTE_MSG_BASE + (last ? PC_CELESTE_MSG_LAST : 0) + n;
}

static int aCEL_player(void) {
    return Common_Get(player_no);
}

static int aCEL_flag(int which) {
    return (pc_const_celeste_flags()[which] >> aCEL_player()) & 1;
}

static void aCEL_set_flag(int which) {
    pc_const_celeste_flags()[which] |= (u8)(1 << aCEL_player());
}

static int aCEL_own_count(void) {
    int i, n = 0;

    for (i = 0; i < PC_CONST_MAX; i++) {
        n += pc_const_get(i)->used && pc_const_get(i)->owner == aCEL_player();
    }
    return n;
}

static int aCEL_free_slot(void) {
    int i;

    for (i = 0; i < PC_CONST_MAX; i++) {
        if (!pc_const_get(i)->used) {
            return i;
        }
    }
    return -1;
}

/* Free strings for her lines: 0 the constellation's name, 1 day, 2 month, 3 hour, 4 minute, 5 AM/PM */
static void aCEL_set_strings(const pc_constellation_c* c) {
    mMsg_Window_c* msg_p = mMsg_Get_base_window_p();
    u8 name[16], h[16], m[16], ampm[16], mon[16], day[16];
    int hour, minute, month, dd;

    pc_celeste_name_str(c, name);
    pc_celeste_peak_tonight(c, &hour, &minute);
    pc_celeste_time_strs(hour, minute, h, m, ampm);
    pc_celeste_peak_date(c, &month, &dd);
    pc_celeste_date_strs(month, dd, mon, day);
    mMsg_Set_free_str(msg_p, mMsg_FREE_STR0, name, 16);
    mMsg_Set_free_str(msg_p, mMsg_FREE_STR1, day, 16);
    mMsg_Set_free_str(msg_p, mMsg_FREE_STR2, mon, 16);
    mMsg_Set_free_str(msg_p, mMsg_FREE_STR3, h, 16);
    mMsg_Set_free_str(msg_p, mMsg_FREE_STR4, m, 16);
    mMsg_Set_free_str(msg_p, mMsg_FREE_STR5, ampm, 16);
}

/* --- moving her talk along --- */

/* Next line, from the end of the one showing */
static void aCEL_say(NPC_CELESTE_ACTOR* cel, int n) {
    mMsg_Set_continue_msg_num(mMsg_Get_base_window_p(), aCEL_msg(n, FALSE));
    cel->wait_msg = n;
    cel->talk_proc = &aCEL_talk_wait_line;
}

/* A last line: the window closes after it and the talk ends */
static void aCEL_say_last(NPC_CELESTE_ACTOR* cel, int n) {
    mMsg_Set_continue_msg_num(mMsg_Get_base_window_p(), aCEL_msg(n, TRUE));
    cel->wait_msg = -1;
    cel->talk_proc = (aCEL_TALK_PROC)&none_proc1;
}

/* Bring the window back with line n, after the telescope or the name editor */
static void aCEL_reopen(NPC_CELESTE_ACTOR* cel, int n) {
    mMsg_Window_c* msg_p = mMsg_Get_base_window_p();

    mMsg_ChangeMsgData(msg_p, aCEL_msg(n, FALSE));
    mMsg_Set_ForceNext(msg_p);
    mMsg_request_main_appear_wait_type1(msg_p);
    cel->wait_msg = n;
    cel->talk_proc = &aCEL_talk_wait_line;
}

static void aCEL_talk_open_wait(NPC_CELESTE_ACTOR* cel, GAME_PLAY* play);

/* Close the window, then open something */
static void aCEL_close_then(NPC_CELESTE_ACTOR* cel, int what) {
    mMsg_request_main_disappear_wait_type1(mMsg_Get_base_window_p());
    cel->after_close = what;
    cel->talk_proc = &aCEL_talk_open_wait;
}

static void aCEL_again(NPC_CELESTE_ACTOR* cel) {
    aCEL_say(cel, aCEL_own_count() > 0 ? aCEL_MSG_AGAIN_ERASE : aCEL_MSG_AGAIN);
}

static void aCEL_make_new(NPC_CELESTE_ACTOR* cel) {
    if (aCEL_free_slot() < 0) {
        aCEL_say(cel, aCEL_MSG_FULL);
        return;
    }
    memset(&cel->work, 0, sizeof(cel->work));
    memset(cel->work.name, ' ', sizeof(cel->work.name));
    cel->slot = -1;
    if (!aCEL_flag(aCEL_FLAG_RULES)) {
        aCEL_say(cel, aCEL_MSG_RULES);
    } else {
        aCEL_close_then(cel, aCEL_OPEN_MAKE);
    }
}

/* The telescope menus: 22/52 Constellations! / Stargazing! / bye, 23/53 with Erasing! before bye */
static void aCEL_scope_choice(NPC_CELESTE_ACTOR* cel, int msg, int choice) {
    int with_erase = msg == aCEL_MSG_SCOPE_ERASE || msg == aCEL_MSG_AGAIN_ERASE;
    int bye = msg == aCEL_MSG_SCOPE || msg == aCEL_MSG_SCOPE_ERASE ? aCEL_MSG_FORGET : aCEL_MSG_BYE;

    if (choice == 0) {
        if (aCEL_own_count() > 0) {
            aCEL_say(cel, aCEL_MSG_MAKE_CHANGE);
        } else {
            aCEL_make_new(cel);
        }
    } else if (choice == 1) {
        aCEL_say(cel, aCEL_MSG_LOOK_TIMES);
    } else if (choice == 2 && with_erase) {
        aCEL_say(cel, aCEL_MSG_WHICH_ERASE);
    } else {
        aCEL_say_last(cel, bye);
    }
}

/* A line finished: what's next */
static void aCEL_line_done(NPC_CELESTE_ACTOR* cel, int msg, int choice) {
    switch (msg) {
        case aCEL_MSG_FIRST:
        case aCEL_MSG_GREET_A:
        case aCEL_MSG_GREET_B:
        case aCEL_MSG_GREET_SLEEPY:
            aCEL_say(cel, aCEL_MSG_MENU);
            break;
        case aCEL_MSG_MENU:
            switch (choice) {
                case 0:
                    aCEL_say(cel, aCEL_MSG_WHERE);
                    break;
                case 1:
                    aCEL_say(cel, aCEL_own_count() > 0 ? aCEL_MSG_SCOPE_ERASE : aCEL_MSG_SCOPE);
                    break;
                case 2:
                    aCEL_say_last(cel, RANDOM(2) ? aCEL_MSG_ADORE_A : aCEL_MSG_ADORE_B);
                    break;
                default:
                    aCEL_say_last(cel, aCEL_MSG_BYE);
                    break;
            }
            break;
        case aCEL_MSG_WHERE:
            aCEL_say(cel, aCEL_own_count() > 0 ? aCEL_MSG_SCOPE_ERASE : aCEL_MSG_SCOPE);
            break;
        case aCEL_MSG_SCOPE:
        case aCEL_MSG_SCOPE_ERASE:
        case aCEL_MSG_AGAIN:
        case aCEL_MSG_AGAIN_ERASE:
            aCEL_scope_choice(cel, msg, choice);
            break;
        case aCEL_MSG_MAKE_CHANGE:
            if (choice == 0) {
                aCEL_make_new(cel);
            } else if (choice == 1) {
                aCEL_say(cel, aCEL_MSG_WHICH_CHANGE);
            } else {
                aCEL_say(cel, aCEL_MSG_CHANGED_MIND);
            }
            break;
        case aCEL_MSG_RULES:
            aCEL_set_flag(aCEL_FLAG_RULES);
            aCEL_close_then(cel, aCEL_OPEN_MAKE);
            break;
        case aCEL_MSG_WHICH_CHANGE:
            cel->purpose = aCEL_PICK_CHANGE;
            aCEL_close_then(cel, aCEL_OPEN_PICK);
            break;
        case aCEL_MSG_WHICH_ERASE:
            cel->purpose = aCEL_PICK_ERASE;
            aCEL_close_then(cel, aCEL_OPEN_PICK);
            break;
        case aCEL_MSG_WHICH_TIMES:
            cel->purpose = aCEL_PICK_TIMES;
            aCEL_close_then(cel, aCEL_OPEN_PICK);
            break;
        case aCEL_MSG_LOOK_TIMES:
            if (choice == 0) {
                aCEL_say(cel, aCEL_MSG_ENJOY);
            } else if (choice == 1) {
                aCEL_say(cel, aCEL_MSG_WHICH_TIMES);
            } else {
                aCEL_say(cel, aCEL_MSG_SHAME);
            }
            break;
        case aCEL_MSG_ENJOY:
            aCEL_close_then(cel, aCEL_OPEN_VIEW);
            break;
        case aCEL_MSG_RENAME:
            if (choice == 0) {
                aCEL_say(cel, aCEL_MSG_NAME_IT);
            } else {
                aCEL_set_strings(&cel->work);
                aCEL_say(cel, aCEL_MSG_LOG);
            }
            break;
        case aCEL_MSG_NAME_IT:
        case aCEL_MSG_SAME_NAME:
            memcpy(cel->name, cel->work.name, sizeof(cel->name));
            aCEL_close_then(cel, aCEL_OPEN_NAME);
            break;
        case aCEL_MSG_REGISTER:
            aCEL_set_strings(&cel->work);
            aCEL_say(cel, aCEL_MSG_LOG);
            break;
        case aCEL_MSG_LOG:
            if (choice == 0) {
                int slot = cel->slot >= 0 ? cel->slot : aCEL_free_slot();

                if (slot >= 0) {
                    cel->work.used = TRUE;
                    cel->work.owner = (u8)aCEL_player();
                    *pc_const_get(slot) = cel->work;
                }
                aCEL_say(cel, aCEL_MSG_LOGGED);
            } else {
                aCEL_say(cel, aCEL_MSG_CHANGED_MIND);
            }
            break;
        case aCEL_MSG_ERASE:
            if (choice == 0 && cel->slot >= 0) {
                memset(pc_const_get(cel->slot), 0, sizeof(pc_constellation_c));
                aCEL_say(cel, aCEL_MSG_ERASED);
            } else {
                aCEL_say(cel, aCEL_MSG_KEEP);
            }
            break;
        case aCEL_MSG_FULL:
        case aCEL_MSG_CHANGED_MIND:
        case aCEL_MSG_LOGGED:
        case aCEL_MSG_ALL_DONE:
        case aCEL_MSG_SHAME:
        case aCEL_MSG_KEEP:
        case aCEL_MSG_ERASED:
        case aCEL_MSG_TONIGHT:
        case aCEL_MSG_RIGHT_NOW:
        case aCEL_MSG_BEST_DATE:
            aCEL_again(cel);
            break;
        default:
            aCEL_say_last(cel, aCEL_MSG_BYE);
            break;
    }
}

static void aCEL_talk_wait_line(NPC_CELESTE_ACTOR* cel, GAME_PLAY* play) {
    mMsg_Window_c* msg_p = mMsg_Get_base_window_p();

    if (cel->wait_msg >= 0 && mMsg_Get_msg_num(msg_p) == aCEL_msg(cel->wait_msg, FALSE) &&
        mMsg_Check_MainNormalContinue(msg_p) == TRUE) {
        int choice = mChoice_Get_ChoseNum(mChoice_Get_base_window_p());
        static const char* dev = (const char*)-1;

        /* dev: PC_CELESTE_PICKS=1,0,... answers her menus in turn instead of the player's choice */
        if (dev == (const char*)-1) {
            dev = getenv("PC_CELESTE_PICKS");
        }
        if (dev != NULL && *dev != '\0' && pc_celeste_line_has_choices(cel->wait_msg)) {
            choice = atoi(dev);
            dev = strchr(dev, ',');
            dev = dev != NULL ? dev + 1 : "";
        }
        if (getenv("PC_CELESTE_LOG") != NULL) {
            printf("[Celeste] line %d done, choice %d\n", cel->wait_msg, choice);
            fflush(stdout);
        }
        aCEL_line_done(cel, cel->wait_msg, choice);
    }
}

/* --- the telescope, the list and the name editor --- */

static void aCEL_talk_scope_wait(NPC_CELESTE_ACTOR* cel, GAME_PLAY* play);
static void aCEL_talk_name_wait(NPC_CELESTE_ACTOR* cel, GAME_PLAY* play);

static void aCEL_talk_open_wait(NPC_CELESTE_ACTOR* cel, GAME_PLAY* play) {
    if (mMsg_Check_main_wait(mMsg_Get_base_window_p()) != TRUE) {
        return;
    }
    switch (cel->after_close) {
        case aCEL_OPEN_MAKE:
            pc_const_edit_open(PC_CONST_EDIT_MAKE, cel->slot >= 0 ? &cel->work : NULL);
            break;
        case aCEL_OPEN_VIEW:
            pc_const_edit_open(PC_CONST_EDIT_VIEW, NULL);
            break;
        case aCEL_OPEN_PICK:
            pc_const_edit_pick(cel->purpose == aCEL_PICK_TIMES ? -1 : aCEL_player());
            break;
        case aCEL_OPEN_NAME:
            if (getenv("PC_CELESTE_NAME") != NULL) {
                /* dev: name it without the editor */
                const char* n = getenv("PC_CELESTE_NAME");
                int i;

                for (i = 0; i < 16; i++) {
                    cel->name[i] = (u8)(i < (int)strlen(n) ? n[i] : ' ');
                }
            } else {
                mSM_open_submenu_new(&play->submenu, mSM_OVL_LEDIT, mLE_TYPE_MYORIGINAL_NAME, 0, cel->name);
            }
            cel->talk_proc = &aCEL_talk_name_wait;
            return;
    }
    cel->talk_proc = &aCEL_talk_scope_wait;
}

/* After the telescope or the list */
static void aCEL_talk_scope_wait(NPC_CELESTE_ACTOR* cel, GAME_PLAY* play) {
    if (pc_const_edit_active() || g_pc_paused) {
        return;
    }
    switch (cel->after_close) {
        case aCEL_OPEN_MAKE: {
            pc_constellation_c made;

            if (!pc_const_edit_result(&made)) {
                aCEL_reopen(cel, aCEL_MSG_CHANGED_MIND);
            } else {
                memcpy(made.name, cel->work.name, sizeof(made.name));
                cel->work = made;
                aCEL_set_strings(&cel->work);
                aCEL_reopen(cel, cel->slot >= 0 ? aCEL_MSG_RENAME : aCEL_MSG_NAME_IT);
            }
        } break;
        case aCEL_OPEN_VIEW:
            aCEL_reopen(cel, aCEL_MSG_ALL_DONE);
            break;
        case aCEL_OPEN_PICK: {
            int slot = pc_const_edit_picked();

            if (slot < 0) {
                aCEL_reopen(cel, cel->purpose == aCEL_PICK_TIMES ? aCEL_MSG_SHAME : aCEL_MSG_CHANGED_MIND);
                break;
            }
            cel->slot = slot;
            cel->work = *pc_const_get(slot);
            aCEL_set_strings(&cel->work);
            if (cel->purpose == aCEL_PICK_CHANGE) {
                /* straight into the telescope with it */
                cel->after_close = aCEL_OPEN_MAKE;
                pc_const_edit_open(PC_CONST_EDIT_MAKE, &cel->work);
            } else if (cel->purpose == aCEL_PICK_ERASE) {
                aCEL_reopen(cel, aCEL_MSG_ERASE);
            } else {
                int hour, minute, now = Common_Get(time.now_sec) / 60, peak;

                pc_celeste_peak_tonight(&cel->work, &hour, &minute);
                peak = hour * 60 + minute;
                if (hour >= 6 && hour < 18) {
                    aCEL_reopen(cel, aCEL_MSG_BEST_DATE); /* daytime now of year: when it's best in the evening */
                } else if (now - peak < 20 && peak - now < 20) {
                    aCEL_reopen(cel, aCEL_MSG_RIGHT_NOW);
                } else {
                    aCEL_reopen(cel, aCEL_MSG_TONIGHT);
                }
            }
        } break;
    }
}

static int aCEL_name_taken(NPC_CELESTE_ACTOR* cel) {
    int i;

    for (i = 0; i < PC_CONST_MAX; i++) {
        const pc_constellation_c* c = pc_const_get(i);

        if (c->used && i != cel->slot && memcmp(c->name, cel->name, sizeof(cel->name)) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static void aCEL_talk_name_wait(NPC_CELESTE_ACTOR* cel, GAME_PLAY* play) {
    int i, blank = TRUE;

    if (play->submenu.open_flag) {
        return;
    }
    for (i = 0; i < 16; i++) {
        blank &= cel->name[i] == ' ';
    }
    if (blank) {
        aCEL_reopen(cel, aCEL_MSG_CHANGED_MIND);
    } else if (aCEL_name_taken(cel)) {
        aCEL_reopen(cel, aCEL_MSG_SAME_NAME);
    } else {
        memcpy(cel->work.name, cel->name, sizeof(cel->name));
        aCEL_set_strings(&cel->work);
        aCEL_reopen(cel, aCEL_MSG_REGISTER);
    }
}

/* --- talking --- */

static void aCEL_set_talk_info(ACTOR* actorx) {
    NPC_CELESTE_ACTOR* cel = (NPC_CELESTE_ACTOR*)actorx;
    int hour = Common_Get(time.now_sec) / 3600;
    int msg;

    pc_celeste_set_talking(TRUE);
    if (!aCEL_flag(aCEL_FLAG_MET)) {
        msg = aCEL_MSG_FIRST;
    } else if (hour >= 1 && hour < 5 && RANDOM(3) == 0) {
        msg = aCEL_MSG_GREET_SLEEPY;
    } else {
        msg = RANDOM(3) ? aCEL_MSG_GREET_A : aCEL_MSG_GREET_B;
    }
    mDemo_Set_msg_num(aCEL_msg(msg, FALSE));
    cel->wait_msg = msg;
    cel->slot = -1;
}

static void aCEL_talk_request(ACTOR* actorx, GAME* game) {
    mDemo_Request(mDemo_TYPE_TALK, actorx, &aCEL_set_talk_info);
}

static int aCEL_talk_init(ACTOR* actorx, GAME* game) {
    NPC_CELESTE_ACTOR* cel = (NPC_CELESTE_ACTOR*)actorx;

    cel->talk_proc = &aCEL_talk_wait_line;
    aCEL_set_flag(aCEL_FLAG_MET);
    mDemo_Set_ListenAble();
    cel->npc_class.head.angle_add_y = DEG2SHORT_ANGLE(5.625f);
    return TRUE;
}

static int aCEL_talk_end_chk(ACTOR* actorx, GAME* game) {
    NPC_CELESTE_ACTOR* cel = (NPC_CELESTE_ACTOR*)actorx;

    (*cel->talk_proc)(cel, (GAME_PLAY*)game);
    if (mDemo_Check(mDemo_TYPE_TALK, actorx) == FALSE) {
        pc_celeste_set_talking(FALSE);
        return TRUE;
    }
    return FALSE;
}

/* --- standing about (Blathers' awake schedule, without the sleeping) --- */

static void aCEL_set_request_act(NPC_CELESTE_ACTOR* cel) {
    cel->npc_class.request.act_priority = 4;
    cel->npc_class.request.act_idx = 16;
    cel->npc_class.request.act_type = aNPC_ACT_TYPE_SEARCH;
}

static void aCEL_act_proc(NPC_ACTOR* actorx, GAME_PLAY* play, int action) {
    switch (action) {
        case 0: /* init */
            (*Common_Get(clip).npc_clip->animation_init_proc)(actorx, aNPC_ANIM_WAIT1, FALSE);
            actorx->condition_info.demo_flg = 0;
            actorx->head.angle_add_y = DEG2SHORT_ANGLE(5.625f);
            actorx->movement.mv_angl = DEG2SHORT_ANGLE(0.0f);
            actorx->movement.mv_add_angl = DEG2SHORT_ANGLE(5.625f);
            actorx->action.step = 0;
            break;
        case 1: /* change data */
            actorx->action.act_obj = aNPC_ACT_OBJ_PLAYER;
            break;
    }
}

static void aCEL_think_proc(NPC_ACTOR* actorx, GAME_PLAY* play, int think_idx) {
    if (think_idx == 0) {
        actorx->action.act_proc = &aCEL_act_proc;
    }
    if (think_idx == 0 || actorx->action.step == aNPC_ACTION_END_STEP) {
        aCEL_set_request_act((NPC_CELESTE_ACTOR*)actorx);
    }
}

static void aCEL_schedule_proc(NPC_ACTOR* actorx, GAME_PLAY* play, int proc_idx) {
    if (proc_idx == 0) {
        actorx->think.think_proc = &aCEL_think_proc;
        actorx->condition_info.hide_request = FALSE;
        (*Common_Get(clip).npc_clip->think_proc)(actorx, play, aNPC_THINK_IN_BLOCK, aNPC_THINK_TYPE_INIT);
    } else if ((*Common_Get(clip).npc_clip->think_proc)(actorx, play, -1, aNPC_THINK_TYPE_CHK_INTERRUPT) == FALSE) {
        (*Common_Get(clip).npc_clip->think_proc)(actorx, play, -1, aNPC_THINK_TYPE_MAIN);
    }
}

static void aCEL_actor_ct(ACTOR* actorx, GAME* game) {
    static aNPC_ct_data_c ct_data = {
        &aCEL_actor_move, &aCEL_actor_draw, 5, &aCEL_talk_request, &aCEL_talk_init, &aCEL_talk_end_chk, 0,
    };
    NPC_CELESTE_ACTOR* cel = (NPC_CELESTE_ACTOR*)actorx;

    if ((*Common_Get(clip).npc_clip->birth_check_proc)(actorx, game) == TRUE) {
        pc_celeste_make_palette();
        cel->npc_class.schedule.schedule_proc = &aCEL_schedule_proc;
        (*Common_Get(clip).npc_clip->ct_proc)(actorx, game, &ct_data);
        actorx->status_data.weight = 255;
        cel->talk_proc = &aCEL_talk_wait_line;
        cel->wait_msg = -1;
        cel->slot = -1;
    }
}

static void aCEL_actor_save(ACTOR* actorx, GAME* game) {
    (*Common_Get(clip).npc_clip->save_proc)(actorx, game);
}

static void aCEL_actor_dt(ACTOR* actorx, GAME* game) {
    pc_celeste_set_talking(FALSE);
    (*Common_Get(clip).npc_clip->dt_proc)(actorx, game);
}

static void aCEL_actor_init(ACTOR* actorx, GAME* game) {
    (*Common_Get(clip).npc_clip->init_proc)(actorx, game);
}

static void aCEL_actor_draw(ACTOR* actorx, GAME* game) {
    (*Common_Get(clip).npc_clip->draw_proc)(actorx, game);
}

static void aCEL_actor_move(ACTOR* actorx, GAME* game) {
    (*Common_Get(clip).npc_clip->move_proc)(actorx, game);
}
