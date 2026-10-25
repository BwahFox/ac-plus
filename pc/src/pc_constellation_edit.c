/* pc_constellation_edit.c - Celeste's telescope. See pc_constellation_edit.h. */
#include "pc_constellation_edit.h"
#include "pc_menu_util.h"
#include "pc_platform.h"
#include "pc_sky.h"
#include "pc_mouse.h"
#include "pc_settings.h"

#include "m_common_data.h"
#include "game.h"
#include "graph.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern int g_pc_paused;
void pc_pause_menu_open_const(void);

#define PC_EDIT_PI 3.14159265f
#define PC_EDIT_COL_W (2.0f * PC_EDIT_PI / PC_CONST_COLS)
#define PC_EDIT_PITCH 0.06f
#define PC_EDIT_FOV 0.17f
#define PC_EDIT_MAX_WIDTH 0.3f /* radians across, so it fits the view outdoors */
#define PC_EDIT_PAN_STEP 0.05f

/* pc_pattern_qr.h's action numbers */
enum { ACT_UP = 1, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_CONFIRM, ACT_CANCEL };

enum { FINISH_DONE, FINISH_KEEP, FINISH_RESTART, FINISH_QUIT, FINISH_NUM };

static struct {
    int active;
    int mode;
    int result;
    pc_constellation_c work;
    pc_constellation_c start;
    f32 heading; /* sky azimuth at the middle of the view */
    pc_const_star_c cursor;
    int has_cursor;
    pc_const_star_c picked;
    int has_picked;
    int finish_menu; /* the Done / Keep going / Start over / Quit list */
    int finish_sel;
    char status[64];
    u32 status_until;
    int pick_slot[PC_CONST_MAX]; /* PICK: the slots listed */
    int pick_num;
    int pick_sel;
    int pick_result;
} l_edit;

static f32 pc_edit_wrap(f32 a) {
    a = fmodf(a + PC_EDIT_PI, 2.0f * PC_EDIT_PI);
    return (a < 0.0f ? a + 2.0f * PC_EDIT_PI : a) - PC_EDIT_PI;
}

static int pc_edit_same(pc_const_star_c a, pc_const_star_c b) {
    return a.col == b.col && a.row == b.row;
}

static void pc_edit_say(const char* s) {
    snprintf(l_edit.status, sizeof(l_edit.status), "%s", s);
    l_edit.status_until = SDL_GetTicks() + 2500;
}

/* --- the constellation as a graph --- */

static int pc_edit_has_star(const pc_constellation_c* c, pc_const_star_c s) {
    int i;

    for (i = 0; i < c->link_num; i++) {
        if (pc_edit_same(c->link[i][0], s) || pc_edit_same(c->link[i][1], s)) {
            return TRUE;
        }
    }
    return FALSE;
}

static int pc_edit_find_link(const pc_constellation_c* c, pc_const_star_c a, pc_const_star_c b) {
    int i;

    for (i = 0; i < c->link_num; i++) {
        if ((pc_edit_same(c->link[i][0], a) && pc_edit_same(c->link[i][1], b)) ||
            (pc_edit_same(c->link[i][0], b) && pc_edit_same(c->link[i][1], a))) {
            return i;
        }
    }
    return -1;
}

int pc_const_connected(const pc_constellation_c* c) {
    u8 reached[PC_CONST_LINKS];
    int changed = TRUE, i, j;

    if (c->link_num == 0) {
        return TRUE;
    }
    memset(reached, 0, sizeof(reached));
    reached[0] = 1;
    while (changed) {
        changed = FALSE;
        for (i = 0; i < c->link_num; i++) {
            if (reached[i]) {
                continue;
            }
            for (j = 0; j < c->link_num && !reached[i]; j++) {
                if (reached[j] && (pc_edit_same(c->link[i][0], c->link[j][0]) || pc_edit_same(c->link[i][0], c->link[j][1]) ||
                                   pc_edit_same(c->link[i][1], c->link[j][0]) || pc_edit_same(c->link[i][1], c->link[j][1]))) {
                    reached[i] = 1;
                    changed = TRUE;
                }
            }
        }
    }
    for (i = 0; i < c->link_num; i++) {
        if (!reached[i]) {
            return FALSE;
        }
    }
    return TRUE;
}

