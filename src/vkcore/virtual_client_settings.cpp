// The render size's settings (virtual_client_settings.hpp).

#include "vkcore/virtual_client_settings.hpp"

#include "vkcore/edge_margin_setting.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mirror_place.hpp"
#include "vkcore/taa_hooks.hpp"

#include <cstdio>
#include <cwchar>
#include <string>

namespace evr::vkcore::virtual_client {

namespace {

// The share of each side DLSS renders at the launcher's DLSS (render_size::dlssInputShare): stereo with
// ETERNALVR_STEREO_DLSS on and anti-aliasing not off (ETERNALVR_STEREO_TAA=0), as Route S and Parallel Eye
// Rendering run it; 1 without DLSS, with DLAA or when ETERNALVR_STEREO_DLSS_QUALITY names no quality (the
// game's own r_dlssQuality is not known this early).
double dlssInputAtLaunch() {
    std::wstring mode;
    std::wstring taa;
    const bool stereo = readEnv(L"ETERNALVR_MODE", mode) && _wcsicmp(mode.c_str(), L"stereo") == 0;
    readEnv(L"ETERNALVR_STEREO_TAA", taa);
    const bool antiAliasingOff = _wcsicmp(taa.c_str(), L"0") == 0 || _wcsicmp(taa.c_str(), L"false") == 0 ||
                                 _wcsicmp(taa.c_str(), L"off") == 0;
    if (!stereo || antiAliasingOff || !taaDlssRequested()) {
        return 1.0;
    }
    return render_size::dlssInputShare(taaDlssQuality(), taaDlssDlaa());
}

} // namespace

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
        // The edge margin grows a size the layer works out itself; a fixed size (the launcher's, already
        // grown) is used as it is.
        if (out.request.mode == render_size::Mode::Auto) {
            out.request.growth = static_cast<double>(edgeMarginFactor());
            // With DLSS the pixel budget holds what DLSS renders, not the size it scales up to.
            out.request.dlssInput = dlssInputAtLaunch();
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
