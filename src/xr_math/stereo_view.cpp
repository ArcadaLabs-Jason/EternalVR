#include "xr_math/stereo_view.hpp"

#include <cmath>
#include <cstddef>

namespace evr::xr_math {

namespace {

bool finite(float v) {
    return std::isfinite(v);
}

} // namespace

IdEyeView
eyeViewInWorld(const IdViewAxis& body, Quat headOpenXr, const Pose& eyeInHead, float unitsPerMetre) {
    const Quat head = normalize(headOpenXr);
    const Quat eye = normalize(head * eyeInHead.orientation);
    IdEyeView view;
    // The eye's offset from the head centre in the tracking space, then into the world like the head's.
    view.offset = headOffsetInWorld(body, rotate(head, eyeInHead.position), unitsPerMetre);
    view.axis = composeHeadAxis(body, openXrToIdTech(eye));
    return view;
}

Pose eyePoseInSpace(const Pose& head, const Pose& eyeInHead) {
    Pose eye = compose(head, eyeInHead);
    eye.orientation = normalize(eye.orientation);
    return eye;
}

Pose eyeInHeadFromSpace(const Pose& headInSpace, const Pose& eyeInSpace) {
    Pose eye = compose(inverse(headInSpace), eyeInSpace);
    eye.orientation = normalize(eye.orientation);
    return eye;
}

bool plausibleEyeInHead(const Pose& eyeInHead) {
    const Vec3 p = eyeInHead.position;
    const Quat q = eyeInHead.orientation;
    if (!finite(p.x) || !finite(p.y) || !finite(p.z) || !finite(q.x) || !finite(q.y) || !finite(q.z) ||
        !finite(q.w)) {
        return false;
    }
    return length(p) <= 0.15f;
}

bool isEnginePerspective(const EngineMatrix& m) {
    for (float v : m) {
        if (!finite(v)) {
            return false;
        }
    }
    return m[14] == -1.0f && m[15] == 0.0f && m[12] == 0.0f && m[13] == 0.0f &&
           (m[10] != 0.0f || m[11] != 0.0f);
}

std::optional<EngineMatrix> engineProjection(const Fov& fov, const EngineMatrix& depthSource) {
    if (!isEnginePerspective(depthSource)) {
        return std::nullopt;
    }
    const auto valid = [](float angle) {
        return finite(angle) && std::fabs(angle) < 1.5607964f;
    }; // < 89.4 deg
    if (!valid(fov.angleLeft) || !valid(fov.angleRight) || !valid(fov.angleUp) || !valid(fov.angleDown)) {
        return std::nullopt;
    }
    const FovTangents t = toTangents(fov);
    const float width = t.right - t.left;
    const float height = t.up - t.down;
    if (!(width > 1e-4f) || !(height > 1e-4f)) {
        return std::nullopt;
    }
    EngineMatrix m{};
    m[0] = 2.0f / width;
    m[2] = (t.right + t.left) / width;
    m[5] = 2.0f / height;
    m[6] = (t.up + t.down) / height;
    for (int i = 8; i < 16; ++i) {
        m[static_cast<std::size_t>(i)] = depthSource[static_cast<std::size_t>(i)];
    }
    return m;
}

std::optional<Fov> fovOfEngineProjection(const EngineMatrix& m) {
    if (!(m[0] > 0.0f) || !(m[5] > 0.0f) || !finite(m[2]) || !finite(m[6])) {
        return std::nullopt;
    }
    // x_ndc = m0 * tan - m2 over the frustum's tangents: right edge +1, left edge -1.
    const float right = (1.0f + m[2]) / m[0];
    const float left = (-1.0f + m[2]) / m[0];
    const float up = (1.0f + m[6]) / m[5];
    const float down = (-1.0f + m[6]) / m[5];
    return fromTangents({left, right, up, down});
}

PixelRect sideBySideRect(int eye, int width, int height) {
    const float x0 = eye == 0 ? 0.0f : 0.5f;
    const float x1 = x0 + 0.5f;
    PixelRect rect;
    rect.x = static_cast<int>(x0 * static_cast<float>(width));
    rect.width = static_cast<int>(x1 * static_cast<float>(width)) - rect.x;
    rect.y = 0;
    rect.height = height;
    return rect;
}

} // namespace evr::xr_math
