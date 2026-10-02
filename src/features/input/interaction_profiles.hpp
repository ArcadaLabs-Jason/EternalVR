#pragma once

// The inputs each supported OpenXR interaction profile has, per hand (OpenXR 1.1 specification,
// section "Interaction Profile Paths", and the registry's <interaction_profile> entries). A suggested
// binding to a path the profile does not list makes xrSuggestInteractionProfileBindings fail for the
// whole profile, so bindings data is checked against these lists before it reaches the runtime. The
// lists hold the inputs we may bind, which for some profiles is fewer than the profile has.
//
// Some profiles come with an instance extension. Their bindings may be suggested only when that
// extension is enabled, or on an OpenXR 1.1 instance when 1.1 made the profile core under the same path.

#include "features/input/controller_state.hpp"
#include "features/input/xr_action_set.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace evr::input {

struct InteractionProfileInfo {
    std::string_view path; // "/interaction_profiles/oculus/touch_controller"
    // Leaf paths relative to /user/hand/<hand>, such as "/input/trigger/value".
    std::span<const std::string_view> left;
    std::span<const std::string_view> right;
    // The instance extension that defines the profile; empty for a profile of OpenXR 1.0.
    std::string_view extension;
    // OpenXR 1.1 made the profile core under this path, so a 1.1 instance has it without the extension.
    bool coreIn11 = false;

    [[nodiscard]] std::span<const std::string_view> inputs(Hand hand) const {
        return hand == Hand::Left ? left : right;
    }
};

std::span<const InteractionProfileInfo> knownInteractionProfiles();
const InteractionProfileInfo* findInteractionProfile(std::string_view path);

// True when bindings for the profile may be suggested on an instance with these extensions enabled
// (`api11`: the instance was created for OpenXR 1.1).
bool profileAvailable(const InteractionProfileInfo& profile,
                      std::span<const std::string_view> enabledExtensions,
                      bool api11);

// The Steam Frame's bumper is /input/shoulder/ from SteamVR 2.17.10 on and was /input/bumper/ before it
// (later versions accept both). A runtime refuses all of a profile's suggested bindings for one path it
// does not know, so the Frame's are suggested again under the older name when the newer one is refused.
// Returns `bindingPath` with the older name, or nullopt when the profile or the path has none.
std::optional<std::string> olderInputName(std::string_view profilePath, std::string_view bindingPath);

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
