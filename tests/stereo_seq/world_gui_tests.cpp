#include "stereo_seq/world_gui.hpp"

#include <doctest/doctest.h>

using evr::stereo_seq::classifyWorldGuiStamp;
using evr::stereo_seq::Eye;
using evr::stereo_seq::worldGuiFrameFor;
using evr::stereo_seq::worldGuiMode;
using evr::stereo_seq::WorldGuiMode;
using evr::stereo_seq::WorldGuiStamp;

TEST_CASE("world gui: the switch") {
    CHECK(worldGuiMode("") == WorldGuiMode::On);
    CHECK(worldGuiMode("1") == WorldGuiMode::On);
    CHECK(worldGuiMode("0") == WorldGuiMode::Off);
    CHECK(worldGuiMode(" Off ") == WorldGuiMode::Off);
    CHECK(worldGuiMode("false") == WorldGuiMode::Off);
    CHECK(worldGuiMode("COUNT") == WorldGuiMode::Count);
}

TEST_CASE("world gui: stamps") {
    CHECK(classifyWorldGuiStamp(40, 40) == WorldGuiStamp::Current);
    CHECK(classifyWorldGuiStamp(40, 41) == WorldGuiStamp::OneBehind);
    CHECK(classifyWorldGuiStamp(40, 42) == WorldGuiStamp::Older);
    CHECK(classifyWorldGuiStamp(41, 40) == WorldGuiStamp::Older);
    CHECK(classifyWorldGuiStamp(0xFFFFFFFFu, 0) == WorldGuiStamp::OneBehind);
}

TEST_CASE("world gui: eye R draws eye L's commit of the same tick") {
    // Eye L commits at 40 and draws; eye R renders at 41.
    CHECK(worldGuiFrameFor(WorldGuiMode::On, Eye::Left, 40, 40) == 40);
    CHECK(worldGuiFrameFor(WorldGuiMode::On, Eye::Right, 40, 41) == 40);
    // A GUI eye R committed itself stays as it is.
    CHECK(worldGuiFrameFor(WorldGuiMode::On, Eye::Right, 41, 41) == 41);
}

TEST_CASE("world gui: everything else keeps the engine's check") {
    // Older commits stay hidden in eye R (the game stopped updating them).
    CHECK(worldGuiFrameFor(WorldGuiMode::On, Eye::Right, 39, 41) == 41);
    // Eye L and mono renders never change.
    CHECK(worldGuiFrameFor(WorldGuiMode::On, Eye::Left, 40, 41) == 41);
    CHECK(worldGuiFrameFor(WorldGuiMode::On, Eye::Mono, 40, 41) == 41);
    // Off and count leave eye R as the engine has it.
    CHECK(worldGuiFrameFor(WorldGuiMode::Off, Eye::Right, 40, 41) == 41);
    CHECK(worldGuiFrameFor(WorldGuiMode::Count, Eye::Right, 40, 41) == 41);
}
