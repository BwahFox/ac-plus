#ifndef PC_OUTDOOR_FTR_H
#define PC_OUTDOOR_FTR_H

/* Furniture placed outdoors in the town (pc_outdoor_ftr.c; ac_my_room.c runs it in "field mode") */

#include "types.h"
#include "m_lib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The grid covers the whole town map, border included, in world units */
#define PC_OFTR_UT_X_NUM (7 * 16)
#define PC_OFTR_UT_Z_NUM (10 * 16)
#define PC_OFTR_UT_TOTAL_NUM (PC_OFTR_UT_X_NUM * PC_OFTR_UT_Z_NUM)

/* Pieces an acre may hold (both layers), and how many can be loaded at once: the acre the player is in and the
 * eight around it (ac_my_room.c loads and unloads them as the player walks) */
int pc_oftr_acre_max(void);
int pc_oftr_loaded_max(void);
/* Pieces in the acre of grid unit ut; TRUE if it can't take another */
int pc_oftr_acre_count(int ut);
int pc_oftr_acre_full(int ut);

/* TRUE in the home town outdoors, where the furniture grid is in use */
int pc_oftr_here(void);
/* Layers as in a room's fg: 0 on the ground, 1 on top of furniture (or what a piece on layer 0 holds, like a stereo's
 * disc), 2 what a piece on layer 1 holds. Unit (x, z) is at [x + z * PC_OFTR_UT_X_NUM]: a piece's item number on its
 * first unit, RSV_FE1F on its others. NULL for another layer */
#define PC_OFTR_LAYER_NUM 3
mActor_name_t* pc_oftr_fg(int layer);
/* The town's grid whatever scene is current: for the town's own furniture actor, whose destructor runs when the
 * next scene may already be set (pc_oftr_fg would hand it the throwaway grid then, and lose what it puts back) */
mActor_name_t* pc_oftr_home_fg(int layer);
/* A unit whose piece was taken away (lost and found): its RSV_NO in the town map goes, unless a villager's house
 * next to it may own that RSV_NO */
void pc_oftr_unit_released(int ut);
/* home ? pc_oftr_home_fg : the throwaway grid (another town's furniture actor) */
mActor_name_t* pc_oftr_grid(int home, int layer);
/* Which way an item lying on furniture (layer 1) at grid unit ut faces */
s16 pc_oftr_angle(int ut);
void pc_oftr_set_angle(int ut, s16 angle);
/* My_Room's per-unit furniture id tables for the grid */
unsigned short* pc_oftr_place_table(int layer);
/* A layer 0 grid unit changed: keep the town's own fg marked (RSV_NO) under furniture */
void pc_oftr_unit_changed(int ut);
/* Can furniture stand on unit (x, z)? Bare, level, dry town ground, as for a ground pattern (or its own marker) */
int pc_oftr_unit_free(int ut_x, int ut_z);
/* Can the player be pulled back onto unit (x, z) from ground height y? Dry land at that height with nothing standing
 * on it (pulling furniture walks the player backwards; a room only has its walls to worry about) */
int pc_oftr_unit_walkable(int ut_x, int ut_z, f32 y);
/* Can a piece already standing on grid unit ut stay there? Not if the town now has something there, or a villager's
 * house or house spot next to it (only the town's fg is looked at, so it can be asked as the town is built) */
int pc_oftr_unit_still_ok(int ut);
/* Is there furniture in the 3 x 3 units around a villager house spot (block (bx, bz) counting the border ring, unit
 * (ut_x, ut_z) in it)? A house built there would stand on it, so the spot isn't offered (m_npc.c) */
int pc_oftr_house_spot_taken(int bx, int bz, int ut_x, int ut_z);
/* Mark every furniture unit in the town's fg (the town was just loaded or built) */
void pc_oftr_mark_town(void);

/* OFTR in the ac-plus save (pc_ext_save.c) */
void pc_oftr_reset(void);
int pc_oftr_read_chunk(const char tag[4], void* f, unsigned int size);
int pc_oftr_write_chunk(void* f);

/* Is the ground under all count units uts at one height (a piece can't straddle a cliff)? */
int pc_oftr_units_level(const int* uts, int count);
/* Put furniture item ftr (its rotation in the item number) on the count units uts (its first unit first, as the
 * furniture actor lists them), as the actor would leave it; it shows once the acre loads again */
int pc_oftr_put(const int* uts, int count, mActor_name_t ftr);

#ifdef __cplusplus
}
#endif

#endif /* PC_OUTDOOR_FTR_H */
