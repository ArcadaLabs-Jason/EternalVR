#include "game/eternal/zoom_look.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>

using evr::game::kScopeGuiNone;
using evr::game::parseZoomLook;
using evr::game::plausibleZoomFields;
using evr::game::ZoomDeclFields;
using evr::game::ZoomLook;
using evr::game::zoomLookFields;

namespace {

constexpr std::uintptr_t kReticle = 0x1000;       // weaponreticle/heavy_cannon_bolt_action (any decl)
constexpr std::uintptr_t kZoomedReticle = 0x2000; // ..._zoomed: zoomStyle RETICLE_ZOOM_STYLE_SCOPE
constexpr std::int32_t kScopeRifle = 2;           // SCOPE_RIFLE

// The Precision Bolt's decl as the rig read it (weapon/player/heavy_cannon_bolt_action: hideHandsOnZoom
// false, hideHandsOnZoomDelay 74 ms (gameTime 73997), scopeGuiNum SCOPE_RIFLE, a zoomed scope reticle).
ZoomDeclFields bolt() {
    ZoomDeclFields f;
    f.hideHandsOnZoom = 0;
    f.hideHandsDelay = 73997;
    f.hideHandsDelayMs = 74;
    f.scopeGuiNum = kScopeRifle;
    f.reticle = kReticle;
    f.reticleWhenZoomed = kZoomedReticle;
    return f;
}

// The gun stays: the game hides the hands at the zoom's start on hideHandsOnZoom, and once zoomed for the
// delay when its gameTime is above 0.
bool gunStays(const ZoomDeclFields& f) {
    return f.hideHandsOnZoom == 0 && f.hideHandsDelay <= 0;
}

// What the owner asked for while zoomed: the gun stays, no scope GUI, and not the scope reticle (with the
// reticle hold it stops every screen GUI draw, the HUD's too).
bool vrZoomLook(const ZoomDeclFields& f) {
    return gunStays(f) && f.scopeGuiNum == kScopeGuiNone && f.reticleWhenZoomed != kZoomedReticle;
}

} // namespace

TEST_CASE("zoom look: the decl offsets are the type info's") {
    CHECK(evr::game::kDeclWeaponHideHandsOnZoom == 0x1750);
    CHECK(evr::game::kDeclWeaponHideHandsDelay == 0x1758);
    CHECK(evr::game::kDeclWeaponHideHandsDelayMs == 0x1760);
    CHECK(evr::game::kDeclWeaponSecondaryFireDecl == 0x7F0);
    CHECK(evr::game::kDeclWeaponScopeGuiNum == 0x176C);
    CHECK(evr::game::kDeclWeaponReticle == 0x2158);
    CHECK(evr::game::kDeclWeaponReticleWhenZoomed == 0x2160);
}

TEST_CASE("zoom look: ETERNALVR_ZOOM_LOOK parses all, off and a list") {
    const std::optional<ZoomLook> all = parseZoomLook("all");
    REQUIRE(all);
    CHECK((all->keepHands && all->noScopeGui));
    for (const char* off : {"off", " GAME ", "none"}) {
        const std::optional<ZoomLook> o = parseZoomLook(off);
        REQUIRE(o);
        CHECK_FALSE((o->keepHands || o->noScopeGui));
    }
    const std::optional<ZoomLook> hands = parseZoomLook("hands");
    REQUIRE(hands);
    CHECK((hands->keepHands && !hands->noScopeGui));
    const std::optional<ZoomLook> two = parseZoomLook(" Scope , HANDS");
    REQUIRE(two);
    CHECK((two->keepHands && two->noScopeGui));
    CHECK_FALSE(parseZoomLook("hands,"));
    CHECK_FALSE(parseZoomLook("reticle"));
    CHECK_FALSE(parseZoomLook("magnify"));
    CHECK_FALSE(parseZoomLook(""));
}

TEST_CASE("zoom look: the Precision Bolt keeps the gun, loses the scope GUI and the scope reticle") {
    const ZoomDeclFields f = zoomLookFields(bolt(), ZoomLook{});
    CHECK(f.hideHandsOnZoom == 0);
    CHECK(f.hideHandsDelay == 0);
    CHECK(f.hideHandsDelayMs == 0);
    CHECK(f.scopeGuiNum == kScopeGuiNone);
    CHECK(f.reticle == kReticle);
    CHECK(f.reticleWhenZoomed == kReticle);
    CHECK(vrZoomLook(f));
}

TEST_CASE("zoom look: a decl that hides the hands at the zoom's start keeps them") {
    ZoomDeclFields f = bolt();
    f.hideHandsOnZoom = 1;
    f.hideHandsDelay = 0;
    f.hideHandsDelayMs = 0;
    CHECK_FALSE(gunStays(f));
    CHECK(gunStays(zoomLookFields(f, ZoomLook{})));
    CHECK(zoomLookFields(f, *parseZoomLook("off")) == f);
}

