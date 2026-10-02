#include "gpu_timing/present_stall.hpp"

#include <doctest/doctest.h>

#include <cstdint>

using evr::gpu_timing::kInPlayTickMs;
using evr::gpu_timing::kStallGapMs;
using evr::gpu_timing::kStallLinesLogged;
using evr::gpu_timing::PresentGap;
using evr::gpu_timing::StallGate;
using evr::gpu_timing::stallSaveNote;
using evr::gpu_timing::StallVerdict;
using evr::gpu_timing::summarySaveNote;

namespace {

PresentGap inPlay(double gapMs) {
    PresentGap g;
    g.gapMs = gapMs;
    g.ticksKnown = true;
    g.tickAgeAtStartMs = 5.0;
    g.tickDuring = true;
    return g;
}

} // namespace

TEST_CASE("present stall: a gap up to the threshold is no stall") {
    StallGate gate;
    CHECK(gate.onGap(inPlay(11.1)) == StallVerdict::None);
    CHECK(gate.onGap(inPlay(kStallGapMs)) == StallVerdict::None);
    CHECK(gate.onGap(inPlay(kStallGapMs + 0.1)) == StallVerdict::Log);
    CHECK(gate.counters().inPlay == 1);
}

TEST_CASE("present stall: loading screens and menus are counted, not logged") {
    StallGate gate;
    PresentGap loadStart = inPlay(1500.0);
    loadStart.tickDuring = false; // the game stopped ticking: a load began (or a menu opened)
    CHECK(gate.onGap(loadStart) == StallVerdict::OutsidePlay);
    PresentGap loadEnd = inPlay(900.0);
    loadEnd.tickAgeAtStartMs = 12000.0; // the first tick after a load: the gap began on the loading screen
    CHECK(gate.onGap(loadEnd) == StallVerdict::OutsidePlay);
    PresentGap noTickYet = inPlay(900.0);
    noTickYet.tickAgeAtStartMs = -1.0; // the title screen before the first map
    CHECK(gate.onGap(noTickYet) == StallVerdict::OutsidePlay);
    PresentGap recent = inPlay(900.0);
    recent.tickAgeAtStartMs = kInPlayTickMs;
    CHECK(gate.onGap(recent) == StallVerdict::Log);
    CHECK(gate.counters().outsidePlay == 3);
    CHECK(gate.counters().inPlay == 1);
}

TEST_CASE("present stall: without the game's ticks every stall is logged") {
    StallGate gate;
    PresentGap g;
    g.gapMs = 400.0; // cinema mode: no camera hook, the phase is unknown
    CHECK(gate.onGap(g) == StallVerdict::Log);
    CHECK(gate.counters().outsidePlay == 0);
}

TEST_CASE("present stall: lines stop at the limit, the count goes on") {
    StallGate gate;
    for (std::uint32_t i = 1; i < kStallLinesLogged; ++i) {
        CHECK(gate.onGap(inPlay(60.0)) == StallVerdict::Log);
    }
    CHECK(gate.onGap(inPlay(1043.0)) == StallVerdict::LastLog);
    CHECK(gate.onGap(inPlay(70.0)) == StallVerdict::Count);
    CHECK(gate.onGap(inPlay(80.0)) == StallVerdict::Count);
    CHECK(gate.counters().logged == kStallLinesLogged);
    CHECK(gate.counters().inPlay == kStallLinesLogged + 2);
    CHECK(gate.counters().longestMs == doctest::Approx(1043.0));
}

TEST_CASE("present stall: a checkpoint save in the gap is named in the line") {
    CHECK(stallSaveNote(0).empty());
    CHECK(stallSaveNote(1) == "; the game saved a checkpoint in the gap");
    CHECK(stallSaveNote(2) == "; the game saved 2 checkpoints in the gap");
}

TEST_CASE("present stall: the 10 s summary counts the checkpoint saves") {
    CHECK(summarySaveNote(0).empty());
    CHECK(summarySaveNote(1) == "; the game saved a checkpoint");
    CHECK(summarySaveNote(3) == "; the game saved 3 checkpoints");
}
