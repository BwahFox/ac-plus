/* pc_sky_game.c - game side of the curved ("rolling log") world and sky.
 * Decides when the outdoor town is being drawn, picks the sky colours and marks
 * the display lists for the render side (pc_sky.c). */
#include "m_play.h"
#include "m_common_data.h"
#include "m_event.h"
#include "m_field_info.h"
#include "m_kankyo.h"
#include "m_submenu.h"
#include "graph.h"
#include "sys_matrix.h"
#include "m_lib.h"
#include "pc_sky.h"
#include "pc_settings.h"
#include "pc_weather.h"
#include "pc_constellation.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

extern f32 mEnv_PC_GetOvercastRate(void);

/* How sharply the ground curves away at world_curve = 100 */
#define PC_SKY_CURVE_K 0.0018f

typedef struct {
    u8 zenith[3];
    u8 horizon[3];
} pc_sky_term_c;

/* Start hour of each row; colours blend towards the next row (m_kankyo's terms, plus
 * 17:00 so the afternoon stays blue before turning to sunset) */
static const int l_pc_sky_term_hour[] = { 0, 4, 6, 8, 12, 16, 17, 18, 20, 24 };
#define PC_SKY_TERM_NUM 9

static const pc_sky_term_c l_pc_sky_clear[PC_SKY_TERM_NUM] = {
    { { 12, 16, 48 }, { 34, 44, 92 } },     /* 00:00 night */
    { { 24, 30, 80 }, { 70, 72, 120 } },    /* 04:00 before dawn */
    { { 70, 110, 190 }, { 250, 196, 160 } }, /* 06:00 sunrise */
    { { 40, 120, 225 }, { 140, 195, 245 } }, /* 08:00 morning */
    { { 30, 110, 225 }, { 130, 190, 245 } }, /* 12:00 afternoon */
    { { 40, 112, 218 }, { 150, 195, 242 } }, /* 16:00 late afternoon */
    { { 46, 104, 208 }, { 178, 190, 228 } }, /* 17:00 evening */
    { { 60, 70, 150 }, { 250, 160, 110 } },  /* 18:00 sunset */
    { { 16, 20, 56 }, { 40, 48, 100 } },     /* 20:00 night */
};

static const pc_sky_term_c l_pc_sky_overcast[PC_SKY_TERM_NUM] = {
    { { 20, 22, 34 }, { 40, 44, 58 } },
    { { 40, 44, 60 }, { 70, 74, 90 } },
    { { 100, 108, 124 }, { 150, 150, 156 } },
    { { 120, 130, 145 }, { 170, 176, 184 } },
    { { 125, 135, 150 }, { 175, 180, 188 } },
    { { 115, 122, 138 }, { 165, 168, 176 } },
    { { 92, 97, 114 }, { 142, 140, 147 } },
    { { 70, 72, 90 }, { 120, 112, 118 } },
    { { 24, 26, 40 }, { 44, 48, 62 } },
};

static int l_pc_sky_this_frame;
static f32 l_pc_sky_k;
static f32 l_pc_sky_center[3];
static f32 l_pc_sky_fwd[2]; /* level forward (x, z) of the camera */

static f32 pc_sky_curve_k(void) {
    return PC_SKY_CURVE_K * g_pc_settings.world_curve / 100.0f;
}

static void pc_sky_blend(const pc_sky_term_c* table, int term, f32 t, f32 zenith[3], f32 horizon[3]) {
    const pc_sky_term_c* a = &table[term];
    const pc_sky_term_c* b = &table[(term + 1) % PC_SKY_TERM_NUM];
    int i;

    for (i = 0; i < 3; i++) {
        zenith[i] = (a->zenith[i] + (b->zenith[i] - a->zenith[i]) * t) / 255.0f;
        horizon[i] = (a->horizon[i] + (b->horizon[i] - a->horizon[i]) * t) / 255.0f;
    }
}

/* 1 from 20:00 to 04:00, fading over an hour and a half either side */
static int pc_sky_dev_hour(void);

