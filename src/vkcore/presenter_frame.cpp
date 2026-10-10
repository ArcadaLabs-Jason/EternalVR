// The XR worker: its thread body and the frame loop (one image per XR frame, the projection layer for
// head-tracked images and the cinema quad otherwise).

#include "vkcore/fence_wait.hpp"
#include "vkcore/presenter_impl.hpp"
#include "vkcore/presenter_menu_release.hpp"

#include "features/comfort/vignette.hpp"
#include "game/eternal/game_action.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/debug_commands.hpp"
#include "vkcore/frame_pacing.hpp"
#include "vkcore/gpu_timing.hpp"
#include "vkcore/head_sweep.hpp"
#include "vkcore/keep_active.hpp"
#include "vkcore/key_inject.hpp"
#include "vkcore/light_cull.hpp"
#include "vkcore/menu_model_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/stall_watch.hpp"
#include "vkcore/status_file.hpp"
#include "vkcore/test_keys.hpp"
#include "vkcore/ui_engine.hpp"
#include "vkcore/vram_watch.hpp"
#include "xr_math/cinema_quad.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace evr::vkcore {

void XrPresenter::Impl::updateImage(LONGLONG frameStart) {
    // The frames table's reason until a new image is taken or nothing newer is found handed over.
    frameLogEyes.repeat = FrameRepeat::Waiting;
    if (heldCopy.stalled) {
        if (copyFence->GetCompletedValue() < copyFenceValue) {
            ++xrRepeats;
            // Held this long, the GPU itself is in trouble (a graphics card reset), not one slow frame.
            if (!heldCopy.longLogged && qpcSeconds(qpcNow() - heldCopy.sinceQpc) >= 2.0) {
                heldCopy.longLogged = true;
                heldCopy.logged = true;
                EVR_LOG(
                    "d3d12: copy did not finish within 2 s; slot %u and the XR image are held until it does",
                    heldCopy.slot);
            }
            return;
        }
        heldCopy.stalled = false;
        if (heldCopy.logged) {
            EVR_LOG("d3d12: the held copy finished after %.0f ms; copies resume",
                    qpcSeconds(qpcNow() - heldCopy.sinceQpc) * 1000.0);
        }
        completeCopy(heldCopy.slot, heldCopy.value, heldCopy.view, heldCopy.hasView);
        return;
    }
    const std::uint64_t packed = latest.load();
    frame_pacing::onHeadsetFrame(displayPeriod.load()); // the headset's frame began: a paced game goes on
    const std::uint64_t newest = packed >> 2;
    if (newest <= lastConsumed) {
        ++xrRepeats;
        frameLogEyes.repeat = frameLogEyes.lastPresent.load(); // why the game's last present handed none
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
        const XrResult r = timedCall(inRuntime.waitImage, xr.xrWaitSwapchainImage, xrSwapchain, &wait);
        if (r == XR_TIMEOUT_EXPIRED) {
            EVR_LOG("xr: swapchain image wait timed out; repeating the last image");
            return;
        }
        if (XR_FAILED(r)) {
            return;
        }
        acquiredWaited = true;
    }

    // A slot whose frame finished rendering (presenter_ring.cpp); none yet: the image stays acquired.
    std::uint32_t slotIndex = 0;
    std::uint64_t value = 0;
    if (!takeRenderedSlot(slotIndex, value, frameStart)) {
        ++xrBusyRepeats;
        return;
    }
    RingSlot& slot = ring[slotIndex];
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
    // At most a couple of display periods, so the frame loop never waits on a busy GPU for long (public issue
    // #19); past that the frame shows the last image and a later frame picks the copy up.
    const std::uint32_t waitMs = pacing::copyWaitMs(displayPeriod.load());
    const LONGLONG waitStart = qpcNow();
    const bool copied = waitFence(copyFence.Get(), copyFenceValue, copyEvent, waitMs);
    copyWait.add(qpcSeconds(qpcNow() - waitStart) * 1000.0);
    if (!copied) {
        // The slot stays marked as being read, so the game never writes it while D3D12 might, and
        // nothing is recorded again (the allocator may still be executing) until the copy is done.
        ++heldCopy.count;
        heldCopy.logged = heldCopy.count <= 5 || heldCopy.count % 100 == 0;
        heldCopy.longLogged = false;
        if (heldCopy.logged) {
            EVR_LOG(
                "d3d12: copy did not finish within %u ms; slot %u and the XR image are held until it does "
                "(%u so far)",
                waitMs, slotIndex, heldCopy.count);
        }
        heldCopy.sinceQpc = waitStart;
        heldCopy.stalled = true;
        heldCopy.slot = slotIndex;
        heldCopy.value = value;
        heldCopy.view = slotView;
        heldCopy.hasView = slotHasView;
        return;
    }
    completeCopy(slotIndex, value, slotView, slotHasView);
}

