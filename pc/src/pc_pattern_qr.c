/* pc_pattern_qr.c - pattern QR codes in New Leaf's format (see pc_pattern_qr.h).
 *
 * New Leaf design (0x26C bytes, little-endian): title UTF-16 0x00 (20 chars + 0),
 * creator id 0x2A, creator UTF-16 0x2C (9), gender 0x3E, town id 0x40, town
 * UTF-16 0x42 (9), language 0x54, country 0x56, region 0x57, 15 palette codes
 * 0x58, 0x67 colour, 0x68 looks, design type 0x69 (0x09 plain pattern), pixels
 * 0x6C: 32 rows of 16 bytes, low nibble first, 0-14 palette, 15 transparent.
 * A normal design is one QR code holding those bytes (byte mode, level M).
 *
 * GameCube designs are 4-bit tiles (8x8, high nibble first) into one of 16
 * fixed palettes; colour 0 is an unused grey, so colours 1-15 map to New Leaf's
 * 15. Colours convert by the nearest match in CIE Lab.
 *
 * Files: qr/export/<slot>-<name>.png (QR + the pattern), qr/import/ read for
 * any PNG/JPG/BMP holding a design's QR code. */
#include "pc_pattern_qr.h"
#include "pc_menu_util.h"
#include "pc_text_draw.h"
#include "m_common_data.h"
#include "m_private.h"
#include "m_font.h"
#include "graph.h"
#include "main.h"

#include <dolphin/os.h>

#include "../lib/qrcodegen/qrcodegen.h"
#include "../lib/quirc/quirc.h"

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STB_IMAGE_IMPLEMENTATION
#include "../lib/stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../lib/stb/stb_image_write.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define qr_mkdir(p) _mkdir(p)
#else
#define qr_mkdir(p) mkdir((p), 0755)
#endif

#define NL_TITLE 0x00
#define NL_CREATOR_ID 0x2A
#define NL_CREATOR 0x2C
#define NL_TOWN_ID 0x40
#define NL_TOWN 0x42
#define NL_PALETTE 0x58
#define NL_LOOKS 0x68
#define NL_TYPE 0x69
#define NL_PIXELS 0x6C
#define NL_TYPE_PLAIN 0x09
#define NL_TRANSPARENT 15

#define QR_EXPORT_DIR "qr/export"
#define QR_IMPORT_DIR "qr/import"

/* New Leaf pattern colours by palette code (0x00-0xFF), 0 = not a colour. 0xRRGGBB, from
 * Thulinma's ACNLPatternTool (src/libs/ACNLFormat.js, WTFPL), checked against an emulator there. */
