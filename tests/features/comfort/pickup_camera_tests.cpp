#include "features/comfort/pickup_camera.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <string>

using namespace evr::comfort;

TEST_CASE("pickup camera: names parse back, any case and spacing") {
    for (const PickupCamera camera : {PickupCamera::Still, PickupCamera::Play}) {
        const auto parsed = parsePickupCamera(pickupCameraName(camera));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == camera);
    }
    CHECK(parsePickupCamera("  Play ") == PickupCamera::Play);
    CHECK(parsePickupCamera("STILL") == PickupCamera::Still);
    CHECK(std::string(pickupCameraName(PickupCamera::Still)) == "still");
    CHECK(std::string(pickupCameraName(PickupCamera::Play)) == "play");
}

TEST_CASE("pickup camera: anything else is not a value") {
    CHECK_FALSE(parsePickupCamera("").has_value());
    CHECK_FALSE(parsePickupCamera("   ").has_value());
    CHECK_FALSE(parsePickupCamera("1").has_value());
    CHECK_FALSE(parsePickupCamera("plays").has_value());
    CHECK_FALSE(parsePickupCamera("follow").has_value());
    CHECK_FALSE(parsePickupCamera("pl ay").has_value());
}

namespace {

// One game view of PickupHeading: an episode start or end at `seconds`, the eye (metres, world scale 1), the
// map loads so far, a death, a menu.
void at(PickupHeading& h,
        double seconds,
        bool started = false,
        bool ended = false,
        evr::Vec3 eye = {},
        std::uint32_t mapLoads = 1,
        bool dead = false,
        bool menu = false,
        float unitsPerMetre = 1.0f) {
    PickupHeading::Frame f;
    f.started = started;
    f.ended = ended;
    f.seconds = seconds;
    f.eye = eye;
    f.mapLoads = mapLoads;
    f.dead = dead;
    f.menu = menu;
    f.unitsPerMetre = unitsPerMetre;
    h.step(f);
}

} // namespace

// The automap station as the rig traced it under Camera moves (docs/rig-findings/camera-animations.md section
// 7): the body faces 270 before Use, the game turns the player's angles 45 degrees to its camera as the
// pickup ends, and the view jumped with them.
TEST_CASE("pickup heading: the body keeps the heading it had before the pickup, then the aim turns back") {
    PickupHeading heading;
    double t = 10.0;
    at(heading, t);
    heading.noteBody(270.0f, true);
    CHECK_FALSE(heading.held().has_value());
    CHECK_FALSE(heading.restore(t).has_value());

    t += 0.016;
    at(heading, t, true); // Use: the pickup starts
    REQUIRE(heading.held().has_value());
    CHECK(*heading.held() == doctest::Approx(270.0f));
    // The game turns the player's angles while the pickup runs and as it ends: not the heading to keep.
    heading.noteBody(225.0f, true);
    t += 2.2;
    at(heading, t, false, false, {0.6f, -0.8f, 0.0f});
    CHECK(*heading.held() == doctest::Approx(270.0f));

    t += 0.016;
    at(heading, t, false, true, {0.6f, -0.8f, 0.0f}); // the forced view is over; the eye moved 1 m
    CHECK(heading.dropped() == PickupHeading::Dropped::None);
    CHECK(heading.movedMetres() == doctest::Approx(1.0f));
    CHECK_FALSE(heading.held().has_value());
    REQUIRE(heading.restore(t).has_value());
    CHECK(*heading.restore(t) == doctest::Approx(270.0f));
    heading.noteBody(225.0f, true); // the window's views do not replace it either
    t += 0.4;
    at(heading, t);
    CHECK(*heading.restore(t) == doctest::Approx(270.0f));

    t += PickupHeading::kRestoreSeconds;
    at(heading, t);
    CHECK_FALSE(heading.restore(t).has_value());
    heading.noteBody(271.0f, true); // the player's own again
    at(heading, t + 1.0, true);
    CHECK(*heading.held() == doctest::Approx(271.0f));
}

TEST_CASE("pickup heading: no heading held when the body was not the player's own") {
    PickupHeading heading;
    at(heading, 0.0);
    heading.noteBody(90.0f, false); // the game's yaw, a camera it drives or a menu's hold
    at(heading, 0.1, true);
    CHECK_FALSE(heading.held().has_value());
    at(heading, 2.0, false, true); // an end while nothing is held
    CHECK_FALSE(heading.restore(2.0).has_value());
    CHECK_FALSE(heading.restore(2.1).has_value());
}

