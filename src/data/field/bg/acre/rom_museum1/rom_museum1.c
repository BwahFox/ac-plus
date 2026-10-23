#include "libforest/gbi_extensions.h"
#include <stdlib.h>
#include <string.h>

#ifdef TARGET_PC
static u16 rom_museum1_mado_pal[0x20 / sizeof(u16)] ATTRIBUTE_ALIGN(32);
#else
static u16 rom_museum1_mado_pal[] ATTRIBUTE_ALIGN(32) = {
#include "assets/field/bg/rom_museum1_mado_pal.inc"
};
#endif

#ifdef TARGET_PC
static u16 rom_museum1_step_pal[0x20 / sizeof(u16)] ATTRIBUTE_ALIGN(32);
#else
static u16 rom_museum1_step_pal[] ATTRIBUTE_ALIGN(32) = {
#include "assets/field/bg/rom_museum1_step_pal.inc"
};
#endif

#ifdef TARGET_PC
static u16 rom_museum1_floor_pal[0x20 / sizeof(u16)] ATTRIBUTE_ALIGN(32);
#else
static u16 rom_museum1_floor_pal[] ATTRIBUTE_ALIGN(32) = {
#include "assets/field/bg/rom_museum1_floor_pal.inc"
};
#endif

#ifdef TARGET_PC
static u16 rom_museum1_wall_pal[0x20 / sizeof(u16)] ATTRIBUTE_ALIGN(32);
#else
static u16 rom_museum1_wall_pal[] ATTRIBUTE_ALIGN(32) = {
#include "assets/field/bg/rom_museum1_wall_pal.inc"
};
#endif

#ifdef TARGET_PC
static u8 rom_museum1_mado1_tex[0x800] ATTRIBUTE_ALIGN(32);
#else
static u8 rom_museum1_mado1_tex[] ATTRIBUTE_ALIGN(32) = {
#include "assets/field/bg/rom_museum1_mado1_tex.inc"
};
#endif

#ifdef TARGET_PC
static u8 rom_museum1_step1_tex[0x800] ATTRIBUTE_ALIGN(32);
#else
static u8 rom_museum1_step1_tex[] ATTRIBUTE_ALIGN(32) = {
#include "assets/field/bg/rom_museum1_step1_tex.inc"
};
#endif

#ifdef TARGET_PC
static u8 rom_museum1_step2_tex[0x400] ATTRIBUTE_ALIGN(32);
#else
static u8 rom_museum1_step2_tex[] ATTRIBUTE_ALIGN(32) = {
#include "assets/field/bg/rom_museum1_step2_tex.inc"
};
#endif

#ifdef TARGET_PC
static u8 rom_museum1_floorA_tex[0x800] ATTRIBUTE_ALIGN(32);
#else
static u8 rom_museum1_floorA_tex[] ATTRIBUTE_ALIGN(32) = {
#include "assets/field/bg/rom_museum1_floorA_tex.inc"
};
#endif

#ifdef TARGET_PC
static u8 rom_museum1_floorB_tex[0x800] ATTRIBUTE_ALIGN(32);
#else
static u8 rom_museum1_floorB_tex[] ATTRIBUTE_ALIGN(32) = {
#include "assets/field/bg/rom_museum1_floorB_tex.inc"
};
#endif

#ifdef TARGET_PC
static u8 rom_museum1_mado2_tex[0x800] ATTRIBUTE_ALIGN(32);
#else
static u8 rom_museum1_mado2_tex[] ATTRIBUTE_ALIGN(32) = {
#include "assets/field/bg/rom_museum1_mado2_tex.inc"
};
#endif

#ifdef TARGET_PC
static u8 rom_museum1_sign1_tex[0x400] ATTRIBUTE_ALIGN(32);
#else
static u8 rom_museum1_sign1_tex[] ATTRIBUTE_ALIGN(32) = {
#include "assets/field/bg/rom_museum1_sign1_tex.inc"
};
#endif

#ifdef TARGET_PC
static u8 rom_museum1_wallA_tex[0x800] ATTRIBUTE_ALIGN(32);
#else
static u8 rom_museum1_wallA_tex[] ATTRIBUTE_ALIGN(32) = {
#include "assets/field/bg/rom_museum1_wallA_tex.inc"
};
#endif

#ifdef TARGET_PC
static u8 rom_museum1_wallB_tex[0x800] ATTRIBUTE_ALIGN(32);
#else
static u8 rom_museum1_wallB_tex[] ATTRIBUTE_ALIGN(32) = {
#include "assets/field/bg/rom_museum1_wallB_tex.inc"
};
#endif

