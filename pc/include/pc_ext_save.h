#ifndef PC_EXT_SAVE_H
#define PC_EXT_SAVE_H

/* The ac-plus town save: one file, save/card_a/town.acplus, holding the town's GameCube save image (GCIS chunk)
 * and the data the GameCube save has no room for. Before the single file, that data was a companion
 * <gci>.acplus next to the town's .gci; such a pair is moved into a town file on first load
 * (pc_ext_town_convert), and a .gci can still be exported for Dolphin or a GameCube (--export-gci).
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

/* After the town's save is read / before nothing else: load or reset. path is the town file read (its
 * chunks), or a .gci (its companion file, if any). */
void pc_ext_save_load(const char* path);
/* The companion file's path for a .gci path (the old two-file save). */
void pc_ext_save_path(char* out, int size, const char* gci_path);

/* Is path a single town file? */
int pc_ext_town_is_merged(const char* path);
/* Reads the GameCube image (img_size bytes: GCI header + card data) of the town file at path, or of its newest
 * good backup when it's damaged; used gets the file read, for pc_ext_save_load. FALSE: none could be read. */
int pc_ext_town_read_image(const char* path, u8* img, u32 img_size, char* used, int used_size);
/* Writes the town file: the image and the ac-plus data in memory (backups rotated). */
int pc_ext_town_write(const char* path, const u8* img, u32 img_size);
/* Makes a town file of a .gci (gci_path: the file itself or one of its backups) and the companion file of
 * ext_of, if there is one. The files themselves are left alone. */
int pc_ext_town_convert(const char* gci_path, const char* ext_of, const char* town_path, u16 land_id);
/* Writes the town file's GameCube image out as a .gci. */
int pc_ext_town_export_gci(const char* town_path, const char* gci_out, u32 img_size);

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
