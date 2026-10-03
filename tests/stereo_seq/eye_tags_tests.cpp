#include "stereo_seq/eye_tags.hpp"

#include <doctest/doctest.h>

#include <cstdint>

using evr::stereo_seq::DesyncReason;
using evr::stereo_seq::Eye;
using evr::stereo_seq::EyeTagQueue;
using evr::stereo_seq::RenderTag;

namespace {

RenderTag tag(Eye eye, std::uint64_t tick) {
    RenderTag t;
    t.eye = eye;
    t.tick = tick;
    t.viewApplied = eye != Eye::Mono;
    return t;
}

} // namespace

TEST_CASE("eye tags: nothing is queued or matched before the first base") {
    EyeTagQueue q;
    CHECK_FALSE(q.synced());
    CHECK_FALSE(q.push(tag(Eye::Left, 1)));
    CHECK_FALSE(q.pop(10).tagged);
    CHECK(q.stats().untagged == 1);
}

TEST_CASE("eye tags: each present gets the tag of its own frame after a base") {
    EyeTagQueue q;
    q.rebase(100);
    REQUIRE(q.push(tag(Eye::Left, 7)));
    REQUIRE(q.push(tag(Eye::Right, 7)));
    REQUIRE(q.push(tag(Eye::Left, 8)));
    const auto a = q.pop(101);
    const auto b = q.pop(102);
    REQUIRE(a.tagged);
    REQUIRE(b.tagged);
    CHECK(a.tag.eye == Eye::Left);
    CHECK(a.tag.tick == 7);
    CHECK(a.tag.backendFrame == 101);
    CHECK(b.tag.eye == Eye::Right);
    CHECK(b.tag.tick == 7);
    REQUIRE(q.push(tag(Eye::Right, 8)));
    CHECK(q.pop(103).tag.eye == Eye::Left);
    CHECK(q.pop(104).tag.eye == Eye::Right);
    CHECK(q.synced());
    CHECK(q.stats().matched == 4);
}

TEST_CASE("eye tags: frames in flight before the base are untagged and do not disturb the sync") {
    EyeTagQueue q;
    q.rebase(50);
    REQUIRE(q.push(tag(Eye::Left, 3)));
    CHECK_FALSE(q.pop(49).tagged); // an older frame presenting late
    CHECK_FALSE(q.pop(50).tagged);
    CHECK(q.synced());
    const auto m = q.pop(51);
    REQUIRE(m.tagged);
    CHECK(m.tag.tick == 3);
}

TEST_CASE("eye tags: a repeated present of the same backend frame is untagged") {
    EyeTagQueue q;
    q.rebase(10);
    REQUIRE(q.push(tag(Eye::Left, 1)));
    REQUIRE(q.push(tag(Eye::Right, 1)));
    CHECK(q.pop(11).tagged);
    CHECK_FALSE(q.pop(11).tagged);
    CHECK(q.synced());
    CHECK(q.pop(12).tag.eye == Eye::Right);
}

TEST_CASE("eye tags: a tagged frame that never presents drops the sync") {
    EyeTagQueue q;
    q.rebase(10);
    REQUIRE(q.push(tag(Eye::Left, 1)));
    REQUIRE(q.push(tag(Eye::Right, 1)));
    // Backend 11 never presented; 12 comes first.
    CHECK_FALSE(q.pop(12).tagged);
    CHECK_FALSE(q.synced());
    CHECK(q.lastDesync() == DesyncReason::MissingPresent);
    CHECK(q.stats().missingPresent == 1);
    CHECK(q.size() == 0);
    CHECK_FALSE(q.push(tag(Eye::Left, 2)));
}

TEST_CASE("eye tags: a present no tag was queued for drops the sync") {
    EyeTagQueue q;
    q.rebase(10);
    REQUIRE(q.push(tag(Eye::Left, 1)));
    REQUIRE(q.pop(11).tagged);
    // The render thread presents a frame that went past the wrapper untagged.
    CHECK_FALSE(q.pop(12).tagged);
    CHECK_FALSE(q.synced());
    CHECK(q.lastDesync() == DesyncReason::UntaggedFrame);
    CHECK(q.stats().untaggedFrame == 1);
}

TEST_CASE("eye tags: a requested desync is counted and clears the queue") {
    EyeTagQueue q;
    q.rebase(10);
    REQUIRE(q.push(tag(Eye::Mono, 0)));
    q.desync(DesyncReason::Requested);
    CHECK_FALSE(q.synced());
    CHECK(q.size() == 0);
    CHECK(q.stats().requested == 1);
    q.desync(DesyncReason::Requested); // already out of sync: not counted again
    CHECK(q.stats().requested == 1);
}

