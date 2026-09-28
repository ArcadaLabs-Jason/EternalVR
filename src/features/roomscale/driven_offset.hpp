#pragma once

// The head's offset while the game drives the view (a glory kill, the Meathook pull, a scripted camera, a
// cutscene; docs/VR_ROOMSCALE.md "Driven views").
//
// The game's eye is then an animated one on the Slayer's own body, and body follow is off, so whatever
// offset the head had from the body when the game took over (a lean, a step body follow had not closed yet,
// a crouch) would put the camera that far from the animated eye: behind or above it the player sees the
// Slayer's own shoulders and arms. While the game drives the view the offset it had when the game took
// over eases out, so the camera sits on the game's eye and moves only by what the head does from there;
// when the game lets go the offset eases back in, and body follow closes it as before.
//
// Room axes (room_anchor.hpp): +X right, +Y up, -Z forward, metres before world scale.

#include "common/vector.hpp"

namespace evr::roomscale {

// The ease's time constant: 95 % of the way in three of these.
inline constexpr double kDrivenEaseSeconds = 0.06;

class DrivenViewOffset {
public:
    // One camera frame: `offset` is the head's offset from the game's eye (room axes, metres), `driven`
    // whether the game drives the view this frame, `seconds` a monotonic time. Returns the offset to use.
    Vec3 update(Vec3 offset, bool driven, double seconds);

    // The part of the offset held back now (room metres).
    [[nodiscard]] Vec3 held() const { return held_; }
    // True on the frame a driven episode began (the call's `driven` was true, the last one's false).
    [[nodiscard]] bool began() const { return began_; }

private:
    bool driven_ = false;
    bool began_ = false;
    Vec3 base_;
    Vec3 held_;
    double last_ = -1.0;
};

} // namespace evr::roomscale
