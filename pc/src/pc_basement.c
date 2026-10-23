/* pc_basement.c - wallpaper and carpets in the house basement (ac_my_indoor.c draws them).
 *
 * The GameCube's basement is one model (rom_myhome_ug) with its own rock walls and floor, and it refused
 * wallpaper and carpets. ac-plus takes them: the basement's main area is the same 8x8 as the L room upstairs,
 * so its walls and floor (rom_myhome3) carry them there. The save already has a wall/floor pair for the
 * basement (homes[].floors[2]); what it lacks is whether the player has put anything down yet, since 0 is a
 * real wallpaper and carpet. That's kept here, per house, as BSMT in the ac-plus save: until then the
 * basement looks as it always did. */
#include "pc_basement.h"
#include "m_common_data.h"

#include <stdio.h>
#include <string.h>

static u8 l_custom[4]; /* per house: PC_BASEMENT_WALL | PC_BASEMENT_FLOOR */

int pc_basement_custom(int house, int what) {
    return house >= 0 && house < 4 && (l_custom[house] & what) != 0;
}

void pc_basement_set_custom(int house, int what) {
    if (house >= 0 && house < 4) {
        l_custom[house] |= (u8)what;
    }
}

void pc_basement_clear_custom(int house, int what) {
    if (house >= 0 && house < 4) {
        l_custom[house] &= (u8)~what;
    }
}

void pc_basement_reset(void) {
    memset(l_custom, 0, sizeof(l_custom));
}

int pc_basement_read_chunk(const char tag[4], void* fp, unsigned int size) {
    FILE* f = (FILE*)fp;

    if (memcmp(tag, "BSMT", 4) != 0) {
        return FALSE;
    }
    if (size != sizeof(l_custom) || fread(l_custom, 1, size, f) != size) {
        pc_basement_reset();
        if (size != sizeof(l_custom)) {
            fseek(f, (long)size, SEEK_CUR);
        }
    }
    return TRUE;
}

int pc_basement_write_chunk(void* fp) {
    FILE* f = (FILE*)fp;
    u32 size = sizeof(l_custom);

    return fwrite("BSMT", 1, 4, f) == 4 && fwrite(&size, 4, 1, f) == 1 && fwrite(l_custom, 1, size, f) == size;
}

/* dev (pc_devtest.c PC_HOUSE_DUMP_AT): each house's size, basement and the basement's wallpaper/carpet state,
 * and Nook's shop progress */
void pc_basement_dev_dump(void) {
    int h;

    for (h = 0; h < PLAYER_NUM; h++) {
        printf("[House] %d: size %d, basement %d, basement wall/carpet set %d/%d\n", h,
               Save_Get(homes[h]).size_info.size, Save_Get(homes[h]).flags.has_basement,
               pc_basement_custom(h, PC_BASEMENT_WALL), pc_basement_custom(h, PC_BASEMENT_FLOOR));
    }
    printf("[Shop] level %d, sales sum %u, visitor %d\n", Save_Get(shop).shop_info.shop_level,
           (unsigned)Save_Get(shop).sales_sum, Save_Get(shop).visitor_flag);
    fflush(stdout);
}
