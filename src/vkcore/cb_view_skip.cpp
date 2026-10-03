// Parallel Eye Rendering rig knobs on top of the command buffer check (cb_view_skip.hpp).

#include "vkcore/cb_view_skip.hpp"

#include "vkcore/cb_check.hpp"
#include "vkcore/log.hpp"
#include "vkcore/view_slots.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cwchar>
#include <mutex>
#include <string>
#include <vector>

namespace evr::vkcore::cb_check {

namespace {

bool splitCategory(int category) {
    return category == 7 || category == 9 || category == 10;
}

// A category list ("all" for every one) as a bit mask; 0 when unset.
std::uint32_t readSkipMask(const wchar_t* name) {
    std::wstring value;
    if (!readEnv(name, value) || value.empty()) {
        return 0u;
    }
    if (value == L"all") {
        return 0xFFFFFFFFu;
    }
    std::uint32_t m = 0;
    int n = -1;
    for (const wchar_t c : value + L",") {
        if (c >= L'0' && c <= L'9') {
            n = (n < 0 ? 0 : n * 10) + (c - L'0');
        } else if (n >= 0) {
            m |= n < 32 ? 1u << n : 0u;
            n = -1;
        }
    }
    return m;
}

std::uint32_t skipMask(int view) {
    static const std::array<std::uint32_t, 2> masks = [] {
        std::uint32_t m0 = readSkipMask(L"ETERNALVR_TEST_VIEW0_GPU_SKIP");
        const std::uint32_t m1 = readSkipMask(L"ETERNALVR_TEST_VIEW1_GPU_SKIP");
        m0 = m0 == 0xFFFFFFFFu ? 0u : m0; // slot 0 also serves the frame
        for (int v = 0; v < 2; ++v) {
            if (const std::uint32_t m = v ? m1 : m0) {
                EVR_LOG("cb-check: view %d's draws and dispatches dropped in categories mask 0x%X "
                        "(ETERNALVR_TEST_VIEW%d_GPU_SKIP)",
                        v, m, v);
            }
        }
        return std::array<std::uint32_t, 2>{m0, m1};
    }();
    return masks[static_cast<std::size_t>(view)];
}

// ETERNALVR_TEST_CALLERS: a category number; -1 (none) when unset or not a number. Read inside a Vulkan
// call, so it never throws.
int callersCategory() {
    static const int category = [] {
        std::wstring value;
        if (!readEnv(L"ETERNALVR_TEST_CALLERS", value) || value.empty()) {
            return -1;
        }
        wchar_t* end = nullptr;
        const long n = std::wcstol(value.c_str(), &end, 10);
        if (end == value.c_str() || *end != L'\0' || n < 0 || n >= kCategories) {
            EVR_LOG("cb-check: ETERNALVR_TEST_CALLERS is not a category from 0 to %d; ignored",
                    kCategories - 1);
            return -1;
        }
        return static_cast<int>(n);
    }();
    return category;
}

std::atomic<std::uint64_t> g_skipped{0};

} // namespace

bool skipDraw(VkCommandBuffer cb) {
    const std::uint32_t mask0 = skipMask(0);
    const std::uint32_t mask1 = skipMask(1);
    if ((!mask0 && !mask1) || !parallelEyesTouch()) {
        return false;
    }
    const int context = findContext(cb);
    if (context < 0) {
        return false;
    }
    const int category = context / kSlots;
    const int slot = context % kSlots;
    const bool view1 = splitCategory(category) ? slot >= 2 : slot == 1;
    const bool view0 = splitCategory(category) ? slot <= 1 : slot == 0;
    if (!(view1 && (mask1 & (1u << category))) && !(view0 && (mask0 & (1u << category)))) {
        return false;
    }
    if (g_skipped.fetch_add(1, std::memory_order_relaxed) % 200000 == 0) {
        EVR_LOG("cb-check: %llu draw(s)/dispatch(es) dropped",
                static_cast<unsigned long long>(g_skipped.load()));
    }
    return true;
}

void noteCaller(VkCommandBuffer cb, const char* call) {
    const int category = callersCategory();
    if (category < 0) {
        return;
    }
    const int context = findContext(cb);
    if (context < 0 || context / kSlots != category ||
        (splitCategory(category) ? context % kSlots < 2 : context % kSlots != 1)) {
        return;
    }
    void* frames[24] = {};
    const USHORT captured = RtlCaptureStackBackTrace(1, 24, frames, nullptr);
    std::string chain = call;
    int shown = 0;
    for (USHORT i = 0; i < captured && shown < 8; ++i) {
        char word[32];
        describeAddress(frames[i], word, sizeof(word));
        if (word[0] == 'G') {
            chain += ' ';
            chain += word;
            ++shown;
        }
    }
    static std::mutex mutex;
    static std::vector<std::string> seen;
    std::lock_guard lock(mutex);
    if (seen.size() >= 24 || std::find(seen.begin(), seen.end(), chain) != seen.end()) {
        return;
    }
    seen.push_back(chain);
    EVR_LOG("cb-check: view 1 category %d records %s", category, chain.c_str());
}

} // namespace evr::vkcore::cb_check
