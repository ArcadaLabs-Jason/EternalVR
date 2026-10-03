// Every family, handedness and runtime, as the layer builds them (usercmd_hook.cpp: the control map with the
// dashboard pause, and the capture chord's buttons): a pause on a button the runtime lets through, mission
// info on a button where the family has one, and a capture chord that no gameplay combination reaches.

#include "features/input/binding_compiler.hpp"
#include "features/input/controller_bindings.hpp"
#include "features/input/dashboard_pause.hpp"
#include "features/input/input_frames.hpp"
#include "features/input/input_mapper.hpp"
#include "game/eternal/controller_data.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <ostream>
#include <string_view>
#include <vector>

using evr::game::builtinControllerData;
using evr::game::contains;
using evr::game::Controller;
using evr::game::controllerName;
using evr::game::GameAction;
using evr::game::Handedness;
using evr::game::kControllers;
using evr::input::BindingProfile;
using evr::input::buildBindingProfile;
using evr::input::ButtonBinding;
using evr::input::ButtonInput;
using evr::input::CaptureButtons;
using evr::input::GameInput;
using evr::input::Hand;
using evr::input::HandState;
using evr::input::InputFrame;
using evr::input::InputMapper;
using evr::input::MapperSettings;
using evr::input::parseControllerData;

namespace {

constexpr std::array kAllHandedness{Handedness::Right, Handedness::LeftButtonSwap,
                                    Handedness::LeftButtonAndStickSwap};
// SteamVR, which keeps Touch's left Menu button, and a runtime that passes every button on.
constexpr std::array<std::string_view, 2> kRuntimes{"SteamVR/OpenXR", "VirtualDesktopXR"};
constexpr float kFrame = 1.0f / 30.0f;

struct Setup {
    BindingProfile profile;
    CaptureButtons chord = CaptureButtons::Menu;
    bool menuTaken = false; // the runtime keeps the left Menu button
};

Setup setupOf(Controller family, Handedness handedness, std::string_view runtime) {
    Setup s;
    s.profile =
        buildBindingProfile(parseControllerData(builtinControllerData(family)).maps.at(handedness)).profile;
    s.menuTaken = evr::input::runtimeTakesMenuButton(runtime, family);
    if (s.menuTaken) {
        evr::input::applyDashboardPause(s.profile);
    }
    s.chord = evr::input::captureButtonsFor(runtime, family);
    return s;
}

bool reachable(const Setup& s, const ButtonBinding& b) {
    return !(s.menuTaken && b.hand == Hand::Left && b.input == ButtonInput::Menu);
}

bool onAButton(const Setup& s, GameAction action) {
    return std::ranges::any_of(s.profile.buttons,
                               [&](const ButtonBinding& b) { return b.action == action && reachable(s, b); });
}

// Families whose controllers have no button for mission info (docs/release/CONTROLS.md).
bool lacksMissionInfo(Controller family) {
    return family == Controller::WindowsMixedReality || family == Controller::ViveWand;
}

struct Press {
    Hand hand;
    ButtonInput input;
};

void press(InputFrame& frame, Press p, bool down) {
    HandState& h = p.hand == Hand::Left ? frame.left : frame.right;
    switch (p.input) {
    case ButtonInput::Trigger:
        h.trigger = down ? 1.0f : 0.0f;
        break;
    case ButtonInput::Grip:
        h.grip = down ? 1.0f : 0.0f;
        break;
    case ButtonInput::StickClick:
        h.stickClick = down;
        break;
    case ButtonInput::Primary:
        h.primaryButton = down;
        break;
    case ButtonInput::Secondary:
        h.secondaryButton = down;
        break;
    case ButtonInput::Face3:
        h.face3Button = down;
        break;
    case ButtonInput::Face4:
        h.face4Button = down;
        break;
    case ButtonInput::Shoulder:
        h.shoulderButton = down;
        break;
    case ButtonInput::Menu:
        h.menuButton = down;
        break;
    case ButtonInput::Count:
        break;
    }
}

// Every input the map binds, the left Menu button (the capture chord's own) aside.
std::vector<Press> gameplayInputs(const Setup& s) {
    std::vector<Press> out;
    for (const ButtonBinding& b : s.profile.buttons) {
        const bool known =
            std::ranges::any_of(out, [&](Press p) { return p.hand == b.hand && p.input == b.input; });
        if (!known && !(b.hand == Hand::Left && b.input == ButtonInput::Menu)) {
            out.push_back({b.hand, b.input});
        }
    }
    return out;
}

struct Run {
    int captures = 0;
    bool triggerActed = false; // an action of the pulled trigger's bindings went down
};

// `held` down from the start for 1 s, the trigger of `pull` pulled from `pullAt` for 0.3 s; 1.4 s in all.
Run run(const Setup& s, const std::vector<Press>& held, Hand pull, float pullAt) {
    MapperSettings settings;
    settings.captureButtons = s.chord;
    InputMapper mapper(s.profile, settings);
    Run r;
    for (int i = 0; i < static_cast<int>(1.4f / kFrame); ++i) {
        const float t = static_cast<float>(i) * kFrame;
        InputFrame frame = evr::test::restingFrame();
        for (const Press p : held) {
            press(frame, p, t < 1.0f);
        }
        press(frame, {pull, ButtonInput::Trigger}, t >= pullAt && t < pullAt + 0.3f);
        const GameInput input = mapper.update(frame, {}, kFrame);
        r.captures += input.capture ? 1 : 0;
        for (const ButtonBinding& b : s.profile.buttons) {
            if (b.hand == pull && b.input == ButtonInput::Trigger && contains(input.down, b.action)) {
                r.triggerActed = true;
            }
        }
    }
    return r;
}

} // namespace

