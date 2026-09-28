#pragma once

// The comfort vignette (ETERNALVR_VIGNETTE, docs/VR_CONTROLLERS.md): while the stick moves or turns the
// player, or the game moves the camera itself (a dash, a glory kill, the Meathook pull), the edges of the
// view darken and leave a clear centre, and they clear again once the motion stops. Less moving picture in
// the corner of the eye is what keeps artificial motion comfortable for many people. Real head motion never
// shows it: only the stick, the turn and the game's own camera moves count.
//
// VignettePolicy turns the motion of each frame into an amount from 0 (clear) to 1 (the look's full
// vignette), rate-limited so it comes in quickly and goes out gently. The layer shows the amount with a few
// images made in advance (a quad layer has no alpha of its own): vignetteLevel picks the nearest one and
// vignetteImage draws it. No OpenXR or Windows here.

#include <cstdint>
#include <vector>

namespace evr::comfort {

// This frame's artificial motion.
struct VignetteMotion {
    float turnDegreesPerSecond = 0.0f; // the stick's turning (smooth or snap), either direction
    float moveMagnitude = 0.0f;        // the move stick after its response, 0 to 1
    bool gameMotion = false;           // the game moves the camera (a dash or a forced view)
};

struct VignetteTiming {
    // The turn rate where the vignette starts, and where it is full.
    float turnStartDegreesPerSecond = 10.0f;
    float turnFullDegreesPerSecond = 120.0f;
    // The move stick's magnitude where the vignette starts, and where it is full.
    float moveStart = 0.1f;
    float moveFull = 0.7f;
    float riseSeconds = 0.2f; // clear to full at most this long
    float fallSeconds = 0.5f; // full to clear at most this long
};

// The amount the motion asks for: the largest of the turn's, the move's and the game's (1 while it moves
// the camera). Non-finite motion counts as none.
float vignetteTarget(const VignetteMotion& motion, const VignetteTiming& timing);

class VignettePolicy {
public:
    // Out-of-range or non-finite timing values take their defaults.
    explicit VignettePolicy(VignetteTiming timing = {});

    // Moves the amount toward the target of `motion` over `dtSeconds` and returns it. A non-finite or
    // negative step changes nothing; a step above 1 s counts as 1 s.
    float update(const VignetteMotion& motion, double dtSeconds);

    [[nodiscard]] float value() const { return value_; }
    [[nodiscard]] const VignetteTiming& timing() const { return timing_; }
    void reset() { value_ = 0.0f; }

private:
    VignetteTiming timing_;
    float value_ = 0.0f;
};

// The vignette's shape at one amount, in degrees from the view's axis: clear inside `clearDegrees`, darker
// on the way out to `darkDegrees`, `opacity` (0 to 1) from there on.
struct VignetteShape {
    float clearDegrees = 90.0f;
    float darkDegrees = 90.0f;
    float opacity = 0.0f;
};

// How far a vignette closes in. At amount 0 the clear area reaches `startClearDegrees` (and the vignette is
// invisible, opacity 0); at amount 1 it has shrunk to `fullClearDegrees`, `featherDegrees` of falloff
// outside it, with `opacity` at the edges.
struct VignetteLook {
    float startClearDegrees = 55.0f;
    float fullClearDegrees = 40.0f;
    float featherDegrees = 25.0f;
    float opacity = 0.8f;
};

// Light: the edges darken a little, most of the view stays. Strong: a narrow tunnel with black edges.
inline constexpr VignetteLook kLightVignette{55.0f, 40.0f, 25.0f, 0.8f};
inline constexpr VignetteLook kStrongVignette{45.0f, 25.0f, 20.0f, 1.0f};

// The shape at `amount` (clamped to 0..1; non-finite counts as 0).
VignetteShape vignetteShape(float amount, const VignetteLook& look);

// The image to show for `amount` out of `levels` images made for the amounts 1/levels, 2/levels ... 1: the
// nearest one, or 0 for none (the amount is closer to clear than to the first image).
int vignetteLevel(float amount, int levels);

// A `size` x `size` RGBA8 image, premultiplied alpha, for a quad seen from its centre's normal that spans
// `quadHalfDegrees` each way from the view axis at its edges' midpoints: black with the shape's alpha by the
// angle from the view axis, a smooth (smoothstep) falloff from clear to dark, transparent in the centre.
std::vector<std::uint8_t>
vignetteImage(std::uint32_t size, const VignetteShape& shape, float quadHalfDegrees);

} // namespace evr::comfort
