#include "features/input/dashboard_pause.hpp"

#include <algorithm>

namespace evr::input {

bool runtimeTakesMenuButton(std::string_view runtimeName) {
    return runtimeName.find("SteamVR") != std::string_view::npos;
}

CaptureButtons captureButtonsFor(std::string_view runtimeName) {
    return runtimeTakesMenuButton(runtimeName) ? CaptureButtons::MenuOrSecondary : CaptureButtons::Menu;
}

std::size_t applyDashboardPause(BindingProfile& profile) {
    std::size_t changed = 0;
    for (ButtonBinding& b : profile.buttons) {
        if (b.input != ButtonInput::Secondary || b.kind != PressKind::Hold ||
            b.action != game::GameAction::MissionInfo) {
            continue;
        }
        const bool menuPauses = std::ranges::any_of(profile.buttons, [&](const ButtonBinding& m) {
            return m.hand == b.hand && m.input == ButtonInput::Menu && m.kind == PressKind::Tap &&
                   m.action == game::GameAction::Pause;
        });
        if (menuPauses) {
            b.action = game::GameAction::Pause;
            ++changed;
        }
    }
    return changed;
}

} // namespace evr::input
