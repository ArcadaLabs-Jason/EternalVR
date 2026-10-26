#pragma once

// The head-locked HUD shown at the head pose its frame was rendered with (presenter_frame.cpp).

#include <openxr/openxr.h>

namespace evr::vkcore {

// A head-locked HUD quad (`q`, posed in `viewSpace`) moved to `localSpace` at `renderedHead`, the head pose
// in LOCAL the frame's eye images were rendered with. The GUI image, its markers included, is drawn for that
// pose as the eye images are, so the runtime now moves the quad with them for the head's motion until the
// frame is shown; in VIEW the quad followed the head as shown, and a marker sat off its target by the turn
// between the two poses. A quad in any other space (a hand quad) is left as it is.
void lockToRenderedHead(XrCompositionLayerQuad& q,
                        XrSpace viewSpace,
                        XrSpace localSpace,
                        const XrPosef& renderedHead);

} // namespace evr::vkcore