static const u32 l_qr_nl_colors[256] = {
    0xFFEEFF, 0xFF99AA, 0xEE5599, 0xFF66AA, 0xFF0066, 0xBB4477, 0xCC0055, 0x990033, 0x552233, 0, 0, 0, 0, 0, 0, 0xFFFFFF,
    0xFFBBCC, 0xFF7777, 0xDD3210, 0xFF5544, 0xFF0000, 0xCC6666, 0xBB4444, 0xBB0000, 0x882222, 0, 0, 0, 0, 0, 0, 0xEEEEEE,
    0xDDCDBB, 0xFFCD66, 0xDD6622, 0xFFAA22, 0xFF6600, 0xBB8855, 0xDD4400, 0xBB4400, 0x663210, 0, 0, 0, 0, 0, 0, 0xDDDDDD,
    0xFFEEDD, 0xFFDDCC, 0xFFCDAA, 0xFFBB88, 0xFFAA88, 0xDD8866, 0xBB6644, 0x995533, 0x884422, 0, 0, 0, 0, 0, 0, 0xCCCDCC,
    0xFFCDFF, 0xEE88FF, 0xCC66DD, 0xBB88CC, 0xCC00FF, 0x996699, 0x8800AA, 0x550077, 0x330044, 0, 0, 0, 0, 0, 0, 0xBBBBBB,
    0xFFBBFF, 0xFF99FF, 0xDD22BB, 0xFF55EE, 0xFF00CC, 0x885577, 0xBB0099, 0x880066, 0x550044, 0, 0, 0, 0, 0, 0, 0xAAAAAA,
    0xDDBB99, 0xCCAA77, 0x774433, 0xAA7744, 0x993200, 0x773222, 0x552200, 0x331000, 0x221000, 0, 0, 0, 0, 0, 0, 0x999999,
    0xFFFFCC, 0xFFFF77, 0xDDDD22, 0xFFFF00, 0xFFDD00, 0xCCAA00, 0x999900, 0x887700, 0x555500, 0, 0, 0, 0, 0, 0, 0x888888,
    0xDDBBFF, 0xBB99EE, 0x6632CC, 0x9955FF, 0x6600FF, 0x554488, 0x440099, 0x220066, 0x221033, 0, 0, 0, 0, 0, 0, 0x777777,
    0xBBBBFF, 0x8899FF, 0x3332AA, 0x3355EE, 0x0000FF, 0x333288, 0x0000AA, 0x101066, 0x000022, 0, 0, 0, 0, 0, 0, 0x666666,
    0x99EEBB, 0x66CD77, 0x226610, 0x44AA33, 0x008833, 0x557755, 0x225500, 0x103222, 0x002210, 0, 0, 0, 0, 0, 0, 0x555555,
    0xDDFFBB, 0xCCFF88, 0x88AA55, 0xAADD88, 0x88FF00, 0xAABB99, 0x66BB00, 0x559900, 0x336600, 0, 0, 0, 0, 0, 0, 0x444444,
    0xBBDDFF, 0x77CDFF, 0x335599, 0x6699FF, 0x1077FF, 0x4477AA, 0x224477, 0x002277, 0x001044, 0, 0, 0, 0, 0, 0, 0x333233,
    0xAAFFFF, 0x55FFFF, 0x0088BB, 0x55BBCC, 0x00CDFF, 0x4499AA, 0x006688, 0x004455, 0x002233, 0, 0, 0, 0, 0, 0, 0x222222,
    0xCCFFEE, 0xAAEEDD, 0x33CDAA, 0x55EEBB, 0x00FFCC, 0x77AAAA, 0x00AA99, 0x008877, 0x004433, 0, 0, 0, 0, 0, 0, 0x000000,
    0xAAFFAA, 0x77FF77, 0x66DD44, 0x00FF00, 0x22DD22, 0x55BB55, 0x00BB00, 0x008800, 0x224422, 0, 0, 0, 0, 0, 0, 0,
};
#define QR_NL_VALID(code) (l_qr_nl_colors[code] != 0 || (code) == 0xEF) /* 0xEF is black */

/* --- Colours --- */

typedef struct {
    f32 l, a, b;
} QrLab;

