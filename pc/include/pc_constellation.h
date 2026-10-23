#ifndef PC_CONSTELLATION_H
#define PC_CONSTELLATION_H

/* Player-made constellations, Wild World / City Folk style (pc_constellation.c).
 *
 * The night sky's stars (pc_sky.c) sit on a grid in sky coordinates: columns
 * around the horizon, rows going up, each cell holding a star or not, from a
 * hash both the shader and this file compute the same way. A constellation is
 * up to 16 links between stars. The sky turns once a day plus once a year, so
 * every constellation rises past the view at its own time of night and is best
 * seen in some months, as in City Folk. Saved in the .acplus companion (CONS). */

#include "types.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PC_CONST_MAX 12
#define PC_CONST_LINKS 16
#define PC_CONST_NAME_LEN 16

/* The star grid */
#define PC_CONST_CELL 0.012f /* radians per cell */
#define PC_CONST_COLS 524    /* around the horizon: 2 pi / PC_CONST_CELL, rounded */
#define PC_CONST_ROW_MIN 1   /* rows a constellation may use (row 0 fades into the horizon) */
#define PC_CONST_ROW_MAX 8 /* the sky C-stick up shows outdoors reaches about 0.12 rad */

typedef struct {
    s16 col; /* 0 .. PC_CONST_COLS-1 */
    s16 row;
} pc_const_star_c;

typedef struct {
    u8 used;
    u8 owner; /* player number */
    u8 name[PC_CONST_NAME_LEN]; /* game characters, space padded */
    u8 link_num;
    u8 pad;
    pc_const_star_c link[PC_CONST_LINKS][2];
} pc_constellation_c;

/* Does the cell hold a star, and where in sky coordinates (radians around, radians up) */
int pc_const_star(int col, int row, f32* az, f32* up);

/* How far the sky has turned now, radians (sky azimuth = view azimuth - this) */
f32 pc_const_sky_turn(void);

pc_constellation_c* pc_const_get(int i);
int pc_const_count(void);

/* Line segments for the sky shader near a view heading, as (az0, up0, az1, up1) in sky
 * coordinates; returns how many were written (at most max) */
int pc_const_sky_lines(f32 heading_sky, f32 range, f32 (*out)[4], int max);

/* Celeste's per-player flags: [0] met her, [1] heard how to make one (bit = player number) */
u8* pc_const_celeste_flags(void);

/* .acplus */
void pc_const_reset(void);
int pc_const_read_chunk(const char tag[4], FILE* f, unsigned int size);
int pc_const_write_chunk(FILE* f);

#ifdef __cplusplus
}
#endif

#endif /* PC_CONSTELLATION_H */
