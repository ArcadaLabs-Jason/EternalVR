#include "game/eternal/quest_touch_bindings.hpp"

namespace evr::game {

namespace {

// Jump on A and dash on B mirror the console layout, so thumb habits transfer. The chainsaw is up and
// weapon selection down on the turn stick. The equipment launcher and Flame Belch are shoulder-mounted
// and aim from the head, so they sit on the off hand.
//
// Each map is written exactly as formatBindingText writes it, which the tests check.
constexpr std::string_view kRightHanded = R"([bindings]
"weapon_hand" = "right"
"left.stick.role" = "move"
"left.trigger.press" = "equipment"
"left.grip.press" = "flame_belch"
"left.stick_click.press" = "crucible"
"left.primary.tap" = "switch_equipment"
"left.primary.hold" = "dossier"
"left.secondary.tap" = "switch_weapon_mod"
"left.secondary.hold" = "mission_info"
"left.menu.tap" = "pause"
"right.stick.role" = "turn"
"right.trigger.press" = "fire"
"right.grip.press" = "weapon_mod"
"right.stick_click.press" = "melee"
"right.primary.press" = "jump"
"right.secondary.press" = "dash"
"right.stick.up" = "chainsaw"
"right.stick.down_tap" = "quick_switch"
"right.stick.down_hold" = "weapon_wheel"
)";

constexpr std::string_view kLeftButtonSwap = R"([bindings]
"weapon_hand" = "left"
"left.stick.role" = "move"
"left.trigger.press" = "fire"
"left.grip.press" = "weapon_mod"
"left.stick_click.press" = "melee"
"left.primary.tap" = "switch_equipment"
"left.primary.hold" = "dossier"
"left.secondary.tap" = "switch_weapon_mod"
"left.secondary.hold" = "mission_info"
"left.menu.tap" = "pause"
"right.stick.role" = "turn"
"right.trigger.press" = "equipment"
"right.grip.press" = "flame_belch"
"right.stick_click.press" = "crucible"
"right.primary.press" = "jump"
"right.secondary.press" = "dash"
"right.stick.up" = "chainsaw"
"right.stick.down_tap" = "quick_switch"
"right.stick.down_hold" = "weapon_wheel"
)";

constexpr std::string_view kLeftButtonAndStickSwap = R"([bindings]
"weapon_hand" = "left"
"left.stick.role" = "turn"
"left.trigger.press" = "fire"
"left.grip.press" = "weapon_mod"
"left.stick_click.press" = "melee"
"left.primary.press" = "jump"
"left.secondary.press" = "dash"
"left.menu.tap" = "pause"
"left.stick.up" = "chainsaw"
"left.stick.down_tap" = "quick_switch"
"left.stick.down_hold" = "weapon_wheel"
"right.stick.role" = "move"
"right.trigger.press" = "equipment"
"right.grip.press" = "flame_belch"
"right.stick_click.press" = "crucible"
"right.primary.tap" = "switch_equipment"
"right.primary.hold" = "dossier"
"right.secondary.tap" = "switch_weapon_mod"
"right.secondary.hold" = "mission_info"
)";

} // namespace

std::string_view questTouchBindingText(Handedness handedness) {
    switch (handedness) {
    case Handedness::LeftButtonSwap:
        return kLeftButtonSwap;
    case Handedness::LeftButtonAndStickSwap:
        return kLeftButtonAndStickSwap;
    case Handedness::Right:
        break;
    }
    return kRightHanded;
}

} // namespace evr::game
