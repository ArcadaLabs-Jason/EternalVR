#include "platform/mp_policy/guard_refusal.hpp"

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace evr::mp_policy {

std::string guardRefusalReason(const DetectionPoints& found,
                               bool knownBuild,
                               bool xbox,
                               std::string_view supportedBuilds) {
    if (!knownBuild) {
        return "this DOOM Eternal version is not one this EternalVR supports (" +
               std::string(supportedBuilds) + "); VR stays off until an EternalVR update";
    }
    const std::array<std::pair<bool, std::string_view>, 6> points{{
        {found.mapLoad, "the map load"},
        {found.joins, xbox ? "the Xbox invites" : "the Steam joins"},
        {found.invites, "the accepted invite"},
        {found.lobbySession, "the BATTLEMODE lobby"},
        {found.gameSession, "the BATTLEMODE game session"},
        {found.mainMenu, "the main menu screens"},
    }};
    std::vector<std::string_view> missing;
    for (const auto& point : points) {
        if (!point.first) {
            missing.push_back(point.second);
        }
    }
    if (missing.empty()) { // not a refusal for a missing point
        return "the mod could not find everything it needs in the game; VR stays off";
    }
    std::string names;
    for (std::size_t i = 0; i < missing.size(); ++i) {
        if (i > 0) {
            names += i + 1 == missing.size() ? " and " : ", ";
        }
        names += missing[i];
    }
    return "the mod could not find " + names +
           " in the game, which it watches to keep VR out of online play; VR stays off";
}

} // namespace evr::mp_policy
