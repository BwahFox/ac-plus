/* pc_sky.c - render side of the curved ("rolling log") world and sky.
 * See pc_sky.h for how the game marks what to curve; the game side is pc_sky_game.c. */
#include <SDL3/SDL.h>
#include "pc_sky.h"
#include "pc_constellation.h"
#include "pc_acplus_gx.h"
#include "pc_platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The renderer bends the world: Aurora's vertex stage through patches/aurora-curved-world.patch (GXAuroraSetCurve,
 * set with every perspective projection in aurora_widescreen.cpp). A port whose Aurora lacks that patch builds with
 * -DPC_SKY_CAN_CURVE=0 (docs/PORTING.md): the ground stays flat, and everything that would follow the curve (culling, the horizon's dip, fireworks'
 * height) sees k = 0. */
#ifndef PC_SKY_CAN_CURVE
#define PC_SKY_CAN_CURVE 1
#endif

int pc_sky_curve_supported(void) {
    return PC_SKY_CAN_CURVE;
}

static struct {
    /* set by the game for the frame being drawn */
    int requested;
    float center[3];
    float zenith[3];
    float horizon[3];
    float k;
    float night;
    float overcast;
    float rainbow;
    float turn;             /* how far the sky has turned (pc_constellation.h) */
    int line_num;
    float lines[PC_SKY_LINE_MAX][4];

    /* derived from the camera when the sky tag runs */
    int active;
    float up_eye[3];
    float fwd_eye[3];
    float pivot;
    float horizon_drop; /* how far the ground has dropped where it meets the horizon */
} s_sky;

static PCSkyGaze s_gaze; /* the stargazing view, drawn over the frame at PC_NOOP_STARGAZE */
static PCGXHTex s_tex_cloud, s_tex_star, s_tex_moon;
static int s_inited;

#define SKY_PI 3.14159265f
#define SKY_COL_W (2.0f * SKY_PI / (float)PC_CONST_COLS)
#define SKY_ROW_H PC_CONST_CELL

/* Uncompressed 32-bit .tga as written by pc/tools/extract_cf_sky.py; not ready if missing. red_alpha: the red
 * channel goes into alpha too (the clouds' cover is cut from it, in the TEV) */
static void sky_load_tga(PCGXHTex* t, const char* path, int repeat, int red_alpha) {
    unsigned char hdr[18];
    unsigned char* px;
    FILE* f = fopen(path, "rb");
    int w, h, y, x;

    memset(t, 0, sizeof(*t));
    if (f == NULL) {
        return;
    }
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr) || hdr[2] != 2 || hdr[16] != 32) {
        printf("[Sky] %s: not an uncompressed 32-bit tga\n", path);
        fclose(f);
        return;
    }
    w = hdr[12] | (hdr[13] << 8);
    h = hdr[14] | (hdr[15] << 8);
    fseek(f, 18 + hdr[0], SEEK_SET);
    px = malloc((size_t)w * h * 4);
    if (px == NULL || fread(px, 4, (size_t)w * h, f) != (size_t)w * h) {
        free(px);
        fclose(f);
        return;
    }
    fclose(f);

    for (y = 0; y < h; y++) { /* BGRA -> RGBA */
        for (x = 0; x < w; x++) {
            unsigned char* p = px + ((size_t)y * w + x) * 4;
            unsigned char tmp = p[0];
            p[0] = p[2];
            p[2] = tmp;
            if (red_alpha) {
                p[3] = p[0];
            }
        }
    }
    if (!(hdr[17] & 0x20)) { /* bottom-up: flip so row 0 is the top */
        for (y = 0; y < h / 2; y++) {
            for (x = 0; x < w * 4; x++) {
                unsigned char tmp = px[(size_t)y * w * 4 + x];
                px[(size_t)y * w * 4 + x] = px[(size_t)(h - 1 - y) * w * 4 + x];
                px[(size_t)(h - 1 - y) * w * 4 + x] = tmp;
            }
        }
    }
    pc_gxh_tex_init(t, px, w, h, repeat, repeat, 1);
    free(px);
    if (t->ready) {
        printf("[Sky] loaded %s (%dx%d)\n", path, w, h);
    }
}

static void sky_init(void) {
    if (s_inited) {
        return;
    }
    s_inited = 1;
    /* City Folk's own art, if extracted from the player's disc (pc/tools/extract_cf_sky.py) */
    sky_load_tga(&s_tex_cloud, "cf_sky/cloud.tga", 1, 1);
    sky_load_tga(&s_tex_star, "cf_sky/star.tga", 0, 0);
    sky_load_tga(&s_tex_moon, "cf_sky/moon.tga", 0, 0);
}

