#include "stereo_seq/refraction_history.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

using evr::stereo_seq::Eye;
using evr::stereo_seq::kRefractionUpdates;
using evr::stereo_seq::RefractionHistory;
using evr::stereo_seq::RefractionPlan;
using evr::stereo_seq::RefractionReadiness;
using evr::stereo_seq::RefractionRestart;

namespace {

// Alternating eyes from counter `from` to `to`: eye L on odd counters, eye R on even ones.
void alternate(RefractionHistory& h, std::uint32_t from, std::uint32_t to) {
    for (std::uint32_t c = from; c <= to; ++c) {
        h.plan(c % 2 ? Eye::Left : Eye::Right, c);
    }
}

// A render with its mask pass and `updates` refraction updates: the updates' plans.
std::array<RefractionPlan, kRefractionUpdates>
renderWith(RefractionHistory& h, Eye eye, std::uint32_t counter, int updates) {
    std::array<RefractionPlan, kRefractionUpdates> out{};
    h.plan(eye, counter);
    for (int u = 0; u < updates; ++u) {
        out[static_cast<std::size_t>(u)] = h.plan(eye, counter, u, updates);
    }
    return out;
}

// Alternating eyes from counter `from` to `to` as above, eye L with `left` updates and eye R with `right`.
void alternateWith(RefractionHistory& h, std::uint32_t from, std::uint32_t to, int left, int right) {
    for (std::uint32_t c = from; c <= to; ++c) {
        renderWith(h, c % 2 ? Eye::Left : Eye::Right, c, c % 2 ? left : right);
    }
}

RefractionReadiness ready() {
    RefractionReadiness r;
    r.installed = true;
    r.gameTouch = true;
    r.imagesMade = true;
    return r;
}

} // namespace

TEST_CASE("refraction history: each eye reads the slot it wrote last, whatever the counter's parity") {
    RefractionHistory h;
    // Eye L on odd counters, eye R on even ones: the engine alone would have eye L always write [1] and read
    // [0], which eye R wrote.
    const RefractionPlan l1 = h.plan(Eye::Left, 11);
    const RefractionPlan r1 = h.plan(Eye::Right, 12);
    const RefractionPlan l2 = h.plan(Eye::Left, 13);
    const RefractionPlan r2 = h.plan(Eye::Right, 14);
    const RefractionPlan l3 = h.plan(Eye::Left, 15);
    CHECK(l1.eye == 0);
    CHECK(r1.eye == 1);
    CHECK(l2.eye == 0);
    // The slot read (write ^ 1) is the one the eye's last render wrote.
    CHECK((l2.write ^ 1) == l1.write);
    CHECK((l3.write ^ 1) == l2.write);
    CHECK((r2.write ^ 1) == r1.write);
    CHECK(l2.write != l1.write);
    CHECK(r2.write != r1.write);
}

TEST_CASE("refraction history: the passes of one render get the same plan") {
    RefractionHistory h;
    alternate(h, 1, 6);
    const RefractionPlan mask = h.plan(Eye::Left, 7);
    const RefractionPlan update0 = h.plan(Eye::Left, 7);
    const RefractionPlan update1 = h.plan(Eye::Left, 7);
    CHECK_FALSE(mask.repeated);
    CHECK(update0.repeated);
    CHECK(update1.repeated);
    CHECK(update0.write == mask.write);
    CHECK(update1.write == mask.write);
    CHECK(update0.restart == mask.restart);
    // Eye R's render in between does not move eye L's plan for its own render.
    const RefractionPlan right = h.plan(Eye::Right, 8);
    CHECK(h.plan(Eye::Left, 7).write == mask.write);
    CHECK(h.plan(Eye::Right, 8).write == right.write);
    // A first render that restarts says so on every pass.
    RefractionHistory fresh;
    CHECK(fresh.plan(Eye::Right, 2).restart == RefractionRestart::First);
    CHECK(fresh.plan(Eye::Right, 2).restart == RefractionRestart::First);
}

