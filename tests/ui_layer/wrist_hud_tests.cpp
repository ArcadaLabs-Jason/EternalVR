#include "ui_layer/wrist_hud.hpp"

#include <doctest/doctest.h>

#include <cmath>
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
using evr::ui_layer::layoutWristQuads;
using evr::ui_layer::PixelRect;
using evr::ui_layer::readUiSettings;
using evr::ui_layer::rotationFromBasis;
using evr::ui_layer::wideContentRect;
using evr::ui_layer::WristBlock;
using evr::ui_layer::WristFacing;
using evr::ui_layer::wristFacingDegrees;
using evr::ui_layer::WristFade;
using evr::ui_layer::wristPanelRotation;
using evr::ui_layer::WristSettings;

namespace {

// The left hand's grip held palm up in front of the chest, forearm across the body: the palm (+X) up, the
// thumb side (+Y) away from the body (-Z), the fingers (-Z) to the right (+X).
Pose leftPalmUp(Vec3 position) {
    return {rotationFromBasis({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}), position};
}

// The same hand palm down (turned 180 degrees about its own forearm).
Pose leftPalmDown(Vec3 position) {
    return {rotationFromBasis({0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}), position};
}

// The right hand's grip palm up, forearm across the body the other way: the palm (-X) up, the thumb side
// (+Y) away from the body, the fingers (-Z) to the left.
Pose rightPalmUp(Vec3 position) {
    return {rotationFromBasis({0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f}), position};
}

// A head at `eye` looking at `target` (-Z toward it), level (no roll).
Pose headLookingAt(Vec3 eye, Vec3 target) {
    const Vec3 forward = evr::normalize(target - eye);
    const Vec3 x = evr::normalize(evr::cross(forward, Vec3{0.0f, 1.0f, 0.0f}));
    const Vec3 z = forward * -1.0f;
    return {rotationFromBasis(x, evr::cross(z, x), z), eye};
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

} // namespace

TEST_CASE("wrist hud: rotations from a basis") {
    const Quat identity = rotationFromBasis({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
    CHECK(near(evr::rotate(identity, {0.3f, -0.2f, 0.9f}), {0.3f, -0.2f, 0.9f}));
    // Every branch of the conversion (trace positive, and each diagonal element largest).
    const Vec3 bases[][3] = {
        {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
        {{-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
        {{-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    };
    for (const auto& b : bases) {
        const Quat q = rotationFromBasis(b[0], b[1], b[2]);
        CHECK(near(evr::rotate(q, {1.0f, 0.0f, 0.0f}), b[0]));
        CHECK(near(evr::rotate(q, {0.0f, 1.0f, 0.0f}), b[1]));
        CHECK(near(evr::rotate(q, {0.0f, 0.0f, 1.0f}), b[2]));
    }
}

TEST_CASE("wrist hud: the test poses are palm up") {
    const Pose left = leftPalmUp({});
    CHECK(near(evr::rotate(left.orientation, {1.0f, 0.0f, 0.0f}), {0.0f, 1.0f, 0.0f}));  // palm up
    CHECK(near(evr::rotate(left.orientation, {0.0f, 0.0f, -1.0f}), {1.0f, 0.0f, 0.0f})); // fingers right
    const Pose right = rightPalmUp({});
    CHECK(near(evr::rotate(right.orientation, {-1.0f, 0.0f, 0.0f}), {0.0f, 1.0f, 0.0f}));  // palm up
    CHECK(near(evr::rotate(right.orientation, {0.0f, 0.0f, -1.0f}), {-1.0f, 0.0f, 0.0f})); // fingers left
}

TEST_CASE("wrist hud: the panel's axes in the grip frame, both hands") {
    for (const bool left : {true, false}) {
        CAPTURE(left);
        const Quat q = wristPanelRotation(left);
        // The normal: out of the palm (+X on the left grip, -X on the right one).
        CHECK(near(evr::rotate(q, {0.0f, 0.0f, 1.0f}), {left ? 1.0f : -1.0f, 0.0f, 0.0f}));
        CHECK(near(evr::rotate(q, {1.0f, 0.0f, 0.0f}), {0.0f, 0.0f, left ? -1.0f : 1.0f}));
        CHECK(near(evr::rotate(q, {0.0f, 1.0f, 0.0f}), {0.0f, 1.0f, 0.0f})); // up: the thumb side
    }
}

TEST_CASE("wrist hud: the offset is mirrored for the right hand") {
    const WristSettings s;
    const Vec3 l = evr::ui_layer::wristPanelOffset(s, true);
    const Vec3 r = evr::ui_layer::wristPanelOffset(s, false);
    CHECK(l.x == doctest::Approx(s.offset.x));
    CHECK(r.x == doctest::Approx(-s.offset.x));
    CHECK(l.y == doctest::Approx(r.y));
    CHECK(l.z == doctest::Approx(r.z));
    CHECK(l.z > 0.0f); // behind the grip, toward the elbow
}

TEST_CASE("wrist hud: palm up below the eyes faces the head, palm down does not") {
    const WristSettings s;
    const Pose head{};
    const float up = wristFacingDegrees(leftPalmUp({0.0f, -0.4f, -0.1f}), head, s, true);
    CHECK(up < 30.0f);
    CHECK(up > 20.0f); // the panel sits toward the elbow, off to the side of the head
    const float down = wristFacingDegrees(leftPalmDown({0.0f, -0.4f, -0.1f}), head, s, true);
    CHECK(down > 150.0f);
    // A hand low at the side, the controller held as usual (pointing forward, palm facing in): sideways.
    const float side = wristFacingDegrees(Pose{Quat::identity(), {-0.25f, -0.6f, -0.1f}}, head, s, true);
    CHECK(side > 60.0f);
    CHECK(side < 120.0f);
    // The right hand palm up is the mirror image of the left.
    const float rightUp = wristFacingDegrees(rightPalmUp({0.0f, -0.4f, -0.1f}), head, s, false);
    CHECK(rightUp == doctest::Approx(up).epsilon(0.001));
    // The head at the panel's centre: no direction, counted as facing away.
    const Pose hand = leftPalmUp({});
    const Pose onPanel{Quat::identity(), evr::transformPoint(hand, evr::ui_layer::wristPanelOffset(s, true))};
    CHECK(wristFacingDegrees(hand, onPanel, s, true) == doctest::Approx(180.0f));
    CHECK(evr::ui_layer::wristGazeDegrees(hand, onPanel, s, true) == doctest::Approx(180.0f));
}

TEST_CASE("wrist hud: the gaze: looking down at the wrist, or away from it") {
    const WristSettings s;
    const Pose hand = leftPalmUp({0.0f, -0.4f, -0.1f});
    const Vec3 panel = evr::transformPoint(hand, evr::ui_layer::wristPanelOffset(s, true));
    // Looking straight at the panel.
    const Pose looking = headLookingAt({}, panel);
    CHECK(evr::ui_layer::wristGazeDegrees(hand, looking, s, true) < 0.5f);
    // Looking ahead: the wrist is 75 degrees below the middle of the view.
    const float ahead = evr::ui_layer::wristGazeDegrees(hand, Pose{}, s, true);
    CHECK(ahead > 70.0f);
    CHECK(ahead < 80.0f);
    // A hand held out to the side at head height, palm in: it faces the head, but the player looks ahead.
    const Pose side{Quat::identity(), {-0.45f, -0.05f, -0.3f}};
    CHECK(wristFacingDegrees(side, Pose{}, s, true) < s.showDegrees);
    CHECK(evr::ui_layer::wristGazeDegrees(side, Pose{}, s, true) > s.gazeHideDegrees);
    WristFacing f;
    CHECK_FALSE(f.update(wristFacingDegrees(side, Pose{}, s, true),
                         evr::ui_layer::wristGazeDegrees(side, Pose{}, s, true), s));
    // The same hand looked at: shown.
    const Pose atSide =
        headLookingAt({}, evr::transformPoint(side, evr::ui_layer::wristPanelOffset(s, true)));
    CHECK(f.update(wristFacingDegrees(side, atSide, s, true),
                   evr::ui_layer::wristGazeDegrees(side, atSide, s, true), s));
}

TEST_CASE("wrist hud: facing and gaze with hysteresis, and always") {
    WristSettings s;
    s.showDegrees = 40.0f;
    s.hideDegrees = 55.0f;
    s.gazeShowDegrees = 30.0f;
    s.gazeHideDegrees = 45.0f;
    WristFacing f;
    CHECK_FALSE(f.update(50.0f, 0.0f, s)); // between: stays hidden
    CHECK(f.update(39.0f, 0.0f, s));
    CHECK(f.update(50.0f, 0.0f, s)); // between: stays shown
    CHECK(f.update(55.0f, 0.0f, s));
    CHECK_FALSE(f.update(55.5f, 0.0f, s));
    CHECK_FALSE(f.update(std::nanf(""), 0.0f, s));
    // The gaze gates the same way.
    CHECK_FALSE(f.update(10.0f, 35.0f, s)); // facing, but not looked at enough to show
    CHECK(f.update(10.0f, 29.0f, s));
    CHECK(f.update(10.0f, 44.0f, s)); // between: stays shown
    CHECK_FALSE(f.update(10.0f, 45.5f, s));
    CHECK_FALSE(f.update(10.0f, std::nanf(""), s));
    s.always = true;
    CHECK(f.update(170.0f, 170.0f, s));
    f.reset();
    CHECK_FALSE(f.shown());
}

TEST_CASE("wrist hud: fade in and out over their times") {
    WristSettings s;
    s.fadeInSeconds = 0.1f;
    s.fadeOutSeconds = 0.2f;
    WristFade fade;
    CHECK(fade.update(true, 0.05f, s) == doctest::Approx(0.5f));
    CHECK(fade.update(true, 0.05f, s) == doctest::Approx(1.0f));
    CHECK(fade.update(true, 1.0f, s) == doctest::Approx(1.0f));
    CHECK(fade.update(false, 0.05f, s) == doctest::Approx(0.75f));
    CHECK(fade.update(false, -1.0f, s) == doctest::Approx(0.75f)); // time never runs back
    CHECK(fade.update(false, 1.0f, s) == doctest::Approx(0.0f));
    s.fadeInSeconds = 0.0f;
    CHECK(fade.update(true, 0.0f, s) == doctest::Approx(1.0f));
}

TEST_CASE("wrist hud: layout of the quads on the left wrist") {
    WristSettings s;
    s.widthMetres = 0.2f;
    const Pose hand = leftPalmUp({0.0f, -0.4f, -0.1f});
    const auto quads = layoutWristQuads(hand, 1280, 1400, s, true);
    REQUIRE(quads.size() == 3);
    CHECK(quads[0].block == WristBlock::Vitals);
    CHECK(quads[1].block == WristBlock::Weapon);
    CHECK(quads[2].block == WristBlock::Abilities);
    // The row is the configured width, at one scale (the pixel aspect kept).
    CHECK(quads[0].width + quads[1].width == doctest::Approx(0.2f));
    for (const auto& q : quads) {
        CHECK(q.width / q.height ==
              doctest::Approx(static_cast<float>(q.rect.width) / static_cast<float>(q.rect.height)));
        // Every quad faces up (palm up) and reads upright from above: image up away from the body.
        CHECK(near(evr::rotate(q.pose.orientation, {0.0f, 0.0f, 1.0f}), {0.0f, 1.0f, 0.0f}));
        CHECK(near(evr::rotate(q.pose.orientation, {0.0f, 1.0f, 0.0f}), {0.0f, 0.0f, -1.0f}));
    }
    // Vitals on the image's left (toward the elbow on the left hand), weapon on its right; they touch.
    CHECK(quads[0].pose.position.x < quads[1].pose.position.x);
    CHECK(quads[1].pose.position.x - quads[0].pose.position.x ==
          doctest::Approx((quads[0].width + quads[1].width) / 2.0f));
    // The abilities above the row: further from the body.
    CHECK(quads[2].pose.position.z < quads[0].pose.position.z);
    s.abilities = false;
    CHECK(layoutWristQuads(hand, 1280, 1400, s, true).size() == 2);
    CHECK(layoutWristQuads(hand, 0, 0, s, true).empty());
}

TEST_CASE("wrist hud: laid out around an identity hand, the quads are relative to the grip") {
    const WristSettings s;
    for (const bool left : {true, false}) {
        CAPTURE(left);
        const auto quads = layoutWristQuads(Pose::identity(), 1280, 1400, s, left);
        REQUIRE(quads.size() == 3);
        // The row's centre is the offset: the two blocks' centres straddle it along the image's right.
        const Vec3 centre = (quads[0].pose.position + quads[1].pose.position) * 0.5f;
        const Vec3 along = evr::rotate(wristPanelRotation(left), {1.0f, 0.0f, 0.0f});
        const Vec3 offset = evr::ui_layer::wristPanelOffset(s, left);
        const float shift = (quads[0].width - quads[1].width) / 4.0f; // unequal widths
        CHECK(near(centre, offset + along * shift, 1e-3f));
        // Every crop lies in the band the UI quad shows.
        const PixelRect band = wideContentRect(1280, 1400);
        for (const auto& q : quads) {
            CHECK(q.rect.y >= band.y);
            CHECK(q.rect.y + static_cast<std::int64_t>(q.rect.height) <=
                  band.y + static_cast<std::int64_t>(band.height));
        }
    }
}

TEST_CASE("wrist hud: the head-locked pieces keep the band but the corners") {
    const std::uint32_t sizes[][2] = {{1280, 1400}, {2064, 2100}, {2560, 2100}, {1920, 1080}};
    for (const auto& size : sizes) {
        const std::uint32_t width = size[0];
        const std::uint32_t height = size[1];
        CAPTURE(width);
        const PixelRect band = wideContentRect(width, height);
        const auto pieces = headLockedPieces(band, width, height);
        REQUIRE(pieces.size() == 2);
        const auto v = evr::ui_layer::cutRect(WristBlock::Vitals, width, height);
        const auto w = evr::ui_layer::cutRect(WristBlock::Weapon, width, height);
        std::uint64_t kept = 0;
        for (const auto& p : pieces) {
            kept += static_cast<std::uint64_t>(p.width) * p.height;
            CHECK(p.y >= band.y);
            CHECK(p.y + static_cast<std::int64_t>(p.height) <=
                  band.y + static_cast<std::int64_t>(band.height));
        }
        CHECK(kept == static_cast<std::uint64_t>(band.width) * band.height -
                          static_cast<std::uint64_t>(v.width) * v.height -
                          static_cast<std::uint64_t>(w.width) * w.height);
        // The top piece is the band down to the corner blocks.
        CHECK(pieces[0].y == band.y);
        CHECK(pieces[0].width == band.width);
    }
    // With ETERNALVR_UI_CROP=0 the quad shows the whole target: the rows outside the band stay on it.
    const auto whole = headLockedPieces(PixelRect{0, 0, 1280, 1400}, 1280, 1400);
    REQUIRE(whole.size() == 3);
    CHECK(whole[0].y == 0);
    CHECK(whole[2].y + static_cast<std::int64_t>(whole[2].height) == 1400);
    CHECK(headLockedPieces(PixelRect{}, 0, 0).empty());
}

TEST_CASE("wrist hud settings: defaults and every variable") {
    std::vector<std::string> warnings;
    const auto d = readUiSettings(envOf({}), warnings);
    CHECK(d.hud == HudMode::Panel); // the wrist is opt-in until it has been tried live
    CHECK_FALSE(d.wrist.always);
    CHECK(d.wrist.showDegrees == doctest::Approx(40.0f));
    CHECK(d.wrist.hideDegrees == doctest::Approx(55.0f));
    CHECK(warnings.empty());

    CHECK(readUiSettings(envOf({{L"ETERNALVR_HUD", L"PANEL"}}), warnings).hud == HudMode::Panel);
    CHECK(readUiSettings(envOf({{L"ETERNALVR_HUD", L"wrist"}}), warnings).hud == HudMode::Wrist);
    const auto s = readUiSettings(envOf({{L"ETERNALVR_HUD", L" Wrist "},
                                         {L"ETERNALVR_WRIST_ALWAYS", L"1"},
                                         {L"ETERNALVR_WRIST_ANGLE", L"30"},
                                         {L"ETERNALVR_WRIST_FADE", L"0.2"},
                                         {L"ETERNALVR_WRIST_WIDTH", L"0.3"},
                                         {L"ETERNALVR_WRIST_OFFSET", L"0.01, -0.05,0.2"},
                                         {L"ETERNALVR_WRIST_ABILITIES", L"0"}}),
                                  warnings);
    CHECK(s.hud == HudMode::Wrist);
    CHECK(s.wrist.always);
    CHECK(s.wrist.showDegrees == doctest::Approx(30.0f));
    CHECK(s.wrist.hideDegrees == doctest::Approx(45.0f));
    CHECK(s.wrist.fadeInSeconds == doctest::Approx(0.2f));
    CHECK(s.wrist.fadeOutSeconds == doctest::Approx(0.3f));
    CHECK(s.wrist.widthMetres == doctest::Approx(0.3f));
    CHECK(s.wrist.offset.x == doctest::Approx(0.01f));
    CHECK(s.wrist.offset.y == doctest::Approx(-0.05f));
    CHECK(s.wrist.offset.z == doctest::Approx(0.2f));
    CHECK_FALSE(s.wrist.abilities);
    CHECK(warnings.empty());
}

TEST_CASE("wrist hud settings: bad values keep the defaults and warn") {
    for (const wchar_t* offset : {L"1,2", L"0,0,0,0", L"0,,0", L"0,0,9", L"a,b,c", L""}) {
        std::vector<std::string> warnings;
        const auto s = readUiSettings(envOf({{L"ETERNALVR_HUD", L"arm"},
                                             {L"ETERNALVR_WRIST_OFFSET", offset},
                                             {L"ETERNALVR_WRIST_ANGLE", L"0"}}),
                                      warnings);
        CHECK(s.hud == HudMode::Panel);
        CHECK(s.wrist.offset.z == doctest::Approx(WristSettings{}.offset.z));
        CHECK(s.wrist.showDegrees == doctest::Approx(40.0f));
        CHECK(warnings.size() == 3);
    }
}
