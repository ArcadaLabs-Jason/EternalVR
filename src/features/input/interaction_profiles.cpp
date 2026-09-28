#include "features/input/interaction_profiles.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace evr::input {

namespace {

constexpr std::array<std::string_view, 16> kTouchLeft{
    "/input/x/click",         "/input/x/touch",       "/input/y/click",          "/input/y/touch",
    "/input/menu/click",      "/input/squeeze/value", "/input/trigger/value",    "/input/trigger/touch",
    "/input/thumbstick/x",    "/input/thumbstick/y",  "/input/thumbstick/click", "/input/thumbstick/touch",
    "/input/thumbrest/touch", "/input/grip/pose",     "/input/aim/pose",         "/output/haptic",
};

// The right Touch controller's System button is reserved for the runtime and not listed.
constexpr std::array<std::string_view, 15> kTouchRight{
    "/input/a/click",       "/input/a/touch",          "/input/b/click",          "/input/b/touch",
    "/input/squeeze/value", "/input/trigger/value",    "/input/trigger/touch",    "/input/thumbstick/x",
    "/input/thumbstick/y",  "/input/thumbstick/click", "/input/thumbstick/touch", "/input/thumbrest/touch",
    "/input/grip/pose",     "/input/aim/pose",         "/output/haptic",
};

// Both Index hands have the same inputs. System is left out: SteamVR keeps it for its dashboard.
constexpr std::array<std::string_view, 20> kIndex{
    "/input/a/click",          "/input/a/touch",       "/input/b/click",       "/input/b/touch",
    "/input/squeeze/value",    "/input/squeeze/force", "/input/trigger/click", "/input/trigger/value",
    "/input/trigger/touch",    "/input/thumbstick/x",  "/input/thumbstick/y",  "/input/thumbstick/click",
    "/input/thumbstick/touch", "/input/trackpad/x",    "/input/trackpad/y",    "/input/trackpad/force",
    "/input/trackpad/touch",   "/input/grip/pose",     "/input/aim/pose",      "/output/haptic",
};

constexpr std::array<InteractionProfileInfo, 2> kProfiles{{
    {"/interaction_profiles/oculus/touch_controller", kTouchLeft, kTouchRight},
    {"/interaction_profiles/valve/index_controller", kIndex, kIndex},
}};

// The identifier part of a path: "/input/trigger/value" -> "/input/trigger". Paths with no
// component ("/output/haptic", "/input/thumbstick") are their own identifier.
std::string_view identifierOf(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    if (slash == std::string_view::npos || slash == 0) {
        return path;
    }
    const std::string_view head = path.substr(0, slash);
    // "/input/x" has one slash after the first: it is already an identifier.
    return head.find('/', 1) == std::string_view::npos ? path : head;
}

std::string_view componentOf(std::string_view path) {
    const std::string_view identifier = identifierOf(path);
    return identifier.size() == path.size() ? std::string_view{} : path.substr(identifier.size() + 1);
}

bool hasLeaf(const InteractionProfileInfo& profile,
             Hand hand,
             std::string_view identifier,
             std::string_view component) {
    return std::ranges::any_of(profile.inputs(hand), [identifier, component](std::string_view leaf) {
        return identifierOf(leaf) == identifier && componentOf(leaf) == component;
    });
}

bool isLeaf(const InteractionProfileInfo& profile, Hand hand, std::string_view path) {
    return std::ranges::find(profile.inputs(hand), path) != profile.inputs(hand).end();
}

} // namespace

std::span<const InteractionProfileInfo> knownInteractionProfiles() {
    return kProfiles;
}

const InteractionProfileInfo* findInteractionProfile(std::string_view path) {
    const auto it = std::ranges::find(kProfiles, path, &InteractionProfileInfo::path);
    return it == kProfiles.end() ? nullptr : &*it;
}

bool profileHasPath(const InteractionProfileInfo& profile, Hand hand, std::string_view path) {
    if (isLeaf(profile, hand, path)) {
        return true;
    }
    return std::ranges::any_of(profile.inputs(hand), [path](std::string_view leaf) {
        return identifierOf(leaf) == path && leaf.size() > path.size();
    });
}

bool pathSuitsAction(const InteractionProfileInfo& profile,
                     Hand hand,
                     std::string_view path,
                     XrActionKind kind) {
    const bool leaf = isLeaf(profile, hand, path);
    const std::string_view component = leaf ? componentOf(path) : std::string_view{};
    const auto has = [&](std::string_view c) {
        return hasLeaf(profile, hand, path, c);
    };
    switch (kind) {
    case XrActionKind::Pose:
        return leaf && component == "pose";
    case XrActionKind::Haptic:
        return path == "/output/haptic";
    case XrActionKind::Vector2:
        return !leaf && has("x") && has("y");
    case XrActionKind::Float:
        if (leaf) {
            return component == "value" || component == "force" || component == "click" ||
                   component == "touch";
        }
        return has("value");
    case XrActionKind::Boolean:
        if (leaf) {
            return component == "click" || component == "touch" || component == "value" ||
                   component == "force";
        }
        return has("click") || has("value");
    }
    return false;
}

bool bindingsOverlap(std::string_view pathA, XrActionKind kindA, std::string_view pathB, XrActionKind kindB) {
    if (pathA == pathB) {
        return true;
    }
    // Make `a` the shorter path: only an identifier can contain the other path.
    if (pathA.size() > pathB.size()) {
        std::swap(pathA, pathB);
        std::swap(kindA, kindB);
    }
    if (!pathB.starts_with(pathA) || pathB[pathA.size()] != '/') {
        return false;
    }
    const std::string_view component = pathB.substr(pathA.size() + 1);
    if (kindA == XrActionKind::Vector2) {
        return component == "x" || component == "y";
    }
    return component == "click" || component == "value" || component == "force";
}

} // namespace evr::input
