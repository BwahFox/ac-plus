/* pc_town_map.c - pick your town's map on the train, as Animal Crossing Deluxe lets you: once the town is named,
 * Rover says he has its map and shows it. "Is this the map of <town>?" Yes, and the talk goes on as before; no, and
 * he digs out another (the whole new town made again, pc_sdi_reroll_town), as many times as it takes.
 *
 * Only for a brand-new town (pc_town_map_new_town, from the new-town setup). His lines are our own; while this runs
 * the guide waits (pc_town_map_busy, ac_npc_guide*.c), as for the skin tone question (pc_skin.c). */

#include "pc_town_map.h"
#include "pc_msg_text.h"
#include "m_common_data.h"
#include "m_controller.h"
#include "m_msg.h"
#include "m_choice.h"
#include "m_play.h"
#include "m_submenu.h"
#include "m_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PC_TOWN_MAP_RIDE 0x2ACB  /* "Hey! I know that place!... So, what are you going to <town> for?" */
#define PC_TOWN_MAP_ASK 0x3DC5   /* "Is this the map of <town>?" (empty in the game; only ours while asking) */
#define PC_TOWN_MAP_AGAIN 0x3DC6 /* "...How about this one?" */

enum { MAP_NONE, MAP_LINE, MAP_CLOSING, MAP_SHOWN, MAP_ASK, MAP_FINAL, MAP_DONE };

static int l_new_town; /* a new town is being started: ask once the town is named */
static int l_state = MAP_NONE;
static int l_line_msg; /* the line that leads to the map: PC_TOWN_MAP_RIDE or PC_TOWN_MAP_AGAIN */

extern int g_pc_paused;
extern void pc_sdi_reroll_town(GAME* game);

void pc_town_map_new_town(int on) {
    l_new_town = on;
    l_state = MAP_NONE;
}

static const char* const l_ask =
    "Is this the map of|{7f}/?{7f}{16}{00}{dc}{00}Q{7f}^{7f}{04}{7f}{0d}{7f}{19}|{7f}{01}";
static const char* const l_again =
    "Huh?{7f}{03}{0a} Then I must've grabbed|the wrong one...{7f}{03}{0c} Lemme see...{7f}{04}|{7f}{02}"
    "How about this one?|{7f}{01}";
static const char* const l_lead = "I've got a map of it around|here somewhere...{7f}{03}{0c} Here!|{7f}{01}";
static const char* const l_yes = "That's the one?{7f}{03}{08} Great!{7f}{03}{08}|I've got a good eye for maps.{7f}{04}|{7f}{02}";

/* The ride line (0x2ACB), as loaded: its first page, then the map (MAP_LINE); or, once a map is picked, the rest of
 * it, his question and its choices (MAP_FINAL). Called on every message the game loads; returns the new size */
int pc_town_map_ride(int index, u8* buf, int size, int buf_size) {
    int page = -1;
    int i;

    if (index != PC_TOWN_MAP_RIDE || !l_new_town || (l_state != MAP_NONE && l_state != MAP_FINAL)) {
        return size;
    }
    for (i = 0; i + 4 < size; i++) {
        if (buf[i] == 0x7F && buf[i + 1] == 0x04 && buf[i + 2] == CHAR_NEW_LINE && buf[i + 3] == 0x7F &&
            buf[i + 4] == 0x02) {
            page = i;
            break;
        }
    }
    if (page < 0) {
        return size; /* not the line seen: as on the GameCube */
    }
    if (l_state == MAP_NONE) {
        /* "Hey! I know that place! <town> is one of my favorite vacation spots!" then his map */
        int n = page;

        buf[n++] = 0x7F, buf[n++] = 0x04, buf[n++] = CHAR_NEW_LINE, buf[n++] = 0x7F, buf[n++] = 0x02;
        n += pc_msg_text(l_lead, buf + n, buf_size - n);
        l_state = MAP_LINE;
        l_line_msg = PC_TOWN_MAP_RIDE;
        return n;
    } else {
        /* "That's the one? Great!" and the rest: "So, what are you going to <town> for?" */
        u8 head[128];
        int hn = pc_msg_text(l_yes, head, sizeof(head));
        int rest = size - (page + 5);

        if (hn + rest > buf_size) {
            return size;
        }
        memmove(buf + hn, buf + page + 5, rest);
        memcpy(buf, head, hn);
        l_state = MAP_DONE;
        l_new_town = FALSE;
        return hn + rest;
    }
}

