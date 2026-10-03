#include "features/foveation/eye_targets.hpp"

#include <doctest/doctest.h>

using evr::foveation::isEyeSpaceTarget;
using evr::foveation::TargetSize;

TEST_CASE("eye targets: the eye image and its half, quarter and eighth size buffers") {
    const TargetSize eye{1280, 720};
    CHECK(isEyeSpaceTarget({1280, 720}, eye));
    CHECK(isEyeSpaceTarget({640, 360}, eye));
    CHECK(isEyeSpaceTarget({320, 180}, eye));
    CHECK(isEyeSpaceTarget({160, 90}, eye));
    CHECK_FALSE(isEyeSpaceTarget({80, 45}, eye));
}

TEST_CASE("eye targets: odd sizes divide rounded down or up") {
    // Quest 3 at the runtime's ask (2064x2208) and at a scaled size with odd halves.
    CHECK(isEyeSpaceTarget({1032, 1104}, {2064, 2208}));
    const TargetSize eye{2057, 2217};
    CHECK(isEyeSpaceTarget({1028, 1108}, eye));
    CHECK(isEyeSpaceTarget({1029, 1109}, eye));
    CHECK(isEyeSpaceTarget({515, 555}, eye));
    CHECK_FALSE(isEyeSpaceTarget({1030, 1108}, eye));
}

TEST_CASE("eye targets: shadow maps, look-up tables and mixed factors are not eye space") {
    // Seen on the rig with foveation on and a 1280x720 eye image (run fov-a).
    const TargetSize eye{1280, 720};
    CHECK_FALSE(isEyeSpaceTarget({8192, 8192}, eye));
    CHECK_FALSE(isEyeSpaceTarget({2048, 2048}, eye));
    CHECK_FALSE(isEyeSpaceTarget({256, 256}, eye));
    CHECK_FALSE(isEyeSpaceTarget({1280, 360}, eye));
    CHECK_FALSE(isEyeSpaceTarget({2560, 1440}, eye));
    // A square eye image (Quest 3 is 2056x2216): the shadow atlas still does not match.
    CHECK_FALSE(isEyeSpaceTarget({8192, 8192}, {2056, 2216}));
    CHECK_FALSE(isEyeSpaceTarget({2048, 2048}, {2056, 2216}));
}

TEST_CASE("eye targets: nothing is eye space before the eye image's size is known") {
    CHECK_FALSE(isEyeSpaceTarget({1280, 720}, {}));
    CHECK_FALSE(isEyeSpaceTarget({}, {1280, 720}));
}

TEST_CASE("eye targets: the DLSS render sizes and their half size buffers") {
    // Quest 3 at 100% (2056x2216): Quality two thirds, Balanced 58%, Performance half, Ultra Performance a
    // third, each side rounded to the nearest pixel.
    const TargetSize eye{2056, 2216};
    // DLAA renders the scene at the eye image's own size (scale 1).
    CHECK(isEyeSpaceTarget({2056, 2216}, eye));
    CHECK(isEyeSpaceTarget({1371, 1477}, eye));
    CHECK(isEyeSpaceTarget({1192, 1285}, eye));
    CHECK(isEyeSpaceTarget({1028, 1108}, eye));
    CHECK(isEyeSpaceTarget({685, 739}, eye));
    // Quality's half and quarter size buffers, from the rounded render size, rounded down or up again.
    CHECK(isEyeSpaceTarget({685, 738}, eye));
    CHECK(isEyeSpaceTarget({686, 739}, eye));
    CHECK(isEyeSpaceTarget({342, 369}, eye));
    CHECK(isEyeSpaceTarget({343, 370}, eye));
    // The simulator's 1280x1400 eye under Quality (rig log), Balanced and Ultra Performance.
    const TargetSize simulator{1280, 1400};
    CHECK(isEyeSpaceTarget({853, 933}, simulator));
    CHECK(isEyeSpaceTarget({742, 812}, simulator));
    CHECK(isEyeSpaceTarget({427, 467}, simulator));
}

TEST_CASE("eye targets: any one scale from 1 down to 1/8, as a dynamic resolution gives") {
    const TargetSize eye{2056, 2216};
    CHECK(isEyeSpaceTarget({1748, 1884}, eye)); // 85%
    CHECK(isEyeSpaceTarget({1747, 1883}, eye));
    // An eighth with both sides rounded up; a tenth is below the game's smallest buffers.
    CHECK(isEyeSpaceTarget({258, 277}, eye));
    CHECK_FALSE(isEyeSpaceTarget({205, 221}, eye));
    // One pixel short on one side is rounding; larger than the eye image is not eye space.
    CHECK(isEyeSpaceTarget({2056, 2215}, eye));
    CHECK_FALSE(isEyeSpaceTarget({2057, 2216}, eye));
    CHECK_FALSE(isEyeSpaceTarget({2056, 2217}, eye));
}

TEST_CASE("eye targets: both sides must agree on the scale within a pixel") {
    const TargetSize simulator{1280, 1400};
    CHECK(isEyeSpaceTarget({853, 931}, simulator)); // both within a pixel at a scale of 0.6657
    CHECK_FALSE(isEyeSpaceTarget({853, 929}, simulator));
    CHECK_FALSE(isEyeSpaceTarget({853, 936}, simulator));
    CHECK_FALSE(isEyeSpaceTarget({856, 933}, simulator));
    CHECK_FALSE(isEyeSpaceTarget({1280, 1280}, simulator));
    CHECK_FALSE(isEyeSpaceTarget({1000, 1000}, simulator));
    CHECK_FALSE(isEyeSpaceTarget({300, 256}, simulator));
}

TEST_CASE("eye targets: power-of-two squares near a square eye image count only at exact fractions") {
    // A headset with square views at the render budget (2064x2064).
    const TargetSize square{2064, 2064};
    CHECK(isEyeSpaceTarget({1376, 1376}, square)); // DLSS Quality
    CHECK(isEyeSpaceTarget({1032, 1032}, square));
    CHECK(isEyeSpaceTarget({1500, 1500}, square)); // any other scale that is not a power of two
    CHECK_FALSE(isEyeSpaceTarget({2048, 2048}, square));
    CHECK_FALSE(isEyeSpaceTarget({1024, 1024}, square));
    CHECK_FALSE(isEyeSpaceTarget({512, 512}, square));
    CHECK_FALSE(isEyeSpaceTarget({256, 256}, square));
    CHECK_FALSE(isEyeSpaceTarget({8192, 8192}, square));
    // A power-of-two square eye image keeps itself and its exact halves.
    const TargetSize pow2{2048, 2048};
    CHECK(isEyeSpaceTarget({2048, 2048}, pow2));
    CHECK(isEyeSpaceTarget({1024, 1024}, pow2));
    CHECK(isEyeSpaceTarget({256, 256}, pow2));
    CHECK(isEyeSpaceTarget({1365, 1365}, pow2)); // DLSS Quality
    CHECK_FALSE(isEyeSpaceTarget({128, 128}, pow2));
}
