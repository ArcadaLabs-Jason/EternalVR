#include "stereo_seq/geomcache_prev.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>

using evr::stereo_seq::Eye;
using evr::stereo_seq::geomCachePreviousValid;
using evr::stereo_seq::geomCachePrevMode;
using evr::stereo_seq::GeomCachePrevMode;
using evr::stereo_seq::GeomCacheStamps;
using evr::stereo_seq::keepGeomCacheSlots;

namespace {

// One geometry cache as the engine's commit and update use its output slots (stereo-geomcache-motion.md
// section 1), with the hooks' rule in between. render() returns the motion the render's motion vectors get,
// in game ticks: the current slot's tick minus the previous slot's (0 when the previous frame is not valid).
struct Cache {
    static constexpr std::size_t kIndex = 2;
    GeomCachePrevMode mode = GeomCachePrevMode::On;
    GeomCacheStamps stamps{4};
    std::uintptr_t cache = 0x1000; // the cache's address
    std::uint32_t frame = 50;      // the render frame counter, one up at each render's start
    int slot = 0;
    std::int64_t written[2] = {-100, -100};    // the game tick each slot holds
    std::uint32_t lastUpdate = 0x80000000u;    // the render counter of the latest update (invalid)
    std::uint32_t counter = 1000;              // the render counter, one per render
    bool lateLeftUpdate = false;               // eye L's GPU update stamps lastUpdate after eye R's check
    std::optional<std::uint32_t> pendingStamp; // eye L's late stamp
    bool kept = false;                         // the latest render kept the slots
    bool engineValid = false;                  // the latest render's engine check

    int render(Eye eye, std::uint64_t tick, std::uint64_t modelTime, bool animates = true) {
        ++frame;
        GeomCacheStamps::Left left;
        if (eye == Eye::Right) {
            left = stamps.left(kIndex, cache, frame, modelTime);
        }
        kept = keepGeomCacheSlots(mode, eye, left, animates);
        if (!kept) {
            slot ^= 1; // the commit's flip
        }
        engineValid = lastUpdate == counter - 1 && animates;
        if (pendingStamp) {
            lastUpdate = *pendingStamp;
            pendingStamp.reset();
        }
        const bool valid =
            eye == Eye::Right ? geomCachePreviousValid(kept, engineValid, left.valid) : engineValid;
        if (eye != Eye::Right) {
            stamps.markLeft(kIndex, cache, frame, engineValid, modelTime);
        }
        const int previous = valid ? slot ^ 1 : slot;
        written[slot] = static_cast<std::int64_t>(tick);
        if (eye == Eye::Left && lateLeftUpdate) {
            pendingStamp = counter;
        } else {
            lastUpdate = counter;
        }
        ++counter;
        return static_cast<int>(written[slot] - written[previous]);
    }
};

} // namespace

TEST_CASE("geometry cache previous frame: the switch") {
    CHECK(geomCachePrevMode("") == GeomCachePrevMode::On);
    CHECK(geomCachePrevMode("1") == GeomCachePrevMode::On);
    CHECK(geomCachePrevMode("0") == GeomCachePrevMode::Off);
    CHECK(geomCachePrevMode(" Off ") == GeomCachePrevMode::Off);
    CHECK(geomCachePrevMode("no") == GeomCachePrevMode::Off);
    CHECK(geomCachePrevMode("COUNT") == GeomCachePrevMode::Count);
}

