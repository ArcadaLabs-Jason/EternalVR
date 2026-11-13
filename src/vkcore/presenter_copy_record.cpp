// The present hook's copy into the ring (presenter_copy.cpp): the command buffer that copies a presented game
// image into its ring slot (the eyes, the GUI target, the captures, the desktop mirror) and its submit on the
// game's queue, which signals the shared timeline.

#include "vkcore/presenter_impl.hpp"

#include "vkcore/presenter_eyes.hpp"
#include "vkcore/ui_engine.hpp"
#include "vkcore/view_snapshot.hpp"
#include "vkcore/vrs_nv.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace evr::vkcore {

bool XrPresenter::Impl::recordCopy(VkCommandBuffer cb,
                                   const SwapchainState& sc,
                                   VkImage source,
                                   std::uint32_t family,
                                   RingSlot& slot,
                                   const CopyTarget& target,
                                   VkBuffer captureBuffer) {
    const bool sameShape =
        sc.format == ringFormat && sc.extent.width == eyeExtent.width && sc.extent.height == eyeExtent.height;
    if (!sameShape) {
        const bool graphics =
            family < dev.queueFamilyFlags.size() && (dev.queueFamilyFlags[family] & VK_QUEUE_GRAPHICS_BIT);
        if (!graphics) {
            ++framesShapeMismatch; // a blit needs a graphics queue; this frame is not copied
            return false;
        }
        VkFormatProperties srcProps{};
        VkFormatProperties dstProps{};
        dev.instance->vk.GetPhysicalDeviceFormatProperties(dev.physicalDevice, sc.format, &srcProps);
        dev.instance->vk.GetPhysicalDeviceFormatProperties(dev.physicalDevice, ringFormat, &dstProps);
        // The blit filters linearly, which the source format must support too.
        if (!(srcProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
            !(srcProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) ||
            !(dstProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
            ++framesShapeMismatch;
            if (!loggedBlitUnsupported) {
                loggedBlitUnsupported = true;
                EVR_LOG("presenter: blit from format %d to %d unsupported; not copying", sc.format,
                        ringFormat);
            }
            return false;
        }
    }

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (dev.vk.ResetCommandBuffer(cb, 0) != VK_SUCCESS ||
        dev.vk.BeginCommandBuffer(cb, &begin) != VK_SUCCESS) {
        return false;
    }

    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    // Alternate eyes: the slot held for the next present takes the same image in the same half.
    const VkImage carry = target.carrySlot < kRingSize ? ring[target.carrySlot].image : VK_NULL_HANDLE;
    const std::uint32_t barriers = carry ? 3u : 2u;
    std::array<VkImageMemoryBarrier, 3> before{};
    // The swapchain image, written by the game (the present's wait semaphores are waited on by this
    // submit), moves to TRANSFER_SRC.
    before[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    before[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    before[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    before[0].oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    before[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    before[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before[0].image = source;
    before[0].subresourceRange = range;
    // The ring slot is acquired from the external (D3D12) side; its old contents are discarded, unless its
    // other half already holds this pair's first eye (released in GENERAL by that eye's copy).
    before[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    before[1].srcAccessMask = 0;
    before[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    before[1].oldLayout = target.keepOther ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
    before[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    before[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    before[1].dstQueueFamilyIndex = family;
    before[1].image = slot.image;
    before[1].subresourceRange = range;
    // The carry slot's old contents are discarded like a fresh slot's.
    before[2] = before[1];
    before[2].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    before[2].image = carry;
    dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                              nullptr, 0, nullptr, barriers, before.data());

    // The game's image into the slot's eyes (and with the eye copy, each eye from its own view:
    // presenter_eyes).
    recordEyeCopies(dev, cb, family, source, sc.extent, sameShape,
                    EyeCopyTarget{slot.image, carry, ringFormat, eyeExtent, target.firstEye, target.eyeCount,
                                  ringEyes == 2 && !settings.stereo.sequential && target.eyeCount == 2,
                                  target.snapshots});
    if (target.kind != stereo_seq::PresentKind::Mono) { // ETERNALVR_VRS_TINT: the eye's reduced-rate areas
        vrs_nv::recordMarks(dev, cb, family, {slot.image, carry}, ringFormat,
                            target.kind == stereo_seq::PresentKind::EyeR ? 1 : 0,
                            {static_cast<std::int32_t>(target.firstEye * eyeExtent.width), 0}, eyeExtent);
    }
    if (captureBuffer && target.captureEyeR) { // Parallel Eye Rendering: both eyes, as the headset gets them
        EyeCapture::recordRingEyes(dev, cb, slot.image, eyeExtent, {captureBuffer, target.captureEyeR});
    } else if (captureBuffer) {
        EyeCapture::record(dev, cb, source, sc.extent, captureBuffer);
    }
    // A menu over a head-tracked frame leaves the GUI out of the eye: the window shows the panel's image.
    const stereo_seq::PanelMirror panel =
        mirror.plan(menuUp.load(std::memory_order_relaxed) && ui_engine::skipComposite(), settings.mirror,
                    windowGate, target.kind, target.toWindow, target.mirror);
    // UI layer: the GUI target goes with a mono or eye L image; eye R keeps the pair's.
    if (target.ui) {
        slot.ui.written = recordUiCopy(cb, family, slot, panel.keep ? &sc : nullptr);
    } else if (!target.keepOther) {
        slot.ui.written = false;
    }
    // The depth layer: this eye's depth into the slot's shared buffer, in this command buffer
    // (presenter_depth.hpp).
    // Any other copy (alternate eyes, mono, Parallel Eye Rendering) leaves its slot, and a carry slot,
    // without depth, so an older pair's depth never goes out with this colour.
    if (depthLayer.active()) {
        const auto slotIndex = static_cast<std::uint32_t>(&slot - ring.data());
        depthLayer.beginSlot(slotIndex, target.keepOther && target.depth);
        if (target.depth) {
            depthLayer.record(dev, cb, family, slotIndex, slotIndex * 2 + (target.keepOther ? 1 : 0),
                              target.firstEye, target.eyeCount, target.depthTick);
        }
        if (target.carrySlot < kRingSize) {
            depthLayer.beginSlot(target.carrySlot, false);
        }
    }
    // The motion-vector capture: with eye R's copy, both eyes of the pair have rendered.
    motionCapture.record(dev, cb, family, target.kind == stereo_seq::PresentKind::EyeR);
    // The desktop mirror, after the eye is in the ring: keep this eye, or put the kept one in its place.
    const VkImageLayout sourceLayout =
        mirror.record(dev, cb, family, source, sc.format, sc.extent, panel.step, target.toWindow);

    std::array<VkImageMemoryBarrier, 3> after{};
    // The swapchain image goes back to PRESENT_SRC for the present.
    after[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    after[0].srcAccessMask = sourceLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                                 ? VK_ACCESS_TRANSFER_WRITE_BIT
                                 : VK_ACCESS_TRANSFER_READ_BIT;
    after[0].dstAccessMask = 0;
    after[0].oldLayout = sourceLayout;
    after[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    after[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    after[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    after[0].image = source;
    after[0].subresourceRange = range;
    // The ring slot is released to the external side in GENERAL (D3D12 COMMON).
    after[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    after[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    after[1].dstAccessMask = 0;
    after[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    after[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    after[1].srcQueueFamilyIndex = family;
    after[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    after[1].image = slot.image;
    after[1].subresourceRange = range;
    after[2] = after[1];
    after[2].image = carry;
    dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0,
                              nullptr, 0, nullptr, barriers, after.data());
    return dev.vk.EndCommandBuffer(cb) == VK_SUCCESS;
}

std::uint64_t XrPresenter::Impl::submitCopy(VkQueue queue,
                                            std::uint32_t family,
                                            const VkPresentInfoKHR* info,
                                            SwapchainState& sc,
                                            std::uint32_t imageIndex,
                                            FamilyCommands& fc,
                                            std::uint32_t slotIndex,
                                            const CopyTarget& target,
                                            VkBuffer captureBuffer) {
    RingSlot& slot = ring[slotIndex];
    const std::size_t buffer = std::size_t{slotIndex} * 2 + (target.keepOther ? 1 : 0);
    VkCommandBuffer cb = fc.buffers[buffer];
    if (!recordCopy(cb, sc, sc.images[imageIndex], family, slot, target, captureBuffer)) {
        uiCapture.cancel();
        uiBackdrop.cancel();
        motionCapture.cancel();
        depthLayer.submitted(static_cast<std::uint32_t>(buffer), 0, queue, timeline);
        return 0;
    }
    const std::uint64_t value = timelineValue + 1;
    // The game presents from more than one queue. Each copy also waits for the previous copy's timeline
    // value, so the shared timeline only ever moves forward, whichever queue signals it.
    std::vector<VkSemaphore> waits(info->pWaitSemaphores, info->pWaitSemaphores + info->waitSemaphoreCount);
    std::vector<VkPipelineStageFlags> waitStages(info->waitSemaphoreCount,
                                                 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    std::vector<std::uint64_t> waitValues(info->waitSemaphoreCount, 0);
    if (timelineValue > 0) {
        waits.push_back(timeline);
        waitStages.push_back(VK_PIPELINE_STAGE_TRANSFER_BIT);
        waitValues.push_back(timelineValue);
    }
    for (const view_snapshot::Copy& c : target.snapshots) {
        if (c.semaphore) { // the eye snapshots' copies it reads (view_snapshot.hpp)
            waits.push_back(c.semaphore);
            waitStages.push_back(VK_PIPELINE_STAGE_TRANSFER_BIT);
            waitValues.push_back(c.wait);
        }
    }
    // A present handed back never waits on its semaphore: the copy leaves it unsignalled.
    const std::array<VkSemaphore, 2> signals{sc.presentSemaphores[imageIndex], timeline};
    const std::array<std::uint64_t, 2> signalValues{0, value};
    const std::uint32_t firstSignal = target.toWindow ? 0u : 1u;
    VkTimelineSemaphoreSubmitInfo timelineInfo{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    timelineInfo.waitSemaphoreValueCount = static_cast<std::uint32_t>(waitValues.size());
    timelineInfo.pWaitSemaphoreValues = waitValues.data();
    timelineInfo.signalSemaphoreValueCount = static_cast<std::uint32_t>(signalValues.size()) - firstSignal;
    timelineInfo.pSignalSemaphoreValues = signalValues.data() + firstSignal;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO, &timelineInfo};
    submit.waitSemaphoreCount = static_cast<std::uint32_t>(waits.size());
    submit.pWaitSemaphores = waits.data();
    submit.pWaitDstStageMask = waitStages.data();
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cb;
    submit.signalSemaphoreCount = static_cast<std::uint32_t>(signals.size()) - firstSignal;
    submit.pSignalSemaphores = signals.data() + firstSignal;
    const VkResult submitted = dev.vk.QueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
    if (submitted != VK_SUCCESS) {
        uiCapture.cancel();
        uiBackdrop.cancel();
        motionCapture.cancel();
        depthLayer.submitted(static_cast<std::uint32_t>(buffer), 0, queue, timeline);
        copyFailed = true;
        EVR_LOG("presenter: vkQueueSubmit failed (%d); copies off, presents pass through", submitted);
        return 0;
    }
    timelineValue = value;
    lastSubmittedValue = value;
    if (target.ui) {
        uiCapture.submitted(value);
        uiBackdrop.submitted(value);
    }
    motionCapture.submitted(value); // only after a record
    depthLayer.submitted(static_cast<std::uint32_t>(buffer), value, queue, timeline);
    fc.lastValue[buffer] = value;
    slot.value.store(value);
    if (target.carrySlot < kRingSize) {
        ring[target.carrySlot].value.store(value); // held (kSlotWriting) until the next present completes it
    }
    return value;
}

} // namespace evr::vkcore
