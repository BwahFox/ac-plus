/* pc_emu64_curve.cpp - long triangles cut up for the curved world (ac-plus).
 *
 * The curve bends every vertex in Aurora's vertex shader (patches/aurora-curved-world.patch), but a triangle stays
 * flat between its corners: a big one, such as those of a field's flat ground, sits far off the curve in its middle,
 * and small things standing on it (trees, furniture, players) look as if they float or sink. So while the curve is
 * on, emu64's triangles go through here: a batch whose triangles are all short enough goes to Aurora as it came;
 * otherwise every triangle longer than the curve allows is cut at its longest edge until the curve's sag inside each
 * is under a unit (as ac-plus's old OpenGL renderer did in pc_gx.c).
 *
 * emu64.c sends its GX vertex calls here (macros at its top, TARGET_PC). With the curve off, or a projection that
 * isn't perspective, everything passes straight through. Dev: PC_CURVE_NO_SPLIT=1 turns the cutting off. */
#include <dolphin/gx.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>

#define PC_EMU64_CURVE_IMPL /* the real GX calls here */
#include "pc_emu64_curve.h"

namespace {

struct CvVertex {
    f32 pos[3];
    f32 nrm[3];
    u8 clr[4];
    s16 tex[2];
};

struct CvState {
    float k = 0.0f; /* the curve now (0: off, or a 2D projection) */
    int off = -1;   /* PC_CURVE_NO_SPLIT */
    f32 mtx[GX_PNMTX9 + 3][3][4]{};
    bool mtxSet[GX_PNMTX9 + 3]{};
    u32 cur = GX_PNMTX0;

    bool recording = false;
    bool indexed = false;
    GXPrimitive prim = GX_TRIANGLES;
    GXVtxFmt fmt = GX_VTXFMT0;
    bool hasNrm = false, hasClr = false, hasTex = false;
    std::vector<CvVertex> in;
    std::vector<u16> indices;
    std::vector<CvVertex> out;
};

CvState s;

/* the squared length of b - a in eye space (the current matrix's 3x3) */
float cv_len2(const CvVertex& a, const CvVertex& b) {
    const f32 d[3] = { b.pos[0] - a.pos[0], b.pos[1] - a.pos[1], b.pos[2] - a.pos[2] };
    float e[3];
    if (s.mtxSet[s.cur]) {
        const f32(*m)[4] = s.mtx[s.cur];
        for (int r = 0; r < 3; r++) {
            e[r] = m[r][0] * d[0] + m[r][1] * d[1] + m[r][2] * d[2];
        }
    } else {
        e[0] = d[0], e[1] = d[1], e[2] = d[2];
    }
    return e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
}

CvVertex cv_mid(const CvVertex& a, const CvVertex& b) {
    CvVertex m;
    for (int i = 0; i < 3; i++) {
        m.pos[i] = (a.pos[i] + b.pos[i]) * 0.5f;
        m.nrm[i] = (a.nrm[i] + b.nrm[i]) * 0.5f;
    }
    for (int i = 0; i < 4; i++) {
        m.clr[i] = static_cast<u8>((a.clr[i] + b.clr[i] + 1) / 2);
    }
    for (int i = 0; i < 2; i++) {
        m.tex[i] = static_cast<s16>((a.tex[i] + b.tex[i]) / 2);
    }
    return m;
}

/* the longest a triangle's edge may be: the curve's sag k*(L/2)^2 under a unit, and never below 16 units */
float cv_max2() {
    const float max2 = 4.0f / s.k;
    return max2 < 256.0f ? 256.0f : max2;
}

constexpr size_t kMaxOut = 60000;

void cv_emit(const CvVertex& a, const CvVertex& b, const CvVertex& c, float max2, int depth) {
    const float ab = cv_len2(a, b), bc = cv_len2(b, c), ca = cv_len2(c, a);
    if (depth >= 10 || s.out.size() + 6 > kMaxOut || (ab <= max2 && bc <= max2 && ca <= max2)) {
        if (s.out.size() + 3 <= kMaxOut) {
            s.out.push_back(a);
            s.out.push_back(b);
            s.out.push_back(c);
        }
        return;
    }
    if (ab >= bc && ab >= ca) {
        const CvVertex m = cv_mid(a, b);
        cv_emit(a, m, c, max2, depth + 1);
        cv_emit(m, b, c, max2, depth + 1);
    } else if (bc >= ca) {
        const CvVertex m = cv_mid(b, c);
        cv_emit(a, b, m, max2, depth + 1);
        cv_emit(a, m, c, max2, depth + 1);
    } else {
        const CvVertex m = cv_mid(c, a);
        cv_emit(a, b, m, max2, depth + 1);
        cv_emit(m, b, c, max2, depth + 1);
    }
}

void cv_send(const CvVertex& v) {
    GXPosition3f32(v.pos[0], v.pos[1], v.pos[2]);
    if (s.hasNrm) GXNormal3f32(v.nrm[0], v.nrm[1], v.nrm[2]);
    if (s.hasClr) GXColor4u8(v.clr[0], v.clr[1], v.clr[2], v.clr[3]);
    if (s.hasTex) GXTexCoord2s16(v.tex[0], v.tex[1]);
}

/* the triangles of what was recorded, as corner numbers into s.in */
void cv_triangles(std::vector<u32>& tris) {
    if (s.indexed) {
        for (u16 i : s.indices) tris.push_back(i);
    } else if (s.prim == GX_QUADS) {
        for (u32 q = 0; q + 3 < s.in.size(); q += 4) {
            const u32 t[6] = { q, q + 1, q + 2, q, q + 2, q + 3 };
            tris.insert(tris.end(), t, t + 6);
        }
    } else {
        for (u32 i = 0; i + 2 < s.in.size(); i += 3) {
            tris.push_back(i), tris.push_back(i + 1), tris.push_back(i + 2);
        }
    }
}

} // namespace

