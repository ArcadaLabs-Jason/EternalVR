#pragma once

// The decals a Route S eye pair keeps (docs/rig-findings/stereo-umbra-decals.md).
//
// Umbra's per-view query (setup RVA 0x1D18BC0) culls with the latched projection. A Route S eye's frustum is
// narrower than the flat game's on its inner side (Quest 3: 40 degrees where the head-tracked mono view
// reaches 54), and with it Umbra reports decals hidden that still cover surfaces the eyes show: the wet
// patches of a puddled floor go missing, the floor turns darker and differs between the eyes. Both eyes draw
// the decal list eye L's view gathers. So, before that gather, every active decal whose bounds reach the
// frustum around the eye pair is added to Umbra's visible decals: a frustum centred on the eye's axis whose
// half-angle on each axis is the larger of the eye's wider side and the flat game's (fov_x / fov_y), tested
// box against side planes, without occlusion (a decal only shows on surfaces in view). Geometry, lights and
// world areas keep the eye's own query.

#include "stereo_seq/centered_matrix.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace evr::stereo_seq {

// The projection of the frustum around the eye pair for `latched` (the eye's latched projectionMatrix,
// row-major, the layout of xr_math::engineProjection) when the flat game's view is `fovXDegrees` x
// `fovYDegrees`: centred on the view axis, each half-angle at least the eye's wider side and at least the
// flat game's, the depth rows unchanged. A FOV outside (0, 180) degrees counts as absent. A projection that
// already is that frustum (a mono view), or a matrix that is not a perspective one, comes back unchanged.
Matrix4 pairDecalProjection(const Matrix4& latched, float fovXDegrees, float fovYDegrees);

// The four side planes of a view's frustum in world space: a point p is outside a plane when n . p > d,
// (n, d) = (plane[0..2], plane[3]). Near and far are not tested.
struct SideFrustum {
    std::array<std::array<float, 4>, 4> planes{};
};

// The side planes for a view at `origin` with `axis` (rows forward, left, up: renderView_t's viewaxis) and
// `projection` (the layout above). nullopt when any input is not finite or the projection is not a
// perspective one (a scale not above 1e-4).
std::optional<SideFrustum>
sideFrustum(const std::array<float, 3>& origin, const std::array<float, 9>& axis, const Matrix4& projection);

// True when the axis-aligned box (mins, maxs) lies entirely outside one of the frustum's side planes. A box
// that is not finite counts as outside (it is not added).
bool boxOutside(const SideFrustum& frustum, const float* mins, const float* maxs);

// Sets the bit (bit i of word i / 64) of every decal i below `count` for which `active(i)` holds and
// `inside(i)` holds, leaving every bit already set. Returns how many bits it set.
template <class Active, class Inside>
std::size_t addDecals(std::span<std::uint64_t> visible, std::size_t count, Active&& active, Inside&& inside) {
    std::size_t added = 0;
    const std::size_t n = count < visible.size() * 64 ? count : visible.size() * 64;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint64_t bit = std::uint64_t{1} << (i % 64);
        if ((visible[i / 64] & bit) != 0 || !active(i) || !inside(i)) {
            continue;
        }
        visible[i / 64] |= bit;
        ++added;
    }
    return added;
}

} // namespace evr::stereo_seq
