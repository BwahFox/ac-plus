/* pc_celeste.c - Celeste's lines and helpers. See pc_celeste.h. */
#include "pc_celeste.h"
#include "pc_cf_lines.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int pc_celeste_available(void) {
    return pc_cf_lines_available(PC_CF_CELESTE);
}

void pc_celeste_set_talking(int on) {
    pc_cf_lines_set_talking(on ? PC_CF_CELESTE : -1);
}

int pc_celeste_line_has_choices(int n) {
    return pc_cf_lines_has_choices(PC_CF_CELESTE, n);
}

/* --- strings --- */

static void pc_celeste_str(u8 out[16], const char* s) {
    int i;

    memset(out, ' ', 16);
    for (i = 0; i < 16 && s[i] != '\0'; i++) {
        out[i] = (u8)s[i];
    }
}

void pc_celeste_name_str(const pc_constellation_c* c, u8 out[16]) {
    memcpy(out, c->name, 16);
}

/* --- when a constellation is best seen --- */

#define PC_CEL_PI 3.14159265f
#define PC_CEL_COL_W (2.0f * PC_CEL_PI / PC_CONST_COLS)

static f32 pc_celeste_center(const pc_constellation_c* c) {
    f32 az0, up, az, sum = 0.0f, d;
    int i;

    if (c->link_num == 0) {
        return 0.0f;
    }
    pc_const_star(c->link[0][0].col, c->link[0][0].row, &az0, &up);
    for (i = 0; i < c->link_num; i++) {
        pc_const_star(c->link[i][0].col, c->link[i][0].row, &az, &up);
        d = fmodf(az - az0 + 3.0f * PC_CEL_PI, 2.0f * PC_CEL_PI) - PC_CEL_PI;
        sum += d;
    }
    return az0 + sum / c->link_num;
}

static f32 pc_celeste_frac(f32 x) {
    return x - floorf(x);
}

