#include "features/comfort/teleport.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <optional>
#include <vector>

using evr::Vec3;
using namespace evr::comfort;

namespace {

constexpr double kFrame = 1.0 / 60.0;

struct View {
    Vec3 origin;
    float yaw = 0.0f;
    std::optional<Vec3> velocity = Vec3{};
};

// Feeds the views one game frame apart from `start`; returns every teleport reported.
std::vector<Teleport> feed(TeleportWatch& watch, const std::vector<View>& views, double start = 0.0) {
    std::vector<Teleport> out;
    double t = start;
    for (const View& v : views) {
        if (const std::optional<Teleport> tp = watch.update(t, v.origin, v.velocity, v.yaw)) {
            out.push_back(*tp);
        }
        t += kFrame;
    }
    return out;
}

// The player moving at `velocity` for `frames` frames from `from`, facing `yaw`; the physics' velocity given
// or not.
std::vector<View> move(Vec3 from, Vec3 velocity, int frames, float yaw = 0.0f, bool withVelocity = true) {
    std::vector<View> v;
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(kFrame) * static_cast<float>(i);
        v.push_back({from + velocity * t, yaw, withVelocity ? std::optional<Vec3>(velocity) : std::nullopt});
    }
    return v;
}

std::vector<View> still(Vec3 at, int frames, float yaw = 0.0f) {
    return move(at, {}, frames, yaw);
}

} // namespace

TEST_CASE("the player's own motion is no teleport, up to a booster's throw and a pit's 160 m/s fall") {
    TeleportWatch watch;
    CHECK(feed(watch, move({0.0f, 0.0f, 0.0f}, {9.5f, 0.0f, 0.0f}, 120)).empty());        // running
    CHECK(feed(watch, move({10.0f, 0.0f, 0.0f}, {75.0f, 0.0f, 0.0f}, 30), 3.0).empty());  // a booster
    CHECK(feed(watch, move({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -32.0f}, 90), 6.0).empty());  // a fall
    CHECK(feed(watch, move({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -160.0f}, 10), 9.0).empty()); // Exultia's pit
    // Without the physics' velocity the move alone counts: the booster's 1.25 m a frame is still none.
    CHECK(feed(watch, move({10.0f, 0.0f, 0.0f}, {75.0f, 0.0f, 0.0f}, 30, 0.0f, false), 12.0).empty());
}

TEST_CASE("a teleporter: one teleport with its jump and the turn the game sets on arrival") {
    TeleportWatch watch;
    std::vector<View> views = still({0.0f, 0.0f, 0.0f}, 10, 30.0f);
    // The Slayer Gate's way: 40 m away, the heading set to 168 in the same frame (a 138 degree turn).
    const std::vector<View> after = still({40.0f, 5.0f, 2.0f}, 10, 168.0f);
    views.insert(views.end(), after.begin(), after.end());
    const std::vector<Teleport> got = feed(watch, views);
    REQUIRE(got.size() == 1);
    CHECK(got[0].metres == doctest::Approx(std::sqrt(40.0f * 40.0f + 25.0f + 4.0f)));
    CHECK(got[0].from.x == doctest::Approx(0.0f));
    CHECK(got[0].to.x == doctest::Approx(40.0f));
    CHECK(got[0].turn == doctest::Approx(138.0f));
    CHECK(got[0].largestStep == doctest::Approx(138.0f));
    CHECK(got[0].views == TeleportWatch::kSettleViews);
    CHECK(got[0].seconds == doctest::Approx(kFrame).epsilon(1e-3));
}

TEST_CASE("the way back after a fall into a pit is a teleport, the fall's velocity left as it was") {
    TeleportWatch watch;
    std::vector<View> views = move({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -30.0f}, 20, 90.0f);
    for (int i = 0; i < 8; ++i) {
        views.push_back({{12.0f, -14.0f, 10.0f}, 121.7f, Vec3{0.0f, 0.0f, -30.0f}});
    }
    const std::vector<Teleport> got = feed(watch, views);
    REQUIRE(got.size() == 1);
    CHECK(got[0].to.z == doctest::Approx(10.0f));
    CHECK(got[0].turn == doctest::Approx(31.7f));
}

TEST_CASE("a heading set a frame after the jump is in the turn, the long way round taken short") {
    TeleportWatch watch;
    std::vector<View> views = still({0.0f, 0.0f, 0.0f}, 5, 170.0f);
    views.push_back({{0.0f, 12.0f, 0.0f}, 170.0f});  // the jump
    views.push_back({{0.0f, 12.0f, 0.0f}, -150.0f}); // the heading a frame later: 40 degrees on
    const std::vector<View> after = still({0.0f, 12.0f, 0.0f}, 8, -150.0f);
    views.insert(views.end(), after.begin(), after.end());
    const std::vector<Teleport> got = feed(watch, views);
    REQUIRE(got.size() == 1);
    CHECK(got[0].turn == doctest::Approx(40.0f));
    CHECK(got[0].largestStep == doctest::Approx(40.0f));
}

