#pragma once

// The render size's settings, read once (virtual_client.hpp): ETERNALVR_RENDER_SIZE and _SCALE, the mirror
// window's options (mirror_place.hpp) and ETERNALVR_WINDOW, and the mirror window shaped to an eye image.

#include "features/render_size/render_size.hpp"

#include <optional>

namespace evr::vkcore::virtual_client {

struct Settings {
    render_size::Request request;
    std::optional<render_size::WindowRect> mirror;
    std::optional<render_size::WindowRect> fallback;
    double crop = 0.0;    // ETERNALVR_MIRROR_CROP (mirror_place.hpp)
    bool fill = false;    // ETERNALVR_MIRROR_SIZE=fill
    bool centred = false; // `mirror` is centred on the display ETERNALVR_MIRROR_DISPLAY chose
};

// Read and logged at the first call.
const Settings& settings();

// ETERNALVR_RENDER_SIZE is set and not `off`.
bool wantedSize();

// The mirror window for an eye image of `eye`: with crop `full` it takes the eye's shape (mirror_place.hpp).
std::optional<render_size::WindowRect> shapedMirror(const std::optional<render_size::Extent>& eye);

} // namespace evr::vkcore::virtual_client