static void pc_edit_star_pos(pc_const_star_c s, f32* az, f32* up) {
    pc_const_star(s.col, s.row, az, up);
}

/* How wide the constellation would be with star s added, radians */
static f32 pc_edit_width_with(const pc_constellation_c* c, pc_const_star_c s) {
    f32 az0, up, lo = 0.0f, hi = 0.0f, az;
    int i, k;

    pc_edit_star_pos(s, &az0, &up);
    for (i = 0; i < c->link_num; i++) {
        for (k = 0; k < 2; k++) {
            pc_edit_star_pos(c->link[i][k], &az, &up);
            az = pc_edit_wrap(az - az0);
            lo = az < lo ? az : lo;
            hi = az > hi ? az : hi;
        }
    }
    return hi - lo;
}

/* A on a second star: link or unlink */
static void pc_edit_link(pc_const_star_c a, pc_const_star_c b) {
    pc_constellation_c* c = &l_edit.work;
    int i = pc_edit_find_link(c, a, b);

    if (i >= 0) {
        pc_constellation_c trial = *c;

        trial.link[i][0] = trial.link[trial.link_num - 1][0];
        trial.link[i][1] = trial.link[trial.link_num - 1][1];
        trial.link_num--;
        if (!pc_const_connected(&trial)) {
            pc_edit_say("That line holds the constellation together!");
            return;
        }
        *c = trial;
        l_edit.picked = b;
        return;
    }
    if (c->link_num >= PC_CONST_LINKS) {
        pc_edit_say("That's all 16 lines.");
        return;
    }
    if (c->link_num > 0 && !pc_edit_has_star(c, a) && !pc_edit_has_star(c, b)) {
        pc_edit_say("New lines have to join the constellation.");
        return;
    }
    if (pc_edit_width_with(c, a) > PC_EDIT_MAX_WIDTH || pc_edit_width_with(c, b) > PC_EDIT_MAX_WIDTH) {
        pc_edit_say("That's too far across the sky.");
        return;
    }
    c->link[c->link_num][0] = a;
    c->link[c->link_num][1] = b;
    c->link_num++;
    l_edit.picked = b; /* keep drawing from the new end, as in City Folk */
}

/* --- moving between stars --- */

/* The nearest star to (az, up), or one in the direction (dx, dy) from it */
static int pc_edit_nearest(f32 az0, f32 up0, f32 dx, f32 dy, pc_const_star_c* out) {
    int col0 = (int)floorf(az0 / PC_EDIT_COL_W);
    int dcol, row, best = 0;
    f32 best_score = 1e9f;

    for (dcol = -30; dcol <= 30; dcol++) {
        for (row = PC_CONST_ROW_MIN; row <= PC_CONST_ROW_MAX; row++) {
            int col = ((col0 + dcol) % PC_CONST_COLS + PC_CONST_COLS) % PC_CONST_COLS;
            f32 az, up, ex, ey, along, across, score;

            if (!pc_const_star(col, row, &az, &up)) {
                continue;
            }
            ex = pc_edit_wrap(az - az0);
            ey = up - up0;
            if (dx == 0.0f && dy == 0.0f) {
                score = ex * ex + ey * ey;
            } else {
                along = ex * dx + ey * dy;
                across = ex * dy - ey * dx;
                if (along < 0.002f) {
                    continue;
                }
                score = along + 2.5f * fabsf(across);
            }
            if (score < best_score) {
                best_score = score;
                out->col = (s16)col;
                out->row = (s16)row;
                best = 1;
            }
        }
    }
    return best;
}

