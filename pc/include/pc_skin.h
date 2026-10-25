#ifndef PC_SKIN_H
#define PC_SKIN_H

/* Skin tones, as Animal Crossing Deluxe has them (pc_skin.c) */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PC_SKIN_TONE_NUM 8
#define PC_SKIN_TONE_DEFAULT 1 /* the GameCube's own skin */

/* The tone of the player now playing (a visitor from another town has the default) */
int pc_skin_tone(void);
/* ...and of the town's player `player` (0-3) */
int pc_skin_tone_of(int player);
void pc_skin_set_tone(int player, int tone);
/* The town's player number of the player now playing, -1 for a visitor */
int pc_skin_now_player(void);

/* The sunburn rank to show: none while the tanning setting is off */
int pc_skin_sunburn_rank(void);
/* Recolour the skin entries (1-5) of a face palette (16 RGB5A3, host order) for `tone` tanned to `rank` */
void pc_skin_apply(u16* pal, int tone, int rank);
/* The face palette as loaded, before pc_skin_apply recoloured it (m_player_lib.c keeps it) */
void pc_skin_keep_vanilla(const u16* pal);
/* For the HD texture pack (pc_texture_pack.c): if `tlut` is the recoloured face palette, the palette it was before,
 * else NULL. The pack finds the HD face by that, then recolours it (pc_skin_recolor_rgba). */
const u16* pc_skin_vanilla_tlut(const void* tlut);
/* Shift the skin of an RGBA8 image from palette `from`'s skin entries to `to`'s (both 16 RGB5A3, host order):
 * each pixel nearest one of entries 1-5 (of all 16) moves by that entry's change */
void pc_skin_recolor_rgba(u8* rgba, int n_pixels, const u16* from, const u16* to);
/* The swatch colour shown for a tone in the picker */
void pc_skin_swatch(int tone, u8* r, u8* g, u8* b);

/* The tone picker: eight swatches, light to dark, two across (a pause menu page, pc_pause_menu.c).
 * Picking sets `player`'s tone and recolours them; B closes it unchanged when `can_cancel`. */
struct game_s;
void pc_skin_pick_open(int player, int can_cancel);
int pc_skin_pick_active(void);
/* After it closes: TRUE if a tone was picked */
int pc_skin_pick_done(void);
void pc_skin_pick_action(int act);
void pc_skin_pick_draw(struct game_s* game);

/* Rover's question on the train, between "boy or girl" and the town (ac_npc_guide*.c): he asks, the picker
 * opens, then the talk goes on with message `next`. While pc_skin_ask_busy, the guide waits. */
void pc_skin_ask_start(int next);
int pc_skin_ask_busy(void);
/* The message loader (m_msg_main.c_inc): his line */
int pc_skin_msg(int index, u8* buf, int buf_size, int* len);

/* SKIN in the ac-plus save (pc_ext_save.c) */
void pc_skin_reset(void);
int pc_skin_read_chunk(const char tag[4], void* f, unsigned int size);
int pc_skin_write_chunk(void* f);

#ifdef __cplusplus
}
#endif

#endif /* PC_SKIN_H */
