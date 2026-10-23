/* pc_ext_save.c - the ac-plus companion save (see pc_ext_save.h).
 *
 * File: "ACPX", u32 version, u16 land id, u16 pad, then chunks of
 * { char tag[4]; u32 size; data }. Little-endian, PC only. Unknown chunks are
 * skipped, missing ones stay empty, so later versions can add chunks freely.
 *
 * Chunks: GRNI ground pattern IDs, STOR item storage, CONS constellations, and the pattern chunks of
 * pc_pattern.c. GRND (ground as player/slot bytes) is only read, from older
 * files, and turned into IDs. */
#include "pc_brewster.h"
#include "pc_ext_save.h"
#include "pc_pattern.h"
#include "pc_constellation.h"
#include "m_common_data.h"
#include "m_name_table.h"

#include <dolphin/os.h>
#include <stdio.h>
#include <string.h>

#define EXT_MAGIC "ACPX"
#define EXT_VERSION 1

typedef struct {
    PersonalID_c owner;
    PCExtStoredItem items[PC_EXT_STORAGE_COUNT];
} ExtStorage;

static struct {
    u16 land_id;
    u32 ground[PC_EXT_GROUND_BLOCK_Z][PC_EXT_GROUND_BLOCK_X][PC_EXT_UNITS][PC_EXT_UNITS];
    ExtStorage storage[PC_EXT_PLAYERS];
} s_ext;

/* Per-acre "has any pattern" cache, rebuilt on load and kept on set. */
static u8 s_ground_used[PC_EXT_GROUND_BLOCK_Z][PC_EXT_GROUND_BLOCK_X];

/* GRND from an older file: 1 + (player_no << 3 | slot) per unit, 0 = none. */
static u8 s_legacy_ground[PC_EXT_GROUND_BLOCK_Z][PC_EXT_GROUND_BLOCK_X][PC_EXT_UNITS][PC_EXT_UNITS];
static int s_legacy_ground_read;

static void ext_reset(u16 land_id) {
    int p;
    int i;

    memset(&s_ext, 0, sizeof(s_ext));
    memset(s_ground_used, 0, sizeof(s_ground_used));
    s_legacy_ground_read = FALSE;
    pc_pattern_reset();
    pc_const_reset();
    pc_brewster_reset();
    s_ext.land_id = land_id;
    for (p = 0; p < PC_EXT_PLAYERS; p++) {
        for (i = 0; i < PC_EXT_STORAGE_COUNT; i++) {
            s_ext.storage[p].items[i].item = EMPTY_NO;
        }
    }
}

static void rebuild_ground_used(void) {
    int bz;
    int bx;
    int u;

    for (bz = 0; bz < PC_EXT_GROUND_BLOCK_Z; bz++) {
        for (bx = 0; bx < PC_EXT_GROUND_BLOCK_X; bx++) {
            const u32* units = &s_ext.ground[bz][bx][0][0];

            s_ground_used[bz][bx] = FALSE;
            for (u = 0; u < PC_EXT_UNITS * PC_EXT_UNITS; u++) {
                if (units[u] != 0) {
                    s_ground_used[bz][bx] = TRUE;
                    break;
                }
            }
        }
    }
}

static u16 current_land_id(void) {
    return Save_Get(land_info).id;
}

/* New town since this data was loaded (town reset): start over. */
static void ext_check_town(void) {
    if (s_ext.land_id != current_land_id()) {
        ext_reset(current_land_id());
    }
}

/* Ground laid by an older version: the pattern each unit's slot holds now. */
static void migrate_legacy_ground(void) {
    u8* old = &s_legacy_ground[0][0][0][0];
    u32* units = &s_ext.ground[0][0][0][0];
    int i;

    for (i = 0; i < (int)sizeof(s_legacy_ground); i++) {
        if (old[i] != 0 && units[i] == 0) {
            units[i] = pc_pattern_slot_id(((old[i] - 1) >> 3) & 3, (old[i] - 1) & 7);
        }
    }
    s_legacy_ground_read = FALSE;
    OSReport("[acplus] Ground patterns now use pattern IDs\n");
}

void pc_ext_save_path(char* out, int size, const char* gci_path) {
    snprintf(out, size, "%s.acplus", gci_path);
}

