#pragma once

// Checks binding entries and compiles them into a BindingProfile.
//
// Entries with problems are reported and left out, so a mostly-good set still gives a usable profile;
// the caller decides whether to use it or fall back to the built-in map. Conflicts reported:
//   - a `press` binding together with `tap` or `hold` on the same input: the press would fire on
//     every tap and hold. The press binding is kept.
//   - both sticks given the same role. The left stick keeps it.
//   - a stick gesture on a stick that is not the turn stick. The gesture is dropped.
// Each conflict names both keys and what each binds (BindingIssue::conflict, value, otherKey and
// otherValue, and the message), so the player sees which two actions on which two inputs clash (T-106).
// Binding one action to several inputs is allowed and not reported.

#include "features/input/binding_issue.hpp"
#include "features/input/binding_profile.hpp"
#include "features/input/binding_text.hpp"

#include <string_view>
#include <vector>

namespace evr::input {

// Written as a value, unbinds the key. Mostly useful in overrides (binding_overrides.hpp).
inline constexpr std::string_view kUnboundValue = "none";

struct BindingBuildResult {
    BindingProfile profile;
    std::vector<BindingIssue> issues;

    [[nodiscard]] bool ok() const { return issues.empty(); }
};

BindingBuildResult buildBindingProfile(const BindingMap& entries);

// The entries that compile back to `profile`. Unassigned sticks and absent bindings are left out.
BindingMap toBindingMap(const BindingProfile& profile);

} // namespace evr::input
