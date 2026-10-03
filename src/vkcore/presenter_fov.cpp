// The game's own FOV setting for the headset (head-tracked modes, docs/VR_HEAD_TRACKED.md): the symmetric FOV
// enclosing both eyes, found once from the runtime's views and held by the camera hook (presenter_head.cpp).

#include "vkcore/presenter_impl.hpp"

#include "xr_math/enclosing_fov.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace evr::vkcore {

void XrPresenter::Impl::updateTargetFov(XrTime time) {
    XrViewLocateInfo info{XR_TYPE_VIEW_LOCATE_INFO};
    info.viewConfigurationType = viewConfig;
    info.displayTime = time;
    info.space = viewSpace;
    XrViewState viewState{XR_TYPE_VIEW_STATE};
    std::array<XrView, 2> views{};
    for (XrView& v : views) {
        v.type = XR_TYPE_VIEW;
    }
    std::uint32_t count = 0;
    if (XR_FAILED(xr.xrLocateViews(session, &info, &viewState, static_cast<std::uint32_t>(views.size()),
                                   &count, views.data())) ||
        count != views.size() || !(viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
        return;
    }
    // One image serves both eyes, rendered from the head centre: the eyes' frusta are taken with
    // their orientation only, and the symmetric FOV enclosing both is what the game renders.
    std::array<xr_math::EyeView, 2> eyes{};
    for (std::size_t i = 0; i < views.size(); ++i) {
        const XrFovf& f = views[i].fov;
        eyes[i].fov = {f.angleLeft, f.angleRight, f.angleUp, f.angleDown};
        const XrQuaternionf& q = views[i].pose.orientation;
        eyes[i].poseInHead.orientation = normalize(Quat{q.x, q.y, q.z, q.w});
        EVR_LOG("xr: eye %zu fov left %.2f right %.2f up %.2f down %.2f deg, position (%.4f %.4f %.4f)", i,
                f.angleLeft * 57.29578f, f.angleRight * 57.29578f, f.angleUp * 57.29578f,
                f.angleDown * 57.29578f, views[i].pose.position.x, views[i].pose.position.y,
                views[i].pose.position.z);
    }
    const auto enclosing = xr_math::enclosingFov(eyes, 1.0f, xr_math::EnclosingShape::Symmetric);
    if (!enclosing) {
        EVR_LOG("xr: no enclosing FOV for the eyes; the game keeps its own FOV");
        fovChecked = true;
        return;
    }
    const auto tangents = xr_math::toTangents(*enclosing);
    const auto game = xr_math::gameFovFromTangents(tangents.right, tangents.up);
    fovChecked = true;
    if (!game) {
        EVR_LOG("xr: the enclosing FOV has no game equivalent; the game keeps its own FOV");
        return;
    }
    targetFovX.store(game->fovX);
    targetFovY.store(game->fovY);
    targetFovValid.store(true, std::memory_order_release);
    constexpr double kDegrees = 57.29578;
    const XrFovf& l = views[0].fov;
    const XrFovf& r = views[1].fov;
    // In stereo each eye renders its own FOV; in mono one image with this one serves both eyes.
    char perEye[192] = "";
    if (settings.stereo.enabled) {
        std::snprintf(perEye, sizeof(perEye),
                      "; each eye renders its own: eye L left %.2f right %.2f up %.2f down %.2f, eye R left "
                      "%.2f right %.2f up %.2f down %.2f deg",
                      l.angleLeft * kDegrees, l.angleRight * kDegrees, l.angleUp * kDegrees,
                      l.angleDown * kDegrees, r.angleLeft * kDegrees, r.angleRight * kDegrees,
                      r.angleUp * kDegrees, r.angleDown * kDegrees);
    }
    EVR_LOG("xr: the game's own FOV setting for the headset (symmetric, %s) %.2f x %.2f deg (tangents %.3f x "
            "%.3f, aspect %.3f)%s; the game's image is %ux%u (aspect %.3f)%s",
            settings.stereo.enabled ? "both eyes" : "one image for both eyes", game->fovX, game->fovY,
            tangents.right, tangents.up, tangents.right / tangents.up, perEye, eyeExtent.width,
            eyeExtent.height, static_cast<double>(eyeExtent.width) / static_cast<double>(eyeExtent.height),
            settings.setGameFov ? "" : "; not applied (ETERNALVR_SET_FOV=0)");
}

} // namespace evr::vkcore