static void pc_edit_follow_cursor(void) {
    f32 az, up, d;

    if (!l_edit.has_cursor) {
        return;
    }
    pc_edit_star_pos(l_edit.cursor, &az, &up);
    d = pc_edit_wrap(az - l_edit.heading);
    if (d > 0.1f) {
        l_edit.heading = pc_edit_wrap(az - 0.1f);
    } else if (d < -0.1f) {
        l_edit.heading = pc_edit_wrap(az + 0.1f);
    }
}

static void pc_edit_move(f32 dx, f32 dy) {
    f32 az, up;
    pc_const_star_c s;

    if (!l_edit.has_cursor) {
        l_edit.has_cursor = pc_edit_nearest(l_edit.heading, 0.06f, 0.0f, 0.0f, &l_edit.cursor);
        return;
    }
    pc_edit_star_pos(l_edit.cursor, &az, &up);
    if (pc_edit_nearest(az, up, dx, dy, &s)) {
        l_edit.cursor = s;
        pc_edit_follow_cursor();
    }
}

/* --- open / close --- */

static f32 pc_edit_center(const pc_constellation_c* c) {
    f32 az0, up, az, sum = 0.0f;
    int i;

    pc_edit_star_pos(c->link[0][0], &az0, &up);
    for (i = 0; i < c->link_num; i++) {
        pc_edit_star_pos(c->link[i][0], &az, &up);
        sum += pc_edit_wrap(az - az0);
    }
    return pc_edit_wrap(az0 + sum / c->link_num);
}

void pc_const_edit_open(int mode, const pc_constellation_c* from) {
    memset(&l_edit, 0, sizeof(l_edit));
    l_edit.active = TRUE;
    l_edit.mode = mode;
    if (from != NULL) {
        l_edit.work = *from;
    }
    l_edit.start = l_edit.work;
    /* the part of the sky due north right now, or the constellation being changed */
    l_edit.heading = l_edit.work.link_num > 0 ? pc_edit_center(&l_edit.work) : pc_edit_wrap(-pc_const_sky_turn());
    if (mode == PC_CONST_EDIT_MAKE) {
        l_edit.has_cursor = pc_edit_nearest(l_edit.heading, 0.06f, 0.0f, 0.0f, &l_edit.cursor);
        if (getenv("PC_CONST_AUTOMAKE") != NULL && l_edit.has_cursor) {
            /* dev: link three stars to the right and finish */
            int k;

            for (k = 0; k < 3; k++) {
                pc_const_star_c from = l_edit.cursor;

                pc_edit_move(1.0f, k == 1 ? 0.5f : 0.0f);
                pc_edit_link(from, l_edit.cursor);
            }
            l_edit.result = TRUE;
            l_edit.active = FALSE;
            return;
        }
    }
    pc_pause_menu_open_const();
}

void pc_const_edit_pick(int owner) {
    int i;

    memset(&l_edit, 0, sizeof(l_edit));
    l_edit.active = TRUE;
    l_edit.mode = PC_CONST_EDIT_PICK;
    l_edit.pick_result = -1;
    for (i = 0; i < PC_CONST_MAX; i++) {
        const pc_constellation_c* c = pc_const_get(i);

        if (c->used && (owner < 0 || c->owner == owner)) {
            l_edit.pick_slot[l_edit.pick_num++] = i;
        }
    }
    if (getenv("PC_CONST_AUTOMAKE") != NULL) {
        /* dev: take the first one */
        l_edit.pick_result = l_edit.pick_num > 0 ? l_edit.pick_slot[0] : -1;
        l_edit.active = FALSE;
        return;
    }
    pc_pause_menu_open_const();
}

int pc_const_edit_picked(void) {
    return l_edit.pick_result;
}

int pc_const_edit_active(void) {
    return l_edit.active;
}

int pc_const_edit_result(pc_constellation_c* out) {
    if (l_edit.result && out != NULL) {
        *out = l_edit.work;
    }
    return l_edit.result;
}

static void pc_edit_close(int result) {
    l_edit.active = FALSE;
    l_edit.result = result;
}

