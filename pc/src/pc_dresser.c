/* pc_dresser.c - a storage for each dresser (see pc_dresser.h).
 *
 * Chunks: DRSR the storages in use ({ u8 id; u8 pad[3]; items[PC_EXT_STORAGE_COUNT] } each), DRBD which
 * furniture holds which ({ u8 house, floor, layer, ut, id, pad[3] } each), DRTG the IDs riding along in
 * pockets ({ u16 item; u8 id; u8 pad } for each player's 15 slots). */
#include "pc_dresser.h"

#include "m_common_data.h"
#include "m_name_table.h"
#include "m_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    u8 id;
    u8 pad[3];
    PCExtStoredItem items[PC_EXT_STORAGE_COUNT];
} DresserStore;

typedef struct {
    u8 house, floor, layer, ut, id, pad[3];
} DresserBind;

typedef struct {
    u16 item;
    u8 id;
    u8 pad;
} DresserTag;

#define BIND_MAX 256 /* more than the furniture all the houses' rooms can hold */

static u8 l_used[PC_DRESSER_MAX];
static PCExtStoredItem l_items[PC_DRESSER_MAX][PC_EXT_STORAGE_COUNT];
static DresserBind l_bind[BIND_MAX];
static int l_bind_num;
static DresserTag l_tag[PLAYER_NUM][mPr_POCKETS_SLOT_COUNT];
static DresserTag l_hand; /* the menu's hand, while it holds an item from the pockets */
static u8 l_pickup;       /* furniture being picked up */
static u8 l_pending;      /* furniture being put down */
static int l_read; /* DRSR came with the file */

static void clear_items(PCExtStoredItem* items) {
    int i;

    for (i = 0; i < PC_EXT_STORAGE_COUNT; i++) {
        items[i].item = EMPTY_NO;
        items[i].cond = 0;
        items[i].pad = 0;
    }
}

static int count_items(const PCExtStoredItem* items) {
    int i, n = 0;

    for (i = 0; i < PC_EXT_STORAGE_COUNT; i++) {
        n += items[i].item != EMPTY_NO;
    }
    return n;
}

void pc_dresser_reset(void) {
    int i;

    memset(l_used, 0, sizeof(l_used));
    for (i = 0; i < PC_DRESSER_MAX; i++) {
        clear_items(l_items[i]);
    }
    l_bind_num = 0;
    memset(l_tag, 0, sizeof(l_tag));
    memset(&l_hand, 0, sizeof(l_hand));
    l_pickup = l_pending = 0;
    l_read = FALSE;
}

int pc_dresser_read_chunk(const char tag[4], void* fp, unsigned int size) {
    FILE* f = (FILE*)fp;

    if (memcmp(tag, "DRSR", 4) == 0) {
        DresserStore st;
        unsigned int n;

        for (n = 0; n + sizeof(st) <= size; n += sizeof(st)) {
            if (fread(&st, sizeof(st), 1, f) != 1) {
                return TRUE;
            }
            if (st.id >= 1 && st.id <= PC_DRESSER_MAX) {
                l_used[st.id - 1] = TRUE;
                memcpy(l_items[st.id - 1], st.items, sizeof(st.items));
            }
        }
        fseek(f, (long)(size - n), SEEK_CUR);
        l_read = TRUE;
        return TRUE;
    }
    if (memcmp(tag, "DRBD", 4) == 0) {
        unsigned int n;

        l_bind_num = 0;
        for (n = 0; n + sizeof(DresserBind) <= size; n += sizeof(DresserBind)) {
            DresserBind b;

            if (fread(&b, sizeof(b), 1, f) != 1) {
                return TRUE;
            }
            if (l_bind_num < BIND_MAX && b.id >= 1 && b.id <= PC_DRESSER_MAX) {
                l_bind[l_bind_num++] = b;
            }
        }
        fseek(f, (long)(size - n), SEEK_CUR);
        return TRUE;
    }
    if (memcmp(tag, "DRTG", 4) == 0) {
        if (size == sizeof(l_tag)) {
            if (fread(l_tag, sizeof(l_tag), 1, f) != 1) {
                memset(l_tag, 0, sizeof(l_tag));
            }
        } else {
            fseek(f, (long)size, SEEK_CUR);
        }
        return TRUE;
    }
    return FALSE;
}

