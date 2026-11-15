/* ui/acplus_online.cpp - Online Play in Nookery's menu style (what pc_mp_menu.c drew in the game before).
 *
 * Opened from the ac-plus window or by Porter ("Online" at the station). Going online starts at the station: hosting
 * and visiting are offered only when Porter opened it, and a visit then leaves on his train (pc_mp.c asks
 * pc_mp_menu_from_porter). Once online: a line to say, the visitors (trust, guest, send home) and leaving.
 * pc_mp.c does the work; this only shows it. The quick chat line (T / LB) stays the game's own (pc_mp_menu.c). */
#include "acgc/ui/acplus.hpp"

#include "acgc/ui/menu_bar.hpp"
#include "acgc/ui/scrolling_background.hpp"

#include <borealis/ui/component.hpp>
#include <borealis/ui/modal.hpp>
#include <borealis/ui/pane.hpp>
#include <borealis/ui/string_button.hpp>
#include <borealis/ui/ui.hpp>

#include <cstdio>
#include <string>

extern "C" {
#include "pc_settings.h"

#define PC_MP_SLOTS_UI 4 /* pc_mp.h PC_MP_SLOTS (that header pulls in game headers) */
int pc_mp_host_start(void);
int pc_mp_visit_start(const char* address);
void pc_mp_stop(void);
int pc_mp_hosting(void);
int pc_mp_visiting(void);
int pc_mp_visitors(void);
const char* pc_mp_invite_code(void);
int pc_mp_visitor(int slot, char* name, int* trusted);
void pc_mp_set_trusted(int slot, int on);
void pc_mp_send_home(int slot);
int pc_mp_browse_start(void);
void pc_mp_browse_stop(void);
int pc_mp_browse_count(void);
int pc_mp_browse_town(int i, char* code, char* town, char* name);
int pc_mp_friend(int i, char* town, char* name);
int pc_mp_visit_friend(int i);
void pc_mp_forget_friend(int i);
int pc_mp_say(const char* text);
const char* pc_mp_status(void);
void pc_mp_fingerprint(char* out);
int mp_local_ip(char* out, int n);
int pc_mp_train_ready(void);
void pc_mp_menu_ui_open(int porter); /* pc_mp_menu.c: Porter's state, for pc_mp.c */
void pc_mp_menu_ui_close(void);
int pc_mp_menu_from_porter(void);
}

namespace acgc::ui {
using namespace borealis::ui;

namespace {

constexpr int kTownsShown = 10;

/* Text kept up to date every frame (status lines) */
class LiveText final : public Component {
public:
    LiveText(Rml::Element* parent, std::function<Rml::String()> text)
        : Component(append(parent, "div")), mText(std::move(text)) {}

    void update() override {
        Rml::String rml = mText();
        if (rml != mCurrent) {
            mRoot->SetInnerRML(rml);
            mCurrent = std::move(rml);
        }
        Component::update();
    }

private:
    std::function<Rml::String()> mText;
    Rml::String mCurrent;
};

Rml::String status_rml() {
    Rml::String rml;
    char line[160], ip[32], fp[24];

    if (pc_mp_hosting()) {
        if (pc_mp_invite_code()[0] != '\0') {
            rml += "Invite code: <b>" + escape(pc_mp_invite_code()) + "</b><br/>";
        }
        if (mp_local_ip(ip, sizeof(ip))) {
            std::snprintf(line, sizeof(line), "On your network: %s:%d", ip, g_pc_settings.mp_port);
            rml += escape(line) + "<br/>";
        }
        std::snprintf(line, sizeof(line), "Visitors: %d of %d", pc_mp_visitors(), PC_MP_SLOTS_UI - 1);
        rml += escape(line) + "<br/>";
    } else if (pc_mp_visiting()) {
        rml += "Visiting.<br/>";
    }
    pc_mp_fingerprint(fp);
    if (fp[0] != '\0') {
        rml += "Your key: " + escape(fp) + "<br/>";
    }
    if (pc_mp_status()[0] != '\0') {
        rml += "<br/>" + escape(pc_mp_status());
    }
    return rml;
}

/* What the window shows depends on these; a change rebuilds it */
std::string layout_key() {
    std::string key = std::to_string(pc_mp_hosting()) + std::to_string(pc_mp_visiting()) +
                      std::to_string(pc_mp_menu_from_porter()) + "/" + std::to_string(pc_mp_browse_count()) + "/";
    char name[16], town[16];
    int trusted;
    for (int i = 1; i < PC_MP_SLOTS_UI; i++) {
        if (pc_mp_visitor(i, name, &trusted)) {
            key += std::to_string(i) + name + (trusted ? "t" : "g");
        }
    }
    for (int i = 0; i < kTownsShown && pc_mp_friend(i, town, name); i++) {
        key += town;
    }
    return key;
}

void ask(Rml::String title, Rml::String body, std::vector<ModalAction> actions) {
    push_document(std::make_unique<Modal>(Modal::Props{
        .title = escape(title),
        .bodyRml = escape(body),
        .actions = std::move(actions),
        .onDismiss = [](Modal& modal) { modal.pop(); },
        .variant = "light",
    }));
    if (auto* doc = top_document()) {
        doc->focus();
    }
}

} // namespace

class AcplusOnlineWindow final : public Window {
public:
    explicit AcplusOnlineWindow(bool porter)
        : Window({ .styleSheets = { "res/rml/scrolling_background.rcss" }, .background = scrolling_background }) {
        pc_mp_menu_ui_open(porter ? 1 : 0);
        mKey = layout_key();
        add_tab("Online", [this](Rml::Element* content) { build_main(content); });
        if (porter && !pc_mp_hosting() && !pc_mp_visiting()) {
            add_tab("Visited towns", [this](Rml::Element* content) { build_friends(content); });
            add_tab("Open towns", [this](Rml::Element* content) {
                pc_mp_browse_start();
                build_browse(content);
            });
        }
    }

