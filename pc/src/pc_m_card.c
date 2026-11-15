/* pc_m_card.c - memory card manager: GCI save/load, village generation, ARAM data blocks
 *
 * Card A (save/card_a/) = player's home town
 * Card B (save/card_b/) = towns for visiting (drop any AC GCI files there; Porter lists them)
 */
#include "m_card.h"
#include "m_start_data_init.h"
#include "m_common_data.h"
#include "m_flashrom.h"
#include "m_field_make.h"
#include "m_msg.h"
#include "m_npc.h"
#include "m_quest.h"
#include "m_island.h"
#include "m_font.h"
#include "m_vibctl.h"
#include "m_bg_item.h"
#include "m_land.h"
#include "m_private.h"
#include "m_event.h"
#include "m_time.h"
#include "m_scene.h"
#include "m_name_table.h"
#include "sys_math3d.h"
#include "sys_math.h"
#include "zurumode.h"
#include "pc_gci.h"
#include "pc_platform.h"
#include "pc_settings.h"
#include "pc_town_select.h"
#include "m_cockroach.h"
#include "m_all_grow_ovl.h"
#include "m_home.h"
#include "lb_rtc.h"
#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "pc_dirent.h"
#include "pc_ext_save.h"
#include "m_play.h"
#ifdef _WIN32
#include <direct.h>
#endif
#include <dolphin/os.h>

/* --- Save paths below the configured --user-dir --- */
#define PC_CWD_CARD_A_GCI_PATH "save/card_a/" PC_GCI_FILENAME
#define PC_CWD_CARD_A_PREFIXED_GCI_PATH "save/card_a/8P-GAFE-" PC_GCI_FILENAME
#define PC_CWD_FLAT_GCI_PATH "save/" PC_GCI_FILENAME

static char l_pc_save_dir[PC_GCI_PATH_MAX];
static char l_pc_card_a_dir[PC_GCI_PATH_MAX];
static char l_pc_card_b_dir[PC_GCI_PATH_MAX];
static char l_pc_gci_path[PC_GCI_PATH_MAX];
static char l_pc_gci_path_legacy[PC_GCI_PATH_MAX];
/* ac-plus: the home town is one file with the GameCube image and the ac-plus data (pc_ext_save.h). A .gci found in
 * card_a without it (an older ac-plus's save, or one from Dolphin/a GameCube) is moved into it on first load. */
static char l_pc_town_path[PC_GCI_PATH_MAX];
static char l_pc_passport_path[PC_GCI_PATH_MAX];
static char l_pc_passport_tmp_path[PC_GCI_PATH_MAX];
#define PC_TOWN_PATH l_pc_town_path
#define PC_PASSPORT_PATH l_pc_passport_path
#define PC_PASSPORT_TMP_PATH l_pc_passport_tmp_path
#define PC_SAVE_MAX_BACKUPS 3

#define PC_SAVE_DIR l_pc_save_dir
#define PC_CARD_A_DIR l_pc_card_a_dir
#define PC_CARD_B_DIR l_pc_card_b_dir
#define PC_GCI_PATH l_pc_gci_path
#define PC_GCI_PATH_LEGACY l_pc_gci_path_legacy

typedef enum {
    PC_START_IDLE,
    PC_START_FAILED,
    PC_START_WRITE_PENDING,
    PC_START_COMPLETE
} pc_start_phase;

static int pc_save_loaded = 0;
static int pc_save_ready = 0;
static pc_save_result pc_save_load_status = PC_SAVE_MISSING;
static pc_start_phase pc_start_state = PC_START_IDLE;

/* --- Travel state --- */
static Save l_keepSave;                        /* Other town's save data (for Card B visit) */
static int l_keepSave_set = FALSE;
static char l_keepSave_path[PC_GCI_PATH_MAX]; /* the file l_keepSave was read from */
static mCD_keep_mail_c l_keepMail;
static mCD_keep_original_c l_keepOriginal;
static mCD_keep_diary_c l_keepDiary;

/* Passport: the traveling player's private data + departing animal */
static union {
    mCD_foreigner_c file;
    u8 sector_align[mCD_ALIGN_SECTORSIZE(sizeof(mCD_foreigner_c))];
} l_mcd_foreigner_file;

static int l_mcd_keep_startCond = 0;
static char l_card_b_gci_path[PC_GCI_PATH_MAX] = {0};

typedef struct {
    char path[PC_GCI_PATH_MAX];
    pc_card_town_entry entry;
} pc_card_town;

_Static_assert(PC_CARD_TOWN_NAME_SIZE == LAND_NAME_SIZE, "town name size mismatch");

static pc_card_town l_card_b_towns[PC_CARD_TOWN_MAX];
static int l_card_b_town_count = 0;
static int l_card_b_usable_count = 0;
static int l_card_b_town_selected = FALSE;

extern void mTM_rtcTime_limit_check(void);
extern void mEv_ClearEventInfo(void);
extern void mEv_toland_clear_common(void);
extern void mNpc_ClearInAnimal(void);
extern void mNpc_FirstClearGoodbyMail(void);
extern void mQst_ClearGrabItemInfo(void);
extern void mISL_ClearKeepIsland(void);
extern void mNpc_ClearCacheName(void);
extern void mTM_clear_renew_is(void);
extern void lbRTC_GetTime(lbRTC_time_c* time);

/* --- ARAM data blocks (mail/diary/original designs) — malloc'd instead of ARAM DMA --- */

static u32 l_aram_alloc_size_table[mCD_ARAM_DATA_NUM] = {
    ALIGN_NEXT(sizeof(mCD_keep_original_c), 32),
    ALIGN_NEXT(sizeof(mCD_keep_mail_c), 32),
    ALIGN_NEXT(sizeof(mCD_keep_diary_c), 32),
};

static void* l_aram_block_p_table[mCD_ARAM_DATA_NUM];

static void pc_init_diary_entries(void* block) {
    mCD_keep_diary_c* diary = (mCD_keep_diary_c*)block;
    int p, m;
    for (p = 0; p < mCD_KEEP_DIARY_COUNT; p++) {
        for (m = 0; m < mCD_KEEP_DIARY_ENTRY_COUNT; m++) {
            mDi_init_diary(&diary->entries[p][m]);
        }
    }
}

static void pc_init_mail_entries(void* block) {
    mCD_keep_mail_c* keep_mail = (mCD_keep_mail_c*)block;
    Mail_c* mail = (Mail_c*)keep_mail->mail;
    int i, j;
    for (i = 0; i < mCD_KEEP_MAIL_PAGE_COUNT; i++) {
        mem_clear(keep_mail->folder_names[i], sizeof(keep_mail->folder_names[i]), CHAR_SPACE);
        for (j = 0; j < mCD_KEEP_MAIL_COUNT; j++) {
            mMl_clear_mail(mail);
            mail++;
        }
    }
}

static void pc_init_original_entries(void* block) {
    mCD_keep_original_c* original = (mCD_keep_original_c*)block;
    int page, slot;
    for (page = 0; page < mCD_KEEP_ORIGINAL_PAGE_COUNT; page++) {
        mem_clear(original->folder_names[page], sizeof(original->folder_names[page]), CHAR_SPACE);
        for (slot = 0; slot < mCD_KEEP_ORIGINAL_COUNT; slot++) {
            mNW_InitOriginalData(&original->original[page][slot]);
        }
    }
}

/* Old PC saves can contain an entirely zeroed design block. Preserve any nonzero data. */
static void pc_fix_zeroed_original_entries(mCD_keep_original_c* original, u16 land_id) {
    const u8* bytes = (const u8*)original;
    u32 i;
    for (i = 0; i < sizeof(*original); i++) {
        if (bytes[i] != 0) return;
    }

    pc_init_original_entries(original);
    original->landid = land_id;
    original->checksum = mFRm_GetFlatCheckSum((u16*)original, sizeof(*original), original->checksum);
    OSReport("[PC] GCI: initialized all-zero Mabel design storage to GC blanks in memory\n");
}

static void pc_init_aram_block(int index) {
    static void (*const initialize[mCD_ARAM_DATA_NUM])(void*) = {
        pc_init_original_entries,
        pc_init_mail_entries,
        pc_init_diary_entries,
    };
    void* block = l_aram_block_p_table[index];
    if (block == NULL) return;
    memset(block, 0, l_aram_alloc_size_table[index]);
    initialize[index](block);
}

void mCD_save_data_aram_malloc(void) {
    int i;
    for (i = 0; i < mCD_ARAM_DATA_NUM; i++) {
        if (l_aram_block_p_table[i] == NULL) {
            l_aram_block_p_table[i] = malloc(l_aram_alloc_size_table[i]);
            pc_init_aram_block(i);
        }
    }
}

/* The ARAM block itself (pc_pattern.c reads pattern storage), NULL if not allocated. */
void* pc_card_aram_block(int idx) {
    return (idx >= 0 && idx < mCD_ARAM_DATA_NUM) ? l_aram_block_p_table[idx] : NULL;
}

int mCD_save_data_aram_to_main(void* dst, u32 size, u32 idx) {
    void* block;
    if (idx >= mCD_ARAM_DATA_NUM) idx = 0;
    block = l_aram_block_p_table[idx];
    if (block != NULL) {
        u32 copy_size = size < l_aram_alloc_size_table[idx] ? size : l_aram_alloc_size_table[idx];
        memcpy(dst, block, copy_size);
        return TRUE;
    }
    return FALSE;
}

int mCD_save_data_main_to_aram(void* src, u32 size, u32 idx) {
    void* block;
    if (idx >= mCD_ARAM_DATA_NUM) idx = 0;
    block = l_aram_block_p_table[idx];
    if (block != NULL) {
        u32 copy_size = size < l_aram_alloc_size_table[idx] ? size : l_aram_alloc_size_table[idx];
        memcpy(block, src, copy_size);
        return TRUE;
    }
    return FALSE;
}

void mCD_set_aram_save_data(void) {
    int i;
    for (i = 0; i < mCD_ARAM_DATA_NUM; i++) {
        pc_init_aram_block(i);
    }
}

/* --- Save paths and game state --- */

static int pc_save_paths_init(void) {
    static int initialized = FALSE;
    if (initialized) return TRUE;

    if (!pc_platform_user_path("save", PC_SAVE_DIR, sizeof(l_pc_save_dir)) ||
        !pc_platform_user_path("save/card_a", PC_CARD_A_DIR, sizeof(l_pc_card_a_dir)) ||
        !pc_platform_user_path("save/card_b", PC_CARD_B_DIR, sizeof(l_pc_card_b_dir)) ||
        !pc_platform_user_path("save/card_a/" PC_GCI_FILENAME, PC_GCI_PATH, sizeof(l_pc_gci_path)) ||
        !pc_platform_user_path("save/" PC_GCI_FILENAME, PC_GCI_PATH_LEGACY,
                               sizeof(l_pc_gci_path_legacy)) ||
        !pc_platform_user_path("save/card_a/town.acplus", l_pc_town_path, sizeof(l_pc_town_path)) ||
        !pc_platform_user_path("save/online_passport.bin", l_pc_passport_path, sizeof(l_pc_passport_path)) ||
        !pc_platform_user_path("save/online_passport.bin.tmp", l_pc_passport_tmp_path,
                               sizeof(l_pc_passport_tmp_path))) {
        OSReport("[PC] Save path exceeds %d bytes\n", PC_GCI_PATH_MAX);
        return FALSE;
    }
    initialized = TRUE;
    return TRUE;
}

