#pragma once

// The hands animation's camera (xr_math/camera_anim.hpp, docs/rig-findings/camera-animations.md).
//
// A mid hook in idPlayer::CalculateViewWithoutUpdates (RVA 0x14526C5 in build 25216728), where the
// `camera` joint of the first-person hands animation has been read: the angles and eye offset the game is
// about to add to the first-person view are on the stack ([rsp+0x48] pitch, yaw, roll; [rsp+0x58] the
// offset in the view frame) and rdi is the idPlayer. The hook only reads them. The camera hook takes them
// once per game frame (frame()): every camera animation of 5 degrees or more is logged, and with
// ETERNALVR_CAMERA_ANIMATIONS=1 its rotation goes back on top of the head-tracked view. Off by default: the
// chainsaw pickup the rig reaches does not move this joint (camera-animations.md section 4), so playing it is
// not yet shown to fix anything, and the head keeps replacing the rotation as before.
//
// The camera hook also reports forced views (noteForcedView): how far the game's rendered view left the
// player's own view angles in each one, so a headset log tells what moved the camera in a pickup.

#include "common/vector.hpp"
#include "xr_math/head_aim.hpp"
#include "xr_math/head_view.hpp"

#include <cstddef>

namespace evr::vkcore {

struct GameImage;

namespace camera_anim {

// Locates and installs the hook (once per process; view_hook.cpp calls it with the game's image).
bool install(const GameImage& image);

// One game frame of the camera hook. `gameAxis` is the game's view without the animation (for the body and
// head aim); `applied` goes on top of the head while `active` (only with ETERNALVR_CAMERA_ANIMATIONS=1, and
// never in a cutscene).
struct Frame {
    xr_math::IdViewAxis gameAxis;
    xr_math::IdAngles applied;
    bool active = false;
};
Frame frame(const std::byte* player, const xr_math::IdViewAxis& gameAxis, bool cutscene, bool forcedView);

// Camera hook, head aim, once per game frame with the player's own view angles: the largest gap between
// them and the game's rendered view during each forced view, logged when it ends.
void noteForcedView(bool forcedView,
                    const xr_math::IdViewAxis& gameAxis,
                    const xr_math::IdAngles& viewAngles);

} // namespace camera_anim
} // namespace evr::vkcore