TEST_CASE("eye tags: mono and stereo frames interleave in one queue") {
    EyeTagQueue q;
    q.rebase(10);
    REQUIRE(q.push(tag(Eye::Mono, 0)));
    REQUIRE(q.push(tag(Eye::Left, 5)));
    REQUIRE(q.push(tag(Eye::Right, 5)));
    CHECK(q.pop(11).tag.eye == Eye::Mono);
    CHECK(q.pop(12).tag.eye == Eye::Left);
    CHECK(q.pop(13).tag.eye == Eye::Right);
    CHECK(q.synced());
}

TEST_CASE("eye tags: too many tags queued drops the sync") {
    EyeTagQueue q(2);
    q.rebase(0);
    REQUIRE(q.push(tag(Eye::Left, 1)));
    REQUIRE(q.push(tag(Eye::Right, 1)));
    CHECK_FALSE(q.push(tag(Eye::Left, 2)));
    CHECK_FALSE(q.synced());
    CHECK(q.lastDesync() == DesyncReason::Overflow);
}

TEST_CASE("eye tags: the backend counter may wrap") {
    EyeTagQueue q;
    q.rebase(0xFFFFFFFEu);
    REQUIRE(q.push(tag(Eye::Left, 1)));
    REQUIRE(q.push(tag(Eye::Right, 1)));
    CHECK(q.pop(0xFFFFFFFFu).tag.eye == Eye::Left);
    CHECK(q.pop(0u).tag.eye == Eye::Right);
    CHECK(q.synced());
}

TEST_CASE("eye tags: a rebase starts over") {
    EyeTagQueue q;
    q.rebase(5);
    REQUIRE(q.push(tag(Eye::Left, 1)));
    q.rebase(40);
    CHECK(q.size() == 0);
    REQUIRE(q.push(tag(Eye::Mono, 0)));
    const auto m = q.pop(41);
    REQUIRE(m.tagged);
    CHECK(m.tag.eye == Eye::Mono);
    CHECK(q.stats().rebases == 2);
}

TEST_CASE("eye tags: a queued frame's tag can be read by its backend value before its present") {
    EyeTagQueue q;
    CHECK(q.peek(1) == nullptr);
    q.rebase(20);
    REQUIRE(q.push(tag(Eye::Left, 5)));
    REQUIRE(q.push(tag(Eye::Right, 5)));
    const RenderTag* left = q.peek(21);
    const RenderTag* right = q.peek(22);
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);
    CHECK(left->eye == Eye::Left);
    CHECK(right->eye == Eye::Right);
    CHECK(q.peek(23) == nullptr);
    CHECK(q.size() == 2); // nothing taken
    REQUIRE(q.pop(21).tagged);
    CHECK(q.peek(21) == nullptr);
    q.desync(DesyncReason::Requested);
    CHECK(q.peek(22) == nullptr);
}

TEST_CASE("eye tags: each eye counts its own frames, mono with eye L, across bases") {
    EyeTagQueue q;
    q.rebase(0);
    REQUIRE(q.push(tag(Eye::Left, 1)));
    REQUIRE(q.push(tag(Eye::Right, 1)));
    REQUIRE(q.push(tag(Eye::Mono, 2)));
    REQUIRE(q.push(tag(Eye::Left, 3)));
    CHECK(q.peek(1)->eyeSeq == 0);
    CHECK(q.peek(2)->eyeSeq == 0);
    CHECK(q.peek(3)->eyeSeq == 1);
    CHECK(q.peek(4)->eyeSeq == 2);
    q.rebase(10);
    REQUIRE(q.push(tag(Eye::Right, 4)));
    CHECK(q.peek(11)->eyeSeq == 1);
}

TEST_CASE("eye tags: only an eye frame with its view applied drew one eye's view") {
    using evr::stereo_seq::drawsEyeView;
    using evr::stereo_seq::eyeIndex;
    CHECK(drawsEyeView(tag(Eye::Left, 1)));
    CHECK(drawsEyeView(tag(Eye::Right, 1)));
    // A mono frame indexes as eye L, but it drew the game's own view (the cinema screen, menus).
    RenderTag mono = tag(Eye::Mono, 2);
    CHECK(eyeIndex(mono.eye) == 0);
    CHECK_FALSE(drawsEyeView(mono));
    mono.viewApplied = true;
    CHECK_FALSE(drawsEyeView(mono));
    RenderTag noView = tag(Eye::Right, 3);
    noView.viewApplied = false;
    CHECK_FALSE(drawsEyeView(noView));
}
