/* pc_brewster.c - Brewster's model, lines and cups. See pc_brewster.h. */
#include "pc_brewster.h"
#include "pc_cf_char.h"
#include "pc_cf_lines.h"

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
                         (const pc_cf_mtx34*)joints, eye, 1.0f);
}

/* --- today's cup --- */

typedef struct {
    u16 year;
    u8 month, day;
} pc_brewster_date_c;

static pc_brewster_date_c l_cup[4]; /* per player; saved as ROST */

int pc_brewster_had_coffee_today(int player) {
    const lbRTC_time_c* t = Common_GetPointer(time.rtc_time);

    return player >= 0 && player < 4 && l_cup[player].year == t->year && l_cup[player].month == t->month &&
           l_cup[player].day == t->day;
}

void pc_brewster_set_coffee_today(int player) {
    const lbRTC_time_c* t = Common_GetPointer(time.rtc_time);

    if (player >= 0 && player < 4) {
        l_cup[player].year = t->year;
        l_cup[player].month = t->month;
        l_cup[player].day = t->day;
    }
}

void pc_brewster_reset(void) {
    memset(l_cup, 0, sizeof(l_cup));
}

int pc_brewster_read_chunk(const char tag[4], void* fp, unsigned int size) {
    FILE* f = (FILE*)fp;

    if (memcmp(tag, "ROST", 4) != 0) {
        return FALSE;
    }
    if (size != sizeof(l_cup) || fread(l_cup, 1, size, f) != size) {
        pc_brewster_reset();
        if (size != sizeof(l_cup)) {
            fseek(f, (long)size, SEEK_CUR);
        }
    }
    return TRUE;
}

int pc_brewster_write_chunk(void* fp) {
    FILE* f = (FILE*)fp;
    u32 size = sizeof(l_cup);

    return fwrite("ROST", 1, 4, f) == 4 && fwrite(&size, 4, 1, f) == 1 && fwrite(l_cup, 1, size, f) == size;
}