#ifdef TARGET_PC
static u8 rom_museum1_sign2_tex[0x400] ATTRIBUTE_ALIGN(32);
#else
static u8 rom_museum1_sign2_tex[] ATTRIBUTE_ALIGN(32) = {
#include "assets/field/bg/rom_museum1_sign2_tex.inc"
};
#endif

#ifdef TARGET_PC
static u8 rom_museum1_sign3_tex[0x400] ATTRIBUTE_ALIGN(32);
#else
static u8 rom_museum1_sign3_tex[] ATTRIBUTE_ALIGN(32) = {
#include "assets/field/bg/rom_museum1_sign3_tex.inc"
};
#endif

#ifdef TARGET_PC
static u8 rom_museum1_sign4_tex[0x400] ATTRIBUTE_ALIGN(32);
#else
static u8 rom_museum1_sign4_tex[] ATTRIBUTE_ALIGN(32) = {
#include "assets/field/bg/rom_museum1_sign4_tex.inc"
};
#endif

#ifdef TARGET_PC
static Vtx rom_museum1_v[0x12C0 / sizeof(Vtx)];
#else
static Vtx rom_museum1_v[] = {
#include "assets/field/bg/rom_museum1_v.inc"
};
#endif

#ifdef TARGET_PC
/* ac-plus: the stairs doorways (below); triangles they replace are left out on PC */
#define PC_HOLE(i) 0
static Gfx pc_rom_museum1_stairs[];
#else
#define PC_HOLE(i) i
#endif

extern Gfx rom_museum1_modelT[] = {
    gsSPTexture(0, 0, 0, 0, G_ON),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED, 1, 0, PRIM_LOD_FRAC, COMBINED),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_ZB_XLU_SURF2),
    gsDPLoadTLUT_Dolphin(15, 16, 1, rom_museum1_mado_pal),
    gsDPLoadTextureBlock_4b_Dolphin(rom_museum1_mado2_tex, G_IM_FMT_CI, 64, 64, 15, GX_CLAMP, GX_CLAMP, 0, 0),
    gsSPLoadGeometryMode(G_ZBUFFER | G_SHADE | G_CULL_BACK | G_FOG | G_SHADING_SMOOTH),
    gsSPVertex(&rom_museum1_v[268], 16, 0),
    gsSPNTriangles(8,       // tri count
                   0, 1, 2, // tri0
                   3, 4, 5, // tri1
                   3, 5, 6  // tri2
                   ),
    gsSPNTriangles_5b(0, 2, 7,    // tri0
                      PC_HOLE(8), PC_HOLE(9), PC_HOLE(10),   // tri1
                      PC_HOLE(11), PC_HOLE(12), PC_HOLE(13), // tri2
                      PC_HOLE(8), PC_HOLE(10), PC_HOLE(14)   // tri3
                      ),
    gsSPNTriangles_5b(PC_HOLE(11), PC_HOLE(13), PC_HOLE(15), // tri0
                      0, 0, 0,    // tri1
                      0, 0, 0,    // tri2
                      0, 0, 0     // tri3
                      ),
    gsDPLoadTextureBlock_4b_Dolphin(rom_museum1_mado1_tex, G_IM_FMT_CI, 64, 64, 15, GX_CLAMP, GX_CLAMP, 0, 0),
    gsSPVertex(&rom_museum1_v[284], 16, 0),
    gsSPNTriangles(8,       // tri count
                   0, 1, 2, // tri0
                   1, 3, 2, // tri1
                   4, 5, 6  // tri2
                   ),
    gsSPNTriangles_5b(7, 8, 9,    // tri0
                      10, 11, 12, // tri1
                      13, 4, 6,   // tri2
                      14, 7, 9    // tri3
                      ),
    gsSPNTriangles_5b(11, 15, 12, // tri0
                      0, 0, 0,    // tri1
                      0, 0, 0,    // tri2
                      0, 0, 0     // tri3
                      ),
    gsSPEndDisplayList(),
};

