/* pc_acplus_gx.h - small helpers for ac-plus's own draws through Aurora's GX (pc_acplus_gx.c): the City Folk sky,
 * rooms and characters (pc_sky.c, pc_cf_room.c, pc_cf_char.c).
 *
 * They draw in the middle of emu64's display lists (at NoOp tags). What they change of the GX state, emu64 sends
 * again afterwards (emu64::pc_restore_after_host_draw, called from its NoOp handler). They draw with their own
 * vertex format (PC_GXH_VTXFMT) and matrix slot (PC_GXH_PNMTX), which emu64 doesn't use. */
#ifndef PC_ACPLUS_GX_H
#define PC_ACPLUS_GX_H

#include <dolphin/gx.h>
#include <dolphin/gx/GXGet.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PC_GXH_VTXFMT GX_VTXFMT7
#define PC_GXH_PNMTX GX_PNMTX5

/* A texture from 8-bit RGBA pixels (row 0 at the top), kept for as long as the game runs (px is copied).
 * wrap: 0 clamp, 1 repeat, 2 mirror. mip: with a mipmap chain (trilinear) */
typedef struct {
    GXTexObj obj;
    void* data;
    int w, h;
    int ready;
} PCGXHTex;

void pc_gxh_tex_init(PCGXHTex* t, const unsigned char* px, int w, int h, int wrap_s, int wrap_t, int mip);
/* the wrap modes of an existing texture changed (the same pixels) */
void pc_gxh_tex_set_wrap(PCGXHTex* t, int wrap_s, int wrap_t);

/* State for a run of plain triangles: position (f32 xyz), colour (rgba8) and, with tex, one texture coordinate (f32
 * st) per vertex. Colour = vertex colour (x2: pass colours halved to go up to 2.0) times the texture.
 * blend: 0 none, 1 alpha, 2 additive (src alpha, one), 3 additive (one, one). alpha_test: drop texels under half alpha.
 * zmode: 0 off, 1 test only, 2 test and write. cull: GX_CULL_* */
void pc_gxh_mesh_state(const PCGXHTex* tex, int blend, int alpha_test, int zmode, GXCullMode cull);

/* The game's camera for what's drawn next: projection as emu64 has it (made wider for widescreen as emu64 does),
 * view (world -> eye) into PC_GXH_PNMTX */
void pc_gxh_camera(const float proj[4][4], const float view[3][4]);
/* Screen space for what's drawn next: positions are (x, y) in -1..1 (y up), z ignored; over the whole window
 * (the viewport and scissor are kept, and put back by pc_gxh_screen_end) */
void pc_gxh_screen(void);
void pc_gxh_screen_end(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_ACPLUS_GX_H */
