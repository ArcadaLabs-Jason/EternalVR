#include "features/input/controller_bindings.hpp"

#include "features/input/binding_keys.hpp"
#include "features/input/button_labels.hpp"
#include "features/input/interaction_profiles.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <optional>
#include <tuple>
#include <utility>

namespace evr::input {

namespace {

constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";
constexpr std::string_view kProfileSection = "profile";
constexpr std::string_view kMapSectionPrefix = "map.";
constexpr std::string_view kLabelsSection = "labels";
constexpr std::string_view kPathKey = "path";
// Earlier versions also suggested bindings for a "menu" action set that was never synced; a player's file
// copied from them still has its keys, which are skipped.
constexpr std::string_view kRetiredSetPrefix = "menu.";

constexpr std::array<std::pair<game::Handedness, std::string_view>, 3> kHandednessNames{{
    {game::Handedness::Right, "right"},
    {game::Handedness::LeftButtonSwap, "left_button_swap"},
    {game::Handedness::LeftButtonAndStickSwap, "left_full_mirror"},
}};

std::string_view trim(std::string_view text) {
    constexpr std::string_view kSpace = " \t\r";
    const std::size_t first = text.find_first_not_of(kSpace);
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(kSpace) - first + 1);
}

std::string quoted(std::string_view text) {
    return "'" + std::string(text) + "'";
}

std::string_view kindName(XrActionKind kind) {
    switch (kind) {
    case XrActionKind::Boolean:
        return "a button";
    case XrActionKind::Float:
        return "an analog";
    case XrActionKind::Vector2:
        return "a two-axis";
    case XrActionKind::Pose:
        return "a pose";
    case XrActionKind::Haptic:
        return "a vibration";
    }
    return "an";
}

// The file split into sections. Every section's text keeps the file's line count, with the lines of
// other sections blanked, so the binding-text reader reports file line numbers.
struct Sections {
    std::vector<std::pair<std::string, std::string>> texts; // in order of first appearance
    std::vector<BindingIssue> issues;
};

Sections splitSections(std::string_view text) {
    if (text.starts_with(kUtf8Bom)) {
        text.remove_prefix(kUtf8Bom.size());
    }
    std::vector<std::string_view> lines;
    for (std::size_t start = 0; start <= text.size();) {
        const std::size_t newline = text.find('\n', start);
        lines.push_back(text.substr(start, newline - start));
        start = newline == std::string_view::npos ? text.size() + 1 : newline + 1;
    }
    Sections sections;
    std::vector<std::ptrdiff_t> owner(lines.size(), -1);
    std::ptrdiff_t current = -1;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string_view line = trim(lines[i]);
        const int lineNumber = static_cast<int>(i) + 1;
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (line.front() != '[') {
            if (current < 0) {
                sections.issues.push_back(
                    issueOf(BindingIssueKind::Syntax, {}, lineNumber,
                            "entries must be under a [profile], [map.<handedness>] or [labels] header"));
            }
            owner[i] = current;
            continue;
        }
        const std::string_view header = trim(line.substr(0, line.find('#')));
        if (header.size() < 3 || header.back() != ']') {
            sections.issues.push_back(
                issueOf(BindingIssueKind::Syntax, {}, lineNumber, "malformed section header"));
            current = -1;
            continue;
        }
        const std::string name(trim(header.substr(1, header.size() - 2)));
        const auto it = std::ranges::find(sections.texts, name, &std::pair<std::string, std::string>::first);
        if (it != sections.texts.end()) {
            sections.issues.push_back(issueOf(BindingIssueKind::DuplicateKey, name, lineNumber,
                                              "[" + name + "] appears more than once"));
            current = it - sections.texts.begin();
            continue;
        }
        current = static_cast<std::ptrdiff_t>(sections.texts.size());
        sections.texts.emplace_back(name, std::string{});
    }
    for (std::size_t s = 0; s < sections.texts.size(); ++s) {
        std::string& out = sections.texts[s].second;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (owner[i] == static_cast<std::ptrdiff_t>(s)) {
                out += lines[i];
            }
            out += '\n';
        }
    }
    return sections;
}

struct Candidate {
    SuggestedBinding binding;
    std::string key;
    int line = 0;
};

class ProfileReader {
public:
    explicit ProfileReader(ControllerData& data) : data_(data) {}

