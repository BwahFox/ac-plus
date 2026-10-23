/* pc_devtest.c - unattended input and frame capture for development, off unless asked for.
 *
 * PC_AUTO_A=N        tap A every N frames (enough to get from the title screen into town)
 * PC_AUTO_A_END=F    stop tapping at frame F
 * PC_SHOT=F1,F2,...  write the picture shown at each listed frame to devshot_<F>.ppm
 * PC_EXIT_AT=F       quit after frame F
 * PC_QUIT_AT=F       ask to quit at frame F, as the pause menu does (saves first with save_on_quit)
 * PC_CSTICK=F1-F2:X:Y the same for the C stick
 * PC_STICK=F1-F2:X:Y hold the main stick at X,Y (-127..127) from frame F1 to F2, e.g. 9500-9800:0:127 walks north;
 *                    several segments can be joined with ';'
 * PC_PRESS=F1-F2:MASK hold PAD_BUTTON_* bits (hex) from frame F1 to F2; segments joined with ';'
 *                    e.g. 9500-9510:10;9505-9508:1010 is Z, then Z+Start
 * PC_DESIGNER_AT=F   pick "Design a Pattern" from the pause menu at frame F
 * PC_STORAGE_AT=F    pick "Pattern Storage" from the pause menu at frame F
 * PC_GROUND_AT=F     "Lay on Ground" with pattern slot 0 at frame F
 * PC_GROUND2_AT=F    the same again (picks it back up)
 * PC_GROUND_PROBE_AT=F  write ground_probe_<F>.txt: which units of the player's acre take a pattern
 *                    (PC_GROUND_PROBE2_AT=F a second one)
 * PC_TILE_INFO_AT=F  toggle the F4 tile readout at frame F
 * PC_ITEMSTORE_AT=F  open item storage (as if from a dresser) at frame F
 * PC_KEYS=F:Key;...  press a key (SDL key name: Escape, Down, Right, Return...) at frame F, e.g. to drive
 *                    the pause menu: 9000:Escape;9010:Down;9020:Down;9030:Return
 * PC_BGITEM_SWAP_AT=F  swap the bg item actor out and back, as the boat to the island does halfway (trees
 *                    stay missing after: the real trip refills them when it moves to the island)
 * PC_CONST_EDIT_AT=F  open Celeste's telescope (PC_CONST_EDIT_MODE: 0 stargazing, 1 make one)
 * PC_WARP_AT=F       go through the museum's door at frame F (pc_devwarp.c)
 * PC_CLOCK_LAG_AT=F:S set the town's clock S seconds behind at frame F, as sleeping would (pc_os.c)
 * PC_VTX_DUMP_AT=F   log the bounds of the basement's and the house rooms' wall/floor models
 * PC_HOUSE_DUMP_AT=F log each house's size, basement and the basement's wallpaper/carpet state
 * PC_EVENT_DUMP_AT=F log the special event, Redd's hours today and his tent (m_event.c)
 * PC_TELEPORT_AT=F   put the player at PC_TELEPORT_POS=x,z[,degrees] at frame F (pc_devwarp.c)
 * PC_SFX_AT=F:name   play cf_sky/sfx/<name>.wav at frame F (pc_sfx.c)
 * PC_TYPE=F:text;...  type text at frame F (as a keyboard would into a text field)
 * PC_PAT_PAINT_AT=F  paint pattern slot 0 with stripes (an edit)
 * PC_PAT_DOOR_AT=F   make pattern slot 0 the player's house door design
 * PC_PAT_WEAR_AT=F   put on pattern slot 0;  PC_PAT_SWAP_AT=F  swap slot 0 with storage space 0
 * PC_PAT_DUMP_AT=F   write pattern_dump_<F>.txt: the player's pattern IDs (PC_PAT_DUMP2_AT a second one)
 * PC_MOUSE=F1-F2:X0,Y0>X1,Y1:B;...  scripted mouse in the pattern designer: moves from X0,Y0 to X1,Y1
 *                    (editor screen, 320x240) over frames F1-F2 holding SDL buttons B (1 left, 4 right)
 * PC_DESIGN_EDIT_AT=F open the pattern editor on slot 0 (nothing is saved unless it is confirmed)
 * PC_CTRLZ_AT=F      hold Ctrl+Z for frame F (pattern designer undo)
 * PC_NO_PAD=1       don't open gamepads (pc_main.c), so a test copy can't read the player's controller
 * Frames count controller reads (game logic frames), so captures land at the
 * same moment with or without --no-framelimit.
 */
