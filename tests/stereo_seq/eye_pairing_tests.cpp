#include "stereo_seq/eye_pairing.hpp"

#include <doctest/doctest.h>

#include <cstdint>

using evr::stereo_seq::Eye;
using evr::stereo_seq::EyePairing;
using evr::stereo_seq::PairAction;
using evr::stereo_seq::PresentMatch;

namespace {

PresentMatch present(Eye eye, std::uint64_t tick, bool applied = true) {
    PresentMatch m;
    m.tagged = true;
    m.tag.eye = eye;
    m.tag.tick = tick;
    m.tag.viewApplied = applied;
    return m;
}

PresentMatch untagged() {
    return PresentMatch{};
}

} // namespace

TEST_CASE("pairing: left then right of one tick make a pair") {
    EyePairing p;
    const auto l = p.onPresent(present(Eye::Left, 4));
    CHECK(l.action == PairAction::StartPair);
    CHECK_FALSE(l.abandoned);
    CHECK(p.pending());
    CHECK(p.pendingTick() == 4);
    const auto r = p.onPresent(present(Eye::Right, 4));
    CHECK(r.action == PairAction::CompletePair);
    CHECK_FALSE(p.pending());
    CHECK(p.stats().pairsCompleted == 1);
    CHECK(p.stats().leftDropped == 0);
    CHECK(p.stats().rightDropped == 0);
}

TEST_CASE("pairing: untagged and mono presents are shown mono") {
    EyePairing p;
    CHECK(p.onPresent(untagged()).action == PairAction::ShowMono);
    CHECK(p.onPresent(present(Eye::Mono, 0, false)).action == PairAction::ShowMono);
    CHECK(p.stats().mono == 2);
}

TEST_CASE("pairing: a missing right half abandons the left one") {
    SUBCASE("the next tick's left comes") {
        EyePairing p;
        REQUIRE(p.onPresent(present(Eye::Left, 1)).action == PairAction::StartPair);
        const auto next = p.onPresent(present(Eye::Left, 2));
        CHECK(next.action == PairAction::StartPair);
        CHECK(next.abandoned);
        CHECK(p.pendingTick() == 2);
        CHECK(p.stats().leftDropped == 1);
        CHECK(p.onPresent(present(Eye::Right, 2)).action == PairAction::CompletePair);
    }
    SUBCASE("a mono frame comes") {
        EyePairing p;
        REQUIRE(p.onPresent(present(Eye::Left, 1)).action == PairAction::StartPair);
        const auto mono = p.onPresent(untagged());
        CHECK(mono.action == PairAction::ShowMono);
        CHECK(mono.abandoned);
        CHECK_FALSE(p.pending());
    }
}

TEST_CASE("pairing: a right half without its left is dropped") {
    EyePairing p;
    const auto r = p.onPresent(present(Eye::Right, 3));
    CHECK(r.action == PairAction::Drop);
    CHECK_FALSE(r.abandoned);
    CHECK(p.stats().rightDropped == 1);
}

TEST_CASE("pairing: halves of different ticks are never paired") {
    EyePairing p;
    REQUIRE(p.onPresent(present(Eye::Left, 5)).action == PairAction::StartPair);
    const auto r = p.onPresent(present(Eye::Right, 6));
    CHECK(r.action == PairAction::Drop);
    CHECK(r.abandoned);
    CHECK_FALSE(p.pending());
    CHECK(p.stats().leftDropped == 1);
    CHECK(p.stats().rightDropped == 1);
    CHECK(p.stats().pairsCompleted == 0);
}

TEST_CASE("pairing: out-of-order halves (right before left) are dropped, the next pair still forms") {
    EyePairing p;
    CHECK(p.onPresent(present(Eye::Right, 7)).action == PairAction::Drop);
    CHECK(p.onPresent(present(Eye::Left, 7)).action == PairAction::StartPair);
    // The left of tick 7 now waits for a right of tick 7 that already went; the next tick replaces it.
    CHECK(p.onPresent(present(Eye::Left, 8)).abandoned);
    CHECK(p.onPresent(present(Eye::Right, 8)).action == PairAction::CompletePair);
    CHECK(p.stats().pairsCompleted == 1);
    CHECK(p.stats().leftDropped == 1);
    CHECK(p.stats().rightDropped == 1);
}

TEST_CASE("pairing: halves the per-eye hook did not write are dropped") {
    SUBCASE("left") {
        EyePairing p;
        const auto l = p.onPresent(present(Eye::Left, 1, false));
        CHECK(l.action == PairAction::Drop);
        CHECK_FALSE(p.pending());
        CHECK(p.onPresent(present(Eye::Right, 1)).action == PairAction::Drop);
        CHECK(p.stats().withoutView == 1);
    }
    SUBCASE("right") {
        EyePairing p;
        REQUIRE(p.onPresent(present(Eye::Left, 1)).action == PairAction::StartPair);
        const auto r = p.onPresent(present(Eye::Right, 1, false));
        CHECK(r.action == PairAction::Drop);
        CHECK(r.abandoned);
        CHECK(p.stats().withoutView == 1);
    }
}

TEST_CASE("pairing: halves that could not be stored") {
    SUBCASE("left: no free slot") {
        EyePairing p;
        REQUIRE(p.onPresent(present(Eye::Left, 1)).action == PairAction::StartPair);
        p.leftNotStored();
        CHECK_FALSE(p.pending());
        CHECK(p.onPresent(present(Eye::Right, 1)).action == PairAction::Drop);
        CHECK(p.stats().notStored == 1);
    }
    SUBCASE("right: the copy failed") {
        EyePairing p;
        REQUIRE(p.onPresent(present(Eye::Left, 1)).action == PairAction::StartPair);
        REQUIRE(p.onPresent(present(Eye::Right, 1)).action == PairAction::CompletePair);
        p.rightNotStored();
        CHECK(p.stats().pairsCompleted == 0);
        CHECK(p.stats().notStored == 1);
        CHECK_FALSE(p.pending());
    }
}

TEST_CASE("pairing: a long stereo run completes every pair") {
    EyePairing p;
    for (std::uint64_t tick = 1; tick <= 100; ++tick) {
        REQUIRE(p.onPresent(present(Eye::Left, tick)).action == PairAction::StartPair);
        REQUIRE(p.onPresent(present(Eye::Right, tick)).action == PairAction::CompletePair);
    }
    CHECK(p.stats().pairsCompleted == 100);
    CHECK(p.stats().presents == 200);
    CHECK(p.stats().leftDropped == 0);
}