/* Is storage `id` held by furniture or riding along in a pocket? */
static int referenced(int id) {
    int i, p;

    for (i = 0; i < l_bind_num; i++) {
        if (l_bind[i].id == id) {
            return TRUE;
        }
    }
    for (p = 0; p < PLAYER_NUM; p++) {
        for (i = 0; i < mPr_POCKETS_SLOT_COUNT; i++) {
            if (l_tag[p][i].id == id) {
                return TRUE;
            }
        }
    }
    return l_hand.id == id || l_pickup == id || l_pending == id;
}

/* The room on screen: its furniture's IDs into l_bind (ac_my_room.c) */
extern void aMR_pc_dresser_flush(void);

int pc_dresser_write_chunks(void* fp) {
    FILE* f = (FILE*)fp;
    u32 size;
    int i, n = 0;
    int ok;

    aMR_pc_dresser_flush();
    for (i = 0; i < PC_DRESSER_MAX; i++) {
        /* an empty storage nothing holds is just free again */
        if (l_used[i] && !referenced(i + 1) && count_items(l_items[i]) == 0) {
            l_used[i] = FALSE;
        }
        n += l_used[i];
    }
    size = (u32)(n * sizeof(DresserStore));
    ok = fwrite("DRSR", 1, 4, f) == 4 && fwrite(&size, 4, 1, f) == 1;
    for (i = 0; ok && i < PC_DRESSER_MAX; i++) {
        if (l_used[i]) {
            DresserStore st;

            memset(&st, 0, sizeof(st));
            st.id = (u8)(i + 1);
            memcpy(st.items, l_items[i], sizeof(st.items));
            ok = fwrite(&st, sizeof(st), 1, f) == 1;
        }
    }
    size = (u32)(l_bind_num * sizeof(DresserBind));
    ok = ok && fwrite("DRBD", 1, 4, f) == 4 && fwrite(&size, 4, 1, f) == 1 &&
         (l_bind_num == 0 || fwrite(l_bind, sizeof(DresserBind), l_bind_num, f) == (size_t)l_bind_num);
    size = sizeof(l_tag);
    return ok && fwrite("DRTG", 1, 4, f) == 4 && fwrite(&size, 4, 1, f) == 1 && fwrite(l_tag, size, 1, f) == 1;
}

void pc_dresser_loaded(PCExtStoredItem* legacy, int count) {
    int i, p;

    if (l_read) {
        return;
    }
    /* each resident's old shared storage: ownerless, for the first dresser they open */
    for (p = 0; p < count; p++) {
        PCExtStoredItem* items = legacy + p * PC_EXT_STORAGE_COUNT;

        if (count_items(items) == 0) {
            continue;
        }
        for (i = 0; i < PC_DRESSER_MAX; i++) {
            if (!l_used[i]) {
                l_used[i] = TRUE;
                memcpy(l_items[i], items, sizeof(l_items[i]));
                break;
            }
        }
    }
}

PCExtStoredItem* pc_dresser_items(int id) {
    if (id < 1 || id > PC_DRESSER_MAX || !l_used[id - 1]) {
        return NULL;
    }
    return l_items[id - 1];
}

int pc_dresser_new_id(void) {
    int best = 0, best_num = -1, i;

    aMR_pc_dresser_flush();
    for (i = 0; i < PC_DRESSER_MAX; i++) {
        if (l_used[i] && !referenced(i + 1) && count_items(l_items[i]) > best_num) {
            best = i + 1;
            best_num = count_items(l_items[i]);
        }
    }
    if (best == 0) {
        for (i = 0; i < PC_DRESSER_MAX; i++) {
            if (!l_used[i]) {
                l_used[i] = TRUE;
                clear_items(l_items[i]);
                best = i + 1;
                break;
            }
        }
    }
    if (getenv("PC_DRESSER_LOG") != NULL) {
        printf("[Dresser] new ID %d (%d items in it)\n", best, best ? count_items(l_items[best - 1]) : 0);
        fflush(stdout);
    }
    return best;
}

int pc_dresser_bound(int house, int floor, int layer, int ut) {
    int i;

    for (i = 0; i < l_bind_num; i++) {
        if (l_bind[i].house == house && l_bind[i].floor == floor && l_bind[i].layer == layer && l_bind[i].ut == ut) {
            return l_bind[i].id;
        }
    }
    return 0;
}

