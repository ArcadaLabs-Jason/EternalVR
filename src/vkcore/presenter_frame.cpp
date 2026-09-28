// The XR worker: its thread body and the frame loop (one image per XR frame, the projection layer for
// head-tracked images and the cinema quad otherwise).

#include "vkcore/presenter_impl.hpp"

#include "vkcore/controllers.hpp"
#include "vkcore/gpu_timing.hpp"
#include "vkcore/head_sweep.hpp"
#include "vkcore/keep_active.hpp"
#include "vkcore/key_inject.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/status_file.hpp"
#include "vkcore/test_keys.hpp"
#include "vkcore/ui_engine.hpp"
#include "xr_math/cinema_quad.hpp"
#include "xr_math/enclosing_fov.hpp"

#include <array>
#include <cstddef>
#include <utility>

namespace evr::vkcore {

void XrPresenter::Impl::updateImage() {
    if (copyStalled) {
        if (copyFence->GetCompletedValue() < copyFenceValue) {
            ++xrRepeats;
            return;
        }
        copyStalled = false;
        EVR_LOG("d3d12: the held copy finished; copies resume");
        completeCopy(stalledSlot, stalledValue, stalledView, stalledHasView);
        return;
    }
    const std::uint64_t packed = latest.load();
    const std::uint64_t newest = packed >> 2;
    if (newest <= lastConsumed) {
        ++xrRepeats;
        return;
    }
    // OpenXR swapchain rules (T-081): after a timeout the image stays acquired and is waited on again
    // next frame; it is released only after a successful wait.
    if (acquiredIndex < 0) {
        std::uint32_t index = 0;
        if (XR_FAILED(xr.xrAcquireSwapchainImage(xrSwapchain, nullptr, &index))) {
            return;
        }
        acquiredIndex = index;
        acquiredWaited = false;
    }
    if (!acquiredWaited) {
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = kSwapchainWaitTimeout;
        const XrResult r = xr.xrWaitSwapchainImage(xrSwapchain, &wait);
        if (r == XR_TIMEOUT_EXPIRED) {
            EVR_LOG("xr: swapchain image wait timed out; repeating the last image");
            return;
        }
        if (XR_FAILED(r)) {
            return;
        }
        acquiredWaited = true;
    }

    const std::uint32_t slotIndex = static_cast<std::uint32_t>(packed & 3u);
    RingSlot& slot = ring[slotIndex];
    int expected = kSlotFree;
    if (!slot.state.compare_exchange_strong(expected, kSlotReading)) {
        return; // being written right now; the image stays acquired for the next frame
    }
    const std::uint64_t value = slot.value.load();
    const ViewRecord slotView = slot.view;
    const bool slotHasView = slot.hasView;
    const bool slotUi = slot.ui.written && acquireUiXrImage(); // UI layer: the GUI image goes along
    ID3D12Resource* target = xrImages[static_cast<std::size_t>(acquiredIndex)];

    d3dAllocator->Reset();
    d3dList->Reset(d3dAllocator.Get(), nullptr);
    std::array<D3D12_RESOURCE_BARRIER, 2> barriers{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = slot.resource.Get();
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = target;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    d3dList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = target;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = slot.resource.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    d3dList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    for (auto& b : barriers) {
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    }
    d3dList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    if (slotUi) {
        recordUiXrCopy(slot);
    }
    const HRESULT closed = d3dList->Close();
    if (FAILED(closed)) {
        // A list that failed to close must never be executed (the device would be removed).
        if (!loggedCloseFailure) {
            loggedCloseFailure = true;
            EVR_LOG("d3d12: the copy's command list failed to close (0x%08lx): ring %ux%u, XR image %llux%u; "
                    "the frame is skipped (image %lld of %zu)",
                    static_cast<unsigned long>(closed), ringExtent.width, ringExtent.height,
                    static_cast<unsigned long long>(target->GetDesc().Width), target->GetDesc().Height,
                    static_cast<long long>(acquiredIndex), xrImages.size());
        }
        slot.state.store(kSlotFree);
        return;
    }

    // GPU-side wait for the Vulkan write of this slot, then the copy.
    d3dQueue->Wait(sharedFence.Get(), value);
    ID3D12CommandList* lists[] = {d3dList.Get()};
    d3dQueue->ExecuteCommandLists(1, lists);
    d3dQueue->Signal(copyFence.Get(), ++copyFenceValue);
    if (copyFence->GetCompletedValue() < copyFenceValue) {
        copyFence->SetEventOnCompletion(copyFenceValue, copyEvent);
        if (WaitForSingleObject(copyEvent, 2000) != WAIT_OBJECT_0) {
            // The slot stays marked as being read, so the game never writes it while D3D12 might, and
            // nothing is recorded again (the allocator may still be executing) until the copy is done.
            EVR_LOG("d3d12: copy did not finish within 2 s; slot %u and the XR image are held until it does",
                    slotIndex);
            copyStalled = true;
            stalledSlot = slotIndex;
            stalledValue = value;
            stalledView = slotView;
            stalledHasView = slotHasView;
            return;
        }
    }
    completeCopy(slotIndex, value, slotView, slotHasView);
}

void XrPresenter::Impl::completeCopy(std::uint32_t slotIndex,
                                     std::uint64_t value,
                                     const ViewRecord& view,
                                     bool hasView) {
    ring[slotIndex].state.store(kSlotFree);
    lastConsumed = value;
    const ViewRecord& slotView = view;
    const bool slotHasView = hasView;

    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xr.xrReleaseSwapchainImage(xrSwapchain, &release);
    acquiredIndex = -1;
    acquiredWaited = false;
    finishUiXrCopy();
    if (!hasImage) {
        EVR_LOG("xr: first game frame on the screen (timeline value %llu)",
                static_cast<unsigned long long>(value));
    }
    hasImage = true;
    shownView = slotView;
    shownHasView = slotHasView;
    ++xrCopies;
}

void XrPresenter::Impl::updateTargetFov(XrTime time) {
    XrViewLocateInfo info{XR_TYPE_VIEW_LOCATE_INFO};
    info.viewConfigurationType = viewConfig;
    info.displayTime = time;
    info.space = viewSpace;
    XrViewState viewState{XR_TYPE_VIEW_STATE};
    std::array<XrView, 2> views{};
    for (XrView& v : views) {
        v.type = XR_TYPE_VIEW;
    }
    std::uint32_t count = 0;
    if (XR_FAILED(xr.xrLocateViews(session, &info, &viewState, static_cast<std::uint32_t>(views.size()),
                                   &count, views.data())) ||
        count != views.size() || !(viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
        return;
    }
    // One image serves both eyes, rendered from the head centre: the eyes' frusta are taken with
    // their orientation only, and the symmetric FOV enclosing both is what the game renders.
    std::array<xr_math::EyeView, 2> eyes{};
    for (std::size_t i = 0; i < views.size(); ++i) {
        const XrFovf& f = views[i].fov;
        eyes[i].fov = {f.angleLeft, f.angleRight, f.angleUp, f.angleDown};
        const XrQuaternionf& q = views[i].pose.orientation;
        eyes[i].poseInHead.orientation = normalize(Quat{q.x, q.y, q.z, q.w});
        EVR_LOG("xr: eye %zu fov left %.2f right %.2f up %.2f down %.2f deg, position (%.4f %.4f %.4f)", i,
                f.angleLeft * 57.29578f, f.angleRight * 57.29578f, f.angleUp * 57.29578f,
                f.angleDown * 57.29578f, views[i].pose.position.x, views[i].pose.position.y,
                views[i].pose.position.z);
    }
    const auto enclosing = xr_math::enclosingFov(eyes, 1.0f, xr_math::EnclosingShape::Symmetric);
    if (!enclosing) {
        EVR_LOG("xr: no enclosing FOV for the eyes; the game keeps its own FOV");
        fovChecked = true;
        return;
    }
    const auto tangents = xr_math::toTangents(*enclosing);
    const auto game = xr_math::gameFovFromTangents(tangents.right, tangents.up);
    fovChecked = true;
    if (!game) {
        EVR_LOG("xr: the enclosing FOV has no game equivalent; the game keeps its own FOV");
        return;
    }
    targetFovX.store(game->fovX);
    targetFovY.store(game->fovY);
    targetFovValid.store(true, std::memory_order_release);
    EVR_LOG(
        "xr: game FOV for the headset %.2f x %.2f deg (tangents %.3f x %.3f, aspect %.3f); the game's image "
        "is %ux%u (aspect %.3f)%s",
        game->fovX, game->fovY, tangents.right, tangents.up, tangents.right / tangents.up, eyeExtent.width,
        eyeExtent.height, static_cast<double>(eyeExtent.width) / static_cast<double>(eyeExtent.height),
        settings.setGameFov ? "" : "; not applied (ETERNALVR_SET_FOV=0)");
}

void XrPresenter::Impl::frame() {
    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState state{XR_TYPE_FRAME_STATE};
    if (const XrResult r = xr.xrWaitFrame(session, &waitInfo, &state); XR_FAILED(r)) {
        if (!loseOnRuntimeFailure(r, "xrWaitFrame")) {
            Sleep(5);
        }
        return;
    }
    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    if (const XrResult r = xr.xrBeginFrame(session, &beginInfo); XR_FAILED(r)) {
        loseOnRuntimeFailure(r, "xrBeginFrame");
        return;
    }
    // The camera hook predicts the head for about when the game's next frame will be shown; under
    // ETERNALVR_POSE_LEAD, later by how late the frames shown were (display_lead.hpp).
    nextDisplayTime.store(state.predictedDisplayTime + state.predictedDisplayPeriod + displayLead.leadNs());
    displayPeriod.store(state.predictedDisplayPeriod);
    controllers::sync(state.predictedDisplayTime, sessionState == XR_SESSION_STATE_FOCUSED);
    test_keys::poll(gameWindow());
    if (settings.mode == Mode::HeadTracked && hooks.gameView && !trackingReady.load() &&
        state.predictedDisplayTime > 0) {
        trackingReady.store(true, std::memory_order_release);
        EVR_LOG("head: tracking ready; the game's view follows the headset");
    }

    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad uiQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad reticleQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    std::array<XrCompositionLayerProjectionView, 2> projectionViews{};
    XrCompositionLayerQuad fade{XR_TYPE_COMPOSITION_LAYER_QUAD};
    std::array<XrCompositionLayerQuad, 2> pointerQuads{};
    for (XrCompositionLayerQuad& q : pointerQuads) {
        q.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    }
    // Back to front: the projection (or the cinema quad), the UI quad, the reticle or the menu pointer (beam,
    // dot), then the fade on top. Unused entries are overwritten as layers are added.
    const XrCompositionLayerBaseHeader* layers[] = {
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad),
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&uiQuad),
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&reticleQuad),
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&fade),
        nullptr,
        nullptr};
    std::uint32_t layerCount = 0;
    const auto addPointer = [&] {
        const std::uint32_t n = fillPointerQuads(pointerQuads.data());
        for (std::uint32_t i = 0; i < n; ++i) {
            layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&pointerQuads[i]);
        }
    };
    bool uiShown = false;
    if (state.shouldRender) {
        if (!quadPlaced || replaceScreen.exchange(false, std::memory_order_acq_rel)) {
            placeQuad(state.predictedDisplayTime);
        }
        if (settings.mode == Mode::HeadTracked && !fovChecked) {
            updateTargetFov(state.predictedDisplayTime);
        }
        updateImage();
        // Once the multiplayer guard is off the game's image is shown only on the flat cinema quad,
        // never as a head-tracked projection (the camera hook no longer writes the view).
        const bool guardOff = !mp_guard::allowsGameTouch();
        if (guardOff && !loggedGuardCinema) {
            loggedGuardCinema = true;
            EVR_LOG("xr: multiplayer guard %s: the game is shown on the flat cinema screen from now on; no "
                    "camera "
                    "writes, head aim or key injection for the rest of this process",
                    mp_policy::toString(mp_guard::state()));
            status::flat(
                "an online mode or a multiplayer invite switched VR off for this session (restart from "
                "the launcher)");
        }
        // Menus: the game's cursor decides whether a menu is up; its panel shows the UI quad over a
        // head-tracked frame, or the game's whole frame on the cinema path (presenter_menu.cpp).
        const bool headTracked = hasImage && shownHasView && !guardOff;
        updateMenu(state.predictedDisplayTime, headTracked ? settings.ui.enabled && uiFresh() : hasImage);
        if (headTracked) {
            // Mono: both eyes get the whole image with the head pose and FOV it was rendered with. Stereo:
            // each eye gets its half with its own pose and FOV (presenter_stereo.cpp).
            fillProjectionViews(projectionViews);
            projection.layerFlags = 0;
            projection.space = localSpace;
            projection.viewCount = static_cast<std::uint32_t>(projectionViews.size());
            projection.views = projectionViews.data();
            layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
            layerCount = 1;
            // UI layer: the game's GUI on its own quad in front (only with a fresh GUI image).
            uiShown = settings.ui.enabled && fillUiQuad(uiQuad);
            if (uiShown) {
                layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&uiQuad);
                if (menuOn) {
                    // A menu over the game (pause, the in-game screens): the UI quad becomes the
                    // world-locked menu panel, and the pointer replaces the hand-aim dot.
                    placeOnMenuPanel(uiQuad);
                    addPointer();
                } else if (menuHeld) {
                    // The cursor went but the menu's backdrop is still up: the panel stays, no pointer.
                    placeOnMenuPanel(uiQuad);
                } else if (fillReticleQuad(reticleQuad)) {
                    // Hand aim: the dot on the weapon hand's ray, after the UI quad.
                    layers[layerCount++] =
                        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&reticleQuad);
                }
            }
            // The head in geometry fades the view to black, over everything (docs/VR_ROOMSCALE.md).
            const float alpha = room.fade(qpcSeconds(qpcNow()));
            if (fadeLayer.prepare(xr, d3dQueue.Get(), alpha, viewSpace, fade)) {
                layers[layerCount] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&fade);
                ++layerCount;
            }
            ++xrProjectionFrames;
            if (!loggedFirstProjection) {
                loggedFirstProjection = true;
                EVR_LOG("xr: first head-tracked frame (view %llu, fov %.2f x %.2f deg)",
                        static_cast<unsigned long long>(shownView.seq),
                        (shownView.fov.angleRight - shownView.fov.angleLeft) * 57.29578f,
                        (shownView.fov.angleUp - shownView.fov.angleDown) * 57.29578f);
            }
        } else if (hasImage) {
            ++xrQuadFrames;
            // The layer repeats the last released image when no new frame arrived (never dropped).
            quad.layerFlags = 0;
            quad.space = localSpace;
            quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quad.subImage.swapchain = xrSwapchain;
            // The left half of a Route S ring (a mono frame fills both halves).
            quad.subImage.imageRect = {
                {0, 0},
                {static_cast<std::int32_t>(eyeExtent.width), static_cast<std::int32_t>(eyeExtent.height)}};
            quad.subImage.imageArrayIndex = 0;
            quad.pose = quadPose;
            quad.size = quadSize;
            layerCount = 1;
            if (menuOn) {
                // The title screen and the main menu: the whole frame on the menu panel, with the pointer.
                placeOnMenuPanel(quad);
                addPointer();
            } else if (const auto band = cinemaView.band(eyeExtent.width, eyeExtent.height)) {
                // A cutscene drawn for a flat display's shape: only its band, on a screen of that shape.
                quad.subImage.imageRect.offset.y = static_cast<std::int32_t>(band->y);
                quad.subImage.imageRect.extent.height = static_cast<std::int32_t>(band->height);
                quad.size.height =
                    quadSize.width * static_cast<float>(band->height) / static_cast<float>(eyeExtent.width);
            }
        }
    }
    // The GUI leaves the eye images only while the quad shows it; menus and loading screens keep it.
    if (settings.ui.enabled) {
        ui_engine::setSkipComposite(settings.ui.skipComposite && uiShown);
    }
    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = state.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = layerCount;
    endInfo.layers = layerCount ? layers : nullptr;
    const XrResult r = xr.xrEndFrame(session, &endInfo);
    if (XR_FAILED(r) && !loseOnRuntimeFailure(r, "xrEndFrame")) {
        // Every frame can fail the same way: the first ones, then one line per 900 (10 s at 90 Hz).
        if (++endFrameFailures <= 5 || endFrameFailures % 900 == 0) {
            char text[XR_MAX_RESULT_STRING_SIZE];
            EVR_LOG("xr: xrEndFrame failed: %s (%u so far)", xrText(r, text), endFrameFailures);
        }
    }
    ++xrFrames;
    if (layerCount) {
        noteShownView(state);
        logFrame(state);
    }
    if (GetTickCount64() - lastXrStatsTicks >= 10000) {
        lastXrStatsTicks = GetTickCount64();
        EVR_LOG("xr: %llu frame(s), %llu new image(s), %llu repeat(s); %llu head-tracked, %llu on the "
                "screen; pose age "
                "average %.1f ms, max %.1f ms; display period %.2f ms, pose lead %.1f ms",
                static_cast<unsigned long long>(xrFrames), static_cast<unsigned long long>(xrCopies),
                static_cast<unsigned long long>(xrRepeats),
                static_cast<unsigned long long>(xrProjectionFrames),
                static_cast<unsigned long long>(xrQuadFrames),
                poseAgeCount ? poseAgeSum / static_cast<double>(poseAgeCount) : 0.0, poseAgeMax,
                static_cast<double>(state.predictedDisplayPeriod) / 1e6,
                static_cast<double>(displayLead.leadNs()) / 1e6);
        poseAgeSum = 0.0;
        poseAgeMax = 0.0;
        poseAgeCount = 0;
        logRates();
        logSizeStats();
        if (frameLog) {
            std::fflush(frameLog);
        }
    }
}