    void read(const std::string& sectionText) {
        BindingTextResult text = parseBindingText(sectionText);
        appendIssues(std::move(text.issues));
        const auto lineOf = [&text](const std::string& key) {
            const auto it = text.lineOf.find(key);
            return it == text.lineOf.end() ? 0 : it->second;
        };
        const InteractionProfileInfo* profile = readPath(text.entries, lineOf(std::string(kPathKey)));
        std::vector<Candidate> candidates;
        for (const auto& [key, value] : text.entries) {
            if (key == kPathKey || key.starts_with(kRetiredSetPrefix)) {
                continue;
            }
            if (auto candidate = readBinding(profile, key, value, lineOf(key))) {
                candidates.push_back(std::move(*candidate));
            }
        }
        keepWithoutConflicts(std::move(candidates));
    }

private:
    void issue(BindingIssueKind kind, const std::string& key, int line, std::string message) {
        data_.issues.push_back(issueOf(kind, key, line, std::move(message)));
    }

    void appendIssues(std::vector<BindingIssue> issues) {
        std::ranges::move(issues, std::back_inserter(data_.issues));
    }

    const InteractionProfileInfo* readPath(const BindingMap& entries, int line) {
        const auto it = entries.find(kPathKey);
        if (it == entries.end()) {
            issue(BindingIssueKind::Syntax, std::string(kPathKey), 0,
                  "[profile] has no \"path\" naming its interaction profile");
            return nullptr;
        }
        data_.profilePath = it->second;
        const InteractionProfileInfo* profile = findInteractionProfile(it->second);
        if (profile == nullptr) {
            issue(BindingIssueKind::UnknownValue, it->first, line,
                  quoted(it->second) + " is not an interaction profile we know; its paths are not checked");
        }
        return profile;
    }

    std::optional<Candidate> readBinding(const InteractionProfileInfo* profile,
                                         const std::string& key,
                                         const std::string& value,
                                         int line) {
        const std::size_t first = key.find('.');
        const std::size_t second = first == std::string::npos ? first : key.find('.', first + 1);
        std::optional<XrActionSetId> set;
        std::optional<Hand> hand;
        std::optional<XrActionId> action;
        if (second != std::string::npos) {
            const std::string_view view(key);
            set = findXrActionSet(view.substr(0, first));
            hand = parseHand(view.substr(first + 1, second - first - 1));
            action = set ? findXrAction(*set, view.substr(second + 1)) : std::nullopt;
        }
        if (!hand || !action) {
            issue(BindingIssueKind::UnknownKey, key, line,
                  quoted(key) + " is not a suggested-binding key; use <set>.<hand>.<action>");
            return std::nullopt;
        }
        if (!value.starts_with("/input/") && value != "/output/haptic") {
            issue(BindingIssueKind::UnknownValue, key, line,
                  quoted(value) + " is not an input path; paths start with /input/ or are /output/haptic");
            return std::nullopt;
        }
        const XrActionDef& def = xrAction(*action);
        if (profile != nullptr) {
            if (!profileHasPath(*profile, *hand, value)) {
                issue(BindingIssueKind::UnknownValue, key, line,
                      quoted(value) + " is not an input of the " + std::string(handName(*hand)) +
                          " controller in " + std::string(profile->path));
                return std::nullopt;
            }
            if (!pathSuitsAction(*profile, *hand, value, def.kind)) {
                issue(BindingIssueKind::UnknownValue, key, line,
                      quoted(value) + " does not suit " + quoted(key) + ", " +
                          std::string(kindName(def.kind)) + " action");
                return std::nullopt;
            }
        }
        return Candidate{{*action, *hand, std::string(handPath(*hand)) + value}, key, line};
    }

    void keepWithoutConflicts(std::vector<Candidate> candidates) {
        const auto order = [](const Candidate& c) {
            return std::tuple{static_cast<int>(xrAction(c.binding.action).set),
                              static_cast<int>(c.binding.hand), static_cast<int>(c.binding.action)};
        };
        std::ranges::sort(candidates,
                          [&order](const Candidate& a, const Candidate& b) { return order(a) < order(b); });
        std::vector<const Candidate*> kept;
        for (const Candidate& candidate : candidates) {
            const XrActionSetId set = xrAction(candidate.binding.action).set;
            const auto clash = std::ranges::find_if(kept, [&](const Candidate* other) {
                return xrAction(other->binding.action).set == set &&
                       other->binding.hand == candidate.binding.hand &&
                       bindingsOverlap(other->binding.path, xrAction(other->binding.action).kind,
                                       candidate.binding.path, xrAction(candidate.binding.action).kind);
            });
            if (clash != kept.end()) {
                reportShared(candidate, **clash, set);
                continue;
            }
            kept.push_back(&candidate);
            data_.suggested.push_back(candidate.binding);
        }
    }

