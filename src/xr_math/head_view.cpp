#include "xr_math/head_view.hpp"

#include <cmath>
#include <numbers>

namespace evr::xr_math {

namespace {

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

constexpr float kDegreesPerRadian = 180.0f / std::numbers::pi_v<float>;
// yawOnly: how far off the horizon (degrees) the view's up starts to give the heading, and gives all of it.
constexpr float kUpHeadingFrom = 75.0f;
constexpr float kUpHeadingFull = 85.0f;

} // namespace

IdViewAxis viewAxisFromQuat(Quat orientation) {
    const Quat q = normalize(orientation);
    return {
        rotate(q, Vec3{1.0f, 0.0f, 0.0f}),
        rotate(q, Vec3{0.0f, 1.0f, 0.0f}),
        rotate(q, Vec3{0.0f, 0.0f, 1.0f}),
    };
}

bool isOrthonormal(const IdViewAxis& axis, float tolerance) {
    if (!finite(axis.forward) || !finite(axis.left) || !finite(axis.up)) {
        return false;
    }
    const auto near = [tolerance](float a, float b) {
        return std::fabs(a - b) <= tolerance;
    };
    if (!near(dot(axis.forward, axis.forward), 1.0f) || !near(dot(axis.left, axis.left), 1.0f) ||
        !near(dot(axis.up, axis.up), 1.0f)) {
        return false;
    }
    if (!near(dot(axis.forward, axis.left), 0.0f) || !near(dot(axis.forward, axis.up), 0.0f) ||
        !near(dot(axis.left, axis.up), 0.0f)) {
        return false;
    }
    const Vec3 c = cross(axis.forward, axis.left);
    return near(c.x, axis.up.x) && near(c.y, axis.up.y) && near(c.z, axis.up.z);
}

std::optional<IdViewAxis> yawOnly(const IdViewAxis& gameAxis) {
    if (!finite(gameAxis.forward) || !finite(gameAxis.up)) {
        return std::nullopt;
    }
    // Straight down the view's up points along the heading; straight up, against it. Near there the
    // forward's own heading is the small part of it off the vertical, which a rolled camera swings round
    // while its image hardly turns (the death camera: 100 degrees a frame at 86 degrees down): from
    // kUpHeadingFrom off the horizon the heading moves over to the up's, all of it from kUpHeadingFull.
    const Vec3 forwardFlat{gameAxis.forward.x, gameAxis.forward.y, 0.0f};
    const float sign = gameAxis.forward.z < 0.0f ? 1.0f : -1.0f;
    const Vec3 upFlat = Vec3{gameAxis.up.x, gameAxis.up.y, 0.0f} * sign;
    const float off = std::asin(std::fmin(std::fabs(gameAxis.forward.z), 1.0f)) * kDegreesPerRadian;
    const float t =
        std::fmin(std::fmax((off - kUpHeadingFrom) / (kUpHeadingFull - kUpHeadingFrom), 0.0f), 1.0f);
    const bool forwardHeading = length(forwardFlat) >= 1e-3f;
    const bool upHeading = length(upFlat) >= 1e-3f;
    Vec3 flat = forwardFlat;
    if (t > 0.0f && upHeading) {
        // The two headings' unit vectors weighted by the share t. Not their angles: a camera rolled past 180
        // degrees flips the shorter way round between them, a jump of t times a full turn in one frame. The
        // sum is continuous; only the two opposite at exactly half and half leave no heading (the larger
        // share's then).
        const Vec3 fromUp = normalize(upFlat);
        flat = forwardHeading && t < 1.0f ? normalize(forwardFlat) * (1.0f - t) + fromUp * t : fromUp;
        if (length(flat) < 1e-3f) {
            flat = t >= 0.5f ? fromUp : forwardFlat;
        }
    } else if (!forwardHeading) {
        return std::nullopt;
    }
    flat = normalize(flat);
    IdViewAxis body;
    body.forward = flat;
    body.up = Vec3{0.0f, 0.0f, 1.0f};
    body.left = cross(body.up, body.forward);
    return body;
}

IdViewAxis composeHeadAxis(const IdViewAxis& body, Quat headInIdTech) {
    const IdViewAxis head = viewAxisFromQuat(headInIdTech);
    const auto toWorld = [&body](Vec3 local) {
        return body.forward * local.x + body.left * local.y + body.up * local.z;
    };
    return {toWorld(head.forward), toWorld(head.left), toWorld(head.up)};
}

Vec3 headOffsetInWorld(const IdViewAxis& body, Vec3 headPositionOpenXr, float unitsPerMetre) {
    const Vec3 local = openXrToIdTech(headPositionOpenXr) * unitsPerMetre;
    return body.forward * local.x + body.left * local.y + body.up * local.z;
}

std::optional<GameFov> gameFovFromTangents(float tanHalfX, float tanHalfY) {
    if (!std::isfinite(tanHalfX) || !std::isfinite(tanHalfY) || !(tanHalfX > 0.0f) || !(tanHalfY > 0.0f)) {
        return std::nullopt;
    }
    return GameFov{2.0f * std::atan(tanHalfX) * kDegreesPerRadian,
                   2.0f * std::atan(tanHalfY) * kDegreesPerRadian};
}

std::optional<Fov> fovFromGame(const GameFov& game) {
    const auto valid = [](float degrees) {
        return std::isfinite(degrees) && degrees > 0.0f && degrees < 180.0f;
    };
    if (!valid(game.fovX) || !valid(game.fovY)) {
        return std::nullopt;
    }
    const float halfX = 0.5f * game.fovX / kDegreesPerRadian;
    const float halfY = 0.5f * game.fovY / kDegreesPerRadian;
    return Fov{-halfX, halfX, halfY, -halfY};
}

} // namespace evr::xr_math
