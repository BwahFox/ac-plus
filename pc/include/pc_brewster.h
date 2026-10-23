#ifndef PC_BREWSTER_H
#define PC_BREWSTER_H

/* Brewster, who runs the Roost in the museum's basement (pc_brewster.c); his actor is
 * src/actor/npc/ac_npc_brewster.c.
 *
 * He is City Folk's Brewster through and through: his lines (cf_sky/brewster.msg, speaker
 * PC_CF_BREWSTER in pc_cf_lines.c) and his model (cf_sky/chars/brewster.cfchar, pc_cf_char.c),
 * posed by Blathers' GameCube skeleton and animations. He only comes when both were extracted. */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct game_play_s;
struct actor_s;
struct skeleton_s;

#define PC_BREWSTER_PRICE 200

int pc_brewster_available(void);

/* Blathers' skeleton with no models: his animations, nothing of him drawn */
struct skeleton_s* pc_brewster_skeleton(void);
/* Around the NPC draw: catch the joints, then draw City Folk's Brewster with them */
void pc_brewster_draw_begin(void);
void pc_brewster_draw_end(struct game_play_s* play, struct actor_s* actorx, int eye);

/* Days a player has had a cup before he knows them, and before they're a regular */
#define PC_BREWSTER_KNOWN_CUPS 3
#define PC_BREWSTER_REGULAR_CUPS 10

/* Today's cup (the day starts at 6 AM) and the days they've had one, per player (saved as ROST in the
 * ac-plus save) */
int pc_brewster_had_coffee_today(int player);
void pc_brewster_set_coffee_today(int player);
/* 0: a stranger, 1: he knows them, 2: a regular (PC_BREWSTER_*_CUPS) */
int pc_brewster_familiarity(int player);
void pc_brewster_reset(void);
int pc_brewster_read_chunk(const char tag[4], void* f, unsigned int size);
int pc_brewster_write_chunk(void* f);

#ifdef __cplusplus
}
#endif

#endif /* PC_BREWSTER_H */
