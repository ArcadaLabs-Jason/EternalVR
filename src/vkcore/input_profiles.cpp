// Motion controllers, the controller families on the OpenXR side (controllers.hpp): which interaction
// profile extensions the instance enables, each family's data, the suggested bindings of every family the
// instance has, and the family the runtime reports.

#include "vkcore/controllers_impl.hpp"

#include "features/input/interaction_profiles.hpp"
#include "features/input/player_controller_data.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/log.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";

// A player's controller data file, or nullopt (logged) when it cannot be read or has issues (in the file,
// or in compiling one of its control maps).
std::optional<input::ControllerData> readPlayerData(const std::string& path) {
    const auto text = readTextFile(path);
    if (!text) {
        EVR_LOG("%s: cannot read the controller data '%s'; the built-in data is used", kTag, path.c_str());
        return std::nullopt;
    }
    input::ControllerData data = input::parseControllerData(*text);
    if (!data.ok()) {
        for (const auto& issue : data.issues) {
            EVR_LOG("%s: %s line %d: %s", kTag, path.c_str(), issue.line, issue.message.c_str());
        }
        EVR_LOG("%s: '%s' has issues; the built-in data is used", kTag, path.c_str());
        return std::nullopt;
    }
    const std::vector<input::BindingIssue> mapIssues = input::controlMapIssues(data);
    if (!mapIssues.empty()) {
        for (const auto& issue : mapIssues) {
            EVR_LOG("%s: %s: %s", kTag, path.c_str(), issue.message.c_str());
        }
        EVR_LOG("%s: '%s' has issues; the built-in data is used", kTag, path.c_str());
        return std::nullopt;
    }
    return data;
}

// True when the instance has the profile of `data`: a profile of OpenXR 1.0, or one whose extension is
// enabled (or that OpenXR 1.1 made core, on a 1.1 instance). A player's file naming a profile we have no
// input list for is tried as it is.
bool profileAvailable(const ProfileSupport& support, const input::ControllerData& data) {
    const input::InteractionProfileInfo* info = input::findInteractionProfile(data.profilePath);
    if (!info) {
        return true;
    }
    const std::vector<std::string_view> enabled(support.extensions.begin(), support.extensions.end());
    return input::profileAvailable(*info, enabled, support.api11);
}

// Suggests the bindings of `data` with these paths (one per suggested binding): the runtime's result.
XrResult suggestPaths(XrInput& xr, const input::ControllerData& data, const std::vector<std::string>& paths) {
    XrPath profile = XR_NULL_PATH;
    XrResult r = xr.xrStringToPath(xr.instance, data.profilePath.c_str(), &profile);
    std::vector<XrActionSuggestedBinding> bindings;
    for (std::size_t i = 0; i < paths.size() && XR_SUCCEEDED(r); ++i) {
        XrPath path = XR_NULL_PATH;
        r = xr.xrStringToPath(xr.instance, paths[i].c_str(), &path);
        bindings.push_back({xr.actions[static_cast<std::size_t>(data.suggested[i].action)], path});
    }
    if (XR_FAILED(r)) {
        return r;
    }
    XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = profile;
    suggested.countSuggestedBindings = static_cast<std::uint32_t>(bindings.size());
    suggested.suggestedBindings = bindings.data();
    return xr.xrSuggestInteractionProfileBindings(xr.instance, &suggested);
}

bool suggestBindings(XrInput& xr, const input::ControllerData& data) {
    std::vector<std::string> paths;
    std::vector<std::string> olderPaths;
    bool renamed = false;
    for (const input::SuggestedBinding& b : data.suggested) {
        paths.push_back(b.path);
        const auto older = input::olderInputName(data.profilePath, b.path);
        renamed = renamed || older.has_value();
        olderPaths.push_back(older.value_or(b.path));
    }
    const char* names = "";
    XrResult r = suggestPaths(xr, data, paths);
    if (renamed) {
        names = " (the bumper as /input/shoulder/)";
        // A runtime older than the rename refuses the whole profile for the newer name (olderInputName).
        if (r == XR_ERROR_PATH_UNSUPPORTED) {
            EVR_LOG("%s: %s: the runtime refuses a path; trying the bumper's older name, /input/bumper/",
                    kTag, data.profilePath.c_str());
            names = " (the bumper as /input/bumper/)";
            r = suggestPaths(xr, data, olderPaths);
        }
    }
    if (XR_FAILED(r)) {
        char text[XR_MAX_RESULT_STRING_SIZE];
        EVR_LOG("%s: xrSuggestInteractionProfileBindings failed for %s: %s", kTag, data.profilePath.c_str(),
                xrText(r, text));
        return false;
    }
    EVR_LOG("%s: %zu binding(s) suggested for %s%s", kTag, paths.size(), data.profilePath.c_str(), names);
    return true;
}

void append(std::string& list, std::string_view item) {
    list += list.empty() ? "" : ", ";
    list += item;
}