    ~AcplusOnlineWindow() override {
        pc_mp_browse_stop();
        pc_mp_menu_ui_close();
    }

    void update() override {
        if (pc_mp_menu_from_porter() && pc_mp_train_ready()) {
            /* the host's town is here: back to the game, where Porter calls the train */
            hide(true);
            MenuBar::hide_instance();
            return;
        }
        const std::string key = layout_key();
        if (key != mKey) {
            mKey = key;
            refresh_active_tab();
        }
        Window::update();
    }

    void hide(bool close) override {
        if (close) {
            pc_mp_browse_stop();
            pc_mp_menu_ui_close();
        }
        Window::hide(close);
    }

private:
    void build_main(Rml::Element* content) {
        auto& left = add_child<Pane>(content, Pane::Type::Controlled);
        auto& right = add_child<Pane>(content, Pane::Type::Uncontrolled);
        const bool online = pc_mp_hosting() || pc_mp_visiting();
        auto status = [](Pane& pane) { pane.add_child<LiveText>(status_rml); };

        if (online) {
            left.add_section(pc_mp_hosting() ? "Hosting your town" : "Visiting");
            left.register_control(left.add_child<StringButton>(StringButton::Props{
                                      .key = "Say",
                                      .getValue = [] { return Rml::String(); },
                                      .setValue = [](Rml::String text) {
                                          if (!text.empty()) pc_mp_say(text.c_str());
                                      },
                                      .maxLength = 32,
                                  }),
                                  right, [status](Pane& pane) {
                                      pane.add_text("A line everyone here sees above your head. (T or LB says one "
                                                    "without opening the menu.)");
                                      status(pane);
                                  });
            char name[16];
            int trusted;
            bool any = false;
            for (int slot = 1; slot < PC_MP_SLOTS_UI; slot++) {
                if (!pc_mp_visitor(slot, name, &trusted)) {
                    continue;
                }
                if (!any) {
                    left.add_section("Visitors");
                    any = true;
                }
                left.register_control(
                    left.add_select_button({ .key = name,
                                             .getValue = [slot] {
                                                 char n[16];
                                                 int t = 0;
                                                 pc_mp_visitor(slot, n, &t);
                                                 return Rml::String(t ? "Trusted" : "Guest");
                                             } })
                        .on_pressed([slot] { visitor_actions(slot); }),
                    right, [](Pane& pane) {
                        pane.add_text("A guest can't dig, chop or pick flowers; a trusted visitor can. Choose to "
                                      "change it, or to send them home.");
                    });
            }
            left.add_section("Leave");
            left.register_control(
                left.add_select_button(
                        { .key = pc_mp_hosting() ? "Stop hosting" : "Leave the visit",
                          .getValue = [] { return Rml::String(); } })
                    .on_pressed([] { pc_mp_stop(); }),
                right, [status](Pane& pane) {
                    pane.add_text(pc_mp_hosting() ? "Your visitors go home, and your town closes."
                                                  : "Back home by train.");
                    status(pane);
                });
            return;
        }

        if (!pc_mp_menu_from_porter()) {
            left.add_section("Going online");
            left.add_text("Talk to Porter at the station and choose Online: from there you host your town or "
                          "visit one.");
            left.add_child<LiveText>(status_rml);
            return;
        }

        left.add_section("Your town");
        left.register_control(left.add_select_button({ .key = "Host my town", .getValue = [] { return Rml::String(); } })
                                  .on_pressed([] { pc_mp_host_start(); }),
                              right, [status](Pane& pane) {
                                  pane.add_text("Open your town to visitors. With an invite code server (ac-plus "
                                                "settings, Online), you get a code to hand out.");
                                  status(pane);
                              });
        left.add_section("Visit a town");
        left.register_control(left.add_child<StringButton>(StringButton::Props{
                                  .key = "Address or invite code",
                                  .getValue = [] { return Rml::String(g_pc_settings.mp_last_visit); },
                                  .setValue = [](Rml::String text) {
                                      std::snprintf(g_pc_settings.mp_last_visit, sizeof(g_pc_settings.mp_last_visit),
                                                    "%s", text.c_str());
                                  },
                                  .maxLength = static_cast<int>(sizeof(g_pc_settings.mp_last_visit)) - 1,
                              }),
                              right, [status](Pane& pane) {
                                  pane.add_text("The host's invite code, or their address (host or IP, and :port "
                                                "if it isn't 41330).");
                                  status(pane);
                              });
        left.register_control(
            left.add_select_button({ .key = "Go visit", .getValue = [] { return Rml::String(); } }).on_pressed([] {
                if (g_pc_settings.mp_last_visit[0] != '\0') {
                    pc_mp_visit_start(g_pc_settings.mp_last_visit);
                }
            }),
            right, [status](Pane& pane) {
                pane.add_text("Connect, then board Porter's train.");
                status(pane);
            });
    }

