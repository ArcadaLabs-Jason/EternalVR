#include "stereo_seq/alternate_eyes.hpp"

#include "stereo_seq/object_prev.hpp"
#include "stereo_seq/prev_matrices.hpp"
#include "stereo_seq/stereo_taa.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

using evr::stereo_seq::AccumPick;
using evr::stereo_seq::AccumRole;
using evr::stereo_seq::AltAction;
using evr::stereo_seq::AlternatePairing;
using evr::stereo_seq::AlternateTaaReset;
using evr::stereo_seq::Eye;
using evr::stereo_seq::EyeAlternator;
using evr::stereo_seq::EyeTagQueue;
using evr::stereo_seq::PresentMatch;
using evr::stereo_seq::RenderTag;

namespace {

PresentMatch present(Eye eye, std::uint64_t tick, bool applied = true) {
    PresentMatch m;
    m.tagged = true;
    m.tag.eye = eye;
    m.tag.tick = tick;
    m.tag.viewApplied = applied;
    return m;
}

Eye other(Eye eye) {
    return eye == Eye::Left ? Eye::Right : Eye::Left;
}

} // namespace

// ---- EyeAlternator ----------------------------------------------------------------------------------------

TEST_CASE("alternate eyes: stereo renders alternate L, R, L, R and each render keeps its answer") {
    EyeAlternator a;
    std::uint32_t frame = 0xFFFFFFFEu; // wraps
    Eye expected = Eye::Left;
    for (int i = 0; i < 8; ++i, ++frame) {
        const Eye eye = a.eyeFor(frame);
        CHECK(eye == expected);
        // Asked again in the same render (each view's previous-matrix store, the per-eye hook, the frame
        // end).
        CHECK(a.eyeFor(frame) == eye);
        CHECK(a.eyeFor(frame) == eye);
        a.rendered(frame, eye, true);
        expected = other(expected);
    }
    CHECK(a.stats().renders[0] == 4);
    CHECK(a.stats().renders[1] == 4);
    CHECK(a.stats().mono == 0);
}

TEST_CASE("alternate eyes: a mono render starts again with eye L") {
    EyeAlternator a;
    REQUIRE(a.eyeFor(1) == Eye::Left);
    a.rendered(1, Eye::Left, true);
    REQUIRE(a.eyeFor(2) == Eye::Right);
    a.rendered(2, Eye::Right, false); // the tick could not be stereo: shown mono
    CHECK(a.eyeFor(3) == Eye::Left);
    a.rendered(3, Eye::Left, true);
    CHECK(a.eyeFor(4) == Eye::Right);
    a.rendered(4, Eye::Mono, true); // mono is never an eye
    CHECK(a.eyeFor(5) == Eye::Left);
    CHECK(a.stats().mono == 2);
}

TEST_CASE("alternate eyes: eye R only right after its eye L") {
    EyeAlternator a;
    SUBCASE("a render frame in between") {
        REQUIRE(a.eyeFor(10) == Eye::Left);
        a.rendered(10, Eye::Left, true);
        CHECK(a.eyeFor(12) == Eye::Left);
    }
    SUBCASE("eye L not stereo") {
        REQUIRE(a.eyeFor(10) == Eye::Left);
        a.rendered(10, Eye::Left, false);
        CHECK(a.eyeFor(11) == Eye::Left);
    }
    SUBCASE("no frame end yet") {
        CHECK(a.eyeFor(10) == Eye::Left);
        CHECK(a.eyeFor(11) == Eye::Left);
    }
}

// ---- AlternatePairing -------------------------------------------------------------------------------------