static void pc_ensure_save_dirs(void) {
    if (!pc_save_paths_init()) return;
#ifdef _WIN32
    _mkdir(PC_SAVE_DIR);
    _mkdir(PC_CARD_A_DIR);
    _mkdir(PC_CARD_B_DIR);
#else
    mkdir(PC_SAVE_DIR, 0755);
    mkdir(PC_CARD_A_DIR, 0755);
    mkdir(PC_CARD_B_DIR, 0755);
#endif
}

static int pc_save_write_gci_to(const char* gci_path);
void pc_mcd_passport_write(void);
static void pc_mcd_passport_done(void);
static void pc_mcd_passport_restore(void);
int pc_mcd_online_visiting(void);
void pc_autosave_reset_timer(void);        /* pc_quick_save.c */
void pc_cloud_save_push(const char* path); /* pc_cloud_save.c */
void pc_cloud_save_pull(const char* path);
void pc_sync_after_save(void);  /* pc_sync.c */
void pc_sync_before_load(void);

/* rotate backups: .bak3 -> delete, .bak2 -> .bak3, .bak1 -> .bak2, current -> .bak1 (town files, pc_ext_save.c) */
void pc_save_rotate_backups(const char* base_path) {
    char older[PC_GCI_PATH_MAX + 8], newer[PC_GCI_PATH_MAX + 8];
    int i;
    struct stat st;

    snprintf(older, sizeof(older), "%s.bak%d", base_path, PC_SAVE_MAX_BACKUPS);
    remove(older);
    /* Windows rename() fails if dest exists, so remove first */
    for (i = PC_SAVE_MAX_BACKUPS; i > 1; i--) {
        snprintf(older, sizeof(older), "%s.bak%d", base_path, i - 1);
        snprintf(newer, sizeof(newer), "%s.bak%d", base_path, i);
        remove(newer);
        rename(older, newer);
    }
    if (stat(base_path, &st) == 0) {
        snprintf(newer, sizeof(newer), "%s.bak1", base_path);
        remove(newer);
        rename(base_path, newer);
    }
}

static u16 pc_get_land_copy_protect(void) {
    u16 code = (u16)RANDOM(0xFFF0);
    return (u16)(code + 1);
}

static int pc_check_reset_code(Private_c* priv) {
    if (priv->state_flags & mPr_FLAG_BIRTHDAY_ACTIVE) {
        priv->reset_code = 0;
    }
    return priv->reset_code == 0;
}

/* mCD_SetResetCode: arm a nonzero code; still set at next load = reset */
static void pc_set_reset_code(Private_c* priv) {
    priv->reset_code = (u32)RANDOM_F(USHT_MAX_S);
    priv->reset_code++;
}

/* GC save_mode: 0 is a full save; door saves leave the reset code armed. */
static void pc_save_pre_write_side_effects(int save_mode) {
    Private_c* priv = Now_Private;
    u16 copy_protect;
    int i;

    mCkRh_SavePlayTime(Common_Get(player_no));

    if (priv != NULL) {
        if (save_mode == 0) {
            priv->reset_code = 0;

            for (i = 0; i < mPr_POCKETS_SLOT_COUNT; i++) {
                if (ITEM_IS_WISP(priv->inventory.pockets[i])) {
                    mPr_SetPossessionItem(priv, i, EMPTY_NO, mPr_ITEM_COND_NORMAL);
                }
            }
        } else if (pc_check_reset_code(priv)) {
            pc_set_reset_code(priv);
        }
    }

    if (save_mode == 0) {
        mAGrw_ClearMoneyStoneShineGround();
    }

    copy_protect = pc_get_land_copy_protect();
    Common_Set(copy_protect, copy_protect);
    Save_Set(copy_protect, copy_protect);
    Save_Set(travel_hard_time, lbRTC_HardTime());
}

static int pc_save_write_gci(void) {
    if (!pc_save_paths_init()) return FALSE;
    return pc_save_write_gci_to(PC_TOWN_PATH);
}

static int pc_save_write_gci_to(const char* gci_path) {
    int is_town;

    if (!pc_save_ready) return TRUE;
    if (pc_mcd_online_visiting()) {
        /* the town in memory is someone else's (an online visit): nothing of it goes over a save on disk */
        OSReport("[save] not saving: visiting a town online (the passport instead)\n");
        pc_mcd_passport_write();
        return TRUE;
    }
    if (pc_save_load_status == PC_SAVE_INVALID || pc_save_load_status == PC_SAVE_IO_ERROR) return FALSE;
    if (!l_aram_block_p_table[mCD_ARAM_DATA_MAIL] ||
        !l_aram_block_p_table[mCD_ARAM_DATA_ORIGINAL] ||
        !l_aram_block_p_table[mCD_ARAM_DATA_DIARY]) return FALSE;

    pc_ensure_save_dirs();
    Save_Get(save_exist) = TRUE;
    Save_Get(save_check).version = mFRm_VERSION;
    mFRm_SetSaveCheckData(Save_GetPointer(save_check));

    is_town = strcmp(gci_path, PC_TOWN_PATH) == 0;
    if (is_town || pc_ext_town_is_merged(gci_path)) {
        /* a town file (the home town, or a Card B town kept as one): the GameCube image goes in its GCIS chunk */
        u32 size = 0;
        u8* image = pc_gci_build(&common_data.save.save, l_aram_block_p_table[mCD_ARAM_DATA_MAIL],
                                 l_aram_block_p_table[mCD_ARAM_DATA_ORIGINAL],
                                 l_aram_block_p_table[mCD_ARAM_DATA_DIARY], &size);
        int ok = image != NULL && pc_ext_town_write(gci_path, image, size);

        free(image);
        if (!ok) return FALSE;
    } else if (!pc_gci_write(gci_path, &common_data.save.save,
                             l_aram_block_p_table[mCD_ARAM_DATA_MAIL],
                             l_aram_block_p_table[mCD_ARAM_DATA_ORIGINAL],
                             l_aram_block_p_table[mCD_ARAM_DATA_DIARY])) {
        return FALSE;
    }
    OSReport("[PC] save written to %s\n", gci_path);
    pc_autosave_reset_timer();
    if (is_town) {
        pc_save_loaded = TRUE;
        pc_save_load_status = PC_SAVE_LOADED;
        pc_mcd_passport_done();
        pc_cloud_save_push(gci_path);
        pc_sync_after_save();
    }
    return TRUE;
}

/* path: a town file (or its newest good backup) or a .gci; used gets the file actually read (its ac-plus data is
 * that file's, or the .gci's companion) */
static pc_save_result pc_save_read_image_used(const char* path, pc_gci_image* image, char* used, int used_size) {
    pc_save_result result;

    if (strcmp(path, PC_TOWN_PATH) == 0 || pc_ext_town_is_merged(path)) {
        u32 size = pc_gci_image_size();
        u8* raw = (u8*)malloc(size);
        struct stat st;

        if (raw == NULL) return PC_SAVE_IO_ERROR;
        if (!pc_ext_town_read_image(path, raw, size, used, used_size)) {
            free(raw);
            return stat(path, &st) == 0 ? PC_SAVE_INVALID : PC_SAVE_MISSING;
        }
        result = pc_gci_parse(raw, size, image);
        free(raw);
    } else {
        result = pc_gci_read(path, image);
        snprintf(used, used_size, "%s", path);
    }
    if (result == PC_SAVE_LOADED) {
        pc_fix_zeroed_original_entries(image->original, image->save->land_info.id);
    }
    return result;
}

static pc_save_result pc_save_read_image(const char* path, pc_gci_image* image) {
    char used[PC_GCI_PATH_MAX + 16];
    pc_save_result result = pc_save_read_image_used(path, image, used, sizeof(used));
    return result;
}

static void pc_save_dev_after_load(void);

static pc_save_result pc_save_read_gci(const char* path) {
    pc_gci_image image;
    char used[PC_GCI_PATH_MAX + 16];
    pc_save_result result = pc_save_read_image_used(path, &image, used, sizeof(used));
    int i;
    if (result != PC_SAVE_LOADED) return result;
    mCD_save_data_aram_malloc();
    for (i = 0; i < mCD_ARAM_DATA_NUM; i++) {
        if (!l_aram_block_p_table[i]) {
            pc_gci_free(&image);
            return PC_SAVE_IO_ERROR;
        }
    }
    memcpy(&common_data.save.save, image.save, sizeof(Save_t));
    memcpy(l_aram_block_p_table[mCD_ARAM_DATA_MAIL], image.mail, sizeof(*image.mail));
    memcpy(l_aram_block_p_table[mCD_ARAM_DATA_ORIGINAL], image.original, sizeof(*image.original));
    memcpy(l_aram_block_p_table[mCD_ARAM_DATA_DIARY], image.diary, sizeof(*image.diary));
    Common_Set(copy_protect, Save_Get(copy_protect));
    pc_gci_free(&image);
    /* the ac-plus data of the same file (a town file, or a .gci's companion) */
    pc_ext_save_load(used);
    pc_save_dev_after_load();
    return PC_SAVE_LOADED;
}

static int pc_save_read_gci_to_keep(const char* path) {
    pc_gci_image image;
    char used[PC_GCI_PATH_MAX + 16];
    l_keepSave_set = FALSE;
    if (pc_save_read_image_used(path, &image, used, sizeof(used)) != PC_SAVE_LOADED) return FALSE;
    snprintf(l_keepSave_path, sizeof(l_keepSave_path), "%s", used);
    memcpy(&l_keepSave.save, image.save, sizeof(Save_t));
    memcpy(&l_keepMail, image.mail, sizeof(l_keepMail));
    memcpy(&l_keepOriginal, image.original, sizeof(l_keepOriginal));
    memcpy(&l_keepDiary, image.diary, sizeof(l_keepDiary));
    pc_gci_free(&image);
    l_keepSave_set = TRUE;
    return TRUE;
}

