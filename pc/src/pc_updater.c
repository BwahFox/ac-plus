/* pc_updater.c - at startup, offer a newer ac-plus release and install it.
 *
 * Release zips carry a VERSION file next to the exe (./ac-plus.sh package): its tag, e.g. alpha-6. At startup
 * the game asks GitHub for update_repo's releases (settings.ini, default BwahFox/ac-plus, so a fork can point it
 * at its own) and, if one is newer and not the one the player skipped, shows its notes with Update now / Later /
 * Skip this version. A build without VERSION (from source) never asks.
 *
 * Only a release signed with the ac-plus release key counts: next to its zip it carries ZIP.sig, an ECDSA P-256
 * signature of the zip's SHA-256 (pc/tools/release-sign.py), checked against UPD_SIGNING_KEY with Windows' own
 * BCrypt before anything is unpacked. So a hijacked GitHub account or a forged release can't reach the players.
 * A fork builds its own exe with its own key.
 *
 * Updating downloads the release's zip with a progress bar, unpacks it in memory, checks every file's CRC and
 * stages it in update/new/. Only then are files replaced, the old ones going to update/previous/ (the last
 * update's are dropped first). Never touched: save/, cf_sky/, texture_pack/, music/, qr/, settings.ini and
 * keybindings.ini. A file that already exists is rewritten in place, so a Linux helper's executable bit
 * (ac-plus-inhibit, ac-sync.sh) survives Wine; the running exe can't be written and is renamed aside instead.
 * If anything fails, what was replaced is put back. The player then starts the game again; a release that needs
 * the City Folk files converted again does that on that start (pc_cf_setup.c).
 *
 * Dev: PC_UPDATE_API=<url> reads the release list from there instead of GitHub, PC_UPDATE_ANSWER=update|later|skip
 * answers the question (tests can't click), PC_UPDATE_VERSION=<tag> pretends to be that release. */
#include "pc_platform.h"
#include "pc_settings.h"
#include "pc_http.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif

extern SDL_Window* g_pc_window;

/* The public half of the ac-plus release key (pc/tools/release-sign.py keygen): P-256, X then Y */
static const unsigned char UPD_SIGNING_KEY[64] = {
    0xac, 0xbe, 0x75, 0x76, 0x09, 0x66, 0xe1, 0x0d, 0x41, 0x3a, 0x12, 0x8e, 0x64, 0x8d, 0xaa, 0xde,
    0x2e, 0x57, 0xf5, 0x49, 0x71, 0x9f, 0x34, 0x93, 0xee, 0x76, 0x0d, 0x16, 0xc2, 0x0f, 0xcb, 0xa5,
    0x39, 0x39, 0x5e, 0x29, 0x96, 0x6e, 0x23, 0xe9, 0xeb, 0x7b, 0x29, 0xf6, 0x9f, 0x1d, 0xfc, 0x6a,
    0x35, 0x50, 0xd4, 0x0e, 0xb2, 0x07, 0xa8, 0x44, 0x4e, 0x93, 0x6a, 0x58, 0x75, 0x88, 0x12, 0x81,
};

/* --- versions --- */

/* "9" or "9.1" (a fix release of 9): 900, 901 */
static long upd_point_rank(const char* n) {
    int major = atoi(n), minor = 0;
    const char* dot = n;

    while (isdigit((unsigned char)*dot)) {
        dot++;
    }
    if (*dot == '.' && isdigit((unsigned char)dot[1])) {
        minor = atoi(dot + 1);
    }
    return major * 100L + (minor < 100 ? minor : 99);
}

/* alpha-N < alpha-N.M < alpha-N+1 < beta-N < N.N.N (or vN.N.N); 0 = not a release name.
 * (Alpha 8 and older read alpha-N.M as alpha-N, so they're only offered whole numbers) */
static long upd_version_rank(const char* tag) {
    int a = 0, b = 0, c = 0;

    if (tag == NULL) {
        return 0;
    }
    if (strncmp(tag, "alpha-", 6) == 0 && isdigit((unsigned char)tag[6])) {
        return 1000000L + upd_point_rank(tag + 6);
    }
    if (strncmp(tag, "beta-", 5) == 0 && isdigit((unsigned char)tag[5])) {
        return 2000000L + upd_point_rank(tag + 5);
    }
    if (*tag == 'v') {
        tag++;
    }
    if (sscanf(tag, "%d.%d.%d", &a, &b, &c) >= 1 && isdigit((unsigned char)*tag)) {
        return 3000000L + a * 10000L + b * 100L + c;
    }
    return 0;
}

