#pragma once

// Plain types of the XR presenter shared by its source files (presenter_impl.hpp holds the presenter's
// state): settings, the view record carried with each image, ring slots, swapchain and command state, and
// the OpenXR function table.

#include "common/pose.hpp"
#include "common/vector.hpp"
#include "common/xr_recovery.hpp"
#include "features/input/cutscene_skip.hpp"
#include "features/menu/enter_tick.hpp"
#include "features/pacing/frame_clock_watch.hpp"
#include "stereo_seq/desktop_window.hpp"
#include "ui_layer/ui_settings.hpp"
#include "vkcore/glory_view.hpp"
#include "vkcore/log.hpp"
#include "vkcore/presenter_stereo.hpp"
#include "vkcore/xr_presenter.hpp"

#include <windows.h>

#include <d3d12.h>
#include <wrl/client.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace evr::vkcore {

using Microsoft::WRL::ComPtr;

// Four: one being shown, the newest published, the newest finished one not shown yet, and one to write. With
// three, a game running a frame or more ahead of its GPU wrote over finished frames before the headset showed
// them (measured: ~2/3 of the game's frames shown at 56-70 fps).
inline constexpr std::uint32_t kRingSize = 4;
// `latest` packs the slot index into its low two bits.
static_assert(kRingSize <= 4);
inline constexpr float kScreenWidthMetres = 2.4f;
inline constexpr float kScreenDistanceMetres = 2.5f;
inline constexpr XrDuration kSwapchainWaitTimeout = 100'000'000; // 100 ms
inline constexpr std::size_t kHistorySize = 32;
// A retired present semaphore is destroyed once this many later ring copies have completed on the game's
// queues. Each copy belongs to a later present of the game, so by then the presents that waited on the
// semaphore have long run (the game keeps at most a few presents in flight).
inline constexpr std::uint64_t kRetireAfterCopies = 8;
// A present whose newest head-tracked view is older than this shows on the cinema quad instead
// (menus and loading screens do not run the game view).
inline constexpr double kViewStaleSeconds = 0.25;
// The game's cutscene skip key, held by ETERNALVR_SKIP_CINEMATICS.
inline constexpr std::uint8_t kSkipKey = input::kCutsceneSkipKey;

enum SlotState : int { kSlotFree = 0, kSlotWriting = 1, kSlotReading = 2 };

enum class Mode { HeadTracked, Cinema };