/* Legacy saves may be relative to the working directory or executable directory. */
static void pc_save_migrate_legacy(void) {
    char exe_card_a[PC_GCI_PATH_MAX] = {0};
    char exe_card_a_prefixed[PC_GCI_PATH_MAX] = {0};
    char exe_flat[PC_GCI_PATH_MAX] = {0};
    const char* sources[7];
    int count = 0;
    int i;

    sources[count++] = PC_GCI_PATH_LEGACY;
    sources[count++] = PC_CWD_CARD_A_GCI_PATH;
    sources[count++] = PC_CWD_CARD_A_PREFIXED_GCI_PATH;
    sources[count++] = PC_CWD_FLAT_GCI_PATH;
    if (pc_platform_base_path(PC_CWD_CARD_A_GCI_PATH, exe_card_a, sizeof(exe_card_a))) {
        sources[count++] = exe_card_a;
    }
    if (pc_platform_base_path(PC_CWD_CARD_A_PREFIXED_GCI_PATH, exe_card_a_prefixed,
                              sizeof(exe_card_a_prefixed))) {
        sources[count++] = exe_card_a_prefixed;
    }
    if (pc_platform_base_path(PC_CWD_FLAT_GCI_PATH, exe_flat, sizeof(exe_flat))) {
        sources[count++] = exe_flat;
    }

    pc_ensure_save_dirs();
    for (i = 0; i < count; i++) {
        if (pc_gci_import(sources[i], PC_GCI_PATH)) break;
    }
}

typedef int (*pc_card_town_proc)(const char* path, void* arg);

static void pc_card_scan_towns(int channel, int import_legacy, pc_card_town_proc proc, void* arg) {
    const char* relative_dir = channel == mCD_SLOT_B ? "save/card_b" : "save/card_a";
    char active_dir[PC_GCI_PATH_MAX];
    char exe_dir[PC_GCI_PATH_MAX] = {0};
    char filename[PC_GCI_PATH_MAX];
    char path[PC_GCI_PATH_MAX];
    char source[PC_GCI_PATH_MAX];
    const char* directories[] = { active_dir, relative_dir, exe_dir };
    const int directory_count = import_legacy ? ARRAY_COUNT(directories) : 1;
    int i, length;
    int found = 0;

    if (!pc_platform_user_path(relative_dir, active_dir, sizeof(active_dir))) return;
    if (!pc_platform_base_path(relative_dir, exe_dir, sizeof(exe_dir))) exe_dir[0] = '\0';

    for (i = 0; i < directory_count && found == 0; i++) {
        if (directories[i][0] == '\0') continue;
        filename[0] = '\0';
        while (pc_platform_next_gci(directories[i], filename, sizeof(filename))) {
            length = snprintf(path, sizeof(path), "%s/%s", active_dir, filename);
            if (length < 0 || length >= (int)sizeof(path)) continue;
            if (i == 0) {
                if (!pc_gci_is_town_file(path)) continue;
            } else {
                length = snprintf(source, sizeof(source), "%s/%s", directories[i], filename);
                if (length < 0 || length >= (int)sizeof(source)) continue;
                if (!pc_gci_is_town_file(source)) continue;
                pc_ensure_save_dirs();
                if (!pc_gci_import(source, path)) continue;
            }
            found++;
            if (proc(path, arg)) return;
        }
    }
}

typedef struct {
    char* path;
    int path_size;
} pc_card_path_buffer;

static int pc_card_take_first_town(const char* path, void* arg) {
    pc_card_path_buffer* buffer = (pc_card_path_buffer*)arg;
    int length = snprintf(buffer->path, buffer->path_size, "%s", path);
    if (length >= 0 && length < buffer->path_size) return TRUE;
    buffer->path[0] = '\0';
    return FALSE;
}

static int pc_card_find_town(int channel, char* path, int path_size) {
    pc_card_path_buffer buffer = { path, path_size };
    path[0] = '\0';
    pc_card_scan_towns(channel, TRUE, pc_card_take_first_town, &buffer);
    return path[0] != '\0';
}

static pc_save_result pc_save_scan_gci_dir(void) {
    char path[PC_GCI_PATH_MAX];
    pc_save_result result;
    pc_save_result candidate;

    result = pc_save_read_gci(PC_GCI_PATH);
    if (result != PC_SAVE_MISSING) return result;
    if (!pc_platform_user_path("save/card_a/8P-GAFE-" PC_GCI_FILENAME, path, sizeof(path))) return PC_SAVE_IO_ERROR;
    candidate = pc_save_read_gci(path);
    if (candidate != PC_SAVE_MISSING) return candidate;
    if (pc_card_find_town(mCD_SLOT_A, path, sizeof(path))) {
        return pc_save_read_gci(path);
    }
    return PC_SAVE_MISSING;
}

static void pc_save_convert_old(void);

static pc_save_result pc_save_find_and_load(void) {
    char tmp_path[PC_GCI_PATH_MAX + 8];
    struct stat st;

    if (!pc_save_paths_init()) return PC_SAVE_IO_ERROR;
    pc_ensure_save_dirs();
    pc_cloud_save_pull(PC_TOWN_PATH);
    pc_sync_before_load();
    if (stat(PC_TOWN_PATH, &st) != 0) {
        /* no town file yet: a .gci from elsewhere (an older port's folder, Dolphin's name) comes to card_a, then
         * moves into a town file */
        pc_save_migrate_legacy();
        pc_save_convert_old();
    }
    /* an interrupted save with no town file left (the town file and its backups are tried first) */
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", PC_TOWN_PATH);
    if (stat(PC_TOWN_PATH, &st) != 0 && stat(tmp_path, &st) == 0) {
        OSReport("[PC] Found orphaned temp save '%s', recovering...\n", tmp_path);
        rename(tmp_path, PC_TOWN_PATH);
    }
    /* pc_save_read_gci falls back to the newest good backup (.bak1-3) of a damaged or missing town file */
    return pc_save_read_gci(PC_TOWN_PATH);
}

int pc_save_check_and_load(void) {
    pc_save_result result;
    pc_save_ready = FALSE;
    pc_start_state = PC_START_IDLE;
    result = pc_save_find_and_load();
    if (result == PC_SAVE_MISSING && pc_save_load_status != PC_SAVE_MISSING) {
        result = PC_SAVE_IO_ERROR;
    }
    pc_save_load_status = result;
    pc_save_loaded = result == PC_SAVE_LOADED;
    Common_Set(save_error_type, result == PC_SAVE_INVALID || result == PC_SAVE_IO_ERROR ? mFRm_ERROR_DATA_CORRUPTED : 0);
    Common_Set(memcard_slot, mCD_SLOT_A);
    if (!pc_save_loaded) mFRm_ClearSaveCheckData(Save_GetPointer(save_check));
    if (pc_save_loaded) pc_mcd_passport_restore();
    return pc_save_loaded;
}

/* Reload the home town from disk. PC equivalent of GC re-reading the memory card. */
int pc_save_reload(void) {
    if (!pc_save_loaded) return 0;
    return pc_save_check_and_load();
}

/* --- Card B scanning --- */

/* A town standing in for Card B's (a random town, pc_random_town.c), until the player is home again */
static char l_card_b_override[PC_GCI_PATH_MAX];

void pc_mcd_card_b_override(const char* path) {
    snprintf(l_card_b_override, sizeof(l_card_b_override), "%s", path != NULL ? path : "");
}

/* a random town's trip is set up (the station's town picker stays out of it) */
int pc_mcd_card_b_override_active(void) {
    return l_card_b_override[0] != '\0';
}

static int pc_card_b_find_town(void) {
    if (l_card_b_override[0] != '\0') {
        struct stat st;
        snprintf(l_card_b_gci_path, sizeof(l_card_b_gci_path), "%s", l_card_b_override);
        return stat(l_card_b_gci_path, &st) == 0;
    }
    if (pc_card_find_town(mCD_SLOT_B, l_card_b_gci_path, sizeof(l_card_b_gci_path))) {
        return TRUE;
    }
    l_card_b_gci_path[0] = '\0';
    return FALSE;
}

static pc_save_result pc_read_gci_land_info(const char* path, mLd_land_info_c* out) {
    pc_gci_image image;
    pc_save_result result = pc_save_read_image(path, &image);
    if (result != PC_SAVE_LOADED) return result;
    memcpy(out, &image.save->land_info, sizeof(*out));
    pc_gci_free(&image);
    return PC_SAVE_LOADED;
}

static int pc_card_b_collect_town(const char* path, void* arg) {
    pc_card_town* town;
    const char* separator;
    mLd_land_info_c land_info;
    pc_save_result result;
    int usable = TRUE;
    int length;
    (void)arg;

    if (l_card_b_town_count >= PC_CARD_TOWN_MAX) {
        OSReport("[PC] Card B: town list full (%d), ignoring '%s'\n", PC_CARD_TOWN_MAX, path);
        return TRUE;
    }
    result = pc_read_gci_land_info(path, &land_info);
    if (result != PC_SAVE_LOADED) {
        OSReport("[PC] Card B: '%s' is unreadable (result %d)\n", path, result);
        usable = FALSE;
    } else if (!mLd_CheckId(land_info.id)) {
        OSReport("[PC] Card B: '%s' has an invalid land id 0x%04X\n", path, land_info.id);
        usable = FALSE;
    } else if (mLd_CheckThisLand(land_info.name, land_info.id)) {
        OSReport("[PC] Card B: skipping '%s' (current town)\n", path);
        return FALSE;
    }

    town = &l_card_b_towns[l_card_b_town_count];
    length = snprintf(town->path, sizeof(town->path), "%s", path);
    if (length < 0 || length >= (int)sizeof(town->path)) return FALSE;
    separator = strrchr(town->path, '/');
    town->entry.file = separator != NULL ? separator + 1 : town->path;
    if (usable) {
        memcpy(town->entry.name, land_info.name, sizeof(town->entry.name));
        l_card_b_usable_count++;
    } else {
        memset(town->entry.name, 0, sizeof(town->entry.name));
    }
    town->entry.usable = usable;
    l_card_b_town_count++;
    return FALSE;
}

int pc_card_b_refresh_towns(void) {
    l_card_b_town_count = 0;
    l_card_b_usable_count = 0;
    l_card_b_town_selected = FALSE;
    if (!pc_save_paths_init() || mLd_PlayerManKindCheck()) return 0;
    pc_card_scan_towns(mCD_SLOT_B, TRUE, pc_card_b_collect_town, NULL);
    OSReport("[PC] Card B: %d visitable town(s), %d damaged\n", l_card_b_usable_count,
             l_card_b_town_count - l_card_b_usable_count);
    return l_card_b_usable_count;
}

int pc_card_b_town_count(void) {
    return l_card_b_town_count;
}

const pc_card_town_entry* pc_card_b_town(int index) {
    if (index < 0 || index >= l_card_b_town_count) return NULL;
    return &l_card_b_towns[index].entry;
}

static int pc_card_count_town(const char* path, void* arg) {
    (void)path;
    ++*(int*)arg;
    return FALSE;
}

int pc_card_b_scan_town_files(void) {
    int count = 0;
    if (!pc_save_paths_init()) return 0;
    pc_card_scan_towns(mCD_SLOT_B, FALSE, pc_card_count_town, &count);
    return count;
}

