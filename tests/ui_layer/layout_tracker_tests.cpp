#include "ui_layer/layout_tracker.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>

using evr::ui_layer::LayoutTracker;

namespace {

constexpr LayoutTracker::Handle kGui = 0x1000;
constexpr LayoutTracker::Handle kOther = 0x2000;
constexpr LayoutTracker::Handle kQueueGraphics = 0x10;
constexpr LayoutTracker::Handle kQueueCompute = 0x20;
constexpr std::int32_t kColor = 2;
constexpr std::int32_t kShaderRead = 5;

} // namespace

TEST_CASE("layout tracker: unknown until a barrier is submitted") {
    LayoutTracker t;
    REQUIRE(t.addCandidate(kGui));
    CHECK_FALSE(t.stateOf(kGui).has_value());
    t.onBarrier(1, kGui, kColor);
    CHECK_FALSE(t.stateOf(kGui).has_value()); // recorded, not submitted
    const std::array<LayoutTracker::Handle, 1> cbs{1};
    t.onSubmit(kQueueGraphics, cbs.data(), cbs.size());
    const auto s = t.stateOf(kGui);
    REQUIRE(s.has_value());
    CHECK(s->layout == kColor);
    CHECK(s->queue == kQueueGraphics);
}

TEST_CASE("layout tracker: submission order wins over recording order") {
    LayoutTracker t;
    t.addCandidate(kGui);
    // Command buffer 2 (the later pass) is recorded first, on another thread.
    t.onBarrier(2, kGui, kShaderRead);
    t.onBarrier(1, kGui, kColor);
    const std::array<LayoutTracker::Handle, 2> cbs{1, 2};
    t.onSubmit(kQueueGraphics, cbs.data(), cbs.size());
    REQUIRE(t.stateOf(kGui).has_value());
    CHECK(t.stateOf(kGui)->layout == kShaderRead);
}

TEST_CASE("layout tracker: the last barrier in a command buffer counts") {
    LayoutTracker t;
    t.addCandidate(kGui);
    t.onBarrier(1, kGui, kColor);
    t.onBarrier(1, kGui, kShaderRead);
    const std::array<LayoutTracker::Handle, 1> cbs{1};
    t.onSubmit(kQueueCompute, cbs.data(), cbs.size());
    CHECK(t.stateOf(kGui)->layout == kShaderRead);
    CHECK(t.stateOf(kGui)->queue == kQueueCompute);
}

TEST_CASE("layout tracker: images that are not candidates are ignored") {
    LayoutTracker t;
    t.addCandidate(kGui);
    t.onBarrier(1, kOther, kColor);
    CHECK(t.pendingCount() == 0);
    const std::array<LayoutTracker::Handle, 1> cbs{1};
    t.onSubmit(kQueueGraphics, cbs.data(), cbs.size());
    CHECK_FALSE(t.stateOf(kOther).has_value());
    CHECK_FALSE(t.stateOf(kGui).has_value());
}

TEST_CASE("layout tracker: re-recording a command buffer drops what it held") {
    LayoutTracker t;
    t.addCandidate(kGui);
    t.onBarrier(1, kGui, kColor);
    t.onBegin(1);
    const std::array<LayoutTracker::Handle, 1> cbs{1};
    t.onSubmit(kQueueGraphics, cbs.data(), cbs.size());
    CHECK_FALSE(t.stateOf(kGui).has_value());
}

TEST_CASE("layout tracker: a later submit without a barrier keeps the layout") {
    LayoutTracker t;
    t.addCandidate(kGui);
    t.onBarrier(1, kGui, kColor);
    const std::array<LayoutTracker::Handle, 1> first{1};
    t.onSubmit(kQueueGraphics, first.data(), first.size());
    const std::array<LayoutTracker::Handle, 2> later{7, 8};
    t.onSubmit(kQueueGraphics, later.data(), later.size());
    CHECK(t.stateOf(kGui)->layout == kColor);
}

TEST_CASE("layout tracker: secondaries pass their layout to the primary") {
    LayoutTracker t;
    t.addCandidate(kGui);
    t.onBarrier(11, kGui, kColor);
    t.onBarrier(12, kGui, kShaderRead);
    const std::array<LayoutTracker::Handle, 2> secondaries{11, 12};
    t.onExecute(1, secondaries.data(), secondaries.size());
    const std::array<LayoutTracker::Handle, 1> cbs{1};
    t.onSubmit(kQueueGraphics, cbs.data(), cbs.size());
    CHECK(t.stateOf(kGui)->layout == kShaderRead);
}

TEST_CASE("layout tracker: a removed candidate is forgotten, a re-added one starts unknown") {
    LayoutTracker t;
    t.addCandidate(kGui);
    t.onBarrier(1, kGui, kColor);
    const std::array<LayoutTracker::Handle, 1> cbs{1};
    t.onSubmit(kQueueGraphics, cbs.data(), cbs.size());
    t.removeCandidate(kGui);
    CHECK_FALSE(t.isCandidate(kGui));
    CHECK_FALSE(t.stateOf(kGui).has_value());
    t.addCandidate(kGui); // a new image with the same handle value
    CHECK_FALSE(t.stateOf(kGui).has_value());
}

TEST_CASE("layout tracker: at most kMaxCandidates images") {
    LayoutTracker t;
    for (LayoutTracker::Handle i = 1; i <= LayoutTracker::kMaxCandidates; ++i) {
        CHECK(t.addCandidate(i));
    }
    CHECK_FALSE(t.addCandidate(LayoutTracker::kMaxCandidates + 1));
    t.removeCandidate(5);
    CHECK(t.addCandidate(LayoutTracker::kMaxCandidates + 1));
    CHECK_FALSE(t.addCandidate(0));
}

TEST_CASE("layout tracker: pending entries are bounded") {
    LayoutTracker t;
    t.addCandidate(kGui);
    for (LayoutTracker::Handle cb = 1; cb <= LayoutTracker::kMaxPending + 10; ++cb) {
        t.onBarrier(cb, kGui, kColor);
    }
    CHECK(t.pendingCount() <= LayoutTracker::kMaxPending);
}