extern Gfx rom_museum1_model[] = {
    gsSPTexture(0, 0, 0, 0, G_ON),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0, 0, 0, 0, TEXEL0, PRIMITIVE, 0, COMBINED, 0, 0, 0, 0, COMBINED),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2),
    gsSPLoadGeometryMode(G_ZBUFFER | G_SHADE | G_CULL_BACK | G_FOG | G_SHADING_SMOOTH),
    gsSPVertex(&rom_museum1_v[0], 16, 0),
    gsSPNTriangles(8,       // tri count
                   0, 1, 2, // tri0
                   0, 3, 1, // tri1
                   4, 5, 6  // tri2
                   ),
    gsSPNTriangles_5b(5, 7, 6,   // tri0
                      8, 9, 10,  // tri1
                      11, 8, 10, // tri2
                      12, 13, 14 // tri3
                      ),
    gsSPNTriangles_5b(12, 14, 15, // tri0
                      0, 0, 0,    // tri1
                      0, 0, 0,    // tri2
                      0, 0, 0     // tri3
                      ),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_TEX_EDGE2),
    gsDPLoadTLUT_Dolphin(15, 16, 1, rom_museum1_step_pal),
    gsDPLoadTextureBlock_4b_Dolphin(rom_museum1_sign4_tex, G_IM_FMT_CI, 64, 32, 15, GX_CLAMP, GX_CLAMP, 0, 0),
    gsSPVertex(&rom_museum1_v[16], 20, 0),
    gsSPNTriangles(3,       // tri count
                   0, 1, 2, // tri0
                   0, 3, 4, // tri1
                   0, 2, 3  // tri2
                   ),
    gsDPLoadTextureBlock_4b_Dolphin(rom_museum1_sign3_tex, G_IM_FMT_CI, 64, 32, 15, GX_CLAMP, GX_CLAMP, 0, 0),
    gsSPNTriangles(3,       // tri count
                   5, 6, 7, // tri0
                   7, 8, 9, // tri1
                   5, 7, 9  // tri2
                   ),
    gsDPLoadTextureBlock_4b_Dolphin(rom_museum1_sign2_tex, G_IM_FMT_CI, 64, 32, 15, GX_CLAMP, GX_CLAMP, 0, 0),
    gsSPNTriangles(3,          // tri count
                   10, 11, 12, // tri0
                   12, 13, 10, // tri1
                   10, 14, 11  // tri2
                   ),
    gsDPLoadTextureBlock_4b_Dolphin(rom_museum1_sign1_tex, G_IM_FMT_CI, 64, 32, 15, GX_CLAMP, GX_CLAMP, 0, 0),
    gsSPNTriangles(3,          // tri count
                   15, 16, 17, // tri0
                   17, 18, 15, // tri1
                   15, 19, 16  // tri2
                   ),
    gsDPLoadTextureBlock_4b_Dolphin(rom_museum1_step2_tex, G_IM_FMT_CI, 64, 32, 15, GX_REPEAT, GX_REPEAT, 0, 0),
    gsSPVertex(&rom_museum1_v[36], 27, 0),
    gsSPNTriangles(14,      // tri count
                   0, 1, 2, // tri0
                   2, 3, 4, // tri1
                   4, 5, 2  // tri2
                   ),
    gsSPNTriangles_5b(5, 0, 2, // tri0
                      6, 7, 8, // tri1
                      6, 9, 7, // tri2
                      8, 10, 6 // tri3
                      ),
    gsSPNTriangles_5b(6, 11, 9,   // tri0
                      12, 13, 14, // tri1
                      15, 16, 12, // tri2
                      12, 14, 15  // tri3
                      ),
    gsSPNTriangles_5b(17, 18, 19, // tri0
                      19, 20, 17, // tri1
                      17, 21, 18, // tri2
                      0, 0, 0     // tri3
                      ),
    gsDPLoadTextureBlock_4b_Dolphin(rom_museum1_step1_tex, G_IM_FMT_CI, 64, 64, 15, GX_CLAMP, GX_CLAMP, 0, 0),
    gsSPNTriangles(3,          // tri count
                   22, 23, 24, // tri0
                   24, 25, 22, // tri1
                   22, 26, 23  // tri2
                   ),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2),
    gsDPLoadTLUT_Dolphin(15, 16, 1, rom_museum1_wall_pal),
    gsDPLoadTextureBlock_4b_Dolphin(rom_museum1_wallA_tex, G_IM_FMT_CI, 64, 64, 15, GX_REPEAT, GX_CLAMP, 0, 0),
    gsSPVertex(&rom_museum1_v[63], 32, 0),
    gsSPNTriangles(24,      // tri count
                   0, 1, 2, // tri0
                   3, 4, 5, // tri1
                   6, 0, 7  // tri2
                   ),
    gsSPNTriangles_5b(4, 8, 9,  // tri0
                      10, 6, 7, // tri1
                      8, 11, 9, // tri2
                      12, 2, 1  // tri3
                      ),
    gsSPNTriangles_5b(5, 13, 3, // tri0
                      2, 14, 0, // tri1
                      4, 15, 5, // tri2
                      0, 16, 7  // tri3
                      ),
    gsSPNTriangles_5b(17, 4, 9,   // tri0
                      18, 19, 14, // tri1
                      20, 15, 21, // tri2
                      15, 4, 21   // tri3
                      ),
    gsSPNTriangles_5b(22, 23, 17, // tri0
                      23, 4, 17,  // tri1
                      24, 25, 16, // tri2
                      0, 24, 16   // tri3
                      ),
    gsSPNTriangles_5b(19, 0, 14, // tri0
                      26, 10, 7, // tri1
                      0, 19, 24, // tri2
                      12, 27, 2  // tri3
                      ),
    gsSPNTriangles_5b(28, 29, 30, // tri0
                      0, 0, 0,    // tri1
                      0, 0, 0,    // tri2
                      0, 0, 0     // tri3
                      ),
    gsSPVertex(&rom_museum1_v[94], 18, 0),
    gsSPNTriangles(11,      // tri count
                   0, 1, 2, // tri0
                   0, 3, 1, // tri1
                   3, 4, 1  // tri2
                   ),
    gsSPNTriangles_5b(4, 2, 1, // tri0
                      3, 5, 6, // tri1
                      5, 7, 6, // tri2
                      3, 8, 5  // tri3
                      ),
    gsSPNTriangles_5b(8, 7, 5,    // tri0
                      9, 10, 11,  // tri1
                      12, 13, 14, // tri2
                      15, 16, 17  // tri3
                      ),
    gsDPLoadTextureBlock_4b_Dolphin(rom_museum1_wallB_tex, G_IM_FMT_CI, 64, 64, 15, GX_REPEAT, GX_CLAMP, 0, 0),
    gsSPVertex(&rom_museum1_v[112], 32, 0),
    gsSPNTriangles(21,      // tri count
                   0, 1, 2, // tri0
                   0, 3, 1, // tri1
                   4, 5, 6  // tri2
                   ),
    gsSPNTriangles_5b(5, 7, 6,   // tri0
                      PC_HOLE(8), PC_HOLE(9), PC_HOLE(10),  // tri1
                      PC_HOLE(9), PC_HOLE(11), PC_HOLE(10), // tri2
                      12, 13, 14 // tri3
                      ),
    gsSPNTriangles_5b(13, 15, 14, // tri0
                      PC_HOLE(8), PC_HOLE(16), PC_HOLE(9),   // tri1
                      PC_HOLE(8), PC_HOLE(17), PC_HOLE(16),  // tri2
                      PC_HOLE(17), PC_HOLE(18), PC_HOLE(16)  // tri3
                      ),
    gsSPNTriangles_5b(PC_HOLE(17), PC_HOLE(19), PC_HOLE(18), // tri0
                      PC_HOLE(20), PC_HOLE(11), PC_HOLE(9),  // tri1
                      21, 22, 23, // tri2
                      22, 24, 23  // tri3
                      ),
    gsSPNTriangles_5b(14, 15, 25, // tri0
                      22, 12, 24, // tri1
                      12, 14, 24, // tri2
                      26, 27, 28  // tri3
                      ),
    gsSPNTriangles_5b(29, 26, 28, // tri0
                      29, 28, 30, // tri1
                      0, 0, 0,    // tri2
                      0, 0, 0     // tri3
                      ),
    gsSPVertex(&rom_museum1_v[143], 32, 0),
    gsSPNTriangles(20,      // tri count
                   0, 1, 2, // tri0
                   1, 3, 2, // tri1
                   4, 5, 6  // tri2
                   ),
    gsSPNTriangles_5b(5, 7, 6,   // tri0
                      5, 8, 7,   // tri1
                      9, 0, 2,   // tri2
                      10, 11, 12 // tri3
                      ),
    gsSPNTriangles_5b(10, 13, 11, // tri0
                      13, 14, 11, // tri1
                      15, 16, 17, // tri2
                      18, 15, 17  // tri3
                      ),
    gsSPNTriangles_5b(15, 19, 16, // tri0
                      PC_HOLE(20), PC_HOLE(21), PC_HOLE(22), // tri1
                      PC_HOLE(23), PC_HOLE(20), PC_HOLE(22), // tri2
                      PC_HOLE(23), PC_HOLE(22), PC_HOLE(24)  // tri3
                      ),
    gsSPNTriangles_5b(PC_HOLE(25), PC_HOLE(23), PC_HOLE(24), // tri0
                      PC_HOLE(25), PC_HOLE(26), PC_HOLE(27), // tri1
                      PC_HOLE(28), PC_HOLE(26), PC_HOLE(25), // tri2
                      PC_HOLE(28), PC_HOLE(25), PC_HOLE(24)  // tri3
                      ),
    gsSPNTriangles_5b(29, 30, 31, // tri0
                      0, 0, 0,    // tri1
                      0, 0, 0,    // tri2
                      0, 0, 0     // tri3
                      ),
    gsSPVertex(&rom_museum1_v[175], 8, 0),
    gsSPNTriangles(6,       // tri count
                   0, 1, 2, // tri0
                   0, 2, 3, // tri1
                   4, 0, 3  // tri2
                   ),
    gsSPNTriangles_5b(4, 3, 5, // tri0
                      6, 4, 5, // tri1
                      6, 5, 7, // tri2
                      0, 0, 0  // tri3
                      ),
    gsDPLoadTLUT_Dolphin(15, 16, 1, rom_museum1_floor_pal),
    gsDPLoadTextureBlock_4b_Dolphin(rom_museum1_floorB_tex, G_IM_FMT_CI, 64, 64, 15, GX_CLAMP, GX_CLAMP, 0, 0),
    gsSPVertex(&rom_museum1_v[183], 32, 0),
    gsSPNTriangles(20,      // tri count
                   0, 1, 2, // tri0
                   0, 3, 1, // tri1
                   3, 4, 1  // tri2
                   ),
    gsSPNTriangles_5b(5, 6, 7,   // tri0
                      6, 8, 7,   // tri1
                      8, 9, 7,   // tri2
                      10, 11, 12 // tri3
                      ),
    gsSPNTriangles_5b(10, 13, 11, // tri0
                      11, 14, 12, // tri1
                      15, 16, 17, // tri2
                      16, 18, 17  // tri3
                      ),
    gsSPNTriangles_5b(16, 19, 18, // tri0
                      20, 21, 22, // tri1
                      20, 23, 21, // tri2
                      24, 25, 26  // tri3
                      ),
    gsSPNTriangles_5b(24, 27, 25, // tri0
                      20, 22, 28, // tri1
                      26, 29, 24, // tri2
                      3, 30, 4    // tri3
                      ),
    gsSPNTriangles_5b(14, 31, 12, // tri0
                      0, 0, 0,    // tri1
                      0, 0, 0,    // tri2
                      0, 0, 0     // tri3
                      ),
    gsSPVertex(&rom_museum1_v[215], 10, 0),
    gsSPNTriangles(4,       // tri count
                   0, 1, 2, // tri0
                   3, 4, 5, // tri1
                   6, 7, 8  // tri2
                   ),
    gsSPNTriangles_5b(7, 9, 8, // tri0
                      0, 0, 0, // tri1
                      0, 0, 0, // tri2
                      0, 0, 0  // tri3
                      ),
    gsDPLoadTextureBlock_4b_Dolphin(rom_museum1_floorA_tex, G_IM_FMT_CI, 64, 64, 15, GX_CLAMP, GX_CLAMP, 0, 0),
    gsSPVertex(&rom_museum1_v[225], 27, 0),
    gsSPNTriangles(16,      // tri count
                   0, 1, 2, // tri0
                   0, 2, 3, // tri1
                   4, 0, 3  // tri2
                   ),
    gsSPNTriangles_5b(5, 6, 7,   // tri0
                      8, 9, 10,  // tri1
                      9, 11, 12, // tri2
                      13, 14, 15 // tri3
                      ),
    gsSPNTriangles_5b(16, 13, 17, // tri0
                      6, 18, 7,   // tri1
                      19, 20, 21, // tri2
                      22, 19, 21  // tri3
                      ),
    gsSPNTriangles_5b(22, 21, 14, // tri0
                      23, 24, 25, // tri1
                      23, 26, 24, // tri2
                      10, 9, 12   // tri3
                      ),
    gsSPNTriangles_5b(17, 13, 15, // tri0
                      0, 0, 0,    // tri1
                      0, 0, 0,    // tri2
                      0, 0, 0     // tri3
                      ),
    gsSPVertex(&rom_museum1_v[252], 16, 0),
    gsSPNTriangles(12,      // tri count
                   0, 1, 2, // tri0
                   PC_HOLE(1), PC_HOLE(3), PC_HOLE(2), // tri1
                   PC_HOLE(3), PC_HOLE(4), PC_HOLE(2)  // tri2
                   ),
    gsSPNTriangles_5b(5, 6, 1, // tri0
                      7, 6, 5, // tri1
                      5, 1, 0, // tri2
                      PC_HOLE(8), PC_HOLE(9), PC_HOLE(10) // tri3
                      ),
    gsSPNTriangles_5b(PC_HOLE(8), PC_HOLE(11), PC_HOLE(9),   // tri0
                      8, 10, 12,  // tri1
                      12, 10, 13, // tri2
                      10, 14, 13  // tri3
                      ),
    gsSPNTriangles_5b(13, 14, 15, // tri0
                      0, 0, 0,    // tri1
                      0, 0, 0,    // tri2
                      0, 0, 0     // tri3
                      ),
