#include "features/arm/mod_room.hpp"

#include <doctest/doctest.h>

using evr::arm::kMaxJointMods;
using evr::arm::ModListCounts;
using evr::arm::modListsAgree;
using evr::arm::planRoom;
using evr::arm::restoreCount;
using evr::arm::roomFor;
using evr::arm::roomMade;
using evr::arm::RoomPlan;

TEST_CASE("the lists InitJointMods leaves are full, and the plan grows both to six more") {
    // Rig run ik3: 3 in use, room for 3 and 3.
    const ModListCounts full{3, 3};
    CHECK(modListsAgree(full, full));
    CHECK_FALSE(roomFor(full, full, 6));
    const RoomPlan plan = planRoom(full, full, 6);
    CHECK(plan.step == RoomPlan::Step::Grow);
    CHECK(plan.num == 3);
    CHECK(plan.growTo == 9);
}

TEST_CASE("lists with room already need nothing") {
    const RoomPlan plan = planRoom({3, 9}, {3, 12}, 6);
    CHECK(plan.step == RoomPlan::Step::Enough);
    CHECK(roomFor({3, 9}, {3, 12}, 6));
    CHECK_FALSE(roomFor({3, 9}, {3, 8}, 6));
}

TEST_CASE("lists that do not check out are refused") {
    CHECK(planRoom({3, 3}, {2, 3}, 6).step == RoomPlan::Step::Refuse);   // different counts
    CHECK(planRoom({4, 3}, {4, 3}, 6).step == RoomPlan::Step::Refuse);   // count past the size
    CHECK(planRoom({-1, 3}, {-1, 3}, 6).step == RoomPlan::Step::Refuse); // negative count
    CHECK(planRoom({3, kMaxJointMods + 1}, {3, 3}, 6).step == RoomPlan::Step::Refuse);
    CHECK(planRoom({kMaxJointMods - 2, kMaxJointMods}, {kMaxJointMods - 2, kMaxJointMods}, 6).step ==
          RoomPlan::Step::Refuse);
    CHECK(planRoom({3, 3}, {3, 3}, 0).step == RoomPlan::Step::Refuse);
    CHECK_FALSE(roomFor({3, 9}, {2, 9}, 6));
}

TEST_CASE("the room is made when both lists hold the old count with room for six") {
    CHECK(roomMade({3, 9}, {3, 9}, 3, 6));
    CHECK_FALSE(roomMade({9, 9}, {9, 9}, 3, 6)); // the count was not lowered again
    CHECK_FALSE(roomMade({3, 3}, {3, 9}, 3, 6)); // the first list could not grow
    CHECK_FALSE(roomMade({3, 9}, {3, 3}, 3, 6)); // the second list could not grow
    CHECK_FALSE(roomMade({4, 9}, {4, 9}, 3, 6)); // not the count asked for
}

TEST_CASE("a count SetNum left raised is written back, nothing else") {
    // The first list could not grow, so SetNum(3) returned early and left the second at 9.
    CHECK(restoreCount({9, 9}, 3));
    CHECK_FALSE(restoreCount({3, 9}, 3));
    CHECK_FALSE(restoreCount({3, 3}, 3));
    CHECK_FALSE(restoreCount({10, 9}, 3)); // past its size: not a list to write
}
