#include "ui_layer/weapon_wheel.hpp"
#include "ui_layer/wrist_hud.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

using evr::ui_layer::BandRect;
using evr::ui_layer::CentredWheel;
using evr::ui_layer::centredWheel;
using evr::ui_layer::cutRect;
using evr::ui_layer::headLockedHud;
using evr::ui_layer::HeadLockedHud;
using evr::ui_layer::headLockedPieces;
using evr::ui_layer::panelPiece;
using evr::ui_layer::PixelRect;
using evr::ui_layer::toPixels;
using evr::ui_layer::UiSettings;
using evr::ui_layer::WheelCentring;
using evr::ui_layer::wideContentRect;
using evr::ui_layer::WristBlock;
using evr::ui_layer::wristBlockRect;

namespace {

constexpr double kDegreesPerRadian = 57.29577951308232;

std::int64_t right(const PixelRect& r) {
    return static_cast<std::int64_t>(r.x) + r.width;
}

std::int64_t bottom(const PixelRect& r) {
    return static_cast<std::int64_t>(r.y) + r.height;
}

bool contains(const PixelRect& outer, const PixelRect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y && right(inner) <= right(outer) &&
           bottom(inner) <= bottom(outer);
}

bool overlap(const PixelRect& a, const PixelRect& b) {
    return a.width != 0 && b.width != 0 && a.x < right(b) && b.x < right(a) && a.y < bottom(b) &&
           b.y < bottom(a);
}

std::uint64_t area(const PixelRect& r) {
    return static_cast<std::uint64_t>(r.width) * r.height;
}

// The wheel as the game drew it on GUI captures with the wheel up (alpha above 8, right of the ability
// icons): 2048x2208 (the default render size), 1760x1400 (a player's headset) and 2048x864 (wider than 16:9,
// a 2560x1080 render size: the game fits its 16:9 frame between empty columns), pixels [x0, x1) x [y0, y1).
// The right edge includes the objective marker beside the wheel.
struct Capture {
    std::uint32_t width;
    std::uint32_t height;
    PixelRect wheel;
};
const Capture kCaptures[] = {
    {2048, 2208, {1341, 791, 1921 - 1341, 1369 - 791}},
    {1760, 1400, {1152, 431, 1651 - 1152, 928 - 431}},
    {2048, 864, {1262, 198, 1697 - 1262, 631 - 198}},
};

// HUD placements: the default (2.0 m wide at 1.5 m) and the player's (2.5 m at 0.9 m).
struct Hud {
    float width;
    float distance;
};
const Hud kHuds[] = {{2.0f, 1.5f}, {2.5f, 0.9f}};

// Where the player sees the wheel, in degrees from straight ahead (horizontal, vertical), and its angular
// width: on its own quad when the HUD has one, otherwise where the head-locked quad shows it (the piece
// holding the wheel's centre, placed on the UI quad `hudWidth` wide at `distance`, centred ahead).
struct Seen {
    double yaw = 0.0;
    double pitch = 0.0;
    double width = 0.0;
};
std::optional<Seen> seenWheel(const HeadLockedHud& hud,
                              const PixelRect& shown,
                              const PixelRect& wheel,
                              float hudWidth,
                              float distance) {
    if (hud.wheel) {
        const CentredWheel& w = *hud.wheel;
        // Straight ahead (CentredWheel).
        return Seen{0.0, 0.0, 2.0 * std::atan(w.width / 2.0 / w.distance) * kDegreesPerRadian};
    }
    const std::int64_t cx = wheel.x + wheel.width / 2;
    const std::int64_t cy = wheel.y + wheel.height / 2;
    for (const PixelRect& p : hud.pieces) {
        if (cx >= p.x && cx < right(p) && cy >= p.y && cy < bottom(p)) {
            const auto onHud = panelPiece(wheel, shown, hudWidth);
            return Seen{std::atan2(onHud.centreX, distance) * kDegreesPerRadian,
                        std::atan2(onHud.centreY, distance) * kDegreesPerRadian,
                        2.0 * std::atan(onHud.width / 2.0 / distance) * kDegreesPerRadian};
        }
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("weapon wheel: the rectangle holds the wheel the game drew on the rig's captures") {
    for (const Capture& c : kCaptures) {
        CAPTURE(c.width);
        CAPTURE(c.height);
        const std::optional<CentredWheel> wheel = centredWheel(c.width, c.height, 1.5f);
        REQUIRE(wheel);
        CHECK(contains(wheel->rect, c.wheel));
        // Inside the band the HUD is laid out in, clear of the blocks the hand HUDs move and of the
        // crosshair's ability rings.
        CHECK(contains(wideContentRect(c.width, c.height), wheel->rect));
        for (const WristBlock b : {WristBlock::Vitals, WristBlock::Weapon, WristBlock::Abilities}) {
            CHECK_FALSE(overlap(wheel->rect, toPixels(wristBlockRect(b), c.width, c.height)));
        }
        for (const WristBlock b : {WristBlock::Vitals, WristBlock::Weapon}) {
            CHECK_FALSE(overlap(wheel->rect, cutRect(b, c.width, c.height)));
        }
        // A margin of at least 1% of the band's width on every side (a highlighted segment, a pixel of
        // rounding).
        const std::int64_t margin = wideContentRect(c.width, c.height).width / 100;
        CHECK(c.wheel.x - wheel->rect.x >= margin);
        CHECK(right(wheel->rect) - right(c.wheel) >= margin);
        CHECK(c.wheel.y - wheel->rect.y >= margin);
        CHECK(bottom(wheel->rect) - bottom(c.wheel) >= margin);
    }
}

TEST_CASE("weapon wheel: with the wheel up it is seen straight ahead at the default HUD's size") {
    // On the default HUD the band is 2.0 m wide at 1.5 m: the wheel's rectangle spans this angle there.
    const BandRect band = evr::ui_layer::wheelBandRect();
    const double defaultDegrees =
        2.0 * std::atan((band.x1 - band.x0) * UiSettings{}.widthMetres / 2.0 / UiSettings{}.distanceMetres) *
        kDegreesPerRadian;
    for (const Capture& c : kCaptures) {
        for (const Hud& h : kHuds) {
            CAPTURE(c.width);
            CAPTURE(h.width);
            CAPTURE(h.distance);
            const PixelRect shown = wideContentRect(c.width, c.height);
            for (const auto& moved :
                 {std::vector<WristBlock>{}, std::vector<WristBlock>{WristBlock::Vitals, WristBlock::Weapon},
                  std::vector<WristBlock>{WristBlock::Weapon}}) {
                const HeadLockedHud hud = headLockedHud(shown, c.width, c.height, moved, true, h.distance);
                const std::optional<Seen> seen = seenWheel(hud, shown, c.wheel, h.width, h.distance);
                REQUIRE(seen);
                CHECK(std::abs(seen->yaw) < 0.5);
                CHECK(std::abs(seen->pitch) < 0.5);
                CHECK(seen->width == doctest::Approx(defaultDegrees).epsilon(0.01));
            }
        }
    }
}

TEST_CASE("weapon wheel: on origin/main's HUD the wheel was seen far to the right") {
    // The case the player reported, as the head-locked quad shows it without the wheel up: about 22 degrees
    // right on the default HUD, about 40 on 2.5 m at 0.9 m (the wheel's centre as measured above).
    const Capture& c = kCaptures[1];
    const PixelRect shown = wideContentRect(c.width, c.height);
    const HeadLockedHud flat = headLockedHud(shown, c.width, c.height, {}, false, 0.9f);
    REQUIRE_FALSE(flat.wheel);
    const std::optional<Seen> wide = seenWheel(flat, shown, c.wheel, 2.5f, 0.9f);
    REQUIRE(wide);
    CHECK(wide->yaw == doctest::Approx(39.5).epsilon(0.02));
    const std::optional<Seen> def = seenWheel(flat, shown, c.wheel, 2.0f, 1.5f);
    REQUIRE(def);
    CHECK(def->yaw == doctest::Approx(21.6).epsilon(0.02));
}

TEST_CASE("weapon wheel: the head-locked HUD keeps everything else where it was, without the wheel") {
    for (const Capture& c : kCaptures) {
        CAPTURE(c.width);
        const PixelRect shown = wideContentRect(c.width, c.height);
        for (const auto& moved :
             {std::vector<WristBlock>{}, std::vector<WristBlock>{WristBlock::Vitals, WristBlock::Weapon},
              std::vector<WristBlock>{WristBlock::Weapon},
              std::vector<WristBlock>{WristBlock::Vitals, WristBlock::Weapon}}) {
            CAPTURE(moved.size());
            const HeadLockedHud up = headLockedHud(shown, c.width, c.height, moved, true, 1.5f);
            REQUIRE(up.wheel);
            // Not shown twice: no piece of the head-locked quad shows any of the wheel's rectangle, nor any
            // of the wheel the game drew (a slice of it left there shows beside the centred quad).
            for (const PixelRect& p : up.pieces) {
                CHECK_FALSE(overlap(p, up.wheel->rect));
                CHECK_FALSE(overlap(p, c.wheel));
                CHECK(contains(shown, p));
            }
            // Nothing else lost: the pieces and the wheel's rectangle cover the band minus the moved blocks.
            std::uint64_t before = 0;
            for (const PixelRect& p : headLockedPieces(shown, c.width, c.height, moved)) {
                before += area(p);
            }
            std::uint64_t kept = 0;
            for (const PixelRect& p : up.pieces) {
                kept += area(p);
            }
            CHECK(kept + area(up.wheel->rect) == before);
            // The presenter has room for 6 pieces besides the hand quads and the wheel (WristHud::kMaxQuads).
            CHECK(up.pieces.size() <= 6);
            // Wheel down: the pieces are exactly those the HUD had before.
            const HeadLockedHud down = headLockedHud(shown, c.width, c.height, moved, false, 1.5f);
            CHECK_FALSE(down.wheel);
            const std::vector<PixelRect> pieces = headLockedPieces(shown, c.width, c.height, moved);
            REQUIRE(down.pieces.size() == pieces.size());
            for (std::size_t i = 0; i < pieces.size(); ++i) {
                CHECK(down.pieces[i].x == pieces[i].x);
                CHECK(down.pieces[i].y == pieces[i].y);
                CHECK(down.pieces[i].width == pieces[i].width);
                CHECK(down.pieces[i].height == pieces[i].height);
            }
        }
    }
}

TEST_CASE("weapon wheel: no quad for an empty target or a distance that is not positive") {
    CHECK_FALSE(centredWheel(0, 0, 1.5f));
    CHECK_FALSE(centredWheel(2048, 2208, 0.0f));
    CHECK_FALSE(centredWheel(2048, 2208, -1.0f));
    CHECK_FALSE(centredWheel(2048, 2208, std::nanf("")));
    CHECK(headLockedHud({}, 0, 0, {}, true, 1.5f).pieces.empty());
}

TEST_CASE("weapon wheel: centred from the first image with the wheel up, held briefly after the last") {
    WheelCentring c;
    WheelCentring::Step s = c.update(false, 10.0);
    CHECK_FALSE(s.centred);
    CHECK_FALSE(s.started);
    s = c.update(true, 10.011);
    CHECK(s.centred);
    CHECK(s.started);
    s = c.update(true, 10.022);
    CHECK(s.centred);
    CHECK_FALSE(s.started);
    // The last image with the wheel at 10.022: still centred within the hold, then back.
    s = c.update(false, 10.022 + WheelCentring::kHoldSeconds / 2);
    CHECK(s.centred);
    CHECK_FALSE(s.ended);
    s = c.update(false, 10.022 + WheelCentring::kHoldSeconds + 0.001);
    CHECK_FALSE(s.centred);
    CHECK(s.ended);
    s = c.update(false, 11.0);
    CHECK_FALSE(s.centred);
    CHECK_FALSE(s.ended);
    // Up again: centred at once.
    s = c.update(true, 12.0);
    CHECK(s.centred);
    CHECK(s.started);
}