static f32 pc_sky_night(void) {
    f32 hour = pc_sky_dev_hour() >= 0 ? pc_sky_dev_hour() + 0.5f : Common_Get(time).now_sec / 3600.0f;

    if (hour >= 18.5f && hour < 20.0f) {
        return (hour - 18.5f) / 1.5f;
    }
    if (hour >= 4.0f && hour < 5.5f) {
        return 1.0f - (hour - 4.0f) / 1.5f;
    }
    return (hour >= 20.0f || hour < 4.0f) ? 1.0f : 0.0f;
}

/* Dev harness: PC_SKY_HOUR=H draws the sky of hour H, PC_SKY_CLEAR=1 a clear one. */
static int pc_sky_dev_hour(void) {
    static int hour = -2;

    if (hour == -2) {
        hour = getenv("PC_SKY_HOUR") != NULL ? atoi(getenv("PC_SKY_HOUR")) % 24 : -1;
    }
    return hour;
}

static f32 pc_sky_overcast(void) {
    static int clear = -1;

    if (clear < 0) {
        clear = getenv("PC_SKY_CLEAR") != NULL;
    }
    if (clear) {
        return 0.0f;
    }
    {
        f32 rate = mEnv_PC_GetOvercastRate();
        f32 cloud = pc_weather_cloud();

        return cloud > rate ? cloud : rate;
    }
}

static f32 pc_sky_rainbow(void) {
    static int dev = -2;

    if (dev == -2) {
        dev = getenv("PC_SKY_RAINBOW") != NULL ? atoi(getenv("PC_SKY_RAINBOW")) : -1;
    }
    return dev >= 0 ? dev / 100.0f : pc_weather_rainbow();
}

static void pc_sky_colors(f32 zenith[3], f32 horizon[3]) {
    int now_sec = pc_sky_dev_hour() >= 0 ? pc_sky_dev_hour() * 3600 + 1800 : Common_Get(time).now_sec;
    int term = 0;
    int start, end, i;
    f32 t, overcast;
    f32 cz[3], ch[3], oz[3], oh[3];

    while (term < PC_SKY_TERM_NUM - 1 && now_sec >= l_pc_sky_term_hour[term + 1] * 3600) {
        term++;
    }
    start = l_pc_sky_term_hour[term] * 3600;
    end = l_pc_sky_term_hour[term + 1] * 3600;
    t = (f32)(now_sec - start) / (f32)(end - start);
    t = t * t * (3.0f - 2.0f * t); /* ease, so each row's colours hold a while */

    pc_sky_blend(l_pc_sky_clear, term, t, cz, ch);
    pc_sky_blend(l_pc_sky_overcast, term, t, oz, oh);
    overcast = pc_sky_overcast();
    for (i = 0; i < 3; i++) {
        zenith[i] = cz[i] + (oz[i] - cz[i]) * overcast;
        horizon[i] = ch[i] + (oh[i] - ch[i]) * overcast;
    }
}

