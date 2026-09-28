#include "stereo_seq/keep_prev.hpp"

#include <doctest/doctest.h>

using evr::stereo_seq::Eye;
using evr::stereo_seq::keepPrevious;
using evr::stereo_seq::KeepPrevMode;
using evr::stereo_seq::keepPrevMode;
using evr::stereo_seq::PairClock;

TEST_CASE("the keep-previous setting is on unless turned off or counting") {
    CHECK(keepPrevMode("") == KeepPrevMode::On);
    CHECK(keepPrevMode("1") == KeepPrevMode::On);
    CHECK(keepPrevMode(" Off ") == KeepPrevMode::Off);
    CHECK(keepPrevMode("0") == KeepPrevMode::Off);
    CHECK(keepPrevMode("no") == KeepPrevMode::Off);
    CHECK(keepPrevMode("COUNT") == KeepPrevMode::Count);
}

TEST_CASE("eye R keeps the previous matrix only for what eye L committed in the same tick") {
    PairClock clock;
    // Tick 10: eye L commits entity A, then eye R renders.
    const std::uint32_t stampA = clock.leftStamp();
    const std::uint32_t pair10 = clock.rightPair(10);
    CHECK(keepPrevious(KeepPrevMode::On, Eye::Right, stampA, pair10));
    // Asked again within the same eye R render: the same pair.
    CHECK(clock.rightPair(10) == pair10);
    // An entity eye L never committed (a zeroed stamp) keeps the engine's behaviour.
    CHECK_FALSE(keepPrevious(KeepPrevMode::On, Eye::Right, 0, pair10));
    // Tick 11: eye L commits entity B only; A's stamp is a tick old.
    const std::uint32_t stampB = clock.leftStamp();
    CHECK(stampB != stampA);
    const std::uint32_t pair11 = clock.rightPair(11);
    CHECK(keepPrevious(KeepPrevMode::On, Eye::Right, stampB, pair11));
    CHECK_FALSE(keepPrevious(KeepPrevMode::On, Eye::Right, stampA, pair11));
}

TEST_CASE("eye L, counting and off never keep") {
    PairClock clock;
    const std::uint32_t stamp = clock.leftStamp();
    const std::uint32_t pair = clock.rightPair(1);
    CHECK_FALSE(keepPrevious(KeepPrevMode::On, Eye::Left, stamp, pair));
    CHECK_FALSE(keepPrevious(KeepPrevMode::Count, Eye::Right, stamp, pair));
    CHECK_FALSE(keepPrevious(KeepPrevMode::Off, Eye::Right, stamp, pair));
    CHECK_FALSE(keepPrevious(KeepPrevMode::On, Eye::Right, 0, 0));
}
