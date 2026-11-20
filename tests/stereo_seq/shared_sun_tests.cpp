#include "stereo_seq/shared_sun.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>

using evr::stereo_seq::Eye;
using evr::stereo_seq::isSunWorldSlice;
using evr::stereo_seq::sharedSunKeeps;
using evr::stereo_seq::SharedSunMode;
using evr::stereo_seq::sharedSunMode;
using evr::stereo_seq::sharedSunModeName;
using evr::stereo_seq::sharedSunSkipsListing;
using evr::stereo_seq::sunCascade;
using evr::stereo_seq::SunCheck;
using evr::stereo_seq::SunTickStamps;

namespace {

SunCheck check(Eye eye,
               std::uint32_t counter,
               bool leftThisTick = true,
               std::uint32_t slice = 0,
               std::int32_t type = 2,
               std::uint64_t tick = 10) {
    SunCheck c;
    c.eye = eye;
    c.tick = tick;
    c.lightType = type;
    c.slice = slice;
    c.counter = counter;
    c.listedByLeftThisTick = leftThisTick;
    return c;
}

constexpr std::uintptr_t kSun = 0x7FF612340000;

} // namespace

TEST_CASE("ETERNALVR_SHARED_SUN values") {
    bool known = false;
    CHECK(sharedSunMode("", &known) == SharedSunMode::Off);
    CHECK(known);
    for (const char* off : {"0", "off", "FALSE", " no "}) {
        CHECK(sharedSunMode(off, &known) == SharedSunMode::Off);
        CHECK(known);
    }
    CHECK(sharedSunMode("count", &known) == SharedSunMode::Count);
    CHECK(sharedSunMode("Count") == SharedSunMode::Count);
    for (const char* on : {"1", "on", "shared", " ON", "true", "Yes"}) {
        CHECK(sharedSunMode(on, &known) == SharedSunMode::Shared);
        CHECK(known);
    }
    CHECK(sharedSunMode("Ceiling", &known) == SharedSunMode::Ceiling);
    CHECK(known);
    CHECK(sharedSunMode("all", &known) == SharedSunMode::Off);
    CHECK_FALSE(known);
    CHECK(std::string(sharedSunModeName(SharedSunMode::Shared)) == "shared");
    CHECK(std::string(sharedSunModeName(SharedSunMode::Ceiling)) == "ceiling");
    CHECK(std::string(sharedSunModeName(SharedSunMode::Count)) == "count");
    CHECK(std::string(sharedSunModeName(SharedSunMode::Off)) == "off");
}

TEST_CASE("sun world slices and their cascades") {
    // Light types: 0 and 4 point, 1 spot, 2 the sun.
    for (std::uint32_t slice = 0; slice < 8; slice += 2) {
        CHECK(isSunWorldSlice(2, slice));
        CHECK_FALSE(isSunWorldSlice(2, slice + 1)); // the player's
        CHECK_FALSE(isSunWorldSlice(1, slice));
        CHECK_FALSE(isSunWorldSlice(0, slice));
        CHECK_FALSE(isSunWorldSlice(4, slice));
        CHECK(sunCascade(slice) == slice / 2);
        CHECK(sunCascade(slice + 1) == slice / 2);
    }
    // Past the four cascades: not a sun world slice.
    CHECK_FALSE(isSunWorldSlice(2, 8));
    CHECK_FALSE(isSunWorldSlice(2, 10));
    CHECK(sunCascade(8) == 3);
    CHECK(sunCascade(41) == 3);
}

TEST_CASE("shared: eye R keeps only a sun cascade eye L listed in the same tick, its counter at 1") {
    const SharedSunMode m = SharedSunMode::Shared;
    for (std::uint32_t slice = 0; slice < 8; slice += 2) {
        CHECK(sharedSunKeeps(m, check(Eye::Right, 1, true, slice)));
    }
    // Eye L did not list it in this tick: a counter left at 0 by an allocation, a map load, a light that left
    // eye L's list or an eye L render that did not happen reads 1 again, and is rendered by eye R.
    CHECK_FALSE(sharedSunKeeps(m, check(Eye::Right, 1, false)));
    // Counter 2 or more: eye L's render has not reset it since eye R's last check.
    CHECK_FALSE(sharedSunKeeps(m, check(Eye::Right, 2)));
    CHECK_FALSE(sharedSunKeeps(m, check(Eye::Right, 7)));
    CHECK_FALSE(sharedSunKeeps(m, check(Eye::Right, 0)));
    CHECK_FALSE(sharedSunKeeps(m, check(Eye::Right, 1, true, 1)));    // the player's slice: placed per render
    CHECK_FALSE(sharedSunKeeps(m, check(Eye::Right, 1, true, 8)));    // past the cascades
    CHECK_FALSE(sharedSunKeeps(m, check(Eye::Right, 1, true, 0, 1))); // a spot light
    CHECK_FALSE(sharedSunKeeps(m, check(Eye::Right, 1, true, 0, 0))); // a point light
    CHECK_FALSE(sharedSunKeeps(m, check(Eye::Left, 1)));
    CHECK_FALSE(sharedSunKeeps(m, check(Eye::Mono, 1)));
    CHECK_FALSE(sharedSunKeeps(m, check(Eye::Right, 1, true, 0, 2, 0))); // unknown tick
}

