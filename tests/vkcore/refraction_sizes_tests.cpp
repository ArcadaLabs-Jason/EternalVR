#include "vkcore/refraction_sizes.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace rs = evr::vkcore::refraction_sizes;

namespace {

// An image object as far as the engine's resize goes: width, height, depth and mip count at their offsets.
struct FakeImage {
    std::array<std::byte, 0x80> bytes{};
    FakeImage(std::int32_t width, std::int32_t height) {
        set(rs::kImageWidth, width);
        set(rs::kImageHeight, height);
        set(rs::kImageDepth, 1);
        set(rs::kImageMips, 1);
    }
    void set(std::size_t at, std::int32_t v) { std::memcpy(bytes.data() + at, &v, sizeof(v)); }
};

int g_resizeCalls = 0;
int g_failingCalls = 0;

// A resize that changes nothing (as the engine's when it fails).
bool failingResize(void*, int, int, int, int) {
    ++g_failingCalls;
    return false;
}

// As the engine's resize (RVA 0x1C4AE30): writes the extent, false when it already had it.
bool fakeResize(void* image, int width, int height, int depth, int mips) {
    ++g_resizeCalls;
    auto* img = static_cast<FakeImage*>(image);
    if (rs::extentOf(img) == rs::Extent{width, height, depth, mips}) {
        return false;
    }
    img->set(rs::kImageWidth, width);
    img->set(rs::kImageHeight, height);
    img->set(rs::kImageDepth, depth);
    img->set(rs::kImageMips, mips);
    return true;
}

// The rig's sizes: the engine's images resized for a 2048x2208 render (accumulation 1024x1104, mask tiles
// 64x69), eye R's still at the start-up window's (594x566 and 74x70).
struct RigSets {
    std::array<FakeImage, 2> engineMask{FakeImage(64, 69), FakeImage(64, 69)};
    std::array<FakeImage, 4> engineAcc{FakeImage(1024, 1104), FakeImage(1024, 1104), FakeImage(1024, 1104),
                                       FakeImage(1024, 1104)};
    std::array<FakeImage, 2> eyeRMask{FakeImage(74, 70), FakeImage(74, 70)};
    std::array<FakeImage, 4> eyeRAcc{FakeImage(594, 566), FakeImage(594, 566), FakeImage(594, 566),
                                     FakeImage(594, 566)};

    rs::ImagePairs pairs() {
        const rs::MaskImages em = {&engineMask[0], &engineMask[1]};
        const rs::AccImages ea = {{{&engineAcc[0], &engineAcc[1]}, {&engineAcc[2], &engineAcc[3]}}};
        const rs::MaskImages rm = {&eyeRMask[0], &eyeRMask[1]};
        const rs::AccImages ra = {{{&eyeRAcc[0], &eyeRAcc[1]}, {&eyeRAcc[2], &eyeRAcc[3]}}};
        return rs::pairsOf(em, ea, rm, ra);
    }
};

} // namespace

TEST_CASE("refraction sizes: eye R's images follow the engine's after a render-size change") {
    RigSets sets;
    const rs::ImagePairs pairs = sets.pairs();
    CHECK_FALSE(rs::sameExtents(pairs));
    g_resizeCalls = 0;
    const rs::FollowResult r = rs::followEngine(pairs, &fakeResize);
    CHECK(r.changed == 6);
    CHECK(r.failed == 0);
    CHECK(r.firstFailed == -1);
    CHECK(g_resizeCalls == 6);
    CHECK(rs::sameExtents(pairs));
    for (const FakeImage& img : sets.eyeRAcc) {
        CHECK(rs::extentOf(&img) == rs::Extent{1024, 1104, 1, 1});
    }
    for (const FakeImage& img : sets.eyeRMask) {
        CHECK(rs::extentOf(&img) == rs::Extent{64, 69, 1, 1});
    }
    // The engine's own are left as they are, and a second resize at the same size changes nothing.
    CHECK(rs::extentOf(&sets.engineAcc[0]) == rs::Extent{1024, 1104, 1, 1});
    g_resizeCalls = 0;
    CHECK(rs::followEngine(pairs, &fakeResize).changed == 0);
    CHECK(g_resizeCalls == 0);
}

TEST_CASE("refraction sizes: one image of another size is caught") {
    RigSets sets;
    const rs::ImagePairs pairs = sets.pairs();
    rs::followEngine(pairs, &fakeResize);
    REQUIRE(rs::sameExtents(pairs));
    sets.eyeRAcc[3].set(rs::kImageWidth, 1023);
    CHECK_FALSE(rs::sameExtents(pairs));
    sets.eyeRAcc[3].set(rs::kImageWidth, 1024);
    sets.eyeRMask[1].set(rs::kImageMips, 2);
    CHECK_FALSE(rs::sameExtents(pairs));
}

