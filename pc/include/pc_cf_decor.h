#ifndef PC_CF_DECOR_H
#define PC_CF_DECOR_H

/* City Folk's wallpaper and carpets (experimental, pc_cf_decor.c) */

#include "types.h"
#include "m_name_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Their wallpaper/carpet indices: past the GameCube's walls, floors, shops' and designs' (and the basement's 71) */
#define PC_CF_DECOR_FIRST 0xA0
#define PC_CF_DECOR_MAX 32

int pc_cf_decor_count(int is_wall);
int pc_cf_decor_is(int is_wall, int idx);    /* a City Folk wallpaper/carpet index that's there */
int pc_cf_decor_item(mActor_name_t item);    /* one of their items */
int pc_cf_decor_copy(u8* dst, int is_wall, int idx); /* its palette and textures, as mRmTp_CopyWallData's */
int pc_cf_decor_name(mActor_name_t item, u8* dst, int len);
u32 pc_cf_decor_price(mActor_name_t item);

#ifdef __cplusplus
}
#endif

#endif /* PC_CF_DECOR_H */
