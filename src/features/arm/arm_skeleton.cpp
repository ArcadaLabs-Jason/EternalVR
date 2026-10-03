#include "features/arm/arm_skeleton.hpp"

#include <algorithm>

namespace evr::arm {

namespace {

bool readU16(const SkeletonRead& read, std::size_t at, std::uint16_t& out) {
    return read(at, &out, sizeof(out));
}

// A table of `count` int16 at `offset`, inside [header, end).
bool readTable(const SkeletonRead& read,
               std::uint16_t offset,
               std::size_t count,
               std::size_t end,
               const char* what,
               std::vector<std::int16_t>& out,
               std::string& error) {
    if (offset < kSkeletonHeaderSize || offset + count * sizeof(std::int16_t) > end) {
        error = std::string("the ") + what + " table (at " + std::to_string(offset) + ", " +
                std::to_string(count) + " joints) does not fit between the header and " + std::to_string(end);
        return false;
    }
    out.resize(count);
    if (!read(offset, out.data(), count * sizeof(std::int16_t))) {
        error = std::string("the ") + what + " table cannot be read";
        return false;
    }
    return true;
}

std::vector<std::int16_t> childrenOf(const Skeleton& s, std::int16_t joint) {
    std::vector<std::int16_t> out;
    for (std::size_t i = 0; i < s.parents.size(); ++i) {
        if (s.parents[i] == joint) {
            out.push_back(static_cast<std::int16_t>(i));
        }
    }
    return out;
}

} // namespace

std::optional<Skeleton> readSkeleton(const SkeletonRead& read, std::string& error) {
    std::uint16_t end = 0;
    std::uint16_t count = 0;
    std::uint16_t parents = 0;
    std::uint16_t handles = 0;
    if (!readU16(read, kSkeletonEndAt, end) || !readU16(read, kSkeletonCountAt, count) ||
        !readU16(read, kSkeletonParentsAt, parents) || !readU16(read, kSkeletonNameHandlesAt, handles)) {
        error = "the skeleton header cannot be read";
        return std::nullopt;
    }
    if (count == 0 || count > kMaxJoints) {
        error = "joint count " + std::to_string(count) + " is out of range";
        return std::nullopt;
    }
    Skeleton out;
    if (!readTable(read, parents, count, end, "parent", out.parents, error) ||
        !readTable(read, handles, count, end, "name-handle", out.nameHandles, error)) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const std::int16_t p = out.parents[i];
        if (p < -1 || p >= static_cast<std::int16_t>(i)) {
            error = "joint " + std::to_string(i) + " has parent " + std::to_string(p);
            return std::nullopt;
        }
    }
    return out;
}

std::optional<ArmJointIndices>
findArmJoints(const Skeleton& skeleton, std::int16_t attach, std::string& error) {
    const auto count = static_cast<std::int16_t>(skeleton.parents.size());
    if (attach < 0 || attach >= count) {
        error = "the attach joint " + std::to_string(attach) + " is not in the skeleton";
        return std::nullopt;
    }
    ArmJointIndices out{};
    out[index(ArmJoint::Attach)] = attach;
    const auto hand = childrenOf(skeleton, attach);
    if (hand.size() != 1) {
        error = "the attach joint has " + std::to_string(hand.size()) + " children, not one (the wrist)";
        return std::nullopt;
    }
    out[index(ArmJoint::Hand)] = hand.front();
    // The forearm: every line of descent from the wrist exactly six joints long that ends in a leaf. Side
    // branches along it are allowed (the marine skeleton hangs a forearm device, extendfront01 and its ten
    // children, from leftforearmroll1).
    constexpr std::size_t kChain = kArmJointCount - 2;
    std::array<std::int16_t, kChain> chain{};
    int found = 0;
    const auto walk = [&](const auto& self, std::int16_t joint, std::size_t depth) -> void {
        chain[depth] = joint;
        const auto next = childrenOf(skeleton, joint);
        if (depth + 1 == kChain) {
            if (next.empty() && ++found == 1) {
                std::copy(chain.begin(), chain.end(), out.begin() + 2);
            }
            return;
        }
        for (const std::int16_t child : next) {
            self(self, child, depth + 1);
        }
    };
    for (const std::int16_t start : childrenOf(skeleton, hand.front())) {
        walk(walk, start, 0);
    }
    if (found != 1) {
        error = "the wrist has " + std::to_string(found) + " lines of six joints ending in a leaf, not one";
        return std::nullopt;
    }
    return out;
}

bool namesMatch(const Skeleton& skeleton,
                ArmSide side,
                const ArmJointIndices& joints,
                const std::array<std::int16_t, kArmJointCount>& handles,
                std::string& error) {
    for (std::size_t j = 0; j < kArmJointCount; ++j) {
        const auto joint = static_cast<std::size_t>(joints[j]);
        if (joint >= skeleton.nameHandles.size() || skeleton.nameHandles[joint] != handles[j]) {
            error = "joint " + std::to_string(joints[j]) + " is not named " + armJointNames(side)[j] +
                    " (handle " +
                    std::to_string(joint < skeleton.nameHandles.size() ? skeleton.nameHandles[joint] : -1) +
                    ", the name's " + std::to_string(handles[j]) + ")";
            return false;
        }
    }
    return true;
}

} // namespace evr::arm
