// The present hook: copies each presented game image into a free ring slot on the game's queue (T-040,
// T-081, T-091) and hands the present its own semaphore. The copy is recorded and submitted in
// presenter_copy_record.cpp.

#include "vkcore/presenter_impl.hpp"

#include "common/finishes.hpp"
#include "stereo_seq/seq_settings.hpp"
#include "vkcore/gpu_timing.hpp"
#include "vkcore/hook_guard.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/presenter_result.hpp"
#include "vkcore/runtime_cvars.hpp"
#include "vkcore/stall_watch.hpp"
#include "vkcore/status_file.hpp"
#include "vkcore/view_slots.hpp"
#include "vkcore/view_snapshot.hpp"
#include "vkcore/virtual_client.hpp"
#include "vkcore/window_timing.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace evr::vkcore {

namespace {
// The game's first swapchain size; a later one of another size is logged (under the presenter's mutex).
stereo_seq::LaunchSizeWatch g_launchSize;
// The present hook's copy (present()).
HookGuard g_copyGuard("the present hook's copy into the ring");
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
    // An exception must not leave vkCreateSwapchainKHR: without the memory for the swapchain's lists (or its
    // entry below) it is not copied, as without its semaphores.
    if (!finishes([&] {
            state.images.resize(count);
            state.presentSemaphores.reserve(count); // the pushes below do not allocate
        })) {
        EVR_LOG("presenter: no memory for the swapchain's image list; this swapchain is not copied");
        return;
    }
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
    if (!finishes([&] { swapchains[swapchain] = std::move(state); })) {
        EVR_LOG("presenter: no memory to keep swapchain %p; not copied", reinterpret_cast<void*>(swapchain));
        for (VkSemaphore s : state.presentSemaphores) { // never used
            dev.vk.DestroySemaphore(dev.device, s, nullptr);
        }
        return;
    }
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
        // it uses stays alive (and the DLL is pinned) until it finishes. Without a thread for it (it would
        // throw into vkCreateSwapchainKHR) the ring is never built: the game's presents pass through.
        if (!finishes([this] { worker = std::thread([self = shared_from_this()] { self->runWorker(); }); })) {
            EVR_LOG("presenter: no thread for the XR worker; the game runs flat");
            status::flat("VR could not start: no thread for the headset's worker");
        }
    }
}

