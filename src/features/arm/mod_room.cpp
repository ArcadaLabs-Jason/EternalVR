#include "features/arm/mod_room.hpp"

#include <cstring>

namespace evr::arm {

namespace {

bool sane(ModListCounts list) {
    return list.num >= 0 && list.num <= list.size && list.size <= kMaxJointMods;
}

} // namespace

bool modListsAgree(ModListCounts first, ModListCounts second) {
    return sane(first) && sane(second) && first.num == second.num;
}

bool roomFor(ModListCounts first, ModListCounts second, std::int32_t extra) {
    return modListsAgree(first, second) && extra >= 0 && first.num + extra <= first.size &&
           second.num + extra <= second.size;
}

RoomPlan planRoom(ModListCounts first, ModListCounts second, std::int32_t extra) {
    RoomPlan plan;
    if (!modListsAgree(first, second) || extra <= 0 || first.num + extra > kMaxJointMods) {
        return plan;
    }
    plan.num = first.num;
    plan.growTo = first.num + extra;
    plan.step = roomFor(first, second, extra) ? RoomPlan::Step::Enough : RoomPlan::Step::Grow;
    return plan;
}

bool restoreCount(ModListCounts list, std::int32_t num) {
    return list.num > num && list.num <= list.size;
}

bool roomMade(ModListCounts first, ModListCounts second, std::int32_t num, std::int32_t extra) {
    return first.num == num && roomFor(first, second, extra);
}

JointModEntry noChangeEntry(std::int16_t joint) {
    JointModEntry entry{};
    const float identity[12] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f};
    const std::int16_t noParent = -1;
    std::memcpy(entry.data(), identity, sizeof(identity));
    std::memcpy(entry.data() + kJointModJoint, &joint, sizeof(joint));
    std::memcpy(entry.data() + kJointModJoint + 2, &noParent, sizeof(noParent));
    std::memcpy(entry.data() + kJointModJoint + 4, &kJointModNoChangeFlags, sizeof(kJointModNoChangeFlags));
    return entry;
}

std::vector<EntryWrite> noChangeWrites(std::int32_t base, std::span<const std::int16_t> ids) {
    std::vector<EntryWrite> out;
    for (std::size_t list = 0; list < 2; ++list) {
        for (std::size_t k = 0; k < ids.size(); ++k) {
            out.push_back(
                {list, (static_cast<std::size_t>(base) + k) * kJointModSize, noChangeEntry(ids[k])});
        }
    }
    return out;
}

} // namespace evr::arm
