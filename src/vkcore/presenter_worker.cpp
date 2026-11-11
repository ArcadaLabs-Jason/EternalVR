// The XR worker's thread (presenter_frame.cpp has its frame loop): the hooks it installs, the OpenXR start-up
// stages, the loop of frames with the reconnect after a loss, and the OpenXR objects' teardown.

#include "vkcore/fence_wait.hpp"
#include "vkcore/presenter_impl.hpp"
#include "vkcore/presenter_menu_release.hpp"

#include "features/pacing/pace_policy.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/debug_commands.hpp"
#include "vkcore/frame_pacing.hpp"
#include "vkcore/hang_note.hpp"
#include "vkcore/head_sweep.hpp"
#include "vkcore/hook_guard.hpp"
#include "vkcore/keep_active.hpp"
#include "vkcore/key_inject.hpp"
#include "vkcore/light_cull.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/new_frame_wait.hpp"
#include "vkcore/stall_watch.hpp"
#include "vkcore/status_file.hpp"
#include "vkcore/vram_watch.hpp"

#include <cstdio>

namespace evr::vkcore {

void XrPresenter::Impl::destroyXrObjects() {
    trackingReady.store(false);
    controllers::detach();
    disableKeepActive();
    viewer.watch.state(pacing::SessionView::NotRunning);
    frame_pacing::setBackgroundHold(viewer.watch.hold());
    std::unique_lock spaceLock(spaceMutex); // no camera hook is locating the head past this point
    // xrEndSession is only valid in STOPPING; destroying the session is valid in any state.
    if (session && sessionRunning && sessionState == XR_SESSION_STATE_STOPPING) {
        xr.xrEndSession(session);
    }
    sessionRunning = false;
    xrSwapchains.destroy(xr, xrSwapchain);
    xrImages.clear();
    destroyUiXrObjects();
    xrSwapchains.forget(fadeLayer.swapchain());
    fadeLayer.destroy(xr);
    for (const XrSwapchain level : vignette.swapchains()) {
        xrSwapchains.forget(level);
    }
    vignette.destroy(xr);
    xrSwapchains.endSession(xr); // the ones kept unused, then the record
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

// An exception must not leave this thread: it would end the game. One from a start-up stage, a hook's install
// or a frame ends VR as a lost session that does not come back does: the game's presents pass through
// (consumerAlive), the OpenXR objects go, the launcher reads flat and the game runs on.
void XrPresenter::Impl::runWorker() {
    static HookGuard stages("the XR worker");
    static HookGuard teardown("the XR worker's teardown");
    const bool ran = stages.run([this] { runWorkerStages(); });
    const bool tornDown = teardown.run([this, ran] {
        if (!ran) {
            status::flat("VR stopped on an error (details in the log)");
        }
        releaseMenuInput(*this, "VR stopped");
        consumerAlive.store(false);
        refresh.logSummary(" at session end");
        if (instance) {
            destroyXrObjects();
        }
        EVR_LOG("xr: worker finished (%llu frame(s), %llu new image(s))",
                static_cast<unsigned long long>(xrFrames), static_cast<unsigned long long>(xrCopies));
    });
    if (!tornDown) {
        consumerAlive.store(false);
    }
    if (frameLog) {
        std::fclose(frameLog);
        frameLog = nullptr;
    }
    SetEvent(workerDone);
}

void XrPresenter::Impl::runWorkerStages() {
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
            // On unless ETERNALVR_LIGHT_FRUSTUM=0: a light Umbra hides is kept if the view frustum keeps it.
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
        // Replaces a "no headset found yet" from before the headset came: the launcher's start watch tells a
        // session the runtime does not start by this state with runtime= set.
        status::starting("the headset runtime has the game's VR session; waiting for it to start VR");
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
                hang_note::check(); // the game stopped presenting in play: where its threads stand
                status::flush();    // a status write lost while the launcher read the file
                if (sessionRunning) {
                    if (frame_pacing::newFramesOnly() && hasImage) { // ETERNALVR_PACE=new or headset
                        const double period = static_cast<double>(displayPeriod.load()) / 1e9;
                        new_frame_wait::wait(latest, period, stop); // each image once, begun once it is drawn
                        waitFenceFor(sharedFence.Get(), latest.load() >> 2, copyEvent,
                                     pacing::newFrameWaitSeconds(period));
                    }
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
}

} // namespace evr::vkcore