/* how much wider the game's projection was made for widescreen (pc_widescreen_set_projection) */
static float sky_x_scale(void) {
    return g_pc_widescreen_stretch == 0 ? pc_widescreen_get_aspect_factor() : 1.0f;
}

void pc_sky_set_frame(const float center[3], const float zenith[3], const float horizon[3], float curve_k, float night,
                      float overcast) {
    int i;

    for (i = 0; i < 3; i++) {
        s_sky.center[i] = center[i];
        s_sky.zenith[i] = zenith[i];
        s_sky.horizon[i] = horizon[i];
    }
    s_sky.k = PC_SKY_CAN_CURVE ? curve_k : 0.0f;
    s_sky.night = night;
    s_sky.overcast = overcast;
    s_sky.rainbow = 0.0f;
    s_sky.line_num = 0;
    s_sky.requested = 1;
}

static void sky_set_active(int active) {
    s_sky.active = active;
}

void pc_sky_set_rainbow(float strength) {
    s_sky.rainbow = strength;
}

void pc_sky_set_stars(float turn, const float (*lines)[4], int line_num) {
    s_sky.turn = turn;
    s_sky.line_num = line_num < PC_SKY_LINE_MAX ? line_num : PC_SKY_LINE_MAX;
    memcpy(s_sky.lines, lines, sizeof(s_sky.lines[0]) * s_sky.line_num);
}

void pc_sky_set_gaze(const PCSkyGaze* gaze) {
    s_gaze = *gaze;
    if (s_gaze.line_num > PC_SKY_LINE_MAX) {
        s_gaze.line_num = PC_SKY_LINE_MAX;
    }
}

void pc_sky_cancel_frame(void) {
    s_sky.requested = 0;
}

/* Start of the render pass, which runs after the game built the frame's lists */
void pc_sky_frame_reset(void) {
    sky_set_active(0);
}

void pc_sky_curve_uniforms(int perspective, float up[4], float fwd[4], float haze[4]) {
    int on = s_sky.active && perspective;

    up[0] = s_sky.up_eye[0];
    up[1] = s_sky.up_eye[1];
    up[2] = s_sky.up_eye[2];
    up[3] = on ? s_sky.k : 0.0f;
    haze[0] = s_sky.horizon[0];
    haze[1] = s_sky.horizon[1];
    haze[2] = s_sky.horizon[2];
    haze[3] = (on && s_sky.horizon_drop > 0.0f) ? 1.0f / s_sky.horizon_drop : 0.0f;
    fwd[0] = s_sky.fwd_eye[0];
    fwd[1] = s_sky.fwd_eye[1];
    fwd[2] = s_sky.fwd_eye[2];
    fwd[3] = s_sky.pivot;
}

float pc_sky_curve_now(int perspective) {
    return s_sky.active && perspective ? s_sky.k : 0.0f;
}

void pc_sky_curve_point(int perspective, float* x, float* y, float* z) {
    float u, drop;

    if (!s_sky.active || !perspective || s_sky.k == 0.0f) {
        return;
    }
    u = *x * s_sky.fwd_eye[0] + *y * s_sky.fwd_eye[1] + *z * s_sky.fwd_eye[2] - s_sky.pivot;
    drop = s_sky.k * u * u;
    *x -= s_sky.up_eye[0] * drop;
    *y -= s_sky.up_eye[1] * drop;
    *z -= s_sky.up_eye[2] * drop;
}

/* view: world -> eye, rows are the eye axes in world space plus translation */
typedef struct {
    float proj[4]; /* 1/P00, 1/P11, P02, P12 */
    float eye_to_world[9];
    float dip;
    float heading;
    float zenith[3];
    float horizon[3];
    float night, overcast, rainbow, turn;
    int line_num;
    int edit_num; /* the first edit_num lines are the one being edited */
    const float (*lines)[4];
    float alpha;  /* < 1 blends over what's drawn */
    int ground;   /* dark ground below the horizon (the stargazing view) */
    float hl[4];  /* stars to ring: cursor az, up; picked az, up */
    int hl_on;    /* 1 cursor, 2 picked */
} SkyParams;

/* --- drawing: the old full-screen shader, as a grid of vertex colours (the gradient) with the rest drawn over it as
 * geometry (stars, lines, moon, rainbow, clouds), all in screen space ---------------------------------------------- */

