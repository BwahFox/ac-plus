/* pc_acplus_host.cpp - where ac-plus meets Nookery's Aurora host (aurora_main.cpp).
 *
 * - startup: the window handle our C code uses, the screensaver inhibit;
 * - before the game: the updater, the newer-save-format warning, --export-gci, City Folk's first-start
 *   conversion, City Folk's furniture;
 * - every frame: our keys (chat, F4 tile readout, F9 jank button), the pause menu's navigation, the mouse wheel,
 *   quitting through save-on-quit, the dev harness;
 * - a simple progress screen (Dear ImGui) for the long jobs before the game (download, conversion);
 * - the old pc_mouse.h calls, answered from Nookery's game pointer. */
#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <dolphin/gx.h>
#include <imgui.h>

#include <cstdio>
#include <cstring>

#include "game_editor.h"
#include "game_pointer.h"
#include "pc_mouse.h"
#include "pc_platform.h"

extern "C" {
SDL_Window* g_pc_window = nullptr;
s32 g_mouse_wheel_delta = 0;
int g_pc_mouse_wheel = 0; /* wheel steps since the pattern designer last looked (pc_designer_mouse.c_inc) */

bool ac_aurora_begin_frame();
void ac_aurora_end_frame();

void pc_inhibit_start(void);
int pc_updater_run(void);
void pc_ext_save_check_newer_format(void);
void pc_save_export_gci(const char* out_path);
int pc_cf_setup(void);
int pc_cf_ftr_count(void);
void pc_sync_finish(void);

int pc_pause_menu_handle_event(const SDL_Event* e);
int pc_pause_menu_quick_say(void);
void pc_pause_menu_toggle(void);
int pc_mp_menu_typing(void);
int pc_mp_menu_text_event(const SDL_Event* e);
int pc_settings_menu_capture_active(void);
int pc_settings_menu_handle_capture_event(const SDL_Event* e);
void pc_ground_debug_toggle(void);
void pc_request_quit(void);
void pc_quit_request_poll(void);
#ifdef PC_DEV
void pc_jank_press(void);
#endif
extern int g_pc_acplus_menu_open;
}

/* ===================================================================================================== startup */

extern "C" void pc_acplus_startup(void) {
    g_pc_window = aurora_get_window();
    if (g_pc_window != nullptr) {
        SDL_SetWindowTitle(g_pc_window, PC_WINDOW_TITLE);
    }
    pc_inhibit_start();
}

/* -1: go on with the game; otherwise the process ends with that code */
extern "C" int pc_acplus_before_game(int argc, char** argv) {
#ifdef ACPLUS_DESKTOP
    /* a newer release, offered and installed (pc_updater.c); the player starts it again after */
    if (pc_updater_run()) {
        return 0;
    }
#endif
    /* a save from a newer ac-plus file format: warned about before anything reads it (pc_ext_save.c) */
    pc_ext_save_check_newer_format();
    /* --export-gci <file>: the home town as a GameCube save (.gci), then quit (pc_m_card.c) */
    for (int a = 1; a + 1 < argc; a++) {
        if (std::strcmp(argv[a], "--export-gci") == 0) {
            pc_save_export_gci(argv[a + 1]);
            return 0;
        }
    }
    /* the City Folk files, made from the player's own City Folk disc on the first start (pc_cf_setup.c) */
    if (!pc_cf_setup()) {
        return 1;
    }
    /* City Folk's furniture, now: it adds itself to Nook's lists, which the first restock may want before anything
     * else asks about one of the pieces (pc_cf_ftr.c) */
    pc_cf_ftr_count();
    return -1;
}

extern "C" void pc_acplus_after_game(void) {
    /* an upload of the last save still on its way gets a moment to finish (pc_sync.c) */
    pc_sync_finish();
}

/* ============================================================================================ events, per frame */

