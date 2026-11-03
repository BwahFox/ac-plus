#ifndef PC_CF_FTR_H
#define PC_CF_FTR_H

/* City Folk's furniture: the pieces the GameCube has none of the same name of (pc_cf_ftr.c, cf_sky/furniture.cfftr
 * from pc/tools/cf_furniture.py and pc/tools/cf_furniture.tsv) */

#include "types.h"
#include "m_name_table.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ftr_profile_s;

/* Their items: 0x3700 + 4 * slot (the low 2 bits are the facing, as the GameCube's), and the furniture indices
 * they make (mRmTp_FtrItemNo2FtrIdx): past the GameCube's own, which stop at 0x4F1 */
#define PC_CF_FTR_ITEM_FIRST 0x3700
#define PC_CF_FTR_SLOTS 448 /* up to 0x3DFF: from 0x3C00, City Folk's fish and bugs put in the house, from 0x3D00 as themselves in the museum (pc_cf_creature.h) */
#define PC_CF_FTR_CATALOG_SLOTS 320 /* the ones the catalog can have: up to 0x3BFF (the save's CFFC is this big) */
#define PC_CF_FTR_IDX_FIRST (0x400 + ((PC_CF_FTR_ITEM_FIRST - 0x3000) >> 2))
/* Every furniture index there can be (0x3000-0x3FFF's top one is 0x7FF): the PC's per-index tables are this big */
#define PC_FTR_IDX_NUM 0x800

int pc_cf_ftr_item(mActor_name_t item); /* one of their items (any facing), and it's there */
int pc_cf_ftr_idx(int ftr_idx);         /* one of their furniture indices, and it's there */
int pc_cf_ftr_count(void);
mActor_name_t pc_cf_ftr_nth(int n);     /* the n-th one there (south-facing item), 0 past the end */
struct ftr_profile_s* pc_cf_ftr_profile(int ftr_idx);
int pc_cf_ftr_name(mActor_name_t item, u8* dst, int len);
u32 pc_cf_ftr_price(mActor_name_t item);
int pc_cf_ftr_series(int ftr_idx);      /* its HRA series (m_mark_room.h), -1 none */
int pc_cf_ftr_group(int ftr_idx);       /* Nook's shop group: 0 A, 1 B, 2 C; -1 not one of theirs */
/* A piece at the current matrix, with its animation at frame (over and over): the museum's fish and bugs */
struct game_s;
void pc_cf_ftr_draw_model(mActor_name_t item, struct game_s* game, f32 frame);
f32 pc_cf_ftr_model_width(mActor_name_t item); /* that piece's width, in its own units */
int pc_cf_ftr_surface(int ftr_idx);     /* things can be put on it */

/* The catalog: the resident playing has had it / now has */
int pc_cf_ftr_collected(mActor_name_t item);
void pc_cf_ftr_collect(mActor_name_t item);
/* the town's ac-plus data (pc_ext_save.c): CFFC */
void pc_cf_ftr_reset(void);
int pc_cf_ftr_read_chunk(const char tag[4], void* fp, unsigned int size);
int pc_cf_ftr_write_chunk(void* fp);

#ifdef __cplusplus
}
#endif

#endif /* PC_CF_FTR_H */
