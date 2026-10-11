#include "features/input/rest_touch_bindings.hpp"

#include "features/input/binding_compiler.hpp"
#include "features/input/binding_keys.hpp"
#include "features/input/interaction_profiles.hpp"
#include "features/input/touch_inputs.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace evr::input {

namespace {

constexpr std::string_view kThumbRestLeaf = "/input/thumbrest/touch";
constexpr std::string_view kStickTouchLeaf = "/input/thumbstick/touch";
constexpr std::string_view kTrackpadTouchLeaf = "/input/trackpad/touch";
constexpr std::string_view kFrameProfile = "/interaction_profiles/valve/frame_controller_valve";

bool pathFree(const ControllerData& data, Hand hand, const std::string& path) {
    return std::ranges::none_of(data.suggested, [&](const SuggestedBinding& b) {
        return b.hand == hand &&
               bindingsOverlap(b.path, xrAction(b.action).kind, path, XrActionKind::Boolean);
    });
}

void sortSuggested(ControllerData& data) {
    std::ranges::stable_sort(data.suggested, [](const SuggestedBinding& a, const SuggestedBinding& b) {
        return std::tuple{static_cast<int>(xrAction(a.action).set), static_cast<int>(a.hand),
                          static_cast<int>(a.action)} < std::tuple{static_cast<int>(xrAction(b.action).set),
                                                                   static_cast<int>(b.hand),
                                                                   static_cast<int>(b.action)};
    });
}

// Calls `visit` with each button key of `data`'s maps that binds an action (any press kind).
template <typename Visit>
void forBoundButtons(const ControllerData& data, Visit visit) {
    for (const auto& [handedness, map] : data.maps) {
        for (const auto& [key, value] : map) {
            const auto parsed = parseBindingKey(key);
            const auto* button = parsed ? std::get_if<ButtonKey>(&*parsed) : nullptr;
            if (button != nullptr && value != kUnboundValue) {
                visit(*button);
            }
        }
    }
}

bool tryAdd(ControllerData& data,
            const InteractionProfileInfo& profile,
            XrActionId action,
            Hand hand,
            const std::optional<std::string>& leaf) {
    if (!leaf || data.find(action, hand) || !profileHasPath(profile, hand, *leaf) ||
        !pathSuitsAction(profile, hand, *leaf, XrActionKind::Boolean)) {
        return false;
    }
    const std::string path = std::string(handPath(hand)) + *leaf;
    if (!pathFree(data, hand, path)) {
        return false;
    }
    data.suggested.push_back({action, hand, path});
    return true;
}

} // namespace

bool bindsThumbRest(const ControllerData& data) {
    bool bound = false;
    forBoundButtons(data,
                    [&](const ButtonKey& key) { bound = bound || key.input == ButtonInput::ThumbRest; });
    return bound;
}

bool bindsTouch(const ControllerData& data, Hand hand, ButtonInput touch) {
    bool bound = false;
    forBoundButtons(data,
                    [&](const ButtonKey& key) { bound = bound || (key.hand == hand && key.input == touch); });
    return bound;
}

RestTouchOptions restTouchOptions(const ControllerData& data, bool wheelOn, bool faceTouch) {
    // A face button's touch is a rest only for the wheel: a bound rest is the thumb rest itself.
    return {wheelOn || bindsThumbRest(data), faceTouchRests(data, faceTouch) && wheelOn};
}

bool faceTouchRests(const ControllerData& data, bool faceTouch) {
    return faceTouch && data.profilePath != kFrameProfile;
}

std::array<bool, 2> wheelRestHands(const ControllerData& data, bool faceTouch) {
    const bool face = faceTouchRests(data, faceTouch);
    return {handHasRest(data, Hand::Left, face), handHasRest(data, Hand::Right, face)};
}

bool isRestTouchAction(XrActionId action) {
    return action == XrActionId::ThumbRest || action == XrActionId::PrimaryTouch ||
           action == XrActionId::SecondaryTouch || action == XrActionId::ThumbstickTouch ||
           action == XrActionId::TrackpadTouch || action == XrActionId::TriggerTouch;
}

std::size_t addRestTouchBindings(ControllerData& data, RestTouchOptions options) {
    const InteractionProfileInfo* profile = findInteractionProfile(data.profilePath);
    if (!profile) {
        return 0;
    }
    // The Steam Frame's bumper holds the wheel: no rest of any kind there.
    if (!options.thumbRest || data.profilePath == kFrameProfile) {
        return 0;
    }
    constexpr std::array kFaceTouch{std::pair{XrActionId::PrimaryTouch, ButtonInput::PrimaryTouch},
                                    std::pair{XrActionId::SecondaryTouch, ButtonInput::SecondaryTouch}};
    std::size_t added = 0;
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        const bool thumbRest = profileHasPath(*profile, hand, kThumbRestLeaf);
        if (thumbRest && tryAdd(data, *profile, XrActionId::ThumbRest, hand, std::string(kThumbRestLeaf))) {
            ++added;
        }
        // The face buttons' touch: the rest itself on a controller without a thumb rest (face touch on), the
        // thumb elsewhere beside a thumb rest.
        const bool faceTouch = options.faceTouch && !thumbRest;
        if (faceTouch || data.find(XrActionId::ThumbRest, hand) != nullptr) {
            for (const auto& [touch, input] : kFaceTouch) {
                if (tryAdd(data, *profile, touch, hand, touchLeaf(data, hand, input))) {
                    ++added;
                }
            }
        }
        // The stick's touch, on a hand that has a rest to tell it from; beside face touch the trackpad's too.
        if (handHasRest(data, hand, faceTouch) &&
            tryAdd(data, *profile, XrActionId::ThumbstickTouch, hand, std::string(kStickTouchLeaf))) {
            ++added;
        }
        if (faceTouch && handHasRest(data, hand, true) &&
            tryAdd(data, *profile, XrActionId::TrackpadTouch, hand, std::string(kTrackpadTouchLeaf))) {
            ++added;
        }
    }
    if (added > 0) {
        sortSuggested(data);
    }
    return added;
}

