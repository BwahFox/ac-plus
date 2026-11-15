#pragma once

#include <borealis/ui/window.hpp>

namespace acgc::ui {

/* The ac-plus window (Nookery's menu bar, ac-plus tab): its town actions (pattern designer and storage, QR codes,
 * Online Play) and its settings (City Folk, music, saving, online), kept in ac-plus.ini (pc_acplus_settings.c). */
class AcplusWindow final : public borealis::ui::Window {
public:
    AcplusWindow();
};

} // namespace acgc::ui
