/* pc_m_card.c - memory card manager: GCI save/load, village generation, ARAM data blocks
 *
 * Card A (save/card_a/) = player's home town
 * Card B (save/card_b/) = second town for visiting (drop any AC GCI file there)
 */
#include "m_card.h"
#include "m_actor.h"
#include "m_play.h"
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
#include "pc_save_bswap.h"
#include "pc_settings.h"
#include "m_cockroach.h"
#include "m_all_grow_ovl.h"
#include "m_home.h"
#include "lb_rtc.h"
#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>  /* _mkdir */
#endif
#include <dolphin/os.h>  /* OSReport */

/* --- Path constants --- */
#define PC_CARD_A_DIR     "save/card_a"
#define PC_CARD_B_DIR     "save/card_b"
#define PC_GCI_FILENAME   "DobutsunomoriP_MURA.gci"
#define PC_GCI_PATH       PC_CARD_A_DIR "/" PC_GCI_FILENAME
#define PC_GCI_TMP_PATH   PC_CARD_A_DIR "/" PC_GCI_FILENAME ".tmp"
#define PC_SAVE_DIR       "save"
#define PC_SAVE_MAX_BACKUPS 3
#define PC_PASSPORT_PATH "save/online_passport.bin"
#define PC_PASSPORT_TMP_PATH "save/online_passport.bin.tmp"

/* Legacy paths for migration from flat save/ layout */
#define PC_GCI_PATH_LEGACY     "save/DobutsunomoriP_MURA.gci"
#define PC_GCI_TMP_PATH_LEGACY "save/DobutsunomoriP_MURA.gci.tmp"

#define GCI_HEADER_SIZE      sizeof(CARDDir)        /* 64 bytes */
#define GCI_FILE_DATA_SIZE   mCD_LAND_SAVE_SIZE     /* 0x72000 */
#define GCI_OTHERS_OFFSET    0
#define GCI_SAVE_MAIN_OFFSET OTHERS_SIZE            /* 0x26000 */
#define GCI_SAVE_BACK_OFFSET (OTHERS_SIZE + sizeof(Save))  /* 0x4C000 */
#define GCI_SECTOR_SIZE      mCD_MEMCARD_SECTORSIZE /* 0x2000 */

int pc_save_loaded = 0;
static int pc_save_ready = 0;

/* --- Travel state --- */
static Save l_keepSave;                        /* Other town's save data (for Card B visit) */
static int l_keepSave_set = FALSE;
static char l_keepSave_path[300];               /* the file l_keepSave was read from */
static mCD_keep_mail_c l_keepMail;             /* Other town's mail ARAM block */
static mCD_keep_original_c l_keepOriginal;     /* Other town's original designs ARAM block */
static mCD_keep_diary_c l_keepDiary;           /* Other town's diary ARAM block */

/* Passport: the traveling player's private data + departing animal */
static union {
    mCD_foreigner_c file;
    u8 sector_align[mCD_ALIGN_SECTORSIZE(sizeof(mCD_foreigner_c))];
} l_mcd_foreigner_file;

static int l_mcd_keep_startCond = 0;
static char l_card_b_gci_path[300] = {0};      /* Path to the Card B GCI file, if found */

/* External: scan card_b/ for valid AC GCI file (defined in pc_card.c) */
extern int pc_card_scan_for_gci(int chan, char* out_path, int out_size);

/* External: functions from decomp used in mCD_toNextLand */
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
            memset(diary->entries[p][m].text, CHAR_SPACE, mDI_ENTRY_SIZE);
        }
    }
}

/* Match GC's mCD_set_init_mail_data: clear mail slots with font=0xFF (unused) */
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

/* Same as m_card.c mCD_set_init_original_data: blank folder names and blank
 * designs. Without it the pattern storage (Mabel's "save designs to the memory
 * card", also the pause menu's Pattern Storage) is raw zeros: "¡¡¡" folder
 * names and black patterns. */
static void pc_init_original_entries(void* buf) {
    mCD_keep_original_c* keep_original = (mCD_keep_original_c*)buf;
    int i;
    int j;

    for (i = 0; i < mCD_KEEP_ORIGINAL_PAGE_COUNT; i++) {
        mem_clear(keep_original->folder_names[i], sizeof(keep_original->folder_names[i]), CHAR_SPACE);
        for (j = 0; j < mCD_KEEP_ORIGINAL_COUNT; j++) {
            mNW_InitOriginalData(&keep_original->original[i][j]);
        }
    }
}

/* Saves written before pc_init_original_entries existed hold an all-zero
 * block; a block that was ever set up has space-filled folder names. */
static void pc_repair_original_entries(void* buf) {
    mCD_keep_original_c* keep_original = (mCD_keep_original_c*)buf;
    const u8* names = &keep_original->folder_names[0][0];
    u32 i;

    for (i = 0; i < sizeof(keep_original->folder_names); i++) {
        if (names[i] != 0) {
            return;
        }
    }
    OSReport("[PC] Pattern storage was never set up, initialising it\n");
    pc_init_original_entries(buf);
}

