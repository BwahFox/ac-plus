/* pc_cf_char.c - City Folk characters drawn in the GameCube game. See pc_cf_char.h. */
#include "pc_gx_internal.h"
#include "pc_cf_char.h"

#include "m_play.h"
#include "graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void pc_gx_restore_after_nes(void);
extern void GXGetProjectionv(f32* p);

#define PC_CF_CHAR_MAX 12
#define PC_CF_CHAR_BONES 32
#define PC_CF_CHAR_VERT_FLOATS 9 /* bone 1, pos 3, normal 3, uv 2 */

typedef struct {
    char name[16];
    int parent;
    float t[3];
    float rest[3][3];
} pc_cf_bone_c;

typedef struct {
    char name[32];
    int tex;
    u8 xlu, cull, alpha_test, wrap_s, wrap_t, eyes;
    int vert_num;
    float* verts;
    GLuint vao, vbo;
} pc_cf_char_mesh_c;

typedef struct {
    char name[32];
    int loaded; /* 1 ok, -1 missing */
    int bone_num;
    pc_cf_bone_c bone[PC_CF_CHAR_BONES];
    int tex_num;
    int* tex_w;
    int* tex_h;
    u8** tex_px;
    GLuint* tex_gl;
    int eye_tex[8]; /* e.0-e.7, or -1 */
    int mesh_num;
    pc_cf_char_mesh_c* mesh;
} pc_cf_char_c;

static pc_cf_char_c l_chars[PC_CF_CHAR_MAX];

/* what the game asked to draw this frame */
static struct {
    pc_cf_char_c* chr;
    float bones[PC_CF_CHAR_BONES][3][4];
    int eye;
    float light;
} l_inst[PC_CF_CHAR_INSTANCES];
static int l_inst_next;
static u32 l_inst_frame;

static int pc_cf_read(FILE* f, void* dst, size_t n) {
    return fread(dst, 1, n, f) == n;
}

static void pc_cf_char_load(pc_cf_char_c* c) {
    char path[96];
    char magic[4];
    u32 version, n, i;
    int k;
    FILE* f;

    snprintf(path, sizeof(path), "cf_sky/chars/%s.cfchar", c->name);
    c->loaded = -1;
    for (k = 0; k < 8; k++) {
        c->eye_tex[k] = -1;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return;
    }
    if (!pc_cf_read(f, magic, 4) || memcmp(magic, "CFCH", 4) != 0 || !pc_cf_read(f, &version, 4) || version != 1 ||
        !pc_cf_read(f, &n, 4) || n > PC_CF_CHAR_BONES) {
        fclose(f);
        return;
    }
    c->bone_num = (int)n;
    for (i = 0; i < n; i++) {
        pc_cf_bone_c* b = &c->bone[i];
        s32 parent;

        if (!pc_cf_read(f, b->name, 16) || !pc_cf_read(f, &parent, 4) || !pc_cf_read(f, b->t, 12) ||
            !pc_cf_read(f, b->rest, 36)) {
            goto bad;
        }
        b->name[15] = '\0';
        b->parent = parent < (s32)i ? parent : -1; /* parents come first */
    }
    if (!pc_cf_read(f, &n, 4)) {
        goto bad;
    }
    c->tex_num = (int)n;
    c->tex_w = calloc(n, sizeof(int));
    c->tex_h = calloc(n, sizeof(int));
    c->tex_px = calloc(n, sizeof(u8*));
    c->tex_gl = calloc(n, sizeof(GLuint));
    for (i = 0; i < n; i++) {
        char name[32];
        u16 wh[2];

        if (!pc_cf_read(f, name, 32) || !pc_cf_read(f, wh, 4)) {
            goto bad;
        }
        name[31] = '\0';
        if (name[0] == 'e' && name[1] == '.' && name[2] >= '0' && name[2] <= '7' && name[3] == '\0') {
            c->eye_tex[name[2] - '0'] = (int)i;
        }
        c->tex_w[i] = wh[0];
        c->tex_h[i] = wh[1];
        c->tex_px[i] = malloc((size_t)wh[0] * wh[1] * 4);
        if (c->tex_px[i] == NULL || !pc_cf_read(f, c->tex_px[i], (size_t)wh[0] * wh[1] * 4)) {
            goto bad;
        }
    }
    if (!pc_cf_read(f, &n, 4)) {
        goto bad;
    }
    c->mesh_num = (int)n;
    c->mesh = calloc(n, sizeof(pc_cf_char_mesh_c));
    for (i = 0; i < n; i++) {
        pc_cf_char_mesh_c* m = &c->mesh[i];
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
        m->wrap_s = flags[3] & 3;
        m->wrap_t = flags[3] >> 2 & 3;
        m->eyes = tex >= 0 && tex == c->eye_tex[0];
        m->vert_num = (int)count;
        m->verts = malloc((size_t)count * PC_CF_CHAR_VERT_FLOATS * sizeof(float));
        for (v = 0; m->verts != NULL && v < count; v++) {
            float* d = &m->verts[v * PC_CF_CHAR_VERT_FLOATS];
            u8 bone[4];

            if (!pc_cf_read(f, bone, 4) || !pc_cf_read(f, &d[1], 8 * sizeof(float))) {
                goto bad;
            }
            d[0] = bone[0] < c->bone_num ? bone[0] : 0;
        }
    }
    fclose(f);
    c->loaded = 1;
    printf("[CFChar] %s: %d bones, %d textures, %d meshes\n", c->name, c->bone_num, c->tex_num, c->mesh_num);
    return;
bad:
    fclose(f);
    printf("[CFChar] %s is cut short\n", path);
}