/* "alpha-6" -> "Alpha 6" for the player */
static void upd_pretty(char* out, size_t n, const char* tag) {
    if (strncmp(tag, "alpha-", 6) == 0) {
        snprintf(out, n, "Alpha %s", tag + 6);
    } else if (strncmp(tag, "beta-", 5) == 0) {
        snprintf(out, n, "Beta %s", tag + 5);
    } else {
        snprintf(out, n, "%s", tag);
    }
}

static int upd_read_version(char* out, size_t n) {
    const char* env = getenv("PC_UPDATE_VERSION");
    FILE* f;

    if (env != NULL) {
        snprintf(out, n, "%s", env);
        return out[0] != '\0';
    }
    f = fopen("VERSION", "r");
    out[0] = '\0';
    if (f == NULL) {
        return FALSE;
    }
    if (fgets(out, (int)n, f) == NULL) {
        out[0] = '\0';
    }
    fclose(f);
    out[strcspn(out, "\r\n \t")] = '\0';
    return out[0] != '\0';
}

/* This game's release (its VERSION file, e.g. alpha-9.1), "" in a dev build; online play compares it (pc_mp.c) */
void pc_updater_release(char* out, size_t n) {
    if (!upd_read_version(out, n)) {
        out[0] = '\0';
    }
}

/* What the title screen shows: "ac-plus Alpha 5", or "ac-plus dev build" without a VERSION file */
const char* pc_updater_version_label(void) {
    static char label[48];

    if (label[0] == '\0') {
        char tag[32], pretty[32];

        if (upd_read_version(tag, sizeof(tag))) {
            upd_pretty(pretty, sizeof(pretty), tag);
            snprintf(label, sizeof(label), "ac-plus %s", pretty);
        } else {
            snprintf(label, sizeof(label), "ac-plus dev build");
        }
    }
    return label;
}

#ifdef _WIN32

#define UPD_DIR "update"
#define UPD_NEW UPD_DIR "\\new"
#define UPD_PREV UPD_DIR "\\previous"
#define UPD_EXE "AnimalCrossing.exe"

/* stb_image's inflate (compiled in pc_pattern_qr.c) */
extern int stbi_zlib_decode_noheader_buffer(char* obuffer, int olen, const char* ibuffer, int ilen);

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} upd_buf_t;

static int upd_http_get(const char* url, upd_buf_t* out, int timeout_ms, pc_http_progress progress) {
    pc_http_resp r;
    int status = pc_http_request("GET", url, "Accept: application/vnd.github+json\r\n", NULL, 0, &r, timeout_ms,
                                 progress);

    memset(out, 0, sizeof(*out));
    if (status != 200) {
        fprintf(stderr, "[Update] %s: %s (HTTP %d)\n", url, status == 0 ? "no answer" : "refused", status);
        pc_http_free(&r);
        return FALSE;
    }
    out->data = r.body;
    out->len = out->cap = r.len;
    return TRUE;
}

/* --- the release list (just enough JSON for GitHub's answer) --- */

typedef struct {
    char tag[32];
    char zip_url[512];
    char sig_url[512]; /* ZIP.sig: without one the release isn't offered */
    char notes[1600];
    int draft;
} upd_release_t;

static const char* upd_ws(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    return p;
}

/* A JSON string at p (after its opening quote) into out (UTF-8, cut to n); returns past its closing quote */
static const char* upd_str(const char* p, char* out, size_t n) {
    size_t k = 0;

    while (*p != '\0' && *p != '"') {
        unsigned c = (unsigned char)*p++;

        if (c == '\\' && *p != '\0') {
            char e = *p++;

            if (e == 'n') {
                c = '\n';
            } else if (e == 't') {
                c = '\t';
            } else if (e == 'r') {
                c = '\r';
            } else if (e == 'u' && isxdigit((unsigned char)p[0]) && isxdigit((unsigned char)p[1]) &&
                       isxdigit((unsigned char)p[2]) && isxdigit((unsigned char)p[3])) {
                char hex[5] = { p[0], p[1], p[2], p[3], 0 };
                unsigned u = (unsigned)strtoul(hex, NULL, 16);

                p += 4;
                if (out != NULL && u >= 0x80) { /* to UTF-8 (the BMP is plenty for release notes) */
                    if (u < 0x800 && k + 2 < n) {
                        out[k++] = (char)(0xC0 | (u >> 6));
                        out[k++] = (char)(0x80 | (u & 0x3F));
                    } else if (u >= 0x800 && k + 3 < n) {
                        out[k++] = (char)(0xE0 | (u >> 12));
                        out[k++] = (char)(0x80 | ((u >> 6) & 0x3F));
                        out[k++] = (char)(0x80 | (u & 0x3F));
                    }
                    continue;
                }
                c = u;
            } else {
                c = (unsigned char)e; /* \" \\ \/ */
            }
        }
        if (out != NULL && k + 1 < n) {
            out[k++] = (char)c;
        }
    }
    if (out != NULL && n > 0) {
        out[k] = '\0';
    }
    return *p == '"' ? p + 1 : p;
}

