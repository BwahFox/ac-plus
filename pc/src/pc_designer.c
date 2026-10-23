/* pc_designer.c - pattern materials: design patterns anywhere, and pattern
 * storage from anywhere.
 *
 * Patterns finished at the Able Sisters are counted. From the fifth on, the
 * sisters sell materials: "Design a Pattern" appears in the pause menu, and it
 * runs the same steps as their counter (pick a slot, edit, name it) for the
 * same 350 Bells.
 *
 * "Pattern Storage" opens the 96-design storage Mabel offers ("save designs to
 * the memory card": 8 folders of 12), so patterns can be swapped in and out of
 * the 8 slots without visiting the shop. It is the game's own screen and data.
 *
 * The count lives in the first spare byte at the end of the player's data
 * (Private_c.unused_2412), which the game never touches, so the save stays
 * GameCube compatible. */
#include "m_common_data.h"
#include "m_play.h"
#include "m_event.h"
#include "m_player.h"
#include "m_player_lib.h"
#include "m_submenu.h"
#include "m_needlework.h"
#include "m_needlework_ovl.h"
#include "m_ledit_ovl.h"
#include "m_shop.h"
#include "m_name_table.h"
#include "m_cporiginal_ovl.h"
#include "ac_npc_needlework.h"
#include "pc_pattern.h"

#include <string.h>

#define DESIGNER_UNLOCK_PATTERNS 5
#define DESIGNER_COUNT(priv) ((priv)->unused_2412[0])
/* Frames to wait for a requested submenu to actually open before giving up. */
#define DESIGNER_OPEN_TIMEOUT 120

void pc_quick_save_show_message(const char* msg);

enum {
    DESIGNER_IDLE,
    DESIGNER_PICK_OPENING,
    DESIGNER_PICK_OPEN,
    DESIGNER_EDIT_OPENING,
    DESIGNER_EDIT_OPEN,
    DESIGNER_NAME_OPENING,
    DESIGNER_NAME_OPEN,
    STORAGE_OPENING,
    STORAGE_OPEN,
};

static int l_state = DESIGNER_IDLE;
static int l_requested = FALSE;
static int l_dev_edit = FALSE;
static int l_storage_requested = FALSE;
static int l_wait = 0;
static int l_image = 0;
static u8 l_name[mNW_ORIGINAL_DESIGN_NAME_LEN];

static Private_c* resident(void) {
    if (Common_Get(player_no) >= PLAYER_NUM) {
        return NULL; /* visitors' patterns live in their own town */
    }
    return Now_Private;
}

/* ac_npc_needlework: a pattern was just made and paid for at the counter. */
void pc_designer_count_pattern(void) {
    Private_c* priv = resident();

    if (priv == NULL || DESIGNER_COUNT(priv) >= 255) {
        return;
    }
    DESIGNER_COUNT(priv)++;
    if (DESIGNER_COUNT(priv) == DESIGNER_UNLOCK_PATTERNS) {
        pc_quick_save_show_message("Pattern materials unlocked! Design from the pause menu.");
    }
}

/* Pause menu: show the entry? */
int pc_designer_unlocked(void) {
    Private_c* priv = resident();

    return priv != NULL && DESIGNER_COUNT(priv) >= DESIGNER_UNLOCK_PATTERNS;
}

/* Pause menu: picked. Handled by pc_designer_tick once the game runs again. */
void pc_designer_request(void) {
    l_requested = TRUE;
}

/* Pause menu: show "Pattern Storage"? Any resident can use it. */
int pc_pattern_storage_available(void) {
    return resident() != NULL;
}

/* Dev harness (PC_DESIGN_EDIT_AT): open the editor on slot 0 without the slot picker. */
void pc_designer_dev_edit(void) {
    l_dev_edit = TRUE;
}

void pc_pattern_storage_request(void) {
    l_storage_requested = TRUE;
}

/* Put the worn pattern on again so a changed design shows (as Mabel does). */
static void redress(GAME_PLAY* play, Private_c* priv) {
    mPlib_request_main_change_cloth_forNPC_type1((GAME*)play, RSV_CLOTH, priv->cloth.idx, TRUE);
}

static int can_open_here(GAME_PLAY* play) {
    return play->submenu.process_status == mSM_PROCESS_WAIT && !play->submenu.open_flag &&
           play->fb_fade_type == FADE_TYPE_NONE && play->fb_wipe_mode == WIPE_MODE_NONE &&
           mPlib_able_submenu_type1((GAME*)play) && !mEv_CheckFirstIntro();
}

