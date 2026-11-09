#pragma once

// Where the free off hand's glove was drawn against where the layer meant it, frame by frame, for the off
// hand's 10 s line (features/input/offhand_watch.hpp, vkcore/offhand_stats.hpp).
//
// The arm is posed in the arms model's space under a placement of the model, and the game draws it under the
// placement the viewmodel hook gives the model in the frame it is drawn in. That is the frame after the one
// it was written in: the game builds the skeleton it draws in a frame before the off-hand hook of that frame
// runs, from the modifiers written in the frame before [rig, docs/rig-findings/hands-rig.md section 26: in
// 168 of 168 frames in which the written wrist moved, GetJointTransforms gave the last frame's write at the
// off-hand hook, at the viewmodel hook and at the camera hook of the frame, and this one's only at the next
// frame's off-hand hook; and the captures showed the glove off for the frames that predicts]. So the wrist
// read by the off-hand hook in a frame is the one drawn in it, under that frame's placement, and it was meant
// where the wrist written in the frame before lands under the placement it was posed under. Frames are the
// game's own numbers (vkcore/game_frame.hpp).
//
// Pure: no game memory.

#include "common/vector.hpp"
#include "xr_math/weapon_pose.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace evr::arm {

// The arms model as the viewmodel hook placed it in a game frame: the eye (world) and the placement from it.
struct FramePlacement {
    std::int32_t frame = 0;
    Vec3 eye;
    xr_math::EyeRelativePose pose;
};

// The free off hand as the layer posed it in a game frame: the eye, the placement it was posed under, the
// wrist written (model space, game units), and the stamps of the hands snapshot it was posed from and of the
// one that placement was placed from (seconds).
struct PosedWrist {
    std::int32_t frame = 0;
    Vec3 eye;
    xr_math::EyeRelativePose placement;
    Vec3 wrist;
    double snapshotSeconds = 0.0;
    double placementSnapshotSeconds = 0.0;
};

// The game's read of the wrist at the off-hand hook of a game frame (model space, game units): the pose drawn
// in that frame.
struct ReadWrist {
    std::int32_t frame = 0;
    Vec3 wrist;
};

struct DrawnWrist {
    Vec3 drawn; // metres, world axes, the world's origin over unitsPerMetre
    Vec3 meant;
    double snapshotGapMs = 0.0; // the posed arm's snapshot minus its placement's
};

// A point in the model's space (game units) placed by `pose` at `eye`, in the world.
Vec3 placedPoint(Vec3 eye, const xr_math::EyeRelativePose& pose, Vec3 point);

// `posed`, drawn in the frame after its own as `read` gives it, under that frame's placement in `placements`.
// nullopt when `read` is not of the frame after `posed`'s, no placement is for its frame (the viewmodel hook
// did not place the model then) or unitsPerMetre is not above zero.
std::optional<DrawnWrist> drawnWrist(const PosedWrist& posed,
                                     std::span<const FramePlacement> placements,
                                     const ReadWrist& read,
                                     float unitsPerMetre);

} // namespace evr::arm
