// The game's render size decoupled from its desktop window (virtual_client.hpp).

#include "vkcore/virtual_client.hpp"

#include "features/render_size/window_steps.hpp"
#include "vkcore/client_rect.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mirror_place.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/present_scaling.hpp"
#include "vkcore/virtual_client_settings.hpp"
#include "vkcore/window_cap.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace evr::vkcore::virtual_client {

namespace {

using render_size::Extent;

std::atomic<std::uint64_t> g_suboptimal{0};
std::atomic<bool> g_off{false}; // State::off, read without the mutex (sizeOff)
// The band the window shows (ETERNALVR_MIRROR_CROP) while the game's swapchain is stretched into it; 0: none.
std::atomic<double> g_cropAspect{0.0};

// A mirror placement waits for a swapchain with present scaling (State::steps); read without the mutex
// (poll).
std::atomic<bool> g_moveWaits{false};

using present_scaling::chooseBehavior;
using present_scaling::chooseGravity;
using present_scaling::Scaling;

struct State {
    std::mutex mutex;
    HWND window = nullptr;
    std::vector<VkSurfaceKHR> surfaces;
    const InstanceData* instance = nullptr;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    bool deviceKnown = false;
    bool deviceMaintenance = false;
    bool hookTried = false;
    bool hookInstalled = false;
    std::optional<Scaling> scaling;
    std::optional<render_size::ViewLimits> limits;
    bool off = false; // for the rest of the process (reason logged)
    std::vector<VkSwapchainKHR> scaledSwapchains;
    std::vector<VkSwapchainKHR> unscaledSwapchains; // the game's, on its window's surface
    // The surfaces of the game's creates without present scaling that have not returned yet (one per create).
    std::vector<VkSurfaceKHR> unscaledCreates;
    render_size::WindowSteps steps;  // the mirror placement after a late answer
    bool stepsWaitForChange = false; // the waiting placement is for a size change (logged)
    int failedScaledCreates = 0;     // in a row; the game retries a failed create at once
    bool cropScaling = false; // the last scaled create stretches for ETERNALVR_MIRROR_CROP or _SIZE=fill
    // A window placement decided under the mutex, carried out after it is released.
    struct Placement {
        std::optional<render_size::WindowRect> rect;
        const char* why = "";
    };
    std::optional<Placement> placement;
};

constexpr int kScaledCreateAttempts = 2;

State& state() {
    static State& s = *new State; // never destroyed (see layer_entry.cpp)
    return s;
}

double nowSeconds() {
    return static_cast<double>(GetTickCount64()) / 1000.0;
}

// Places the game's window: its client area at `rect`, or where it is. SWP_FRAMECHANGED makes Windows send
// WM_WINDOWPOSCHANGED even when nothing moves, which makes the game read its client area again; the
// request is posted (SWP_ASYNCWINDOWPOS) so no thread waits for the window's.
void placeWindow(HWND window, const std::optional<render_size::WindowRect>& rect, const char* why) {
    if (!window || !IsWindow(window)) {
        return;
    }
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS;
    RECT frame{0, 0, 0, 0};
    if (rect) {
        frame = {0, 0, rect->width, rect->height};
        const auto style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
        const auto exStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
        AdjustWindowRectEx(&frame, style, FALSE, exStyle);
    } else {
        flags |= SWP_NOMOVE | SWP_NOSIZE;
    }
    int x = rect ? rect->x + frame.left : 0;
    int y = rect ? rect->y + frame.top : 0;
    if (rect) {
        mirror_place::keepFrameOnScreen(x, y, frame.right - frame.left, frame.bottom - frame.top, frame);
    }
    const BOOL ok =
        SetWindowPos(window, nullptr, x, y, frame.right - frame.left, frame.bottom - frame.top, flags);
    if (rect) {
        EVR_LOG("size: %s: window client area to %d,%d %dx%d%s", why, rect->x, rect->y, rect->width,
                rect->height, ok ? "" : " - SetWindowPos failed");
    } else {
        EVR_LOG("size: %s: the window is told to read its client area again%s", why,
                ok ? "" : " - SetWindowPos failed");
    }
}

// `driverLimit`: what the driver lacks, when that is why (logged with the eye's real size).
void turnOff(State& s, const char* why, const char* driverLimit = nullptr) {
    if (s.off) {
        return;
    }
    s.off = true;
    g_off.store(true, std::memory_order_release);
    g_cropAspect.store(0.0);
    s.steps.cancel();
    g_moveWaits.store(false, std::memory_order_relaxed);
    const bool wasActive = client_rect::answer().has_value();
    client_rect::setAnswer(std::nullopt);
    EVR_LOG("size: render size off: %s%s%s", why,
            wasActive ? "; the game renders at its window's size again"
                      : "; the game renders at its window's size",
            window_cap::capText(wasActive ? nullptr : s.window,
                                render_size::selectSize(settings().request, s.limits), driverLimit)
                .c_str());
    if (wasActive) {
        s.placement = State::Placement{settings().fallback, "render size off"};
    }
}

// Carries out a placement decided under the mutex (taken and released here).
void placePending(State& s) {
    std::optional<State::Placement> placement;
    HWND window = nullptr;
    {
        std::lock_guard lock(s.mutex);
        placement.swap(s.placement);
        window = s.window;
    }
    if (placement) {
        placeWindow(window, placement->rect, placement->why);
    }
}

// Decides whether the render size is on, and at which size. Under the state's mutex.
void update(State& s) {
    if (s.off || !wantedSize() || !s.window) {
        return;
    }
    if (!mp_guard::allowsGameTouch()) {
        if (mp_guard::state() != mp_policy::GuardState::Unarmed) {
            turnOff(s, "the multiplayer guard does not allow touching the game");
        }
        return; // an unarmed guard may still arm: decided at a later event
    }
    if (!s.hookTried) {
        s.hookTried = true;
        s.hookInstalled = client_rect::install();
        if (!s.hookInstalled) {
            turnOff(s, "the client area cannot be answered");
            return;
        }
    }
    if (!s.deviceKnown) {
        return;
    }
    if (!s.deviceMaintenance) {
        turnOff(s, "the game's device has no swapchain maintenance extension (present scaling)",
                "the graphics driver cannot scale presented images");
        return;
    }
    if (!s.scaling) {
        if (s.surfaces.empty() || !s.instance) {
            return; // checked on the next surface
        }
        s.scaling = present_scaling::query(*s.instance, s.physicalDevice, s.surfaces.back());
        if (!s.scaling || chooseBehavior(*s.scaling) == 0) {
            turnOff(s, "the window's surface offers no present scaling");
            return;
        }
        EVR_LOG("size: present scaling 0x%x, gravity 0x%x, scaled image %ux%u to %ux%u", s.scaling->behavior,
                s.scaling->gravity, s.scaling->minScaled.width, s.scaling->minScaled.height,
                s.scaling->maxScaled.width, s.scaling->maxScaled.height);
    }
    const std::optional<Extent> size = render_size::selectSize(settings().request, s.limits);
    if (!size) {
        return; // auto: waits for the runtime's recommendation
    }
    if (size->width < s.scaling->minScaled.width || size->height < s.scaling->minScaled.height ||
        size->width > s.scaling->maxScaled.width || size->height > s.scaling->maxScaled.height) {
        const std::string limit = window_cap::rangeLimit(s.scaling->minScaled, s.scaling->maxScaled);
        turnOff(s, "the render size is outside the surface's scaled image range", limit.c_str());
        return;
    }
    const std::optional<Extent> previous = client_rect::answer();
    if (previous == size) {
        return;
    }
    client_rect::setAnswer(size);
    EVR_LOG("size: the game's window reports a %ux%u client area%s", size->width, size->height,
            previous ? " (changed)" : "; each eye renders at this size");
    // A swapchain the game made (or is making) without present scaling would go out of date if the window
    // moved now: the window first only reads its client area again, and moves once a scaled swapchain exists
    // (window_steps.hpp).
    const render_size::WindowStep step =
        s.steps.onAnswer(render_size::GameSwapchains{!s.unscaledSwapchains.empty(),
                                                     !s.unscaledCreates.empty(), !s.scaledSwapchains.empty()},
                         nowSeconds());
    g_moveWaits.store(s.steps.waiting(), std::memory_order_relaxed);
    s.stepsWaitForChange = previous.has_value();
    if (step == render_size::WindowStep::Move) {
        s.placement =
            State::Placement{shapedMirror(size), previous ? "render size changed" : "render size on"};
        return;
    }
    s.placement = State::Placement{std::nullopt,
                                   previous ? "render size changed, the game's swapchain is not scaled yet"
                                            : "render size on, the game's swapchain is not scaled yet"};
}

// The mirror placement that waited for a scaled swapchain (`scaled`: one was made, else the wait ran out).
// Under the mutex.
void placeAfterWait(State& s, bool scaled) {
    g_moveWaits.store(false, std::memory_order_relaxed);
    const char* why = nullptr;
    if (s.stepsWaitForChange) {
        why = scaled ? "render size changed, the game's swapchain is scaled"
                     : "render size changed, the game made no scaled swapchain within 10 s";
    } else {
        why = scaled ? "render size on, the game's swapchain is scaled"
                     : "render size on, the game made no scaled swapchain within 10 s";
    }
    s.placement = State::Placement{shapedMirror(client_rect::answer()), why};
}

} // namespace

