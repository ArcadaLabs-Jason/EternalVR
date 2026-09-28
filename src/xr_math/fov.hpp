#pragma once

// Field of view in the OpenXR convention, without depending on OpenXR headers.

namespace evr::xr_math {

// Half-angles of a view frustum in radians, measured from the view's forward axis (-Z).
// As in XrFovf, angleLeft and angleDown are normally negative and angleRight and angleUp positive.
// The frustum need not be symmetric: headset eye frusta are usually wider on the temporal side.
struct Fov {
    float angleLeft = 0.0f;
    float angleRight = 0.0f;
    float angleUp = 0.0f;
    float angleDown = 0.0f;
};

// The same frustum expressed as tangents of the half-angles. Projection math works on tangents, and
// the extents of a frustum on the plane z = -1 are exactly these values.
struct FovTangents {
    float left = 0.0f;
    float right = 0.0f;
    float up = 0.0f;
    float down = 0.0f;
};

FovTangents toTangents(const Fov& fov);
Fov fromTangents(const FovTangents& tangents);

// Widens an asymmetric FOV to the smallest symmetric one containing it.
Fov makeSymmetric(const Fov& fov);

} // namespace evr::xr_math
