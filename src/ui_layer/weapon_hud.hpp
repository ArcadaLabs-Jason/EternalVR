#pragma once

// The weapon HUD (docs/VR_HANDS_HUD.md): the HUD's ammo block (and optionally health and armor) on a small
// panel above the back of the gun in the weapon hand, tilted toward the eyes, shown while it faces the head.
//
// Frames: the gun's frame is the viewmodel's (vkcore/viewmodel_hook.cpp): the weapon hand's OpenXR grip
// position with its aim orientation, so -Z runs along the barrel, +Y up out of the top of the gun and +X to
// its right. The panel sits at an offset in that frame (x mirrored for the left hand); its image's right is
// the gun's right, and its face turns from straight back along the barrel (toward a player behind the gun)
// up by the tilt. All poses are in one space (the layer's room space, or the aim space itself): aim, grip,
// head and the result.

#include "common/pose.hpp"
#include "common/vector.hpp"
#include "ui_layer/hud_regions.hpp"
#include "ui_layer/ui_settings.hpp"
#include "ui_layer/wrist_hud.hpp"

#include <cstdint>
#include <vector>

namespace evr::ui_layer {

// The gun's frame: the grip's position (the aim's when the grip is not tracked) with the aim's orientation.
Pose weaponFrame(const Pose& aim, const Pose& grip, bool gripValid);

// The panel's orientation in the gun's frame: image right +X, the normal turned from +Z (back along the
// barrel) toward +Y (up) by `tiltDegrees`, image up = normal x right.
Quat weaponPanelRotation(float tiltDegrees);

// The panel centre's offset in the gun's frame for the configured offset (x mirrored for the left hand).
Vec3 weaponPanelOffset(const WeaponHudSettings& s, bool leftHand);

// Facing: the angle in degrees between the panel's normal and the direction from the panel to the head.
// 0 = the panel looks straight at the eyes; 180 = it faces away (also when the head is at the panel).
float weaponFacingDegrees(const Pose& weapon, const Pose& head, const WeaponHudSettings& s, bool leftHand);

// The show / hide limits and fade times for WristFacing and WristFade: the facing limits of `weapon`, no
// gaze limit (pass a gaze of 0), the fade times of `wrist` (ETERNALVR_WRIST_FADE).
WristSettings weaponShowSettings(const WeaponHudSettings& weapon, const WristSettings& wrist);

// The blocks the weapon HUD takes off the head-locked quad: the ammo block, and the health block with
// `vitals`.
std::vector<WristBlock> weaponMovedBlocks(const WeaponHudSettings& s);

// The weapon HUD's quads for a `width` x `height` GUI target and the gun's frame `weapon`: the ammo block
// `s.widthMetres` wide, and with `s.vitals` the health block at the same scale on its left (as on screen),
// the row centred on the offset. Empty for an empty target.
std::vector<WristQuad> layoutWeaponQuads(
    const Pose& weapon, std::uint32_t width, std::uint32_t height, const WeaponHudSettings& s, bool leftHand);

} // namespace evr::ui_layer