bool wanted() {
    return wantedSize();
}

void onGameSurface(const InstanceData& inst, HWND window, VkSurfaceKHR surface) {
    if (!wanted()) {
        return;
    }
    State& s = state();
    {
        std::lock_guard lock(s.mutex);
        if (s.window && s.window != window) {
            EVR_LOG("size: the game made a surface for another window %p; render size stays with %p",
                    static_cast<void*>(window), static_cast<void*>(s.window));
            return;
        }
        if (!s.window) {
            s.window = window;
            client_rect::setWindow(window);
        }
        s.instance = &inst;
        s.surfaces.push_back(surface);
        update(s);
    }
    placePending(s);
}

void onSurfaceDestroyed(VkSurfaceKHR surface) {
    if (!wanted()) {
        return;
    }
    State& s = state();
    std::lock_guard lock(s.mutex);
    s.surfaces.erase(std::remove(s.surfaces.begin(), s.surfaces.end(), surface), s.surfaces.end());
}

void onGameDevice(const InstanceData& inst, VkPhysicalDevice physicalDevice, bool swapchainMaintenance) {
    if (!wanted()) {
        return;
    }
    State& s = state();
    {
        std::lock_guard lock(s.mutex);
        s.instance = &inst;
        s.physicalDevice = physicalDevice;
        s.deviceKnown = true;
        s.deviceMaintenance = swapchainMaintenance;
        window_cap::setDeviceScaling(swapchainMaintenance);
        s.scaling.reset();
        update(s);
    }
    placePending(s);
}

