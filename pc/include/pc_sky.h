#ifndef PC_SKY_H
#define PC_SKY_H

/* City Folk style outdoor view: the ground curves away from the camera like a
 * rolling log, and a sky is drawn behind it.
 *
 * The game marks its display lists with NoOp tags (see pc_platform.h for the
 * widescreen tags that work the same way):
 *   PC_NOOP_SKY          top of BG_OPA: draw the sky and start curving
 *   PC_NOOP_WORLD_BEGIN  start curving again (POLY_XLU)
 *   PC_NOOP_WORLD_END    stop curving (before menus, FONT, OVERLAY)
 * Orthographic draws are never curved. */
#define PC_NOOP_SKY         0xAC5712u
#define PC_NOOP_WORLD_BEGIN 0xAC5710u
#define PC_NOOP_WORLD_END   0xAC5711u
#define PC_NOOP_STARGAZE    0xAC5713u /* top of FONT: the stargazing view over everything drawn so far */

#ifdef __cplusplus
extern "C" {
#endif

struct game_play_s;

/* Game side (m_play.c), while building the frame's display lists */
void pc_sky_play_draw(struct game_play_s* play);
void pc_sky_play_draw_end(struct game_play_s* play);
/* Bends a world position the way the vertex shader will draw it this frame (copies it when the
 * world isn't curved), so culling sees what is actually on screen. in and out may be the same. */
void pc_sky_curve_world(const float in[3], float out[3]);
/* Fireworks (ef_hanabi_*.c) and shooting stars: whether they are drawn a second time up in the sky this frame, and the matrix
 * for each pass (sky FALSE: the GameCube's, flat where it burst; TRUE: in the sky, facing the camera) */
int pc_sky_fireworks(void);
void pc_sky_firework_place(struct game_s* game, const struct xyz_s* pos, const struct xyz_s* offset, int sky);
/* The sky pass's matrix for something at pos, as fireworks get it (also shooting stars, ef_shooting*.c): facing
 * the camera, scaled */
void pc_sky_place(struct game_s* game, const struct xyz_s* pos, float scale);
/* Fog off for what's drawn next (pc_sky_place does it; for a draw that sets its render mode after it) */
void pc_sky_no_fog(struct game_s* game);
/* After the sky pass: fog back on */
void pc_sky_firework_done(struct game_s* game);

/* Game side -> render side, once per outdoor frame.
 * center: the camera's look-at point; the ground stays flat there.
 * zenith/horizon: sky colours, 0-1. night: stars and moon, 0-1. overcast: rain/snow, 0-1. */
void pc_sky_set_frame(const float center[3], const float zenith[3], const float horizon[3], float curve_k, float night,
                      float overcast);
void pc_sky_cancel_frame(void); /* not outdoors this frame */
void pc_sky_set_rainbow(float strength); /* 0-1, after pc_sky_set_frame */
/* How far the sky has turned, and constellation links near the view (sky coordinates), after pc_sky_set_frame */
#define PC_SKY_LINE_MAX 48
void pc_sky_set_stars(float turn, const float (*lines)[4], int line_num);

/* The full-screen sky view of Celeste's telescope (pc_constellation_edit.c) */
typedef struct {
    float alpha;
    float heading, pitch, fov_y; /* radians; heading as the sky's azimuth before turning */
    float zenith[3], horizon[3];
    float night, overcast, turn;
    int line_num, edit_num;
    float lines[PC_SKY_LINE_MAX][4];
    float hl[4];
    int hl_on;
} PCSkyGaze;
/* Set it, then put a PC_NOOP_STARGAZE tag where it should be drawn; it stays until set again */
void pc_sky_set_gaze(const PCSkyGaze* gaze);

/* Render side */
void pc_sky_noop(unsigned int tag, const float proj[4][4], const float view[3][4]);
void pc_sky_frame_reset(void);
/* Fills the shaders' curve uniforms; strength (up[3]) and haze[3] are 0 when off */
void pc_sky_curve_uniforms(int perspective, float up[4], float fwd[4], float haze[4]);
/* Bends an eye-space point the same way the vertex shader does */
void pc_sky_curve_point(int perspective, float* x, float* y, float* z);

#ifdef __cplusplus
}
#endif

#endif
