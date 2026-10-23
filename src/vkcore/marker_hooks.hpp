#pragma once

// The HUD's world-to-screen projection for its world-attached markers (objective and POI markers, damage
// numbers; docs/rig-findings/hud-markers.md; Steam build 25216728, the Game Pass build has the same code).
//
// Each game frame the player's GUI build (RVA 0x6CF640) builds the HUD's matrix: the projection of the
// world's render view (its latched view at +0x289D0, through the builder at RVA 0x1CE1290, into [rbp + 0x20])
// times the view matrix of the player's view (the builder at RVA 0x1CE1350, into [rbp - 0x20]), multiplied
// into a global by the thunk at RVA 0xF29890 (call at 0x6CF769). The HUD's projector (RVA 0xF2ABC0) maps a
// world point through that global onto its SWF's screen; the POI update (RVA 0xEEE970) then stretches the
// position for the render's aspect and tests and clamps markers off its frame (`test dil, dil` at RVA
// 0xEEEC05).
//
// While the XR worker shows the HUD on the head-locked quad (noteMarkerQuad), two mid hooks put the markers
// where the quad shows their targets:
// - after the view build (RVA 0x6CF761) the projection in [rbp + 0x20] gets the quad's rows 0 and 1
//   (xr_math::markerProjection: the GUI target as the quad shows it, seen from the head), so the multiply
//   makes the HUD's matrix from it;
// - at the off-frame test, a marker the game took for on its frame but draws outside it after the aspect
//   stretch (above or below the 16:9 band on a near-square target, rows the quad does not show) is marked off
//   the frame, so the game's own off-frame path clamps it to the frame's edge as on a 16:9 screen. Only while
//   the quad shows the band alone (ETERNALVR_UI_CROP on).
// Without the quad (the flat game, menus on their panel, the cinema screen, a stale worker) both leave the
// game's values alone.
//
// ETERNALVR_TEST_MARKER_LOG=1 (a rig test, never set by the launcher) also logs the HUD's projection and view
// and, for each marker the HUD draws, its world point and where it is drawn (`hud-markers: test log:` lines,
// about once a second), for the marker measurement of hud-markers.md.
//
// Located by signature on both builds and checked: the two builders by their first bytes (the projection
// builder tests useExplicitProjectionMatrix at +0x90, the view builder forceIdentityViewMatrix at +0x130),
// the thunk by its shape, and the projector called before the off-frame test by its read of the thunk's
// global. Anything missing installs nothing and logs why, once.

#include "ui_layer/ui_settings.hpp"

#include <cstdint>

namespace evr::vkcore {

// Locates and installs once per process; later calls return the first result. Only while the multiplayer
// guard is armed.
bool installMarkerHooks();

// XR worker, every frame: whether the HUD is on the head-locked quad now (not the menu panel or the cinema
// screen), with the UI settings and the GUI target's size. Lasts 250 ms.
void noteMarkerQuad(bool headLocked,
                    const ui_layer::UiSettings& ui,
                    std::uint32_t width,
                    std::uint32_t height);

} // namespace evr::vkcore