#define SKY_GRID_W 40
#define SKY_GRID_H 30

static float sky_smoothstep(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);

    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return t * t * (3.0f - 2.0f * t);
}

static float sky_clamp01(float x) {
    return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}

static float sky_wrap(float a) {
    a = fmodf(a + SKY_PI, 2.0f * SKY_PI);
    if (a < 0.0f) {
        a += 2.0f * SKY_PI;
    }
    return a - SKY_PI;
}

static float sky_hash(int col, int row, u32 k) {
    u32 h = (u32)col * 0x8DA6B343u + (u32)row * 0xD8163841u + k * 0xCB1AB31Fu;

    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    return (float)(h & 0xFFFFFFu) / 16777216.0f;
}

/* colour for GXColor4u8 through the x2 TEV of pc_gxh_mesh_state */
static u8 sky_c(float v) {
    v *= 127.5f;
    return (u8)(v <= 0.0f ? 0 : v >= 255.0f ? 255 : v + 0.5f);
}

static u8 sky_a(float v) {
    v *= 255.0f;
    return (u8)(v <= 0.0f ? 0 : v >= 255.0f ? 255 : v + 0.5f);
}

/* screen (ndc) -> az and angle above the horizon */
static void sky_ndc_to_sky(const SkyParams* sp, float nx, float ny, float* az, float* above) {
    float e[3] = { (nx + sp->proj[2]) * sp->proj[0], (ny + sp->proj[3]) * sp->proj[1], -1.0f };
    float d[3], len;
    int k;

    for (k = 0; k < 3; k++) {
        d[k] = sp->eye_to_world[0 * 3 + k] * e[0] + sp->eye_to_world[1 * 3 + k] * e[1] +
               sp->eye_to_world[2 * 3 + k] * e[2];
    }
    len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    d[0] /= len, d[1] /= len, d[2] /= len;
    *above = asinf(d[1] < -1.0f ? -1.0f : d[1] > 1.0f ? 1.0f : d[1]) + sp->dip;
    *az = atan2f(d[0], -d[2]);
}

/* az and angle above the horizon -> screen (ndc); FALSE when behind the camera */
static int sky_to_ndc(const SkyParams* sp, float az, float above, float* nx, float* ny) {
    float el = above - sp->dip;
    float d[3] = { sinf(az) * cosf(el), sinf(el), -cosf(az) * cosf(el) };
    float e[3];
    int i;

    for (i = 0; i < 3; i++) {
        e[i] = sp->eye_to_world[i * 3 + 0] * d[0] + sp->eye_to_world[i * 3 + 1] * d[1] +
               sp->eye_to_world[i * 3 + 2] * d[2];
    }
    if (e[2] > -0.02f) {
        return FALSE;
    }
    *nx = e[0] / -e[2] / sp->proj[0] - sp->proj[2];
    *ny = e[1] / -e[2] / sp->proj[1] - sp->proj[3];
    return TRUE;
}

static void sky_vtx(float nx, float ny, u8 r, u8 g, u8 b, u8 a) {
    GXPosition3f32(nx, ny, 0.0f);
    GXColor4u8(r, g, b, a);
}

static void sky_vtx_uv(float nx, float ny, u8 r, u8 g, u8 b, u8 a, float u, float v) {
    GXPosition3f32(nx, ny, 0.0f);
    GXColor4u8(r, g, b, a);
    GXTexCoord2f32(u, v);
}

/* The view's sky: what range of angles the screen covers */
typedef struct {
    float az[SKY_GRID_H + 1][SKY_GRID_W + 1];
    float above[SKY_GRID_H + 1][SKY_GRID_W + 1];
    float daz_min, daz_max; /* around sp->heading */
    float above_min, above_max;
} SkyView;

static SkyView s_view;

static void sky_view(const SkyParams* sp) {
    int x, y;

    s_view.daz_min = 10.0f, s_view.daz_max = -10.0f;
    s_view.above_min = 10.0f, s_view.above_max = -10.0f;
    for (y = 0; y <= SKY_GRID_H; y++) {
        for (x = 0; x <= SKY_GRID_W; x++) {
            float az, above, daz;

            sky_ndc_to_sky(sp, -1.0f + 2.0f * x / SKY_GRID_W, -1.0f + 2.0f * y / SKY_GRID_H, &az, &above);
            s_view.az[y][x] = az;
            s_view.above[y][x] = above;
            daz = sky_wrap(az - sp->heading);
            if (daz < s_view.daz_min) s_view.daz_min = daz;
            if (daz > s_view.daz_max) s_view.daz_max = daz;
            if (above < s_view.above_min) s_view.above_min = above;
            if (above > s_view.above_max) s_view.above_max = above;
        }
    }
    if (s_view.above_max > 1.2f) { /* the zenith in view: all the way round */
        s_view.daz_min = -SKY_PI, s_view.daz_max = SKY_PI;
    }
}

