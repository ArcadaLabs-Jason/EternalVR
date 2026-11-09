// Motion controllers, the OpenXR spaces (controllers_impl.hpp): each hand's aim and grip action spaces, with
// the fit of that hand's controller family and the player's controller angle as their pose in action space
// (features/input/controller_fit.hpp, controller_tilt.hpp), and the view space.
//
// A hand's family is known only once the runtime reports its profile, a second or so after the action sets
// are attached, so the hand spaces are first created with no fit and a hand's are created again when its
// family has another fit (refitHand). The spaces they replace stay valid until detach: the presenter may have
// been given one for a layer of a frame still being submitted (weaponAimSpace, offHandGripSpace).

#include "vkcore/controllers_impl.hpp"

#include "features/input/controller_tilt.hpp"
#include "vkcore/log.hpp"
#include "vkcore/xr_runtime.hpp"

#include <array>
#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <string>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";

std::size_t handIndex(input::Hand hand) {
    return static_cast<std::size_t>(hand);
}

const char* handName(input::Hand hand) {
    return hand == input::Hand::Left ? "left" : "right";
}

XrPosef toXr(const Pose& p) {
    return {{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
            {p.position.x, p.position.y, p.position.z}};
}

// `hand`'s aim and grip spaces with `fit` (the runtime moves and turns every pose read from them). On a
// failure neither is kept, and the runtime's result is returned.
XrResult createHandSpaces(
    const XrInput& xr, input::Hand hand, const input::ControllerFit& fit, XrSpace& aim, XrSpace& grip) {
    const input::HandSpaceOffsets offsets = input::handSpaceOffsets(fit, settings(), hand, weaponHand());
    XrActionSpaceCreateInfo info{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    info.subactionPath = xr.handPaths[handIndex(hand)];
    info.poseInActionSpace = toXr(offsets.aim);
    info.action = xr.actions[static_cast<std::size_t>(input::XrActionId::AimPose)];
    XrResult r = xr.xrCreateActionSpace(xr.session, &info, &aim);
    if (XR_FAILED(r)) {
        aim = XR_NULL_HANDLE;
        return r;
    }
    info.poseInActionSpace = toXr(offsets.grip);
    info.action = xr.actions[static_cast<std::size_t>(input::XrActionId::GripPose)];
    r = xr.xrCreateActionSpace(xr.session, &info, &grip);
    if (XR_FAILED(r)) {
        xr.xrDestroySpace(aim);
        aim = grip = XR_NULL_HANDLE;
    }
    return r;
}

} // namespace

bool createSpaces(XrInput& xr) {
    for (const input::Hand hand : {input::Hand::Left, input::Hand::Right}) {
        const std::size_t i = handIndex(hand);
        EVR_XR_TRY(createHandSpaces(xr, hand, xr.fits[i], xr.aimSpaces[i], xr.gripSpaces[i]));
    }
    const XrPosef identity{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    XrReferenceSpaceCreateInfo view{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    view.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    view.poseInReferenceSpace = identity;
    EVR_XR_TRY(xr.xrCreateReferenceSpace(xr.session, &view, &xr.viewSpace));
    return true;
}

void refitHand(State& s, input::Hand hand, game::Controller family) {
    std::unique_lock lock(s.xrMutex);
    XrInput& xr = s.xr;
    const std::size_t i = handIndex(hand);
    if (!s.attached.load() || xr.fitFamilies[i] == family) {
        return;
    }
    const std::string runtime = xrRuntimeName();
    const input::ControllerFit fit = input::controllerFit(family, runtime);
    if (!(fit == xr.fits[i])) {
        input::RefitRetry& retry = xr.refitRetries[i];
        if (!retry.take(family, qpcToSeconds(nowQpc()))) {
            return;
        }
        XrSpace aim = XR_NULL_HANDLE;
        XrSpace grip = XR_NULL_HANDLE;
        if (const XrResult r = createHandSpaces(xr, hand, fit, aim, grip); XR_FAILED(r)) {
            char text[XR_MAX_RESULT_STRING_SIZE];
            EVR_LOG(
                "%s: the %s hand's spaces with its %s controller's fit were refused (%s); they keep their "
                "previous fit%s",
                kTag, handName(hand), std::string(game::controllerName(family)).c_str(), xrText(r, text),
                retry.exhausted(family) ? "; no more tries for this controller"
                                        : ", tried again in a few seconds");
            return;
        }
        for (const XrSpace old : {xr.aimSpaces[i], xr.gripSpaces[i]}) {
            if (old) {
                xr.retiredSpaces.push_back(old);
            }
        }
        xr.aimSpaces[i] = aim;
        xr.gripSpaces[i] = grip;
        xr.fits[i] = fit;
    }
    xr.fitFamilies[i] = family;
    const bool familyHasFit = !(input::controllerFit(family) == input::ControllerFit{});
    const bool otherRuntime = familyHasFit && !input::runtimeHasFits(runtime);
    EVR_LOG("%s: %s%s%s%s", kTag,
            input::fitText(hand, game::controllerName(family), fit, familyHasFit, settings(), weaponHand())
                .c_str(),
            otherRuntime ? " (runtime '" : "", otherRuntime ? runtime.c_str() : "", otherRuntime ? "')" : "");
}

void destroySpaces(XrInput& xr) {
    for (std::array<XrSpace, 2>* spaces : {&xr.aimSpaces, &xr.gripSpaces}) {
        for (XrSpace& space : *spaces) {
            if (space) {
                xr.xrDestroySpace(space);
                space = XR_NULL_HANDLE;
            }
        }
    }
    for (const XrSpace space : xr.retiredSpaces) {
        xr.xrDestroySpace(space);
    }
    xr.retiredSpaces.clear();
    if (xr.viewSpace) {
        xr.xrDestroySpace(xr.viewSpace);
        xr.viewSpace = XR_NULL_HANDLE;
    }
}

} // namespace evr::vkcore::controllers
