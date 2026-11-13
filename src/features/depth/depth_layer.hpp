#pragma once

// The depth layer (ETERNALVR_DEPTH_LAYER, docs/ARCHITECTURE.md section 6 "Depth"; T-057, T-080): each eye's
// depth is handed to the runtime with its projection view (XR_KHR_composition_layer_depth), so a runtime that
// reprojects a late frame can move it by depth, not by rotation alone. This is the part without Vulkan, D3D12
// or OpenXR: the switch, the depth swapchain's format, what the game's depth values mean in metres, which
// game images can be the source and in which layout they are read, whether the layer runs at all, and the
// statistics of a depth image read back.
//
// The game's depth (docs/rig-findings/presenter.md, section "Depth"): each eye's projection is the engine's
// standard matrix (its depth row m10, m11 from the engine's own view, the eye's rows 0 and 1 from the
// headset), but the value stored is reversed, 1 minus the standard depth: d = 1 + m10 - m11 / D for a point D
// game units ahead (about 0.06 / D), 0 at the far plane and the sky, 1 at the near plane. In OpenXR's terms
// that is a standard projection whose plane at depth 0 (`nearZ`) is the far plane and whose plane at depth 1
// (`farZ`) is the near plane: nearZ > farZ, which the extension allows for reversed depth.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace evr::depth {

// ETERNALVR_DEPTH_LAYER: 1, on or true turn it on; 0, off or false (and empty) leave it off; nullopt for any
// other text.
std::optional<bool> parseSwitch(std::string_view text);

// ---- The depth swapchain's format
// ---------------------------------------------------------------------------

inline constexpr std::int64_t kDxgiD32Float = 40;      // DXGI_FORMAT_D32_FLOAT
inline constexpr std::int64_t kDxgiD32FloatS8X24 = 20; // DXGI_FORMAT_D32_FLOAT_S8X24_UINT
inline constexpr std::int64_t kDxgiD24UnormS8 = 45;    // DXGI_FORMAT_D24_UNORM_S8_UINT (not taken)
inline constexpr std::int64_t kDxgiD16Unorm = 55;      // DXGI_FORMAT_D16_UNORM (not taken)

// The copy writes 32-bit floats, the game's own depth precision: D32_FLOAT, else D32_FLOAT_S8X24_UINT (its
// depth plane holds the same floats), from the formats xrEnumerateSwapchainFormats listed. Nullopt when
// neither is offered: 24- and 16-bit depth would lose the far range a reversed float keeps.
std::optional<std::int64_t> chooseFormat(const std::vector<std::int64_t>& offered);
const char* formatName(std::int64_t dxgi);

// ---- What the stored values mean
// ------------------------------------------------------------------------------

// The stored depth of a point D game units ahead is a + b / D.
struct DepthModel {
    double a = 0.0;
    double b = 0.0;
};

// From the eye projection's depth row (engine matrix elements [10] and [11]), for the reversed storage above.
// Nullopt unless depth grows toward the eye (b > 0), stays within 0..1 for points beyond the near plane, and
// the far plane is not past infinity (a above a rounding's worth of zero).
std::optional<DepthModel> modelFromEngineRow(float m10, float m11);

// The stored value for a point `units` game units ahead (no clamp).
double depthAt(const DepthModel& model, double units);
// The distance in game units of a stored value; +infinity for a value at or below the far plane's.
double unitsAt(const DepthModel& model, double depth);

// OpenXR's planes for minDepth 0 and maxDepth 1, in metres: `nearZ` is the distance of depth 0 (the far
// plane; for an infinite one kMaxPlaneMetres, a finite value every runtime takes) and `farZ` that of depth 1
// (the near plane), so nearZ > farZ. Game
// units are metres times `unitsPerMetre` (ETERNALVR_WORLD_SCALE, the launcher's World size; T-080's unit
// scale times world size, the unit scale being 1 in this game). Nullopt for a scale that is not positive and
// finite.
struct Planes {
    float nearZ = 0.0f;
    float farZ = 0.0f;
};
std::optional<Planes> planesOf(const DepthModel& model, float unitsPerMetre);
// The far plane handed over for an infinite one (and the largest one handed over): 100 km.
inline constexpr float kMaxPlaneMetres = 100000.0f;

// The first-person models (hands, guns) are drawn squeezed toward the near plane (under 0.11 m on the rig),
// so a runtime would move them as if a few centimetres from the eye. The copy writes at most the value of
// this distance: nothing reaches the runtime nearer than it.
inline constexpr float kNearestMetres = 0.25f;
// That value for `model` at `unitsPerMetre` (1 when the distance is inside the near plane).
float nearestDepth(const DepthModel& model, float unitsPerMetre);