TEST_CASE(
    "alternate pairing: steady play shows every fresh eye with the other eye's image of the tick before") {
    AlternatePairing p;
    const auto first = p.onPresent(present(Eye::Left, 100));
    CHECK(first.action == AltAction::Hold);
    CHECK(first.freshIndex == 0);
    CHECK_FALSE(first.abandoned);
    Eye eye = Eye::Right;
    for (std::uint64_t tick = 101; tick < 110; ++tick, eye = other(eye)) {
        const auto step = p.onPresent(present(eye, tick));
        CHECK(step.action == AltAction::Publish);
        CHECK(step.freshIndex == evr::stereo_seq::eyeIndex(eye));
        CHECK(step.freshTick == tick);
        CHECK(step.heldTick == tick - 1);
        CHECK_FALSE(step.abandoned);
        CHECK(p.holding());
        CHECK(p.heldEye() == eye);
        CHECK(p.heldTick() == tick);
    }
    CHECK(p.stats().held == 1);
    CHECK(p.stats().published == 9);
    CHECK(p.stats().heldAgeSum == 9);
    CHECK(p.stats().heldAgeMax == 1);
}

TEST_CASE("alternate pairing: a single eye's image is never shown alone") {
    SUBCASE("the same eye twice") {
        AlternatePairing p;
        REQUIRE(p.onPresent(present(Eye::Left, 1)).action == AltAction::Hold);
        const auto again = p.onPresent(present(Eye::Left, 2));
        CHECK(again.action == AltAction::Hold);
        CHECK(again.abandoned);
        CHECK(p.stats().abandoned == 1);
        CHECK(p.onPresent(present(Eye::Right, 3)).action == AltAction::Publish);
    }
    SUBCASE("a held image too old") {
        AlternatePairing p(2);
        REQUIRE(p.onPresent(present(Eye::Left, 1)).action == AltAction::Hold);
        const auto late = p.onPresent(present(Eye::Right, 4));
        CHECK(late.action == AltAction::Hold);
        CHECK(late.abandoned);
        const auto next = p.onPresent(present(Eye::Left, 6)); // two frames: still pairs
        CHECK(next.action == AltAction::Publish);
        CHECK(next.heldTick == 4);
        CHECK(p.stats().heldAgeMax == 2);
    }
    SUBCASE("a held image not older than the fresh one") {
        AlternatePairing p;
        REQUIRE(p.onPresent(present(Eye::Left, 5)).action == AltAction::Hold);
        CHECK(p.onPresent(present(Eye::Right, 5)).action == AltAction::Hold);
    }
}

TEST_CASE("alternate pairing: mono, untagged and unwritten presents give up the held image") {
    AlternatePairing p;
    REQUIRE(p.onPresent(present(Eye::Left, 1)).action == AltAction::Hold);
    const auto mono = p.onPresent(present(Eye::Mono, 2, false));
    CHECK(mono.action == AltAction::ShowMono);
    CHECK(mono.abandoned);
    CHECK_FALSE(p.holding());
    CHECK(p.onPresent(PresentMatch{}).action == AltAction::ShowMono);
    REQUIRE(p.onPresent(present(Eye::Right, 3)).action == AltAction::Hold);
    const auto unwritten = p.onPresent(present(Eye::Left, 4, false));
    CHECK(unwritten.action == AltAction::Drop);
    CHECK(unwritten.abandoned);
    CHECK_FALSE(p.holding());
    CHECK(p.stats().withoutView == 1);
    CHECK(p.stats().mono == 2);
    CHECK(p.onPresent(present(Eye::Right, 5)).action == AltAction::Hold);
}

TEST_CASE("alternate pairing: an image that could not be stored is not paired with") {
    SUBCASE("the carry") {
        AlternatePairing p;
        REQUIRE(p.onPresent(present(Eye::Left, 1)).action == AltAction::Hold);
        REQUIRE(p.onPresent(present(Eye::Right, 2)).action == AltAction::Publish);
        p.carryNotStored();
        CHECK_FALSE(p.holding());
        CHECK(p.onPresent(present(Eye::Left, 3)).action == AltAction::Hold);
        CHECK(p.stats().notStored == 1);
    }
    SUBCASE("the publish") {
        AlternatePairing p;
        REQUIRE(p.onPresent(present(Eye::Left, 1)).action == AltAction::Hold);
        REQUIRE(p.onPresent(present(Eye::Right, 2)).action == AltAction::Publish);
        p.publishNotStored();
        CHECK(p.stats().published == 0);
        CHECK_FALSE(p.holding());
        CHECK(p.onPresent(present(Eye::Left, 3)).action == AltAction::Hold);
    }
}

