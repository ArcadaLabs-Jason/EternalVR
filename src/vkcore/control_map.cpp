// Motion controllers, the control map (controllers.hpp, docs/VR_CONTROLLERS.md): the family's map for the
// handedness, compiled, and the mapper built from it. Building it also gives the game's prompts the buttons'
// names (prompt_hooks.cpp).
//
// The mapper runs with the game's user commands (usercmd_hook.cpp), which the title screen and the main menu
// do not build, so on its own the map was first built about 1.5 s into the first level and the main menu's
// hint bars named the keyboard keys until then ("[ESC] BACK"; owner, 2026-10-02). The XR worker builds it
// ahead of that as soon as the runtime reports a controller (prepareControlMap).

#include "vkcore/controllers_impl.hpp"

#include "features/input/binding_compiler.hpp"
#include "features/input/binding_keys.hpp"
#include "features/input/dashboard_pause.hpp"
#include "features/input/dossier_press.hpp"
#include "features/input/family_touch_settings.hpp"
#include "features/input/touch_inputs.hpp"
#include "vkcore/log.hpp"
#include "vkcore/room_scale.hpp"
#include "vkcore/xr_runtime.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";

// The control map for the family and handedness, compiled; nullopt (logged) when it has issues.
std::optional<input::BindingProfile> controlMap(const State& s, game::Controller family) {
    const input::ControllerData& data = s.controllerData[static_cast<std::size_t>(family)];
    const auto map = data.maps.find(s.settings.handedness);
    if (map == data.maps.end()) {
        EVR_LOG("%s: the %s data has no control map for this handedness", kTag,
                std::string(game::controllerName(family)).c_str());
        return std::nullopt;
    }
    input::BindingBuildResult built = input::buildBindingProfile(map->second);
    for (const input::BindingIssue& issue : built.issues) {
        EVR_LOG("%s: control map: %s", kTag, issue.message.c_str());
    }
    if (!built.ok()) {
        EVR_LOG("%s: the control map has conflicts; controllers send nothing until it is fixed", kTag);
        return std::nullopt;
    }
    // SteamVR opens its dashboard on Touch's left Menu button: holding Y (B in the full mirror) pauses
    // instead of showing mission info (dashboard_pause.hpp).
    if (input::runtimeTakesMenuButton(xrRuntimeName(), family)) {
        if (const auto hand = input::applyDashboardPause(built.profile)) {
            EVR_LOG("%s: the runtime keeps the Menu button for its dashboard: holding the %s button pauses",
                    kTag, *hand == input::Hand::Left ? "Y" : "B");
        }
    }
    // ETERNALVR_DOSSIER=tap: X taps open the Dossier and a hold switches equipment (dossier_press.hpp).
    if (s.settings.dossier == input::DossierPress::Tap &&
        input::applyDossierPress(built.profile, s.settings.dossier) == 0) {
        EVR_LOG("%s: ETERNALVR_DOSSIER=tap: no button taps switch equipment and holds for the Dossier; "
                "the map is kept",
                kTag);
    }
    return built.profile;
}

const char* pressName(input::PressKind kind) {
    return kind == input::PressKind::Tap ? "tap" : kind == input::PressKind::Hold ? "hold" : "press";
}

// Whether `input` is what the wheel reads as `hand`'s rest: its thumb rest, or where a face button's touch
// is the rest, that touch.
bool wheelRestInput(const input::MapperSettings& settings, input::Hand hand, input::ButtonInput in) {
    return in == input::ButtonInput::ThumbRest ||
           (input::faceTouchIsRest(settings, hand) &&
            (in == input::ButtonInput::PrimaryTouch || in == input::ButtonInput::SecondaryTouch));
}

// What `hand`'s wheel rest is bound to, "tap = quick_switch, hold = automap" (a face button's touch named:
// "primary_touch tap = jump"), or empty when it is not bound (features/input/touch_button.hpp).
std::string restBindings(const input::InputMapper& mapper, input::Hand hand) {
    std::string text;
    for (const input::ButtonBinding& b : mapper.profile().buttons) {
        if (b.hand != hand || !wheelRestInput(mapper.settings(), hand, b.input)) {
            continue;
        }
        text += text.empty() ? "" : ", ";
        if (b.input != input::ButtonInput::ThumbRest) {
            text += b.input == input::ButtonInput::PrimaryTouch ? "primary_touch " : "secondary_touch ";
        }
        text += std::string(pressName(b.kind)) + " = " + std::string(game::gameActionName(b.action));
    }
    return text;
}

const char* side(input::Hand hand) {
    return hand == input::Hand::Left ? "left" : "right";
}

// What the wheel reads as `hand`'s rest, for the log.
const char* restName(const input::MapperSettings& settings, input::Hand hand) {
    return input::faceTouchIsRest(settings, hand) ? "face-button touch" : "thumb rest";
}