static f32 qr_srgb_lin(f32 c) {
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

static f32 qr_lab_f(f32 t) {
    return t > 0.008856f ? cbrtf(t) : 7.787f * t + 16.0f / 116.0f;
}

static QrLab qr_lab(int r, int g, int b) {
    f32 R = qr_srgb_lin(r / 255.0f), G = qr_srgb_lin(g / 255.0f), B = qr_srgb_lin(b / 255.0f);
    f32 x = qr_lab_f((0.4124f * R + 0.3576f * G + 0.1805f * B) / 0.95047f);
    f32 y = qr_lab_f(0.2126f * R + 0.7152f * G + 0.0722f * B);
    f32 z = qr_lab_f((0.0193f * R + 0.1192f * G + 0.9505f * B) / 1.08883f);
    QrLab lab = { 116.0f * y - 16.0f, 500.0f * (x - y), 200.0f * (y - z) };
    return lab;
}

static f32 qr_dist(QrLab p, QrLab q) {
    return (p.l - q.l) * (p.l - q.l) + (p.a - q.a) * (p.a - q.a) + (p.b - q.b) * (p.b - q.b);
}

static void qr_rgb5a3(u16 c, int rgb[3]) {
    if (c & 0x8000) {
        rgb[0] = ((c >> 10) & 31) * 255 / 31;
        rgb[1] = ((c >> 5) & 31) * 255 / 31;
        rgb[2] = (c & 31) * 255 / 31;
    } else {
        rgb[0] = ((c >> 8) & 15) * 17;
        rgb[1] = ((c >> 4) & 15) * 17;
        rgb[2] = (c & 15) * 17;
    }
}

static QrLab qr_gc_lab(int palette, int idx) {
    int rgb[3];

    qr_rgb5a3(mNW_PaletteIdx2Palette(palette)[idx & 15], rgb);
    return qr_lab(rgb[0], rgb[1], rgb[2]);
}

static QrLab qr_nl_lab(int code) {
    u32 c = l_qr_nl_colors[code & 0xFF];

    return qr_lab((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

/* --- Pixels --- */

static int qr_gc_pixel(const mNW_original_design_c* d, int x, int y) {
    u8 b = d->design.data[((y / 8) * 4 + (x / 8)) * 32 + (y % 8) * 4 + (x % 8) / 2];

    return (x & 1) ? (b & 15) : (b >> 4);
}

static void qr_gc_set_pixel(mNW_original_design_c* d, int x, int y, int v) {
    u8* b = &d->design.data[((y / 8) * 4 + (x / 8)) * 32 + (y % 8) * 4 + (x % 8) / 2];

    *b = (x & 1) ? (u8)((*b & 0xF0) | (v & 15)) : (u8)((*b & 0x0F) | ((v & 15) << 4));
}

static int qr_nl_pixel(const u8* nl, int x, int y) {
    u8 b = nl[NL_PIXELS + y * 16 + x / 2];

    return (x & 1) ? (b >> 4) : (b & 15);
}

/* --- Text: game strings are ASCII for letters, digits and common punctuation --- */

static int qr_plain_char(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '!' ||
           c == '?' || c == '.' || c == ',' || c == '\'' || c == '-' || c == '&' || c == '(' || c == ')' || c == ':';
}

static void qr_put_utf16(u8* nl, int ofs, int max_chars, const u8* s, int len) {
    int n = 0;
    int i;

    while (len > 0 && s[len - 1] == ' ') {
        len--;
    }
    for (i = 0; i < len && n < max_chars; i++) {
        int c = qr_plain_char(s[i]) ? s[i] : '?';

        nl[ofs + n * 2] = (u8)c;
        nl[ofs + n * 2 + 1] = 0;
        n++;
    }
}

/* UTF-16 to a game string of len bytes, space padded. */
static void qr_get_utf16(const u8* nl, int ofs, int max_chars, u8* out, int len) {
    int n = 0;
    int i;

    memset(out, ' ', len);
    for (i = 0; i < max_chars && n < len; i++) {
        int c = nl[ofs + i * 2] | (nl[ofs + i * 2 + 1] << 8);

        if (c == 0) {
            break;
        }
        out[n++] = (u8)(qr_plain_char(c) ? c : '?');
    }
}

/* Name for menus and files: printable, trailing spaces gone. */
static void qr_name_ascii(const u8* name, int len, char* out, int size) {
    int n = 0;
    int i;

    for (i = 0; i < len && n < size - 1; i++) {
        out[n++] = qr_plain_char(name[i]) ? (char)name[i] : '_';
    }
    while (n > 0 && out[n - 1] == ' ') {
        n--;
    }
    out[n] = '\0';
}

/* --- Conversion --- */

void pc_qr_design_to_nl(const mNW_original_design_c* design, const u8* creator, const u8* town, u8* nl) {
    int map[16];
    int i, x, y;

    memset(nl, 0, PC_QR_NL_SIZE);
    qr_put_utf16(nl, NL_TITLE, 20, design->name, mNW_ORIGINAL_DESIGN_NAME_LEN);
    if (creator != NULL) {
        qr_put_utf16(nl, NL_CREATOR, 9, creator, PLAYER_NAME_LEN);
    }
    if (town != NULL) {
        qr_put_utf16(nl, NL_TOWN, 9, town, LAND_NAME_SIZE);
    }
    nl[NL_LOOKS] = 0x0A;
    nl[NL_TYPE] = NL_TYPE_PLAIN;

    /* GameCube colours 1-15 become New Leaf's palette 0-14, each the nearest code. */
    for (i = 1; i < 16; i++) {
        QrLab c = qr_gc_lab(design->palette, i);
        f32 best = 1e30f;
        int code;

        for (code = 0; code < 256; code++) {
            f32 d;

            if (!QR_NL_VALID(code)) {
                continue;
            }
            d = qr_dist(c, qr_nl_lab(code));
            if (d < best) {
                best = d;
                nl[NL_PALETTE + i - 1] = (u8)code;
            }
        }
        map[i] = i - 1;
    }
    /* Colour 0 isn't offered by the editor; if a design has it, use the nearest of the 15. */
    {
        QrLab c = qr_gc_lab(design->palette, 0);
        f32 best = 1e30f;

        map[0] = 0;
        for (i = 1; i < 16; i++) {
            f32 d = qr_dist(c, qr_gc_lab(design->palette, i));

            if (d < best) {
                best = d;
                map[0] = i - 1;
            }
        }
    }

    for (y = 0; y < 32; y++) {
        for (x = 0; x < 32; x += 2) {
            nl[NL_PIXELS + y * 16 + x / 2] =
                (u8)(map[qr_gc_pixel(design, x, y)] | (map[qr_gc_pixel(design, x + 1, y)] << 4));
        }
    }
}

int pc_qr_nl_to_design(const u8* nl, mNW_original_design_c* design) {
    int count[16] = { 0 };
    QrLab want[16];
    int best_pal = 0;
    f32 best_cost = 1e30f;
    int map[16];
    int p, k, j, x, y;

    for (y = 0; y < 32; y++) {
        for (x = 0; x < 32; x++) {
            count[qr_nl_pixel(nl, x, y)]++;
        }
    }
    for (k = 0; k < 15; k++) {
        want[k] = qr_nl_lab(nl[NL_PALETTE + k]);
    }
    want[NL_TRANSPARENT] = qr_lab(255, 255, 255); /* see-through shows as white */

    /* The palette whose colours fit the used ones best, weighted by how much each is used. */
    for (p = 0; p < 16; p++) {
        f32 cost = 0.0f;

        for (k = 0; k < 16; k++) {
            f32 best = 1e30f;

            if (count[k] == 0) {
                continue;
            }
            for (j = 1; j < 16; j++) {
                f32 d = qr_dist(want[k], qr_gc_lab(p, j));

                if (d < best) {
                    best = d;
                }
            }
            cost += best * count[k];
        }
        if (cost < best_cost) {
            best_cost = cost;
            best_pal = p;
        }
    }
    for (k = 0; k < 16; k++) {
        f32 best = 1e30f;

        map[k] = 1;
        for (j = 1; j < 16; j++) {
            f32 d = qr_dist(want[k], qr_gc_lab(best_pal, j));

            if (d < best) {
                best = d;
                map[k] = j;
            }
        }
    }

    design->palette = (u8)best_pal;
    for (y = 0; y < 32; y++) {
        for (x = 0; x < 32; x++) {
            qr_gc_set_pixel(design, x, y, map[qr_nl_pixel(nl, x, y)]);
        }
    }
    qr_get_utf16(nl, NL_TITLE, 20, design->name, mNW_ORIGINAL_DESIGN_NAME_LEN);
    return TRUE;
}

/* A plausible New Leaf design: 32x32 type and real colour codes. */
static int qr_nl_ok(const u8* nl, int len) {
    int k;

    if (len < PC_QR_NL_SIZE || nl[NL_TYPE] < 0x07 || nl[NL_TYPE] > 0x09 || nl[NL_TYPE] == 0x08) {
        return FALSE;
    }
    for (k = 0; k < 15; k++) {
        if (!QR_NL_VALID(nl[NL_PALETTE + k])) {
            return FALSE;
        }
    }
    return TRUE;
}

/* --- QR encoding --- */

/* Does this QR read back as the design? Rendered at 3 px a module with a quiet zone, then decoded. */
static int qr_reads_back(const u8* qr, const u8* nl) {
    static struct quirc_data data;
    struct quirc* q = quirc_new();
    int size = qrcodegen_getSize(qr);
    int px = (size + 8) * 3;
    int ok = FALSE;
    int x, y, i;

    if (q == NULL) {
        return TRUE; /* can't check: trust the encoder */
    }
    if (quirc_resize(q, px, px) >= 0) {
        u8* img = quirc_begin(q, NULL, NULL);

        for (y = 0; y < px; y++) {
            for (x = 0; x < px; x++) {
                int mx = x / 3 - 4, my = y / 3 - 4;

                img[y * px + x] = (mx >= 0 && my >= 0 && mx < size && my < size && qrcodegen_getModule(qr, mx, my))
                                      ? 0 : 255;
            }
        }
        quirc_end(q);
        for (i = 0; i < quirc_count(q) && !ok; i++) {
            struct quirc_code code;

            quirc_extract(q, i, &code);
            ok = quirc_decode(&code, &data) == QUIRC_SUCCESS && data.payload_len == PC_QR_NL_SIZE &&
                 memcmp(data.payload, nl, PC_QR_NL_SIZE) == 0;
        }
    }
    quirc_destroy(q);
    return ok;
}

/* Encodes a design; returns the QR size in modules, 0 on failure. qr must hold qrcodegen_BUFFER_LEN_MAX.
 * Very regular designs (stripes, solid colours) can come out, with the mask the encoder
 * prefers, as checkerboard that QR readers lose their bearings in, so every code is read
 * back first and the other masks are tried if it doesn't. */
static int qr_encode(const u8* nl, u8* qr) {
    static u8 temp[qrcodegen_BUFFER_LEN_MAX];
    int mask;

    for (mask = -1; mask < 8; mask++) {
        memcpy(temp, nl, PC_QR_NL_SIZE);
        if (!qrcodegen_encodeBinary(temp, PC_QR_NL_SIZE, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                    qrcodegen_VERSION_MAX, mask < 0 ? qrcodegen_Mask_AUTO : (enum qrcodegen_Mask)mask,
                                    false)) {
            return 0;
        }
        if (qr_reads_back(qr, nl)) {
            return qrcodegen_getSize(qr);
        }
        OSReport("[qr] mask %d didn't read back, trying another\n", mask);
    }
    return qrcodegen_getSize(qr); /* none read back; keep the last */
}

/* --- PNG export --- */

#define QR_PNG_MODULE 4
#define QR_PNG_QUIET 4
#define QR_PNG_PATTERN 6 /* pixels per design pixel */

static void qr_export_png(const char* path, const u8* qr, int size, const mNW_original_design_c* design) {
    int qr_px = (size + QR_PNG_QUIET * 2) * QR_PNG_MODULE;
    int pat_px = 32 * QR_PNG_PATTERN;
    int w = qr_px + pat_px + QR_PNG_QUIET * QR_PNG_MODULE;
    int h = qr_px > pat_px ? qr_px : pat_px;
    u8* img = (u8*)malloc((size_t)w * h * 3);
    int x, y;

    if (img == NULL) {
        return;
    }
    memset(img, 255, (size_t)w * h * 3);
    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            if (qrcodegen_getModule(qr, x, y)) {
                int px, py;

                for (py = 0; py < QR_PNG_MODULE; py++) {
                    for (px = 0; px < QR_PNG_MODULE; px++) {
                        u8* p = &img[(((y + QR_PNG_QUIET) * QR_PNG_MODULE + py) * w +
                                      (x + QR_PNG_QUIET) * QR_PNG_MODULE + px) * 3];
                        p[0] = p[1] = p[2] = 0;
                    }
                }
            }
        }
    }
    /* The pattern itself, to the right. */
    for (y = 0; y < pat_px; y++) {
        for (x = 0; x < pat_px; x++) {
            int rgb[3];
            u8* p = &img[((y + (h - pat_px) / 2) * w + qr_px + x) * 3];

            qr_rgb5a3(mNW_PaletteIdx2Palette(design->palette)[qr_gc_pixel(design, x / QR_PNG_PATTERN,
                                                                             y / QR_PNG_PATTERN)],
                      rgb);
            p[0] = (u8)rgb[0];
            p[1] = (u8)rgb[1];
            p[2] = (u8)rgb[2];
        }
    }
    if (!stbi_write_png(path, w, h, 3, img, w * 3)) {
        OSReport("[qr] Could not write %s\n", path);
    }
    free(img);
}

/* --- Menu page --- */

#define QR_TEX_MAX 112 /* 4-bit tiles; fits version 25 with a quiet zone of 4 */
#define QR_IMPORT_MAX 32

typedef struct {
    u8 tex[QR_TEX_MAX * QR_TEX_MAX / 2] ATTRIBUTE_ALIGN(32);
    int tex_size;
} QrTex;

typedef struct {
    char file[64];
    mNW_original_design_c design;
} QrImport;

enum { QR_PAGE_SHARE, QR_PAGE_IMPORT, QR_PAGE_CONFIRM };
enum { QR_ACT_UP = 1, QR_ACT_DOWN, QR_ACT_LEFT, QR_ACT_RIGHT, QR_ACT_CONFIRM, QR_ACT_CANCEL };

static u16 l_qr_tlut[16] ATTRIBUTE_ALIGN(32) = { 0xFFFF, 0x8000 }; /* white, black */
static QrTex l_qr_slot_tex[mPr_ORIGINAL_DESIGN_COUNT];
static QrImport l_qr_imports[QR_IMPORT_MAX];
static int l_qr_import_count;
static int l_qr_active;
static int l_qr_page;
static int l_qr_row;
static int l_qr_slot;   /* shown slot, in the order the game lists them */
static int l_qr_import; /* shown import */
static int l_qr_target; /* slot an import goes into */
static int l_qr_confirm_sel;
static char l_qr_status[96];

static Private_c* qr_resident(void) {
    return Common_Get(player_no) < PLAYER_NUM ? Now_Private : NULL;
}

static int qr_image(int shown) {
    return Now_Private->my_org_no_table[shown & 7] & 7;
}

/* QR modules into a CI4 texture (index 0 white, 1 black) with a quiet zone of 4. */
static void qr_to_tex(const u8* qr, int size, QrTex* t) {
    int tex = (size + 8 + 7) & ~7;
    int x, y;

    if (tex > QR_TEX_MAX) {
        t->tex_size = 0;
        return;
    }
    memset(t->tex, 0, sizeof(t->tex));
    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            if (qrcodegen_getModule(qr, x, y)) {
                int tx = x + 4, ty = y + 4;
                u8* b = &t->tex[((ty / 8) * (tex / 8) + (tx / 8)) * 32 + (ty % 8) * 4 + (tx % 8) / 2];

                *b |= (tx & 1) ? 0x01 : 0x10;
            }
        }
    }
    t->tex_size = tex;
}

