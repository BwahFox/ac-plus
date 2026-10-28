/* pc_jank.c - dev builds only (PC_DEV): F9 is the "jank button". When something looks or feels wrong, press it
 * and keep playing: jank/<date>_<time>/ gets
 *   shot.png   the picture on screen
 *   state.txt  the frame, the clocks, the scene, the player, the villagers and online play
 *   log.txt    the end of the game's log (the last 256 KB of PC_LOG_FILE; dev builds log to dev.log without one,
 *              except with --verbose, whose log goes to the console)
 * The window title says "jank #N saved" for a moment. Nothing here is in a release build. */
#ifdef PC_DEV
#include "pc_platform.h"
#include "m_play.h"
#include "m_common_data.h"
#include "m_private.h"
#include "m_field_info.h"
#include "m_npc.h"
#include "m_player_lib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h> /* _mkdir */
#else
#include <sys/stat.h>
#endif

#define JANK_LOG_TAIL (256 * 1024)

extern int stbi_write_png(const char* filename, int w, int h, int comp, const void* data, int stride_in_bytes);

extern u32 pc_devtest_frame(void);
extern int pc_museum_rooms_scene(void); /* pc_museum_rooms.c */
extern const char* pc_mp_status(void); /* pc_mp.c */
extern int pc_mp_hosting(void);
extern int pc_mp_visiting(void);
extern int pc_mp_visitors(void);
extern int pc_mp_my_slot(void);

static int l_wanted;     /* F9 was pressed: save at the end of this frame, when the picture is drawn */
static int l_count;      /* this session's saves, for the title */
static int l_title_left; /* frames the title keeps saying so */

static void jank_mkdir(const char* path) {
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

/* The picture on screen now (the back buffer, just before it's presented) */
static void jank_shot(const char* path) {
    int w = g_pc_window_w, h = g_pc_window_h, y;
    unsigned char* rgb = malloc((size_t)w * h * 3);
    unsigned char* row = malloc((size_t)w * 3);

    if (rgb != NULL && row != NULL) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb);
        for (y = 0; y < h / 2; y++) { /* GL rows are bottom-up */
            memcpy(row, rgb + (size_t)y * w * 3, (size_t)w * 3);
            memcpy(rgb + (size_t)y * w * 3, rgb + (size_t)(h - 1 - y) * w * 3, (size_t)w * 3);
            memcpy(rgb + (size_t)(h - 1 - y) * w * 3, row, (size_t)w * 3);
        }
        stbi_write_png(path, w, h, 3, rgb, w * 3);
    }
    free(row);
    free(rgb);
}