// What the bound thumb rests do to the wheel (one rest, one job): `wheelSensors` are its rests before the
// mapper left the bound ones out.
input::RestWheelLoss boundRestLoss(const input::InputMapper& mapper,
                                   const std::array<bool, 2>& wheelSensors) {
    const input::BindingProfile& profile = mapper.profile();
    return input::restWheelLoss(mapper.restWheel().settings().mode,
                                {wheelSensors, profile.moveStick, profile.turnStick},
                                {input::bindsWheelRest(profile, mapper.settings(), input::Hand::Left),
                                 input::bindsWheelRest(profile, mapper.settings(), input::Hand::Right)});
}

// Why the bound rests turn the wheel off (input::RestWheelLoss::Off).
std::string restWheelOffReason(const input::InputMapper& mapper, const std::array<bool, 2>& wheelSensors) {
    const input::RestWheel& rest = mapper.restWheel();
    const auto& left = rest.hands().hasRest;
    if (!left[0] && !left[1]) {
        const bool face = input::faceTouchIsRest(mapper.settings(), input::Hand::Left) ||
                          input::faceTouchIsRest(mapper.settings(), input::Hand::Right);
        const char* rests = face ? "rests" : "thumb rests";
        return wheelSensors[0] && wheelSensors[1]
                   ? std::string("both ") + rests + " are bound as buttons"
                   : std::string("its ") + (face ? "rest" : "thumb rest") + " is bound as a button";
    }
    const input::Hand kept = left[0] ? input::Hand::Left : input::Hand::Right;
    const input::Hand bound = input::otherHand(kept);
    const std::string boundRest =
        std::string("the ") + side(bound) + " " + restName(mapper.settings(), bound);
    if (rest.settings().mode == input::RestWheelMode::Extreme) {
        return boundRest + ", which gives the turn stick back its turning, is bound as a button";
    }
    return boundRest + " is bound as a button, and the " + side(kept) +
           " rest has no stick to pick with (the " + side(bound) + " stick has no role)";
}

// The thumb rests bound like buttons, for the log; with the wheel on, a bound rest is not the wheel's (the
// mapper leaves it out of the wheel's sensors).
void logRestButtons(const input::InputMapper& mapper, const std::array<bool, 2>& wheelSensors) {
    const input::RestWheel& rest = mapper.restWheel();
    const input::RestWheelLoss loss = boundRestLoss(mapper, wheelSensors);
    for (const input::Hand hand : {input::Hand::Left, input::Hand::Right}) {
        const std::string bound = restBindings(mapper, hand);
        if (bound.empty()) {
            continue;
        }
        const char* name = restName(mapper.settings(), hand);
        if (loss == input::RestWheelLoss::OtherRest && wheelSensors[static_cast<std::size_t>(hand)]) {
            EVR_LOG("%s: the %s %s is bound (%s); the thumb-rest wheel uses the %s rest only", kTag,
                    side(hand), name, bound.c_str(), side(input::otherHand(hand)));
            continue;
        }
        // A face button's touch bound with the wheel off is only a touch bound as a button (logTouchButtons).
        if (input::bindsInput(mapper.profile(), hand, input::ButtonInput::ThumbRest) ||
            rest.settings().mode != input::RestWheelMode::Off) {
            EVR_LOG("%s: the %s %s is bound (%s)", kTag, side(hand), name, bound.c_str());
        }
    }
    if (loss == input::RestWheelLoss::Off) {
        EVR_LOG("%s: thumb-rest wheel %s: off for these controllers (%s)", kTag,
                input::restWheelModeName(rest.settings().mode),
                restWheelOffReason(mapper, wheelSensors).c_str());
    }
}

// The touches on controls bound like buttons (features/input/touch_inputs.hpp), one line for all, or nothing
// when none is: "touches bound as buttons: right.primary_touch.tap = weapon_slot_2, ...".
void logTouchButtons(const input::InputMapper& mapper) {
    std::string text;
    for (const input::ButtonBinding& b : mapper.profile().buttons) {
        if (!input::isControlTouch(b.input)) {
            continue;
        }
        text += text.empty() ? "" : ", ";
        text += input::formatBindingKey(input::ButtonKey{b.hand, b.input, b.kind}) + " = " +
                std::string(game::gameActionName(b.action));
    }
    if (!text.empty()) {
        EVR_LOG("%s: touches bound as buttons (using a control under that thumb or finger ends one): %s",
                kTag, text.c_str());
    }
}

