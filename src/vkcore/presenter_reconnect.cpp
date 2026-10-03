// Bringing VR back after the headset or its runtime went away (common/xr_recovery.hpp). The D3D12 device, its
// queue and fences, and the shared ring with the game's side of it stay as they are; the OpenXR instance,
// session, spaces and swapchains are made again on the same device once the runtime finds the headset. The
// game runs flat meanwhile, as it does before the first session starts.

#include "vkcore/fence_wait.hpp"
#include "vkcore/presenter_impl.hpp"
#include "vkcore/presenter_menu_release.hpp"

#include "vkcore/status_file.hpp"

#include <cstring>
#include <optional>

namespace evr::vkcore {

namespace {

// Every D3D12 copy into the old swapchains must finish before they go; a copy held since a stall (frame())
// gives its ring slot back once it has.
bool copiesDone(XrPresenter::Impl& p) {
    if (!waitFence(p.copyFence.Get(), p.copyFenceValue, p.copyEvent, 2000)) {
        return false;
    }
    if (p.copyStalled) {
        p.ring[p.stalledSlot].state.store(kSlotFree);
        p.copyStalled = false;
    }
    return true;
}

// A removed D3D12 device (the graphics card was reset: DXGI_ERROR_DEVICE_HUNG, _RESET or _REMOVED) never
// takes a session again; the runtime refuses it (XR_ERROR_GRAPHICS_DEVICE_INVALID) for the rest of the game.
// VR stays off and the launcher says why, instead of waiting for a headset that is not the problem.
bool deviceRemoved(XrPresenter::Impl& p) {
    const HRESULT reason = p.d3dDevice->GetDeviceRemovedReason();
    if (SUCCEEDED(reason)) {
        return false;
    }
    p.loggedDeviceRemoved = true;
    EVR_LOG("xr: reconnect: the presenter's D3D12 device was removed (0x%08lx): the graphics card was reset; "
            "VR off",
            static_cast<unsigned long>(reason));
    status::flat("the graphics card was reset; VR is off");
    return true;
}

// The frame loop's state that belongs to one session.
void resetSessionState(XrPresenter::Impl& p) {
    p.sessionState = XR_SESSION_STATE_UNKNOWN;
    p.sessionRunning = false;
    p.sessionFocused.store(false, std::memory_order_relaxed);
    p.acquiredIndex = -1;
    p.acquiredWaited = false;
    p.hasImage = false;
    p.shownHasView = false;
    p.quadPlaced = false;
    p.placeAttempts = 0;
    p.fovChecked = false;
    p.loggedFirstProjection = false;
    p.endFrameFailures = 0;
}

// One attempt: a new instance, the headset's system on the same graphics card, and a session on the
// presenter's D3D12 device. False leaves nothing made.
bool tryReconnect(XrPresenter::Impl& p, bool log) {
    if (!p.createXrInstance(true)) {
        if (log) {
            EVR_LOG("xr: reconnect: the headset runtime is not answering yet");
        }
        return false;
    }
    XrSystemGetInfo info{XR_TYPE_SYSTEM_GET_INFO};
    info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    const XrResult system = p.xr.xrGetSystem(p.instance, &info, &p.systemId);
    if (XR_FAILED(system)) {
        if (log) {
            char text[XR_MAX_RESULT_STRING_SIZE];
            EVR_LOG("xr: reconnect: no headset yet (%s)", p.xrText(system, text));
        }
        return false;
    }
    XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
    p.xr.xrGetSystemProperties(p.instance, p.systemId, &sp);
    VkExtent2D ring{};
    {
        std::lock_guard lock(p.mutex);
        ring = p.ringExtent;
    }
    // The ring stays, so the headset must take its images as they are.
    if (sp.graphicsProperties.maxSwapchainImageWidth < ring.width ||
        sp.graphicsProperties.maxSwapchainImageHeight < ring.height) {
        if (log) {
            EVR_LOG("xr: reconnect: '%s' takes images up to %ux%u, smaller than the %ux%u in use",
                    sp.systemName, sp.graphicsProperties.maxSwapchainImageWidth,
                    sp.graphicsProperties.maxSwapchainImageHeight, ring.width, ring.height);
        }
        return false;
    }
    XrGraphicsRequirementsD3D12KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
    const LUID ours = p.d3dDevice->GetAdapterLuid();
    if (XR_FAILED(p.xr.xrGetD3D12GraphicsRequirementsKHR(p.instance, p.systemId, &req)) ||
        std::memcmp(&req.adapterLuid, &ours, sizeof(LUID)) != 0) {
        if (log) {
            EVR_LOG("xr: reconnect: the runtime now uses another graphics card than the game");
        }
        return false;
    }
    {
        std::lock_guard lock(p.mutex);
        p.maxSwapchainWidth = sp.graphicsProperties.maxSwapchainImageWidth;
        p.maxSwapchainHeight = sp.graphicsProperties.maxSwapchainImageHeight;
    }
    EVR_LOG("xr: reconnect: system '%s'", sp.systemName);
    return p.createSession();
}

} // namespace

bool XrPresenter::Impl::reconnect() {
    releaseMenuInput(*this, "the session ended");
    consumerAlive.store(false); // the game's presents pass through meanwhile
    const LONGLONG lostQpc = qpcNow();
    EVR_LOG("xr: reconnecting (%s); the D3D12 device and the ring stay, the OpenXR objects are made again",
            xr_recovery::lossName(loss.kind));
    for (std::uint32_t attempt = 0; !stop.load(); ++attempt) {
        if (deviceRemoved(*this)) {
            return false;
        }
        // In short steps, so shutdown is not held up.
        for (std::uint32_t waited = 0; waited < xr_recovery::retryDelayMs(attempt) && !stop.load();
             waited += 100) {
            Sleep(100);
        }
        if (stop.load()) {
            break;
        }
        const bool log = xr_recovery::logsAttempt(attempt);
        if (!copiesDone(*this)) {
            if (log) {
                EVR_LOG("xr: reconnect: the last copy to the headset has not finished; waiting");
            }
            continue;
        }
        destroyXrObjects();
        resetSessionState(*this);
        if (!tryReconnect(*this, log)) {
            destroyXrObjects();
            status::waiting(
                "the headset disconnected or its runtime stopped: reconnect it or start the runtime "
                "again (VR comes back by itself)");
            continue;
        }
        loss.lost = false;
        ++loss.reconnects;
        // The new LOCAL space may not be where the old one was: re-anchored as after a recenter.
        room.onSpaceChange(std::nullopt);
        menuReplace.store(true, std::memory_order_relaxed);
        consumerAlive.store(true);
        status::waiting("the headset is back; waiting for its session to start");
        EVR_LOG("xr: reconnected after %.1f s (attempt %u, reconnect %u); waiting for the session to start",
                qpcSeconds(qpcNow() - lostQpc), attempt + 1, loss.reconnects);
        return true;
    }
    return false;
}

} // namespace evr::vkcore
