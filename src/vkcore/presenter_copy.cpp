// The present hook: copies each presented game image into a free ring slot on the game's queue (T-040,
// T-081, T-091) and hands the present its own semaphore.

#include "vkcore/presenter_impl.hpp"

#include "stereo_seq/seq_settings.hpp"
#include "vkcore/gpu_timing.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/presenter_eyes.hpp"
#include "vkcore/presenter_result.hpp"
#include "vkcore/runtime_cvars.hpp"
#include "vkcore/stall_watch.hpp"
#include "vkcore/ui_engine.hpp"
#include "vkcore/view_slots.hpp"
#include "vkcore/virtual_client.hpp"
#include "vkcore/vrs_nv.hpp"
#include "vkcore/window_timing.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace evr::vkcore {

namespace {
// The game's first swapchain size; a later one of another size is logged (under the presenter's mutex).
stereo_seq::LaunchSizeWatch g_launchSize;
} // namespace

// ---------------------------------------------------------------------------------------------------
// Vulkan side

void XrPresenter::Impl::onSwapchainCreated(VkSwapchainKHR swapchain, const VkSwapchainCreateInfoKHR& info) {
    std::lock_guard lock(mutex);
    SwapchainState state;
    state.format = info.imageFormat;
    state.extent = info.imageExtent;
    std::uint32_t count = 0;
    dev.vk.GetSwapchainImagesKHR(dev.device, swapchain, &count, nullptr);
    state.images.resize(count);
    dev.vk.GetSwapchainImagesKHR(dev.device, swapchain, &count, state.images.data());
    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (std::uint32_t i = 0; i < count; ++i) {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        if (dev.vk.CreateSemaphore(dev.device, &semInfo, nullptr, &semaphore) != VK_SUCCESS) {
            EVR_LOG("presenter: vkCreateSemaphore failed; this swapchain is not copied");
            for (VkSemaphore s : state.presentSemaphores) {
                dev.vk.DestroySemaphore(dev.device, s, nullptr);
            }
            return;
        }
        state.presentSemaphores.push_back(semaphore);
    }
    if ((info.imageUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0) {
        EVR_LOG("presenter: swapchain usage lacks TRANSFER_SRC; not copied");
        for (VkSemaphore s : state.presentSemaphores) {
            dev.vk.DestroySemaphore(dev.device, s, nullptr);
        }
        return;
    }
    EVR_LOG("presenter: game swapchain %p, %u image(s)", reinterpret_cast<void*>(swapchain), count);
    if (info.imageColorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        EVR_LOG("presenter: WARNING the game presents in colour space %d (HDR); the headset image will look "
                "wrong. Launch with +r_hdrDisplay 0 (T-080).",
                info.imageColorSpace);
    }
    // The expected size is the render size while the layer answers the client area with it, else the launch
    // size.
    const auto render = virtual_client::activeExtent();
    const auto expected =
        render ? std::optional<stereo_seq::SwapchainSize>({render->width, render->height}) : std::nullopt;
    if (const auto ratio =
            g_launchSize.onSwapchain({info.imageExtent.width, info.imageExtent.height}, expected)) {
        EVR_LOG("presenter: WARNING the game's swapchain is %ux%u, not the expected %ux%u: %.2fx the pixels "
                "per eye (the game changed its video mode; runtime_cvars holds the window cvars)",
                info.imageExtent.width, info.imageExtent.height, g_launchSize.reference().width,
                g_launchSize.reference().height, *ratio);
    }
    swapchains[swapchain] = std::move(state);
    gameSwapchain = swapchain;

    // The worker sizes the ring from the newest shape; once a ring exists, a different shape rebuilds it
    // (and a change while it is being built is caught when the build ends, requestRebuildIfStale).
    requestedExtent = info.imageExtent;
    requestedFormat = info.imageFormat;
    if (workerStarted && ringBuilt) {
        requestRebuildIfStale();
    }
    if (!workerStarted) {
        workerStarted = true;
        workerDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        // The worker holds its own reference: if it ever has to be left behind at shutdown, the state
        // it uses stays alive (and the DLL is pinned) until it finishes.
        worker = std::thread([self = shared_from_this()] { self->runWorker(); });
    }
}

void XrPresenter::Impl::onSwapchainDestroyed(VkSwapchainKHR swapchain) {
    std::lock_guard lock(mutex);
    const auto it = swapchains.find(swapchain);
    if (it == swapchains.end()) {
        return;
    }
    // A present may still be waiting on these semaphores; they go once later copies have completed.
    for (VkSemaphore s : it->second.presentSemaphores) {
        retiredSemaphores.retire(s, timelineValue + kRetireAfterCopies);
    }
    swapchains.erase(it);
    dropHeldImages(swapchain);
    if (gameSwapchain == swapchain) {
        gameSwapchain = VK_NULL_HANDLE;
    }
}

FamilyCommands* XrPresenter::Impl::commandsFor(std::uint32_t family) {
    const auto it = commands.find(family);
    if (it != commands.end()) {
        return &it->second;
    }
    FamilyCommands fc;
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = family;
    if (dev.vk.CreateCommandPool(dev.device, &poolInfo, nullptr, &fc.pool) != VK_SUCCESS) {
        return nullptr;
    }
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = fc.pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = static_cast<std::uint32_t>(fc.buffers.size());
    if (dev.vk.AllocateCommandBuffers(dev.device, &alloc, fc.buffers.data()) != VK_SUCCESS) {
        dev.vk.DestroyCommandPool(dev.device, fc.pool, nullptr);
        return nullptr;
    }
    for (VkCommandBuffer cb : fc.buffers) {
        // Command buffers made by a layer need the loader's dispatch pointer.
        dev.setDeviceLoaderData(dev.device, cb);
    }
    EVR_LOG("presenter: command pool for queue family %u", family);
    return &commands.emplace(family, fc).first->second;
}

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
                                  ringEyes == 2 && !settings.stereo.sequential && target.eyeCount == 2});
    if (target.kind != stereo_seq::PresentKind::Mono) { // ETERNALVR_VRS_TINT: the eye's reduced-rate areas
        vrs_nv::recordMarks(dev, cb, family, {slot.image, carry}, ringFormat,
                            target.kind == stereo_seq::PresentKind::EyeR ? 1 : 0,
                            {static_cast<std::int32_t>(target.firstEye * eyeExtent.width), 0}, eyeExtent);
    }
    if (captureBuffer) {
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
    fc.lastValue[buffer] = value;
    slot.value.store(value);
    if (target.carrySlot < kRingSize) {
        ring[target.carrySlot].value.store(value); // held (kSlotWriting) until the next present completes it
    }
    return value;
}

