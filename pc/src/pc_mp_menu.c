/* pc_mp_menu.c - pause menu > Online Play: host this town, visit one by its address or invite code, browse the
 * server's open towns, leave; while hosting, each visitor's own page (trust, send home). pc_mp.c does it.
 * Typing the address works like the settings' text fields: A or Enter keeps it, B or Escape puts back what was
 * there, Ctrl+V pastes; on the Steam Deck, Steam + X brings up the keyboard. The chat button (Chat in the
 * keybindings: T, or LB on a controller) opens the menu straight onto the Say line and closes it once said.
 * Going online starts at the station: hosting and visiting are only offered when Porter opened the menu (his
 * "Online"); a visit then leaves on his train (pc_mp.c). Once online, the pause menu's page has it all. */
#include "pc_mp.h"

#include <stdio.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "game.h"
#include "pc_menu_util.h"
#include "pc_settings.h"

#include "mp/mp_link.h"

enum {
    ROW_HOST, ROW_ADDRESS, ROW_GO, ROW_BROWSE, ROW_STOP, ROW_BACK,
    ROW_TRUST, ROW_HOME, ROW_REFRESH, ROW_SAY, ROW_FRIENDS, ROW_PORTER,
    ROW_FRIEND = 40, /* + index in the list */
    ROW_VISITOR = 10, /* + slot */
    ROW_TOWN = 20,    /* + index in the list */
};
enum { VIEW_MAIN, VIEW_VISITOR, VIEW_BROWSE, VIEW_FRIENDS };
#define TOWNS_SHOWN 10

static int s_active;
static int s_view;
static int s_vslot; /* VIEW_VISITOR: whose page */
static int s_sel;
static int s_typing;
static char s_address[128];
static char s_before[128];
static char s_say[33];       /* a line to say (MP_CHAT_LEN) */
static char* s_buf;          /* what's being typed: s_address or s_say */
static size_t s_buf_size;
static int s_quick;          /* opened by the chat button: close the menu when the line is done */
static int s_porter;         /* opened by Porter: hosting and visiting offered */
static Uint32 s_quick_ticks; /* when, to drop the chat key's own letter */

int pc_mp_menu_active(void) {
    return s_active;
}

int pc_mp_menu_typing(void) {
    return s_active && s_typing;
}

/* Porter's "Online" (pc_pause_menu_open_online) */
void pc_mp_menu_enter_porter(void) {
    extern void pc_mp_menu_enter(void);
    pc_mp_menu_enter();
    s_porter = 1;
}

/* pc_mp.c: a visit started from here leaves on Porter's train */
int pc_mp_menu_from_porter(void) {
    return s_active && s_porter;
}

void pc_mp_menu_enter(void) {
    s_active = 1;
    s_quick = 0;
    s_porter = 0;
    s_view = VIEW_MAIN;
    s_sel = 0;
    s_typing = 0;
    snprintf(s_address, sizeof(s_address), "%s", g_pc_settings.mp_last_visit);
}

static int rows(int* out) {
    char name[16];
    int trusted, n = 0, i;

    if (s_view == VIEW_VISITOR && !pc_mp_visitor(s_vslot, name, &trusted)) {
        s_view = VIEW_MAIN; /* they left */
    }
    if (s_view == VIEW_VISITOR) {
        out[n++] = ROW_TRUST;
        out[n++] = ROW_HOME;
    } else if (s_view == VIEW_FRIENDS) {
        char town[16];
        for (i = 0; i < TOWNS_SHOWN && pc_mp_friend(i, town, name); i++) {
            out[n++] = ROW_FRIEND + i;
        }
    } else if (s_view == VIEW_BROWSE) {
        for (i = 0; i < pc_mp_browse_count() && i < TOWNS_SHOWN; i++) {
            out[n++] = ROW_TOWN + i;
        }
        out[n++] = ROW_REFRESH;
    } else if (pc_mp_hosting() || pc_mp_visiting()) {
        out[n++] = ROW_SAY;
        for (i = 1; i < PC_MP_SLOTS; i++) {
            if (pc_mp_visitor(i, name, &trusted)) {
                out[n++] = ROW_VISITOR + i;
            }
        }
        out[n++] = ROW_STOP;
    } else if (!s_porter) {
        out[n++] = ROW_PORTER; /* going online starts at the station */
    } else {
        out[n++] = ROW_HOST;
        out[n++] = ROW_ADDRESS;
        out[n++] = ROW_GO;
        out[n++] = ROW_FRIENDS;
        out[n++] = ROW_BROWSE;
    }
    out[n++] = ROW_BACK;
    return n;
}