/* the gradient, with the gaze view's dark ground below the horizon */
static void sky_draw_gradient(const SkyParams* sp) {
    int x, y;

    pc_gxh_mesh_state(NULL, sp->alpha < 1.0f ? 1 : 0, FALSE, 0, GX_CULL_NONE);
    for (y = 0; y < SKY_GRID_H; y++) {
        GXBegin(GX_TRIANGLESTRIP, PC_GXH_VTXFMT, (SKY_GRID_W + 1) * 2);
        for (x = 0; x <= SKY_GRID_W; x++) {
            int r;

            for (r = 1; r >= 0; r--) {
                float above = s_view.above[y + r][x];
                float t = sky_smoothstep(-0.01f, 0.16f, above);
                float col[3];
                int k;

                for (k = 0; k < 3; k++) {
                    col[k] = sp->horizon[k] + (sp->zenith[k] - sp->horizon[k]) * t;
                }
                if (sp->ground) {
                    static const float dark[3] = { 0.04f, 0.07f, 0.10f };
                    float g = 1.0f - sky_smoothstep(-0.003f, 0.003f, above);

                    for (k = 0; k < 3; k++) {
                        col[k] += (dark[k] - col[k]) * g;
                    }
                }
                sky_vtx(-1.0f + 2.0f * x / SKY_GRID_W, -1.0f + 2.0f * (y + r) / SKY_GRID_H, sky_c(col[0]),
                        sky_c(col[1]), sky_c(col[2]), sky_a(sp->alpha));
            }
        }
        GXEnd();
    }
}

static float sky_star_vis(const SkyParams* sp, float above) {
    return sp->night * (1.0f - sp->overcast) * sky_smoothstep(0.0f, 0.02f, above);
}

/* A quad in sky space (saz, above): centre, half sizes; FALSE if not drawable */
static int sky_quad(const SkyParams* sp, float saz, float above, float hw, float hh, float q[4][2]) {
    float az = saz + sp->turn;

    return sky_to_ndc(sp, az - hw, above + hh, &q[0][0], &q[0][1]) &&
           sky_to_ndc(sp, az + hw, above + hh, &q[1][0], &q[1][1]) &&
           sky_to_ndc(sp, az + hw, above - hh, &q[2][0], &q[2][1]) &&
           sky_to_ndc(sp, az - hw, above - hh, &q[3][0], &q[3][1]);
}

static void sky_draw_stars(const SkyParams* sp, float secs) {
    int row, row_max, n = 0;
    float saz0, saz1, c;

    if (!s_tex_star.ready || sp->night <= 0.0f || sp->overcast >= 1.0f) {
        return;
    }
    row_max = (int)(s_view.above_max / SKY_ROW_H) + 1;
    if (row_max > (int)(SKY_PI * 0.5f / SKY_ROW_H) + 1) {
        row_max = (int)(SKY_PI * 0.5f / SKY_ROW_H) + 1;
    }
    saz0 = sp->heading + s_view.daz_min - sp->turn - SKY_COL_W;
    saz1 = sp->heading + s_view.daz_max - sp->turn + SKY_COL_W;
    pc_gxh_mesh_state(&s_tex_star, 3, FALSE, 0, GX_CULL_NONE);
    GXBegin(GX_QUADS, PC_GXH_VTXFMT, GX_AUTO);
    for (row = s_view.above_min > 0.0f ? (int)(s_view.above_min / SKY_ROW_H) : 0; row < row_max; row++) {
        for (c = floorf(saz0 / SKY_COL_W); c <= saz1 / SKY_COL_W; c += 1.0f) {
            int col = ((int)c % PC_CONST_COLS + PC_CONST_COLS) % PC_CONST_COLS;
            float cx, cy, above, saz, h, twinkle, i;
            float q[4][2];
            u8 v;

            if (sky_hash(col, row, 0) <= 0.84f) {
                continue;
            }
            cx = 0.35f + 0.3f * sky_hash(col, row, 1);
            cy = 0.35f + 0.3f * sky_hash(col, row, 2);
            saz = (c + cx) * SKY_COL_W;
            above = (row + cy) * SKY_ROW_H;
            h = sky_hash(col, row, 3);
            twinkle = 0.6f + 0.4f * sinf(secs * (1.5f + 3.0f * h) + h * 40.0f);
            i = twinkle * sky_star_vis(sp, above) * sp->alpha;
            /* the star image spans 0.009 rad of the cell (its uv runs 0-1 there) */
            if (i <= 0.0f || !sky_quad(sp, saz, above, 0.375f * SKY_COL_W, 0.0045f, q)) {
                continue;
            }
            v = sky_c(i);
            sky_vtx_uv(q[0][0], q[0][1], v, v, v, 255, 0.0f, 0.0f);
            sky_vtx_uv(q[1][0], q[1][1], v, v, v, 255, 1.0f, 0.0f);
            sky_vtx_uv(q[2][0], q[2][1], v, v, v, 255, 1.0f, 1.0f);
            sky_vtx_uv(q[3][0], q[3][1], v, v, v, 255, 0.0f, 1.0f);
            n++;
        }
    }
    GXEnd(); /* (GX_AUTO: nothing written draws nothing) */
    (void)n;
}

