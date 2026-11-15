#pragma once
#include <borealis/ui/menu_bar.hpp>
#include "ui.hpp"
namespace acgc::ui {
class MenuBar final : public borealis::ui::MenuBar {
public:
    MenuBar();
    ~MenuBar() override;

    /* ac-plus: closes the menu bar from elsewhere (the ac-plus window handing over to the game) */
    static void hide_instance();
    static void show_instance();
protected:
    void build_tabs() override;
};
} // namespace acgc::ui