void onViewLimits(const render_size::ViewLimits& limits) {
    if (!wanted()) {
        return;
    }
    State& s = state();
    {
        std::lock_guard lock(s.mutex);
        s.limits = limits;
        const auto size = render_size::selectSize(settings().request, limits);
        EVR_LOG("size: the runtime recommends %ux%u per eye (max image %ux%u, max swapchain %ux%u, %u eye(s) "
                "side "
                "by side); render size %s gives %ux%u",
                limits.recommended.width, limits.recommended.height, limits.maxImageRect.width,
                limits.maxImageRect.height, limits.maxSwapchain.width, limits.maxSwapchain.height,
                limits.eyesSideBySide, render_size::describe(settings().request).c_str(),
                size ? size->width : 0, size ? size->height : 0);
        update(s);
    }
    placePending(s);
}

void adjustCapabilities(VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR& caps) {
    const std::optional<Extent> answer = client_rect::lastAnswerOnThisThread();
    if (!answer || answer != client_rect::answer()) {
        return;
    }
    State& s = state();
    {
        std::lock_guard lock(s.mutex);
        if (std::find(s.surfaces.begin(), s.surfaces.end(), surface) == s.surfaces.end()) {
            return;
        }
    }
    caps.currentExtent = {answer->width, answer->height};
    caps.minImageExtent = caps.currentExtent;
    caps.maxImageExtent = caps.currentExtent;
}

