// Motion controllers, the controller families on the OpenXR side (controllers.hpp): which interaction
// profile extensions the instance enables, each family's data, the suggested bindings of every family the
// instance has, and the family the runtime reports.

#include "vkcore/controllers_impl.hpp"

#include "features/input/binding_compiler.hpp"
#include "features/input/binding_keys.hpp"
#include "features/input/controller_family.hpp"
#include "features/input/interaction_profiles.hpp"
#include "features/input/player_controller_data.hpp"
#include "features/input/rest_touch_bindings.hpp"
#include "features/input/touch_inputs.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/log.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
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

// Suggests `data`, again with the bumper's older name when the runtime refuses the newer one. `names` says
// which name went through.
XrResult suggestWithBumperNames(XrInput& xr, const input::ControllerData& data, const char*& names) {
    std::vector<std::string> paths;
    std::vector<std::string> olderPaths;
    bool renamed = false;
    for (const input::SuggestedBinding& b : data.suggested) {
        paths.push_back(b.path);
        const auto older = input::olderInputName(data.profilePath, b.path);
        renamed = renamed || older.has_value();
        olderPaths.push_back(older.value_or(b.path));
    }
    names = "";
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
    return r;
}

// The touch sensors bound like buttons (a thumb rest, features/input/touch_button.hpp, or a touch on a
// control, touch_inputs.hpp) in the map for the handedness whose binding the runtime refused: one line per
// binding `before` suggested and `after` leaves out (a key whose sensor the family lacks had none to lose).
void logRefusedTouchButtons(const input::ControllerData& before, const input::ControllerData& after) {
    const auto map = before.maps.find(settings().handedness);
    if (map == before.maps.end()) {
        return;
    }
    for (const auto& [key, value] : map->second) {
        const auto parsed = input::parseBindingKey(key);
        const auto* button = parsed ? std::get_if<input::ButtonKey>(&*parsed) : nullptr;
        const auto action = button != nullptr ? input::touchAction(button->input) : std::nullopt;
        if (!action || value == input::kUnboundValue) {
            continue;
        }
        const input::SuggestedBinding* had = before.find(*action, button->hand);
        if (had != nullptr && after.find(*action, button->hand) == nullptr) {
            EVR_LOG("%s: %s: '%s' is bound (%s) but the runtime refused its touch binding (%s); that binding "
                    "does nothing this session",
                    kTag, before.profilePath.c_str(), key.c_str(), value.c_str(), had->path.c_str());
        }
    }
}

// What a retry of a refused profile leaves out, for the log.
std::string retryText(const input::ControllerData& data, const input::TouchRetryStep& step) {
    switch (step.retry) {
    case input::TouchRetry::WithoutVeto:
        return "the stick, trackpad and face-button touch read as the thumb elsewhere";
    case input::TouchRetry::WithoutTouchButtons:
        return std::string("the touches bound as buttons") +
               (input::hasVetoTouchBindings(data) ? " and the touch read as the thumb elsewhere" : "") +
               (input::hasRestTouchBindings(step.data) ? ", keeping the rests' touch" : "");
    case input::TouchRetry::WithoutRestTouch:
        break;
    }
    return "the thumb rests' touch bindings";
}

// What a retry that went through costs, for the log.
std::string retryCost(const input::TouchRetryStep& step) {
    switch (step.retry) {
    case input::TouchRetry::WithoutVeto:
        return "suggested without the touch beside the rest; the thumb rest still counts, a thumb on its "
               "stick or buttons only when it presses or pushes";
    case input::TouchRetry::WithoutTouchButtons:
        return std::string("suggested without the touches bound as buttons") +
               (input::hasRestTouchBindings(step.data) ? "; the rests still count" : "");
    case input::TouchRetry::WithoutRestTouch:
        break;
    }
    // Only a rest's touch is left to drop here (the steps before took the rest): the wheel's rests, or with
    // the wheel off a thumb rest bound like a button.
    return std::string("suggested without the touch bindings; ") +
           (settings().thumbRest.mode != input::RestWheelMode::Off
                ? "the thumb-rest wheel is off for these controllers"
                : "their thumb rests sense nothing this session");
}

