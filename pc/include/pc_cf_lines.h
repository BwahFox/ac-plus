#ifndef PC_CF_LINES_H
#define PC_CF_LINES_H

/* City Folk characters' lines in the GameCube game (pc_cf_lines.c).
 *
 * pc/tools/extract_cf_sky.py converts a character's City Folk messages from the player's own
 * disc into cf_sky/<name>.msg. While one of them talks (pc_cf_lines_set_talking), message
 * numbers from PC_CF_MSG_BASE show their line n (and PC_CF_MSG_BASE + PC_CF_MSG_LAST + n the
 * same line closing the window), and choice strings from PC_CF_CHOICE_BASE their menu options.
 * Only one talks at a time, so they share the numbers. */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PC_CF_CELESTE,  /* cf_sky/celeste.msg */
    PC_CF_BREWSTER, /* cf_sky/brewster.msg */
    PC_CF_SPEAKER_NUM
};

#define PC_CF_MSG_BASE 0x3E00
#define PC_CF_MSG_LAST 0x100
#define PC_CF_CHOICE_BASE 607 /* the game's own choice strings end at 606 */

int pc_cf_lines_available(int who);
int pc_cf_lines_count(int who);
/* who talks now, or -1 */
void pc_cf_lines_set_talking(int who);
int pc_cf_lines_has_choices(int who, int n);

/* The game's message and choice loaders (m_msg_main.c_inc, m_choice_main.c_inc) */
int pc_cf_lines_msg(int index, u8* buf, int buf_size, int* len);
int pc_cf_lines_choice(int str_no, u8* str, int len);

#ifdef __cplusplus
}
#endif

#endif /* PC_CF_LINES_H */