int pc_card_b_town_base(char* output, int output_size) {
    const char* separator = strrchr(l_card_b_gci_path, '/');
    const char* extension = strrchr(l_card_b_gci_path, '.');
    int length;

    if (output == NULL || output_size <= 0 || l_card_b_gci_path[0] == '\0') return FALSE;
    if (extension == NULL || (separator != NULL && extension < separator)) {
        length = (int)strlen(l_card_b_gci_path);
    } else {
        length = (int)(extension - l_card_b_gci_path);
    }
    if (length <= 0 || (separator != NULL && length <= (int)(separator - l_card_b_gci_path) + 1) ||
        length >= output_size) {
        return FALSE;
    }
    memcpy(output, l_card_b_gci_path, length);
    output[length] = '\0';
    return TRUE;
}

int pc_card_b_is_legacy_town(void) {
    char first[PC_GCI_PATH_MAX];
    pc_card_path_buffer buffer = { first, sizeof(first) };
    first[0] = '\0';
    if (l_card_b_gci_path[0] == '\0') return FALSE;
    pc_card_scan_towns(mCD_SLOT_B, FALSE, pc_card_take_first_town, &buffer);
    return first[0] != '\0' && strcmp(first, l_card_b_gci_path) == 0;
}

int pc_card_b_select_town(int index) {
    const pc_card_town_entry* town = pc_card_b_town(index);
    if (town == NULL || !town->usable || mLd_PlayerManKindCheck()) return FALSE;
    memcpy(l_card_b_gci_path, l_card_b_towns[index].path, sizeof(l_card_b_gci_path));
    l_card_b_town_selected = TRUE;
    OSReport("[PC] Card B: selected destination '%s'\n", l_card_b_gci_path);
    return TRUE;
}

/* --- Memory card API implementation --- */

void mCD_init_card(void) {
    /* Town and NES saves bypass Aurora CARD emulation; CARDInit would create an unused card directory. */
}

void mCD_InitAll(void) {
    pc_save_ready = FALSE;
    pc_start_state = PC_START_IDLE;
    /* ARAM blocks must persist — don't null them */
    memset(&l_mcd_foreigner_file, 0, sizeof(l_mcd_foreigner_file));
    l_keepSave_set = FALSE;
    l_mcd_keep_startCond = 0;
    l_card_b_gci_path[0] = '\0';
    l_card_b_town_count = 0;
    l_card_b_usable_count = 0;
    l_card_b_town_selected = FALSE;
}

static int pc_start_write(void) {
    if (!pc_save_write_gci()) return mCD_TRANS_ERR_IOERROR;
    pc_start_state = PC_START_COMPLETE;
    return mCD_TRANS_ERR_NONE;
}

static int pc_start_persist(void) {
    u16 copy_protect = pc_get_land_copy_protect();
    Common_Set(copy_protect, copy_protect);
    Save_Set(copy_protect, copy_protect);
    Save_Set(travel_hard_time, lbRTC_HardTime());
    pc_start_state = PC_START_WRITE_PENDING;
    return pc_start_write();
}

static int pc_start_resident(void) {
    Private_c* priv = Now_Private;
    if (priv == NULL) return mCD_TRANS_ERR_IOERROR;
    Common_Set(reset_flag, FALSE);
    if (!pc_check_reset_code(priv) && !ZURUMODE2_ENABLED() && !g_pc_settings.disable_resetti) {
        Common_Set(reset_flag, TRUE);
        priv->reset_count++;
    }
    pc_set_reset_code(priv);
    return pc_start_persist();
}

static void pc_start_load_passport(int player_no) {
    Private_c* foreigner = mPr_GetForeignerP();
    mPr_CopyPrivateInfo(foreigner, &l_mcd_foreigner_file.file.priv);
    memcpy(mNpc_GetInAnimalP(), &l_mcd_foreigner_file.file.remove_animal, sizeof(Animal_c));
    mPr_LoadPak_and_SetPrivateInfo2(foreigner, (u8)player_no);
    mFM_SetBlockKindLoadCombi();
    mNpc_FinishRemoveAnimal(mNpc_GetInAnimalP());
}

static int pc_start_return_home(void) {
    mHm_SetNowHome();
    if (Common_Get(player_no) != mPr_FOREIGNER && Now_Private != NULL) {
        pc_set_reset_code(Now_Private);
        return pc_start_persist();
    }
    pc_start_state = PC_START_COMPLETE;
    return mCD_TRANS_ERR_NONE;
}

int mCD_InitGameStart_bg(int player_no, int card_private_idx, int start_cond, s32* mounted_chan) {
    static const int init_modes[mCD_START_COND_NUM] = {
        mSDI_INIT_MODE_NEW, mSDI_INIT_MODE_FROM, mSDI_INIT_MODE_NEW_PLAYER,
        mSDI_INIT_MODE_PAK, mSDI_INIT_MODE_PAK,
    };
    int mode;
    (void)card_private_idx;
    if (mounted_chan) *mounted_chan = mCD_SLOT_A;
    if (pc_save_load_status == PC_SAVE_INVALID || pc_save_load_status == PC_SAVE_IO_ERROR ||
        start_cond < 0 || start_cond >= mCD_START_COND_NUM || player_no < 0 || player_no >= TOTAL_PLAYER_NUM) {
        return mCD_TRANS_ERR_IOERROR;
    }

    switch (pc_start_state) {
        case PC_START_COMPLETE: return mCD_TRANS_ERR_NONE;
        case PC_START_FAILED: return mCD_TRANS_ERR_IOERROR;
        case PC_START_WRITE_PENDING: return pc_start_write();
        case PC_START_IDLE: break;
    }
    pc_start_state = PC_START_FAILED;
    mode = init_modes[start_cond];
    if (!pc_save_loaded) {
        if (start_cond != mCD_START_COND_0 && start_cond != mCD_START_COND_2) return mCD_TRANS_ERR_IOERROR;
        mode = mSDI_INIT_MODE_NEW;
    }
    if (start_cond == mCD_START_COND_INCOMING_FOREIGNER) {
        memcpy(mNpc_GetInAnimalP(), &l_mcd_foreigner_file.file.remove_animal, sizeof(Animal_c));
    } else if (start_cond == mCD_START_COND_OUTGOING_FOREIGNER) {
        pc_start_load_passport(player_no);
    }
    if (!mSDI_StartDataInit(gamePT, player_no, mode)) return mCD_TRANS_ERR_IOERROR;
    l_mcd_keep_startCond = start_cond;
    pc_save_ready = TRUE;

    switch (start_cond) {
        case mCD_START_COND_1:
            return pc_start_resident();
        case mCD_START_COND_OUTGOING_FOREIGNER:
            return pc_start_return_home();
        case mCD_START_COND_INCOMING_FOREIGNER:
            Common_Set(now_private, &l_mcd_foreigner_file.file.priv);
            Common_Set(player_no, mPr_FOREIGNER);
            break;
        default:
            break;
    }
    pc_start_state = PC_START_COMPLETE;
    return mCD_TRANS_ERR_NONE;
}

void mCD_LoadLand(void) {
    pc_save_check_and_load();
}

int mCD_SaveHome_bg(int param_1, int* chan) {
    int slot = mCD_GetThisLandSlotNo();
    int result;

    if (chan) *chan = slot;
    if (slot == mCD_SLOT_B && l_card_b_gci_path[0] == '\0') {
        OSReport("[PC] mCD_SaveHome_bg: no Card B path\n");
        return mCD_TRANS_ERR_IOERROR;
    }

    pc_save_pre_write_side_effects(param_1);

    if (slot == mCD_SLOT_B) {
        result = pc_save_write_gci_to(l_card_b_gci_path);
    } else {
        result = pc_save_write_gci();
    }

    if (!result) {
        OSReport("[PC] mCD_SaveHome_bg: save failed!\n");
        return mCD_TRANS_ERR_IOERROR;
    }

    return mCD_TRANS_ERR_NONE;
}

/* Z+Start (pc_quick_save.c): save the town on the spot. Unlike the gyroid's
 * save-and-quit this leaves the session alone (no wisp or money rock cleanup),
 * but it clears the reset code like a proper save, so quitting afterwards does
 * not bring Resetti. */
int pc_quick_save(void) {
    Private_c* priv = Now_Private;
    u16 copy_protect;
    int result;

    if (priv == NULL || Common_Get(player_no) >= PLAYER_NUM) {
        return FALSE;
    }

    mCkRh_SavePlayTime(Common_Get(player_no));
    priv->reset_code = 0;
    if (Common_Get(now_home) != NULL) {
        Common_Get(now_home)->flags.has_saved = TRUE;
    }

    copy_protect = pc_get_land_copy_protect();
    Common_Set(copy_protect, copy_protect);
    Save_Set(copy_protect, copy_protect);
    Save_Set(travel_hard_time, lbRTC_HardTime());

    if (mCD_GetThisLandSlotNo() == mCD_SLOT_B && l_card_b_gci_path[0] != '\0') {
        result = pc_save_write_gci_to(l_card_b_gci_path);
    } else {
        result = pc_save_write_gci();
    }
    return result;
}

/* --- Travel / Station functions --- */

int mCD_CheckStation_bg(s32* chan) {
    int is_foreigner = mLd_PlayerManKindCheck();
    int slot = is_foreigner ? mCD_SLOT_A : mCD_SLOT_B;
    const char* path;
    if (!pc_save_paths_init()) return mCD_TRANS_ERR_IOERROR;
    path = is_foreigner ? PC_TOWN_PATH : l_card_b_gci_path;
    mLd_land_info_c land_info;

    if (chan) *chan = slot;
    if (!is_foreigner && l_card_b_override[0] != '\0') {
        pc_card_b_find_town(); /* a random town's trip: that town, whatever was picked before */
    } else if (!is_foreigner && !l_card_b_town_selected && !pc_card_b_find_town()) {
        OSReport("[PC] CheckStation: no destination town on Card B\n");
        return mCD_TRANS_ERR_NO_TOWN_DATA;
    }

    switch (pc_read_gci_land_info(path, &land_info)) {
        case PC_SAVE_MISSING: return mCD_TRANS_ERR_NO_TOWN_DATA;
        case PC_SAVE_INVALID: return mCD_TRANS_ERR_CORRUPT;
        case PC_SAVE_IO_ERROR: return mCD_TRANS_ERR_IOERROR;
        case PC_SAVE_LOADED: break;
    }
    if (!mLd_CheckId(land_info.id)) return mCD_TRANS_ERR_CORRUPT;
    if (mLd_CheckThisLand(land_info.name, land_info.id)) {
        OSReport("[PC] CheckStation: destination is the current town\n");
        return mCD_TRANS_ERR_LAND_EXIST;
    }

    return mCD_TRANS_ERR_NONE_NEXTLAND;
}

