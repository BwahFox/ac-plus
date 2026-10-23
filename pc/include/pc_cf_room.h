#ifndef PC_CF_ROOM_H
#define PC_CF_ROOM_H

/* City Folk rooms drawn in the GameCube game (pc_cf_room.c).
 *
 * pc/tools/extract_cf_sky.py converts rooms from the player's City Folk disc into
 * cf_sky/rooms/<name>.cfroom (pc/tools/cf_rooms.py). A scene that uses one calls
 * pc_cf_room_play_draw() each frame while building its display lists; that puts a
 * PC_NOOP_CF_ROOM tag at the top of BG_OPA, and the render side draws the room there
 * with the frame's camera, into the same depth buffer as everything else. */

#ifdef __cplusplus
extern "C" {
#endif

#define PC_NOOP_CF_ROOM 0xAC5714u

struct game_play_s;

/* Game side: draw this room this frame (name as in cf_sky/rooms, without .cfroom).
 * light: 0-1 brightness of the room's light; y_offset raises the room (City Folk floors are at 0).
 * Returns FALSE if the room isn't there. */
int pc_cf_room_play_draw(struct game_play_s* play, const char* name, float light, float y_offset);
/* Game side, before pc_cf_room_play_draw: a pool of light around (x, z) in room units, radius r, at strength
 * 0-1 (0 = none), lifting the room's light there to full and a little warm. For one frame. */
void pc_cf_room_set_spot(float x, float z, float r, float strength);
int pc_cf_room_exists(const char* name);
/* The room's 16x16 floor plan (nonzero = walkable, row-major by z), or NULL */
const unsigned char* pc_cf_room_grid(const char* name);

/* Render side (emu64's NoOp handler) */
void pc_cf_room_noop(unsigned int tag, const float proj[4][4], const float view[3][4]);

#ifdef __cplusplus
}
#endif

#endif /* PC_CF_ROOM_H */
