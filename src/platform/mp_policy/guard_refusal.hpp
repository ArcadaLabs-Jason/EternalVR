#pragma once

// The sentence the player sees when the multiplayer guard refuses to arm (vkcore/mp_guard.cpp, T-094): the
// launcher shows the status file's reason ("VR is off for this session: <reason>. The game runs flat."), so a
// refusal names what is missing. Portable, so the wording is tested without the game.

#include <string>
#include <string_view>

namespace evr::mp_policy {

// The detection points mp_guard::install() located and validated (docs/rig-findings/mp-guard.md).
struct DetectionPoints {
    bool mapLoad = false;
    bool joins = false; // Steam's join callbacks, or on Game Pass the Xbox invite path
    bool invites = false;
    bool lobbySession = false;
    bool gameSession = false;
    bool mainMenu = false;

    [[nodiscard]] bool all() const {
        return mapLoad && joins && invites && lobbySession && gameSession && mainMenu;
    }
};

// The status reason for a refused guard. On a build EternalVR does not know (`knownBuild` false) every point
// is expected to be missing: the version is not a supported one (`supportedBuilds`, "Steam build 25216728 or
// Game Pass 1.0.56.0"). On a known build the missing points are named, in the order install() tries them, and
// `xbox` names the Game Pass join path instead of Steam's.
std::string guardRefusalReason(const DetectionPoints& found,
                               bool knownBuild,
                               bool xbox,
                               std::string_view supportedBuilds);

} // namespace evr::mp_policy
