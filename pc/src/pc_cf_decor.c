/* pc_cf_decor.c - City Folk's wallpaper and carpets (experimental): the ones the GameCube doesn't have, converted
 * from the player's disc (pc/tools/cf_decor.py -> cf_sky/decor.cfdecor). They're wallpaper/carpet indices from
 * PC_CF_DECOR_FIRST up, and items ITM_WALL00/ITM_CARPET00 + index, as the GameCube's own: their names and prices
 * come from here, and the catalog always has them to order. */

#include "pc_cf_decor.h"
#include "m_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PC_CF_DECOR_WALL_SIZE (16 * 2 + 2 * 0x800)
#define PC_CF_DECOR_FLOOR_SIZE (16 * 2 + 4 * 0x800)

typedef struct {
    char name[17];
    u32 price;
    u8* data; /* palette (host order) then the tiles: what mRmTp_CopyWallData/CopyFloorData give */
} pc_cf_decor_c;

static pc_cf_decor_c l_wall[PC_CF_DECOR_MAX];
static pc_cf_decor_c l_floor[PC_CF_DECOR_MAX];
static int l_walls, l_floors;
static int l_loaded;

static int pc_cf_decor_read(FILE* f, pc_cf_decor_c* d, int size) {
    u8 head[20];

    if (fread(head, 1, 20, f) != 20 || (d->data = (u8*)malloc(size)) == NULL || fread(d->data, 1, size, f) != (size_t)size) {
        return FALSE;
    }
    memcpy(d->name, head, 16);
    d->name[16] = '\0';
    d->price = head[16] | head[17] << 8 | head[18] << 16 | (u32)head[19] << 24;
    return TRUE;
}

static void pc_cf_decor_load(void) {
    FILE* f;
    u8 head[16];
    u32 nw, nf;
    int i;

    if (l_loaded) {
        return;
    }
    l_loaded = TRUE;
    f = fopen("cf_sky/decor.cfdecor", "rb");
    if (f == NULL) {
        return;
    }
    if (fread(head, 1, 16, f) == 16 && memcmp(head, "CFDC", 4) == 0) {
        nw = head[8] | head[9] << 8;
        nf = head[12] | head[13] << 8;
        for (i = 0; i < (int)nw && i < PC_CF_DECOR_MAX; i++) {
            if (!pc_cf_decor_read(f, &l_wall[i], PC_CF_DECOR_WALL_SIZE)) {
                break;
            }
            l_walls = i + 1;
        }
        if (nw > PC_CF_DECOR_MAX) {
            fseek(f, (long)(nw - PC_CF_DECOR_MAX) * (20 + PC_CF_DECOR_WALL_SIZE), SEEK_CUR);
        }
        for (i = 0; i < (int)nf && i < PC_CF_DECOR_MAX; i++) {
            if (!pc_cf_decor_read(f, &l_floor[i], PC_CF_DECOR_FLOOR_SIZE)) {
                break;
            }
            l_floors = i + 1;
        }
    }
    fclose(f);
    printf("[cf decor] %d wallpaper, %d carpets\n", l_walls, l_floors);
}

static pc_cf_decor_c* pc_cf_decor_get(int is_wall, int idx) {
    pc_cf_decor_load();
    idx -= PC_CF_DECOR_FIRST;
    if (idx < 0 || idx >= (is_wall ? l_walls : l_floors)) {
        return NULL;
    }
    return is_wall ? &l_wall[idx] : &l_floor[idx];
}

int pc_cf_decor_count(int is_wall) {
    pc_cf_decor_load();
    return is_wall ? l_walls : l_floors;
}

int pc_cf_decor_is(int is_wall, int idx) {
    return pc_cf_decor_get(is_wall, idx) != NULL;
}

int pc_cf_decor_item(mActor_name_t item) {
    if (item >= ITM_WALL00 + PC_CF_DECOR_FIRST && item < ITM_WALL00 + PC_CF_DECOR_FIRST + PC_CF_DECOR_MAX) {
        return pc_cf_decor_is(TRUE, item - ITM_WALL00);
    }
    if (item >= ITM_CARPET00 + PC_CF_DECOR_FIRST && item < ITM_CARPET00 + PC_CF_DECOR_FIRST + PC_CF_DECOR_MAX) {
        return pc_cf_decor_is(FALSE, item - ITM_CARPET00);
    }
    return FALSE;
}

static pc_cf_decor_c* pc_cf_decor_of(mActor_name_t item) {
    if (item >= ITM_WALL00 + PC_CF_DECOR_FIRST && item < ITM_WALL00 + PC_CF_DECOR_FIRST + PC_CF_DECOR_MAX) {
        return pc_cf_decor_get(TRUE, item - ITM_WALL00);
    }
    if (item >= ITM_CARPET00 + PC_CF_DECOR_FIRST && item < ITM_CARPET00 + PC_CF_DECOR_FIRST + PC_CF_DECOR_MAX) {
        return pc_cf_decor_get(FALSE, item - ITM_CARPET00);
    }
    return NULL;
}

int pc_cf_decor_copy(u8* dst, int is_wall, int idx) {
    pc_cf_decor_c* d = pc_cf_decor_get(is_wall, idx);

    if (d == NULL || dst == NULL) {
        return FALSE;
    }
    memcpy(dst, d->data, is_wall ? PC_CF_DECOR_WALL_SIZE : PC_CF_DECOR_FLOOR_SIZE);
    return TRUE;
}

int pc_cf_decor_name(mActor_name_t item, u8* dst, int len) {
    pc_cf_decor_c* d = pc_cf_decor_of(item);
    int k, n;

    if (d == NULL) {
        return FALSE;
    }
    n = (int)strlen(d->name);
    while (n > 0 && d->name[n - 1] == ' ') {
        n--;
    }
    for (k = 0; k < len; k++) {
        dst[k] = k < n ? (u8)d->name[k] : CHAR_SPACE;
    }
    return TRUE;
}

u32 pc_cf_decor_price(mActor_name_t item) {
    pc_cf_decor_c* d = pc_cf_decor_of(item);

    return d != NULL ? d->price : 0;
}
