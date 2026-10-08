#include "features/posture/floor_check.hpp"

#include <doctest/doctest.h>

#include <optional>
#include <ostream>

using evr::posture::FloorCheck;
using evr::posture::kFloorWatchSeconds;

namespace {

constexpr double kFrame = 1.0 / 90.0;

} // namespace

TEST_CASE("plausible readings are used, others are not") {
    FloorCheck floor;
    CHECK(floor.update(1.67f, 0.0f, 0.0) == std::optional<float>(1.67f));
    CHECK_FALSE(floor.update(0.06f, 0.0f, kFrame));
    CHECK_FALSE(floor.update(2.6f, 0.0f, 2 * kFrame));
    CHECK_FALSE(floor.update(std::nullopt, 0.0f, 3 * kFrame));
    CHECK_FALSE(floor.moved());
}

TEST_CASE("a floor moved at the runtime's recenter is not used until it is back") {
    // A seated player (head 1.1 m above the floor); SteamVR recenters and puts its floor at head height.
    FloorCheck floor;
    floor.update(1.1f, 0.0f, 0.0);
    floor.spaceChangePending();
    CHECK(floor.pending());
    CHECK_FALSE(floor.update(0.05f, 0.0f, 0.05)); // between the announcement and the re-anchor: not used
    const auto moved = floor.afterSpaceChange(0.05f, 0.0f, 0.12);
    REQUIRE(moved);
    CHECK(*moved == doctest::Approx(1.05f));
    CHECK(floor.moved());
    CHECK_FALSE(floor.pending());
    // Standing up now reads 0.6 m: plausible, but the floor is wrong.
    CHECK_FALSE(floor.update(0.6f, 0.55f, 1.0));
    // Another recenter puts it back: within 0.3 m of the reading before.
    CHECK(floor.update(1.15f, 0.0f, 2.0) == std::optional<float>(1.15f));
    CHECK_FALSE(floor.moved());
}

TEST_CASE("a recenter that leaves the floor is not a move") {
    FloorCheck floor;
    floor.update(1.67f, 0.0f, 0.0);
    floor.spaceChangePending();
    CHECK_FALSE(floor.afterSpaceChange(1.60f, 0.0f, 0.12));
    CHECK_FALSE(floor.moved());
    CHECK(floor.update(1.60f, 0.0f, 0.2));
    // No reading before or at the re-anchor: nothing to compare.
    FloorCheck none;
    none.spaceChangePending();
    CHECK_FALSE(none.afterSpaceChange(0.06f, 0.0f, 0.12));
    CHECK_FALSE(none.moved());
    FloorCheck lost;
    lost.update(1.2f, 0.0f, 0.0);
    lost.spaceChangePending();
    CHECK_FALSE(lost.afterSpaceChange(std::nullopt, 0.0f, 0.12));
    CHECK_FALSE(lost.moved());
    CHECK(lost.update(1.2f, 0.0f, 0.2));
}

TEST_CASE("the floor moving down is a move too") {
    FloorCheck floor;
    floor.update(1.2f, 0.0f, 0.0);
    floor.spaceChangePending();
    const auto moved = floor.afterSpaceChange(2.0f, 0.0f, 0.12);
    REQUIRE(moved);
    CHECK(*moved == doctest::Approx(-0.8f));
    CHECK_FALSE(floor.update(2.0f, 0.0f, 0.2));
}

TEST_CASE("a floor space change alone: the floor's own height decides, not the head's") {
    // Standing, LOCAL's origin at the head: the floor is 1.67 m below it in LOCAL.
    FloorCheck floor;
    floor.update(1.67f, 0.0f, 0.0);
    floor.floorChangePending(0.0);
    // The player crouches 0.5 m: the head moved, the floor did not. Used.
    CHECK(floor.update(1.17f, -0.5f, 0.5) == std::optional<float>(1.17f));
    CHECK_FALSE(floor.moved());
    // Back up, then the runtime moves the floor 0.5 m up: not used, wherever the head goes.
    CHECK(floor.update(1.67f, 0.0f, 1.0));
    CHECK_FALSE(floor.update(1.17f, 0.0f, 1.5));
    CHECK(floor.moved());
    CHECK(floor.move() == doctest::Approx(0.5f));
    CHECK_FALSE(floor.update(1.4f, 0.23f, 5.0)); // standing taller: still the moved floor
    // The floor goes back down: used again.
    CHECK(floor.update(1.67f, 0.0f, 6.0) == std::optional<float>(1.67f));
    CHECK_FALSE(floor.moved());
}

TEST_CASE("the floor watch ends; a later move is not seen by it") {
    FloorCheck floor;
    floor.update(1.67f, 0.0f, 0.0);
    floor.floorChangePending(0.0);
    CHECK(floor.update(1.67f, 0.0f, kFloorWatchSeconds + 0.1));
    CHECK(floor.update(1.17f, 0.0f, kFloorWatchSeconds + 0.2)); // plausible, after the watch: used
    CHECK_FALSE(floor.moved());
}

TEST_CASE("a LOCAL change while the floor is moved switches to the reading's return") {
    FloorCheck floor;
    floor.update(1.67f, 0.0f, 0.0);
    floor.floorChangePending(0.0);
    CHECK_FALSE(floor.update(1.0f, 0.0f, 0.5));
    REQUIRE(floor.moved());
    floor.spaceChangePending();
    CHECK_FALSE(floor.afterSpaceChange(1.0f, 1.0f, 0.62)); // already moved: nothing new
    CHECK(floor.moved());
    CHECK_FALSE(floor.update(1.0f, 1.0f, 1.0));
    CHECK(floor.update(1.6f, 1.0f, 2.0) == std::optional<float>(1.6f)); // the reading is back near 1.67 m
}