void pc_dresser_room_clear(int house, int floor) {
    int i, n = 0;

    for (i = 0; i < l_bind_num; i++) {
        if (l_bind[i].house != house || l_bind[i].floor != floor) {
            l_bind[n++] = l_bind[i];
        }
    }
    l_bind_num = n;
}

void pc_dresser_room_bind(int house, int floor, int layer, int ut, int id) {
    DresserBind* b;

    if (id < 1 || id > PC_DRESSER_MAX || l_bind_num >= BIND_MAX) {
        return;
    }
    b = &l_bind[l_bind_num++];
    memset(b, 0, sizeof(*b));
    b->house = (u8)house;
    b->floor = (u8)floor;
    b->layer = (u8)layer;
    b->ut = (u8)ut;
    b->id = (u8)id;
}

static DresserTag* pocket_tag(int slot) {
    int player_no = Common_Get(player_no);

    if (player_no < 0 || player_no >= PLAYER_NUM || slot < 0 || slot >= mPr_POCKETS_SLOT_COUNT) {
        return NULL;
    }
    return &l_tag[player_no][slot];
}

void pc_dresser_tag_pocket(int slot, mActor_name_t item, int id) {
    DresserTag* t = pocket_tag(slot);

    if (t != NULL) {
        t->item = item;
        t->id = (u8)id;
        if (getenv("PC_DRESSER_LOG") != NULL) {
            printf("[Dresser] ID %d into pocket %d (%04X)\n", id, slot, item);
            fflush(stdout);
        }
    }
}

int pc_dresser_take_pocket(int slot, mActor_name_t item) {
    DresserTag* t = pocket_tag(slot);
    int id = 0;

    if (t != NULL) {
        if (t->id != 0 && t->item == item) {
            id = t->id;
        }
        t->item = 0;
        t->id = 0;
    }
    return id;
}

void pc_dresser_set_pickup(int id) {
    l_pickup = (u8)id;
}

void pc_dresser_pickup_into(int slot) {
    if (l_pickup != 0 && Now_Private != NULL && slot >= 0 && slot < mPr_POCKETS_SLOT_COUNT) {
        pc_dresser_tag_pocket(slot, Now_Private->inventory.pockets[slot], l_pickup);
    }
    l_pickup = 0;
}

void pc_dresser_set_pending(int id) {
    l_pending = (u8)id;
}

int pc_dresser_take_pending(void) {
    int id = l_pending;

    l_pending = 0;
    return id;
}

void pc_dresser_hand_catch(int slot) {
    DresserTag* t = pocket_tag(slot);

    if (t != NULL) {
        l_hand = *t;
        memset(t, 0, sizeof(*t));
    }
}

void pc_dresser_hand_swap(int slot) {
    DresserTag* t = pocket_tag(slot);

    if (t != NULL) {
        DresserTag tmp = *t;

        *t = l_hand;
        l_hand = tmp;
    }
}

void pc_dresser_hand_put(int slot) {
    DresserTag* t = pocket_tag(slot);

    if (t != NULL) {
        *t = l_hand;
    }
    memset(&l_hand, 0, sizeof(l_hand));
}

void pc_dresser_dev_dump(void) {
    extern void aMR_pc_dresser_list(void);
    int i, p;

    aMR_pc_dresser_flush();
    aMR_pc_dresser_list();
    for (i = 0; i < PC_DRESSER_MAX; i++) {
        if (l_used[i]) {
            printf("[Dresser] storage %d: %d items%s\n", i + 1, count_items(l_items[i]),
                   referenced(i + 1) ? "" : " (nothing holds it)");
        }
    }
    for (i = 0; i < l_bind_num; i++) {
        printf("[Dresser] house %d floor %d layer %d unit %d,%d: storage %d\n", l_bind[i].house, l_bind[i].floor,
               l_bind[i].layer, l_bind[i].ut % 16, l_bind[i].ut / 16, l_bind[i].id);
    }
    for (p = 0; p < PLAYER_NUM; p++) {
        for (i = 0; i < mPr_POCKETS_SLOT_COUNT; i++) {
            if (l_tag[p][i].id != 0) {
                printf("[Dresser] player %d pocket %d (%04X): storage %d\n", p, i, l_tag[p][i].item, l_tag[p][i].id);
            }
        }
    }
    fflush(stdout);
}
