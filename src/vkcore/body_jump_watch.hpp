#pragma once

// The body yaw turning more than 90 degrees in one game frame (a stick turn never does): logged with what
// drove the view, to find the rare half turn seen once in a seated glory kill (2026-09-28). Camera hook
// thread only.

namespace evr::vkcore {

// Each head-tracked game view, with the body yaw the view was built on and what drove it.
void watchBodyYaw(float yaw, bool headAimed, bool forcedView, bool cutscene, bool menuUp);

} // namespace evr::vkcore
