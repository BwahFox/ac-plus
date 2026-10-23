/* pc_sky.c - render side of the curved ("rolling log") world and sky.
 * See pc_sky.h for how the game marks what to curve; the game side is pc_sky_game.c. */
#include "pc_gx_internal.h"
#include "pc_sky.h"
#include "pc_constellation.h"

#include <dolphin/gx/GXEnum.h>
#include <stdlib.h>

extern float pc_gx_projection_x_scale(void);

extern void pc_gx_restore_after_nes(void);

#define PC_SKY_STR2(x) #x
#define PC_SKY_STR(x) PC_SKY_STR2(x)
#define PC_SKY_LINE_MAX_STR PC_SKY_STR(PC_SKY_LINE_MAX)

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

static GLuint s_prog;
static GLuint s_vao;
static GLint s_u_proj, s_u_eye_to_world, s_u_zenith, s_u_horizon, s_u_dip, s_u_misc, s_u_have, s_u_rainbow;
static GLint s_u_turn, s_u_lines, s_u_line_num, s_u_edit_num, s_u_ground, s_u_hl, s_u_hl_on;

static PCSkyGaze s_gaze; /* the stargazing view, drawn over the frame at PC_NOOP_STARGAZE */
static GLuint s_tex_cloud, s_tex_star, s_tex_moon;

static const char* s_vert_src =
    "#version 330 core\n"
    "out vec2 v_ndc;\n"
    "void main() {\n"
    "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;\n"
    "    v_ndc = p;\n"
    "    gl_Position = vec4(p, 1.0, 1.0);\n"
    "}\n";

