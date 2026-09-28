#pragma once

// Head-tracked game view (mono): conversion between the OpenXR and id Tech 7 conventions and the
// composition of the game's body yaw with the headset orientation.
//
// id Tech 7 world and view axes: right-handed, +X forward, +Y left, +Z up. A renderView_t's
// `viewaxis` is an idMat3 whose rows are the view's forward, left and up directions in world space.
// OpenXR: right-handed, +X right, +Y up, -Z forward. The mapping between the two is a proper
// rotation (determinant +1), so orientations convert by rotating the quaternion's vector part:
//     id.x = -xr.z    id.y = -xr.x    id.z = xr.y
//
// World scale: game units per metre. DOOM Eternal's units are very likely metres
// (docs/notes/eternal-unit-scale-evidence.md), so the default is 1.0.

#include "common/quat.hpp"
#include "common/vector.hpp"
#include "xr_math/fov.hpp"

#include <optional>

namespace evr::xr_math {

// The rows of an id Tech idMat3 view axis, each a world-space direction.
struct IdViewAxis {
    Vec3 forward{1.0f, 0.0f, 0.0f};
    Vec3 left{0.0f, 1.0f, 0.0f};
    Vec3 up{0.0f, 0.0f, 1.0f};
};

// Converts a direction or offset from OpenXR axes to id Tech axes (no scaling).
constexpr Vec3 openXrToIdTech(Vec3 v) {
    return {-v.z, -v.x, v.y};
}

// Converts an orientation from OpenXR axes to id Tech axes.
constexpr Quat openXrToIdTech(Quat q) {
    return {-q.z, -q.x, q.y, q.w};
}

// The view axis of an orientation in id Tech axes: the rotated forward, left and up unit vectors.
IdViewAxis viewAxisFromQuat(Quat orientation);

// True when the three rows are unit length and mutually orthogonal within `tolerance`, and form a
// right-handed frame (forward x left = up). Game data is checked with this before it is used.
bool isOrthonormal(const IdViewAxis& axis, float tolerance = 1e-3f);

// The body frame: the game view's heading with pitch and roll removed (up = world +Z). When the game
// looks straight up or down the heading comes from the view's up vector instead. Returns nullopt for
// a degenerate axis (zero or non-finite rows).
std::optional<IdViewAxis> yawOnly(const IdViewAxis& gameAxis);

// The head-tracked view axis: the headset orientation (id Tech axes, relative to the tracking space)
// applied inside the body frame. World = body * head.
IdViewAxis composeHeadAxis(const IdViewAxis& body, Quat headInIdTech);

// The head's tracked position as a world-space offset in game units: the OpenXR position (metres,
// tracking space) converted to id Tech axes, scaled, and rotated by the body frame.
Vec3 headOffsetInWorld(const IdViewAxis& body, Vec3 headPositionOpenXr, float unitsPerMetre);

// Game FOV values (renderView_t fov_x / fov_y, full angles in degrees) for a symmetric frustum.
struct GameFov {
    float fovX = 90.0f;
    float fovY = 90.0f;
};

// The game FOV for symmetric tangents; nullopt unless both tangents are finite and positive.
std::optional<GameFov> gameFovFromTangents(float tanHalfX, float tanHalfY);

// The OpenXR FOV the game renders for the given fov_x / fov_y (symmetric about the view axis).
// nullopt unless both angles are finite and inside (0, 180) degrees.
std::optional<Fov> fovFromGame(const GameFov& game);

} // namespace evr::xr_math
