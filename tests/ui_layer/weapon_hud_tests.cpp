#include "ui_layer/weapon_hud.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using evr::Pose;
using evr::Quat;
using evr::Vec3;
using evr::ui_layer::headLockedPieces;
using evr::ui_layer::HudMode;
using evr::ui_layer::layoutWeaponQuads;
using evr::ui_layer::PixelRect;
using evr::ui_layer::readUiSettings;
using evr::ui_layer::rotationFromBasis;
using evr::ui_layer::weaponFacingDegrees;
using evr::ui_layer::weaponFrame;
using evr::ui_layer::WeaponHudSettings;
using evr::ui_layer::weaponMovedBlocks;
using evr::ui_layer::weaponPanelOffset;
using evr::ui_layer::weaponPanelRotation;
using evr::ui_layer::wideContentRect;
using evr::ui_layer::WristBlock;
using evr::ui_layer::WristFacing;
using evr::ui_layer::WristFade;
using evr::ui_layer::WristSettings;

namespace {

constexpr float kPi = 3.14159265358979f;

// A gun pointing `yawDegrees` to the left of -Z (counter-clockwise seen from above), level, upright.
Pose gunPointing(float yawDegrees, Vec3 position) {
    const float y = yawDegrees * kPi / 180.0f;
    const Vec3 forward{-std::sin(y), 0.0f, -std::cos(y)}; // the barrel (-Z of the frame)
    const Vec3 up{0.0f, 1.0f, 0.0f};
    const Vec3 right = evr::cross(forward, up);
    return {rotationFromBasis(right, up, forward * -1.0f), position};
}

// A gun pitched `pitchDegrees` up from level, pointing along -Z.
Pose gunPitched(float pitchDegrees, Vec3 position) {
    const float p = pitchDegrees * kPi / 180.0f;
    const Vec3 forward{0.0f, std::sin(p), -std::cos(p)};
    const Vec3 right{1.0f, 0.0f, 0.0f};
    const Vec3 up = evr::cross(forward * -1.0f, right);
    return {rotationFromBasis(right, up, forward * -1.0f), position};
}

bool near(Vec3 a, Vec3 b, float eps = 1e-4f) {
    return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
}

evr::ui_layer::EnvLookup envOf(std::map<std::wstring, std::wstring> values) {
    return [values](std::wstring_view name) -> std::optional<std::wstring> {
        const auto it = values.find(std::wstring(name));
        if (it == values.end()) {
            return std::nullopt;
        }
        return it->second;
    };
}

// Where the right hand holds a gun at the chest, from a head at the origin.
constexpr Vec3 kRightChest{0.2f, -0.35f, -0.35f};

} // namespace

TEST_CASE("weapon hud: the test guns point where they say") {
    CHECK(near(evr::rotate(gunPointing(0.0f, {}).orientation, {0.0f, 0.0f, -1.0f}), {0.0f, 0.0f, -1.0f}));
    CHECK(near(evr::rotate(gunPointing(90.0f, {}).orientation, {0.0f, 0.0f, -1.0f}), {-1.0f, 0.0f, 0.0f}));
    CHECK(near(evr::rotate(gunPointing(90.0f, {}).orientation, {0.0f, 1.0f, 0.0f}), {0.0f, 1.0f, 0.0f}));
    const Pose up = gunPitched(30.0f, {});
    CHECK(near(evr::rotate(up.orientation, {0.0f, 0.0f, -1.0f}), {0.0f, 0.5f, -0.8660254f}));
    CHECK(near(evr::rotate(up.orientation, {1.0f, 0.0f, 0.0f}), {1.0f, 0.0f, 0.0f}));
}

TEST_CASE("weapon hud: the panel's axes in the gun's frame") {
    // Straight back along the barrel: the image upright, facing a player behind the gun.
    const Quat back = weaponPanelRotation(0.0f);
    CHECK(near(evr::rotate(back, {0.0f, 0.0f, 1.0f}), {0.0f, 0.0f, 1.0f}));
    CHECK(near(evr::rotate(back, {0.0f, 1.0f, 0.0f}), {0.0f, 1.0f, 0.0f}));
    CHECK(near(evr::rotate(back, {1.0f, 0.0f, 0.0f}), {1.0f, 0.0f, 0.0f}));
    // Lying on top of the gun, facing up: the image's top toward the muzzle.
    const Quat flat = weaponPanelRotation(90.0f);
    CHECK(near(evr::rotate(flat, {0.0f, 0.0f, 1.0f}), {0.0f, 1.0f, 0.0f}));
    CHECK(near(evr::rotate(flat, {0.0f, 1.0f, 0.0f}), {0.0f, 0.0f, -1.0f}));
    CHECK(near(evr::rotate(flat, {1.0f, 0.0f, 0.0f}), {1.0f, 0.0f, 0.0f}));
    // The default leans back halfway.
    const float h = std::sqrt(0.5f);
    const Quat tilted = weaponPanelRotation(WeaponHudSettings{}.tiltDegrees);
    CHECK(near(evr::rotate(tilted, {0.0f, 0.0f, 1.0f}), {0.0f, h, h}));
    CHECK(near(evr::rotate(tilted, {0.0f, 1.0f, 0.0f}), {0.0f, h, -h}));
}