void pc_const_edit_action(int act) {
    if (!l_edit.active) {
        return;
    }
    if (l_edit.mode == PC_CONST_EDIT_PICK) {
        if (act == ACT_UP && l_edit.pick_num > 0) {
            l_edit.pick_sel = (l_edit.pick_sel + l_edit.pick_num - 1) % l_edit.pick_num;
        } else if (act == ACT_DOWN && l_edit.pick_num > 0) {
            l_edit.pick_sel = (l_edit.pick_sel + 1) % l_edit.pick_num;
        } else if (act == ACT_CONFIRM) {
            l_edit.pick_result = l_edit.pick_num > 0 ? l_edit.pick_slot[l_edit.pick_sel] : -1;
            pc_edit_close(FALSE);
        } else if (act == ACT_CANCEL) {
            l_edit.pick_result = -1;
            pc_edit_close(FALSE);
        }
        return;
    }
    if (l_edit.mode == PC_CONST_EDIT_VIEW) {
        if (act == ACT_LEFT || act == ACT_RIGHT) {
            l_edit.heading = pc_edit_wrap(l_edit.heading + (act == ACT_LEFT ? -PC_EDIT_PAN_STEP : PC_EDIT_PAN_STEP));
        } else if (act == ACT_CONFIRM || act == ACT_CANCEL) {
            pc_edit_close(FALSE);
        }
        return;
    }

    if (l_edit.finish_menu) {
        switch (act) {
            case ACT_UP:
                l_edit.finish_sel = (l_edit.finish_sel + FINISH_NUM - 1) % FINISH_NUM;
                break;
            case ACT_DOWN:
                l_edit.finish_sel = (l_edit.finish_sel + 1) % FINISH_NUM;
                break;
            case ACT_CANCEL:
                l_edit.finish_menu = FALSE;
                break;
            case ACT_CONFIRM:
                l_edit.finish_menu = FALSE;
                switch (l_edit.finish_sel) {
                    case FINISH_DONE:
                        if (l_edit.work.link_num == 0) {
                            pc_edit_say("Connect some stars first!");
                        } else {
                            pc_edit_close(TRUE);
                        }
                        break;
                    case FINISH_RESTART:
                        l_edit.work = l_edit.start;
                        l_edit.work.link_num = 0;
                        l_edit.has_picked = FALSE;
                        break;
                    case FINISH_QUIT:
                        pc_edit_close(FALSE);
                        break;
                }
                break;
        }
        return;
    }

    switch (act) {
        case ACT_UP:
            pc_edit_move(0.0f, 1.0f);
            break;
        case ACT_DOWN:
            pc_edit_move(0.0f, -1.0f);
            break;
        case ACT_LEFT:
            pc_edit_move(-1.0f, 0.0f);
            break;
        case ACT_RIGHT:
            pc_edit_move(1.0f, 0.0f);
            break;
        case ACT_CONFIRM:
            if (!l_edit.has_cursor) {
                break;
            }
            if (!l_edit.has_picked) {
                if (l_edit.work.link_num > 0 && !pc_edit_has_star(&l_edit.work, l_edit.cursor)) {
                    pc_edit_say("Start from a star in the constellation.");
                } else {
                    l_edit.picked = l_edit.cursor;
                    l_edit.has_picked = TRUE;
                }
            } else if (pc_edit_same(l_edit.picked, l_edit.cursor)) {
                l_edit.has_picked = FALSE;
            } else {
                pc_edit_link(l_edit.picked, l_edit.cursor);
            }
            break;
        case ACT_CANCEL:
            if (l_edit.has_picked) {
                l_edit.has_picked = FALSE;
            } else {
                l_edit.finish_menu = TRUE;
                l_edit.finish_sel = FINISH_DONE;
            }
            break;
    }
}

/* --- the mouse (with the Mouse setting on "Menus") ---
 * The cursor follows the pointer to the nearest star; left click is A, right click B; the wheel, or the pointer
 * held near the left or right edge, looks around. In the lists, hovering picks a row. */