static const char* upd_skip_value(const char* p) {
    p = upd_ws(p);
    if (*p == '"') {
        return upd_str(p + 1, NULL, 0);
    }
    if (*p == '{' || *p == '[') {
        int depth = 0;

        while (*p != '\0') {
            if (*p == '"') {
                p = upd_str(p + 1, NULL, 0);
                continue;
            }
            if (*p == '{' || *p == '[') {
                depth++;
            } else if (*p == '}' || *p == ']') {
                if (--depth == 0) {
                    return p + 1;
                }
            }
            p++;
        }
        return p;
    }
    while (*p != '\0' && *p != ',' && *p != '}' && *p != ']') {
        p++;
    }
    return p;
}

/* One asset object: its browser_download_url, if it's a .zip */
static const char* upd_asset(const char* p, upd_release_t* r) {
    p = upd_ws(p);
    if (*p != '{') {
        return upd_skip_value(p);
    }
    p++;
    for (;;) {
        char key[64];

        p = upd_ws(p);
        if (*p == '}') {
            return p + 1;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p != '"') {
            return p;
        }
        p = upd_ws(upd_str(p + 1, key, sizeof(key)));
        if (*p == ':') {
            p++;
        }
        p = upd_ws(p);
        if (strcmp(key, "browser_download_url") == 0 && *p == '"') {
            char url[512];
            size_t l;

            p = upd_str(p + 1, url, sizeof(url));
            l = strlen(url);
            if (l > 4 && strcmp(url + l - 4, ".zip") == 0 && r->zip_url[0] == '\0') {
                snprintf(r->zip_url, sizeof(r->zip_url), "%s", url);
            } else if (l > 8 && strcmp(url + l - 8, ".zip.sig") == 0 && r->sig_url[0] == '\0') {
                snprintf(r->sig_url, sizeof(r->sig_url), "%s", url);
            }
        } else {
            p = upd_skip_value(p);
        }
    }
}

static const char* upd_release(const char* p, upd_release_t* r) {
    memset(r, 0, sizeof(*r));
    p = upd_ws(p);
    if (*p != '{') {
        return upd_skip_value(p);
    }
    p++;
    for (;;) {
        char key[64];

        p = upd_ws(p);
        if (*p == '}') {
            return p + 1;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p != '"') {
            return p;
        }
        p = upd_ws(upd_str(p + 1, key, sizeof(key)));
        if (*p == ':') {
            p++;
        }
        p = upd_ws(p);
        if (strcmp(key, "tag_name") == 0 && *p == '"') {
            p = upd_str(p + 1, r->tag, sizeof(r->tag));
        } else if (strcmp(key, "body") == 0 && *p == '"') {
            p = upd_str(p + 1, r->notes, sizeof(r->notes));
        } else if (strcmp(key, "draft") == 0) {
            r->draft = strncmp(p, "true", 4) == 0;
            p = upd_skip_value(p);
        } else if (strcmp(key, "assets") == 0 && *p == '[') {
            p++;
            for (;;) {
                p = upd_ws(p);
                if (*p == ']') {
                    p++;
                    break;
                }
                if (*p == ',') {
                    p++;
                    continue;
                }
                if (*p == '\0') {
                    break;
                }
                p = upd_asset(p, r);
            }
        } else {
            p = upd_skip_value(p);
        }
    }
}

