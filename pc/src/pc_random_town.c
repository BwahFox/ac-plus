/* pc_random_town.c - a town nobody lives in yet (experimental): Porter's "Somewhere new" (pc_mp.c) takes the
 * player by train to a freshly made town, visited as the GameCube visits a town on another memory card: the town is
 * written where Card B's town would be (PC_RANDOM_TOWN_PATH, pc_mcd_card_b_override) and the GameCube's own trip
 * does the rest: the ride, the player a visitor there, and Porter's way home. The next trip makes another.
 *
 * The town is made as a new game makes one (mSDI's new-town setup: land, villagers, shops, island), in the game's
 * own data, which is put back as it was right after: only the town's save comes out of it.
 *
 * It's a reward for the end of the house: Porter offers it once the player's last loan is paid and their statue is
 * up (pc_random_town_unlocked), not before. */

#include "pc_random_town.h"
#include "pc_nlrooms.h"
#include "pc_town_map.h"
#include "m_common_data.h"
#include "m_land.h"
#include "m_private.h"
#include "m_home.h"
#include "m_house.h"
#include "m_font.h"
#include "m_start_data_init.h"
#include "sys_math.h"
#include "m_flashrom.h"
#include "m_npc.h"
#include "m_time.h"

#include <dolphin/os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* Names: two halves, as town names go */
static const char* const l_head[] = { "Sun", "Moss", "Pine", "Clover", "Maple", "Brook", "Hazel", "Fern",
                                      "Willow", "Coral", "Amber", "Misty", "Pebble", "Juni", "Holly", "Tulip" };
static const char* const l_tail[] = { "ville", "dale", "wood", "port", "bury", "field", "glen", "ton",
                                      "brook", "haven", "moor", "crest" };

static void pc_rt_name(u8* out) {
    char name[32];
    int i;

    for (;;) {
        snprintf(name, sizeof(name), "%s%s", l_head[RANDOM(sizeof(l_head) / sizeof(l_head[0]))],
                 l_tail[RANDOM(sizeof(l_tail) / sizeof(l_tail[0]))]);
        if (strlen(name) <= LAND_NAME_SIZE) {
            break;
        }
    }
    for (i = 0; i < LAND_NAME_SIZE; i++) {
        out[i] = i < (int)strlen(name) ? (u8)name[i] : CHAR_SPACE;
    }
}

int pc_random_town_unlocked(void) {
    mHm_hs_c* home;

    if (getenv("PC_RANDOM_TOWN_ANY") != NULL) {
        return TRUE; /* tests */
    }
    if (Common_Get(player_no) >= PLAYER_NUM) {
        return FALSE; /* a visitor */
    }
    home = &Save_Get(homes)[mHS_get_arrange_idx(Common_Get(player_no))];
    return mPr_CheckCmpPersonalID(&home->ownerID, &Now_Private->player_ID) == TRUE &&
           (home->size_info.size >= mHm_HOMESIZE_STATUE || home->size_info.next_size >= mHm_HOMESIZE_STATUE);
    /* (the statue is next_size: the house's own size stays at its last floor, mHm_CheckRehouseOrder) */
}

Save_t* pc_random_town_make(GAME* game) {
    common_data_t* keep = (common_data_t*)malloc(sizeof(common_data_t));
    Save_t* town = (Save_t*)malloc(sizeof(Save_t));
    void* rooms;
    int i;

    if (keep == NULL || town == NULL) {
        free(keep);
        free(town);
        return NULL;
    }
    memcpy(keep, &common_data, sizeof(common_data_t));
    rooms = pc_nlr_keep(); /* the setup below clears every house's extra rooms too, which live outside common_data */
    if (mSDI_StartInitBefore(game, 0, mSDI_INIT_MODE_NEW, mSDI_MALLOC_FLAG_ZELDA) == TRUE) {
        mSDI_StartInitAfter(game, TRUE, mSDI_MALLOC_FLAG_ZELDA);
    }
    pc_rt_name(Save_Get(land_info).name);
    do {
        Save_Get(land_info).id = (u16)(mLd_BITMASK | (RANDOM(0xFE) + 1)); /* a town id, not the player's own town's */
    } while (Save_Get(land_info).id == keep->save.save.land_info.id);
    Save_Get(land_info).exists = TRUE;
    /* nobody lives there: the new game's player and their house go */
    for (i = 0; i < PLAYER_NUM; i++) {
        mPr_ClearPrivateInfo(Save_GetPointer(private_data[i]));
        mPr_ClearPersonalID(&Save_Get(homes[i]).ownerID);
    }
    /* its villagers have always lived there: none is new in town (a villager who moved in says where from, and
     * these have no old town to name), and nobody moves in while the player's there (mNpc_Grow counts from now) */
    for (i = 0; i < ANIMAL_NUM_MAX; i++) {
        Save_Get(animals[i]).moved_in = FALSE;
    }
    lbRTC_TimeCopy(Save_GetPointer(last_grow_time), Common_GetPointer(time.rtc_time));
    {
        /* checked as a save read from a card is, before the train's swap takes it (mCD_toNextLand) */
        static Save whole; /* the sector-sized save the checksum covers */

        memset(&whole, 0, sizeof(whole));
        memcpy(&whole.save, Common_GetPointer(save.save), sizeof(Save_t));
        whole.save.save_check.version = mFRm_VERSION;
        mFRm_SetSaveCheckData(&whole.save.save_check);
        whole.save.save_check.checksum = 0;
        whole.save.save_check.checksum = mFRm_GetFlatCheckSum((u16*)&whole, sizeof(Save), 0);
        memcpy(town, &whole.save, sizeof(Save_t));
    }
    memcpy(&common_data, keep, sizeof(common_data_t)); /* the game as it was */
    pc_nlr_put_back(rooms);
    pc_town_map_new_town(FALSE); /* (the setup took this for a brand-new town) */
    free(keep);
    OSReport("[random town] made %.8s\n", town->land_info.name);
    return town;
}
