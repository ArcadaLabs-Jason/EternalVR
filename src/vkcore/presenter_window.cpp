// The game's own window under Route S (docs/VR_STEREO.md, Desktop window). The window is only a mirror: the
// OpenXR runtime paces the game. Every present that reaches the window can be made to wait by the desktop
// display (a compositor that takes one image per refresh, a desktop capture), which then caps the tick rate
// at half the refresh. With VK_KHR_swapchain_maintenance1 only the presents the window shows reach it (the
// mirrored eye, at most one per two refreshes of its display); the others are handed back to the swapchain
// unpresented once their ring copy has finished.

#include "vkcore/presenter_impl.hpp"

#include "vkcore/keep_active.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/stereo_present.hpp"
#include "vkcore/virtual_client.hpp"
#include "vkcore/window_timing.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>

namespace evr::vkcore {

namespace {

// At most this many images handed back late: a later present then reaches the window as usual, so the game
// never runs out of images to acquire.
constexpr std::size_t kMaxHeldImages = 2;
// The display's refresh rate is read this often by the XR worker. EnumDisplaySettingsW can take long (it
// asks the display driver), so it stays off the game's present path and out from under the presenter's
// lock; a rate change on the desktop is rare, and seen within this time.
constexpr ULONGLONG kRefreshQueryMs = 3000;

} // namespace

bool XrPresenter::Impl::decideWindow(const VkPresentInfoKHR* info, stereo_seq::PresentKind kind) {
    windowHold = false;
    if (!windowGateChecked) {
        windowGateChecked = true;
        windowGate = dev.releaseSwapchainImages != nullptr && windowPresentsGated();
        EVR_LOG("window: %s",
                windowGate
                    ? "only the presents the window shows reach it (the mirrored eye, at most one per two "
                      "refreshes of its display); the others are handed back unpresented"
                : windowPresentsGated()
                    ? "every present reaches the window (VK_KHR_swapchain_maintenance1 is not available)"
                    : "every present reaches the window (ETERNALVR_WINDOW_PRESENTS=all)");
    }
    if (!windowGate || info->swapchainCount != 1 || heldImages.size() >= kMaxHeldImages ||
        !mp_guard::allowsGameTouch()) {
        return true;
    }
    const bool toWindow = windowPresents.present(
        qpcSeconds(qpcNow()), windowRefreshHz.load(std::memory_order_relaxed), kind, settings.mirror);
    windowHold = !toWindow;
    return toWindow;
}

stereo_seq::MirrorStep XrPresenter::Impl::windowMirrorStep(std::uint32_t eye, bool toWindow) const {
    if (!windowGate) {
        return stereo_seq::mirrorStep(settings.mirror, eye);
    }
    // Gated: the window only ever receives the mirrored eye, so nothing is copied; `off` clears it.
    return toWindow && settings.mirror == stereo_seq::Mirror::Off ? stereo_seq::MirrorStep::Clear
                                                                  : stereo_seq::MirrorStep::None;
}

void XrPresenter::Impl::holdImage(VkSwapchainKHR swapchain, std::uint32_t image, std::uint64_t value) {
    heldImages.push_back(HeldImage{swapchain, image, value});
}

void XrPresenter::Impl::handBackImages(std::uint64_t completed) {
    // An image goes back only once the device is done with it: the game's rendering (its present's wait
    // semaphores) and the ring copy that waited for it, both behind the copy's timeline value.
    for (auto it = heldImages.begin(); it != heldImages.end();) {
        if (it->value > completed) {
            ++it;
            continue;
        }
        VkReleaseSwapchainImagesInfoKHR release{VK_STRUCTURE_TYPE_RELEASE_SWAPCHAIN_IMAGES_INFO_KHR};
        release.swapchain = it->swapchain;
        release.imageIndexCount = 1;
        release.pImageIndices = &it->image;
        if (dev.releaseSwapchainImages(dev.device, &release) == VK_SUCCESS) {
            ++imagesHandedBack;
        } else {
            ++handBackFailures;
        }
        it = heldImages.erase(it);
    }
}

void XrPresenter::Impl::dropHeldImages(VkSwapchainKHR swapchain) {
    heldImages.erase(std::remove_if(heldImages.begin(), heldImages.end(),
                                    [swapchain](const HeldImage& h) { return h.swapchain == swapchain; }),
                     heldImages.end());
}

void XrPresenter::Impl::refreshDisplayRate() {
    const ULONGLONG now = GetTickCount64();
    if (lastRefreshTicks != 0 && now - lastRefreshTicks < kRefreshQueryMs) {
        return;
    }
    lastRefreshTicks = now;
    const HWND window = gameWindow();
    const HMONITOR monitor = window ? MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) : nullptr;
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (monitor && GetMonitorInfoW(monitor, &info) &&
        EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1) {
        windowRefreshHz.store(static_cast<double>(mode.dmDisplayFrequency), std::memory_order_relaxed);
    } else {
        windowRefreshHz.store(0.0, std::memory_order_relaxed); // unknown: the gate assumes 60 Hz
    }
}