    void reportShared(const Candidate& dropped, const Candidate& kept, XrActionSetId set) {
        BindingIssue shared;
        shared.kind = BindingIssueKind::Conflict;
        shared.key = dropped.key;
        shared.line = dropped.line;
        shared.conflict = BindingConflict::SharedInput;
        shared.value = dropped.binding.path;
        shared.otherKey = kept.key;
        shared.otherValue = kept.binding.path;
        shared.message = quoted(dropped.key) + " (" + dropped.binding.path + ") conflicts with " +
                         quoted(kept.key) + " (" + kept.binding.path + "): one input would drive two " +
                         std::string(xrActionSet(set).name) + " actions; " + quoted(dropped.key) +
                         " is ignored";
        data_.issues.push_back(std::move(shared));
    }

    ControllerData& data_;
};

void readLabels(ControllerData& data, const std::string& sectionText) {
    BindingTextResult text = parseBindingText(sectionText);
    std::ranges::move(text.issues, std::back_inserter(data.issues));
    for (auto& [key, value] : text.entries) {
        const auto lineIt = text.lineOf.find(key);
        const int line = lineIt == text.lineOf.end() ? 0 : lineIt->second;
        if (!parseLabelKey(key)) {
            data.issues.push_back(issueOf(BindingIssueKind::UnknownKey, key, line,
                                          quoted(key) + " is not a label key; use <hand>.<input>, e.g. "
                                                        "\"left.primary\""));
            continue;
        }
        if (trim(value).empty()) {
            data.issues.push_back(
                issueOf(BindingIssueKind::UnknownValue, key, line, quoted(key) + " has an empty name"));
            continue;
        }
        data.labels.emplace(key, std::string(trim(value)));
    }
}

} // namespace

const SuggestedBinding* ControllerData::find(XrActionId action, Hand hand) const {
    const auto it = std::ranges::find_if(suggested, [action, hand](const SuggestedBinding& binding) {
        return binding.action == action && binding.hand == hand;
    });
    return it == suggested.end() ? nullptr : &*it;
}

ControllerData parseControllerData(std::string_view text) {
    ControllerData data;
    Sections sections = splitSections(text);
    data.issues = std::move(sections.issues);
    bool sawProfile = false;
    for (const auto& [name, sectionText] : sections.texts) {
        if (name == kProfileSection) {
            sawProfile = true;
            ProfileReader(data).read(sectionText);
            continue;
        }
        if (name == kLabelsSection) {
            readLabels(data, sectionText);
            continue;
        }
        const std::optional<game::Handedness> handedness =
            name.starts_with(kMapSectionPrefix)
                ? parseHandednessName(std::string_view(name).substr(kMapSectionPrefix.size()))
                : std::nullopt;
        if (!handedness) {
            data.issues.push_back(
                issueOf(BindingIssueKind::UnknownKey, name, 0,
                        "[" + name +
                            "] is not a section of controller data; use [profile], [labels] or "
                            "[map.right], [map.left_button_swap], [map.left_full_mirror]"));
            continue;
        }
        BindingTextResult map = parseBindingText(sectionText);
        std::ranges::move(map.issues, std::back_inserter(data.issues));
        data.maps[*handedness] = std::move(map.entries);
    }
    if (!sawProfile) {
        data.issues.push_back(
            issueOf(BindingIssueKind::Syntax, {}, 0, "the controller data has no [profile] section"));
    }
    return data;
}

std::string_view handednessName(game::Handedness handedness) {
    for (const auto& [value, name] : kHandednessNames) {
        if (value == handedness) {
            return name;
        }
    }
    return {};
}

std::optional<game::Handedness> parseHandednessName(std::string_view name) {
    for (const auto& [value, entryName] : kHandednessNames) {
        if (entryName == name) {
            return value;
        }
    }
    return std::nullopt;
}

std::string suggestedBindingKey(XrActionId action, Hand hand) {
    const XrActionDef& def = xrAction(action);
    return std::string(xrActionSet(def.set).name) + "." + std::string(handName(hand)) + "." +
           std::string(def.name);
}

} // namespace evr::input
