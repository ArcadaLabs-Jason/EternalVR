#pragma once

// The game FOV the XR worker found for the headset (presenter_fov.cpp), in one atomic word: the camera hook
// never reads half of a change.

#include "xr_math/head_view.hpp"

#include <cstdint>
#include <optional>

namespace evr::vkcore {

std::uint64_t packGameFov(const xr_math::GameFov& fov);
// nullopt for 0 (none found yet).
std::optional<xr_math::GameFov> unpackGameFov(std::uint64_t packed);

} // namespace evr::vkcore