/* An SDL event as Aurora hands it to the host (ac_aurora_begin_frame). Returns 1 when ac-plus used it up. */
extern "C" int pc_acplus_sdl_event(const SDL_Event* event) {
    if (pc_mp_menu_typing() && pc_mp_menu_text_event(event)) {
        return 1; /* typing an address (pc_mp_menu.c) */
    }
    if (pc_settings_menu_capture_active() &&
        (event->type == SDL_EVENT_TEXT_INPUT || event->type == SDL_EVENT_KEY_DOWN)) {
        pc_settings_menu_handle_capture_event(event); /* typing a server address */
        return 1;
    }
    switch (event->type) {
        case SDL_EVENT_MOUSE_WHEEL:
            g_mouse_wheel_delta += (s32)event->wheel.y;
            g_pc_mouse_wheel += (int)event->wheel.y;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event->key.repeat) {
                break;
            }
            if (event->key.key == SDLK_F4) {
                pc_ground_debug_toggle(); /* tile readout (pc_ground_pattern.c) */
                return 1;
            }
#ifdef PC_DEV
            if (event->key.key == SDLK_F9) {
                pc_jank_press(); /* the jank button (pc_jank.c) */
                return 1;
            }
#endif
            if (g_pc_acplus_menu_open) {
                return pc_pause_menu_handle_event(event);
            }
            /* the chat button, online (not while typing into the game's own keyboard) */
            if (event->key.scancode == SDL_SCANCODE_T && acgc_editor_state() != GAME_EDITOR_ACTIVE &&
                pc_pause_menu_quick_say()) {
                return 1;
            }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            if (g_pc_acplus_menu_open) {
                return pc_pause_menu_handle_event(event);
            }
            if (event->gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) {
                return pc_pause_menu_quick_say(); /* the chat button, online (LB: no GameCube button) */
            }
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (g_pc_acplus_menu_open) {
                return pc_pause_menu_handle_event(event);
            }
            break;
        default:
            break;
    }
    return 0;
}

extern "C" void pc_acplus_frame(void) {
    pc_quit_request_poll();
}

/* ==================================================================================== progress before the game */

/* One frame of a progress screen: what, and pct of it done (-1: no bar). Returns 0 when the window was closed. */
extern "C" int pc_acplus_progress(const char* what, int pct) {
    extern int g_pc_running;
    char title[160];

    if (g_pc_window != nullptr) {
        if (pct >= 0) {
            std::snprintf(title, sizeof(title), "ac-plus - %s: %d%%", what, pct);
        } else {
            std::snprintf(title, sizeof(title), "ac-plus - %s", what);
        }
        SDL_SetWindowTitle(g_pc_window, title);
    }
    if (!ac_aurora_begin_frame()) {
        return g_pc_running;
    }
    GXSetCopyClear(GXColor{ 26, 33, 26, 255 }, 0xFFFFFF);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = vp->Size.x * 0.6f;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + (vp->Size.x - w) / 2.0f, vp->Pos.y + vp->Size.y * 0.42f));
    ImGui::SetNextWindowSize(ImVec2(w, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.13f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.45f, 0.75f, 0.35f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.18f, 0.22f, 0.18f, 1.0f));
    ImGui::Begin("##acplus-progress", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoInputs);
    ImGui::TextUnformatted(what);
    if (pct >= 0) {
        ImGui::ProgressBar((float)pct / 100.0f, ImVec2(-1.0f, 0.0f));
    }
    ImGui::End();
    ImGui::PopStyleColor(3);
    ac_aurora_end_frame();
    return g_pc_running;
}

/* ============================================================================= pc_mouse.h, from the game pointer */

static const GamePointerFrame* pc_pointer() {
    return acgc_pointer_frame();
}

extern "C" u32 pc_mouse_button_held(void) {
    const GamePointerFrame* f = pc_pointer();
    return f != nullptr && f->owner == GAME_POINTER_MOUSE ? f->held : 0;
}

extern "C" u32 pc_mouse_button_pressed(void) {
    const GamePointerFrame* f = pc_pointer();
    return f != nullptr && f->owner == GAME_POINTER_MOUSE ? f->pressed : 0;
}

extern "C" u32 pc_mouse_button_released(void) {
    return 0;
}

extern "C" s32 pc_mouse_scroll_wheel(void) {
    s32 wheel = g_mouse_wheel_delta;
    g_mouse_wheel_delta = 0;
    return wheel;
}

/* In the game's 320x240 screen (with the widescreen margins), as the old pc_mouse_get_native_position */
extern "C" void pc_mouse_get_native_position(s32* x, s32* y) {
    const GamePointerFrame* f = pc_pointer();
    if (x != nullptr) *x = f != nullptr ? (s32)f->x : 0;
    if (y != nullptr) *y = f != nullptr ? (s32)f->y : 0;
}

/* In window pixels */
extern "C" void pc_mouse_get_position(s32* x, s32* y) {
    float fx = 0.0f, fy = 0.0f;
    SDL_GetMouseState(&fx, &fy);
    if (x != nullptr) *x = (s32)fx;
    if (y != nullptr) *y = (s32)fy;
}

extern "C" int pc_mouse_moved(void) {
    const GamePointerFrame* f = pc_pointer();
    return f != nullptr && f->owner == GAME_POINTER_MOUSE && f->moved;
}

extern "C" int pc_mouse_active(void) {
    const GamePointerFrame* f = pc_pointer();
    return f != nullptr && f->owner == GAME_POINTER_MOUSE && f->active;
}
