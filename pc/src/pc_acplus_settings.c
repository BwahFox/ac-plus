/* pc_acplus_settings.c - ac-plus's own settings, in ac-plus.ini next to Nookery's config.json.
 * Nookery keeps its settings (video, audio, controls, its gameplay options) in config.json with its own menu;
 * what only ac-plus has is here. The first time, they're read from an older ac-plus's settings.ini. */
#include "pc_settings.h"
#include "pc_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* ACPLUS_SETTINGS_FILE = "ac-plus.ini";

static int s_seen_server_sync = 0; /* the file has server_sync (import_ac_sync_conf) */

static void acplus_defaults(void) {
    g_pc_settings.world_curve = 75;
    g_pc_settings.cf_weather = 1;
    g_pc_settings.cf_tv = 1;
    g_pc_settings.tanning = 0;
    g_pc_settings.mouse_hold_pockets = 0;
    g_pc_settings.weather_clear_days[0] = '\0';
    g_pc_settings.music_pack[0] = '\0';
    g_pc_settings.music_volume = 100;
    g_pc_settings.music_custom = 1;
    g_pc_settings.rain_spring = 0;
    g_pc_settings.autosave_minutes = 0;
    g_pc_settings.save_on_quit = 1;
    g_pc_settings.cloud_save = 0;
    g_pc_settings.cloud_save_folder[0] = '\0';
    g_pc_settings.quick_load = 0;
    g_pc_settings.server_sync = 0;
    g_pc_settings.server_sync_host[0] = '\0';
    snprintf(g_pc_settings.server_sync_dir, sizeof(g_pc_settings.server_sync_dir), "ac-plus-saves/sync");
    g_pc_settings.sync_enabled = 0;
    g_pc_settings.sync_url[0] = '\0';
    g_pc_settings.sync_token[0] = '\0';
    g_pc_settings.mp_port = 41330;
    g_pc_settings.mp_last_visit[0] = '\0';
    snprintf(g_pc_settings.mp_server, sizeof(g_pc_settings.mp_server), "%s", PC_MP_DEFAULT_SERVER);
    g_pc_settings.mp_public = 0;
    g_pc_settings.update_check = 1;
    snprintf(g_pc_settings.update_repo, sizeof(g_pc_settings.update_repo), "BwahFox/ac-plus");
    g_pc_settings.update_skip[0] = '\0';
    g_pc_settings.mouse_mode = 1;
}

static const char* skip_ws(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void trim_end(char* s) {
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' ||
                       s[len-1] == '\r' || s[len-1] == '\n')) {
        s[--len] = '\0';
    }
}