void pc_sky_play_draw(GAME_PLAY* play) {
    GRAPH* graph = play->game.graph;
    f32 center[3], zenith[3], horizon[3];

    l_pc_sky_this_frame = g_pc_settings.world_curve > 0 && Common_Get(field_type) == mFI_FIELDTYPE2_FG &&
                          mEv_CheckTitleDemo() != mEv_TITLEDEMO_STAFFROLL &&
                          play->submenu.mode < mSM_MODE_PRERENDER_DONE && play->fb_mode != FBDEMO_MODE_MOVE;
    if (!l_pc_sky_this_frame) {
        pc_sky_cancel_frame();
        return;
    }

    center[0] = play->view.center.x;
    center[1] = play->view.center.y;
    center[2] = play->view.center.z;
    pc_sky_colors(zenith, horizon);
    pc_sky_set_frame(center, zenith, horizon, pc_sky_curve_k(), pc_sky_night(), pc_sky_overcast());
    pc_sky_set_rainbow(pc_sky_rainbow());
    {
        /* constellations near where the camera looks, in sky coordinates */
        f32 lines[PC_SKY_LINE_MAX][4];
        f32 turn = pc_const_sky_turn();
        f32 heading = atan2f(play->view.center.x - play->view.eye.x, -(play->view.center.z - play->view.eye.z));
        extern void pc_const_dev_make(f32 heading_sky);

        pc_const_dev_make(heading - turn);

        int n = pc_const_sky_lines(heading - turn, 1.2f, lines, PC_SKY_LINE_MAX);
        pc_sky_set_stars(turn, (const f32(*)[4])lines, n);
    }

    /* same curve for culling; matches pc_sky.c's eye-space version */
    {
        f32 fx = play->view.center.x - play->view.eye.x;
        f32 fz = play->view.center.z - play->view.eye.z;
        f32 len = sqrtf(fx * fx + fz * fz);

        l_pc_sky_k = len > 0.001f ? pc_sky_curve_k() : 0.0f;
        l_pc_sky_fwd[0] = len > 0.001f ? fx / len : 0.0f;
        l_pc_sky_fwd[1] = len > 0.001f ? fz / len : 0.0f;
        l_pc_sky_center[0] = center[0];
        l_pc_sky_center[1] = center[1];
        l_pc_sky_center[2] = center[2];
    }

    OPEN_DISP(graph);
    gDPNoOpTag(NEXT_BG_OPA_DISP, PC_NOOP_SKY);
    gDPNoOpTag(NEXT_POLY_XLU_DISP, PC_NOOP_WORLD_BEGIN);
    gDPNoOpTag(NEXT_FONT_DISP, PC_NOOP_WORLD_END);
    gDPNoOpTag(NEXT_OVERLAY_DISP, PC_NOOP_WORLD_END);
    CLOSE_DISP(graph);
}

void pc_sky_curve_world(const float in[3], float out[3]) {
    f32 u;

    out[0] = in[0];
    out[2] = in[2];
    if (!l_pc_sky_this_frame || l_pc_sky_k == 0.0f) {
        out[1] = in[1];
        return;
    }
    u = (in[0] - l_pc_sky_center[0]) * l_pc_sky_fwd[0] + (in[2] - l_pc_sky_center[2]) * l_pc_sky_fwd[1];
    out[1] = in[1] - l_pc_sky_k * u * u;
}

/* After the world, before anything drawn on top of it (menus, effects on screen) */
void pc_sky_play_draw_end(GAME_PLAY* play) {
    GRAPH* graph = play->game.graph;

    if (!l_pc_sky_this_frame) {
        return;
    }
    OPEN_DISP(graph);
    gDPNoOpTag(NEXT_POLY_OPA_DISP, PC_NOOP_WORLD_END);
    CLOSE_DISP(graph);
}

/* Fireworks (ef_hanabi_*.c) and shooting stars (ef_shooting*.c). The GameCube lays them flat on the ground and
 * the pond, reflections for a camera that never sees the sky. With the sky in view each is drawn again up in
 * it, facing the camera. Like the stars they hang in a fixed direction, far off, so walking about doesn't slide
 * them around: north, a little above the curved horizon, spread side to side and up by where each burst
 * against the pond (the show's centre). Looking down, the horizon is off the top of the screen and so are
 * they. */
#define PC_SKY_FIREWORK_DIST   1000.0f  /* how far out from the camera they're drawn */
#define PC_SKY_FIREWORK_RISE   0.07f    /* radians above the horizon, for one bursting over the pond */
#define PC_SKY_FIREWORK_TURN   0.0016f  /* radians around per unit of the reflection east or west of the pond */
#define PC_SKY_FIREWORK_CLIMB  0.0004f  /* radians up per unit of it north of the pond */
#define PC_SKY_FIREWORK_SCALE  2.6f

int pc_sky_fireworks(void) {
    return l_pc_sky_this_frame && l_pc_sky_k > 0.0f;
}