TEST_CASE("refraction history: the first render of each eye restarts, steady stereo never does") {
    RefractionHistory h;
    CHECK(h.plan(Eye::Left, 1).restart == RefractionRestart::First);
    CHECK(h.plan(Eye::Right, 2).restart == RefractionRestart::First);
    for (std::uint32_t c = 3; c <= 40; ++c) {
        CHECK(h.plan(c % 2 ? Eye::Left : Eye::Right, c).restart == RefractionRestart::None);
    }
}

TEST_CASE("refraction history: a skipped eye R keeps eye L's chain and restarts eye R") {
    RefractionHistory h;
    alternate(h, 1, 6);
    const RefractionPlan l7 = h.plan(Eye::Left, 7);
    // Eye R skipped this tick: eye L renders again at 8, from its own render just before.
    const RefractionPlan l8 = h.plan(Eye::Left, 8);
    CHECK(l8.restart == RefractionRestart::None);
    CHECK((l8.write ^ 1) == l7.write);
    CHECK(h.plan(Eye::Right, 9).restart == RefractionRestart::Gap); // its own last render is 6
    CHECK(h.plan(Eye::Left, 10).restart == RefractionRestart::None);
    CHECK(h.plan(Eye::Right, 11).restart == RefractionRestart::None);
}

TEST_CASE("refraction history: renders without refraction for a while restart the eye") {
    RefractionHistory h;
    alternate(h, 1, 6);
    // The glass leaves the view: no refraction passes from 7 to 12.
    CHECK(h.plan(Eye::Left, 13).restart == RefractionRestart::Gap);
    CHECK(h.plan(Eye::Right, 14).restart == RefractionRestart::Gap);
    CHECK(h.plan(Eye::Left, 15).restart == RefractionRestart::None);
}

TEST_CASE("refraction history: mono renders are eye L's chain") {
    RefractionHistory h;
    const RefractionPlan m1 = h.plan(Eye::Mono, 1);
    CHECK(m1.restart == RefractionRestart::First);
    CHECK(m1.eye == 0);
    const RefractionPlan m2 = h.plan(Eye::Mono, 2);
    CHECK(m2.restart == RefractionRestart::None);
    CHECK((m2.write ^ 1) == m1.write);
    // Stereo starts: eye L goes on from the mono renders, eye R starts its own.
    const RefractionPlan l = h.plan(Eye::Left, 3);
    CHECK(l.restart == RefractionRestart::None);
    CHECK((l.write ^ 1) == m2.write);
    CHECK(h.plan(Eye::Right, 4).restart == RefractionRestart::First);
    CHECK(h.plan(Eye::Left, 5).restart == RefractionRestart::None);
}

TEST_CASE("refraction history: an untagged render after eye L restarts, and so does eye L after it") {
    RefractionHistory h;
    alternate(h, 1, 6);
    CHECK(h.plan(Eye::Left, 7).restart == RefractionRestart::None);
    // Eye R's render whose tag was not found.
    CHECK(h.plan(Eye::Mono, 8).restart == RefractionRestart::Untagged);
    // Eye L's next render would read that one's picture.
    CHECK(h.plan(Eye::Left, 9).restart == RefractionRestart::AfterUntagged);
    CHECK(h.plan(Eye::Right, 10).restart == RefractionRestart::Gap); // its own last render is 6
    CHECK(h.plan(Eye::Left, 11).restart == RefractionRestart::None);
}

TEST_CASE("refraction history: a mono stretch after stereo is one chain, eye R restarts after it") {
    RefractionHistory h;
    alternate(h, 1, 6);
    CHECK(h.plan(Eye::Mono, 7).restart == RefractionRestart::None); // after eye R's render 6
    for (std::uint32_t c = 8; c <= 30; ++c) {
        CHECK(h.plan(Eye::Mono, c).restart == RefractionRestart::None);
    }
    CHECK(h.plan(Eye::Left, 31).restart == RefractionRestart::None);
    CHECK(h.plan(Eye::Right, 32).restart == RefractionRestart::Gap);
    CHECK(h.plan(Eye::Left, 33).restart == RefractionRestart::None);
}