// The interaction profile the runtime reports for a hand, or empty (none yet, or the call failed), kept in
// handProfiles. A change is logged per hand.
std::string currentProfile(const XrInput& xr, State& s, input::Hand hand) {
    const auto index = static_cast<std::size_t>(hand);
    std::string& logged = s.handProfiles[index];
    XrInteractionProfileState profile{XR_TYPE_INTERACTION_PROFILE_STATE};
    char text[XR_MAX_PATH_LENGTH] = {};
    if (XR_SUCCEEDED(xr.xrGetCurrentInteractionProfile(xr.session, xr.handPaths[index], &profile)) &&
        profile.interactionProfile != XR_NULL_PATH) {
        std::uint32_t length = 0;
        if (XR_FAILED(
                xr.xrPathToString(xr.instance, profile.interactionProfile, sizeof(text), &length, text))) {
            text[0] = '\0';
        }
    }
    if (logged != text) {
        logged = text;
        EVR_LOG("%s: the runtime reports %s for the %s hand", kTag, text[0] ? text : "no controller",
                hand == input::Hand::Left ? "left" : "right");
    }
    return text;
}

} // namespace

std::vector<std::string> profileExtensions(std::span<const XrExtensionProperties> offered) {
    std::vector<std::string> out;
    for (const input::InteractionProfileInfo& profile : input::knownInteractionProfiles()) {
        if (profile.extension.empty() || std::ranges::find(out, profile.extension) != out.end()) {
            continue;
        }
        const bool found = std::ranges::any_of(offered, [&profile](const XrExtensionProperties& p) {
            return profile.extension == p.extensionName;
        });
        if (found) {
            out.emplace_back(profile.extension);
        }
        EVR_LOG("xr: controller extension %.*s %s", static_cast<int>(profile.extension.size()),
                profile.extension.data(), found ? "enabled" : "not offered by the runtime");
    }
    return out;
}

FamilyData loadControllerData(const input::ControllerSettings& settings) {
    FamilyData data;
    for (const game::Controller family : game::kControllers) {
        data[static_cast<std::size_t>(family)] =
            input::parseControllerData(game::builtinControllerData(family));
    }
    const std::string& path = settings.controllerDataPath;
    if (path.empty()) {
        return data;
    }
    // A folder: every *.toml directly inside it, in name order; a file: that file alone.
    std::vector<std::string> files;
    if (auto names = folderFileNames(path)) {
        for (const std::string& name : input::controllerDataFileOrder(std::move(*names))) {
            files.push_back(input::joinFolderPath(path, name));
        }
        if (files.empty()) {
            EVR_LOG("%s: the controller data folder '%s' has no .toml files; the built-in data is used", kTag,
                    path.c_str());
        }
    } else {
        files.push_back(path);
    }
    std::vector<std::string> sources;
    for (const std::string& file : files) {
        auto player = readPlayerData(file);
        if (!player) {
            continue;
        }
        const input::PlayerDataPlacement placement =
            input::placePlayerData(data, sources, file, std::move(*player));
        if (!placement.replaced.empty()) {
            EVR_LOG(
                "%s: controller data '%s' replaces '%s', which names the same profile (the later file wins)",
                kTag, file.c_str(), placement.replaced.c_str());
        } else {
            EVR_LOG("%s: controller data '%s' %s", kTag, file.c_str(),
                    placement.placed ? "replaces the built-in data of its profile"
                                     : "names no supported profile; unused");
        }
    }
    return data;
}

bool suggestAllBindings(XrInput& xr, const ProfileSupport& support, const FamilyData& data) {
    bool any = false;
    std::string suggested;
    std::string skipped;
    for (const input::ControllerData& d : data) {
        if (!profileAvailable(support, d)) {
            append(skipped, d.profilePath);
            continue;
        }
        // A profile the runtime refuses is logged by suggestBindings; the others still go ahead.
        if (suggestBindings(xr, d)) {
            any = true;
            append(suggested, d.profilePath);
        }
    }
    EVR_LOG("%s: bindings suggested for %s; skipped (extension missing): %s", kTag,
            suggested.empty() ? "none" : suggested.c_str(), skipped.empty() ? "none" : skipped.c_str());
    return any;
}

std::optional<game::Controller> currentFamily(const XrInput& xr, State& s) {
    currentProfile(xr, s, input::Hand::Left); // logged only
    const std::string text = currentProfile(xr, s, input::Hand::Right);
    if (text.empty()) {
        return std::nullopt;
    }
    for (const game::Controller family : game::kControllers) {
        if (s.controllerData[static_cast<std::size_t>(family)].profilePath == text) {
            return family;
        }
    }
    static std::string loggedUnknown; // the input thread only
    if (loggedUnknown != text) {
        loggedUnknown = text;
        EVR_LOG("input: no bindings for controller profile %s: its controllers do nothing", text.c_str());
    }
    return std::nullopt;
}

} // namespace evr::vkcore::controllers
