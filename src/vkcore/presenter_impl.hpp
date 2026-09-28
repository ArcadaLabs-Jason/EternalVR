#pragma once

// Internal state of the XR presenter (xr_presenter.hpp), shared by its source files:
// xr_presenter.cpp (settings, shutdown, public entry points), presenter_copy.cpp (the present hook's
// copy into the ring), presenter_ring.cpp (the shared ring: creation, import, rebuild),
// presenter_head.cpp (the camera hook, head aim, the render latch), presenter_xr.cpp (OpenXR start-up
// and events), presenter_frame.cpp (the XR worker and its frame loop), presenter_stereo.cpp (the per-eye
// hook, stereo settings and experiments), presenter_seq.cpp (Route S: eye pairs in the ring),
// presenter_ui.cpp (the UI layer: the game's GUI target on its own quad), presenter_reticle.cpp (the
// hand-aim dot and static images), presenter_menu.cpp (menus), presenter_window.cpp (which presents reach
// the game's window, the render size's statistics) and keep_active.cpp. The plain types they
// share are in presenter_types.hpp.

#include "common/retire_queue.hpp"
#include "features/input/capture_chord.hpp"
#include "features/menu/menu_router.hpp"
#include "features/menu/panel_pointer.hpp"
#include "stereo_seq/centered_matrix.hpp"
#include "stereo_seq/eye_pairing.hpp"
#include "ui_layer/gui_target.hpp"
#include "ui_layer/ui_settings.hpp"
#include "vkcore/backdrop_probe.hpp"
#include "vkcore/cinema_view.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/eye_capture.hpp"
#include "vkcore/log.hpp"
#include "vkcore/player_aim.hpp"
#include "vkcore/presenter_fade.hpp"
#include "vkcore/presenter_mirror.hpp"
#include "vkcore/presenter_stereo.hpp"
#include "vkcore/presenter_types.hpp"
#include "vkcore/room_scale.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/stereo_hooks.hpp"
#include "vkcore/ui_capture.hpp"
#include "vkcore/ui_wash.hpp"
#include "vkcore/view_hook.hpp"
#include "vkcore/xr_presenter.hpp"
#include "xr_math/display_lead.hpp"
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
    bool loggedFirstCopy = false;
    bool loggedFirstPresent = false;
    bool loggedBlitUnsupported = false;
    std::uint32_t lastPresentFamily = UINT32_MAX;
    ULONGLONG lastStatsTicks = 0;
    ULONGLONG lastXrStatsTicks = 0;
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
    std::atomic<std::uint64_t> latest{0}; // (timeline value << 2) | slot of the newest written slot

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

    // A D3D12 copy that did not finish in time: its slot and swapchain image stay held until it does.
    bool copyStalled = false;
    std::uint32_t stalledSlot = 0;
    std::uint64_t stalledValue = 0;
    ViewRecord stalledView;
    bool stalledHasView = false;

    // D3D12 (worker thread only, then released at shutdown)
    ComPtr<ID3D12Device> d3dDevice;
    ComPtr<ID3D12InfoQueue> d3dInfoQueue; // ETERNALVR_D3D12_DEBUG only
    std::uint64_t d3dMessagesLogged = 0;
    ComPtr<ID3D12CommandQueue> d3dQueue;
    ComPtr<ID3D12CommandAllocator> d3dAllocator;
    ComPtr<ID3D12GraphicsCommandList> d3dList;
    ComPtr<ID3D12Fence> sharedFence; // signalled by Vulkan: "slot written"
    ComPtr<ID3D12Fence> copyFence;   // signalled by our D3D12 queue after each copy
    std::uint64_t copyFenceValue = 0;
    HANDLE copyEvent = nullptr;

    // OpenXR (worker thread only)
    HMODULE xrLoader = nullptr;
    XrFunctions xr;
    XrInstance instance = XR_NULL_HANDLE;
    controllers::ProfileSupport controllerProfiles; // the instance's controller-profile extensions
    bool perfCounterTime = false; // XR_KHR_win32_convert_performance_counter_time enabled
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace localSpace = XR_NULL_HANDLE;
    XrSpace viewSpace = XR_NULL_HANDLE;
    XrSpace floorSpace = XR_NULL_HANDLE; // LOCAL_FLOOR or STAGE when the runtime has one (posture, height)
    XrSwapchain xrSwapchain = XR_NULL_HANDLE;
    std::int64_t xrSwapchainFormat = 0;
    FadeLayer fadeLayer;
    std::vector<ID3D12Resource*> xrImages;
    XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
    bool sessionRunning = false;
    bool sessionLost = false;
    std::int64_t acquiredIndex = -1;
    bool acquiredWaited = false;
    bool hasImage = false;
    bool quadPlaced = false;
    // Set by the game thread when a cutscene starts on the flat screen: the worker places it again.
    std::atomic<bool> replaceScreen{false};
    int placeAttempts = 0;
    CinemaView cinemaView; // cutscenes at a flat display's shape (camera hook and worker)
    XrPosef quadPose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -kScreenDistanceMetres}};
    XrExtent2Df quadSize{kScreenWidthMetres, kScreenWidthMetres * 9.0f / 16.0f};
    std::uint64_t lastConsumed = 0;
    std::uint64_t xrFrames = 0;
    std::uint64_t xrCopies = 0;
    std::uint64_t xrRepeats = 0;
    std::uint64_t xrProjectionFrames = 0;
    std::uint64_t xrQuadFrames = 0;
    ViewRecord shownView; // the view of the image in the XR swapchain
    bool shownHasView = false;
    bool fovChecked = false;
    bool loggedFirstProjection = false;
    bool loggedGuardCinema = false; // XR worker only
    XrViewConfigurationType viewConfig = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    std::FILE* frameLog = nullptr;
    xr_math::DisplayLead displayLead; // worker only (ETERNALVR_POSE_LEAD)
    double poseAgeSum = 0.0;
    double poseAgeMax = 0.0;
    std::uint64_t poseAgeCount = 0;
    // The game's own rates beside the XR rate in the 10 s line: presents (render thread), stereo pairs
    // handed to the worker (present hook); the worker keeps the last values.
    std::atomic<std::uint64_t> gamePresents{0};
    std::atomic<std::uint64_t> pairsPublished{0};
    std::uint64_t lastRatePresents = 0;
    std::uint64_t lastRateTicks = 0;
    std::uint64_t lastRatePairs = 0;
    std::uint64_t lastRateXrFrames = 0;
    std::uint64_t lastRateXrCopies = 0;
    LONGLONG lastRateQpc = 0;

    // ---- Head tracking: shared between the XR worker and the game's camera hook ------------------------

    // Guards localSpace/viewSpace against destruction while the camera hook locates the head.
    std::shared_mutex spaceMutex;
    std::atomic<bool> trackingReady{false};
    std::atomic<bool> sessionFocused{false};
    // Room-scale, recenter, posture and the head-collision fade (room_scale.hpp).
    RoomScale room;
    // Latest predictedDisplayTime + one period (+ the display lead under ETERNALVR_POSE_LEAD).
    std::atomic<XrTime> nextDisplayTime{0};
    std::atomic<XrDuration> displayPeriod{0};
    // The game FOV covering both eyes (set by the worker from xrLocateViews once).
    std::atomic<bool> targetFovValid{false};
    std::atomic<float> targetFovX{0.0f};
    std::atomic<float> targetFovY{0.0f};
    ViewHookStatus hooks;

    // Cutscenes (camera hook thread only).
    bool cutscene = false;
    ULONGLONG cutsceneSince = 0;
    std::uint64_t cutsceneChanges = 0;
    bool skipHolding = false;
    ULONGLONG skipHoldStart = 0;
    ULONGLONG skipReleased = 0;
    std::uint64_t skipHolds = 0;

    // Head aim (camera hook thread only).
    enum class AimPhase { Unchecked, Verifying, Active, Off };
    AimPhase aimPhase = AimPhase::Unchecked;
    PlayerAim playerAim;
    PlayerAim::DeltaField aimField = PlayerAim::DeltaField::Physics;
    xr_math::HeadAimState aimState;
    xr_math::IdAngles aimLastDelta;
    bool aimWritten = false;
    std::optional<float> aimMenuBody; // the body yaw held while a menu is up (aimWithHead); nullopt: none
    std::uint64_t aimMenuFrames = 0;
    bool loggedNotPlayer = false;
    int aimChecks = 0;
    int aimPhysicsMatches = 0;
    int aimStateMatches = 0;
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
    std::uint64_t presentsWithView = 0;
    std::uint64_t presentsWithoutView = 0;
    std::uint64_t presentSeqGapSum = 0;

    // ViewHookSink
    void onGameView(std::byte* renderView, std::byte* player) override;
    // Logs cutscene changes and, with ETERNALVR_SKIP_CINEMATICS, holds the skip key during them.
    void trackCutscene(bool inCutscene);
    // Head aim: moves the player's view angles toward body + head; returns the body yaw axis when it
    // did (first-person play with a verified layout), nullopt to fall back to the game's own yaw.
    std::optional<xr_math::IdViewAxis>
    aimWithHead(std::byte* player, const xr_math::IdViewAxis& gameAxis, Quat headInIdTech);
    void onRenderLatch(const std::byte* renderView, const float* previousProjection) override;
    bool latestView(ViewRecord& out, std::uint64_t& gap);

    // ---- Stereo (presenter_stereo.cpp) -------------------------------------------------------------

    StereoHookStatus stereoHooks;
    StereoStats stereoStats;
    bool loggedFirstEyes = false;  // camera hook thread
    std::uint64_t eyesMissing = 0; // camera hook thread
    // Camera hook: each eye's pose, FOV, world offset and axis for this frame (xrLocateViews in VIEW space).
    void prepareEyes(ViewRecord& record, const xr_math::IdViewAxis& body, Quat headOpenXr);
    // StereoHookSink
    void onEyeView(std::byte* renderView, int viewIndex, const std::byte* firstViewG) override;
    void onEyeLatched(std::byte* renderView, int viewIndex) override;
    // Route S: the centred matrix's depth row read before each eye's latch (onSeqEyeView), written back
    // into the latched eye's (onSeqEyeLatched). Render job threads; the chains never overlap.
    std::array<std::optional<stereo_seq::CenteredDepth>, 2> centeredDepth{};
    std::array<bool, 2> eyePoseWritten{};
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

    // ---- Route S (presenter_seq.cpp) ---------------------------------------------------------------

    std::atomic<bool> seqActive{false}; // the Route S hooks are installed: two-eye ring, pairing
    // Present hook, under `mutex`:
    stereo_seq::EyePairing pairing;
    std::uint32_t pendingSlot = kRingSize; // slot holding the left half of the pending pair
    std::uint64_t pairsWithoutRecord = 0;
    EyeCapture capture;
    DesktopMirror mirror; // the game's window shows one eye (ETERNALVR_MIRROR)
    ULONGLONG lastSeqStatsTicks = 0;
    SeqCounters lastSeqCounters;
    std::uint64_t lastSeqGameTicks = 0;
    stereo_seq::EyePairing::Stats lastPairStats;
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
    // After the copy into `buffer` from above was submitted (timeline `value`; 0: it was not): notes whether
    // the GUI target came along; a mono frame's capture is complete with it. Nothing without a buffer.
    void captureCopied(VkBuffer buffer, std::uint64_t value, bool mono);
    // Arms the eye and UI captures for the in-headset capture (names, the text file); false when it has to
    // wait for a later frame.
    bool armCapture(const SwapchainState& sc, std::uint64_t pairIndex, std::uint64_t tick, bool mono);
    // The armed capture was taken into `buffer`, or disarmed when there is none.
    VkBuffer takeArmedCapture(VkBuffer buffer);

    // ---- UI layer (presenter_ui.cpp) --------------------------------------------------------------

    VkExtent2D uiExtent{};           // the shared GUI images' size (the game's swapchain size)
    bool uiReady = false;            // the shared GUI images exist (worker sets it before the ring is ready)
    UiCapture uiCapture;             // ETERNALVR_CAPTURE_UI (present hook)
    std::uint64_t uiCaptures = 0;    // present hook
    std::uint64_t uiNotCaptured = 0; // present hook
    BackdropProbe uiBackdrop;        // the crosshair mask's backdrop test (present hook)
    std::uint64_t uiUnmaskedBackdrop = 0; // present hook: hand-aim copies not masked for a backdrop
    std::uint64_t uiUnmaskedMenu = 0;     // ... for a menu
    ui_layer::TargetCheck uiLastCheck = ui_layer::TargetCheck::Ok;
    ULONGLONG lastUiStatsTicks = 0;
    // Worker only:
    XrSwapchain uiSwapchain = XR_NULL_HANDLE;
    std::vector<ID3D12Resource*> uiXrImages;
    std::int64_t uiAcquired = -1;
    bool uiAcquiredWaited = false;
    bool uiCopyPending = false; // a D3D12 copy into the acquired UI image is recorded
    bool uiHasImage = false;
    bool uiFailed = false;
    LONGLONG uiShownQpc = 0; // when the newest GUI image reached the UI swapchain
    std::uint64_t uiXrCopies = 0;
    UiWash uiWash; // ETERNALVR_UI_WASH: the additive wash taken out before the copy (off in menus)
    bool uiWashLogged = false;
    XrSwapchain reticleSwapchain = XR_NULL_HANDLE; // hand aim: a static dot image
    bool reticleFailed = false;
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
    // True while the UI quad has a GUI image fresh enough to show.
    [[nodiscard]] bool uiFresh() const;

    // ---- Menus (presenter_menu.cpp, docs/VR_MENUS.md) -----------------------------------------------

    // Worker only.
    bool menuInstalled = false;   // the game's menu cursor was located
    bool menuOn = false;          // this frame: the menu is on the world-locked panel with the pointer
    bool menuPanelPlaced = false; // menuPanel holds a placement
    menu::Panel menuPanel;        // in LOCAL
    bool menuHeld = false;        // this frame: the cursor went, the panel stays while the backdrop does
    double menuCursorGone = 0.0;  // when the cursor last went while the panel was up (qpc seconds)
    bool menuPopup = false;       // the menu up is a popup the game raised by itself (menu_router.hpp)
    bool menuDossier = false;     // the menu is the Dossier the controllers asked for (opens on its map)
    bool menuMapPage = false;     // the router takes the Dossier's map page to be up (logged on change)
    std::optional<Pose> menuRoom; // the room transform last seen with the panel up (re-anchor check)
    std::atomic<bool> menuReplace{false}; // the runtime moved LOCAL: an open panel is placed again
    // Worker writes, present hook reads: a menu is up (the cursor, the panel or its hold); no crosshair
    // mask meanwhile.
    std::atomic<bool> menuUp{false};
    std::optional<menu::MenuRouter> menuRouter;
    input::CaptureChord menuChord; // the capture chord's hold on the triggers (capture_chord.hpp)
    struct MenuPointer {
        bool visible = false; // draw the beam (and the dot when it hits)
        Vec3 from;            // the hand
        Vec3 to;              // the hit, or a point along the ray when it misses
        Vec3 eye;             // the head, which the beam turns to face
        bool hit = false;
        Pose dot;
        float dotSide = 0.0f;
    };
    MenuPointer menuPointer;
    XrSwapchain beamSwapchain = XR_NULL_HANDLE;
    bool beamFailed = false;
    double menuPanelSeen = 0.0; // when a panel last had something to show (qpc seconds)
    std::uint64_t menuFrames = 0;
    ULONGLONG lastMenuStatsTicks = 0;
    // Worker, once the multiplayer guard is armed: locates the game's cursor.
    void startMenu();
    // Worker, every rendered frame before the layers: whether a menu is up and a panel can show it
    // (`panelContent`), the panel's placement, the pointer's rays and the router's events into the game.
    void updateMenu(XrTime time, bool panelContent);
    // Worker: `quad` moved onto the menu panel (world-locked, LOCAL), its image cut to the 16:9 band the
    // game's menus are laid out in (ETERNALVR_UI_CROP, ui_layer::wideContentRect).
    void placeOnMenuPanel(XrCompositionLayerQuad& quad) const;
    bool placeMenuPanel(XrTime time); // the panel in front of the head (yaw only); false: no head pose
    // Worker: the part of the game's image the menu panel shows (its 16:9 band, or all of it).
    [[nodiscard]] ui_layer::PixelRect menuContent() const;
    // Worker: the beam and the dot; the number of quads filled (0 to 2), written from `first` on.
    std::uint32_t fillPointerQuads(XrCompositionLayerQuad* first);
    void logMenuStats();
    void destroyMenuXrObjects();

    // ---- The game's window (presenter_window.cpp) ----------------------------------------------------

    // Present hook, under the mutex. With VK_KHR_swapchain_maintenance1 under Route S only the presents the
    // window shows reach it; the others are handed back once their ring copy is done.
    bool windowGateChecked = false;
    bool windowGate = false; // presents are gated (the extension is on and ETERNALVR_WINDOW_PRESENTS allows)
    stereo_seq::WindowPresentGate windowPresents;
    bool windowHold = false; // this present is handed back (decideWindow; read by present())
    std::uint64_t lastSubmittedValue = 0;
    struct HeldImage {
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        std::uint32_t image = 0;
        std::uint64_t value = 0; // the ring copy that last used it
    };
    std::vector<HeldImage> heldImages;
    std::uint64_t imagesHandedBack = 0;
    std::uint64_t handBackFailures = 0;
    double windowRefreshHz = 0.0;
    ULONGLONG lastRefreshTicks = 0;
    ULONGLONG lastWindowStatsTicks = 0;
    stereo_seq::WindowPresentGate::Counters lastWindowCounters;
    // Whether this present reaches the window (sets windowHold); true whenever gating is off.
    bool decideWindow(const VkPresentInfoKHR* info, stereo_seq::PresentKind kind);
    // The desktop mirror step for eye 0 or 1: with gating the window only receives the mirrored eye (black
    // for off); without it the kept eye is copied over the other (presenter_mirror.hpp).
    [[nodiscard]] stereo_seq::MirrorStep windowMirrorStep(std::uint32_t eye, bool toWindow) const;
    void holdImage(VkSwapchainKHR swapchain, std::uint32_t image, std::uint64_t value);
    void handBackImages(std::uint64_t completed);
    void dropHeldImages(VkSwapchainKHR swapchain);
    double displayRefresh();
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
    void logCopyStats(const SwapchainState& sc, std::uint32_t family);
    bool importRing(const std::array<HANDLE, kRingSize>& imageHandles, HANDLE fenceHandle);
    void shutdown();
    void destroyVulkanObjects();

    // ---- Worker ----------------------------------------------------------------------------------

    void runWorker();
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
    bool createXrInstance();
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
    bool createRing();
    void pollEvents();
    void frame();
    // A frame call or xrPollEvent returned a result that means the runtime or the session is gone
    // (XR_ERROR_SESSION_LOST, XR_ERROR_INSTANCE_LOST, XR_ERROR_RUNTIME_FAILURE): the same path as a lost
    // session (the game continues flat, the worker leaves its loop). False for any other result.
    bool loseOnRuntimeFailure(XrResult result, const char* call);
    std::uint32_t endFrameFailures = 0;
    void placeQuad(XrTime time);
    void updateTargetFov(XrTime time);
    void updateImage();
    void completeCopy(std::uint32_t slotIndex, std::uint64_t value, const ViewRecord& view, bool hasView);
    // Worker (presenter_frame_log.cpp): the frame table, and the shown view's lateness for the display lead.
    void openFrameLog();
    void noteShownView(const XrFrameState& state);
    void logFrame(const XrFrameState& state);
    // Worker, every 10 s: the game's present, tick and stereo pair rates beside the XR frame rate.
    void logRates();
    // Worker, with the rates: the D3D12 debug layer's messages (ETERNALVR_D3D12_DEBUG) and a removed device.
    void logD3dHealth();
    bool loggedDeviceRemoved = false;
    bool loggedCloseFailure = false;
    void destroyXrObjects();
    const char* xrText(XrResult result, char (&buffer)[XR_MAX_RESULT_STRING_SIZE]) const;
};

} // namespace evr::vkcore