/* A band along the sky-space segment a-b: full colour within core of it, fading out by edge */
static void sky_band(const SkyParams* sp, const float a[2], const float b[2], float core, float edge,
                     const float col[3], float alpha, int with_stars) {
    float dx = b[0] - a[0], dy = b[1] - a[1];
    float len = sqrtf(dx * dx + dy * dy), px, py;
    int steps, i, j;

    if (len < 1e-6f) {
        return;
    }
    px = -dy / len, py = dx / len;
    steps = 2 + (int)(len / 0.01f);
    if (steps > 24) {
        steps = 24;
    }
    for (j = 0; j < 3; j++) { /* the three strips across: fade in, core, fade out */
        GXBegin(GX_TRIANGLESTRIP, PC_GXH_VTXFMT, (u16)((steps + 1) * 2));
        for (i = 0; i <= steps; i++) {
            float t = (float)i / steps;
            float sx = a[0] + dx * t, sy = a[1] + dy * t;
            int r;

            for (r = 0; r < 2; r++) {
                int k = j + r;
                float off = k == 0 ? -edge : k == 1 ? -core : k == 2 ? core : edge;
                float vs = sx + px * off, va = sy + py * off;
                float al = (k == 1 || k == 2) ? alpha * (with_stars ? sky_star_vis(sp, va) : 1.0f) : 0.0f;
                float nx = 0.0f, ny = 0.0f;

                if (!sky_to_ndc(sp, vs + sp->turn, va, &nx, &ny)) {
                    nx = ny = 2.0f, al = 0.0f;
                }
                sky_vtx(nx, ny, sky_c(col[0]), sky_c(col[1]), sky_c(col[2]), sky_a(al));
            }
        }
        GXEnd();
    }
}

static void sky_draw_lines(const SkyParams* sp, float secs) {
    static const float glow[3] = { 0.72f, 0.84f, 1.0f };
    static const float edit[3] = { 1.0f, 0.86f, 0.45f };
    int i;

    if (sp->night <= 0.0f || (sp->line_num <= 0 && sp->hl_on == 0)) {
        return;
    }
    pc_gxh_mesh_state(NULL, 1, FALSE, 0, GX_CULL_NONE);
    for (i = 0; i < sp->line_num; i++) {
        const float* l = sp->lines[i];
        float a[2] = { l[0], l[1] }, b[2] = { l[2], l[3] };
        int ed = i < sp->edit_num;

        sky_band(sp, a, b, 0.0008f, 0.0028f, ed ? edit : glow, (ed ? 0.85f : 0.55f) * sp->alpha, TRUE);
    }
    /* rings around the cursor star and the picked one */
    for (i = 0; i < 2; i++) {
        static const float white[3] = { 1.0f, 1.0f, 1.0f };
        static const float gold[3] = { 1.0f, 0.8f, 0.3f };
        const float* col = i == 0 ? white : gold;
        float al = (0.6f + 0.3f * sinf(secs * 6.0f)) * sp->alpha;
        int seg;

        if ((sp->hl_on & (1 << i)) == 0) {
            continue;
        }
        for (seg = 0; seg < 32; seg++) {
            float t0 = seg * 2.0f * SKY_PI / 32.0f, t1 = (seg + 1) * 2.0f * SKY_PI / 32.0f;
            float a[2] = { sp->hl[i * 2] + 0.0085f * cosf(t0), sp->hl[i * 2 + 1] + 0.0085f * sinf(t0) };
            float b[2] = { sp->hl[i * 2] + 0.0085f * cosf(t1), sp->hl[i * 2 + 1] + 0.0085f * sinf(t1) };

            sky_band(sp, a, b, 0.0006f, 0.0016f, col, al, FALSE);
        }
    }
}

