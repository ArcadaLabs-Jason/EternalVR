#pragma once

// The copy of a presented image into a ring slot's eyes (presenter_copy.cpp), and Parallel Eye Rendering's
// eye copy: in the two-view renderer each eye takes its own view's image instead of the presented one. By
// default each view's screen pass output: eye 0 the presented image (view 0's), eye 1 view 1's clone of the
// screen target (view_clones.hpp). ETERNALVR_TEST_EYE_COPY=1 (experiments) takes each view's image before the
// screen pass instead: view 0's from the engine's post-process final target (RVA 0x66E3208), view 1's from
// its clone; =0 leaves both eyes the presented image. Each such image is followed by the UI layer's Vulkan
// hooks (ui_vulkan.hpp, which Parallel Eye Rendering needs on), moved to TRANSFER_SRC and back to the layout
// the game left it in; another format (the engine's final images are B10G11R11 float) is blitted. An eye
// whose image cannot be copied (not followed yet, another size than the eye, no blit for its format, owned by
// another queue family) takes the presented image, and the reason is logged; so does eye 1 on a frame that
// did not render view 1 (view_slots.hpp viewSlotsView1Rendered).

#include "vkcore/dispatch.hpp"
#include "vkcore/view_snapshot.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

namespace evr::vkcore {

// Parallel Eye Rendering is on with an eye copy (not ETERNALVR_TEST_EYE_COPY=0).
bool eyeCopyRequested();

struct EyeCopyTarget {
    VkImage slot = VK_NULL_HANDLE;  // the ring slot, in TRANSFER_DST
    VkImage carry = VK_NULL_HANDLE; // alternate eyes: the slot held for the next present, or null
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D eyeExtent{};
    std::uint32_t firstEye = 0;
    std::uint32_t eyeCount = 1;
    bool viewImages = false; // the eye copy: each eye from its own view's final image when it can
    // Eye 1 from its copy when set (TRANSFER_SRC; view_snapshot.hpp); both set, a pair: each eye from its
    // own.
    std::array<view_snapshot::Copy, 2> snapshots{};
};

// Records the copies (same shape) or blits (another size or format, graphics queue only) of `source`, in
// TRANSFER_SRC, into the target's eyes.
void recordEyeCopies(DeviceData& dev,
                     VkCommandBuffer cb,
                     std::uint32_t family,
                     VkImage source,
                     VkExtent2D sourceExtent,
                     bool sameShape,
                     const EyeCopyTarget& target);

} // namespace evr::vkcore