void XrPresenter::Impl::destroyXrObjects() {
    trackingReady.store(false);
    controllers::detach();
    disableKeepActive();
    std::unique_lock spaceLock(spaceMutex); // no camera hook is locating the head past this point
    // xrEndSession is only valid in STOPPING; destroying the session is valid in any state.
    if (session && sessionRunning && sessionState == XR_SESSION_STATE_STOPPING) {
        xr.xrEndSession(session);
    }
    sessionRunning = false;
    if (xrSwapchain) {
        xr.xrDestroySwapchain(xrSwapchain);
        xrSwapchain = XR_NULL_HANDLE;
    }
    xrImages.clear();
    destroyUiXrObjects();
    fadeLayer.destroy(xr);
    if (floorSpace) {
        xr.xrDestroySpace(floorSpace);
        floorSpace = XR_NULL_HANDLE;
    }
    if (viewSpace) {
        xr.xrDestroySpace(viewSpace);
        viewSpace = XR_NULL_HANDLE;
    }
    if (localSpace) {
        xr.xrDestroySpace(localSpace);
        localSpace = XR_NULL_HANDLE;
    }
    if (session) {
        xr.xrDestroySession(session);
        session = XR_NULL_HANDLE;
    }
    if (instance) {
        xr.xrDestroyInstance(instance);
        instance = XR_NULL_HANDLE;
    }
}