/* Makes every slot's QR code, and saves them as PNGs. */
static void qr_build_share(void) {
    static u8 qr[qrcodegen_BUFFER_LEN_MAX];
    u8 nl[PC_QR_NL_SIZE];
    Private_c* priv = qr_resident();
    int saved = 0;
    int i;

    qr_mkdir("qr");
    qr_mkdir(QR_EXPORT_DIR);
    qr_mkdir(QR_IMPORT_DIR);
    for (i = 0; i < mPr_ORIGINAL_DESIGN_COUNT; i++) {
        const mNW_original_design_c* d = &priv->my_org[qr_image(i)];
        int size;

        pc_qr_design_to_nl(d, priv->player_ID.player_name, priv->player_ID.land_name, nl);
        size = qr_encode(nl, qr);
        if (size == 0) {
            l_qr_slot_tex[i].tex_size = 0;
            continue;
        }
        qr_to_tex(qr, size, &l_qr_slot_tex[i]);
        {
            char name[24];
            char path[96];

            qr_name_ascii(d->name, mNW_ORIGINAL_DESIGN_NAME_LEN, name, sizeof(name));
            snprintf(path, sizeof(path), QR_EXPORT_DIR "/%d-%s.png", i + 1, name[0] ? name : "pattern");
            qr_export_png(path, qr, size, d);
            saved++;
        }
    }
    /* (the game font has no slash) */
    snprintf(l_qr_status, sizeof(l_qr_status), "Saved %d QR codes in the qr folder (export)", saved);
}