static const char* s_frag_src =
    "#version 330 core\n"
    "in vec2 v_ndc;\n"
    "out vec4 frag;\n"
    "uniform vec4 u_proj;          /* 1/P00, 1/P11, P02, P12 */\n"
    "uniform mat3 u_eye_to_world;\n"
    "uniform vec3 u_zenith;\n"
    "uniform vec3 u_horizon;\n"
    "uniform float u_dip;          /* how far below level the curved horizon sits, radians */\n"
    "uniform vec4 u_misc;          /* seconds, night 0-1, overcast 0-1, camera heading */\n"
    "uniform int u_have;           /* 1 clouds, 2 stars, 4 moon */\n"
    "uniform float u_rainbow;      /* 0-1 */\n"
    "uniform float u_turn;         /* sky azimuth = view azimuth - u_turn */\n"
    "uniform vec4 u_lines[" PC_SKY_LINE_MAX_STR "]; /* constellation links in sky coordinates */\n"
    "uniform int u_line_num;\n"
    "uniform int u_edit_num;       /* the first u_edit_num lines are being edited */\n"
    "uniform int u_ground;         /* 1: dark ground below the horizon */\n"
    "uniform vec4 u_hl;            /* cursor star, picked star (sky az, up) */\n"
    "uniform int u_hl_on;          /* 1 cursor, 2 picked */\n"
    "uniform sampler2D u_cloud;\n"
    "uniform sampler2D u_star;\n"
    "uniform sampler2D u_moon;\n"
    "const float PI = 3.14159265;\n"
    /* the star grid, shared with pc_constellation.c */
    "const float COL_W = 2.0 * PI / float(" PC_SKY_STR(PC_CONST_COLS) ");\n"
    "const float ROW_H = " PC_SKY_STR(PC_CONST_CELL) ";\n"
    "float hashi(ivec2 c, uint k) {\n"
    "    uint h = uint(c.x) * 0x8DA6B343u + uint(c.y) * 0xD8163841u + k * 0xCB1AB31Fu;\n"
    "    h ^= h >> 13u; h *= 0x5BD1E995u; h ^= h >> 15u;\n"
    "    return float(h & 0xFFFFFFu) / 16777216.0;\n"
    "}\n"
    "float wrap(float a) { return mod(a + PI, 2.0 * PI) - PI; }\n"
    "float seg_dist(vec2 p, vec2 a, vec2 b) {\n"
    "    vec2 ab = b - a; float t = clamp(dot(p - a, ab) / max(dot(ab, ab), 1e-9), 0.0, 1.0);\n"
    "    return length(p - a - ab * t);\n"
    "}\n"
    "bool inside(vec2 uv) { return all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))); }\n"
    "void main() {\n"
    "    vec3 d_eye = vec3((v_ndc.x + u_proj.z) * u_proj.x, (v_ndc.y + u_proj.w) * u_proj.y, -1.0);\n"
    "    vec3 d = normalize(u_eye_to_world * d_eye);\n"
    "    /* angles above the curved horizon and around from north; the camera only ever\n"
    "       sees a few degrees of sky, so everything lives low like City Folk's backdrop */\n"
    "    float above = asin(clamp(d.y, -1.0, 1.0)) + u_dip;\n"
    "    float az = atan(d.x, -d.z);\n"
    "    float night = u_misc.y;\n"
    "    float overcast = u_misc.z;\n"
    "    vec3 col = mix(u_horizon, u_zenith, smoothstep(-0.01, 0.16, above));\n"
    "    float saz = mod(az - u_turn, 2.0 * PI);\n"
    "    if ((u_have & 2) != 0 && night > 0.0) {\n"
    "        vec2 g = vec2(saz / COL_W, above / ROW_H);\n"
    "        ivec2 cell = ivec2(floor(g));\n"
    "        cell.x = cell.x % " PC_SKY_STR(PC_CONST_COLS) ";\n"
    "        if (cell.y >= 0 && hashi(cell, 0u) > 0.84) {\n"
    "            vec2 c = vec2(0.35) + 0.3 * vec2(hashi(cell, 1u), hashi(cell, 2u));\n"
    "            vec2 uv = (fract(g) - c) * (ROW_H / 0.009) + 0.5;\n"
    "            if (inside(uv)) {\n"
    "                float h = hashi(cell, 3u);\n"
    "                float twinkle = 0.6 + 0.4 * sin(u_misc.x * (1.5 + 3.0 * h) + h * 40.0);\n"
    "                col += texture(u_star, uv).r * twinkle * night * (1.0 - overcast) * smoothstep(0.0, 0.02, above);\n"
    "            }\n"
    "        }\n"
    "        /* constellations: soft lines between their stars */\n"
    "        float glow = 0.0, edit = 0.0;\n"
    "        for (int i = 0; i < u_line_num; i++) {\n"
    "            vec4 l = u_lines[i];\n"
    "            vec2 p = vec2(l.x + wrap(saz - l.x), above);\n"
    "            float g = 1.0 - smoothstep(0.0008, 0.0028, seg_dist(p, l.xy, l.zw));\n"
    "            if (i < u_edit_num) edit = max(edit, g); else glow = max(glow, g);\n"
    "        }\n"
    "        float vis = night * (1.0 - overcast) * smoothstep(0.0, 0.02, above);\n"
    "        col = mix(col, vec3(0.72, 0.84, 1.0), glow * 0.55 * vis);\n"
    "        col = mix(col, vec3(1.0, 0.86, 0.45), edit * 0.85 * vis);\n"
    "        for (int i = 0; i < 2; i++) {\n"
    "            if ((u_hl_on & (1 << i)) == 0) continue;\n"
    "            vec2 c = i == 0 ? u_hl.xy : u_hl.zw;\n"
    "            float r = length(vec2(wrap(saz - c.x), above - c.y));\n"
    "            float ring = 1.0 - smoothstep(0.0006, 0.0016, abs(r - 0.0085));\n"
    "            col = mix(col, i == 0 ? vec3(1.0) : vec3(1.0, 0.8, 0.3), ring * (0.6 + 0.3 * sin(u_misc.x * 6.0)));\n"
    "        }\n"
    "    }\n"
    "    if ((u_have & 4) != 0 && night > 0.0) {\n"
    "        vec2 uv = (vec2(az, above) - vec2(u_misc.w + 0.16, 0.034)) / 0.032 + 0.5;\n"
    "        uv.y = 1.0 - uv.y;\n"
    "        if (inside(uv)) {\n"
    "            vec4 m = texture(u_moon, uv);\n"
    "            col = mix(col, m.rgb, m.a * night * (1.0 - 0.8 * overcast));\n"
    "        }\n"
    "    }\n"
    "    if (u_rainbow > 0.0) {\n"
    "        /* an arc a little left of ahead, red outside to violet inside */\n"
    "        const float R = 0.35, PEAK = 0.075, W = 0.03;\n"
    "        float daz = mod(az - (u_misc.w - 0.12) + PI, 2.0 * PI) - PI;\n"
    "        float x = (R - length(vec2(daz, above - (PEAK - R)))) / W;\n"
    "        if (x > 0.0 && x < 1.0) {\n"
    "            vec3 c = clamp(abs(mod(x * 4.5 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);\n"
    "            float a = 0.35 * u_rainbow * sqrt(sin(PI * x)) * smoothstep(-0.005, 0.02, above) * (1.0 - night);\n"
    "            col = mix(col, c, a);\n"
    "        }\n"
    "    }\n"
    "    if ((u_have & 1) != 0) {\n"
    "        float v = above / 0.10;\n"
    "        if (v < 1.0) {\n"
    "            vec2 uv = vec2(az / (2.0 * PI) * 10.0 + u_misc.x * 0.012, v * 0.6);\n"
    "            float cover = mix(0.62, 0.25, overcast);\n"
    "            float a = smoothstep(cover, cover + 0.3, texture(u_cloud, uv).r) * (1.0 - v);\n"
    "            vec3 cloud = mix(u_horizon, vec3(1.0), 0.8) * (1.0 - 0.55 * night);\n"
    "            col = mix(col, cloud, a * 0.9);\n"
    "        }\n"
    "    }\n"
    "    if (u_ground != 0) {\n"
    "        col = mix(col, vec3(0.04, 0.07, 0.10), 1.0 - smoothstep(-0.003, 0.003, above));\n"
    "    }\n"
    "    frag = vec4(col, 1.0);\n"
    "}\n";