// ---- The source image (the game's `_upscaledOpaqueDepth`) ----

// Vulkan values used here (plain numbers, so this module needs no Vulkan headers).
namespace vk {
inline constexpr std::int32_t kFormatD16 = 124;
inline constexpr std::int32_t kFormatX8D24 = 125;
inline constexpr std::int32_t kFormatD32 = 126;
inline constexpr std::int32_t kFormatD16S8 = 128;
inline constexpr std::int32_t kFormatD24S8 = 129;
inline constexpr std::int32_t kFormatD32S8 = 130;
inline constexpr std::uint32_t kUsageSampled = 0x04;
inline constexpr std::uint32_t kUsageDepthStencilAttachment = 0x20;
inline constexpr std::uint32_t kUsageTransientAttachment = 0x40;
inline constexpr std::int32_t kImageType2D = 1;
inline constexpr std::int32_t kSharingConcurrent = 1;
inline constexpr std::int32_t kLayoutDepthStencilAttachment = 3;
inline constexpr std::int32_t kLayoutDepthStencilReadOnly = 4;
inline constexpr std::int32_t kLayoutShaderReadOnly = 5;
inline constexpr std::uint32_t kAspectDepth = 0x2;
inline constexpr std::uint32_t kAspectStencil = 0x4;
} // namespace vk

bool isDepthFormat(std::int32_t vkFormat);
// The aspects a layout transition of the whole image names: depth, and stencil when the format has it.
std::uint32_t barrierAspects(std::int32_t vkFormat);

// What vkCreateImage is asked for (the fields the choice needs).
struct ImageDesc {
    std::int32_t imageType = 0;
    std::int32_t format = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;
    std::uint32_t mipLevels = 0;
    std::uint32_t arrayLayers = 0;
    std::uint32_t samples = 0;
    std::uint32_t usage = 0;
};
// With the depth layer on, the image hook keeps a record of every image that can be the game's scene depth,
// so the one the render system names can be followed: 2D, one mip, layer and sample, a depth format, a depth
// attachment made SAMPLED already (the copy samples it) and not transient, at least 256 wide. The image is
// made exactly as the game asks either way: the layer never changes a game image's usage for the depth layer.
bool isCandidate(const ImageDesc& desc);

enum class SourceCheck {
    Ok,
    NoImage,          // the render system holds no image (or not a readable one)
    UnknownImage,     // a VkImage the layer kept no record of
    NotDepth,         // made with a format that holds no depth
    NotSampled,       // made without SAMPLED
    NoLayout,         // no barrier on it submitted yet
    BadLayout,        // in a layout the copy cannot read or leave
    OtherQueueFamily, // exclusive to another queue family than the copying one
    SizeMismatch,     // not the eye image's size (the copy maps it 1:1)
};
const char* toString(SourceCheck check);

struct SourceRecord {
    std::int32_t format = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t usage = 0;
    std::int32_t sharingMode = 0;
    std::vector<std::uint32_t> queueFamilies; // concurrent sharing's
};
// The layout the copy's shader reads the source in. Only layouts positively expected of a finished scene
// depth are taken: shader read-only and depth-stencil read-only in place (the game's own state at a present
// on the rig: shader read-only), and the depth-stencil attachment layout with a transition to depth-stencil
// read-only and back, on a queue of a family with graphics only (`graphicsQueue`). Nullopt for anything else:
// GENERAL, transfer layouts, a layout the tracker could have missed a change of; that frame then has no
// depth.
struct ReadLayout {
    std::int32_t layout = 0;
    bool transition = false;
};
std::optional<ReadLayout> readLayoutFor(std::int32_t layout, bool graphicsQueue);

// Whether the source may be read now on a queue of `copyFamily` (`graphicsQueue`: that family has graphics):
// `record` (nullptr: no record), its layout once the submitted work has run and the family whose submit set
// it (nullopt: unknown), and the eye image's size it must have. An exclusive image is read only on the family
// that used it last (the game never transfers ownership).
SourceCheck checkSource(const SourceRecord* record,
                        std::optional<std::int32_t> layout,
                        std::optional<std::uint32_t> lastFamily,
                        std::uint32_t copyFamily,
                        bool graphicsQueue,
                        std::uint32_t eyeWidth,
                        std::uint32_t eyeHeight);

// ---- Whether the layer runs ----

enum class Off {
    None,          // on
    Switch,        // ETERNALVR_DEPTH_LAYER is not on (the default)
    NoExtension,   // the runtime does not list XR_KHR_composition_layer_depth
    NotRouteS,     // not stereo pairs (mono, the flat game, or Route S could not start)
    AlternateEyes, // Alternate eyes on or Auto (its images pair eyes of different presents and ticks)
    ParallelEyes,  // both eyes as two views of one render (the eyes come from copies the depth does not
                   // follow)
    NoTracking,    // the UI layer's image hooks are off (they follow the source's layout)
    NoFormat,      // no 32-bit float depth swapchain format
};
const char* toString(Off off);

