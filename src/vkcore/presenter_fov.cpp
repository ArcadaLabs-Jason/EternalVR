// The game's own FOV setting for the headset (head-tracked modes, docs/VR_HEAD_TRACKED.md): the symmetric FOV
// enclosing both eyes, from the runtime's views read again during the session
// (features/tracking/fov_watch.hpp) and held by the camera hook (presenter_head.cpp).

#include "vkcore/presenter_fov.hpp"
#include "vkcore/presenter_impl.hpp"

#include "xr_math/enclosing_fov.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>

namespace evr::vkcore {

namespace {

constexpr double kDegrees = 57.29578;

// Implausible reads logged: the first ones, then one in kImplausibleEvery, kImplausibleMaxLines in all; later
// ones are only counted (XR worker only).
constexpr std::uint64_t kImplausibleLines = 5;
constexpr std::uint64_t kImplausibleEvery = 100;
constexpr std::uint64_t kImplausibleMaxLines = 20;
std::uint64_t g_implausibleLogged = 0;
// Changes that wait to hold, and changes taken, logged; later ones are not.
constexpr std::uint64_t kWaitingLines = 10;
constexpr std::uint64_t kChangeLines = 10;
std::uint64_t g_waitingLogged = 0;

// "eye L left .. right .. up .. down .., eye R ..." in degrees.
void eyesText(const tracking::EyeFovs& eyes, char (&out)[160]) {
    const xr_math::Fov& l = eyes[0];
    const xr_math::Fov& r = eyes[1];
    std::snprintf(
        out, sizeof(out),
        "eye L left %.2f right %.2f up %.2f down %.2f, eye R left %.2f right %.2f up %.2f down %.2f deg",
        l.angleLeft * kDegrees, l.angleRight * kDegrees, l.angleUp * kDegrees, l.angleDown * kDegrees,
        r.angleLeft * kDegrees, r.angleRight * kDegrees, r.angleUp * kDegrees, r.angleDown * kDegrees);
}

} // namespace

std::uint64_t packGameFov(const xr_math::GameFov& fov) {
    return (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(fov.fovX)) << 32) |
           std::bit_cast<std::uint32_t>(fov.fovY);
}

