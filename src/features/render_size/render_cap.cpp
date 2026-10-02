#include "features/render_size/render_cap.hpp"

#include "features/render_size/mirror_window.hpp"

#include <algorithm>
#include <cstdio>

namespace evr::render_size {

bool capped(Extent real, Extent planned) {
    return (planned.width > 0 && real.width < planned.width) ||
           (planned.height > 0 && real.height < planned.height);
}

std::uint32_t percentOf(std::uint32_t real, std::uint32_t planned) {
    if (planned == 0) {
        return 100;
    }
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(real) * 100u / planned);
}

std::uint32_t cappedPercent(Extent real, Extent planned) {
    return std::min(percentOf(real.width, planned.width), percentOf(real.height, planned.height));
}

std::string describeCap(Extent real, Extent planned) {
    const std::uint32_t w = percentOf(real.width, planned.width);
    const std::uint32_t h = percentOf(real.height, planned.height);
    char text[128];
    if (w == h) {
        std::snprintf(text, sizeof(text), "%ux%u, %u%% of the planned %ux%u", real.width, real.height, w,
                      planned.width, planned.height);
    } else {
        std::snprintf(text, sizeof(text), "%ux%u, %u%% x %u%% of the planned %ux%u", real.width, real.height,
                      w, h, planned.width, planned.height);
    }
    return text;
}

std::optional<WindowRect> largestEyeWindow(Extent eye, const WindowRect& workArea, const FrameInsets& frame) {
    const std::int32_t width = workArea.width - frame.left - frame.right;
    const std::int32_t height = workArea.height - frame.top - frame.bottom;
    if (eye.width == 0 || eye.height == 0 || width <= 0 || height <= 0) {
        return std::nullopt;
    }
    const WindowRect box{workArea.x + frame.left, workArea.y + frame.top, width, height};
    if (static_cast<std::int64_t>(eye.width) <= width && static_cast<std::int64_t>(eye.height) <= height) {
        const auto w = static_cast<std::int32_t>(eye.width);
        const auto h = static_cast<std::int32_t>(eye.height);
        return WindowRect{box.x + (width - w) / 2, box.y + (height - h) / 2, w, h};
    }
    return shapeToImage(box, eye, true);
}

} // namespace evr::render_size