/* Reads every image in qr/import for New Leaf design QR codes. */
static void qr_scan_imports(void) {
    struct quirc* q = quirc_new();
    DIR* dir = opendir(QR_IMPORT_DIR);
    struct dirent* e;

    l_qr_import_count = 0;
    if (q == NULL || dir == NULL) {
        if (q != NULL) {
            quirc_destroy(q);
        }
        if (dir != NULL) {
            closedir(dir);
        }
        return;
    }
    while ((e = readdir(dir)) != NULL && l_qr_import_count < QR_IMPORT_MAX) {
        char path[320];
        int w, h, n, i, count;
        u8* img;

        if (e->d_name[0] == '.') {
            continue;
        }
        snprintf(path, sizeof(path), QR_IMPORT_DIR "/%s", e->d_name);
        img = stbi_load(path, &w, &h, &n, 1);
        if (img == NULL) {
            continue;
        }
        if (quirc_resize(q, w, h) >= 0) {
            u8* buf = quirc_begin(q, NULL, NULL);

            memcpy(buf, img, (size_t)w * h);
            quirc_end(q);
            count = quirc_count(q);
            for (i = 0; i < count && l_qr_import_count < QR_IMPORT_MAX; i++) {
                struct quirc_code code;
                static struct quirc_data data;
                quirc_decode_error_t err;

                quirc_extract(q, i, &code);
                err = quirc_decode(&code, &data);
                if (err == QUIRC_ERROR_DATA_ECC) {
                    quirc_flip(&code); /* mirrored photos */
                    err = quirc_decode(&code, &data);
                }
                if (err != QUIRC_SUCCESS) {
                    continue;
                }
                if (qr_nl_ok(data.payload, data.payload_len)) {
                    QrImport* imp = &l_qr_imports[l_qr_import_count++];

                    memset(imp, 0, sizeof(*imp));
                    snprintf(imp->file, sizeof(imp->file), "%s", e->d_name);
                    pc_qr_nl_to_design(data.payload, &imp->design);
                }
            }
        }
        stbi_image_free(img);
    }
    closedir(dir);
    quirc_destroy(q);
}

