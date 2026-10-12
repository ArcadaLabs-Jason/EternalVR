#pragma once

// The window surface's present scaling for the render size (virtual_client.hpp,
// docs/rig-findings/render-size.md): what the surface allows over every present mode the game may use, and
// the behaviour and gravity the game's swapchain is created with.

#include "features/render_size/render_size.hpp"
#include "vkcore/dispatch.hpp"

#include <cstdint>
#include <optional>

namespace evr::vkcore::present_scaling {

// What the surface allows for present scaling, over every present mode the game may use.
struct Scaling {
    VkPresentScalingFlagsKHR behavior = 0;
    VkPresentGravityFlagsKHR gravity = 0;
    render_size::Extent minScaled{0, 0};
    render_size::Extent maxScaled{UINT32_MAX, UINT32_MAX};
};

// The surface's present scaling over the present modes the game may use (FIFO, immediate, mailbox); nullopt
// without the instance's surface maintenance extension or when no such mode is offered.
std::optional<Scaling> query(const InstanceData& inst, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);

// The scaling the swapchain is created with: the whole image letterboxed into the window (aspect kept),
// else stretched, else one to one (a crop). 0 when none is offered. With the whole image the window has the
// eye's shape (virtual_client's mirror placement), so no bars show, and a driver that stretches the first
// presents regardless (the rig's NVIDIA driver did until the window was minimised and restored) shows the
// same image.
VkPresentScalingFlagsKHR chooseBehavior(const Scaling& s);

// Centred when offered, else the minimum edge, else 0.
VkPresentGravityFlagsKHR chooseGravity(const Scaling& s);

} // namespace evr::vkcore::present_scaling
