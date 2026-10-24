#pragma once

// The decals a Route S eye pair keeps (docs/rig-findings/stereo-umbra-decals.md).
//
// Umbra's per-view query (setup RVA 0x1D18BC0) culls with the latched projection. A Route S eye's frustum is
// narrower than the flat game's on its inner side (Quest 3: 40 degrees where the head-tracked mono view
// reaches 54), and with it Umbra reports decals hidden that still cover surfaces the eyes show: the wet
// patches of a puddled floor go missing, the floor turns darker and differs between the eyes. Both eyes draw
// the decal list eye L's view gathers. So the decals whose bounds reach the frustum around the eye pair (a
// frustum centred on the eye's axis whose half-angle on each axis is the larger of the eye's wider side and
// the flat game's fov_x / fov_y, tested box against side planes, without occlusion: a decal only shows on
// surfaces in view) are chosen at the gather, the nearest ones, and follow the engine's own list after its
// scoring (see the limit below). Geometry, lights and world areas keep the eye's own query.

#include "stereo_seq/centered_matrix.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

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

// What the engine does with a view's decals after the gather (read statically, Steam build; the same bodies
// in the Game Pass 1.0.56.0 image): its scoring job (RVA 0x1CB8950) scores each listed decal and writes one
// 64-bit key per decal it keeps (decals facing away or too small drop out), the record index in the low 16
// bits; its sorting job (RVA 0x1CB8D80) sorts the keys, keeps the first 2048 and rewrites the list from them
// (list[i] = records + (key & 0xFFFF) * 0x50). Past the first 1,664 or so of that list the decals stop
// drawing. That limit was measured, not read (what takes the sorted list on was not read): at the eye view
// the floor's decals held with 1,660 scored decals and were lost from 1,678, at every yaw and for every list
// size tried (docs/rig-findings/stereo-umbra-decals.md section 7). The flat game's list is Umbra's alone, a
// few dozen decals in a tunnel and about 1,600 in the busiest scene tried; adding every decal in the pair's
// frustum to it (thousands) let the ones the engine ranks first push the decals Umbra itself reported past
// the limit.
//
// So the decals chosen for the pair are scored and sorted by the engine's own jobs on their own and follow
// the engine's list, under `kViewDecalSafeCount` in all (see `placeExtras` for when the engine's list gives
// way).
inline constexpr std::size_t kViewDecalCapacity = 1664;
inline constexpr std::size_t kViewDecalHeadroom = 32;
inline constexpr std::size_t kViewDecalSafeCount = kViewDecalCapacity - kViewDecalHeadroom;
// How many decals the gather chooses for the engine's scoring: the nearest ones in the pair's frustum, chosen
// before scoring, so a decal past them is never scored even if the engine would rank it high (a large one far
// away). Within the sorting job's 2048, so the engine's sort of them cuts nothing.
inline constexpr std::size_t kPairCandidates = 2048;

// How many decals may follow the `kept` the engine's scoring left for a view without passing the limit.
constexpr std::size_t extraRoom(std::size_t kept) {
    return kept < kViewDecalSafeCount ? kViewDecalSafeCount - kept : 0;
}

// The least the chosen decals may take when the engine's own list leaves less room. In a busy scene (the
// Reclaimed Earth start: about 1,400 decals kept) the room left is about 250, and the wet patches of that
// page's section 1 came back for eye L with the nearest 400 and for both eyes with 640 (250 and 400 brought
// back neither / one). The engine's own last-ranked decals give way for them, never more than this many in
// all.
inline constexpr std::size_t kPairMinimum = 640;

// Where the `scored` chosen decals that the engine's scoring kept go, given the `kept` it left: the first
// `keep` of the engine's list stay, and `take` chosen decals follow them. keep + take never passes the limit
// once something is placed, and the engine's list gives up decals only to make `kPairMinimum` of room.
struct Placement {
    std::size_t keep = 0;
    std::size_t take = 0;
};
constexpr Placement placeExtras(std::size_t kept, std::size_t scored) {
    const std::size_t room = extraRoom(kept);
    const std::size_t want = room > kPairMinimum ? room : kPairMinimum;
    const std::size_t take = scored < want ? scored : want;
    if (take == 0) {
        return {kept, 0}; // nothing to place: the engine's list as it is
    }
    const std::size_t fit = kViewDecalSafeCount > take ? kViewDecalSafeCount - take : 0;
    return {kept < fit ? kept : fit, take};
}

// The squared distance from `origin` to the centre of the decal box (mins[0..2], maxs[3..5]).
constexpr float boxDistance2(const std::array<float, 6>& box, const std::array<float, 3>& origin) {
    float d2 = 0.0f;
    for (std::size_t k = 0; k < 3; ++k) {
        const float c = 0.5f * (box[k] + box[k + 3]) - origin[k];
        d2 += c * c;
    }
    return d2;
}

// The record a scoring key names (its low 16 bits).
constexpr std::size_t keyRecord(std::uint64_t key) {
    return static_cast<std::size_t>(key & 0xFFFF);
}

