#pragma once

// What the game's presents that reach the driver return, and whether they keep coming (docs/VR_STEREO.md,
// Desktop window).
//
// VK_ERROR_OUT_OF_DATE_KHR (the window changed size: AMD's driver returns it where NVIDIA's returns
// VK_SUBOPTIMAL_KHR) or VK_ERROR_SURFACE_LOST_KHR: the images the window gate holds (presenter_window.cpp) go
// back to the swapchain at once, after their ring copies finish, instead of at the game's next present. A
// game that recreates its swapchain, or that waits in vkAcquireNextImageKHR for a free image, may not present
// again first, and the held images would then never go back. The image's present semaphore is replaced too,
// in case the driver did not run its wait. The first such result is logged with what was handed back.
//
// ETERNALVR_TEST_PRESENT_OUT_OF_DATE=<seconds> (a test knob, never set by the launcher): the first present
// that reaches the driver at least that many seconds after the first one returns VK_ERROR_OUT_OF_DATE_KHR
// to the game (the driver took it as usual), so the game's recreate and the layer's handling run on any GPU.
// Only while the XR presenter is in the present path (VR on).

#include "vkcore/xr_presenter.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace evr::vkcore {

// Present hook, after the driver's present (and without the presenter's lock): the result the game gets.
// `wait` is the semaphore the present waited on instead of the game's (VK_NULL_HANDLE: the present went out
// unchanged), for image `image` of `swapchain`.
VkResult windowPresented(XrPresenter::Impl& p,
                         VkResult result,
                         const VkPresentInfoKHR* info,
                         VkSwapchainKHR swapchain,
                         std::uint32_t image,
                         VkSemaphore wait);

// XR worker, with the 10 s rates line: a `present:` line once no game present has come for a few seconds
// while the headset runs ("the game has stopped presenting": the headset repeats the last image), with the
// newest failed present result and the images the window gate holds, and another when presents come again.
void checkGamePresents(XrPresenter::Impl& p);

} // namespace evr::vkcore
