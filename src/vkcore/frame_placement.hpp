#pragma once

// The arms posed under their own frame's placement of the arms model (docs/VR_HANDS_HUD.md, "The arms and the
// gun in one frame").
//
// The off-hand hook poses both arms in the arms model's space, and the game draws them under the placement
// the viewmodel hook gives the model. In a game frame the off-hand hook (idHands::Update) runs before the
// viewmodel hook (idHands::UpdatePosition) [rig: the per-tick record, docs/rig-findings/hands-rig.md section
// 26], so the placement it read was the last frame's. Now, when the viewmodel hook has not run yet in the
// frame (vkcore/game_frame.hpp), the off-hand hook takes the placement the viewmodel hook will give that
// frame, from the same hands snapshot and with the viewmodel hook's own function, poses the arms under it and
// pins it with the inputs it was made from. The viewmodel hook still places the model itself, as before,
// every frame: the gun never takes the pin. It checks the pin against its own placement and counts each frame
// (the same, or why not: another held item, other hands, the left glove moved on the gun, or the model not
// placed), a 10 s line and the first differing frames in the log. Only in a frame where they differ were the
// arms posed under a placement the model did not get (a switch's first frame, as before this).
//
// The game draws the arm a frame after it is written (features/arm/drawn_wrist.hpp), under that next frame's
// placement: a move of the weapon hand still moves a still glove for that one frame (before this, for two).
//
// Threads: the hands' hook threads (the game runs the two hooks of a frame one after the other, on whichever
// job threads); the pin is kept under a lock, the viewmodel hook's caches under its own.

#include "features/arm/frame_pin.hpp"
#include "vkcore/controllers_impl.hpp"

#include <cstddef>
#include <optional>

namespace evr::vkcore::controllers {

// The viewmodel hook's placement of the model and the inputs it was made from.
struct ViewmodelPlacement {
    xr_math::EyeRelativePose pose;
    arm::PinInputs inputs;
};

// The arms model's placement (relative to the eye) the viewmodel hook gives the player's hands for the arms'
// item `arms` and the hands `world` (viewmodel_hook.cpp), or nullopt where it leaves the model to the game
// (the viewmodel off, a forced view, head aim, the fists on the game's view). Reads the held item's rows
// under the viewmodel hook's lock.
std::optional<ViewmodelPlacement> viewmodelPlacement(const ArmsTick& arms, const WorldHand& world);

namespace frame_placement {

// Off-hand hook, in the modes that pose an arm: the view to pose the arms of `hands` by in this tick: `view`,
// with the model's pose (and the stamp of the hands it came from) the frame's own when the viewmodel hook has
// not placed the model in it yet, pinned for the check; its clock stamp and currency stay the last
// placement's.
TickView forArms(const std::byte* hands, const ArmsTick& arms, TickView view);

// Viewmodel hook, for each tick of the player's hands: takes the frame's pin. `placed` with the placement
// written checks it; without that call it counts as not placed.
class PinCheck {
public:
    explicit PinCheck(const std::byte* hands);
    ~PinCheck();
    PinCheck(const PinCheck&) = delete;
    PinCheck& operator=(const PinCheck&) = delete;
    void placed(const arm::PinInputs& inputs, const xr_math::EyeRelativePose& placement);

private:
    std::optional<ViewmodelPlacement> pin_;
    bool checked_ = false;
};

} // namespace frame_placement
} // namespace evr::vkcore::controllers
