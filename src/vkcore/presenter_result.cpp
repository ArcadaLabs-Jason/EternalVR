// What the game's presents return, and whether they keep coming (presenter_result.hpp).

#include "vkcore/presenter_impl.hpp"

#include "stereo_seq/present_watch.hpp"
#include "vkcore/presenter_result.hpp"
#include "vkcore/window_timing.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace evr::vkcore {

namespace {

// No game present for this long while the headset runs is a stop (checked with the 10 s rates line).
constexpr double kStoppedSeconds = 5.0;
// How long the present hook waits for the held images' ring copies before handing them back.
constexpr ULONGLONG kHandBackWaitMs = 250;
// Out of date and lost surface results logged with what was handed back; later ones are counted.
constexpr std::uint64_t kLoggedLosses = 3;

std::atomic<std::uint64_t> g_losses{0};              // out of date and lost surface results
std::atomic<int> g_lastFailure{VK_SUCCESS};          // the newest failed present result
std::atomic<std::uint64_t> g_lastFailurePresents{0}; // the game's present count at that result

// ETERNALVR_TEST_PRESENT_OUT_OF_DATE, read at the first present.
std::once_flag g_knobOnce;
std::atomic<bool> g_knobArmed{false};
std::mutex g_knobMutex;
std::optional<stereo_seq::OneShotAfter> g_knob;

// XR worker only.
stereo_seq::PresentWatch g_watch(kStoppedSeconds);

double nowSeconds() {
    return static_cast<double>(window_timing::nowMicros()) / 1e6;
}

void readKnob() {
    std::wstring value;
    if (!readEnv(L"ETERNALVR_TEST_PRESENT_OUT_OF_DATE", value) || value.empty()) {
        return;
    }
    const std::optional<double> seconds = stereo_seq::parseTestSeconds(value);
    if (!seconds) {
        EVR_LOG("test: ETERNALVR_TEST_PRESENT_OUT_OF_DATE '%ls' is not a number of seconds; off",
                value.c_str());
        return;
    }
    g_knob.emplace(*seconds);
    g_knobArmed.store(true, std::memory_order_release);
    EVR_LOG("test: one game present %.1f s after the first returns VK_ERROR_OUT_OF_DATE_KHR "
            "(ETERNALVR_TEST_PRESENT_OUT_OF_DATE, a test knob)",
            *seconds);
}

// The test knob: true for the one present it turns into out of date.
bool knobDue() {
    std::call_once(g_knobOnce, readKnob);
    if (!g_knobArmed.load(std::memory_order_acquire)) {
        return false;
    }
    std::lock_guard lock(g_knobMutex);
    if (!g_knob->due(nowSeconds())) {
        return false;
    }
    g_knobArmed.store(false, std::memory_order_release);
    return true;
}

// Every held image goes back to its swapchain once its ring copy has finished: waits up to kHandBackWaitMs
// for the newest one, without the presenter's lock. Images whose copy is still running stay held and go back
// at the next present as usual.
void handBackHeld(XrPresenter::Impl& p, VkResult result) {
    std::uint64_t needed = 0;
    std::size_t held = 0;
    VkSemaphore timeline = VK_NULL_HANDLE;
    {
        std::lock_guard lock(p.mutex);
        held = p.heldImages.size();
        for (const HeldImage& h : p.heldImages) {
            needed = std::max(needed, h.value);
        }
        timeline = p.timeline;
    }
    std::uint64_t completed = 0;
    if (held != 0 && timeline != VK_NULL_HANDLE) {
        const ULONGLONG start = GetTickCount64();
        while (p.dev.vk.GetSemaphoreCounterValueKHR(p.dev.device, timeline, &completed) == VK_SUCCESS &&
               completed < needed && GetTickCount64() - start < kHandBackWaitMs) {
            Sleep(1);
        }
    }
    std::size_t left = 0;
    std::uint64_t handedBack = 0;
    std::uint64_t failed = 0;
    {
        std::lock_guard lock(p.mutex);
        const std::uint64_t backBefore = p.imagesHandedBack;
        const std::uint64_t failedBefore = p.handBackFailures;
        if (!p.heldImages.empty()) {
            p.handBackImages(completed);
        }
        left = p.heldImages.size();
        handedBack = p.imagesHandedBack - backBefore;
        failed = p.handBackFailures - failedBefore;
    }
    const std::uint64_t n = g_losses.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= kLoggedLosses) {
        EVR_LOG("present: the game's window present returned %s (%d; %llu so far): %llu held image(s) handed "
                "back to the swapchain now, %llu failed, %zu still held (their copies are running); the game "
                "is expected to recreate its swapchain",
                result == VK_ERROR_OUT_OF_DATE_KHR ? "out of date" : "surface lost", result,
                static_cast<unsigned long long>(n), static_cast<unsigned long long>(handedBack),
                static_cast<unsigned long long>(failed), left);
    }
}

} // namespace