// The Sentinel Crystal (docs/rig-findings/camera-animations.md section 7): Use opens the upgrade menu, which
// holds the body (not head aim's own step), and the crystal's sync starts as the menu closes, with the menu
// still up for a few views. The heading from before the menu is held through the sync.
TEST_CASE("pickup heading: a menu before the pickup keeps the heading the player had when it came up") {
    PickupHeading heading;
    at(heading, 0.0);
    heading.noteBody(135.0f, true);
    for (int i = 1; i <= 50; ++i) { // the upgrade menu: the held body, then a camera the game drives under it
        at(heading, 0.1 * i, false, false, {}, 1, false, true);
        heading.noteBody(i < 49 ? 135.0f : 125.3f, false);
    }
    at(heading, 5.1, true, false, {}, 1, false, true); // the sync starts with the menu still up
    REQUIRE(heading.held().has_value());
    CHECK(*heading.held() == doctest::Approx(135.0f));
    at(heading, 8.4, false, true);
    REQUIRE(heading.restore(8.4).has_value());
    CHECK(*heading.restore(8.4) == doctest::Approx(135.0f));

    PickupHeading driven; // out of a menu, a body that is not the player's own still holds nothing
    at(driven, 0.0);
    driven.noteBody(135.0f, true);
    at(driven, 0.1);
    driven.noteBody(125.3f, false);
    at(driven, 0.2, true);
    CHECK_FALSE(driven.held().has_value());
}

TEST_CASE("pickup heading: a pickup right after another keeps the first one's heading") {
    PickupHeading heading;
    at(heading, 0.0);
    heading.noteBody(30.0f, true);
    at(heading, 0.1, true);
    at(heading, 2.0, false, true);
    heading.noteBody(75.0f, true); // the restore window: the game's turn
    at(heading, 2.2, true);
    REQUIRE(heading.held().has_value());
    CHECK(*heading.held() == doctest::Approx(30.0f));
    CHECK_FALSE(heading.restore(2.2).has_value());
}

TEST_CASE("pickup heading: an end without a start and a heading that is not finite change nothing") {
    PickupHeading heading;
    at(heading, 1.0, false, true);
    CHECK_FALSE(heading.restore(1.0).has_value());
    heading.noteBody(std::numeric_limits<float>::quiet_NaN(), true);
    at(heading, 1.5, true);
    CHECK_FALSE(heading.held().has_value());
}

TEST_CASE("pickup heading: a start and an end in one view, and an episode that never ends") {
    PickupHeading heading;
    at(heading, 0.0);
    heading.noteBody(10.0f, true);
    at(heading, 0.1, true, true);
    CHECK_FALSE(heading.held().has_value());
    REQUIRE(heading.restore(0.1).has_value());
    CHECK(*heading.restore(0.1) == doctest::Approx(10.0f));

    PickupHeading open;
    at(open, 0.0);
    open.noteBody(20.0f, true);
    at(open, 0.1, true);
    for (int i = 1; i <= 100; ++i) { // no end in 100 s: the heading stays held, nothing is restored
        at(open, 0.1 + i);
    }
    REQUIRE(open.held().has_value());
    CHECK(*open.held() == doctest::Approx(20.0f));
    CHECK_FALSE(open.restore(100.1).has_value());
}

TEST_CASE("pickup heading: no restore after a long gap") {
    PickupHeading heading;
    at(heading, 0.0);
    heading.noteBody(10.0f, true);
    at(heading, 0.1, true);
    at(heading, 2.0, false, true);
    at(heading, 60.0); // the next view a minute later
    CHECK_FALSE(heading.restore(60.0).has_value());
    heading.noteBody(50.0f, true);
    at(heading, 61.0, true);
    CHECK(*heading.held() == doctest::Approx(50.0f));
}