bool addPresentScaling(VkSwapchainCreateInfoKHR& info, VkSwapchainPresentScalingCreateInfoKHR& scaling) {
    if (!wanted()) {
        return false;
    }
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (std::find(s.surfaces.begin(), s.surfaces.end(), info.surface) == s.surfaces.end()) {
        return false;
    }
    // Under the mutex, so an answer that arrives during this create sees it (update, onSwapchainCreated).
    const std::optional<Extent> active = client_rect::answer();
    if (!s.scaling || !active || *active != Extent{info.imageExtent.width, info.imageExtent.height}) {
        s.unscaledCreates.push_back(info.surface);
        return false;
    }
    scaling = {VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_SCALING_CREATE_INFO_KHR};
    // A crop needs the image stretched into the window, which has the band's shape (the presenter stretches
    // the band over the whole image, presenter_mirror.hpp). A window that fills its display takes the image
    // (or the band) stretched to its own shape.
    const bool wantsStretch = settings().crop > 0.0 || settings().fill;
    s.cropScaling = wantsStretch && (s.scaling->behavior & VK_PRESENT_SCALING_STRETCH_BIT_KHR);
    if (wantsStretch && !s.cropScaling) {
        EVR_LOG(
            "size: the surface cannot stretch the image; the window shows the whole eye image, letterboxed");
    }
    scaling.scalingBehavior = s.cropScaling ? VK_PRESENT_SCALING_STRETCH_BIT_KHR : chooseBehavior(*s.scaling);
    scaling.presentGravityX = chooseGravity(*s.scaling);
    scaling.presentGravityY = scaling.presentGravityX;
    scaling.pNext = info.pNext;
    info.pNext = &scaling;
    return true;
}

void onSwapchainCreated(VkResult result, VkSwapchainKHR swapchain, VkSurfaceKHR surface, bool scaled) {
    if (!scaled) {
        if (!wanted()) {
            return;
        }
        State& s = state();
        std::lock_guard lock(s.mutex);
        const auto creating = std::find(s.unscaledCreates.begin(), s.unscaledCreates.end(), surface);
        if (creating != s.unscaledCreates.end()) {
            s.unscaledCreates.erase(creating);
        }
        if (result == VK_SUCCESS &&
            std::find(s.surfaces.begin(), s.surfaces.end(), surface) != s.surfaces.end()) {
            s.unscaledSwapchains.push_back(swapchain);
        }
        return;
    }
    State& s = state();
    {
        std::lock_guard lock(s.mutex);
        if (result == VK_SUCCESS) {
            s.failedScaledCreates = 0;
            s.scaledSwapchains.push_back(swapchain);
            g_cropAspect.store(s.cropScaling ? settings().crop : 0.0);
            if (!s.steps.onScaledSwapchain()) {
                return;
            }
            placeAfterWait(s, true);
        } else {
            // The game's first create on a new surface can fail once and succeed when the game retries it,
            // with or without present scaling (rig run rs-r23), so only a second failure in a row turns the
            // size off.
            if (++s.failedScaledCreates < kScaledCreateAttempts) {
                EVR_LOG(
                    "size: vkCreateSwapchainKHR with present scaling failed (%d); the game's retry keeps it",
                    result);
                return;
            }
            turnOff(s, "vkCreateSwapchainKHR with present scaling failed twice");
        }
    }
    placePending(s);
}

void onSwapchainDestroyed(VkSwapchainKHR swapchain) {
    if (!wanted()) {
        return;
    }
    State& s = state();
    std::lock_guard lock(s.mutex);
    s.scaledSwapchains.erase(std::remove(s.scaledSwapchains.begin(), s.scaledSwapchains.end(), swapchain),
                             s.scaledSwapchains.end());
    s.unscaledSwapchains.erase(
        std::remove(s.unscaledSwapchains.begin(), s.unscaledSwapchains.end(), swapchain),
        s.unscaledSwapchains.end());
}