struct Settings {
    Mode mode = Mode::HeadTracked;
    float unitsPerMetre = 1.0f;  // ETERNALVR_WORLD_SCALE
    bool headPosition = true;    // ETERNALVR_HEAD_POSITION
    bool setGameFov = true;      // ETERNALVR_SET_FOV
    bool keepActive = true;      // ETERNALVR_KEEP_ACTIVE
    bool headAim = true;         // ETERNALVR_AIM: "head" (default) or "view"
    bool skipCinematics = false; // ETERNALVR_SKIP_CINEMATICS
    // ETERNALVR_POSE_LEAD: predict the head and hands for when each game frame is measured to be shown,
    // not one display period ahead (xr_math/display_lead.hpp). Off by default, on by default under
    // ETERNALVR_PACE=headset.
    bool poseLead = false;
    // ETERNALVR_CUTSCENES: "cinema" (default) shows cutscenes on the flat screen in front of the player, with
    // the game's own camera; "immersive" keeps the head in the cutscene's moving camera.
    bool cutsceneCinema = true;
    // ETERNALVR_CUTSCENE_CUT_REBASE (on by default): in a cutscene shown around the player each cut of its
    // camera, and its end, turn the view so the new shot's forward is where the head looks
    // (xr_math/cutscene_cuts.hpp).
    bool cutsceneCutRebase = true;
    // ETERNALVR_CINEMA_ASPECT: the flat screen's shape during a cutscene, `16:9` (default) or `16:10` drawn
    // as a flat display of that shape shows it, or `full` (the eye image as the game draws it;
    // cinema_view.hpp).
    double cinemaAspect = 16.0 / 9.0;
    // ETERNALVR_GLORY_KILLS: how glory kills are shown, follow (default), steady, fade or screen
    // (features/comfort/glory_kill.hpp).
    comfort::GloryView gloryKills = comfort::GloryView::Follow;
    // ETERNALVR_TEST_GLORY=start,duration: a glory kill forced for rig tests (GloryKills);
    // a negative start is none.
    double testGloryStart = -1.0;
    double testGloryDuration = 0.0;
    // ETERNALVR_TEST_HEAD_SWAY=yaw,pitch,period: a sinusoidal head turn (degrees, seconds) added to the
    // tracked pose, for checking head tracking and head aim without a moving headset. An optional fourth
    // value (yaw,pitch,period,base) turns the head by `base` degrees of yaw first, so runs can be compared
    // around one view (a zero amplitude holds it).
    float swayYaw = 0.0f;
    float swayPitch = 0.0f;
    float swayPeriod = 0.0f;
    float swayBaseYaw = 0.0f;
    StereoSettings stereo;   // ETERNALVR_MODE=stereo (head-tracked plus the engine's two views)
    ui_layer::UiSettings ui; // ETERNALVR_UI_LAYER and the quad's placement
    // ETERNALVR_MIRROR: what the game's window shows during stereo pairs (presenter_mirror.hpp).
    stereo_seq::Mirror mirror = stereo_seq::Mirror::Left;
    // ETERNALVR_TEST_XR_LOSS=seconds: once the session has run this long, the worker takes it as lost (as
    // if the headset had gone away) and reconnects, for testing the recovery on a desktop runtime.
    float testLossSeconds = 0.0f;
    // ETERNALVR_TEST_XR_LOSS_REMOVE=1: that loss also removes the presenter's D3D12 device, as a graphics
    // card reset would (ID3D12Device5::RemoveDevice).
    bool testLossRemovesDevice = false;
};

// The session ended, and why (XR worker only; presenter_reconnect.cpp brings VR back when it can).
struct XrLossState {
    bool lost = false; // the worker leaves its frame loop
    xr_recovery::Loss kind = xr_recovery::Loss::Session;
    std::uint32_t reconnects = 0; // sessions made again after a loss
    LONGLONG runningSinceQpc = 0; // when the current session began running
    bool testLossDone = false;    // ETERNALVR_TEST_XR_LOSS has been applied
};

// What one game frame was rendered with: written by the camera hook, carried with the presented image
// through the ring, and submitted in the projection layer's views.
struct ViewRecord {
    std::uint64_t seq = 0;
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}}; // head in LOCAL
    XrTime poseTime = 0;                                        // the time the pose was predicted for
    LONGLONG locatedQpc = 0;                                    // when xrLocateSpace returned
    XrFovf fov{};                                               // the game's FOV for this frame
    std::array<float, 9> axis{};                                // the viewaxis written, the latch's match key
    bool stereo = false;                                        // eyes valid: rendered as two views
    // A cutscene frame (renderView_t.inCutscene), and one whose arms are hidden with the game's weapon FOV
    // kept (controllers::cutsceneArmsHidden): the per-eye hooks leave the hands and guns matrices alone then.
    bool cutscene = false;
    bool cutsceneArms = false;
    std::array<EyeRecord, 2> eyes{};
    // Route S: the ring image holds this frame's two eyes, each shown with its own pose and FOV (false:
    // both halves show the head's).
    bool showEyes = false;
    // Under hand aim: the weapon hand's aim ray at poseTime in LOCAL, as the gun and the shots of this frame
    // use it (smoothed), and its orientation as tracked (the frame log).
    bool weaponAimValid = false;
    XrPosef weaponAim{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    XrQuaternionf weaponAimTracked{0.0f, 0.0f, 0.0f, 1.0f};
    // How far along that ray the world is hit, in metres (reticle_depth.hpp); 0 when not known.
    float weaponAimHitMetres = 0.0f;
};