static void sky_draw_moon(const SkyParams* sp) {
    float q[4][2];
    float vis = sp->night * (1.0f - 0.8f * sp->overcast) * sp->alpha;
    float az = sp->heading + 0.16f;
    u8 a = sky_a(vis);
    u8 w = sky_c(1.0f);

    if (!s_tex_moon.ready || vis <= 0.0f) {
        return;
    }
    if (!sky_to_ndc(sp, az - 0.016f, 0.034f + 0.016f, &q[0][0], &q[0][1]) ||
        !sky_to_ndc(sp, az + 0.016f, 0.034f + 0.016f, &q[1][0], &q[1][1]) ||
        !sky_to_ndc(sp, az + 0.016f, 0.034f - 0.016f, &q[2][0], &q[2][1]) ||
        !sky_to_ndc(sp, az - 0.016f, 0.034f - 0.016f, &q[3][0], &q[3][1])) {
        return;
    }
    pc_gxh_mesh_state(&s_tex_moon, 1, FALSE, 0, GX_CULL_NONE);
    GXBegin(GX_QUADS, PC_GXH_VTXFMT, 4);
    sky_vtx_uv(q[0][0], q[0][1], w, w, w, a, 0.0f, 0.0f);
    sky_vtx_uv(q[1][0], q[1][1], w, w, w, a, 1.0f, 0.0f);
    sky_vtx_uv(q[2][0], q[2][1], w, w, w, a, 1.0f, 1.0f);
    sky_vtx_uv(q[3][0], q[3][1], w, w, w, a, 0.0f, 1.0f);
    GXEnd();
}

/* an arc a little left of ahead, red outside to violet inside */
static void sky_draw_rainbow(const SkyParams* sp) {
    const float R = 0.35f, PEAK = 0.075f, W = 0.03f;
    const int XS = 10, PS = 48;
    float strength = 0.35f * sp->rainbow * (1.0f - sp->night) * sp->alpha;
    int xi, pi;

    if (strength <= 0.0f) {
        return;
    }
    pc_gxh_mesh_state(NULL, 1, FALSE, 0, GX_CULL_NONE);
    for (xi = 0; xi < XS; xi++) {
        GXBegin(GX_TRIANGLESTRIP, PC_GXH_VTXFMT, (u16)((PS + 1) * 2));
        for (pi = 0; pi <= PS; pi++) {
            float phi = -0.75f + 1.5f * pi / PS;
            int r;

            for (r = 0; r < 2; r++) {
                float x = (float)(xi + r) / XS;
                float dist = R - x * W;
                float daz = dist * sinf(phi), above = (PEAK - R) + dist * cosf(phi);
                float col[3], al, nx = 2.0f, ny = 2.0f;
                int k;

                for (k = 0; k < 3; k++) {
                    float h = fmodf(x * 4.5f + (k == 0 ? 0.0f : k == 1 ? 4.0f : 2.0f), 6.0f);
                    col[k] = sky_clamp01(fabsf(h - 3.0f) - 1.0f);
                }
                al = strength * sqrtf(sinf(SKY_PI * x)) * sky_smoothstep(-0.005f, 0.02f, above);
                if (!sky_to_ndc(sp, daz + sp->heading - 0.12f, above, &nx, &ny)) {
                    al = 0.0f;
                }
                sky_vtx(nx, ny, sky_c(col[0]), sky_c(col[1]), sky_c(col[2]), sky_a(al));
            }
        }
        GXEnd();
    }
}

