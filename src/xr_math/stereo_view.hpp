#pragma once

// Per-eye views for the engine's own two-view path (docs/VR_STEREO.md).
//
// The engine renders one head-centred game view per tick. With two screen views, each eye's render
// view gets the head view moved to that eye and turned to its orientation, and an explicit projection
// matrix for the eye's own (asymmetric) frustum. This file holds the math, free of engine and OpenXR
// types:
//
// - the eye's origin offset and view axis in id Tech world space, from the game's body frame, the
//   headset orientation and the eye's pose in head (VIEW) space;
// - the eye's pose in the tracking space, for the projection layer;
// - the eye's projection in the engine's matrix layout (idRenderMatrix, row-major, the layout of the
//   engine's own builder at RVA 0x39A310: m[2] = (r + l) / (r - l), m[14] = -1);
// - the eye's rectangle in the side-by-side image the engine presents;
// - which render views take the TAA jitter of the first screen view.

#include "common/pose.hpp"
#include "common/quat.hpp"
#include "common/vector.hpp"
#include "xr_math/fov.hpp"
#include "xr_math/head_view.hpp"

#include <array>
#include <optional>

namespace evr::xr_math {

// An eye relative to the head centre (the OpenXR VIEW space), as xrLocateViews reports it.
struct EyeInHead {
    Pose pose;
    Fov fov;
};

// Where the engine renders one eye from, in id Tech world space.
struct IdEyeView {
    Vec3 offset;     // added to the head-centred view origin (game units)
    IdViewAxis axis; // the eye's view axis
};

// The eye's view: headOpenXr is the headset orientation in the tracking space (OpenXR axes), body
// the game's body frame (yaw only), eyeInHead the eye's pose in head space (OpenXR axes, metres).
IdEyeView eyeViewInWorld(const IdViewAxis& body, Quat headOpenXr, const Pose& eyeInHead, float unitsPerMetre);

// The eye's pose in the tracking space: the head pose composed with the eye's pose in head space.
Pose eyePoseInSpace(const Pose& head, const Pose& eyeInHead);

// The eye's pose in head space from the head and the eye located in the same tracking space at the same
// time. Locating the eyes in the tracking space and taking them relative to the head does not depend on
// the runtime's VIEW space (OpenXR-Simulator 1.5 reports VIEW-space eye poses with the head's height in
// them).
Pose eyeInHeadFromSpace(const Pose& headInSpace, const Pose& eyeInSpace);

// True for an eye pose in head space a headset can have: finite and within 15 cm of the head centre.
bool plausibleEyeInHead(const Pose& eyeInHead);

using EngineMatrix = std::array<float, 16>; // idRenderMatrix, row-major: element [row * 4 + column]

// True when `m` is a perspective matrix of the engine's builder: finite, m[14] = -1, m[15] = 0 and a
// depth row (m[10], m[11]) that is not zero. Only such a matrix provides the depth rows.
bool isEnginePerspective(const EngineMatrix& m);

// The engine's projection for an asymmetric frustum: rows 0 and 1 from the FOV (the engine's builder
// with l = n tan(left), r = n tan(right), b = n tan(down), t = n tan(up)), rows 2 and 3 copied from
// `depthSource` (the engine's own matrix for the same view, which fixes the near and far planes).
// nullopt for an FOV without a finite frustum or a depthSource that is not an engine perspective.
std::optional<EngineMatrix> engineProjection(const Fov& fov, const EngineMatrix& depthSource);

// The half-angles an engine matrix projects (the inverse of engineProjection's rows 0 and 1).
std::optional<Fov> fovOfEngineProjection(const EngineMatrix& m);

struct PixelRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// The rectangle of screen view `eye` (0 left, 1 right) in an image of the given size when the engine
// lays two views out side by side. The engine truncates `fraction * size` to an integer for each edge
// (RVA 0x17E8740), so the halves of an odd width differ by one pixel.
PixelRect sideBySideRect(int eye, int width, int height);

// The TAA jitter index is set by the engine on the first screen view only (RVA 0x1CB9EE0). Every other
// screen view takes the first one's, so both eyes jitter in step and each eye's history stays its own.
constexpr bool copiesFirstViewJitter(int screenViewIndex) {
    return screenViewIndex > 0;
}

} // namespace evr::xr_math