// The thumb-rest wheel's line for the log (features/input/rest_wheel.hpp). `wheelSensors`: its rests before
// the mapper left the ones bound like buttons out.
void logRestWheel(const input::InputMapper& mapper, bool faceTouch, const std::array<bool, 2>& wheelSensors) {
    const input::RestWheel& rest = mapper.restWheel();
    const input::RestWheelSettings& w = rest.settings();
    if (w.mode == input::RestWheelMode::Off) {
        EVR_LOG("%s: thumb-rest wheel off", kTag);
        return;
    }
    if (boundRestLoss(mapper, wheelSensors) == input::RestWheelLoss::Off) {
        return; // the rests bound as buttons turned it off (logRestButtons says why)
    }
    const auto& sensors = rest.hands().hasRest;
    if (!rest.usable()) {
        EVR_LOG("%s: thumb-rest wheel %s: off for these controllers (%s)", kTag,
                input::restWheelModeName(w.mode),
                wheelSensors[0] || wheelSensors[1] ? "no stick to pick with" : "no thumb-rest sensor");
        return;
    }
    const char* rests = "both hands";
    if (!sensors[0] || !sensors[1]) {
        rests = sensors[0] ? "the left hand" : "the right hand";
    }
    const std::string picks = w.pick == input::RestWheelPick::Slots
                                  ? "weapon by direction (" + input::weaponDirectionsText(w.directions) + ")"
                                  : std::string("the game's wheel");
    EVR_LOG("%s: thumb-rest wheel %s: rest sensors on %s%s, picks with %s, window %.2f s", kTag,
            input::restWheelModeName(w.mode), rests, faceTouch ? " (face-button touch counts)" : "",
            picks.c_str(), w.windowSeconds);
}

} // namespace

bool ensureMapper(State& s, game::Controller controller, const char* when) {
    if (s.mapper && s.mapperController == controller) {
        return true;
    }
    if (s.mapperBroken && s.mapperController == controller) {
        return false; // reported once when it was built
    }
    s.mapperController = controller;
    auto profile = controlMap(s, controller);
    s.mapperBroken = !profile;
    if (!profile) {
        s.mapper.reset();
        return false;
    }
    const input::ControllerSettings& cfg = settings();
    input::MapperSettings mapperSettings;
    mapperSettings.locomotionFrame = cfg.locomotion;
    mapperSettings.turn = cfg.turn;
    // A Menu press shorter than the recenter hold pauses; with the recenter binding off, any press does.
    const float recenterHold = roomScaleSettings().recenterHoldSeconds;
    mapperSettings.menuTapSeconds = recenterHold > 0.0f ? recenterHold : input::kMaxHoldSeconds;
    // Both sticks held: the recenter chord (off with the recenter hold).
    mapperSettings.stickChordRecenter = recenterHold > 0.0f;
    // SteamVR keeps Touch's left Menu: both sticks held + a trigger capture (capture_chord.hpp).
    mapperSettings.captureButtons = input::captureButtonsFor(xrRuntimeName(), controller);
    mapperSettings.throwGesture = cfg.throwGesture;
    mapperSettings.swing = cfg.swing;
    mapperSettings.handsJump = cfg.handsJump;
    mapperSettings.punch.thresholdMetresPerSecond = cfg.punchSpeed;
    // The hold time (the launcher's Hold time) for the buttons, and for the stick held down to open the
    // weapon wheel, which keeps its own margin over the buttons' (0.3 s against 0.25 s by default).
    mapperSettings.buttonHoldSeconds = cfg.holdSeconds;
    mapperSettings.turnStick.holdSeconds =
        cfg.holdSeconds + (input::TurnStickSettings{}.holdSeconds - input::kDefaultHoldSeconds);
    // The thumb-rest wheel, on the hands whose controllers can sense a resting thumb (scripted input stands
    // in for any).
    mapperSettings.restWheel = cfg.thumbRest;
    // The rests, face touch and the controls under each touch from the family's data
    // (family_touch_settings.hpp): a bound rest is not the wheel's, the Steam Frame's face touch is never a
    // rest.
    const input::ControllerData& data = s.controllerData[static_cast<std::size_t>(controller)];
    input::applyFamilyTouchSettings(mapperSettings, data, cfg.thumbRestFaceTouch, !cfg.testInputPath.empty());
    s.mapper = std::make_unique<input::InputMapper>(std::move(*profile), mapperSettings);
    restartRestWheelStats();
    publishPromptLabels(s.mapper->profile(), controller);
    EVR_LOG("%s: control map for %s controllers%s, %s", kTag,
            std::string(game::controllerName(controller)).c_str(), when, s.mapper->summary().c_str());
    logRestButtons(*s.mapper, mapperSettings.restSensors);
    logTouchButtons(*s.mapper);
    logRestWheel(*s.mapper, mapperSettings.restFaceTouch, mapperSettings.restSensors);
    return true;
}

void prepareControlMap(game::Controller controller) {
    State& s = state();
    std::lock_guard lock(s.mapperMutex);
    ensureMapper(s, controller, " (by the XR worker, for the menus' prompts)");
}

} // namespace evr::vkcore::controllers
