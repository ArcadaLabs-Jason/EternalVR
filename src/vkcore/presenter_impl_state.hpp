#pragma once

// The XR presenter's state by part, inherited by XrPresenter::Impl (presenter_impl.hpp), which declares the
// member functions that use it: the worker's D3D12 and OpenXR objects, Route S's pairing, the UI layer, the
// menus and the game's window.

#include "features/input/capture_chord.hpp"
#include "features/menu/menu_router.hpp"
#include "features/menu/panel_follow.hpp"
#include "features/menu/panel_pointer.hpp"
#include "features/tracking/fov_watch.hpp"
#include "stereo_seq/desktop_window.hpp"
#include "stereo_seq/eye_pairing.hpp"
#include "ui_layer/gui_target.hpp"
#include "vkcore/backdrop_probe.hpp"
#include "vkcore/cinema_view.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/eye_capture.hpp"
#include "vkcore/motion_capture.hpp"
#include "vkcore/presenter_alt.hpp"
#include "vkcore/presenter_fade.hpp"
#include "vkcore/presenter_markers.hpp"
#include "vkcore/presenter_mirror.hpp"
#include "vkcore/presenter_refresh.hpp"
#include "vkcore/presenter_swapchains.hpp"
#include "vkcore/presenter_types.hpp"
#include "vkcore/presenter_vignette.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/ui_capture.hpp"
#include "vkcore/ui_wash.hpp"
#include "vkcore/wait_stats.hpp"
#include "xr_math/display_lead.hpp"

#include <windows.h>

#include <d3d12.h>
#include <wrl/client.h>

#include <openxr/openxr.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

