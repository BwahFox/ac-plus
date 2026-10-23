/* pc_cf_room.c - City Folk rooms drawn in the GameCube game. See pc_cf_room.h. */
#include "pc_gx_internal.h"
#include "pc_cf_room.h"

#include "m_play.h"
#include "graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void pc_gx_restore_after_nes(void);
extern void GXGetProjectionv(f32* p);

#define PC_CF_ROOM_MAX 4
#define PC_CF_ROOM_NAME 32

typedef struct {
    char name[32];
    int tex;
    u8 xlu, cull, alpha_test;
    u8 wrap_s, wrap_t; /* 0 clamp, 1 repeat, 2 mirror, as the material's GX wrap modes */
    int vert_num;
    float* verts; /* x y z nx ny nz u v, then rgba as u8 packed into the last float slot */
    GLuint vao, vbo;
} pc_cf_mesh_c;

typedef struct {
    char name[PC_CF_ROOM_NAME];
    int loaded; /* 1 ok, -1 missing */
    int tex_num;
    int* tex_w;
    int* tex_h;
    u8** tex_px;
    GLuint* tex_gl;
    int mesh_num;
    pc_cf_mesh_c* mesh;
    int has_grid;
    u8 grid[16 * 16]; /* walkable units, from version 2 files */
} pc_cf_room_c;

static pc_cf_room_c l_rooms[PC_CF_ROOM_MAX];

/* set by the game for the frame being drawn */
static struct {
    pc_cf_room_c* room;
    float light;
    float y_offset;
} l_frame;

#define PC_CF_VERT_FLOATS 9 /* pos 3, normal 3, uv 2, colour (4 bytes) 1 */

static int pc_cf_read(FILE* f, void* dst, size_t n) {
    return fread(dst, 1, n, f) == n;
}

static void pc_cf_room_load(pc_cf_room_c* r) {
    char path[96];
    char magic[4];
    u32 version, n, i;
    FILE* f;

    snprintf(path, sizeof(path), "cf_sky/rooms/%s.cfroom", r->name);
    r->loaded = -1;
    f = fopen(path, "rb");
    if (f == NULL) {
        return;
    }
    if (!pc_cf_read(f, magic, 4) || memcmp(magic, "CFRM", 4) != 0 || !pc_cf_read(f, &version, 4) || version < 1 || version > 3 ||
        !pc_cf_read(f, &n, 4)) {
        fclose(f);
        return;
    }
    r->tex_num = (int)n;
    r->tex_w = calloc(n, sizeof(int));
    r->tex_h = calloc(n, sizeof(int));
    r->tex_px = calloc(n, sizeof(u8*));
    r->tex_gl = calloc(n, sizeof(GLuint));
    for (i = 0; i < n; i++) {
        char name[32];
        u16 wh[2];

        if (!pc_cf_read(f, name, 32) || !pc_cf_read(f, wh, 4)) {
            goto bad;
        }
        r->tex_w[i] = wh[0];
        r->tex_h[i] = wh[1];
        r->tex_px[i] = malloc((size_t)wh[0] * wh[1] * 4);
        if (r->tex_px[i] == NULL || !pc_cf_read(f, r->tex_px[i], (size_t)wh[0] * wh[1] * 4)) {
            goto bad;
        }
    }
    if (!pc_cf_read(f, &n, 4)) {
        goto bad;
    }
    r->mesh_num = (int)n;
    r->mesh = calloc(n, sizeof(pc_cf_mesh_c));
    for (i = 0; i < n; i++) {
        pc_cf_mesh_c* m = &r->mesh[i];
        s32 tex;
        u8 flags[4];
        u32 count, v;

        if (!pc_cf_read(f, m->name, 32) || !pc_cf_read(f, &tex, 4) || !pc_cf_read(f, flags, 4) ||
            !pc_cf_read(f, &count, 4)) {
            goto bad;
        }
        m->tex = tex;
        m->xlu = flags[0];
        m->cull = flags[1];
        m->alpha_test = flags[2];
        m->wrap_s = version >= 3 ? flags[3] & 3 : 1;
        m->wrap_t = version >= 3 ? flags[3] >> 2 & 3 : 1;
        m->vert_num = (int)count;
        m->verts = malloc((size_t)count * PC_CF_VERT_FLOATS * sizeof(float));
        for (v = 0; m->verts != NULL && v < count; v++) {
            float* d = &m->verts[v * PC_CF_VERT_FLOATS];
            u8 rgba[4];

            if (!pc_cf_read(f, d, 8 * sizeof(float)) || !pc_cf_read(f, rgba, 4)) {
                goto bad;
            }
            memcpy(&d[8], rgba, 4);
        }
    }
    if (version >= 2) {
        if (!pc_cf_read(f, r->grid, sizeof(r->grid))) {
            goto bad;
        }
        r->has_grid = TRUE;
    }
    fclose(f);
    r->loaded = 1;
    printf("[CFRoom] %s: %d textures, %d meshes\n", r->name, r->tex_num, r->mesh_num);
    return;
bad:
    fclose(f);
    printf("[CFRoom] %s is cut short\n", path);
}

