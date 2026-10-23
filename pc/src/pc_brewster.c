/* pc_brewster.c - Brewster's model, lines and cups. See pc_brewster.h. */
#include "pc_brewster.h"
#include "pc_cf_char.h"
#include "pc_cf_lines.h"
#include "ac_npc_kk_roost.h"

#include "m_play.h"
#include "m_actor.h"
#include "m_common_data.h"
#include "c_keyframe.h"
#include "sys_matrix.h"

#include <stdio.h>
#include <string.h>

#define PC_BREWSTER_CHAR "brewster"
#define PC_BREWSTER_JOINTS 26 /* Blathers' skeleton (owl_1.c) */

extern cKF_Skeleton_R_c cKF_bs_r_owl_1;
extern MtxF* pc_cKF_capture;
extern int pc_cKF_capture_num;

/* Blathers' joints by City Folk bone name (owl_1.c's joint table; NULL: no bone of that name) */
static const char* const l_joint_names[PC_BREWSTER_JOINTS] = {
    NULL,     "base",  NULL,    "Lfoot1", "Lfoot2", "Lfoot3", NULL,    "Rfoot1", "Rfoot2",
    "Rfoot3", NULL,    "tail1", "tail2",  "chest",  NULL,     "Larm1", "Larm2",  NULL,
    "Rarm1",  "Rarm2", NULL,    NULL,     NULL,     "mouth",  "head",  NULL,
};

static cKF_Joint_R_c l_joints[PC_BREWSTER_JOINTS];
cKF_Skeleton_R_c pc_brewster_skel; /* npc_draw_data.c's Brewster entry; filled by pc_brewster_skeleton() */
static MtxF l_capture[1 + PC_BREWSTER_JOINTS];

int pc_brewster_available(void) {
    return pc_cf_lines_available(PC_CF_BREWSTER) && pc_cf_char_exists(PC_BREWSTER_CHAR);
}

cKF_Skeleton_R_c* pc_brewster_skeleton(void) {
    if (pc_brewster_skel.joint_table == NULL) {
        int i;

        for (i = 0; i < PC_BREWSTER_JOINTS && i < cKF_bs_r_owl_1.num_joints; i++) {
            l_joints[i] = cKF_bs_r_owl_1.joint_table[i];
            l_joints[i].model = NULL;
        }
        pc_brewster_skel = cKF_bs_r_owl_1;
        pc_brewster_skel.joint_table = l_joints;
    }
    return &pc_brewster_skel;
}

void pc_brewster_draw_begin(void) {
    memset(l_capture, 0, sizeof(l_capture));
    pc_cKF_capture = l_capture;
    pc_cKF_capture_num = 1 + PC_BREWSTER_JOINTS;
}

/* MtxF (row vectors, translation in the last row) -> column-vector 3x4 */
static void pc_brewster_mtx34(pc_cf_mtx34 out, const MtxF* m) {
    int r, c;

    for (r = 0; r < 3; r++) {
        for (c = 0; c < 4; c++) {
            out[r][c] = m->mf[c][r];
        }
    }
}

void pc_brewster_draw_end(GAME_PLAY* play, ACTOR* actorx, int eye) {
    pc_cf_mtx34 actor;
    pc_cf_mtx34 joints[PC_BREWSTER_JOINTS];
    int i;

    (void)actorx;
    pc_cKF_capture = NULL;
    pc_cKF_capture_num = 0;
    if (l_capture[0].mf[3][3] == 0.0f) {
        return; /* the skeleton wasn't drawn this frame */
    }
    pc_brewster_mtx34(actor, &l_capture[0]);
    for (i = 0; i < PC_BREWSTER_JOINTS; i++) {
        pc_brewster_mtx34(joints[i], &l_capture[1 + i]);
    }
    pc_cf_char_play_draw(play, PC_BREWSTER_CHAR, actor, PC_BREWSTER_JOINTS, l_joint_names,
                         (const pc_cf_mtx34*)joints, eye, aKKR_room_light());
}

/* --- today's cup, and how well he knows the player --- */

typedef struct {
    u16 year;
    u8 month, day; /* the day of the last cup */
    u16 cups;      /* days they've had one */
    u16 pad;
} pc_brewster_cup_c;

typedef struct {
    u16 year;
    u8 month, day;
} pc_brewster_cup_v1_c; /* ROST before cups were counted */

static pc_brewster_cup_c l_cup[4]; /* per player; saved as ROST */

/* The Roost's day starts at 6 AM, as City Folk's do: a cup after midnight still counts for the night before */
static void pc_brewster_today(lbRTC_time_c* day) {
    lbRTC_TimeCopy(day, Common_GetPointer(time.rtc_time));
    lbRTC_Sub_hh(day, 6);
}

int pc_brewster_had_coffee_today(int player) {
    lbRTC_time_c t;

    pc_brewster_today(&t);
    return player >= 0 && player < 4 && l_cup[player].year == t.year && l_cup[player].month == t.month &&
           l_cup[player].day == t.day;
}

void pc_brewster_set_coffee_today(int player) {
    lbRTC_time_c t;

    if (player >= 0 && player < 4 && !pc_brewster_had_coffee_today(player)) {
        pc_brewster_today(&t);
        l_cup[player].year = t.year;
        l_cup[player].month = t.month;
        l_cup[player].day = t.day;
        if (l_cup[player].cups < 0xFFFF) {
            l_cup[player].cups++;
        }
    }
}

int pc_brewster_familiarity(int player) {
    int cups = player >= 0 && player < 4 ? l_cup[player].cups : 0;

    return cups >= PC_BREWSTER_REGULAR_CUPS ? 2 : cups >= PC_BREWSTER_KNOWN_CUPS ? 1 : 0;
}

void pc_brewster_reset(void) {
    memset(l_cup, 0, sizeof(l_cup));
}

int pc_brewster_read_chunk(const char tag[4], void* fp, unsigned int size) {
    FILE* f = (FILE*)fp;
    pc_brewster_cup_v1_c v1[4];
    int i;

    if (memcmp(tag, "ROST", 4) != 0) {
        return FALSE;
    }
    if (size == sizeof(l_cup)) {
        if (fread(l_cup, 1, size, f) != size) {
            pc_brewster_reset();
        }
    } else if (size == sizeof(v1)) {
        /* cups weren't counted: a player who's had one has had one */
        pc_brewster_reset();
        if (fread(v1, 1, size, f) == size) {
            for (i = 0; i < 4; i++) {
                l_cup[i].year = v1[i].year;
                l_cup[i].month = v1[i].month;
                l_cup[i].day = v1[i].day;
                l_cup[i].cups = v1[i].year != 0 ? 1 : 0;
            }
        }
    } else {
        pc_brewster_reset();
        fseek(f, (long)size, SEEK_CUR);
    }
    return TRUE;
}

int pc_brewster_write_chunk(void* fp) {
    FILE* f = (FILE*)fp;
    u32 size = sizeof(l_cup);

    return fwrite("ROST", 1, 4, f) == 4 && fwrite(&size, 4, 1, f) == 1 && fwrite(l_cup, 1, size, f) == size;
}
