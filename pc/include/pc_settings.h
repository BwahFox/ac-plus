#ifndef PC_SETTINGS_H
#define PC_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int window_width;
    int window_height;
    int fullscreen;       /* 0=windowed, 1=fullscreen, 2=borderless */
    int vsync;            /* 0=off, 1=on */
    int max_fps;          /* 0=uncapped, otherwise frame limiter target */
    int msaa;             /* 0=off, 2/4/8=samples */
    int texture_filtering; /* 0=force nearest-neighbor, 1=use the game's texture filtering */
    int preload_textures; /* 0=off (load on demand), 1=on (load all at startup), 2=on + cache file */
    int world_curve;      /* City Folk style curved ground + sky outdoors, strength in percent (0=flat, default 75) */
    int cf_weather;       /* 1 = City Folk style weather: changes during the day, overcast, rainbows (default) */
    int cf_tv;            /* 1 = TVs show City Folk's programs through the day, when converted (default) */
    int tanning;          /* 1 = the sun tans the player (as the GameCube), 0 = skin keeps its chosen tone (default) */
    char weather_clear_days[128]; /* "M/D,M/D,...": days the City Folk weather stays clear all day (pc_weather.c) */
    int disable_resetti;  /* 0=normal (Resetti appears on reset), 1=disable reset penalty */
    int disable_shop_visitor_req; /* 0=normal (Nookington's needs a foreign-town shopper), 1=skip requirement */
    int borderless_acres; /* 0=original acre transitions (faster, draws less), 1=continuous camera/movement */
    int nes_aspect;       /* NES emulator aspect: 0=fullscreen stretch, 1=4:3 pillarbox (default) */
    int master_volume;    /* Applied at the PC audio output, 0-100 (default 100) */
    char music_pack[64];  /* Folder under music/ with custom hourly tracks, empty = off (see pc_music.h) */
    int music_volume;     /* Custom music pack volume, 0-100 (default 100) */
    int autosave_minutes; /* Save automatically after this many minutes of play, 0 = off (default) */
    int save_on_quit;     /* 1 = save the town when quitting, where Z+Start could (default) */
    int cloud_save;       /* 1 = mirror the town save into cloud_save_folder (see pc_cloud_save.c) */
    char cloud_save_folder[260];
    int quick_load;       /* 1 = returning players skip the arrival scene and start in town */
    int server_sync;      /* 1 = ac-sync.sh downloads the save from server_sync_host before playing, uploads after */
    char server_sync_host[128]; /* ssh destination, e.g. user@myserver */
    char server_sync_dir[128];  /* folder on the server, relative to its home */
    int stick_deadzone;   /* Gamepad main stick deadzone, percent 0-40 (default 12) */
    int cstick_deadzone;  /* Gamepad C-stick deadzone, percent 0-40 (default 12) */
    int mouse_mode;       /* 0 = off, 1 = menus and designer (pc_mouse.c, default), 2 = ac-plus designer only (pc_designer_mouse.c_inc) */
} PCSettings;

extern PCSettings g_pc_settings;

void pc_settings_load(void);
void pc_settings_save(void);
void pc_settings_apply(void);
void pc_settings_cycle_resolution(int* width, int* height, int dir);

#ifdef __cplusplus
}
#endif

#endif /* PC_SETTINGS_H */