#ifdef TARGET_PC
    gsSPDisplayList(pc_rom_museum1_stairs),
#endif
    gsSPEndDisplayList(),
};

#ifdef TARGET_PC
/* ac-plus: doorways in the front half of the side walls, with steps down on the left (to the
 * café) and up on the right (to the observatory), like the wing doorways: a short corridor
 * through the thick wall (x 40-120 and 360-440, z 360-440). The wall panels and the lower half
 * of the front windows there are left out (PC_HOLE), the window's upper half stays above the
 * doorway, and the pieces below are built at startup from quads in world units. */
enum { PC_ST_STEP, PC_ST_WALL, PC_ST_CAP, PC_ST_GROUPS };

typedef struct {
    s16 p[4][3]; /* world units */
    s16 tc[4][2];
    const u8* cn;
} pc_st_quad_c;

#define PC_ST_MAX_QUADS 48
static Vtx pc_rom_museum1_stairs_v[PC_ST_MAX_QUADS * 4];
static Gfx pc_rom_museum1_stairs[64 + PC_ST_MAX_QUADS * 2];
static pc_st_quad_c pc_st_quads[PC_ST_GROUPS][PC_ST_MAX_QUADS];
static int pc_st_num[PC_ST_GROUPS];

static void pc_st_add(int group, s16 x0, s16 y0, s16 z0, s16 x1, s16 y1, s16 z1, s16 x2, s16 y2, s16 z2, s16 x3, s16 y3,
                      s16 z3, s16 su, s16 tv, const u8* cn) {
    pc_st_quad_c* q = &pc_st_quads[group][pc_st_num[group]++];
    s16 p[4][3] = { { x0, y0, z0 }, { x1, y1, z1 }, { x2, y2, z2 }, { x3, y3, z3 } };
    /* texture: su texels*32 across (p0 -> p3), tv down (p0 -> p1) */
    s16 tc[4][2] = { { 0, 0 }, { 0, tv }, { su, tv }, { su, 0 } };

    memcpy(q->p, p, sizeof(p));
    memcpy(q->tc, tc, sizeof(tc));
    q->cn = cn;
}