static void go_view(int view) {
    if (s_view == VIEW_BROWSE && view != VIEW_BROWSE) {
        pc_mp_browse_stop();
    }
    s_view = view;
    s_sel = 0;
}

static void type_begin(char* buf, size_t size) {
    s_buf = buf;
    s_buf_size = size;
    snprintf(s_before, sizeof(s_before), "%s", buf);
    s_typing = 1;
    SDL_StartTextInput();
}

static void type_end(int keep) {
    if (s_buf == s_say) {
        if (keep && s_say[0] != '\0') {
            pc_mp_say(s_say); /* said: the line starts empty again */
        }
        s_say[0] = '\0';
    } else if (!keep) {
        snprintf(s_buf, s_buf_size, "%s", s_before);
    }
    s_typing = 0;
    SDL_StopTextInput();
    if (s_quick) {
        extern void pc_pause_menu_toggle(void);
        s_quick = 0;
        pc_pause_menu_toggle(); /* back to the game */
    }
}

/* The chat button (pc_pause_menu_quick_say): the Say line, typing at once */
void pc_mp_menu_quick_say(void) {
    pc_mp_menu_enter();
    s_quick = 1;
    s_quick_ticks = SDL_GetTicks();
    type_begin(s_say, sizeof(s_say));
}

/* an address is a host name or IP and maybe :port; a line said is plain letters, digits, spaces and punctuation */
static void type_add(const char* add) {
    size_t len = strlen(s_buf);
    const char* c;
    for (c = add; *c; c++) {
        int ok = s_buf == s_say ? (*c >= 0x20 && *c < 0x7F)
                                : (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ||
                                      *c == '.' || *c == '-' || *c == ':' || *c == '_';
        if (ok && len + 1 < s_buf_size) {
            s_buf[len++] = *c;
            s_buf[len] = '\0';
        }
    }
}

int pc_mp_menu_text_event(const SDL_Event* e) {
    size_t len;

    if (s_buf == NULL) {
        s_buf = s_address, s_buf_size = sizeof(s_address);
    }
    len = strlen(s_buf);

    switch (e->type) {
        case SDL_TEXTINPUT:
            /* a chat key that types (T) can still send its letter just after typing starts */
            if (s_quick && s_buf == s_say && s_say[0] == '\0' && SDL_GetTicks() - s_quick_ticks < 150 &&
                e->text.text[0] != '\0' && e->text.text[1] == '\0') {
                s_quick_ticks = 0;
                break;
            }
            type_add(e->text.text);
            break;
        case SDL_KEYDOWN:
            switch (e->key.keysym.sym) {
                case SDLK_BACKSPACE:
                    if (len > 0) {
                        s_buf[len - 1] = '\0';
                    }
                    break;
                case SDLK_v:
                    if (e->key.keysym.mod & KMOD_CTRL) {
                        char* clip = SDL_GetClipboardText();
                        if (clip != NULL) {
                            type_add(clip);
                            SDL_free(clip);
                        }
                    }
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    type_end(1);
                    break;
                case SDLK_ESCAPE:
                    type_end(0);
                    break;
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            if (e->cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                type_end(1);
            }
            if (e->cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                type_end(0);
            }
            break;
        default:
            return 0;
    }
    return 1;
}

/* actions as pc_pause_menu.c numbers them: 1 up, 2 down, 3 left, 4 right, 5 confirm, 6 cancel */
void pc_mp_menu_action(int act) {
    int list[24];
    int n = rows(list);
    char name[16], code[8], town[16];
    int trusted;

    if (s_sel >= n) {
        s_sel = 0;
    }
    switch (act) {
        case 1:
            s_sel = (s_sel + n - 1) % n;
            break;
        case 2:
            s_sel = (s_sel + 1) % n;
            break;
        case 5:
            switch (list[s_sel]) {
                case ROW_HOST:
                    pc_mp_host_start();
                    s_sel = 0;
                    break;
                case ROW_ADDRESS:
                    type_begin(s_address, sizeof(s_address));
                    break;
                case ROW_SAY:
                    type_begin(s_say, sizeof(s_say));
                    break;
                case ROW_GO:
                    if (s_address[0] != '\0') {
                        pc_mp_visit_start(s_address);
                        s_sel = 0;
                    }
                    break;
                case ROW_BROWSE:
                    go_view(VIEW_BROWSE);
                    pc_mp_browse_start();
                    break;
                case ROW_FRIENDS:
                    go_view(VIEW_FRIENDS);
                    break;
                case ROW_REFRESH:
                    pc_mp_browse_start();
                    s_sel = 0;
                    break;
                case ROW_STOP:
                    pc_mp_stop();
                    s_sel = 0;
                    break;
                case ROW_TRUST:
                    if (pc_mp_visitor(s_vslot, name, &trusted)) {
                        pc_mp_set_trusted(s_vslot, !trusted);
                    }
                    break;
                case ROW_HOME:
                    pc_mp_send_home(s_vslot);
                    go_view(VIEW_MAIN);
                    break;
                case ROW_BACK:
                    if (s_view != VIEW_MAIN) {
                        go_view(VIEW_MAIN);
                    } else {
                        s_active = 0;
                    }
                    break;
                default:
                    if (list[s_sel] >= ROW_FRIEND) {
                        if (pc_mp_visit_friend(list[s_sel] - ROW_FRIEND)) {
                            go_view(VIEW_MAIN);
                        }
                    } else if (list[s_sel] >= ROW_TOWN) {
                        if (pc_mp_browse_town(list[s_sel] - ROW_TOWN, code, town, name)) {
                            go_view(VIEW_MAIN);
                            snprintf(s_address, sizeof(s_address), "%s", code);
                            pc_mp_visit_start(code);
                        }
                    } else if (list[s_sel] >= ROW_VISITOR) {
                        s_vslot = list[s_sel] - ROW_VISITOR;
                        go_view(VIEW_VISITOR);
                    }
                    break;
            }
            break;
        case 3:
            /* left on a friend's town: forget it */
            if (s_view == VIEW_FRIENDS && list[s_sel] >= ROW_FRIEND) {
                pc_mp_forget_friend(list[s_sel] - ROW_FRIEND);
                s_sel = 0;
            }
            break;
        case 6:
            if (s_view != VIEW_MAIN) {
                go_view(VIEW_MAIN);
            } else {
                s_active = 0;
            }
            break;
    }
}

static void draw_info(struct game_s* game, f32 y) {
    char line[160];
    char fp[24];
    char ip[32];

    if (s_view == VIEW_FRIENDS) {
        char town[16], name[16];
        pc_menu_draw_centered(game, pc_mp_friend(0, town, name) ? "A: visit if it's open    Left: forget it"
                                                                  : "Towns you visit show up here.",
                              y, 230, 230, 230, 255, 0.8f);
        y += 13.0f;
    } else if (s_view == VIEW_BROWSE) {
        int count = pc_mp_browse_count();
        const char* what = count == -1 ? "Asking the server..."
                           : count == -2 ? "The server didn't answer."
                           : count == 0  ? "No towns are open to everyone right now."
                                         : NULL;
        if (what != NULL) {
            pc_menu_draw_centered(game, what, y, 230, 230, 230, 255, 0.85f);
            y += 15.0f;
        }
    } else if (s_view == VIEW_MAIN && pc_mp_hosting()) {
        if (pc_mp_invite_code()[0] != '\0') {
            snprintf(line, sizeof(line), "Invite code: %s", pc_mp_invite_code());
            pc_menu_draw_centered(game, line, y, 255, 255, 200, 255, 1.0f);
            y += 16.0f;
        }
        if (mp_local_ip(ip, sizeof(ip))) {
            snprintf(line, sizeof(line), "On your network: %s:%d    Visitors: %d of %d", ip, g_pc_settings.mp_port,
                     pc_mp_visitors(), PC_MP_SLOTS - 1);
        } else {
            snprintf(line, sizeof(line), "Port %d    Visitors: %d of %d", g_pc_settings.mp_port, pc_mp_visitors(),
                     PC_MP_SLOTS - 1);
        }
        pc_menu_draw_centered(game, line, y, 230, 230, 230, 255, 0.8f);
        y += 13.0f;
    }
    if (s_view == VIEW_MAIN) {
        pc_mp_fingerprint(fp);
        if (fp[0] != '\0') {
            snprintf(line, sizeof(line), "Your key: %s", fp);
            pc_menu_draw_centered(game, line, y, 180, 180, 180, 255, 0.8f);
            y += 13.0f;
        }
    }
    if (pc_mp_status()[0] != '\0') {
        pc_menu_draw_centered(game, pc_mp_status(), y + 4.0f, 255, 230, 160, 255, 0.85f);
    }
}

void pc_mp_menu_draw(struct game_s* game) {
    extern int pc_mp_train_ready(void);
    int list[24];
    int n;

    if (s_porter && !s_typing && pc_mp_train_ready()) {
        /* the host's town is here: back to the game, where Porter calls the train */
        extern void pc_pause_menu_toggle(void);
        s_porter = 0;
        pc_pause_menu_toggle();
        return;
    }
    n = rows(list);
    char line[160];
    char name[16], code[8], town[16];
    int trusted;
    f32 y = 94.0f;
    int i;

    switch (s_view) {
        case VIEW_VISITOR:
            pc_mp_visitor(s_vslot, name, &trusted);
            snprintf(line, sizeof(line), "- %s -", name);
            break;
        case VIEW_BROWSE:
            snprintf(line, sizeof(line), "- Open towns -");
            break;
        case VIEW_FRIENDS:
            snprintf(line, sizeof(line), "- Towns you've visited -");
            break;
        default:
            snprintf(line, sizeof(line), "- Online Play -");
            break;
    }
    pc_menu_draw_centered(game, line, 70.0f, 255, 255, 255, 255, 1.0f);
    for (i = 0; i < n; i++) {
        int r, g, b, a;
        int selected = i == s_sel;
        const char* label = "";
        f32 step = 16.0f;

        switch (list[i]) {
            case ROW_HOST:
                label = "Host my town";
                break;
            case ROW_PORTER:
                label = "Talk to Porter at the station to go online";
                break;
            case ROW_ADDRESS:
                snprintf(line, sizeof(line), "Visit: %s%s", s_address[0] ? s_address : (s_typing ? "" : "(an address or invite code)"),
                         s_typing && (SDL_GetTicks() / 400) % 2 ? "_" : "");
                label = line;
                break;
            case ROW_SAY:
                snprintf(line, sizeof(line), "Say: %s%s", s_say[0] || (s_typing && s_buf == s_say) ? s_say : "(type, then Enter)",
                         s_typing && s_buf == s_say && (SDL_GetTicks() / 400) % 2 ? "_" : "");
                label = line;
                break;
            case ROW_GO:
                label = "Go visit";
                break;
            case ROW_BROWSE:
                label = "Browse open towns";
                break;
            case ROW_FRIENDS:
                label = "Towns you've visited";
                break;
            case ROW_REFRESH:
                label = "Ask again";
                break;
            case ROW_STOP:
                label = pc_mp_hosting() ? "Stop hosting" : "Leave the visit";
                break;
            case ROW_TRUST:
                pc_mp_visitor(s_vslot, name, &trusted);
                label = trusted ? "Trusted: may dig, chop and pick flowers (A: make a guest)"
                                : "Guest: no digging or chopping (A: trust)";
                break;
            case ROW_HOME:
                pc_mp_visitor(s_vslot, name, &trusted);
                snprintf(line, sizeof(line), "Send %s home", name);
                label = line;
                break;
            case ROW_BACK:
                label = "Back";
                break;
            default:
                if (list[i] >= ROW_FRIEND) {
                    pc_mp_friend(list[i] - ROW_FRIEND, town, name);
                    snprintf(line, sizeof(line), "%s (%s)", town, name);
                    step = 15.0f;
                } else if (list[i] >= ROW_TOWN) {
                    pc_mp_browse_town(list[i] - ROW_TOWN, code, town, name);
                    snprintf(line, sizeof(line), "%s (%s)   %s", town, name, code);
                    step = 15.0f;
                } else {
                    pc_mp_visitor(list[i] - ROW_VISITOR, name, &trusted);
                    snprintf(line, sizeof(line), "%s - %s", name, trusted ? "trusted" : "guest");
                }
                label = line;
                break;
        }
        pc_menu_row_colors(selected, &r, &g, &b, &a);
        pc_menu_draw_centered(game, label, y, r, g, b, a, selected ? PC_MENU_SCALE_SELECTED : 1.0f);
        y += step;
    }
    draw_info(game, y + 8.0f);
}