int pc_qr_menu_available(void) {
    return qr_resident() != NULL;
}

void pc_qr_menu_enter(void) {
    if (qr_resident() == NULL) {
        return;
    }
    l_qr_active = TRUE;
    l_qr_page = QR_PAGE_SHARE;
    l_qr_row = 0;
    qr_build_share();
}

int pc_qr_menu_active(void) {
    return l_qr_active;
}

static void qr_do_import(void) {
    Private_c* priv = qr_resident();
    mNW_original_design_c* dst;
    const QrImport* imp = &l_qr_imports[l_qr_import];
    if (priv == NULL || l_qr_import >= l_qr_import_count) {
        return;
    }
    dst = &priv->my_org[qr_image(l_qr_target)];
    memcpy(dst->name, imp->design.name, sizeof(dst->name));
    dst->palette = imp->design.palette;
    mNW_OverWriteOriginalTexture(dst, (u8*)imp->design.design.data);
    /* A new pattern in that slot: pattern IDs (pc_pattern.c) give it a new ID at the next reconcile. */
    qr_build_share();
    snprintf(l_qr_status, sizeof(l_qr_status), "Imported into pattern %d", l_qr_target + 1);
}

void pc_qr_menu_action(int act) {
    int rows = l_qr_page == QR_PAGE_SHARE ? 3 : 4;

    if (!l_qr_active) {
        return;
    }
    if (l_qr_page == QR_PAGE_CONFIRM) {
        if (act == QR_ACT_LEFT || act == QR_ACT_RIGHT || act == QR_ACT_UP || act == QR_ACT_DOWN) {
            l_qr_confirm_sel ^= 1;
        } else if (act == QR_ACT_CONFIRM) {
            if (l_qr_confirm_sel == 1) {
                qr_do_import();
            }
            l_qr_page = QR_PAGE_IMPORT;
        } else if (act == QR_ACT_CANCEL) {
            l_qr_page = QR_PAGE_IMPORT;
        }
        return;
    }

    switch (act) {
        case QR_ACT_UP:
            l_qr_row = (l_qr_row + rows - 1) % rows;
            break;
        case QR_ACT_DOWN:
            l_qr_row = (l_qr_row + 1) % rows;
            break;
        case QR_ACT_LEFT:
        case QR_ACT_RIGHT: {
            int dir = act == QR_ACT_RIGHT ? 1 : -1;

            if (l_qr_page == QR_PAGE_SHARE && l_qr_row == 0) {
                l_qr_slot = (l_qr_slot + 8 + dir) % 8;
            } else if (l_qr_page == QR_PAGE_IMPORT && l_qr_row == 0 && l_qr_import_count > 0) {
                l_qr_import = (l_qr_import + l_qr_import_count + dir) % l_qr_import_count;
            } else if (l_qr_page == QR_PAGE_IMPORT && l_qr_row == 1) {
                l_qr_target = (l_qr_target + 8 + dir) % 8;
            }
        } break;
        case QR_ACT_CONFIRM:
            if (l_qr_page == QR_PAGE_SHARE) {
                if (l_qr_row == 0) {
                    l_qr_slot = (l_qr_slot + 1) % 8;
                } else if (l_qr_row == 1) {
                    qr_scan_imports();
                    l_qr_page = QR_PAGE_IMPORT;
                    l_qr_row = 0;
                    l_qr_import = 0;
                    snprintf(l_qr_status, sizeof(l_qr_status), "%d pattern%s found in the qr folder (import)", l_qr_import_count,
                             l_qr_import_count == 1 ? "" : "s");
                } else {
                    l_qr_active = FALSE;
                }
            } else {
                if (l_qr_row == 0 && l_qr_import_count > 0) {
                    l_qr_import = (l_qr_import + 1) % l_qr_import_count;
                } else if (l_qr_row == 1) {
                    l_qr_target = (l_qr_target + 1) % 8;
                } else if (l_qr_row == 2 && l_qr_import_count > 0) {
                    l_qr_page = QR_PAGE_CONFIRM;
                    l_qr_confirm_sel = 0;
                } else if (l_qr_row == 3) {
                    l_qr_page = QR_PAGE_SHARE;
                    l_qr_row = 1;
                }
            }
            break;
        case QR_ACT_CANCEL:
            if (l_qr_page == QR_PAGE_IMPORT) {
                l_qr_page = QR_PAGE_SHARE;
                l_qr_row = 1;
            } else {
                l_qr_active = FALSE;
            }
            break;
    }
}

