#include "xr_math/eye_clip_transform.hpp"

namespace evr::xr_math {

std::optional<Mat4>
eyeClipTransform(const Mat4& centerProj, const Mat4& centerView, const Mat4& eyeProj, const Mat4& eyeView) {
    const std::optional<Mat4> inverseProj = inverse(centerProj);
    const std::optional<Mat4> inverseView = inverse(centerView);
    if (!inverseProj || !inverseView) {
        return std::nullopt;
    }
    return eyeProj * eyeView * *inverseView * *inverseProj;
}

} // namespace evr::xr_math
