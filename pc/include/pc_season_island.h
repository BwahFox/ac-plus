#ifndef PC_SEASON_ISLAND_H
#define PC_SEASON_ISLAND_H

/* Season islands, sold by Tom Nook (pc_season_island.c) */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { PC_ISL_SUMMER, PC_ISL_SPRING, PC_ISL_FALL, PC_ISL_WINTER, PC_ISL_SEASONS };

#define PC_ISL_MSG_WHICH 0x3E17  /* "which island?" (messages the GameCube leaves empty) */
#define PC_ISL_MSG_SEASON 0x3E18 /* + season: what happens for the one picked */
#define PC_ISL_MSG_BUY 0x3E35
#define PC_ISL_MSG_NO 0x3E36
#define PC_ISL_MSG_KAPPN 0x3E1C /* Kapp'n: "which island?" */
#define PC_ISL_MSG_KAPPN_NO 0x3E1D /* Kapp'n, when the player picks none */
#define PC_ISL_CHOICE_FIRST 0x7E4 /* + season: "Summer island"... */
#define PC_ISL_CHOICE_MENU 0x7E8  /* "Islands", in Nook's "other things" */
#define PC_ISL_CHOICE_NONE 0x7E9  /* "Never mind", Kapp'n's last choice */

int pc_season_island_term(void);   /* the season's term on the island (m_time.c) */
int pc_season_island_winter(void); /* rain falls as snow there */

int pc_season_island_msg(int index, u8* buf, int buf_size, int* len);
int pc_season_island_choice(int str_no, u8* str, int len);
void pc_season_island_set_menu(int on);
int pc_season_island_menu_on(void);

/* Kapp'n at the dock, once the player says they'll ride: TRUE when he should ask which island (PC_ISL_MSG_KAPPN;
 * next_msg: any non-zero). pc_season_island_kappn runs his talk while that's asked: 0 not asking, 1 still asking
 * (the GameCube's code waits), 2 an island picked (go on as the GameCube's "yes" does), 3 none: he says goodbye */
int pc_season_island_kappn_start(int next_msg);
int pc_season_island_kappn(void);

void pc_season_island_reset(void);
int pc_season_island_read_chunk(const char tag[4], void* fp, unsigned int size);
int pc_season_island_write_chunk(void* fp);

#ifdef __cplusplus
}
#endif

#endif /* PC_SEASON_ISLAND_H */
