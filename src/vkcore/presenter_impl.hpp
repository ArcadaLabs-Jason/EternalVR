#pragma once

// Internal state of the XR presenter (xr_presenter.hpp), shared by its source files: xr_presenter.cpp
// (settings, shutdown, public entry points), presenter_copy.cpp and presenter_copy_record.cpp (the present
// hook's copy into the ring), presenter_ring.cpp (the shared ring: creation, import, rebuild, its slots),
// presenter_head.cpp (the camera hook, head aim, the render latch), presenter_xr.cpp (OpenXR start-up and
// events), presenter_worker.cpp and presenter_frame.cpp (the XR worker and its frame loop),
// presenter_stereo.cpp (the per-eye hook, stereo settings and experiments), presenter_seq.cpp (Route S: eye
// pairs in the ring), presenter_ui.cpp and presenter_ui_xr.cpp (the UI layer: the game's GUI target on its
// own quad), presenter_reticle.cpp (the hand-aim dot and static images), presenter_menu.cpp and
// presenter_menu_update.cpp (menus), presenter_window.cpp (which presents reach the game's window, the render
// size's statistics), presenter_depth_glue.cpp (the depth layer's start, presenter_depth.hpp) and
// keep_active.cpp. The plain types they share are in presenter_types.hpp; the state of some parts (the D3D12
// and OpenXR objects, Route S, the UI layer, menus, the window) is in presenter_impl_state.hpp.