static pc_cf_room_c* pc_cf_room_get(const char* name) {
    int i;

    for (i = 0; i < PC_CF_ROOM_MAX; i++) {
        if (l_rooms[i].name[0] != '\0' && strcmp(l_rooms[i].name, name) == 0) {
            return &l_rooms[i];
        }
    }
    for (i = 0; i < PC_CF_ROOM_MAX; i++) {
        if (l_rooms[i].name[0] == '\0') {
            snprintf(l_rooms[i].name, sizeof(l_rooms[i].name), "%s", name);
            pc_cf_room_load(&l_rooms[i]);
            return &l_rooms[i];
        }
    }
    return NULL;
}

int pc_cf_room_exists(const char* name) {
    pc_cf_room_c* r = pc_cf_room_get(name);

    return r != NULL && r->loaded == 1;
}

const u8* pc_cf_room_grid(const char* name) {
    pc_cf_room_c* r = pc_cf_room_get(name);

    return r != NULL && r->loaded == 1 && r->has_grid ? r->grid : NULL;
}

int pc_cf_room_play_draw(GAME_PLAY* play, const char* name, float light, float y_offset) {
    pc_cf_room_c* r = pc_cf_room_get(name);
    GRAPH* graph = play->game.graph;

    if (r == NULL || r->loaded != 1) {
        return FALSE;
    }
    l_frame.room = r;
    l_frame.light = light;
    l_frame.y_offset = y_offset;
    OPEN_DISP(graph);
    if (getenv("PC_CF_ROOM_LATE") != NULL) {
        gDPNoOpTag(NEXT_POLY_XLU_DISP, PC_NOOP_CF_ROOM); /* dev: after everything else */
    } else {
        /* at the head of POLY_OPA, before the actors: BG_OPA is painted over indoors */
        gDPNoOpTag(NEXT_POLY_OPA_DISP, PC_NOOP_CF_ROOM);
    }
    CLOSE_DISP(graph);
    return TRUE;
}

/* --- render side --- */

static const char* l_vert_src =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_pos;\n"
    "layout(location = 1) in vec3 a_nrm;\n"
    "layout(location = 2) in vec2 a_uv;\n"
    "layout(location = 3) in vec4 a_col;\n"
    "uniform mat4 u_proj;\n"
    "uniform mat4 u_view;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_col;\n"
    "out vec3 v_nrm;\n"
    "void main() {\n"
    "    gl_Position = u_proj * (u_view * vec4(a_pos, 1.0));\n"
    "    v_uv = a_uv;\n"
    "    v_col = a_col;\n"
    "    v_nrm = a_nrm;\n"
    "}\n";

static const char* l_frag_src =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "in vec4 v_col;\n"
    "in vec3 v_nrm;\n"
    "out vec4 frag;\n"
    "uniform sampler2D u_tex;\n"
    "uniform int u_has_tex;\n"
    "uniform int u_alpha_test;\n"
    "uniform float u_light;\n"
    "void main() {\n"
    "    vec4 c = v_col;\n"
    "    if (u_has_tex != 0) c *= texture(u_tex, v_uv);\n"
    "    if (u_alpha_test != 0 && c.a < 0.5) discard;\n"
    "    /* a little shape from the normals, as the room's own lights would */\n"
    "    float shade = 0.82 + 0.18 * clamp(dot(normalize(v_nrm), normalize(vec3(0.3, 1.0, 0.6))), 0.0, 1.0);\n"
    "    frag = vec4(c.rgb * shade * u_light, c.a);\n"
    "}\n";

static GLuint l_prog;
static GLint l_u_proj, l_u_view, l_u_has_tex, l_u_alpha_test, l_u_light;

static GLuint pc_cf_compile(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        printf("[CFRoom] shader compile failed: %s\n", log);
    }
    return sh;
}