// ---- Pose bookkeeping -------------------------------------------------------------------------------------

namespace {
struct FakeEye {
    int tick = 0;
    int side = 0;
};
struct FakeRecord {
    std::uint64_t seq = 0;
    std::array<FakeEye, 2> eyes{};
};
FakeRecord record(int tick) {
    FakeRecord r;
    r.seq = static_cast<std::uint64_t>(tick);
    r.eyes = {FakeEye{tick, 0}, FakeEye{tick, 1}};
    return r;
}
} // namespace

TEST_CASE("alternate pairing: each half is shown with the pose of the tick it was rendered in") {
    const FakeRecord fresh = record(8);
    const FakeRecord held = record(7);
    const FakeRecord leftHeld = evr::stereo_seq::composeHalves(fresh, held, 0);
    CHECK(leftHeld.seq == 8);
    CHECK(leftHeld.eyes[0].tick == 7);
    CHECK(leftHeld.eyes[0].side == 0);
    CHECK(leftHeld.eyes[1].tick == 8);
    const FakeRecord rightHeld = evr::stereo_seq::composeHalves(fresh, held, 1);
    CHECK(rightHeld.eyes[0].tick == 8);
    CHECK(rightHeld.eyes[1].tick == 7);
    CHECK(rightHeld.eyes[1].side == 1);
}

// ---- Temporal state ---------------------------------------------------------------------------------------

TEST_CASE("alternate eyes: the TAA history is reset until each eye has its own previous render") {
    AlternateTaaReset r;
    CHECK(r.onEye(Eye::Left, 10));
    CHECK(r.onEye(Eye::Right, 11));
    CHECK_FALSE(r.onEye(Eye::Left, 12));
    CHECK_FALSE(r.onEye(Eye::Right, 13));
    SUBCASE("a game frame without a render") {
        CHECK(r.onEye(Eye::Left, 15));
        CHECK(r.onEye(Eye::Right, 16));
        CHECK_FALSE(r.onEye(Eye::Left, 17));
    }
    SUBCASE("the same eye twice") {
        CHECK(r.onEye(Eye::Right, 14));
        CHECK(r.onEye(Eye::Left, 15));
        CHECK_FALSE(r.onEye(Eye::Right, 16));
    }
}

TEST_CASE("alternate eyes: each eye goes through every jitter phase") {
    for (const int n : {1, 2, 8, 16}) {
        std::set<int> left;
        std::set<int> right;
        for (std::uint64_t frame = 1000; frame < 1000 + 4 * static_cast<std::uint64_t>(n); ++frame) {
            const int phase = evr::stereo_seq::alternateSubSample(frame, n);
            CHECK(phase < n);
            (frame % 2 == 0 ? left : right).insert(phase);
        }
        CHECK(left.size() == static_cast<std::size_t>(n));
        CHECK(right.size() == static_cast<std::size_t>(n));
    }
}

TEST_CASE("alternate eyes: eye R's DLSS twin keeps its history when it evaluates every other game frame") {
    evr::stereo_seq::NgxTwins twins;
    twins.created(1, 2);
    const std::uint64_t step = evr::stereo_seq::eyeFrameStep(true);
    CHECK(twins.resetTwin(1, 11, step)); // first evaluation
    CHECK_FALSE(twins.resetTwin(1, 13, step));
    CHECK_FALSE(twins.resetTwin(1, 15, step));
    CHECK(twins.resetTwin(1, 18, step)); // a break
    CHECK(evr::stereo_seq::eyeFrameStep(false) == 1);
}

