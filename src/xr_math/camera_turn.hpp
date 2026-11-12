#pragma once

// A pickup camera's turn on top of the head-tracked view (comfort/pickup_camera.hpp, docs/rig-findings/
// camera-animations.md section 6).
//
// A pickup's sync animation drives the game's camera for a few seconds. In the flat game the camera cuts to
// the animation's own start in one frame (the Praetor token's: from the player's 15 degrees down to 5.6
// degrees), then moves smoothly (down to 11.4 degrees over 1.3 s, up to 2.2 degrees over 0.45 s), and the
// game puts the player's view angles where it ended. On the head-tracked view the head is where the view
// starts, so the part played is the camera's smooth motion since it took the view: its turn from one game
// frame to the next, summed. A step larger than CameraTurnTiming::cutDegrees in one frame is a cut (the
// start's, for one) and is left out, so the view never jumps; and when the pickup ends, what is left of the
// turn eases out instead of snapping back.

#include "xr_math/head_aim.hpp"
#include "xr_math/head_view.hpp"

#include <optional>

namespace evr::xr_math {

struct CameraTurnTiming {
    // A turn of more than this in any angle from one game frame to the next is a cut, not motion (the token's
    // smooth motion is under 0.7 degrees a frame; its start cuts 9.4 degrees).
    float cutDegrees = 5.0f;
    // Each angle of the turn is held within this.
    float limitDegrees = 45.0f;
    // After the pickup the turn left eases to none over this (a smoothstep).
    double releaseSeconds = 0.3;
};

class CameraTurn {
public:
    explicit CameraTurn(CameraTurnTiming timing = {});

    // One game frame: whether the camera is followed now (a pickup runs), the game's camera and a monotonic
    // clock (seconds). Returns the turn to add on top of the head this frame (addCameraAnim's angles): the
    // summed motion while followed, then easing out; nullopt once nothing is left. A camera with angles that
    // are not finite adds nothing; a clock that is not finite ends a release at once.
    std::optional<IdAngles> update(bool follow, const IdViewAxis& camera, double seconds);

    // The cuts left out and the largest turn (each angle) since the camera was last followed from none.
    [[nodiscard]] unsigned cuts() const { return cuts_; }
    [[nodiscard]] const IdAngles& largest() const { return largest_; }

private:
    CameraTurnTiming timing_;
    std::optional<IdAngles> last_; // the camera's angles last frame while followed
    IdAngles turn_;
    bool releasing_ = false;
    IdAngles releaseFrom_;
    double releaseStart_ = 0.0;
    unsigned cuts_ = 0;
    IdAngles largest_;
};

// The part of a camera turn that goes on top of the head: all of it on a body that keeps the player's own
// heading; without its yaw on a body that already faces the camera's heading (the view built on the game's
// yaw: head aim off, not checked yet, or not the player), which would turn the view by the camera's yaw
// twice.
IdAngles turnOnBody(const IdAngles& turn, bool bodyOwnHeading);

} // namespace evr::xr_math