/* texel*32 for a length in world units: steps 64 across a tile (40), walls 64 a tile both ways */
#define PC_ST_TC(len) ((s16)((len) * 2048 / 40))

static void pc_rom_museum1_build_stairs(void) {
    const u8* step_cn = rom_museum1_v[233].v.cn;
    const u8* wall_l = rom_museum1_v[163].v.cn;
    const u8* wall_r = rom_museum1_v[120].v.cn;
    const u8* cap_cn = rom_museum1_v[252].v.cn;
    Gfx* g = pc_rom_museum1_stairs;
    int k, grp, n, i;

    /* the window backing behind the front windows: only above the doorway now */
    rom_museum1_v[9].v.ob[1] = rom_museum1_v[10].v.ob[1] = 120 * 16;
    rom_museum1_v[12].v.ob[1] = rom_museum1_v[13].v.ob[1] = 120 * 16;

    /* left: four steps down to y 0 going west; risers face the room */
    for (k = 0; k < 4; k++) {
        s16 xe = 120 - 20 * k, xw = xe - 20, yt = 40 - 10 * k, yb = yt - 10;

        pc_st_add(PC_ST_STEP, xe, yt, 360, xe, yb, 360, xe, yb, 440, xe, yt, 440, PC_ST_TC(80), PC_ST_TC(10) / 2,
                  step_cn);
        pc_st_add(PC_ST_STEP, xe, yb, 360, xw, yb, 360, xw, yb, 440, xe, yb, 440, PC_ST_TC(80), PC_ST_TC(20) / 2,
                  step_cn);
        /* the steps' side, seen through the open front */
        pc_st_add(PC_ST_WALL, xw, yb, 440, xw, 0, 440, xe, 0, 440, xe, yb, 440, PC_ST_TC(20), PC_ST_TC(yb), wall_l);
    }
    /* right: four steps up to y 80 going east */
    for (k = 0; k < 4; k++) {
        s16 xw = 360 + 20 * k, xe = xw + 20, yb = 40 + 10 * k, yt = yb + 10;

        pc_st_add(PC_ST_STEP, xw, yt, 440, xw, yb, 440, xw, yb, 360, xw, yt, 360, PC_ST_TC(80), PC_ST_TC(10) / 2,
                  step_cn);
        pc_st_add(PC_ST_STEP, xw, yt, 360, xe, yt, 360, xe, yt, 440, xw, yt, 440, PC_ST_TC(80), PC_ST_TC(20) / 2,
                  step_cn);
        pc_st_add(PC_ST_WALL, xw, yt, 440, xw, 40, 440, xe, 40, 440, xe, yt, 440, PC_ST_TC(20), PC_ST_TC(yt - 40),
                  wall_r);
    }
    /* the corridors' back walls (facing the camera), and the wall between each wing door and its stairs */
    pc_st_add(PC_ST_WALL, 40, 120, 360, 40, 0, 360, 120, 0, 360, 120, 120, 360, PC_ST_TC(80), PC_ST_TC(120), wall_l);
    pc_st_add(PC_ST_WALL, 360, 120, 360, 360, 40, 360, 440, 40, 360, 440, 120, 360, PC_ST_TC(80), PC_ST_TC(80), wall_r);
    pc_st_add(PC_ST_WALL, 120, 120, 320, 120, 40, 320, 120, 40, 360, 120, 120, 360, PC_ST_TC(40), PC_ST_TC(80), wall_l);
    pc_st_add(PC_ST_WALL, 360, 120, 360, 360, 40, 360, 360, 40, 320, 360, 120, 320, PC_ST_TC(40), PC_ST_TC(80), wall_r);
    /* the walls' front faces above the doorways (the halves the cut took away) */
    pc_st_add(PC_ST_CAP, 40, 200, 440, 40, 120, 440, 120, 120, 440, 120, 120, 440, PC_ST_TC(80), PC_ST_TC(80), cap_cn);
    pc_st_add(PC_ST_CAP, 360, 120, 440, 440, 120, 440, 440, 200, 440, 440, 200, 440, PC_ST_TC(80), PC_ST_TC(80), cap_cn);

    if (getenv("PC_STAIRS_OFF") != NULL) { /* dev: just the cuts */
        memset(pc_st_num, 0, sizeof(pc_st_num));
    }
    /* -> vertices (model units: 16 per world unit) and a display list */
    gSPLoadGeometryMode(g++, G_ZBUFFER | G_SHADE | G_FOG | G_SHADING_SMOOTH); /* both sides */
    gDPSetRenderMode(g++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2);
    n = 0;
    for (grp = 0; grp < PC_ST_GROUPS; grp++) {
        switch (grp) {
            case PC_ST_STEP:
                gDPLoadTLUT_Dolphin(g++, 15, 16, 1, rom_museum1_step_pal);
                gDPLoadTextureBlock_4b_Dolphin(g++, rom_museum1_step2_tex, G_IM_FMT_CI, 64, 32, 15, GX_REPEAT, GX_REPEAT, 0, 0);
                break;
            case PC_ST_WALL:
                gDPLoadTLUT_Dolphin(g++, 15, 16, 1, rom_museum1_wall_pal);
                gDPLoadTextureBlock_4b_Dolphin(g++, rom_museum1_wallB_tex, G_IM_FMT_CI, 64, 64, 15, GX_REPEAT, GX_REPEAT, 0, 0);
                break;
            case PC_ST_CAP:
                gDPLoadTLUT_Dolphin(g++, 15, 16, 1, rom_museum1_floor_pal);
                gDPLoadTextureBlock_4b_Dolphin(g++, rom_museum1_floorA_tex, G_IM_FMT_CI, 64, 64, 15, GX_REPEAT, GX_REPEAT, 0, 0);
                break;
        }
        for (k = 0; k < pc_st_num[grp]; k += 8) {
            int batch = pc_st_num[grp] - k < 8 ? pc_st_num[grp] - k : 8;
            Vtx* v0 = &pc_rom_museum1_stairs_v[n];

            for (i = 0; i < batch * 4; i++) {
                const pc_st_quad_c* q = &pc_st_quads[grp][k + i / 4];
                Vtx* v = &pc_rom_museum1_stairs_v[n++];

                v->v.ob[0] = q->p[i % 4][0] * 16, v->v.ob[1] = q->p[i % 4][1] * 16, v->v.ob[2] = q->p[i % 4][2] * 16;
                v->v.flag = 0;
                v->v.tc[0] = q->tc[i % 4][0], v->v.tc[1] = q->tc[i % 4][1];
                memcpy(v->v.cn, q->cn, 4);
            }
            gSPVertex(g++, v0, batch * 4, 0);
            for (i = 0; i < batch; i++) {
                gSP2Triangles(g++, i * 4 + 0, i * 4 + 1, i * 4 + 2, 0, i * 4 + 0, i * 4 + 2, i * 4 + 3, 0);
            }
        }
    }
    gSPEndDisplayList(g++);
}

