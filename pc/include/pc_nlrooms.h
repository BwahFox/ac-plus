#ifndef PC_NLROOMS_H
#define PC_NLROOMS_H

/* New Leaf style extra house rooms (pc_nlrooms.c).
 *
 * A fully upgraded house (big main room, upstairs, basement) can grow three more rooms off its main room: one to
 * the left and one to the right, through doors near the front corners, and one at the back, through the middle of
 * the back wall. Each is 6x6 at first and can grow to 8x8, like the main room before it.
 *
 * The GameCube save keeps three floors per house, so these rooms live in the ac-plus companion save (chunk
 * NLRM). An extra room is shown with the main room's own scenes, SCENE_MY_ROOM_M (6x6) or SCENE_MY_ROOM_L (8x8),
 * while mFI_GetPlayerHouseFloorNo() reports its floor, PC_NLR_FLOOR_FIRST + room, and code that reads a floor's
 * data goes through mHm_GetFloorP() (mHm_FLOOR in m_home_h.h).
 *
 * The doors are openings in the walls, cut into the wall models (pc_nlr_wall_model()), that the player walks into:
 * pc_nlr_door_in_front() says there's a door there, and the scene's door table gets entries for them, so nothing is
 * written into the house's own furniture grid. The back room's front door leads back to the main room; the side
 * rooms have none.
 *
 * dev: PC_NLR_GRANT=a,b,c gives every fully upgraded house the left, right and back rooms at sizes a, b, c
 * (0, 6 or 8). */

#include "types.h"
#include "m_home_h.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PC_NLR_ROOMS 3      /* left, right, back: the order Nook offers them */
#define PC_NLR_FLOOR_FIRST 3 /* the GameCube's floors are 0-2 */

enum {
    PC_NLR_LEFT,
    PC_NLR_RIGHT,
    PC_NLR_BACK,
};

struct door_data_s;
struct xyz_s;

/* The floor data for house `house`, floor `floor_no` (the extra rooms are floors 3-5) */
mHm_flr_c* mHm_GetFloorP(int house, int floor_no);

/* The floor an extra room shown in `scene` is, or -1 when the player isn't in one */
int pc_nlr_floor_for_scene(int scene);
/* The extra room the player is in, -1 when none; its size (6 or 8) in *size */
int pc_nlr_current(int* size);

/* The size of a house's extra room (0 when it has none yet), and growing or building one */
int pc_nlr_room_size(int house, int room);
void pc_nlr_set_room_size(int house, int room, int size);

/* Scene changes (m_scene.c, m_play.c) */
struct door_data_s* pc_nlr_door_data(int scene, struct door_data_s* doors, int* num_doors);
void pc_nlr_scene_left(struct door_data_s* door); /* a door was taken: which room it leads to */
void pc_nlr_play_init(void);

/* The player is looking at `item` at `probe` (m_player_other_func.c_inc): what's really there */
int pc_nlr_door_in_front(const struct xyz_s* probe, int item);
/* One of the doors above, which work from flat floor */
int pc_nlr_is_door(int item);

/* Nook (ac_npc_shop_common.c), greeting the owner of house `house`: TRUE when he has news about the rooms,
 * then shown in place of message msg_no. Once the house is paid off (big main room, upstairs, basement) he orders
 * the next room (left, right, back, then each grown to 8x8), and only then the statue; it's built on the first load
 * on a later day
 * (pc_nlr_check_orders, m_start_data_init.c) and billed on the next visit. */
int pc_nlr_nook_greeting(int house, int msg_no);
void pc_nlr_check_orders(void);
/* Nook's lines (m_msg_main.c_inc) */
int pc_nlr_msg(int index, unsigned char* buf, int buf_size, int* len);

/* The room's wall model `model` with openings for the doors cut into it (ac_my_indoor.c), or `model` itself */
Gfx* pc_nlr_wall_model(Gfx* model);

/* Outside (pc_nlr_outside.c): a wing on the house for each extra room. The ground under house `house`'s wing for
 * `room`: TRUE when it can be built on, and with `clear` it's cleared (weeds pulled, items and flowers sent to the
 * dump); the wings' ground made solid, the house's actor at `pos` (ac_my_house.c); and drawn, in the house model's
 * space (after its walls) */
int pc_nlr_wing_ground(int house, int room, int clear);
void pc_nlr_wing_collision(int house, const struct xyz_s* pos);
void pc_nlr_draw_wings(struct game_s* game, int house, int winter, void* palette, const u8 light[3]);

/* Companion save chunk NLRM (pc_ext_save.c) */
int pc_nlr_read_chunk(const char tag[4], void* fp, unsigned int size);
int pc_nlr_write_chunk(void* fp);
void pc_nlr_reset(void);
void pc_nlr_clear_house(int house);

#ifdef __cplusplus
}
#endif

#endif /* PC_NLROOMS_H */
