#pragma once

// Geometry caches' previous frame in eye R (docs/rig-findings/stereo-geomcache-motion.md).
//
// A geometry cache (idRenderModelGeomCache: the Alembic animations of swaying banners, hanging bodies, the
// damned in cages) interpolates its vertices and transforms on the GPU into one of two output slots per kind.
// Each commit flips both: the new frame goes to one slot, and the other one, written by the commit before, is
// the previous frame the motion vectors come from (used only while the update before was in the render just
// before). In mono that is one game frame of motion. Under Route S eye R commits the cache again in the same
// tick: it flips to the slot eye L's commit did not write, interpolates the same model time, and its previous
// frame is eye L's of the same tick. The cache has no motion in eye R and eye R's TAA smears it.
//
// For a cache eye L updated in the same tick at the same model time, eye R keeps both slots where eye L's
// commit put them: it writes what eye L wrote into the same slot and reads the same previous slot, which
// holds the tick before (eye L and eye R of that tick both wrote it). Eye R's update then takes eye L's
// verdict on that previous frame, not the engine's check, which depends on whether the render thread has run
// eye L's update yet. A cache eye R updates on its own, one at another model time than eye L's, one that does
// not animate, and every render outside Route S's two-render tick keep the engine's behaviour.

#include "stereo_seq/eye_tags.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace evr::stereo_seq {

enum class GeomCachePrevMode {
    Off,   // the engine's flips: geometry caches have no motion in eye R
    Count, // as Off, and the commits and updates are counted
    On,    // eye R keeps eye L's slots for caches eye L updated in the render just before
};

// ETERNALVR_STEREO_GEOMCACHE_PREV: "0"/"off"/"false"/"no" Off, "count" Count, anything else (or unset) On.
GeomCachePrevMode geomCachePrevMode(std::string_view value);

// The latest update of each cache by the engine's own chain (eye L, mono, alternate eyes), by the index of
// the cache's render entity: the cache, the render frame it was in (the engine's render frame counter, one
// per render), whether it had a valid previous frame, and the low 30 bits of its model time. Eye R's render
// is nested right after its eye L's, so eye L of the same tick is eye R's render frame - 1: a stamp from any
// other render (a mono one, an earlier tick, the map before a load) never matches. Indices past the table are
// never updated; an entity whose index another cache stamped answers for that cache only.
class GeomCacheStamps {
public:
    explicit GeomCacheStamps(std::size_t entries);

    std::size_t size() const { return size_; }

    void markLeft(std::size_t index,
                  std::uintptr_t cache,
                  std::uint32_t renderFrame,
                  bool valid,
                  std::uint64_t modelTime);

    struct Left {
        bool updated = false;  // eye L of this tick updated the cache
        bool sameTime = false; // and at this model time
        bool valid = false;    // its update had a valid previous frame
    };
    // What the render just before eye R's (`renderFrame`) did to `cache`, compared with eye R's model time.
    Left
    left(std::size_t index, std::uintptr_t cache, std::uint32_t renderFrame, std::uint64_t modelTime) const;

private:
    struct Entry {
        std::atomic<std::uint64_t> stamp{
            0}; // bit 63 written, bit 62 valid, bits 32-61 the time, the render frame
        std::atomic<std::uintptr_t> cache{0};
    };
    std::unique_ptr<Entry[]> entries_;
    std::size_t size_ = 0;
};

// Whether eye R's commit keeps both slots where they are: mode On, eye R, eye L updated the cache in the
// render just before at the same model time, and the cache animates (one that does not is pinned to its still
// slot by the commit, which keeping would undo).
bool keepGeomCacheSlots(GeomCachePrevMode mode, Eye eye, const GeomCacheStamps::Left& left, bool animates);

// The previous frame's validity eye R's update uses: eye L's for a cache whose slots it kept (the same pair
// of slots), else the engine's own check.
bool geomCachePreviousValid(bool kept, bool engineValid, bool leftValid);

} // namespace evr::stereo_seq