void XrPresenter::Impl::runWorker() {
    setPassThroughThread(true);
    // The code this thread runs, and the hooks and import replacements it installs, must outlive any
    // unload of the layer (the loader unloads layers when an instance is destroyed).
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&moduleDirectory), &self);
    VkExtent2D startExtent{};
    {
        std::lock_guard lock(mutex);
        startExtent = requestedExtent;
    }
    EVR_LOG("xr: worker started for a %ux%u game image; mode %s", startExtent.width, startExtent.height,
            settings.mode == Mode::Cinema ? "cinema"
            : settings.stereo.enabled     ? "stereo"
                                          : "head-tracked");
    if (settings.mode == Mode::HeadTracked) {
        EVR_LOG("head: world scale %.3f unit(s) per metre, head position %s, game FOV %s, aim %s",
                settings.unitsPerMetre, settings.headPosition ? "on" : "off",
                settings.setGameFov ? "set to the headset's" : "the game's own",
                settings.headAim ? "follows the head" : "the game's own (view only)");
        // The camera hook, the stereo hooks and key injection are installed only while the multiplayer
        // guard is armed; once installed they check it again before every change they make.
        if (mp_guard::allowsGameTouch()) {
            hooks = installViewHooks(this);
            startStereo();
            if (settings.ui.enabled) {
                startUi();
            }
            if (settings.skipCinematics) {
                installKeyInjection();
            }
            controllers::installGameHooks();
            startMenu();
            if (roomScaleSettings().collision) {
                installHeadSweep();
            }
        } else {
            EVR_LOG("head: the multiplayer guard is %s; no camera hook, stereo, head aim or key injection",
                    mp_policy::toString(mp_guard::state()));
        }
        if (settings.poseLead) {
            EVR_LOG("head: pose lead on: the head and hands are predicted for when frames are shown");
        }
        if (settings.swayPeriod > 0.0f) {
            EVR_LOG("head: test sway on: yaw %.1f deg, pitch %.1f deg, period %.1f s around a %.1f deg turn, "
                    "added to the tracked pose",
                    settings.swayYaw, settings.swayPitch, settings.swayPeriod, settings.swayBaseYaw);
        }
        if (!hooks.gameView) {
            EVR_LOG("head: no game view hook; the game's view stays as it is and frames show on the screen");
        }
        openFrameLog();
    }
    // Every stage is followed by a stop check: once shutdown has begun the game's device may be gone.
    const auto running = [this] {
        return !stop.load();
    };
    bool ok = loadOpenXr() && running() && createXrInstance() && running() && waitForSystem() && running() &&
              createD3D12AndSession() && running() && createRing();
    if (ok) {
        {
            std::lock_guard lock(mutex);
            ringReady.store(true);
            requestRebuildIfStale();
        }
        consumerAlive.store(true);
        EVR_LOG("xr: presenter ready; waiting for the session to start");
        while (!stop.load() && !sessionLost) {
            if (resizeRequested.load()) {
                recreateRing();
            }
            pollEvents();
            if (sessionRunning) {
                frame();
            } else {
                Sleep(10);
            }
        }
    } else if (!stop.load()) {
        EVR_LOG("xr: presenter inert; the game runs flat");
        status::flat("VR could not start (see the reason before this in the log)");
    }
    consumerAlive.store(false);
    if (instance) {
        destroyXrObjects();
    }
    EVR_LOG("xr: worker finished (%llu frame(s), %llu new image(s))",
            static_cast<unsigned long long>(xrFrames), static_cast<unsigned long long>(xrCopies));
    if (frameLog) {
        std::fclose(frameLog);
        frameLog = nullptr;
    }
    SetEvent(workerDone);
}

} // namespace evr::vkcore
