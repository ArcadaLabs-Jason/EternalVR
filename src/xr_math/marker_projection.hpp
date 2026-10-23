#pragma once

// The HUD's screen-projected world markers on the head-locked UI quad (docs/rig-findings/hud-markers.md).
//
// The HUD places objective and POI markers, damage numbers and the other world-attached pieces at the GUI
// pixel the game's own projection gives a world point: one matrix per game frame, the latched projection of
// the world's render view times the player's view (RVA 0x6CF769, read by the projector at RVA 0xF2ABC0),
// mapped over the whole GUI target. The headset does not show the GUI target over the game's frustum: the UI
// layer shows it on a quad 2 m wide at 1.5 m (about 67 x 41 degrees for its 16:9 band) while the eye frustum
// spans about 108 degrees, so a marker sits at roughly half its target's angle from the middle of the view.
//
// The fix gives the HUD a projection whose frustum is the GUI target as the quad shows it, seen from the head
// centre: a direction then lands on the GUI pixel that the quad shows in that direction. Rows 2 and 3 (depth
// and w) stay the engine's.

#include "xr_math/fov.hpp"
#include "xr_math/stereo_view.hpp"

#include <optional>

namespace evr::xr_math {

// The head-locked UI quad: `widthMetres` wide (its height from the shown part's aspect), centred
// `distanceMetres` ahead of the head and `offsetYMetres` up, facing the head; it shows the part `shown*` of a
// `targetWidth` x `targetHeight` GUI target (the 16:9 band, or the whole target).
struct MarkerQuad {
    float widthMetres = 0.0f;
    float distanceMetres = 0.0f;
    float offsetYMetres = 0.0f;
    int targetWidth = 0;
    int targetHeight = 0;
    int shownX = 0;
    int shownY = 0;
    int shownWidth = 0;
    int shownHeight = 0;
};

// The frustum the whole GUI target covers as the quad shows it, from the head centre (OpenXR angles: left and
// down negative). nullopt for an empty target or shown part, a shown part outside the target, or a quad that
// is not ahead of the head.
std::optional<Fov> markerTargetFov(const MarkerQuad& quad);

// The HUD's projection for the quad: rows 0 and 1 from markerTargetFov (xr_math::engineProjection), rows 2
// and 3 from `engine` (the game's own projection for the frame). nullopt when either is missing.
std::optional<EngineMatrix> markerProjection(const MarkerQuad& quad, const EngineMatrix& engine);

// A pixel of the GUI target, x to the right and y down from its top-left corner.
struct GuiPoint {
    float x = 0.0f;
    float y = 0.0f;
};

// Where the quad shows the direction with tangents (`right`, `up`) from the head: the GUI target pixel at the
// point where that ray meets the quad's plane. nullopt for an invalid quad.
std::optional<GuiPoint> quadPixelOfTangents(const MarkerQuad& quad, float right, float up);

// The direction the quad shows GUI target pixel `p` in, as tangents (right, up) from the head. nullopt for an
// invalid quad.
struct ViewTangents {
    float right = 0.0f;
    float up = 0.0f;
};
std::optional<ViewTangents> tangentsOfQuadPixel(const MarkerQuad& quad, GuiPoint p);

// The game's projector (RVA 0xF2ABC0) for a point in the engine's view space (x right, y up, z back) through
// `m`, onto a `width` x `height` screen: ((1 + x) / 2 * width, (1 - y) / 2 * height). nullopt for a point on
// the eye plane (w = 0) or behind the head.
std::optional<GuiPoint>
projectToScreen(const EngineMatrix& m, float x, float y, float z, float width, float height);

// Whether the HUD treats a marker as off its `width` x `height` frame (and clamps it to the frame's edge):
// the game's own test (`gameOffFrame`), made on the position before the HUD stretches it for the render's
// aspect, or the position it draws (`drawn`, after the stretch) outside [0, width] x [0, height]. On a target
// taller than 16:9 the game's test alone lets a marker above or below the 16:9 band count as on the frame
// while it is drawn in the rows the quad does not show.
bool markerOffFrame(bool gameOffFrame, GuiPoint drawn, float width, float height);

} // namespace evr::xr_math
