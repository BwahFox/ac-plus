#ifndef PC_EXT_SAVE_H
#define PC_EXT_SAVE_H

/* ac-plus companion save: data the GameCube save has no room for, kept next to
 * the town's .gci as <gci>.acplus and written/read/cloud-synced with it. The
 * .gci itself stays exactly GameCube format.
 *
 * Holds per-unit ground patterns for the town, the dressers' storages (pc_dresser.c) and
 * the pattern IDs and library (pc_pattern.c).
 * It belongs to one town (land id): data from another town is never used. */

#include "types.h"
#include "m_personal_id.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Town ground: 6 x 5 acres of 16 x 16 units, like Save_t.fg. */
#define PC_EXT_GROUND_BLOCK_Z 6
#define PC_EXT_GROUND_BLOCK_X 5
#define PC_EXT_UNITS 16

/* Item storage: pages of 15, the pockets' size. */
#define PC_EXT_STORAGE_PAGE_SIZE 15
#define PC_EXT_STORAGE_PAGES 6
#define PC_EXT_STORAGE_COUNT (PC_EXT_STORAGE_PAGE_SIZE * PC_EXT_STORAGE_PAGES)
#define PC_EXT_PLAYERS 4

typedef struct {
    u16 item; /* mActor_name_t, EMPTY_NO when free */
    u8 cond;  /* mPr_ITEM_COND_* */
    u8 pad;
} PCExtStoredItem;

/* After the town's save is read / before nothing else: load or reset. */
void pc_ext_save_load(const char* gci_path);
/* After the town's save is written. */
void pc_ext_save_write(const char* gci_path);
/* The companion file's path for a .gci path. */
void pc_ext_save_path(char* out, int size, const char* gci_path);

/* Ground pattern of a town unit: 0 = none, else a pattern ID (pc_pattern.h).
 * bx/bz are town acres (0-based, FG blocks), ux/uz units within the acre. */
u32 pc_ext_ground_get(int bx, int bz, int ux, int uz);
void pc_ext_ground_set(int bx, int bz, int ux, int uz, u32 value);
/* Calls fn for every pattern ID on the ground (once per unit). */
void pc_ext_ground_each_id(void (*fn)(u32 id));
/* Does any unit of this acre hold a pattern? (quick skip when drawing) */
int pc_ext_ground_block_used(int bx, int bz);


#ifdef __cplusplus
}
#endif

#endif /* PC_EXT_SAVE_H */
