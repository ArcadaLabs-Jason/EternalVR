// The weapon arm's [right_grip] rows (weapon_arm.hpp, rowGrip).

#include "vkcore/weapon_arm.hpp"

#include "vkcore/log.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace evr::vkcore::controllers::weapon_arm {

namespace {

constexpr const char* kTag = "weapon arm";

// The arms' item's [right_grip] row, looked up when the item changes (off-hand hook thread only).
const std::byte* g_gripDecl = nullptr;
std::optional<game::RightGrip> g_grip;
std::string g_gripItem;
std::uint64_t g_gripLines = 0;

// The glove's grip for a [right_grip] row: the game grip, its wrist moved where the row says.
arm::GripPose gripPose(const game::RightGrip& row) {
    arm::GripPose grip = arm::heavyCannonGrip();
    switch (row.grip) {
    case game::GameGrip::HeavyCannon:
        break;
    }
    if (row.wrist) {
        grip.wrist.position = {(*row.wrist)[0], (*row.wrist)[1], (*row.wrist)[2]};
    }
    return grip;
}

} // namespace

std::optional<arm::GripPose> rowGrip(const std::byte* decl) {
    if (decl != g_gripDecl) {
        g_gripDecl = decl;
        g_gripItem = decl ? itemDeclName(decl) : std::string{};
        g_grip = state().offsets.lookupRightGrip(g_gripItem);
        if (g_grip && g_gripLines < 20) {
            ++g_gripLines;
            EVR_LOG("%s: '%s' holds its gun by a handle turned away from a controller's grip: the glove goes "
                    "on a grip in the Heavy Cannon's idle grip while the layer poses the arm (the gun stays "
                    "the game's)",
                    kTag, g_gripItem.c_str());
        }
    }
    return g_grip ? std::optional(gripPose(*g_grip)) : std::nullopt;
}

} // namespace evr::vkcore::controllers::weapon_arm
