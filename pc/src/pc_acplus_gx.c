/* pc_acplus_gx.c - helpers for ac-plus's own GX draws. See pc_acplus_gx.h. */
#include "pc_acplus_gx.h"
#include "pc_platform.h"

#include <dolphin/gx/GXAurora.h>
#include <stdlib.h>
#include <string.h>

static GXTexWrapMode pc_gxh_wrap(int w) {
    return w == 1 ? GX_REPEAT : w == 2 ? GX_MIRROR : GX_CLAMP;
}

/* RGBA8 levels, each half the one before (box filter), packed one after another as GX_TF_RGBA8_PC wants them */
static void* pc_gxh_mips(const unsigned char* px, int w, int h, int* levels) {
    size_t total = 0;
    int lw = w, lh = h, n = 1, i;
    unsigned char* out;
    unsigned char* prev;
    unsigned char* cur;

    while (lw > 1 || lh > 1) {
        total += (size_t)lw * lh * 4;
        lw = lw > 1 ? lw / 2 : 1;
        lh = lh > 1 ? lh / 2 : 1;
        n++;
    }
    total += (size_t)lw * lh * 4;
    out = (unsigned char*)malloc(total);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, px, (size_t)w * h * 4);
    prev = out;
    lw = w, lh = h;
    for (i = 1; i < n; i++) {
        int nw = lw > 1 ? lw / 2 : 1, nh = lh > 1 ? lh / 2 : 1, x, y, c;

        cur = prev + (size_t)lw * lh * 4;
        for (y = 0; y < nh; y++) {
            for (x = 0; x < nw; x++) {
                int x0 = x * 2 < lw ? x * 2 : lw - 1, x1 = x * 2 + 1 < lw ? x * 2 + 1 : lw - 1;
                int y0 = y * 2 < lh ? y * 2 : lh - 1, y1 = y * 2 + 1 < lh ? y * 2 + 1 : lh - 1;

                for (c = 0; c < 4; c++) {
                    int s = prev[((size_t)y0 * lw + x0) * 4 + c] + prev[((size_t)y0 * lw + x1) * 4 + c] +
                            prev[((size_t)y1 * lw + x0) * 4 + c] + prev[((size_t)y1 * lw + x1) * 4 + c];
                    cur[((size_t)y * nw + x) * 4 + c] = (unsigned char)((s + 2) / 4);
                }
            }
        }
        prev = cur;
        lw = nw, lh = nh;
    }
    *levels = n;
    return out;
}

void pc_gxh_tex_init(PCGXHTex* t, const unsigned char* px, int w, int h, int wrap_s, int wrap_t, int mip) {
    int levels = 1;

    memset(t, 0, sizeof(*t));
    if (px == NULL || w <= 0 || h <= 0 || w > 1024 || h > 1024) {
        return;
    }
    if (mip) {
        t->data = pc_gxh_mips(px, w, h, &levels);
    } else {
        t->data = malloc((size_t)w * h * 4);
        if (t->data != NULL) {
            memcpy(t->data, px, (size_t)w * h * 4);
        }
    }
    if (t->data == NULL) {
        return;
    }
    t->w = w;
    t->h = h;
    GXInitTexObj(&t->obj, t->data, (u16)w, (u16)h, GX_TF_RGBA8_PC, pc_gxh_wrap(wrap_s), pc_gxh_wrap(wrap_t),
                 levels > 1 ? GX_TRUE : GX_FALSE);
    GXInitTexObjLOD(&t->obj, levels > 1 ? GX_LIN_MIP_LIN : GX_LINEAR, GX_LINEAR, 0.0f, (f32)(levels - 1), 0.0f,
                    GX_FALSE, GX_FALSE, GX_ANISO_1);
    t->ready = 1;
}

void pc_gxh_tex_set_wrap(PCGXHTex* t, int wrap_s, int wrap_t) {
    if (t->ready) {
        GXInitTexObjWrapMode(&t->obj, pc_gxh_wrap(wrap_s), pc_gxh_wrap(wrap_t));
    }
}