TEST_CASE("every family, handedness and runtime has a pause on a button the runtime passes on") {
    for (const Controller family : kControllers) {
        for (const Handedness handedness : kAllHandedness) {
            for (const std::string_view runtime : kRuntimes) {
                CAPTURE(controllerName(family));
                CAPTURE(static_cast<int>(handedness));
                CAPTURE(runtime);
                CHECK(onAButton(setupOf(family, handedness, runtime), GameAction::Pause));
            }
        }
    }
}

TEST_CASE("mission info keeps a button, except where the family has none or SteamVR's pause needs it") {
    for (const Controller family : kControllers) {
        for (const Handedness handedness : kAllHandedness) {
            for (const std::string_view runtime : kRuntimes) {
                CAPTURE(controllerName(family));
                CAPTURE(static_cast<int>(handedness));
                CAPTURE(runtime);
                const Setup s = setupOf(family, handedness, runtime);
                // Under SteamVR, Touch's mission-info hold is the pause (the Dossier also shows it).
                const bool expected = !lacksMissionInfo(family) && !s.menuTaken;
                CHECK(onAButton(s, GameAction::MissionInfo) == expected);
            }
        }
    }
    // Index under SteamVR keeps it: its Menu input reaches the game (input review L3).
    CHECK(
        onAButton(setupOf(Controller::ValveIndex, Handedness::Right, kRuntimes[0]), GameAction::MissionInfo));
}

TEST_CASE("the capture chord's own buttons carry no gameplay action, and it captures") {
    for (const Controller family : kControllers) {
        for (const Handedness handedness : kAllHandedness) {
            for (const std::string_view runtime : kRuntimes) {
                CAPTURE(controllerName(family));
                CAPTURE(static_cast<int>(handedness));
                CAPTURE(runtime);
                const Setup s = setupOf(family, handedness, runtime);
                for (const ButtonBinding& b : s.profile.buttons) {
                    if (b.hand == Hand::Left && b.input == ButtonInput::Menu) {
                        CHECK((b.action == GameAction::Pause || b.action == GameAction::Recenter));
                    }
                }
                // The Menu chord only where the Menu button arrives; the sticks where it does not.
                CHECK((s.chord == CaptureButtons::MenuOrSticks) == s.menuTaken);
                const std::vector<Press> gate =
                    s.menuTaken ? std::vector<Press>{{Hand::Left, ButtonInput::StickClick},
                                                     {Hand::Right, ButtonInput::StickClick}}
                                : std::vector<Press>{{Hand::Left, ButtonInput::Menu}};
                for (const Hand pull : {Hand::Left, Hand::Right}) {
                    const Run r = run(s, gate, pull, 0.5f);
                    CHECK(r.captures == 1);
                    CHECK_FALSE(r.triggerActed);
                }
            }
        }
    }
}

TEST_CASE("no gameplay button or pair of buttons with a trigger pull captures or holds the pull back") {
    for (const Controller family : kControllers) {
        for (const Handedness handedness : kAllHandedness) {
            for (const std::string_view runtime : kRuntimes) {
                CAPTURE(controllerName(family));
                CAPTURE(static_cast<int>(handedness));
                CAPTURE(runtime);
                const Setup s = setupOf(family, handedness, runtime);
                const std::vector<Press> inputs = gameplayInputs(s);
                std::vector<std::vector<Press>> combos;
                for (std::size_t i = 0; i < inputs.size(); ++i) {
                    combos.push_back({inputs[i]});
                    for (std::size_t j = i + 1; j < inputs.size(); ++j) {
                        // Both sticks held are the layer's own chord (recenter, and the capture under
                        // SteamVR).
                        const bool sticks = inputs[i].input == ButtonInput::StickClick &&
                                            inputs[j].input == ButtonInput::StickClick;
                        if (!(sticks && s.chord == CaptureButtons::MenuOrSticks)) {
                            combos.push_back({inputs[i], inputs[j]});
                        }
                    }
                }
                int failures = 0;
                for (const std::vector<Press>& combo : combos) {
                    for (const Hand pull : {Hand::Left, Hand::Right}) {
                        const bool pullHeld = std::ranges::any_of(combo, [&](Press p) {
                            return p.hand == pull && p.input == ButtonInput::Trigger;
                        });
                        if (pullHeld) {
                            continue;
                        }
                        // A pull right after the buttons go down, and one after their hold time.
                        for (const float pullAt : {0.05f, 0.5f}) {
                            const Run r = run(s, combo, pull, pullAt);
                            if (r.captures != 0 || !r.triggerActed) {
                                ++failures;
                                CAPTURE(static_cast<int>(combo.front().hand));
                                CAPTURE(static_cast<int>(combo.front().input));
                                CAPTURE(static_cast<int>(combo.back().hand));
                                CAPTURE(static_cast<int>(combo.back().input));
                                CAPTURE(static_cast<int>(pull));
                                CAPTURE(pullAt);
                                CHECK(r.captures == 0);
                                CHECK(r.triggerActed);
                            }
                        }
                    }
                }
                CHECK(failures == 0);
            }
        }
    }
}
