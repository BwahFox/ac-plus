#include "acgc/ui/menu_bar.hpp"
#include "acgc/ui/acplus.hpp"

#include "acgc/ui/controller_config.hpp"
#include <borealis/ui/modal.hpp>
#include "acgc/ui/settings.hpp"
#include "acgc/ui/localization.hpp"
#include "acgc/ui/ui.hpp"

extern "C" int g_pc_running;
extern "C" void pc_request_quit(void);   /* saves first when save_on_quit is set (pc_quick_save.c) */

namespace acgc::ui {
using namespace borealis::ui;

namespace {
MenuBar* sInstance = nullptr;
}

MenuBar::MenuBar() {
    sInstance = this;
    build_tabs();
}

MenuBar::~MenuBar() {
    if (sInstance == this) {
        sInstance = nullptr;
    }
}

void MenuBar::show_instance() {
    if (sInstance != nullptr) {
        sInstance->show();
    }
}

void MenuBar::hide_instance() {
    if (sInstance != nullptr) {
        sInstance->hide(false);
    }
}

void MenuBar::build_tabs() {
    mTabBar->add_tab(localization::get("common.settings"), [this] { push(std::make_unique<SettingsWindow>()); });
    mTabBar->add_tab(localization::get("menu.controls"), [this] { push(std::make_unique<ControllerConfigWindow>()); });
    /* ac-plus: its own menu (Online Play, pattern storage, QR codes, its settings), drawn in the game */
    mTabBar->add_tab("ac-plus", [this] { push(std::make_unique<AcplusWindow>()); });
    mTabBar->add_tab(localization::get("common.quit"), [this] {
        mTabBar->set_active_tab(-1);
        push(std::make_unique<Modal>(Modal::Props{
            .title = escape(localization::get("menu.quit.title")),
            .bodyRml = escape(localization::get("menu.quit.warning")),
            .actions = {
                {localization::get("common.cancel"), [](Modal& modal) { modal.pop(); }},
                {localization::get("common.quit"), [this](Modal& modal) {
                    modal.pop();
                    /* ac-plus: the game runs on for a frame or two to save first (save_on_quit) */
                    hide(false);
                    pc_request_quit();
                },},
            },
            .onDismiss = [](Modal& modal) { modal.pop(); },
            .variant = "light",
            .icon = "warning",
        }));
    });
}
} // namespace acgc::ui
