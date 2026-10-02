// Motion controllers, whether the runtime binds our actions (controllers_impl.hpp,
// features/input/binding_watch.hpp).
//
// Once a hand's interaction profile has settled, the sources bound to each gameplay input action are listed
// (xrEnumerateBoundSourcesForAction, with xrGetInputSourceLocalizedName's names), with the actions left
// unbound per hand (a runtime that lists no source at all gets a note, since some list none even for working
// bindings). When no hand pose has become valid in play with the headset tracked, one WARNING line names the
// likely cause: a controller binding chosen in the runtime for the game
// that names other actions (a SteamVR workshop binding made for another mod of the game). The layer has no
// text of its own in the headset, so the warning is in the log only (the launcher's preflight warns about
// such a binding before the launch).
//
// XR worker only, inside sync under the shared xrMutex; the action sets are attached by then. Both calls
// are core OpenXR 1.0 and need no external synchronisation.

#include "vkcore/controllers_impl.hpp"

#include "features/input/binding_watch.hpp"
#include "features/input/xr_action_set.hpp"
#include "vkcore/log.hpp"
#include "vkcore/xr_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";

std::string pathText(const XrInput& xr, XrPath path) {
    char text[XR_MAX_PATH_LENGTH] = {};
    std::uint32_t length = 0;
    if (XR_FAILED(xr.xrPathToString(xr.instance, path, sizeof(text), &length, text))) {
        return "(unnamed)";
    }
    return text;
}

// The runtime's name for a source ("Left Hand Trigger"), or empty.
std::string localizedName(const XrInput& xr, XrPath source) {
    if (!xr.xrGetInputSourceLocalizedName) {
        return {};
    }
    XrInputSourceLocalizedNameGetInfo info{XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO};
    info.sourcePath = source;
    info.whichComponents =
        XR_INPUT_SOURCE_LOCALIZED_NAME_USER_PATH_BIT | XR_INPUT_SOURCE_LOCALIZED_NAME_COMPONENT_BIT;
    std::uint32_t count = 0;
    if (XR_FAILED(xr.xrGetInputSourceLocalizedName(xr.session, &info, 0, &count, nullptr)) || count == 0) {
        return {};
    }
    std::string name(count, '\0');
    if (XR_FAILED(xr.xrGetInputSourceLocalizedName(xr.session, &info, count, &count, name.data()))) {
        return {};
    }
    name.resize(std::strlen(name.c_str())); // the count includes the terminator
    return name;
}

// The sources the runtime binds to `action`, or nullopt (logged) when it cannot say.
std::optional<std::vector<XrPath>> boundSources(const XrInput& xr, XrAction action, const char* name) {
    XrBoundSourcesForActionEnumerateInfo info{XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO};
    info.action = action;
    std::uint32_t count = 0;
    XrResult r = xr.xrEnumerateBoundSourcesForAction(xr.session, &info, 0, &count, nullptr);
    std::vector<XrPath> sources(count);
    if (XR_SUCCEEDED(r) && count > 0) {
        r = xr.xrEnumerateBoundSourcesForAction(xr.session, &info, count, &count, sources.data());
        sources.resize(count);
    }
    if (XR_FAILED(r)) {
        char text[XR_MAX_RESULT_STRING_SIZE];
        EVR_LOG("%s: the sources bound to %s cannot be listed (%s)", kTag, name, xrText(r, text));
        return std::nullopt;
    }
    return sources;
}

const char* profileText(const std::string& profile) {
    return profile.empty() ? "no controller" : profile.c_str();
}

// Lists the bound sources of every gameplay input action, then the verdict (a warning when nothing is bound).
void listBoundSources(const XrInput& xr, const State& s) {
    if (!xr.xrEnumerateBoundSourcesForAction) {
        EVR_LOG("%s: the runtime cannot list the sources bound to the actions", kTag);
        return;
    }
    std::vector<input::BoundAction> listed;
    for (const input::XrActionDef& def : input::xrActions()) {
        if (def.set != input::XrActionSetId::Gameplay || def.kind == input::XrActionKind::Haptic) {
            continue;
        }
        const std::string name(def.name);
        const auto sources = boundSources(xr, xr.actions[static_cast<std::size_t>(def.id)], name.c_str());
        if (!sources) {
            return;
        }
        input::BoundAction action{def.id, {}};
        std::string line;
        for (const XrPath source : *sources) {
            action.sources.push_back(pathText(xr, source));
            const std::string localized = localizedName(xr, source);
            line += line.empty() ? "" : ", ";
            line += action.sources.back();
            line += localized.empty() ? "" : " (" + localized + ")";
        }
        EVR_LOG("%s: bound to %s: %s", kTag, name.c_str(), line.empty() ? "nothing" : line.c_str());
        listed.push_back(std::move(action));
    }
    const input::BindingVerdict verdict = input::judgeBoundSources(listed);
    EVR_LOG("%s: %zu of %zu action(s) bound (left hand %s, right hand %s); unbound on the left hand: %s; on "
            "the right hand: %s",
            kTag, verdict.boundActions, verdict.actions, profileText(s.handProfiles[0]),
            profileText(s.handProfiles[1]), input::actionNames(verdict.unbound[0]).c_str(),
            input::actionNames(verdict.unbound[1]).c_str());
    // Some runtimes list no source even for working bindings (the rig's OpenXR simulator does), so this is a
    // note only: the hand pose check decides whether the controllers really never reach the mod.
    if (verdict.noneBound()) {
        EVR_LOG("%s: the runtime lists no source for any of the mod's controller actions; if no hand pose "
                "becomes valid either, a WARNING follows (likely cause: %s)",
                kTag, std::string(input::unboundAdvice(xrRuntimeName())).c_str());
    }
}

} // namespace

void watchBindings(const XrInput& xr, State& s, input::BindingWatchSample sample) {
    sample.seconds = secondsSince(0); // QueryPerformanceCounter time, monotonic
    sample.profile = {!s.handProfiles[0].empty(), !s.handProfiles[1].empty()};
    const input::BindingWatchActions actions = s.bindingWatch.update(sample);
    if (actions.listSources) {
        listBoundSources(xr, s);
    }
    if (actions.posesNeverValid) {
        EVR_LOG("%s: WARNING no hand pose has been valid for %.0f s of play with the headset tracked and the "
                "runtime reporting controllers (left hand %s, right hand %s). If the controllers are on and "
                "tracked, the likely cause: %s",
                kTag, input::kPosesNeverValidSeconds, profileText(s.handProfiles[0]),
                profileText(s.handProfiles[1]), std::string(input::unboundAdvice(xrRuntimeName())).c_str());
    }
}

} // namespace evr::vkcore::controllers
