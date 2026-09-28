#include "features/arm/mod_room.hpp"

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

} // namespace evr::arm
