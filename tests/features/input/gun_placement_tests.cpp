#include "features/input/gun_placement.hpp"

#include "features/input/action_aim.hpp"
#include "features/input/controller_settings.hpp"

#include "common/vector.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

using evr::Vec3;
using evr::input::ActionAimSource;
using evr::input::AimSource;
using evr::input::GunPlacement;
using evr::input::gunPlacement;
using evr::input::headGunOrigin;
using evr::input::HeadOffsetNote;
using evr::input::parseControllerSettings;
using evr::input::ViewClockRead;

namespace {

constexpr ActionAimSource kMeleeAims[] = {ActionAimSource::Same, ActionAimSource::Head,
                                          ActionAimSource::OffHand};

// The settings the layer reads from these variables, the rest unset.
evr::input::ControllerSettings settingsFrom(const std::map<std::string, std::string>& env) {
    const auto result = parseControllerSettings([&env](std::string_view name) -> std::optional<std::string> {
        const auto it = env.find(std::string(name));
        return it == env.end() ? std::nullopt : std::optional<std::string>(it->second);
    });
    REQUIRE(result.issues.empty());
    return result.settings;
}

GunPlacement placementFor(const evr::input::ControllerSettings& s, bool fistsHeld) {
    return gunPlacement(s.aim, fistsHeld, s.actionAim.melee);
}

} // namespace

TEST_CASE("head aim: the gun is where the game's view puts it, at the head, not at the weapon hand") {
    for (const ActionAimSource melee : kMeleeAims) {
        CHECK(gunPlacement(AimSource::Head, false, melee) == GunPlacement::Head);
    }
}

TEST_CASE("head aim: the fists of a melee are at the head too, where the game's view sends the punch") {
    for (const ActionAimSource melee : kMeleeAims) {
        CHECK(gunPlacement(AimSource::Head, true, melee) == GunPlacement::Head);
    }
}

TEST_CASE("hand aim: the gun stays at the weapon hand") {
    for (const ActionAimSource melee : kMeleeAims) {
        CHECK(gunPlacement(AimSource::Hand, false, melee) == GunPlacement::Hand);
    }
}

TEST_CASE("hand aim: the fists follow the game's view only while melee aims with the head or the off hand") {
    CHECK(gunPlacement(AimSource::Hand, true, ActionAimSource::Same) == GunPlacement::Hand);
    CHECK(gunPlacement(AimSource::Hand, true, ActionAimSource::Head) == GunPlacement::GameView);
    CHECK(gunPlacement(AimSource::Hand, true, ActionAimSource::OffHand) == GunPlacement::GameView);
}

TEST_CASE("view aim: the gun stays at the weapon hand, as before") {
    for (const ActionAimSource melee : kMeleeAims) {
        CHECK(gunPlacement(AimSource::View, false, melee) == GunPlacement::Hand);
        CHECK(gunPlacement(AimSource::View, true, melee) == GunPlacement::Hand);
    }
}

TEST_CASE("a player's head-aim session (controllers on, aim head) puts the gun at the head") {
    // A player's 0.1.68 log: `controllers: on: aim head, ...`, from the launcher's Aim with = Head.
    const auto s = settingsFrom({{"ETERNALVR_CONTROLLERS", "1"}, {"ETERNALVR_AIM", "head"}});
    REQUIRE(s.enabled);
    REQUIRE(s.aim == AimSource::Head);
    CHECK(placementFor(s, false) == GunPlacement::Head);
    CHECK(placementFor(s, true) == GunPlacement::Head);
}

TEST_CASE(
    "aim with the weapon hand (the launcher's Aim with default, which it always writes) keeps the gun at "
    "the hand") {
    const auto s = settingsFrom({{"ETERNALVR_CONTROLLERS", "1"}, {"ETERNALVR_AIM", "hand"}});
    CHECK(placementFor(s, false) == GunPlacement::Hand);
    CHECK(placementFor(s, true) == GunPlacement::Hand);
}

TEST_CASE("the layer's own default aim (ETERNALVR_AIM unset) is the head: the gun at the head") {
    const auto s = settingsFrom({{"ETERNALVR_CONTROLLERS", "1"}});
    REQUIRE(s.enabled);
    CHECK(s.aim == AimSource::Head);
    CHECK(placementFor(s, false) == GunPlacement::Head);
}

