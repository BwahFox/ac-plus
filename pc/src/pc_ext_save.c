/* pc_ext_save.c - the ac-plus town save (see pc_ext_save.h).
 *
 * File: "ACPX", u32 version, u16 land id, u16 pad, then chunks of
 * { char tag[4]; u32 size; data }. Little-endian, PC only. Unknown chunks are
 * skipped, missing ones stay empty, so later versions can add chunks freely.
 *
 * Since version 2 the last chunk is CSUM: a CRC-32 of every byte before it. A file whose
 * checksum doesn't match is kept as .acplus.bad and the newest good backup (.acplus.bak1-3,
 * rotated on every save like the .gci's) is loaded instead; with none, the damaged file is
 * still loaded rather than lost. Files from before version 2 have no CSUM and load as they are.
 *
 * Version 3 is the single town file: its first chunk, GCIS, is the town's GameCube save image (the bytes a .gci
 * would hold). Version 2 and older are companion files next to a .gci; they're still read, from a .gci on Card B
 * and when a town is moved into a single file.
 *
 * CFFC: City Folk furniture each resident has had (pc_cf_ftr.c, the catalog).
 * CRMU: City Folk fish and bugs given to the museum, and by whom; CRCL: those each resident has caught (pc_cf_creature.c).
 * LPLY: the resident who saved last (pc_quick_save.c, for quick load with several residents).
 * Chunks: GRNI ground pattern IDs, CONS constellations, ROST the Roost, BSMT basement wallpaper/carpet, MTRE 90,000 Bells money trees
 * (pc_basement.c), the New Leaf style extra rooms (NLRM, pc_nlrooms.c), the dressers' storages (DRSR/DRBD/DRTG, pc_dresser.c) and the pattern chunks of
 * pc_pattern.c. STOR (one shared storage per resident) is only read, from older files, and handed to
 * pc_dresser.c. GRND (ground as player/slot bytes) is only read, from older
 * files, and turned into IDs.
 *
 * MADE says who wrote the file: { u32 save level; char release[28] } (the level is EXT_SAVE_LEVEL, raised
 * whenever a release adds save data older ones don't know). A town from a newer level gets a warning before it
 * loads (pc_ext_save_newer_warning), and chunks this version doesn't know are kept and written back as they were,
 * so playing an older version doesn't throw a newer one's data away. Files without MADE are level 0. */
#include "pc_basement.h"
#include "pc_money_tree.h"
#include "pc_outdoor_ftr.h"
#include "pc_skin.h"
#include "pc_fossil_id.h"
#include "pc_cf_ftr.h"
#include "pc_cf_creature.h"
/* pc_quick_save.c: who played last */
void pc_last_player_reset(void);
int pc_last_player_read_chunk(const char tag[4], void* fp, unsigned int size);
int pc_last_player_write_chunk(void* fp);
#include "pc_season_island.h"
#include "pc_nlrooms.h"
#include "pc_brewster.h"
#include "pc_dresser.h"
#include "pc_ext_save.h"
#include "pc_pattern.h"
#include "pc_constellation.h"
#include "m_common_data.h"
#include "m_name_table.h"

#include <dolphin/os.h>
#include "pc_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#define EXT_MAGIC "ACPX"
#define EXT_VERSION 3 /* 3 = the single town file (GCIS chunk); 2 = companion next to a .gci */
/* Raise when a release adds save data that older ones don't know (a new chunk). 1 = Alpha 6, 2 = the single town file. */
#define EXT_SAVE_LEVEL 2
#define EXT_UNKNOWN_MAX 64

/* Chunks from a newer version, kept to be written back */
static struct {
    char tag[4];
    u32 size;
    u8* data;
} s_unknown[EXT_UNKNOWN_MAX];
static int s_unknown_n;
static u32 s_made_level;
static char s_made_by[28];

void pc_save_rotate_backups(const char* base_path); /* pc_m_card.c */

