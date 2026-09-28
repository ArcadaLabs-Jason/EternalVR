#pragma once

// The inputs each supported OpenXR interaction profile has, per hand (OpenXR 1.1 specification,
// section "Interaction Profile Paths"). A suggested binding to a path the profile does not list makes
// xrSuggestInteractionProfileBindings fail for the whole profile, so bindings data is checked
// against these lists before it reaches the runtime.

#include "features/input/controller_state.hpp"
#include "features/input/xr_action_set.hpp"

#include <span>
#include <string_view>

namespace evr::input {

struct InteractionProfileInfo {
    std::string_view path; // "/interaction_profiles/oculus/touch_controller"
    // Leaf paths relative to /user/hand/<hand>, such as "/input/trigger/value".
    std::span<const std::string_view> left;
    std::span<const std::string_view> right;

    [[nodiscard]] std::span<const std::string_view> inputs(Hand hand) const {
        return hand == Hand::Left ? left : right;
    }
};

std::span<const InteractionProfileInfo> knownInteractionProfiles();
const InteractionProfileInfo* findInteractionProfile(std::string_view path);

// True when `path` is one of the hand's leaf paths, or an input identifier that has leaves (such as
// "/input/thumbstick" for "/input/thumbstick/x").
bool profileHasPath(const InteractionProfileInfo& profile, Hand hand, std::string_view path);

// True when an action of `kind` may be bound to `path` on this profile and hand: a pose action to a
// ".../pose" leaf, a haptic action to "/output/haptic", a two-axis action to an identifier with x and y
// leaves, a float action to a value, force, click or touch leaf (or an identifier that has a value), and
// a boolean action to a click, touch, value or force leaf (or an identifier with a click or a value;
// the runtime applies its own threshold to values). Axis leaves (x, y) take no single action, which
// avoids binding half a stick by mistake. `path` must pass profileHasPath.
bool pathSuitsAction(const InteractionProfileInfo& profile,
                     Hand hand,
                     std::string_view path,
                     XrActionKind kind);

// True when two bindings would drive two actions from one physical input. A leaf path covers itself;
// an identifier covers the components the runtime reads through it: x and y for a two-axis action
// ("/input/thumbstick" leaves "/input/thumbstick/click" free), and click, value and force for a button
// or analog action ("/input/trigger" overlaps "/input/trigger/value").
bool bindingsOverlap(std::string_view pathA, XrActionKind kindA, std::string_view pathB, XrActionKind kindB);

} // namespace evr::input
