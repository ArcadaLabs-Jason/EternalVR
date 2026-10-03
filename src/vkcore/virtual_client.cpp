// The game's render size decoupled from its desktop window (virtual_client.hpp).

#include "vkcore/virtual_client.hpp"

#include "vkcore/client_rect.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mirror_place.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/window_cap.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
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

// What the surface allows for present scaling, over every present mode the game may use.
struct Scaling {
    VkPresentScalingFlagsKHR behavior = 0;
    VkPresentGravityFlagsKHR gravity = 0;
    Extent minScaled{0, 0};
    Extent maxScaled{UINT32_MAX, UINT32_MAX};
};

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
    int failedScaledCreates = 0; // in a row; the game retries a failed create at once
    bool cropScaling = false;    // the last scaled create stretches for ETERNALVR_MIRROR_CROP or _SIZE=fill
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

struct Settings {
    render_size::Request request;
    std::optional<render_size::WindowRect> mirror;
    std::optional<render_size::WindowRect> fallback;
    double crop = 0.0;    // ETERNALVR_MIRROR_CROP (mirror_place.hpp)
    bool fill = false;    // ETERNALVR_MIRROR_SIZE=fill
    bool centred = false; // `mirror` is centred on the display ETERNALVR_MIRROR_DISPLAY chose
};

const Settings& settings() {
    static const Settings s = [] {
        Settings out;
        std::wstring size;
        std::wstring scale;
        std::wstring mirror;
        std::wstring window;
        readEnv(L"ETERNALVR_RENDER_SIZE", size);
        const auto request = render_size::parseRenderSize(size);
        if (!request) {
            EVR_LOG("size: ETERNALVR_RENDER_SIZE '%ls' is not auto, WxH or off; off", size.c_str());
            return out;
        }
        out.request = *request;
        if (readEnv(L"ETERNALVR_RENDER_SCALE", scale)) {
            if (const auto parsed = render_size::parseRenderScale(scale)) {
                out.request.scale = *parsed;
            } else {
                EVR_LOG("size: ETERNALVR_RENDER_SCALE '%ls' is not auto or 0.5 to 2; 1.0", scale.c_str());
            }
        }
        if (readEnv(L"ETERNALVR_MIRROR_WINDOW", mirror) && !mirror.empty()) {
            out.mirror = render_size::parseWindowRect(mirror);
            if (!out.mirror) {
                EVR_LOG(
                    "size: ETERNALVR_MIRROR_WINDOW '%ls' is not x,y,width,height; the window keeps its place",
                    mirror.c_str());
            }
        }
        if (readEnv(L"ETERNALVR_WINDOW", window) && !window.empty()) {
            out.fallback = render_size::parseWindowRect(window);
        }
        if (out.request.mode != render_size::Mode::Off) {
            const mirror_place::Options options = mirror_place::read(out.mirror);
            out.mirror = options.rect;
            out.crop = options.crop;
            out.fill = options.fill;
            out.centred = options.centred;
            char mirrorText[64] = "where it is";
            if (out.mirror) {
                std::snprintf(mirrorText, sizeof(mirrorText), "at %d,%d %dx%d", out.mirror->x, out.mirror->y,
                              out.mirror->width, out.mirror->height);
            }
            EVR_LOG("size: render size %s (T-031); the desktop window shows it scaled, %s",
                    render_size::describe(out.request).c_str(), mirrorText);
        }
        return out;
    }();
    return s;
}

bool wantedSize() {
    return settings().request.mode != render_size::Mode::Off;
}