std::size_t addBoundTouchBindings(ControllerData& data) {
    const InteractionProfileInfo* profile = findInteractionProfile(data.profilePath);
    if (!profile) {
        return 0;
    }
    std::size_t added = 0;
    forBoundButtons(data, [&](const ButtonKey& key) {
        const auto action = touchAction(key.input);
        if (action && isControlTouch(key.input) &&
            tryAdd(data, *profile, *action, key.hand, touchLeaf(data, key.hand, key.input))) {
            ++added;
        }
    });
    if (added > 0) {
        sortSuggested(data);
    }
    return added;
}

ControllerData withoutRestTouch(ControllerData data) {
    std::erase_if(data.suggested, [](const SuggestedBinding& b) { return isRestTouchAction(b.action); });
    return data;
}

bool hasRestTouchBindings(const ControllerData& data) {
    return std::ranges::any_of(data.suggested,
                               [](const SuggestedBinding& b) { return isRestTouchAction(b.action); });
}

namespace {

// A control map of `data` binds the touch `action` reads on `hand` like a button.
bool boundAsButton(const ControllerData& data, XrActionId action, Hand hand) {
    for (const ButtonInput touch : kControlTouches) {
        if (touchAction(touch) == action) {
            return bindsTouch(data, hand, touch);
        }
    }
    return false;
}

// The bindings that only say the thumb is elsewhere: the stick's and the trackpad's touch, and the face
// buttons' touch on a hand with a thumb rest; not one a control map binds like a button, which is kept.
// `thumbRests`: the hands with a thumb rest binding.
bool isVetoTouch(const ControllerData& data,
                 const SuggestedBinding& b,
                 const std::array<bool, 2>& thumbRests) {
    const bool veto = b.action == XrActionId::ThumbstickTouch || b.action == XrActionId::TrackpadTouch ||
                      ((b.action == XrActionId::PrimaryTouch || b.action == XrActionId::SecondaryTouch) &&
                       thumbRests[static_cast<std::size_t>(b.hand)]);
    return veto && !boundAsButton(data, b.action, b.hand);
}

std::array<bool, 2> thumbRestHands(const ControllerData& data) {
    return {data.find(XrActionId::ThumbRest, Hand::Left) != nullptr,
            data.find(XrActionId::ThumbRest, Hand::Right) != nullptr};
}

// A binding of a touch on a control that a control map binds like a button.
bool isBoundTouch(const ControllerData& data, const SuggestedBinding& b) {
    return boundAsButton(data, b.action, b.hand);
}

} // namespace

ControllerData withoutVetoTouch(ControllerData data) {
    const std::array<bool, 2> thumbRests = thumbRestHands(data);
    // The predicate reads the maps only, which the erase leaves alone.
    std::erase_if(data.suggested,
                  [&](const SuggestedBinding& b) { return isVetoTouch(data, b, thumbRests); });
    return data;
}

bool hasVetoTouchBindings(const ControllerData& data) {
    const std::array<bool, 2> thumbRests = thumbRestHands(data);
    return std::ranges::any_of(data.suggested,
                               [&](const SuggestedBinding& b) { return isVetoTouch(data, b, thumbRests); });
}

ControllerData withoutTouchButtons(ControllerData data) {
    const std::array<bool, 2> thumbRests = thumbRestHands(data);
    std::erase_if(data.suggested, [&](const SuggestedBinding& b) {
        return isVetoTouch(data, b, thumbRests) || isBoundTouch(data, b);
    });
    return data;
}

bool hasTouchButtonBindings(const ControllerData& data) {
    return std::ranges::any_of(data.suggested,
                               [&](const SuggestedBinding& b) { return isBoundTouch(data, b); });
}

std::vector<TouchRetryStep> touchRetries(const ControllerData& data) {
    // Each step leaves out what the one before kept and more; a step that leaves out nothing more is skipped.
    std::vector<TouchRetryStep> steps;
    std::size_t kept = data.suggested.size();
    const auto add = [&](TouchRetry retry, ControllerData candidate) {
        if (candidate.suggested.size() < kept) {
            kept = candidate.suggested.size();
            steps.push_back({retry, std::move(candidate)});
        }
    };
    add(TouchRetry::WithoutVeto, withoutVetoTouch(data));
    add(TouchRetry::WithoutTouchButtons, withoutTouchButtons(data));
    add(TouchRetry::WithoutRestTouch, withoutRestTouch(data));
    return steps;
}

bool handHasRest(const ControllerData& data, Hand hand, bool faceTouch) {
    return data.find(XrActionId::ThumbRest, hand) != nullptr ||
           (faceTouch && (data.find(XrActionId::PrimaryTouch, hand) != nullptr ||
                          data.find(XrActionId::SecondaryTouch, hand) != nullptr));
}

} // namespace evr::input