TEST_CASE("geometry cache stamps: eye L's update in the render before, its validity and its model time") {
    constexpr std::uintptr_t kCache = 0x5000;
    GeomCacheStamps stamps(8);
    CHECK(stamps.size() == 8);
    // Never updated: nothing matches, whatever the render frame (0 included).
    CHECK_FALSE(stamps.left(3, kCache, 1, 0).updated);
    CHECK_FALSE(stamps.left(3, 0, 0, 0).updated);
    stamps.markLeft(3, kCache, 40, true, 0x123456789ull);
    const GeomCacheStamps::Left left = stamps.left(3, kCache, 41, 0x123456789ull);
    CHECK(left.updated);
    CHECK(left.sameTime);
    CHECK(left.valid);
    CHECK_FALSE(stamps.left(3, kCache, 41, 0x12345678Aull).sameTime);
    // Only the render just before: not the same render, not two renders back.
    CHECK_FALSE(stamps.left(3, kCache, 40, 0x123456789ull).updated);
    CHECK_FALSE(stamps.left(3, kCache, 42, 0x123456789ull).updated);
    stamps.markLeft(3, kCache, 0xFFFFFFFFu, false, 7); // the render frame counter wraps
    const GeomCacheStamps::Left invalid = stamps.left(3, kCache, 0, 7);
    CHECK(invalid.updated);
    CHECK_FALSE(invalid.valid);
    // Past the table: ignored.
    stamps.markLeft(8, kCache, 50, true, 7);
    CHECK_FALSE(stamps.left(8, kCache, 51, 7).updated);
}

TEST_CASE("geometry cache stamps: another cache on the same entity index answers for itself only") {
    GeomCacheStamps stamps(8);
    stamps.markLeft(5, 0x5000, 10, true, 3);
    CHECK_FALSE(stamps.left(5, 0x6000, 11, 3).updated);
    CHECK(stamps.left(5, 0x5000, 11, 3).updated);
    // The second cache's update replaces the first's, which then keeps the engine's behaviour.
    stamps.markLeft(5, 0x6000, 10, true, 3);
    CHECK_FALSE(stamps.left(5, 0x5000, 11, 3).updated);
    CHECK(stamps.left(5, 0x6000, 11, 3).updated);
}

TEST_CASE("geometry cache stamps: a stamp from mono renders or before a load never matches") {
    // Loading and menus render mono through the engine's own chain and stamp what they update; a cache of the
    // next map can take the same entity index, at the same model time (both 0 at playback start).
    Cache cache;
    for (std::uint64_t tick = 1; tick <= 20; ++tick) {
        cache.render(Eye::Mono, tick, 0);
    }
    // The first stereo tick: eye L does not update the cache (it is not on screen yet), eye R does.
    ++cache.frame; // eye L's render
    cache.render(Eye::Right, 21, 0);
    CHECK_FALSE(cache.kept);
    // Eye R updating a cache in a tick without its eye L's update, after a tick whose eye R updated nothing.
    Cache skipped;
    skipped.render(Eye::Left, 1, 1);
    ++skipped.frame; // eye R of tick 1 updates no cache
    ++skipped.frame; // eye L of tick 2 does not update this one
    skipped.render(Eye::Right, 2, 1);
    CHECK_FALSE(skipped.kept);
}

TEST_CASE("geometry cache slots are kept only by eye R, for what eye L updated, at its time, animating") {
    GeomCacheStamps::Left left;
    left.updated = true;
    left.sameTime = true;
    CHECK(keepGeomCacheSlots(GeomCachePrevMode::On, Eye::Right, left, true));
    CHECK_FALSE(keepGeomCacheSlots(GeomCachePrevMode::On, Eye::Left, left, true));
    CHECK_FALSE(keepGeomCacheSlots(GeomCachePrevMode::On, Eye::Mono, left, true));
    CHECK_FALSE(keepGeomCacheSlots(GeomCachePrevMode::Count, Eye::Right, left, true));
    CHECK_FALSE(keepGeomCacheSlots(GeomCachePrevMode::Off, Eye::Right, left, true));
    CHECK_FALSE(keepGeomCacheSlots(GeomCachePrevMode::On, Eye::Right, left, false));
    left.sameTime = false;
    CHECK_FALSE(keepGeomCacheSlots(GeomCachePrevMode::On, Eye::Right, left, true));
    left.updated = false;
    left.sameTime = true;
    CHECK_FALSE(keepGeomCacheSlots(GeomCachePrevMode::On, Eye::Right, left, true));
    // Validity: eye L's for kept slots, the engine's otherwise.
    CHECK(geomCachePreviousValid(true, false, true));
    CHECK_FALSE(geomCachePreviousValid(true, true, false));
    CHECK(geomCachePreviousValid(false, true, false));
    CHECK_FALSE(geomCachePreviousValid(false, false, true));
}