enum {
    EXT_FILE_NONE,       /* not there */
    EXT_FILE_BAD,        /* not a companion save, cut short, or the checksum doesn't match */
    EXT_FILE_OTHER_TOWN, /* someone else's */
    EXT_FILE_UNCHECKED,  /* from before checksums */
    EXT_FILE_GOOD,
};

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
    for (p = 0; p < s_unknown_n; p++) {
        free(s_unknown[p].data);
    }
    s_unknown_n = 0;
    s_made_level = 0;
    s_made_by[0] = '\0';
    s_legacy_ground_read = FALSE;
    pc_pattern_reset();
    pc_const_reset();
    pc_brewster_reset();
    pc_basement_reset();
    pc_money_tree_reset();
    pc_oftr_reset();
    pc_skin_reset();
    pc_fossil_id_reset();
    pc_last_player_reset();
    pc_cf_ftr_reset();
    pc_cf_museum_reset();
    pc_season_island_reset();
    pc_nlr_reset();
    pc_dresser_reset();
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

/* New town since this data was loaded (town reset): start over. The skin tones stay: the new town's first player
 * picked theirs on the train, before the town had its id. */
static void ext_check_town(void) {
    if (s_ext.land_id != current_land_id()) {
        int tone[PLAYER_NUM];
        int p;

        for (p = 0; p < PLAYER_NUM; p++) {
            tone[p] = pc_skin_tone_of(p);
        }
        ext_reset(current_land_id());
        for (p = 0; p < PLAYER_NUM; p++) {
            pc_skin_set_tone(p, tone[p]);
        }
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

static u32 ext_crc32(u32 crc, const u8* p, size_t n) {
    int k;

    crc = ~crc;
    while (n-- > 0) {
        crc ^= *p++;
        for (k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static u32 ext_get32(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* What state the file at path is in: the header, the chunk sizes and the checksum */
static int ext_check_file(const char* path, int want_land) {
    FILE* f = fopen(path, "rb");
    u8* buf;
    long size;
    long at;
    int res = EXT_FILE_UNCHECKED;

    if (f == NULL) {
        return EXT_FILE_NONE;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 12 || fseek(f, 0, SEEK_SET) != 0 ||
        (buf = (u8*)malloc((size_t)size)) == NULL) {
        fclose(f);
        return EXT_FILE_BAD;
    }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size || memcmp(buf, EXT_MAGIC, 4) != 0) {
        res = EXT_FILE_BAD;
    } else if (want_land >= 0 && (u16)(buf[8] | (buf[9] << 8)) != (u16)want_land) {
        res = EXT_FILE_OTHER_TOWN;
    } else {
        for (at = 12; at < size; at += 8 + (long)ext_get32(&buf[at + 4])) {
            if (size - at < 8 || (long)ext_get32(&buf[at + 4]) > size - at - 8) {
                res = EXT_FILE_BAD; /* cut short */
                break;
            }
            if (memcmp(&buf[at], "CSUM", 4) == 0) {
                res = ext_get32(&buf[at + 4]) == 4 && at + 12 == size &&
                              ext_get32(&buf[at + 8]) == ext_crc32(0, buf, (size_t)at)
                          ? EXT_FILE_GOOD
                          : EXT_FILE_BAD;
                break;
            }
        }
        if (res == EXT_FILE_UNCHECKED && ext_get32(&buf[4]) >= 2) {
            res = EXT_FILE_BAD; /* written with a checksum, which is gone */
        }
    }
    free(buf);
    fclose(f);
    return res;
}

static void ext_read_file(const char* path) {
    FILE* f = fopen(path, "rb");

    if (f == NULL || fseek(f, 12, SEEK_SET) != 0) {
        if (f != NULL) {
            fclose(f);
        }
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
            pc_brewster_read_chunk(tag, f, size) || pc_basement_read_chunk(tag, f, size) || pc_money_tree_read_chunk(tag, f, size) || pc_oftr_read_chunk(tag, f, size) || pc_skin_read_chunk(tag, f, size) || pc_fossil_id_read_chunk(tag, f, size) || pc_last_player_read_chunk(tag, f, size) || pc_cf_ftr_read_chunk(tag, f, size) || pc_season_island_read_chunk(tag, f, size) || pc_nlr_read_chunk(tag, f, size) ||
            pc_dresser_read_chunk(tag, f, size) || pc_cf_museum_read_chunk(tag, f, size)) {
            continue;
        }
        if (memcmp(tag, "MADE", 4) == 0 && size == 4 + sizeof(s_made_by)) {
            if (fread(&s_made_level, 4, 1, f) != 1 || fread(s_made_by, 1, sizeof(s_made_by), f) != sizeof(s_made_by)) {
                break;
            }
            s_made_by[sizeof(s_made_by) - 1] = '\0';
            continue;
        }
        if (memcmp(tag, "CSUM", 4) != 0 && memcmp(tag, "GRNI", 4) != 0 && memcmp(tag, "GRND", 4) != 0 &&
            memcmp(tag, "STOR", 4) != 0 && memcmp(tag, "MADE", 4) != 0 && memcmp(tag, "GCIS", 4) != 0) {
            /* a newer version's: kept as it is */
            u8* data = size <= 64u * 1024 * 1024 ? (u8*)malloc(size ? size : 1) : NULL;

            if (data == NULL || s_unknown_n == EXT_UNKNOWN_MAX || fread(data, 1, size, f) != size) {
                free(data);
                OSReport("[acplus] couldn't keep the unknown chunk %.4s\n", tag);
                break;
            }
            memcpy(s_unknown[s_unknown_n].tag, tag, 4);
            s_unknown[s_unknown_n].size = size;
            s_unknown[s_unknown_n].data = data;
            s_unknown_n++;
            OSReport("[acplus] kept a newer version's chunk %.4s (%u bytes)\n", tag, (unsigned)size);
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
}

/* Keep a damaged file for a look later, as <path>.bad */
static void ext_keep_bad(const char* path) {
    char bad[340];
    FILE* in;
    FILE* out;
    char buf[4096];
    size_t n;

    snprintf(bad, sizeof(bad), "%s.bad", path);
    if ((in = fopen(path, "rb")) == NULL) {
        return;
    }
    if ((out = fopen(bad, "wb")) != NULL) {
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
            fwrite(buf, 1, n, out);
        }
        fclose(out);
    }
    fclose(in);
}

/* Says msg, and lets the player quit (nothing saved) or play on */
static void pc_ext_save_newer_ask(const char* msg) {
    extern SDL_Window* g_pc_window;
    extern const char* pc_updater_version_label(void);
    static const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit" },
        { 0, 1, "Play anyway" },
    };
    const char* answer = getenv("PC_SAVE_NEWER_ANSWER"); /* dev: tests can't click */
    SDL_MessageBoxData box;
    int hit = 0;

    OSReport("[acplus] %s\n", msg);
    if (answer != NULL) {
        hit = strcmp(answer, "play") == 0;
    } else {
        memset(&box, 0, sizeof(box));
        box.flags = SDL_MESSAGEBOX_WARNING;
        box.window = g_pc_window;
        box.title = "ac-plus - save from a newer version";
        box.message = msg;
        box.numbuttons = 2;
        box.buttons = buttons;
        if (SDL_ShowMessageBox(&box, &hit) != 0) {
            hit = 0;
        }
    }
    if (hit != 1) {
        OSReport("[acplus] quitting without saving\n");
        fflush(stdout);
        SDL_Quit();
        exit(0);
    }
}

/* The town was last saved by a newer ac-plus (its MADE level is higher than ours) */
static void pc_ext_save_newer_warning(const char* made_by) {
    extern const char* pc_updater_version_label(void);
    char msg[640];

    snprintf(msg, sizeof(msg),
             "This town was last saved by a newer version of ac-plus (%s). You're running %s.\n\n"
             "This version doesn't know everything the newer one added. What it doesn't know is kept, but playing "
             "here can still leave things out of step.\n\n"
             "It's best to quit and update first. Quitting now saves nothing.",
             made_by[0] != '\0' ? made_by : "unknown", pc_updater_version_label());
    pc_ext_save_newer_ask(msg);
}

/* At startup, before any save is touched: a save file in a newer format than this version reads (an "ACPX" header
 * with a higher version, as the planned single-file .acplus will have) means a newer ac-plus has been here. This
 * version wouldn't see that town and could start a new one over it. */
void pc_ext_save_check_newer_format(void) {
    extern const char* pc_updater_version_label(void);
    static const char* dirs[] = { "save/card_a", "save/card_b" };
    size_t d;

    for (d = 0; d < sizeof(dirs) / sizeof(dirs[0]); d++) {
        DIR* dir = opendir(dirs[d]);
        struct dirent* e;

        if (dir == NULL) {
            continue;
        }
        while ((e = readdir(dir)) != NULL) {
            size_t n = strlen(e->d_name);
            char path[512];
            u8 head[8];
            FILE* f;
            int newer = FALSE;

            if (n < 7 || strcmp(e->d_name + n - 7, ".acplus") != 0) {
                continue;
            }
            snprintf(path, sizeof(path), "%s/%s", dirs[d], e->d_name);
            if ((f = fopen(path, "rb")) == NULL) {
                continue;
            }
            newer = fread(head, 1, 8, f) == 8 && memcmp(head, EXT_MAGIC, 4) == 0 && ext_get32(&head[4]) > EXT_VERSION;
            fclose(f);
            if (newer) {
                char msg[1024];

                closedir(dir);
                snprintf(msg, sizeof(msg),
                         "Your save (%s) was written by a newer version of ac-plus, in a format this version (%s) "
                         "can't read.\n\n"
                         "Playing here, it wouldn't find your town and could start a new one.\n\n"
                         "Please quit and update ac-plus first. Quitting now changes nothing.",
                         path, pc_updater_version_label());
                pc_ext_save_newer_ask(msg);
                return;
            }
        }
        closedir(dir);
    }
}

/* Is the file at path a single town file (ACPX version 3 or later)? */
int pc_ext_town_is_merged(const char* path) {
    FILE* f = fopen(path, "rb");
    u8 head[8];
    int yes;

    if (f == NULL) {
        return FALSE;
    }
    yes = fread(head, 1, 8, f) == 8 && memcmp(head, EXT_MAGIC, 4) == 0 && ext_get32(&head[4]) >= 3;
    fclose(f);
    return yes;
}

/* Everything after the chunks are read: what the data needs once it's all there */
static void ext_after_read(const char* from) {
    int p;

    pc_pattern_loaded();
    for (p = 0; p < PC_EXT_PLAYERS; p++) {
        pc_dresser_loaded(s_ext.storage[p].items, 1);
    }
    if (s_legacy_ground_read) {
        migrate_legacy_ground();
    }
    rebuild_ground_used();
    OSReport("[acplus] Loaded %s (save level %u, %s)\n", from, (unsigned)s_made_level,
             s_made_by[0] != '\0' ? s_made_by : "an older ac-plus");
    if (s_made_level > EXT_SAVE_LEVEL) {
        pc_ext_save_newer_warning(s_made_by);
    }
}

/* path: a town file (its chunks, read after pc_ext_town_read_image picked it), or a .gci (its companion
 * <gci>.acplus, if there is one, as before the single file) */
void pc_ext_save_load(const char* path_in) {
    char path[320];
    char bak[330];
    const char* from;
    int state;
    int b;

    ext_reset(current_land_id());
    if (pc_ext_town_is_merged(path_in)) {
        ext_read_file(path_in);
        ext_after_read(path_in);
        return;
    }
    pc_ext_save_path(path, sizeof(path), path_in);
    from = path;
    state = ext_check_file(path, current_land_id());
    if (state == EXT_FILE_NONE) {
        OSReport("[acplus] No companion save yet (%s)\n", path);
        pc_pattern_loaded();
        return;
    }
    if (state == EXT_FILE_OTHER_TOWN) {
        OSReport("[acplus] %s belongs to another town, ignoring it\n", path);
        pc_pattern_loaded();
        return;
    }
    if (state == EXT_FILE_BAD) {
        OSReport("[acplus] %s is damaged (kept as %s.bad)\n", path, path);
        ext_keep_bad(path);
        for (b = 1; b <= 3; b++) {
            snprintf(bak, sizeof(bak), "%s.bak%d", path, b);
            state = ext_check_file(bak, current_land_id());
            if (state == EXT_FILE_GOOD || state == EXT_FILE_UNCHECKED) {
                OSReport("[acplus] Loading the backup %s instead\n", bak);
                from = bak;
                break;
            }
        }
        if (from == path) {
            OSReport("[acplus] No good backup: loading what can be read of it\n");
        }
    }

    ext_read_file(from);
    ext_after_read(from);
}

/* The GCIS chunk of a town file: the town's GameCube save image, 0x40 + 0x72000 bytes */
static int ext_read_gcis(const char* path, u8* img, u32 img_size) {
    FILE* f = fopen(path, "rb");
    int ok = FALSE;

    if (f == NULL || fseek(f, 12, SEEK_SET) != 0) {
        if (f != NULL) {
            fclose(f);
        }
        return FALSE;
    }
    for (;;) {
        char tag[4];
        u32 size;

        if (fread(tag, 1, 4, f) != 4 || fread(&size, 4, 1, f) != 1) {
            break;
        }
        if (memcmp(tag, "GCIS", 4) == 0) {
            ok = size == img_size && fread(img, 1, size, f) == size;
            break;
        }
        if (fseek(f, (long)size, SEEK_CUR) != 0) {
            break;
        }
    }
    fclose(f);
    return ok;
}

int pc_ext_town_read_image(const char* path, u8* img, u32 img_size, char* used, int used_size) {
    char bak[340];
    int state = ext_check_file(path, -1);
    int b;

    if (state == EXT_FILE_NONE) {
        return FALSE;
    }
    if ((state == EXT_FILE_GOOD || state == EXT_FILE_UNCHECKED) && ext_read_gcis(path, img, img_size)) {
        snprintf(used, used_size, "%s", path);
        return TRUE;
    }
    OSReport("[acplus] %s is damaged (kept as %s.bad)\n", path, path);
    ext_keep_bad(path);
    for (b = 1; b <= 3; b++) {
        snprintf(bak, sizeof(bak), "%s.bak%d", path, b);
        state = ext_check_file(bak, -1);
        if ((state == EXT_FILE_GOOD || state == EXT_FILE_UNCHECKED) && ext_read_gcis(bak, img, img_size)) {
            OSReport("[acplus] Loading the backup %s instead\n", bak);
            snprintf(used, used_size, "%s", bak);
            return TRUE;
        }
    }
    OSReport("[acplus] No readable backup of %s\n", path);
    return FALSE;
}

static int write_chunk(FILE* f, const char* tag, const void* data, u32 size) {
    return fwrite(tag, 1, 4, f) == 4 && fwrite(&size, 4, 1, f) == 1 && fwrite(data, 1, size, f) == size;
}

/* The CSUM chunk: a CRC-32 of the whole file so far (f open for update, at its end) */
static int write_checksum(FILE* f) {
    u8 buf[4096];
    u32 crc = 0;
    u32 size = 4;
    size_t n;

    if (fflush(f) != 0 || fseek(f, 0, SEEK_SET) != 0) {
        return FALSE;
    }
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        crc = ext_crc32(crc, buf, n);
    }
    if (ferror(f) || fseek(f, 0, SEEK_END) != 0) {
        return FALSE;
    }
    return fwrite("CSUM", 1, 4, f) == 4 && fwrite(&size, 4, 1, f) == 1 && fwrite(&crc, 4, 1, f) == 1;
}

/* MADE: this version's save level and release; a newer level's, if the town came from one, so it still
 * warns there (its chunks are kept) */
static int write_made(FILE* f) {
    extern const char* pc_updater_version_label(void);
    u8 buf[4 + sizeof(s_made_by)];
    u32 level = s_made_level > EXT_SAVE_LEVEL ? s_made_level : EXT_SAVE_LEVEL;

    memset(buf, 0, sizeof(buf));
    memcpy(buf, &level, 4);
    snprintf((char*)buf + 4, sizeof(s_made_by), "%s",
             s_made_level > EXT_SAVE_LEVEL ? s_made_by : pc_updater_version_label());
    return write_chunk(f, "MADE", buf, sizeof(buf));
}

static int write_unknown(FILE* f) {
    int i;

    for (i = 0; i < s_unknown_n; i++) {
        if (!write_chunk(f, s_unknown[i].tag, s_unknown[i].data, s_unknown[i].size)) {
            return FALSE;
        }
    }
    return TRUE;
}

/* Writes path.tmp with write_body, then moves it in place, the last three going to .bak1-3 as before
 * (this also clears the way: Windows rename() won't replace a file) */
static int ext_write_atomic(const char* path, int (*write_body)(FILE* f, void* arg), void* arg) {
    char tmp[340];
    FILE* f;
    int ok;

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = fopen(tmp, "w+b");
    if (f == NULL) {
        OSReport("[acplus] Could not write %s\n", tmp);
        return FALSE;
    }
    ok = write_body(f, arg) && write_checksum(f);
    if (fclose(f) != 0) {
        ok = FALSE;
    }
    if (!ok) {
        OSReport("[acplus] Writing %s failed\n", tmp);
        remove(tmp);
        return FALSE;
    }
    pc_save_rotate_backups(path);
    if (rename(tmp, path) != 0) {
        char bak1[350];

        OSReport("[acplus] Could not replace %s\n", path);
        snprintf(bak1, sizeof(bak1), "%s.bak1", path);
        rename(bak1, path);
        remove(tmp);
        return FALSE;
    }
    return TRUE;
}

static int write_header(FILE* f, u16 land_id) {
    u32 version = EXT_VERSION;
    u16 pad = 0;

    return fwrite(EXT_MAGIC, 1, 4, f) == 4 && fwrite(&version, 4, 1, f) == 1 && fwrite(&land_id, 2, 1, f) == 1 &&
           fwrite(&pad, 2, 1, f) == 1;
}

typedef struct {
    const u8* img;
    u32 img_size;
} TownWrite;

static int write_town_body(FILE* f, void* arg) {
    TownWrite* w = (TownWrite*)arg;

    return write_header(f, s_ext.land_id) && write_chunk(f, "GCIS", w->img, w->img_size) &&
           write_chunk(f, "GRNI", s_ext.ground, sizeof(s_ext.ground)) && pc_dresser_write_chunks(f) &&
           pc_pattern_write_chunks(f) && pc_const_write_chunk(f) && pc_brewster_write_chunk(f) &&
           pc_basement_write_chunk(f) && pc_money_tree_write_chunk(f) && pc_oftr_write_chunk(f) &&
           pc_skin_write_chunk(f) && pc_fossil_id_write_chunk(f) && pc_last_player_write_chunk(f) && pc_cf_ftr_write_chunk(f) && pc_cf_museum_write_chunk(f) &&
           pc_season_island_write_chunk(f) && pc_nlr_write_chunk(f) && write_made(f) && write_unknown(f);
}

int pc_ext_town_write(const char* path, const u8* img, u32 img_size) {
    TownWrite w;

    ext_check_town();
    w.img = img;
    w.img_size = img_size;
    if (!ext_write_atomic(path, write_town_body, &w)) {
        return FALSE;
    }
    OSReport("[acplus] Town saved to %s\n", path);
    return TRUE;
}

static u8* ext_read_whole(const char* path, long* size_out) {
    FILE* f = fopen(path, "rb");
    long size;
    u8* buf = NULL;

    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0 &&
        (buf = (u8*)malloc(size > 0 ? (size_t)size : 1)) != NULL && fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf != NULL) {
        *size_out = size;
    }
    return buf;
}

typedef struct {
    const u8* gci;
    long gci_size;
    const u8* ext; /* the companion's whole file, or NULL */
    long ext_size;
    u16 land_id;
} ConvertWrite;

static int write_convert_body(FILE* f, void* arg) {
    ConvertWrite* c = (ConvertWrite*)arg;
    long at;

    if (!write_header(f, c->land_id) || !write_chunk(f, "GCIS", c->gci, (u32)c->gci_size)) {
        return FALSE;
    }
    /* the companion's chunks as they are; its checksum is made again for the whole file */
    for (at = 12; c->ext != NULL && c->ext_size - at >= 8; at += 8 + (long)ext_get32(&c->ext[at + 4])) {
        u32 size = ext_get32(&c->ext[at + 4]);

        if ((long)size > c->ext_size - at - 8) {
            break;
        }
        if (memcmp(&c->ext[at], "CSUM", 4) == 0 || memcmp(&c->ext[at], "GCIS", 4) == 0) {
            continue;
        }
        if (fwrite(&c->ext[at], 1, 8 + size, f) != 8 + size) {
            return FALSE;
        }
    }
    return TRUE;
}

int pc_ext_town_convert(const char* gci_path, const char* ext_of, const char* town_path, u16 land_id) {
    char ext_path[340];
    char bak[350];
    const char* ext_from = ext_path;
    ConvertWrite c;
    int state;
    int b;
    int ok;

    memset(&c, 0, sizeof(c));
    c.land_id = land_id;
    c.gci = ext_read_whole(gci_path, &c.gci_size);
    if (c.gci == NULL) {
        return FALSE;
    }
    pc_ext_save_path(ext_path, sizeof(ext_path), ext_of);
    state = ext_check_file(ext_path, land_id);
    if (state == EXT_FILE_BAD) {
        ext_from = NULL;
        for (b = 1; b <= 3 && ext_from == NULL; b++) {
            snprintf(bak, sizeof(bak), "%s.bak%d", ext_path, b);
            state = ext_check_file(bak, land_id);
            if (state == EXT_FILE_GOOD || state == EXT_FILE_UNCHECKED) {
                ext_from = bak;
            }
        }
        if (ext_from == NULL) {
            ext_from = ext_path; /* what can be read of it, as loading it would */
            state = EXT_FILE_UNCHECKED;
        }
    }
    if (state == EXT_FILE_GOOD || state == EXT_FILE_UNCHECKED) {
        c.ext = ext_read_whole(ext_from, &c.ext_size);
    }
    ok = ext_write_atomic(town_path, write_convert_body, &c);
    OSReport("[acplus] %s %s%s%s into %s\n", ok ? "Moved" : "Could not move", gci_path, c.ext != NULL ? " and " : "",
             c.ext != NULL ? ext_from : "", town_path);
    free((void*)c.gci);
    free((void*)c.ext);
    return ok;
}

int pc_ext_town_export_gci(const char* town_path, const char* gci_out, u32 img_size) {
    u8* img = (u8*)malloc(img_size);
    char used[340];
    FILE* f;
    int ok = FALSE;

    if (img == NULL) {
        return FALSE;
    }
    if (pc_ext_town_read_image(town_path, img, img_size, used, sizeof(used)) && (f = fopen(gci_out, "wb")) != NULL) {
        ok = fwrite(img, 1, img_size, f) == img_size;
        ok = fclose(f) == 0 && ok;
    }
    free(img);
    OSReport("[acplus] %s %s as %s\n", ok ? "Exported" : "Could not export", town_path, gci_out);
    return ok;
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