static void apply_setting(const char* key, const char* value) {
    int val = atoi(value);

    if (strcmp(key, "world_curve") == 0) {
        if (val >= 0 && val <= 300) g_pc_settings.world_curve = val;
    } else if (strcmp(key, "cf_weather") == 0) {
        if (val == 0 || val == 1) g_pc_settings.cf_weather = val;
    } else if (strcmp(key, "cf_tv") == 0) {
        if (val == 0 || val == 1) g_pc_settings.cf_tv = val;
    } else if (strcmp(key, "tanning") == 0) {
        if (val == 0 || val == 1) g_pc_settings.tanning = val;
    } else if (strcmp(key, "mouse_hold_pockets") == 0) {
        if (val == 0 || val == 1) g_pc_settings.mouse_hold_pockets = val;
    } else if (strcmp(key, "weather_clear_days") == 0) {
        snprintf(g_pc_settings.weather_clear_days, sizeof(g_pc_settings.weather_clear_days), "%s", value);
    } else if (strcmp(key, "music_pack") == 0) {
        snprintf(g_pc_settings.music_pack, sizeof(g_pc_settings.music_pack), "%s", value);
    } else if (strcmp(key, "music_volume") == 0) {
        if (val >= 0 && val <= 100) g_pc_settings.music_volume = val;
    } else if (strcmp(key, "music_custom") == 0) {
        if (val == 0 || val == 1) g_pc_settings.music_custom = val;
    } else if (strcmp(key, "rain_spring") == 0) {
        if (val == 0 || val == 1) g_pc_settings.rain_spring = val;
    } else if (strcmp(key, "autosave_minutes") == 0) {
        if (val >= 0 && val <= 600) g_pc_settings.autosave_minutes = val;
    } else if (strcmp(key, "save_on_quit") == 0) {
        if (val == 0 || val == 1) g_pc_settings.save_on_quit = val;
    } else if (strcmp(key, "cloud_save") == 0) {
        if (val == 0 || val == 1) g_pc_settings.cloud_save = val;
    } else if (strcmp(key, "cloud_save_folder") == 0) {
        snprintf(g_pc_settings.cloud_save_folder, sizeof(g_pc_settings.cloud_save_folder), "%s", value);
    } else if (strcmp(key, "quick_load") == 0) {
        if (val == 0 || val == 1) g_pc_settings.quick_load = val;
    } else if (strcmp(key, "server_sync") == 0) {
        if (val == 0 || val == 1) g_pc_settings.server_sync = val;
        s_seen_server_sync = 1;
    } else if (strcmp(key, "server_sync_host") == 0) {
        snprintf(g_pc_settings.server_sync_host, sizeof(g_pc_settings.server_sync_host), "%s", value);
    } else if (strcmp(key, "server_sync_dir") == 0) {
        snprintf(g_pc_settings.server_sync_dir, sizeof(g_pc_settings.server_sync_dir), "%s", value);
    } else if (strcmp(key, "sync_enabled") == 0) {
        if (val == 0 || val == 1) g_pc_settings.sync_enabled = val;
    } else if (strcmp(key, "mp_port") == 0) {
        int v = atoi(value);
        g_pc_settings.mp_port = (v >= 1024 && v <= 65535) ? v : 41330;
    } else if (strcmp(key, "mp_server") == 0) {
        snprintf(g_pc_settings.mp_server, sizeof(g_pc_settings.mp_server), "%s", value);
    } else if (strcmp(key, "mp_public") == 0) {
        g_pc_settings.mp_public = atoi(value) != 0;
    } else if (strcmp(key, "mp_last_visit") == 0) {
        snprintf(g_pc_settings.mp_last_visit, sizeof(g_pc_settings.mp_last_visit), "%s", value);
    } else if (strcmp(key, "sync_url") == 0) {
        snprintf(g_pc_settings.sync_url, sizeof(g_pc_settings.sync_url), "%s", value);
    } else if (strcmp(key, "sync_token") == 0) {
        snprintf(g_pc_settings.sync_token, sizeof(g_pc_settings.sync_token), "%s", value);
    } else if (strcmp(key, "update_check") == 0) {
        if (val == 0 || val == 1) g_pc_settings.update_check = val;
    } else if (strcmp(key, "update_repo") == 0) {
        snprintf(g_pc_settings.update_repo, sizeof(g_pc_settings.update_repo), "%s", value);
    } else if (strcmp(key, "update_skip") == 0) {
        snprintf(g_pc_settings.update_skip, sizeof(g_pc_settings.update_skip), "%s", value);
    } else if (strcmp(key, "mouse_mode") == 0) {
        if (val >= 0 && val <= 2) g_pc_settings.mouse_mode = val;
    }
    (void)val;
}

