#include "features/arm/arm_skeleton.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using evr::arm::ArmJoint;
using evr::arm::ArmSide;
using evr::arm::findArmJoints;
using evr::arm::index;
using evr::arm::kArmJointCount;
using evr::arm::namesMatch;
using evr::arm::readSkeleton;
using evr::arm::Skeleton;
using evr::arm::SkeletonRead;

namespace {

// The arms skeleton's parent table (arms.md6skl, build 25216728): 79 joints, the left arm at 30, 31 and
// 52 to 57, the right arm at 2, 3 and 24 to 29.
const std::vector<std::int16_t> kArmsParents{-1, 0,  0,  2,  3,  4,  5,  3,  7,  8,  9,  3,  11, 12, 13, 3,
                                             15, 16, 17, 3,  19, 20, 21, 3,  3,  24, 25, 26, 27, 28, 0,  30,
                                             31, 32, 33, 31, 35, 36, 37, 31, 39, 40, 41, 31, 43, 44, 45, 31,
                                             47, 48, 49, 31, 31, 52, 53, 54, 55, 56, 0,  0,  0,  0,  61, 62,
                                             63, 64, 61, 66, 67, 68, 61, 70, 71, 72, 73, 72, 75, 72, 77};

// Skeleton data laid out as the game's: header, parents at 0x40, name handles after them, then the end of
// the tables (where the file's names would start). Handle of joint i: 1000 + i.
std::vector<std::byte> buildSkeleton(const std::vector<std::int16_t>& parents) {
    const auto count = static_cast<std::uint16_t>(parents.size());
    const std::uint16_t parentsAt = 0x40;
    const auto handlesAt = static_cast<std::uint16_t>(parentsAt + count * 2 + 2);
    const auto end = static_cast<std::uint16_t>(handlesAt + count * 2);
    std::vector<std::byte> data(end + 16);
    std::memcpy(data.data() + 0x0, &end, 2);
    std::memcpy(data.data() + 0x2, &count, 2);
    std::memcpy(data.data() + 0xC, &parentsAt, 2);
    std::memcpy(data.data() + 0x10, &handlesAt, 2);
    for (std::size_t i = 0; i < parents.size(); ++i) {
        const auto handle = static_cast<std::int16_t>(1000 + i);
        std::memcpy(data.data() + parentsAt + i * 2, &parents[i], 2);
        std::memcpy(data.data() + handlesAt + i * 2, &handle, 2);
    }
    // What the first rig run read where the file keeps its names: not a length.
    const std::uint32_t junk = 0x3B2E8A6B;
    std::memcpy(data.data() + end, &junk, 4);
    return data;
}

SkeletonRead reader(const std::vector<std::byte>& data) {
    return [&data](std::size_t offset, void* out, std::size_t size) {
        if (offset + size > data.size()) {
            return false;
        }
        std::memcpy(out, data.data() + offset, size);
        return true;
    };
}

std::array<std::int16_t, kArmJointCount> handlesOf(const evr::arm::ArmJointIndices& joints) {
    std::array<std::int16_t, kArmJointCount> out{};
    for (std::size_t j = 0; j < kArmJointCount; ++j) {
        out[j] = static_cast<std::int16_t>(1000 + joints[j]);
    }
    return out;
}

} // namespace

TEST_CASE("the arm's joints are found by shape from the attach joint in the arms skeleton") {
    const auto data = buildSkeleton(kArmsParents);
    std::string error;
    const auto skeleton = readSkeleton(reader(data), error);
    REQUIRE_MESSAGE(skeleton, error);
    CHECK(skeleton->parents.size() == 79);
    CHECK(skeleton->nameHandles[57] == 1057);
    const auto joints = findArmJoints(*skeleton, 30, error);
    REQUIRE_MESSAGE(joints, error);
    const evr::arm::ArmJointIndices expected{30, 31, 52, 53, 54, 55, 56, 57};
    CHECK(*joints == expected);
    CHECK(namesMatch(*skeleton, ArmSide::Left, *joints, handlesOf(expected), error));
}

TEST_CASE("the weapon arm's joints are found by shape from the right attach joint") {
    // righthandattach 2 (a child of origin), RightHand 3 with the thumb, the finger roots and the prop under
    // it, then rightforearmroll3..1, RightForeArmRoll, RightForeArm and RightArm (24 to 29).
    const auto data = buildSkeleton(kArmsParents);
    std::string error;
    const auto skeleton = readSkeleton(reader(data), error);
    REQUIRE_MESSAGE(skeleton, error);
    const auto right = findArmJoints(*skeleton, 2, error);
    REQUIRE_MESSAGE(right, error);
    const evr::arm::ArmJointIndices expected{2, 3, 24, 25, 26, 27, 28, 29};
    CHECK(*right == expected);
    CHECK(namesMatch(*skeleton, ArmSide::Right, *right, handlesOf(expected), error));
    // The left arm's joints are not the right arm's.
    CHECK_FALSE(
        namesMatch(*skeleton, ArmSide::Right, *right, handlesOf({30, 31, 52, 53, 54, 55, 56, 57}), error));
    CHECK(error == "joint 2 is not named righthandattach (handle 1002, the name's 1030)");
    // From the wrist or a finger root the shape is not there.
    CHECK_FALSE(findArmJoints(*skeleton, 3, error));
    CHECK_FALSE(findArmJoints(*skeleton, 7, error));
}

