#include "features/comfort/pickup_camera.hpp"

#include <cctype>
#include <cstddef>

namespace evr::comfort {

namespace {

bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

// `text` without its surrounding spaces equals `name` (lower case), any case.
bool namedNoCase(std::string_view text, std::string_view name) {
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    if (text.size() != name.size()) {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) != name[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<PickupCamera> parsePickupCamera(std::string_view text) {
    for (const PickupCamera camera : {PickupCamera::Still, PickupCamera::Play}) {
        if (namedNoCase(text, pickupCameraName(camera))) {
            return camera;
        }
    }
    return std::nullopt;
}

const char* pickupCameraName(PickupCamera camera) {
    return camera == PickupCamera::Play ? "play" : "still";
}

} // namespace evr::comfort
