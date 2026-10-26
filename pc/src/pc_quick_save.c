/* pc_quick_save.c - Z+Start saves the town on the spot, without visiting the gyroid.
 * The same save also runs on a timer when autosave_minutes is set.
 *
 * The file written is a normal full save (reset code cleared), so quitting
 * afterwards does not summon Resetti. Nothing else about the session changes.
 *
 * While a building, mailbox or sign is on screen its spot on the map holds a
 * placeholder (DUMMY_*); the game puts the real item back (restore_fgdata_all)
 * only when leaving the scene, and the gyroid saves after that. Saving mid-scene
 * has to do the same or the town loses those things, so the save is written from
 * a restored copy and the live data is put back afterwards.
 *
 * Quitting (pause menu or closing the window) also saves when save_on_quit is
 * set: the request waits for the next play frame, saves if Z+Start could, then
 * exits. Outside a play scene nothing picks it up, so it exits a few frames later.
 */
#include "m_common_data.h"
#include "m_play.h"
#include "m_demo.h"
#include "m_event.h"
#include "m_font.h"
#include "m_player.h"
#include "m_player_lib.h"
#include "pc_menu_util.h"
#include "pc_settings.h"

#include <dolphin/os.h>
#include <stdlib.h>
#include <string.h>

#define QSAVE_MSG_FRAMES 120

extern int g_pc_paused;
extern int g_pc_running;
extern int pc_quick_save(void);
extern unsigned int SDL_GetTicks(void);

void pc_autosave_reset_timer(void);

static const char* l_qsave_msg = NULL;
static int l_qsave_timer = 0;

/* Autosave: milliseconds of unpaused play since the last save of any kind. */
static u32 l_autosave_elapsed = 0;
static u32 l_autosave_last_tick = 0;

/* Save-on-quit: frames left before quitting without a save, 0 = no quit asked. */
#define QUIT_WAIT_FRAMES 3
static int l_quit_wait = 0;

extern int aMR_pc_busy(void);

static int qsave_allowed(GAME_PLAY* play) {
    if (mEv_CheckTitleDemo() != mEv_TITLEDEMO_NONE || mEv_CheckFirstIntro()) {
        return FALSE;
    }

    /* Residents only: a visitor's data lives in the travel passport, not this town's save. */
    if (Common_Get(player_no) >= PLAYER_NUM || Now_Private == NULL) {
        return FALSE;
    }

    if (mDemo_CheckDemo() || play->submenu.open_flag) {
        return FALSE;
    }

    return TRUE;
}

/* Autosave is stricter than Z+Start: only while the player is simply walking
 * around, never mid-action, mid-transition or while anything is on screen. */
static int autosave_allowed(GAME_PLAY* play) {
    int index;

    if (!qsave_allowed(play) || aMR_pc_busy()) {
        return FALSE;
    }
    if (play->fb_wipe_mode != WIPE_MODE_NONE || play->fb_fade_type != FADE_TYPE_NONE) {
        return FALSE;
    }

    index = mPlib_get_player_actor_main_index((GAME*)play);
    return index == mPlayer_INDEX_WAIT || index == mPlayer_INDEX_WALK || index == mPlayer_INDEX_RUN ||
           index == mPlayer_INDEX_DASH;
}

static const char* do_quick_save(GAME_PLAY* play) {
    Save_t* live = &common_data.save.save;
    Save_t* snapshot = (Save_t*)malloc(sizeof(Save_t));
    int ok;

    if (snapshot == NULL) {
        return NULL;
    }
    memcpy(snapshot, live, sizeof(Save_t));
    restore_fgdata_all(play); /* real items back in place of on-screen placeholders */
    ok = pc_quick_save();
    memcpy(live, snapshot, sizeof(Save_t)); /* the scene carries on with its placeholders */
    free(snapshot);
    if (ok) {
        pc_autosave_reset_timer();
    }
    return ok ? "Saved" : NULL;
}

/* Any save restarts the autosave countdown (pc_m_card.c calls this too). */
void pc_autosave_reset_timer(void) {
    l_autosave_elapsed = 0;
}

