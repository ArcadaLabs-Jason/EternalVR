#include "vkcore/clone_limits.hpp"
#include "vkcore/clone_stencil.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace cs = evr::vkcore::clone_stencil;

namespace {

constexpr std::uintptr_t kEngineView = 0x1000; // the device context's stencil view (dc+0x438)
constexpr std::uintptr_t kDepth = 0x8000;      // the device context's depth image (dc+0x230)
constexpr std::uintptr_t kClone = 0x2000;      // view 1's clone of it
constexpr std::uintptr_t kGpu = 0x3000;        // the clone's GPU image
constexpr std::uintptr_t kOwn = 0x4000;        // view 1's own handle

// The builds a process may make (clone_limits.hpp), one more to be sure.
constexpr std::size_t kBuildsPerProcess = evr::vkcore::clone_limits::kMaxBuildsPerProcess + 1;
static_assert(evr::vkcore::clone_limits::kMaxBuildsPerProcess >=
              evr::vkcore::clone_limits::kMaxBuildsPerProcessNewNames);

const cs::Own kMapOwn{kOwn, kEngineView, kClone, kGpu};

} // namespace

TEST_CASE("a build makes view 1 a stencil view only over its clone of the device context's depth image") {
    CHECK(cs::plan(true, kEngineView, kDepth, kDepth, kClone, kGpu) == cs::Plan::Make);
    // The device context's stencil view over another image (engineImage != dc+0x230): left as it is.
    CHECK(cs::plan(true, kEngineView, kDepth + 0x10, kDepth, kClone, kGpu) == cs::Plan::NotDepth);
    CHECK(cs::plan(true, kEngineView, 0, kDepth, kClone, kGpu) == cs::Plan::NotDepth);
    CHECK(cs::plan(true, kEngineView, 0, 0, kClone, kGpu) == cs::Plan::NotDepth);
    CHECK(cs::plan(true, 0, 0, kDepth, kClone, kGpu) == cs::Plan::NoView);
    CHECK(cs::plan(true, kEngineView, kDepth, kDepth, 0, kGpu) == cs::Plan::NoClone);
    CHECK(cs::plan(true, kEngineView, kDepth, kDepth, kClone, 0) == cs::Plan::NoClone);
    // Sites not as known or ETERNALVR_TEST_VIEW_CLONE_SKIP=stencil: nothing made.
    CHECK(cs::plan(false, kEngineView, kDepth, kDepth, kClone, kGpu) == cs::Plan::Off);
}

TEST_CASE("view 1's bind of the engine's stencil view takes the map's own, any other stays") {
    CHECK(cs::bindFor(kMapOwn, kEngineView, kEngineView, true, kGpu) == kOwn);
    CHECK(cs::bindFor(kMapOwn, 0x5000, kEngineView, true, kGpu) == 0);
    CHECK(cs::bindFor(kMapOwn, 0, kEngineView, true, kGpu) == 0);
    // The device context made a new handle since the build: the map's is not over its depth now.
    CHECK(cs::bindFor(kMapOwn, kEngineView, kEngineView + 0x10, true, kGpu) == 0);
    // The engine freed the clone and made it again (another GPU image): not viewed.
    CHECK(cs::bindFor(kMapOwn, kEngineView, kEngineView, true, kGpu + 0x10) == 0);
    // A map without a handle (none made, or the make failed): the bind stays.
    const cs::Own none{0, kEngineView, kClone, kGpu};
    CHECK(cs::bindFor(none, kEngineView, kEngineView, true, kGpu) == 0);
    CHECK(cs::bindFor(cs::Own{}, 0, 0, true, 0) == 0);
    // The clone's GPU image could not be read (its memory freed): the bind stays, whatever was read.
    CHECK(cs::bindFor(kMapOwn, kEngineView, kEngineView, false, kGpu) == 0);
    CHECK(cs::bindFor(kMapOwn, kEngineView, kEngineView, false, 0) == 0);
}

TEST_CASE("a failed image view is seen and released as the engine's release can take it") {
    CHECK(cs::viewMade(0, 0x6000, 0));
    CHECK_FALSE(cs::viewMade(0, 0, 0)); // vkCreateImageView failed: +8 left 0
    CHECK(cs::viewMade(cs::kImageSet, 0x7000, 0x6000));
    CHECK_FALSE(cs::viewMade(cs::kImageSet, 0x7000, 0)); // a set's first view not made
    CHECK_FALSE(cs::viewMade(cs::kImageSet, 0, 0));      // its list not made
    CHECK(cs::releasable(0, 0));                         // a null view queues nothing
    CHECK(cs::releasable(cs::kImageSet, 0x7000));
    CHECK_FALSE(cs::releasable(cs::kImageSet, 0)); // the release would read the list at 0
}

TEST_CASE("the release's stand-in carries the image flags at +0x9C, zeros elsewhere") {
    const cs::StandIn s = cs::standIn(0x12345178u);
    REQUIRE(s.size() == 0xA0);
    std::uint32_t flags = 0;
    std::memcpy(&flags, s.data() + 0x9C, sizeof(flags));
    CHECK(flags == 0x12345178u);
    CHECK((flags & cs::kImageSet) == cs::kImageSet);
    for (std::size_t i = 0; i < 0x9C; ++i) {
        CHECK(s[i] == std::byte{0});
    }
    CHECK(cs::standIn(0)[0x9D] == std::byte{0});
}

TEST_CASE(
    "every build's view waits for the engine's resize, however many builds; the resize takes them all") {
    cs::Ledger ledger;
    // More builds than a process may make, each with its view (no cap brings view 0's back).
    for (std::size_t i = 0; i < kBuildsPerProcess; ++i) {
        CHECK(cs::plan(true, kEngineView, kDepth, kDepth, kClone + 0x10 * i, kGpu) == cs::Plan::Make);
        ledger.add(kOwn + i, static_cast<std::uint32_t>(i));
    }
    CHECK(ledger.size() == kBuildsPerProcess);
    const std::vector<cs::Made> released = ledger.take();
    REQUIRE(released.size() == kBuildsPerProcess);
    CHECK(released.front().view == kOwn);
    CHECK(released.back().view == kOwn + kBuildsPerProcess - 1);
    CHECK(released.back().imageFlags == kBuildsPerProcess - 1);
    CHECK(ledger.size() == 0);
    CHECK(ledger.take().empty()); // a second resize releases nothing again
    ledger.add(kOwn, 0);
    CHECK(ledger.size() == 1);
}
