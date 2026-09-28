#include "features/input/cutscene_skip.hpp"

#include <doctest/doctest.h>

using evr::input::CutsceneSkip;

TEST_CASE("cutscene skip: nothing outside a cutscene") {
    CutsceneSkip skip;
    CHECK_FALSE(skip.update(false, true).keyDown);
    CHECK_FALSE(skip.update(false, false).keyDown);
}

TEST_CASE("cutscene skip: dash pressed in a cutscene holds the key until it is let go") {
    CutsceneSkip skip;
    CHECK_FALSE(skip.update(true, false).keyDown);
    auto out = skip.update(true, true);
    CHECK(out.keyDown);
    CHECK(out.firstHold);
    out = skip.update(true, true);
    CHECK(out.keyDown);
    CHECK_FALSE(out.firstHold);
    CHECK_FALSE(skip.update(true, false).keyDown);
    // A second hold in the same cutscene works, logged once.
    out = skip.update(true, true);
    CHECK(out.keyDown);
    CHECK_FALSE(out.firstHold);
}

TEST_CASE("cutscene skip: the key goes up when the cutscene ends") {
    CutsceneSkip skip;
    skip.update(true, false);
    CHECK(skip.update(true, true).keyDown);
    CHECK_FALSE(skip.update(false, true).keyDown);
    CHECK_FALSE(skip.keyDown());
}

TEST_CASE("cutscene skip: a dash held into a cutscene does not skip it") {
    CutsceneSkip skip;
    skip.update(false, true);
    CHECK_FALSE(skip.update(true, true).keyDown);
    CHECK_FALSE(skip.update(true, true).keyDown);
    CHECK_FALSE(skip.update(true, false).keyDown);
    CHECK(skip.update(true, true).keyDown);
}

TEST_CASE("cutscene skip: the next cutscene logs its first hold again") {
    CutsceneSkip skip;
    skip.update(true, false);
    CHECK(skip.update(true, true).firstHold);
    skip.update(false, false);
    skip.update(true, false);
    CHECK(skip.update(true, true).firstHold);
}
