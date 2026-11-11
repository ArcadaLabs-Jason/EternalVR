// The XR worker's frame loop (one image per XR frame, the projection layer for head-tracked images and the
// cinema quad otherwise); the worker's thread is in presenter_worker.cpp.

#include "vkcore/fence_wait.hpp"
#include "vkcore/presenter_hud_lock.hpp"
#include "vkcore/presenter_impl.hpp"
#include "vkcore/presenter_menu_release.hpp"
#include "vkcore/presenter_perf.hpp"

#include "features/comfort/vignette.hpp"
#include "features/pacing/pace_policy.hpp"
#include "game/eternal/game_action.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/frame_pacing.hpp"
#include "vkcore/gpu_timing.hpp"
#include "vkcore/keep_active.hpp"
#include "vkcore/marker_hooks.hpp"
#include "vkcore/menu_model_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/status_file.hpp"
#include "vkcore/test_keys.hpp"
#include "vkcore/ui_engine.hpp"
#include "xr_math/cinema_quad.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
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
    const std::uint64_t packed = frame_pacing::awaitLateImage(latest, frameStart, stop);
    frame_pacing::onHeadsetFrame(displayPeriod.load(), frameStart, inRuntime.waitFrame.lastMs);
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
    // Our own reference since the swapchain was made (presenter_swapchains.hpp): valid even if the runtime
    // lets go of the image early.
    ID3D12Resource* target = xrImages[static_cast<std::size_t>(acquiredIndex)].Get();

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
    XrCompositionLayerQuad quadR{XR_TYPE_COMPOSITION_LAYER_QUAD}; // eye R's screen (stereo cutscenes)
    XrCompositionLayerQuad uiQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad reticleQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    std::array<XrCompositionLayerQuad, 2> markerQuads{reticleQuad, reticleQuad}; // the HUD's, one per eye
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    std::array<XrCompositionLayerProjectionView, 2> projectionViews{};
    XrCompositionLayerQuad fade{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad vignetteQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    std::array<XrCompositionLayerQuad, 2> pointerQuads{};
    for (XrCompositionLayerQuad& q : pointerQuads) {
        q.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    }
    std::array<XrCompositionLayerQuad, WristHud::kMaxQuads> hudQuads{}; // pieces, hand quads, the wheel
    XrCompositionLayerQuad perfQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};    // the game's Performance Metrics
    // Back to front: the projection (or the cinema quad), the comfort vignette, the UI quad (or its
    // head-locked pieces, the hand quads and the centred weapon wheel), the reticle or the menu pointer
    // (beam, dot), then the fade on top. Entries are written as layers are added.
    std::array<const XrCompositionLayerBaseHeader*, 6 + WristHud::kMaxQuads> layers{};
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
        // head-tracked frame, or the game's whole frame on the cinema path (presenter_menu_update.cpp).
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
                motion.turnStick = stick.turnStick;
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
                // The hand HUDs and the weapon wheel straight ahead (presenter_wrist.cpp): the UI quad's
                // head-locked pieces, the hand quads and the wheel's quad; not while the UI quad is the menu
                // panel.
                wrist.perfLevel =
                    ui_layer::perfLevelShown(settings.ui.perfOverlayLevel, uiBackdrop.backdrop());
                const std::uint32_t hud =
                    menuOn || menuHeld
                        ? 0
                        : wrist.fill(settings.ui, uiExtent.width, uiExtent.height, uiQuad,
                                     shownView.weaponWheel, shownView.cutscene, hudQuads.data());
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
                } else {
                    // The game's Performance Metrics just above the HUD panel (presenter_perf.hpp).
                    // After it: the markers' two quads (below), the reticle and the fade.
                    const bool perf = fillPerfMetricsQuad(
                        wrist.perfLevel, uiExtent.width, uiExtent.height, uiQuad, layerCount,
                        (hudMarkers.ready() && hud == 0 ? 2u : 0u) + 2u, perfQuad);
                    if (perf) {
                        layers[layerCount++] =
                            reinterpret_cast<const XrCompositionLayerBaseHeader*>(&perfQuad);
                    }
                    if (hudMarkers.ready() && hud == 0) {
                        // The HUD stays head-locked, its markers on their own quads (one per eye) at the
                        // frame's head pose.
                        hudMarkers.fill(uiQuad, viewSpace, localSpace, shownView.pose, markerQuads,
                                        layers.data(), layerCount);
                    } else {
                        for (std::uint32_t i = 0; i < hud; ++i) {
                            lockToRenderedHead(hudQuads[i], viewSpace, localSpace, shownView.pose);
                        }
                        lockToRenderedHead(uiQuad, viewSpace, localSpace, shownView.pose);
                        lockToRenderedHead(perfQuad, viewSpace, localSpace, shownView.pose);
                    }
                    if (fillReticleQuad(reticleQuad)) {
                        // Hand aim: the dot on the weapon hand's ray, after the UI quad.
                        layers[layerCount++] =
                            reinterpret_cast<const XrCompositionLayerBaseHeader*>(&reticleQuad);
                    }
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
            fillScreenQuad(*this, quad); // the layer repeats the last released image (never dropped)
            layerCount = 1;
            if (menuOn) {
                // The title screen and the main menu: the whole frame on the menu panel, with the pointer.
                placeOnMenuPanel(quad);
                addPointer();
            } else if (menuHeld) {
                // The cursor went but the backdrop is still up (the end of a loading screen, or the main
                // menu changing screens): the panel stays, no pointer, instead of a jump to the screen.
                placeOnMenuPanel(quad);
            } else {
                // The screen: a cutscene's band, and a stereo cutscene's eyes (presenter_cinema3d.cpp).
                layerCount += screenLayers(*this, quad, quadR, uiQuad, &layers[layerCount], uiShown);
            }
        }
        // A menu's 3D model goes where the panel shows the menu (menu_model_hook.hpp).
        menu_model::notePanel(headTracked && uiShown && (menuOn || menuHeld), uiQuad, uiExtent.width,
                              uiExtent.height);
        // The HUD's world-attached markers follow the head-locked quad (marker_hooks.hpp).
        noteMarkerQuad(headTracked && uiShown && !menuOn && !menuHeld, settings.ui, uiExtent.width,
                       uiExtent.height);
    } else {
        releaseMenuInput(*this, "the headset is not showing frames");
    }
    // The GUI leaves the eye images only while the quad shows it; menus and loading screens keep it.
    if (settings.ui.enabled) {
        ui_engine::setSkipComposite(settings.ui.skipComposite && uiShown);
    }
    cinema3dNoteUiLayer(settings.ui.enabled && settings.ui.skipComposite && ui_engine::skipHookInstalled() &&
                        uiReady && !uiFailed);
    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = state.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = layerCount;
    endInfo.layers = layerCount ? layers.data() : nullptr;
    frame_pacing::beforeEndFrame();
    const XrResult r = timedCall(inRuntime.endFrame, xr.xrEndFrame, session, &endInfo);
    if (XR_FAILED(r) && !loseOnRuntimeFailure(r, "xrEndFrame")) {
        // Every frame can fail the same way: the first ones, then one line per 900 (10 s at 90 Hz).
        ++endFrameFailures;
        if (endFrameFailures <= 5 || endFrameFailures % 900 == 0) {
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

} // namespace evr::vkcore