TEST_CASE("refraction history: reset makes every eye start over") {
    RefractionHistory h;
    alternate(h, 1, 6);
    h.reset();
    const RefractionPlan l = h.plan(Eye::Left, 7);
    CHECK(l.restart == RefractionRestart::First);
    CHECK(l.write == 0);
    CHECK(h.plan(Eye::Right, 8).restart == RefractionRestart::First);
    CHECK(h.plan(Eye::Left, 9).restart == RefractionRestart::None);
}

TEST_CASE("refraction history: the counter wraps") {
    RefractionHistory h;
    h.plan(Eye::Left, 0xFFFFFFFDu);
    h.plan(Eye::Right, 0xFFFFFFFEu);
    h.plan(Eye::Left, 0xFFFFFFFFu);
    CHECK(h.plan(Eye::Right, 0u).restart == RefractionRestart::None);
    CHECK(h.plan(Eye::Left, 1u).restart == RefractionRestart::None);
}

TEST_CASE("refraction history: eye R's update 1 after its renders with one update restarts") {
    RefractionHistory h;
    alternateWith(h, 1, 8, 1, 1);
    // Both eyes go from one update to two in the same tick. The engine starts eye L's render over (its count
    // differs from eye R's render before) but not eye R's (eye L's render before has two as well), whose
    // update 1 would read eye R's slot of an older render, or of none.
    const auto left = renderWith(h, Eye::Left, 9, 2);
    const auto right = renderWith(h, Eye::Right, 10, 2);
    CHECK(left[1].fresh());
    CHECK(right[1].fresh());
    CHECK(right[1].restart == RefractionRestart::None);
    CHECK(right[1].updateRestart == RefractionRestart::CountChanged);
    // The count changed for update 0 too: it starts over, as by the engine's rule.
    CHECK(right[0].updateRestart == RefractionRestart::CountChanged);
    // The next renders blend both updates with their own history.
    for (std::uint32_t c = 11; c <= 16; ++c) {
        const auto plans = renderWith(h, c % 2 ? Eye::Left : Eye::Right, c, 2);
        CHECK_FALSE(plans[0].fresh());
        CHECK_FALSE(plans[1].fresh());
    }
}

TEST_CASE("refraction history: eye L's update 1 after its renders with one update restarts") {
    RefractionHistory h;
    alternateWith(h, 1, 8, 1, 1);
    // Eye R has the second refraction update a tick before eye L: the engine starts eye R's render over (eye
    // L's render before has one) but not eye L's next one (eye R's render before has two as well).
    renderWith(h, Eye::Left, 9, 1);
    const auto right = renderWith(h, Eye::Right, 10, 2);
    CHECK(right[1].updateRestart == RefractionRestart::CountChanged);
    const auto left = renderWith(h, Eye::Left, 11, 2);
    CHECK(left[1].restart == RefractionRestart::None);
    CHECK(left[1].updateRestart == RefractionRestart::CountChanged);
    CHECK(left[0].updateRestart == RefractionRestart::CountChanged);
    CHECK_FALSE(renderWith(h, Eye::Right, 12, 2)[1].fresh());
    CHECK_FALSE(renderWith(h, Eye::Left, 13, 2)[1].fresh());
}

TEST_CASE("refraction history: eyes with different update counts keep their history") {
    RefractionHistory h;
    alternateWith(h, 1, 4, 1, 2);
    // Eye L has one refraction update, eye R two: the engine finds the count changed at every render and
    // starts both eyes over; per eye nothing changes.
    for (std::uint32_t c = 5; c <= 20; ++c) {
        const int updates = c % 2 ? 1 : 2;
        const auto plans = renderWith(h, c % 2 ? Eye::Left : Eye::Right, c, updates);
        for (int u = 0; u < updates; ++u) {
            CHECK_FALSE(plans[static_cast<std::size_t>(u)].fresh());
        }
    }
}