/* Wait for a submenu we asked for to open. */
static void wait_open(GAME_PLAY* play, int next_state) {
    if (play->submenu.open_flag) {
        l_state = next_state;
    } else if (++l_wait > DESIGNER_OPEN_TIMEOUT) {
        l_state = DESIGNER_IDLE;
    }
}

/* m_play.c, every frame just before the submenu system runs. */
void pc_designer_tick(GAME_PLAY* play) {
    Submenu* submenu = &play->submenu;
    Private_c* priv = resident();

    pc_pattern_tick(submenu->open_flag);
    if (priv == NULL) {
        l_state = DESIGNER_IDLE;
        l_requested = FALSE;
        l_storage_requested = FALSE;
        return;
    }

    switch (l_state) {
        case DESIGNER_IDLE:
            if (l_dev_edit && can_open_here(play)) { /* dev harness: straight into the editor */
                l_dev_edit = FALSE;
                mSM_open_submenu(submenu, mSM_OVL_DESIGN, 0, 0);
                break;
            }
            if (l_storage_requested) {
                l_storage_requested = FALSE;
                if (!can_open_here(play)) {
                    pc_quick_save_show_message("You can't open pattern storage right now.");
                    break;
                }
                mSM_open_submenu(submenu, mSM_OVL_NEEDLEWORK, mNW_OPEN_CPORIGINAL, 0);
                l_wait = 0;
                l_state = STORAGE_OPENING;
                break;
            }
            if (!l_requested) {
                break;
            }
            l_requested = FALSE;
            if (!pc_designer_unlocked()) {
                break;
            }
            if (!can_open_here(play)) {
                pc_quick_save_show_message("You can't design a pattern right now.");
                break;
            }
            if (priv->inventory.wallet < aNNW_DESIGN_PRICE) {
                pc_quick_save_show_message("Pattern materials cost 350 Bells.");
                break;
            }
            mSM_open_submenu_new2(submenu, mSM_OVL_NEEDLEWORK, 0, Common_Get(player_no), NULL, mNW_OPEN_DESIGN);
            l_wait = 0;
            l_state = DESIGNER_PICK_OPENING;
            break;

        case DESIGNER_PICK_OPENING:
            wait_open(play, DESIGNER_PICK_OPEN);
            break;

        case DESIGNER_PICK_OPEN:
            if (submenu->open_flag) {
                break;
            }
            if (submenu->item_p->item != RSV_NO) {
                l_state = DESIGNER_IDLE; /* backed out of the slot picker */
                break;
            }
            l_image = mNW_get_image_no(submenu, submenu->item_p->slot_no);
            mSM_open_submenu(submenu, mSM_OVL_DESIGN, submenu->item_p->slot_no, 0);
            l_wait = 0;
            l_state = DESIGNER_EDIT_OPENING;
            break;

        case DESIGNER_EDIT_OPENING:
            wait_open(play, DESIGNER_EDIT_OPEN);
            break;

        case DESIGNER_EDIT_OPEN:
            if (submenu->open_flag) {
                break;
            }
            if (submenu->item_p->slot_no != 1) {
                l_state = DESIGNER_IDLE; /* quit without saving: nothing used, nothing paid */
                break;
            }
            /* Saved. Name it, starting from the slot's current name. */
            memcpy(l_name, priv->my_org[l_image & 7].name, sizeof(l_name));
            mSM_open_submenu_new(submenu, mSM_OVL_LEDIT, mLE_TYPE_MYORIGINAL_NAME, 0, l_name);
            l_wait = 0;
            l_state = DESIGNER_NAME_OPENING;
            break;

        case DESIGNER_NAME_OPENING:
            wait_open(play, DESIGNER_NAME_OPEN);
            break;

        case DESIGNER_NAME_OPEN:
            if (submenu->open_flag) {
                break;
            }
            mSP_get_sell_price(aNNW_DESIGN_PRICE);
            mNW_OverWriteOriginalName(&priv->my_org[l_image & 7], l_name);
            pc_pattern_note_edit(Common_Get(player_no), l_image & 7);
            /* Wearing the pattern that changed: put it on again so the new design shows. */
            if ((CLOTH_NUM + 1) + l_image == priv->cloth.idx) {
                redress(play, priv);
            }
            l_state = DESIGNER_IDLE;
            break;

        case STORAGE_OPENING:
            wait_open(play, STORAGE_OPEN);
            break;

        case STORAGE_OPEN:
            if (submenu->open_flag) {
                break;
            }
            if (mCO_get_change_flg()) {
                redress(play, priv); /* the worn pattern was swapped out or changed */
            }
            l_state = DESIGNER_IDLE;
            break;
    }
}