void mCD_save_data_aram_malloc(void) {
    int i;
    for (i = 0; i < mCD_ARAM_DATA_NUM; i++) {
        if (l_aram_block_p_table[i] == NULL) {
            l_aram_block_p_table[i] = malloc(l_aram_alloc_size_table[i]);
            if (l_aram_block_p_table[i]) {
                memset(l_aram_block_p_table[i], 0, l_aram_alloc_size_table[i]);
                if (i == mCD_ARAM_DATA_DIARY) {
                    pc_init_diary_entries(l_aram_block_p_table[i]);
                } else if (i == mCD_ARAM_DATA_MAIL) {
                    pc_init_mail_entries(l_aram_block_p_table[i]);
                } else if (i == mCD_ARAM_DATA_ORIGINAL) {
                    pc_init_original_entries(l_aram_block_p_table[i]);
                }
            }
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
        if (l_aram_block_p_table[i] != NULL) {
            memset(l_aram_block_p_table[i], 0, l_aram_alloc_size_table[i]);
            if (i == mCD_ARAM_DATA_DIARY) {
                pc_init_diary_entries(l_aram_block_p_table[i]);
            } else if (i == mCD_ARAM_DATA_MAIL) {
                pc_init_mail_entries(l_aram_block_p_table[i]);
            } else if (i == mCD_ARAM_DATA_ORIGINAL) {
                pc_init_original_entries(l_aram_block_p_table[i]);
            }
        }
    }
}

/* --- GCI read/write helpers --- */

static void put_be32(u8* dst, u32 val) {
    dst[0] = (u8)(val >> 24);
    dst[1] = (u8)(val >> 16);
    dst[2] = (u8)(val >> 8);
    dst[3] = (u8)(val);
}

static void put_be16(u8* dst, u16 val) {
    dst[0] = (u8)(val >> 8);
    dst[1] = (u8)(val);
}

/* rotate backups: .bak3→delete, .bak2→.bak3, .bak1→.bak2, current→.bak1 */
void pc_save_rotate_backups(const char* base_path) {
    char older[300], newer[300];
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

static void pc_ensure_save_dirs(void) {
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

static int pc_save_write_gci_to(const char* gci_path, const char* tmp_path);
void pc_mcd_passport_write(void);
static void pc_mcd_passport_done(void);
static void pc_mcd_passport_restore(void);
int pc_mcd_online_visiting(void);
void pc_autosave_reset_timer(void);        /* pc_quick_save.c */
void pc_cloud_save_push(const char* path); /* pc_cloud_save.c */
void pc_cloud_save_pull(const char* path);
void pc_sync_after_save(void);  /* pc_sync.c */
void pc_sync_before_load(void);
#include "pc_ext_save.h"

/* mCD_get_land_copyProtect */
static u16 pc_get_land_copy_protect(void) {
    u16 code = (u16)RANDOM(0xFFF0);
    return (u16)(code + 1);
}

/* mCD_CheckResetCode: TRUE if no reset code armed (birthday clears it) */
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

/* Money rock / Wisp / Copy Protect. save_mode mirrors GC SaveHome _04:
 * 0 = full save (clears reset code), nonzero = door save (keeps it armed) */
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
    return pc_save_write_gci_to(PC_GCI_PATH, PC_GCI_TMP_PATH);
}

static int pc_save_write_gci_to(const char* gci_path, const char* tmp_path) {
    FILE* fp;
    u8* file_data;
    CARDDir dir_hdr;
    Save_t* save_copy;
    u16 checksum;
    u8* others_ptr;

    if (!pc_save_ready) return TRUE;
    if (pc_mcd_online_visiting()) {
        /* the town in memory is someone else's (an online visit): nothing of it goes over a save on disk */
        printf("[save] not saving: visiting a town online (the passport instead)\n");
        pc_mcd_passport_write();
        return TRUE;
    }

    pc_ensure_save_dirs();

    Save_Get(save_exist) = TRUE;
    Save_Get(save_check).version = mFRm_VERSION;
    mFRm_SetSaveCheckData(Save_GetPointer(save_check));

    file_data = (u8*)calloc(1, GCI_FILE_DATA_SIZE);
    if (!file_data) return FALSE;

    /* Others block (offset 0) — comment, banner, ARAM blocks */
    others_ptr = file_data + GCI_OTHERS_OFFSET;
    {
        const char* title = "DobutsunomoriP (AC PC Port)";
        u8* comment = others_ptr;
        memset(comment, 0, CARD_COMMENT_SIZE);
        strncpy((char*)comment, title, 32);
        memcpy(comment + 32, Save_Get(land_info).name, 8);
    }

    /* ARAM blocks: mail, original, diary (GC/Dolphin order).
     * Set landid on mail block so load-time detection identifies the order. */
    {
        u32 offset = sizeof(MemcardHeader_c) + 32; /* 0x1460 */
        u16 land_id = Save_Get(land_info).id;

        offset = ALIGN_NEXT(offset, 32);
        if (l_aram_block_p_table[mCD_ARAM_DATA_MAIL]) {
            u8* blk = others_ptr + offset;
            u32 sz = l_aram_alloc_size_table[mCD_ARAM_DATA_MAIL];
            memcpy(blk, l_aram_block_p_table[mCD_ARAM_DATA_MAIL], sz);
            pc_save_bswap_keep_mail((mCD_keep_mail_c*)blk, PC_BSWAP_TO_BE);
            /* Set landid (BE u16 at offset 2) so load detects GC order */
            put_be16(blk + 2, land_id);
            /* Recompute BE checksum over the bswapped block so Dolphin's
             * mFRm_ReturnCheckSum validates. */
            blk[0] = 0;
            blk[1] = 0;
            put_be16(blk, pc_checksum_be(blk, sz, 0));
        }
        offset += l_aram_alloc_size_table[mCD_ARAM_DATA_MAIL];

        offset = ALIGN_NEXT(offset, 32);
        if (l_aram_block_p_table[mCD_ARAM_DATA_ORIGINAL]) {
            u8* blk = others_ptr + offset;
            u32 sz = l_aram_alloc_size_table[mCD_ARAM_DATA_ORIGINAL];
            memcpy(blk, l_aram_block_p_table[mCD_ARAM_DATA_ORIGINAL], sz);
            pc_save_bswap_keep_original((mCD_keep_original_c*)blk, PC_BSWAP_TO_BE);
            blk[0] = 0;
            blk[1] = 0;
            put_be16(blk, pc_checksum_be(blk, sz, 0));
        }
        offset += l_aram_alloc_size_table[mCD_ARAM_DATA_ORIGINAL];

        offset = ALIGN_NEXT(offset, 32);
        if (l_aram_block_p_table[mCD_ARAM_DATA_DIARY]) {
            u8* blk = others_ptr + offset;
            u32 sz = l_aram_alloc_size_table[mCD_ARAM_DATA_DIARY];
            memcpy(blk, l_aram_block_p_table[mCD_ARAM_DATA_DIARY], sz);
            pc_save_bswap_keep_diary((mCD_keep_diary_c*)blk, PC_BSWAP_TO_BE);
            blk[0] = 0;
            blk[1] = 0;
            put_be16(blk, pc_checksum_be(blk, sz, 0));
        }
    }

    /* Main Save_t (offset 0x26000) */
    save_copy = (Save_t*)(file_data + GCI_SAVE_MAIN_OFFSET);
    memcpy(save_copy, &common_data.save.save, sizeof(Save_t));

    pc_save_bswap(save_copy, PC_BSWAP_TO_BE);
    {
        u8* chk_ptr = (u8*)&save_copy->save_check.checksum;
        chk_ptr[0] = 0;
        chk_ptr[1] = 0;
    }
    checksum = pc_checksum_be((const u8*)save_copy, sizeof(Save_t), 0);
    put_be16((u8*)&save_copy->save_check.checksum, checksum);

    /* Backup = copy of main */
    memcpy(file_data + GCI_SAVE_BACK_OFFSET, file_data + GCI_SAVE_MAIN_OFFSET, sizeof(Save));

    /* CARDDir header */
    memset(&dir_hdr, 0, sizeof(dir_hdr));
    memcpy(dir_hdr.gameName, "GAFE", 4);
    memcpy(dir_hdr.company, "01", 2);
    dir_hdr.bannerFormat = 0;
    strncpy((char*)dir_hdr.fileName, "DobutsunomoriP_MURA", CARD_FILENAME_MAX);
    {
        time_t unix_now = time(NULL);
        u32 gc_secs = (u32)(unix_now - 946684800LL);
        put_be32((u8*)&dir_hdr.time, gc_secs);
    }
    put_be32((u8*)&dir_hdr.iconAddr, 0xFFFFFFFF);
    put_be16((u8*)&dir_hdr.iconFormat, 0);
    put_be16((u8*)&dir_hdr.iconSpeed, 0);
    dir_hdr.permission = 0x04;
    dir_hdr.copyTimes = 0;
    put_be16((u8*)&dir_hdr.startBlock, 5);
    put_be16((u8*)&dir_hdr.length, (u16)(GCI_FILE_DATA_SIZE / GCI_SECTOR_SIZE));
    put_be32((u8*)&dir_hdr.commentAddr, 0);

    /* write temp file → rotate backups → rename */
    fp = fopen(tmp_path, "wb");
    if (!fp) {
        OSReport("[PC] GCI save: failed to open temp file '%s'\n", tmp_path);
        free(file_data);
        return FALSE;
    }

    if (fwrite(&dir_hdr, GCI_HEADER_SIZE, 1, fp) != 1 ||
        fwrite(file_data, GCI_FILE_DATA_SIZE, 1, fp) != 1) {
        OSReport("[PC] GCI save: fwrite failed (disk full?)\n");
        fclose(fp);
        remove(tmp_path);
        free(file_data);
        return FALSE;
    }

    fflush(fp);
    fclose(fp);
    free(file_data);

    pc_save_rotate_backups(gci_path);
    if (rename(tmp_path, gci_path) != 0) {
        OSReport("[PC] GCI save: rename '%s' -> '%s' failed, recovering...\n",
                 tmp_path, gci_path);
        {
            char bak1[300];
            snprintf(bak1, sizeof(bak1), "%s.bak1", gci_path);
            rename(bak1, gci_path);
        }
        remove(tmp_path);
        return FALSE;
    }

    OSReport("[PC] GCI save: written successfully to %s (backups rotated)\n", gci_path);
    pc_autosave_reset_timer();
    if (strcmp(gci_path, PC_GCI_PATH) == 0) {
        pc_mcd_passport_done();
        pc_ext_save_write(gci_path);
        pc_cloud_save_push(gci_path);
        pc_sync_after_save();
    }
    return TRUE;
}

/* Read a GCI file into common_data (for home town / Card A) */
static int pc_save_read_gci(const char* path) {
    FILE* fp;
    CARDDir dir_hdr;
    u8* file_data;
    Save_t* save_src;
    u32 offset;
    long file_size;

    fp = fopen(path, "rb");
    if (!fp) {
        OSReport("[PC] GCI: fopen('%s') failed\n", path);
        return FALSE;
    }

    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    OSReport("[PC] GCI: opened '%s', size = %ld (0x%lX), expected %ld (0x%lX)\n",
             path, file_size, (unsigned long)file_size,
             (long)(GCI_HEADER_SIZE + GCI_FILE_DATA_SIZE),
             (unsigned long)(GCI_HEADER_SIZE + GCI_FILE_DATA_SIZE));

    if (fread(&dir_hdr, GCI_HEADER_SIZE, 1, fp) != 1) {
        OSReport("[PC] GCI: failed to read %u-byte header\n", (unsigned)GCI_HEADER_SIZE);
        fclose(fp);
        return FALSE;
    }

    OSReport("[PC] GCI: gameName='%c%c%c%c' company='%c%c' fileName='%.32s'\n",
             dir_hdr.gameName[0], dir_hdr.gameName[1],
             dir_hdr.gameName[2], dir_hdr.gameName[3],
             dir_hdr.company[0], dir_hdr.company[1],
             dir_hdr.fileName);
    if (memcmp(dir_hdr.gameName, "GAF", 3) != 0) {
        OSReport("[PC] GCI: not an Animal Crossing save (expected GAFx, got '%.4s')\n",
                 dir_hdr.gameName);
        fclose(fp);
        return FALSE;
    }

    file_data = (u8*)malloc(GCI_FILE_DATA_SIZE);
    if (!file_data) {
        OSReport("[PC] GCI: malloc(%u) failed\n", (unsigned)GCI_FILE_DATA_SIZE);
        fclose(fp);
        return FALSE;
    }

    if (fread(file_data, GCI_FILE_DATA_SIZE, 1, fp) != 1) {
        OSReport("[PC] GCI: failed to read %u bytes of file data (file may be too small)\n",
                 (unsigned)GCI_FILE_DATA_SIZE);
        fclose(fp);
        free(file_data);
        return FALSE;
    }
    fclose(fp);

    save_src = (Save_t*)(file_data + GCI_SAVE_MAIN_OFFSET);
    pc_save_bswap_verify_roundtrip((const u8*)save_src, sizeof(Save_t));

    memcpy(&common_data.save.save, save_src, sizeof(Save_t));
    pc_save_bswap(&common_data.save.save, PC_BSWAP_FROM_BE);

    /* --- Load ARAM blocks from Others section ---
     * Current saves (PC + Dolphin/GC) use order: mail, original, diary.
     * Old PC saves (before landid fix) used: original, mail, diary.
     * Detect by checking the landid field at offset 2 of the first block:
     * if it matches save's land_info.id, first block is mail (GC order). */
    {
        u8* others_ptr = file_data + GCI_OTHERS_OFFSET;
        u32 block_start = ALIGN_NEXT(sizeof(MemcardHeader_c) + 32, 32);
        u16 first_landid = ((u16)others_ptr[block_start + 2] << 8) | others_ptr[block_start + 3];
        u16 save_land_id = common_data.save.save.land_info.id;
        int gc_order = (first_landid == save_land_id && save_land_id != 0);
        u32 mail_size = l_aram_alloc_size_table[mCD_ARAM_DATA_MAIL];
        u32 orig_size = l_aram_alloc_size_table[mCD_ARAM_DATA_ORIGINAL];
        u32 off_mail, off_orig, off_diary;

        if (gc_order) {
            /* Dolphin/GC save: mail, original, diary */
            off_mail = block_start;
            off_orig = ALIGN_NEXT(block_start + mail_size, 32);
            off_diary = ALIGN_NEXT(off_orig + orig_size, 32);
        } else {
            /* PC save: original, mail, diary */
            off_orig = block_start;
            off_mail = ALIGN_NEXT(block_start + orig_size, 32);
            off_diary = ALIGN_NEXT(off_mail + mail_size, 32);
        }

        if (l_aram_block_p_table[mCD_ARAM_DATA_MAIL]) {
            pc_save_bswap_verify_roundtrip_mail(others_ptr + off_mail, mail_size);
            memcpy(l_aram_block_p_table[mCD_ARAM_DATA_MAIL], others_ptr + off_mail, mail_size);
            pc_save_bswap_keep_mail((mCD_keep_mail_c*)l_aram_block_p_table[mCD_ARAM_DATA_MAIL],
                                    PC_BSWAP_FROM_BE);
        }
        if (l_aram_block_p_table[mCD_ARAM_DATA_ORIGINAL]) {
            pc_save_bswap_verify_roundtrip_original(others_ptr + off_orig, orig_size);
            memcpy(l_aram_block_p_table[mCD_ARAM_DATA_ORIGINAL], others_ptr + off_orig, orig_size);
            pc_save_bswap_keep_original((mCD_keep_original_c*)l_aram_block_p_table[mCD_ARAM_DATA_ORIGINAL],
                                        PC_BSWAP_FROM_BE);
            pc_repair_original_entries(l_aram_block_p_table[mCD_ARAM_DATA_ORIGINAL]);
        }
        if (l_aram_block_p_table[mCD_ARAM_DATA_DIARY]) {
            pc_save_bswap_verify_roundtrip_diary(others_ptr + off_diary,
                                                  l_aram_alloc_size_table[mCD_ARAM_DATA_DIARY]);
            memcpy(l_aram_block_p_table[mCD_ARAM_DATA_DIARY], others_ptr + off_diary,
                   l_aram_alloc_size_table[mCD_ARAM_DATA_DIARY]);
            pc_save_bswap_keep_diary((mCD_keep_diary_c*)l_aram_block_p_table[mCD_ARAM_DATA_DIARY],
                                     PC_BSWAP_FROM_BE);
        }
    }

    free(file_data);
    /* The ac-plus companion save always goes with the home town's main file,
     * also when this read was a recovery from a backup. */
    pc_ext_save_load(PC_GCI_PATH);
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
    return TRUE;
}

/* Read a GCI file's Save_t into a provided buffer (for Card B — does NOT touch common_data).
 * Also loads ARAM blocks (mail/original/diary) into the l_keep* buffers.
 * Returns TRUE on success. */
static int pc_save_read_gci_to_keep(const char* path) {
    FILE* fp;
    CARDDir dir_hdr;
    u8* file_data;
    Save_t* save_src;
    u32 offset;

    fp = fopen(path, "rb");
    if (!fp) return FALSE;

    if (fread(&dir_hdr, GCI_HEADER_SIZE, 1, fp) != 1) { fclose(fp); return FALSE; }
    if (memcmp(dir_hdr.gameName, "GAF", 3) != 0) { fclose(fp); return FALSE; }

    file_data = (u8*)malloc(GCI_FILE_DATA_SIZE);
    if (!file_data) { fclose(fp); return FALSE; }

    if (fread(file_data, GCI_FILE_DATA_SIZE, 1, fp) != 1) {
        fclose(fp); free(file_data); return FALSE;
    }
    fclose(fp);

    /* Load Save_t into l_keepSave (try main, fall back to backup) */
    save_src = (Save_t*)(file_data + GCI_SAVE_MAIN_OFFSET);
    memcpy(&l_keepSave.save, save_src, sizeof(Save_t));
    pc_save_bswap(&l_keepSave.save, PC_BSWAP_FROM_BE);

    /* Validate — if main is corrupt, try backup */
    if (!mLd_CheckId(l_keepSave.save.land_info.id)) {
        OSReport("[PC] Card B: main save invalid, trying backup\n");
        save_src = (Save_t*)(file_data + GCI_SAVE_BACK_OFFSET);
        memcpy(&l_keepSave.save, save_src, sizeof(Save_t));
        pc_save_bswap(&l_keepSave.save, PC_BSWAP_FROM_BE);
        if (!mLd_CheckId(l_keepSave.save.land_info.id)) {
            OSReport("[PC] Card B: backup save also invalid\n");
            free(file_data);
            return FALSE;
        }
    }

    /* Load ARAM blocks — detect GC vs legacy PC order (same landid check as main load) */
    {
        u8* others_ptr = file_data + GCI_OTHERS_OFFSET;
        u32 block_start = ALIGN_NEXT(sizeof(MemcardHeader_c) + 32, 32);
        u16 first_landid = ((u16)others_ptr[block_start + 2] << 8) | others_ptr[block_start + 3];
        u16 save_land_id = l_keepSave.save.land_info.id;
        int gc_order = (first_landid == save_land_id && save_land_id != 0);
        u32 mail_size = l_aram_alloc_size_table[mCD_ARAM_DATA_MAIL];
        u32 orig_size = l_aram_alloc_size_table[mCD_ARAM_DATA_ORIGINAL];
        u32 off_mail, off_orig, off_diary;

        if (gc_order) {
            off_mail = block_start;
            off_orig = ALIGN_NEXT(block_start + mail_size, 32);
            off_diary = ALIGN_NEXT(off_orig + orig_size, 32);
        } else {
            off_orig = block_start;
            off_mail = ALIGN_NEXT(block_start + orig_size, 32);
            off_diary = ALIGN_NEXT(off_mail + mail_size, 32);
        }

        /* the blocks' sizes are rounded up to 32 bytes; copy only what the structs hold */
        memcpy(&l_keepMail, others_ptr + off_mail, sizeof(l_keepMail));
        pc_save_bswap_keep_mail(&l_keepMail, PC_BSWAP_FROM_BE);

        memcpy(&l_keepOriginal, others_ptr + off_orig, sizeof(l_keepOriginal));
        pc_save_bswap_keep_original(&l_keepOriginal, PC_BSWAP_FROM_BE);

        memcpy(&l_keepDiary, others_ptr + off_diary, sizeof(l_keepDiary));
        pc_save_bswap_keep_diary(&l_keepDiary, PC_BSWAP_FROM_BE);
    }

    free(file_data);
    l_keepSave_set = TRUE;
    snprintf(l_keepSave_path, sizeof(l_keepSave_path), "%s", path);
    OSReport("[PC] Card B: loaded town '%.*s' (id=0x%04X) from '%s'\n",
             8, l_keepSave.save.land_info.name,
             l_keepSave.save.land_info.id, path);
    return TRUE;
}

/* Migrate legacy flat save/ layout to save/card_a/ */
static void pc_save_migrate_legacy(void) {
    struct stat st_legacy, st_new;

    if (stat(PC_GCI_PATH_LEGACY, &st_legacy) == 0 &&
        stat(PC_GCI_PATH, &st_new) != 0) {
        int b;
        OSReport("[PC] Migrating save from '%s' to '%s'\n", PC_GCI_PATH_LEGACY, PC_GCI_PATH);
        pc_ensure_save_dirs();

        /* Move main save */
        remove(PC_GCI_PATH); /* in case it somehow exists */
        rename(PC_GCI_PATH_LEGACY, PC_GCI_PATH);

        /* Move backups */
        for (b = 1; b <= PC_SAVE_MAX_BACKUPS; b++) {
            char old_bak[300], new_bak[300];
            snprintf(old_bak, sizeof(old_bak), "%s.bak%d", PC_GCI_PATH_LEGACY, b);
            snprintf(new_bak, sizeof(new_bak), "%s.bak%d", PC_GCI_PATH, b);
            remove(new_bak);
            rename(old_bak, new_bak);
        }

        /* Move temp file if orphaned */
        {
            char old_tmp[300];
            snprintf(old_tmp, sizeof(old_tmp), "%s.tmp", PC_GCI_PATH_LEGACY);
            remove(PC_GCI_TMP_PATH);
            rename(old_tmp, PC_GCI_TMP_PATH);
        }

        OSReport("[PC] Migration complete\n");
    }
}

static int pc_save_scan_gci_dir(void) {
    /* Try common AC save filenames in card_a/ */
    static const char* gci_names[] = {
        PC_CARD_A_DIR "/DobutsunomoriP_MURA.gci",
        PC_CARD_A_DIR "/8P-GAFE-DobutsunomoriP_MURA.gci",
        NULL
    };
    int i;
    struct stat st;

    for (i = 0; gci_names[i] != NULL; i++) {
        if (stat(gci_names[i], &st) == 0) {
            OSReport("[PC] GCI scan: found '%s'\n", gci_names[i]);
            if (pc_save_read_gci(gci_names[i])) {
                return TRUE;
            }
        }
    }

    /* Also try dynamic scan of card_a/ for any GCI */
    {
        char found_path[300];
        if (pc_card_scan_for_gci(0, found_path, sizeof(found_path))) {
            OSReport("[PC] GCI scan: found '%s' via directory scan\n", found_path);
            if (pc_save_read_gci(found_path)) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

/* Reload save from GCI file on disk. PC equivalent of GC re-reading the
 * memory card. */
static int pc_save_load_home(void);

int pc_save_reload(void) {
    struct stat st;
    int ok;
    if (!pc_save_loaded) return 0;
    ok = stat(PC_GCI_PATH, &st) == 0 ? pc_save_read_gci(PC_GCI_PATH) : pc_save_scan_gci_dir();
    if (ok) {
        pc_mcd_passport_restore();
    }
    return ok;
}

int pc_save_check_and_load(void) {
    int ok = pc_save_load_home();
    if (ok) {
        pc_mcd_passport_restore();
    }
    return ok;
}

static int pc_save_load_home(void) {
    struct stat st;
    {
        char cwd[512];
        if (getcwd(cwd, sizeof(cwd))) {
            OSReport("[PC] Save: current working directory = '%s'\n", cwd);
        }
    }

    pc_ensure_save_dirs();
    pc_save_migrate_legacy();
    pc_cloud_save_pull(PC_GCI_PATH);
    pc_sync_before_load();

    if (stat(PC_GCI_PATH, &st) == 0) {
        OSReport("[PC] Found GCI save: %s (%ld bytes)\n", PC_GCI_PATH, (long)st.st_size);
        if (pc_save_read_gci(PC_GCI_PATH)) {
            OSReport("[PC] GCI save loaded successfully\n");
            return TRUE;
        }
        OSReport("[PC] GCI save load FAILED\n");
    } else {
        OSReport("[PC] No GCI save at %s\n", PC_GCI_PATH);
    }

    OSReport("[PC] Scanning for other GCI files...\n");
    if (pc_save_scan_gci_dir()) {
        OSReport("[PC] GCI save loaded via scan\n");
        return TRUE;
    }

    /* recovery: try temp file, then backups */
    if (stat(PC_GCI_TMP_PATH, &st) == 0) {
        OSReport("[PC] Found orphaned temp save '%s', recovering...\n", PC_GCI_TMP_PATH);
        if (rename(PC_GCI_TMP_PATH, PC_GCI_PATH) == 0 && pc_save_read_gci(PC_GCI_PATH)) {
            OSReport("[PC] Recovered save from temp file\n");
            return TRUE;
        }
    }
    {
        char bak_path[300];
        int b;
        for (b = 1; b <= PC_SAVE_MAX_BACKUPS; b++) {
            snprintf(bak_path, sizeof(bak_path), "%s.bak%d", PC_GCI_PATH, b);
            if (stat(bak_path, &st) == 0) {
                OSReport("[PC] Found backup save '%s', recovering...\n", bak_path);
                if (pc_save_read_gci(bak_path)) {
                    OSReport("[PC] Recovered save from backup %d\n", b);
                    return TRUE;
                }
            }
        }
    }

    OSReport("[PC] No save file found\n");
    return FALSE;
}

/* --- Card B scanning --- */

/* Check if Card B directory has a valid AC town GCI */
static int pc_card_b_find_town(void) {
    if (pc_card_scan_for_gci(1, l_card_b_gci_path, sizeof(l_card_b_gci_path))) {
        OSReport("[PC] Card B: found GCI at '%s'\n", l_card_b_gci_path);
        return TRUE;
    }
    l_card_b_gci_path[0] = '\0';
    return FALSE;
}

/* --- Memory card API implementation --- */

void mCD_init_card(void) {
    CARDInit();
}

void mCD_InitAll(void) {
    /* ARAM blocks must persist — don't null them */
    memset(&l_mcd_foreigner_file, 0, sizeof(l_mcd_foreigner_file));
    l_keepSave_set = FALSE;
    l_mcd_keep_startCond = 0;
    l_card_b_gci_path[0] = '\0';
}

int mCD_InitGameStart_bg(int player_no, int card_private_idx, int start_cond, s32* mounted_chan) {
    static int init_done = 0;

    /* On GC, save is re-read from the memory card each game start.
     * On PC, the save was already reloaded from disk in common_data_reinit
     * and aAL_title_game_data_init_start_select. We just need to allow
     * mSDI_StartDataInit to re-process it. */
    if (init_done && pc_save_loaded) {
        init_done = 0;
    }

    if (!init_done) {
        init_done = 1; /* before call — prevents re-entry via crash recovery */

        if (pc_save_loaded) {
            static int init_mode_table[] = { mSDI_INIT_MODE_NEW, mSDI_INIT_MODE_FROM,
                                             mSDI_INIT_MODE_NEW_PLAYER, mSDI_INIT_MODE_PAK,
                                             mSDI_INIT_MODE_PAK };
            int mode = mSDI_INIT_MODE_FROM;
            if (start_cond >= 0 && start_cond < 5) {
                mode = init_mode_table[start_cond];
            }
            mSDI_StartDataInit(gamePT, player_no, mode);
            l_mcd_keep_startCond = start_cond;
            pc_save_ready = 1;

            /* Reset detection (Resetti): check if previous session ended without saving */
            if (Now_Private != NULL && start_cond != mCD_START_COND_INCOMING_FOREIGNER
                && start_cond != mCD_START_COND_OUTGOING_FOREIGNER) {
                Common_Set(reset_flag, FALSE);
                if (Now_Private->reset_code != 0) {
                    /* Birthday clears reset penalty (matches original) */
                    if (Now_Private->state_flags & mPr_FLAG_BIRTHDAY_ACTIVE) {
                        Now_Private->reset_code = 0;
                    } else if (!ZURUMODE2_ENABLED() && !g_pc_settings.disable_resetti) {
                        Common_Set(reset_flag, TRUE);
                        Now_Private->reset_count++;
                        OSReport("[PC] Reset detected! reset_count=%d — Resetti will appear\n",
                                 Now_Private->reset_count);
                    }
                }
                /* Arm reset code: if player quits without saving, next load detects it */
                pc_set_reset_code(Now_Private);

                /* GC writes the save (armed code included) back to the card
                 * here (bg_write_main/bg_write_bk). Persist to disk or the
                 * armed code never survives a quit and Resetti can't trigger.
                 * Cond 1 only - matches GC (new players aren't saved yet). */
                if (start_cond == mCD_START_COND_1) {
                    u16 copy_protect = pc_get_land_copy_protect();
                    Common_Set(copy_protect, copy_protect);
                    Save_Set(copy_protect, copy_protect);
                    Save_Set(travel_hard_time, lbRTC_HardTime());
                    if (!pc_save_write_gci()) {
                        OSReport("[PC] InitGameStart: reset-code persist failed\n");
                    }
                }
            }

            /* Handle foreigner start conditions */
            if (start_cond == mCD_START_COND_INCOMING_FOREIGNER) {
                /* Arriving at another town as foreigner — point now_private to passport */
                Common_Set(now_private, &l_mcd_foreigner_file.file.priv);
                Common_Set(player_no, mPr_FOREIGNER);
                OSReport("[PC] InitGameStart: INCOMING_FOREIGNER — player is visiting\n");
            } else if (start_cond == mCD_START_COND_OUTGOING_FOREIGNER) {
                /* Returning home. Matches m_card.c:4780 (GC case 4): merge
                 * the session passport back into the matching home save slot
                 * — this restores player_no/now_private and carries visit
                 * inventory changes into the home save. */
                Private_c* foreigner = mPr_GetForeignerP();
                mPr_CopyPrivateInfo(foreigner, &l_mcd_foreigner_file.file.priv);
                mPr_LoadPak_and_SetPrivateInfo2(foreigner, (u8)player_no);
                mHm_SetNowHome();

                /* Cond 4 fires on both train rides; write card A only after
                 * a real home landing (merge left player_no in a home slot). */
                if (Common_Get(player_no) != mPr_FOREIGNER && Now_Private != NULL) {
                    u16 copy_protect = pc_get_land_copy_protect();
                    pc_set_reset_code(Now_Private);
                    Common_Set(copy_protect, copy_protect);
                    Save_Set(copy_protect, copy_protect);
                    Save_Set(travel_hard_time, lbRTC_HardTime());
                    if (!pc_save_write_gci()) {
                        OSReport("[PC] InitGameStart: return-home persist failed\n");
                        if (mounted_chan) *mounted_chan = mCD_SLOT_A;
                        return mCD_TRANS_ERR_IOERROR;
                    }
                }
                OSReport("[PC] InitGameStart: OUTGOING_FOREIGNER — landed player_no=%d\n",
                         Common_Get(player_no));
            }
        } else if (start_cond == mCD_START_COND_0 || start_cond == mCD_START_COND_2) {
            mSDI_StartDataInit(gamePT, player_no, mSDI_INIT_MODE_NEW);
            pc_save_ready = 1;
        }
    }

    if (mounted_chan) *mounted_chan = mCD_SLOT_A;
    return mCD_TRANS_ERR_NONE;
}

void mCD_LoadLand(void) {
    (void)pc_save_loaded;
}

int mCD_SaveHome_bg(int param_1, int* chan) {
    int slot = mCD_GetThisLandSlotNo();
    int result;


    pc_save_pre_write_side_effects(param_1);

    if (slot == mCD_SLOT_B && l_card_b_gci_path[0] != '\0') {
        /* Visiting Card B's town — save to Card B GCI */
        char tmp_path[300];
        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", l_card_b_gci_path);
        result = pc_save_write_gci_to(l_card_b_gci_path, tmp_path);
        if (chan) *chan = mCD_SLOT_B;
    } else {
        result = pc_save_write_gci();
        if (chan) *chan = mCD_SLOT_A;
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
        char tmp_path[300];
        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", l_card_b_gci_path);
        result = pc_save_write_gci_to(l_card_b_gci_path, tmp_path);
    } else {
        result = pc_save_write_gci();
    }
    return result;
}

/* --- Travel / Station functions --- */

/* Read a GCI's Save_t into `out` (byte-swapped). Returns TRUE on success. */
static int pc_read_gci_land_info(const char* path, Save_t* out) {
    FILE* fp;
    CARDDir hdr;
    u8* file_data;
    int ok = FALSE;

    fp = fopen(path, "rb");
    if (!fp) return FALSE;

    if (fread(&hdr, GCI_HEADER_SIZE, 1, fp) == 1 &&
        memcmp(hdr.gameName, "GAF", 3) == 0) {
        file_data = (u8*)malloc(GCI_FILE_DATA_SIZE);
        if (file_data) {
            if (fread(file_data, GCI_FILE_DATA_SIZE, 1, fp) == 1) {
                Save_t* save_src = (Save_t*)(file_data + GCI_SAVE_MAIN_OFFSET);
                memcpy(out, save_src, sizeof(Save_t));
                pc_save_bswap(out, PC_BSWAP_FROM_BE);
                ok = TRUE;
            }
            free(file_data);
        }
    }
    fclose(fp);
    return ok;
}

/* Scan the "other" card for a travel-eligible town.
 *  - Resident: scan Card B for a different town.
 *  - Foreigner: scan Card A for the home town. */
int mCD_CheckStation_bg(s32* chan) {
    int is_foreigner = mLd_PlayerManKindCheck();

    if (is_foreigner) {
        Save_t temp_save;
        if (chan) *chan = mCD_SLOT_B;
        if (pc_read_gci_land_info(PC_GCI_PATH, &temp_save)) {
            if (mLd_CheckId(temp_save.land_info.id) &&
                !mLd_CheckThisLand(temp_save.land_info.name, temp_save.land_info.id)) {
                OSReport("[PC] CheckStation: Card A has home town '%.*s' (id=0x%04X) — return available\n",
                         8, temp_save.land_info.name, temp_save.land_info.id);
                if (chan) *chan = mCD_SLOT_A;
                return mCD_TRANS_ERR_NONE_NEXTLAND;
            }
            OSReport("[PC] CheckStation: Card A has same town as current (unexpected)\n");
        } else {
            OSReport("[PC] CheckStation: could not read Card A save\n");
        }
        return mCD_TRANS_ERR_NONE;
    }

    if (chan) *chan = mCD_SLOT_A;
    if (pc_card_b_find_town()) {
        Save_t temp_save;
        if (pc_read_gci_land_info(l_card_b_gci_path, &temp_save)) {
            if (mLd_CheckId(temp_save.land_info.id)) {
                if (!mLd_CheckThisLand(temp_save.land_info.name, temp_save.land_info.id)) {
                    OSReport("[PC] CheckStation: Card B has town '%.*s' (id=0x%04X) — travel available\n",
                             8, temp_save.land_info.name, temp_save.land_info.id);
                    if (chan) *chan = mCD_SLOT_B;
                    return mCD_TRANS_ERR_NONE_NEXTLAND;
                }
                OSReport("[PC] CheckStation: Card B has same town as Card A\n");
            } else {
                OSReport("[PC] CheckStation: Card B has invalid land_info\n");
            }
        }
    }

    return mCD_TRANS_ERR_NONE;
}

/* Persist current town and load the "other" town into l_keepSave.
 *  - Resident: save home (Card A, marked away) + load Card B → l_keepSave.
 *  - Foreigner: save visited town (Card B) + load Card A → l_keepSave. */
int mCD_SaveStation_NextLand_bg(s32* chan) {
    int is_foreigner = mLd_PlayerManKindCheck();

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
                mPr_CopyPrivateInfo(&l_mcd_foreigner_file.file.priv, current_priv);
                l_mcd_foreigner_file.file.checksum = 0;
                l_mcd_foreigner_file.file.checksum =
                    mFRm_GetFlatCheckSum((u16*)&l_mcd_foreigner_file.file,
                                         sizeof(mCD_foreigner_c),
                                         l_mcd_foreigner_file.file.checksum);
                OSReport("[PC] SaveStation_NextLand(return): refreshed passport for '%.*s'\n",
                         PLAYER_NAME_LEN, l_mcd_foreigner_file.file.priv.player_ID.player_name);
            }
        }

        /* Persist visited-town state to its Card B GCI. */
        if (l_card_b_gci_path[0] != '\0') {
            char tmp_path[320];
            snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", l_card_b_gci_path);
            if (!pc_save_write_gci_to(l_card_b_gci_path, tmp_path)) {
                OSReport("[PC] SaveStation_NextLand(return): failed to save visited town\n");
                if (chan) *chan = mCD_SLOT_B;
                return mCD_TRANS_ERR_IOERROR;
            }
        } else {
            OSReport("[PC] SaveStation_NextLand(return): no Card B path cached\n");
        }

        if (!pc_save_read_gci_to_keep(PC_GCI_PATH)) {
            OSReport("[PC] SaveStation_NextLand(return): failed to load home town\n");
            if (chan) *chan = mCD_SLOT_A;
            return mCD_TRANS_ERR_CORRUPT;
        }

        l_mcd_keep_startCond = mCD_START_COND_OUTGOING_FOREIGNER;

        if (chan) *chan = mCD_SLOT_A;
        return mCD_TRANS_ERR_NONE;
    }

    if (chan) *chan = mCD_SLOT_A;

    /* Must have a Card B town path from prior CheckStation */
    if (l_card_b_gci_path[0] == '\0') {
        OSReport("[PC] SaveStation_NextLand: no Card B path\n");
        return mCD_TRANS_ERR_NO_TOWN_DATA;
    }

    /* 0. Record home town info for Rover's dialogue (departure town name) */
    {
        mCD_persistent_data_c* persistant = Common_GetPointer(travel_persistent_data);
        int i;
        memcpy(&persistant->land, Save_GetPointer(land_info), sizeof(mLd_land_info_c));
        for (i = 0; i < PLAYER_NUM; i++) {
            mPr_CopyPersonalID(&persistant->pid[i], &Save_Get(private_data[i]).player_ID);
        }
    }

    /* 1. Build passport BEFORE marking player as away (need full player data) */
    {
        Private_c* current_priv = Now_Private;
        remove(PC_PASSPORT_PATH); /* a train trip: no online visit's passport may bring this player home */
        memset(&l_mcd_foreigner_file, 0, sizeof(l_mcd_foreigner_file));
        if (current_priv) {
            mPr_CopyPrivateInfo(&l_mcd_foreigner_file.file.priv, current_priv);
        }
        memset(&l_mcd_foreigner_file.file.remove_animal, 0, sizeof(Animal_c));
        l_mcd_foreigner_file.file.copy_protect = (u16)Common_Get(copy_protect);
        l_mcd_foreigner_file.file.checksum = 0;
        l_mcd_foreigner_file.file.checksum =
            mFRm_GetFlatCheckSum((u16*)&l_mcd_foreigner_file.file,
                                 sizeof(mCD_foreigner_c),
                                 l_mcd_foreigner_file.file.checksum);
        OSReport("[PC] SaveStation_NextLand: built passport for '%.*s'\n",
                 PLAYER_NAME_LEN, l_mcd_foreigner_file.file.priv.player_ID.player_name);
    }

    /* 2. Mark player as "away" and clear reset code before saving Card A.
     * If the player quits during the visit, next load sees exists==FALSE
     * → gyroid face + inventory cleared as punishment (m_start_data_init.c:426) */
    if (Now_Private != NULL && mLd_PlayerManKindCheckNo(Common_Get(player_no)) == FALSE) {
        Now_Private->exists = FALSE;
        Now_Private->reset_code = 0;
        OSReport("[PC] SaveStation_NextLand: marked player as away (exists=FALSE)\n");
    }

    /* 3. Save home town to Card A (with player marked as away) */
    if (!pc_save_write_gci()) {
        /* Restore player state on failure */
        if (Now_Private != NULL) Now_Private->exists = TRUE;
        OSReport("[PC] SaveStation_NextLand: failed to save home town\n");
        return mCD_TRANS_ERR_IOERROR;
    }

    /* 3. Load other town from Card B into l_keepSave + l_keep* ARAM blocks */
    if (!pc_save_read_gci_to_keep(l_card_b_gci_path)) {
        OSReport("[PC] SaveStation_NextLand: failed to load Card B town\n");
        return mCD_TRANS_ERR_CORRUPT;
    }

    l_mcd_keep_startCond = mCD_START_COND_INCOMING_FOREIGNER;

    if (chan) *chan = mCD_SLOT_B;
    return mCD_TRANS_ERR_NONE;
}

/* Save passport file on Card B (simplified for PC).
 * On GC this creates a separate GCI file; on PC the passport is just in memory. */
int mCD_SaveStation_Passport_bg(s32* chan) {
    if (chan) *chan = mCD_SLOT_B;

    /* The passport is already built in l_mcd_foreigner_file from SaveStation_NextLand_bg.
     * On PC we don't need to write a separate passport GCI file — the foreigner data
     * persists in memory through the town transition (mCD_toNextLand). */
    OSReport("[PC] SaveStation_Passport: passport ready in memory\n");
    return mCD_TRANS_ERR_NONE;
}

/* Transition to the other town. Called from scene cleanup when switching to the visited town.
 * Faithfully reproduces the original mCD_toNextLand from m_card.c:7188. */
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
        OSReport("[PC] toNextLand: l_keepSave not set, aborting\n");
        return;
    }

    land_info = &save->land_info;
    if (!mLd_CheckId(land_info->id)) {
        OSReport("[PC] toNextLand: invalid land_info in l_keepSave\n");
        return;
    }

    scene_no = Save_Get(scene_no);

    /* Save persistent data that must survive the common_data wipe */
    memcpy(&persis, Common_GetPointer(travel_persistent_data), sizeof(persis));
    last_scene_no = Common_Get(last_scene_no);
    last_field_id = Common_Get(last_field_id);
    memcpy(demo_profile, Common_Get(demo_profiles), sizeof(demo_profile));
    memcpy(&time, Common_GetPointer(time), sizeof(time));
    memcpy(&transition, Common_GetPointer(transition), sizeof(transition));

    /* Wipe common_data */
    memset(&common_data, 0, sizeof(common_data_t));

    /* Restore persistent fields */
    memcpy(Common_GetPointer(travel_persistent_data), &persis, sizeof(persis));
    Common_Set(last_field_id, last_field_id);
    Common_Set(last_scene_no, last_scene_no);
    memcpy(Common_Get(demo_profiles), demo_profile, sizeof(demo_profile));
    memcpy(Common_GetPointer(time), &time, sizeof(time));
    memcpy(Common_GetPointer(transition), &transition, sizeof(transition));

    /* Load the other town's save data */
    memcpy(Common_GetPointer(save), save, sizeof(Save));
    /* and its ac-plus companion save (none for a town visited, so its ground has none of home's patterns and its
     * houses none of home's rooms; home's again on the way back, before anything's saved there) */
    pc_ext_save_load(l_keepSave_path);

    Common_Set(copy_protect, Save_Get(copy_protect));
    Save_Set(scene_no, scene_no);

    /* RTC check */
    rtc_enabled = Common_Get(time.rtc_enabled);
    Common_Set(time.rtc_enabled, TRUE);
    mTM_rtcTime_limit_check();
    Common_Set(time.rtc_enabled, rtc_enabled);
    lbRTC_GetTime(Common_GetPointer(time.rtc_time));

    /* Set current player as foreigner */
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

    /* Clear keepSave now that it's been applied */
    memset(&l_keepSave, 0, sizeof(Save));
    l_keepSave_set = FALSE;

    /* Load ARAM blocks from the other town */
    mCD_save_data_main_to_aram(&l_keepMail, sizeof(l_keepMail), mCD_ARAM_DATA_MAIL);
    mCD_save_data_main_to_aram(&l_keepOriginal, sizeof(l_keepOriginal), mCD_ARAM_DATA_ORIGINAL);
    mCD_save_data_main_to_aram(&l_keepDiary, sizeof(l_keepDiary), mCD_ARAM_DATA_DIARY);

    OSReport("[PC] toNextLand: transitioned to visited town, player_no=%d\n",
             Common_Get(player_no));
}