TEST_CASE("refraction history: fewer updates start the eye's updates over, as the engine does") {
    RefractionHistory h;
    alternateWith(h, 1, 6, 2, 2);
    const auto left = renderWith(h, Eye::Left, 7, 1);
    CHECK(left[0].updateRestart == RefractionRestart::CountChanged);
    CHECK_FALSE(renderWith(h, Eye::Right, 8, 2)[0].fresh());
    CHECK_FALSE(renderWith(h, Eye::Left, 9, 1)[0].fresh());
    renderWith(h, Eye::Right, 10, 2);
    // Back to two: the count changed again.
    const auto again = renderWith(h, Eye::Left, 11, 2);
    CHECK(again[0].updateRestart == RefractionRestart::CountChanged);
    CHECK(again[1].updateRestart == RefractionRestart::CountChanged);
}

TEST_CASE("refraction history: an update the eye's last render did not run restarts") {
    RefractionHistory h;
    alternateWith(h, 1, 6, 2, 2);
    // Eye L's render 7 runs its mask pass and update 0 only (update 1 never reaches the hooks).
    h.plan(Eye::Left, 7);
    CHECK_FALSE(h.plan(Eye::Left, 7, 0, 2).fresh());
    renderWith(h, Eye::Right, 8, 2);
    const auto left = renderWith(h, Eye::Left, 9, 2);
    CHECK_FALSE(left[0].fresh());
    CHECK(left[1].restart == RefractionRestart::None);
    CHECK(left[1].updateRestart == RefractionRestart::NotInLast);
    // A render with the mask pass only: the next render's updates have no history of their own.
    renderWith(h, Eye::Right, 10, 2);
    h.plan(Eye::Left, 11);
    renderWith(h, Eye::Right, 12, 2);
    const auto after = renderWith(h, Eye::Left, 13, 2);
    CHECK(after[0].updateRestart == RefractionRestart::NotInLast);
    CHECK(after[1].updateRestart == RefractionRestart::NotInLast);
}

TEST_CASE("refraction history: an update asked twice gets the same answer and the render's slot") {
    RefractionHistory h;
    alternateWith(h, 1, 6, 1, 1);
    const RefractionPlan mask = h.plan(Eye::Left, 7);
    const RefractionPlan first = h.plan(Eye::Left, 7, 1, 2);
    const RefractionPlan again = h.plan(Eye::Left, 7, 1, 2);
    CHECK(first.updateRestart == RefractionRestart::CountChanged);
    CHECK_FALSE(first.updateRepeated);
    CHECK(again.updateRestart == first.updateRestart);
    CHECK(again.updateRepeated);
    CHECK(first.write == mask.write);
    CHECK(again.write == mask.write);
    CHECK(first.repeated);
    // The mask pass asks nothing about updates.
    CHECK(mask.updateRestart == RefractionRestart::None);
    // An update that asks first makes the render's plan.
    RefractionHistory other;
    const RefractionPlan u0 = other.plan(Eye::Right, 2, 0, 1);
    CHECK_FALSE(u0.repeated);
    CHECK(u0.restart == RefractionRestart::First);
    CHECK(u0.fresh());
    CHECK(other.plan(Eye::Right, 2).repeated);
    // An update past the engine's images always starts over.
    CHECK(h.plan(Eye::Left, 7, kRefractionUpdates, 3).fresh());
}

TEST_CASE("refraction history: a render that restarts restarts every update") {
    RefractionHistory h;
    alternateWith(h, 1, 6, 2, 2);
    // Glass out of view from 7 to 12.
    const auto left = renderWith(h, Eye::Left, 13, 2);
    CHECK(left[0].restart == RefractionRestart::Gap);
    CHECK(left[1].restart == RefractionRestart::Gap);
    CHECK(left[0].updateRestart == RefractionRestart::None);
    CHECK(left[0].fresh());
    CHECK(left[1].fresh());
    renderWith(h, Eye::Right, 14, 2);
    CHECK_FALSE(renderWith(h, Eye::Left, 15, 2)[1].fresh());
}

