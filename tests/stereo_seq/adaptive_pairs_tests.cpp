// ETERNALVR_ALTERNATE_EYES=auto: pairs that render both eyes in their tick (Route S) mixed with
// alternating ones, through the alternator, the present pairing, the TAA and DLSS resets and the object ring.

#include "stereo_seq/alternate_eyes.hpp"
#include "stereo_seq/object_prev.hpp"
#include "stereo_seq/stereo_taa.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

using evr::stereo_seq::AltAction;
using evr::stereo_seq::AlternatePairing;
using evr::stereo_seq::AlternateTaaReset;
using evr::stereo_seq::AltStep;
using evr::stereo_seq::Eye;
using evr::stereo_seq::EyeAlternator;
using evr::stereo_seq::NgxTwins;
using evr::stereo_seq::ObjectRing;
using evr::stereo_seq::PresentMatch;

namespace {

PresentMatch present(Eye eye, std::uint64_t tick, bool pairInTick) {
    PresentMatch m;
    m.tagged = true;
    m.tag.eye = eye;
    m.tag.tick = tick;
    m.tag.viewApplied = true;
    m.tag.pairInTick = pairInTick;
    return m;
}

// One render as the renderer sees it.
struct Render {
    Eye eye;
    std::uint64_t tick;
    bool pairInTick;
};

// A run of pairs, each both eyes in its tick (true) or alternating (false): the renders in order.
std::vector<Render> renders(const std::vector<bool>& pairs, std::uint64_t firstTick = 100) {
    std::vector<Render> out;
    std::uint64_t tick = firstTick;
    for (const bool both : pairs) {
        out.push_back({Eye::Left, tick, both});
        if (!both) {
            ++tick;
        }
        out.push_back({Eye::Right, tick, both});
        ++tick;
    }
    return out;
}

const std::vector<bool> kMixed = {true, true,  true, false, false, false,
                                  true, false, true, true,  false, false};

} // namespace

TEST_CASE("adaptive pairs: after a pair with eye R nested the next render starts a pair with eye L") {
    EyeAlternator a;
    std::uint32_t frame = 50;
    for (const bool both : kMixed) {
        CHECK(a.eyeFor(frame) == Eye::Left);
        a.rendered(frame, Eye::Left, true, both);
        // Eye R nested: its own render frame, never asked of the alternator.
        frame += both ? 2u : 1u;
        if (!both) {
            CHECK(a.eyeFor(frame) == Eye::Right);
            a.rendered(frame, Eye::Right, true);
            ++frame;
        }
    }
    // The nested render frame not counted as one: the next frame after eye L's also starts a pair.
    EyeAlternator b;
    b.eyeFor(10);
    b.rendered(10, Eye::Left, true, true);
    CHECK(b.eyeFor(11) == Eye::Left);
}

TEST_CASE("adaptive pairs: a both-eyes tick shows eye L with its own eye R, alternating ones as before") {
    AlternatePairing p;
    std::uint64_t shown = 0;
    for (const Render& r : renders(kMixed)) {
        const AltStep s = p.onPresent(present(r.eye, r.tick, r.pairInTick));
        if (r.pairInTick && r.eye == Eye::Left) {
            // Held for its own eye R, never shown beside the other eye's older image.
            CHECK(s.action == AltAction::Hold);
        } else if (r.pairInTick) {
            CHECK(s.action == AltAction::Publish);
            CHECK(s.heldTick == r.tick); // the same game frame
            ++shown;
        } else if (s.action == AltAction::Publish) {
            CHECK(s.heldTick + 1 == r.tick); // the other eye of the tick before
            ++shown;
        }
    }
    const AlternatePairing::Stats& st = p.stats();
    CHECK(st.abandoned == 0);  // a both-eyes tick's eye L releases the held image without giving it up
    CHECK(st.pairStarts == 6); // the eye L of each both-eyes pair
    CHECK(st.sameTick == 6);
    CHECK(st.published == shown);
    CHECK(st.held == 0); // every alternating eye finds the other eye's image of the tick before
    CHECK(st.published == 18);
    CHECK(st.heldAgeMax == 1);
}

TEST_CASE("adaptive pairs: without pairInTick tags the pairing is alternate eyes' own") {
    AlternatePairing p;
    // Eye L then eye R of the same tick, untagged: not a partner (as with alternate eyes on).
    CHECK(p.onPresent(present(Eye::Left, 10, false)).action == AltAction::Hold);
    CHECK(p.onPresent(present(Eye::Right, 10, false)).action == AltAction::Hold);
    CHECK(p.stats().abandoned == 1);
    CHECK(p.stats().sameTick == 0);
}