std::optional<xr_math::GameFov> unpackGameFov(std::uint64_t packed) {
    if (packed == 0) {
        return std::nullopt;
    }
    return xr_math::GameFov{std::bit_cast<float>(static_cast<std::uint32_t>(packed >> 32)),
                            std::bit_cast<float>(static_cast<std::uint32_t>(packed))};
}

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
        return; // read again next frame
    }
    tracking::EyeFovs fovs{};
    for (std::size_t i = 0; i < views.size(); ++i) {
        const XrFovf& f = views[i].fov;
        fovs[i] = {f.angleLeft, f.angleRight, f.angleUp, f.angleDown};
    }
    using Outcome = tracking::FovWatch::Outcome;
    const Outcome outcome = fovWatch.onRead(fovs, qpcSeconds(qpcNow()), settings.checkFov);
    char text[160];
    if (outcome == Outcome::Implausible) {
        const std::uint64_t n = fovWatch.implausibleReads();
        if ((n <= kImplausibleLines || n % kImplausibleEvery == 0) &&
            g_implausibleLogged < kImplausibleMaxLines) {
            ++g_implausibleLogged;
            eyesText(fovs, text);
            const std::optional<xr_math::GameFov> used = unpackGameFov(targetFov.load());
            char kept[96] = "the game keeps its own FOV until a plausible one is read";
            if (used) {
                std::snprintf(kept, sizeof(kept), "the FOV in use stays (%.2f x %.2f deg)", used->fovX,
                              used->fovY);
            }
            EVR_LOG("xr: the runtime's eye FOVs (%s) are not a headset's: %s (%llu such read(s)); %s", text,
                    tracking::fovProblemText(fovWatch.problem()), static_cast<unsigned long long>(n), kept);
            if (g_implausibleLogged == kImplausibleMaxLines) {
                EVR_LOG("xr: %llu implausible FOV lines logged; later ones are only counted",
                        static_cast<unsigned long long>(kImplausibleMaxLines));
            }
        }
        return;
    }
    if (outcome == Outcome::Waiting) {
        if (++g_waitingLogged <= kWaitingLines) {
            eyesText(fovs, text);
            EVR_LOG("xr: the eyes' FOV differs from the one in use (%s); taken once %d reads in a row agree",
                    text, tracking::FovWatch::kStableReads);
        }
        return;
    }
    if (outcome != Outcome::First && outcome != Outcome::Changed) {
        return;
    }
    // The first FOV and the first kChangeLines changes are logged; later changes only change the FOV.
    const bool logged = outcome == Outcome::First || fovWatch.changes() <= kChangeLines;
    if (outcome == Outcome::Changed && logged) {
        EVR_LOG("xr: the headset's FOV changed (change %llu); the game's FOV follows%s",
                static_cast<unsigned long long>(fovWatch.changes()),
                fovWatch.changes() == kChangeLines ? "; later changes are not logged" : "");
    }
    // One image serves both eyes, rendered from the head centre: the eyes' frusta are taken with
    // their orientation only, and the symmetric FOV enclosing both is what the game renders.
    std::array<xr_math::EyeView, 2> eyes{};
    for (std::size_t i = 0; i < views.size(); ++i) {
        const XrFovf& f = views[i].fov;
        eyes[i].fov = fovs[i];
        const XrQuaternionf& q = views[i].pose.orientation;
        eyes[i].poseInHead.orientation = normalize(Quat{q.x, q.y, q.z, q.w});
        if (!logged) {
            continue;
        }
        EVR_LOG("xr: eye %zu fov left %.2f right %.2f up %.2f down %.2f deg, position (%.4f %.4f %.4f)", i,
                f.angleLeft * 57.29578f, f.angleRight * 57.29578f, f.angleUp * 57.29578f,
                f.angleDown * 57.29578f, views[i].pose.position.x, views[i].pose.position.y,
                views[i].pose.position.z);
    }
    const auto enclosing = xr_math::enclosingFov(eyes, 1.0f, xr_math::EnclosingShape::Symmetric);
    if (!enclosing) {
        if (logged) {
            EVR_LOG("xr: no enclosing FOV for the eyes; the game keeps %s",
                    targetFov.load() ? "the FOV in use" : "its own FOV");
        }
        return;
    }
    const auto tangents = xr_math::toTangents(*enclosing);
    const auto game = xr_math::gameFovFromTangents(tangents.right, tangents.up);
    if (!game) {
        if (logged) {
            EVR_LOG("xr: the enclosing FOV has no game equivalent; the game keeps %s",
                    targetFov.load() ? "the FOV in use" : "its own FOV");
        }
        return;
    }
    targetFov.store(packGameFov(*game), std::memory_order_release);
    if (!logged) {
        return;
    }
    // In stereo each eye renders its own FOV; in mono one image with this one serves both eyes.
    char perEye[192] = "";
    if (settings.stereo.enabled) {
        eyesText(fovs, text);
        std::snprintf(perEye, sizeof(perEye), "; each eye renders its own: %s", text);
    }
    EVR_LOG("xr: the game's own FOV setting for the headset (symmetric, %s) %.2f x %.2f deg (tangents %.3f x "
            "%.3f, aspect %.3f)%s; the game's image is %ux%u (aspect %.3f)%s",
            settings.stereo.enabled ? "both eyes" : "one image for both eyes", game->fovX, game->fovY,
            tangents.right, tangents.up, tangents.right / tangents.up, perEye, eyeExtent.width,
            eyeExtent.height, static_cast<double>(eyeExtent.width) / static_cast<double>(eyeExtent.height),
            settings.setGameFov ? "" : "; not applied (ETERNALVR_SET_FOV=0)");
}

} // namespace evr::vkcore