struct StartState {
    bool switchOn = false;
    bool extensionEnabled = false;
    bool routeS = false;
    bool alternateEyes = false;
    bool parallelEyes = false;
    bool tracking = false;
    bool formatOffered = true; // false once the formats are known and none fits
};
// The first reason the layer stays off, in the order above; Off::None when it runs.
Off whyOff(const StartState& s);

// Whether an xrEndFrame result (as a number) on a frame with depth says the runtime refused its layers:
// XR_ERROR_VALIDATION_FAILURE, _LAYER_INVALID, _SWAPCHAIN_RECT_INVALID or _SWAPCHAIN_FORMAT_UNSUPPORTED. Any
// other result (success, a lost session or runtime, a session not running) is not the depth's.
bool refusesDepth(int xrResult);

// ---- The depth swapchain image on the XR worker ----

// Which depth image is acquired and waited, whether the colour copy recorded a depth copy into it, and
// whether the image the headset shows has depth (and its planes). A new image releases the depth image only
// when its copy went into it; a repeated image (no new one) keeps its depth; a new image without one is shown
// without.
class SwapImageState {
public:
    [[nodiscard]] bool acquired() const { return acquired_ >= 0; }
    [[nodiscard]] bool waited() const { return waited_; }
    [[nodiscard]] std::int64_t index() const { return acquired_; }
    void onAcquired(std::int64_t index);
    void onWaited() { waited_ = acquired_ >= 0; }
    // A depth copy into the acquired, waited image was recorded with the colour copy (`planes`: of its
    // frame).
    void onCopyRecorded(std::optional<Planes> planes);
    // The colour copy's list was not executed: nothing went into the image (it stays acquired and waited).
    void onCopyAbandoned() { pending_ = false; }
    // A new image reached the headset. True when the depth image is to be released (its copy went into it).
    bool onNewImage();
    // The runtime refused the depth (or the layer stopped): nothing more is shown with depth.
    void onRefused() { shown_ = false; }
    // The swapchain went (a ring rebuild, the session's end).
    void onDetached();
    [[nodiscard]] bool pending() const { return pending_; }
    [[nodiscard]] bool shown() const { return shown_; }
    [[nodiscard]] const Planes& planes() const { return planes_; }

private:
    std::int64_t acquired_ = -1;
    bool waited_ = false;
    bool pending_ = false;
    std::optional<Planes> pendingPlanes_;
    bool shown_ = false;
    Planes planes_{};
};

// Waits for the depth image that time out (the runtime holds it): the first is logged, then every
// kWaitLogEvery-th, and after kMaxWaitTimeouts in a row the layer stops (colour only).
inline constexpr std::uint32_t kMaxWaitTimeouts = 5;
inline constexpr std::uint32_t kWaitLogEvery = 100;
struct WaitTimeouts {
    std::uint32_t inRow = 0;
    std::uint64_t total = 0;
    struct Step {
        bool log = false;  // write a line for this one
        bool stop = false; // the layer is to stop
    };
    Step onTimeout();
    void onWaited() { inRow = 0; }
};

// ---- A depth image read back (ETERNALVR_TEST_DEPTH_READBACK) ----

struct EyeStats {
    std::uint64_t samples = 0;
    std::uint64_t sky = 0;     // at exactly 0: the sky, or nothing drawn
    std::uint64_t clamped = 0; // at the nearest value (kNearestMetres or nearer)
    std::uint64_t invalid = 0; // NaN, below 0 or above 1
    double minMetres = 0.0;    // the nearest and farthest finite distances (0 when none)
    double maxMetres = 0.0;
    double medianMetres = 0.0;
    double centreMetres = 0.0; // the image centre's distance (+infinity at the far plane)
};
// One eye of a depth buffer laid out row after row (`rowTexels` floats per row), the eye's columns from `x0`,
// `width` x `height`, every `step`th texel in each direction.
EyeStats statsOf(const float* texels,
                 std::size_t rowTexels,
                 std::size_t x0,
                 std::size_t width,
                 std::size_t height,
                 std::size_t step,
                 const DepthModel& model,
                 float unitsPerMetre,
                 float nearestValue);
// The share of the sampled texels (outside the far plane in either eye) where both eyes hold the same value:
// near 1 when both halves hold one eye's depth.
double sameShare(
    const float* texels, std::size_t rowTexels, std::size_t eyeWidth, std::size_t height, std::size_t step);

} // namespace evr::depth