TEST_CASE("refraction history: invalidate makes every eye start over for its reason") {
    RefractionHistory h;
    alternateWith(h, 1, 6, 2, 2);
    const RefractionPlan before = h.plan(Eye::Left, 7);
    h.invalidate(RefractionRestart::Resumed);
    const auto left = renderWith(h, Eye::Left, 9, 2);
    CHECK(left[0].restart == RefractionRestart::Resumed);
    CHECK(left[1].fresh());
    CHECK(h.plan(Eye::Left, 9).write != before.write); // the slots keep alternating
    CHECK(renderWith(h, Eye::Right, 10, 2)[0].restart == RefractionRestart::Resumed);
    const auto next = renderWith(h, Eye::Left, 11, 2);
    CHECK_FALSE(next[0].fresh());
    CHECK_FALSE(next[1].fresh());

    // The render size changed: both eyes start over once more, for that reason.
    renderWith(h, Eye::Right, 12, 2);
    h.invalidate(RefractionRestart::Resized);
    CHECK(renderWith(h, Eye::Left, 13, 2)[0].restart == RefractionRestart::Resized);
    CHECK(renderWith(h, Eye::Right, 14, 2)[1].restart == RefractionRestart::Resized);
    CHECK_FALSE(renderWith(h, Eye::Left, 15, 2)[0].fresh());
    CHECK(std::strcmp(evr::stereo_seq::refractionRestartName(RefractionRestart::Resized),
                      "after the render size changed") == 0);
}

TEST_CASE("refraction history: the engine's accumulation index is pair * 2 + update") {
    // _refractionAccumulationBuffer<pair><update> at device context + 0x440 + 8 * index, made in this order
    // by the constructor's two loops; the pass writes [parity * 2 + update].
    CHECK(evr::stereo_seq::refractionAccIndex(0, 0) == 0);
    CHECK(evr::stereo_seq::refractionAccIndex(0, 1) == 1);
    CHECK(evr::stereo_seq::refractionAccIndex(1, 0) == 2);
    CHECK(evr::stereo_seq::refractionAccIndex(1, 1) == 3);
}

TEST_CASE("refraction readiness: why not, and when stereo renders are held off") {
    RefractionReadiness r = ready();
    CHECK(evr::stereo_seq::refractionNotReady(r) == nullptr);
    CHECK_FALSE(evr::stereo_seq::refractionHeldOff(r, Eye::Left));
    CHECK_FALSE(evr::stereo_seq::refractionHeldOff(r, Eye::Right));

    r.imagesMade = false;
    CHECK(evr::stereo_seq::refractionNotReady(r) != nullptr);
    CHECK(evr::stereo_seq::refractionHeldOff(r, Eye::Left));
    CHECK(evr::stereo_seq::refractionHeldOff(r, Eye::Right));
    CHECK_FALSE(evr::stereo_seq::refractionHeldOff(r, Eye::Mono)); // mono stays the engine's

    r = ready();
    r.failedClosed = true;
    CHECK(evr::stereo_seq::refractionHeldOff(r, Eye::Right));

    // Dynamic resolution scales only the engine's images: held off in stereo until rs_enable is 0 again.
    r = ready();
    r.dynamicResolution = true;
    CHECK(evr::stereo_seq::refractionNotReady(r) != nullptr);
    CHECK(evr::stereo_seq::refractionHeldOff(r, Eye::Left));
    CHECK(evr::stereo_seq::refractionHeldOff(r, Eye::Right));
    CHECK_FALSE(evr::stereo_seq::refractionHeldOff(r, Eye::Mono));

    // Without the hooks or the guard: the engine's own, nothing held.
    r = ready();
    r.installed = false;
    CHECK_FALSE(evr::stereo_seq::refractionHeldOff(r, Eye::Left));
    r = ready();
    r.gameTouch = false;
    CHECK_FALSE(evr::stereo_seq::refractionHeldOff(r, Eye::Right));
}
