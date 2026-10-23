#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_color0;
layout(location = 3) in vec2 a_texcoord0;
uniform mat4 u_projection;
uniform mat4 u_modelview;
uniform mat3 u_normal_mtx;
uniform vec4 u_texmtx_row0[2];
uniform vec4 u_texmtx_row1[2];
uniform int u_texmtx_enable[2];
uniform int u_texgen_src[2];  /* 1=GX_TG_NRM, 4=GX_TG_TEX0, etc. */
uniform vec4 u_curve_up;   /* xyz: world up in eye space, w: curve strength (0 = flat) */
uniform vec4 u_curve_fwd;  /* xyz: level forward in eye space, w: its value at the flat point */
out vec4 v_color;
out vec2 v_texcoord0;
out vec2 v_texcoord1;
out vec3 v_normal;
out float v_fog_z;
out float v_curve_drop;  /* how far the curve lowered this point (ahead of the flat point only) */
void main() {
    vec4 eyePos = u_modelview * vec4(a_position, 1.0);
    v_curve_drop = 0.0;
    if (u_curve_up.w != 0.0) {
        /* rolling log: drop by k*u^2 with u the distance ahead of/behind the flat point */
        float u = dot(eyePos.xyz, u_curve_fwd.xyz) - u_curve_fwd.w;
        float drop = u_curve_up.w * u * u;
        eyePos.xyz -= u_curve_up.xyz * drop;
        v_curve_drop = u > 0.0 ? drop : 0.0;
    }
    gl_Position = u_projection * eyePos;
    v_fog_z = -eyePos.z;
    v_color = a_color0;
    v_normal = normalize(u_normal_mtx * a_normal);
    vec4 tc0 = (u_texgen_src[0] == 1) ? vec4(v_normal, 1.0) : vec4(a_texcoord0, 0.0, 1.0);
    if (u_texmtx_enable[0] != 0) {
        v_texcoord0 = vec2(dot(u_texmtx_row0[0], tc0), dot(u_texmtx_row1[0], tc0));
    } else {
        v_texcoord0 = tc0.xy;
    }
    vec4 tc1 = (u_texgen_src[1] == 1) ? vec4(v_normal, 1.0) : vec4(a_texcoord0, 0.0, 1.0);
    if (u_texmtx_enable[1] != 0) {
        v_texcoord1 = vec2(dot(u_texmtx_row0[1], tc1), dot(u_texmtx_row1[1], tc1));
    } else {
        v_texcoord1 = tc1.xy;
    }
}