void XrPresenter::Impl::onSwapchainDestroyed(VkSwapchainKHR swapchain) {
    std::lock_guard lock(mutex);
    const auto it = swapchains.find(swapchain);
    if (it == swapchains.end()) {
        return;
    }
    // A present may still be waiting on these semaphores; they go once later copies have completed. One there
    // is no memory to queue is left alive (never destroyed under a present); the swapchain goes regardless.
    if (!finishes([&] {
            for (VkSemaphore s : it->second.presentSemaphores) {
                retiredSemaphores.retire(s, timelineValue + kRetireAfterCopies);
            }
        })) {
        EVR_LOG("presenter: no memory to retire the semaphores of swapchain %p; left alive",
                reinterpret_cast<void*>(swapchain));
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
        // Copies off or the headset gone with the ring still there (its timeline lives while it is ready,
        // under this lock): images held from the window still go back once their copies are done, or the
        // game's swapchain would run out of images.
        std::uint64_t done = 0;
        if (held_images::handsBackUncopied(!heldImages.empty(), ringReady.load()) &&
            dev.vk.GetSemaphoreCounterValueKHR(dev.device, timeline, &done) == VK_SUCCESS) {
            handBackImages(done);
        }
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
    depthLayer.poll(dev, completed); // the depth layer: GPU times, views no longer in use
    logUiStats();
    handBackImages(completed);

    if (seq) {
        const std::uint64_t dropped = framesDropped;
        const VkSemaphore semaphore =
            seqCopyForPresent(queue, family, sc, imageIndex, info, *fc, completed, match);
        if (framesDropped != dropped) { // no slot free, the copy not submitted or the pair's record missing
            frameLogEyes.lastPresent.store(FrameRepeat::NotHanded);
        }
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
        frameLogEyes.lastPresent.store(FrameRepeat::NotHanded);
        return VK_NULL_HANDLE;
    }
    // Parallel Eye Rendering's eye copy (presenter_stereo.cpp): a two-eye ring outside Route S.
    const bool eyeCopy = ringEyes == 2 && !settings.stereo.sequential;
    const std::optional<view_snapshot::Pick> snapshot =
        eyeCopy && mp_guard::allowsGameTouch()
            ? view_snapshot::forPresent(info->pWaitSemaphores, info->waitSemaphoreCount,
                                        sc.images[imageIndex])
            : std::nullopt;
    if (snapshot && snapshot->drop) { // no new pair (or the guess's pair a frame apart): the headset keeps
        view_snapshot::notRead(*snapshot); // the last one
        frameLogEyes.lastPresent.store(snapshot->dropWhy);
        ring[slotIndex].state.store(kSlotFree);
        return VK_NULL_HANDLE;
    }
    const bool toWindow = decideWindow(info, stereo_seq::PresentKind::Mono);
    // The in-headset capture (bug_capture.hpp): with the eye copy, both halves of the slot.
    const std::array<VkBuffer, 2> buffers =
        eyeCopy && ring[slotIndex].copySource
            ? ringCaptureBuffers(sc, completed, snapshot ? snapshot->view : 0)
            : std::array<VkBuffer, 2>{monoCaptureBuffer(sc, completed)};
    CopyTarget target{0, eyeCopy ? 2u : 1u, false, settings.ui.enabled};
    target.toWindow = toWindow;
    target.captureEyeR = buffers[1];
    if (OneShot* shot = buffers[0] ? capture.once() : nullptr; shot && shot->frames > 1 && eyeCopy) {
        shot->sidecar += view_snapshot::describe(snapshot); // which copy eye 1 of this burst frame shows
    }
    if (snapshot) {
        target.snapshots = snapshot->eyes;
    }
    const std::uint64_t value =
        submitCopy(queue, family, info, sc, imageIndex, *fc, slotIndex, target, buffers[0]);
    if (snapshot && value != 0) {
        view_snapshot::read(*snapshot, timeline, value);
    } else if (snapshot) {
        view_snapshot::notRead(*snapshot);
    }
    captureCopied(buffers[0], value, true);
    if (value == 0) {
        ring[slotIndex].state.store(kSlotFree);
        ++framesDropped;
        frameLogEyes.lastPresent.store(FrameRepeat::NotHanded);
        return VK_NULL_HANDLE;
    }
    RingSlot& slot = ring[slotIndex];
    slot.eyes = eyeFramesOf(snapshot);
    slot.hasView = false;
    if (settings.mode == Mode::HeadTracked && mp_guard::allowsGameTouch()) {
        std::uint64_t gap = 0;
        slot.hasView = viewForPresent(snapshot ? snapshot->view : 0, slot.view, gap);
        if (eyeCopy) {
            slot.view.showEyes = slot.hasView && slot.view.stereo; // the eye copy's two-eye ring
            // A Parallel Eye pair, counted for the 10 s rates line (the launcher's game frame rate).
            pairsPublished.fetch_add(slot.view.showEyes ? 1 : 0, std::memory_order_relaxed);
        }
        if (slot.hasView) {
            ++presentViews.withView;
            presentViews.seqGapSum += gap;
        } else {
            ++presentViews.withoutView;
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
    if (!finishes([&] { retiredSemaphores.retire(used, timelineValue + kRetireAfterCopies); })) {
        EVR_LOG("presenter: no memory to retire a present semaphore; left alive"); // never destroyed in use
    }
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
            firstGamePresent();
        }
        // An exception must not leave the present hook (hook_guard.hpp). The copy does not throw once its
        // submit has waited on the game's semaphores, so one thrown before leaves the game's present as it
        // came, and copies off for the rest of the session (the frame's slot, eye pairing and captures were
        // left half done), as when the submit fails.
        if (!g_copyGuard.run(
                [&] { wait = copyForPresent(queue, family, info, copiedSwapchain, copiedImage); })) {
            copyFailed = true;
        }
        hold = wait != VK_NULL_HANDLE && windowHold;
        if (hold) {
            holdImage(copiedSwapchain, copiedImage, lastSubmittedValue); // does not allocate (decideWindow)
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
