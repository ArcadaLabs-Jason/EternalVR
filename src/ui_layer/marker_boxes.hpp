#pragma once

// The boxes the HUD's world-attached markers take from the GUI target for their own quad
// (docs/rig-findings/hud-markers.md 5b; vkcore/presenter_markers.hpp).
//
// The POI update draws each marker (the diamond and the distance under it) about the point it computed
// (vkcore/marker_hooks.hpp): the SWF's frame (3840 x 2160) over the 16:9 band of the GUI target
// (wideContentRect), to the pixel [rig, docs/rig-findings/hud-markers.md]. A box around that point holds the
// marker: in a 1280 x 1400 target's 1280 x 720 band at the HUD Scale VR holds (Large) the diamond is about
// 26 px across and its distance (up to "533.0m") about 42 x 12 px right under it [rig GUI captures,
// qa-hud-markers-ui 2026-10-25]. The boxes below hold that with room to spare, in band fractions so they
// follow the band's size. A box is taken for each of the last few game frames' points (the GUI image shown is
// one of them), so a marker that moves between them stays inside its box.

#include "ui_layer/ui_settings.hpp"

#include <cstdint>
#include <vector>

namespace evr::ui_layer {

// A marker's point in band coordinates: 0 to 1 across and down the band (the SWF's frame), and how far ahead
// of the view its target is, in world units (0: not known).
struct MarkerSpot {
    float x = 0.0f;
    float y = 0.0f;
    float ahead = 0.0f;
};

// A marker's box about its point, in band fractions: half its width, and its reach above and below.
inline constexpr float kMarkerHalfWidth = 0.025f; // 32 px of a 1280 px band
inline constexpr float kMarkerAbove = 0.03f;      // 22 px of a 720 px band
inline constexpr float kMarkerBelow = 0.055f;     // 40 px

// The boxes for `spots` in pixels of the target whose band is `band`: each spot's box, clamped to the band
// (a spot outside the band has none), and boxes that overlap merged into the rectangle around both until no
// two overlap. Empty for no spots or an empty band.
std::vector<PixelRect> markerBoxes(const std::vector<MarkerSpot>& spots, const PixelRect& band);

// Each eye sees the markers at their targets' depth (T-186). The game draws a marker where the line from the
// head's centre to its target crosses the HUD quad (D ahead). An eye's image shows the world as seen from
// where that eye was rendered (r to the right of the centre, left eye negative) and is shown from where the
// eye is (s; the same unless ETERNALVR_IPD renders a separation of its own). For a target d ahead to sit on
// its marker in that eye, the marker moves `s - r * D / d` to the right on the quad: with s = r, by up to the
// eye's offset for a far marker, not at all at the quad's distance, the other way for a nearer one. Without
// it every marker sat at the HUD's distance and looked doubled while the eyes rested on its target.
struct MarkerParallax {
    float quadMetres = 0.0f;     // the HUD quad's distance ahead of the head
    float pixelsPerMetre = 0.0f; // across the quad, in pixels of the GUI target
    float unitsPerMetre = 1.0f;  // world units per metre of the eyes' distance (ETERNALVR_WORLD_SCALE)
};

// An eye's offset to the right of the head's centre, in metres: where its image is shown from, and where it
// was rendered from.
struct MarkerEye {
    float shownMetres = 0.0f;
    float renderedMetres = 0.0f;
};

// Targets nearer than this are taken at this distance (a box moved too far no longer covers its own marker).
inline constexpr float kMarkerNearestMetres = 0.3f;

// How many pixels to the right a marker whose target is `ahead` world units ahead moves in `eye`'s image. 0
// for an unknown distance (0 or less) or an empty parallax.
float markerShiftPixels(const MarkerParallax& parallax, const MarkerEye& eye, float ahead);

// One box of the GUI target to copy into an eye's marker image: `source`, put `dx` pixels to the right.
struct MarkerCopy {
    PixelRect source;
    std::int32_t dx = 0;
};

// The copies for `eye`: each spot's box (as markerBoxes
// makes it, not merged: two markers that overlap can be at different distances) moved by its own shift,
// trimmed so it lands inside the band. A spot outside the band, or one whose box would land wholly outside
// it, has none.
std::vector<MarkerCopy> markerEyeCopies(const std::vector<MarkerSpot>& spots,
                                        const PixelRect& band,
                                        const MarkerParallax& parallax,
                                        const MarkerEye& eye);

} // namespace evr::ui_layer