TEST_CASE("views further apart than the gap are not measured: a load is no teleport") {
    TeleportWatch watch;
    CHECK(feed(watch, still({0.0f, 0.0f, 0.0f}, 10)).empty());
    // A loading screen of 2 s, then the checkpoint's start 300 m away.
    CHECK(feed(watch, still({300.0f, 0.0f, 0.0f}, 10), 2.5).empty());
}

TEST_CASE("an origin that could not be read starts the jump's count over") {
    TeleportWatch watch;
    double t = 0.0;
    const Vec3 zero{};
    CHECK_FALSE(watch.update(t, Vec3{0.0f, 0.0f, 0.0f}, zero, 0.0f));
    t += kFrame;
    CHECK_FALSE(watch.update(t, std::nullopt, std::nullopt, 0.0f));
    t += kFrame;
    CHECK_FALSE(watch.update(t, Vec3{50.0f, 0.0f, 0.0f}, zero, 0.0f)); // no origin before it: not measured
    t += kFrame;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(watch.update(t, Vec3{nan, 0.0f, 0.0f}, zero, 0.0f));
    t += kFrame;
    CHECK_FALSE(watch.update(t, Vec3{0.0f, 0.0f, 0.0f}, zero, 0.0f));
}

TEST_CASE("a second teleport within the count reports the first with the views so far") {
    TeleportWatch watch;
    std::vector<View> views = still({0.0f, 0.0f, 0.0f}, 3, 0.0f);
    views.push_back({{20.0f, 0.0f, 0.0f}, 90.0f});
    views.push_back({{20.0f, 0.0f, 0.0f}, 90.0f});
    const std::vector<View> back = still({-20.0f, 0.0f, 0.0f}, 7, 0.0f);
    views.insert(views.end(), back.begin(), back.end());
    const std::vector<Teleport> got = feed(watch, views);
    REQUIRE(got.size() == 2);
    CHECK(got[0].to.x == doctest::Approx(20.0f));
    CHECK(got[0].views == 2);
    CHECK(got[0].turn == doctest::Approx(0.0f)); // the second jump's own view turned it back
    CHECK(got[0].largestStep == doctest::Approx(90.0f));
    CHECK(got[1].from.x == doctest::Approx(20.0f));
    CHECK(got[1].metres == doctest::Approx(40.0f));
    CHECK(got[1].turn == doctest::Approx(-90.0f));
}

TEST_CASE("a gap during the count ends it with the views before the gap") {
    TeleportWatch watch;
    std::vector<View> views = still({0.0f, 0.0f, 0.0f}, 3, 0.0f);
    views.push_back({{20.0f, 0.0f, 0.0f}, 45.0f});
    views.push_back({{20.0f, 0.0f, 0.0f}, 45.0f});
    CHECK(feed(watch, views).empty());
    const std::optional<Teleport> got = watch.update(1.0, Vec3{20.0f, 0.0f, 0.0f}, Vec3{}, 45.0f);
    REQUIRE(got);
    CHECK(got->views == 1);
    CHECK(got->turn == doctest::Approx(45.0f));
}

TEST_CASE(
    "a glory kill's snap onto the demon while the game drives the view is no teleport; a long jump is") {
    TeleportWatch watch;
    double t = 0.0;
    const auto view = [&](Vec3 at, float yaw, bool driven) {
        const std::optional<Teleport> got = watch.update(t, at, Vec3{}, yaw, driven);
        t += kFrame;
        return got;
    };
    for (int i = 0; i < 5; ++i) {
        CHECK_FALSE(view({36.0f, -1590.0f, 17.0f}, 90.0f, false));
    }
    // The sync's snaps of the rig's glory kills, 3.0 to 4.1 m in one game frame under a forced view.
    Vec3 at{36.0f, -1590.0f, 17.0f};
    for (float snap : {3.0f, 3.4f, 4.1f, 5.0f}) {
        at.y += snap;
        CHECK_FALSE(watch.startedLastUpdate());
        CHECK_FALSE(view(at, 90.0f, true));
        CHECK_FALSE(watch.startedLastUpdate());
        for (int i = 0; i < 6; ++i) {
            CHECK_FALSE(view(at, 90.0f, true));
        }
    }
    // The same 4.1 m with the view the player's own is one.
    at.y += 4.1f;
    CHECK_FALSE(view(at, 90.0f, false));
    CHECK(watch.startedLastUpdate());
    std::vector<Teleport> got;
    for (int i = 0; i < 6; ++i) {
        if (const std::optional<Teleport> tp = view(at, 90.0f, false)) {
            got.push_back(*tp);
        }
    }
    // Under a forced view a long jump is one: a debug teleport onto a pickup 1184 m away, the way back after
    // a fall.
    for (int i = 0; i < 2; ++i) {
        CHECK_FALSE(view(at, 90.0f, true));
    }
    const Vec3 far{at.x + 1183.7f, at.y, at.z};
    CHECK_FALSE(view(far, 90.0f, true));
    CHECK(watch.startedLastUpdate());
    for (int i = 0; i < 6; ++i) {
        if (const std::optional<Teleport> tp = view(far, 90.0f, true)) {
            got.push_back(*tp);
        }
    }
    REQUIRE(got.size() == 2);
    CHECK(got[0].metres == doctest::Approx(4.1f));
    CHECK(got[1].metres == doctest::Approx(1183.7f));
}