TEST_CASE("weapon hud: the offset is mirrored for the left hand") {
    WeaponHudSettings s;
    s.offset = {0.02f, 0.07f, 0.05f};
    const Vec3 r = weaponPanelOffset(s, false);
    const Vec3 l = weaponPanelOffset(s, true);
    CHECK(near(r, s.offset));
    CHECK(near(l, {-0.02f, 0.07f, 0.05f}));
    // The default: above the gun and behind the grip, on its centre line.
    const Vec3 d = WeaponHudSettings{}.offset;
    CHECK(d.x == doctest::Approx(0.0f));
    CHECK(d.y > 0.0f);
    CHECK(d.z > 0.0f);
}

TEST_CASE("weapon hud: the gun's frame is the grip's position with the aim's orientation") {
    const Pose aim = gunPitched(10.0f, {0.2f, -0.3f, -0.45f});
    const Pose grip{rotationFromBasis({1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}),
                    {0.2f, -0.33f, -0.4f}};
    const Pose gun = weaponFrame(aim, grip, true);
    CHECK(near(gun.position, grip.position));
    CHECK(near(evr::rotate(gun.orientation, {0.0f, 0.0f, -1.0f}),
               evr::rotate(aim.orientation, {0.0f, 0.0f, -1.0f})));
    CHECK(near(weaponFrame(aim, grip, false).position, aim.position));
    // Placed relative to the aim (the presenter's quads live in the aim space), the layout lands where it
    // does in room space.
    const WeaponHudSettings s;
    const Pose inAim = evr::compose(evr::inverse(aim), gun);
    const auto room = layoutWeaponQuads(gun, 2064, 2100, s, false);
    const auto local = layoutWeaponQuads(inAim, 2064, 2100, s, false);
    REQUIRE(room.size() == 1);
    REQUIRE(local.size() == 1);
    const Pose back = evr::compose(aim, local[0].pose);
    CHECK(near(back.position, room[0].pose.position, 1e-4f));
    CHECK(near(evr::rotate(back.orientation, {0.0f, 0.0f, 1.0f}),
               evr::rotate(room[0].pose.orientation, {0.0f, 0.0f, 1.0f}), 1e-4f));
}

TEST_CASE("weapon hud: a gun held at the chest faces the head, one turned away does not") {
    const WeaponHudSettings s;
    const Pose head{};
    const float ahead = weaponFacingDegrees(gunPointing(0.0f, kRightChest), head, s, false);
    CHECK(ahead < 35.0f);
    CHECK(ahead > 15.0f);
    // Pointing up or down 45 degrees, or out to the right: still readable.
    CHECK(weaponFacingDegrees(gunPitched(45.0f, kRightChest), head, s, false) < s.showDegrees);
    CHECK(weaponFacingDegrees(gunPitched(-45.0f, kRightChest), head, s, false) < s.showDegrees);
    CHECK(weaponFacingDegrees(gunPointing(-90.0f, kRightChest), head, s, false) < s.showDegrees);
    // Across the body to the left: the panel turns its face to the right, away from the eyes.
    CHECK(weaponFacingDegrees(gunPointing(90.0f, kRightChest), head, s, false) > s.hideDegrees);
    // Pointed back at the player: its face turns forward and up, away from the eyes.
    CHECK(weaponFacingDegrees(gunPointing(180.0f, kRightChest), head, s, false) > s.hideDegrees);
    // The left hand at the mirrored place, pointing ahead: the same angle.
    const Vec3 leftChest{-kRightChest.x, kRightChest.y, kRightChest.z};
    CHECK(weaponFacingDegrees(gunPointing(0.0f, leftChest), head, s, true) ==
          doctest::Approx(ahead).epsilon(0.001));
    // The head at the panel's centre: no direction, counted as facing away.
    const Pose gun = gunPointing(0.0f, kRightChest);
    const Pose onPanel{Quat::identity(), evr::transformPoint(gun, weaponPanelOffset(s, false))};
    CHECK(weaponFacingDegrees(gun, onPanel, s, false) == doctest::Approx(180.0f));
}

