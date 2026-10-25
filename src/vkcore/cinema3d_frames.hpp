#pragma once

// Stereoscopic cutscenes (presenter_cinema3d.hpp): which game frames were rendered as a pair of the
// cutscene's camera, for the render passes' hooks.

#include <cstdint>

namespace evr::vkcore {

// Any thread: game frame `seq` (ViewRecord::seq, a render tag's tick) is one of the latest pair run's.
// Foveation keeps full rate for its passes (vrs_pass_eye.cpp): its rate images are made for the headset's
// eyes, whose centre is not the screen's.
bool cinema3dFrame(std::uint64_t seq);

} // namespace evr::vkcore