/* Re-check and reload home town after returning from visit */
void mCD_ReCheckLoadLand(GAME_PLAY* play) {
    int scene = Save_Get(scene_no);

    /* Reload home town from Card A */
    pc_save_check_and_load();
    Save_Set(scene_no, scene);

    if (mFRm_CheckSaveData()) {
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

    OSReport("[PC] ReCheckLoadLand: next_scene=%d\n", play->next_scene_no);
}

/* --- Remaining card management functions --- */

int mCD_GetThisLandSlotNo(void) {
    /* If visiting another town, current land is on Card B */
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
    /* If foreigner mode is active, player 4 (mPr_FOREIGNER) is not native */
    if (Common_Get(player_no) == mPr_FOREIGNER && idx == mPr_FOREIGNER) {
        return FALSE;
    }
    return TRUE;
}

int mCD_CheckPassportFile(void) {
    /* Check if there's a passport in memory (from a previous travel) */
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
    /* Clear the in-memory passport */
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

/* --- Online visits (multiplayer, pc_mp.c) ---
 * The GameCube trip with a passport, with the other town coming from the host over the network (already checked
 * and built by pc_mp_town.c) instead of from card B, and no train: the scene change after these goes straight to
 * the town's station, and the start-of-town setup that the train's arrival scene does runs right after the swap
 * (pc_mcd_online_after_swap, from m_play.c). The visited town is never written anywhere: no card B path. */

static int l_online_pending = 0; /* start condition to run after the swap, 0 = none */
static int l_online_home_player = 0;

#define PC_ONLINE_KEEP_PATH "save/online_visit.gci" /* never exists: the visited town has no companion save */

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
    FILE* fp = fopen(PC_PASSPORT_PATH, "rb");
    Private_c* priv;
    u16 sum;
    int ok;

    if (fp == NULL) {
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
    if (!pc_save_read_gci_to_keep(PC_GCI_PATH)) {
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
        /* the swap wiped the door we left by: arrive at the station, where just off the platform, south of the tracks
         * (ac_npc_p_sel2_talk.c_inc's station door; goto_other_scene stores next_scene_id + 1) */
        Door_data_c station = { SCENE_FG, mSc_DIRECT_SOUTH, FALSE, 0, { 1979, 0, 840 }, EMPTY_NO, 0, { 0, 0, 0 } };
        Common_Set(door_data, station);
        Common_Get(door_data).next_scene_id = SCENE_FG + 1;
    }
    OSReport("[PC] online visit: arrived, player_no=%d\n", Common_Get(player_no));
}
