/* pc_money_tree.c - 90,000 Bells money trees.
 *
 * The GameCube has a money tree for each of its bags, each its own field item through four growth stages.
 * The 90,000 Bells one is a 30,000 Bells tree (it looks the same, as they all do) whose unit is kept here,
 * as MTRE in the ac-plus save: shaken, it drops 90,000 Bells bags. A unit stops being one once the tree there
 * is no longer a 30,000 Bells one (shaken, chopped, withered) or something else is buried there.
 * Only in the town: the island's units aren't the town's. */
#include "pc_money_tree.h"
#include "m_field_info.h"
#include "m_name_table.h"

#include <stdio.h>
#include <string.h>

#define MTREE_MAX 64

typedef struct {
    s16 ut_x;
    s16 ut_z;
} mtree_c;

static mtree_c l_tree[MTREE_MAX];
static u32 l_count;

static int is_30000_tree(mActor_name_t item) {
    return item >= TREE_30000BELLS_SAPLING && item <= TREE_30000BELLS;
}

static int find(int ut_x, int ut_z) {
    u32 i;

    for (i = 0; i < l_count; i++) {
        if (l_tree[i].ut_x == ut_x && l_tree[i].ut_z == ut_z) {
            return (int)i;
        }
    }
    return -1;
}

static void remove_at(u32 i) {
    memmove(&l_tree[i], &l_tree[i + 1], (l_count - i - 1) * sizeof(mtree_c));
    l_count--;
}

/* Forget the units whose tree isn't a 30,000 Bells one any more (in the town only, where they can be seen) */
static void prune(void) {
    u32 i = 0;

    if (mFI_CheckInIsland() || mFI_GetFieldId() != mFI_FIELD_FG) {
        return;
    }
    while (i < l_count) {
        mActor_name_t* fg = mFI_UtNum2UtFG(l_tree[i].ut_x, l_tree[i].ut_z);

        if (fg == NULL || !is_30000_tree(*fg)) {
            remove_at(i);
        } else {
            i++;
        }
    }
}

static int town_unit(xyz_t pos, int* ut_x, int* ut_z) {
    return !mFI_CheckInIsland() && mFI_GetFieldId() == mFI_FIELD_FG && mFI_Wpos2UtNum(ut_x, ut_z, pos);
}

void pc_money_tree_forget(xyz_t pos) {
    int ut_x;
    int ut_z;
    int i;

    if (town_unit(pos, &ut_x, &ut_z) && (i = find(ut_x, ut_z)) >= 0) {
        remove_at((u32)i);
    }
}

void pc_money_tree_plant(xyz_t pos) {
    int ut_x;
    int ut_z;

    if (!town_unit(pos, &ut_x, &ut_z)) {
        return;
    }
    prune();
    if (find(ut_x, ut_z) >= 0) {
        return;
    }
    if (l_count == MTREE_MAX) {
        remove_at(0); /* the oldest */
    }
    l_tree[l_count].ut_x = (s16)ut_x;
    l_tree[l_count].ut_z = (s16)ut_z;
    l_count++;
}

int pc_money_tree_shake(int ut_x, int ut_z) {
    int i;

    if (mFI_CheckInIsland()) {
        return FALSE;
    }
    i = find(ut_x, ut_z);
    if (i < 0) {
        return FALSE;
    }
    remove_at((u32)i);
    return TRUE;
}

void pc_money_tree_reset(void) {
    l_count = 0;
}

int pc_money_tree_read_chunk(const char tag[4], void* fp, unsigned int size) {
    FILE* f = (FILE*)fp;

    if (memcmp(tag, "MTRE", 4) != 0) {
        return FALSE;
    }
    pc_money_tree_reset();
    if (size % sizeof(mtree_c) != 0 || size > sizeof(l_tree) || fread(l_tree, 1, size, f) != size) {
        if (size % sizeof(mtree_c) != 0 || size > sizeof(l_tree)) {
            fseek(f, (long)size, SEEK_CUR);
        }
        return TRUE;
    }
    l_count = size / sizeof(mtree_c);
    return TRUE;
}

int pc_money_tree_write_chunk(void* fp) {
    FILE* f = (FILE*)fp;
    u32 size;

    prune();
    size = l_count * sizeof(mtree_c);
    return fwrite("MTRE", 1, 4, f) == 4 && fwrite(&size, 4, 1, f) == 1 &&
           (size == 0 || fwrite(l_tree, 1, size, f) == size);
}
