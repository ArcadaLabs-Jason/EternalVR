#include "vkcore/status_fields.hpp"
#include "vkcore/status_file.hpp"

#include <doctest/doctest.h>

using evr::vkcore::status::Fields;
using evr::vkcore::status::FlushGate;
using evr::vkcore::status::retried;
using evr::vkcore::status::SaveState;

TEST_CASE("status file: a move that fails while the launcher reads the file is tried again") {
    int moves = 0;
    int pauses = 0;
    // The reader lets go after the third try.
    CHECK(retried(10, [&] { return ++moves == 3; }, [&] { ++pauses; }));
    CHECK(moves == 3);
    CHECK(pauses == 2);
}

TEST_CASE("status file: the first move that works is the only one, with no pause") {
    int moves = 0;
    int pauses = 0;
    CHECK(retried(10, [&] { return ++moves > 0; }, [&] { ++pauses; }));
    CHECK(moves == 1);
    CHECK(pauses == 0);
}

TEST_CASE("status file: the tries are bounded, so a reader that never lets go costs only the pauses") {
    int moves = 0;
    int pauses = 0;
    CHECK_FALSE(retried(10, [&] { return ++moves < 0; }, [&] { ++pauses; }));
    CHECK(moves == 10);
    CHECK(pauses == 9);
}

TEST_CASE("status file: a start step keeps its first time; a headset key takes each new value") {
    Fields f;
    CHECK(f.set("first_headset_frame", "14.3", true));
    // The ring rebuilt after a size change: its first frame again, later.
    CHECK_FALSE(f.set("first_headset_frame", "45.5", true));
    CHECK(f.set("render", "1024x1104", false));
    CHECK_FALSE(f.set("render", "1024x1104", false));
    CHECK(f.set("render", "992x488", false));
    REQUIRE(f.items().size() == 2);
    CHECK(f.items()[0].first == "first_headset_frame");
    CHECK(f.items()[0].second == "14.3");
    CHECK(f.items()[1].second == "992x488");
}

TEST_CASE(
    "status file: a lost write is saved again by the next call, changed or not, and logged once each way") {
    SaveState s;
    CHECK_FALSE(s.due(false));
    CHECK(s.due(true));
    CHECK(s.saved(true) == SaveState::Outcome::Saved);
    // Every try failed while the launcher had the file open.
    CHECK(s.saved(false) == SaveState::Outcome::Lost);
    CHECK(s.dirty());
    CHECK(s.due(false));
    CHECK(s.saved(false) == SaveState::Outcome::StillLost);
    CHECK(s.saved(true) == SaveState::Outcome::Recovered);
    CHECK_FALSE(s.dirty());
    CHECK_FALSE(s.due(false));
    CHECK(s.saved(true) == SaveState::Outcome::Saved);
}

TEST_CASE("status file: the first flush after a loss saves at once, then at most once a second") {
    FlushGate gate(1000);
    gate.lost();
    // The first flush after the loss goes at once (a gate that never flushed would too: the next test is the
    // one that needs lost()); later ones wait out the interval.
    CHECK(gate.due(5000));
    CHECK_FALSE(gate.due(5001));
    CHECK_FALSE(gate.due(5999));
    CHECK(gate.due(6000));
    CHECK_FALSE(gate.due(6500));
    CHECK(gate.due(7200));
}

TEST_CASE("status file: a new loss right after a flush is flushed at once") {
    FlushGate gate(1000);
    CHECK(gate.due(100)); // the first one ever, at the clock's start, goes too
    CHECK_FALSE(gate.due(400));
    // The flush recovered the file; a later save is lost again 0.2 s after that flush.
    gate.lost();
    CHECK(gate.due(600));
    CHECK_FALSE(gate.due(1500));
}