void XrPresenter::Impl::logWindowStats() {
    const window_timing::Totals t = window_timing::take();
    const auto average = [](const window_timing::Calls& c) {
        return c.count ? static_cast<double>(c.micros) / static_cast<double>(c.count) / 1000.0 : 0.0;
    };
    EVR_LOG("window: last 10 s: %llu acquire(s), average %.2f ms, max %.2f ms; %llu present call(s), average "
            "%.2f ms, max %.2f ms",
            static_cast<unsigned long long>(t.acquire.count), average(t.acquire),
            static_cast<double>(t.acquire.maxMicros) / 1000.0,
            static_cast<unsigned long long>(t.present.count), average(t.present),
            static_cast<double>(t.present.maxMicros) / 1000.0);
    if (!windowGate) {
        return;
    }
    const stereo_seq::WindowPresentGate::Counters& c = windowPresents.counters();
    EVR_LOG(
        "window: last 10 s: %llu present(s) to the window, %llu of the other eye and %llu too soon handed "
        "back; display %.0f Hz; %llu handed back, %llu failed, %zu held, in total",
        static_cast<unsigned long long>(c.presented - lastWindowCounters.presented),
        static_cast<unsigned long long>(c.otherEye - lastWindowCounters.otherEye),
        static_cast<unsigned long long>(c.tooSoon - lastWindowCounters.tooSoon),
        windowRefreshHz.load(std::memory_order_relaxed), static_cast<unsigned long long>(imagesHandedBack),
        static_cast<unsigned long long>(handBackFailures), heldImages.size());
    lastWindowCounters = c;
}

void XrPresenter::Impl::logSizeStats() {
    const std::optional<render_size::Extent> size = virtual_client::activeExtent();
    if (!size) {
        return;
    }
    const virtual_client::Counters c = virtual_client::counters();
    RECT client{};
    if (const HWND window = gameWindow()) {
        GetClientRect(window, &client);
    }
    EVR_LOG(
        "size: render size %ux%u (swapchain %ux%u, eye image %ux%u, window client %ldx%ld); last 10 s: %llu "
        "client area answer(s), %llu suboptimal result(s) taken as success; %llu call(s), %llu window "
        "move(s) "
        "by the game in total",
        size->width, size->height, gameExtent.width, gameExtent.height, eyeExtent.width, eyeExtent.height,
        client.right, client.bottom, static_cast<unsigned long long>(c.answers - lastSizeAnswers),
        static_cast<unsigned long long>(c.suboptimal - lastSizeSuboptimal),
        static_cast<unsigned long long>(c.calls), static_cast<unsigned long long>(c.moves));
    lastSizeAnswers = c.answers;
    lastSizeSuboptimal = c.suboptimal;
}

} // namespace evr::vkcore
