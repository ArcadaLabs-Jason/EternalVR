#include "features/input/player_controller_data.hpp"

#include "features/input/binding_compiler.hpp"

#include <algorithm>
#include <cstddef>
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
