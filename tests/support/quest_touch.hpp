#pragma once

// The built-in Quest Touch maps, parsed and compiled, for tests that need a realistic profile.

#include "features/input/binding_compiler.hpp"
#include "features/input/binding_profile.hpp"
#include "features/input/binding_text.hpp"
#include "game/eternal/quest_touch_bindings.hpp"

namespace evr::test {

inline input::BindingMap questTouchBindings(game::Handedness handedness = game::Handedness::Right) {
    return input::parseBindingText(game::questTouchBindingText(handedness)).entries;
}

inline input::BindingProfile questTouchProfile(game::Handedness handedness = game::Handedness::Right) {
    return input::buildBindingProfile(questTouchBindings(handedness)).profile;
}

} // namespace evr::test