namespace evr::vkcore {

// ---- D3D12 and OpenXR (the XR worker: presenter_xr.cpp, presenter_worker.cpp, presenter_frame.cpp) --------

struct PresenterXrState {
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
    bool perfCounterTime = false;                   // XR_KHR_win32_convert_performance_counter_time enabled
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace localSpace = XR_NULL_HANDLE;
    XrSpace viewSpace = XR_NULL_HANDLE;
    XrSpace floorSpace = XR_NULL_HANDLE; // LOCAL_FLOOR or STAGE when the runtime has one (posture, height)
    XrSwapchainSet xrSwapchains; // every XR swapchain made, checked before use (presenter_swapchains.hpp)
    XrSwapchain xrSwapchain = XR_NULL_HANDLE;
    std::int64_t xrSwapchainFormat = 0;
    FadeLayer fadeLayer;
    VignetteLayer vignette; // ETERNALVR_VIGNETTE (presenter_vignette.hpp)
    XrSwapchainSet::Images xrImages;
    XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
    bool sessionRunning = false;
    ClockWatchState clock; // presenter_types.hpp
    ViewerState viewer;    // presenter_types.hpp
    XrLossState loss;      // presenter_types.hpp
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
    std::atomic<std::uint64_t> lastConsumed{0}; // the worker's; the present hook keeps the newest after it
    std::uint64_t xrFrames = 0;
    std::uint64_t xrCopies = 0;
    std::uint64_t xrRepeats = 0;
    std::uint64_t xrBusyRepeats = 0, xrNewestWaits = 0, xrNewestTaken = 0; // no image, waits, newest
    WaitStats copyWait, newestWait; // the worker's waits: its D3D12 copy, the newest image's frame
    RuntimeCallTimes inRuntime;     // the worker's time inside xrWaitFrame and the like (presenter_types.hpp)
    std::uint64_t xrProjectionFrames = 0, xrQuadFrames = 0;
    ViewRecord shownView; // the view of the image in the XR swapchain
    bool shownHasView = false;
    FrameLogEyes frameLogEyes;   // the frames table's eye and repeat columns (presenter_types.hpp)
    tracking::FovWatch fovWatch; // the headset's FOV, read again during the session (presenter_fov.cpp)
    bool loggedFirstProjection = false, loggedGuardCinema = false; // XR worker only
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
    RateMarks lastRates;
    RefreshLog refresh; // worker: the display period's changes and summary (presenter_refresh.hpp)
};

// ---- Route S (presenter_seq.cpp) ---------------------------------------------------------------

struct PresenterSeqState {
    std::atomic<bool> seqActive{false}; // the Route S hooks are installed: two-eye ring, pairing
    // Present hook, under `mutex`:
    stereo_seq::EyePairing pairing;
    AltPresentState alt; // ETERNALVR_ALTERNATE_EYES instead of `pairing` (presenter_alt.cpp)
    std::uint32_t pendingSlot = kRingSize; // slot holding the left half of the pending pair
    std::uint64_t pairsWithoutRecord = 0;
    EyeCapture capture;
    MotionCapture motionCapture; // ETERNALVR_CAPTURE_MOTION: both eyes' velocity images with eye R's copy
    DesktopMirror mirror;        // the game's window shows one eye (ETERNALVR_MIRROR)
    ULONGLONG lastSeqStatsTicks = 0;
    SeqCounters lastSeqCounters;
    std::uint64_t lastSeqGameTicks = 0;
    stereo_seq::EyePairing::Stats lastPairStats;
};

// ---- UI layer (presenter_ui.cpp, presenter_ui_xr.cpp) -----------------------------------------------------

struct PresenterUiState {
    VkExtent2D uiExtent{};           // the shared GUI images' size (the game's swapchain size)
    bool uiReady = false;            // the shared GUI images exist (worker sets it before the ring is ready)
    UiCapture uiCapture;             // ETERNALVR_CAPTURE_UI (present hook)
    std::uint64_t uiCaptures = 0;    // present hook
    std::uint64_t uiNotCaptured = 0; // present hook
    BackdropProbe uiBackdrop;        // the crosshair mask's backdrop test (present hook)
    std::uint64_t uiUnmaskedBackdrop = 0; // present hook: hand-aim copies not masked for a backdrop
    std::uint64_t uiUnmaskedMenu = 0;     // ... for a menu
    ui_layer::HoldTally uiHold; // ... for the crosshair hold, and its changes (ui_layer/crosshair.hpp)
    ui_layer::TargetCheck uiLastCheck = ui_layer::TargetCheck::Ok;
    ULONGLONG lastUiStatsTicks = 0;
    // Worker only:
    XrSwapchain uiSwapchain = XR_NULL_HANDLE;
    XrSwapchainSet::Images uiXrImages;
    std::int64_t uiAcquired = -1;
    bool uiAcquiredWaited = false, uiHasImage = false, uiFailed = false, uiWashLogged = false;
    bool uiCopyPending = false; // a D3D12 copy into the acquired UI image is recorded
    LONGLONG uiShownQpc = 0;    // when the newest GUI image reached the UI swapchain
    std::uint64_t uiXrCopies = 0;
    UiWash uiWash; // ETERNALVR_UI_WASH: the additive wash taken out before the copy (off in menus)
    HudMarkerLayer hudMarkers; // the HUD's markers on their own quad (presenter_markers.hpp)
    XrSwapchain reticleSwapchain = XR_NULL_HANDLE; // hand aim: a static dot image
    bool reticleFailed = false;
};

// ---- Menus (presenter_menu.cpp, presenter_menu_update.cpp, docs/VR_MENUS.md) ------------------------------

struct PresenterMenuState {
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
    menu::PanelFollow menuFollow; // the panel comes back when the head turns away from it (panel_follow.hpp)
    std::atomic<bool> menuReplace{false}; // the runtime moved LOCAL: an open panel is placed again
    // Worker writes, present hook reads: a menu is up (the cursor, the panel or its hold); no crosshair
    // mask meanwhile.
    std::atomic<bool> menuUp{false};
    std::optional<menu::MenuRouter> menuRouter;
    input::CaptureChord menuChord;   // the capture chord's hold on the triggers (capture_chord.hpp)
    MenuPointer menuPointer;         // rebuilt every frame
    MenuPointerKept menuPointerKept; // the beam's image, the vibration ticks, the chord's clock: kept
    double menuPanelSeen = 0.0;      // when a panel last had something to show (qpc seconds)
    std::uint64_t menuFrames = 0;
    ULONGLONG lastMenuStatsTicks = 0;
};

// ---- The game's window (presenter_window.cpp) ----------------------------------------------------

struct PresenterWindowState {
    // Present hook, under the mutex. With VK_KHR_swapchain_maintenance1 under Route S only the presents the
    // window shows reach it; the others are handed back once their ring copy is done.
    bool windowGateChecked = false;
    bool windowGate = false; // presents are gated (the extension is on and windowGateForDevice)
    stereo_seq::WindowPresentGate windowPresents;
    bool windowHold = false; // this present is handed back (decideWindow; read by present())
    std::uint64_t lastSubmittedValue = 0;
    std::vector<HeldImage> heldImages;
    std::uint64_t imagesHandedBack = 0;
    std::uint64_t handBackFailures = 0;
    std::atomic<double> windowRefreshHz{0.0}; // written by the worker (refreshDisplayRate), read by the gate
    ULONGLONG lastRefreshTicks = 0;           // worker only
    ULONGLONG lastWindowStatsTicks = 0;
    stereo_seq::WindowPresentGate::Counters lastWindowCounters;
};

} // namespace evr::vkcore
