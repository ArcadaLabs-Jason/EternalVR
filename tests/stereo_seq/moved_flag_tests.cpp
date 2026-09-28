#include "stereo_seq/moved_flag.hpp"

#include <doctest/doctest.h>

using evr::stereo_seq::Eye;
using evr::stereo_seq::movedFlagMode;
using evr::stereo_seq::MovedFlagMode;
using evr::stereo_seq::movedFlagStatusFor;

TEST_CASE("moved flag: the switch") {
    CHECK(movedFlagMode("") == MovedFlagMode::On);
    CHECK(movedFlagMode("1") == MovedFlagMode::On);
    CHECK(movedFlagMode("0") == MovedFlagMode::Off);
    CHECK(movedFlagMode(" No ") == MovedFlagMode::Off);
    CHECK(movedFlagMode("Count") == MovedFlagMode::Count);
}

TEST_CASE("moved flag: eye R leaves the cleanup to the next eye L") {
    CHECK(movedFlagStatusFor(MovedFlagMode::On, Eye::Right, 1) == 0);
    // Eye L and mono clean up as the engine does.
    CHECK(movedFlagStatusFor(MovedFlagMode::On, Eye::Left, 1) == 1);
    CHECK(movedFlagStatusFor(MovedFlagMode::On, Eye::Mono, 1) == 1);
    // Other statuses (0 nothing to do, 2 a newer transform to commit) are left as they are.
    CHECK(movedFlagStatusFor(MovedFlagMode::On, Eye::Right, 0) == 0);
    CHECK(movedFlagStatusFor(MovedFlagMode::On, Eye::Right, 2) == 2);
    // Off and count never change anything.
    CHECK(movedFlagStatusFor(MovedFlagMode::Off, Eye::Right, 1) == 1);
    CHECK(movedFlagStatusFor(MovedFlagMode::Count, Eye::Right, 1) == 1);
}
