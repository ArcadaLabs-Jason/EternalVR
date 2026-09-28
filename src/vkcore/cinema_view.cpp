#include "vkcore/cinema_view.hpp"

#include "vkcore/log.hpp"
#include "xr_math/cinema_quad.hpp"

#include <chrono>

namespace evr::vkcore {

namespace {

constexpr std::uint32_t kLoggedCutscenes = 20;

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

void CinemaView::setImage(std::uint32_t width, std::uint32_t height) {
    image_.store(static_cast<std::uint64_t>(width) << 32 | height, std::memory_order_relaxed);
}

void CinemaView::onGameView(float* fov, bool cutscene, double aspect) {
    const std::uint64_t image = image_.load(std::memory_order_relaxed);
    const auto width = static_cast<std::uint32_t>(image >> 32);
    const auto height = static_cast<std::uint32_t>(image & 0xFFFFFFFFu);
    const std::optional<xr_math::CinemaFov> flat =
        cutscene ? xr_math::cinemaFov(fov[0], fov[1], width, height, aspect) : std::nullopt;
    if (cutscene && !wasCutscene_ && logged_ < kLoggedCutscenes) {
        ++logged_;
        const render_size::Band rows = render_size::centredBand(width, height, flat ? aspect : 0.0);
        if (flat) {
            EVR_LOG(
                "cinema: cutscene fov %.2f x %.2f for a %ux%u image drawn as %.2f x %.2f; the screen shows "
                "rows %u to %u (%.3f:1, ETERNALVR_CINEMA_ASPECT)",
                fov[0], fov[1], width, height, flat->fovX, flat->fovY, rows.y, rows.y + rows.height, aspect);
        } else {
            EVR_LOG(
                "cinema: cutscene fov %.2f x %.2f for a %ux%u image kept; the screen shows the whole image",
                fov[0], fov[1], width, height);
        }
    }
    wasCutscene_ = cutscene;
    if (flat) {
        fov[0] = flat->fovX;
        fov[1] = flat->fovY;
    }
    aspect_.store(flat ? aspect : 0.0, std::memory_order_relaxed);
    viewMs_.store(nowMs(), std::memory_order_relaxed);
}

std::optional<render_size::Band> CinemaView::band(std::uint32_t width, std::uint32_t height) const {
    const double aspect = aspect_.load(std::memory_order_relaxed);
    if (!(aspect > 0.0) || nowMs() - viewMs_.load(std::memory_order_relaxed) > kFreshMs) {
        return std::nullopt;
    }
    const render_size::Band rows = render_size::centredBand(width, height, aspect);
    if (rows.height == height) {
        return std::nullopt;
    }
    return rows;
}

} // namespace evr::vkcore