TEST_CASE("alternate eyes: the TAA images and the exposure stay per eye") {
    EyeTagQueue q;
    q.rebase(0);
    evr::stereo_seq::ExposurePlanner exposure;
    std::array<int, 2> lastOutput{-1, -1};
    int lastLeftExposure = -1;
    Eye eye = Eye::Left;
    for (std::uint64_t tick = 1; tick <= 12; ++tick, eye = other(eye)) {
        RenderTag tag;
        tag.eye = eye;
        tag.tick = tick;
        tag.viewApplied = true;
        REQUIRE(q.push(tag));
        const auto match = q.pop(static_cast<std::uint32_t>(tick));
        REQUIRE(match.tagged);
        const AccumPick out = evr::stereo_seq::pickAccumulation(AccumRole::Output, &match.tag, true);
        const AccumPick history = evr::stereo_seq::pickAccumulation(AccumRole::History, &match.tag, true);
        const int e = evr::stereo_seq::eyeIndex(eye);
        CHECK(out.pair == e);
        CHECK(history.pair == e);
        CHECK(out.index != history.index);
        if (lastOutput[static_cast<std::size_t>(e)] >= 0) {
            // Each eye reads what it wrote at its own last render, two game frames before.
            CHECK(history.index == lastOutput[static_cast<std::size_t>(e)]);
        }
        lastOutput[static_cast<std::size_t>(e)] = out.index;
        const int index = exposure.indexFor(match.tag);
        if (eye == Eye::Left) {
            lastLeftExposure = index;
        } else {
            CHECK(index == lastLeftExposure); // eye R keeps eye L's exposure (it skips its update)
        }
    }
}

namespace {
// The previous-matrix test view (prev_matrices_tests.cpp): current at [0, 4), previous at [8, 12).
struct FakeView {
    std::vector<std::byte> bytes = std::vector<std::byte>(16);
    void latch(std::uint8_t value) {
        for (std::size_t i = 0; i < 4; ++i) {
            bytes[i] = std::byte{value};
        }
    }
    void store() {
        for (std::size_t i = 0; i < 4; ++i) {
            bytes[8 + i] = bytes[i];
        }
    }
    std::uint8_t previous() const { return std::to_integer<std::uint8_t>(bytes[8]); }
};
} // namespace

TEST_CASE("alternate eyes: each eye's previous matrices are its own last render, two game frames back") {
    evr::stereo_seq::PrevMatrixBook book({{8, 4}});
    EyeAlternator alternator;
    FakeView view;
    std::array<int, 2> lastLatched{-1, -1};
    std::uint32_t renderFrame = 50;
    for (int tick = 1; tick <= 10; ++tick, ++renderFrame) {
        // One render per game frame: the store (the eye decided by the render frame), the latch, the frame
        // end.
        const Eye eye = alternator.eyeFor(renderFrame);
        view.store();
        book.afterStore(view.bytes.data(), eye, renderFrame);
        const int e = evr::stereo_seq::eyeIndex(eye);
        if (tick >= 3) {
            CHECK(view.previous() == lastLatched[static_cast<std::size_t>(e)]);
        }
        const auto latched = static_cast<std::uint8_t>(10 * tick + e);
        view.latch(latched);
        lastLatched[static_cast<std::size_t>(e)] = latched;
        alternator.rendered(renderFrame, eye, true);
    }
}

TEST_CASE("alternate eyes: the object ring reads the render before and never races an upload") {
    evr::stereo_seq::ObjectRing ring;
    std::array<std::uint32_t, 3> lastRead{};
    std::array<bool, 3> read{};
    std::uint32_t counter = 7;
    std::uint32_t lastWritten = 3; // no slot yet
    Eye eye = Eye::Left;
    for (std::uint64_t tick = 1; tick <= 30; ++tick, ++counter, eye = other(eye)) {
        bool inOrder = true;
        const auto picks = ring.picksFor(eye, tick, counter, &inOrder);
        CHECK(inOrder);
        const std::uint32_t writes = picks.current % 3;
        const std::uint32_t reads = (picks.previous + 2) % 3;
        if (lastWritten < 3) {
            // Object motion spans one game frame (the render before, the other eye's): see alternate-eye.md.
            CHECK(reads == lastWritten);
            CHECK(writes != reads);
        }
        if (read[writes]) {
            CHECK(counter - lastRead[writes] >= 2); // written again at least two renders after its last read
        }
        read[reads] = true;
        lastRead[reads] = counter;
        lastWritten = writes;
    }
}