// The index of `record` in the array of `count` records of `recordSize` bytes at `base`, or kNoRecord when it
// is not the start of one of them.
inline constexpr std::size_t kNoRecord = ~std::size_t{0};
inline std::size_t
recordIndex(const std::byte* record, const std::byte* base, std::size_t recordSize, std::size_t count) {
    const auto r = reinterpret_cast<std::uintptr_t>(record);
    const auto b = reinterpret_cast<std::uintptr_t>(base);
    if (r < b || recordSize == 0 || (r - b) % recordSize != 0 || (r - b) / recordSize >= count) {
        return kNoRecord;
    }
    return (r - b) / recordSize;
}

// Sets in `held` (bit i of word i / 64, cleared first) the records the engine's list names.
inline void markHeld(std::span<std::uint64_t> held,
                     std::span<std::byte* const> list,
                     const std::byte* base,
                     std::size_t recordSize) {
    std::fill(held.begin(), held.end(), std::uint64_t{0});
    for (const std::byte* record : list) {
        const std::size_t i = recordIndex(record, base, recordSize, held.size() * 64);
        if (i != kNoRecord) {
            held[i / 64] |= std::uint64_t{1} << (i % 64);
        }
    }
}

// The `chosen` indices whose bit in `held` is clear, in order (the engine's list already holds the others).
inline void withoutHeld(std::span<const std::uint16_t> chosen,
                        std::span<const std::uint64_t> held,
                        std::vector<std::uint16_t>& out) {
    out.clear();
    for (const std::uint16_t i : chosen) {
        if (i / 64 >= held.size() || ((held[i / 64] >> (i % 64)) & 1) == 0) {
            out.push_back(i);
        }
    }
}

// Moves to keys[0..take) the `take` keys whose records are nearest by `distance2(record)` (ties: the lower
// record), in no order; the rest follow in no order.
template <class Distance2>
void keepNearestKeys(std::span<std::uint64_t> keys, std::size_t take, Distance2&& distance2) {
    if (take >= keys.size()) {
        return;
    }
    struct Ranked {
        float distance;
        std::uint64_t key;
    };
    static thread_local std::vector<Ranked> ranked;
    ranked.clear();
    for (const std::uint64_t key : keys) {
        ranked.push_back({distance2(keyRecord(key)), key});
    }
    std::nth_element(ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(take), ranked.end(),
                     [](const Ranked& a, const Ranked& b) {
                         return a.distance < b.distance ||
                                (a.distance == b.distance && keyRecord(a.key) < keyRecord(b.key));
                     });
    for (std::size_t i = 0; i < ranked.size(); ++i) {
        keys[i] = ranked[i].key;
    }
}

// Writes `extras` (keys) after the first `place.keep` entries of a view's `list` and `keys` (the engine's
// arrays, both of their full capacity), each list entry the record its key names as the sorting job writes
// it, and returns the view's new count: keep + the extras written (at most place.take, never past the
// arrays).
inline std::size_t writeExtras(std::span<std::byte*> list,
                               std::span<std::uint64_t> keys,
                               Placement place,
                               std::span<const std::uint64_t> extras,
                               std::byte* base,
                               std::size_t recordSize) {
    const std::size_t capacity = std::min(list.size(), keys.size());
    const std::size_t keep = std::min(place.keep, capacity);
    const std::size_t n = std::min({place.take, extras.size(), capacity - keep});
    for (std::size_t i = 0; i < n; ++i) {
        keys[keep + i] = extras[i];
        list[keep + i] = base + keyRecord(extras[i]) * recordSize;
    }
    return keep + n;
}

// The indices of the decals below `count` that are active, inside and not set in `visible` (the decals the
// engine's list already holds): the `limit` nearest by `distance2(i)` (ties: the lower index), in no order.
template <class Active, class Inside, class Distance2>
std::vector<std::uint16_t> nearestDecals(std::span<const std::uint64_t> visible,
                                         std::size_t count,
                                         std::size_t limit,
                                         Active&& active,
                                         Inside&& inside,
                                         Distance2&& distance2) {
    struct Candidate {
        float distance;
        std::uint16_t index;
    };
    const std::size_t n = std::min({count, visible.size() * 64, std::size_t{0x10000}});
    // One list per thread, kept between calls: the gather runs for each eye every frame.
    static thread_local std::vector<Candidate> candidates;
    candidates.clear();
    for (std::size_t i = 0; i < n; ++i) {
        if (((visible[i / 64] >> (i % 64)) & 1) != 0 || !active(i) || !inside(i)) {
            continue;
        }
        candidates.push_back({distance2(i), static_cast<std::uint16_t>(i)});
    }
    const auto nearer = [](const Candidate& a, const Candidate& b) {
        return a.distance < b.distance || (a.distance == b.distance && a.index < b.index);
    };
    if (candidates.size() > limit) {
        std::nth_element(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(limit),
                         candidates.end(), nearer);
        candidates.resize(limit);
    }
    std::vector<std::uint16_t> indices;
    indices.reserve(candidates.size());
    for (const Candidate& c : candidates) {
        indices.push_back(c.index);
    }
    return indices;
}

} // namespace evr::stereo_seq
