#pragma once

// What a zoomed weapon looks like in VR (the owner, 2026-10-13: "I think I'd prefer for the weapon to just
// stay"). A weapon's zoom (its `ironSightZoom` in the weapon decl) can hide the hands and the gun at once
// (`hideHandsOnZoom`) or once zoomed for `hideHandsOnZoomDelay`, draw a scope GUI (`scopeGuiNum`) and switch
// to a zoomed reticle (`reticleWhenZoomed`), which for the Heavy Cannon's Precision Bolt is a full-screen
// scope (`zoomStyle` RETICLE_ZOOM_STYLE_SCOPE). In a headset the world is not magnified (the view keeps the
// headset's FOV), so the scope is a ring on a head-locked panel with the gun gone from the hand, and with the
// crosshair hold (hand aim) the game draws no screen GUI at all while scoped, the HUD included.
//
// The layer changes only what is drawn, in the held weapon's decls (its own, its secondary fire's, both fire
// modes' mod decls and supercharge decls) that hide the gun at the zoom's start or draw a scope
// (zoomLookNeeded; the record of the writes is zoom_look_book.hpp): the hands stay, no scope GUI, and the
// weapon's own reticle while zoomed. The last keeps the HUD under the crosshair hold: with the zoomed scope
// reticle and the reticle style held at none the game draws no screen GUI at all (rig, aim-0151: the fully
// zoomed GUI capture 0 pixels without it, the HUD's 32828 with it). The scope's ring is not taken out by the
// decls (rig: with the weapon's own reticle, and with the zoomed reticle's zoomStyle and showZoom written to
// none, head aim still drew it), so while a weapon whose decl has a scope GUI is zoomed the presenter holds
// the reticle style at none under head aim too, as under hand aim (vkcore/zoom_look.cpp,
// ui_layer/crosshair.hpp). The zoom itself (the player's zoom state, the precision shot, the zoom's speed and
// sensitivity) is untouched. Each part is a rig and debug switch (ETERNALVR_ZOOM_LOOK, parseZoomLook);
// players get both.
//
// Field offsets: the engine's type info (tools/typeinfo, Steam build 25216728). The game hides the hands on
// zoom from the decl idWeapon picks per fire mode (supercharge decl, else the mod's, else for the secondary
// fire the base decl's secondaryFireDecl, else the base decl): `hideHandsOnZoom` at the zoom's start
// (RVA 0x16C6170) and, while zoomed, after `hideHandsOnZoomDelay.gameTime` when that is above 0 (RVA
// 0x16C61D0, read by 0x145F2F0), both as idHands hidden reason 2. The Game Pass build is the same game code
// relinked (vkcore/game_build.hpp), so its decls have the same layout; the layer checks every pointer it
// follows by the decl's name and every value by its range before it writes (vkcore/zoom_look.cpp).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace evr::game {

// idDeclWeapon (size 0x2328): ironSightZoom (idDeclWeapon::zoomInfo_t, 0x88) at 0x1700.
inline constexpr std::size_t kDeclWeaponZoomInfo = 0x1700;
inline constexpr std::size_t kDeclWeaponHideHandsOnZoom = kDeclWeaponZoomInfo + 0x50; // bool
// hideHandsOnZoomDelay (milliToGameTime_t): gameTime (int64) at +0x58, milliseconds (int32) at +0x60.
inline constexpr std::size_t kDeclWeaponHideHandsDelay = kDeclWeaponZoomInfo + 0x58;
inline constexpr std::size_t kDeclWeaponHideHandsDelayMs = kDeclWeaponZoomInfo + 0x60;
inline constexpr std::size_t kDeclWeaponScopeGuiNum = kDeclWeaponZoomInfo + 0x6C; // scopeGuiNum_t
inline constexpr std::size_t kDeclWeaponSecondaryFireDecl = 0x7F0;                // idDeclWeapon*
inline constexpr std::size_t kDeclWeaponReticle = 0x2158;                         // idDeclWeaponReticle*
inline constexpr std::size_t kDeclWeaponReticleWhenZoomed = 0x2160;               // idDeclWeaponReticle*
// idDeclWeapon::scopeGuiNum_t: SCOPE_NONE 0 ... SCOPE_SKIPCROSSHAIR 6 (the enum table in the exe).
inline constexpr std::int32_t kScopeGuiNone = 0;
inline constexpr std::int32_t kScopeGuiLast = 6;
// A hide delay above a minute is not a decl's (the Bolt's is 74 ms).
inline constexpr std::int32_t kHideDelayMsMax = 60000;

struct ZoomLook {
    bool keepHands = true;  // hideHandsOnZoom false and no hide delay
    bool noScopeGui = true; // scopeGuiNum SCOPE_NONE, reticleWhenZoomed = reticle, and the reticle hold
                            // while a scoped weapon is zoomed
};

// ETERNALVR_ZOOM_LOOK: "all" (the default), "off" / "game" / "none" (the game's own look), or a comma list of
// "hands" and "scope" (any case, spaces around each ignored), each of which the layer then changes. Nullopt
// for anything else.
std::optional<ZoomLook> parseZoomLook(std::string_view text);

// The fields of one weapon decl the zoom look changes.
struct ZoomDeclFields {
    std::uint8_t hideHandsOnZoom = 0;
    std::int64_t hideHandsDelay = 0;   // hideHandsOnZoomDelay.gameTime: the game hides the hands when above 0
    std::int32_t hideHandsDelayMs = 0; // hideHandsOnZoomDelay.milliseconds (the decl's value)
    std::int32_t scopeGuiNum = kScopeGuiNone;
    std::uintptr_t reticle = 0; // read only: a layout anchor, a weapon decl with a zoom has one
    std::uintptr_t reticleWhenZoomed = 0;

    bool operator==(const ZoomDeclFields&) const = default;
};

// Plausible as a weapon decl's: a reticle (the caller checks its name: a layout anchor), a bool 0 or 1, the
// hide delay 0 to a minute and 0 with a gameTime of 0, the scope GUI one of the enum's values. A decl that
// reads otherwise is not written (another build's layout, or not a weapon decl).
bool plausibleZoomFields(const ZoomDeclFields& fields);

// The decls the look changes: those whose own values hide the hands at the zoom's start or draw a scope GUI
// (the Precision Bolt draws a scope). A hide delay alone does not count: the Heavy Cannon's own decl and
// heavy_cannon_burst_detonate carry 150 ms and kept the gun on the rig with the look off (aim-0151), so
// they are left as the game has them.
bool zoomLookNeeded(const ZoomDeclFields& game);

// The fields as the look wants them, for a decl that needs it (else the game's): the hands neither hidden on
// zoom nor after a delay, and where the decl draws a scope, no scope GUI and the weapon's own reticle while
// zoomed (only where it has both: a decl without a zoomed reticle already uses its own).
ZoomDeclFields zoomLookFields(const ZoomDeclFields& game, const ZoomLook& look);

// The presenter holds the reticle style at none for the scope (under any aim): the player is zoomed with a
// weapon whose decl draws a scope GUI in the game's own values, and the look's "scope" part is on.
bool scopeReticleHold(bool zoomed, bool scopedWeapon, const ZoomLook& look);

} // namespace evr::game
