#include "xr_math/head_view.hpp"

#include <cmath>
#include <numbers>

namespace evr::xr_math {

namespace {

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

constexpr float kDegreesPerRadian = 180.0f / std::numbers::pi_v<float>;

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
    Vec3 flat{gameAxis.forward.x, gameAxis.forward.y, 0.0f};
    if (length(flat) < 1e-3f) {
        // Straight down: the view's up points along the heading; straight up: against it.
        const float sign = gameAxis.forward.z < 0.0f ? 1.0f : -1.0f;
        flat = Vec3{gameAxis.up.x, gameAxis.up.y, 0.0f} * sign;
        if (length(flat) < 1e-3f) {
            return std::nullopt;
        }
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
