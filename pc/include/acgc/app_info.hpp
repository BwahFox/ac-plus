#pragma once

#include <borealis/app_info.hpp>

namespace acgc {

inline constexpr borealis::AppInfo AppInfo{
    .orgName = {},
    .appName = "Animal Crossing Plus", /* ac-plus fork of Nookery: window name, data folder name */
    .githubOwner = "BwahFox", /* our releases, never stock Nookery's */
    .githubRepo = "ac-plus",
    .discordApplicationId = {},
};

} // namespace acgc
