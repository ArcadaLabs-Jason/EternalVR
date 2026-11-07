#include "features/roomscale/late_trace.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <optional>
#include <ostream>

using evr::Vec3;
using namespace evr::roomscale;

namespace {

// From a player's session log: the game's eye (r.vieworg) and a head leaned 0.12 m ahead of it; a pit reset
// moves the eye by metres.
const Vec3 kEye{214.0f, -100.34f, 91.51f};
const Vec3 kHead{214.12f, -100.30f, 91.55f};
const float kHeadSlack = length(kHead - kEye) + kLateHeadSlackMetres;

PhysicsState physics(std::optional<std::uint8_t> step, bool task, std::optional<std::uint32_t> state) {
    PhysicsState p;
    p.stepInFlight = step;
    p.taskPresent = task;
    p.taskState = state;
    return p;
}

// One deferred round: the camera hook asks, the sync takes and answers.
void deferred(LateTrace& trace, Vec3 from, Vec3 to, float fraction) {
    trace.ask(from, to);
    REQUIRE(trace.take().has_value());
    trace.answer(fraction);
}

} // namespace

TEST_CASE("the world is queried only at rest: the step's byte clear and the pending task finished") {
    // The camera hook between the step's start and its sync: the byte is set.
    CHECK_FALSE(syncQueryAllowed(physics(std::uint8_t{1}, false, std::nullopt)));
    // The byte unread (the physics object not where expected).
    CHECK_FALSE(syncQueryAllowed(physics(std::nullopt, false, std::nullopt)));
    // The byte clear but a pending task still running, or one whose state could not be read.
    CHECK_FALSE(syncQueryAllowed(physics(std::uint8_t{0}, true, 0x00000001u)));
    CHECK_FALSE(syncQueryAllowed(physics(std::uint8_t{0}, true, 0x80000002u)));
    CHECK_FALSE(syncQueryAllowed(physics(std::uint8_t{0}, true, std::nullopt)));
    // At rest: no task, or a finished one (a waiter registered or not).
    CHECK(syncQueryAllowed(physics(std::uint8_t{0}, false, std::nullopt)));
    CHECK(syncQueryAllowed(physics(std::uint8_t{0}, true, 0x00000004u)));
    CHECK(syncQueryAllowed(physics(std::uint8_t{0}, true, 0x80000004u)));
}

TEST_CASE("a task is finished when its state's low three bits are 4") {
    CHECK(taskFinished(4u));
    CHECK(taskFinished(0x7FFFFFF4u));
    CHECK_FALSE(taskFinished(0u));
    CHECK_FALSE(taskFinished(5u));
    CHECK_FALSE(taskFinished(0x80000003u));
}

TEST_CASE("the post-step hook sweeps only for the physics object of the asks' world") {
    CHECK(postStepIsOurs(0x7FF600078C58u, 0x7FF600078C58u));
    CHECK_FALSE(postStepIsOurs(0x7FF600178C58u, 0x7FF600078C58u)); // another map instance's step
    CHECK_FALSE(postStepIsOurs(0, 0));                             // no world read: never ours
}

TEST_CASE("a deferred answer is used one ask later, a direct one at once") {
    LateTrace trace;
    trace.ask(kEye, kHead);
    CHECK_FALSE(trace.fraction(kEye, kHeadSlack).has_value()); // not swept yet
    REQUIRE(trace.take().has_value());
    trace.answer(0.42f);
    trace.ask(kEye, kHead); // the next camera hook asks, then reads the answer to the one before
    REQUIRE(trace.fraction(kEye, kHeadSlack).has_value());
    CHECK(*trace.fraction(kEye, kHeadSlack) == doctest::Approx(0.42f));

    trace.answerNow(kEye, kHead, 0.7f); // no step in flight: swept at once
    CHECK(*trace.fraction(kEye, kHeadSlack) == doctest::Approx(0.7f));
    CHECK_FALSE(trace.take().has_value()); // nothing left to sweep after the sync
}

TEST_CASE("no step for a while: a direct answer every frame keeps the head in the wall") {
    // A pause: the camera hook finds the world at rest and sweeps at once; no sync runs. Before, the last
    // deferred answer went stale after 0.1 s and the head read clear.
    LateTrace trace;
    deferred(trace, kEye, kHead, 0.6f);
    for (int frame = 0; frame < 600; ++frame) {
        trace.answerNow(kEye, kHead, 0.6f);
        REQUIRE(trace.fraction(kEye, kHeadSlack).has_value());
    }
    CHECK(*trace.fraction(kEye, kHeadSlack) == doctest::Approx(0.6f));
}

TEST_CASE("an answer counts asks, not seconds, and stops after a few unanswered asks") {
    LateTrace trace;
    deferred(trace, kEye, kHead, 0.5f);
    // Slow frames (any length): one ask a frame, answered at the next sync.
    for (std::uint64_t i = 1; i <= kLateMaxAsksBehind; ++i) {
        trace.ask(kEye, kHead);
        CHECK(trace.fraction(kEye, kHeadSlack).has_value());
    }
    trace.ask(kEye, kHead); // asks with no sync between them: the answer is for a world gone by
    CHECK_FALSE(trace.fraction(kEye, kHeadSlack).has_value());
}

TEST_CASE("an answer for another place is not used (a teleport)") {
    LateTrace trace;
    deferred(trace, kEye, kHead, 0.3f);
    const Vec3 walked{kEye.x + 0.10f, kEye.y, kEye.z}; // the body walked 0.10 m under the head
    trace.ask(walked, walked + (kHead - kEye));
    CHECK(trace.fraction(walked, kHeadSlack).has_value());
    const Vec3 reset{kEye.x + 6.0f, kEye.y - 2.0f, kEye.z}; // a pit reset
    trace.ask(reset, reset + (kHead - kEye));
    CHECK_FALSE(trace.fraction(reset, kHeadSlack).has_value());
}