void XrPresenter::Impl::completeCopy(std::uint32_t slotIndex,
                                     std::uint64_t value,
                                     const ViewRecord& view,
                                     bool hasView) {
    frameLogEyes.shown = ring[slotIndex].eyes; // read before the slot is free again
    frameLogEyes.repeat = FrameRepeat::New;
    ring[slotIndex].state.store(kSlotFree);
    lastConsumed = value;

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
    shownView = view;
    shownHasView = hasView;
    ++xrCopies;
}

void XrPresenter::Impl::frame() {
    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState state{XR_TYPE_FRAME_STATE};
    if (const XrResult r = timedCall(inRuntime.waitFrame, xr.xrWaitFrame, session, &waitInfo, &state);
        XR_FAILED(r)) {
        if (!loseOnRuntimeFailure(r, "xrWaitFrame")) {
            Sleep(5);
        }
        return;
    }
    const LONGLONG frameStart = qpcNow(); // the headset's frame begins (slot_choice.hpp's wait)
    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    if (const XrResult r = xr.xrBeginFrame(session, &beginInfo); XR_FAILED(r)) {
        loseOnRuntimeFailure(r, "xrBeginFrame");
        return;
    }
    // The camera hook predicts the head for about when the game's next frame will be shown: one display
    // period on, at most 1.5x the headset's refresh period while the runtime throttles
    // (presenter_refresh.hpp); under ETERNALVR_POSE_LEAD the period as reported, later by how late the frames
    // shown were (display_lead.hpp).
    nextDisplayTime.store(state.predictedDisplayTime +
                          refresh.predictionPeriod(state.predictedDisplayPeriod, settings.poseLead) +
                          displayLead.leadNs());
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
    XrCompositionLayerQuad vignetteQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    std::array<XrCompositionLayerQuad, 2> pointerQuads{};
    for (XrCompositionLayerQuad& q : pointerQuads) {
        q.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    }
    std::array<XrCompositionLayerQuad, WristHud::kMaxQuads> hudQuads{}; // wrist mode: pieces and wrist quads
    // Back to front: the projection (or the cinema quad), the comfort vignette, the UI quad (or in wrist mode
    // its head-locked pieces and the wrist quads), the reticle or the menu pointer (beam, dot), then the fade
    // on top. Entries are written as layers are added.
    std::array<const XrCompositionLayerBaseHeader*, 5 + WristHud::kMaxQuads> layers{};
    layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
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
        if (settings.mode == Mode::HeadTracked && fovWatch.due(qpcSeconds(frameStart))) {
            updateTargetFov(state.predictedDisplayTime);
        }
        updateImage(frameStart);
        // Once the multiplayer guard is off the game's image is shown only on the flat cinema quad,
        // never as a head-tracked projection (the camera hook no longer writes the view).
        const bool guardOff = !mp_guard::allowsGameTouch();
        if (guardOff && !loggedGuardCinema) {
            loggedGuardCinema = true;
            EVR_LOG("xr: multiplayer guard %s: the game is shown on the flat cinema screen from now on; no "
                    "camera writes, head aim or key injection for the rest of this process",
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
            // The comfort vignette goes right over the game's view and under everything else: the HUD in the
            // corners and the aim dot stay readable while the world at the edges darkens, and the collision
            // fade still covers it all. Never with a menu up (the panel is what the player looks at then).
            if (settings.ui.vignette != ui_layer::VignetteMode::Off) {
                const controllers::ArtificialMotion stick = controllers::artificialMotion();
                comfort::VignetteMotion motion;
                motion.turnDegreesPerSecond = stick.turnDegreesPerSecond;
                motion.moveMagnitude = stick.move;
                motion.gameMotion = controllers::forcedView() ||
                                    game::contains(controllers::heldActions(), game::GameAction::Dash);
                const auto make = [this](XrSwapchain& swapchain, const std::vector<std::uint8_t>& pixels,
                                         std::uint32_t width, std::uint32_t height, const char* what) {
                    return createStaticImage(swapchain, pixels, width, height, what);
                };
                if (vignette.prepare(settings.ui.vignette, motion, !menuUp.load(std::memory_order_relaxed),
                                     qpcSeconds(qpcNow()), viewSpace, make, vignetteQuad)) {
                    layers[layerCount++] =
                        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&vignetteQuad);
                }
            }
            // UI layer: the game's GUI on its own quad in front (only with a fresh GUI image).
            uiShown = settings.ui.enabled && fillUiQuad(uiQuad);
            if (uiShown) {
                // Wrist mode (presenter_wrist.cpp): the UI quad's head-locked pieces and the wrist quads;
                // not while the UI quad is the menu panel.
                const std::uint32_t hud =
                    menuOn || menuHeld
                        ? 0
                        : wrist.fill(settings.ui, uiExtent.width, uiExtent.height, uiQuad, hudQuads.data());
                for (std::uint32_t i = 0; i < hud; ++i) {
                    layers[layerCount++] =
                        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&hudQuads[i]);
                }
                if (hud == 0) {
                    layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&uiQuad);
                }
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
            } else if (menuHeld) {
                // The cursor went but the backdrop is still up (the end of a loading screen, or the main
                // menu changing screens): the panel stays, no pointer, instead of a jump to the screen.
                placeOnMenuPanel(quad);
            } else if (const auto band = cinemaView.band(eyeExtent.width, eyeExtent.height)) {
                // A cutscene drawn for a flat display's shape: only its band, on a screen of that shape.
                quad.subImage.imageRect.offset.y = static_cast<std::int32_t>(band->y);
                quad.subImage.imageRect.extent.height = static_cast<std::int32_t>(band->height);
                quad.size.height =
                    quadSize.width * static_cast<float>(band->height) / static_cast<float>(eyeExtent.width);
            }
        }
        // A menu's 3D model goes where the panel shows the menu (menu_model_hook.hpp).
        menu_model::notePanel(headTracked && uiShown && (menuOn || menuHeld), uiQuad, uiExtent.width,
                              uiExtent.height);
    } else {
        releaseMenuInput(*this, "the headset is not showing frames");
    }
    // The GUI leaves the eye images only while the quad shows it; menus and loading screens keep it.
    if (settings.ui.enabled) {
        ui_engine::setSkipComposite(settings.ui.skipComposite && uiShown);
    }
    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = state.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = layerCount;
    endInfo.layers = layerCount ? layers.data() : nullptr;
    const XrResult r = timedCall(inRuntime.endFrame, xr.xrEndFrame, session, &endInfo);
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
    afterFrame(state);
    watchFrameClock(state);
}

