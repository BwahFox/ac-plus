#ifndef PC_CF_ANIM_H
#define PC_CF_ANIM_H

/* City Folk's animations in the GameCube game (pc_cf_anim.c).
 *
 * pc/tools/extract_cf_sky.py bakes them from the player's City Folk disc (cf_roost_anim.py,
 * cf_player_anim.py):
 *  - cf_sky/anims/roost.cfanim: Brewster's own animations and the props that go with them
 *    (cup, saucer, pot, his polishing cup and cloth), each frame's bones already worked out,
 *    from one of two places: Brewster's (where he stands) or the seat's (where the player sits).
 *  - cf_sky/anims/player.cfpanm: the player's (drinking at the counter), as GameCube keyframe
 *    animations for the player's own skeleton. They play as player animation indices from
 *    PC_CF_PANIM_FIRST (mPlib_Get_Pointer_Animation).
 * City Folk animates at 60 frames a second: frames here are City Folk's. */

#include "pc_cf_char.h"

#ifdef __cplusplus
extern "C" {
#endif

struct game_play_s;
struct animation_s;

/* --- Brewster and the props (roost.cfanim) --- */

/* Frames in the animation (its last frame is frames - 1), or 0 if it isn't there */
int pc_cf_anim_frames(const char* name);

#define PC_CF_ANIM_BODY 1  /* the character's own track */
#define PC_CF_ANIM_PROPS 2 /* what he handles (pot, cloth...) but the cup */
#define PC_CF_ANIM_CUP 4   /* the cup and its saucer */

/* owner: Brewster's place (position and turn, no scale). seat: the player's, or NULL for the seat right
 * across from him. cup: the model for the "cup" track (cup_coffee, cup_milk...). Draws the tracks parts
 * picks at frame (City Folk frames, may be fractional). Returns FALSE if the animation isn't there. */
int pc_cf_anim_draw(struct game_play_s* play, const char* name, float frame, const pc_cf_mtx34 owner,
                    const pc_cf_mtx34 seat, const char* cup, int eye, float light, int parts);

/* The seat right across from Brewster, given his place */
void pc_cf_anim_owner_seat(pc_cf_mtx34 out, const pc_cf_mtx34 owner);

/* --- the player (player.cfpanm) --- */

enum {
    PC_CF_PANIM_CAFE_DRINK1, /* lift the cup, sip, turn to say what they think */
    PC_CF_PANIM_CAFE_DRINK2, /* turn back, set the cup down */
    PC_CF_PANIM_NUM
};
#define PC_CF_PANIM_FIRST 1000 /* player animation index of PC_CF_PANIM_CAFE_DRINK1 */

/* The GameCube animation, or NULL if it wasn't extracted */
struct animation_s* pc_cf_anim_player(int which);
int pc_cf_anim_player_available(int which);

/* Place (position, turn about y) -> owner/seat matrix */
void pc_cf_anim_place(pc_cf_mtx34 out, float x, float y, float z, short angle_y);

#ifdef __cplusplus
}
#endif

#endif /* PC_CF_ANIM_H */
