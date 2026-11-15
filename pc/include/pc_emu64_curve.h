/* pc_emu64_curve.h - emu64's GX vertex calls, cut up for the curved world (pc_emu64_curve.cpp, ac-plus).
 * Included by emu64.c (C++) only: after it, GXBegin & co. in that file go through pc_emu64_curve.cpp. A port that
 * doesn't want the curve can leave the include out. */
#ifndef PC_EMU64_CURVE_H
#define PC_EMU64_CURVE_H

#include <dolphin/gx.h>

#ifdef __cplusplus
extern "C" {
#endif
/* the curve for what's drawn next (set with every projection: 0 for 2D or no curve) */
void pc_emu64_curve_set_k(float k);
#ifdef __cplusplus
}

void pc_cv_load_pos_mtx(const void* mtx, u32 id);
void pc_cv_set_current_mtx(u32 id);
void pc_cv_begin(GXPrimitive prim, GXVtxFmt fmt, u16 n);
void pc_cv_begin_indexed(GXVtxFmt fmt, u16 nverts, const u16* indices, u32 nindices);
void pc_cv_pos3(f32 x, f32 y, f32 z);
void pc_cv_nrm3(f32 x, f32 y, f32 z);
void pc_cv_clr(u8 r, u8 g, u8 b, u8 a);
void pc_cv_tex(s16 s, s16 t);
void pc_cv_end();

#ifndef PC_EMU64_CURVE_IMPL
#define GXLoadPosMtxImm pc_cv_load_pos_mtx
#define GXSetCurrentMtx pc_cv_set_current_mtx
#define GXBegin pc_cv_begin
#define GXBeginIndexed pc_cv_begin_indexed
#define GXPosition3f32 pc_cv_pos3
#define GXNormal3f32 pc_cv_nrm3
#define GXColor4u8 pc_cv_clr
#define GXTexCoord2s16 pc_cv_tex
#define GXEnd pc_cv_end
#endif
#endif

#endif /* PC_EMU64_CURVE_H */