// A D3D12 copy that did not finish within the frame's wait (XR worker only): its slot and swapchain image
// stay held until it does (presenter_frame.cpp).
struct HeldCopy {
    bool stalled = false;
    std::uint32_t slot = 0;
    std::uint64_t value = 0;
    ViewRecord view;
    bool hasView = false;
    LONGLONG sinceQpc = 0;   // when the held copy was submitted
    bool logged = false;     // the held copy's start or its 2 s mark is in the log (so is its end)
    bool longLogged = false; // the held copy's 2 s mark is in the log
    std::uint32_t count = 0; // copies not finished within the frame's wait, so far
};

// The runtime's frame clock (XR worker only; frame_clock_watch.hpp, public issue #19): a stall ends the
// session to start a new one, at most kMaxRestarts times in a game.
struct ClockWatchState {
    static constexpr std::uint32_t kMaxRestarts = 3;
    bool wasFocused = false; // FOCUSED since the session became READY
    pacing::FrameClockWatch watch;
    std::uint32_t restarts = 0;
    bool loggedKept = false; // a stall past the limit is in the log
};

// Where a present's image goes in a ring slot (Route S rings hold two eye images side by side).
struct CopyTarget {
    std::uint32_t firstEye = 0; // 0: the left half (or the whole image of a one-eye ring), 1: the right half
    std::uint32_t eyeCount = 1; // 2: the same image into both halves
    bool keepOther = false;     // the slot's other half holds this pair's first eye: keep its contents
    bool ui = false;            // also copy the game's GUI target into the slot's UI image (UI layer)
    // The desktop mirror: what this present does to the image the game's window receives.
    stereo_seq::MirrorStep mirror = stereo_seq::MirrorStep::None;
    // False: the present is handed back to the swapchain instead of reaching the window, so the copy does not
    // signal its present semaphore (presenter_window.cpp).
    bool toWindow = true;
    // Which present of a tick this is: while a menu is up the window shows the panel's image, which only a
    // mono or eye L present carries (stereo_seq::panelMirror).
    stereo_seq::PresentKind kind = stereo_seq::PresentKind::Mono;
    // Alternate eyes: the image also goes into its half of this slot (held for the next present); kRingSize:
    // none.
    std::uint32_t carrySlot = kRingSize;
};

// A ring slot's copy of the game's GUI target (UI layer): shared like the slot's image.
struct UiImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    ComPtr<ID3D12Resource> resource;
    bool written = false; // this slot's image came with a GUI copy (kSlotWriting / kSlotReading owner)
};

struct RingSlot {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    ComPtr<ID3D12Resource> resource;
    std::atomic<int> state{kSlotFree};
    std::atomic<std::uint64_t> value{0};     // timeline value whose signal completes the last write
    std::atomic<std::uint64_t> published{0}; // the value it held when last published (a present), 0: never
    // The view the image in this slot was rendered with (valid when hasView); written by the present
    // hook while the slot is kSlotWriting, read by the worker while it is kSlotReading.
    ViewRecord view;
    bool hasView = false;
    UiImage ui;
};

Settings readSettings();
LONGLONG qpcNow();
double qpcSeconds(LONGLONG delta);
// The folder this DLL was loaded from (openxr_loader.dll sits next to it).
std::wstring moduleDirectory();

// The menu pointer's beam and dot (presenter_menu.cpp), in LOCAL.
struct MenuPointer {
    bool visible = false; // draw the beam (and the dot when it hits)
    Vec3 from;            // the hand
    Vec3 to;              // the hit, or a point along the ray when it misses
    Vec3 eye;             // the head, which the beam turns to face
    bool hit = false;
    Pose dot;
    float dotSide = 0.0f;
};