TEST_CASE("a re-anchor that found no move watches the floor a moment longer") {
    // SteamVR moves LOCAL first and its floor a moment after the re-anchor.
    FloorCheck floor;
    floor.update(1.67f, 0.0f, 0.0);
    floor.spaceChangePending();
    CHECK_FALSE(floor.afterSpaceChange(1.67f, 0.0f, 0.12));
    CHECK(floor.update(1.2f, -0.47f, 0.5)); // a crouch: the floor stayed
    CHECK_FALSE(floor.update(0.06f, 0.0f, 1.0));
    CHECK(floor.moved());
    CHECK(floor.move() == doctest::Approx(1.61f));
    // After the watch a floor move is not seen this way (the reading's own checks still apply).
    FloorCheck late;
    late.update(1.67f, 0.0f, 0.0);
    late.spaceChangePending();
    CHECK_FALSE(late.afterSpaceChange(1.67f, 0.0f, 0.12));
    CHECK(late.update(1.2f, 0.0f, 0.12 + kFloorWatchSeconds + 0.1));
    CHECK_FALSE(late.moved());
}

TEST_CASE("a late LOCAL shift during the watch is not a floor move") {
    FloorCheck floor;
    floor.update(1.67f, 0.0f, 0.0);
    floor.spaceChangePending();
    CHECK_FALSE(floor.afterSpaceChange(1.67f, 0.0f, 0.12));
    // LOCAL's origin drops 0.5 m a moment after the re-anchor: the head reads 0.5 m higher in LOCAL, the
    // same above the floor.
    CHECK(floor.update(1.67f, 0.5f, 0.3) == std::optional<float>(1.67f));
    CHECK_FALSE(floor.moved());
    CHECK(floor.update(1.67f, 0.5f, 1.0));
    // A real floor move afterwards is still seen, from the new reference.
    CHECK_FALSE(floor.update(1.0f, 0.5f, 1.5));
    CHECK(floor.moved());
    CHECK(floor.move() == doctest::Approx(0.67f));
    // The floor space's own watch the same way.
    FloorCheck own;
    own.update(1.67f, 0.0f, 0.0);
    own.floorChangePending(0.0);
    CHECK(own.update(1.67f, -0.4f, 0.5));
    CHECK_FALSE(own.moved());
}

TEST_CASE("a LOCAL change forgets the floor's LOCAL height") {
    FloorCheck floor;
    floor.update(1.67f, 0.0f, 0.0);
    floor.spaceChangePending();
    floor.sessionRestarted();      // LOCAL gone without a re-anchor
    floor.floorChangePending(0.5); // no LOCAL height known yet: nothing to compare
    CHECK(floor.update(1.0f, 0.0f, 1.0));
    CHECK_FALSE(floor.moved());
}

TEST_CASE("a reconnect compares nothing and keeps a moved floor until it reads back") {
    FloorCheck floor;
    floor.update(1.67f, 0.0f, 0.0);
    floor.spaceChangePending();
    REQUIRE(floor.afterSpaceChange(0.06f, 0.0f, 0.12));
    floor.sessionRestarted();
    CHECK_FALSE(floor.pending());
    // The new session's floor is still the moved one, even where a head could be.
    CHECK_FALSE(floor.update(0.6f, 0.0f, 10.0));
    CHECK(floor.moved());
    CHECK(floor.update(1.6f, 0.0f, 11.0) == std::optional<float>(1.6f));
    // Without a move, a reconnect is no comparison: a head that moved meanwhile reads as it is.
    FloorCheck fine;
    fine.update(1.67f, 0.0f, 0.0);
    fine.sessionRestarted();
    CHECK(fine.update(1.0f, 0.3f, 5.0) == std::optional<float>(1.0f));
    CHECK_FALSE(fine.moved());
    // A floor moved by its own space change, then a reconnect: back when the reading is.
    FloorCheck own;
    own.update(1.67f, 0.0f, 0.0);
    own.floorChangePending(0.0);
    CHECK_FALSE(own.update(1.0f, 0.0f, 0.5));
    own.sessionRestarted();
    CHECK_FALSE(own.update(1.0f, 0.4f, 5.0));
    CHECK(own.update(1.65f, 0.4f, 6.0));
}

TEST_CASE("the player's recenter uses the floor again") {
    FloorCheck floor;
    floor.update(1.67f, 0.0f, 0.0);
    floor.spaceChangePending();
    REQUIRE(floor.afterSpaceChange(0.9f, 0.0f, 0.12));
    CHECK_FALSE(floor.update(0.9f, 0.0f, 1.0));
    // Seated now, the runtime put its floor back: 1.1 m never reads within 0.3 m of 1.67 m, so only the
    // player's recenter can say the floor is right again.
    CHECK_FALSE(floor.update(1.1f, -0.57f, 2.0));
    floor.trustAgain();
    CHECK(floor.update(1.1f, -0.57f, 3.0) == std::optional<float>(1.1f));
    CHECK_FALSE(floor.moved());
    // And it is the new baseline for the next runtime recenter.
    floor.spaceChangePending();
    CHECK_FALSE(floor.afterSpaceChange(1.15f, 0.0f, 4.0));
    CHECK_FALSE(floor.moved());
}
