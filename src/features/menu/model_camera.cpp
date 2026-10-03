#include "features/menu/model_camera.hpp"

#include "common/finite.hpp"
#include "common/quat.hpp"

#include <cmath>

namespace evr::menu {

namespace {

constexpr float kDegrees = 57.29578f;

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool unit(Vec3 v) {
    return finite(v) && std::fabs(length(v) - 1.0f) < 1e-2f;
}

bool finitePose(const Pose& p) {
    const Quat& q = p.orientation;
    return finite(p.position) && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) &&
           std::isfinite(q.w) && (q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w) > 1e-6f;
}

} // namespace

Vec3 worldDirection(const WorldHead& head, Vec3 local) {
    // Into the head's own axes (OpenXR), then id Tech's (forward = -Z, left = -X, up = +Y) on the game's
    // view of the head.
    const Vec3 h = rotate(conjugate(normalize(head.local.orientation)), local);
    return head.forward * -h.z + head.left * -h.x + head.up * h.y;
}

Vec3 worldPoint(const WorldHead& head, Vec3 local) {
    return head.origin + worldDirection(head, local - head.local.position) * head.unitsPerMetre;
}

std::optional<ModelCamera> panelCamera(const PanelImage& image, const WorldHead& head, float fovXDegrees) {
    const Panel& panel = image.panel;
    if (!finiteInRange(fovXDegrees, 1.0f, 170.0f) || !finiteInRange(head.unitsPerMetre, 0.01f, 100.0f) ||
        !(panel.width > 0.0f) || !(panel.height > 0.0f) || !std::isfinite(panel.width) ||
        !std::isfinite(panel.height) || !(image.rectWidth > 0.0f) || !(image.rectHeight > 0.0f) ||
        !std::isfinite(image.rectX) || !std::isfinite(image.rectY) || !std::isfinite(image.rectWidth) ||
        !std::isfinite(image.rectHeight) || image.imageWidth == 0 || image.imageHeight == 0 ||
        !finitePose(panel.pose) || !finitePose(head.local) || !finite(head.origin) || !unit(head.forward) ||
        !unit(head.left) || !unit(head.up)) {
        return std::nullopt;
    }
    // Metres per image pixel on the panel, and where the whole image's centre is on it (the 16:9 band of a
    // taller image is centred: no offset).
    const float perPixelX = panel.width / image.rectWidth;
    const float perPixelY = panel.height / image.rectHeight;
    const float imageWidth = static_cast<float>(image.imageWidth);
    const float imageHeight = static_cast<float>(image.imageHeight);
    const float centreX = (imageWidth * 0.5f - (image.rectX + image.rectWidth * 0.5f)) * perPixelX;
    const float centreY = -(imageHeight * 0.5f - (image.rectY + image.rectHeight * 0.5f)) * perPixelY;
    const float halfWidth = imageWidth * perPixelX * 0.5f;
    const float halfHeight = imageHeight * perPixelY * 0.5f;
    const float distance = halfWidth / std::tan(fovXDegrees * 0.5f / kDegrees);

    const Quat q = normalize(panel.pose.orientation);
    const Vec3 local = transformPoint(Pose{q, panel.pose.position}, Vec3{centreX, centreY, distance});
    ModelCamera camera;
    camera.origin = worldPoint(head, local);
    camera.forward = worldDirection(head, rotate(q, Vec3{0.0f, 0.0f, -1.0f}));
    camera.left = worldDirection(head, rotate(q, Vec3{-1.0f, 0.0f, 0.0f}));
    camera.up = worldDirection(head, rotate(q, Vec3{0.0f, 1.0f, 0.0f}));
    camera.fovX = fovXDegrees;
    camera.fovY = 2.0f * std::atan(halfHeight / distance) * kDegrees;
    camera.panelDistance = distance * head.unitsPerMetre;
    if (!finite(camera.origin) || !std::isfinite(camera.fovY) || !(camera.panelDistance > 0.0f)) {
        return std::nullopt;
    }
    return camera;
}

std::optional<ModelOnPanel> modelOnPanel(const ModelCamera& camera, Vec3 position, Vec3 scale) {
    if (!finite(position) || !finite(scale)) {
        return std::nullopt;
    }
    const Vec3 fromCamera = position - camera.origin;
    const float depth = dot(fromCamera, camera.forward);
    if (!(depth > 0.0f)) {
        return std::nullopt;
    }
    const float factor = camera.panelDistance / depth;
    if (!finiteInRange(factor, kMinModelFactor, kMaxModelFactor)) {
        return std::nullopt;
    }
    return ModelOnPanel{camera.origin + fromCamera * factor, scale * factor, factor};
}

} // namespace evr::menu
