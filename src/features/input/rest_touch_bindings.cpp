#include "features/input/rest_touch_bindings.hpp"

#include "features/input/interaction_profiles.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace evr::input {

namespace {

constexpr std::string_view kThumbRestLeaf = "/input/thumbrest/touch";
constexpr std::string_view kFrameProfile = "/interaction_profiles/valve/frame_controller_valve";

// The leaf a binding path of `hand` names, without the hand ("/input/a/click"), or nullopt.
std::optional<std::string> leafOf(const SuggestedBinding& binding) {
    const std::string_view prefix = handPath(binding.hand);
    if (!binding.path.starts_with(prefix)) {
        return std::nullopt;
    }
    return binding.path.substr(prefix.size());
}

// The touch leaf of the face button bound to `button` on `hand` ("/input/a/click" -> "/input/a/touch");
// nullopt when that is not an A, B, X or Y button (a trackpad or a D-pad is no place to rest a thumb).
std::optional<std::string> touchLeafOf(const ControllerData& data, XrActionId button, Hand hand) {
    const SuggestedBinding* bound = data.find(button, hand);
    const auto leaf = bound ? leafOf(*bound) : std::nullopt;
    if (!leaf) {
        return std::nullopt;
    }
    for (const std::string_view face : {"/input/a", "/input/b", "/input/x", "/input/y"}) {
        if (*leaf == face || leaf->starts_with(std::string(face) + "/")) {
            return std::string(face) + "/touch";
        }
    }
    return std::nullopt;
}

bool pathFree(const ControllerData& data, Hand hand, const std::string& path) {
    return std::ranges::none_of(data.suggested, [&](const SuggestedBinding& b) {
        return b.hand == hand &&
               bindingsOverlap(b.path, xrAction(b.action).kind, path, XrActionKind::Boolean);
    });
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

bool isRestTouchAction(XrActionId action) {
    return action == XrActionId::ThumbRest || action == XrActionId::PrimaryTouch ||
           action == XrActionId::SecondaryTouch;
}

std::size_t addRestTouchBindings(ControllerData& data, RestTouchOptions options) {
    const InteractionProfileInfo* profile = findInteractionProfile(data.profilePath);
    if (!profile) {
        return 0;
    }
    std::size_t added = 0;
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        if (options.thumbRest &&
            tryAdd(data, *profile, XrActionId::ThumbRest, hand, std::string(kThumbRestLeaf))) {
            ++added;
        }
        const bool faceTouch = options.thumbRest && options.faceTouch && data.profilePath != kFrameProfile &&
                               !profileHasPath(*profile, hand, kThumbRestLeaf);
        if (!faceTouch) {
            continue;
        }
        for (const auto& [touch, button] : {std::pair{XrActionId::PrimaryTouch, XrActionId::Primary},
                                            std::pair{XrActionId::SecondaryTouch, XrActionId::Secondary}}) {
            if (tryAdd(data, *profile, touch, hand, touchLeafOf(data, button, hand))) {
                ++added;
            }
        }
    }
    if (added > 0) {
        std::ranges::stable_sort(data.suggested, [](const SuggestedBinding& a, const SuggestedBinding& b) {
            return std::tuple{static_cast<int>(xrAction(a.action).set), static_cast<int>(a.hand),
                              static_cast<int>(a.action)} <
                   std::tuple{static_cast<int>(xrAction(b.action).set), static_cast<int>(b.hand),
                              static_cast<int>(b.action)};
        });
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

bool handHasRest(const ControllerData& data, Hand hand, bool faceTouch) {
    return data.find(XrActionId::ThumbRest, hand) != nullptr ||
           (faceTouch && (data.find(XrActionId::PrimaryTouch, hand) != nullptr ||
                          data.find(XrActionId::SecondaryTouch, hand) != nullptr));
}

} // namespace evr::input
