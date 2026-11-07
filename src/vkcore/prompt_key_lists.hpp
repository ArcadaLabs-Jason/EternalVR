#pragma once

// The key lists GetKeysForAction fills (prompt_hooks.cpp): three idList<idStr>, one per kind of device, each
// { data, num, size, granularity }.

#include <cstddef>
#include <cstdint>
#include <utility>

namespace evr::vkcore::controllers {

struct IdList {
    std::byte* data;
    int num;
    int size;
    std::int64_t granularity;
};
static_assert(sizeof(IdList) == 0x18);

// The first list with an entry, moved to the front, cut to that one entry; false when all three are empty.
inline bool keepOneEntry(IdList* lists) {
    for (int i = 0; i < 3; ++i) {
        if (lists[i].data != nullptr && lists[i].num > 0 && lists[i].num <= lists[i].size) {
            if (i != 0) {
                std::swap(lists[0], lists[i]);
            }
            lists[0].num = 1;
            lists[1].num = 0;
            return true;
        }
    }
    return false;
}

} // namespace evr::vkcore::controllers