/* Persist current town and load the "other" town into l_keepSave.
 *  - Resident: save home (Card A, marked away) + load Card B → l_keepSave.
 *  - Foreigner: save visited town (Card B) + load Card A → l_keepSave. */
int mCD_SaveStation_NextLand_bg(s32* chan) {
    int is_foreigner = mLd_PlayerManKindCheck();

    l_keepSave_set = FALSE;
    if (chan) *chan = is_foreigner ? mCD_SLOT_B : mCD_SLOT_A;
    if (l_card_b_gci_path[0] == '\0') {
        OSReport("[PC] SaveStation_NextLand: no Card B path\n");
        return mCD_TRANS_ERR_NO_TOWN_DATA;
    }

    if (is_foreigner) {
        /* Record departure info (visited town) for Rover. */
        {
            mCD_persistent_data_c* persistant = Common_GetPointer(travel_persistent_data);
            int i;
            memcpy(&persistant->land, Save_GetPointer(land_info), sizeof(mLd_land_info_c));
            for (i = 0; i < PLAYER_NUM; i++) {
                mPr_CopyPersonalID(&persistant->pid[i], &Save_Get(private_data[i]).player_ID);
            }
        }

        /* Refresh passport from Now_Private so visit-time changes carry home. */
        {
            Private_c* current_priv = Now_Private;
            if (current_priv) {
                mNpc_GetRemoveAnimal(mNpc_GetInAnimalP(), TRUE);
                mPr_CopyPrivateInfo(&l_mcd_foreigner_file.file.priv, current_priv);
                memcpy(&l_mcd_foreigner_file.file.remove_animal, mNpc_GetInAnimalP(), sizeof(Animal_c));
                l_mcd_foreigner_file.file.checksum = 0;
                l_mcd_foreigner_file.file.checksum =
                    mFRm_GetFlatCheckSum((u16*)&l_mcd_foreigner_file.file,
                                         sizeof(mCD_foreigner_c),
                                         l_mcd_foreigner_file.file.checksum);
            }
        }

        if (!pc_save_write_gci_to(l_card_b_gci_path)) {
            OSReport("[PC] SaveStation_NextLand(return): failed to save visited town\n");
            return mCD_TRANS_ERR_IOERROR;
        }

        if (!pc_save_read_gci_to_keep(PC_TOWN_PATH)) {
            OSReport("[PC] SaveStation_NextLand(return): failed to load home town\n");
            if (chan) *chan = mCD_SLOT_A;
            return mCD_TRANS_ERR_CORRUPT;
        }

        l_mcd_keep_startCond = mCD_START_COND_OUTGOING_FOREIGNER;

        if (chan) *chan = mCD_SLOT_A;
        return mCD_TRANS_ERR_NONE;
    }

    if (!pc_save_read_gci_to_keep(l_card_b_gci_path)) {
        OSReport("[PC] SaveStation_NextLand: failed to load Card B town\n");
        if (chan) *chan = mCD_SLOT_B;
        return mCD_TRANS_ERR_CORRUPT;
    }

    /* Preserve the departure town for Rover's dialogue. */
    {
        mCD_persistent_data_c* persistant = Common_GetPointer(travel_persistent_data);
        int i;
        memcpy(&persistant->land, Save_GetPointer(land_info), sizeof(mLd_land_info_c));
        for (i = 0; i < PLAYER_NUM; i++) {
            mPr_CopyPersonalID(&persistant->pid[i], &Save_Get(private_data[i]).player_ID);
        }
    }

    /* Capture the full player data before marking the resident as away. */
    {
        extern void pc_skin_note_traveller(void); /* their skin tone goes along (pc_skin.c) */
        pc_skin_note_traveller();
    }
    {
        Private_c* current_priv = Now_Private;
        remove(PC_PASSPORT_PATH); /* a train trip: no online visit's passport may bring this player home */
        memset(&l_mcd_foreigner_file, 0, sizeof(l_mcd_foreigner_file));
        if (current_priv) {
            mPr_CopyPrivateInfo(&l_mcd_foreigner_file.file.priv, current_priv);
        }
        mNpc_ClearInAnimal();
        /* ac-plus: no villager follows the player to a random town ("Somewhere new", pc_random_town.c): there's
         * no going back there, so they'd be gone for good */
        if (l_card_b_override[0] == '\0' &&
            (g_pc_settings.travel_move_out_chance >= 100 || RANDOM(100) < g_pc_settings.travel_move_out_chance)) {
            mNpc_CopyRemoveAnimal(mNpc_GetInAnimalP());
        }
        memcpy(&l_mcd_foreigner_file.file.remove_animal, mNpc_GetInAnimalP(), sizeof(Animal_c));
        l_mcd_foreigner_file.file.copy_protect = (u16)Common_Get(copy_protect);
        l_mcd_foreigner_file.file.checksum = 0;
        l_mcd_foreigner_file.file.checksum =
            mFRm_GetFlatCheckSum((u16*)&l_mcd_foreigner_file.file,
                                 sizeof(mCD_foreigner_c),
                                 l_mcd_foreigner_file.file.checksum);
    }

    /* GC uses exists == FALSE to detect an interrupted visit and apply its return-home penalty. */
    if (Now_Private != NULL && mLd_PlayerManKindCheckNo(Common_Get(player_no)) == FALSE) {
        Now_Private->exists = FALSE;
        Now_Private->reset_code = 0;
    }

    if (!pc_save_write_gci()) {
        if (Now_Private != NULL) {
            Now_Private->exists = l_mcd_foreigner_file.file.priv.exists;
            Now_Private->reset_code = l_mcd_foreigner_file.file.priv.reset_code;
        }
        mNpc_ClearInAnimal();
        l_keepSave_set = FALSE;
        OSReport("[PC] SaveStation_NextLand: failed to save home town\n");
        return mCD_TRANS_ERR_IOERROR;
    }

    l_mcd_keep_startCond = mCD_START_COND_INCOMING_FOREIGNER;

    if (chan) *chan = mCD_SLOT_B;
    return mCD_TRANS_ERR_NONE;
}

/* PC travel requires a destination town; standalone passport files are unsupported. */
int mCD_SaveStation_Passport_bg(s32* chan) {
    if (chan) *chan = mLd_PlayerManKindCheck() ? mCD_SLOT_A : mCD_SLOT_B;
    l_keepSave_set = FALSE;
    OSReport("[PC] SaveStation_Passport: no destination town loaded; travel blocked\n");
    return mCD_TRANS_ERR_NO_TOWN_DATA;
}

void mCD_toNextLand(void) {
    Save_t* save = &l_keepSave.save;
    int scene_no;
    mCD_persistent_data_c persis;
    mLd_land_info_c* land_info;
    int last_scene_no;
    mActor_name_t last_field_id;
    s16 demo_profile[2];
    Time_c time;
    Transition_c transition;
    int rtc_enabled;

    if (l_keepSave_set != TRUE) {
        return;
    }

    land_info = &save->land_info;
    if (!mLd_CheckId(land_info->id)) {
        OSReport("[PC] toNextLand: invalid land_info in l_keepSave\n");
        return;
    }

    if (g_pc_verbose) {
        OSReport("[PC] Travel transition: from=0x%04X to=0x%04X start_cond=%d\n",
                 Save_Get(land_info).id, land_info->id, l_mcd_keep_startCond);
    }
    scene_no = Save_Get(scene_no);

    /* Save persistent data that must survive the common_data wipe */
    memcpy(&persis, Common_GetPointer(travel_persistent_data), sizeof(persis));
    last_scene_no = Common_Get(last_scene_no);
    last_field_id = Common_Get(last_field_id);
    memcpy(demo_profile, Common_Get(demo_profiles), sizeof(demo_profile));
    memcpy(&time, Common_GetPointer(time), sizeof(time));
    memcpy(&transition, Common_GetPointer(transition), sizeof(transition));

    memset(&common_data, 0, sizeof(common_data_t));

    memcpy(Common_GetPointer(travel_persistent_data), &persis, sizeof(persis));
    Common_Set(last_field_id, last_field_id);
    Common_Set(last_scene_no, last_scene_no);
    memcpy(Common_Get(demo_profiles), demo_profile, sizeof(demo_profile));
    memcpy(Common_GetPointer(time), &time, sizeof(time));
    memcpy(Common_GetPointer(transition), &transition, sizeof(transition));

    memcpy(Common_GetPointer(save), save, sizeof(Save));
    /* and its ac-plus companion save (none for a town visited, so its ground has none of home's patterns and its
     * houses none of home's rooms; home's again on the way back, before anything's saved there) */
    pc_ext_save_load(l_keepSave_path);

    Common_Set(copy_protect, Save_Get(copy_protect));
    Save_Set(scene_no, scene_no);

    rtc_enabled = Common_Get(time.rtc_enabled);
    Common_Set(time.rtc_enabled, TRUE);
    mTM_rtcTime_limit_check();
    Common_Set(time.rtc_enabled, rtc_enabled);
    lbRTC_GetTime(Common_GetPointer(time.rtc_time));

    Common_Set(now_private, &l_mcd_foreigner_file.file.priv);
    Common_Set(player_no, mPr_FOREIGNER);
    Common_Set(auto_nwrite_set, FALSE);
    memset(Common_GetPointer(auto_nwrite_time), 0, sizeof(Common_Get(auto_nwrite_time)));
    Common_Set(ball_pos, ZeroVec);

    /* Clear town-specific state */
    mTM_clear_renew_is();
    mEv_ClearEventInfo();
    mEv_toland_clear_common();
    mNpc_ClearInAnimal();
    mNpc_FirstClearGoodbyMail();
    mQst_ClearGrabItemInfo();
    memset(Common_Get(npc_schedule), 0, sizeof(Common_Get(npc_schedule)));
    mISL_ClearKeepIsland();
    memset(Common_GetPointer(unused_mail_26522), 0, sizeof(Mail_c));
    Common_Set(_2664E, 0);
    Common_Set(_26650, 0);
    mNpc_ClearCacheName();

    Common_Set(submenu_disabled, TRUE);

    pc_start_state = PC_START_IDLE;

    memset(&l_keepSave, 0, sizeof(Save));
    l_keepSave_set = FALSE;
    l_card_b_town_selected = FALSE;

    mCD_save_data_main_to_aram(&l_keepMail, l_aram_alloc_size_table[mCD_ARAM_DATA_MAIL], mCD_ARAM_DATA_MAIL);
    mCD_save_data_main_to_aram(&l_keepOriginal, l_aram_alloc_size_table[mCD_ARAM_DATA_ORIGINAL], mCD_ARAM_DATA_ORIGINAL);
    mCD_save_data_main_to_aram(&l_keepDiary, l_aram_alloc_size_table[mCD_ARAM_DATA_DIARY], mCD_ARAM_DATA_DIARY);
}

