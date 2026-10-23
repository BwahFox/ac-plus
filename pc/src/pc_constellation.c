/* pc_constellation.c - player-made constellations. See pc_constellation.h. */
#include "pc_constellation.h"

#include "m_common_data.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PC_CONST_PI 3.14159265f
#define PC_CONST_COL_W (2.0f * PC_CONST_PI / PC_CONST_COLS)

static pc_constellation_c l_const[PC_CONST_MAX];
static u8 l_celeste[4]; /* met Celeste (bit per player), heard her rules (bit per player); saved as CELE */

/* Same as pc_sky.c's shader: a cell holds a star when hash 0 is above PC_CONST_STAR_ODDS */
#define PC_CONST_STAR_ODDS 0.84f

static f32 pc_const_hash(int col, int row, u32 k) {
    u32 h = (u32)col * 0x8DA6B343u + (u32)row * 0xD8163841u + k * 0xCB1AB31Fu;

    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    return (f32)(h & 0xFFFFFFu) / 16777216.0f;
}

int pc_const_star(int col, int row, f32* az, f32* up) {
    col = ((col % PC_CONST_COLS) + PC_CONST_COLS) % PC_CONST_COLS;
    if (row < 0 || pc_const_hash(col, row, 0) <= PC_CONST_STAR_ODDS) {
        return FALSE;
    }
    if (az != NULL) {
        *az = (col + 0.35f + 0.3f * pc_const_hash(col, row, 1)) * PC_CONST_COL_W;
    }
    if (up != NULL) {
        *up = (row + 0.35f + 0.3f * pc_const_hash(col, row, 2)) * PC_CONST_CELL;
    }
    return TRUE;
}

f32 pc_const_sky_turn(void) {
    static const u16 month_start[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    const lbRTC_time_c* rtc = &Common_Get(time.rtc_time);
    int month = rtc->month >= 1 && rtc->month <= 12 ? rtc->month : 1;
    f32 day = month_start[month - 1] + rtc->day - 1;
    f32 hour = rtc->hour + rtc->min / 60.0f + rtc->sec / 3600.0f;
    f32 turns = hour / 24.0f + day / 365.25f;

    return (turns - floorf(turns)) * 2.0f * PC_CONST_PI;
}

pc_constellation_c* pc_const_get(int i) {
    return i >= 0 && i < PC_CONST_MAX ? &l_const[i] : NULL;
}

int pc_const_count(void) {
    int i, n = 0;

    for (i = 0; i < PC_CONST_MAX; i++) {
        n += l_const[i].used != 0;
    }
    return n;
}

/* a - b around the circle, in -pi .. pi */
static f32 pc_const_wrap(f32 a) {
    a = fmodf(a + PC_CONST_PI, 2.0f * PC_CONST_PI);
    return (a < 0.0f ? a + 2.0f * PC_CONST_PI : a) - PC_CONST_PI;
}

int pc_const_sky_lines(f32 heading_sky, f32 range, f32 (*out)[4], int max) {
    int i, j, n = 0;

    for (i = 0; i < PC_CONST_MAX; i++) {
        const pc_constellation_c* c = &l_const[i];

        if (!c->used) {
            continue;
        }
        for (j = 0; j < c->link_num && j < PC_CONST_LINKS && n < max; j++) {
            f32 az0, up0, az1, up1;

            if (!pc_const_star(c->link[j][0].col, c->link[j][0].row, &az0, &up0) ||
                !pc_const_star(c->link[j][1].col, c->link[j][1].row, &az1, &up1) ||
                fabsf(pc_const_wrap(az0 - heading_sky)) > range) {
                continue;
            }
            /* keep both ends on the same side of the wrap */
            az1 = az0 + pc_const_wrap(az1 - az0);
            out[n][0] = az0;
            out[n][1] = up0;
            out[n][2] = az1;
            out[n][3] = up1;
            n++;
        }
    }
    return n;
}

/* dev: PC_CONST_DEV=1 keeps a constellation of the stars around the view in the last slot */
void pc_const_dev_make(f32 heading_sky) {
    pc_constellation_c* c = &l_const[PC_CONST_MAX - 1];
    int col0 = (int)(heading_sky / PC_CONST_COL_W) - 10;
    int col, row, prev_col = 0, prev_row = 0, have_prev = FALSE;

    static int dev = -1;

    if (dev < 0) {
        dev = getenv("PC_CONST_DEV") != NULL;
    }
    if (!dev) {
        return;
    }
    memset(c, 0, sizeof(*c));
    c->used = 1;
    memcpy(c->name, "Dev Dipper      ", PC_CONST_NAME_LEN);
    for (col = col0; col < col0 + 20 && c->link_num < PC_CONST_LINKS; col++) {
        for (row = PC_CONST_ROW_MAX; row >= PC_CONST_ROW_MIN + 3; row--) {
            if (pc_const_star(col, row, NULL, NULL)) {
                if (have_prev) {
                    c->link[c->link_num][0].col = (s16)(((prev_col % PC_CONST_COLS) + PC_CONST_COLS) % PC_CONST_COLS);
                    c->link[c->link_num][0].row = (s16)prev_row;
                    c->link[c->link_num][1].col = (s16)(((col % PC_CONST_COLS) + PC_CONST_COLS) % PC_CONST_COLS);
                    c->link[c->link_num][1].row = (s16)row;
                    c->link_num++;
                }
                prev_col = col;
                prev_row = row;
                have_prev = TRUE;
                break;
            }
        }
    }
}

void pc_const_reset(void) {
    memset(l_const, 0, sizeof(l_const));
    memset(l_celeste, 0, sizeof(l_celeste));
}

u8* pc_const_celeste_flags(void) {
    return l_celeste;
}

int pc_const_rtc_month(void) {
    return Common_Get(time.rtc_time).month;
}

int pc_const_rtc_day(void) {
    return Common_Get(time.rtc_time).day;
}

int pc_const_read_chunk(const char tag[4], FILE* f, unsigned int size) {
    if (memcmp(tag, "CELE", 4) == 0) {
        if (size != sizeof(l_celeste) || fread(l_celeste, 1, size, f) != size) {
            memset(l_celeste, 0, sizeof(l_celeste));
            if (size != sizeof(l_celeste)) {
                fseek(f, (long)size, SEEK_CUR);
            }
        }
        return TRUE;
    }
    if (memcmp(tag, "CONS", 4) != 0) {
        return FALSE;
    }
    if (size == sizeof(l_const)) {
        if (fread(l_const, 1, size, f) != size) {
            pc_const_reset();
        }
    } else {
        fseek(f, (long)size, SEEK_CUR);
    }
    return TRUE;
}

int pc_const_write_chunk(FILE* f) {
    u32 size = sizeof(l_const);

    u32 cele = sizeof(l_celeste);

    return fwrite("CONS", 1, 4, f) == 4 && fwrite(&size, 4, 1, f) == 1 && fwrite(l_const, 1, size, f) == size &&
           fwrite("CELE", 1, 4, f) == 4 && fwrite(&cele, 4, 1, f) == 1 && fwrite(l_celeste, 1, cele, f) == cele;
}