/* How far above level the camera at eye sees the curved ground's edge, ground_y being the ground's height */
static f32 pc_sky_horizon_angle(const xyz_t* eye, f32 ground_y) {
    f32 to_center = (l_pc_sky_center[0] - eye->x) * l_pc_sky_fwd[0] + (l_pc_sky_center[2] - eye->z) * l_pc_sky_fwd[1];
    f32 best = -1.6f;
    f32 d;

    for (d = 40.0f; d < 4000.0f; d += 40.0f) {
        f32 u = d - to_center;
        f32 a = atan2f(ground_y - l_pc_sky_k * u * u - eye->y, d);

        if (a > best) {
            best = a;
        }
    }
    return best;
}

/* The pond's middle, where the GameCube centres both shows; FALSE in a town without one */
static int pc_sky_pond(f32* x, f32* z) {
    int bx, bz;

    if (!mFI_BlockKind2BkNum(&bx, &bz, mRF_BLOCKKIND_POOL) || !mFI_BkNum2WposXZ(x, z, bx, bz)) {
        return FALSE;
    }
    *x += mFI_BK_WORLDSIZE_X_F * 0.5f;
    *z += mFI_BK_WORLDSIZE_Z_F * 0.5f;
    return TRUE;
}

void pc_sky_no_fog(GAME* game);

void pc_sky_place(GAME* game, const xyz_t* pos, f32 scale) {
    GAME_PLAY* play = (GAME_PLAY*)game;
    const xyz_t* eye = &play->view.eye;
    f32 pond_x, pond_z, turn, up, x, y, z, u;

    if (!pc_sky_pond(&pond_x, &pond_z)) {
        pond_x = eye->x;
        pond_z = eye->z;
    }
    turn = (pos->x - pond_x) * PC_SKY_FIREWORK_TURN;
    up = pc_sky_horizon_angle(eye, pos->y) + PC_SKY_FIREWORK_RISE + (pond_z - pos->z) * PC_SKY_FIREWORK_CLIMB;
    /* that way, north (-z) turned east by turn and raised by up */
    x = eye->x + sinf(turn) * cosf(up) * PC_SKY_FIREWORK_DIST;
    y = eye->y + sinf(up) * PC_SKY_FIREWORK_DIST;
    z = eye->z - cosf(turn) * cosf(up) * PC_SKY_FIREWORK_DIST;
    /* the vertex shader will lower it by k u^2, so it goes up as much */
    u = (x - l_pc_sky_center[0]) * l_pc_sky_fwd[0] + (z - l_pc_sky_center[2]) * l_pc_sky_fwd[1];
    Matrix_translate(x, y + l_pc_sky_k * u * u, z, MTX_LOAD);
    Matrix_mult(&play->billboard_matrix, MTX_MULT);
    Matrix_scale(scale, scale, scale, MTX_MULT);
    pc_sky_no_fog(game);
}

/* the night's fog would all but hide it that far out */
void pc_sky_no_fog(GAME* game) {
    OPEN_DISP(game->graph);
    gSPClearGeometryMode(NEXT_POLY_XLU_DISP, G_FOG);
    CLOSE_DISP(game->graph);
}

void pc_sky_firework_place(GAME* game, const xyz_t* pos, const xyz_t* offset, int sky) {
    if (!sky) { /* the GameCube's: flat on the ground */
        Matrix_translate(pos->x + offset->x, pos->y + offset->y, pos->z + offset->z, MTX_LOAD);
        Matrix_RotateX(DEG2SHORT_ANGLE2(270), MTX_MULT);
        return;
    }
    pc_sky_place(game, pos, PC_SKY_FIREWORK_SCALE);
}

void pc_sky_firework_done(GAME* game) {
    OPEN_DISP(game->graph);
    gSPSetGeometryMode(NEXT_POLY_XLU_DISP, G_FOG);
    CLOSE_DISP(game->graph);
}