TEST_CASE("the newest ask before the sync is the one swept, and taken once") {
    LateTrace trace;
    trace.ask(kEye, kHead);
    trace.ask(kHead, kEye); // the camera hook ran twice before a sync
    const std::optional<LateAsk> taken = trace.take();
    REQUIRE(taken.has_value());
    CHECK(taken->from.x == kHead.x);
    CHECK_FALSE(trace.take().has_value());
}

TEST_CASE("an answer that is not a fraction is not used") {
    for (const float bad : {std::nanf(""), -0.1f, 1.5f, INFINITY}) {
        LateTrace trace;
        trace.answerNow(kEye, kHead, bad);
        CHECK_FALSE(trace.fraction(kEye, kHeadSlack).has_value());
    }
    LateTrace trace;
    trace.answerNow(kEye, kHead, 1.0f); // clear: the caller reads it as no contact
    CHECK(*trace.fraction(kEye, kHeadSlack) == 1.0f);
}

TEST_CASE("each channel casts its own query") {
    CHECK(lateQuery(kLateHead).contents == kHeadContents);
    CHECK_FALSE(lateQuery(kLateHead).weaponShape);
    for (std::size_t c = kLateReticle; c < kLateHead; ++c) {
        CHECK(lateQuery(c).contents == kShotContents);
        CHECK(lateQuery(c).weaponShape);
    }
    CHECK(kLateHead == kLateChannels - 1);
    CHECK(kLateCinemaFirst + kLateCinemaRays == kLateHead);
}

TEST_CASE("another collision world forgets a channel's asks and answers") {
    LateChannels channels;
    LateTrace* head = channels.use(kLateHead, 0x1000, 7);
    REQUIRE(head != nullptr);
    head->answerNow(kEye, kHead, 0.4f);
    head->ask(kEye, kHead);
    REQUIRE(channels.use(kLateHead, 0x1000, 7)->fraction(kEye, kHeadSlack).has_value());
    LateTrace* again = channels.use(kLateHead, 0x2000, 7); // the next map
    CHECK_FALSE(again->fraction(kEye, kHeadSlack).has_value());
    std::uint64_t dropped = 0;
    const auto taken = channels.takeFor(0x2000, dropped);
    CHECK_FALSE(taken[kLateHead].has_value()); // the old map's ask is gone
    CHECK(dropped == 0);
    CHECK(channels.use(kLateChannels, 0x2000, 7) == nullptr);
}

TEST_CASE("the sync takes only the asks for its world, with each channel's spawn id and query") {
    LateChannels channels;
    channels.use(kLateHead, 0x1000, 0x1004584)->ask(kEye, kHead);
    channels.use(kLateReticle, 0x1000, 0x1004584)->ask(kEye, kHead);
    channels.use(kLateCinemaFirst + 3, 0x3000, 0x1004584)->ask(kEye, kHead); // asked for another world
    std::uint64_t dropped = 0;
    const auto taken = channels.takeFor(0x1000, dropped);
    REQUIRE(taken[kLateHead].has_value());
    CHECK(taken[kLateHead]->spawnId == 0x1004584);
    CHECK(taken[kLateHead]->query.contents == kHeadContents);
    REQUIRE(taken[kLateReticle].has_value());
    CHECK(taken[kLateReticle]->query.weaponShape);
    CHECK_FALSE(taken[kLateCinemaFirst + 3].has_value());
    CHECK(dropped == 1);

    channels.answer(kLateHead, 0.25f);
    channels.answer(kLateChannels, 0.25f); // out of range: ignored
    LateTrace* head = channels.use(kLateHead, 0x1000, 0x1004584);
    head->ask(kEye, kHead);
    CHECK(*head->fraction(kEye, kHeadSlack) == doctest::Approx(0.25f));

    std::uint64_t none = 0;
    const auto nothing = channels.takeFor(0, none); // no world read: everything asked is dropped
    CHECK(none == 1);
    CHECK_FALSE(nothing[kLateHead].has_value());
}

TEST_CASE("forget drops only the channels in range") {
    LateChannels channels;
    for (std::size_t c = 0; c < kLateChannels; ++c) {
        channels.use(c, 0x1000, 1)->answerNow(kEye, kHead, 0.5f);
    }
    channels.forget(kLateCinemaFirst, kLateCinemaRays); // a camera cut: the fan's rays
    CHECK(channels.use(kLateReticle, 0x1000, 1)->fraction(kEye, kHeadSlack).has_value());
    CHECK_FALSE(channels.use(kLateCinemaFirst, 0x1000, 1)->fraction(kEye, kHeadSlack).has_value());
    CHECK_FALSE(channels.use(kLateHead - 1, 0x1000, 1)->fraction(kEye, kHeadSlack).has_value());
    CHECK(channels.use(kLateHead, 0x1000, 1)->fraction(kEye, kHeadSlack).has_value());
    // A count past the end stops at the last channel; a start past the end forgets nothing.
    channels.forget(kLateChannels, 5);
    CHECK(channels.use(kLateHead, 0x1000, 1)->fraction(kEye, kHeadSlack).has_value());
    channels.forget(kLateHead, 1000);
    CHECK_FALSE(channels.use(kLateHead, 0x1000, 1)->fraction(kEye, kHeadSlack).has_value());
}
