/* ui/acplus.cpp - the ac-plus window in Nookery's menu (Esc -> ac-plus): what was ac-plus's in-game pause menu.
 *
 * Town: the pattern designer, pattern storage, pattern QR codes and Online Play (the last two still drawn by the
 * game: pc_pattern_qr.c, pc_mp_menu.c). The rest are ac-plus's own settings, kept in ac-plus.ini; a change is
 * saved as it's made, as Nookery's own settings are. */
#include "acgc/ui/acplus.hpp"

#include "acgc/ui/bool_button.hpp"
#include "acgc/ui/number_button.hpp"
#include "acgc/ui/scrolling_background.hpp"
#include "acgc/ui/menu_bar.hpp"

#include <borealis/ui/dropdown_button.hpp>
#include <borealis/ui/pane.hpp>
#include <borealis/ui/ui.hpp>
#include <borealis/ui/string_button.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

extern "C" {
#include "pc_settings.h"

int pc_designer_unlocked(void);
void pc_designer_request(void);
int pc_pattern_storage_available(void);
void pc_pattern_storage_request(void);
int pc_qr_menu_available(void);
void pc_pause_menu_open_page(int which);
void pc_acplus_online_open(int porter); /* ui/acplus_online.cpp */
}

namespace acgc::ui {
using namespace borealis::ui;

namespace {

/* Back to the game, then `then` (the game picks it up on its next frame) */
void acplus_leave_menus(Document& window, void (*then)(void)) {
    window.hide(true);
    MenuBar::hide_instance();
    then();
}

void open_qr(void) {
    pc_pause_menu_open_page(0);
}

void open_online(void) {
    pc_acplus_online_open(0);
}

/* A setting changed: kept at once */
void saved() {
    pc_settings_save();
}

template <size_t N>
void set_text(char (&field)[N], const Rml::String& value) {
    std::snprintf(field, N, "%s", value.c_str());
    saved();
}

void add_bool(Pane& left, Pane& right, int& field, Rml::String label, Rml::String help) {
    left.register_control(left.add_child<BoolButton>(BoolButton::Props{
                              .key = std::move(label),
                              .getValue = [&field] { return field != 0; },
                              .setValue = [&field](bool on) { field = on ? 1 : 0, saved(); },
                          }),
                          right, [help = std::move(help)](Pane& pane) { pane.add_text(help); });
}

template <size_t N>
void add_string(Pane& left, Pane& right, char (&field)[N], Rml::String label, Rml::String help) {
    left.register_control(left.add_child<StringButton>(StringButton::Props{
                              .key = std::move(label),
                              .getValue = [&field] { return Rml::String(field); },
                              .setValue = [&field](Rml::String value) { set_text(field, value); },
                              .maxLength = static_cast<int>(N) - 1,
                          }),
                          right, [help = std::move(help)](Pane& pane) { pane.add_text(help); });
}

/* The music packs: the folders in music/ (pc_music.c) */
std::vector<std::string> music_packs() {
    std::vector<std::string> packs;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("music", ec)) {
        if (entry.is_directory(ec)) {
            packs.push_back(entry.path().filename().string());
        }
    }
    std::sort(packs.begin(), packs.end());
    return packs;
}

constexpr std::array kAutosaveMinutes = { 0, 5, 10, 15, 30, 60 };

} // namespace

