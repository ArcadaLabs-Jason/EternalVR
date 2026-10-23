#include "xr_math/marker_projection.hpp"

#include <cmath>

namespace evr::xr_math {

namespace {

bool validQuad(const MarkerQuad& q) {
    const bool sizes = q.targetWidth > 0 && q.targetHeight > 0 && q.shownWidth > 0 && q.shownHeight > 0;
    const bool inside = q.shownX >= 0 && q.shownY >= 0 && q.shownX + q.shownWidth <= q.targetWidth &&
                        q.shownY + q.shownHeight <= q.targetHeight;
    return sizes && inside && std::isfinite(q.widthMetres) && q.widthMetres > 0.0f &&
           std::isfinite(q.distanceMetres) && q.distanceMetres > 0.0f && std::isfinite(q.offsetYMetres);
}

// The quad's height: its width scaled by the shown part's aspect (ui_layer::panelSize).
float quadHeight(const MarkerQuad& q) {
    return q.widthMetres * static_cast<float>(q.shownHeight) / static_cast<float>(q.shownWidth);
}

} // namespace

std::optional<Fov> markerTargetFov(const MarkerQuad& quad) {
    const auto left = tangentsOfQuadPixel(quad, {0.0f, 0.0f});
    const auto right = tangentsOfQuadPixel(
        quad, {static_cast<float>(quad.targetWidth), static_cast<float>(quad.targetHeight)});
    if (!left || !right) {
        return std::nullopt;
    }
    return fromTangents({left->right, right->right, left->up, right->up});
}

std::optional<EngineMatrix> markerProjection(const MarkerQuad& quad, const EngineMatrix& engine) {
    const auto fov = markerTargetFov(quad);
    if (!fov) {
        return std::nullopt;
    }
    return engineProjection(*fov, engine);
}

std::optional<GuiPoint> quadPixelOfTangents(const MarkerQuad& quad, float right, float up) {
    if (!validQuad(quad) || !std::isfinite(right) || !std::isfinite(up)) {
        return std::nullopt;
    }
    const float height = quadHeight(quad);
    // The point on the quad's plane, from the quad's top-left corner, in metres.
    const float fromLeft = right * quad.distanceMetres + 0.5f * quad.widthMetres;
    const float fromTop = 0.5f * height + quad.offsetYMetres - up * quad.distanceMetres;
    return GuiPoint{
        static_cast<float>(quad.shownX) + fromLeft / quad.widthMetres * static_cast<float>(quad.shownWidth),
        static_cast<float>(quad.shownY) + fromTop / height * static_cast<float>(quad.shownHeight)};
}

std::optional<ViewTangents> tangentsOfQuadPixel(const MarkerQuad& quad, GuiPoint p) {
    if (!validQuad(quad) || !std::isfinite(p.x) || !std::isfinite(p.y)) {
        return std::nullopt;
    }
    const float height = quadHeight(quad);
    const float fromLeft =
        (p.x - static_cast<float>(quad.shownX)) / static_cast<float>(quad.shownWidth) * quad.widthMetres;
    const float fromTop =
        (p.y - static_cast<float>(quad.shownY)) / static_cast<float>(quad.shownHeight) * height;
    return ViewTangents{(fromLeft - 0.5f * quad.widthMetres) / quad.distanceMetres,
                        (0.5f * height + quad.offsetYMetres - fromTop) / quad.distanceMetres};
}

std::optional<GuiPoint>
projectToScreen(const EngineMatrix& m, float x, float y, float z, float width, float height) {
    const float cx = m[0] * x + m[1] * y + m[2] * z + m[3];
    const float cy = m[4] * x + m[5] * y + m[6] * z + m[7];
    const float w = m[12] * x + m[13] * y + m[14] * z + m[15];
    if (!(w > 0.0f) || !std::isfinite(cx) || !std::isfinite(cy)) {
        return std::nullopt;
    }
    return GuiPoint{(1.0f + cx / w) * 0.5f * width, (1.0f - cy / w) * 0.5f * height};
}

bool markerOffFrame(bool gameOffFrame, GuiPoint drawn, float width, float height) {
    return gameOffFrame || !(drawn.x >= 0.0f && drawn.x <= width && drawn.y >= 0.0f && drawn.y <= height);
}

} // namespace evr::xr_math
