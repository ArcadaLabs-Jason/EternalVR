#include "features/input/xr_action_set.hpp"

#include <algorithm>
#include <array>

namespace evr::input {

namespace {

constexpr std::array<XrActionSetDef, 1> kSets{{
    {XrActionSetId::Gameplay, "gameplay", "Gameplay", 0},
}};

constexpr std::array<XrActionDef, kXrActionCount> kActions{{
    {XrActionId::Trigger, XrActionSetId::Gameplay, "trigger", "Trigger", XrActionKind::Float},
    {XrActionId::Grip, XrActionSetId::Gameplay, "grip", "Grip", XrActionKind::Float},
    {XrActionId::Thumbstick, XrActionSetId::Gameplay, "thumbstick", "Thumbstick (move / turn)",
     XrActionKind::Vector2},
    {XrActionId::ThumbstickClick, XrActionSetId::Gameplay, "thumbstick_click", "Thumbstick click",
     XrActionKind::Boolean},
    {XrActionId::Primary, XrActionSetId::Gameplay, "primary", "Primary button (A/X)", XrActionKind::Boolean},
    {XrActionId::Secondary, XrActionSetId::Gameplay, "secondary", "Secondary button (B/Y)",
     XrActionKind::Boolean},
    {XrActionId::Face3, XrActionSetId::Gameplay, "face3", "Third face button", XrActionKind::Boolean},
    {XrActionId::Face4, XrActionSetId::Gameplay, "face4", "Fourth face button", XrActionKind::Boolean},
    {XrActionId::Shoulder, XrActionSetId::Gameplay, "shoulder", "Shoulder button (bumper)",
     XrActionKind::Boolean},
    {XrActionId::Menu, XrActionSetId::Gameplay, "menu", "Menu button (pause)", XrActionKind::Boolean},
    {XrActionId::AimPose, XrActionSetId::Gameplay, "aim_pose", "Aim pose", XrActionKind::Pose},
    {XrActionId::GripPose, XrActionSetId::Gameplay, "grip_pose", "Grip pose", XrActionKind::Pose},
    {XrActionId::Haptic, XrActionSetId::Gameplay, "haptic", "Vibration", XrActionKind::Haptic},
    {XrActionId::ThumbRest, XrActionSetId::Gameplay, "thumbrest", "Thumb rest (touch)",
     XrActionKind::Boolean},
    {XrActionId::PrimaryTouch, XrActionSetId::Gameplay, "primary_touch", "Primary button (touch)",
     XrActionKind::Boolean},
    {XrActionId::SecondaryTouch, XrActionSetId::Gameplay, "secondary_touch", "Secondary button (touch)",
     XrActionKind::Boolean},
}};

} // namespace

std::span<const XrActionSetDef> xrActionSets() {
    return kSets;
}

std::span<const XrActionDef> xrActions() {
    return kActions;
}

const XrActionSetDef& xrActionSet(XrActionSetId id) {
    return kSets[static_cast<std::size_t>(id)];
}

const XrActionDef& xrAction(XrActionId id) {
    return kActions[static_cast<std::size_t>(id)];
}

std::optional<XrActionSetId> findXrActionSet(std::string_view name) {
    const auto it = std::ranges::find(kSets, name, &XrActionSetDef::name);
    if (it == kSets.end()) {
        return std::nullopt;
    }
    return it->id;
}

std::optional<XrActionId> findXrAction(XrActionSetId set, std::string_view name) {
    const auto it = std::ranges::find_if(kActions, [set, name](const XrActionDef& action) {
        return action.set == set && action.name == name;
    });
    if (it == kActions.end()) {
        return std::nullopt;
    }
    return it->id;
}

std::string_view handPath(Hand hand) {
    return hand == Hand::Left ? "/user/hand/left" : "/user/hand/right";
}

bool isValidXrName(std::string_view name) {
    constexpr std::size_t kMaxNameLength = 63;
    if (name.empty() || name.size() > kMaxNameLength) {
        return false;
    }
    return std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    });
}

} // namespace evr::input