static void qr_draw_design(struct game_s* game, const mNW_original_design_c* d, f32 x, f32 y, f32 size) {
    pc_image_draw_ci4(game, d->design.data, mNW_PaletteIdx2Palette(d->palette), 32, 32, x, y, size, size);
}

static void qr_draw_row(struct game_s* game, int row, const char* text, f32 y) {
    int r, g, b, a;
    int sel = l_qr_row == row;

    pc_menu_row_colors(sel, &r, &g, &b, &a);
    pc_menu_draw_left(game, text, 176.0f, y, r, g, b, a, sel ? PC_MENU_SCALE_SELECTED : 1.0f);
}

void pc_qr_menu_draw(struct game_s* game) {
    Private_c* priv = qr_resident();
    char line[64];
    char name[24];

    if (!l_qr_active || priv == NULL) {
        return;
    }

    if (l_qr_page == QR_PAGE_SHARE) {
        const mNW_original_design_c* d = &priv->my_org[qr_image(l_qr_slot)];
        const QrTex* t = &l_qr_slot_tex[l_qr_slot];

        pc_menu_draw_centered(game, "- Pattern QR Codes -", 14.0f, 255, 255, 255, 255, 1.0f);
        if (t->tex_size > 0) {
            pc_image_draw_ci4(game, t->tex, l_qr_tlut, t->tex_size, t->tex_size, 16.0f, 36.0f, 150.0f, 150.0f);
        }
        qr_draw_design(game, d, 176.0f, 36.0f, 64.0f);
        qr_name_ascii(d->name, mNW_ORIGINAL_DESIGN_NAME_LEN, name, sizeof(name));
        snprintf(line, sizeof(line), "< %d: %s >", l_qr_slot + 1, name);
        qr_draw_row(game, 0, line, 108.0f);
        qr_draw_row(game, 1, "Import QR codes...", 128.0f);
        qr_draw_row(game, 2, "Back", 148.0f);
        pc_menu_draw_left(game, "Scan with New Leaf or NookLink", 176.0f, 170.0f, 170, 170, 170, 200, 0.8f);
    } else {
        pc_menu_draw_centered(game, "- Import QR Codes -", 14.0f, 255, 255, 255, 255, 1.0f);
        if (l_qr_import_count > 0) {
            const QrImport* imp = &l_qr_imports[l_qr_import];

            qr_draw_design(game, &imp->design, 24.0f, 40.0f, 128.0f);
            qr_name_ascii(imp->design.name, mNW_ORIGINAL_DESIGN_NAME_LEN, name, sizeof(name));
            snprintf(line, sizeof(line), "< %d of %d: %s >", l_qr_import + 1, l_qr_import_count, name);
            qr_draw_row(game, 0, line, 44.0f);
            pc_menu_draw_left(game, imp->file, 176.0f, 62.0f, 170, 170, 170, 200, 0.8f);
        } else {
            pc_menu_draw_left(game, "Put QR images in the qr folder (import)", 24.0f, 60.0f, 220, 220, 220, 230, 0.9f);
            qr_draw_row(game, 0, "No QR codes found", 44.0f);
        }
        qr_name_ascii(priv->my_org[qr_image(l_qr_target)].name, mNW_ORIGINAL_DESIGN_NAME_LEN, name, sizeof(name));
        snprintf(line, sizeof(line), "< Into %d: %s >", l_qr_target + 1, name);
        qr_draw_row(game, 1, line, 84.0f);
        qr_draw_design(game, &priv->my_org[qr_image(l_qr_target)], 176.0f, 100.0f, 40.0f);
        qr_draw_row(game, 2, "Import", 150.0f);
        qr_draw_row(game, 3, "Back", 170.0f);
        if (l_qr_page == QR_PAGE_CONFIRM) {
            pc_menu_dim_rect(game->graph, 235);
            snprintf(line, sizeof(line), "Replace pattern %d: %s?", l_qr_target + 1, name);
            pc_menu_draw_centered(game, line, 100.0f, 255, 255, 255, 255, 1.0f);
            pc_menu_draw_two_choice(game, "No", "Yes", l_qr_confirm_sel, 130.0f);
        }
    }
    pc_menu_draw_centered(game, l_qr_status, 200.0f, 200, 230, 160, 230, 0.9f);
}
