#pragma once

// The screen-space reflections' edge fade in a frame both eyes share
// (docs/rig-findings/stereo-screen-space.md).
//
// Every SSR shader of the game (18 forward materials, 4 compute passes) fades a reflection by where its hit
// lands in the eye's image: smoothstep(0, 0.5, clamp(1 - dot(ndc, ndc), 0, 1)), ndc = the hit's reprojected
// uv * 2 - 1. Each eye's image is asymmetric (Quest 3: 54 degrees out, 40 in), so the fade ran over other
// world angles in each eye: from 27.2 degrees on the nose's side, 46.4 on the other. A reflection in one
// eye's nasal band faded in that eye only.
//
// The patch gives the fade the horizontal frame both eyes have pixels for, centred on the head and as wide as
// the eyes' inner edges: ndc_x' = (ndc_x + m2) / (1 - |m2|), m2 the eye projection's off-centre term. No new
// data reaches the shader: under Route S the view rotation the shader loads (three view parameters, the
// columns of a mat3) is sheared by c = m2 / m0 (screen_space_view.hpp), so c = X.x X.z + Y.x Y.z + Z.x Z.z,
// and the packed projection the shader loads holds -0.5 m0 in x, so m2 = -2 packed.x c. With an unsheared
// rotation (mono, the flat game) c is 0 and the fade is the original. Vertically both eyes have the same
// frustum and the fade keeps the eye's own.
//
// Only modules known byte for byte are patched (their hash before and after; the patched words were validated
// with spirv-val), with the view parameters' member indices of each; anything else is passed on unchanged.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace evr::vkcore::ssr_fade {

struct KnownModule {
    std::uint64_t inputHash;  // fnv1a64 of the game's SPIR-V words
    std::uint32_t rowsMember; // the view block's member holding the rotation's first column (X); Y, Z follow
    std::uint32_t packedMember; // the view block's member holding packedProjection
    std::uint64_t outputHash;   // fnv1a64 of the patched words that were validated
};

inline constexpr std::size_t kKnownModules = 22;

// The game's SSR modules (Steam build 25216728).
const std::array<KnownModule, kKnownModules>& knownModules();

// Index into knownModules() of `code`'s hash, or nullopt.
std::optional<std::size_t> knownIndex(std::span<const std::uint32_t> code);

// The patch on `code` for the given members; nullopt (with `why`) when the module does not have the expected
// shape: exactly one fade, exactly one rotation built from those members, a packed projection load from the
// same block.
std::optional<std::vector<std::uint32_t>> patchFade(std::span<const std::uint32_t> code,
                                                    std::uint32_t rowsMember,
                                                    std::uint32_t packedMember,
                                                    std::string& why);

// True when `code` holds the SSR fade's signature (an OpDot of a vec2 with itself, 1 minus it, then
// smoothstep(0, 0.5, ...)) and a mat3 built from view parameters: a module the game could fade SSR with.
bool hasFadeSignature(std::span<const std::uint32_t> code);

} // namespace evr::vkcore::ssr_fade