TEST_CASE("the marine skeleton the game loads: a forearm device branches off the roll joints") {
    // marine.md6skl (92 joints, rig run ik2): arms.md6skl's first 58 joints, then extendfront01 under
    // leftforearmroll1 with ten children, camera joints, the legs and the body.
    std::vector<std::int16_t> parents(kArmsParents.begin(), kArmsParents.begin() + 58);
    parents.push_back(54); // 58 extendfront01
    for (int i = 0; i < 10; ++i) {
        parents.push_back(58); // 59..68
    }
    const std::vector<std::int16_t> rest{0,  0,  0,  0,  0,  -1, 74, 75, 76, 77, 74, 79,
                                         80, 81, 74, 83, 84, 85, 86, 85, 88, 85, 90};
    parents.insert(parents.end(), rest.begin(), rest.end());
    parents[74] = 0; // Hips under origin
    REQUIRE(parents.size() == 92);
    const auto data = buildSkeleton(parents);
    std::string error;
    const auto skeleton = readSkeleton(reader(data), error);
    REQUIRE_MESSAGE(skeleton, error);
    const auto joints = findArmJoints(*skeleton, 30, error);
    REQUIRE_MESSAGE(joints, error);
    const evr::arm::ArmJointIndices expected{30, 31, 52, 53, 54, 55, 56, 57};
    CHECK(*joints == expected);
    // No device on the right forearm: the weapon arm is the same as in arms.md6skl.
    const auto right = findArmJoints(*skeleton, 2, error);
    REQUIRE_MESSAGE(right, error);
    CHECK(*right == evr::arm::ArmJointIndices{2, 3, 24, 25, 26, 27, 28, 29});
}

TEST_CASE("a name handle that differs is reported") {
    const auto data = buildSkeleton(kArmsParents);
    std::string error;
    const auto skeleton = readSkeleton(reader(data), error);
    REQUIRE(skeleton);
    const evr::arm::ArmJointIndices joints{30, 31, 52, 53, 54, 55, 56, 57};
    auto handles = handlesOf(joints);
    handles[index(ArmJoint::ForeArm)] = 7;
    CHECK_FALSE(namesMatch(*skeleton, ArmSide::Left, joints, handles, error));
    CHECK(error == "joint 56 is not named LeftForeArm (handle 1056, the name's 7)");
    const evr::arm::ArmJointIndices right{2, 3, 24, 25, 26, 27, 28, 29};
    handles = handlesOf(right);
    handles[index(ArmJoint::Roll1)] = 9;
    CHECK_FALSE(namesMatch(*skeleton, ArmSide::Right, right, handles, error));
    CHECK(error == "joint 26 is not named rightforearmroll1 (handle 1026, the name's 9)");
}

TEST_CASE("a skeleton of another shape is refused") {
    std::string error;
    auto parents = kArmsParents;
    parents[57] = 55; // LeftArm under the forearm roll: the chain branches
    auto skeleton = readSkeleton(reader(buildSkeleton(parents)), error);
    REQUIRE(skeleton);
    CHECK_FALSE(findArmJoints(*skeleton, 30, error));
    CHECK(error == "the wrist has 0 lines of six joints ending in a leaf, not one");
    parents = kArmsParents;
    parents[29] = 27; // RightArm under the forearm roll
    skeleton = readSkeleton(reader(buildSkeleton(parents)), error);
    REQUIRE(skeleton);
    CHECK_FALSE(findArmJoints(*skeleton, 2, error));
    parents = kArmsParents;
    parents.push_back(29); // something hanging from RightArm: the line no longer ends in a leaf
    skeleton = readSkeleton(reader(buildSkeleton(parents)), error);
    REQUIRE(skeleton);
    CHECK_FALSE(findArmJoints(*skeleton, 2, error));

    skeleton = readSkeleton(reader(buildSkeleton(kArmsParents)), error);
    REQUIRE(skeleton);
    CHECK_FALSE(findArmJoints(*skeleton, 0, error));  // origin has many children
    CHECK_FALSE(findArmJoints(*skeleton, 57, error)); // a leaf
    CHECK_FALSE(findArmJoints(*skeleton, 79, error));
    CHECK_FALSE(findArmJoints(*skeleton, -1, error));
}

TEST_CASE("a skeleton that does not check out is refused") {
    std::string error;
    auto parents = kArmsParents;
    parents[5] = 9; // a parent after its child
    CHECK_FALSE(readSkeleton(reader(buildSkeleton(parents)), error));
    CHECK(error == "joint 5 has parent 9");

    // No joints, a table past the end, or cut short.
    auto data = buildSkeleton(kArmsParents);
    const std::uint16_t zero = 0;
    std::memcpy(data.data() + 0x2, &zero, 2);
    CHECK_FALSE(readSkeleton(reader(data), error));
    data = buildSkeleton(kArmsParents);
    const std::uint16_t late = 0x200;
    std::memcpy(data.data() + 0x10, &late, 2);
    CHECK_FALSE(readSkeleton(reader(data), error));
    data = buildSkeleton(kArmsParents);
    data.resize(0x60);
    CHECK_FALSE(readSkeleton(reader(data), error));
    const std::vector<std::byte> empty;
    CHECK_FALSE(readSkeleton(reader(empty), error));
}