// What the menu pointer keeps from frame to frame, unlike MenuPointer (presenter_menu.cpp).
struct MenuPointerKept {
    XrSwapchain beamSwapchain = XR_NULL_HANDLE;
    bool beamFailed = false;
    std::array<menu::EnterTick, 2> enterTicks{}; // each hand's vibration tick onto the panel
    double chordSeconds = 0.0;                   // when the capture chord last ran (qpc seconds)
};

struct SwapchainState {
    std::vector<VkImage> images;
    std::vector<VkSemaphore> presentSemaphores; // one per image (T-081, T-091)
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
};

// The game's and the headset's counters at the last `rates:` line (presenter_frame_log.cpp).
struct RateMarks {
    std::uint64_t presents = 0;
    std::uint64_t ticks = 0;
    std::uint64_t pairs = 0;
    std::uint64_t xrFrames = 0;
    std::uint64_t xrCopies = 0;
    LONGLONG qpc = 0;
};

// A game swapchain image whose present was handed back instead of reaching the window (presenter_window.cpp).
struct HeldImage {
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::uint32_t image = 0;
    std::uint64_t value = 0; // the ring copy that last used it
};

struct FamilyCommands {
    VkCommandPool pool = VK_NULL_HANDLE;
    // Two per slot: a Route S slot takes eye L's copy and then eye R's while eye L's may still be pending,
    // so each half has its own command buffer ([slot * 2 + half]).
    std::array<VkCommandBuffer, kRingSize * 2> buffers{};
    std::array<std::uint64_t, kRingSize * 2> lastValue{};
};

#define EVR_XR_FUNCTIONS(X)                                                                                  \
    X(xrDestroyInstance)                                                                                     \
    X(xrGetInstanceProperties)                                                                               \
    X(xrGetSystem)                                                                                           \
    X(xrGetSystemProperties)                                                                                 \
    X(xrCreateSession)                                                                                       \
    X(xrDestroySession)                                                                                      \
    X(xrBeginSession)                                                                                        \
    X(xrEndSession)                                                                                          \
    X(xrPollEvent)                                                                                           \
    X(xrCreateReferenceSpace)                                                                                \
    X(xrEnumerateReferenceSpaces)                                                                            \
    X(xrDestroySpace)                                                                                        \
    X(xrLocateSpace)                                                                                         \
    X(xrLocateViews)                                                                                         \
    X(xrEnumerateViewConfigurationViews)                                                                     \
    X(xrEnumerateSwapchainFormats)                                                                           \
    X(xrCreateSwapchain)                                                                                     \
    X(xrDestroySwapchain)                                                                                    \
    X(xrEnumerateSwapchainImages)                                                                            \
    X(xrAcquireSwapchainImage)                                                                               \
    X(xrWaitSwapchainImage)                                                                                  \
    X(xrReleaseSwapchainImage)                                                                               \
    X(xrWaitFrame)                                                                                           \
    X(xrBeginFrame)                                                                                          \
    X(xrEndFrame)                                                                                            \
    X(xrResultToString)                                                                                      \
    X(xrGetD3D12GraphicsRequirementsKHR)

struct XrFunctions {
    PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr = nullptr;
    PFN_xrEnumerateInstanceExtensionProperties xrEnumerateInstanceExtensionProperties = nullptr;
    PFN_xrCreateInstance xrCreateInstance = nullptr;
#define EVR_XR_DECLARE(name) PFN_##name name = nullptr;
    EVR_XR_FUNCTIONS(EVR_XR_DECLARE)
#undef EVR_XR_DECLARE
};

#define EVR_XR_CHECK(call)                                                                                   \
    do {                                                                                                     \
        const XrResult evr_r = (call);                                                                       \
        if (XR_FAILED(evr_r)) {                                                                              \
            char evr_text[XR_MAX_RESULT_STRING_SIZE];                                                        \
            EVR_LOG("xr: %s failed: %s", #call, xrText(evr_r, evr_text));                                    \
            return false;                                                                                    \
        }                                                                                                    \
    } while (0)

} // namespace evr::vkcore
