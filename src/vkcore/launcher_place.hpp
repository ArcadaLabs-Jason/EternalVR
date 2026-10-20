#pragma once

// The shoulder launcher's model under ETERNALVR_EQUIPMENT_AIM (xr_math/launcher_pose.hpp, DECISIONS T-144).
//
// Static RE (Steam build 25216728): each tick of the player's hands, idHandsItem's update (0x138AB20) first
// places its item's model (0x138A480): the joint the item's decl names on the arms model (idHands+0x370),
// from that model's current origin and axis (its render entity's +0x158 and +0x164, which the viewmodel
// hook set to the weapon hand), plus the item's offset, stored as the model's current and deferred pose.
// For the shoulder launchers (hands slots 5 and 6) that model is the launcher the player sees pop up for a
// Flame Belch or an equipment launch, and the muzzle the Belch's flames, its shots and the launch start from
// is a joint of it. A mid hook on that store (RVA 0x138A75D: the item at r13, the hands at r12, the origin
// at [rsp+0x30], the axis at [rsp+0x68]) moves the pose from the arms model at the weapon hand to the arms
// model at the rendered head, pointing along the head's ray or the off hand's (xr_math::launcherAtHead).
// Installed with the equipment hooks, under hand aim only (head aim already places the arms at the head).

#include "common/vector.hpp"

#include <optional>

namespace evr::vkcore::controllers::launcher_place {

bool install();

// The direction the launcher of hands slot `slot` was last drawn along, when the hook moved it within the
// last tick or so (the launch reads its muzzle in the same tick); nullopt when it was left at the weapon
// hand.
std::optional<Vec3> drawnAlong(int slot);

} // namespace evr::vkcore::controllers::launcher_place
