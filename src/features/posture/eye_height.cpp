#include "features/posture/eye_height.hpp"

namespace evr::posture {

float eyeHeightOffset(float anchorHeadHeight, float targetEyeHeight) {
    return targetEyeHeight - anchorHeadHeight;
}

} // namespace evr::posture
