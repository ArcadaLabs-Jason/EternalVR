// The render size's settings (virtual_client_settings.hpp).

#include "vkcore/virtual_client_settings.hpp"

#include "vkcore/log.hpp"
#include "vkcore/mirror_place.hpp"

#include <cstdio>
#include <string>

namespace evr::vkcore::virtual_client {

const Settings& settings() {
    static const Settings s = [] {
        Settings out;
        std::wstring size;
        std::wstring scale;
        std::wstring mirror;
        std::wstring window;
        readEnv(L"ETERNALVR_RENDER_SIZE", size);
        const auto request = render_size::parseRenderSize(size);
        if (!request) {
            EVR_LOG("size: ETERNALVR_RENDER_SIZE '%ls' is not auto, WxH or off; off", size.c_str());
            return out;
        }
        out.request = *request;
        if (readEnv(L"ETERNALVR_RENDER_SCALE", scale)) {
            if (const auto parsed = render_size::parseRenderScale(scale)) {
                out.request.scale = *parsed;
            } else {
                EVR_LOG("size: ETERNALVR_RENDER_SCALE '%ls' is not auto or 0.5 to 2; 1.0", scale.c_str());
            }
        }
        if (readEnv(L"ETERNALVR_MIRROR_WINDOW", mirror) && !mirror.empty()) {
            out.mirror = render_size::parseWindowRect(mirror);
            if (!out.mirror) {
                EVR_LOG(
                    "size: ETERNALVR_MIRROR_WINDOW '%ls' is not x,y,width,height; the window keeps its place",
                    mirror.c_str());
            }
        }
        if (readEnv(L"ETERNALVR_WINDOW", window) && !window.empty()) {
            out.fallback = render_size::parseWindowRect(window);
        }
        if (out.request.mode != render_size::Mode::Off) {
            const mirror_place::Options options = mirror_place::read(out.mirror);
            out.mirror = options.rect;
            out.crop = options.crop;
            out.fill = options.fill;
            out.centred = options.centred;
            char mirrorText[64] = "where it is";
            if (out.mirror) {
                std::snprintf(mirrorText, sizeof(mirrorText), "at %d,%d %dx%d", out.mirror->x, out.mirror->y,
                              out.mirror->width, out.mirror->height);
            }
            EVR_LOG("size: render size %s (T-031); the desktop window shows it scaled, %s",
                    render_size::describe(out.request).c_str(), mirrorText);
        }
        return out;
    }();
    return s;
}

bool wantedSize() {
    return settings().request.mode != render_size::Mode::Off;
}

std::optional<render_size::WindowRect> shapedMirror(const std::optional<render_size::Extent>& eye) {
    const Settings& set = settings();
    return mirror_place::shapeToEye(mirror_place::Options{set.mirror, set.crop, set.fill, set.centred}, eye);
}

} // namespace evr::vkcore::virtual_client