// A refused profile is suggested again without the touch bindings the layer added
// (rest_touch_bindings.hpp, input::touchRetries), in steps, and what was left out is dropped from `data`:
// first without the sensors that only tell the thumb is elsewhere (the rests still count, a thumb on its
// stick or buttons is seen only when pressed or pushed), then also without the touches bound like buttons
// (each logged), then without any (that family has no thumb rest this session).
bool suggestBindings(XrInput& xr, input::ControllerData& data) {
    const char* names = "";
    XrResult r = suggestWithBumperNames(xr, data, names);
    if (XR_FAILED(r)) {
        for (input::TouchRetryStep& step : input::touchRetries(data)) {
            char text[XR_MAX_RESULT_STRING_SIZE];
            EVR_LOG("%s: %s: the runtime refuses the bindings (%s); trying again without %s", kTag,
                    data.profilePath.c_str(), xrText(r, text), retryText(data, step).c_str());
            r = suggestWithBumperNames(xr, step.data, names);
            if (XR_SUCCEEDED(r)) {
                EVR_LOG("%s: %s: %s", kTag, data.profilePath.c_str(), retryCost(step).c_str());
                logRefusedTouchButtons(data, step.data);
                data = std::move(step.data);
                break;
            }
        }
    }
    if (XR_FAILED(r)) {
        char text[XR_MAX_RESULT_STRING_SIZE];
        EVR_LOG("%s: xrSuggestInteractionProfileBindings failed for %s: %s", kTag, data.profilePath.c_str(),
                xrText(r, text));
        return false;
    }
    EVR_LOG("%s: %zu binding(s) suggested for %s%s", kTag, data.suggested.size(), data.profilePath.c_str(),
            names);
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

// The touch bindings of the thumb-rest wheel a family's data lacks (a player's file from before it), added
// for this session (rest_touch_bindings.hpp); with the wheel off, only to a family whose maps bind a thumb
// rest like a button. Then the touches on controls its maps bind like buttons (touch_inputs.hpp): none for
// a family whose maps bind none, which the built-in maps do not.
void addRestTouch(FamilyData& data, const input::ControllerSettings& settings) {
    const bool wheelOn = settings.thumbRest.mode != input::RestWheelMode::Off;
    for (input::ControllerData& d : data) {
        const input::RestTouchOptions options =
            input::restTouchOptions(d, wheelOn, settings.thumbRestFaceTouch);
        if (const std::size_t added = input::addRestTouchBindings(d, options); added > 0) {
            EVR_LOG("%s: %zu touch binding(s) for the %s added to the %s data", kTag, added,
                    wheelOn ? "thumb-rest wheel" : "thumb rest bound as a button", d.profilePath.c_str());
        }
        if (const std::size_t added = input::addBoundTouchBindings(d); added > 0) {
            EVR_LOG("%s: %zu touch binding(s) for touches bound as buttons added to the %s data", kTag, added,
                    d.profilePath.c_str());
        }
    }
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
        addRestTouch(data, settings);
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
    addRestTouch(data, settings);
    return data;
}

bool suggestAllBindings(XrInput& xr, const ProfileSupport& support, FamilyData& data) {
    bool any = false;
    std::string suggested;
    std::string skipped;
    for (input::ControllerData& d : data) {
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

game::Controller currentFamily(const XrInput& xr, State& s, game::Controller current) {
    std::array<std::optional<game::Controller>, 2> families;
    for (const input::Hand hand : {input::Hand::Left, input::Hand::Right}) {
        const auto index = static_cast<std::size_t>(hand);
        const std::string text = currentProfile(xr, s, hand);
        if (text.empty()) {
            continue;
        }
        for (const game::Controller family : game::kControllers) {
            if (s.controllerData[static_cast<std::size_t>(family)].profilePath == text) {
                families[index] = family;
            }
        }
        static std::array<std::string, 2> loggedUnknown; // the input thread only
        if (!families[index] && loggedUnknown[index] != text) {
            loggedUnknown[index] = text;
            EVR_LOG(
                "input: no bindings for controller profile %s (the %s hand): that controller does nothing",
                text.c_str(), hand == input::Hand::Left ? "left" : "right");
        }
    }
    return input::pickControllerFamily(families[0], families[1], current);
}

} // namespace evr::vkcore::controllers