namespace {

// Microseconds; the layer's limits (controllers_impl.hpp kWorldStaleSeconds, kWorldHitchCapSeconds).
constexpr std::int64_t kStale = 100'000;
constexpr std::int64_t kCap = 1'000'000;
constexpr std::int64_t kFrame = 16'667;
const Vec3 kGame{36.0f, -1604.0f, 19.16f}; // the game's model origin (its eye)
const Vec3 kLean{0.05f, -0.10f, 0.25f};    // the rendered head's offset from the game's eye

// The views' clock with ticks of the hands at `first` and one frame later, read at `now`.
ViewClockRead viewsAt(std::int64_t first, std::int64_t now) {
    ViewClockRead v;
    v.clock.tick(first);
    v.clock.tick(first + kFrame);
    v.now = now;
    v.stale = kStale;
    v.cap = kCap;
    return v;
}

bool near(const std::optional<Vec3>& got, Vec3 want) {
    return got && std::fabs(got->x - want.x) < 1e-4f && std::fabs(got->y - want.y) < 1e-4f &&
           std::fabs(got->z - want.z) < 1e-4f;
}

} // namespace

TEST_CASE("head aim's gun: the game's origin carried by the head offset of the last view") {
    // Ticks at 1.0 s and a frame later; the view noted between them; read at the second tick.
    const ViewClockRead views = viewsAt(1'000'000, 1'000'000 + kFrame);
    const HeadOffsetNote note{kLean, 1'000'000 + kFrame / 2, true};
    CHECK(near(headGunOrigin(kGame, note, 0, views), kGame + kLean));
    // Head position off: a zero offset leaves the game's placement where it was.
    CHECK(near(headGunOrigin(kGame, {{}, 1'000'000 + kFrame / 2, true}, 0, views), kGame));
}

TEST_CASE("head aim's gun: no note yet, the game's placement stays") {
    const ViewClockRead views = viewsAt(1'000'000, 1'000'000 + kFrame);
    CHECK_FALSE(headGunOrigin(kGame, HeadOffsetNote{}, 0, views));
}

TEST_CASE("head aim's gun: when the views stop (head tracking lost) the last lean is not kept") {
    const HeadOffsetNote note{kLean, 1'000'000, true};
    // Ticks go on with no view after the note: past the stale limit the gun goes back to the game's
    // placement, as the view goes back to the game's eye.
    CHECK_FALSE(headGunOrigin(kGame, note, 0, viewsAt(1'000'000 + 6 * kFrame, 1'000'000 + 7 * kFrame)));
    // Two ticks with no view but within the stale limit (a late view): still used.
    CHECK(near(headGunOrigin(kGame, note, 0, viewsAt(1'000'000 + kFrame, 1'000'000 + 3 * kFrame)),
               kGame + kLean));
}

TEST_CASE("head aim's gun: after a load the old level's lean is not used") {
    const HeadOffsetNote note{kLean, 1'000'000, true};
    // A load: views and ticks both stop; the first tick after it is past the hitch cap.
    ViewClockRead late;
    late.clock.tick(1'000'000 - kFrame);
    late.clock.tick(5'000'000);
    late.now = 5'000'000;
    late.stale = kStale;
    late.cap = kCap;
    CHECK_FALSE(headGunOrigin(kGame, note, 0, late));
    // New hands (a level load): a note from before their first tick is not used, even a current one; the
    // next view's is.
    const ViewClockRead views = viewsAt(1'000'000 - kFrame, 1'000'000 + kFrame / 2);
    CHECK_FALSE(headGunOrigin(kGame, note, 1'000'000 + kFrame / 4, views));
    CHECK(near(headGunOrigin(kGame, {kLean, 1'000'000 + kFrame / 3, true}, 1'000'000 + kFrame / 4, views),
               kGame + kLean));
}

TEST_CASE("head aim's gun: a non-finite offset or origin leaves the game's placement") {
    const ViewClockRead views = viewsAt(1'000'000, 1'000'000 + kFrame);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK_FALSE(headGunOrigin(kGame, {{nan, 0.0f, 0.0f}, 1'000'000 + kFrame / 2, true}, 0, views));
    CHECK_FALSE(headGunOrigin({0.0f, inf, 0.0f}, {kLean, 1'000'000 + kFrame / 2, true}, 0, views));
    // Finite both, but their sum is not.
    CHECK_FALSE(
        headGunOrigin({3e38f, 0.0f, 0.0f}, {{3e38f, 0.0f, 0.0f}, 1'000'000 + kFrame / 2, true}, 0, views));
}
