#include "vkcore/presenter_hud_lock.hpp"

#include "common/pose.hpp"
#include "vkcore/log.hpp"

namespace evr::vkcore {

namespace {

Pose fromXr(const XrPosef& p) {
    return {Quat{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
            Vec3{p.position.x, p.position.y, p.position.z}};
}

XrPosef toXr(const Pose& p) {
    return {{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
            {p.position.x, p.position.y, p.position.z}};
}

} // namespace

void lockToRenderedHead(XrCompositionLayerQuad& q,
                        XrSpace viewSpace,
                        XrSpace localSpace,
                        const XrPosef& renderedHead) {
    if (q.space != viewSpace) {
        return;
    }
    q.space = localSpace;
    q.pose = toXr(compose(fromXr(renderedHead), fromXr(q.pose)));
    static bool logged = false; // XR worker only
    if (!logged) {
        logged = true;
        EVR_LOG("ui: the head-locked HUD is shown at the head pose its frame was rendered with (LOCAL)");
    }
}

} // namespace evr::vkcore