void XrPresenter::Impl::destroyXrObjects() {
    trackingReady.store(false);
    controllers::detach();
    disableKeepActive();
    frame_pacing::setUnfocused(false);
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
    vignette.destroy(xr);
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
            installDebugCommands(); // ETERNALVR_DEBUG_COMMANDS: console commands on a schedule (test rig)
            // ETERNALVR_LIGHT_FRUSTUM=1 (experimental): lights culled by the view, not dropped by Umbra.
            installLightFrustumCull();
            stall_watch::installCheckpointSaveHook(); // stall lines name the game's own checkpoint saves
            startMenu();
            if (roomScaleSettings().collision || settings.ui.reticle) { // the aim dot's depth uses it too
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
    stall_watch::trackGameTicks(hooks.gameView); // the camera hook's ticks tell play from loading and menus
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
        // A lost session leaves the inner loop; reconnect() returns once a new session exists (or at
        // shutdown).
        while (!stop.load()) {
            while (!stop.load() && !loss.lost) {
                if (resizeRequested.load()) {
                    recreateRing();
                }
                pollEvents();
                refreshDisplayRate(); // between frames: xrWaitFrame has the slack
                vram::poll();
                if (sessionRunning) {
                    frame();
                } else {
                    Sleep(10);
                }
            }
            if (stop.load() || !xr_recovery::recovers(loss.kind) || !reconnect()) {
                break;
            }
        }
    } else if (!stop.load()) {
        EVR_LOG("xr: presenter inert; the game runs flat");
        status::flat("VR could not start (see the reason before this in the log)");
    }
    releaseMenuInput(*this, "VR stopped");
    consumerAlive.store(false);
    refresh.logSummary(" at session end");
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
