#pragma once

// The game's crosshair on the gun's ray under hand aim (ETERNALVR_UI_CROSSHAIR=gun, opt-in, with the aim dot
// on; crosshair.hpp; docs/VR_CONTROLLERS.md, "The game's crosshair under hand aim").
//
// idHUD_Reticle (swf/hud/hud_reticle.swf, the HUD element decl's PERSPECTIVE_CENTER_PLANE) draws the held
// weapon's reticle and every cue it carries in the middle of the GUI target, for the game's view: the
// Lock-On Burst's lock pieces, LOCKED ON and cooldown, the charge, heat, burst and cooldown bars, the Sticky
// Bombs' and Micro Missiles' pips, the BFG's and the Microwave Beam's bars, the chainsaw's fuel, the hit
// marker and the hostile-target colour [static: the weaponreticle decls, the SWF's sprite names]. Under hand
// aim the game's view is the gun's, so all of it is right except where it is shown: the middle of the
// head-locked quad marks the head's ray. So with each GUI copy the XR worker takes this rectangle of it off
// the head-locked quad and shows it on a quad of its own, centred on the weapon ray at the aim dot's
// distance, at the angle it spans on the HUD (vkcore/presenter_gun_crosshair.hpp).
//
// The HUD's world-attached markers (objective markers, the rocket lock-on's triangle on its target, the
// Meat Hook's mark; ui_layer/marker_boxes.hpp) are drawn for the head's view and stay where they are: their
// boxes are cleared out of the moved rectangle and kept on the head-locked quad. Everything else the game
// draws in the rectangle moves with the crosshair, as it sits around the crosshair in the flat game: the
// low ammo and chainsaw texts (hud_lowwarning, the same centre plane), a pickup's name (idHUD_FocusInfo, at
// the centre anchor), the ability rings in the layouts around the crosshair.

#include "ui_layer/backdrop.hpp"
#include "ui_layer/ui_settings.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace evr::ui_layer {

// The rectangle's size, in fractions of the band's height (the SWF's frame is laid out over the band, so the
// reticle scales with it) [rig GUI captures of every slot and mod, docs/VR_CONTROLLERS.md]. Its width holds
// the widest reticle, the Ballista's: the Destroyer Blade's charge bars 0.52 to each side of the middle, the
// Arbalest's ammo count out to 0.62 right of it while zoomed. Its height stops 0.175 above and below the
// middle: the low ammo banner's glow starts 0.18 below it (the banner stays on the HUD), so the only thing
// cut is the faint outer ring of the Arbalest's zoom (0.21 from the middle), whose top and bottom arcs
// stay on the HUD. The other reticles stay within 0.10 of the middle, the chainsaw's texts within 0.15.
inline constexpr float kGunCrosshairWidth = 1.28f;
inline constexpr float kGunCrosshairHeight = 0.35f;

// The rectangle in a `width` x `height` GUI target: kGunCrosshairWidth x kGunCrosshairHeight of its band's
// height (wideContentRect), centred in the band, rounded to whole pixels, at most the band. Empty (0 x 0) for
// an empty target.
PixelRect gunCrosshairRect(std::uint32_t width, std::uint32_t height);

// The same-copy border test (vkcore/gun_border_probe.hpp): the backdrop test (backdrop.hpp) decides from
// earlier copies, so the first copies of a pause menu, a death screen or a loading fade would still be moved,
// a slab of the backdrop cleared from the HUD and shown at the gun. So each copy that is to be moved also
// reads these points of its own GUI image, the corners and edge midpoints of `rect` `margin` pixels outside
// it (kept inside the `width` x `height` target), and is kept whole when they show a full-screen layer
// (readingShowsBackdrop: most of them visible). Over the game they hold nothing or the HUD's thin elements
// (the low ammo banner's glow reaches one of them). Nullopt for an empty rectangle or target.
inline constexpr std::uint32_t kGunBorderMarginPixels = 4;
std::optional<std::array<PixelPoint, kBackdropProbePoints>> gunCrosshairBorderPoints(const PixelRect& rect,
                                                                                     std::uint32_t width,
                                                                                     std::uint32_t height,
                                                                                     std::uint32_t margin);

// What one GUI copy clears for the move, given the markers' boxes (markerBoxes): in the UI image, the
// rectangle less the boxes (the markers stay on the head-locked quad); in the moved image (the rectangle's
// own pixels, from its top-left corner), the parts of the boxes inside the rectangle (no marker shows twice).
struct GunCrosshairCuts {
    std::vector<PixelRect> hud;
    std::vector<PixelRect> moved;
};
GunCrosshairCuts gunCrosshairCuts(const PixelRect& rect, const std::vector<PixelRect>& markerBoxes);

// The moved quad's size in metres at `atMetres` along the ray, so that `rect` spans the angle it spans on the
// HUD quad: `panelWidthMetres` wide at `panelDistanceMetres`, showing `shownWidth` pixels across (the UI quad
// as filled: its size, its distance and its image rect's width). 0 x 0 for an empty, zero, negative or
// non-finite input.
struct GunCrosshairSize {
    float width = 0.0f;
    float height = 0.0f;
};
GunCrosshairSize gunCrosshairSize(const PixelRect& rect,
                                  std::uint32_t shownWidth,
                                  float panelWidthMetres,
                                  float panelDistanceMetres,
                                  float atMetres);

} // namespace evr::ui_layer