// A gore nest moves the player 5.4 m at most on the rig; no pickup measured moved the player further than 10
// m (the Slayer gate's arena, 2917 m away, comes in a forced view of its own after its pickup).
TEST_CASE("pickup heading: no restore after the player was moved away, a map load or a death") {
    for (const float metres : {5.4f, 12.0f, 2917.0f}) {
        PickupHeading heading;
        at(heading, 0.0, false, false, {100.0f, 0.0f, 0.0f});
        heading.noteBody(10.0f, true);
        at(heading, 0.1, true, false, {100.0f, 0.0f, 0.0f});
        at(heading, 2.0, false, true, {100.0f, 0.0f, metres});
        CHECK(heading.movedMetres() == doctest::Approx(metres));
        CHECK(heading.restore(2.0).has_value() == (metres <= PickupHeading::kMovedMetres));
        CHECK((heading.dropped() == PickupHeading::Dropped::Moved) == (metres > PickupHeading::kMovedMetres));
    }
    PickupHeading load;
    at(load, 0.0, false, false, {}, 3);
    load.noteBody(10.0f, true);
    at(load, 0.1, true, false, {}, 3);
    at(load, 2.0, false, true, {}, 4);
    CHECK(load.dropped() == PickupHeading::Dropped::MapLoad);
    CHECK_FALSE(load.restore(2.0).has_value());

    PickupHeading died;
    at(died, 0.0);
    died.noteBody(10.0f, true);
    at(died, 0.1, true);
    at(died, 1.0, false, false, {}, 1, true);
    at(died, 2.0, false, true);
    CHECK(died.dropped() == PickupHeading::Dropped::Died);
    CHECK_FALSE(died.restore(2.0).has_value());

    PickupHeading later; // a map load in the window ends it
    at(later, 0.0);
    later.noteBody(10.0f, true);
    at(later, 0.1, true);
    at(later, 2.0, false, true);
    REQUIRE(later.restore(2.0).has_value());
    at(later, 2.1, false, false, {}, 2);
    CHECK_FALSE(later.restore(2.1).has_value());
}

TEST_CASE("pickup heading: time with a menu up does not count toward the window") {
    PickupHeading heading;
    at(heading, 0.0);
    heading.noteBody(10.0f, true);
    at(heading, 0.1, true);
    at(heading, 2.0, false, true);
    for (int i = 1; i <= 30; ++i) { // a menu for 3 s
        at(heading, 2.0 + 0.1 * i, false, false, {}, 1, false, true);
    }
    REQUIRE(heading.restore(5.0).has_value());
    at(heading, 5.3);
    CHECK(heading.restore(5.3).has_value());
    at(heading, 5.6);
    CHECK_FALSE(heading.restore(5.6).has_value());
}

TEST_CASE("pickup heading: a death in the restore window ends it") {
    PickupHeading heading;
    at(heading, 0.0);
    heading.noteBody(10.0f, true);
    at(heading, 0.1, true);
    at(heading, 2.0, false, true);
    REQUIRE(heading.restore(2.0).has_value());
    at(heading, 2.1, false, false, {}, 1, true);
    CHECK_FALSE(heading.restore(2.1).has_value());
}

TEST_CASE("pickup heading: the distance counts in metres at the world scale, and not without one") {
    // 40 units a metre: 400 units is 10 m (kept), 480 is 12 m (dropped).
    for (const float units : {400.0f, 480.0f}) {
        PickupHeading heading;
        at(heading, 0.0, false, false, {}, 1, false, false, 40.0f);
        heading.noteBody(10.0f, true);
        at(heading, 0.1, true, false, {}, 1, false, false, 40.0f);
        at(heading, 2.0, false, true, {units, 0.0f, 0.0f}, 1, false, false, 40.0f);
        CHECK(heading.movedMetres() == doctest::Approx(units / 40.0f));
        CHECK(heading.restore(2.0).has_value() == (units / 40.0f <= PickupHeading::kMovedMetres));
    }
    for (const float scale : {0.0f, -1.0f}) { // no world scale: the distance is not known, not checked
        PickupHeading heading;
        at(heading, 0.0, false, false, {}, 1, false, false, scale);
        heading.noteBody(10.0f, true);
        at(heading, 0.1, true, false, {}, 1, false, false, scale);
        at(heading, 2.0, false, true, {5000.0f, 0.0f, 0.0f}, 1, false, false, scale);
        CHECK(heading.movedMetres() == doctest::Approx(0.0f));
        CHECK(heading.dropped() == PickupHeading::Dropped::None);
        CHECK(heading.restore(2.0).has_value());
    }
}

TEST_CASE(
    "pickup heading: a map load before a death before a move, and nothing dropped when nothing was held") {
    PickupHeading all;
    at(all, 0.0);
    all.noteBody(10.0f, true);
    at(all, 0.1, true);
    at(all, 1.0, false, false, {}, 1, true);
    at(all, 2.0, false, true, {50.0f, 0.0f, 0.0f}, 2);
    CHECK(all.dropped() == PickupHeading::Dropped::MapLoad);

    PickupHeading diedMoved;
    at(diedMoved, 0.0);
    diedMoved.noteBody(10.0f, true);
    at(diedMoved, 0.1, true);
    at(diedMoved, 1.0, false, false, {}, 1, true);
    at(diedMoved, 2.0, false, true, {50.0f, 0.0f, 0.0f});
    CHECK(diedMoved.dropped() == PickupHeading::Dropped::Died);

    PickupHeading none; // no heading held (not head aim): nothing to drop, nothing to log
    at(none, 0.0);
    none.noteBody(10.0f, false);
    at(none, 0.1, true);
    at(none, 2.0, false, true, {50.0f, 0.0f, 0.0f}, 2);
    CHECK(none.dropped() == PickupHeading::Dropped::None);
    CHECK_FALSE(none.restore(2.0).has_value());
}