#define PC_EDIT_EDGE 0.06f      /* how near the edge, of the window's width, pans */
#define PC_EDIT_EDGE_PAN 0.012f /* radians per frame there */

extern SDL_Window* g_pc_window;

/* The sky's azimuth and height under window point (wx, wy), as pc_sky.c's sky_draw_gaze looks */
static void pc_edit_window_to_sky(int wx, int wy, int ww, int wh, f32* az, f32* up) {
    f32 ch = cosf(l_edit.heading), sh = sinf(l_edit.heading);
    f32 cp = cosf(PC_EDIT_PITCH), sp = sinf(PC_EDIT_PITCH);
    f32 fwd[3] = { sh * cp, sp, -ch * cp };
    f32 right[3] = { ch, 0.0f, sh };
    f32 upv[3] = { right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2],
                   right[0] * fwd[1] - right[1] * fwd[0] };
    f32 t = tanf(PC_EDIT_FOV * 0.5f);
    f32 nx = (2.0f * (f32)wx / (f32)ww - 1.0f) * t * (f32)ww / (f32)wh;
    f32 ny = (1.0f - 2.0f * (f32)wy / (f32)wh) * t;
    f32 d[3], len;
    int k;

    for (k = 0; k < 3; k++) {
        d[k] = fwd[k] + right[k] * nx + upv[k] * ny;
    }
    len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    *az = atan2f(d[0], -d[2]);
    *up = asinf(d[1] / len);
}

/* The list row under the pointer (rows `n` from y0, `step` apart, in the 320x240 screen), or -1 */
static int pc_edit_mouse_row(f32 y0, f32 step, int n) {
    s32 sx, sy;
    int i;

    pc_mouse_get_native_position(&sx, &sy);
    for (i = 0; i < n; i++) {
        if ((f32)sy >= y0 + i * step - 2.0f && (f32)sy < y0 + (i + 1) * step - 2.0f) {
            return i;
        }
    }
    return -1;
}

static void pc_edit_mouse(void) {
    u32 pressed = pc_mouse_button_pressed();
    s32 wheel = pc_mouse_scroll_wheel();
    s32 mx, my;
    int ww = 0, wh = 0, row;

    if (g_pc_settings.mouse_mode != 1 || g_pc_window == NULL) {
        return;
    }
    SDL_GetWindowSize(g_pc_window, &ww, &wh);
    if (ww <= 0 || wh <= 0) {
        return;
    }
    pc_mouse_get_position(&mx, &my);

    if (l_edit.mode == PC_CONST_EDIT_PICK || l_edit.finish_menu) {
        int pick = l_edit.mode == PC_CONST_EDIT_PICK;

        row = pick ? pc_edit_mouse_row(64.0f, 14.0f, l_edit.pick_num) : pc_edit_mouse_row(90.0f, 18.0f, FINISH_NUM);
        if (row >= 0 && pc_mouse_moved()) {
            *(pick ? &l_edit.pick_sel : &l_edit.finish_sel) = row;
        }
        if ((pressed & PC_MOUSE_BUTTON_LEFT) && row >= 0) {
            *(pick ? &l_edit.pick_sel : &l_edit.finish_sel) = row;
            pc_const_edit_action(ACT_CONFIRM);
        } else if (pressed & PC_MOUSE_BUTTON_RIGHT) {
            pc_const_edit_action(ACT_CANCEL);
        }
        return;
    }

    /* looking around: the wheel, or the pointer at the edge */
    if (wheel != 0) {
        l_edit.heading = pc_edit_wrap(l_edit.heading - (f32)wheel * PC_EDIT_PAN_STEP);
    }
    if (mx >= 0 && mx < ww && my >= 0 && my < wh) {
        if (mx < ww * PC_EDIT_EDGE) {
            l_edit.heading = pc_edit_wrap(l_edit.heading - PC_EDIT_EDGE_PAN);
        } else if (mx > ww * (1.0f - PC_EDIT_EDGE)) {
            l_edit.heading = pc_edit_wrap(l_edit.heading + PC_EDIT_EDGE_PAN);
        }
    }

    if (l_edit.mode == PC_CONST_EDIT_VIEW) {
        if (pressed & (PC_MOUSE_BUTTON_LEFT | PC_MOUSE_BUTTON_RIGHT)) {
            pc_const_edit_action(ACT_CONFIRM);
        }
        return;
    }

    if (pc_mouse_moved() || wheel != 0 || (pressed & PC_MOUSE_BUTTON_LEFT)) {
        f32 az, up;
        pc_const_star_c s;

        pc_edit_window_to_sky(mx, my, ww, wh, &az, &up);
        if (pc_edit_nearest(az, up, 0.0f, 0.0f, &s)) {
            l_edit.cursor = s;
            l_edit.has_cursor = TRUE;
        }
    }
    if (pressed & PC_MOUSE_BUTTON_LEFT) {
        pc_const_edit_action(ACT_CONFIRM);
    } else if (pressed & PC_MOUSE_BUTTON_RIGHT) {
        pc_const_edit_action(ACT_CANCEL);
    }
}

