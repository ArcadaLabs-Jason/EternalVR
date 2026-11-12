#pragma once

// How a pickup animation's camera is shown in the headset (ETERNALVR_PICKUP_CAMERA, docs/VR_HEAD_TRACKED.md,
// docs/rig-findings/camera-animations.md section 6). A pickup (a Praetor token, a Sentinel Crystal, a rune,
// the automap station: a sync animation whose entityDef is `interact/...`, comfort::isKillSync false) takes
// the view for a few seconds and turns the game's camera on its own. The options:
// - Still (the default): the head replaces the camera's rotation, as in play; its yaw turns the view only
//   once it is more than 15 degrees off the player's view (xr_math::drivenCamera), as for any camera the
//   game drives. The eye goes where the game's camera goes in both.
// - Play: the camera's turn away from the player's view angles (pitch, yaw and roll) goes on top of the head
//   from the pickup's start to the end of its forced view, so the view turns as the flat game's camera turns
//   while the head keeps turning it from there.
// No OpenXR or Windows here.

#include <optional>
#include <string_view>

namespace evr::comfort {

enum class PickupCamera { Still, Play };

// "still" or "play", any case, surrounding spaces ignored; nullopt otherwise.
std::optional<PickupCamera> parsePickupCamera(std::string_view text);
const char* pickupCameraName(PickupCamera camera);

} // namespace evr::comfort