void pc_gxh_mesh_state(const PCGXHTex* tex, int blend, int alpha_test, int zmode, GXCullMode cull) {
    int textured = tex != NULL && tex->ready;

    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(PC_GXH_VTXFMT, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(PC_GXH_VTXFMT, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    if (textured) {
        GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GXSetVtxAttrFmt(PC_GXH_VTXFMT, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    }

    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetNumIndStages(0);
    GXSetNumTevStages(1);
    GXSetTevDirect(GX_TEVSTAGE0);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    if (textured) {
        GXSetNumTexGens(1);
        GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
        GXLoadTexObj((GXTexObj*)&tex->obj, GX_TEXMAP0);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
        GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
    } else {
        GXSetNumTexGens(0);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
        GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
    }
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

    if (alpha_test) {
        GXSetAlphaCompare(GX_GEQUAL, 128, GX_AOP_AND, GX_ALWAYS, 0);
        GXSetZCompLoc(GX_FALSE); /* depth after the texture, so dropped texels don't write it */
    } else {
        GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
        GXSetZCompLoc(GX_TRUE);
    }
    if (blend == 1) {
        GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
    } else if (blend == 2) {
        GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_NOOP);
    } else if (blend == 3) {
        GXSetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_NOOP);
    } else {
        GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP);
    }
    GXSetZMode(zmode != 0 ? GX_TRUE : GX_FALSE, GX_LEQUAL, zmode == 2 ? GX_TRUE : GX_FALSE);
    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetCullMode(cull);
    {
        GXColor none = { 0, 0, 0, 0 };
        GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, none);
    }
}

void pc_gxh_camera(const float proj[4][4], const float view[3][4]) {
    /* an orthographic projection has no perspective row */
    int ortho = proj[3][3] == 1.0f && proj[3][2] == 0.0f;

    pc_widescreen_set_projection(proj, ortho ? GX_ORTHOGRAPHIC : GX_PERSPECTIVE);
    GXLoadPosMtxImm(view, PC_GXH_PNMTX);
    GXSetCurrentMtx(PC_GXH_PNMTX);
}

static f32 s_saved_vp[6];
static u32 s_saved_sc[4];
static int s_saved;

void pc_gxh_screen(void) {
    static const float proj[4][4] = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, -1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
    };
    static const float ident[3][4] = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
    };

    /* the whole window, whatever the game's list had set (pc_gxh_screen_end puts that back) */
    if (!s_saved) {
        GXGetViewportv(s_saved_vp);
        GXGetScissor(&s_saved_sc[0], &s_saved_sc[1], &s_saved_sc[2], &s_saved_sc[3]);
        s_saved = 1;
    }
    GXSetViewport(0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f);
    GXSetScissor(0, 0, 640, 480);
    GXSetProjection(proj, GX_ORTHOGRAPHIC);
    GXLoadPosMtxImm(ident, PC_GXH_PNMTX);
    GXSetCurrentMtx(PC_GXH_PNMTX);
}

void pc_gxh_screen_end(void) {
    if (s_saved) {
        GXSetViewport(s_saved_vp[0], s_saved_vp[1], s_saved_vp[2], s_saved_vp[3], s_saved_vp[4], s_saved_vp[5]);
        GXSetScissor(s_saved_sc[0], s_saved_sc[1], s_saved_sc[2], s_saved_sc[3]);
        s_saved = 0;
    }
}

/* The ground drawn a hair further back, so what lies flat on it (patterns, mats, shadows) always shows: emu64's
 * PC_NOOP_GROUND_ON/OFF around the field's ground. PC_GROUND_BIAS=n sets the offset (0 = off). */
void pc_gx_set_ground(int on) {
    static float bias = -1.0f;

    if (bias < 0.0f) {
        const char* env = getenv("PC_GROUND_BIAS");
        bias = env != NULL ? (float)atof(env) : 2.0f;
    }
    if (on && bias > 0.0f) {
        GX2SetPolygonOffset(bias, bias, bias, bias, 0.0f);
    } else {
        GX2SetPolygonOffset(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    }
}