static void jank_state(FILE* f) {
    GAME_PLAY* play = (GAME_PLAY*)gamePT;
    const lbRTC_time_c* rtc = &Common_Get(time.rtc_time);
    int in_field = play != NULL && play->game.exec == play_main && mFI_CheckFieldData();
    int i;

    fprintf(f, "frame %lu\n", (unsigned long)pc_devtest_frame());
    fprintf(f, "town clock %04u-%02u-%02u %02u:%02u:%02u\n", rtc->year, rtc->month, rtc->day, rtc->hour, rtc->min,
            rtc->sec);
    fprintf(f, "scene %d (saved scene %d)\n", pc_museum_rooms_scene(), Save_Get(scene_no));

    if (in_field && GET_PLAYER_ACTOR(play) != NULL) {
        ACTOR* p = &GET_PLAYER_ACTOR(play)->actor_class;
        fprintf(f, "player at %.1f, %.1f, %.1f facing %d\n", p->world.position.x, p->world.position.y,
                p->world.position.z, (int)p->shape_info.rotation.y);
    } else {
        fprintf(f, "player: not in a play scene\n");
    }
    if (Now_Private != NULL) {
        fprintf(f, "wallet %u, bank %u\npockets", Now_Private->inventory.wallet, Now_Private->bank_account);
        for (i = 0; i < mPr_POCKETS_SLOT_COUNT; i++) {
            fprintf(f, " %04X", Now_Private->inventory.pockets[i]);
        }
        fprintf(f, "\n");
    }

    fprintf(f, "\nvillagers (index id home? | where the town thinks | actor here)\n");
    for (i = 0; i < ANIMAL_NUM_MAX; i++) {
        Animal_c* an = &Save_Get(animals[i]);
        const xyz_t* np = &Common_Get(npclist)[i].position;
        ACTOR* a = NULL;

        if (an->id.npc_id == EMPTY_NO) {
            continue;
        }
        if (in_field) {
            for (a = play->actor_info.list[ACTOR_PART_NPC].actor; a != NULL; a = a->next_actor) {
                if (a->npc_id == an->id.npc_id) {
                    break;
                }
            }
        }
        fprintf(f, "%2d %04X %s | %.0f, %.0f | ", i, an->id.npc_id, an->is_home ? "home" : "out ", np->x, np->z);
        if (a != NULL) {
            fprintf(f, "%.0f, %.0f facing %d\n", a->world.position.x, a->world.position.z,
                    (int)a->shape_info.rotation.y);
        } else {
            fprintf(f, "-\n");
        }
    }

    fprintf(f, "\nonline: %s%s, slot %d, %d visitors\nstatus: %s\n",
            pc_mp_hosting() ? "hosting" : pc_mp_visiting() ? "visiting" : "off",
            pc_mp_hosting() || pc_mp_visiting() ? "" : " (not in a visit)", pc_mp_my_slot(), pc_mp_visitors(),
            pc_mp_status() != NULL ? pc_mp_status() : "");
}

static void jank_log_tail(const char* path) {
    const char* log = getenv("PC_LOG_FILE");
    FILE *in, *out;
    char buf[8192];
    long size;
    size_t n;

    if (log == NULL || log[0] == '\0' || (in = fopen(log, "rb")) == NULL) {
        return;
    }
    fseek(in, 0, SEEK_END);
    size = ftell(in);
    fseek(in, size > JANK_LOG_TAIL ? size - JANK_LOG_TAIL : 0, SEEK_SET);
    if ((out = fopen(path, "wb")) != NULL) {
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
            fwrite(buf, 1, n, out);
        }
        fclose(out);
    }
    fclose(in);
}

static void jank_save(void) {
    char dir[64], path[96], title[64];
    time_t now = time(NULL);
    FILE* f;

    jank_mkdir("jank");
    strftime(path, sizeof(path), "%Y-%m-%d_%H-%M-%S", localtime(&now));
    snprintf(dir, sizeof(dir), "jank/%s", path);
    jank_mkdir(dir);

    printf("[jank] #%d at frame %lu: %s\n", l_count + 1, (unsigned long)pc_devtest_frame(), dir);
    fflush(stdout); /* so the log's copy has this line and everything before it */
    fflush(stderr);

    snprintf(path, sizeof(path), "%s/shot.png", dir);
    jank_shot(path);
    snprintf(path, sizeof(path), "%s/state.txt", dir);
    if ((f = fopen(path, "w")) != NULL) {
        jank_state(f);
        fclose(f);
    }
    snprintf(path, sizeof(path), "%s/log.txt", dir);
    jank_log_tail(path);

    l_count++;
    if (g_pc_window != NULL) {
        snprintf(title, sizeof(title), "%s [jank #%d saved]", PC_WINDOW_TITLE, l_count);
        SDL_SetWindowTitle(g_pc_window, title);
        l_title_left = 120;
    }
}

void pc_jank_press(void) {
    l_wanted = 1;
}

/* Just before the picture is presented (pc_devtest_frame_end) */
void pc_jank_frame_end(void) {
    if (l_wanted) {
        l_wanted = 0;
        jank_save();
    } else if (l_title_left > 0 && --l_title_left == 0 && g_pc_window != NULL) {
        extern int g_pc_speedhack_enabled;
        SDL_SetWindowTitle(g_pc_window, g_pc_speedhack_enabled ? "Animal Crossing Plus [5x]" : PC_WINDOW_TITLE);
    }
}
#endif