static pc_cf_char_c* pc_cf_char_get(const char* name) {
    int i;

    for (i = 0; i < PC_CF_CHAR_MAX; i++) {
        if (l_chars[i].name[0] != '\0' && strcmp(l_chars[i].name, name) == 0) {
            return &l_chars[i];
        }
    }
    for (i = 0; i < PC_CF_CHAR_MAX; i++) {
        if (l_chars[i].name[0] == '\0') {
            snprintf(l_chars[i].name, sizeof(l_chars[i].name), "%s", name);
            pc_cf_char_load(&l_chars[i]);
            return &l_chars[i];
        }
    }
    return NULL;
}

int pc_cf_char_exists(const char* name) {
    pc_cf_char_c* c = pc_cf_char_get(name);

    return c != NULL && c->loaded == 1;
}

/* --- posing --- */

static void pc_cf_rot_mul(float out[3][3], const float a[3][3], const float b[3][3]) {
    int r, k;

    for (r = 0; r < 3; r++) {
        for (k = 0; k < 3; k++) {
            out[r][k] = a[r][0] * b[0][k] + a[r][1] * b[1][k] + a[r][2] * b[2][k];
        }
    }
}

int pc_cf_char_play_draw(GAME_PLAY* play, const char* name, const pc_cf_mtx34 actor, int joint_num,
                         const char* const* joint_names, const pc_cf_mtx34* joints, int eye, float light) {
    pc_cf_char_c* c = pc_cf_char_get(name);
    GRAPH* graph = play->game.graph;
    float rot[PC_CF_CHAR_BONES][3][3];
    float pos[PC_CF_CHAR_BONES][3];
    int slot, b, j, r, k;

    if (c == NULL || c->loaded != 1) {
        return FALSE;
    }
    if (l_inst_frame != play->game_frame) {
        l_inst_frame = play->game_frame;
        l_inst_next = 0;
    }
    if (l_inst_next >= PC_CF_CHAR_INSTANCES) {
        return FALSE;
    }
    slot = l_inst_next++;

    for (b = 0; b < c->bone_num; b++) {
        const pc_cf_bone_c* bone = &c->bone[b];
        const float(*src)[4] = NULL;

        for (j = 0; j < joint_num; j++) {
            if (joint_names[j] != NULL && strcmp(joint_names[j], bone->name) == 0) {
                src = joints[j];
                break;
            }
        }
        if (bone->parent < 0) {
            /* the root: where the actor stands */
            for (r = 0; r < 3; r++) {
                for (k = 0; k < 3; k++) {
                    rot[b][r][k] = actor[r][k];
                }
                pos[b][r] = actor[r][3];
            }
            continue;
        }
        /* its own length, from the parent as posed */
        for (r = 0; r < 3; r++) {
            pos[b][r] = pos[bone->parent][r] + PC_CF_CHAR_SCALE * (rot[bone->parent][r][0] * bone->t[0] +
                                                                    rot[bone->parent][r][1] * bone->t[1] +
                                                                    rot[bone->parent][r][2] * bone->t[2]);
        }
        if (src != NULL) {
            /* its joint's turn */
            for (r = 0; r < 3; r++) {
                for (k = 0; k < 3; k++) {
                    rot[b][r][k] = src[r][k];
                }
            }
        } else {
            pc_cf_rot_mul(rot[b], rot[bone->parent], bone->rest);
        }
    }
    for (b = 0; b < c->bone_num; b++) {
        for (r = 0; r < 3; r++) {
            for (k = 0; k < 3; k++) {
                l_inst[slot].bones[b][r][k] = rot[b][r][k] * PC_CF_CHAR_SCALE;
            }
            l_inst[slot].bones[b][r][3] = pos[b][r];
        }
    }
    l_inst[slot].chr = c;
    l_inst[slot].eye = eye & 7;
    l_inst[slot].light = light;
    OPEN_DISP(graph);
    gDPNoOpTag(NEXT_POLY_OPA_DISP, PC_NOOP_CF_CHAR + slot);
    CLOSE_DISP(graph);
    return TRUE;
}

