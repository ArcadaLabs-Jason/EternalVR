#include "features/arm/mod_room.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using evr::arm::ArmJoint;
using evr::arm::kMaxJointMods;
using evr::arm::layerModCount;
using evr::arm::layerModJoint;
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

TEST_CASE("room is made for six modifiers per arm, both arms at once") {
    CHECK(evr::arm::modsForArms(1) == 6);
    CHECK(evr::arm::modsForArms(2) == 12);
    CHECK(evr::arm::modsForArms(0) == 0);
    CHECK(evr::arm::modsForArms(3) == 0);
    const RoomPlan plan = planRoom({3, 3}, {3, 3}, evr::arm::modsForArms(2));
    CHECK(plan.step == RoomPlan::Step::Grow);
    CHECK(plan.growTo == 15);
    // The off hand's six appended first, then the weapon arm's six fit after them.
    CHECK(roomFor({3, 15}, {3, 15}, 6));
    CHECK(roomFor({9, 15}, {9, 15}, 6));
    CHECK_FALSE(roomFor({15, 15}, {15, 15}, 6));
    CHECK_FALSE(roomFor({9, 9}, {9, 9}, 6)); // room for one arm only
}

TEST_CASE("an arm's modifiers: the six overrides, the wrist's change first when there is one") {
    CHECK(layerModCount(false) == 6);
    CHECK(layerModCount(true) == 7);
    // Without the wrist's change: the forearm's roll joints from the wrist up, the elbow, the shoulder end.
    constexpr ArmJoint six[] = {ArmJoint::Roll3,       ArmJoint::Roll2,   ArmJoint::Roll1,
                                ArmJoint::ForeArmRoll, ArmJoint::ForeArm, ArmJoint::UpperArm};
    for (int k = 0; k < 6; ++k) {
        CAPTURE(k);
        CHECK(layerModJoint(false, k) == six[k]);
        // With it, the same six one place later.
        CHECK(layerModJoint(true, k + 1) == six[k]);
    }
    // The wrist before every override (the blend applies the list in order, each against its parent as
    // modified so far): listed last, it would carry the overrides below it off (rig r4-bind).
    CHECK(layerModJoint(true, 0) == ArmJoint::Hand);
    for (int k = 0; k < layerModCount(false); ++k) {
        CHECK(layerModJoint(false, k) != ArmJoint::Hand);
    }
}

namespace {

// Two modifier lists as the game keeps them (jointMod_t entries), each with the layer's modifiers from `base`
// written as overrides: what both hold after the layer posed an arm for two ticks.
struct Lists {
    std::array<std::vector<std::byte>, 2> data;
};

Lists posedLists(std::int32_t base, const std::vector<std::int16_t>& ids) {
    Lists l;
    for (std::vector<std::byte>& list : l.data) {
        list.assign((static_cast<std::size_t>(base) + ids.size()) * evr::arm::kJointModSize, std::byte{0x5A});
        for (std::size_t k = 0; k < ids.size(); ++k) {
            evr::arm::JointModEntry e = evr::arm::noChangeEntry(ids[k]);
            const std::uint16_t override =
                0x22B; // the layer's override flags, with a pose that moves the joint
            std::memcpy(e.data() + evr::arm::kJointModJoint + 4, &override, sizeof(override));
            e[0] = std::byte{0x7F};
            std::memcpy(list.data() + (static_cast<std::size_t>(base) + k) * evr::arm::kJointModSize,
                        e.data(), e.size());
        }
    }
    return l;
}

bool noChangeAt(const std::vector<std::byte>& list, std::int32_t base, const std::vector<std::int16_t>& ids) {
    for (std::size_t k = 0; k < ids.size(); ++k) {
        const evr::arm::JointModEntry want = evr::arm::noChangeEntry(ids[k]);
        if (std::memcmp(list.data() + (static_cast<std::size_t>(base) + k) * evr::arm::kJointModSize,
                        want.data(), want.size()) != 0) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("a no-change modifier is the identity on its joint with the game's change flags") {
    const evr::arm::JointModEntry e = evr::arm::noChangeEntry(27);
    float pose[12] = {};
    std::memcpy(pose, e.data(), sizeof(pose));
    CHECK(pose[3] == 1.0f); // quaternion w
    CHECK(pose[0] == 0.0f);
    CHECK(pose[4] == 0.0f); // translation
    CHECK(pose[8] == 1.0f); // scale
    std::int16_t joint = 0;
    std::int16_t parent = 0;
    std::uint16_t flags = 0;
    std::memcpy(&joint, e.data() + 0x30, 2);
    std::memcpy(&parent, e.data() + 0x32, 2);
    std::memcpy(&flags, e.data() + 0x34, 2);
    CHECK(joint == 27);
    CHECK(parent == -1);
    CHECK(flags == 0x20B);
}

TEST_CASE("the give-back after a guard trip leaves no override of the layer's in either list") {
    // The weapon arm's 22 modifiers from index 3 (behind the game's three attach-joint ones); after the trip
    // no tick runs, so whatever the one give-back leaves in a list stays there.
    std::vector<std::int16_t> ids(22);
    for (std::size_t k = 0; k < ids.size(); ++k) {
        ids[k] = static_cast<std::int16_t>(3 + k);
    }
    Lists l = posedLists(3, ids);
    std::array<std::size_t, 2> writes{};
    for (const evr::arm::EntryWrite& w : evr::arm::noChangeWrites(3, ids)) {
        ++writes[w.list < 2 ? w.list : 0];
        REQUIRE(w.list < 2);
        REQUIRE(w.offset + w.bytes.size() <= l.data[w.list].size());
        std::memcpy(l.data[w.list].data() + w.offset, w.bytes.data(), w.bytes.size());
    }
    CHECK(writes[0] == ids.size()); // each list gets every modifier, once
    CHECK(writes[1] == ids.size());
    CHECK(noChangeAt(l.data[0], 3, ids));
    CHECK(noChangeAt(l.data[1], 3, ids));
    // The game's three entries before them are untouched.
    CHECK(l.data[0][0] == std::byte{0x5A});
    CHECK(l.data[1][2 * evr::arm::kJointModSize] == std::byte{0x5A});
}
