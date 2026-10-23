/* pc_pattern.c - stable pattern IDs (see pc_pattern.h).
 *
 * Locations are the 8 slots of each resident (32) and the 96 storage spaces.
 * Each holds an ID; the library holds the last design seen for every ID.
 * Reconciling compares each location with its ID's library copy:
 *   - same design: nothing to do
 *   - changed, and the editor said so (pc_pattern_note_edit): same ID, new copy
 *   - otherwise the pattern left: if another location now holds a design that
 *     left its own spot in the same pass, or one the library knows but no
 *     location holds, that ID moves there (swaps with storage, buying a pattern
 *     back); anything else is a new pattern with a new ID.
 * The library copy of a pattern that left stays as long as something uses it.
 *
 * Chunks: PIDS (IDs of the locations and of worn shirts / held umbrellas),
 * PLIB (the library) and PUSE (doors, walls, floors, mannequins, signs). Only for the home town's residents; visitors and other
 * towns are left alone. */
#include "pc_pattern.h"
#include "pc_ext_save.h"
#include "m_common_data.h"
#include "m_card.h"
#include "m_private.h"
#include "m_player_lib.h"
#include "m_house.h"

#include <dolphin/os.h>
#include <string.h>

#define PAT_SLOTS (PLAYER_NUM * mPr_ORIGINAL_DESIGN_COUNT)
#define PAT_STORE (mCD_KEEP_ORIGINAL_PAGE_COUNT * mCD_KEEP_ORIGINAL_COUNT)
#define PAT_LOCS (PAT_SLOTS + PAT_STORE)
#define PAT_LIB_MAX 1024

void* pc_card_aram_block(int idx);

/* Saved as PIDS. */
typedef struct {
    u32 next_id;
    PersonalID_c owner[PLAYER_NUM];
    u32 loc_id[PAT_LOCS];
    u32 cloth_id[PLAYER_NUM];
    u16 cloth_idx[PLAYER_NUM]; /* the cloth.idx it was bound for */
    u32 umbrella_id[PLAYER_NUM];
    u8 umbrella_image[PLAYER_NUM];
} PatIds;

/* Saved as PLIB, one after another. The design stays 32-byte aligned for drawing. */
typedef struct {
    u32 id;
    u32 pad;
    u64 hash;
    u8 pad2[16];
    mNW_original_design_c design;
} PatLibEntry;

/* Saved as PUSE, one after another. */
typedef struct {
    u8 kind;
    s8 player_no;
    u8 image;
    u8 pad;
    u32 key;
    u32 id;
} PatUse;

#define PAT_USE_MAX 1024

static PatIds s_ids;
static PatUse s_uses[PAT_USE_MAX];
static int s_use_count;
static PatLibEntry s_lib[PAT_LIB_MAX] ATTRIBUTE_ALIGN(32);
static int s_lib_count;
static u8 s_edited[PAT_LOCS];
static int s_last_open;
static int s_loaded;

/* --- Locations --- */

static int active(void) {
    return s_loaded && Common_Get(player_no) < PLAYER_NUM;
}

static mCD_keep_original_c* keep_block(void) {
    return (mCD_keep_original_c*)pc_card_aram_block(mCD_ARAM_DATA_ORIGINAL);
}

static mNW_original_design_c* loc_design(int loc) {
    mCD_keep_original_c* keep;

    if (loc < PAT_SLOTS) {
        return &Save_Get(private_data[loc / mPr_ORIGINAL_DESIGN_COUNT]).my_org[loc % mPr_ORIGINAL_DESIGN_COUNT];
    }
    keep = keep_block();
    if (keep == NULL) {
        return NULL;
    }
    loc -= PAT_SLOTS;
    return &keep->original[loc / mCD_KEEP_ORIGINAL_COUNT][loc % mCD_KEEP_ORIGINAL_COUNT];
}

/* FNV-1a over everything but the padding before the texture. */
static u64 design_hash(const mNW_original_design_c* d) {
    u64 h = 0xCBF29CE484222325ull;
    const u8* p = (const u8*)d;
    int i;

    for (i = 0; i < (int)sizeof(*d); i++) {
        if (i == 0x12) {
            i = 0x20;
        }
        h = (h ^ p[i]) * 0x100000001B3ull;
    }
    return h;
}