TEST_CASE("without the rule eye R's motion is zero; with it both eyes move one tick") {
    for (const GeomCachePrevMode mode : {GeomCachePrevMode::Off, GeomCachePrevMode::Count}) {
        Cache cache;
        cache.mode = mode;
        for (std::uint64_t tick = 1; tick <= 6; ++tick) {
            const int left = cache.render(Eye::Left, tick, tick * 10);
            const int right = cache.render(Eye::Right, tick, tick * 10);
            if (tick > 1) {
                CHECK(left == 1);
            }
            CHECK(right == 0);
        }
    }
    Cache cache;
    for (std::uint64_t tick = 1; tick <= 6; ++tick) {
        const int left = cache.render(Eye::Left, tick, tick * 10);
        const int right = cache.render(Eye::Right, tick, tick * 10);
        CHECK(cache.kept);
        if (tick > 1) {
            CHECK(left == 1);
            CHECK(right == 1);
        }
    }
}

TEST_CASE("eye R moves one tick even when eye L's update has not stamped the cache by eye R's check") {
    Cache off;
    off.mode = GeomCachePrevMode::Off;
    off.lateLeftUpdate = true;
    Cache on;
    on.lateLeftUpdate = true;
    for (std::uint64_t tick = 1; tick <= 6; ++tick) {
        const int offLeft = off.render(Eye::Left, tick, tick);
        CHECK(off.render(Eye::Right, tick, tick) == 0);
        const int onLeft = on.render(Eye::Left, tick, tick);
        const int onRight = on.render(Eye::Right, tick, tick);
        CHECK_FALSE(on.engineValid); // the engine's own check fails in eye R
        if (tick > 1) {
            CHECK(offLeft == 1);
            CHECK(onLeft == 1);
            CHECK(onRight == 1);
        }
    }
}

TEST_CASE("the engine's behaviour for eye R alone, another model time, a still cache and mono") {
    SUBCASE("eye R updates a cache eye L did not") {
        Cache cache;
        cache.render(Eye::Left, 1, 1);
        cache.render(Eye::Right, 1, 1);
        // Tick 2: only eye R updates it (eye L's stamp is a pair old).
        cache.render(Eye::Right, 2, 2);
        CHECK_FALSE(cache.kept);
    }
    SUBCASE("eye R's model time is not eye L's") {
        Cache cache;
        cache.render(Eye::Left, 1, 10);
        cache.render(Eye::Right, 1, 11);
        CHECK_FALSE(cache.kept);
    }
    SUBCASE("a cache that does not animate") {
        Cache cache;
        cache.render(Eye::Left, 1, 10, false);
        cache.render(Eye::Right, 1, 10, false);
        CHECK_FALSE(cache.kept);
    }
    SUBCASE("mono, alternate eyes: every render is the engine's own chain") {
        Cache cache;
        for (std::uint64_t tick = 1; tick <= 6; ++tick) {
            const int motion = cache.render(Eye::Left, tick, tick);
            CHECK_FALSE(cache.kept);
            if (tick > 1) {
                CHECK(motion == 1);
            }
        }
    }
}

TEST_CASE("the first stereo tick after mono and a tick without eye R") {
    Cache cache;
    cache.render(Eye::Mono, 1, 1);
    cache.render(Eye::Mono, 2, 2);
    CHECK(cache.render(Eye::Left, 3, 3) == 1);
    CHECK(cache.render(Eye::Right, 3, 3) == 1);
    // Tick 4 renders eye L only (eye R skipped): tick 5 is a pair again.
    CHECK(cache.render(Eye::Left, 4, 4) == 1);
    CHECK(cache.render(Eye::Left, 5, 5) == 1);
    CHECK(cache.render(Eye::Right, 5, 5) == 1);
    CHECK(cache.kept);
}
