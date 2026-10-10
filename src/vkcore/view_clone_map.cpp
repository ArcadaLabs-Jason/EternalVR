// Parallel Eye Rendering: the helpers view_clone_map.hpp declares for the clone files (the map's lookup,
// view 1's command contexts, the hooks' install). The clones and their map are made in view_clones.cpp.

#include "vkcore/view_clone_map.hpp"

#include "vkcore/log.hpp"
#include "vkcore/view_slots.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace evr::vkcore::view_clone {

namespace {

constexpr const char* kTag = "view-clones";

} // namespace

std::uintptr_t baseAddress() {
    return reinterpret_cast<std::uintptr_t>(base());
}

std::uintptr_t deviceContext() {
    return read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(base() + kDeviceContext));
}

std::uintptr_t lookup(const Map& map, std::uintptr_t engine) {
    const auto it =
        std::lower_bound(map.entries.begin(), map.entries.end(), std::make_pair(engine, std::uintptr_t{0}));
    return it != map.entries.end() && it->first == engine ? it->second : 0;
}

bool isView1Context(std::uintptr_t p, bool blocks) {
    if (!p) {
        return false;
    }
    const auto split = [](int c) {
        return c == 7 || c == 9 || c == 10;
    };
    const auto* table = reinterpret_cast<const std::uintptr_t*>(base() + kCommandTable);
    for (int c = 0; c < kTableCategories; ++c) {
        for (int s = split(c) ? 2 : 1; s <= (split(c) ? 3 : 1); ++s) {
            const std::uintptr_t context = table[c * kTableSlots + s];
            if (context && (blocks ? read<std::uintptr_t>(context + kContextBlock) : context) == p) {
                return true;
            }
        }
    }
    return false;
}

bool partOn(parallel_eyes::ViewPart part) {
    return (parallelEyesSettings().off & part) == 0;
}

bool hookAt(std::uint32_t rva, MidHookEditCallback callback, const char* what) {
    std::string error;
    if (!installMidHookEdit(const_cast<std::byte*>(base() + rva), callback, error)) {
        EVR_LOG("%s: %s hook at RVA 0x%X failed: %s", kTag, what, rva, error.c_str());
        return false;
    }
    return true;
}

bool watchAt(std::uint32_t rva, MidHookCallback callback, const char* what) {
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(base() + rva), callback, error)) {
        EVR_LOG("%s: %s hook at RVA 0x%X failed: %s", kTag, what, rva, error.c_str());
        return false;
    }
    return true;
}

} // namespace evr::vkcore::view_clone