TEST_CASE("pickup heading: a start and an end in one view right after a map load hold nothing") {
    PickupHeading heading;
    at(heading, 0.0, false, false, {}, 1);
    heading.noteBody(10.0f, true); // noted in the map before
    at(heading, 0.1, true, true, {}, 2);
    CHECK_FALSE(heading.held().has_value());
    CHECK_FALSE(heading.restore(0.1).has_value());
    CHECK(heading.dropped() == PickupHeading::Dropped::None);
    heading.noteBody(20.0f, true); // the new map's own
    at(heading, 1.0, true, false, {}, 2);
    REQUIRE(heading.held().has_value());
    CHECK(*heading.held() == doctest::Approx(20.0f));
}

TEST_CASE("pickup heading: a menu on the view the pickup ends does not lengthen the window") {
    PickupHeading heading;
    at(heading, 0.0);
    heading.noteBody(10.0f, true);
    at(heading, 0.1, true);
    at(heading, 5.0, false, true, {}, 1, false, true); // 4.9 s since the last view, a menu up
    CHECK(heading.restore(5.4).has_value());
    at(heading, 5.6);
    CHECK_FALSE(heading.restore(5.6).has_value());
}

// The Sentinel Crystal under Camera moves (run cv2): its sync has no forced view; the game's SetViewAngles
// comes with the view its flag clears, and that set's 3-frame forced view starts as the pickup's episode
// ends.
TEST_CASE("end set window: the forced view that starts as the pickup ends, until it ends") {
    EndSetWindow w;
    double t = 1.0;
    const double frame = 1.0 / 60.0;
    w.step(false, false, t);
    CHECK_FALSE(w.covers(true, t)); // no pickup ended yet
    t += frame;
    w.step(true, false, t); // the episode ended with the last view
    CHECK(w.covers(true, t));
    CHECK_FALSE(w.covers(false, t));
    for (int i = 0; i < 3; ++i) { // the set's forced view goes on
        t += frame;
        w.step(false, true, t);
        CHECK(w.covers(true, t));
    }
    t += frame;
    w.step(false, false, t); // it ended
    CHECK_FALSE(w.covers(true, t));
    t += frame;
    w.step(false, true, t);
    CHECK_FALSE(w.covers(true, t)); // another one later is not the set's
}

TEST_CASE("end set window: a forced view that starts later than 0.1 s after the pickup is not the set's") {
    for (const bool menuFirst : {false, true}) {
        EndSetWindow w;
        double t = 1.0;
        w.step(true, false, t);
        // A pause menu is no forced view: nothing to keep while it is up.
        for (int i = 0; i < 6; ++i) {
            t += 1.0 / 60.0;
            w.step(false, false, t);
            CHECK_FALSE(w.covers(false, t));
        }
        t += menuFirst ? 0.5 : 0.01;
        w.step(false, false, t);
        CHECK_FALSE(w.covers(true, t)); // a glory kill a while after the pickup
    }
}

TEST_CASE(
    "kept heading: the pickup's while it runs, a steady kill's, the restore heading only on the end set") {
    KeptHeading k;
    CHECK_FALSE(keptHeading(k).has_value());
    k.pickupHeld = 135.0f;
    CHECK_FALSE(keptHeading(k).has_value()); // not head aim's body
    k.headAimed = true;
    CHECK(*keptHeading(k) == doctest::Approx(135.0f));
    k.gloryHeld = 40.0f;
    CHECK(*keptHeading(k) == doctest::Approx(135.0f)); // the pickup's comes first
    k.pickupHeld.reset();
    CHECK(*keptHeading(k) == doctest::Approx(40.0f));
    k.gloryHeld.reset();
    k.pickupRestore = -45.0f;
    CHECK_FALSE(keptHeading(k).has_value()); // a pause menu or a kill in the window: not kept
    k.endSetView = true;
    CHECK(*keptHeading(k) == doctest::Approx(-45.0f)); // the game's forced view of the ending set
    k.headAimed = false;
    CHECK_FALSE(keptHeading(k).has_value());
}