#include "common/retire_queue.hpp"
#include "stereo_seq/centered_matrix.hpp"
#include "ui_layer/gui_target.hpp"
#include "ui_layer/ui_settings.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/log.hpp"
#include "vkcore/player_aim.hpp"
#include "vkcore/presenter_cutscene.hpp"
#include "vkcore/presenter_depth.hpp"
#include "vkcore/presenter_impl_state.hpp"
#include "vkcore/presenter_stereo.hpp"
#include "vkcore/presenter_types.hpp"
#include "vkcore/presenter_wrist.hpp"
#include "vkcore/room_scale.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/stereo_hooks.hpp"
#include "vkcore/view_hook.hpp"
#include "vkcore/xr_presenter.hpp"
#include "xr_math/aim_check.hpp"
#include "xr_math/head_aim.hpp"
#include "xr_math/head_view.hpp"

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace evr::vkcore {

struct XrPresenter::Impl final : ViewHookSink,
                                 StereoHookSink,
                                 PresenterXrState,
                                 PresenterSeqState,
                                 PresenterUiState,
                                 PresenterMenuState,
                                 PresenterWindowState,
                                 std::enable_shared_from_this<XrPresenter::Impl> {
    explicit Impl(DeviceData& d) : dev(d), settings(readSettings()), gameLuidValid(d.luidValid) {
        std::memcpy(gameLuid, d.luid, sizeof(gameLuid));
    }

    // The game's device. Only touched while `stop` is not set: a worker left behind at shutdown may
    // outlive it.
    DeviceData& dev;
    const Settings settings;
    // The game device's LUID, copied so the worker never reads the device's data for it.
    const bool gameLuidValid;
    std::uint8_t gameLuid[VK_LUID_SIZE] = {};
    std::mutex mutex; // present, swapchain create/destroy, command objects

    std::unordered_map<VkSwapchainKHR, SwapchainState> swapchains;
    VkSwapchainKHR gameSwapchain = VK_NULL_HANDLE;
    // Present semaphores of destroyed swapchains, and ones whose present failed: destroyed once the shared
    // timeline shows kRetireAfterCopies later copies done (T-081's later fence), or at shutdown.
    RetireQueue<VkSemaphore> retiredSemaphores;
    std::unordered_map<std::uint32_t, FamilyCommands> commands;
    std::uint32_t nextSlot = 0;
    std::uint64_t timelineValue = 0;
    std::uint64_t framesCopied = 0;
    std::uint64_t framesDropped = 0;
    bool loggedFirstCopy = false, loggedFirstPresent = false, loggedBlitUnsupported = false;
    std::uint32_t lastPresentFamily = UINT32_MAX;
    ULONGLONG lastStatsTicks = 0, lastXrStatsTicks = 0;
    bool copyFailed = false;

    // Shared ring (written by the worker before ringReady is set, then read-only until shutdown).
    std::array<RingSlot, kRingSize> ring;
    VkSemaphore timeline = VK_NULL_HANDLE;
    VkFormat ringFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D ringExtent{};
    VkExtent2D eyeExtent{};     // one eye's image in the ring (the whole ring unless ringEyes is 2)
    std::uint32_t ringEyes = 1; // 2 with Route S active; set by the worker before the first ring
    std::atomic<bool> ringReady{false};
    std::atomic<bool> consumerAlive{false};
    std::atomic<std::uint64_t> latest{0}; // (timeline value << 2) | slot of the newest published image

    // Worker
    std::thread worker;
    HANDLE workerDone = nullptr;
    std::atomic<bool> stop{false};
    bool workerStarted = false;
    bool workerLeftBehind = false;
    std::uint64_t framesShapeMismatch = 0; // not copied: size or format differs and no blit on this queue
    // Under `mutex`: the game swapchain's newest shape (requested), and the shape the ring was last built
    // for (game; written by the worker). resizeRequested is set whenever they differ after a build.
    VkExtent2D requestedExtent{};
    VkFormat requestedFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D gameExtent{};
    VkFormat gameFormat = VK_FORMAT_UNDEFINED;
    bool ringBuilt = false;
    std::atomic<bool> resizeRequested{false};
    std::uint32_t maxSwapchainWidth = 16384;
    std::uint32_t maxSwapchainHeight = 16384;
    std::vector<std::int64_t> xrFormats;

    HeldCopy heldCopy; // a D3D12 copy that did not finish in time (presenter_types.hpp)

    // ---- Head tracking: shared between the XR worker and the game's camera hook ------------------------

    // Guards localSpace/viewSpace against destruction while the camera hook locates the head.
    std::shared_mutex spaceMutex;
    std::atomic<bool> trackingReady{false};
    std::atomic<bool> sessionFocused{false};
    // Room-scale, recenter, posture and the head-collision fade (room_scale.hpp).
    RoomScale room;
    // Latest predictedDisplayTime + one period, at most 1.5x the refresh (+ the lead of ETERNALVR_POSE_LEAD).
    std::atomic<XrTime> nextDisplayTime{0};
    std::atomic<XrDuration> displayPeriod{0};
    // The game FOV covering both eyes (the worker's, from its FOV reads; presenter_fov.hpp packs it), 0:
    // none.
    std::atomic<std::uint64_t> targetFov{0};
    ViewHookStatus hooks;

    // Cutscenes and glory kills (camera hook thread only; the worker reads glory.flat()).
    GloryKills glory{settings.gloryKills, settings.testGloryStart, settings.testGloryDuration};
    PickupView pickup{settings.pickupCamera}; // a pickup animation's camera (pickup_view.hpp)
    bool cutscene = false;
    CutsceneTrack cutsceneTrack; // changes, the skip key's hold, the cuts' re-base (presenter_cutscene.hpp)

    // Head aim (camera hook thread only).
    enum class AimPhase { Unchecked, Verifying, Active, Off };
    AimPhase aimPhase = AimPhase::Unchecked;
    PlayerAim playerAim;
    PlayerAim::DeltaField aimField = PlayerAim::DeltaField::Physics;
    xr_math::HeadAimState aimState;
    xr_math::IdAngles aimLastDelta;
    bool aimWritten = false;
    std::optional<float> aimMenuBody; // the body yaw held while a menu is up (aimWithHead); nullopt: none
    bool aimBodyOwn =
        false; // aimWithHead's last body came from head aim's own step (not a camera, menu, hold)
    std::uint64_t aimMenuFrames = 0;
    bool loggedNotPlayer = false;
    xr_math::AimCheck aimCheck; // Verifying: the start-up check and its later tries
    std::uint64_t aimFrames = 0;
    std::uint64_t aimCameraFrames = 0;
    std::uint64_t aimRewrites = 0;
    std::uint64_t aimRestores = 0; // rewrites back to a value head aim wrote
    ULONGLONG lastAimStatsTicks = 0;

    // Records of recent game frames (camera hook writes; render latch and present read).
    std::mutex historyMutex;
    std::array<ViewRecord, kHistorySize> history{};
    std::uint64_t latestSeq = 0;
    std::atomic<std::uint64_t> latchedSeq{0};
    // Statistics (camera hook thread unless noted).
    std::uint64_t gameViews = 0;
    std::atomic<std::uint64_t> gameTicks{0}; // head-tracked game frames, read by the Route S statistics
    std::uint64_t gameViewsSkipped = 0;
    // The render latch runs on several render job threads at once.
    std::atomic<std::uint64_t> latchMatched{0};
    std::atomic<std::uint64_t> latchUnmatched{0};
    std::atomic<ULONGLONG> lastLatchStatsTicks{0};
    std::atomic<int> loggedLatches{0};
    ULONGLONG lastHookStatsTicks = 0;
    bool loggedFirstGameView = false;
    bool loggedBadAxis = false;
    bool loggedGuardOff = false;
    int loggedPresents = 0;
    PresentViews presentViews; // presenter_types.hpp

    void onGameView(std::byte* renderView, std::byte* player) override; // ViewHookSink
    // presenter_cutscene.cpp: logs cutscene changes and, with ETERNALVR_SKIP_CINEMATICS, holds the skip key
    // during them; in a cutscene shown around the player, the body yaw re-based on its cuts, and head aim's
    // yaw re-based on the first frame after it (before aimWithHead).
    void trackCutscene(bool inCutscene);
    float cutsceneBodyYaw(float bodyYaw, const xr_math::IdViewAxis& camera, Quat head, bool cameraCut);
    void rebaseAfterCutscene(Quat headInIdTech);
    // Head aim: moves the player's view angles toward body + head; returns the body yaw axis when it
    // did (first-person play with a verified layout), nullopt to fall back to the game's own yaw.
    std::optional<xr_math::IdViewAxis>
    aimWithHead(std::byte* player, const xr_math::IdViewAxis& gameAxis, Quat headInIdTech);
    void onRenderLatch(const std::byte* renderView, const float* previousProjection) override;
    bool latestView(ViewRecord& out, std::uint64_t& gap);
    // The view a present shows: latestView's, or under Parallel Eye game frame `pick` (eye 1's; 0: none).
    bool viewForPresent(std::uint64_t pick, ViewRecord& out, std::uint64_t& gap);

    // ---- Stereo (presenter_stereo.cpp) -------------------------------------------------------------

    StereoHookStatus stereoHooks;
    StereoStats stereoStats;
    bool loggedFirstEyes = false;  // camera hook thread
    std::uint64_t eyesMissing = 0; // camera hook thread
    // Camera hook: each eye's pose, FOV, world offset and axis for this frame (xrLocateViews in VIEW space).
    void prepareEyes(ViewRecord& record, const xr_math::IdViewAxis& body, Quat headOpenXr);
    // StereoHookSink
    void onEyeView(std::byte* renderView, int viewIndex, const std::byte* firstViewG) override;
    void onEyeLatched(std::byte* renderView, int screenView) override;
    // The centred matrix's depth row read before each eye's latch (onSeqEyeView; onEyeView under Parallel Eye
    // Rendering), written back into the latched eye's (onSeqEyeLatched). Render job threads, one per eye.
    std::array<std::optional<stereo_seq::CenteredDepth>, 2> centeredDepth{};
    std::array<bool, 2> eyePoseWritten{}, eyeInCutscene{}, eyeArmsHidden{}; // with its record's flags
    std::array<std::uint64_t, 2> eyeSeq{};                                  // and its game frame
    std::atomic<std::uint64_t> centeredRepairs{0};
    std::atomic<std::uint64_t> weaponRetargets{0};
    void onSeqEyeLatched(std::byte* renderView, int eyeIndex);
    // Per-eye hook helpers: the recorded game frame whose axis the view carries; writes an eye's origin,
    // axis and explicit projection (nullopt: no usable projection, nothing written); keeps what was
    // written for the post-latch check.
    bool recordForView(const std::byte* renderView, ViewRecord& out);
    std::optional<xr_math::EngineMatrix> writeEyePose(std::byte* renderView, const EyeRecord& eye);
    void noteWritten(int slot, const xr_math::EngineMatrix& projection);
    // Worker: installs the stereo hooks (stereo mode only).
    void startStereo();
    // Worker: the projection layer's views for the shown image; false when it has no view.
    bool fillProjectionViews(std::array<XrCompositionLayerProjectionView, 2>& views);

    // ---- The depth layer (presenter_depth.hpp, presenter_depth_glue.cpp)
    // ------------------------------------

    DepthLayer depthLayer{settings.depthLayer, settings.unitsPerMetre};
    // Worker, while the ring is built (no present copies): whether the depth layer runs, its shared buffers.
    void createDepthBuffers();

    // ---- Route S (presenter_seq.cpp) ---------------------------------------------------------------

    // Per-eye hook (render job threads) for Route S: the chain's eye gets its view and flags.
    void onSeqEyeView(std::byte* renderView);
    // Worker: installs the Route S hooks; on any failure stays mono.
    void startSequential();
    // Present hook, under `mutex`: the Route S copy (pairing, halves, capture).
    VkSemaphore seqCopyForPresent(VkQueue queue,
                                  std::uint32_t family,
                                  SwapchainState& sc,
                                  std::uint32_t imageIndex,
                                  const VkPresentInfoKHR* info,
                                  FamilyCommands& fc,
                                  std::uint64_t completed,
                                  const stereo_seq::PresentMatch& match);
    // Present hook, under `mutex`: a tagged present that is not copied still moves the pairing on.
    void seqPresentNotCopied(const stereo_seq::PresentMatch& match);
    void releasePendingSlot();
    bool viewBySeq(std::uint64_t seq, ViewRecord& out);
    void logSeqStats();

    // ---- The in-headset capture (presenter_snapshot.cpp, bug_capture.hpp) ---------------------------

    // Present hook, under `mutex`. Eye L's capture buffer for Route S pair `pairIndex` of game tick `tick`:
    // the in-headset capture's when one is wanted, else the periodic capture's (or none).
    VkBuffer pairCaptureBuffer(const SwapchainState& sc,
                               std::uint64_t completed,
                               std::uint64_t pairIndex,
                               std::uint64_t tick);
    // A mono frame's: the in-headset capture's when one is wanted (under Route S once no pair came for
    // bug_capture::kMonoAfterSeconds), else none.
    VkBuffer monoCaptureBuffer(const SwapchainState& sc, std::uint64_t completed);
    // Parallel Eye Rendering's (the eye copy): eye L's and eye R's, both halves of the ring slot, or none;
    // the text file describes the view viewForPresent(`pick`) gives the slot.
    std::array<VkBuffer, 2>
    ringCaptureBuffers(const SwapchainState& sc, std::uint64_t completed, std::uint64_t pick);
    // After the copy into `buffer` from above was submitted (timeline `value`; 0: it was not): notes whether
    // the GUI target came along; with `complete` (a mono frame, both ring eyes) the capture is done with it.
    void captureCopied(VkBuffer buffer, std::uint64_t value, bool complete);

    // ---- UI layer (presenter_ui.cpp, presenter_ui_xr.cpp) -------------------------------------------------

    // Worker: locates the engine side (and the composite hook) once the guard is armed.
    void startUi();
    // Worker, while the ring is built: the shared GUI images (none when the engine side was not located).
    void createUiImages();
    bool importUiImage(HANDLE handle, UiImage& ui);
    // Under `mutex` (or while no present can copy): frees them.
    void destroyUiImages();
    // Present hook, under `mutex`: records the GUI target's copy into `slot`'s UI image (with `panel`, also
    // into the mirror's, for that swapchain); false when the target fails a check (no GUI image then).
    bool recordUiCopy(VkCommandBuffer cb, std::uint32_t family, RingSlot& slot, const SwapchainState* panel);
    void logUiStats();
    // Worker: the UI swapchain image for the next copy (acquired and waited on), its copy, its release.
    bool createUiSwapchain();
    bool acquireUiXrImage();
    void recordUiXrCopy(RingSlot& slot);
    void finishUiXrCopy();
    // Worker, every rendered frame after its layers: a gap in the game's images that ends with this frame's
    // new image gets one line (ui_layer::GuiGapTally). `imageBefore`: uiImageQpc before this frame's image.
    void noteUiGap(LONGLONG imageBefore, bool newImage, bool shown);
    // Worker: the head-locked quad with the newest GUI image; false when there is none (or it is stale).
    bool fillUiQuad(XrCompositionLayerQuad& quad);
    // Worker: the reticle quad on the weapon hand's ray under hand aim; false when there is none.
    bool fillReticleQuad(XrCompositionLayerQuad& quad);
    bool createReticle();
    // Worker: a one-image swapchain holding `pixels` (width x height RGBA8, premultiplied); `what` names it
    // in the log.
    bool createStaticImage(XrSwapchain& swapchain,
                           const std::vector<std::uint8_t>& pixels,
                           std::uint32_t width,
                           std::uint32_t height,
                           const char* what);
    void destroyUiXrObjects();
    XrSwapchain detachUiSwapchain(); // the UI swapchain's state reset; its handle returned, not destroyed
    // True while the UI quad has a GUI image fresh enough to show.
    [[nodiscard]] bool uiFresh() const;

    WristHud wrist; // worker only: ETERNALVR_HUD=wrist (presenter_wrist.hpp)
    // ---- Menus (presenter_menu.cpp, presenter_menu_update.cpp, docs/VR_MENUS.md) --------------------------

    // Worker, once the multiplayer guard is armed: locates the game's cursor.
    void startMenu();
    // Worker, every rendered frame before the layers: whether a menu is up and a panel can show it
    // (`panelContent`), the panel's placement, the pointer's rays and the router's events into the game.
    void updateMenu(XrTime time, bool panelContent);
    // Worker: `quad` moved onto the menu panel (world-locked, LOCAL), its image cut to the 16:9 band the
    // game's menus are laid out in (ETERNALVR_UI_CROP, ui_layer::wideContentRect).
    void placeOnMenuPanel(XrCompositionLayerQuad& quad) const;
    bool placeMenuPanel(XrTime time); // the panel in front of the head (yaw only); false: no head pose
    std::optional<Pose> locateHead(XrTime time) const; // the head in LOCAL; nullopt when not tracked
    // Worker: the part of the game's image the menu panel shows (its 16:9 band, or all of it).
    [[nodiscard]] ui_layer::PixelRect menuContent() const;
    // Worker: the beam and the dot; the number of quads filled (0 to 2), written from `first` on.
    std::uint32_t fillPointerQuads(XrCompositionLayerQuad* first);
    void logMenuStats();
    void destroyMenuXrObjects();

    // ---- The game's window (presenter_window.cpp) ----------------------------------------------------

    // Whether this present reaches the window (sets windowHold); true whenever gating is off.
    bool decideWindow(const VkPresentInfoKHR* info, stereo_seq::PresentKind kind);
    // The desktop mirror step for eye 0 or 1: with gating the window only receives the mirrored eye (black
    // for off); without it the kept eye is copied over the other (presenter_mirror.hpp).
    [[nodiscard]] stereo_seq::MirrorStep windowMirrorStep(std::uint32_t eye, bool toWindow) const;
    void holdImage(VkSwapchainKHR swapchain, std::uint32_t image, std::uint64_t value);
    void handBackImages(std::uint64_t completed);
    void dropHeldImages(VkSwapchainKHR swapchain);
    void refreshDisplayRate(); // worker loop, every few seconds: windowRefreshHz, never on the present hook
    void logWindowStats();
    // Worker, every 10 s while the render size is on (virtual_client.hpp): its size and counters.
    std::uint64_t lastSizeAnswers = 0;
    std::uint64_t lastSizeSuboptimal = 0;
    void logSizeStats();

    // ---- Vulkan side -----------------------------------------------------------------------------

    void onSwapchainCreated(VkSwapchainKHR swapchain, const VkSwapchainCreateInfoKHR& info);
    void onSwapchainDestroyed(VkSwapchainKHR swapchain);
    VkResult present(VkQueue queue, std::uint32_t family, const VkPresentInfoKHR* info);
    // Under `mutex`: copies the presented image into the ring and returns the semaphore the present
    // must wait on instead of the game's, or VK_NULL_HANDLE to pass the present through unchanged.
    VkSemaphore copyForPresent(VkQueue queue,
                               std::uint32_t family,
                               const VkPresentInfoKHR* info,
                               VkSwapchainKHR& copiedSwapchain,
                               std::uint32_t& copiedImage);
    void replacePresentSemaphore(VkSwapchainKHR swapchain, std::uint32_t image, VkSemaphore used);
    FamilyCommands* commandsFor(std::uint32_t family);
    bool recordCopy(VkCommandBuffer cb,
                    const SwapchainState& sc,
                    VkImage source,
                    std::uint32_t family,
                    RingSlot& slot,
                    const CopyTarget& target,
                    VkBuffer captureBuffer);
    // Under `mutex`: a free slot whose last write and command buffer have finished (marked kSlotWriting),
    // never the newest published one; kRingSize when there is none.
    std::uint32_t acquireFreeSlot(const FamilyCommands& fc, std::uint64_t completed);
    // Under `mutex`: records and submits the copy of the presented image into a slot acquired above; the
    // timeline value it signals, or 0 when nothing was submitted.
    std::uint64_t submitCopy(VkQueue queue,
                             std::uint32_t family,
                             const VkPresentInfoKHR* info,
                             SwapchainState& sc,
                             std::uint32_t imageIndex,
                             FamilyCommands& fc,
                             std::uint32_t slotIndex,
                             const CopyTarget& target,
                             VkBuffer captureBuffer);
    // Under `mutex`: hands a written slot to the worker.
    void publishSlot(std::uint32_t slotIndex, std::uint64_t value);
    bool takeRenderedSlot(std::uint32_t& slotIndex, std::uint64_t& value, LONGLONG frameStart);
    void logCopyStats(const SwapchainState& sc, std::uint32_t family);
    bool importRing(const std::array<HANDLE, kRingSize>& imageHandles, HANDLE fenceHandle);
    void shutdown();
    void destroyVulkanObjects();

    // ---- Worker ----------------------------------------------------------------------------------

    void runWorker();
    void runWorkerStages(); // runWorker's start-up and frames, under its guard
    bool createXrSwapchain();
    // Worker: the floor space (LOCAL_FLOOR, else STAGE) and the fade layer; both optional.
    void createRoomObjects();
    // Rebuilds the ring and the XR swapchain for the game swapchain's new size or format (worker).
    void recreateRing();
    // Sets ringExtent and eyeExtent for a game image of `game` (under `mutex`).
    void setRingExtent(VkExtent2D game);
    // Under `mutex`, after a build: asks for another rebuild if the game's swapchain changed meanwhile.
    void requestRebuildIfStale();
    bool loadOpenXr();
    bool createXrInstance(bool quiet = false); // quiet: failures are neither logged nor shown
    // Replaces localSpace with an upright space from STAGE when the runtime's LOCAL is tilted
    // (xr_math/upright_space.hpp). Called once, right after localSpace is created.
    void makeLocalUpright();
    bool waitForSystem();
    // Worker, once the system is known: the runtime's view limits to the render size (virtual_client.hpp).
    void reportViewLimits();
    // Worker: waits (up to kRenderSizeWaitMs) for the game's swapchain to take the render size, so the ring
    // is built once at that size.
    void awaitRenderSize();
    static constexpr ULONGLONG kRenderSizeWaitMs = 3000;
    bool createD3D12AndSession();
    // Worker, after a loss (presenter_reconnect.cpp); createSession is createD3D12AndSession's session half.
    bool reconnect();
    bool createSession();
    bool createRing();
    void pollEvents();
    void frame();
    // A frame call or xrPollEvent returned XR_ERROR_SESSION_LOST, _INSTANCE_LOST or XR_ERROR_RUNTIME_FAILURE:
    // the same path as a lost session (the worker leaves its loop to reconnect). False for any other result.
    bool loseOnRuntimeFailure(XrResult result, const char* call);
    std::uint32_t endFrameFailures = 0;
    void placeQuad(XrTime time);
    void updateTargetFov(XrTime time);
    void updateImage(LONGLONG frameStart);
    void completeCopy(std::uint32_t slotIndex, std::uint64_t value, const ViewRecord& view, bool hasView);
    // Worker (presenter_frame_log.cpp): the frame table, and the shown view's lateness for the display lead.
    void openFrameLog();
    void noteShownView(const XrFrameState& state);
    void logFrame(const XrFrameState& state);
    // Worker, after each frame (presenter_clock.cpp): the runtime's clock; a stall restarts the session.
    void watchFrameClock(const XrFrameState& state);
    // Worker, after each frame: the display period's watch and, every 10 s, the xr line and the statistics.
    void afterFrame(const XrFrameState& state);
    // Worker, every 10 s: the game's present, tick and stereo pair rates beside the XR frame rate.
    void logRates();
    // Worker, with the rates: the D3D12 debug layer's messages (ETERNALVR_D3D12_DEBUG) and a removed device.
    void logD3dHealth();
    bool loggedDeviceRemoved = false, loggedCloseFailure = false;
    void destroyXrObjects();
    const char* xrText(XrResult result, char (&buffer)[XR_MAX_RESULT_STRING_SIZE]) const;
};

} // namespace evr::vkcore