void mCD_ReCheckLoadLand(GAME_PLAY* play) {
    int scene = Save_Get(scene_no);

    l_card_b_override[0] = '\0'; /* home again: a random town's trip is over */
    /* Reload home town from Card A at the same point as GC. */
    mCD_LoadLand();
    Save_Set(scene_no, scene);

    if (Common_Get(save_error_type) != 0) {
        play->next_scene_no = SCENE_PLAYERSELECT_3;
        Common_Set(house_owner_name, RSV_NO);
        Common_Set(last_field_id, RSV_NO);
    } else if (mFRm_CheckSaveData()) {
        int rtc_on = Common_Get(time.rtc_enabled);
        Common_Set(time.rtc_enabled, TRUE);
        mTM_rtcTime_limit_check();
        play->next_scene_no = SCENE_PLAYERSELECT_2;
        Common_Set(time.rtc_enabled, rtc_on);
        mEv_ClearEventInfo();
    } else {
        play->next_scene_no = SCENE_PLAYERSELECT;
        Common_Set(house_owner_name, RSV_NO);
        Common_Set(last_field_id, RSV_NO);
    }
}

/* --- Remaining card management functions --- */

int mCD_GetThisLandSlotNo(void) {

    if (Common_Get(player_no) == mPr_FOREIGNER) return mCD_SLOT_B;
    return mCD_SLOT_A;
}

int mCD_GetThisLandSlotNo_code(int* player_no, s32* slot_card_results) {
    int slot = mCD_GetThisLandSlotNo();

    if (player_no) *player_no = Common_Get(player_no);
    if (slot_card_results) {
        slot_card_results[mCD_SLOT_A] = CARD_RESULT_READY;
        slot_card_results[mCD_SLOT_B] = (slot == mCD_SLOT_B) ? CARD_RESULT_READY : CARD_RESULT_NOCARD;
    }
    return slot;
}

int mCD_GetSaveHomeSlotNo(void) {
    return mCD_SLOT_A;
}

int mCD_GetPlayerNum(void) {
    return 1;
}

int mCD_GetCardPrivateNameCopy(u8* name, int idx) {
    (void)name;
    (void)idx;
    return 0;
}

int mCD_CheckCardPlayerNative(int idx) {

    if (Common_Get(player_no) == mPr_FOREIGNER && idx == mPr_FOREIGNER) {
        return FALSE;
    }
    return TRUE;
}

int mCD_CheckPassportFile(void) {

    if (l_mcd_foreigner_file.file.checksum != 0) {
        return 0; /* passport exists on slot 0 */
    }
    return -1; /* no passport */
}

int mCD_CheckBrokenPassportFile(int slot) {
    (void)slot;
    return 0;
}

int mCD_EraseBrokenLand_bg(int* slot) {
    if (slot) *slot = mCD_SLOT_A;
    return mCD_TRANS_ERR_NONE;
}

int mCD_EraseLand_bg(int* slot) {
    if (slot) *slot = mCD_SLOT_A;
    return mCD_TRANS_ERR_NONE;
}

int mCD_ErasePassportFile_bg(int slot) {
    (void)slot;

    memset(&l_mcd_foreigner_file, 0, sizeof(l_mcd_foreigner_file));
    return mCD_TRANS_ERR_NONE;
}

int mCD_SaveErasePlayer_bg(int* slot) {
    if (slot) *slot = mCD_SLOT_A;
    return mCD_TRANS_ERR_NONE;
}

int mCD_card_format_bg(s32 chan) {
    (void)chan;
    return mCD_TRANS_ERR_NONE;
}

void mCD_PrintErrInfo(gfxprint_t* gfxprint) {
    (void)gfxprint;
}

/* ===================== ac-plus ===================== */

static void pc_save_dev_after_load(void) {
    {
        /* dev: PC_GRANT_BASEMENT=1 gives every house a basement (and at least the M room, whose stairs reach it),
         * to try the basement on a save without one; =2 also the big main room and upstairs, as for the extra
         * rooms (pc_nlrooms.c); =3 as well the statue built and the house paid off, so Nook offers them */
        /* dev: PC_GIVE_ITEM=hex,n puts n of that item in the first player's empty pocket slots */
        const char* give = getenv("PC_GIVE_ITEM");
        const char* grant = getenv("PC_GRANT_BASEMENT");

        if (give != NULL && strchr(give, ',') != NULL) {
            mActor_name_t item = (mActor_name_t)strtoul(give, NULL, 16);
            int n = atoi(strchr(give, ',') + 1), i;
            Private_c* priv = Save_GetPointer(private_data[0]);

            for (i = 0; i < mPr_POCKETS_SLOT_COUNT && n > 0; i++) {
                if (priv->inventory.pockets[i] == EMPTY_NO) {
                    priv->inventory.pockets[i] = item;
                    n--;
                }
            }
            printf("[PC] gave item %04X, %d short\n", item, n);
            fflush(stdout);
        }
        int h;

        /* dev: PC_FAVOR_TEST=1 puts a delivery favor in the first player's pockets, from the first villager to the
         * second, and seats the second at the Roost all day (ac_roost_guest.c) to try handing favors to guests;
         * PC_FAVOR_TEST=2 instead has the first villager send the player to fetch something from the second */
        if (getenv("PC_FAVOR_TEST") != NULL) {
            extern int pc_favor_test_guest;
            Private_c* priv = Save_GetPointer(private_data[0]);
            int from = -1, to = -1, slot = -1, i;

            for (i = 0; i < ANIMAL_NUM_MAX; i++) {
                if (!mNpc_CheckFreeAnimalInfo(Save_GetPointer(animals[i]))) {
                    if (from < 0) {
                        from = i;
                    } else if (to < 0) {
                        to = i;
                    }
                }
            }
            for (i = 0; i < mPr_POCKETS_SLOT_COUNT; i++) {
                if (priv->inventory.pockets[i] == EMPTY_NO && mQst_CheckFreeQuest(&priv->deliveries[i].base)) {
                    slot = i;
                    break;
                }
            }
            if (to >= 0 && atoi(getenv("PC_FAVOR_TEST")) == 2) {
                for (i = 0; i < mPr_ERRAND_QUEST_NUM; i++) {
                    if (mQst_CheckFreeQuest(&priv->errands[i].base)) {
                        mQst_errand_c* errand = &priv->errands[i];

                        mQst_ClearErrand(errand, 1);
                        errand->base.quest_type = mQst_QUEST_TYPE_ERRAND;
                        errand->base.quest_kind = mQst_ERRAND_REQUEST;
                        errand->base.time_limit_enabled = FALSE;
                        errand->base.progress = 1; /* the last link: the second hands it over */
                        mNpc_CopyAnimalPersonalID(&errand->sender, &Save_Get(animals[from]).id);
                        mNpc_CopyAnimalPersonalID(&errand->recipient, &Save_Get(animals[to]).id);
                        errand->item = ITM_QST_WATCH;
                        errand->pockets_idx = -1;
                        errand->errand_type = mQst_ERRAND_TYPE_CHAIN;
                        mNpc_CopyAnimalPersonalID(&errand->info.chain.used_ids[0], &Save_Get(animals[from]).id);
                        errand->info.chain.used_num = 1;
                        pc_favor_test_guest = to;
                        slot = i;
                        break;
                    }
                }
                printf("[PC] favor test: fetch from villager %d for %d, errand %d\n", to, from, slot);
            } else if (to >= 0 && slot >= 0) {
                mQst_delivery_c* delivery = &priv->deliveries[slot];

                mQst_ClearDelivery(delivery, 1);
                delivery->base.quest_type = mQst_QUEST_TYPE_DELIVERY;
                delivery->base.quest_kind = mQst_DELIVERY_KIND_NORMAL;
                delivery->base.time_limit_enabled = FALSE;
                delivery->base.progress = 0;
                mNpc_CopyAnimalPersonalID(&delivery->sender, &Save_Get(animals[from]).id);
                mNpc_CopyAnimalPersonalID(&delivery->recipient, &Save_Get(animals[to]).id);
                mPr_SetPossessionItem(priv, slot, ITM_CLOTH001 + 4, mPr_ITEM_COND_QUEST);
                pc_favor_test_guest = to;
            }
            printf("[PC] favor test: villager %d -> %d, pocket %d\n", from, to, slot);
            fflush(stdout);
        }
        for (h = 0; grant != NULL && atoi(grant) && h < PLAYER_NUM; h++) {
            extern void mHm_PC_DevRehouse(int no, int size);

            if (mPr_NullCheckPersonalID(&Save_Get(homes[h]).ownerID)) {
                continue;
            }
            Save_Get(homes[h]).flags.has_basement = TRUE;
            if (Save_Get(homes[h]).size_info.size < 1) {
                Save_Get(homes[h]).size_info.size = Save_Get(homes[h]).size_info.next_size = 1;
            }
            if (atoi(grant) >= 2) {
                mHm_PC_DevRehouse(h, mHm_HOMESIZE_UPPER);
            }
            if (atoi(grant) >= 3) {
                int p;

                Save_Get(homes[h]).size_info.next_size = mHm_HOMESIZE_STATUE;
                Save_Get(homes[h]).size_info.statue_ordered = FALSE;
                Save_Get(homes[h]).size_info.basement_ordered = FALSE; /* nothing still to build or bill */
                Save_Get(homes[h]).size_info.renew = FALSE;
                for (p = 0; p < PLAYER_NUM; p++) {
                    Private_c* priv = Save_GetPointer(private_data[p]);

                    if (mPr_CheckCmpPersonalID(&priv->player_ID, &Save_Get(homes[h]).ownerID)) {
                        priv->inventory.loan = 0;
                    }
                }
            }
        }
    }
}

/* An older save in card_a (a .gci, with its companion .acplus if any; or a .gci from Dolphin or a GameCube) and no
 * town file: the town moves into one. The old files go to a folder of their own, so nothing reads them again
 * (a new town file is never overwritten by an old .gci) and they're still there to go back to. */