static int pc_cf_init(void) {
    GLuint vs, fs;
    GLint ok = 0;

    if (l_prog != 0) {
        return TRUE;
    }
    vs = pc_cf_compile(GL_VERTEX_SHADER, l_vert_src);
    fs = pc_cf_compile(GL_FRAGMENT_SHADER, l_frag_src);
    l_prog = glCreateProgram();
    glAttachShader(l_prog, vs);
    glAttachShader(l_prog, fs);
    glLinkProgram(l_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGetProgramiv(l_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        printf("[CFRoom] shader link failed\n");
        glDeleteProgram(l_prog);
        l_prog = 0;
        return FALSE;
    }
    l_u_proj = glGetUniformLocation(l_prog, "u_proj");
    l_u_view = glGetUniformLocation(l_prog, "u_view");
    l_u_has_tex = glGetUniformLocation(l_prog, "u_has_tex");
    l_u_alpha_test = glGetUniformLocation(l_prog, "u_alpha_test");
    l_u_light = glGetUniformLocation(l_prog, "u_light");
    glUseProgram(l_prog);
    glUniform1i(glGetUniformLocation(l_prog, "u_tex"), 0);
    return TRUE;
}

/* GL objects for a room, the first time it's drawn */
static void pc_cf_upload(pc_cf_room_c* r) {
    int i;

    for (i = 0; i < r->tex_num; i++) {
        if (r->tex_gl[i] != 0 || r->tex_px[i] == NULL) {
            continue;
        }
        glGenTextures(1, &r->tex_gl[i]);
        glBindTexture(GL_TEXTURE_2D, r->tex_gl[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, r->tex_w[i], r->tex_h[i], 0, GL_RGBA, GL_UNSIGNED_BYTE, r->tex_px[i]);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    for (i = 0; i < r->mesh_num; i++) {
        pc_cf_mesh_c* m = &r->mesh[i];
        GLsizei stride = PC_CF_VERT_FLOATS * sizeof(float);

        if (m->vao != 0 || m->verts == NULL) {
            continue;
        }
        glGenVertexArrays(1, &m->vao);
        glBindVertexArray(m->vao);
        glGenBuffers(1, &m->vbo);
        glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)m->vert_num * stride, m->verts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)(8 * sizeof(float)));
    }
    glBindVertexArray(0);
}

static void pc_cf_draw(const float view[3][4]) {
    pc_cf_room_c* r = l_frame.room;
    float proj[16], v[16];
    int pass, i;

    if (r == NULL || r->loaded != 1 || !pc_cf_init()) {
        return;
    }
    pc_gx_draw_pending();
    pc_cf_upload(r);

    GXGetProjectionv(proj);
    {
        static int logged;
        if (!logged++ || (getenv("PC_CF_ROOM_LOG") != NULL && logged % 120 == 0)) {
            printf("[CFRoom] view %.2f %.2f %.2f %.1f / %.2f %.2f %.2f %.1f / %.2f %.2f %.2f %.1f proj00 %.3f 11 %.3f 22 %.4f 23 %.2f 32 %.2f\n",
                   view[0][0], view[0][1], view[0][2], view[0][3], view[1][0], view[1][1], view[1][2], view[1][3],
                   view[2][0], view[2][1], view[2][2], view[2][3], proj[0], proj[5], proj[10], proj[11], proj[14]);
            fflush(stdout);
        }
    }
    memset(v, 0, sizeof(v));
    for (i = 0; i < 3; i++) {
        v[i * 4 + 0] = view[i][0];
        v[i * 4 + 1] = view[i][1];
        v[i * 4 + 2] = view[i][2];
        v[i * 4 + 3] = view[i][3] + view[i][1] * l_frame.y_offset; /* the room raised by y_offset */
    }
    v[15] = 1.0f;

    glUseProgram(l_prog);
    glUniformMatrix4fv(l_u_proj, 1, GL_TRUE, proj); /* both row-major, as pc_gx.c uploads them */
    glUniformMatrix4fv(l_u_view, 1, GL_TRUE, v);
    glUniform1f(l_u_light, l_frame.light);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < r->mesh_num; i++) {
            pc_cf_mesh_c* m = &r->mesh[i];

            if (m->xlu != pass || m->vao == 0) {
                continue;
            }
            if (m->cull == 0) {
                glDisable(GL_CULL_FACE);
            } else {
                /* GX culls by its own winding; the converter keeps the model's, so cull the other side */
                glEnable(GL_CULL_FACE);
                glFrontFace(GL_CW);
                glCullFace(m->cull == 1 ? GL_FRONT : m->cull == 2 ? GL_BACK : GL_FRONT_AND_BACK);
            }
            if (pass == 1) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
            } else {
                glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
            }
            glUniform1i(l_u_alpha_test, pass == 0 && m->alpha_test);
            glUniform1i(l_u_has_tex, m->tex >= 0 && m->tex < r->tex_num);
            if (m->tex >= 0 && m->tex < r->tex_num) {
                static const GLint wraps[] = { GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT, GL_REPEAT };

                glBindTexture(GL_TEXTURE_2D, r->tex_gl[m->tex]);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wraps[m->wrap_s & 3]);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wraps[m->wrap_t & 3]);
            }
            glBindVertexArray(m->vao);
            glDrawArrays(GL_TRIANGLES, 0, m->vert_num);
        }
    }
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    glDepthMask(GL_TRUE);
    pc_gx_restore_after_nes(); /* rebinds the game's GL objects and marks all GX state dirty */
}

void pc_cf_room_noop(unsigned int tag, const float proj[4][4], const float view[3][4]) {
    (void)proj;
    if (tag == PC_NOOP_CF_ROOM) {
        pc_cf_draw(view);
        l_frame.room = NULL;
    }
}