TEST_CASE("shared: staleness is bounded to the tick, whatever happens to the counter") {
    // A tick loop over one cascade: eye L lists it in some ticks and not in others (it left eye L's list, eye
    // L's render was skipped); the counter is whatever the entry holds. Eye R may keep only in ticks eye L
    // listed it.
    SunTickStamps stamps;
    std::uint32_t counter = 0;
    for (std::uint64_t tick = 1; tick <= 40; ++tick) {
        const bool leftLists = (tick % 5) != 0 && (tick % 7) != 0;
        if (leftLists) {
            stamps.stamp(kSun, 0, tick);
            counter = 0; // eye L's render
        }
        ++counter; // eye R's check raises it
        const bool kept = sharedSunKeeps(
            SharedSunMode::Shared, check(Eye::Right, counter, stamps.stamped(kSun, 0, tick), 0, 2, tick));
        CHECK(kept == leftLists);
        if (kept) {
            counter = 0; // as eye R's render would have set it
        }
    }
}

TEST_CASE("the tick stamps of eye L's sun slices") {
    SunTickStamps stamps;
    CHECK_FALSE(stamps.stamped(kSun, 0, 5));
    stamps.stamp(kSun, 0, 5);
    CHECK(stamps.stamped(kSun, 0, 5));
    CHECK_FALSE(stamps.stamped(kSun, 0, 6));     // another tick
    CHECK_FALSE(stamps.stamped(kSun, 2, 5));     // another cascade
    CHECK_FALSE(stamps.stamped(kSun + 8, 0, 5)); // another light
    stamps.stamp(kSun, 0, 0);                    // an unknown tick is not stamped
    CHECK_FALSE(stamps.stamped(kSun, 0, 0));
    stamps.stamp(kSun, 2, 9);
    CHECK(stamps.stamped(kSun, 2, 9));
    // A stamp a tick behind the highest (a worker still listing it) keeps the others' stamps.
    stamps.stamp(kSun, 4, 8);
    CHECK(stamps.stamped(kSun, 2, 9));
    CHECK(stamps.stamped(kSun, 4, 8));
    stamps.clear();
    stamps.stamp(kSun, 0, 100);
    stamps.stamp(kSun, 2, 99);
    CHECK(stamps.stamped(kSun, 0, 100));
    CHECK(stamps.stamped(kSun, 2, 99));
    // The game frame count started again (a map load): the table is emptied.
    stamps.stamp(kSun, 4, 3);
    CHECK_FALSE(stamps.stamped(kSun, 0, 100));
    CHECK_FALSE(stamps.stamped(kSun, 2, 99));
    CHECK(stamps.stamped(kSun, 4, 3));
    // A gap of exactly 16 is still out of order, not a restart.
    stamps.stamp(kSun, 0, 19);
    CHECK(stamps.stamped(kSun, 4, 3));
    stamps.stamp(kSun, 6, 3);
    CHECK(stamps.stamped(kSun, 0, 19));
    stamps.stamp(kSun, 6, 2);
    CHECK_FALSE(stamps.stamped(kSun, 0, 19));
    stamps.clear();
    CHECK_FALSE(stamps.stamped(kSun, 4, 3));
    stamps.stamp(kSun, 4, 3);
    CHECK(stamps.stamped(kSun, 4, 3));
}

TEST_CASE("a slot taken by another slice reads as not stamped") {
    // Two keys in the same of the 256 slots: the later stamp overwrites the earlier, which then is not kept.
    SunTickStamps stamps;
    const std::uintptr_t base = kSun;
    const std::uint32_t slice = 0;
    // Find another light whose slot (hash % 256) equals base's, by the stamp itself: stamp candidates until
    // the first is no longer stamped.
    stamps.stamp(base, slice, 7);
    REQUIRE(stamps.stamped(base, slice, 7));
    std::uintptr_t other = 0;
    for (std::uintptr_t i = 1; i < 100000; ++i) {
        stamps.stamp(base + i * 8, slice, 7);
        if (!stamps.stamped(base, slice, 7)) {
            other = base + i * 8;
            break;
        }
    }
    REQUIRE(other != 0);
    CHECK(stamps.stamped(other, slice, 7));
    CHECK_FALSE(stamps.stamped(base, slice, 7)); // overwritten: eye R renders it as the engine would
    // The first one's stamp again overwrites the second.
    stamps.stamp(base, slice, 7);
    CHECK(stamps.stamped(base, slice, 7));
    CHECK_FALSE(stamps.stamped(other, slice, 7));
}

TEST_CASE("ceiling keeps every eye R sun world slice; count and off keep none") {
    CHECK(sharedSunKeeps(SharedSunMode::Ceiling, check(Eye::Right, 1)));
    CHECK(sharedSunKeeps(SharedSunMode::Ceiling, check(Eye::Right, 3, false, 6)));
    CHECK_FALSE(sharedSunKeeps(SharedSunMode::Ceiling, check(Eye::Right, 1, true, 1)));
    CHECK_FALSE(sharedSunKeeps(SharedSunMode::Ceiling, check(Eye::Left, 1)));
    for (const SharedSunMode m : {SharedSunMode::Count, SharedSunMode::Off}) {
        CHECK_FALSE(sharedSunKeeps(m, check(Eye::Right, 1)));
        CHECK_FALSE(sharedSunSkipsListing(m, Eye::Right, 10, 2, 0));
    }
    CHECK(sharedSunSkipsListing(SharedSunMode::Ceiling, Eye::Right, 10, 2, 0));
    CHECK_FALSE(sharedSunSkipsListing(SharedSunMode::Ceiling, Eye::Right, 10, 2, 1));
    CHECK_FALSE(sharedSunSkipsListing(SharedSunMode::Ceiling, Eye::Left, 10, 2, 0));
    CHECK_FALSE(sharedSunSkipsListing(SharedSunMode::Ceiling, Eye::Right, 0, 2, 0));
    CHECK_FALSE(sharedSunSkipsListing(SharedSunMode::Shared, Eye::Right, 10, 2, 0));
}
