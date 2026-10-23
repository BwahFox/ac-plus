#ifndef PC_CONSTELLATION_EDIT_H
#define PC_CONSTELLATION_EDIT_H

/* Celeste's telescope (pc_constellation_edit.c): a pause-menu page showing the sky
 * full screen. Stargazing pans around it; making or changing a constellation moves
 * a cursor from star to star and links them, City Folk's rules: up to 16 links,
 * new links grow from the constellation, and a link can't be taken away if that
 * would split it in two. */

#include "pc_constellation.h"

#ifdef __cplusplus
extern "C" {
#endif

struct game_s;

enum {
    PC_CONST_EDIT_VIEW, /* stargazing */
    PC_CONST_EDIT_MAKE, /* a new one, or changing an existing one */
    PC_CONST_EDIT_PICK, /* "which constellation?" */
};

/* Opens the telescope (pauses the game). For MAKE, start from `from` (NULL = empty). */
void pc_const_edit_open(int mode, const pc_constellation_c* from);
int pc_const_edit_active(void);
/* After it closes: TRUE if a MAKE finished with "Done", the result in *out */
int pc_const_edit_result(pc_constellation_c* out);
/* The list of constellations (owner's, or everyone's with -1); after it closes, the slot picked or -1 */
void pc_const_edit_pick(int owner);
int pc_const_edit_picked(void);

/* Pause menu page (pc_pause_menu.c); act as pc_qr_menu_action */
void pc_const_edit_action(int act);
void pc_const_edit_draw(struct game_s* game);

/* Whether a constellation's links are all connected (used when taking a link away) */
int pc_const_connected(const pc_constellation_c* c);

#ifdef __cplusplus
}
#endif

#endif /* PC_CONSTELLATION_EDIT_H */