namespace {

bool isScaled(State& s, VkSwapchainKHR swapchain) {
    return std::find(s.scaledSwapchains.begin(), s.scaledSwapchains.end(), swapchain) !=
           s.scaledSwapchains.end();
}

} // namespace

VkResult scaledResult(VkSwapchainKHR swapchain, VkResult result) {
    if (result != VK_SUBOPTIMAL_KHR || !wanted()) {
        return result;
    }
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (!isScaled(s, swapchain)) {
        return result;
    }
    g_suboptimal.fetch_add(1, std::memory_order_relaxed);
    return VK_SUCCESS;
}

void scaledResults(const VkPresentInfoKHR& info, VkResult& result) {
    if (!wanted() || info.swapchainCount == 0) {
        return;
    }
    bool suboptimal = result == VK_SUBOPTIMAL_KHR;
    for (std::uint32_t i = 0; info.pResults && i < info.swapchainCount; ++i) {
        suboptimal = suboptimal || info.pResults[i] == VK_SUBOPTIMAL_KHR;
    }
    if (!suboptimal) {
        return; // the usual case: no lock taken
    }
    State& s = state();
    std::lock_guard lock(s.mutex);
    bool allScaled = true;
    for (std::uint32_t i = 0; i < info.swapchainCount; ++i) {
        const bool scaled = isScaled(s, info.pSwapchains[i]);
        allScaled = allScaled && scaled;
        if (info.pResults && info.pResults[i] == VK_SUBOPTIMAL_KHR && scaled) {
            info.pResults[i] = VK_SUCCESS;
            suboptimal = true;
        }
    }
    if (result == VK_SUBOPTIMAL_KHR && allScaled) {
        result = VK_SUCCESS;
    }
    if (suboptimal && allScaled) {
        g_suboptimal.fetch_add(1, std::memory_order_relaxed);
    }
}

void poll() {
    if (g_moveWaits.load(std::memory_order_relaxed)) {
        State& s = state();
        {
            std::lock_guard lock(s.mutex);
            if (s.steps.overdue(nowSeconds())) {
                placeAfterWait(s, false);
            }
        }
        placePending(s);
    }
    if (!client_rect::answer() || mp_guard::allowsGameTouch()) {
        return;
    }
    State& s = state();
    {
        std::lock_guard lock(s.mutex);
        turnOff(s, "the multiplayer guard no longer allows touching the game");
    }
    placePending(s);
}

void askClientArea(HWND window, const char* why) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    if (!window) {
        State& s = state();
        std::lock_guard lock(s.mutex);
        window = s.window;
    }
    placeWindow(window, std::nullopt, why);
}

std::optional<Extent> activeExtent() {
    return client_rect::answer();
}

std::optional<Extent> plannedSize() {
    State& s = state();
    std::lock_guard lock(s.mutex);
    return render_size::selectSize(settings().request, s.limits);
}

bool sizeOff() {
    return g_off.load(std::memory_order_acquire);
}

std::optional<render_size::WindowRect> mirrorWindow() {
    if (!wantedSize()) {
        return std::nullopt;
    }
    // The eye's size: the one the window reports, else a fixed render size (`auto` waits for the runtime,
    // and the window takes the eye's shape when the render size turns on).
    std::optional<Extent> eye = client_rect::answer();
    if (!eye) {
        eye = render_size::selectSize(settings().request, std::nullopt);
    }
    return shapedMirror(eye);
}

bool mirrorFills() {
    return wantedSize() && settings().fill;
}

double mirrorCropAspect() {
    return client_rect::answer() ? g_cropAspect.load(std::memory_order_relaxed) : 0.0;
}

Counters counters() {
    const auto calls = client_rect::calls();
    return {client_rect::answers(), g_suboptimal.load(std::memory_order_relaxed),
            calls[0] + calls[1] + calls[2] + calls[3], calls[4]};
}

} // namespace evr::vkcore::virtual_client
