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
 * when the basement is first covered, and putting one down there brings the rock back. In any other room they're
 * wallpaper and a carpet like the rest (index PC_BASEMENT_LOOK_IDX in its wall/floor pair, which a GameCube shows
 * as the default). They're in the catalog, but not in shops, villagers' requests or Saharah's trades. */
#define PC_ITM_BASEMENT_FLOOR (0x2600 + 71) /* ITM_CARPET_END */
#define PC_ITM_BASEMENT_WALL (0x2700 + 71)  /* ITM_WALL_END */
#define PC_BASEMENT_LOOK_IDX 71             /* their wallpaper/carpet index */
#define PC_ITEM_IS_BASEMENT_LOOK(item) ((item) == PC_ITM_BASEMENT_FLOOR || (item) == PC_ITM_BASEMENT_WALL)
#define PC_BASEMENT_LOOK_PRICE 400
/* Has the player put wallpaper (PC_BASEMENT_WALL) or a carpet (PC_BASEMENT_FLOOR) in house's basement? */
int pc_basement_custom(int house, int what);
void pc_basement_set_custom(int house, int what);
/* Back to the basement's own rock wall or floor */
void pc_basement_clear_custom(int house, int what);
/* The rock wall (PC_BASEMENT_WALL) or floor (PC_BASEMENT_FLOOR) as a room's wallpaper/carpet data: palette, then
 * its two wall or four floor textures (the layout mRmTp_CopyWallData/CopyFloorData give) */
void pc_basement_copy_look(u8* dst, int what);

/* BSMT in the ac-plus save (pc_ext_save.c) */
void pc_basement_reset(void);
int pc_basement_read_chunk(const char tag[4], void* f, unsigned int size);
int pc_basement_write_chunk(void* f);

#ifdef __cplusplus
}
#endif

#endif /* PC_BASEMENT_H */
