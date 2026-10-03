#include "stereo_seq/ngx_eye.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>

using evr::stereo_seq::Eye;
using evr::stereo_seq::NgxOutputBook;
using evr::stereo_seq::NgxResetBook;
using evr::stereo_seq::pickNgxEye;
using evr::stereo_seq::RenderTag;

namespace {

RenderTag tagOf(Eye eye, std::uint64_t tick, std::uint32_t backendFrame) {
    RenderTag t;
    t.eye = eye;
    t.tick = tick;
    t.backendFrame = backendFrame;
    return t;
}

} // namespace

TEST_CASE("NGX output book: an image finds the tag it was picked with last") {
    NgxOutputBook book;
    CHECK_FALSE(book.find(0x10).has_value());
    book.note(0x10, tagOf(Eye::Left, 5, 100));
    book.note(0x20, tagOf(Eye::Right, 5, 101));
    REQUIRE(book.find(0x10).has_value());
    CHECK(book.find(0x10)->eye == Eye::Left);
    CHECK(book.find(0x20)->eye == Eye::Right);
    CHECK(book.find(0x20)->tick == 5);
    book.note(0x10, tagOf(Eye::Left, 7, 104)); // the same image two ticks later
    CHECK(book.find(0x10)->tick == 7);
    CHECK_FALSE(book.find(0x30).has_value());
}

TEST_CASE("NGX output book: nothing is noted or found for image 0") {
    NgxOutputBook book;
    book.note(0, tagOf(Eye::Right, 1, 1));
    CHECK_FALSE(book.find(0).has_value());
}

TEST_CASE("NGX output book: the oldest image is forgotten when a new one does not fit") {
    NgxOutputBook book;
    for (std::uint64_t i = 1; i <= NgxOutputBook::kCapacity; ++i) {
        book.note(i, tagOf(Eye::Left, i, static_cast<std::uint32_t>(i)));
    }
    book.note(1, tagOf(Eye::Left, 50, 50)); // image 1 is the newest again
    book.note(100, tagOf(Eye::Right, 51, 51));
    CHECK(book.find(1).has_value());
    CHECK_FALSE(book.find(2).has_value()); // the oldest now
    CHECK(book.find(100)->eye == Eye::Right);
    for (std::uint64_t i = 3; i <= NgxOutputBook::kCapacity; ++i) {
        CHECK(book.find(i).has_value());
    }
}

TEST_CASE("NGX eye pick: the own tag goes, and agrees with the tag in flight before the swap") {
    const auto pick = pickNgxEye(tagOf(Eye::Right, 9, 200), tagOf(Eye::Right, 9, 200));
    REQUIRE(pick.tag.has_value());
    CHECK(pick.tag->eye == Eye::Right);
    CHECK(pick.own);
    CHECK_FALSE(pick.fallback);
    CHECK_FALSE(pick.inFlightDiffers);
}

TEST_CASE("NGX eye pick: after the swap the tag in flight names the next render; the own tag still goes") {
    // Eye R of tick 9 evaluates while the tag in flight is already eye L of tick 10.
    const auto right = pickNgxEye(tagOf(Eye::Right, 9, 200), tagOf(Eye::Left, 10, 201));
    REQUIRE(right.tag.has_value());
    CHECK(right.tag->eye == Eye::Right);
    CHECK(right.tag->tick == 9);
    CHECK(right.own);
    CHECK(right.inFlightDiffers);
    // Eye L of tick 10 while eye R of tick 10 is in flight.
    const auto left = pickNgxEye(tagOf(Eye::Left, 10, 201), tagOf(Eye::Right, 10, 202));
    CHECK(left.tag->eye == Eye::Left);
    CHECK(left.inFlightDiffers);
    // Eye L, then a mono render in flight: mono counts as eye L.
    CHECK_FALSE(pickNgxEye(tagOf(Eye::Left, 10, 201), tagOf(Eye::Mono, 0, 202)).inFlightDiffers);
    // Eye R, then nothing tagged in flight (the next frame's tag not queued yet).
    const auto alone = pickNgxEye(tagOf(Eye::Right, 9, 200), std::nullopt);
    CHECK(alone.own);
    CHECK(alone.tag->eye == Eye::Right);
    CHECK(alone.inFlightDiffers);
}

TEST_CASE("NGX eye pick: a stale own tag gives way to the tag in flight") {
    // An image last noted many presents ago (a render whose pick was not noted).
    const auto stale = pickNgxEye(tagOf(Eye::Left, 3, 150), tagOf(Eye::Right, 9, 200));
    CHECK_FALSE(stale.own);
    CHECK(stale.fallback);
    CHECK(stale.tag->eye == Eye::Right);
    // An own tag ahead of the tag in flight cannot be this render's either.
    CHECK_FALSE(pickNgxEye(tagOf(Eye::Right, 9, 201), tagOf(Eye::Left, 9, 200)).own);
}

TEST_CASE("NGX eye pick: without an own tag the tag in flight goes; mono and untagged are no fallback") {
    const auto inFlight = pickNgxEye(std::nullopt, tagOf(Eye::Left, 4, 10));
    CHECK_FALSE(inFlight.own);
    CHECK(inFlight.fallback);
    CHECK(inFlight.tag->eye == Eye::Left);
    const auto mono = pickNgxEye(std::nullopt, tagOf(Eye::Mono, 0, 10));
    CHECK_FALSE(mono.fallback);
    const auto none = pickNgxEye(std::nullopt, std::nullopt);
    CHECK_FALSE(none.tag.has_value());
    CHECK_FALSE(none.fallback);
    CHECK_FALSE(none.inFlightDiffers);
}

TEST_CASE("NGX eye pick: the backend counter wraps") {
    const auto pick = pickNgxEye(tagOf(Eye::Right, 9, 0xFFFFFFFFu), tagOf(Eye::Left, 10, 0));
    CHECK(pick.own);
    CHECK(pick.inFlightDiffers);
}

TEST_CASE("NGX reset book: a reset is taken once, by its eye and game frame") {
    NgxResetBook book;
    book.note(Eye::Left, 12);
    book.note(Eye::Right, 12);
    CHECK_FALSE(book.take(Eye::Left, 11));
    CHECK(book.take(Eye::Left, 12));
    CHECK_FALSE(book.take(Eye::Left, 12)); // once
    CHECK(book.take(Eye::Right, 12));
    CHECK_FALSE(book.take(Eye::Mono, 12));
}

TEST_CASE("NGX reset book: game frame 0 is never noted; the newest resets are kept") {
    NgxResetBook book;
    book.note(Eye::Left, 0);
    CHECK_FALSE(book.take(Eye::Left, 0));
    for (std::uint64_t f = 1; f <= NgxResetBook::kCapacity + 2; ++f) {
        book.note(Eye::Left, f);
    }
    CHECK_FALSE(book.take(Eye::Left, 1));
    CHECK_FALSE(book.take(Eye::Left, 2));
    CHECK(book.take(Eye::Left, 3));
    CHECK(book.take(Eye::Left, NgxResetBook::kCapacity + 2));
}

TEST_CASE("NGX reset book: a reset noted twice is taken once") {
    NgxResetBook book;
    book.note(Eye::Right, 7);
    book.note(Eye::Right, 7);
    CHECK(book.take(Eye::Right, 7));
    CHECK_FALSE(book.take(Eye::Right, 7));
}
