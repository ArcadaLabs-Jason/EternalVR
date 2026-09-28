#include "features/input/dossier_press.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace evr::input {

namespace {

bool bound(
    const BindingProfile& profile, Hand hand, ButtonInput input, PressKind kind, game::GameAction action) {
    return std::ranges::any_of(profile.buttons, [&](const ButtonBinding& b) {
        return b.hand == hand && b.input == input && b.kind == kind && b.action == action;
    });
}

} // namespace

std::size_t applyDossierPress(BindingProfile& profile, DossierPress press) {
    if (press != DossierPress::Tap) {
        return 0;
    }
    // Only the buttons that carry exactly the pair are swapped, found before any binding changes.
    std::vector<std::pair<Hand, ButtonInput>> pairs;
    for (const ButtonBinding& b : profile.buttons) {
        if (b.kind == PressKind::Tap && b.action == game::GameAction::SwitchEquipment &&
            bound(profile, b.hand, b.input, PressKind::Hold, game::GameAction::Dossier)) {
            pairs.emplace_back(b.hand, b.input);
        }
    }
    for (ButtonBinding& b : profile.buttons) {
        if (std::ranges::find(pairs, std::pair{b.hand, b.input}) == pairs.end()) {
            continue;
        }
        if (b.kind == PressKind::Tap && b.action == game::GameAction::SwitchEquipment) {
            b.action = game::GameAction::Dossier;
        } else if (b.kind == PressKind::Hold && b.action == game::GameAction::Dossier) {
            b.action = game::GameAction::SwitchEquipment;
        }
    }
    return pairs.size();
}

const char* dossierPressName(DossierPress press) {
    return press == DossierPress::Tap ? "tap" : "hold";
}

} // namespace evr::input
