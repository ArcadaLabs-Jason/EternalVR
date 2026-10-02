#pragma once

// The OpenXR action sets and actions the layer creates (ARCHITECTURE section 6, R06 section 4.3).
//
// The actions are the controllers' physical inputs, not game actions: which game action an input
// triggers is decided by our own bindings (binding_profile.hpp), so remapping never depends on the
// runtime's binding UI. Each action has both hands as subaction paths, so one action serves either
// hand and handedness is a question for the control map only. Which input of an interaction profile
// drives each action is data (controller_bindings.hpp and data/input/controllers/).
//
// Nothing here calls OpenXR; the names, types and priorities are plain data the XR side turns into
// xrCreateActionSet / xrCreateAction calls.

#include "features/input/controller_state.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace evr::input {

enum class XrActionSetId : std::uint8_t {
    Gameplay,
    Menu, // Active while one of our panels or the game's menus has the pointer (M6).
    Count,
};

// The OpenXR action type each action is created with.
enum class XrActionKind : std::uint8_t {
    Boolean,
    Float,
    Vector2,
    Pose,
    Haptic,
};

enum class XrActionId : std::uint8_t {
    // Gameplay: the fields of HandState (controller_state.hpp).
    Trigger,
    Grip,
    Thumbstick,
    ThumbstickClick,
    Primary,   // A on the right Touch controller, X on the left.
    Secondary, // B on the right, Y on the left.
    // Two more face buttons and the bumper, for controllers that have them (the Steam Frame's).
    Face3, // X on the right Steam Frame controller, D-pad right on the left.
    Face4, // Y on the right, D-pad up on the left.
    Shoulder,
    Menu,
    AimPose,
    GripPose, // The weapon and arms are placed at this pose (T-054).
    Haptic,
    // Menu.
    MenuSelect,
    MenuBack,
    MenuScroll,
    MenuPointerPose,
    MenuClose,
    Count,
};

inline constexpr std::size_t kXrActionCount = static_cast<std::size_t>(XrActionId::Count);

struct XrActionSetDef {
    XrActionSetId id = XrActionSetId::Gameplay;
    std::string_view name;          // XrActionSetCreateInfo::actionSetName
    std::string_view localizedName; // shown by runtime binding UIs
    std::uint32_t priority = 0;     // higher wins when both sets are active and bind one input
};

struct XrActionDef {
    XrActionId id = XrActionId::Trigger;
    XrActionSetId set = XrActionSetId::Gameplay;
    std::string_view name; // XrActionCreateInfo::actionName, unique within its set
    std::string_view localizedName;
    XrActionKind kind = XrActionKind::Boolean;
};

std::span<const XrActionSetDef> xrActionSets();
std::span<const XrActionDef> xrActions();

const XrActionSetDef& xrActionSet(XrActionSetId id);
const XrActionDef& xrAction(XrActionId id);

std::optional<XrActionSetId> findXrActionSet(std::string_view name);
std::optional<XrActionId> findXrAction(XrActionSetId set, std::string_view name);

// "/user/hand/left" or "/user/hand/right": every action's two subaction paths.
std::string_view handPath(Hand hand);

// True for a well-formed OpenXR action or action set name: 1 to 63 characters from lower-case ASCII
// letters, digits, '-', '_' and '.' (XR_MAX_ACTION_NAME_SIZE is 64 with the terminator).
bool isValidXrName(std::string_view name);

} // namespace evr::input