TEST_CASE("refraction sizes: the pairs are laid out per slot, mask tiles first, then the updates") {
    RigSets sets;
    const rs::ImagePairs p = sets.pairs();
    CHECK(p[0].engine == &sets.engineMask[0]);
    CHECK(p[0].eyeR == &sets.eyeRMask[0]);
    CHECK(p[1].engine == &sets.engineAcc[0]); // update 0, slot 0
    CHECK(p[2].engine == &sets.engineAcc[2]); // update 1, slot 0
    CHECK(p[2].eyeR == &sets.eyeRAcc[2]);
    CHECK(p[3].engine == &sets.engineMask[1]);
    CHECK(p[4].engine == &sets.engineAcc[1]); // update 0, slot 1
    CHECK(p[5].eyeR == &sets.eyeRAcc[3]);     // update 1, slot 1
}

TEST_CASE("refraction sizes: a missing image is never resized and never the same size") {
    FakeImage engine(1024, 1104);
    rs::ImagePairs pairs{};
    pairs[0] = {&engine, nullptr};
    g_resizeCalls = 0;
    CHECK(rs::followEngine(pairs, &fakeResize).changed == 0);
    CHECK(g_resizeCalls == 0);
    CHECK_FALSE(rs::sameExtents(pairs));
    CHECK(rs::extentOf(nullptr) == rs::Extent{});
}

TEST_CASE(
    "refraction sizes: a resize that fails is reported, and eye R renders hold off until a retry works") {
    RigSets sets;
    const rs::ImagePairs pairs = sets.pairs();
    g_failingCalls = 0;
    const rs::FollowResult failed = rs::followEngine(pairs, &failingResize);
    CHECK(failed.changed == 0);
    CHECK(failed.failed == 6);
    CHECK(failed.firstFailed == 0);
    CHECK(g_failingCalls == 6);

    // Eye R's renders blend with no history while the sizes differ; every pass of a render gets one answer.
    // The first such render starts both eyes over (HoldOffFirst), and the other renders hold off with it.
    rs::SizeGuard guard;
    CHECK_FALSE(guard.mismatched());
    CHECK(guard.atRender(100, rs::sameExtents(pairs)) == rs::SizeCheck::HoldOffFirst);
    CHECK(guard.atRender(100, rs::sameExtents(pairs)) == rs::SizeCheck::HoldOffFirst);
    CHECK(guard.mismatched());
    CHECK(guard.atRender(102, rs::sameExtents(pairs)) == rs::SizeCheck::HoldOff);
    CHECK(guard.mismatched());
    // The next render-size change retries with the engine's resize, which works: the history goes on.
    CHECK(rs::followEngine(pairs, &fakeResize).changed == 6);
    CHECK(guard.atRender(104, rs::sameExtents(pairs)) == rs::SizeCheck::Same);
    CHECK_FALSE(guard.mismatched());

    // Sizes that stay different fail closed only after kMismatchedRenders renders in a row.
    sets.eyeRAcc[0].set(rs::kImageWidth, 594);
    std::uint32_t render = 200;
    CHECK(guard.atRender(render, rs::sameExtents(pairs)) == rs::SizeCheck::HoldOffFirst);
    for (int i = 1; i < rs::kMismatchedRenders; ++i) {
        CHECK(guard.atRender(render += 2, rs::sameExtents(pairs)) == rs::SizeCheck::HoldOff);
    }
    CHECK(guard.atRender(render += 2, rs::sameExtents(pairs)) == rs::SizeCheck::FailClosed);
    // A new device context's images: the guard starts over.
    guard.reset();
    CHECK(guard.atRender(render += 2, rs::sameExtents(pairs)) == rs::SizeCheck::HoldOffFirst);
}

TEST_CASE("refraction sizes: a pass of an older render changes nothing") {
    RigSets sets;
    const rs::ImagePairs pairs = sets.pairs();
    rs::SizeGuard guard;
    CHECK(guard.atRender(0xFFFFFFFEu, rs::sameExtents(pairs)) == rs::SizeCheck::HoldOffFirst);
    CHECK(guard.atRender(0u, rs::sameExtents(pairs)) == rs::SizeCheck::HoldOff); // the counter wrapped: newer
    // A straggler of the render before: held off (the sizes differ), and the count of renders in a row and
    // the last render's answer stay as they were.
    CHECK(guard.atRender(0xFFFFFFFEu, rs::sameExtents(pairs)) == rs::SizeCheck::HoldOff);
    CHECK(guard.atRender(0u, rs::sameExtents(pairs)) == rs::SizeCheck::HoldOff);
    rs::followEngine(pairs, &fakeResize);
    CHECK(guard.atRender(0xFFFFFFFEu, rs::sameExtents(pairs)) == rs::SizeCheck::Same); // sizes match now
    CHECK(guard.mismatched()); // still the last render's
    CHECK(guard.atRender(2u, rs::sameExtents(pairs)) == rs::SizeCheck::Same);
    CHECK_FALSE(guard.mismatched());
}