TEST_CASE("weapon hud: shown with hysteresis on the facing alone, faded with the wrist's times") {
    WeaponHudSettings g;
    g.showDegrees = 60.0f;
    g.hideDegrees = 75.0f;
    WristSettings w;
    w.fadeInSeconds = 0.1f;
    w.fadeOutSeconds = 0.2f;
    w.always = true;
    const WristSettings s = evr::ui_layer::weaponShowSettings(g, w);
    CHECK_FALSE(s.always);
    WristFacing f;
    CHECK_FALSE(f.update(70.0f, 0.0f, s)); // between: stays hidden
    CHECK(f.update(59.0f, 0.0f, s));
    CHECK(f.update(74.0f, 0.0f, s)); // between: stays shown
    CHECK_FALSE(f.update(76.0f, 0.0f, s));
    CHECK_FALSE(f.update(std::nanf(""), 0.0f, s));
    // No gaze limit: a gun far out of view that faces the head still counts as shown.
    CHECK(f.update(10.0f, 170.0f, s));
    WristFade fade;
    CHECK(fade.update(true, 0.05f, s) == doctest::Approx(0.5f));
    CHECK(fade.update(false, 0.05f, s) == doctest::Approx(0.25f));
}

TEST_CASE("weapon hud: the ammo block alone, or with health and armor beside it") {
    WeaponHudSettings s;
    s.widthMetres = 0.12f;
    const auto quads = layoutWeaponQuads(Pose::identity(), 1280, 1400, s, false);
    REQUIRE(quads.size() == 1);
    const auto& ammo = quads[0];
    CHECK(ammo.block == WristBlock::Weapon);
    CHECK(ammo.width == doctest::Approx(0.12f));
    CHECK(ammo.width / ammo.height ==
          doctest::Approx(static_cast<float>(ammo.rect.width) / static_cast<float>(ammo.rect.height)));
    // At the offset, turned by the tilt.
    CHECK(near(ammo.pose.position, s.offset));
    CHECK(near(evr::rotate(ammo.pose.orientation, {0.0f, 0.0f, 1.0f}),
               evr::rotate(weaponPanelRotation(s.tiltDegrees), {0.0f, 0.0f, 1.0f})));
    // The crop lies in the band the UI quad shows.
    const PixelRect band = wideContentRect(1280, 1400);
    CHECK(ammo.rect.y >= band.y);
    CHECK(ammo.rect.y + static_cast<std::int64_t>(ammo.rect.height) <=
          band.y + static_cast<std::int64_t>(band.height));

    s.vitals = true;
    for (const bool left : {false, true}) {
        CAPTURE(left);
        const auto row = layoutWeaponQuads(Pose::identity(), 1280, 1400, s, left);
        REQUIRE(row.size() == 2);
        CHECK(row[0].block == WristBlock::Vitals);
        CHECK(row[1].block == WristBlock::Weapon);
        // The ammo keeps its size; health and armor at the same scale, on the image's left, touching.
        CHECK(row[1].width == doctest::Approx(0.12f));
        CHECK(row[0].height == doctest::Approx(row[1].height));
        CHECK(row[0].pose.position.x < row[1].pose.position.x);
        CHECK(row[1].pose.position.x - row[0].pose.position.x ==
              doctest::Approx((row[0].width + row[1].width) / 2.0f));
        // The row is centred on the (mirrored) offset.
        const Vec3 centre = (row[0].pose.position + row[1].pose.position) * 0.5f;
        const float shift = (row[0].width - row[1].width) / 4.0f;
        CHECK(near(centre, weaponPanelOffset(s, left) + Vec3{shift, 0.0f, 0.0f}, 1e-4f));
    }
    CHECK(layoutWeaponQuads(Pose::identity(), 0, 0, s, false).empty());
}