/* --- Library --- */

static PatLibEntry* lib_find(u32 id) {
    static int last = 0;
    int i;

    if (id == 0) {
        return NULL;
    }
    if (last < s_lib_count && s_lib[last].id == id) {
        return &s_lib[last];
    }
    for (i = 0; i < s_lib_count; i++) {
        if (s_lib[i].id == id) {
            last = i;
            return &s_lib[i];
        }
    }
    return NULL;
}

static int loc_holds(u32 id) {
    int loc;

    for (loc = 0; loc < PAT_LOCS; loc++) {
        if (s_ids.loc_id[loc] == id) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Things outside the locations that use an ID. */
static u8* s_keep_flags;

static void keep_id(u32 id) {
    int i;

    for (i = 0; i < s_lib_count; i++) {
        if (s_lib[i].id == id) {
            s_keep_flags[i] = TRUE;
            return;
        }
    }
}

/* Drop library entries nothing uses any more. */
static void lib_compact(void) {
    static u8 keep[PAT_LIB_MAX];
    int i;
    int n = 0;

    memset(keep, 0, sizeof(keep));
    s_keep_flags = keep;
    for (i = 0; i < PAT_LOCS; i++) {
        keep_id(s_ids.loc_id[i]);
    }
    for (i = 0; i < PLAYER_NUM; i++) {
        keep_id(s_ids.cloth_id[i]);
        keep_id(s_ids.umbrella_id[i]);
    }
    for (i = 0; i < s_use_count; i++) {
        keep_id(s_uses[i].id);
    }
    pc_ext_ground_each_id(keep_id);
    s_keep_flags = NULL;

    for (i = 0; i < s_lib_count; i++) {
        if (keep[i]) {
            if (n != i) {
                s_lib[n] = s_lib[i];
            }
            n++;
        }
    }
    s_lib_count = n;
}

static u32 lib_add(const mNW_original_design_c* d, u64 hash) {
    PatLibEntry* e;

    if (s_lib_count == PAT_LIB_MAX) {
        lib_compact();
        if (s_lib_count == PAT_LIB_MAX) {
            OSReport("[pattern] library full\n");
            return 0;
        }
    }
    if (s_ids.next_id == 0) {
        s_ids.next_id = 1;
    }
    e = &s_lib[s_lib_count++];
    memset(e, 0, sizeof(*e));
    e->id = s_ids.next_id++;
    e->hash = hash;
    memcpy(&e->design, d, sizeof(e->design));
    return e->id;
}

/* --- Reconcile --- */

/* A different resident in a player slot: their patterns are new. */
static void check_owners(void) {
    int p;
    int i;

    for (p = 0; p < PLAYER_NUM; p++) {
        PersonalID_c* pid = &Save_Get(private_data[p]).player_ID;

        if (memcmp(&s_ids.owner[p], pid, sizeof(PersonalID_c)) != 0) {
            memcpy(&s_ids.owner[p], pid, sizeof(PersonalID_c));
            for (i = 0; i < mPr_ORIGINAL_DESIGN_COUNT; i++) {
                s_ids.loc_id[p * mPr_ORIGINAL_DESIGN_COUNT + i] = 0;
            }
            s_ids.cloth_id[p] = 0;
            s_ids.umbrella_id[p] = 0;
        }
    }
}

void pc_pattern_reconcile(void) {
    static u64 hash[PAT_LOCS];
    static u8 moved[PAT_LOCS];
    static u32 orphan[PAT_LOCS];
    int n_orphan = 0;
    int loc;
    int k;

    if (!active()) {
        return;
    }
    check_owners();

    for (loc = 0; loc < PAT_LOCS; loc++) {
        const mNW_original_design_c* d = loc_design(loc);
        PatLibEntry* e;

        moved[loc] = FALSE;
        if (d == NULL) {
            continue;
        }
        hash[loc] = design_hash(d);
        e = lib_find(s_ids.loc_id[loc]);
        if (e != NULL && e->hash == hash[loc]) {
            continue;
        }
        if (e != NULL && s_edited[loc]) {
            e->hash = hash[loc];
            memcpy(&e->design, d, sizeof(e->design));
            continue;
        }
        if (e != NULL) {
            orphan[n_orphan++] = e->id;
        }
        moved[loc] = TRUE;
    }

    for (loc = 0; loc < PAT_LOCS; loc++) {
        u32 id = 0;

        if (!moved[loc]) {
            continue;
        }
        /* A pattern that left its spot in this same pass? */
        for (k = 0; k < n_orphan && id == 0; k++) {
            PatLibEntry* e = lib_find(orphan[k]);

            if (e != NULL && e->hash == hash[loc]) {
                id = orphan[k];
                orphan[k] = 0;
            }
        }
        /* One the library still keeps for something, back in a slot? */
        for (k = 0; k < s_lib_count && id == 0; k++) {
            if (s_lib[k].hash == hash[loc] && !loc_holds(s_lib[k].id)) {
                id = s_lib[k].id;
            }
        }
        if (id == 0) {
            id = lib_add(loc_design(loc), hash[loc]);
        }
        s_ids.loc_id[loc] = id;
    }
    memset(s_edited, 0, sizeof(s_edited));
}

void pc_pattern_tick(int submenu_open) {
    if (s_last_open && !submenu_open) {
        pc_pattern_reconcile();
    }
    s_last_open = submenu_open;
}

void pc_pattern_note_edit(int player_no, int image) {
    if (player_no >= 0 && player_no < PLAYER_NUM && image >= 0 && image < mPr_ORIGINAL_DESIGN_COUNT) {
        s_edited[player_no * mPr_ORIGINAL_DESIGN_COUNT + image] = TRUE;
    }
}

/* --- Lookups and bindings --- */

u32 pc_pattern_slot_id(int player_no, int image) {
    if (player_no < 0 || player_no >= PLAYER_NUM || image < 0 || image >= mPr_ORIGINAL_DESIGN_COUNT) {
        return 0;
    }
    pc_pattern_reconcile();
    return active() ? s_ids.loc_id[player_no * mPr_ORIGINAL_DESIGN_COUNT + image] : 0;
}

const mNW_original_design_c* pc_pattern_get(u32 id) {
    PatLibEntry* e = lib_find(id);

    return e != NULL ? &e->design : NULL;
}

void pc_pattern_bind_cloth(int player_no, int image) {
    u32 id = pc_pattern_slot_id(player_no, image);

    if (id != 0) {
        s_ids.cloth_id[player_no] = id;
        s_ids.cloth_idx[player_no] = (u16)(0x100 + image);
    }
}

const mNW_original_design_c* pc_pattern_worn(int player_no, int cloth_idx) {
    if (!active() || player_no < 0 || player_no >= PLAYER_NUM || s_ids.cloth_id[player_no] == 0 ||
        s_ids.cloth_idx[player_no] != cloth_idx) {
        return NULL;
    }
    return pc_pattern_get(s_ids.cloth_id[player_no]);
}

void pc_pattern_bind_umbrella(int player_no, int image) {
    u32 id = pc_pattern_slot_id(player_no, image);

    if (id != 0) {
        s_ids.umbrella_id[player_no] = id;
        s_ids.umbrella_image[player_no] = (u8)image;
    }
}

const mNW_original_design_c* pc_pattern_umbrella(int player_no, int image) {
    if (!active() || player_no < 0 || player_no >= PLAYER_NUM || s_ids.umbrella_id[player_no] == 0 ||
        s_ids.umbrella_image[player_no] != image) {
        return NULL;
    }
    return pc_pattern_get(s_ids.umbrella_id[player_no]);
}

static PatUse* use_find(int kind, u32 key) {
    int i;

    for (i = 0; i < s_use_count; i++) {
        if (s_uses[i].kind == kind && s_uses[i].key == key) {
            return &s_uses[i];
        }
    }
    return NULL;
}

void pc_pattern_bind_use(int kind, u32 key, int player_no, int image) {
    u32 id = pc_pattern_slot_id(player_no, image);
    PatUse* u;

    if (id == 0) {
        return;
    }
    u = use_find(kind, key);
    if (u == NULL) {
        if (s_use_count == PAT_USE_MAX) {
            OSReport("[pattern] too many things made from patterns\n");
            return;
        }
        u = &s_uses[s_use_count++];
    }
    u->kind = (u8)kind;
    u->player_no = (s8)player_no;
    u->image = (u8)image;
    u->pad = 0;
    u->key = key;
    u->id = id;
}

const mNW_original_design_c* pc_pattern_use(int kind, u32 key, int player_no, int image) {
    PatUse* u;

    if (!s_loaded) {
        return NULL;
    }
    u = use_find(kind, key);
    if (u == NULL || u->image != (image & 7) || (player_no >= 0 && u->player_no != player_no)) {
        return NULL;
    }
    return pc_pattern_get(u->id);
}

/* --- Companion save --- */

void pc_pattern_reset(void) {
    memset(&s_ids, 0, sizeof(s_ids));
    s_ids.next_id = 1;
    s_lib_count = 0;
    s_use_count = 0;
    memset(s_edited, 0, sizeof(s_edited));
    s_loaded = FALSE;
}

int pc_pattern_read_chunk(const char* tag, FILE* f, u32 size) {
    if (memcmp(tag, "PIDS", 4) == 0 && size == sizeof(s_ids)) {
        return fread(&s_ids, 1, size, f) == size;
    }
    if (memcmp(tag, "PLIB", 4) == 0 && size % sizeof(PatLibEntry) == 0 && size / sizeof(PatLibEntry) <= PAT_LIB_MAX) {
        s_lib_count = (int)(size / sizeof(PatLibEntry));
        return fread(s_lib, 1, size, f) == size;
    }
    if (memcmp(tag, "PUSE", 4) == 0 && size % sizeof(PatUse) == 0 && size / sizeof(PatUse) <= PAT_USE_MAX) {
        s_use_count = (int)(size / sizeof(PatUse));
        return fread(s_uses, 1, size, f) == size;
    }
    return FALSE;
}

static int write_chunk(FILE* f, const char* tag, const void* data, u32 size) {
    return fwrite(tag, 1, 4, f) == 4 && fwrite(&size, 4, 1, f) == 1 && fwrite(data, 1, size, f) == size;
}

int pc_pattern_write_chunks(FILE* f) {
    pc_pattern_reconcile();
    lib_compact();
    return write_chunk(f, "PIDS", &s_ids, sizeof(s_ids)) &&
           write_chunk(f, "PLIB", s_lib, (u32)(s_lib_count * sizeof(PatLibEntry))) &&
           write_chunk(f, "PUSE", s_uses, (u32)(s_use_count * sizeof(PatUse)));
}

void pc_pattern_loaded(void) {
    s_loaded = TRUE;
    pc_pattern_reconcile();
    OSReport("[pattern] %d patterns in the library\n", s_lib_count);
}

/* --- Dev test harness (pc_devtest.c) --- */

/* Put on slot 0 as the inventory does. */
void pc_pattern_dev_wear(void* play) {
    int player_no = Common_Get(player_no);

    if (player_no >= PLAYER_NUM) {
        return;
    }
    Now_Private->cloth.item = RSV_CLOTH;
    Now_Private->cloth.idx = 0x100;
    pc_pattern_bind_cloth(player_no, 0);
    mPlib_request_main_change_cloth_forNPC_type1((GAME*)play, RSV_CLOTH, 0x100, TRUE);
}

/* Swap slot 0 with storage 0, as the storage screen does, then settle IDs as when it closes. */
void pc_pattern_dev_swap(void* play) {
    mCD_keep_original_c* keep = keep_block();
    static mNW_original_design_c tmp;

    if (keep == NULL || Common_Get(player_no) >= PLAYER_NUM) {
        return;
    }
    tmp = Now_Private->my_org[0];
    Now_Private->my_org[0] = keep->original[0][0];
    keep->original[0][0] = tmp;
    pc_pattern_reconcile();
    mPlib_request_main_change_cloth_forNPC_type1((GAME*)play, RSV_CLOTH, Now_Private->cloth.idx, TRUE);
}

/* Paint slot 0 with bold stripes (as if edited), so tests can tell designs apart. */
void pc_pattern_dev_paint(void) {
    mNW_original_design_c* d;
    int i;

    if (Common_Get(player_no) >= PLAYER_NUM) {
        return;
    }
    d = &Now_Private->my_org[0];
    for (i = 0; i < (int)sizeof(d->design.data); i++) {
        /* CI4, 8x8 tiles of 32 bytes: 4 bytes per row, so stripes by tile row. */
        d->design.data[i] = ((i / 4) % 8 < 4) ? 0x11 : 0x99;
    }
    d->palette = 3;
    pc_pattern_note_edit(Common_Get(player_no), 0);
    pc_pattern_reconcile();
}

/* Use slot 0 as the player's house door design, as the gyroid does. */
void pc_pattern_dev_door(void) {
    int p = Common_Get(player_no);
    int h;

    for (h = 0; h < PLAYER_NUM; h++) {
        if (mHS_get_pl_no_detail(h) == p) {
            Save_Get(homes[h]).door_original = 0;
            pc_pattern_bind_use(PC_PAT_USE_DOOR, h, p, 0);
        }
    }
}

/* A design as a 32x32 PPM (CI4 in 8x8 tiles, RGB5A3 palette). */
static void dump_design_ppm(const char* path, const mNW_original_design_c* d) {
    FILE* f = fopen(path, "wb");
    u16* pal = mNW_PaletteIdx2Palette(d->palette);
    int x, y;

    if (f == NULL) {
        return;
    }
    fprintf(f, "P6\n32 32\n255\n");
    for (y = 0; y < 32; y++) {
        for (x = 0; x < 32; x++) {
            int tile = (y / 8) * 4 + (x / 8);
            u8 b = d->design.data[tile * 32 + (y % 8) * 4 + (x % 8) / 2];
            int ci = (x & 1) ? (b & 15) : (b >> 4);
            u16 c = pal[ci];
            u8 rgb[3];

            if (c & 0x8000) {
                rgb[0] = ((c >> 10) & 31) * 255 / 31;
                rgb[1] = ((c >> 5) & 31) * 255 / 31;
                rgb[2] = (c & 31) * 255 / 31;
            } else {
                rgb[0] = ((c >> 8) & 15) * 17;
                rgb[1] = ((c >> 4) & 15) * 17;
                rgb[2] = (c & 15) * 17;
            }
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

void pc_pattern_dev_dump(const char* path) {
    FILE* f = fopen(path, "w");
    int p = Common_Get(player_no);
    int i;

    if (f == NULL || p >= PLAYER_NUM) {
        if (f != NULL) {
            fclose(f);
        }
        return;
    }
    pc_pattern_reconcile();
    fprintf(f, "library %d next %u\n", s_lib_count, (unsigned)s_ids.next_id);
    for (i = 0; i < mPr_ORIGINAL_DESIGN_COUNT; i++) {
        int loc = p * mPr_ORIGINAL_DESIGN_COUNT + i;
        fprintf(f, "slot %d id %u hash %016llx\n", i, (unsigned)s_ids.loc_id[loc],
                (unsigned long long)design_hash(loc_design(loc)));
    }
    if (loc_design(PAT_SLOTS) != NULL) {
        fprintf(f, "storage 0 id %u hash %016llx\n", (unsigned)s_ids.loc_id[PAT_SLOTS],
                (unsigned long long)design_hash(loc_design(PAT_SLOTS)));
    }
    {
        char img[64];

        snprintf(img, sizeof(img), "%s.slot0.ppm", path);
        dump_design_ppm(img, loc_design(p * mPr_ORIGINAL_DESIGN_COUNT));
        if (loc_design(PAT_SLOTS) != NULL) {
            snprintf(img, sizeof(img), "%s.storage0.ppm", path);
            dump_design_ppm(img, loc_design(PAT_SLOTS));
        }
        if (pc_pattern_worn(p, Now_Private->cloth.idx) != NULL) {
            snprintf(img, sizeof(img), "%s.worn.ppm", path);
            dump_design_ppm(img, pc_pattern_worn(p, Now_Private->cloth.idx));
        }
    }
    fprintf(f, "cloth item %04x idx %03x bound id %u for idx %03x\n", Now_Private->cloth.item, Now_Private->cloth.idx,
            (unsigned)s_ids.cloth_id[p], s_ids.cloth_idx[p]);
    if (pc_pattern_worn(p, Now_Private->cloth.idx) != NULL) {
        fprintf(f, "worn hash %016llx\n", (unsigned long long)design_hash(pc_pattern_worn(p, Now_Private->cloth.idx)));
    }
    fclose(f);
}