/* the clouds along the horizon; their cover is cut from the texture in the TEV (alpha = (red - cover) x 4) */
static void sky_draw_clouds(const SkyParams* sp, float secs) {
    const int ROWS = 6;
    float az0 = sp->heading + s_view.daz_min - 0.05f, az1 = sp->heading + s_view.daz_max + 0.05f;
    int cols = (int)((az1 - az0) / 0.03f) + 1, x, y;
    float cover = 0.62f + (0.25f - 0.62f) * sp->overcast;
    float cloud[3];
    int k;
    GXColor kc = { 0, 0, 0, 0 };

    if (!s_tex_cloud.ready || s_view.above_min > 0.10f) {
        return;
    }
    if (cols > 400) {
        cols = 400;
    }
    for (k = 0; k < 3; k++) {
        cloud[k] = (sp->horizon[k] + (1.0f - sp->horizon[k]) * 0.8f) * (1.0f - 0.55f * sp->night);
    }
    pc_gxh_mesh_state(&s_tex_cloud, 1, FALSE, 0, GX_CULL_NONE);
    kc.a = sky_a(cover);
    GXSetTevKColor(GX_KCOLOR0, kc);
    GXSetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_K0_A);
    GXSetNumTevStages(2);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_KONST, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_4, GX_TRUE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE1);
    GXSetTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_APREV, GX_CA_RASA, GX_CA_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

    for (y = 0; y < ROWS; y++) {
        GXBegin(GX_TRIANGLESTRIP, PC_GXH_VTXFMT, (u16)((cols + 1) * 2));
        for (x = 0; x <= cols; x++) {
            float az = az0 + (az1 - az0) * x / cols;
            int r;

            for (r = 1; r >= 0; r--) {
                /* (the gaze view's ground hides what's below the horizon) */
                float above0 = sp->ground ? 0.0f : -0.01f;
                float above = above0 + (0.10f - above0) * (float)(y + r) / ROWS;
                float v = above / 0.10f;
                float fade = sky_clamp01(1.0f - v) * 0.9f * sp->alpha;
                float nx = 2.0f, ny = 2.0f;

                if (!sky_to_ndc(sp, az, above, &nx, &ny)) {
                    fade = 0.0f;
                }
                sky_vtx_uv(nx, ny, sky_c(cloud[0]), sky_c(cloud[1]), sky_c(cloud[2]), sky_a(fade),
                           az / (2.0f * SKY_PI) * 10.0f + secs * 0.012f, v * 0.6f);
            }
        }
        GXEnd();
    }
}

static void sky_render(const SkyParams* sp) {
    float secs = (float)SDL_GetTicks() / 1000.0f;

    pc_gxh_screen();
    sky_view(sp);
    sky_draw_gradient(sp);
    sky_draw_stars(sp, secs);
    sky_draw_lines(sp, secs);
    sky_draw_moon(sp);
    sky_draw_rainbow(sp);
    sky_draw_clouds(sp, secs);
    pc_gxh_screen_end();
}

static void sky_draw(const float proj[4][4], const float view[3][4]) {
    float cam[3], fwd_w[3], len, d0, h, k, m, dip;
    float eye_to_world[9];
    float center_eye[3];
    int i;

    /* camera position in world: -R^T t */
    for (i = 0; i < 3; i++) {
        cam[i] = -(view[0][i] * view[0][3] + view[1][i] * view[1][3] + view[2][i] * view[2][3]);
    }

    /* curve directions in eye space */
    s_sky.up_eye[0] = view[0][1];
    s_sky.up_eye[1] = view[1][1];
    s_sky.up_eye[2] = view[2][1];
    fwd_w[0] = -view[2][0];
    fwd_w[1] = 0.0f;
    fwd_w[2] = -view[2][2];
    len = sqrtf(fwd_w[0] * fwd_w[0] + fwd_w[2] * fwd_w[2]);
    if (len < 1e-4f) { /* looking straight down: nothing sensible to curve along */
        s_sky.k = 0.0f;
        return;
    }
    fwd_w[0] /= len;
    fwd_w[2] /= len;
    for (i = 0; i < 3; i++) {
        s_sky.fwd_eye[i] = view[i][0] * fwd_w[0] + view[i][2] * fwd_w[2];
        center_eye[i] = view[i][0] * s_sky.center[0] + view[i][1] * s_sky.center[1] +
                        view[i][2] * s_sky.center[2] + view[i][3];
    }
    s_sky.pivot = center_eye[0] * s_sky.fwd_eye[0] + center_eye[1] * s_sky.fwd_eye[1] +
                  center_eye[2] * s_sky.fwd_eye[2];

    /* Where the curved ground's horizon sits: the eye's tangent line to
     * y = -k u^2, with the eye h above and d0 behind the flat point. */
    k = s_sky.k;
    h = cam[1] - s_sky.center[1];
    d0 = (s_sky.center[0] - cam[0]) * fwd_w[0] + (s_sky.center[2] - cam[2]) * fwd_w[2];
    dip = 0.0f;
    s_sky.horizon_drop = 0.0f;
    if (k > 0.0f && h > 0.0f) {
        m = 2.0f * k * (-d0 + sqrtf(d0 * d0 + h / k));
        dip = atanf(m);
        s_sky.horizon_drop = m * m / (4.0f * k); /* k u^2 at the tangent point u = m / 2k */
    }

    {
        static int logged;
        if (!logged) {
            logged = 1;
            printf("[Sky] first outdoor frame: k=%g eye height=%.0f flat point %.0f ahead, horizon %.1f deg down\n",
                   k, h, d0, dip * 57.29578f);
        }
    }

    sky_init();

    /* eye -> world is R^T; GL reads column-major, so R row-major works as-is */
    for (i = 0; i < 3; i++) {
        eye_to_world[i * 3 + 0] = view[i][0];
        eye_to_world[i * 3 + 1] = view[i][1];
        eye_to_world[i * 3 + 2] = view[i][2];
    }

    {
        SkyParams sp;

        memset(&sp, 0, sizeof(sp));
        sp.proj[0] = 1.0f / (proj[0][0] * sky_x_scale());
        sp.proj[1] = 1.0f / proj[1][1];
        sp.proj[2] = proj[0][2];
        sp.proj[3] = proj[1][2];
        memcpy(sp.eye_to_world, eye_to_world, sizeof(eye_to_world));
        sp.dip = dip;
        sp.heading = atan2f(fwd_w[0], -fwd_w[2]);
        memcpy(sp.zenith, s_sky.zenith, sizeof(sp.zenith));
        memcpy(sp.horizon, s_sky.horizon, sizeof(sp.horizon));
        sp.night = s_sky.night;
        sp.overcast = s_sky.overcast;
        sp.rainbow = s_sky.rainbow;
        sp.turn = s_sky.turn;
        sp.line_num = s_sky.line_num;
        sp.lines = (const float(*)[4])s_sky.lines;
        sp.alpha = 1.0f;
        sky_render(&sp);
    }
}