    void build_friends(Rml::Element* content) {
        auto& left = add_child<Pane>(content, Pane::Type::Controlled);
        auto& right = add_child<Pane>(content, Pane::Type::Uncontrolled);
        char town[16], name[16];

        left.add_section("Towns you've visited");
        if (!pc_mp_friend(0, town, name)) {
            left.add_text("Towns you visit show up here.");
            return;
        }
        for (int i = 0; i < kTownsShown && pc_mp_friend(i, town, name); i++) {
            left.register_control(
                left.add_select_button({ .key = town, .getValue = [n = Rml::String(name)] { return n; } })
                    .on_pressed([i, t = Rml::String(town)] {
                        ask(t, "Visit this town if it's open, or forget it?",
                                            { { .label = "Visit", .onPressed = [i](Modal& modal) { modal.pop(); pc_mp_visit_friend(i); } },
                              { .label = "Forget it",
                                .onPressed = [i](Modal& modal) { modal.pop(); pc_mp_forget_friend(i); } },
                              { .label = "Cancel", .onPressed = [](Modal& modal) { modal.pop(); } } });
                    }),
                right, [](Pane& pane) {
                    pane.add_text("Choose to visit (if its host is online) or to forget it.");
                    pane.add_child<LiveText>(status_rml);
                });
        }
    }

    void build_browse(Rml::Element* content) {
        auto& left = add_child<Pane>(content, Pane::Type::Controlled);
        auto& right = add_child<Pane>(content, Pane::Type::Uncontrolled);
        const int count = pc_mp_browse_count();

        left.add_section("Open to everyone");
        if (count <= 0) {
            left.add_text(count == -1   ? "Asking the server..."
                          : count == -2 ? "The server didn't answer."
                                        : "No towns are open to everyone right now.");
        }
        for (int i = 0; i < count && i < kTownsShown; i++) {
            char code[8], town[16], name[16];
            if (!pc_mp_browse_town(i, code, town, name)) {
                continue;
            }
            left.register_control(
                left.add_select_button({ .key = town, .getValue = [n = Rml::String(name)] { return n; } })
                    .on_pressed([c = std::string(code)] {
                        std::snprintf(g_pc_settings.mp_last_visit, sizeof(g_pc_settings.mp_last_visit), "%s",
                                      c.c_str());
                        pc_mp_visit_start(c.c_str());
                    }),
                right, [](Pane& pane) {
                    pane.add_text("Visit this town.");
                    pane.add_child<LiveText>(status_rml);
                });
        }
        left.register_control(left.add_select_button({ .key = "Ask again", .getValue = [] { return Rml::String(); } })
                                  .on_pressed([] { pc_mp_browse_start(); }),
                              right, [](Pane& pane) { pane.add_text("Ask the server for the open towns again."); });
    }

    static void visitor_actions(int slot) {
        char name[16];
        int trusted = 0;
        if (!pc_mp_visitor(slot, name, &trusted)) {
            return;
        }
        ask(name, trusted ? "Trusted: may dig, chop and pick flowers." : "Guest: no digging, chopping or picking.",
            { { .label = trusted ? "Make a guest" : "Trust",
                .onPressed = [slot, trusted](Modal& modal) { modal.pop(); pc_mp_set_trusted(slot, !trusted); } },
              { .label = "Send home", .onPressed = [slot](Modal& modal) { modal.pop(); pc_mp_send_home(slot); } },
              { .label = "Cancel", .onPressed = [](Modal& modal) { modal.pop(); } } });
    }

    std::string mKey;
};

} // namespace acgc::ui

/* The ac-plus window's Online play (porter 0) and Porter's "Online" (porter 1): Nookery's menu, Online Play open */
extern "C" void pc_acplus_online_open(int porter) {
    acgc::ui::MenuBar::show_instance();
    borealis::ui::push_document(std::make_unique<acgc::ui::AcplusOnlineWindow>(porter != 0));
}