int pc_town_map_msg(int index, u8* buf, int buf_size, int* len) {
    if (index == PC_TOWN_MAP_ASK && l_state == MAP_ASK) {
        *len = pc_msg_text(l_ask, buf, buf_size);
        return TRUE;
    }
    if (index == PC_TOWN_MAP_AGAIN && l_state == MAP_LINE && l_line_msg == PC_TOWN_MAP_AGAIN) {
        *len = pc_msg_text(l_again, buf, buf_size);
        return TRUE;
    }
    return FALSE;
}

int pc_town_map_busy(void) {
    mMsg_Window_c* msg_p = mMsg_Get_base_window_p();
    GAME_PLAY* play = (GAME_PLAY*)gamePT;
    static int last = -1;

    if (l_state != last && getenv("PC_TOWN_MAP_LOG") != NULL) {
        printf("[townmap] state %d msg %04X submenu type %d open %d status %d\n", l_state, mMsg_Get_msg_num(msg_p),
               play != NULL ? play->submenu.menu_type : -1, play != NULL ? play->submenu.open_flag : -1,
               play != NULL ? play->submenu.process_status : -1);
        last = l_state;
    }

    switch (l_state) {
        case MAP_LINE:
            /* his line shown in full, and A pressed to go on (the window stops waiting at its end) */
            if (mMsg_Get_msg_num(msg_p) == l_line_msg && mMsg_Check_MainNormalContinue(msg_p) == TRUE &&
                chkTrigger(BUTTON_A)) {
                mMsg_request_main_disappear_wait_type1(msg_p);
                l_state = MAP_CLOSING;
            }
            break;
        case MAP_CLOSING:
            if (mMsg_Check_main_wait(msg_p) == TRUE && play != NULL) {
                mSM_open_submenu(&play->submenu, mSM_OVL_MAP, 0, 0);
                l_state = MAP_SHOWN;
            }
            break;
        case MAP_SHOWN:
            /* while the map is up the actors (Rover too) stand still, so this sees it asked for, then closed */
            if (play != NULL && play->submenu.menu_type == mSM_OVL_NONE &&
                play->submenu.process_status == mSM_PROCESS_WAIT && !g_pc_paused) {
                l_state = MAP_ASK;
                mMsg_ChangeMsgData(msg_p, PC_TOWN_MAP_ASK);
                mMsg_Set_ForceNext(msg_p);
                mMsg_request_main_appear_wait_type1(msg_p);
            }
            break;
        case MAP_ASK:
            if (mMsg_Get_msg_num(msg_p) == PC_TOWN_MAP_ASK && mMsg_Check_MainNormalContinue(msg_p) == TRUE) {
                if (mChoice_Get_ChoseNum(mChoice_Get_base_window_p()) == mChoice_CHOICE0) {
                    l_state = MAP_FINAL; /* the ride line again, its question this time (pc_town_map_ride) */
                    mMsg_Set_continue_msg_num(msg_p, PC_TOWN_MAP_RIDE);
                } else {
                    pc_sdi_reroll_town(gamePT);
                    l_state = MAP_LINE;
                    l_line_msg = PC_TOWN_MAP_AGAIN;
                    mMsg_Set_continue_msg_num(msg_p, PC_TOWN_MAP_AGAIN);
                }
                mMsg_Set_ForceNext(msg_p);
            }
            break;
        case MAP_FINAL:
            /* the guide takes over once the ride line is on (pc_town_map_ride makes it MAP_DONE) */
            break;
    }
    return l_state == MAP_LINE || l_state == MAP_CLOSING || l_state == MAP_SHOWN || l_state == MAP_ASK ||
           l_state == MAP_FINAL;
}
