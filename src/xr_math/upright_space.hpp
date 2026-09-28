#pragma once

// A LOCAL reference space that is not upright, and the upright one to use instead.
//
// OpenXR requires LOCAL's +Y to point up (against gravity). Some runtime setups break that: SteamVR
// fronting a Quest through Virtual Desktop has reported LOCAL turned upside down, with the head 1 m
// below its origin and rolled 180 degrees, while its STAGE space was upright. Everything the layer
// derives from LOCAL (the game's camera, panels placed ahead of the head) then turns over with it.
// When LOCAL's orientation in STAGE tilts its up axis, the layer creates its base space from STAGE
// instead, at LOCAL's origin and with LOCAL's heading only, so it keeps LOCAL's place and facing and
// is upright.

#include <optional>

namespace evr::xr_math {

// A rotation as an OpenXR quaternion (x, y, z, w), Y up, -Z forward.
struct Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

// How far LOCAL's up axis may lean from STAGE's before LOCAL counts as not upright: the cosine of
// the angle between them (0.9 is about 26 degrees). A runtime's own recentering never tilts LOCAL.
inline constexpr float kUprightMinCosine = 0.9f;

// The cosine of the angle between the up axis rotated by `q` and +Y (1 for upright, -1 for upside
// down). A zero-length quaternion reads as upright.
float upCosine(const Quaternion& q);

// The rotation about +Y alone that keeps `q`'s heading: its forward axis (-Z) projected on the floor.
// When the forward axis is vertical, the up axis gives the heading instead; identity when both are.
Quaternion headingOnly(const Quaternion& q);

// The orientation for the replacement base space, relative to STAGE: nullopt when `localInStage` is
// upright (keep LOCAL), else its heading only.
std::optional<Quaternion> uprightReplacement(const Quaternion& localInStage,
                                             float minCosine = kUprightMinCosine);

} // namespace evr::xr_math
