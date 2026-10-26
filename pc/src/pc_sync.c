/* pc_sync.c - the town save kept in sync through an ac-plus-server (server/ac-plus-server.py), by the game itself.
 *
 * With sync_enabled = 1, sync_url and sync_token (settings.ini), the game:
 *   - at startup, before the town loads, asks the server for its save's info and compares both sides with what
 *     they were at the last sync (save/card_a/sync-state.txt: the server's rev, and a hash of the local files):
 *     only the server's changed -> download it; only this machine's -> upload it; both -> ask which to keep;
 *   - after every save (gyroid, Z+Start, autosave, save on quit), uploads in the background and says
 *     "Save Upload Complete" when the server has it, so the player knows it's safe to close;
 *   - on quitting, waits for an upload still on its way (a few seconds at most).
 * Steam's "Exit Game" kills the game outright, so nothing is left for the end: every save is already up.
 *
 * What's synced: every .gci in save/card_a with its .acplus (the town and travel passports), as ac-sync.sh does,
 * sent together as one bundle ("ACSB", u32 1, u32 count, then per file u16 name length, name, u32 size, data), so
 * the server never holds a .gci from one save and an .acplus from another. A download first copies what it
 * replaces to save/sync-backups/<time>-before-download/; the server keeps its own history. */
#include "pc_platform.h"
#include "pc_settings.h"
#include "pc_http.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define SYNC_DIR "save/card_a"
#define SYNC_STATE SYNC_DIR "/sync-state.txt"
#define SYNC_MAX_FILES 64
#define SYNC_TIMEOUT_MS 6000

extern SDL_Window* g_pc_window;
void pc_quick_save_show_message(const char* msg);

typedef struct {
    char* data;
    size_t len;
} sync_buf_t;

typedef struct {
    long rev;
    char hash[65];
} sync_state_t;

typedef struct {
    long rev;
    long saved_at;
    char device[64];
    char sha256[65];
} sync_info_t;

static SDL_Thread* s_thread;
static SDL_mutex* s_lock;
static SDL_cond* s_wake;
static sync_buf_t s_pending;   /* the next bundle to upload (NULL: none) */
static int s_pending_force;    /* upload it regardless of what the server has (the player chose this machine's) */
static int s_busy;             /* an upload is on its way */
static const char* s_message;  /* for the screen, picked up by pc_sync_poll */
static int s_off;              /* sync stood aside for this session (too many files, or the player hasn't chosen) */
static int s_undecided;        /* ...because the player hasn't chosen between two saves: said again at every save */

static int sync_enabled(void) {
    return !s_off && g_pc_settings.sync_enabled && g_pc_settings.sync_url[0] != '\0' && g_pc_settings.sync_token[0] != '\0';
}

static void sync_set_message(const char* msg) {
    SDL_LockMutex(s_lock);
    s_message = msg;
    SDL_UnlockMutex(s_lock);
}

/* --- the local files --- */

static int sync_name_cmp(const void* a, const void* b) {
    return strcmp((const char*)a, (const char*)b);
}

static int sync_wanted(const char* name) {
    size_t n = strlen(name);

    return (n > 4 && strcmp(name + n - 4, ".gci") == 0) || (n > 11 && strcmp(name + n - 11, ".gci.acplus") == 0);
}

/* The save files, sorted (the bundle and its hash don't depend on the folder's order) */
static int sync_list(char names[SYNC_MAX_FILES][128]) {
    DIR* d = opendir(SYNC_DIR);
    struct dirent* e;
    int n = 0;

    if (d == NULL) {
        return 0;
    }
    while ((e = readdir(d)) != NULL) {
        if (sync_wanted(e->d_name) && strlen(e->d_name) < 128) {
            if (n == SYNC_MAX_FILES) {
                /* only some of them would go: a .gci without its .acplus, say */
                printf("[sync] more than %d save files in " SYNC_DIR "; sync is off until there are fewer\n",
                       SYNC_MAX_FILES);
                s_off = TRUE;
                break;
            }
            snprintf(names[n++], 128, "%s", e->d_name);
        }
    }
    closedir(d);
    qsort(names, (size_t)n, 128, sync_name_cmp);
    return n;
}