/* Uncompressed 32-bit .tga as written by pc/tools/extract_cf_sky.py; 0 if missing */
static GLuint sky_load_tga(const char* path, int repeat) {
    unsigned char hdr[18];
    unsigned char* px;
    FILE* f = fopen(path, "rb");
    GLuint tex = 0;
    int w, h, y, x;

    if (f == NULL) {
        return 0;
    }
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr) || hdr[2] != 2 || hdr[16] != 32) {
        printf("[Sky] %s: not an uncompressed 32-bit tga\n", path);
        fclose(f);
        return 0;
    }
    w = hdr[12] | (hdr[13] << 8);
    h = hdr[14] | (hdr[15] << 8);
    fseek(f, 18 + hdr[0], SEEK_SET);
    px = malloc((size_t)w * h * 4);
    if (px == NULL || fread(px, 4, (size_t)w * h, f) != (size_t)w * h) {
        free(px);
        fclose(f);
        return 0;
    }
    fclose(f);

    for (y = 0; y < h; y++) { /* BGRA -> RGBA */
        for (x = 0; x < w; x++) {
            unsigned char* p = px + ((size_t)y * w + x) * 4;
            unsigned char t = p[0];
            p[0] = p[2];
            p[2] = t;
        }
    }
    if (!(hdr[17] & 0x20)) { /* bottom-up: flip so row 0 is the top */
        for (y = 0; y < h / 2; y++) {
            for (x = 0; x < w * 4; x++) {
                unsigned char t = px[(size_t)y * w * 4 + x];
                px[(size_t)y * w * 4 + x] = px[(size_t)(h - 1 - y) * w * 4 + x];
                px[(size_t)(h - 1 - y) * w * 4 + x] = t;
            }
        }
    }

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    free(px);
    printf("[Sky] loaded %s (%dx%d)\n", path, w, h);
    return tex;
}

static GLuint sky_compile(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        printf("[Sky] shader compile failed: %s\n", log);
    }
    return sh;
}

