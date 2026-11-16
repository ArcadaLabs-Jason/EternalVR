// The edge margin for this process (edge_margin_setting.hpp).

#include "vkcore/edge_margin_setting.hpp"

#include "features/render_size/edge_margin.hpp"
#include "vkcore/log.hpp"

#include <atomic>
#include <cwchar>
#include <optional>
#include <string>

namespace evr::vkcore {

namespace {

// The FOV's factor once the runtime's limits are known (0: not yet, the asked factor applies).
std::atomic<float> g_fovFactor{0.0f};
// The last factor logged as held below the asked one (0: none).
std::atomic<float> g_loggedFovFactor{0.0f};
// The size the FOV's factor was fitted to, width << 32 | height (0: not known), and whether an eye image
// below it was logged.
std::atomic<std::uint64_t> g_fittedSize{0};
std::atomic<bool> g_loggedSmaller{false};
std::atomic<int> g_smallerSamples{0};

} // namespace

float edgeMarginFactor() {
    static const float factor = [] {
        std::wstring value;
        if (!readEnv(L"ETERNALVR_EDGE_MARGIN", value) || value.empty()) {
            return 1.0f;
        }
        std::wstring mode;
        const bool stereo = readEnv(L"ETERNALVR_MODE", mode) && _wcsicmp(mode.c_str(), L"stereo") == 0;
        const std::optional<float> parsed = render_size::parseEdgeMargin(value);
        if (!parsed) {
            EVR_LOG(
                "edge margin: ETERNALVR_EDGE_MARGIN '%ls' is not off, small, medium, large or a percentage "
                "1 to %d; off",
                value.c_str(), render_size::kMaxEdgeMarginPercent);
            return 1.0f;
        }
        if (!stereo) {
            if (*parsed > 1.0f) {
                EVR_LOG("edge margin: %s asked for, off: stereo only",
                        render_size::describeEdgeMargin(*parsed).c_str());
            }
            return 1.0f;
        }
        if (*parsed > 1.0f) {
            EVR_LOG(
                "edge margin: %s: each eye renders its FOV's tangents times %.2f and the projection layer "
                "has that FOV; an automatic render size grows by the same factor (a fixed one is used as "
                "it is)",
                render_size::describeEdgeMargin(*parsed).c_str(), static_cast<double>(*parsed));
        }
        return *parsed;
    }();
    return factor;
}

float edgeMarginFovFactor() {
    const float asked = edgeMarginFactor();
    const float held = g_fovFactor.load(std::memory_order_acquire);
    return held > 0.0f && held < asked ? held : asked;
}

void noteSizeLimits(const render_size::Request& request, const render_size::ViewLimits& limits) {
    const float asked = edgeMarginFactor();
    if (!(asked > 1.0f)) {
        return;
    }
    const auto kept = static_cast<float>(render_size::requestGrowth(request, limits, asked));
    g_fovFactor.store(kept, std::memory_order_release);
    const std::optional<render_size::Extent> size = render_size::selectSize(request, limits);
    g_fittedSize.store(size ? (std::uint64_t{size->width} << 32) | size->height : 0,
                       std::memory_order_relaxed);
    if (!(kept < asked) || g_loggedFovFactor.exchange(kept) == kept) {
        return;
    }
    EVR_LOG("edge margin: the runtime's limits hold each eye to %ux%u, which keeps x%.3f of the %s margin at "
            "the centre's density: each eye's FOV is widened x%.3f%s",
            size ? size->width : 0, size ? size->height : 0, static_cast<double>(kept),
            render_size::describeEdgeMargin(asked).c_str(), static_cast<double>(kept),
            kept > 1.0f ? "" : " (no margin)");
}

void noteEyeExtent(std::uint32_t width, std::uint32_t height) {
    const std::uint64_t fitted = g_fittedSize.load(std::memory_order_relaxed);
    if (fitted == 0 || width == 0 || height == 0 || !(edgeMarginFovFactor() > 1.0f)) {
        return;
    }
    const auto fittedWidth = static_cast<std::uint32_t>(fitted >> 32);
    const auto fittedHeight = static_cast<std::uint32_t>(fitted & 0xFFFFFFFFu);
    // Two 10 s samples in a row, so the switch to the render size (the window's size until then) is not one.
    if (width >= fittedWidth && height >= fittedHeight) {
        g_smallerSamples.store(0, std::memory_order_relaxed);
        return;
    }
    if (g_smallerSamples.fetch_add(1, std::memory_order_relaxed) < 1 || g_loggedSmaller.exchange(true)) {
        return;
    }
    EVR_LOG("edge margin: each eye renders %ux%u, below the %ux%u its x%.3f margin was fitted to (a later "
            "limit or the render size turned off): the FOV keeps that factor, at less detail",
            width, height, fittedWidth, fittedHeight, static_cast<double>(edgeMarginFovFactor()));
}

} // namespace evr::vkcore