AcplusWindow::AcplusWindow()
    : Window({ .styleSheets = { "res/rml/scrolling_background.rcss" }, .background = scrolling_background }) {
    add_tab("Town", [this](Rml::Element* content) {
        auto& left = add_child<Pane>(content, Pane::Type::Controlled);
        auto& right = add_child<Pane>(content, Pane::Type::Uncontrolled);

        left.add_section("Patterns");
        if (pc_designer_unlocked()) {
            left.register_control(
                left.add_select_button({ .key = "Pattern designer", .getValue = [] { return Rml::String(); } })
                    .on_pressed([this] { acplus_leave_menus(*this, pc_designer_request); }),
                right, [](Pane& pane) { pane.add_text("Draw a design, as at Mabel's, wherever you are."); });
        }
        if (pc_pattern_storage_available()) {
            left.register_control(
                left.add_select_button({ .key = "Pattern storage", .getValue = [] { return Rml::String(); } })
                    .on_pressed([this] { acplus_leave_menus(*this, pc_pattern_storage_request); }),
                right, [](Pane& pane) {
                    pane.add_text("Your saved designs, as Mabel keeps them on the memory card: put them on, "
                                  "swap them in and out of your pockets' patterns.");
                });
        }
        if (pc_qr_menu_available()) {
            left.register_control(
                left.add_select_button({ .key = "Pattern QR codes", .getValue = [] { return Rml::String(); } })
                    .on_pressed([this] { acplus_leave_menus(*this, open_qr); }),
                right, [](Pane& pane) {
                    pane.add_text("Share designs as QR codes (New Leaf's format): export them to qr/export, or "
                                  "bring in pictures from qr/import.");
                });
        }

        left.add_section("Mouse");
        add_bool(left, right, g_pc_settings.mouse_hold_pockets, "Hold right-click for pockets",
                 "Holding the right mouse button with empty hands opens your pockets. Off: right-click is only the "
                 "A button.");

        left.add_section("Online");
        left.register_control(
            left.add_select_button({ .key = "Online play", .getValue = [] { return Rml::String(); } })
                .on_pressed([] { open_online(); }),
            right, [](Pane& pane) {
                pane.add_text("Host your town, or visit a friend's (with an invite code or their address). "
                              "Porter at the station takes you too.");
            });
    });

    add_tab("City Folk", [this](Rml::Element* content) {
        auto& left = add_child<Pane>(content, Pane::Type::Controlled);
        auto& right = add_child<Pane>(content, Pane::Type::Uncontrolled);

        left.add_section("World");
        left.register_control(left.add_child<NumberButton>(NumberButton::Props{
                                  .key = "Curved world",
                                  .getValue = [] { return g_pc_settings.world_curve; },
                                  .setValue = [](int v) { g_pc_settings.world_curve = v, saved(); },
                                  .min = 0,
                                  .max = 300,
                                  .step = 25,
                                  .suffix = "%",
                              }),
                              right, [](Pane& pane) {
                                  pane.add_text("How much the land rolls away like a log outdoors, as in City Folk, "
                                                "with its sky. 0% is the GameCube's flat world.");
                              });
        add_bool(left, right, g_pc_settings.cf_weather, "City Folk weather",
                 "The weather can change during the day, with overcast skies and rainbows after rain. Off: one "
                 "weather all day, as on the GameCube.");
        add_bool(left, right, g_pc_settings.cf_tv, "TV programs",
                 "TVs show City Folk's programs through the day, with their sound. Off: the GameCube's static.");
        add_bool(left, right, g_pc_settings.tanning, "Tanning",
                 "Summer sun tans your skin, as on the GameCube. Off: it keeps the tone you picked.");
    });

    add_tab("Music", [this](Rml::Element* content) {
        auto& left = add_child<Pane>(content, Pane::Type::Controlled);
        auto& right = add_child<Pane>(content, Pane::Type::Uncontrolled);
        auto packs = std::make_shared<std::vector<std::string>>(music_packs());

        left.add_section("Custom music");
        {
            std::vector<DropdownButton::Option> options{ { "None" } };
            for (const auto& pack : *packs) {
                options.push_back({ pack });
            }
            left.register_control(
                left.add_child<DropdownButton>(DropdownButton::Props{
                    .key = "Music pack",
                    .options = std::move(options),
                    .getValue =
                        [packs] {
                            const auto it =
                                std::find(packs->begin(), packs->end(), std::string(g_pc_settings.music_pack));
                            return it == packs->end() ? 0 : static_cast<int>(it - packs->begin()) + 1;
                        },
                    .setValue =
                        [packs](int i) {
                            set_text(g_pc_settings.music_pack,
                                     i <= 0 || i > static_cast<int>(packs->size()) ? Rml::String() : (*packs)[i - 1]);
                        },
                }),
                right, [](Pane& pane) {
                    pane.add_text("Your own hourly music: a folder in music/ with hourly/00 to 23 (and rain/ and "
                                  "snow/ if you like). docs/MUSIC.md explains it.");
                });
        }
        add_bool(left, right, g_pc_settings.music_custom, "Play the music pack",
                 "Off: the game's own music, keeping the pack chosen above.");
        left.register_control(left.add_child<NumberButton>(NumberButton::Props{
                                  .key = "Music pack volume",
                                  .getValue = [] { return g_pc_settings.music_volume; },
                                  .setValue = [](int v) { g_pc_settings.music_volume = v, saved(); },
                                  .min = 0,
                                  .max = 100,
                                  .step = 5,
                                  .suffix = "%",
                              }),
                              right, [](Pane& pane) { pane.add_text("How loud the music pack plays."); });
        left.add_section("The game's music");
        left.register_control(left.add_child<DropdownButton>(DropdownButton::Props{
                                  .key = "Rain music",
                                  .options = { { "The rain song" }, { "Cherry blossom arrangement" } },
                                  .getValue = [] { return g_pc_settings.rain_spring ? 1 : 0; },
                                  .setValue = [](int i) { g_pc_settings.rain_spring = i == 1, saved(); },
                              }),
                              right, [](Pane& pane) {
                                  pane.add_text("What plays while it rains: the GameCube's rain song, or the "
                                                "hour's spring (cherry blossom) arrangement.");
                              });
    });

    add_tab("Saving", [this](Rml::Element* content) {
        auto& left = add_child<Pane>(content, Pane::Type::Controlled);
        auto& right = add_child<Pane>(content, Pane::Type::Uncontrolled);

        left.add_section("Saving");
        {
            std::vector<DropdownButton::Option> options;
            for (int minutes : kAutosaveMinutes) {
                options.push_back({ minutes == 0 ? Rml::String("Off") : std::to_string(minutes) + " minutes" });
            }
            left.register_control(
                left.add_child<DropdownButton>(DropdownButton::Props{
                    .key = "Autosave",
                    .options = std::move(options),
                    .getValue =
                        [] {
                            const auto it = std::find(kAutosaveMinutes.begin(), kAutosaveMinutes.end(),
                                                      g_pc_settings.autosave_minutes);
                            return it == kAutosaveMinutes.end() ? 0 : static_cast<int>(it - kAutosaveMinutes.begin());
                        },
                    .setValue = [](int i) { g_pc_settings.autosave_minutes = kAutosaveMinutes[i], saved(); },
                }),
                right, [](Pane& pane) {
                    pane.add_text("Saves by itself after this much play, while you're walking around outdoors.");
                });
        }
        add_bool(left, right, g_pc_settings.save_on_quit, "Save on quit",
                 "Quitting (or closing the window) saves first, where Z+Start could save.");
        add_bool(left, right, g_pc_settings.quick_load, "Quick load",
                 "A returning player starts straight in town, without the arrival scene.");

        left.add_section("In-game sync (ac-plus-server)");
        add_bool(left, right, g_pc_settings.sync_enabled, "In-game sync",
                 "Keeps your town in sync with an ac-plus-server: a newer save is downloaded at startup, every "
                 "save is uploaded. server/README.md explains the server.");
        add_string(left, right, g_pc_settings.sync_url, "Sync server",
                   "The server: host:port, or a full https:// address.");
        add_string(left, right, g_pc_settings.sync_token, "Sync token", "The token from the server's add-token.");

        left.add_section("SSH sync (ac-sync.sh)");
        add_bool(left, right, g_pc_settings.server_sync, "SSH sync",
                 "When the game is started through ac-sync.sh: the town comes from your server before playing and "
                 "goes back after. Needs ssh login with a key.");
        add_string(left, right, g_pc_settings.server_sync_host, "SSH server",
                   "Where to: user@host (or a Host from ~/.ssh/config).");
    });

    add_tab("Online", [this](Rml::Element* content) {
        auto& left = add_child<Pane>(content, Pane::Type::Controlled);
        auto& right = add_child<Pane>(content, Pane::Type::Uncontrolled);

        left.add_section("Online visits");
        add_string(left, right, g_pc_settings.mp_server, "Invite code server",
                   "An ac-plus-server for invite codes (host[:port]). With one, hosting gives you a code to hand "
                   "out, and most networks need no router setup. Empty: visits by address only.");
        add_bool(left, right, g_pc_settings.mp_public, "Open to everyone",
                 "While you host, your town is listed on that server for anyone to visit.");
        left.register_control(left.add_child<NumberButton>(NumberButton::Props{
                                  .key = "Hosting port",
                                  .getValue = [] { return g_pc_settings.mp_port; },
                                  .setValue = [](int v) { g_pc_settings.mp_port = v, saved(); },
                                  .min = 1024,
                                  .max = 65535,
                              }),
                              right, [](Pane& pane) {
                                  pane.add_text("The UDP port your town is hosted on. Forward it on your router "
                                                "for visitors from outside your network without an invite code.");
                              });
    });
}

} // namespace acgc::ui

/* dev (pc_devtest.c PC_CTL "acplusui"): the ac-plus window, as the menu bar's tab opens it */
extern "C" void pc_acplus_ui_open(void) {
    acgc::ui::MenuBar::show_instance();
    borealis::ui::push_document(std::make_unique<acgc::ui::AcplusWindow>());
}
