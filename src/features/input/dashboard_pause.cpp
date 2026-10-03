#include "features/input/dashboard_pause.hpp"

#include <algorithm>

namespace evr::input {

bool runtimeTakesMenuButton(std::string_view runtimeName, game::Controller family) {
    return runtimeName.find("SteamVR") != std::string_view::npos &&
           std::ranges::find(kDashboardMenuFamilies, family) != kDashboardMenuFamilies.end();
}

CaptureButtons captureButtonsFor(std::string_view runtimeName, game::Controller family) {
    return runtimeTakesMenuButton(runtimeName, family) ? CaptureButtons::MenuOrSticks : CaptureButtons::Menu;
}

std::optional<Hand> applyDashboardPause(BindingProfile& profile) {
    const auto menuPause = std::ranges::find_if(profile.buttons, [](const ButtonBinding& b) {
        return b.input == ButtonInput::Menu && b.kind == PressKind::Tap &&
               b.action == game::GameAction::Pause;
    });
    const bool otherPause = std::ranges::any_of(profile.buttons, [](const ButtonBinding& b) {
        return b.input != ButtonInput::Menu && b.action == game::GameAction::Pause;
    });
    if (menuPause == profile.buttons.end() || otherPause) {
        return std::nullopt;
    }
    ButtonBinding* missionInfo = nullptr;
    for (ButtonBinding& b : profile.buttons) {
        if (b.input != ButtonInput::Secondary || b.kind != PressKind::Hold ||
            b.action != game::GameAction::MissionInfo) {
            continue;
        }
        if (!missionInfo || b.hand == menuPause->hand) {
            missionInfo = &b;
        }
    }
    if (!missionInfo) {
        return std::nullopt;
    }
    missionInfo->action = game::GameAction::Pause;
    return missionInfo->hand;
}

} // namespace evr::input