static void autosave_tick(GAME_PLAY* play) {
    u32 now = SDL_GetTicks();
    u32 delta = (l_autosave_last_tick != 0) ? now - l_autosave_last_tick : 0;

    l_autosave_last_tick = now;
    if (g_pc_settings.autosave_minutes <= 0 || g_pc_paused) {
        return;
    }
    /* Long gaps (loading, a debugger, the window asleep) don't count as play. */
    if (delta < 1000) {
        l_autosave_elapsed += delta;
    }
    if (l_autosave_elapsed < (u32)g_pc_settings.autosave_minutes * 60u * 1000u || !autosave_allowed(play)) {
        return;
    }

    if (do_quick_save(play) != NULL) {
        l_qsave_msg = "Autosaved";
    } else {
        l_qsave_msg = "Autosave failed!";
        l_autosave_elapsed = 0; /* try again after another interval rather than every frame */
    }
    OSReport("[autosave] %s\n", l_qsave_msg);
    l_qsave_timer = QSAVE_MSG_FRAMES;
}

/* Quit the game, saving first if save_on_quit is set. Asking twice quits at once. */
void pc_request_quit(void) {
    if (!g_pc_settings.save_on_quit || l_quit_wait > 0) {
        g_pc_running = 0;
        return;
    }
    l_quit_wait = QUIT_WAIT_FRAMES;
}

/* Called once per frame from event polling: quits if no play frame took the request. */
void pc_quit_request_poll(void) {
    if (l_quit_wait > 0 && --l_quit_wait == 0) {
        OSReport("[save-on-quit] not in a play scene, quitting without saving\n");
        g_pc_running = 0;
    }
}

static void quit_save(GAME_PLAY* play) {
    static int settle = 0;
    const char* msg = "can't save here";

    if (aMR_pc_busy() && settle++ < 120) {
        l_quit_wait = QUIT_WAIT_FRAMES; /* a moment for the furniture to come to rest: back here next frame */
        return;
    }

    if (qsave_allowed(play)) {
        msg = do_quick_save(play) != NULL ? "saved" : "save failed";
    }
    OSReport("[save-on-quit] %s\n", msg);
    l_quit_wait = 0;
    g_pc_running = 0;
}

/* Called at the top of play_main, before the game reads this frame's input. */
void pc_quick_save_tick(GAME* game) {
    GAME_PLAY* play = (GAME_PLAY*)game;
    pad_t* pad = &game->pads[0];

    if (l_quit_wait > 0) {
        quit_save(play);
        return;
    }

    autosave_tick(play);

    if (g_pc_paused || !(pad->now.button & BUTTON_Z) || !(pad->on.button & BUTTON_START)) {
        return;
    }

    /* Swallow the Start press so the inventory doesn't open as well. */
    pad->on.button &= ~BUTTON_START;

    /* furniture mid-move or an item flying onto it would be saved nowhere */
    if (!qsave_allowed(play) || aMR_pc_busy()) {
        l_qsave_msg = "Can't save right now";
    } else {
        l_qsave_msg = do_quick_save(play);
        if (l_qsave_msg == NULL) {
            l_qsave_msg = "Save failed!";
        }
    }
    OSReport("[quick-save] %s\n", l_qsave_msg);
    l_qsave_timer = QSAVE_MSG_FRAMES;
}

/* A short notice in the same spot as "Saved" (pc_designer.c uses it too). */
void pc_quick_save_show_message(const char* msg) {
    l_qsave_msg = msg;
    l_qsave_timer = QSAVE_MSG_FRAMES * 2;
}

/* Called from graph_main next to the pause overlay. */
void pc_quick_save_draw(struct game_s* game) {
    extern void pc_sync_poll(void);
    int alpha;

    if (game != NULL && game->exec == play_main) {
        pc_sync_poll(); /* "Save Upload Complete" and the like (pc_sync.c); held until there's a town to show it on */
    }
    if (l_qsave_timer <= 0 || l_qsave_msg == NULL || game == NULL || game->graph == NULL) {
        return;
    }

    alpha = l_qsave_timer >= 30 ? 255 : (l_qsave_timer * 255) / 30;
    mFont_SetMatrix(game->graph, mFont_MODE_FONT);
    pc_menu_draw_centered(game, l_qsave_msg, 20.0f, 255, 255, 255, alpha, 1.0f);
    mFont_UnSetMatrix(game->graph, mFont_MODE_FONT);
    l_qsave_timer--;
}