/* --- drawing --- */

static void pc_edit_name(const pc_constellation_c* c, char* buf, int n) {
    int len = PC_CONST_NAME_LEN, i;

    while (len > 0 && c->name[len - 1] == ' ') {
        len--;
    }
    for (i = 0; i < len && i < n - 1; i++) {
        buf[i] = c->name[i] >= 32 && c->name[i] < 127 ? (char)c->name[i] : '?';
    }
    buf[i] = '\0';
}

void pc_const_edit_draw(struct game_s* game) {
    PCSkyGaze g;
    f32 others[PC_SKY_LINE_MAX][4];
    int n = 0, i, k, other_num;
    char buf[64];

    if (!l_edit.active) {
        return;
    }
    pc_edit_mouse();
    if (!l_edit.active) {
        return; /* a click closed it */
    }
    if (l_edit.mode == PC_CONST_EDIT_PICK) {
        pc_menu_dim_rect(game->graph, 180);
        pc_menu_draw_centered(game, "- Constellations -", 40.0f, 255, 255, 255, 255, 1.0f);
        for (i = 0; i < l_edit.pick_num; i++) {
            int r, gg, b, a;

            pc_edit_name(pc_const_get(l_edit.pick_slot[i]), buf, sizeof(buf));
            pc_menu_row_colors(i == l_edit.pick_sel, &r, &gg, &b, &a);
            pc_menu_draw_centered(game, buf, 64.0f + i * 14.0f, r, gg, b, a,
                                  i == l_edit.pick_sel ? PC_MENU_SCALE_SELECTED : 1.0f);
        }
        pc_menu_draw_centered(game, "A: this one.  B: never mind", 228.0f, 210, 210, 230, 230, 1.0f);
        return;
    }
    memset(&g, 0, sizeof(g));
    g.alpha = 1.0f;
    g.heading = l_edit.heading;
    g.pitch = PC_EDIT_PITCH;
    g.fov_y = PC_EDIT_FOV;
    g.zenith[0] = 0.04f, g.zenith[1] = 0.06f, g.zenith[2] = 0.20f;
    g.horizon[0] = 0.13f, g.horizon[1] = 0.17f, g.horizon[2] = 0.36f;
    g.night = 1.0f;
    g.turn = 0.0f; /* the telescope works in sky coordinates */

    /* the one being made first (gold), the pending line to the cursor, then everyone else's */
    if (l_edit.mode == PC_CONST_EDIT_MAKE) {
        for (i = 0; i < l_edit.work.link_num && n < PC_SKY_LINE_MAX; i++, n++) {
            pc_edit_star_pos(l_edit.work.link[i][0], &g.lines[n][0], &g.lines[n][1]);
            pc_edit_star_pos(l_edit.work.link[i][1], &g.lines[n][2], &g.lines[n][3]);
            g.lines[n][2] = g.lines[n][0] + pc_edit_wrap(g.lines[n][2] - g.lines[n][0]);
        }
        if (l_edit.has_picked && l_edit.has_cursor && !pc_edit_same(l_edit.picked, l_edit.cursor) && n < PC_SKY_LINE_MAX) {
            pc_edit_star_pos(l_edit.picked, &g.lines[n][0], &g.lines[n][1]);
            pc_edit_star_pos(l_edit.cursor, &g.lines[n][2], &g.lines[n][3]);
            g.lines[n][2] = g.lines[n][0] + pc_edit_wrap(g.lines[n][2] - g.lines[n][0]);
            n++;
        }
        g.edit_num = n;
        if (l_edit.has_cursor) {
            pc_edit_star_pos(l_edit.cursor, &g.hl[0], &g.hl[1]);
            g.hl_on |= 1;
        }
        if (l_edit.has_picked) {
            pc_edit_star_pos(l_edit.picked, &g.hl[2], &g.hl[3]);
            g.hl_on |= 2;
        }
    }
    other_num = pc_const_sky_lines(l_edit.heading, 0.6f, others, PC_SKY_LINE_MAX - n);
    for (i = 0; i < other_num; i++) {
        for (k = 0; k < 4; k++) {
            g.lines[n][k] = others[i][k];
        }
        n++;
    }
    g.line_num = n;
    pc_sky_set_gaze(&g);
    /* in the FONT list after the game's own (the clock), before this page's text */
    OPEN_DISP(game->graph);
    gDPNoOpTag(NEXT_FONT_DISP, PC_NOOP_STARGAZE);
    CLOSE_DISP(game->graph);

    /* text over the sky */
    if (l_edit.mode == PC_CONST_EDIT_VIEW) {
        pc_menu_draw_centered(game, "- Stargazing -", 16.0f, 255, 255, 255, 255, 1.0f);
        for (i = 0; i < PC_CONST_MAX; i++) {
            const pc_constellation_c* c = pc_const_get(i);

            if (c->used && c->link_num > 0 && fabsf(pc_edit_wrap(pc_edit_center(c) - l_edit.heading)) < 0.08f) {
                pc_edit_name(c, buf, sizeof(buf));
                pc_menu_draw_centered(game, buf, 190.0f, 255, 230, 150, 255, 1.0f);
                break;
            }
        }
        pc_menu_draw_centered(game, "Left and right look around.  A or B: done", 214.0f, 210, 210, 230, 230, 1.0f);
        return;
    }

    pc_menu_draw_centered(game, "- Celeste's Telescope -", 16.0f, 255, 255, 255, 255, 1.0f);
    snprintf(buf, sizeof(buf), "Lines: %d of %d", l_edit.work.link_num, PC_CONST_LINKS);
    pc_menu_draw_centered(game, buf, 30.0f, 255, 230, 150, 255, 1.0f);
    if (l_edit.finish_menu) {
        static const char* labels[FINISH_NUM] = { "Done!", "Keep going", "Start over", "Quit without saving" };

        pc_menu_dim_rect(game->graph, 120);
        for (i = 0; i < FINISH_NUM; i++) {
            int r, gg, b, a;

            pc_menu_row_colors(i == l_edit.finish_sel, &r, &gg, &b, &a);
            pc_menu_draw_centered(game, labels[i], 90.0f + i * 18.0f, r, gg, b, a,
                                  i == l_edit.finish_sel ? PC_MENU_SCALE_SELECTED : 1.0f);
        }
        return;
    }
    if (l_edit.status[0] != '\0' && SDL_GetTicks() < l_edit.status_until) {
        pc_menu_draw_centered(game, l_edit.status, 190.0f, 255, 190, 150, 255, 1.0f);
    }
    pc_menu_draw_centered(game, l_edit.has_picked ? "A: link to this star.  B: let go"
                                                  : "A: pick a star.  B: finish",
                          214.0f, 210, 210, 230, 230, 1.0f);
}
