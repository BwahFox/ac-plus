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
#include "pc_sky.h"
#include "pc_settings.h"
#include "pc_weather.h"
#include "pc_constellation.h"

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
