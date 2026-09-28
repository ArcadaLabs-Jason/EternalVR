#pragma once

// The game's render size in VR (T-031, docs/rig-findings/render-size.md): each eye renders at the OpenXR
// runtime's recommended view size times a render scale, whatever the desktop's displays are. The layer makes
// the game believe its window's client area has that size (the game sizes its swapchain and its output from
// the client area) and lets the driver scale the presented image into the real, small desktop window.
//
// Plain logic: the settings, the size chosen from the runtime's limits, and the desktop window rectangles.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace evr::render_size {

struct Extent {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    friend constexpr bool operator==(Extent a, Extent b) = default;
};

// ETERNALVR_RENDER_SIZE: `auto` (the runtime's recommendation, see autoSize), `WxH` (that size exactly,
// within the runtime's limits) or `off` (the game renders at its window's size, as before T-031).
enum class Mode : std::uint8_t { Off, Auto, Fixed };
struct Request {
    Mode mode = Mode::Off;
    Extent fixed;       // Mode::Fixed
    float scale = 1.0f; // Mode::Auto: ETERNALVR_RENDER_SCALE
    friend bool operator==(const Request&, const Request&) = default;
};

// Unset or empty, `off`, `0`: Off. `auto`. `WxH` with each side kMinSide..kMaxFixedSide. nullopt for anything
// else.
std::optional<Request> parseRenderSize(std::wstring_view text);

// ETERNALVR_RENDER_SCALE: empty or `auto` gives 1.0; a number kMinScale..kMaxScale. nullopt for anything
// else.
std::optional<float> parseRenderScale(std::wstring_view text);

inline constexpr std::uint32_t kMinSide = 256;
inline constexpr std::uint32_t kMaxFixedSide = 8192;
inline constexpr std::uint32_t kMaxSide = 16384; // Vulkan's usual maxImageDimension2D
inline constexpr std::uint32_t kAlign = 8;       // sides are multiples of this
inline constexpr float kMinScale = 0.5f;
inline constexpr float kMaxScale = 2.0f;

// The default pixel budget per eye: 2064 x 2208 (4.56 Mpix). The runtime's recommendation is scaled down
// uniformly to fit it, never up. The owner's RTX 4080 runs Route S at 2064 x 2100 per eye at about 115
// stereo ticks per second (two renders each) with the v1 cvars; VDXR recommends 2496 x 2688 for a Quest 3
// at 100 % (6.7 Mpix, research 11: at the 90 Hz limit without upscaling), which this turns into 2056 x 2216.
// A render scale above 1 goes past the budget on purpose (supersampling).
inline constexpr std::uint64_t kDefaultPixelBudget = 2064ull * 2208ull;

// What the runtime allows (xrEnumerateViewConfigurationViews, xrGetSystemProperties).
struct ViewLimits {
    Extent recommended;  // the largest recommended image rect over the views
    Extent maxImageRect; // the smallest maximum image rect over the views (0: no limit)
    Extent maxSwapchain; // the system's maximum swapchain image size (0: no limit)
    // Eye images side by side in one swapchain image (Route S: 2): the width limit is shared.
    std::uint32_t eyesSideBySide = 1;
};

// Auto: the recommendation scaled uniformly into `budget` pixels (never up), times `scale`, then fitted.
// nullopt when the recommendation is empty or the scale is not a positive finite number.
std::optional<Extent>
autoSize(const ViewLimits& limits, float scale, std::uint64_t budget = kDefaultPixelBudget);

// `size` scaled down uniformly, if needed, to fit the runtime's limits (and kMaxSide), then rounded to
// multiples of kAlign (at least kMinSide per side, aspect kept as closely as the rounding allows).
Extent fitToLimits(Extent size, const ViewLimits& limits);

// The size a request gives: Off gives nullopt; Fixed its size (fitted when the limits are known); Auto needs
// the limits (nullopt without them).
std::optional<Extent> selectSize(const Request& request, const std::optional<ViewLimits>& limits);

// A window's client area on the desktop (ETERNALVR_WINDOW, ETERNALVR_MIRROR_WINDOW).
struct WindowRect {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    friend constexpr bool operator==(WindowRect a, WindowRect b) = default;
};
// `x,y,width,height` with a positive size; nullopt otherwise.
std::optional<WindowRect> parseWindowRect(std::wstring_view text);

// For the log: "off", "auto x1.00" or "WxH".
std::string describe(const Request& request);

} // namespace evr::render_size