/* dev: the lobby's vertices, for working out where the stairs go */
void* pc_rom_museum1_vtx(int* count) {
    *count = (int)(sizeof(rom_museum1_v) / sizeof(rom_museum1_v[0]));
    return rom_museum1_v;
}
extern void pc_load_asset(const char*, void*, unsigned int, unsigned int, int, int);
void _pc_load_src_data_field_bg_acre_rom_museum1_rom_museum1_c(void) {
    pc_load_asset("assets/field/bg/rom_museum1_mado_pal.bin", rom_museum1_mado_pal, 0x20, 0xBA38E0, 0, 1);
    pc_load_asset("assets/field/bg/rom_museum1_step_pal.bin", rom_museum1_step_pal, 0x20, 0xBA3900, 0, 1);
    pc_load_asset("assets/field/bg/rom_museum1_floor_pal.bin", rom_museum1_floor_pal, 0x20, 0xBA3920, 0, 1);
    pc_load_asset("assets/field/bg/rom_museum1_wall_pal.bin", rom_museum1_wall_pal, 0x20, 0xBA3940, 0, 1);
    pc_load_asset("assets/field/bg/rom_museum1_mado1_tex.bin", rom_museum1_mado1_tex, 0x800, 0xBA3960, 0, 0);
    pc_load_asset("assets/field/bg/rom_museum1_step1_tex.bin", rom_museum1_step1_tex, 0x800, 0xBA4160, 0, 0);
    pc_load_asset("assets/field/bg/rom_museum1_step2_tex.bin", rom_museum1_step2_tex, 0x400, 0xBA4960, 0, 0);
    pc_load_asset("assets/field/bg/rom_museum1_floorA_tex.bin", rom_museum1_floorA_tex, 0x800, 0xBA4D60, 0, 0);
    pc_load_asset("assets/field/bg/rom_museum1_floorB_tex.bin", rom_museum1_floorB_tex, 0x800, 0xBA5560, 0, 0);
    pc_load_asset("assets/field/bg/rom_museum1_mado2_tex.bin", rom_museum1_mado2_tex, 0x800, 0xBA5D60, 0, 0);
    pc_load_asset("assets/field/bg/rom_museum1_sign1_tex.bin", rom_museum1_sign1_tex, 0x400, 0xBA6560, 0, 0);
    pc_load_asset("assets/field/bg/rom_museum1_wallA_tex.bin", rom_museum1_wallA_tex, 0x800, 0xBA6960, 0, 0);
    pc_load_asset("assets/field/bg/rom_museum1_wallB_tex.bin", rom_museum1_wallB_tex, 0x800, 0xBA7160, 0, 0);
    pc_load_asset("assets/field/bg/rom_museum1_sign2_tex.bin", rom_museum1_sign2_tex, 0x400, 0xBA7960, 0, 0);
    pc_load_asset("assets/field/bg/rom_museum1_sign3_tex.bin", rom_museum1_sign3_tex, 0x400, 0xBA7D60, 0, 0);
    pc_load_asset("assets/field/bg/rom_museum1_sign4_tex.bin", rom_museum1_sign4_tex, 0x400, 0xBA8160, 0, 0);
    pc_load_asset("assets/field/bg/rom_museum1_v.bin", rom_museum1_v, 0x12C0, 0xBA8560, 0, 2);
    pc_rom_museum1_build_stairs();
}
#endif
