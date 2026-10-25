#ifndef PC_MONEY_TREE_H
#define PC_MONEY_TREE_H

/* 90,000 Bells money trees (pc_money_tree.c; bg_item_common.c_inc plants and shakes them) */

#include "types.h"
#include "m_lib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A 90,000 Bells bag buried in a glowing spot (a money tree, not just a sapling): mark the 30,000 Bells sapling
 * planted there as a 90,000 Bells one */
void pc_money_tree_plant(xyz_t pos);
/* Anything else buried there: it's no longer one */
void pc_money_tree_forget(xyz_t pos);
/* Shaking the grown 30,000 Bells tree at unit (ut_x, ut_z): TRUE (and forgotten) if it's a 90,000 Bells one */
int pc_money_tree_shake(int ut_x, int ut_z);

/* MTRE in the ac-plus save (pc_ext_save.c) */
void pc_money_tree_reset(void);
int pc_money_tree_read_chunk(const char tag[4], void* f, unsigned int size);
int pc_money_tree_write_chunk(void* f);

#ifdef __cplusplus
}
#endif

#endif /* PC_MONEY_TREE_H */