void pc_settings_save(void) {
    FILE* f = fopen(ACPLUS_SETTINGS_FILE, "w");
    if (!f) {
        printf("[Settings] Failed to write %s\n", ACPLUS_SETTINGS_FILE);
        return;
    }
    fprintf(f, "# ac-plus's own settings (Nookery's are in config.json)\n");
    fprintf(f, "[City Folk]\n");
    fprintf(f, "# City Folk style curved world and sky outdoors, strength in percent: 0 = flat (original), 75 = default, up to 300\n");
    fprintf(f, "world_curve = %d\n", g_pc_settings.world_curve);
    fprintf(f, "\n");
    fprintf(f, "# City Folk weather: 1 = the weather can change during the day, with overcast skies and rainbows after rain.\n");
    fprintf(f, "# 0 = the original one weather per day\n");
    fprintf(f, "cf_weather = %d\n", g_pc_settings.cf_weather);
    fprintf(f, "\n");
    fprintf(f, "# Days the City Folk weather stays clear all day, as month/day: 10/25,12/31\n");
    fprintf(f, "weather_clear_days = %s\n", g_pc_settings.weather_clear_days);
    fprintf(f, "\n");
    fprintf(f, "# City Folk TV: 1 = TVs show City Folk's programs through the day (from your City Folk disc), 0 = the original static\n");
    fprintf(f, "cf_tv = %d\n", g_pc_settings.cf_tv);
    fprintf(f, "\n");
    fprintf(f, "# Tanning: 1 = summer sun tans your skin as on the GameCube, 0 = it keeps the tone you picked\n");
    fprintf(f, "tanning = %d\n", g_pc_settings.tanning);
    fprintf(f, "\n");
    fprintf(f, "# Mouse: 1 = holding right-click with empty hands opens your pockets (Nookery's), 0 = right-click is only A\n");
    fprintf(f, "mouse_hold_pockets = %d\n", g_pc_settings.mouse_hold_pockets);
    fprintf(f, "\n");
    fprintf(f, "[Saving]\n");
    fprintf(f, "# Autosave: save automatically after this many minutes of play (only while you're walking around). 0 = off\n");
    fprintf(f, "autosave_minutes = %d\n", g_pc_settings.autosave_minutes);
    fprintf(f, "\n");
    fprintf(f, "# Save on quit: 1 = save the town when you quit from the pause menu or close the window (if saving is possible there)\n");
    fprintf(f, "save_on_quit = %d\n", g_pc_settings.save_on_quit);
    fprintf(f, "\n");
    fprintf(f, "# Cloud saves: 1 = copy the town save into cloud_save_folder after every save, and load a newer copy\n");
    fprintf(f, "# from there at startup. Point it at a synced folder (Syncthing, Nextcloud, an sshfs mount...).\n");
    fprintf(f, "# Any save that gets replaced is kept as a .cloudbak file next to it.\n");
    fprintf(f, "cloud_save = %d\n", g_pc_settings.cloud_save);
    fprintf(f, "cloud_save_folder = %s\n", g_pc_settings.cloud_save_folder);
    fprintf(f, "\n");
    fprintf(f, "# Quick load: 1 = skip the arrival scene when a returning player loads and start straight in town\n");
    fprintf(f, "quick_load = %d\n", g_pc_settings.quick_load);
    fprintf(f, "\n");
    fprintf(f, "# Server sync: 1 = when the game is started through ac-sync.sh, download the save from a server\n");
    fprintf(f, "# before playing and upload it after. Uses ssh with key login: server_sync_host is user@host\n");
    fprintf(f, "# (or a Host from ~/.ssh/config), server_sync_dir a folder in that user's home.\n");
    fprintf(f, "server_sync = %d\n", g_pc_settings.server_sync);
    fprintf(f, "server_sync_host = %s\n", g_pc_settings.server_sync_host);
    fprintf(f, "server_sync_dir = %s\n", g_pc_settings.server_sync_dir);
    fprintf(f, "\n");
    fprintf(f, "# In-game sync: 1 = the game keeps the town in sync with an ac-plus-server by itself (no wrapper\n");
    fprintf(f, "# script): it downloads a newer save at startup and uploads after every save. sync_url is the server\n");
    fprintf(f, "# (https://... or http://...), sync_token comes from the server's add-token. See server/README.md.\n");
    fprintf(f, "sync_enabled = %d\n", g_pc_settings.sync_enabled);
    fprintf(f, "sync_url = %s\n", g_pc_settings.sync_url);
    fprintf(f, "sync_token = %s\n", g_pc_settings.sync_token);
    fprintf(f, "\n");
    fprintf(f, "[Online]\n");
    fprintf(f, "# Online visits (pause menu > Online Play): mp_port is the UDP port your town is hosted on (forward it on\n");
    fprintf(f, "# your router for visitors from outside your network); mp_last_visit is the last town you visited.\n");
    fprintf(f, "# mp_server is an ac-plus-server for invite codes (host[:port], UDP 41331 by default; see server/README.md):\n");
    fprintf(f, "# with one, hosting gets a code to give your visitors, and no router setup is needed for most networks.\n");
    fprintf(f, "# The default is the ac-plus project's server; empty = none (visit by address only).\n");
    fprintf(f, "# mp_public = 1 also lists your town on that server while you host.\n");
    fprintf(f, "mp_port = %d\n", g_pc_settings.mp_port);
    fprintf(f, "mp_last_visit = %s\n", g_pc_settings.mp_last_visit);
    fprintf(f, "mp_server = %s\n", g_pc_settings.mp_server);
    fprintf(f, "mp_public = %d\n", g_pc_settings.mp_public);
    fprintf(f, "\n");
    fprintf(f, "[Updates]\n");
    fprintf(f, "# Update check: 1 = at startup, look for a newer ac-plus release and offer to install it (saves, settings,\n");
    fprintf(f, "# music and texture packs are kept). update_repo is the GitHub repository it comes from (owner/name), so a\n");
    fprintf(f, "# fork can point it at its own. update_skip is a release you chose to skip.\n");
    fprintf(f, "update_check = %d\n", g_pc_settings.update_check);
    fprintf(f, "update_repo = %s\n", g_pc_settings.update_repo);
    fprintf(f, "update_skip = %s\n", g_pc_settings.update_skip);
    fprintf(f, "\n");
    fprintf(f, "# Custom hourly music: the name of a folder in music/ (leave empty for the game's own music).\n");
    fprintf(f, "# The folder holds hourly/00-23, plus optional rain/ and snow/ variants (ogg, mp3, flac or wav).\n");
    fprintf(f, "music_pack = %s\n", g_pc_settings.music_pack);
    fprintf(f, "\n");
    fprintf(f, "# Custom music volume as a percentage (0-100)\n");
    fprintf(f, "music_volume = %d\n", g_pc_settings.music_volume);
    fprintf(f, "\n");
    fprintf(f, "# Custom music: 1 = play the music pack above, 0 = the game's own music (keeps the pack's name)\n");
    fprintf(f, "music_custom = %d\n", g_pc_settings.music_custom);
    fprintf(f, "\n");
    fprintf(f, "# Rain music (the game's own music): 0 = the rain song, 1 = the hour's spring (cherry blossom) arrangement\n");
    fprintf(f, "rain_spring = %d\n", g_pc_settings.rain_spring);
    fprintf(f, "\n");
    fprintf(f, "# Mouse: 1 = in menus, the inventory and the pattern designer, 2 = only ac-plus's pattern designer mouse, 0 = off\n");
    fprintf(f, "mouse_mode = %d\n", g_pc_settings.mouse_mode);
    fclose(f);
    printf("[Settings] Saved %s\n", ACPLUS_SETTINGS_FILE);
}