static int sky_init(void) {
    GLuint vs, fs;
    GLint ok = 0;

    if (s_prog != 0) {
        return 1;
    }
    vs = sky_compile(GL_VERTEX_SHADER, s_vert_src);
    fs = sky_compile(GL_FRAGMENT_SHADER, s_frag_src);
    s_prog = glCreateProgram();
    glAttachShader(s_prog, vs);
    glAttachShader(s_prog, fs);
    glLinkProgram(s_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGetProgramiv(s_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        printf("[Sky] shader link failed\n");
        glDeleteProgram(s_prog);
        s_prog = 0;
        return 0;
    }
    s_u_proj = glGetUniformLocation(s_prog, "u_proj");
    s_u_eye_to_world = glGetUniformLocation(s_prog, "u_eye_to_world");
    s_u_zenith = glGetUniformLocation(s_prog, "u_zenith");
    s_u_horizon = glGetUniformLocation(s_prog, "u_horizon");
    s_u_dip = glGetUniformLocation(s_prog, "u_dip");
    s_u_misc = glGetUniformLocation(s_prog, "u_misc");
    s_u_have = glGetUniformLocation(s_prog, "u_have");
    s_u_rainbow = glGetUniformLocation(s_prog, "u_rainbow");
    s_u_turn = glGetUniformLocation(s_prog, "u_turn");
    s_u_lines = glGetUniformLocation(s_prog, "u_lines");
    s_u_line_num = glGetUniformLocation(s_prog, "u_line_num");
    s_u_edit_num = glGetUniformLocation(s_prog, "u_edit_num");
    s_u_ground = glGetUniformLocation(s_prog, "u_ground");
    s_u_hl = glGetUniformLocation(s_prog, "u_hl");
    s_u_hl_on = glGetUniformLocation(s_prog, "u_hl_on");
    glUseProgram(s_prog);
    glUniform1i(glGetUniformLocation(s_prog, "u_cloud"), 0);
    glUniform1i(glGetUniformLocation(s_prog, "u_star"), 1);
    glUniform1i(glGetUniformLocation(s_prog, "u_moon"), 2);
    glGenVertexArrays(1, &s_vao);

    /* City Folk's own art, if extracted from the player's disc (pc/tools/extract_cf_sky.py) */
    s_tex_cloud = sky_load_tga("cf_sky/cloud.tga", 1);
    s_tex_star = sky_load_tga("cf_sky/star.tga", 0);
    s_tex_moon = sky_load_tga("cf_sky/moon.tga", 0);
    return 1;
}

void pc_sky_set_frame(const float center[3], const float zenith[3], const float horizon[3], float curve_k, float night,
                      float overcast) {
    int i;

    for (i = 0; i < 3; i++) {
        s_sky.center[i] = center[i];
        s_sky.zenith[i] = zenith[i];
        s_sky.horizon[i] = horizon[i];
    }
    s_sky.k = curve_k;
    s_sky.night = night;
    s_sky.overcast = overcast;
    s_sky.rainbow = 0.0f;
    s_sky.line_num = 0;
    s_sky.requested = 1;
}

static void sky_set_active(int active) {
    if (active != s_sky.active) {
        pc_gx_flush_if_begin_complete();
        s_sky.active = active;
        DIRTY(PC_GX_DIRTY_PROJECTION); /* curve uniforms ride with the projection */
    }
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

static void sky_render(const SkyParams* sp) {
    glViewport(0, 0, g_pc_window_w, g_pc_window_h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    if (sp->alpha < 1.0f) {
        glEnable(GL_BLEND);
        glBlendColor(0.0f, 0.0f, 0.0f, sp->alpha);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glUseProgram(s_prog);
    glUniform4fv(s_u_proj, 1, sp->proj);
    glUniformMatrix3fv(s_u_eye_to_world, 1, GL_FALSE, sp->eye_to_world);
    glUniform3fv(s_u_zenith, 1, sp->zenith);
    glUniform3fv(s_u_horizon, 1, sp->horizon);
    glUniform1f(s_u_dip, sp->dip);
    glUniform4f(s_u_misc, (float)SDL_GetTicks() / 1000.0f, sp->night, sp->overcast, sp->heading);
    glUniform1f(s_u_rainbow, sp->rainbow);
    glUniform1f(s_u_turn, sp->turn);
    glUniform1i(s_u_line_num, sp->line_num);
    glUniform1i(s_u_edit_num, sp->edit_num);
    if (sp->line_num > 0) {
        glUniform4fv(s_u_lines, sp->line_num, &sp->lines[0][0]);
    }
    glUniform1i(s_u_ground, sp->ground);
    glUniform4fv(s_u_hl, 1, sp->hl);
    glUniform1i(s_u_hl_on, sp->hl_on);
    glUniform1i(s_u_have, (s_tex_cloud ? 1 : 0) | (s_tex_star ? 2 : 0) | (s_tex_moon ? 4 : 0));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_tex_cloud);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s_tex_star);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, s_tex_moon);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(s_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    pc_gx_restore_after_nes(); /* rebinds the game's GL objects and marks all GX state dirty */
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

    if (!sky_init()) {
        return;
    }

    pc_gx_draw_pending();

    /* eye -> world is R^T; GL reads column-major, so R row-major works as-is */
    for (i = 0; i < 3; i++) {
        eye_to_world[i * 3 + 0] = view[i][0];
        eye_to_world[i * 3 + 1] = view[i][1];
        eye_to_world[i * 3 + 2] = view[i][2];
    }

    {
        SkyParams sp;

        memset(&sp, 0, sizeof(sp));
        sp.proj[0] = 1.0f / (proj[0][0] * pc_gx_projection_x_scale());
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

    if (!sky_init()) {
        return;
    }
    pc_gx_draw_pending();
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
                DIRTY(PC_GX_DIRTY_PROJECTION);
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