TEST_CASE("weapon hud: the head-locked pieces lose only what moved to the gun") {
    const std::uint32_t sizes[][2] = {{1280, 1400}, {2064, 2100}, {2560, 2100}, {1920, 1080}};
    for (const auto& size : sizes) {
        const std::uint32_t width = size[0];
        const std::uint32_t height = size[1];
        CAPTURE(width);
        const PixelRect band = wideContentRect(width, height);
        WeaponHudSettings s;
        const auto pieces = headLockedPieces(band, width, height, weaponMovedBlocks(s));
        // The band down to the ammo block, then the bottom row left of it (health stays head-locked).
        REQUIRE(pieces.size() == 2);
        CHECK(pieces[0].y == band.y);
        CHECK(pieces[0].width == band.width);
        const auto w = evr::ui_layer::cutRect(WristBlock::Weapon, width, height);
        CHECK(pieces[1].x == band.x);
        CHECK(pieces[1].width == static_cast<std::uint32_t>(w.x - band.x));
        std::uint64_t kept = 0;
        for (const auto& p : pieces) {
            kept += static_cast<std::uint64_t>(p.width) * p.height;
        }
        CHECK(kept == static_cast<std::uint64_t>(band.width) * band.height -
                          static_cast<std::uint64_t>(w.width) * w.height);
        // With health and armor on the gun too: both corners go, as on the wrist.
        s.vitals = true;
        const auto both = headLockedPieces(band, width, height, weaponMovedBlocks(s));
        const auto wrist = headLockedPieces(band, width, height, {WristBlock::Vitals, WristBlock::Weapon});
        REQUIRE(both.size() == wrist.size());
        for (std::size_t i = 0; i < both.size(); ++i) {
            CHECK(both[i].x == wrist[i].x);
            CHECK(both[i].y == wrist[i].y);
            CHECK(both[i].width == wrist[i].width);
            CHECK(both[i].height == wrist[i].height);
        }
    }
}

TEST_CASE("weapon hud settings: defaults and every variable") {
    std::vector<std::string> warnings;
    const auto d = readUiSettings(envOf({}), warnings);
    CHECK(d.hud == HudMode::Panel); // the weapon HUD is opt-in
    CHECK(d.weapon.showDegrees == doctest::Approx(60.0f));
    CHECK(d.weapon.hideDegrees == doctest::Approx(75.0f));
    CHECK(d.weapon.widthMetres == doctest::Approx(0.10f));
    CHECK(d.weapon.tiltDegrees == doctest::Approx(45.0f));
    CHECK_FALSE(d.weapon.vitals);
    CHECK(warnings.empty());

    const auto s = readUiSettings(envOf({{L"ETERNALVR_HUD", L" weapon "},
                                         {L"ETERNALVR_WEAPON_HUD_ANGLE", L"50"},
                                         {L"ETERNALVR_WEAPON_HUD_WIDTH", L"0.08"},
                                         {L"ETERNALVR_WEAPON_HUD_OFFSET", L"-0.03, 0.1,0.02"},
                                         {L"ETERNALVR_WEAPON_HUD_TILT", L"-10"},
                                         {L"ETERNALVR_WEAPON_HUD_VITALS", L"1"}}),
                                  warnings);
    CHECK(s.hud == HudMode::Weapon);
    CHECK(std::string(evr::ui_layer::hudModeName(s.hud)) == "weapon");
    CHECK(s.weapon.showDegrees == doctest::Approx(50.0f));
    CHECK(s.weapon.hideDegrees == doctest::Approx(65.0f));
    CHECK(s.weapon.widthMetres == doctest::Approx(0.08f));
    CHECK(s.weapon.offset.x == doctest::Approx(-0.03f));
    CHECK(s.weapon.offset.y == doctest::Approx(0.1f));
    CHECK(s.weapon.offset.z == doctest::Approx(0.02f));
    CHECK(s.weapon.tiltDegrees == doctest::Approx(-10.0f));
    CHECK(s.weapon.vitals);
    CHECK(warnings.empty());
}

TEST_CASE("weapon hud settings: bad values keep the defaults and warn") {
    std::vector<std::string> warnings;
    const auto s = readUiSettings(envOf({{L"ETERNALVR_WEAPON_HUD_ANGLE", L"0"},
                                         {L"ETERNALVR_WEAPON_HUD_WIDTH", L"2"},
                                         {L"ETERNALVR_WEAPON_HUD_OFFSET", L"0,0"},
                                         {L"ETERNALVR_WEAPON_HUD_TILT", L"120"},
                                         {L"ETERNALVR_WEAPON_HUD_VITALS", L"yes"}}),
                                  warnings);
    const WeaponHudSettings d;
    CHECK(s.weapon.showDegrees == doctest::Approx(d.showDegrees));
    CHECK(s.weapon.widthMetres == doctest::Approx(d.widthMetres));
    CHECK(s.weapon.offset.y == doctest::Approx(d.offset.y));
    CHECK(s.weapon.tiltDegrees == doctest::Approx(d.tiltDegrees));
    CHECK_FALSE(s.weapon.vitals);
    CHECK(warnings.size() == 5);
}