void pc_ext_save_load(const char* gci_path) {
    char path[320];
    char magic[4];
    u32 version;
    u16 land_id;
    u16 pad;
    FILE* f;

    ext_reset(current_land_id());
    pc_ext_save_path(path, sizeof(path), gci_path);
    f = fopen(path, "rb");
    if (f == NULL) {
        OSReport("[acplus] No companion save yet (%s)\n", path);
        pc_pattern_loaded();
        return;
    }

    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, EXT_MAGIC, 4) != 0 || fread(&version, 4, 1, f) != 1 ||
        fread(&land_id, 2, 1, f) != 1 || fread(&pad, 2, 1, f) != 1) {
        OSReport("[acplus] %s is not a companion save, ignoring it\n", path);
        fclose(f);
        pc_pattern_loaded();
        return;
    }
    if (land_id != current_land_id()) {
        OSReport("[acplus] %s belongs to another town, ignoring it\n", path);
        fclose(f);
        pc_pattern_loaded();
        return;
    }

    for (;;) {
        char tag[4];
        u32 size;
        void* dst = NULL;
        u32 dst_size = 0;

        if (fread(tag, 1, 4, f) != 4 || fread(&size, 4, 1, f) != 1) {
            break;
        }
        if (pc_pattern_read_chunk(tag, f, size) || pc_const_read_chunk(tag, f, size) ||
            pc_brewster_read_chunk(tag, f, size)) {
            continue;
        }
        if (memcmp(tag, "GRNI", 4) == 0) {
            dst = s_ext.ground;
            dst_size = sizeof(s_ext.ground);
        } else if (memcmp(tag, "GRND", 4) == 0) {
            dst = s_legacy_ground;
            dst_size = sizeof(s_legacy_ground);
            s_legacy_ground_read = TRUE;
        } else if (memcmp(tag, "STOR", 4) == 0) {
            dst = s_ext.storage;
            dst_size = sizeof(s_ext.storage);
        }

        if (dst != NULL && size == dst_size) {
            if (fread(dst, 1, size, f) != size) {
                OSReport("[acplus] %s is cut short\n", path);
                break;
            }
        } else if (fseek(f, (long)size, SEEK_CUR) != 0) {
            break;
        }
    }
    fclose(f);
    pc_pattern_loaded();
    if (s_legacy_ground_read) {
        migrate_legacy_ground();
    }
    rebuild_ground_used();
    OSReport("[acplus] Loaded %s\n", path);
}

static int write_chunk(FILE* f, const char* tag, const void* data, u32 size) {
    return fwrite(tag, 1, 4, f) == 4 && fwrite(&size, 4, 1, f) == 1 && fwrite(data, 1, size, f) == size;
}

void pc_ext_save_write(const char* gci_path) {
    char path[320];
    char tmp[330];
    u32 version = EXT_VERSION;
    u16 pad = 0;
    FILE* f;
    int ok;

    ext_check_town();
    pc_ext_save_path(path, sizeof(path), gci_path);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    f = fopen(tmp, "wb");
    if (f == NULL) {
        OSReport("[acplus] Could not write %s\n", tmp);
        return;
    }
    ok = fwrite(EXT_MAGIC, 1, 4, f) == 4 && fwrite(&version, 4, 1, f) == 1 && fwrite(&s_ext.land_id, 2, 1, f) == 1 &&
         fwrite(&pad, 2, 1, f) == 1 && write_chunk(f, "GRNI", s_ext.ground, sizeof(s_ext.ground)) &&
         write_chunk(f, "STOR", s_ext.storage, sizeof(s_ext.storage)) && pc_pattern_write_chunks(f) &&
         pc_const_write_chunk(f) && pc_brewster_write_chunk(f);
    if (fclose(f) != 0) {
        ok = FALSE;
    }
    if (!ok) {
        OSReport("[acplus] Writing %s failed\n", tmp);
        remove(tmp);
        return;
    }
    /* Windows rename() won't replace an existing file. */
    remove(path);
    if (rename(tmp, path) != 0) {
        OSReport("[acplus] Could not replace %s\n", path);
        remove(tmp);
    }
}

static int ground_in_range(int bx, int bz, int ux, int uz) {
    return bx >= 0 && bx < PC_EXT_GROUND_BLOCK_X && bz >= 0 && bz < PC_EXT_GROUND_BLOCK_Z && ux >= 0 &&
           ux < PC_EXT_UNITS && uz >= 0 && uz < PC_EXT_UNITS;
}

u32 pc_ext_ground_get(int bx, int bz, int ux, int uz) {
    return ground_in_range(bx, bz, ux, uz) ? s_ext.ground[bz][bx][uz][ux] : 0;
}

void pc_ext_ground_set(int bx, int bz, int ux, int uz, u32 value) {
    if (!ground_in_range(bx, bz, ux, uz)) {
        return;
    }
    ext_check_town();
    s_ext.ground[bz][bx][uz][ux] = value;
    if (value != 0) {
        s_ground_used[bz][bx] = TRUE;
    } else {
        rebuild_ground_used();
    }
}

void pc_ext_ground_each_id(void (*fn)(u32 id)) {
    const u32* units = &s_ext.ground[0][0][0][0];
    int i;

    for (i = 0; i < (int)(sizeof(s_ext.ground) / sizeof(u32)); i++) {
        if (units[i] != 0) {
            fn(units[i]);
        }
    }
}

int pc_ext_ground_block_used(int bx, int bz) {
    if (bx < 0 || bx >= PC_EXT_GROUND_BLOCK_X || bz < 0 || bz >= PC_EXT_GROUND_BLOCK_Z) {
        return FALSE;
    }
    return s_ground_used[bz][bx];
}

PCExtStoredItem* pc_ext_storage(int player_no, const PersonalID_c* owner) {
    ExtStorage* st;
    int i;

    if (player_no < 0 || player_no >= PC_EXT_PLAYERS || owner == NULL) {
        return NULL;
    }
    ext_check_town();
    st = &s_ext.storage[player_no];
    if (memcmp(&st->owner, owner, sizeof(PersonalID_c)) != 0) {
        /* A different (or new) resident in this player slot. */
        memcpy(&st->owner, owner, sizeof(PersonalID_c));
        for (i = 0; i < PC_EXT_STORAGE_COUNT; i++) {
            st->items[i].item = EMPTY_NO;
            st->items[i].cond = 0;
        }
    }
    return st->items;
}