TEST_CASE("adaptive pairs: a both-eyes eye R whose eye L is missing is held, not shown") {
    AlternatePairing p;
    CHECK(p.onPresent(present(Eye::Left, 10, false)).action == AltAction::Hold);
    CHECK(p.onPresent(present(Eye::Right, 11, false)).action == AltAction::Publish);
    // A both-eyes tick whose eye L was dropped: the held image is eye R's own of tick 11, so eye R is held.
    CHECK(p.onPresent(present(Eye::Right, 12, true)).action == AltAction::Hold);
    CHECK(p.onPresent(present(Eye::Left, 13, true)).action == AltAction::Hold);
    CHECK(p.onPresent(present(Eye::Right, 13, true)).action == AltAction::Publish);
}

TEST_CASE("adaptive pairs: switching between the ways never resets TAA after the first run") {
    AlternateTaaReset adaptive(true);
    int resets = 0;
    int index = 0;
    for (const Render& r : renders(kMixed)) {
        const bool reset = adaptive.onEye(r.eye, r.tick);
        if (index++ >= 2) {
            CHECK_FALSE(reset);
        }
        resets += reset ? 1 : 0;
    }
    CHECK(resets == 2); // the first two renders
    // Alternate eyes on: eye R of eye L's own game frame is a break, as before.
    AlternateTaaReset on;
    CHECK(on.onEye(Eye::Left, 10));
    CHECK(on.onEye(Eye::Right, 11));
    CHECK_FALSE(on.onEye(Eye::Left, 12));
    CHECK(on.onEye(Eye::Right, 12));
}

TEST_CASE("adaptive pairs: eye R's DLSS twin keeps its history one or two game frames apart") {
    NgxTwins twins;
    twins.created(1, 2);
    std::vector<std::uint64_t> rightTicks;
    for (const Render& r : renders(kMixed)) {
        if (r.eye == Eye::Right) {
            rightTicks.push_back(r.tick);
        }
    }
    CHECK(twins.resetTwin(1, rightTicks[0], 1, 2)); // first evaluation
    for (std::size_t i = 1; i < rightTicks.size(); ++i) {
        CHECK_FALSE(twins.resetTwin(1, rightTicks[i], 1, 2));
    }
    CHECK(twins.resetTwin(1, rightTicks.back() + 3, 1, 2)); // a break
    CHECK(twins.resetTwin(1, rightTicks.back() + 3, 1, 2)); // the same frame again
    // The one-step forms keep their meaning.
    CHECK_FALSE(twins.resetTwin(1, rightTicks.back() + 4));
    CHECK(twins.resetTwin(1, rightTicks.back() + 6));
    CHECK_FALSE(twins.resetTwin(1, rightTicks.back() + 8, 2));
}

TEST_CASE("adaptive pairs: the object ring keeps its upload rule through the switches") {
    ObjectRing ring;
    std::array<std::uint32_t, 3> lastRead{};
    std::array<bool, 3> read{};
    std::array<std::uint64_t, 3> tickIn{};
    std::uint32_t counter = 1000;
    for (int repeat = 0; repeat < 4; ++repeat) {
        for (const Render& r : renders(kMixed, 100 + static_cast<std::uint64_t>(repeat) * 100)) {
            const ObjectRing::Picks p = ring.picksFor(r.eye, r.tick, counter);
            const int written = static_cast<int>(p.current % 3u);
            const int readSlot = static_cast<int>((p.previous + 2u) % 3u);
            // Written no sooner than two renders after its last read, whichever way the pairs go.
            if (read[static_cast<std::size_t>(written)]) {
                CHECK(counter - lastRead[static_cast<std::size_t>(written)] >= 2u);
            }
            // The previous frame is an earlier game frame (never this render's own tick's other eye when that
            // eye rendered a tick before).
            if (read[static_cast<std::size_t>(readSlot)]) {
                CHECK(tickIn[static_cast<std::size_t>(readSlot)] <= r.tick);
            }
            tickIn[static_cast<std::size_t>(written)] = r.tick;
            lastRead[static_cast<std::size_t>(written)] = counter;
            read[static_cast<std::size_t>(written)] = true;
            lastRead[static_cast<std::size_t>(readSlot)] = counter;
            read[static_cast<std::size_t>(readSlot)] = true;
            ++counter;
        }
    }
}
