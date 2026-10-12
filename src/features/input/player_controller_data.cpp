#include "features/input/player_controller_data.hpp"

#include "features/input/binding_compiler.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace evr::input {

namespace {

char lowerChar(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool endsWithToml(std::string_view name) {
    constexpr std::string_view kSuffix = ".toml";
    if (name.size() <= kSuffix.size()) {
        return false;
    }
    const std::string_view tail = name.substr(name.size() - kSuffix.size());
    return std::equal(tail.begin(), tail.end(), kSuffix.begin(),
                      [](char a, char b) { return lowerChar(a) == b; });
}

std::string lowered(const std::string& text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), lowerChar);
    return out;
}

// Name order ignoring case; names equal but for case keep a fixed order.
bool nameBefore(const std::string& a, const std::string& b) {
    return std::pair(lowered(a), std::string_view(a)) < std::pair(lowered(b), std::string_view(b));
}

} // namespace

std::vector<std::string> controllerDataFileOrder(std::vector<std::string> names) {
    std::erase_if(names, [](const std::string& name) { return !endsWithToml(name); });
    std::sort(names.begin(), names.end(), nameBefore);
    return names;
}

std::string joinFolderPath(std::string_view folder, std::string_view name) {
    std::string out(folder);
    if (!out.empty() && out.back() != '\\' && out.back() != '/') {
        out += '\\';
    }
    out += name;
    return out;
}

std::vector<BindingIssue> controlMapIssues(const ControllerData& data) {
    std::vector<BindingIssue> issues;
    for (const auto& [handedness, map] : data.maps) {
        for (BindingIssue& issue : buildBindingProfile(map).issues) {
            issue.message = "[map." + std::string(handednessName(handedness)) + "] " + issue.message;
            issues.push_back(std::move(issue));
        }
    }
    return issues;
}

std::vector<BindingIssue> takeSkippedEntries(ControllerData& data) {
    std::vector<BindingIssue> skipped;
    std::vector<BindingIssue> kept;
    for (BindingIssue& issue : data.issues) {
        (issue.skipped ? skipped : kept).push_back(std::move(issue));
    }
    data.issues = std::move(kept);
    for (auto& [handedness, map] : data.maps) {
        const std::string section = "[map." + std::string(handednessName(handedness)) + "] ";
        std::vector<BindingIssue> issues = buildBindingProfile(map).issues;
        // A map of nothing but such keys keeps them: compiling it reports that it has no binding left
        // (buildBindingProfile), and the file is not used.
        if (static_cast<std::size_t>(std::ranges::count_if(issues, &BindingIssue::skipped)) == map.size()) {
            continue;
        }
        for (BindingIssue& issue : issues) {
            const auto entry = map.find(issue.key);
            if (!issue.skipped || entry == map.end()) {
                continue;
            }
            issue.message = section + "'" + entry->first + "' (= '" + entry->second +
                            "') is not a binding this version knows (a later version's, or a typo)";
            map.erase(entry);
            skipped.push_back(std::move(issue));
        }
    }
    return skipped;
}

UsablePlayerData usablePlayerData(std::string_view text) {
    UsablePlayerData result;
    result.data = parseControllerData(text);
    result.skipped = takeSkippedEntries(result.data);
    result.problems = result.data.ok() ? controlMapIssues(result.data) : result.data.issues;
    return result;
}

PlayerDataPlacement placePlayerData(std::span<ControllerData> data,
                                    std::vector<std::string>& sources,
                                    std::string_view file,
                                    ControllerData player) {
    sources.resize(data.size());
    PlayerDataPlacement result;
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (data[i].profilePath != player.profilePath) {
            continue;
        }
        data[i] = std::move(player);
        result.placed = true;
        result.replaced = std::exchange(sources[i], std::string(file));
        break;
    }
    return result;
}

} // namespace evr::input
