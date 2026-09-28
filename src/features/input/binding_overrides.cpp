#include "features/input/binding_overrides.hpp"

#include "features/input/binding_keys.hpp"

#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace evr::input {

BindingMap applyBindingOverrides(const BindingMap& base, const BindingMap& overrides) {
    BindingMap merged = base;
    for (const auto& [key, value] : overrides) {
        if (value == kUnboundValue) {
            merged.erase(key);
        } else {
            merged[key] = value;
        }
    }
    return merged;
}

BindingMap bindingOverrides(const BindingMap& base, const BindingMap& edited) {
    BindingMap overrides;
    for (const auto& [key, value] : edited) {
        const auto inBase = base.find(key);
        if (inBase == base.end() || inBase->second != value) {
            overrides[key] = value;
        }
    }
    for (const auto& [key, value] : base) {
        if (!edited.contains(key)) {
            overrides[key] = std::string(kUnboundValue);
        }
    }
    return overrides;
}

BindingBuildResult resolveBindings(const BindingMap& base, std::string_view overrideText) {
    BindingTextResult parsed = parseBindingText(overrideText);

    // Every override key is checked here, whatever its value. An unbinding override never reaches
    // the compiler, so without this a mistyped key set to "none" would be dropped silently while
    // the binding the player meant to remove stays active.
    BindingMap validOverrides;
    std::vector<BindingIssue> keyIssues;
    for (const auto& [key, value] : parsed.entries) {
        if (parseBindingKey(key)) {
            validOverrides.emplace(key, value);
        } else {
            keyIssues.push_back(issueOf(BindingIssueKind::UnknownKey, key, parsed.lineOf.at(key),
                                        "'" + key + "' is not a binding key; the override is ignored"));
        }
    }
    parsed.issues.insert(parsed.issues.end(), std::make_move_iterator(keyIssues.begin()),
                         std::make_move_iterator(keyIssues.end()));

    BindingBuildResult result = buildBindingProfile(applyBindingOverrides(base, validOverrides));

    // Point compile issues for overridden keys at the line that set them.
    for (BindingIssue& issue : result.issues) {
        if (const auto line = parsed.lineOf.find(issue.key); line != parsed.lineOf.end()) {
            issue.line = line->second;
        }
    }
    parsed.issues.insert(parsed.issues.end(), std::make_move_iterator(result.issues.begin()),
                         std::make_move_iterator(result.issues.end()));
    result.issues = std::move(parsed.issues);
    return result;
}

} // namespace evr::input
