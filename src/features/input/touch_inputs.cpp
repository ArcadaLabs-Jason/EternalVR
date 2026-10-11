#include "features/input/touch_inputs.hpp"

#include "features/input/interaction_profiles.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string_view>

namespace evr::input {

namespace {

constexpr std::string_view kTouch = "/touch";

// The leaf a binding of `hand` names, without the hand ("/input/a/click"), or empty.
std::string_view leafOf(const SuggestedBinding& binding) {
    const std::string_view prefix = handPath(binding.hand);
    const std::string_view path = binding.path;
    return path.starts_with(prefix) ? path.substr(prefix.size()) : std::string_view{};
}

// `leaf` is `identifier` or one of its components ("/input/a/click" under "/input/a").
bool underIdentifier(std::string_view leaf, std::string_view identifier) {
    return leaf == identifier ||
           (leaf.starts_with(identifier) && leaf.substr(identifier.size()).starts_with('/'));
}

// The inputs on a controller's face, under the thumb, of every family (interaction_profiles.cpp): the face
// buttons, the stick, the trackpad, the Steam Frame's D-pad and the Menu and View buttons (Index's left Menu
// is its trackpad pressed).
constexpr std::array<std::string_view, 12> kThumbIdentifiers{
    "/input/a",          "/input/b",          "/input/x",       "/input/y",
    "/input/thumbstick", "/input/trackpad",   "/input/dpad_up", "/input/dpad_down",
    "/input/dpad_left",  "/input/dpad_right", "/input/menu",    "/input/view",
};

bool underThumb(std::string_view leaf) {
    return std::ranges::any_of(
        kThumbIdentifiers, [leaf](std::string_view identifier) { return underIdentifier(leaf, identifier); });
}

// The A, B, X or Y button bound to `button` on `hand` ("/input/a"), or empty: a trackpad or a D-pad bound
// as a face button has no face-button touch of its own (the trackpad's is TrackpadTouch).
std::string faceIdentifier(const ControllerData& data, XrActionId button, Hand hand) {
    const SuggestedBinding* bound = data.find(button, hand);
    const std::string_view leaf = bound ? leafOf(*bound) : std::string_view{};
    for (const std::string_view face : {"/input/a", "/input/b", "/input/x", "/input/y"}) {
        if (underIdentifier(leaf, face)) {
            return std::string(face);
        }
    }
    return {};
}

// The touch's identifier ("/input/a"), or empty.
std::string touchIdentifier(const ControllerData& data, Hand hand, ButtonInput input) {
    switch (input) {
    case ButtonInput::ThumbRest:
        return "/input/thumbrest";
    case ButtonInput::PrimaryTouch:
        return faceIdentifier(data, XrActionId::Primary, hand);
    case ButtonInput::SecondaryTouch:
        return faceIdentifier(data, XrActionId::Secondary, hand);
    case ButtonInput::StickTouch:
        return "/input/thumbstick";
    case ButtonInput::TrackpadTouch:
        return "/input/trackpad";
    case ButtonInput::TriggerTouch:
        return "/input/trigger";
    case ButtonInput::Trigger:
    case ButtonInput::Grip:
    case ButtonInput::StickClick:
    case ButtonInput::Primary:
    case ButtonInput::Secondary:
    case ButtonInput::Face3:
    case ButtonInput::Face4:
    case ButtonInput::Shoulder:
    case ButtonInput::Menu:
    case ButtonInput::Count:
        break;
    }
    return {};
}

// The gameplay controls a touch can sit on.
constexpr std::array kControls{
    XrActionId::Trigger,  XrActionId::Grip,      XrActionId::Thumbstick, XrActionId::ThumbstickClick,
    XrActionId::Primary,  XrActionId::Secondary, XrActionId::Face3,      XrActionId::Face4,
    XrActionId::Shoulder, XrActionId::Menu};

} // namespace

std::optional<XrActionId> touchAction(ButtonInput input) {
    switch (input) {
    case ButtonInput::ThumbRest:
        return XrActionId::ThumbRest;
    case ButtonInput::PrimaryTouch:
        return XrActionId::PrimaryTouch;
    case ButtonInput::SecondaryTouch:
        return XrActionId::SecondaryTouch;
    case ButtonInput::StickTouch:
        return XrActionId::ThumbstickTouch;
    case ButtonInput::TrackpadTouch:
        return XrActionId::TrackpadTouch;
    case ButtonInput::TriggerTouch:
        return XrActionId::TriggerTouch;
    case ButtonInput::Trigger:
    case ButtonInput::Grip:
    case ButtonInput::StickClick:
    case ButtonInput::Primary:
    case ButtonInput::Secondary:
    case ButtonInput::Face3:
    case ButtonInput::Face4:
    case ButtonInput::Shoulder:
    case ButtonInput::Menu:
    case ButtonInput::Count:
        break;
    }
    return std::nullopt;
}

bool touchSensed(const HandState& hand, ButtonInput input) {
    switch (input) {
    case ButtonInput::ThumbRest:
        return hand.thumbRest;
    case ButtonInput::PrimaryTouch:
        return hand.primaryTouch;
    case ButtonInput::SecondaryTouch:
        return hand.secondaryTouch;
    case ButtonInput::StickTouch:
        return hand.stickTouch;
    case ButtonInput::TrackpadTouch:
        return hand.trackpadTouch;
    case ButtonInput::TriggerTouch:
        return hand.triggerTouch;
    case ButtonInput::Trigger:
    case ButtonInput::Grip:
    case ButtonInput::StickClick:
    case ButtonInput::Primary:
    case ButtonInput::Secondary:
    case ButtonInput::Face3:
    case ButtonInput::Face4:
    case ButtonInput::Shoulder:
    case ButtonInput::Menu:
    case ButtonInput::Count:
        break;
    }
    return false;
}

std::optional<std::string> touchLeaf(const ControllerData& data, Hand hand, ButtonInput input) {
    const std::string identifier = touchIdentifier(data, hand, input);
    if (identifier.empty()) {
        return std::nullopt;
    }
    std::string leaf = identifier + std::string(kTouch);
    const InteractionProfileInfo* profile = findInteractionProfile(data.profilePath);
    if (profile != nullptr && !profileHasPath(*profile, hand, leaf)) {
        return std::nullopt;
    }
    return leaf;
}

OwnControls defaultOwnControls(ButtonInput touch) {
    OwnControls own;
    if (isThumbTouch(touch)) {
        for (const XrActionId control : {XrActionId::Primary, XrActionId::Secondary, XrActionId::Thumbstick,
                                         XrActionId::ThumbstickClick}) {
            own.add(control);
        }
    } else if (touch == ButtonInput::TriggerTouch) {
        own.add(XrActionId::Trigger);
    }
    return own;
}

OwnControls ownControlsOf(const ControllerData& data, Hand hand, ButtonInput touch) {
    if (isThumbTouch(touch)) {
        // Every control under the thumb: whichever of them the thumb goes on to use, it has left the touch.
        OwnControls own;
        for (const XrActionId control : kControls) {
            const SuggestedBinding* bound = data.find(control, hand);
            if (bound != nullptr && underThumb(leafOf(*bound))) {
                own.add(control);
            }
        }
        return own;
    }
    return landingControlsOf(data, hand, touch);
}

OwnControls defaultLandingControls(ButtonInput touch) {
    OwnControls own;
    switch (touch) {
    case ButtonInput::PrimaryTouch:
        own.add(XrActionId::Primary);
        break;
    case ButtonInput::SecondaryTouch:
        own.add(XrActionId::Secondary);
        break;
    case ButtonInput::StickTouch:
        own.add(XrActionId::Thumbstick);
        own.add(XrActionId::ThumbstickClick);
        break;
    case ButtonInput::TriggerTouch:
        own.add(XrActionId::Trigger);
        break;
    case ButtonInput::TrackpadTouch:
    case ButtonInput::ThumbRest:
    case ButtonInput::Trigger:
    case ButtonInput::Grip:
    case ButtonInput::StickClick:
    case ButtonInput::Primary:
    case ButtonInput::Secondary:
    case ButtonInput::Face3:
    case ButtonInput::Face4:
    case ButtonInput::Shoulder:
    case ButtonInput::Menu:
    case ButtonInput::Count:
        break;
    }
    return own;
}

OwnControls landingControlsOf(const ControllerData& data, Hand hand, ButtonInput touch) {
    const std::optional<std::string> leaf = touchLeaf(data, hand, touch);
    if (!leaf) {
        return defaultLandingControls(touch);
    }
    const std::string_view identifier = std::string_view(*leaf).substr(0, leaf->size() - kTouch.size());
    OwnControls own;
    for (const XrActionId control : kControls) {
        const SuggestedBinding* bound = data.find(control, hand);
        if (bound != nullptr && underIdentifier(leafOf(*bound), identifier)) {
            own.add(control);
        }
    }
    return own;
}

bool ownControlUsed(const HandState& hand, OwnControls own, OwnControlLevels levels) {
    return (own.has(XrActionId::Trigger) && levels.trigger) || (own.has(XrActionId::Grip) && levels.grip) ||
           (own.has(XrActionId::Thumbstick) && magnitude(hand.stick) > kOwnStickOut) ||
           (own.has(XrActionId::ThumbstickClick) && hand.stickClick) ||
           (own.has(XrActionId::Primary) && hand.primaryButton) ||
           (own.has(XrActionId::Secondary) && hand.secondaryButton) ||
           (own.has(XrActionId::Face3) && hand.face3Button) ||
           (own.has(XrActionId::Face4) && hand.face4Button) ||
           (own.has(XrActionId::Shoulder) && hand.shoulderButton) ||
           (own.has(XrActionId::Menu) && hand.menuButton);
}

} // namespace evr::input
