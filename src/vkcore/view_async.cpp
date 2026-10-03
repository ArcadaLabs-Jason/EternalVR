#include "vkcore/view_async.hpp"

#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/view_slots.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "parallel eyes";

// 0x1CC397E mov rax, [rip + cvar] (the cvar object 0x66E8930); 0x1CC3998 mov ecx, [rax + 8] (its integer
// value); 0x1CC399B mov eax, [rip + 0x667EF78] (the vendor), then the value picks async compute (r14b).
// The hook runs at 0x1CC399B, with ecx loaded.
constexpr std::uint32_t kValueLoad = 0x1CC3998;
constexpr std::uint8_t kValueLoadBytes[] = {0x8B, 0x48, 0x08, 0x8B, 0x05};
constexpr std::uint32_t kHookSite = 0x1CC399B;

bool g_keep = false;                // ETERNALVR_TEST_PE_ASYNC=keep
std::atomic<bool> g_asyncOn{false}; // the game's device has an async compute queue all the same
std::atomic<bool> g_read{false};

// Inert unless Parallel Eye Rendering is fully installed and the guard allows game touches.
void onAsyncRead(HookRegisters& r) {
    if (!viewSlotsActive() || !mp_guard::allowsGameTouch()) {
        return;
    }
    const auto value = static_cast<std::int32_t>(r.rcx);
    if (!g_keep) {
        r.rcx = 0;
    }
    if (!g_read.exchange(true)) {
        if (g_keep) {
            EVR_LOG("%s: r_enableAsyncCompute %d left as the game has it (ETERNALVR_TEST_PE_ASYNC=keep)",
                    kTag, value);
        } else {
            EVR_LOG("%s: async compute off (the device setup read r_enableAsyncCompute %d as 0): both eyes "
                    "would record into the engine's one set of async compute contexts",
                    kTag, value);
        }
    }
}

} // namespace

bool checkAsyncComputeRead(const std::byte* base) {
    for (std::size_t i = 0; i < sizeof(kValueLoadBytes); ++i) {
        if (std::to_integer<std::uint8_t>(base[kValueLoad + i]) != kValueLoadBytes[i]) {
            EVR_LOG("%s: RVA 0x%zX is not the expected r_enableAsyncCompute read", kTag, kValueLoad + i);
            return false;
        }
    }
    return true;
}

bool installAsyncComputeOff(const std::byte* base) {
    std::wstring value;
    g_keep = readEnv(L"ETERNALVR_TEST_PE_ASYNC", value) && value == L"keep";
    std::string error;
    if (!installMidHookEdit(const_cast<std::byte*>(base + kHookSite), &onAsyncRead, error)) {
        EVR_LOG("%s: async compute hook at RVA 0x%X failed: %s", kTag, kHookSite, error.c_str());
        return false;
    }
    return true;
}

void viewAsyncOnDevice(const VkDeviceCreateInfo& info, const std::vector<VkQueueFlags>& familyFlags) {
    if (!viewSlotsActive()) {
        return;
    }
    for (std::uint32_t i = 0; i < info.queueCreateInfoCount; ++i) {
        const std::uint32_t family = info.pQueueCreateInfos[i].queueFamilyIndex;
        if (family >= familyFlags.size() || !computeOnlyFamily(familyFlags[family])) {
            continue;
        }
        if (g_keep) {
            EVR_LOG("%s: the game's device has an async compute queue (family %u), kept for the test", kTag,
                    family);
            return;
        }
        g_asyncOn.store(true);
        EVR_LOG(
            "%s: the game's device has an async compute queue (family %u) although the layer turned async "
            "compute off (read %s); view 1 is not rendered, one eye",
            kTag, family, g_read.load() ? "seen" : "not seen");
        return;
    }
}

bool viewAsyncComputeOn() {
    return g_asyncOn.load(std::memory_order_relaxed);
}

} // namespace evr::vkcore