// The mirror window for an eye image of `eye`: with crop `full` it takes the eye's shape (mirror_place.hpp).
std::optional<render_size::WindowRect> shapedMirror(const std::optional<Extent>& eye) {
    const Settings& set = settings();
    return mirror_place::shapeToEye(mirror_place::Options{set.mirror, set.crop, set.fill, set.centred}, eye);
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

// The surface's present scaling over the present modes the game may use (FIFO, immediate, mailbox).
std::optional<Scaling>
queryScaling(const InstanceData& inst, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
    if (!inst.surfaceMaintenance1 || !inst.nextSurfaceCapabilities2 ||
        !inst.vk.GetPhysicalDeviceSurfacePresentModesKHR) {
        return std::nullopt;
    }
    std::uint32_t count = 0;
    inst.vk.GetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    inst.vk.GetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, modes.data());
    Scaling all;
    all.behavior = ~0u;
    all.gravity = ~0u;
    bool any = false;
    for (const VkPresentModeKHR mode :
         {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR}) {
        if (std::find(modes.begin(), modes.end(), mode) == modes.end()) {
            continue;
        }
        VkSurfacePresentModeKHR presentMode{VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_KHR};
        presentMode.presentMode = mode;
        VkPhysicalDeviceSurfaceInfo2KHR info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
                                             &presentMode};
        info.surface = surface;
        VkSurfacePresentScalingCapabilitiesKHR scaling{
            VK_STRUCTURE_TYPE_SURFACE_PRESENT_SCALING_CAPABILITIES_KHR};
        VkSurfaceCapabilities2KHR caps{VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR, &scaling};
        if (inst.nextSurfaceCapabilities2(physicalDevice, &info, &caps) != VK_SUCCESS) {
            return std::nullopt;
        }
        window_cap::logPresentMode(mode, scaling, caps.surfaceCapabilities);
        any = true;
        all.behavior &= scaling.supportedPresentScaling;
        all.gravity &= scaling.supportedPresentGravityX & scaling.supportedPresentGravityY;
        all.minScaled.width = std::max(all.minScaled.width, scaling.minScaledImageExtent.width);
        all.minScaled.height = std::max(all.minScaled.height, scaling.minScaledImageExtent.height);
        all.maxScaled.width = std::min(all.maxScaled.width, scaling.maxScaledImageExtent.width);
        all.maxScaled.height = std::min(all.maxScaled.height, scaling.maxScaledImageExtent.height);
    }
    if (!any) {
        return std::nullopt;
    }
    return all;
}

// The scaling the swapchain is created with: the whole image letterboxed into the window (aspect kept),
// else stretched, else one to one (a crop). 0 when none is offered. With the whole image the window has the
// eye's shape (shapedMirror), so no bars show, and a driver that stretches the first presents regardless (the
// rig's NVIDIA driver did until the window was minimised and restored) shows the same image.
VkPresentScalingFlagsKHR chooseBehavior(const Scaling& s) {
    if ((s.behavior & VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_KHR) && s.gravity) {
        return VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_KHR;
    }
    if (s.behavior & VK_PRESENT_SCALING_STRETCH_BIT_KHR) {
        return VK_PRESENT_SCALING_STRETCH_BIT_KHR;
    }
    if (s.behavior & VK_PRESENT_SCALING_ONE_TO_ONE_BIT_KHR) {
        return VK_PRESENT_SCALING_ONE_TO_ONE_BIT_KHR;
    }
    return 0;
}

VkPresentGravityFlagsKHR chooseGravity(const Scaling& s) {
    if (s.gravity & VK_PRESENT_GRAVITY_CENTERED_BIT_KHR) {
        return VK_PRESENT_GRAVITY_CENTERED_BIT_KHR;
    }
    return s.gravity & VK_PRESENT_GRAVITY_MIN_BIT_KHR ? VK_PRESENT_GRAVITY_MIN_BIT_KHR : 0;
}

// `driverLimit`: what the driver lacks, when that is why (logged with the eye's real size).
void turnOff(State& s, const char* why, const char* driverLimit = nullptr) {
    if (s.off) {
        return;
    }
    s.off = true;
    g_off.store(true, std::memory_order_release);
    g_cropAspect.store(0.0);
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
        s.scaling = queryScaling(*s.instance, s.physicalDevice, s.surfaces.back());
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
    s.placement = State::Placement{shapedMirror(size), previous ? "render size changed" : "render size on"};
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
    const std::optional<Extent> active = client_rect::answer();
    if (!active || *active != Extent{info.imageExtent.width, info.imageExtent.height}) {
        return false;
    }
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (!s.scaling || std::find(s.surfaces.begin(), s.surfaces.end(), info.surface) == s.surfaces.end()) {
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

void onSwapchainCreated(VkResult result, VkSwapchainKHR swapchain, bool scaled) {
    if (!scaled) {
        return;
    }
    State& s = state();
    {
        std::lock_guard lock(s.mutex);
        if (result == VK_SUCCESS) {
            s.failedScaledCreates = 0;
            s.scaledSwapchains.push_back(swapchain);
            g_cropAspect.store(s.cropScaling ? settings().crop : 0.0);
            return;
        }
        // The game's first create on a new surface can fail once and succeed when the game retries it, with
        // or without present scaling (rig run rs-r23), so only a second failure in a row turns the size off.
        if (++s.failedScaledCreates < kScaledCreateAttempts) {
            EVR_LOG("size: vkCreateSwapchainKHR with present scaling failed (%d); the game's retry keeps it",
                    result);
            return;
        }
        turnOff(s, "vkCreateSwapchainKHR with present scaling failed twice");
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
