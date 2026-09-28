#pragma once

// The OpenXR runtime's name, set once when the presenter creates its instance and read by the input side
// (the control map's runtime tweaks, features/input/dashboard_pause.hpp).

#include <string>

namespace evr::vkcore {

void setXrRuntimeName(const char* name);

// Empty until the instance exists.
std::string xrRuntimeName();

} // namespace evr::vkcore
