#pragma once

// The shoulder launcher under Equipment aim = Head or Off hand (docs/VR_CONTROLLERS.md "Melee and equipment
// aim", DECISIONS T-144).
//
// The game places the launcher's model at a joint of the arms model, and the viewmodel hook puts the arms
// model at the weapon hand (weapon_pose.hpp): the launcher then pops up in front of the weapon hand and
// points along it, and the Flame Belch's flames and the equipment's launch start there. This moves it to
// where the game's own view shows it, with the rendered head as that view: the game puts the arms model's
// origin at its eye with the view's axis (the viewmodel hook's log: the game's origin is the eye), so the
// launcher's pose in the arms model, its pop-up animation included, is carried from the arms model at the
// weapon hand to the arms model at the head. Its position follows the head's view (over the left shoulder, as
// the flat game shows it); it points along the ray the equipment aims with (the head's, or the off hand's),
// so its muzzle, the flames and the launch all leave the launcher the player sees.
//
// Conventions as in weapon_pose.hpp: poses relative to the game's eye, id Tech axes (+X forward, +Y left,
// +Z up), axes as rows. Pure: no Windows, OpenXR or game memory.

#include "xr_math/head_view.hpp"
#include "xr_math/weapon_pose.hpp"

#include <optional>

namespace evr::xr_math {

// `launcher`: the launcher model's pose as the game computed it from the arms model placed at `arms`;
// `head`: the rendered head (its offset from the eye and its axis); `aim`: the direction of the ray the
// equipment aims with (the head's forward, or the off hand's aim). The head's view and the aim are taken
// without roll, as the game's own view has none. nullopt when a direction is unusable or a value is not
// finite.
std::optional<EyeRelativePose> launcherAtHead(const EyeRelativePose& arms,
                                              const EyeRelativePose& launcher,
                                              const EyeRelativePose& head,
                                              Vec3 aim);

// An equipment launch under Equipment aim = Head or Off hand (the launch core's origin and axis), relative to
// the game's eye `eye`. The game takes them from the launcher's muzzle: `gameOrigin`, and `gameDirection`
// with the decl's turn and arc from the ray the launcher points along. `drawnAlong`: that ray when the
// shoulder launcher hook drew the launcher at the head this tick; then the launch keeps the game's origin
// (the muzzle the player sees) and the game's turn and arc, carried onto `ray`. nullopt: the launcher stayed
// at the weapon hand (the hook off, or it left this tick alone), so the muzzle is not where the ray starts;
// the launch then starts at the ray's start (`eye + ray.offset`, at most `maxReach` from the eye) and is
// turned from the weapon hand's ray `weaponHand` onto `ray`. nullopt when a direction is unusable.
std::optional<Shot> equipmentLaunch(Vec3 eye,
                                    Vec3 gameOrigin,
                                    Vec3 gameDirection,
                                    const std::optional<Vec3>& drawnAlong,
                                    Vec3 weaponHand,
                                    const EyeRelativePose& ray,
                                    float maxReach);

} // namespace evr::xr_math