static int acplus_read(const char* path) {
    FILE* f = fopen(path, "r");
    char line[512];

    if (f == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        const char* p = skip_ws(line);
        char key[64];
        char* eq;
        int n;

        if (*p == '#' || *p == ';' || *p == '[' || *p == '\0' || *p == '\n' || *p == '\r') {
            continue;
        }
        eq = strchr(p, '=');
        if (eq == NULL) {
            continue;
        }
        n = (int)(eq - p);
        if (n >= (int)sizeof(key)) {
            continue;
        }
        memcpy(key, p, n);
        key[n] = '\0';
        trim_end(key);
        trim_end(eq + 1);
        apply_setting(key, skip_ws(eq + 1));
    }
    fclose(f);
    return 1;
}

/* An ac-sync.conf from before server_sync was a setting: its host and folder, switched on */
static void import_ac_sync_conf(void) {
    FILE* f = fopen("ac-sync.conf", "r");
    char line[256];

    if (f == NULL || s_seen_server_sync) {
        if (f != NULL) fclose(f);
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        trim_end(line);
        if (strncmp(line, "AC_SYNC_HOST=", 13) == 0) {
            snprintf(g_pc_settings.server_sync_host, sizeof(g_pc_settings.server_sync_host), "%s", line + 13);
            g_pc_settings.server_sync = 1;
        } else if (strncmp(line, "AC_SYNC_DIR=", 12) == 0) {
            snprintf(g_pc_settings.server_sync_dir, sizeof(g_pc_settings.server_sync_dir), "%s", line + 12);
        }
    }
    fclose(f);
}

void pc_acplus_settings_load(void) {
    acplus_defaults();
    if (!acplus_read(ACPLUS_SETTINGS_FILE)) {
        /* the first start of this build: an older ac-plus's settings.ini has them (Nookery reads its own part) */
        if (acplus_read("settings.ini")) {
            printf("[Settings] ac-plus settings taken over from settings.ini\n");
        }
        import_ac_sync_conf();
        pc_settings_save();
    }
    if (getenv("PC_NO_UPDATE_CHECK") != NULL) {
        g_pc_settings.update_check = 0;
    }
}

/* jaudio (game64.c_inc), which doesn't see pc_settings.h */
int pc_settings_rain_spring(void) {
    return g_pc_settings.rain_spring;
}

/* m_controller.c (game code, no pc_settings.h there): Nookery's hold-right-click-for-pockets, if wanted */
int pc_mouse_hold_pockets(void) {
    return g_pc_settings.mouse_hold_pockets;
}
