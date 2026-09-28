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

// HP Reverb G2 (XR_EXT_hp_mixed_reality_controller): X/Y on the left, A/B on the right, Menu on both.
constexpr std::array<std::string_view, 11> kHpLeft{
    "/input/x/click",       "/input/y/click",      "/input/menu/click",   "/input/squeeze/value",
    "/input/trigger/value", "/input/thumbstick/x", "/input/thumbstick/y", "/input/thumbstick/click",
    "/input/grip/pose",     "/input/aim/pose",     "/output/haptic",
};
constexpr std::array<std::string_view, 11> kHpRight{
    "/input/a/click",       "/input/b/click",      "/input/menu/click",   "/input/squeeze/value",
    "/input/trigger/value", "/input/thumbstick/x", "/input/thumbstick/y", "/input/thumbstick/click",
    "/input/grip/pose",     "/input/aim/pose",     "/output/haptic",
};

// Windows Mixed Reality motion controllers: both hands alike, a stick and a trackpad, no face buttons.
constexpr std::array<std::string_view, 13> kWmr{
    "/input/menu/click",     "/input/squeeze/click",    "/input/trigger/value", "/input/thumbstick/x",
    "/input/thumbstick/y",   "/input/thumbstick/click", "/input/trackpad/x",    "/input/trackpad/y",
    "/input/trackpad/click", "/input/trackpad/touch",   "/input/grip/pose",     "/input/aim/pose",
    "/output/haptic",
};

// HTC Vive Cosmos (XR_HTC_vive_cosmos_controller_interaction). The right System button is reserved.
constexpr std::array<std::string_view, 14> kCosmosLeft{
    "/input/x/click",       "/input/y/click",          "/input/menu/click",       "/input/shoulder/click",
    "/input/squeeze/click", "/input/trigger/click",    "/input/trigger/value",    "/input/thumbstick/x",
    "/input/thumbstick/y",  "/input/thumbstick/click", "/input/thumbstick/touch", "/input/grip/pose",
    "/input/aim/pose",      "/output/haptic",
};
constexpr std::array<std::string_view, 13> kCosmosRight{
    "/input/a/click",          "/input/b/click",          "/input/shoulder/click", "/input/squeeze/click",
    "/input/trigger/click",    "/input/trigger/value",    "/input/thumbstick/x",   "/input/thumbstick/y",
    "/input/thumbstick/click", "/input/thumbstick/touch", "/input/grip/pose",      "/input/aim/pose",
    "/output/haptic",
};

// HTC Vive wands: both hands alike, a trackpad and no stick. System is left out: SteamVR keeps it.
constexpr std::array<std::string_view, 11> kViveWand{
    "/input/squeeze/click", "/input/menu/click", "/input/trigger/click",  "/input/trigger/value",
    "/input/trackpad/x",    "/input/trackpad/y", "/input/trackpad/click", "/input/trackpad/touch",
    "/input/grip/pose",     "/input/aim/pose",   "/output/haptic",
};

// Pico 4 (XR_BD_controller_interaction). System on both hands is reserved.
constexpr std::array<std::string_view, 17> kPicoLeft{
    "/input/x/click",          "/input/x/touch",          "/input/y/click",       "/input/y/touch",
    "/input/menu/click",       "/input/squeeze/click",    "/input/squeeze/value", "/input/trigger/click",
    "/input/trigger/touch",    "/input/trigger/value",    "/input/thumbstick/x",  "/input/thumbstick/y",
    "/input/thumbstick/click", "/input/thumbstick/touch", "/input/grip/pose",     "/input/aim/pose",
    "/output/haptic",
};
constexpr std::array<std::string_view, 16> kPicoRight{
    "/input/a/click",          "/input/a/touch",       "/input/b/click",       "/input/b/touch",
    "/input/squeeze/click",    "/input/squeeze/value", "/input/trigger/click", "/input/trigger/touch",
    "/input/trigger/value",    "/input/thumbstick/x",  "/input/thumbstick/y",  "/input/thumbstick/click",
    "/input/thumbstick/touch", "/input/grip/pose",     "/input/aim/pose",      "/output/haptic",
};

// The Touch Pro and Touch Plus profiles (XR_FB_touch_controller_pro, XR_META_touch_controller_plus) are
// left out on purpose: every runtime that has them reports those controllers as Touch controllers when
// an application suggests no bindings for them, and a family of their own would only split the Touch
// data (a player's Touch file would stop applying to them).
constexpr std::array<InteractionProfileInfo, 7> kProfiles{{
    {"/interaction_profiles/oculus/touch_controller", kTouchLeft, kTouchRight, {}, false},
    {"/interaction_profiles/valve/index_controller", kIndex, kIndex, {}, false},
    {"/interaction_profiles/hp/mixed_reality_controller", kHpLeft, kHpRight,
     "XR_EXT_hp_mixed_reality_controller", true},
    {"/interaction_profiles/microsoft/motion_controller", kWmr, kWmr, {}, false},
    {"/interaction_profiles/htc/vive_cosmos_controller", kCosmosLeft, kCosmosRight,
     "XR_HTC_vive_cosmos_controller_interaction", true},
    {"/interaction_profiles/htc/vive_controller", kViveWand, kViveWand, {}, false},
    {"/interaction_profiles/bytedance/pico4_controller", kPicoLeft, kPicoRight,
     "XR_BD_controller_interaction", true},
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

bool profileAvailable(const InteractionProfileInfo& profile,
                      std::span<const std::string_view> enabledExtensions,
                      bool api11) {
    if (profile.extension.empty() || (api11 && profile.coreIn11)) {
        return true;
    }
    return std::ranges::find(enabledExtensions, profile.extension) != enabledExtensions.end();
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
