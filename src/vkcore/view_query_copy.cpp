#include "vkcore/view_query_copy.hpp"

#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/view_slots.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "view-redirects";

// `mov dword ptr [rsp + 0x38], 3`: the copy's flags argument (VK_QUERY_RESULT_64_BIT | WAIT_BIT).
constexpr std::uint8_t kFlagsStore[] = {0xC7, 0x44, 0x24, 0x38, 0x03, 0x00, 0x00, 0x00};
constexpr std::uint32_t kSites[] = {0x1C32FAF, 0x1C33039}; // a run of pending queries; the last run
constexpr std::uint32_t kFlagsOffset = 0x38;
constexpr std::uint32_t kNoWait = 0x1; // VK_QUERY_RESULT_64_BIT

std::uintptr_t g_base = 0;
std::atomic<std::uint64_t> g_copies{0}; // copies made without the wait

// The store is skipped and the flags written without the wait flag (rsp is the game's at the store).
template <std::size_t I>
void onFlagsStore(HookRegisters& r) {
    if (!parallelEyesTouch()) {
        return;
    }
    std::memcpy(reinterpret_cast<void*>(r.rsp + kFlagsOffset), &kNoWait, sizeof(kNoWait));
    r.resumeAt = g_base + kSites[I] + sizeof(kFlagsStore);
    g_copies.fetch_add(1, std::memory_order_relaxed);
}
constexpr MidHookEditCallback kCallbacks[] = {&onFlagsStore<0>, &onFlagsStore<1>};
static_assert(std::size(kCallbacks) == std::size(kSites));

} // namespace

bool prepareViewQueryCopy(const std::byte* base) {
    for (const std::uint32_t rva : kSites) {
        if (std::memcmp(base + rva, kFlagsStore, sizeof(kFlagsStore)) != 0) {
            EVR_LOG("%s: RVA 0x%X is not the occlusion query copy's flags; not changed", kTag, rva);
            return false;
        }
    }
    return true;
}

bool installViewQueryCopy(const std::byte* base) {
    g_base = reinterpret_cast<std::uintptr_t>(base);
    for (std::size_t i = 0; i < std::size(kSites); ++i) {
        std::string error;
        if (!installMidHookEdit(const_cast<std::byte*>(base + kSites[i]), kCallbacks[i], error)) {
            EVR_LOG("%s: occlusion query copy hook at RVA 0x%X failed: %s", kTag, kSites[i], error.c_str());
            return false;
        }
    }
    return true;
}

void viewQueryCopyLogCounts() {
    EVR_LOG("%s: occlusion query results copied without waiting %llu time(s)", kTag,
            static_cast<unsigned long long>(g_copies.load()));
}

} // namespace evr::vkcore
