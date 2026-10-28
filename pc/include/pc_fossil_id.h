#ifndef PC_FOSSIL_ID_H
#define PC_FOSSIL_ID_H

/* Blathers identifies fossils himself, as Animal Crossing Deluxe has him do (pc_fossil_id.c, ac_npc_curator) */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PC_FOSSIL_ID_DONATED_NEEDED 15 /* fossils in the museum before he's certified */

/* His lines, in message numbers the GameCube leaves empty */
#define PC_FOSSIL_ID_MSG_MORE 0x2F9B  /* any more fossils? */
#define PC_FOSSIL_ID_MSG_TOLD 0x2F9C  /* he's certified now (once per player) */
#define PC_FOSSIL_ID_MSG_DONE 0x2F9D  /* identified: one (item string 0) or several */
#define PC_FOSSIL_ID_MSG_WHICH 0x2F9E /* which fossils? (then the pockets) */
#define PC_FOSSIL_ID_CHOICE 0x7E2     /* "Identify fossils" (after the post office's 0x7E0, 0x7E1) */

int pc_fossil_id_msg(int index, u8* buf, int buf_size, int* len);
int pc_fossil_id_choice(int str_no, u8* str, int len);
int pc_fossil_id_menu(u8* buf, int size, int buf_size);
void pc_fossil_id_set_menu(int on);
void pc_fossil_id_set_count(int count);

int pc_fossil_id_told(void);
void pc_fossil_id_set_told(void);
void pc_fossil_id_forget_player(int player);

void pc_fossil_id_reset(void);
int pc_fossil_id_read_chunk(const char tag[4], void* fp, unsigned int size);
int pc_fossil_id_write_chunk(void* fp);

#ifdef __cplusplus
}
#endif

#endif /* PC_FOSSIL_ID_H */
