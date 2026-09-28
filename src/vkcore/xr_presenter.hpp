#pragma once

// The OpenXR presenter: the game's presented frame in the headset.
//
// Data path (ARCHITECTURE section 6, T-040, T-080, T-081): a 3-slot ring of images created on our own
// D3D12 device (the OpenXR runtime's adapter) and imported into the game's Vulkan device, plus a
// shared D3D12 fence imported as a Vulkan timeline semaphore. The present hook copies the presented
// swapchain image into a free slot on the game's queue and signals the timeline; the XR worker waits
// for the newest written slot on its D3D12 queue and copies it into the XR swapchain.
//
// Modes (ETERNALVR_MODE):
// - "head" (default): head-tracked mono view (docs/VR_HEAD_TRACKED.md). The engine camera hook
//   replaces the game view's orientation with the headset's each game frame and sets the game's FOV to
//   one that covers both eyes; the frame is submitted as a projection layer whose two views carry the
//   pose and FOV that frame was rendered with, so the runtime reprojects it correctly. Frames without
//   a head-tracked view (menus, loading, before tracking starts) are shown on the cinema quad.
// - "cinema": the first-light flat screen, a quad 2.5 m ahead; the game's camera is not touched.
//
// Deviation from T-082 and section 6.1: the OpenXR instance, system and session live on a worker thread
// started at the game's first vkCreateSwapchainKHR, and the XR frame loop runs on that thread,
// decoupled from the game's frames. Nothing OpenXR runs inside vkCreateInstance or the present hook,
// so a slow or absent runtime can never stall the game. The camera hook only calls xrLocateSpace.

#include "vkcore/dispatch.hpp"

#include <memory>

namespace evr::vkcore {

class XrPresenter {
public:
    explicit XrPresenter(DeviceData& device);
    ~XrPresenter();
    XrPresenter(const XrPresenter&) = delete;
    XrPresenter& operator=(const XrPresenter&) = delete;

    void onSwapchainCreated(VkSwapchainKHR swapchain, const VkSwapchainCreateInfoKHR& info);
    void onSwapchainDestroyed(VkSwapchainKHR swapchain);

    // Replaces vkQueuePresentKHR for the game's device. Never waits on the XR side.
    VkResult present(VkQueue queue, std::uint32_t queueFamily, const VkPresentInfoKHR* presentInfo);

    // Stops the XR worker and frees every object; called before the device is destroyed.
    void shutdown();

    struct Impl;

private:
    std::shared_ptr<Impl> impl_;
};

// Marks the calling thread as one whose Vulkan instances pass through the layer untouched.
void setPassThroughThread(bool passThrough);

// The game's window, recorded when the game creates its Win32 surface (nullptr until then).
void setGameWindow(void* hwnd);

// True once the process is exiting (DLL_PROCESS_DETACH at process exit): no teardown may run then.
bool processTerminating();

} // namespace evr::vkcore
