#pragma once

// Cutscenes on the flat screen at a display's aspect (ETERNALVR_CINEMA_ASPECT, docs/VR_HEAD_TRACKED.md,
// Cutscenes). The eye image is near-square or taller, so a cutscene drawn into it as the game draws it fills
// a tall screen with a narrow slice of the flat view. Instead the camera hook gives the cutscene the FOV of a
// flat 16:9 (or 16:10) display across the image's width, extended above and below with square pixels
// (xr_math::cinemaFov): the image's centred band of that aspect is then exactly what a flat player sees, and
// the screen shows only that band. Nothing is resized or re-viewported; the rows outside the band are drawn
// and not shown.

#include "features/render_size/mirror_window.hpp"

#include <atomic>
#include <cstdint>
#include <optional>

namespace evr::vkcore {

class CinemaView {
public:
    // XR worker: the eye image's size, whenever the ring is (re)built.
    void setImage(std::uint32_t width, std::uint32_t height);

    // Camera hook, every game view (while the multiplayer guard allows touching the game): during a cutscene
    // on the flat screen (`cutscene`) the view's FOV (`fov`: fov_x, fov_y in degrees) becomes the flat
    // display's at `aspect` (0: the game's own, the whole image on the screen).
    void onGameView(float* fov, bool cutscene, double aspect);

    // XR worker: the rows of a `width` x `height` eye image the flat screen shows while the newest game view
    // was drawn for the band (and is less than kFreshMs old: loading screens run no game view); nullopt for
    // the whole image. Frames already in flight when a cutscene starts or ends may show one frame on the
    // wrong screen shape.
    std::optional<render_size::Band> band(std::uint32_t width, std::uint32_t height) const;

private:
    std::atomic<std::uint64_t> image_{0}; // width << 32 | height
    static constexpr std::int64_t kFreshMs = 500;
    std::atomic<double> aspect_{0.0};     // the band drawn by the newest game view, 0 for none
    std::atomic<std::int64_t> viewMs_{0}; // when that view was (steady clock, milliseconds)
    bool wasCutscene_ = false;            // camera hook only
    std::uint32_t logged_ = 0;            // camera hook only
};

} // namespace evr::vkcore