/* The stargazing view: looking up at heading/pitch with fov_y, over the whole screen */
static void sky_draw_gaze(void) {
    SkyParams sp;
    float ch = cosf(s_gaze.heading), sh = sinf(s_gaze.heading);
    float cp = cosf(s_gaze.pitch), spi = sinf(s_gaze.pitch);
    float fwd[3] = { sh * cp, spi, -ch * cp };
    float right[3] = { ch, 0.0f, sh };
    float up[3] = { right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2],
                    right[0] * fwd[1] - right[1] * fwd[0] };
    float t = tanf(s_gaze.fov_y * 0.5f);
    int i;

    sky_init();
    memset(&sp, 0, sizeof(sp));
    sp.proj[0] = t * (float)g_pc_window_w / (float)(g_pc_window_h > 0 ? g_pc_window_h : 1);
    sp.proj[1] = t;
    for (i = 0; i < 3; i++) { /* columns: right, up, back */
        sp.eye_to_world[i] = right[i];
        sp.eye_to_world[3 + i] = up[i];
        sp.eye_to_world[6 + i] = -fwd[i];
    }
    sp.heading = s_gaze.heading;
    memcpy(sp.zenith, s_gaze.zenith, sizeof(sp.zenith));
    memcpy(sp.horizon, s_gaze.horizon, sizeof(sp.horizon));
    sp.night = s_gaze.night;
    sp.overcast = s_gaze.overcast;
    sp.turn = s_gaze.turn;
    sp.line_num = s_gaze.line_num;
    sp.edit_num = s_gaze.edit_num;
    sp.lines = (const float(*)[4])s_gaze.lines;
    sp.alpha = s_gaze.alpha;
    sp.ground = 1;
    memcpy(sp.hl, s_gaze.hl, sizeof(sp.hl));
    sp.hl_on = s_gaze.hl_on;
    sky_render(&sp);
}

void pc_sky_noop(unsigned int tag, const float proj[4][4], const float view[3][4]) {
    switch (tag) {
        case PC_NOOP_SKY:
            if (s_sky.requested) {
                sky_draw(proj, view);
                sky_set_active(s_sky.k > 0.0f);
            }
            break;
        case PC_NOOP_WORLD_BEGIN:
            sky_set_active(s_sky.requested && s_sky.k > 0.0f);
            break;
        case PC_NOOP_WORLD_END:
            sky_set_active(0);
            break;
        case PC_NOOP_STARGAZE:
            sky_set_active(0);
            if (s_gaze.alpha > 0.0f) {
                sky_draw_gaze();
            }
            break;
    }
}
