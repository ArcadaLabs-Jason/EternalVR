#pragma once

// Shot cuts in a cutscene shown around the player (ETERNALVR_CUTSCENES=immersive, docs/VR_HEAD_TRACKED.md).
//
// The view in a cutscene is the camera's yaw plus the head's (pitch and roll are the head's). A reverse shot
// turns the camera about 170 degrees in one frame, so the world swung by the cut and the action landed far
// from where a player who had turned to follow it was looking (public issue #24). On the cutscene's first
// frame and on each cut the body yaw is re-based so the camera's forward lands where the head looks now
// (body = camera yaw - head yaw); within a shot the view still follows the camera's own turns. When the
// cutscene ends the re-base is dropped, and head aim's yaw is re-based instead (rebaseHeadYaw), so the
// player's own view faces where the head looks too.
//
// A cut is a camera yaw change of more than kCutYawDegrees in one game frame, unless the camera looks within
// kCutPitchDegrees of straight up or down in that frame or the last (its yaw flips there). renderView_t.
// cameraCut (+0x16) is logged with each re-base but not used until a rig run shows it is set on cuts only.
// A gap of more than kCutGapSeconds since the last frame seen (a map load, a hitch, frames the hook did not
// reach) starts afresh: re-based like a cutscene's first frame. While a menu is over the cutscene nothing is
// detected and the offset stays.

#include "xr_math/head_aim.hpp"

#include <cstdint>
#include <optional>

namespace evr::xr_math {

inline constexpr float kCutYawDegrees = 90.0f;
inline constexpr float kCutPitchDegrees = 80.0f;
inline constexpr double kCutGapSeconds = 0.5;

struct CutRebaseState {
    bool inCutscene = false;
    std::optional<float> lastCameraYaw;
    float lastCameraPitch = 0.0f;
    std::optional<double> lastSeconds;
    float offset = 0.0f; // degrees added to the body yaw until the cutscene ends
};

struct CutFrame {
    bool cutscene = false;    // a cutscene frame shown around the player
    float cameraYaw = 0.0f;   // the cutscene camera's yaw this frame (degrees)
    float cameraPitch = 0.0f; // and its pitch (id Tech: positive looks down)
    float bodyYaw = 0.0f;     // the body yaw the view would have without the re-base
    float headYaw = 0.0f;     // the head's yaw in the room
    double seconds = 0.0;     // a monotonic time
    bool menu = false;        // a menu is over the cutscene: no start or cut, the offset stays
};

enum class CutEvent : std::uint8_t {
    None,
    Start, // the cutscene's first frame: re-based
    Cut,   // a cut: re-based
    Gap,   // the first frame after a gap of more than kCutGapSeconds: re-based
    End,   // the first frame after it: the re-base is dropped
};

struct CutStep {
    float bodyYaw = 0.0f;
    CutEvent event = CutEvent::None;
    float turn = 0.0f;       // how far this frame's re-base turned the body from the last frame's (degrees)
    float cameraTurn = 0.0f; // the camera's yaw change since the last frame (degrees)
    double gapSeconds = 0.0; // the time since the last frame seen
};

// One game frame.
CutStep cutRebaseStep(CutRebaseState& state, const CutFrame& frame);

// The game's yaw now holds `headYaw` of head yaw: head aim's next step takes the body as the game's yaw
// less `headYaw`, so the view (body + head) faces the game's yaw where the head looks now. The values head
// aim wrote before are forgotten (a restore of one of them would undo this). Returns how far the body
// turns from what head aim would have used (degrees).
float rebaseHeadYaw(HeadAimState& state, float headYaw);

const char* cutEventName(CutEvent event);

} // namespace evr::xr_math