VkSemaphore XrPresenter::Impl::copyForPresent(VkQueue queue,
                                              std::uint32_t family,
                                              const VkPresentInfoKHR* info,
                                              VkSwapchainKHR& copiedSwapchain,
                                              std::uint32_t& copiedImage) {
    windowHold = false;
    if (gameSwapchain == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    std::uint32_t index = info->swapchainCount;
    for (std::uint32_t i = 0; i < info->swapchainCount; ++i) {
        if (info->pSwapchains[i] == gameSwapchain) {
            index = i;
            break;
        }
    }
    if (index == info->swapchainCount) {
        return VK_NULL_HANDLE;
    }
    // Route S: every present of the game's swapchain takes its eye tag, copied or not, so that the tags
    // stay in step with the presents.
    const bool seq = seqActive.load(std::memory_order_acquire);
    if (seq) { // runtime_cvars.hpp: from Route S's first present, before the runtime's session
        runtime_cvars::apply(true);
    } else if (parallelEyesChangedEngine()) { // the same for Parallel Eye Rendering's set, every frame
        runtime_cvars::apply(false);
    }
    const stereo_seq::PresentMatch match = seq ? seqTakePresent() : stereo_seq::PresentMatch{};
    if (seq && match.tagged) {
        gpu_timing::tagEye(match.tag.eye);
    }
    const auto notCopied = [&]() -> VkSemaphore {
        if (seq) {
            seqPresentNotCopied(match);
        }
        return VK_NULL_HANDLE;
    };
    if (!ringReady.load() || !consumerAlive.load() || copyFailed) {
        return notCopied();
    }
    SwapchainState& sc = swapchains[gameSwapchain];
    const std::uint32_t imageIndex = info->pImageIndices[index];
    if (imageIndex >= sc.images.size()) {
        return notCopied();
    }
    // In a level the game presents from its compute queue (family 2 on the rig), whose last write to
    // the swapchain image leaves it owned by that family. A copy works on any queue with transfer
    // support (graphics and compute queues included); a blit needs a graphics queue (recordCopy and the
    // desktop mirror's crop and panel check the family).
    const VkQueueFlags familyFlags = family < dev.queueFamilyFlags.size() ? dev.queueFamilyFlags[family] : 0;
    if (!(familyFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT))) {
        return notCopied();
    }
    if (family != lastPresentFamily) {
        EVR_LOG("presenter: game presents from queue family %u (flags 0x%x)", family, familyFlags);
        lastPresentFamily = family;
    }
    FamilyCommands* fc = commandsFor(family);
    if (!fc) {
        EVR_LOG("presenter: no command pool for family %u; copies off", family);
        copyFailed = true;
        return notCopied();
    }

    std::uint64_t completed = 0;
    if (dev.vk.GetSemaphoreCounterValueKHR(dev.device, timeline, &completed) != VK_SUCCESS) {
        completed = 0;
    }
    if (!retiredSemaphores.empty()) {
        retiredSemaphores.collect(completed,
                                  [this](VkSemaphore s) { dev.vk.DestroySemaphore(dev.device, s, nullptr); });
    }
    uiCapture.poll(dev, completed);
    uiBackdrop.poll(dev, completed);
    motionCapture.poll(dev, completed);
    logUiStats();
    handBackImages(completed);

    if (seq) {
        const VkSemaphore semaphore =
            seqCopyForPresent(queue, family, sc, imageIndex, info, *fc, completed, match);
        if (semaphore != VK_NULL_HANDLE) {
            logCopyStats(sc, family);
            copiedSwapchain = gameSwapchain;
            copiedImage = imageIndex;
        }
        return semaphore;
    }
    const std::uint32_t slotIndex = acquireFreeSlot(*fc, completed);
    if (slotIndex == kRingSize) {
        ++framesDropped;
        return VK_NULL_HANDLE;
    }
    const bool toWindow = decideWindow(info, stereo_seq::PresentKind::Mono);
    // Parallel Eye Rendering's eye copy (presenter_stereo.cpp): a two-eye ring outside Route S.
    const bool eyeCopy = ringEyes == 2 && !settings.stereo.sequential;
    const VkBuffer buffer = monoCaptureBuffer(sc, completed); // the in-headset capture (bug_capture.hpp)
    const std::uint64_t value = submitCopy(
        queue, family, info, sc, imageIndex, *fc, slotIndex,
        CopyTarget{0, eyeCopy ? 2u : 1u, false, settings.ui.enabled, stereo_seq::MirrorStep::None, toWindow},
        buffer);
    captureCopied(buffer, value, true);
    if (value == 0) {
        ring[slotIndex].state.store(kSlotFree);
        ++framesDropped;
        return VK_NULL_HANDLE;
    }
    RingSlot& slot = ring[slotIndex];
    slot.hasView = false;
    if (settings.mode == Mode::HeadTracked && mp_guard::allowsGameTouch()) {
        std::uint64_t gap = 0;
        slot.hasView = latestView(slot.view, gap);
        if (eyeCopy) {
            slot.view.showEyes = slot.hasView && slot.view.stereo; // the eye copy's two-eye ring
        }
        if (slot.hasView) {
            ++presentsWithView;
            presentSeqGapSum += gap;
        } else {
            ++presentsWithoutView;
        }
        if (loggedPresents < 12) {
            ++loggedPresents;
            EVR_LOG("present: timeline %llu carries view %llu (%s, %llu frame(s) behind the newest)",
                    static_cast<unsigned long long>(value),
                    static_cast<unsigned long long>(slot.hasView ? slot.view.seq : 0),
                    slot.hasView ? "head-tracked" : "none", static_cast<unsigned long long>(gap));
        }
    }
    publishSlot(slotIndex, value);
    logCopyStats(sc, family);

    copiedSwapchain = gameSwapchain;
    copiedImage = imageIndex;
    return sc.presentSemaphores[imageIndex];
}

