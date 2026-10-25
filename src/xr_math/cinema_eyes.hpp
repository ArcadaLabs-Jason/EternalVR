#pragma once

// Stereoscopic cutscenes on the flat screen (docs/VR_STEREO.md, "Stereoscopic cutscenes"): the two eyes of
// the game's cutscene camera, as parallel cameras `interaxial` apart with their frusta shifted (off-axis), so
// the pair is right for a flat screen: no vertical parallax, no keystone, and infinity lands a small constant
// distance behind the screen.
//
// Screen disparity is reported as a fraction of the screen's width, right image minus left image: positive
// is behind the screen (uncrossed), negative in front of it (crossed). For a point `depth` metres in front of
// the camera, with tanHalfX the tangent of the horizontal half-FOV and `shift` the disparity of infinity:
//     disparity = shift - interaxial / (2 * tanHalfX * depth)

#include "common/vector.hpp"
#include "xr_math/fov.hpp"
#include "xr_math/head_view.hpp"

#include <array>
#include <optional>

namespace evr::xr_math {

// Infinity sits this far behind the screen (fraction of its width): 2.4 cm on a 2.4 m screen, 0.55 degrees
// at 2.5 m, well under any eye separation, so the eyes never diverge.
inline constexpr float kCinemaInfinityShift = 0.01f;
// The most a nearby object may be in front of the screen (fraction of its width): 1.2 degrees of crossed
// disparity on a 2.4 m screen at 2.5 m, the usual comfort limit being 1 to 1.5 degrees.
inline constexpr float kCinemaMaxCrossed = 0.0218f;
// The nearest thing a shot is assumed to show: a face in a close-up, 0.7 m from the camera, whatever the lens
// (rig captures of the opening cutscene had narrow lenses at 0.7 to 1 m and wide ones at 0.2 to 0.6 m). With
// it the largest separation is proportional to the lens's half-width tangent and the zero-parallax plane sits
// at a fixed distance (kCinemaNearestMetres * (shift + crossed limit) / shift, 2.2 m), so a given depth has
// the same disparity in every shot.
inline constexpr float kCinemaNearestMetres = 0.7f;
// Measured distances: the camera hook traces rays through the shot (cinemaRayFan), and the nearest hit within
// kCinemaTraceReachMetres counts as the nearest thing the shot shows when it is nearer than
// kCinemaNearestMetres (never farther: what the traces cannot see, such as an actor in a cutscene, stays with
// the assumption), but never nearer than kCinemaMinTraceMetres (a camera inside a wall is not worth a flat
// shot).
inline constexpr float kCinemaTraceReachMetres = 6.0f;
inline constexpr float kCinemaMinTraceMetres = 0.25f;
// The separation allowed may fall at once (a close object appears) and rises back over about this long (what
// was close leaves, or the lens changes), so the depth does not pump; a camera cut takes the new value at
// once.
inline constexpr float kCinemaLimitRiseSeconds = 0.6f;
// The camera separation when the player has set none (ETERNALVR_IPD unset): an average adult's.
inline constexpr float kCinemaDefaultInteraxialMetres = 0.063f;

struct CinemaEyeView {
    float leftUnits = 0.0f; // the eye's offset along the camera's left axis, game units (eye L: positive)
    Fov fov;                // the eye's off-axis frustum
};

struct CinemaStereoInput {
    float interaxialMetres = kCinemaDefaultInteraxialMetres; // wanted camera separation
    float fovXDegrees = 0.0f;                                // the view's FOV as the game is given it
    float fovYDegrees = 0.0f;
    float unitsPerMetre = 1.0f;
    // The nearest distance the camera hook's traces found (0: none, or no hit within reach).
    float traceNearestMetres = 0.0f;
    // The largest separation to allow, when the caller smooths it over time (cinemaSmoothLimit); 0: the limit
    // for this frame (CinemaStereo::comfortMetres).
    float limitMetres = 0.0f;
};

struct CinemaStereo {
    std::array<CinemaEyeView, 2> eyes; // [0] left, [1] right
    float interaxialMetres = 0.0f;     // used (never above the wanted one)
    bool clamped = false;              // the comfort clamp lowered it
    float zeroParallaxMetres = 0.0f;   // the camera distance that lands on the screen
    float comfortMetres = 0.0f;        // the largest separation for this frame's nearest distance
    float nearestMetres = 0.0f;    // the nearest distance the clamp assumes (the assumption and the traces)
    float nearestDisparity = 0.0f; // screen disparity there (negative: in front of the screen)
};

// Disparity (fraction of the screen width) of a point `depthMetres` in front of the camera.
float cinemaDisparity(float interaxialMetres, float tanHalfX, float shift, float depthMetres);

// The camera distance whose disparity is zero.
float cinemaZeroParallax(float interaxialMetres, float tanHalfX, float shift);

// The nearest distance a shot is assumed to show: kCinemaNearestMetres, or the traces' (`traceNearestMetres`,
// 0: none) when nearer.
float cinemaNearestMetres(float traceNearestMetres = 0.0f);

// The largest separation that keeps the assumed nearest distance within kCinemaMaxCrossed.
float cinemaComfortInteraxial(float tanHalfX, float traceNearestMetres = 0.0f);

// A ray of the fan the camera hook traces: the direction is forward + right * `right` + up * `up` (tangents,
// not normalised).
struct CinemaRay {
    float right = 0.0f;
    float up = 0.0f;
};
// 5 columns by 3 rows of rays spread over the screen's view: columns at -0.8 to 0.8 of the half-width, rows
// at -0.7, 0 and 0.7 of the half-height (`tanHalfLens`: the band's, not the tall image's).
std::array<CinemaRay, 15> cinemaRayFan(float tanHalfX, float tanHalfLens);

// The unit direction of `ray` for a camera with view axis `axis` (id Tech rows): the image's right is the
// camera's -left, so a ray to the right of the picture has a negative component along `axis.left`.
Vec3 cinemaRayDirection(const IdViewAxis& axis, const CinemaRay& ray);

// The separation limit for the next frame: `target` when it is below `current` (or `current` is not set, 0)
// or `cut` is set, else `current` moving toward it with the time constant kCinemaLimitRiseSeconds over
// `seconds` (0 to 0.1 are used).
float cinemaSmoothLimit(float current, float target, float seconds, bool cut);

// The vertical FOV of what a screen of `screenAspect` (width / height; 0: the whole image) shows of an image
// drawn with fovXDegrees x fovYDegrees (square pixels): the image's centred band of that aspect.
float cinemaLensFovY(float fovXDegrees, float fovYDegrees, double screenAspect);

// The two eyes for a cutscene view; nullopt for FOVs out of (0, 170) degrees, a non-positive scale or
// separation, or anything not finite.
std::optional<CinemaStereo> cinemaStereo(const CinemaStereoInput& input);

} // namespace evr::xr_math