/* Bones already posed (pc_cf_anim.c): bone_names[i]'s world matrix is bones[i], scale included.
 * Bones not given keep their rest pose under their parent. */
int pc_cf_char_play_draw_mtx(GAME_PLAY* play, const char* name, int bone_num, const char* const* bone_names,
                             const pc_cf_mtx34* bones, int eye, float light) {
    pc_cf_char_c* c = pc_cf_char_get(name);
    GRAPH* graph = play->game.graph;
    int slot, b, j, r, k;

    if (c == NULL || c->loaded != 1) {
        return FALSE;
    }
    if (l_inst_frame != play->game_frame) {
        l_inst_frame = play->game_frame;
        l_inst_next = 0;
    }
    if (l_inst_next >= PC_CF_CHAR_INSTANCES) {
        return FALSE;
    }
    slot = l_inst_next++;

    for (b = 0; b < c->bone_num; b++) {
        const pc_cf_bone_c* bone = &c->bone[b];
        float(*out)[4] = l_inst[slot].bones[b];

        for (j = 0; j < bone_num; j++) {
            if (strcmp(bone_names[j], bone->name) == 0) {
                break;
            }
        }
        if (j < bone_num) {
            memcpy(out, bones[j], sizeof(pc_cf_mtx34));
        } else if (bone->parent >= 0) {
            const float(*p)[4] = l_inst[slot].bones[bone->parent];

            for (r = 0; r < 3; r++) {
                for (k = 0; k < 3; k++) {
                    out[r][k] = p[r][0] * bone->rest[0][k] + p[r][1] * bone->rest[1][k] + p[r][2] * bone->rest[2][k];
                }
                out[r][3] = p[r][0] * bone->t[0] + p[r][1] * bone->t[1] + p[r][2] * bone->t[2] + p[r][3];
            }
        } else {
            memset(out, 0, sizeof(pc_cf_mtx34)); /* nothing to place it by: not drawn */
        }
    }
    l_inst[slot].chr = c;
    l_inst[slot].eye = eye & 7;
    l_inst[slot].light = light;
    OPEN_DISP(graph);
    gDPNoOpTag(NEXT_POLY_OPA_DISP, PC_NOOP_CF_CHAR + slot);
    CLOSE_DISP(graph);
    return TRUE;
}

/* --- render side --- */

static const char* l_vert_src =
    "#version 330 core\n"
    "layout(location = 0) in float a_bone;\n"
    "layout(location = 1) in vec3 a_pos;\n"
    "layout(location = 2) in vec3 a_nrm;\n"
    "layout(location = 3) in vec2 a_uv;\n"
    "uniform mat4 u_proj;\n"
    "uniform mat4 u_view;\n"
    "uniform mat4 u_bones[32];\n"
    "out vec2 v_uv;\n"
    "out vec3 v_nrm;\n"
    "void main() {\n"
    "    mat4 b = u_bones[int(a_bone + 0.5)];\n"
    "    gl_Position = u_proj * (u_view * (b * vec4(a_pos, 1.0)));\n"
    "    v_uv = a_uv;\n"
    "    v_nrm = mat3(b) * a_nrm;\n"
    "}\n";

static const char* l_frag_src =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "in vec3 v_nrm;\n"
    "out vec4 frag;\n"
    "uniform sampler2D u_tex;\n"
    "uniform int u_has_tex;\n"
    "uniform int u_alpha_test;\n"
    "uniform float u_light;\n"
    "void main() {\n"
    "    vec4 c = vec4(1.0);\n"
    "    if (u_has_tex != 0) c = texture(u_tex, v_uv);\n"
    "    if (u_alpha_test != 0 && c.a < 0.5) discard;\n"
    "    /* soft light from above and in front, as the game's own characters */\n"
    "    float d = dot(normalize(v_nrm), normalize(vec3(0.2, 1.0, 0.8)));\n"
    "    float shade = 0.62 + 0.38 * clamp(d * 0.5 + 0.5, 0.0, 1.0);\n"
    "    frag = vec4(c.rgb * shade * u_light, c.a);\n"
    "}\n";

static GLuint l_prog;
static GLint l_u_proj, l_u_view, l_u_bones, l_u_has_tex, l_u_alpha_test, l_u_light;

static GLuint pc_cf_char_compile(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        printf("[CFChar] shader compile failed: %s\n", log);
    }
    return sh;
}