#include "pc_platform.h"
#include "PR/gbi.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static u32 s_frame;

static int devtest_env_int(const char* name) {
    const char* env = getenv(name);
    return env != NULL ? atoi(env) : 0;
}

/* Next listed frame at or after `frame`, 0 if none */
static u32 devtest_next_shot(u32 frame) {
    const char* p = getenv("PC_SHOT");
    u32 best = 0;

    while (p != NULL && *p != '\0') {
        u32 f = (u32)atoi(p);
        if (f >= frame && (best == 0 || f < best)) {
            best = f;
        }
        p = strchr(p, ',');
        if (p != NULL) {
            p++;
        }
    }
    return best;
}

u32 pc_devtest_buttons(void) {
    static int period = -1;
    static int end = 0;

    if (period < 0) {
        period = devtest_env_int("PC_AUTO_A");
        end = devtest_env_int("PC_AUTO_A_END");
    }
    u32 held = 0;
    const char* p = getenv("PC_PRESS");
    int f1, f2;
    unsigned int mask;

    s_frame++;
    if (s_frame == (u32)devtest_env_int("PC_DESIGNER_AT")) {
        extern void pc_designer_request(void);
        pc_designer_request();
    }
    if (s_frame == (u32)devtest_env_int("PC_GROUND_AT") || s_frame == (u32)devtest_env_int("PC_GROUND2_AT")) {
        extern void pc_ground_pattern_request(int image);
        pc_ground_pattern_request(0);
    }
    if (s_frame == (u32)devtest_env_int("PC_GROUND_PROBE_AT") ||
        s_frame == (u32)devtest_env_int("PC_GROUND_PROBE2_AT")) {
        extern void pc_ground_pattern_probe(void* play, const char* path);
        extern void* gamePT;
        char path[40];

        snprintf(path, sizeof(path), "ground_probe_%u.txt", (unsigned)s_frame);
        pc_ground_pattern_probe(gamePT, path);
    }
    if (s_frame == (u32)devtest_env_int("PC_TILE_INFO_AT")) {
        extern void pc_ground_debug_toggle(void);
        pc_ground_debug_toggle();
    }
    if (s_frame == (u32)devtest_env_int("PC_ITEMSTORE_AT")) {
        extern void pc_item_storage_request(void);
        pc_item_storage_request();
    }
    if (s_frame == (u32)devtest_env_int("PC_PAT_PAINT_AT")) {
        extern void pc_pattern_dev_paint(void);
        pc_pattern_dev_paint();
    }
    if (s_frame == (u32)devtest_env_int("PC_DESIGN_EDIT_AT")) {
        extern void pc_designer_dev_edit(void);
        pc_designer_dev_edit();
    }
    if (s_frame == (u32)devtest_env_int("PC_PAT_DOOR_AT")) {
        extern void pc_pattern_dev_door(void);
        pc_pattern_dev_door();
    }
    if (s_frame == (u32)devtest_env_int("PC_PAT_WEAR_AT")) {
        extern void pc_pattern_dev_wear(void* play);
        extern void* gamePT;
        pc_pattern_dev_wear(gamePT);
    }
    if (s_frame == (u32)devtest_env_int("PC_PAT_SWAP_AT")) {
        extern void pc_pattern_dev_swap(void* play);
        extern void* gamePT;
        pc_pattern_dev_swap(gamePT);
    }
    if (s_frame == (u32)devtest_env_int("PC_PAT_DUMP_AT") || s_frame == (u32)devtest_env_int("PC_PAT_DUMP2_AT")) {
        extern void pc_pattern_dev_dump(const char* path);
        char path[40];

        snprintf(path, sizeof(path), "pattern_dump_%u.txt", (unsigned)s_frame);
        pc_pattern_dev_dump(path);
    }
    if (s_frame == (u32)devtest_env_int("PC_BGITEM_SWAP_AT")) {
        /* what the boat to the island does halfway (mBI_change_bg_item) */
        extern void mBI_change_bg_item(void* play);
        extern void* gamePT;
        mBI_change_bg_item(gamePT);
    }
    if (s_frame == (u32)devtest_env_int("PC_CONST_EDIT_AT")) {
        extern void pc_const_edit_open(int mode, const void* from);
        pc_const_edit_open(devtest_env_int("PC_CONST_EDIT_MODE"), NULL);
    }
    if (s_frame == (u32)devtest_env_int("PC_MSG_DUMP_AT")) {
        /* PC_MSG_DUMP_IDS=a,b,c logs those messages' bytes (with PC_MSG_DUMP=1) */
        extern void pc_msg_dev_dump(int index);
        const char* ids = getenv("PC_MSG_DUMP_IDS");

        while (ids != NULL && *ids != '\0') {
            pc_msg_dev_dump(atoi(ids));
            ids = strchr(ids, ',');
            if (ids != NULL) {
                ids++;
            }
        }
    }
    if (s_frame == (u32)devtest_env_int("PC_WARP_AT")) {
        extern void pc_dev_warp(void* play, int which);
        extern void* gamePT;
        pc_dev_warp(gamePT, devtest_env_int("PC_WARP_SCENE"));
    }
    {
        const char* lag = getenv("PC_CLOCK_LAG_AT");

        if (lag != NULL && strchr(lag, ':') != NULL && s_frame == (u32)atoi(lag)) {
            extern void pc_os_dev_clock_lag(int secs);
            pc_os_dev_clock_lag(atoi(strchr(lag, ':') + 1));
            printf("[PC] clock: dev lag %s s at frame %u\n", strchr(lag, ':') + 1, s_frame);
        }
    }
    if (s_frame == (u32)devtest_env_int("PC_VTX_DUMP_AT")) {
        extern Vtx rom_myhome_ug_v[], rom_myhome4_1_wall_v[], rom_myhome4_1_floor_v[], rom_myhome3_wall_v[];
        static const struct { const char* name; Vtx* v; int n; } models[] = {
            { "basement (ug)", rom_myhome_ug_v, 0x980 / 16 },
            { "LL1 wall", rom_myhome4_1_wall_v, 0x4F0 / 16 },
            { "LL1 floor", rom_myhome4_1_floor_v, 0x5B0 / 16 },
            { "L wall", rom_myhome3_wall_v, 0x4F0 / 16 },
        };
        int m, i;

        for (m = 0; m < 4; m++) {
            int n = models[m].n ? models[m].n : 64;
            int lo[3] = { 99999, 99999, 99999 }, hi[3] = { -99999, -99999, -99999 }, k;

            for (i = 0; i < n; i++) {
                for (k = 0; k < 3; k++) {
                    int c = models[m].v[i].v.ob[k];
                    lo[k] = c < lo[k] ? c : lo[k];
                    hi[k] = c > hi[k] ? c : hi[k];
                }
            }
            printf("[Vtx] %s: x %d..%d y %d..%d z %d..%d\n", models[m].name, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
        }
        fflush(stdout);
    }
    if (s_frame == (u32)devtest_env_int("PC_HOUSE_DUMP_AT")) {
        extern void pc_basement_dev_dump(void);
        pc_basement_dev_dump();
    }
    if (s_frame == (u32)devtest_env_int("PC_EVENT_DUMP_AT")) {
        extern void pc_mEv_dev_dump(void);
        pc_mEv_dev_dump();
    }
    if (s_frame == (u32)devtest_env_int("PC_TELEPORT_AT")) {
        extern void pc_dev_teleport(void* play);
        extern void* gamePT;
        pc_dev_teleport(gamePT);
    }
    if (s_frame == (u32)devtest_env_int("PC_LOBBY_DUMP_AT")) {
        /* Vtx: s16 ob[3], u16 flag, s16 tc[2], u8 cn[4] */
        extern void* pc_rom_museum1_vtx(int* count);
        int n, i;
        const s16* v = (const s16*)pc_rom_museum1_vtx(&n);

        for (i = 0; i < n; i++, v += 8) {
            printf("[LobbyVtx] %d %d %d %d %d %d\n", i, v[0], v[1], v[2], v[4], v[5]);
        }
        fflush(stdout);
    }
    if (s_frame == (u32)devtest_env_int("PC_STORAGE_AT")) {
        extern void pc_pattern_storage_request(void);
        pc_pattern_storage_request();
    }
    {
        const char* k = getenv("PC_KEYS");
        char name[32];
        int f;

        while (k != NULL && *k != '\0') {
            if (sscanf(k, "%d:%31[^;]", &f, name) == 2 && s_frame == (u32)f) {
                SDL_Event ev;

                memset(&ev, 0, sizeof(ev));
                ev.type = SDL_KEYDOWN;
                ev.key.keysym.sym = SDL_GetKeyFromName(name);
                ev.key.keysym.scancode = SDL_GetScancodeFromKey(ev.key.keysym.sym);
                SDL_PushEvent(&ev);
                ev.type = SDL_KEYUP;
                SDL_PushEvent(&ev);
            }
            k = strchr(k, ';');
            if (k != NULL) {
                k++;
            }
        }
    }
    {
        /* PC_TYPE=F:text;...  types text (SDL_TEXTINPUT) at frame F */
        const char* t = getenv("PC_TYPE");
        char text[32];
        int f;

        while (t != NULL && *t != '\0') {
            if (sscanf(t, "%d:%31[^;]", &f, text) == 2 && s_frame == (u32)f) {
                SDL_Event ev;

                memset(&ev, 0, sizeof(ev));
                ev.type = SDL_TEXTINPUT;
                snprintf(ev.text.text, sizeof(ev.text.text), "%s", text);
                SDL_PushEvent(&ev);
            }
            t = strchr(t, ';');
            if (t != NULL) {
                t++;
            }
        }
    }
    while (p != NULL && *p != '\0') {
        if (sscanf(p, "%d-%d:%x", &f1, &f2, &mask) == 3 && s_frame >= (u32)f1 && s_frame <= (u32)f2) {
            held |= mask;
        }
        p = strchr(p, ';');
        if (p != NULL) {
            p++;
        }
    }
    if (period <= 0 || (end > 0 && s_frame >= (u32)end)) {
        return held;
    }
    return held | ((s_frame % period) < 2 ? 0x0100 /* PAD_BUTTON_A */ : 0);
}

static void devtest_stick_env(const char* name, s8* x, s8* y);

void pc_devtest_stick(s8* x, s8* y) {
    devtest_stick_env("PC_STICK", x, y);
}

void pc_devtest_cstick(s8* x, s8* y) {
    devtest_stick_env("PC_CSTICK", x, y);
}

static void devtest_stick_env(const char* name, s8* x, s8* y) {
    const char* p = getenv(name);
    int f1, f2, sx, sy;

    while (p != NULL && *p != '\0') {
        if (sscanf(p, "%d-%d:%d:%d", &f1, &f2, &sx, &sy) == 4 && s_frame >= (u32)f1 && s_frame <= (u32)f2) {
            *x = (s8)sx;
            *y = (s8)sy;
            return;
        }
        p = strchr(p, ';');
        if (p != NULL) {
            p++;
        }
    }
}

/* PC_CTRLZ_AT: is Ctrl+Z held this frame? */
int pc_devtest_ctrl_z(void) {
    return s_frame == (u32)devtest_env_int("PC_CTRLZ_AT");
}

/* Scripted mouse (PC_MOUSE) for this frame, in window pixels. FALSE when not scripted. */
int pc_devtest_mouse(int* wx, int* wy, u32* buttons) {
    static int last_x = -1, last_y = -1;
    const char* p = getenv("PC_MOUSE");
    int f1, f2, x0, y0, x1, y1;
    unsigned int b;
    int ww = 640, wh = 480;

    if (p == NULL) {
        return FALSE;
    }
    SDL_GetWindowSize(g_pc_window, &ww, &wh);
    *buttons = 0;
    while (*p != '\0') {
        if (sscanf(p, "%d-%d:%d,%d>%d,%d:%u", &f1, &f2, &x0, &y0, &x1, &y1, &b) == 7 && s_frame >= (u32)f1 &&
            s_frame <= (u32)f2) {
            f32 t = f2 > f1 ? (f32)(s_frame - f1) / (f32)(f2 - f1) : 1.0f;
            f32 sx = x0 + (x1 - x0) * t;
            f32 sy = y0 + (y1 - y0) * t;

            /* 4:3 window: plain scale (wider ones pillarbox; see pc_designer_mouse.c_inc). */
            last_x = (int)(sx * ww / 320.0f);
            last_y = (int)(sy * wh / 240.0f);
            *buttons = b;
        }
        p = strchr(p, ';');
        if (p == NULL) {
            break;
        }
        p++;
    }
    if (last_x < 0) {
        return FALSE;
    }
    *wx = last_x;
    *wy = last_y;
    return TRUE;
}

/* Called just before the back buffer is presented. */
void pc_devtest_frame_end(void) {
    static int exit_at = -1;
    static int quit_at = -1;
    static u32 shot = 1;

    if (exit_at < 0) {
        exit_at = devtest_env_int("PC_EXIT_AT");
        quit_at = devtest_env_int("PC_QUIT_AT");
    }
    if (shot != 0) {
        shot = devtest_next_shot(shot);
    }

    if (shot != 0 && s_frame >= shot) {
        int w = g_pc_window_w;
        int h = g_pc_window_h;
        unsigned char* rgb = malloc((size_t)w * h * 3);
        char path[64];
        FILE* f;
        int y;

        if (rgb != NULL) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glReadBuffer(GL_BACK);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb);
            snprintf(path, sizeof(path), "devshot_%lu.ppm", (unsigned long)shot);
            f = fopen(path, "wb");
            if (f != NULL) {
                fprintf(f, "P6\n%d %d\n255\n", w, h);
                for (y = h - 1; y >= 0; y--) { /* GL rows are bottom-up */
                    fwrite(rgb + (size_t)y * w * 3, 1, (size_t)w * 3, f);
                }
                fclose(f);
                printf("[devtest] wrote %s\n", path);
            }
            free(rgb);
        }
        shot++;
    }

    {
        static int sfx_at = -1;
        static char sfx_name[32];

        if (sfx_at < 0) {
            const char* e = getenv("PC_SFX_AT");

            sfx_at = 0;
            if (e != NULL && sscanf(e, "%d:%31s", &sfx_at, sfx_name) != 2) {
                sfx_at = 0;
            }
        }
        if (sfx_at > 0 && s_frame >= (u32)sfx_at) {
            extern void pc_sfx_play(const char* name, float volume);

            sfx_at = 0;
            pc_sfx_play(sfx_name, 1.0f);
        }
    }
    if (exit_at > 0 && s_frame >= (u32)exit_at) {
        g_pc_running = 0;
    }
    if (quit_at > 0 && s_frame >= (u32)quit_at) {
        extern void pc_request_quit(void);
        quit_at = 0;
        pc_request_quit();
    }
}
