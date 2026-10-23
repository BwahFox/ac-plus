#ifndef PC_BASEMENT_H
#define PC_BASEMENT_H

/* Wallpaper and carpets in the house basement (pc_basement.c; drawn by src/actor/ac_my_indoor.c) */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PC_BASEMENT_WALL 1
#define PC_BASEMENT_FLOOR 2

/* The basement's own rock wall and floor as items, just past the GameCube's wallpaper and carpets: handed back
 * when the basement is first covered, and putting one down there brings the rock back. They aren't in the
 * catalog, shops, villagers' requests or Saharah's trades; elsewhere they're plain wallpaper/carpet items. */
#define PC_ITM_BASEMENT_FLOOR (0x2600 + 71) /* ITM_CARPET_END */
#define PC_ITM_BASEMENT_WALL (0x2700 + 71)  /* ITM_WALL_END */
#define PC_ITEM_IS_BASEMENT_LOOK(item) ((item) == PC_ITM_BASEMENT_FLOOR || (item) == PC_ITM_BASEMENT_WALL)
#define PC_BASEMENT_LOOK_PRICE 400
/* Has the player put wallpaper (PC_BASEMENT_WALL) or a carpet (PC_BASEMENT_FLOOR) in house's basement? */
int pc_basement_custom(int house, int what);
void pc_basement_set_custom(int house, int what);
/* Back to the basement's own rock wall or floor */
void pc_basement_clear_custom(int house, int what);

/* BSMT in the ac-plus save (pc_ext_save.c) */
void pc_basement_reset(void);
int pc_basement_read_chunk(const char tag[4], void* f, unsigned int size);
int pc_basement_write_chunk(void* f);

#ifdef __cplusplus
}
#endif

#endif /* PC_BASEMENT_H */