TEST_CASE("zoom look: positive control, the game's own Bolt decl (0.1.50) fails the same check") {
    CHECK_FALSE(vrZoomLook(bolt()));
    CHECK_FALSE(gunStays(bolt())); // the 74 ms delay hides the gun (rig, aim-0151)
    // The first try's fault: clearing only hideHandsOnZoom, the Bolt's gun still goes.
    ZoomDeclFields boolOnly = bolt();
    boolOnly.hideHandsOnZoom = 0;
    CHECK_FALSE(gunStays(boolOnly));
    CHECK_FALSE(vrZoomLook(zoomLookFields(bolt(), *parseZoomLook("off"))));
    // Each part alone leaves the other.
    CHECK_FALSE(vrZoomLook(zoomLookFields(bolt(), *parseZoomLook("hands"))));
    CHECK_FALSE(vrZoomLook(zoomLookFields(bolt(), *parseZoomLook("scope"))));
}

TEST_CASE("zoom look: each switch changes only its own field") {
    const ZoomDeclFields hands = zoomLookFields(bolt(), *parseZoomLook("hands"));
    CHECK(gunStays(hands));
    CHECK(hands.scopeGuiNum == kScopeRifle);
    const ZoomDeclFields scope = zoomLookFields(bolt(), *parseZoomLook("scope"));
    CHECK(scope.hideHandsDelayMs == 74);
    CHECK(scope.scopeGuiNum == kScopeGuiNone);
    CHECK(scope.hideHandsDelay == 73997);
    CHECK(scope.reticleWhenZoomed == kReticle);
    CHECK(hands.reticleWhenZoomed == kZoomedReticle);
}

TEST_CASE("zoom look: a weapon without a zoom look is left as it is") {
    ZoomDeclFields shotgun;
    shotgun.reticle = kReticle;
    CHECK(zoomLookFields(shotgun, ZoomLook{}) == shotgun);
    // No reticle of its own: its zoomed reticle stays (never none while zoomed).
    ZoomDeclFields bare = bolt();
    bare.reticle = 0;
    CHECK(zoomLookFields(bare, ZoomLook{}).reticleWhenZoomed == kZoomedReticle);
}

TEST_CASE("zoom look: only plausible weapon decl fields are written") {
    CHECK(plausibleZoomFields(bolt()));
    ZoomDeclFields odd = bolt();
    odd.hideHandsOnZoom = 7;
    CHECK_FALSE(plausibleZoomFields(odd));
    odd = bolt();
    odd.scopeGuiNum = 7;
    CHECK_FALSE(plausibleZoomFields(odd));
    odd.scopeGuiNum = -1;
    CHECK_FALSE(plausibleZoomFields(odd));
    // A hide at the zoom's start (another weapon's decl) is a weapon decl's too.
    odd = bolt();
    odd.hideHandsOnZoom = 1;
    CHECK(plausibleZoomFields(odd));
    // The delay: 0 to a minute, its gameTime 0 when the milliseconds are.
    odd = bolt();
    odd.hideHandsDelayMs = -1;
    CHECK_FALSE(plausibleZoomFields(odd));
    odd.hideHandsDelayMs = evr::game::kHideDelayMsMax + 1;
    CHECK_FALSE(plausibleZoomFields(odd));
    odd = bolt();
    odd.hideHandsDelayMs = 0;
    CHECK_FALSE(plausibleZoomFields(odd));
    odd.hideHandsDelay = 0;
    CHECK(plausibleZoomFields(odd));
    odd = bolt();
    odd.hideHandsDelay = -5;
    CHECK_FALSE(plausibleZoomFields(odd));
}

TEST_CASE("zoom look: the reticle hold while a scoped weapon is zoomed") {
    using evr::game::scopeReticleHold;
    CHECK(scopeReticleHold(true, true, ZoomLook{}));
    CHECK_FALSE(scopeReticleHold(false, true, ZoomLook{})); // not zoomed
    CHECK_FALSE(scopeReticleHold(true, false, ZoomLook{})); // a weapon without a scope keeps its reticle
    // Positive control: the game's own look (0.1.50 under head aim) and a look without "scope" keep the ring.
    CHECK_FALSE(scopeReticleHold(true, true, *parseZoomLook("off")));
    CHECK_FALSE(scopeReticleHold(true, true, *parseZoomLook("hands")));
    CHECK(scopeReticleHold(true, true, *parseZoomLook("scope")));
}

TEST_CASE("zoom look: only decls that hide the gun at the start or draw a scope are changed") {
    using evr::game::zoomLookNeeded;
    CHECK(zoomLookNeeded(bolt())); // its scope
    // The Heavy Cannon's own decl and heavy_cannon_burst_detonate: a 150 ms delay alone, left as the game has
    // it.
    ZoomDeclFields cannon;
    cannon.hideHandsDelay = 149997;
    cannon.hideHandsDelayMs = 150;
    cannon.reticle = kReticle;
    CHECK_FALSE(zoomLookNeeded(cannon));
    CHECK(zoomLookFields(cannon, ZoomLook{}) == cannon);
    // A hide at the start without a scope: the hands kept, its scope GUI and zoomed reticle untouched.
    ZoomDeclFields hider = cannon;
    hider.hideHandsOnZoom = 1;
    hider.reticleWhenZoomed = kZoomedReticle;
    const ZoomDeclFields kept = zoomLookFields(hider, ZoomLook{});
    CHECK(gunStays(kept));
    CHECK(kept.scopeGuiNum == kScopeGuiNone);
    CHECK(kept.reticleWhenZoomed == kZoomedReticle);
}

TEST_CASE("zoom look: a decl without a reticle is not a layout to write") {
    ZoomDeclFields f = bolt();
    f.reticle = 0;
    CHECK_FALSE(plausibleZoomFields(f));
}