extern "C" void pc_emu64_curve_set_k(float k) {
    s.k = k;
}

void pc_cv_load_pos_mtx(const void* mtx, u32 id) {
    if (id < GX_PNMTX9 + 3) {
        std::memcpy(s.mtx[id], mtx, sizeof(s.mtx[id]));
        s.mtxSet[id] = true;
    }
    GXLoadPosMtxImm(mtx, id);
}

void pc_cv_set_current_mtx(u32 id) {
    s.cur = id < GX_PNMTX9 + 3 ? id : GX_PNMTX0;
    GXSetCurrentMtx(id);
}

void pc_cv_begin(GXPrimitive prim, GXVtxFmt fmt, u16 n) {
    if (s.off < 0) {
        s.off = std::getenv("PC_CURVE_NO_SPLIT") != nullptr;
    }
    if (s.off || s.k <= 0.0f || (prim != GX_TRIANGLES && prim != GX_QUADS)) {
        s.recording = false;
        GXBegin(prim, fmt, n);
        return;
    }
    s.recording = true;
    s.indexed = false;
    s.prim = prim;
    s.fmt = fmt;
    s.hasNrm = s.hasClr = s.hasTex = false;
    s.in.clear();
}

void pc_cv_begin_indexed(GXVtxFmt fmt, u16 nverts, const u16* indices, u32 nindices) {
    if (s.off < 0) {
        s.off = std::getenv("PC_CURVE_NO_SPLIT") != nullptr;
    }
    if (s.off || s.k <= 0.0f) {
        s.recording = false;
        GXBeginIndexed(fmt, nverts, indices, nindices);
        return;
    }
    s.recording = true;
    s.indexed = true;
    s.prim = GX_TRIANGLES;
    s.fmt = fmt;
    s.hasNrm = s.hasClr = s.hasTex = false;
    s.in.clear();
    s.indices.assign(indices, indices + nindices);
}

void pc_cv_pos3(f32 x, f32 y, f32 z) {
    if (!s.recording) {
        GXPosition3f32(x, y, z);
        return;
    }
    CvVertex v{};
    v.pos[0] = x, v.pos[1] = y, v.pos[2] = z;
    s.in.push_back(v);
}

void pc_cv_nrm3(f32 x, f32 y, f32 z) {
    if (!s.recording || s.in.empty()) {
        GXNormal3f32(x, y, z);
        return;
    }
    s.hasNrm = true;
    s.in.back().nrm[0] = x, s.in.back().nrm[1] = y, s.in.back().nrm[2] = z;
}

void pc_cv_clr(u8 r, u8 g, u8 b, u8 a) {
    if (!s.recording || s.in.empty()) {
        GXColor4u8(r, g, b, a);
        return;
    }
    s.hasClr = true;
    s.in.back().clr[0] = r, s.in.back().clr[1] = g, s.in.back().clr[2] = b, s.in.back().clr[3] = a;
}

void pc_cv_tex(s16 u, s16 v) {
    if (!s.recording || s.in.empty()) {
        GXTexCoord2s16(u, v);
        return;
    }
    s.hasTex = true;
    s.in.back().tex[0] = u, s.in.back().tex[1] = v;
}

void pc_cv_end() {
    if (!s.recording) {
        GXEnd();
        return;
    }
    s.recording = false;

    std::vector<u32> tris;
    cv_triangles(tris);
    const float max2 = cv_max2();
    bool split = false;
    for (size_t i = 0; i + 2 < tris.size() && !split; i += 3) {
        if (tris[i] >= s.in.size() || tris[i + 1] >= s.in.size() || tris[i + 2] >= s.in.size()) {
            continue;
        }
        const CvVertex& a = s.in[tris[i]];
        const CvVertex& b = s.in[tris[i + 1]];
        const CvVertex& c = s.in[tris[i + 2]];
        split = cv_len2(a, b) > max2 || cv_len2(b, c) > max2 || cv_len2(c, a) > max2;
    }

    if (!split) {
        /* as it came */
        if (s.indexed) {
            GXBeginIndexed(s.fmt, static_cast<u16>(s.in.size()), s.indices.data(), static_cast<u32>(s.indices.size()));
        } else {
            GXBegin(s.prim, s.fmt, static_cast<u16>(s.in.size()));
        }
        for (const CvVertex& v : s.in) cv_send(v);
        GXEnd();
        return;
    }

    s.out.clear();
    static int log = -1;
    static unsigned long batches, vin, vout;
    if (log < 0) {
        log = std::getenv("PC_CURVE_LOG") != nullptr; /* dev: how much gets cut up */
    }
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        if (tris[i] >= s.in.size() || tris[i + 1] >= s.in.size() || tris[i + 2] >= s.in.size()) {
            continue;
        }
        cv_emit(s.in[tris[i]], s.in[tris[i + 1]], s.in[tris[i + 2]], max2, 0);
    }
    if (log) {
        batches++, vin += tris.size(), vout += s.out.size();
        if (batches % 2000 == 0) {
            std::printf("[curve] %lu batches cut up: %lu corners -> %lu (k %g)\n", batches, vin, vout, s.k);
            std::fflush(stdout);
        }
    }
    /* GXBegin counts vertices in a u16 */
    for (size_t start = 0; start < s.out.size(); start += 65535 - 65535 % 3) {
        const size_t n = std::min<size_t>(s.out.size() - start, 65535 - 65535 % 3);
        GXBegin(GX_TRIANGLES, s.fmt, static_cast<u16>(n));
        for (size_t i = 0; i < n; i++) cv_send(s.out[start + i]);
        GXEnd();
    }
}