VkResult windowPresented(XrPresenter::Impl& p,
                         VkResult result,
                         const VkPresentInfoKHR* info,
                         VkSwapchainKHR swapchain,
                         std::uint32_t image,
                         VkSemaphore wait) {
    if (result >= 0 && knobDue()) {
        EVR_LOG("test: this present returns VK_ERROR_OUT_OF_DATE_KHR to the game (the driver returned %d; "
                "ETERNALVR_TEST_PRESENT_OUT_OF_DATE)",
                result);
        result = VK_ERROR_OUT_OF_DATE_KHR;
        for (std::uint32_t i = 0; info->pResults && i < info->swapchainCount; ++i) {
            info->pResults[i] = VK_ERROR_OUT_OF_DATE_KHR;
        }
    }
    if (result >= 0) {
        return result;
    }
    g_lastFailure.store(result, std::memory_order_relaxed);
    g_lastFailurePresents.store(p.gamePresents.load(std::memory_order_relaxed), std::memory_order_relaxed);
    if (wait != VK_NULL_HANDLE) {
        // The present's wait may not have run, leaving the semaphore signalled; the image gets a new one. For
        // out of date and a lost surface the wait runs by the specification, but a driver that did not run it
        // would leave the semaphore signalled for the image's next copy; a new one costs nothing either way
        // (the old one is retired as usual).
        std::lock_guard lock(p.mutex);
        p.replacePresentSemaphore(swapchain, image, wait);
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR) {
        handBackHeld(p, result);
    }
    return result;
}

void checkGamePresents(XrPresenter::Impl& p) {
    const std::uint64_t presents = p.gamePresents.load(std::memory_order_relaxed);
    const stereo_seq::PresentWatch::Event event = g_watch.check(presents, nowSeconds());
    if (event == stereo_seq::PresentWatch::Event::Resumed) {
        EVR_LOG("present: the game presents again after %.0f s without a present", g_watch.quiet());
        return;
    }
    if (event != stereo_seq::PresentWatch::Event::Stopped) {
        return;
    }
    // The presenter's lock is only tried: a game thread stuck while holding it is itself worth the line.
    std::string held = "the presenter's lock is busy (a present or a swapchain call is stuck inside it)";
    std::unique_lock lock(p.mutex, std::try_to_lock);
    if (lock.owns_lock()) {
        held = std::to_string(p.heldImages.size()) + " image(s) held by the window gate";
        lock.unlock();
    }
    const int failure = g_lastFailure.load(std::memory_order_relaxed);
    const std::uint64_t at = g_lastFailurePresents.load(std::memory_order_relaxed);
    EVR_LOG("present: the game has stopped presenting: no game present for %.0f s while the headset runs (it "
            "repeats the last image); %llu present(s) in all, the newest failed result %d%s; %s; %llu out of "
            "date or lost surface result(s)",
            g_watch.quiet(), static_cast<unsigned long long>(presents), failure,
            failure == VK_SUCCESS ? " (none)"
                                  : (at == presents ? " (the last present)" : " (an earlier present)"),
            held.c_str(), static_cast<unsigned long long>(g_losses.load(std::memory_order_relaxed)));
}

} // namespace evr::vkcore