static void pc_save_convert_old(void) {
    char name0[PC_GCI_PATH_MAX + 64], name1[PC_GCI_PATH_MAX + 64];
    const char* gci_names[3];
    char found[PC_GCI_PATH_MAX];
    char from[PC_GCI_PATH_MAX + 16];
    char keep_dir[PC_GCI_PATH_MAX + 64];
    const char* gci = NULL;
    struct stat st;
    mLd_land_info_c* info;
    int i;
    int ok = FALSE;

    snprintf(name0, sizeof(name0), "%s/DobutsunomoriP_MURA.gci", PC_CARD_A_DIR);
    snprintf(name1, sizeof(name1), "%s/8P-GAFE-DobutsunomoriP_MURA.gci", PC_CARD_A_DIR);
    gci_names[0] = name0;
    gci_names[1] = name1;
    gci_names[2] = NULL;
    if (stat(PC_TOWN_PATH, &st) == 0) {
        return;
    }
    for (i = 0; gci_names[i] != NULL && gci == NULL; i++) {
        if (stat(gci_names[i], &st) == 0) {
            gci = gci_names[i];
        }
    }
    if (gci == NULL && pc_card_find_town(mCD_SLOT_A, found, sizeof(found))) {
        gci = found;
    }
    if (gci == NULL) {
        return;
    }
    info = (mLd_land_info_c*)malloc(sizeof(mLd_land_info_c));
    if (info == NULL) {
        return;
    }
    /* the .gci, else what's left of an interrupted save, else its newest backup */
    for (i = 0; i <= 1 + PC_SAVE_MAX_BACKUPS && !ok; i++) {
        if (i == 0) {
            snprintf(from, sizeof(from), "%s", gci);
        } else if (i == 1) {
            snprintf(from, sizeof(from), "%s.tmp", gci);
        } else {
            snprintf(from, sizeof(from), "%s.bak%d", gci, i - 1);
        }
        ok = stat(from, &st) == 0 && pc_read_gci_land_info(from, info) == PC_SAVE_LOADED && mLd_CheckId(info->id) &&
             pc_ext_town_convert(from, gci, PC_TOWN_PATH, info->id);
    }
    free(info);
    if (!ok) {
        OSReport("[PC] Couldn't move %s into a town file; it's left as it is\n", gci);
        return;
    }

    /* everything that went with it: <name>.gci, .tmp, .bak*, .acplus, .acplus.bak*, .bad, .cloudbak */
    {
        time_t now = time(NULL);
        struct tm* t = localtime(&now);
        const char* base = strrchr(gci, '/') != NULL ? strrchr(gci, '/') + 1 : gci;
        size_t base_len = strlen(base);
        DIR* d;
        struct dirent* e;

        snprintf(keep_dir, sizeof(keep_dir), "%s/before-town-file-%04d-%02d-%02d_%02d%02d%02d",
                 PC_CARD_A_DIR, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
#ifdef _WIN32
        _mkdir(keep_dir);
#else
        mkdir(keep_dir, 0755);
#endif
        d = opendir(PC_CARD_A_DIR);
        while (d != NULL && (e = readdir(d)) != NULL) {
            char old_path[PC_GCI_PATH_MAX * 2];
            char new_path[PC_GCI_PATH_MAX * 3];

            if (strncmp(e->d_name, base, base_len) != 0) {
                continue;
            }
            snprintf(old_path, sizeof(old_path), "%s/%s", PC_CARD_A_DIR, e->d_name);
            snprintf(new_path, sizeof(new_path), "%s/%s", keep_dir, e->d_name);
            if (rename(old_path, new_path) != 0) {
                OSReport("[PC] Couldn't move %s to %s\n", old_path, keep_dir);
            }
        }
        if (d != NULL) {
            closedir(d);
        }
        OSReport("[PC] Town moved into %s; the old files are in %s\n", PC_TOWN_PATH, keep_dir);
    }
}

/* dev/player: --export-gci <file> writes the home town's GameCube save (without what only ac-plus has: patterns
 * on the ground, extra rooms...) for Dolphin or a GameCube, then the game goes on as usual */
/* What only ac-plus has, for a GameCube's save: its own items (City Folk's furniture, wallpaper and carpets, the
 * basement looks) leave the pockets, the houses, the cottage, the town's ground and the island's */
#include "pc_cf_ftr.h"
#include "pc_cf_creature.h"
#include "pc_cf_decor.h"
#include "pc_basement.h"
static int pc_save_gc_unknown(mActor_name_t item) {
    return pc_cf_ftr_item(item) || PC_CF_ITEM_IS_FISH(item) || PC_CF_ITEM_IS_BUG(item) || pc_cf_decor_item(item) || PC_ITEM_IS_BASEMENT_LOOK(item) ||
           (item >= PC_CF_FTR_ITEM_FIRST && item < PC_CF_FTR_ITEM_FIRST + 0x900); /* (also when not converted here) */
}

static int pc_save_strip_items(mActor_name_t* items, int n) {
    int i, gone = 0;

    for (i = 0; i < n; i++) {
        if (pc_save_gc_unknown(items[i])) {
            items[i] = EMPTY_NO;
            gone++;
        }
    }
    return gone;
}

static int pc_save_strip_for_gc(Save_t* sv) {
    int gone = 0, p, f, z, x;

    for (p = 0; p < PLAYER_NUM; p++) {
        gone += pc_save_strip_items(sv->private_data[p].inventory.pockets, mPr_POCKETS_SLOT_COUNT);
        for (f = 0; f < mHm_ROOM_NUM; f++) {
            mHm_flr_c* fl = &sv->homes[p].floors[f];

            gone += pc_save_strip_items(&fl->layer_main.items[0][0], UT_TOTAL_NUM);
            gone += pc_save_strip_items(&fl->layer_secondary.items[0][0], UT_TOTAL_NUM);
            gone += pc_save_strip_items(&fl->layer_storage1.items[0][0], UT_TOTAL_NUM);
            gone += pc_save_strip_items(&fl->layer_storage2.items[0][0], UT_TOTAL_NUM);
            /* City Folk's wallpaper/carpets and the basement's look (pc_basement.h): the first of the GameCube's */
            if (fl->wall_floor.wallpaper_idx >= PC_CF_DECOR_FIRST || fl->wall_floor.wallpaper_idx == PC_BASEMENT_LOOK_IDX) {
                fl->wall_floor.wallpaper_idx = 0;
                gone++;
            }
            if (fl->wall_floor.flooring_idx >= PC_CF_DECOR_FIRST || fl->wall_floor.flooring_idx == PC_BASEMENT_LOOK_IDX) {
                fl->wall_floor.flooring_idx = 0;
                gone++;
            }
        }
    }
    for (z = 0; z < FG_BLOCK_Z_NUM; z++) {
        for (x = 0; x < FG_BLOCK_X_NUM; x++) {
            gone += pc_save_strip_items(&sv->fg[z][x].items[0][0], UT_TOTAL_NUM);
        }
    }
    for (x = 0; x < mISL_FG_BLOCK_X_NUM; x++) {
        gone += pc_save_strip_items(&sv->island.fgblock[0][x].items[0][0], UT_TOTAL_NUM);
    }
    gone += pc_save_strip_items(&sv->island.cottage.room.layer_main.items[0][0], UT_TOTAL_NUM);
    gone += pc_save_strip_items(&sv->island.cottage.room.layer_secondary.items[0][0], UT_TOTAL_NUM);
    /* what's in letters and with the gyroids */
    for (p = 0; p < PLAYER_NUM; p++) {
        for (f = 0; f < mPr_INVENTORY_MAIL_COUNT; f++) {
            gone += pc_save_strip_items(&sv->private_data[p].mail[f].present, 1);
        }
        for (f = 0; f < HOME_MAILBOX_SIZE; f++) {
            gone += pc_save_strip_items(&sv->homes[p].mailbox[f].present, 1);
        }
        for (f = 0; f < HANIWA_ITEM_HOLD_NUM; f++) {
            gone += pc_save_strip_items(&sv->homes[p].haniwa.items[f].item, 1);
        }
    }
    for (f = 0; f < mPO_MAIL_STORAGE_SIZE; f++) {
        gone += pc_save_strip_items(&sv->post_office.mail[f].present, 1);
    }
    return gone;
}

void pc_save_export_gci(const char* out_path) {
    pc_gci_image image;
    char used[PC_GCI_PATH_MAX + 16];
    u8* gci = NULL;
    u32 size = 0;
    FILE* f;
    int ok = FALSE;

    if (!pc_save_paths_init()) return;
    pc_ensure_save_dirs();
    pc_save_convert_old();
    used[0] = '\0';
    if (pc_save_read_image_used(PC_TOWN_PATH, &image, used, sizeof(used)) == PC_SAVE_LOADED) {
        int gone = pc_save_strip_for_gc(image.save);

        OSReport("[PC] Export: %d ac-plus things left out for the GameCube\n", gone);
        gci = pc_gci_build(image.save, image.mail, image.original, image.diary, &size);
        pc_gci_free(&image);
    }
    if (gci != NULL && (f = fopen(out_path, "wb")) != NULL) {
        ok = fwrite(gci, size, 1, f) == 1;
        ok = fclose(f) == 0 && ok;
    }
    free(gci);
    OSReport("[PC] %s %s as %s\n", ok ? "Exported" : "Could not export", used, out_path);
}

/* Writes `town` as a GameCube save at path, for a Card B trip to it (pc_random_town.c): the town in memory and the
 * mail, pattern and diary blocks are swapped out for it (fresh blocks) while it's written, then put back */
int pc_mcd_write_town_gci(const Save_t* town, const char* path) {
    Save_t* keep = (Save_t*)malloc(sizeof(Save_t));
    void* keep_blocks[mCD_ARAM_DATA_NUM];
    void* blocks[mCD_ARAM_DATA_NUM];
    int ok = keep != NULL;
    int i;

    for (i = 0; i < mCD_ARAM_DATA_NUM; i++) {
        blocks[i] = calloc(1, l_aram_alloc_size_table[i]);
        ok = ok && blocks[i] != NULL;
    }
    if (ok) {
        pc_init_original_entries(blocks[mCD_ARAM_DATA_ORIGINAL]);
        pc_init_mail_entries(blocks[mCD_ARAM_DATA_MAIL]);
        pc_init_diary_entries(blocks[mCD_ARAM_DATA_DIARY]);
        memcpy(keep, &common_data.save.save, sizeof(Save_t));
        memcpy(&common_data.save.save, town, sizeof(Save_t));
        for (i = 0; i < mCD_ARAM_DATA_NUM; i++) {
            keep_blocks[i] = l_aram_block_p_table[i];
            l_aram_block_p_table[i] = blocks[i];
        }
        pc_ensure_save_dirs();
        ok = pc_save_write_gci_to(path);
        for (i = 0; i < mCD_ARAM_DATA_NUM; i++) {
            l_aram_block_p_table[i] = keep_blocks[i];
        }
        memcpy(&common_data.save.save, keep, sizeof(Save_t));
    }
    for (i = 0; i < mCD_ARAM_DATA_NUM; i++) {
        free(blocks[i]);
    }
    free(keep);
    return ok;
}

/* --- Online visits (multiplayer, pc_mp.c) ---
 * The GameCube trip with a passport, with the other town coming from the host over the network (already checked
 * and built by pc_mp_town.c) instead of from card B, and no train: the scene change after these goes straight to
 * the town's station, and the start-of-town setup that the train's arrival scene does runs right after the swap
 * (pc_mcd_online_after_swap, from m_play.c). The visited town is never written anywhere: no card B path. */

static int l_online_pending = 0; /* start condition to run after the swap, 0 = none */
static int l_online_home_player = 0;

#define PC_ONLINE_KEEP_PATH "online_visit.gci" /* never exists: the visited town has no companion save */

/* The passport: while visiting online, the player as they are right now (pockets, bells, everything), in a file next
 * to the home save. Online, a visit can end without the trip home - a closed window, a crash, a lost connection - and
 * the GameCube's rule for that (the pockets are gone, a gyroid's face) is far too harsh for an internet connection.
 * So the next start brings the player home from the passport instead. It holds what they really have at that moment,
 * so nothing can be duplicated by it. Written when leaving, every few seconds during the visit and on every save the
 * visit refuses (quitting, autosave); removed once the home save is written with the player back home. */
#define PC_PASSPORT_MAGIC 0x41435050u /* 'ACPP' */
#define PC_PASSPORT_VERSION 1

typedef struct {
    u32 magic;
    u32 version;
    u32 home_player; /* the player's slot in the home save */
    u32 size;        /* sizeof(mCD_foreigner_c), as written */
    mCD_foreigner_c file;
} pc_passport_c;

static pc_passport_c l_passport;

static void pc_passport_save(Private_c* priv) {
    FILE* fp;

    if (l_online_home_player < 0 || l_online_home_player >= PLAYER_NUM) {
        return;
    }
    memset(&l_passport, 0, sizeof(l_passport));
    l_passport.magic = PC_PASSPORT_MAGIC;
    l_passport.version = PC_PASSPORT_VERSION;
    l_passport.home_player = (u32)l_online_home_player;
    l_passport.size = sizeof(mCD_foreigner_c);
    l_passport.file = l_mcd_foreigner_file.file;
    mPr_CopyPrivateInfo(&l_passport.file.priv, priv);
    l_passport.file.priv.exists = TRUE;
    l_passport.file.checksum = 0;
    l_passport.file.checksum = mFRm_GetFlatCheckSum((u16*)&l_passport.file, sizeof(mCD_foreigner_c), 0);

    pc_ensure_save_dirs();
    fp = fopen(PC_PASSPORT_TMP_PATH, "wb");
    if (fp == NULL) {
        return;
    }
    if (fwrite(&l_passport, sizeof(l_passport), 1, fp) != 1) {
        fclose(fp);
        remove(PC_PASSPORT_TMP_PATH);
        return;
    }
    fflush(fp);
    fclose(fp);
    remove(PC_PASSPORT_PATH); /* (Windows' rename won't replace) */
    rename(PC_PASSPORT_TMP_PATH, PC_PASSPORT_PATH);
}

void pc_mcd_passport_write(void) {
    if (pc_mcd_online_visiting() && Now_Private != NULL) {
        pc_passport_save(Now_Private);
    }
}

/* the home save written with the player at home: the passport has done its job */
static void pc_mcd_passport_done(void) {
    if (Common_Get(player_no) < PLAYER_NUM && Save_Get(private_data[Common_Get(player_no)]).exists) {
        remove(PC_PASSPORT_PATH);
    }
}

/* right after the home save is read from disk: a player it says is away, whose passport is here, comes home */
static void pc_mcd_passport_restore(void) {
    FILE* fp;
    Private_c* priv;
    u16 sum;
    int ok;

    if (!pc_save_paths_init() || (fp = fopen(PC_PASSPORT_PATH, "rb")) == NULL) {
        return;
    }
    memset(&l_passport, 0, sizeof(l_passport));
    ok = fread(&l_passport, sizeof(l_passport), 1, fp) == 1;
    fclose(fp);
    if (!ok || l_passport.magic != PC_PASSPORT_MAGIC || l_passport.version != PC_PASSPORT_VERSION ||
        l_passport.size != sizeof(mCD_foreigner_c) || l_passport.home_player >= PLAYER_NUM) {
        OSReport("[PC] passport: unreadable, left alone\n");
        return;
    }
    sum = l_passport.file.checksum;
    l_passport.file.checksum = 0;
    if (mFRm_GetFlatCheckSum((u16*)&l_passport.file, sizeof(mCD_foreigner_c), 0) != sum) {
        OSReport("[PC] passport: checksum wrong, left alone\n");
        return;
    }
    priv = Save_GetPointer(private_data[l_passport.home_player]);
    if (priv->exists) {
        return; /* home already (the trip back finished; the passport goes with the next save) */
    }
    if (!mPr_CheckCmpPersonalID(&priv->player_ID, &l_passport.file.priv.player_ID)) {
        OSReport("[PC] passport: not this town's player, left alone\n");
        return;
    }
    mPr_CopyPrivateInfo(priv, &l_passport.file.priv);
    priv->exists = TRUE;
    priv->reset_code = 0; /* no penalty: the visit just ended without the train */
    OSReport("[PC] passport: player %u is home from an online visit\n", l_passport.home_player);
}

int pc_mcd_online_depart(GAME_PLAY* play, const Save_t* town) {
    mCD_persistent_data_c* persistant = Common_GetPointer(travel_persistent_data);
    int i;

    if (Now_Private == NULL || mLd_PlayerManKindCheck() || Common_Get(player_no) >= PLAYER_NUM) {
        return FALSE; /* already away */
    }
    memcpy(&persistant->land, Save_GetPointer(land_info), sizeof(mLd_land_info_c));
    for (i = 0; i < PLAYER_NUM; i++) {
        mPr_CopyPersonalID(&persistant->pid[i], &Save_Get(private_data[i]).player_ID);
    }
    {
        extern void pc_skin_note_traveller(void); /* their skin tone goes along (pc_skin.c) */
        pc_skin_note_traveller();
    }

    /* the passport, as at the station */
    memset(&l_mcd_foreigner_file, 0, sizeof(l_mcd_foreigner_file));
    mPr_CopyPrivateInfo(&l_mcd_foreigner_file.file.priv, Now_Private);
    l_mcd_foreigner_file.file.copy_protect = (u16)Common_Get(copy_protect);
    l_mcd_foreigner_file.file.checksum = 0;
    l_mcd_foreigner_file.file.checksum =
        mFRm_GetFlatCheckSum((u16*)&l_mcd_foreigner_file.file, sizeof(mCD_foreigner_c), l_mcd_foreigner_file.file.checksum);

    /* away from home while visiting, as on the GameCube (quitting during the visit costs the pockets) */
    l_online_home_player = Common_Get(player_no);
    Now_Private->exists = FALSE;
    Now_Private->reset_code = 0;
    {
        /* buildings on screen have placeholders on the map until the scene ends: write from a restored copy (as
         * the station does with Actor_info_save_actor, and quick save with restore_fgdata_all) */
        Save_t* live = Common_GetPointer(save.save);
        Save_t* snapshot = (Save_t*)malloc(sizeof(Save_t));
        int ok;

        if (snapshot == NULL) {
            Now_Private->exists = TRUE;
            return FALSE;
        }
        memcpy(snapshot, live, sizeof(Save_t));
        if (play != NULL) {
            restore_fgdata_all(play);
        }
        ok = pc_save_write_gci();
        memcpy(live, snapshot, sizeof(Save_t));
        free(snapshot);
        if (!ok) {
            Now_Private->exists = TRUE;
            OSReport("[PC] online visit: couldn't save home first\n");
            return FALSE;
        }
    }
    pc_passport_save(&l_mcd_foreigner_file.file.priv); /* as they leave: home from here if the visit never ends well */

    memset(&l_keepSave, 0, sizeof(Save));
    memcpy(&l_keepSave.save, town, sizeof(Save_t));
    memset(&l_keepMail, 0, sizeof(l_keepMail));
    memset(&l_keepOriginal, 0, sizeof(l_keepOriginal));
    memset(&l_keepDiary, 0, sizeof(l_keepDiary));
    snprintf(l_keepSave_path, sizeof(l_keepSave_path), "%s", PC_ONLINE_KEEP_PATH);
    l_card_b_gci_path[0] = '\0';
    l_keepSave_set = TRUE;
    l_mcd_keep_startCond = mCD_START_COND_INCOMING_FOREIGNER;
    l_online_pending = mCD_START_COND_INCOMING_FOREIGNER;
    OSReport("[PC] online visit: leaving for %.8s\n", town->land_info.name);
    return TRUE;
}

int pc_mcd_online_return(void) {
    if (Common_Get(player_no) != mPr_FOREIGNER || l_online_pending != 0) {
        return FALSE;
    }
    if (Now_Private != NULL) {
        mPr_CopyPrivateInfo(&l_mcd_foreigner_file.file.priv, Now_Private);
        l_mcd_foreigner_file.file.checksum = 0;
        l_mcd_foreigner_file.file.checksum = mFRm_GetFlatCheckSum((u16*)&l_mcd_foreigner_file.file,
                                                                  sizeof(mCD_foreigner_c),
                                                                  l_mcd_foreigner_file.file.checksum);
    }
    OSReport("[PC] online visit: loading home\n");
    if (!pc_save_paths_init() || !pc_save_read_gci_to_keep(PC_TOWN_PATH)) {
        OSReport("[PC] online visit: couldn't load home to go back\n");
        return FALSE;
    }
    l_mcd_keep_startCond = mCD_START_COND_OUTGOING_FOREIGNER;
    l_online_pending = mCD_START_COND_OUTGOING_FOREIGNER;
    OSReport("[PC] online visit: going home\n");
    return TRUE;
}

int pc_mcd_online_visiting(void) {
    return Common_Get(player_no) == mPr_FOREIGNER && l_keepSave_path[0] != '\0' &&
           strcmp(l_keepSave_path, PC_ONLINE_KEEP_PATH) == 0;
}

/* m_play.c, right after mCD_toNextLand: what the arrival scene after the train would do */
void pc_mcd_online_after_swap(void) {
    int cond = l_online_pending;
    s32 chan;

    if (cond == 0) {
        return;
    }
    l_online_pending = 0;
    if (cond == mCD_START_COND_INCOMING_FOREIGNER) {
        extern void pc_mp_town_apply_extras(void);
        mCD_InitGameStart_bg(mPr_FOREIGNER, 0, cond, &chan);
        pc_mp_town_apply_extras(); /* the host's ground patterns and outdoor furniture, before the town is built */
    } else {
        extern void pc_oftr_visit_clear(void);
        mCD_InitGameStart_bg(l_online_home_player, 0, cond, &chan);
        pc_oftr_visit_clear();
        l_keepSave_path[0] = '\0';
    }
    {
        /* the swap wiped the door we left by: arrive at the station on the train, as after the GameCube's train ride
         * (ac_npc_p_sel2_talk.c_inc's station door and ride-off demo: the player steps off, Porter greets them, the
         * train pulls out; goto_other_scene stores next_scene_id + 1) */
        Door_data_c station = { SCENE_FG, mSc_DIRECT_SOUTH, FALSE, 0, { 1979, 0, 760 }, EMPTY_NO, 0, { 0, 0, 0 } };
        Common_Set(door_data, station);
        Common_Get(door_data).next_scene_id = SCENE_FG + 1;
        Common_Set(demo_profiles[0], mAc_PROFILE_RIDE_OFF_DEMO);
    }
    OSReport("[PC] online visit: arrived, player_no=%d\n", Common_Get(player_no));
}