static int pc_cf_char_init(void) {
    GLuint vs, fs;
    GLint ok = 0;

    if (l_prog != 0) {
        return TRUE;
    }
    vs = pc_cf_char_compile(GL_VERTEX_SHADER, l_vert_src);
    fs = pc_cf_char_compile(GL_FRAGMENT_SHADER, l_frag_src);
    l_prog = glCreateProgram();
    glAttachShader(l_prog, vs);
    glAttachShader(l_prog, fs);
    glLinkProgram(l_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGetProgramiv(l_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        printf("[CFChar] shader link failed\n");
        glDeleteProgram(l_prog);
        l_prog = 0;
        return FALSE;
    }
    l_u_proj = glGetUniformLocation(l_prog, "u_proj");
    l_u_view = glGetUniformLocation(l_prog, "u_view");
    l_u_bones = glGetUniformLocation(l_prog, "u_bones");
    l_u_has_tex = glGetUniformLocation(l_prog, "u_has_tex");
    l_u_alpha_test = glGetUniformLocation(l_prog, "u_alpha_test");
    l_u_light = glGetUniformLocation(l_prog, "u_light");
    glUseProgram(l_prog);
    glUniform1i(glGetUniformLocation(l_prog, "u_tex"), 0);
    return TRUE;
}

static void pc_cf_char_upload(pc_cf_char_c* c) {
    int i;

    for (i = 0; i < c->tex_num; i++) {
        if (c->tex_gl[i] != 0 || c->tex_px[i] == NULL) {
            continue;
        }
        glGenTextures(1, &c->tex_gl[i]);
        glBindTexture(GL_TEXTURE_2D, c->tex_gl[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, c->tex_w[i], c->tex_h[i], 0, GL_RGBA, GL_UNSIGNED_BYTE, c->tex_px[i]);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    for (i = 0; i < c->mesh_num; i++) {
        pc_cf_char_mesh_c* m = &c->mesh[i];
        GLsizei stride = PC_CF_CHAR_VERT_FLOATS * sizeof(float);

        if (m->vao != 0 || m->verts == NULL) {
            continue;
        }
        glGenVertexArrays(1, &m->vao);
        glBindVertexArray(m->vao);
        glGenBuffers(1, &m->vbo);
        glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)m->vert_num * stride, m->verts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(1 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(float)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void*)(7 * sizeof(float)));
    }
    glBindVertexArray(0);
}

int pc_cf_char_is_tag(unsigned int tag) {
    return tag >= PC_NOOP_CF_CHAR && tag < PC_NOOP_CF_CHAR + PC_CF_CHAR_INSTANCES;
}

void pc_cf_char_noop(unsigned int tag, const float proj_unused[4][4], const float view[3][4]) {
    static const GLint wraps[] = { GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT, GL_REPEAT };
    int slot = (int)(tag - PC_NOOP_CF_CHAR);
    pc_cf_char_c* c;
    float proj[16], v[16], bones[PC_CF_CHAR_BONES][16];
    int pass, i, r;

    (void)proj_unused;
    if (!pc_cf_char_is_tag(tag) || (c = l_inst[slot].chr) == NULL || c->loaded != 1 || !pc_cf_char_init()) {
        return;
    }
    l_inst[slot].chr = NULL;
    pc_gx_draw_pending();
    pc_cf_char_upload(c);

    GXGetProjectionv(proj);
    memset(v, 0, sizeof(v));
    for (r = 0; r < 3; r++) {
        memcpy(&v[r * 4], view[r], 4 * sizeof(float));
    }
    v[15] = 1.0f;
    memset(bones, 0, sizeof(bones));
    for (i = 0; i < c->bone_num; i++) {
        for (r = 0; r < 3; r++) {
            memcpy(&bones[i][r * 4], l_inst[slot].bones[i][r], 4 * sizeof(float));
        }
        bones[i][15] = 1.0f;
    }

    glUseProgram(l_prog);
    glUniformMatrix4fv(l_u_proj, 1, GL_TRUE, proj); /* row-major, as pc_gx.c uploads them */
    glUniformMatrix4fv(l_u_view, 1, GL_TRUE, v);
    glUniformMatrix4fv(l_u_bones, c->bone_num, GL_TRUE, &bones[0][0]);
    glUniform1f(l_u_light, l_inst[slot].light);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < c->mesh_num; i++) {
            pc_cf_char_mesh_c* m = &c->mesh[i];
            int tex = m->tex;

            if (m->xlu != pass || m->vao == 0) {
                continue;
            }
            if (m->eyes && c->eye_tex[l_inst[slot].eye] >= 0) {
                tex = c->eye_tex[l_inst[slot].eye];
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
            glUniform1i(l_u_has_tex, tex >= 0 && tex < c->tex_num);
            if (tex >= 0 && tex < c->tex_num) {
                glBindTexture(GL_TEXTURE_2D, c->tex_gl[tex]);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wraps[m->wrap_s]);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wraps[m->wrap_t]);
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