/* The newest release in the list that has a zip, newer than `current`; FALSE if none */
static int upd_newest(const char* json, long current, upd_release_t* best) {
    const char* p = upd_ws(json);
    long best_rank = current;
    int found = FALSE;

    if (*p != '[') {
        return FALSE;
    }
    p++;
    for (;;) {
        upd_release_t r;

        p = upd_ws(p);
        if (*p == ']' || *p == '\0') {
            break;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        p = upd_release(p, &r);
        if (!r.draft && r.zip_url[0] != '\0' && r.sig_url[0] != '\0' && upd_version_rank(r.tag) > best_rank) {
            best_rank = upd_version_rank(r.tag);
            *best = r;
            found = TRUE;
        }
    }
    return found;
}

/* Release notes for a message box: no markdown emphasis, not too long */
static void upd_notes_plain(char* out, size_t n, const char* md) {
    size_t k = 0;

    for (; *md != '\0' && k + 1 < n; md++) {
        if (*md == '*' || *md == '`' || *md == '\r') {
            continue;
        }
        out[k++] = *md;
    }
    out[k] = '\0';
    if (k > 700) { /* cut at a line */
        char* nl = strrchr(out, '\n');

        while (nl != NULL && nl - out > 700) {
            *nl = '\0';
            nl = strrchr(out, '\n');
        }
        if (strlen(out) > 700) {
            out[700] = '\0';
        }
        strncat(out, "\n...", n - strlen(out) - 1);
    }
}

/* --- progress --- */

static void upd_draw(int pct, const char* what) {
    int w = 0, h = 0, bw, bh, x, y;
    char title[128];

    SDL_GL_GetDrawableSize(g_pc_window, &w, &h);
    bw = w * 3 / 5, bh = h / 30 > 6 ? h / 30 : 6;
    x = (w - bw) / 2, y = (h - bh) / 2;
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, w, h);
    glClearColor(0.10f, 0.13f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x - 2, y - 2, bw + 4, bh + 4);
    glClearColor(0.85f, 0.80f, 0.62f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(x, y, bw, bh);
    glClearColor(0.18f, 0.22f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(x, y, bw * pct / 100, bh);
    glClearColor(0.45f, 0.75f, 0.35f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    SDL_GL_SwapWindow(g_pc_window);
    snprintf(title, sizeof(title), "ac-plus - %s: %d%%", what, pct);
    SDL_SetWindowTitle(g_pc_window, title);
}

static char s_upd_what[64];
static Uint32 s_upd_last_draw;

static void upd_download_progress(size_t got, size_t total) {
    SDL_Event ev;
    Uint32 now = SDL_GetTicks();

    while (SDL_PollEvent(&ev)) {
        /* keep the window answering */
    }
    if (now - s_upd_last_draw >= 50 || (total != 0 && got >= total)) {
        s_upd_last_draw = now;
        upd_draw(total != 0 ? (int)(got * 100 / total) : 0, s_upd_what);
    }
}

/* --- files --- */

static unsigned long upd_crc32(const unsigned char* p, size_t n) {
    static unsigned long table[256];
    unsigned long c = 0xFFFFFFFFUL;
    size_t i;

    if (table[1] == 0) {
        unsigned long k;
        int j;

        for (k = 0; k < 256; k++) {
            unsigned long v = k;

            for (j = 0; j < 8; j++) {
                v = (v & 1) ? 0xEDB88320UL ^ (v >> 1) : v >> 1;
            }
            table[k] = v;
        }
    }
    for (i = 0; i < n; i++) {
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFUL;
}

/* Every folder on the way to path (a file path, \ separated) */
static void upd_make_dirs(const char* path) {
    char tmp[MAX_PATH];
    char* s;

    snprintf(tmp, sizeof(tmp), "%s", path);
    for (s = tmp; *s != '\0'; s++) {
        if (*s == '\\') {
            *s = '\0';
            CreateDirectoryA(tmp, NULL);
            *s = '\\';
        }
    }
}

static void upd_remove_tree(const char* dir) {
    char pat[MAX_PATH], sub[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;

    snprintf(pat, sizeof(pat), "%s\\*", dir);
    h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
                continue;
            }
            snprintf(sub, sizeof(sub), "%s\\%s", dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                upd_remove_tree(sub);
            } else {
                SetFileAttributesA(sub, FILE_ATTRIBUTE_NORMAL);
                DeleteFileA(sub);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(dir);
}

static int upd_write_file(const char* path, const char* data, size_t len, DWORD disposition) {
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, disposition, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD put = 0;
    int ok;

    if (h == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    ok = WriteFile(h, data, (DWORD)len, &put, NULL) && put == len;
    ok = CloseHandle(h) && ok;
    return ok;
}

static int upd_read_file(const char* path, upd_buf_t* out) {
    FILE* f = fopen(path, "rb");
    long n;

    memset(out, 0, sizeof(*out));
    if (f == NULL) {
        return FALSE;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out->data = malloc((size_t)n + 1);
    if (out->data == NULL || fread(out->data, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(out->data);
        memset(out, 0, sizeof(*out));
        return FALSE;
    }
    fclose(f);
    out->len = out->cap = (size_t)n;
    return TRUE;
}

/* Files the update must never touch: the player's own */
static int upd_protected(const char* rel) {
    static const char* dirs[] = { "save\\", "cf_sky\\", "texture_pack\\", "music\\", "qr\\", UPD_DIR "\\" };
    size_t i;

    for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        if (_strnicmp(rel, dirs[i], strlen(dirs[i])) == 0) {
            return TRUE;
        }
    }
    return _stricmp(rel, "settings.ini") == 0 || _stricmp(rel, "keybindings.ini") == 0 ||
           _stricmp(rel, "ac-sync.conf") == 0;
}

#define UPD_MAX_FILES 4096

typedef struct {
    char rel[MAX_PATH];
    int existed;
    int applied;
} upd_file_t;

static u16 upd_le16(const unsigned char* p) {
    return (u16)(p[0] | (p[1] << 8));
}
static u32 upd_le32(const unsigned char* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* Unpacks the zip's ac-plus/ files into update\new\, checking each; fills files, returns their count, -1 on error.
 * want_tag: the zip's VERSION must say it. */
static int upd_unpack(const upd_buf_t* zip, upd_file_t* files, const char* want_tag, char* err, size_t err_n) {
    const unsigned char* z = (const unsigned char*)zip->data;
    long eocd = -1, i;
    u32 cd_off, cd_n, k, pos;
    int count = 0, have_exe = FALSE, have_version = FALSE;

    for (i = (long)zip->len - 22; i >= 0 && i >= (long)zip->len - 22 - 65535; i--) {
        if (upd_le32(z + i) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        snprintf(err, err_n, "the download isn't a zip");
        return -1;
    }
    cd_n = upd_le16(z + eocd + 10);
    cd_off = upd_le32(z + eocd + 16);
    upd_remove_tree(UPD_NEW);
    for (k = 0, pos = cd_off; k < cd_n; k++) {
        u16 method, name_len, extra_len, comment_len;
        u32 crc, csize, usize, loff, data;
        char name[MAX_PATH], rel[MAX_PATH], dst[MAX_PATH];
        char* out;
        size_t j;

        if (pos + 46 > zip->len || upd_le32(z + pos) != 0x02014b50) {
            snprintf(err, err_n, "the zip is damaged");
            return -1;
        }
        method = upd_le16(z + pos + 10);
        crc = upd_le32(z + pos + 16);
        csize = upd_le32(z + pos + 20);
        usize = upd_le32(z + pos + 24);
        name_len = upd_le16(z + pos + 28);
        extra_len = upd_le16(z + pos + 30);
        comment_len = upd_le16(z + pos + 32);
        loff = upd_le32(z + pos + 42);
        if (name_len >= sizeof(name) || pos + 46 + name_len > zip->len) {
            snprintf(err, err_n, "the zip is damaged");
            return -1;
        }
        memcpy(name, z + pos + 46, name_len);
        name[name_len] = '\0';
        pos += 46 + name_len + extra_len + comment_len;
        if (strncmp(name, "ac-plus/", 8) != 0 || name[name_len - 1] == '/') {
            continue; /* folders, and anything outside the game folder */
        }
        snprintf(rel, sizeof(rel), "%s", name + 8);
        for (j = 0; rel[j] != '\0'; j++) {
            if (rel[j] == '/') {
                rel[j] = '\\';
            }
        }
        if (strstr(rel, "..") != NULL || rel[0] == '\\' || strchr(rel, ':') != NULL) {
            snprintf(err, err_n, "the zip has a file outside the game folder (%s)", name);
            return -1;
        }
        if (upd_protected(rel)) {
            continue;
        }
        if (count == UPD_MAX_FILES) {
            snprintf(err, err_n, "the zip has too many files");
            return -1;
        }
        if (loff + 30 > zip->len || upd_le32(z + loff) != 0x04034b50) {
            snprintf(err, err_n, "the zip is damaged (%s)", name);
            return -1;
        }
        data = loff + 30 + upd_le16(z + loff + 26) + upd_le16(z + loff + 28);
        if (data + csize > zip->len) {
            snprintf(err, err_n, "the zip is cut short (%s)", name);
            return -1;
        }
        out = malloc(usize ? usize : 1);
        if (out == NULL) {
            snprintf(err, err_n, "out of memory unpacking %s", name);
            return -1;
        }
        if (method == 0 && csize == usize) {
            memcpy(out, z + data, usize);
        } else if (method != 8 ||
                   stbi_zlib_decode_noheader_buffer(out, (int)usize, (const char*)z + data, (int)csize) != (int)usize) {
            free(out);
            snprintf(err, err_n, "couldn't unpack %s", name);
            return -1;
        }
        if (upd_crc32((const unsigned char*)out, usize) != crc) {
            free(out);
            snprintf(err, err_n, "%s is damaged in the download", name);
            return -1;
        }
        if (_stricmp(rel, "VERSION") == 0) {
            char tag[32];

            snprintf(tag, sizeof(tag), "%.*s", (int)(usize < 31 ? usize : 31), out);
            tag[strcspn(tag, "\r\n \t")] = '\0';
            have_version = strcmp(tag, want_tag) == 0;
        }
        have_exe |= _stricmp(rel, UPD_EXE) == 0;
        snprintf(dst, sizeof(dst), UPD_NEW "\\%s", rel);
        upd_make_dirs(dst);
        if (!upd_write_file(dst, out, usize, CREATE_ALWAYS)) {
            free(out);
            snprintf(err, err_n, "couldn't write %s", dst);
            return -1;
        }
        free(out);
        snprintf(files[count].rel, sizeof(files[count].rel), "%s", rel);
        files[count].existed = files[count].applied = FALSE;
        count++;
    }
    if (!have_exe || !have_version) {
        snprintf(err, err_n, "the download isn't the release it should be (%s)", !have_exe ? "no game" : "wrong VERSION");
        return -1;
    }
    return count;
}

/* Puts back what was replaced (newest first) */
static void upd_roll_back(upd_file_t* files, int count) {
    int i;

    for (i = count - 1; i >= 0; i--) {
        char prev[MAX_PATH];

        if (!files[i].applied) {
            continue;
        }
        snprintf(prev, sizeof(prev), UPD_PREV "\\%s", files[i].rel);
        if (!files[i].existed) {
            DeleteFileA(files[i].rel);
        } else if (_stricmp(files[i].rel, UPD_EXE) == 0) {
            DeleteFileA(UPD_EXE);
            MoveFileExA(prev, UPD_EXE, MOVEFILE_REPLACE_EXISTING);
        } else {
            upd_buf_t b;

            if (upd_read_file(prev, &b)) {
                upd_write_file(files[i].rel, b.data, b.len, TRUNCATE_EXISTING);
                free(b.data);
            }
        }
    }
}

/* Replaces the game's files with the staged ones, keeping the old ones in update\previous\ */
static int upd_apply(upd_file_t* files, int count, char* err, size_t err_n) {
    int i;

    upd_remove_tree(UPD_PREV);
    for (i = 0; i < count; i++) {
        char staged[MAX_PATH], prev[MAX_PATH];
        const char* rel = files[i].rel;
        DWORD attr = GetFileAttributesA(rel);

        snprintf(staged, sizeof(staged), UPD_NEW "\\%s", rel);
        snprintf(prev, sizeof(prev), UPD_PREV "\\%s", rel);
        files[i].existed = attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
        upd_make_dirs(rel);
        if (!files[i].existed) {
            if (!MoveFileExA(staged, rel, MOVEFILE_REPLACE_EXISTING)) {
                snprintf(err, err_n, "couldn't add %s", rel);
                return FALSE;
            }
        } else if (_stricmp(rel, UPD_EXE) == 0) {
            /* running: it can be renamed, not written */
            upd_make_dirs(prev);
            if (!MoveFileExA(UPD_EXE, prev, MOVEFILE_REPLACE_EXISTING)) {
                snprintf(err, err_n, "couldn't move the running game aside");
                return FALSE;
            }
            if (!MoveFileExA(staged, UPD_EXE, MOVEFILE_REPLACE_EXISTING)) {
                MoveFileExA(prev, UPD_EXE, MOVEFILE_REPLACE_EXISTING);
                snprintf(err, err_n, "couldn't put the new game in place");
                return FALSE;
            }
        } else {
            /* rewritten in place, so it keeps its permissions (a Linux helper's executable bit under Wine) */
            upd_buf_t b;
            int ok;

            upd_make_dirs(prev);
            if (!CopyFileA(rel, prev, FALSE) || !upd_read_file(staged, &b)) {
                snprintf(err, err_n, "couldn't back up %s", rel);
                return FALSE;
            }
            ok = upd_write_file(rel, b.data, b.len, TRUNCATE_EXISTING);
            free(b.data);
            if (!ok) {
                files[i].applied = TRUE; /* it may be half written: put back */
                snprintf(err, err_n, "couldn't write %s", rel);
                return FALSE;
            }
        }
        files[i].applied = TRUE;
    }
    return TRUE;
}

/* --- the release signature --- */

/* TRUE if sig_url holds a good signature of the zip by UPD_SIGNING_KEY */
static int upd_signed(const upd_buf_t* zip, const char* sig_url) {
    struct {
        BCRYPT_ECCKEY_BLOB head;
        unsigned char xy[64];
    } blob;
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    unsigned char hash[32];
    char hex[65];
    upd_buf_t sig;
    NTSTATUS st = -1;
    int i;

    if (!upd_http_get(sig_url, &sig, 15000, NULL)) {
        fprintf(stderr, "[Update] couldn't download the signature\n");
        return FALSE;
    }
    if (sig.len != 64) {
        fprintf(stderr, "[Update] the signature is %u bytes, not 64\n", (unsigned)sig.len);
        free(sig.data);
        return FALSE;
    }
    pc_sha256_hex(zip->data, zip->len, hex);
    for (i = 0; i < 32; i++) {
        unsigned v;

        sscanf(hex + 2 * i, "%2x", &v);
        hash[i] = (unsigned char)v;
    }
    blob.head.dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    blob.head.cbKey = 32;
    memcpy(blob.xy, UPD_SIGNING_KEY, sizeof(blob.xy));
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, NULL, 0) == 0 &&
        BCryptImportKeyPair(alg, NULL, BCRYPT_ECCPUBLIC_BLOB, &key, (PUCHAR)&blob, sizeof(blob), 0) == 0) {
        st = BCryptVerifySignature(key, NULL, hash, sizeof(hash), (PUCHAR)sig.data, 64, 0);
    }
    if (key != NULL) {
        BCryptDestroyKey(key);
    }
    if (alg != NULL) {
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    free(sig.data);
    fprintf(stderr, "[Update] signature %s (0x%lx)\n", st == 0 ? "good" : "NOT good", (unsigned long)st);
    return st == 0;
}

/* --- the whole thing --- */

static void upd_message(Uint32 flags, const char* title, const char* msg) {
    fprintf(stderr, "[Update] %s\n", msg);
    if (getenv("PC_UPDATE_ANSWER") == NULL) {
        SDL_ShowSimpleMessageBox(flags, title, msg, g_pc_window);
    }
}

enum { UPD_ANSWER_UPDATE, UPD_ANSWER_LATER, UPD_ANSWER_SKIP };

static int upd_ask(const char* mine, const upd_release_t* r) {
    const char* env = getenv("PC_UPDATE_ANSWER");
    static const SDL_MessageBoxButtonData buttons[] = {
        { 0, UPD_ANSWER_SKIP, "Skip this version" },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, UPD_ANSWER_LATER, "Later" },
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, UPD_ANSWER_UPDATE, "Update now" },
    };
    SDL_MessageBoxData box;
    char theirs[32], have[32], notes[1000], msg[1600];
    int hit = UPD_ANSWER_LATER;

    upd_pretty(theirs, sizeof(theirs), r->tag);
    upd_pretty(have, sizeof(have), mine);
    upd_notes_plain(notes, sizeof(notes), r->notes);
    snprintf(msg, sizeof(msg),
             "ac-plus %s is out (you have %s).\n\n%s\n\n"
             "Updating keeps your saves, settings, music and texture packs.",
             theirs, have, notes);
    fprintf(stderr, "[Update] offering %s (have %s)\n", r->tag, mine);
    if (env != NULL) {
        return strcmp(env, "update") == 0 ? UPD_ANSWER_UPDATE
               : strcmp(env, "skip") == 0 ? UPD_ANSWER_SKIP
                                          : UPD_ANSWER_LATER;
    }
    memset(&box, 0, sizeof(box));
    box.flags = SDL_MESSAGEBOX_INFORMATION;
    box.window = g_pc_window;
    box.title = "ac-plus - update available";
    box.message = msg;
    box.numbuttons = 3;
    box.buttons = buttons;
    if (SDL_ShowMessageBox(&box, &hit) != 0) {
        hit = UPD_ANSWER_LATER;
    }
    return hit;
}

int pc_updater_run(void) {
    char mine[32], url[512], err[512], pretty[32], msg[768];
    const char* api = getenv("PC_UPDATE_API");
    upd_buf_t list, zip;
    upd_release_t r;
    upd_file_t* files;
    int count;

    if (!g_pc_settings.update_check || !upd_read_version(mine, sizeof(mine)) || upd_version_rank(mine) == 0) {
        return FALSE; /* turned off, or a build from source */
    }
    if (api != NULL) {
        snprintf(url, sizeof(url), "%s", api);
    } else {
        snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases?per_page=30", g_pc_settings.update_repo);
    }
    if (!upd_http_get(url, &list, 4000, NULL)) {
        fprintf(stderr, "[Update] couldn't check for updates (offline?)\n");
        return FALSE;
    }
    count = upd_newest(list.data, upd_version_rank(mine), &r);
    free(list.data);
    if (!count) {
        fprintf(stderr, "[Update] %s is the newest\n", mine);
        return FALSE;
    }
    if (strcmp(r.tag, g_pc_settings.update_skip) == 0) {
        fprintf(stderr, "[Update] %s is out, skipped\n", r.tag);
        return FALSE;
    }
    switch (upd_ask(mine, &r)) {
        case UPD_ANSWER_SKIP:
            snprintf(g_pc_settings.update_skip, sizeof(g_pc_settings.update_skip), "%s", r.tag);
            pc_settings_save();
            return FALSE;
        case UPD_ANSWER_LATER:
            return FALSE;
        default:
            break;
    }

    upd_pretty(pretty, sizeof(pretty), r.tag);
    snprintf(s_upd_what, sizeof(s_upd_what), "downloading %s", pretty);
    upd_draw(0, s_upd_what);
    if (!upd_http_get(r.zip_url, &zip, 30000, &upd_download_progress)) {
        SDL_SetWindowTitle(g_pc_window, "Animal Crossing Plus");
        upd_message(SDL_MESSAGEBOX_WARNING, "ac-plus - update",
                    "The download didn't finish, so nothing was changed.\n\nThe game will start as it is; "
                    "it'll offer the update again next time.");
        return FALSE;
    }
    if (!upd_signed(&zip, r.sig_url)) {
        free(zip.data);
        SDL_SetWindowTitle(g_pc_window, "Animal Crossing Plus");
        upd_message(SDL_MESSAGEBOX_WARNING, "ac-plus - update",
                    "This update isn't signed with the ac-plus release key, so it wasn't installed and nothing was "
                    "changed.\n\nIt may have been damaged on the way, or not come from ac-plus at all. The game "
                    "will start as it is.");
        return FALSE;
    }
    upd_draw(100, "installing");
    files = calloc(UPD_MAX_FILES, sizeof(upd_file_t));
    count = files != NULL ? upd_unpack(&zip, files, r.tag, err, sizeof(err)) : -1;
    free(zip.data);
    if (count < 0) {
        upd_remove_tree(UPD_NEW);
        free(files);
        SDL_SetWindowTitle(g_pc_window, "Animal Crossing Plus");
        snprintf(msg, sizeof(msg), "The update couldn't be installed: %s.\n\nNothing was changed; the game will start "
                                   "as it is.", files != NULL ? err : "out of memory");
        upd_message(SDL_MESSAGEBOX_WARNING, "ac-plus - update", msg);
        return FALSE;
    }
    if (!upd_apply(files, count, err, sizeof(err))) {
        upd_roll_back(files, count);
        upd_remove_tree(UPD_NEW);
        free(files);
        SDL_SetWindowTitle(g_pc_window, "Animal Crossing Plus");
        snprintf(msg, sizeof(msg), "The update couldn't be installed: %s.\n\nEverything was put back as it was; the "
                                   "game will start as it is.", err);
        upd_message(SDL_MESSAGEBOX_WARNING, "ac-plus - update", msg);
        return FALSE;
    }
    upd_remove_tree(UPD_NEW);
    free(files);
    fprintf(stderr, "[Update] installed %s (%d files; the old ones are in " UPD_PREV ")\n", r.tag, count);
    snprintf(msg, sizeof(msg), "ac-plus is now %s!\n\nStart the game again to play.", pretty);
    upd_message(SDL_MESSAGEBOX_INFORMATION, "ac-plus - updated", msg);
    return TRUE;
}

#else

int pc_updater_run(void) {
    return FALSE;
}

#endif
