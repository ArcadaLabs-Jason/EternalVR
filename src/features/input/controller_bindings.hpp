#pragma once

// Controller data files (data/input/controllers/*.toml): one per controller family, holding the
// OpenXR suggested bindings for its interaction profile and the default control maps.
//
//   [profile]
//   "path" = "/interaction_profiles/oculus/touch_controller"
//   "<set>.<hand>.<action>" = "<path under /user/hand/<hand>>"   e.g. "gameplay.left.primary" =
//   "/input/x/click"
//
//   [map.right]                  a control map in binding text (binding_text.hpp), one per handedness:
//   [map.left_button_swap]       right, left_button_swap and left_full_mirror
//   [map.left_full_mirror]
//
// Each section is read with the binding-text reader, so the same lenient rules and line numbers
// apply. Suggested bindings are checked against the profile's input list (interaction_profiles.hpp)
// and against each other: two actions of one set on one physical input is a conflict, reported with
// both action keys and both input paths (BindingConflict::SharedInput), and the later one is left out.
// The control maps are returned as entries; buildBindingProfile compiles and checks them.
//
// There is no file IO here: the caller passes the text (the built-in copy in game/eternal, or a file
// from the player's profile folder).

#include "features/input/binding_issue.hpp"
#include "features/input/binding_text.hpp"
#include "features/input/controller_state.hpp"
#include "features/input/xr_action_set.hpp"
#include "game/eternal/quest_touch_bindings.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evr::input {

struct SuggestedBinding {
    XrActionId action = XrActionId::Trigger;
    Hand hand = Hand::Right;
    std::string path; // Full binding path, e.g. "/user/hand/right/input/trigger/value".

    friend bool operator==(const SuggestedBinding&, const SuggestedBinding&) = default;
};

struct ControllerData {
    std::string profilePath; // Empty if the [profile] section has no usable "path".
    // Ordered by action set, hand and action, one entry per bound action and hand.
    std::vector<SuggestedBinding> suggested;
    std::map<game::Handedness, BindingMap> maps;
    std::vector<BindingIssue> issues;

    [[nodiscard]] bool ok() const { return issues.empty(); }
    [[nodiscard]] const SuggestedBinding* find(XrActionId action, Hand hand) const;
};

ControllerData parseControllerData(std::string_view text);

// "[map.<name>]" section names.
std::string_view handednessName(game::Handedness handedness);
std::optional<game::Handedness> parseHandednessName(std::string_view name);

// "<set>.<hand>.<action>", the key a suggested binding is written under.
std::string suggestedBindingKey(XrActionId action, Hand hand);

} // namespace evr::input
