#include "pc_menu_util.h"
#include "pc_text_draw.h"

#include "graph.h"
#include "m_font.h"
#include "m_rcp.h"
#include "main.h" /* SCREEN_WIDTH_F */

void pc_menu_dim_rect(GRAPH* graph, int alpha) {
    extern GAME* gamePT;

    if (gamePT == NULL || gamePT->graph != graph) return;
    /* past the 4:3 screen on both sides: the widescreen margins too */
    pc_text_fill(gamePT, -320.0f, -8.0f, 640.0f, 248.0f, 0, 0, 0, alpha);
}

void pc_menu_row_colors(int selected, int* r, int* g, int* b, int* a) {
    if (selected) { *r = 255; *g = 235; *b = 120; *a = 255; }
    else          { *r = 200; *g = 200; *b = 200; *a = 200; }
}

void pc_menu_draw_centered(struct game_s* game, const char* s, f32 y,
                           int r, int g, int b, int a, f32 scale) {
    f32 w = (f32)pc_text_width(s) * scale;
    f32 x = (SCREEN_WIDTH_F - w) * 0.5f;
    pc_text_draw(game, s, x, y, r, g, b, a, scale);
}

void pc_menu_draw_left(struct game_s* game, const char* s, f32 x, f32 y,
                       int r, int g, int b, int a, f32 scale) {
    pc_text_draw(game, s, x, y, r, g, b, a, scale);
}

void pc_menu_draw_two_choice(struct game_s* game, const char* left,
                             const char* right, int sel, f32 y) {
    int r, g, b, a;
    f32 gap = 70.0f;
    f32 cx = SCREEN_WIDTH_F * 0.5f;
    f32 left_x  = cx - gap - (f32)pc_text_width(left);
    f32 right_x = cx + gap;

    pc_menu_row_colors(sel == 0, &r, &g, &b, &a);
    pc_menu_draw_left(game, left, left_x, y, r, g, b, a,
                      sel == 0 ? PC_MENU_SCALE_SELECTED : 1.0f);
    pc_menu_row_colors(sel == 1, &r, &g, &b, &a);
    pc_menu_draw_left(game, right, right_x, y, r, g, b, a,
                      sel == 1 ? PC_MENU_SCALE_SELECTED : 1.0f);
}
