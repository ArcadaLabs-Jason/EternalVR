#pragma once

// Parallel Eye Rendering's water (view_water.hpp): the code it hooks and relies on, Steam build 25216728
// (RVAs), and their byte checks.

#include <cstddef>
#include <cstdint>

namespace evr::vkcore::view_water_sites {

inline constexpr std::uint32_t kSetup = 0x1CE3A90;    // the water setup's entry: rcx = the water context
inline constexpr std::uint32_t kSetupEnd = 0x1CE53AB; // its one way out, after the async flag: rdi = context
inline constexpr std::uint32_t kJobState = 0x1CE2B1A; // the water job's read of the state: r14 = the context
// The grid mesh's (re)make (state): from the setup only (0x1CE3E80); frees the old mesh and makes a new one
// when the state has none or r_waterGridResolution is "modified".
inline constexpr std::uint32_t kGridMesh = 0x1CE3430;
// `mov rcx, [rip + cvar]` before each "modified" test the setup makes: r_waterGridResolution (the grid mesh
// is remade, 0x1CE3430) and r_waterQualityFFT (the wave spectrum is rebuilt, +0x1124).
inline constexpr std::uint32_t kGridResolutionRead = 0x1CE3439;
inline constexpr std::uint32_t kQualityFftRead = 0x1CE3D5C;

// Every site as known; each one that is not is logged with the bytes found and those expected. From before
// anything of the game is changed: the redirects hook RVA 0x1C57880, one of the sites (view_redirects.cpp).
bool known(const std::byte* base);

// The target of the `mov rcx, [rip + disp32]` at `rva`.
const std::byte* ripTarget(const std::byte* base, std::uint32_t rva);

} // namespace evr::vkcore::view_water_sites
