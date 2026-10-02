// OpenXR session events on the XR worker: session state changes, a lost runtime or instance, a LOCAL
// space change (a recenter), a refresh rate change (presenter_refresh.hpp), and placing the flat screen in
// front of the head.

#include "vkcore/presenter_impl.hpp"

#include "vkcore/keep_active.hpp"
#include "vkcore/status_file.hpp"
#include "xr_math/cinema_quad.hpp"

#include <optional>

namespace evr::vkcore {

namespace {

// The session ended: tracking stops and the worker leaves its frame loop, to reconnect when the loss allows
// it (presenter_reconnect.cpp). The first cause is the one kept.
void markLost(XrPresenter::Impl& p, xr_recovery::Loss kind, const char* what) {
    if (!p.loss.lost) {
        p.loss.kind = kind;
        if (xr_recovery::recovers(kind)) {
            status::waiting(
                "the headset disconnected or its runtime stopped: reconnect it or start the runtime "
                "again (VR comes back by itself)");
            EVR_LOG("xr: %s (%s); the game continues flat until the headset is back", what,
                    xr_recovery::lossName(kind));
        } else {
            status::flat("the headset runtime closed VR for the game; quit the game and start again from the "
                         "launcher");
            EVR_LOG("xr: %s; the runtime asked to leave VR, the game continues flat", what);
        }
    }
    p.trackingReady.store(false);
    disableKeepActive();
    p.sessionRunning = false;
    p.loss.lost = true;
}

} // namespace

bool XrPresenter::Impl::loseOnRuntimeFailure(XrResult result, const char* call) {
    if (result != XR_ERROR_SESSION_LOST && result != XR_ERROR_INSTANCE_LOST &&
        result != XR_ERROR_RUNTIME_FAILURE) {
        return false;
    }
    char text[XR_MAX_RESULT_STRING_SIZE];
    char what[160];
    std::snprintf(what, sizeof(what), "%s: %s", call, xrText(result, text));
    markLost(*this,
             result == XR_ERROR_SESSION_LOST ? xr_recovery::Loss::Session : xr_recovery::Loss::Instance,
             what);
    return true;
}

void XrPresenter::Impl::pollEvents() {
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    XrResult polled = XR_SUCCESS;
    while ((polled = xr.xrPollEvent(instance, &event)) == XR_SUCCESS) {
        switch (event.type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            const auto& changed = reinterpret_cast<const XrEventDataSessionStateChanged&>(event);
            sessionState = changed.state;
            sessionFocused.store(sessionState == XR_SESSION_STATE_FOCUSED, std::memory_order_relaxed);
            EVR_LOG("xr: session state %d", static_cast<int>(sessionState));
            if (sessionState == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                begin.primaryViewConfigurationType = viewConfig;
                const XrResult r = xr.xrBeginSession(session, &begin);
                sessionRunning = XR_SUCCEEDED(r);
                EVR_LOG("xr: xrBeginSession: %d", static_cast<int>(r));
                if (sessionRunning) {
                    loss.runningSinceQpc = qpcNow();
                    status::vr("the headset shows the game");
                    refresh.onSessionRunning(session);
                }
                if (sessionRunning && settings.keepActive) {
                    enableKeepActive();
                }
            } else if (sessionState == XR_SESSION_STATE_STOPPING) {
                trackingReady.store(false);
                disableKeepActive();
                xr.xrEndSession(session);
                sessionRunning = false;
                EVR_LOG("xr: session ended");
            } else if (sessionState == XR_SESSION_STATE_EXITING) {
                markLost(*this, xr_recovery::Loss::Exiting, "session exiting");
            } else if (sessionState == XR_SESSION_STATE_LOSS_PENDING) {
                markLost(*this, xr_recovery::Loss::Session, "session loss pending");
            }
            break;
        }
        case XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB:
            refresh.onRateChanged(reinterpret_cast<const XrEventDataDisplayRefreshRateChangedFB&>(event));
            break;
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            markLost(*this, xr_recovery::Loss::Instance, "instance loss pending");
            break;
        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
            const auto& change = reinterpret_cast<const XrEventDataReferenceSpaceChangePending&>(event);
            if (change.referenceSpaceType != XR_REFERENCE_SPACE_TYPE_LOCAL) {
                EVR_LOG("xr: reference space %d change pending", static_cast<int>(change.referenceSpaceType));
                break;
            }
            quadPlaced = false;
            placeAttempts = 0;
            menuReplace.store(true, std::memory_order_relaxed);
            const XrPosef& p = change.poseInPreviousSpace;
            // T-063: every recenter re-anchors yaw; the room keeps its height across the runtime's move.
            room.onSpaceChange(change.poseValid
                                   ? std::optional<Pose>(Pose{Quat{p.orientation.x, p.orientation.y,
                                                                   p.orientation.z, p.orientation.w},
                                                              Vec3{p.position.x, p.position.y, p.position.z}})
                                   : std::nullopt);
            EVR_LOG("xr: LOCAL change pending (pose %s: (%.3f %.3f %.3f)); re-placing the screen and "
                    "re-anchoring the room's heading",
                    change.poseValid ? "valid" : "not given", p.position.x, p.position.y, p.position.z);
            break;
        }
        default:
            break;
        }
        event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
    if (polled != XR_EVENT_UNAVAILABLE) {
        loseOnRuntimeFailure(polled, "xrPollEvent");
    }
    // ETERNALVR_TEST_XR_LOSS: the running session taken as lost once, as a headset going away would be.
    if (settings.testLossSeconds > 0.0f && !loss.testLossDone && sessionRunning &&
        qpcSeconds(qpcNow() - loss.runningSinceQpc) >= settings.testLossSeconds) {
        loss.testLossDone = true;
        loseOnRuntimeFailure(XR_ERROR_SESSION_LOST, "test (ETERNALVR_TEST_XR_LOSS)");
    }
}

void XrPresenter::Impl::placeQuad(XrTime time) {
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(xr.xrLocateSpace(viewSpace, localSpace, time, &location))) {
        return;
    }
    constexpr XrSpaceLocationFlags needed =
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
    if ((location.locationFlags & needed) != needed) {
        if (++placeAttempts == 90) {
            EVR_LOG("xr: head pose not valid yet; the screen stays at the default place");
        }
        return;
    }
    Pose head;
    head.orientation = {location.pose.orientation.x, location.pose.orientation.y, location.pose.orientation.z,
                        location.pose.orientation.w};
    head.position = {location.pose.position.x, location.pose.position.y, location.pose.position.z};
    const Pose quad = xr_math::cinemaQuadPose(head, kScreenDistanceMetres);
    quadPose.orientation = {quad.orientation.x, quad.orientation.y, quad.orientation.z, quad.orientation.w};
    quadPose.position = {quad.position.x, quad.position.y, quad.position.z};
    quadPlaced = true;
    EVR_LOG("xr: screen placed at (%.2f, %.2f, %.2f) in LOCAL", quadPose.position.x, quadPose.position.y,
            quadPose.position.z);
}

} // namespace evr::vkcore
