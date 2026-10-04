#include "vkcore/stereo_present.hpp"

#include "stereo_seq/desktop_window.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/stereo_hooks.hpp"
#include "vkcore/view_slots.hpp"

#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

namespace evr::vkcore {

namespace {

// Route S without an experiment (or a mono frame-rate reference), and the game's swapchain on a surface. Not
// once Parallel Eye Rendering has changed the engine (installed, or failed after a change: view_slots.hpp),
// which the game's vkCreateInstance installs before it first asks; asked again each time, so it follows the
// install's result, not its request.
bool routeSPresent() {
    static const bool immediate = [] {
        std::wstring value;
        return readEnv(L"ETERNALVR_PRESENT_IMMEDIATE", value) && value == L"1";
    }();
    static const bool routeS = [] {
        std::wstring mode;
        std::wstring vsync;
        return readEnv(L"ETERNALVR_MODE", mode) && _wcsicmp(mode.c_str(), L"stereo") == 0 &&
               stereoExperimentFromEnv() == StereoExperiment::None &&
               !(readEnv(L"ETERNALVR_STEREO_VSYNC", vsync) && vsync == L"1");
    }();
    // ETERNALVR_PRESENT_IMMEDIATE=1: any mode, for frame-rate comparisons without vsync (S4).
    return immediate || (routeS && !parallelEyesChangedEngine());
}

std::uint32_t wantedImages() {
    static const std::uint32_t wanted = [] {
        constexpr std::uint32_t kDefault = 4;
        std::wstring text;
        if (!readEnv(L"ETERNALVR_STEREO_SWAP_IMAGES", text) || text.empty()) {
            return kDefault;
        }
        const auto parsed = stereo_seq::parseSwapImages(text);
        if (!parsed) {
            EVR_LOG("stereo: ETERNALVR_STEREO_SWAP_IMAGES is not 0 to 8; %u image(s)", kDefault);
            return kDefault;
        }
        return *parsed;
    }();
    return wanted;
}

} // namespace

namespace {

stereo_seq::WindowPresents windowPresentsSetting() {
    static const stereo_seq::WindowPresents setting = [] {
        std::wstring text;
        return readEnv(L"ETERNALVR_WINDOW_PRESENTS", text) ? stereo_seq::parseWindowPresents(text)
                                                           : stereo_seq::WindowPresents::Default;
    }();
    return setting;
}

} // namespace

bool windowPresentsGated() {
    return routeSPresent() && windowPresentsSetting() != stereo_seq::WindowPresents::All;
}

bool windowGateForDevice(const DeviceData& data) {
    if (!windowPresentsGated() || !data.instance->vk.GetPhysicalDeviceProperties) {
        return false;
    }
    VkPhysicalDeviceProperties props{};
    data.instance->vk.GetPhysicalDeviceProperties(data.physicalDevice, &props);
    return stereo_seq::windowGateWanted(windowPresentsSetting(), props.vendorID);
}

VkPresentModeKHR stereoPresentMode(const DeviceData& data, const VkSwapchainCreateInfoKHR& info) {
    if (!routeSPresent() || info.presentMode != VK_PRESENT_MODE_FIFO_KHR || !data.instance->isGame ||
        !data.instance->vk.GetPhysicalDeviceSurfacePresentModesKHR || !mp_guard::allowsGameTouch()) {
        return info.presentMode;
    }
    std::uint32_t count = 0;
    data.instance->vk.GetPhysicalDeviceSurfacePresentModesKHR(data.physicalDevice, info.surface, &count,
                                                              nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    data.instance->vk.GetPhysicalDeviceSurfacePresentModesKHR(data.physicalDevice, info.surface, &count,
                                                              modes.data());
    for (const VkPresentModeKHR want : {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR}) {
        for (const VkPresentModeKHR m : modes) {
            if (m == want) {
                return want;
            }
        }
    }
    return info.presentMode;
}

std::uint32_t stereoImageCount(const DeviceData& data, const VkSwapchainCreateInfoKHR& info) {
    if (!routeSPresent() || !data.instance->isGame ||
        !data.instance->vk.GetPhysicalDeviceSurfaceCapabilitiesKHR || !mp_guard::allowsGameTouch()) {
        return info.minImageCount;
    }
    VkSurfaceCapabilitiesKHR caps{};
    if (data.instance->vk.GetPhysicalDeviceSurfaceCapabilitiesKHR(data.physicalDevice, info.surface, &caps) !=
        VK_SUCCESS) {
        return info.minImageCount;
    }
    return stereo_seq::stereoSwapchainImages(info.minImageCount, wantedImages(), caps.minImageCount,
                                             caps.maxImageCount);
}

} // namespace evr::vkcore
