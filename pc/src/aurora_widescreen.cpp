#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <dolphin/gx/GXAurora.h>

#include "pc_platform.h"
#include "pc_settings.h"

#include <cstdlib>
#include <cstring>

extern "C" {
void pc_sky_curve_uniforms(int perspective, float up[4], float fwd[4], float haze[4]);
void pc_emu64_curve_set_k(float k);

float pc_widescreen_get_aspect_factor(void)
{
    if (g_pc_window_w <= 0 || g_pc_window_h <= 0) {
        return 1.0f;
    }

    const float gcAspect = static_cast<float>(PC_GC_WIDTH) / static_cast<float>(PC_GC_HEIGHT);
    const float renderAspect = static_cast<float>(g_pc_window_w) / static_cast<float>(g_pc_window_h);
    if (renderAspect <= gcAspect + 0.01f) {
        return 1.0f;
    }
    return gcAspect / renderAspect;
}

float pc_widescreen_get_hud_edge_offset(void)
{
    if (!g_pc_settings.hud_at_edges) {
        return 0.0f;
    }

    const float halfFontWidth = static_cast<float>(PC_GC_WIDTH) * 0.25f;
    return halfFontWidth * (1.0f / pc_widescreen_get_aspect_factor() - 1.0f);
}

void pc_widescreen_set_projection(const void* mtx, u32 type)
{
    Mtx44 adjusted;
    std::memcpy(adjusted, mtx, sizeof(adjusted));

    const float aspectFactor = pc_widescreen_get_aspect_factor();
    const bool correctProjection =
        g_pc_widescreen_stretch == 0 ||
        (g_pc_widescreen_stretch == 2 && type == GX_ORTHOGRAPHIC);
    if (correctProjection && aspectFactor < 1.0f) {
        adjusted[0][0] *= aspectFactor;
    }

    GXSetProjection(adjusted, static_cast<GXProjectionType>(type));
    pc_emu64_curve_set_k(0.0f); /* 2D: nothing to cut up (pc_emu64_curve.cpp) */
    if (type == GX_PERSPECTIVE) {
        /* ac-plus: the curved world for what this projection draws (pc_sky.c; Aurora patch
         * patches/aurora-curved-world.patch) */
        float up[4] = {}, fwd[4] = {}, haze[4] = {};
        pc_sky_curve_uniforms(1, up, fwd, haze);
        if (const char* test = std::getenv("PC_CURVE_TEST")) {
            /* dev: a curve whatever the sky says (strength per unit^2), to see the bend alone */
            up[1] = 1.0f, up[3] = static_cast<float>(std::atof(test));
            fwd[2] = -1.0f, fwd[3] = 0.0f;
        }
        GXAuroraSetCurve(up, fwd);
        pc_emu64_curve_set_k(up[3]);
    }
}

void pc_widescreen_set_viewport(float left, float top, float width, float height,
                                float nearz, float farz)
{
    const float aspectFactor = pc_widescreen_get_aspect_factor();
    if (g_pc_widescreen_stretch == 2 && aspectFactor < 1.0f) {
        const bool isFullViewport =
            left < 1.0f && top < 1.0f &&
            width > static_cast<float>(PC_GC_WIDTH - 1) &&
            height > static_cast<float>(PC_GC_HEIGHT - 1);
        if (!isFullViewport) {
            const float contentLeft =
                (1.0f - aspectFactor) * 0.5f * static_cast<float>(PC_GC_WIDTH);
            left = contentLeft + left * aspectFactor;
            width *= aspectFactor;
        }
    }

    GXSetViewport(left, top, width, height, nearz, farz);
}

} // extern "C"
