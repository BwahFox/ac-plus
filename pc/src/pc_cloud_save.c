/* pc_cloud_save.c - keep the home town save in a synced folder.
 *
 * With cloud_save = 1 and cloud_save_folder set, every save of the home town
 * (gyroid, Z+Start, autosave) is copied into that folder, and at startup a copy
 * there that is newer than the local save replaces it before the game loads.
 * The syncing itself is left to whatever owns the folder: Syncthing, Nextcloud,
 * an sshfs mount, a Steam Deck shared folder... Under Wine/Proton the folder can
 * be a Unix path (/home/me/Sync/ac-plus) or a drive path (Z:\home\...).
 *
 * The newer file wins. Whatever it replaces is kept next to it as .cloudbak, so
 * playing on two machines without syncing in between never loses a town. */
#include "pc_settings.h"
#include "pc_ext_save.h"

#include <dolphin/os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Same size check the loader relies on: GCI header + 0x72000 bytes of card data. */
#define CLOUD_GCI_SIZE (0x40 + 0x72000)
/* Copies whose times differ by less than this are the same save. */
#define CLOUD_MTIME_SLACK 2

static int cloud_enabled(void) {
    return g_pc_settings.cloud_save && g_pc_settings.cloud_save_folder[0] != '\0';
}

static const char* base_name(const char* path) {
    const char* a = strrchr(path, '/');
    const char* b = strrchr(path, '\\');
    const char* p = (a > b) ? a : b;
    return p ? p + 1 : path;
}

static void cloud_path_for(char* out, size_t n, const char* local_path) {
    const char* folder = g_pc_settings.cloud_save_folder;
    size_t len = strlen(folder);
    const char* sep = (len > 0 && (folder[len - 1] == '/' || folder[len - 1] == '\\')) ? "" : "/";

    snprintf(out, n, "%s%s%s", folder, sep, base_name(local_path));
}

static int copy_file(const char* src, const char* dst) {
    char tmp[600];
    FILE* in;
    FILE* out;
    char buf[16384];
    size_t got;
    int ok = TRUE;

    snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
    in = fopen(src, "rb");
    if (in == NULL) {
        return FALSE;
    }
    out = fopen(tmp, "wb");
    if (out == NULL) {
        fclose(in);
        return FALSE;
    }
    while ((got = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, got, out) != got) {
            ok = FALSE;
            break;
        }
    }
    if (ferror(in)) {
        ok = FALSE;
    }
    fclose(in);
    if (fclose(out) != 0) {
        ok = FALSE;
    }
    if (!ok) {
        remove(tmp);
        return FALSE;
    }
    /* Windows rename() won't replace an existing file. */
    remove(dst);
    if (rename(tmp, dst) != 0) {
        remove(tmp);
        return FALSE;
    }
    return TRUE;
}

static int looks_like_town_save(const char* path) {
    struct stat st;
    char game[4] = { 0 };
    FILE* f;

    if (stat(path, &st) != 0 || st.st_size != CLOUD_GCI_SIZE) {
        return FALSE;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return FALSE;
    }
    if (fread(game, 1, 3, f) != 3) {
        game[0] = '\0';
    }
    fclose(f);
    return memcmp(game, "GAF", 3) == 0;
}

/* After a successful save of the home town: the .gci and its ac-plus
 * companion file (pc_ext_save.c) travel together. */
void pc_cloud_save_push(const char* local_path) {
    char cloud[600];
    char ext[400];
    struct stat st;

    if (!cloud_enabled()) {
        return;
    }
    cloud_path_for(cloud, sizeof(cloud), local_path);
    if (copy_file(local_path, cloud)) {
        OSReport("[cloud] Uploaded save to %s\n", cloud);
    } else {
        OSReport("[cloud] Could not write %s (does the folder exist?)\n", cloud);
        return;
    }

    pc_ext_save_path(ext, sizeof(ext), local_path);
    if (stat(ext, &st) == 0) {
        cloud_path_for(cloud, sizeof(cloud), ext);
        if (!copy_file(ext, cloud)) {
            OSReport("[cloud] Could not write %s\n", cloud);
        }
    }
}

/* At startup, before the home town save is read. */
void pc_cloud_save_pull(const char* local_path) {
    char cloud[600];
    char keep[600];
    struct stat st_cloud;
    struct stat st_local;
    int have_local;

    if (!cloud_enabled()) {
        return;
    }
    cloud_path_for(cloud, sizeof(cloud), local_path);
    if (stat(cloud, &st_cloud) != 0) {
        OSReport("[cloud] No cloud save at %s yet\n", cloud);
        return;
    }

    have_local = stat(local_path, &st_local) == 0;
    if (have_local && st_cloud.st_mtime <= st_local.st_mtime + CLOUD_MTIME_SLACK) {
        OSReport("[cloud] Local save is up to date\n");
        return;
    }
    if (!looks_like_town_save(cloud)) {
        OSReport("[cloud] %s is not a town save, ignoring it\n", cloud);
        return;
    }

    if (have_local) {
        snprintf(keep, sizeof(keep), "%s.cloudbak", local_path);
        if (!copy_file(local_path, keep)) {
            OSReport("[cloud] Could not back up the local save, keeping it\n");
            return;
        }
    }
    if (copy_file(cloud, local_path)) {
        OSReport("[cloud] Newer save downloaded from %s%s\n", cloud,
                 have_local ? " (previous local save kept as .cloudbak)" : "");
    } else {
        OSReport("[cloud] Could not copy %s over the local save\n", cloud);
        return;
    }

    /* Its companion file comes along (or goes, if the cloud has none). */
    {
        char ext[400];
        char cloud_ext[600];
        struct stat st;

        pc_ext_save_path(ext, sizeof(ext), local_path);
        cloud_path_for(cloud_ext, sizeof(cloud_ext), ext);
        if (stat(ext, &st) == 0) {
            snprintf(keep, sizeof(keep), "%s.cloudbak", ext);
            copy_file(ext, keep);
        }
        if (stat(cloud_ext, &st) == 0) {
            copy_file(cloud_ext, ext);
        } else {
            remove(ext);
        }
    }
}
