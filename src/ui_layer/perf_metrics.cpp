#include "ui_layer/perf_metrics.hpp"

#include <algorithm>
#include <cwctype>
#include <string_view>

namespace evr::ui_layer {

namespace {

// How far the game's metrics reach from the target's top-right corner at each level, in pixels (index =
// level). Rig GUI captures (Steam build 25216728, 2048 x 2176): level 1 drew 3 lines (frame rate and frame
// time, the resolution, the tick mode) in rows 18..71, level 2 7 lines (and the average frame rate, HDR, ray
// tracing, DLSS) in rows 18..153, level 3 (the cvar's "retail version") the frame rate and frame time alone
// in rows 18..31; the longest line was 181 pixels wide. Each corner is wider than that and reaches a line
// lower, for longer values.
struct Reach {
    std::uint32_t width;
    std::uint32_t height;
};
constexpr Reach kReach[kPerfOverlayMaxLevel + 1] = {{0, 0}, {320, 90}, {320, 172}, {320, 50}};

std::wstring_view trimmed(std::wstring_view s) {
    while (!s.empty() && std::iswspace(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::iswspace(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

} // namespace

int readPerfOverlayLevel(const EnvLookup& env, std::vector<std::string>& warnings) {
    const std::optional<std::wstring> text = env(L"ETERNALVR_PERF_OVERLAY");
    if (!text) {
        return 0;
    }
    const std::wstring_view t = trimmed(*text);
    if (t.size() == 1 && t[0] >= L'0' && t[0] <= L'0' + kPerfOverlayMaxLevel) {
        return t[0] - L'0';
    }
    warnings.push_back("ETERNALVR_PERF_OVERLAY is not 0 to 3; the game's Performance Metrics are not shown");
    return 0;
}

bool perfQuadFits(std::uint32_t before, std::uint32_t after) {
    return before < kMaxFrameLayers && after < kMaxFrameLayers - before;
}

int perfLevelShown(int level, bool fullScreenBackdrop) {
    return fullScreenBackdrop ? 0 : level;
}

std::optional<PixelRect> perfMetricsRect(int level, std::uint32_t width, std::uint32_t height) {
    if (level <= 0 || level > kPerfOverlayMaxLevel || width == 0 || height == 0) {
        return std::nullopt;
    }
    const Reach reach = kReach[level];
    const std::uint32_t w = std::min(reach.width, width);
    const std::uint32_t h = std::min(reach.height, height);
    return PixelRect{static_cast<std::int32_t>(width - w), 0, w, h};
}

std::optional<PerfMetricsQuad> perfMetricsQuad(
    int level, std::uint32_t width, std::uint32_t height, const PixelRect& shown, float panelWidth) {
    const std::optional<PixelRect> corner = perfMetricsRect(level, width, height);
    // The UI quad shows the rows the metrics are in already (a target of 16:9 or wider, or no crop).
    if (!corner || shown.y <= 0 || shown.x != 0 || shown.width != width || shown.height == 0 ||
        !(panelWidth > 0.0f)) {
        return std::nullopt;
    }
    // A band of rows spans the target's width, so the corner's right edge is the UI quad's.
    PerfMetricsQuad q;
    q.source = *corner;
    // Only rows above the band: any below it are on the UI quad where the game drew them.
    q.source.height = std::min(q.source.height, static_cast<std::uint32_t>(shown.y));
    // At the UI quad's scale, but never smaller than on a band kReferenceWidth pixels wide: the game draws
    // the metrics in pixels of its own, the same size at every target size, so on a larger one they would
    // shrink.
    const float metresPerPixel = panelWidth / static_cast<float>(std::min(shown.width, kReferenceWidth));
    q.width = static_cast<float>(q.source.width) * metresPerPixel;
    q.height = static_cast<float>(q.source.height) * metresPerPixel;
    q.centreX = panelWidth / 2.0f - q.width / 2.0f;
    // The panel's own height, at its own scale.
    q.centreY = static_cast<float>(shown.height) * panelWidth / static_cast<float>(shown.width) / 2.0f +
                q.height / 2.0f;
    return q;
}

} // namespace evr::ui_layer