static int sync_read(const char* path, sync_buf_t* out) {
    FILE* f = fopen(path, "rb");
    long n;

    out->data = NULL, out->len = 0;
    if (f == NULL) {
        return FALSE;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out->data = malloc(n > 0 ? (size_t)n : 1);
    if (out->data == NULL || n < 0 || fread(out->data, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(out->data);
        out->data = NULL;
        return FALSE;
    }
    fclose(f);
    out->len = (size_t)n;
    return TRUE;
}

static void sync_put(sync_buf_t* b, const void* p, size_t n) {
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

/* The bundle of the local save files; FALSE (and empty) if there are none */
static int sync_bundle(sync_buf_t* out) {
    char names[SYNC_MAX_FILES][128];
    sync_buf_t files[SYNC_MAX_FILES];
    int n = sync_list(names), i, have = 0;
    size_t total = 12;
    unsigned u32;

    out->data = NULL, out->len = 0;
    for (i = 0; i < n; i++) {
        char path[192];

        snprintf(path, sizeof(path), SYNC_DIR "/%s", names[i]);
        if (!sync_read(path, &files[i])) {
            files[i].data = NULL, files[i].len = 0;
            names[i][0] = '\0';
            continue;
        }
        total += 2 + strlen(names[i]) + 4 + files[i].len;
        have++;
    }
    if (have == 0) {
        return FALSE;
    }
    out->data = malloc(total);
    if (out->data == NULL) {
        for (i = 0; i < n; i++) {
            free(files[i].data);
        }
        return FALSE;
    }
    sync_put(out, "ACSB", 4);
    u32 = 1;
    sync_put(out, &u32, 4);
    u32 = (unsigned)have;
    sync_put(out, &u32, 4);
    for (i = 0; i < n; i++) {
        unsigned short nl;

        if (names[i][0] == '\0') {
            continue;
        }
        nl = (unsigned short)strlen(names[i]);
        sync_put(out, &nl, 2);
        sync_put(out, names[i], nl);
        u32 = (unsigned)files[i].len;
        sync_put(out, &u32, 4);
        sync_put(out, files[i].data, files[i].len);
        free(files[i].data);
    }
    return TRUE;
}

static void sync_local_hash(char hash[65]) {
    sync_buf_t b;

    if (sync_bundle(&b)) {
        pc_sha256_hex(b.data, b.len, hash);
        free(b.data);
    } else {
        snprintf(hash, 65, "none");
    }
}

/* --- state --- */

static int sync_replace(const char* from, const char* to) {
#ifdef _WIN32
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(from, to) == 0;
#endif
}

/* Which server and town the state is about: another server, or another token, starts over as never synced */
static void sync_server_id(char id[65]) {
    char buf[384];

    snprintf(buf, sizeof(buf), "%s\n%s", g_pc_settings.sync_url, g_pc_settings.sync_token);
    pc_sha256_hex(buf, strlen(buf), id);
}

static void sync_state_read(sync_state_t* st) {
    FILE* f = fopen(SYNC_STATE, "r");
    char server[65], want[65];

    st->rev = -1; /* never synced */
    snprintf(st->hash, sizeof(st->hash), "none");
    if (f == NULL) {
        return;
    }
    sync_server_id(want);
    if (fscanf(f, "rev %ld\nhash %64s\nserver %64s", &st->rev, st->hash, server) != 3 || strcmp(server, want) != 0) {
        st->rev = -1;
        snprintf(st->hash, sizeof(st->hash), "none");
    }
    fclose(f);
}

static void sync_state_write(long rev, const char* hash) {
    FILE* f = fopen(SYNC_STATE ".tmp", "w");
    char server[65];

    if (f == NULL) {
        return;
    }
    sync_server_id(server);
    fprintf(f, "rev %ld\nhash %s\nserver %s\n", rev, hash, server);
    fclose(f);
    sync_replace(SYNC_STATE ".tmp", SYNC_STATE);
}

/* --- the server --- */

static void sync_url(char* out, size_t n, const char* path) {
    size_t l;

    /* typed in the settings menu as just host:port: plain HTTP (a server at home) */
    snprintf(out, n, "%s%s", strstr(g_pc_settings.sync_url, "://") != NULL ? "" : "http://", g_pc_settings.sync_url);
    l = strlen(out);
    while (l > 0 && out[l - 1] == '/') {
        out[--l] = '\0';
    }
    snprintf(out + l, n - l, "%s", path);
}

static void sync_device(char* out, size_t n) {
#ifdef _WIN32
    DWORD len = (DWORD)n;

    if (GetComputerNameA(out, &len)) {
        return;
    }
#endif
    snprintf(out, n, "unknown");
}

static long sync_json_long(const char* json, const char* key) {
    char k[40];
    const char* p;

    snprintf(k, sizeof(k), "\"%s\":", key);
    p = json != NULL ? strstr(json, k) : NULL;
    return p != NULL ? strtol(p + strlen(k), NULL, 10) : 0;
}

static void sync_json_str(const char* json, const char* key, char* out, size_t n) {
    char k[40];
    const char* p;
    size_t i = 0;

    out[0] = '\0';
    snprintf(k, sizeof(k), "\"%s\":", key);
    p = json != NULL ? strstr(json, k) : NULL;
    if (p == NULL || (p = strchr(p + strlen(k), '"')) == NULL) {
        return;
    }
    for (p++; *p != '\0' && *p != '"' && i + 1 < n; p++) {
        out[i++] = *p == '\\' && p[1] != '\0' ? *++p : *p;
    }
    out[i] = '\0';
}

static void sync_info_parse(const char* json, sync_info_t* info) {
    info->rev = sync_json_long(json, "rev");
    info->saved_at = sync_json_long(json, "saved_at");
    sync_json_str(json, "device", info->device, sizeof(info->device));
    sync_json_str(json, "sha256", info->sha256, sizeof(info->sha256));
}

static void sync_auth(char* out, size_t n, const char* extra) {
    snprintf(out, n, "Authorization: Bearer %s\r\n%s", g_pc_settings.sync_token, extra != NULL ? extra : "");
}

/* PUT the bundle: the HTTP status, the server's info in info */
static int sync_upload(const sync_buf_t* b, long base_rev, int force, sync_info_t* info) {
    char url[384], headers[512], extra[256], hash[65], device[64];
    pc_http_resp r;
    int status;

    pc_sha256_hex(b->data, b->len, hash);
    sync_device(device, sizeof(device));
    snprintf(extra, sizeof(extra), "X-Base-Rev: %ld\r\nX-Sha256: %s\r\nX-Device: %s\r\nX-Force: %d\r\n", base_rev,
             hash, device, force ? 1 : 0);
    sync_auth(headers, sizeof(headers), extra);
    sync_url(url, sizeof(url), "/v1/save");
    status = pc_http_request("PUT", url, headers, b->data, b->len, &r, SYNC_TIMEOUT_MS * 3, NULL);
    sync_info_parse(r.body, info);
    pc_http_free(&r);
    return status;
}

/* --- uploading, in the background --- */

static int sync_thread(void* unused) {
    (void)unused;
    for (;;) {
        sync_buf_t b;
        int force;
        sync_state_t st;
        sync_info_t info;
        int status;
        char hash[65];

        SDL_LockMutex(s_lock);
        while (s_pending.data == NULL) {
            s_busy = FALSE;
            SDL_CondWait(s_wake, s_lock);
        }
        b = s_pending;
        force = s_pending_force;
        s_pending.data = NULL, s_pending.len = 0;
        s_pending_force = FALSE;
        s_busy = TRUE;
        SDL_UnlockMutex(s_lock);

        sync_state_read(&st);
        memset(&info, 0, sizeof(info));
        status = sync_upload(&b, st.rev < 0 ? 0 : st.rev, force, &info);
        pc_sha256_hex(b.data, b.len, hash);
        free(b.data);
        if (status == 200) {
            sync_state_write(info.rev, hash);
            printf("[sync] uploaded: the server has rev %ld\n", info.rev);
            sync_set_message("Save Upload Complete");
        } else if (status == 409) {
            printf("[sync] not uploaded: the server has rev %ld from %s, newer than this save's base\n", info.rev,
                   info.device);
            sync_set_message("Not uploaded: another device saved since. You'll be asked next start.");
        } else if (status == 401) {
            printf("[sync] not uploaded: the server doesn't know this sync token\n");
            sync_set_message("Not uploaded: the sync token isn't accepted");
        } else {
            printf("[sync] not uploaded (HTTP %d); trying again at the next save\n", status);
            sync_set_message("Save upload failed. It'll try again next save.");
        }
        fflush(stdout);
    }
    return 0;
}

static void sync_start_thread(void) {
    if (s_thread == NULL) {
        s_lock = SDL_CreateMutex();
        s_wake = SDL_CreateCond();
        s_thread = SDL_CreateThread(sync_thread, "SaveSync", NULL);
    }
}

/* Hands the save files, as they are now, to the uploader */
static void sync_queue_upload(int force) {
    sync_buf_t b;

    if (!sync_bundle(&b)) {
        return;
    }
    sync_start_thread();
    SDL_LockMutex(s_lock);
    free(s_pending.data);
    s_pending = b;
    s_pending_force |= force; /* a save right after "keep this machine's" still has to win */
    s_busy = TRUE;
    SDL_CondSignal(s_wake);
    SDL_UnlockMutex(s_lock);
}

void pc_sync_after_save(void) {
    if (sync_enabled()) {
        sync_queue_upload(FALSE);
    } else if (s_undecided) {
        sync_set_message("Not uploaded: two different saves. Choose at the next start.");
    }
}

/* Each frame (pc_quick_save_draw): a message from the uploader goes on screen */
void pc_sync_poll(void) {
    const char* msg;

    if (s_lock == NULL) {
        return;
    }
    SDL_LockMutex(s_lock);
    msg = s_message;
    s_message = NULL;
    SDL_UnlockMutex(s_lock);
    if (msg != NULL) {
        pc_quick_save_show_message(msg);
    }
}

/* On quitting: wait (a little) for an upload still on its way */
void pc_sync_finish(void) {
    Uint32 start = SDL_GetTicks();
    int busy;

    if (s_lock == NULL) {
        return;
    }
    for (;;) {
        SDL_LockMutex(s_lock);
        busy = s_busy;
        SDL_UnlockMutex(s_lock);
        if (!busy || SDL_GetTicks() - start > 15000) {
            break;
        }
        if (g_pc_window != NULL) {
            SDL_SetWindowTitle(g_pc_window, "Animal Crossing Plus - uploading your save...");
        }
        SDL_Delay(50);
    }
    if (busy) {
        printf("[sync] quitting with the upload unfinished; it'll go up at the next start\n");
    }
}

/* --- at startup --- */

static void sync_mkdirs(const char* path) {
    char tmp[256];
    char* s;

    snprintf(tmp, sizeof(tmp), "%s", path);
    for (s = tmp + 1; *s != '\0'; s++) {
        if (*s == '/') {
            *s = '\0';
#ifdef _WIN32
            CreateDirectoryA(tmp, NULL);
#endif
            *s = '/';
        }
    }
}

/* path.sync-tmp, written whole */
static int sync_write_tmp(const char* path, const char* data, size_t len) {
    char tmp[256];
    FILE* f;
    int ok;

    snprintf(tmp, sizeof(tmp), "%s.sync-tmp", path);
    f = fopen(tmp, "wb");
    if (f == NULL) {
        return FALSE;
    }
    ok = fwrite(data, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
    if (!ok) {
        remove(tmp);
    }
    return ok;
}

static int sync_commit_tmp(const char* path) {
    char tmp[256];

    snprintf(tmp, sizeof(tmp), "%s.sync-tmp", path);
    return sync_replace(tmp, path);
}

static int sync_write_file(const char* path, const char* data, size_t len) {
    return sync_write_tmp(path, data, len) && sync_commit_tmp(path);
}

/* Copies the local save files to save/sync-backups/<time>-<why>/ (dir gets that folder); FALSE if any didn't copy */
static int sync_backup_local(const char* why, char* dir, size_t dir_n) {
    char names[SYNC_MAX_FILES][128];
    int n = sync_list(names), i, ok = TRUE;
    time_t now = time(NULL);
    char stamp[32];

    strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H%M%S", localtime(&now));
    snprintf(dir, dir_n, "save/sync-backups/%s-%s/", stamp, why);
    if (n == 0) {
        return TRUE;
    }
    sync_mkdirs(dir);
    for (i = 0; i < n; i++) {
        char src[192], dst[320];
        sync_buf_t b;

        snprintf(src, sizeof(src), SYNC_DIR "/%s", names[i]);
        snprintf(dst, sizeof(dst), "%s%s", dir, names[i]);
        if (!sync_read(src, &b) || !sync_write_file(dst, b.data, b.len)) {
            ok = FALSE;
        }
        free(b.data);
    }
    printf("[sync] %s this machine's save in %s\n", ok ? "kept" : "COULDN'T keep", dir);
    return ok;
}

/* Puts back the save files kept in dir (after a download that went wrong halfway) */
static void sync_restore_backup(const char* dir) {
    char names[SYNC_MAX_FILES][128];
    int n = sync_list(names), i;
    DIR* d;
    struct dirent* e;

    for (i = 0; i < n; i++) { /* the downloaded ones out first: the backup is the whole set */
        char path[192];

        snprintf(path, sizeof(path), SYNC_DIR "/%s", names[i]);
        remove(path);
    }
    if ((d = opendir(dir)) == NULL) {
        return;
    }
    while ((e = readdir(d)) != NULL) {
        char src[320], dst[192];
        sync_buf_t b;

        if (!sync_wanted(e->d_name) || strlen(e->d_name) >= 128) {
            continue;
        }
        snprintf(src, sizeof(src), "%s%s", dir, e->d_name);
        snprintf(dst, sizeof(dst), SYNC_DIR "/%s", e->d_name);
        if (sync_read(src, &b)) {
            sync_write_file(dst, b.data, b.len);
            free(b.data);
        }
    }
    closedir(d);
    printf("[sync] put this machine's save back from %s\n", dir);
}

/* One entry of a bundle at q: FALSE if it doesn't fit in what's left or isn't a save file's name */
static int sync_entry(const unsigned char* q, const unsigned char* end, char name[128], unsigned* size) {
    size_t left = (size_t)(end - q);
    unsigned short nl;

    if (left < 6) {
        return FALSE;
    }
    memcpy(&nl, q, 2);
    if (nl == 0 || nl >= 128 || left - 6 < nl) {
        return FALSE;
    }
    memcpy(name, q + 2, nl);
    name[nl] = '\0';
    memcpy(size, q + 2 + nl, 4);
    return *size <= left - 6 - nl && strlen(name) == nl && strpbrk(name, "/\\:") == NULL && sync_wanted(name);
}

/* Downloads the server's save and puts it in place: all of it or none of it */
static int sync_download(const sync_info_t* info) {
    char url[384], headers[256], hash[65], backup[192];
    char names[SYNC_MAX_FILES][128];
    pc_http_resp r;
    const unsigned char* p;
    const unsigned char* q;
    const unsigned char* end;
    unsigned count, i, size;
    int status, n, k, ok = TRUE;

    sync_auth(headers, sizeof(headers), NULL);
    sync_url(url, sizeof(url), "/v1/save");
    status = pc_http_request("GET", url, headers, NULL, 0, &r, SYNC_TIMEOUT_MS * 3, NULL);
    if (status != 200) {
        printf("[sync] download failed (HTTP %d)\n", status);
        pc_http_free(&r);
        return FALSE;
    }
    pc_sha256_hex(r.body, r.len, hash);
    p = (const unsigned char*)r.body, end = p + r.len;
    if (strcmp(hash, info->sha256) != 0 || r.len < 12 || memcmp(p, "ACSB", 4) != 0) {
        printf("[sync] the download arrived damaged; keeping this machine's save\n");
        pc_http_free(&r);
        return FALSE;
    }
    memcpy(&count, p + 8, 4);
    if (count == 0 || count > SYNC_MAX_FILES) {
        pc_http_free(&r);
        return FALSE;
    }
    /* check it all before touching anything */
    for (i = 0, q = p + 12; i < count; i++) {
        if (!sync_entry(q, end, names[i], &size)) {
            printf("[sync] the download isn't a save bundle; keeping this machine's save\n");
            pc_http_free(&r);
            return FALSE;
        }
        q += 6 + strlen(names[i]) + size;
    }
    if (!sync_backup_local("before-download", backup, sizeof(backup))) {
        pc_http_free(&r);
        return FALSE;
    }
    /* every file written beside its old one first, then all swapped in */
    for (i = 0, q = p + 12; i < count && ok; i++) {
        char path[192];

        sync_entry(q, end, names[i], &size);
        snprintf(path, sizeof(path), SYNC_DIR "/%s", names[i]);
        ok = sync_write_tmp(path, (const char*)q + 6 + strlen(names[i]), size);
        q += 6 + strlen(names[i]) + size;
    }
    pc_http_free(&r);
    for (k = 0; k < (int)count; k++) {
        char path[192];

        snprintf(path, sizeof(path), SYNC_DIR "/%s", names[k]);
        if (!ok) {
            strncat(path, ".sync-tmp", sizeof(path) - strlen(path) - 1);
            remove(path);
        } else if (!sync_commit_tmp(path)) {
            printf("[sync] couldn't put %s in place\n", path);
            ok = FALSE;
        }
    }
    if (!ok) {
        printf("[sync] couldn't write the download; keeping this machine's save\n");
        sync_restore_backup(backup);
        return FALSE;
    }
    /* save files the server's set doesn't have go (they're in the backup) */
    {
        char local[SYNC_MAX_FILES][128];

        n = sync_list(local);
        for (k = 0; k < n; k++) {
            for (i = 0; i < count && strcmp(local[k], names[i]) != 0; i++) {
            }
            if (i == count) {
                char path[192];

                snprintf(path, sizeof(path), SYNC_DIR "/%s", local[k]);
                remove(path);
                printf("[sync] %s isn't in the server's save; it's in the backup\n", local[k]);
            }
        }
    }
    sync_local_hash(hash);
    sync_state_write(info->rev, hash);
    printf("[sync] downloaded rev %ld (saved on %s)\n", info->rev, info->device);
    return TRUE;
}

enum { SYNC_KEEP_SERVER, SYNC_KEEP_LOCAL, SYNC_UNDECIDED };

static int sync_ask_conflict(const sync_info_t* info) {
    const char* env = getenv("PC_SYNC_ANSWER"); /* dev: tests can't click */
    char when[64], msg[768], keep_server[96];
    time_t t = (time_t)info->saved_at;
    SDL_MessageBoxButtonData buttons[2];
    SDL_MessageBoxData box;
    int hit = SYNC_UNDECIDED;

    strftime(when, sizeof(when), "%b %d, %H:%M", localtime(&t));
    snprintf(keep_server, sizeof(keep_server), "Use %s's", info->device[0] ? info->device : "the server");
    snprintf(msg, sizeof(msg),
             "Your town was saved on %s (%s) since you last played here, and this machine has unsynced progress "
             "too.\n\nWhich one do you want to play? The other is kept as a backup (on the server, or in "
             "save/sync-backups).",
             info->device[0] ? info->device : "another device", when);
    printf("[sync] conflict: %s\n", msg);
    if (env != NULL) {
        return strcmp(env, "local") == 0 ? SYNC_KEEP_LOCAL : strcmp(env, "server") == 0 ? SYNC_KEEP_SERVER : SYNC_UNDECIDED;
    }
    buttons[0].flags = 0, buttons[0].buttonid = SYNC_KEEP_LOCAL, buttons[0].text = "Keep this machine's";
    buttons[1].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, buttons[1].buttonid = SYNC_KEEP_SERVER,
    buttons[1].text = keep_server;
    memset(&box, 0, sizeof(box));
    box.flags = SDL_MESSAGEBOX_WARNING;
    box.window = g_pc_window;
    box.title = "Animal Crossing Plus - two saves";
    box.message = msg;
    box.numbuttons = 2;
    box.buttons = buttons;
    if (SDL_ShowMessageBox(&box, &hit) != 0 || hit < 0) {
        hit = SYNC_UNDECIDED; /* the box couldn't show, or was closed */
    }
    return hit;
}

/* Before the town loads (pc_m_card.c): brings this machine and the server together */
void pc_sync_before_load(void) {
    char url[384], headers[256], local[65];
    pc_http_resp r;
    sync_info_t info;
    sync_state_t st;
    int status, local_changed, server_changed, choice;

    static int s_done;

    /* once, at startup: this also runs when the town is loaded again later (pc_save_check_and_load) */
    if (s_done) {
        return;
    }
    s_done = TRUE;
    if (!sync_enabled()) {
        return;
    }
    sync_auth(headers, sizeof(headers), NULL);
    sync_url(url, sizeof(url), "/v1/save/info");
    status = pc_http_request("GET", url, headers, NULL, 0, &r, SYNC_TIMEOUT_MS, NULL);
    memset(&info, 0, sizeof(info));
    sync_info_parse(r.body, &info);
    pc_http_free(&r);
    if (status == 0) {
        printf("[sync] couldn't reach %s; playing this machine's save\n", g_pc_settings.sync_url);
        sync_start_thread();
        sync_set_message("Couldn't reach the sync server");
        return;
    }
    if (status != 200) {
        printf("[sync] the server said HTTP %d%s\n", status, status == 401 ? " (sync token not accepted)" : "");
        sync_start_thread();
        sync_set_message(status == 401 ? "Sync token not accepted" : "The sync server had a problem");
        return;
    }
    sync_state_read(&st);
    sync_local_hash(local);
    if (s_off) {
        return; /* too many save files to sync (sync_list) */
    }
    if (info.rev < st.rev) {
        /* the server went back (its data reset, or restored): start over as if this machine never synced */
        printf("[sync] the server has rev %ld, older than the last sync's %ld; starting over\n", info.rev, st.rev);
        st.rev = -1;
        snprintf(st.hash, sizeof(st.hash), "none");
        remove(SYNC_STATE); /* the uploader reads it for its base */
    }
    if (info.rev > 0 && strcmp(local, info.sha256) == 0) {
        /* already the same: a first sync on a machine with this save, or an upload the game quit before hearing back */
        if (info.rev != st.rev || strcmp(local, st.hash) != 0) {
            sync_state_write(info.rev, local);
        }
        printf("[sync] in step with the server (rev %ld)\n", info.rev);
        return;
    }
    local_changed = strcmp(local, "none") != 0 && strcmp(local, st.hash) != 0;
    server_changed = info.rev > 0 && info.rev != st.rev;
    printf("[sync] server rev %ld (%s), last synced rev %ld; local %s\n", info.rev, info.device, st.rev,
           local_changed ? "changed" : "unchanged");

    if (!server_changed) {
        if (local_changed) {
            sync_queue_upload(FALSE); /* this machine has newer saves (or the server has none yet) */
        }
        return;
    }
    choice = local_changed ? sync_ask_conflict(&info) : SYNC_KEEP_SERVER;
    if (choice == SYNC_UNDECIDED) {
        /* nothing is decided for the player: this machine's save, and no uploads until the next start asks again */
        printf("[sync] no answer about the two saves; playing this machine's, not uploading\n");
        s_off = TRUE;
        s_undecided = TRUE;
        sync_start_thread();
        sync_set_message("Two different saves: sync is paused until you choose at the next start");
        return;
    }
    if (choice == SYNC_KEEP_SERVER) {
        if (!sync_download(&info)) {
            sync_start_thread();
            sync_set_message("Couldn't download your save; playing this machine's");
        }
        return;
    }
    sync_queue_upload(TRUE); /* the player chose this machine's: the server keeps the other in its history */
}