void XrPresenter::Impl::replacePresentSemaphore(VkSwapchainKHR swapchain,
                                                std::uint32_t image,
                                                VkSemaphore used) {
    const auto it = swapchains.find(swapchain);
    if (it == swapchains.end() || image >= it->second.presentSemaphores.size() ||
        it->second.presentSemaphores[image] != used) {
        return;
    }
    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore fresh = VK_NULL_HANDLE;
    if (dev.vk.CreateSemaphore(dev.device, &semInfo, nullptr, &fresh) != VK_SUCCESS) {
        copyFailed = true;
        EVR_LOG("presenter: cannot replace a present semaphore; copies off");
        return;
    }
    retiredSemaphores.retire(used, timelineValue + kRetireAfterCopies);
    it->second.presentSemaphores[image] = fresh;
}

VkResult XrPresenter::Impl::present(VkQueue queue, std::uint32_t family, const VkPresentInfoKHR* info) {
    VkSwapchainKHR copiedSwapchain = VK_NULL_HANDLE;
    std::uint32_t copiedImage = 0;
    VkSemaphore wait = VK_NULL_HANDLE;
    bool hold = false;
    gamePresents.fetch_add(1, std::memory_order_relaxed);
    {
        const std::uint64_t waitStart = window_timing::nowMicros();
        std::lock_guard lock(mutex); // also the XR worker's (ring rebuilds) and swapchain creation's
        stall_watch::addLockWait(window_timing::nowMicros() - waitStart);
        if (!loggedFirstPresent) {
            loggedFirstPresent = true;
            EVR_LOG("presenter: first game present");
        }
        wait = copyForPresent(queue, family, info, copiedSwapchain, copiedImage);
        hold = wait != VK_NULL_HANDLE && windowHold;
        if (hold) {
            holdImage(copiedSwapchain, copiedImage, lastSubmittedValue);
        }
    }
    // The downstream present runs without our lock: it can re-enter the driver's swapchain code.
    const auto timed = [this, queue](const VkPresentInfoKHR* present) {
        const std::uint64_t start = window_timing::nowMicros();
        const VkResult r = dev.vk.QueuePresentKHR(queue, present);
        const std::uint64_t took = window_timing::nowMicros() - start;
        window_timing::addPresent(took);
        stall_watch::addDriverPresent(took);
        return r;
    };
    if (wait == VK_NULL_HANDLE) {
        return windowPresented(*this, timed(info), info, VK_NULL_HANDLE, 0, VK_NULL_HANDLE);
    }
    if (hold) {
        // Not presented: the image goes back to the swapchain once its copy is done (presenter_window.cpp).
        if (info->pResults) {
            info->pResults[0] = VK_SUCCESS;
        }
        return VK_SUCCESS;
    }
    VkPresentInfoKHR replaced = *info;
    replaced.waitSemaphoreCount = 1;
    replaced.pWaitSemaphores = &wait;
    // A failed present gets a new semaphore, out of date hands the held images back (presenter_result.hpp).
    return windowPresented(*this, timed(&replaced), info, copiedSwapchain, copiedImage, wait);
}

} // namespace evr::vkcore