static int pc_celeste_day_of_year(void) {
    static const u16 month_start[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    extern int pc_const_rtc_month(void);
    extern int pc_const_rtc_day(void);
    int month = pc_const_rtc_month();

    return month_start[(month >= 1 && month <= 12 ? month : 1) - 1] + pc_const_rtc_day() - 1;
}

/* The sky turns hour / 24 + day / 365.25 of a circle (pc_const_sky_turn); a constellation at sky
 * azimuth c is due north (the town camera's view) when that is -c / 2 pi */
void pc_celeste_peak_tonight(const pc_constellation_c* c, int* hour, int* minute) {
    f32 h = 24.0f * pc_celeste_frac(-pc_celeste_center(c) / (2.0f * PC_CEL_PI) -
                                    pc_celeste_day_of_year() / 365.25f);
    int total = (int)(h * 60.0f + 0.5f) % (24 * 60);

    *hour = total / 60;
    *minute = total % 60;
}

void pc_celeste_peak_date(const pc_constellation_c* c, int* month, int* day) {
    static const u8 month_days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int d = (int)(365.25f * pc_celeste_frac(-pc_celeste_center(c) / (2.0f * PC_CEL_PI) - 20.0f / 24.0f)) % 365;
    int m = 0;

    while (m < 11 && d >= month_days[m]) {
        d -= month_days[m++];
    }
    *month = m + 1;
    *day = d + 1;
}

void pc_celeste_time_strs(int hour, int minute, u8 h[16], u8 m[16], u8 ampm[16]) {
    char buf[8];

    snprintf(buf, sizeof(buf), "%d", hour % 12 == 0 ? 12 : hour % 12);
    pc_celeste_str(h, buf);
    snprintf(buf, sizeof(buf), "%02d", minute);
    pc_celeste_str(m, buf);
    pc_celeste_str(ampm, hour < 12 ? "AM" : "PM");
}

void pc_celeste_date_strs(int month, int day, u8 mon[16], u8 dstr[16]) {
    static const char* names[] = { "January", "February", "March",     "April",   "May",      "June",
                                   "July",    "August",   "September", "October", "November", "December" };
    char buf[8];
    const char* suffix = (day % 10 == 1 && day != 11) ? "st" : (day % 10 == 2 && day != 12) ? "nd"
                         : (day % 10 == 3 && day != 13) ? "rd" : "th";

    pc_celeste_str(mon, names[(month >= 1 && month <= 12 ? month : 1) - 1]);
    snprintf(buf, sizeof(buf), "%d%s", day, suffix);
    pc_celeste_str(dstr, buf);
}

/* --- her colours --- */

extern unsigned short owl_1_pal[];
u16 pc_celeste_pal[16] __attribute__((aligned(32)));

static void pc_celeste_rgb5a3(u16 v, f32* r, f32* g, f32* b, int* a) {
    if (v & 0x8000) {
        *r = ((v >> 10) & 31) / 31.0f, *g = ((v >> 5) & 31) / 31.0f, *b = (v & 31) / 31.0f;
        *a = -1;
    } else {
        *r = ((v >> 8) & 15) / 15.0f, *g = ((v >> 4) & 15) / 15.0f, *b = (v & 15) / 15.0f;
        *a = (v >> 12) & 7;
    }
}

static u16 pc_celeste_pack(f32 r, f32 g, f32 b, int a) {
    if (a < 0) {
        return (u16)(0x8000 | ((int)(r * 31 + 0.5f) << 10) | ((int)(g * 31 + 0.5f) << 5) | (int)(b * 31 + 0.5f));
    }
    return (u16)((a << 12) | ((int)(r * 15 + 0.5f) << 8) | ((int)(g * 15 + 0.5f) << 4) | (int)(b * 15 + 0.5f));
}

extern unsigned char owl_1_tmem_txt[];
extern unsigned char owl_1_eye1_TA_tex_txt[], owl_1_eye2_TA_tex_txt[], owl_1_eye3_TA_tex_txt[],
    owl_1_eye4_TA_tex_txt[], owl_1_eye5_TA_tex_txt[], owl_1_eye6_TA_tex_txt[], owl_1_eye7_TA_tex_txt[],
    owl_1_eye8_TA_tex_txt[];

#define PC_CELESTE_TEX_FILE "cf_sky/celeste.tex"
#define PC_CELESTE_TMEM_SIZE 0x640
#define PC_CELESTE_EYE_SIZE 0x100

u8 pc_celeste_tmem[PC_CELESTE_TMEM_SIZE] __attribute__((aligned(32)));
u8 pc_celeste_eye[8][PC_CELESTE_EYE_SIZE] __attribute__((aligned(32)));

/* Blathers' body sheet (owl_1.c's texture loads): offset, width, height */
static const u16 l_parts[][3] = { { 0x000, 16, 8 },  { 0x040, 16, 16 }, { 0x0C0, 32, 8 },  { 0x140, 32, 8 },
                                  { 0x1C0, 32, 8 },  { 0x240, 32, 24 }, { 0x3C0, 32, 24 }, { 0x540, 16, 16 },
                                  { 0x5C0, 16, 8 },  { 0x600, 16, 8 } };

#define PC_CELESTE_CLEAR 15 /* celeste_tex.py's see-through palette entry */

static int pc_celeste_clear(u16 v) {
    return (v & 0x8000) == 0 && (v >> 12 & 7) == 0;
}

/* Her own textures from City Folk's, by extract_cf_sky.py (celeste_tex.py): "CELT", version 2,
 * her 16 colours (RGB5A3, little-endian), remap[16] (Blathers' colour -> hers), a u16 of the parts
 * painted from her textures, then the body sheet and 8 eye frames in Blathers' layout (GX CI4).
 * Parts not painted are his, in her colours. */
static int pc_celeste_load_textures(void) {
    FILE* f = fopen(PC_CELESTE_TEX_FILE, "rb");
    u8 magic[4], remap[16];
    u8 sheet[PC_CELESTE_TMEM_SIZE];
    u32 version = 0;
    u16 painted = 0;
    int ok, k, i;

    if (f == NULL) {
        return FALSE;
    }
    ok = fread(magic, 1, 4, f) == 4 && memcmp(magic, "CELT", 4) == 0 && fread(&version, 4, 1, f) == 1 &&
         version == 2 && fread(pc_celeste_pal, 2, 16, f) == 16 && fread(remap, 1, 16, f) == 16 &&
         fread(&painted, 2, 1, f) == 1 && fread(sheet, 1, sizeof(sheet), f) == sizeof(sheet) &&
         fread(pc_celeste_eye, 1, sizeof(pc_celeste_eye), f) == sizeof(pc_celeste_eye);
    fclose(f);
    if (!ok) {
        return FALSE;
    }
    for (k = 0; k < (int)(sizeof(l_parts) / sizeof(l_parts[0])); k++) {
        int off = l_parts[k][0], size = l_parts[k][1] * l_parts[k][2] / 2;

        for (i = off; i < off + size; i++) {
            u8 v = owl_1_tmem_txt[i];
            u8 c = (painted >> k) & 1 ? sheet[i] : (u8)(remap[v >> 4] << 4 | remap[v & 15]);

            /* his see-through texels stay see-through: the model's cut-outs rely on them */
            if (pc_celeste_clear(owl_1_pal[v >> 4])) {
                c = (u8)((c & 0x0F) | PC_CELESTE_CLEAR << 4);
            }
            if (pc_celeste_clear(owl_1_pal[v & 15])) {
                c = (u8)((c & 0xF0) | PC_CELESTE_CLEAR);
            }
            pc_celeste_tmem[i] = c;
        }
    }
    return TRUE;
}

static void pc_celeste_recolour(void);

/* Her textures: City Folk's if extracted, else Blathers' recoloured */
void pc_celeste_make_palette(void) {
    u8* eyes[8] = { owl_1_eye1_TA_tex_txt, owl_1_eye2_TA_tex_txt, owl_1_eye3_TA_tex_txt, owl_1_eye4_TA_tex_txt,
                    owl_1_eye5_TA_tex_txt, owl_1_eye6_TA_tex_txt, owl_1_eye7_TA_tex_txt, owl_1_eye8_TA_tex_txt };
    int i;

    if (pc_celeste_load_textures()) {
        return;
    }
    memcpy(pc_celeste_tmem, owl_1_tmem_txt, PC_CELESTE_TMEM_SIZE);
    for (i = 0; i < 8; i++) {
        memcpy(pc_celeste_eye[i], eyes[i], PC_CELESTE_EYE_SIZE);
    }
    pc_celeste_recolour();
}

/* Blathers' browns become her deep reds and pinks, his cream a pale pink; greys, white and black stay */
static void pc_celeste_recolour(void) {
    int i;

    for (i = 0; i < 16; i++) {
        u16 v = owl_1_pal[i];
        f32 r, g, b, mx, mn, s, l;
        int a;

        pc_celeste_rgb5a3(v, &r, &g, &b, &a);
        mx = fmaxf(r, fmaxf(g, b));
        mn = fminf(r, fminf(g, b));
        s = mx > 0.0f ? (mx - mn) / mx : 0.0f;
        l = (mx + mn) * 0.5f;
        if (s > 0.2f && r >= g && g >= b) { /* warm: brown, tan, orange, cream */
            if (l > 0.7f) {
                r = 0.98f, g = 0.78f, b = 0.80f;                         /* cream -> pale pink */
            } else {
                f32 k = l / 0.7f;                                         /* brown -> red to pink */
                r = 0.45f + 0.5f * k, g = 0.05f + 0.35f * k * k, b = 0.10f + 0.30f * k * k;
            }
        }
        pc_celeste_pal[i] = pc_celeste_pack(r, g, b, a);
    }
}
