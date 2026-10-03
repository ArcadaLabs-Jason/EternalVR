#include "features/arm/arm_surfaces.hpp"

#include <doctest/doctest.h>

#include <string>

using evr::arm::armKitName;
using evr::arm::ArmSide;
using evr::arm::armSurfaceNames;
using evr::arm::equalsIgnoringCase;
using evr::arm::kMaxSurfaces;
using evr::arm::planSurface;
using evr::arm::surfaceBit;
using evr::arm::surfaceNameIs;
using evr::arm::SurfacePlan;
using evr::arm::SurfaceStep;

TEST_CASE("each arm's surfaces and kit are praetor.md6's") {
    REQUIRE(armSurfaceNames(ArmSide::Right).size() == 1);
    CHECK(std::string(armSurfaceNames(ArmSide::Right)[0]) == "arm_low_rt_base");
    REQUIRE(armSurfaceNames(ArmSide::Left).size() == 3);
    CHECK(std::string(armSurfaceNames(ArmSide::Left)[0]) == "arm_low_lf_base");
    CHECK(std::string(armSurfaceNames(ArmSide::Left)[1]) == "armor_hand_01_low_lf_base");
    CHECK(std::string(armSurfaceNames(ArmSide::Left)[2]) == "armor_hand_02_low_lf_base");
    CHECK(std::string(armKitName(ArmSide::Right)) == "ArmRight");
    CHECK(std::string(armKitName(ArmSide::Left)) == "ArmLeft");
}

TEST_CASE("surface names compare like FindSurfaces: up to a '$', ignoring case") {
    CHECK(surfaceNameIs("arm_low_rt_base", "arm_low_rt_base"));
    CHECK(surfaceNameIs("ARM_Low_RT_Base", "arm_low_rt_base"));
    CHECK(surfaceNameIs("arm_low_rt_base$models/characters/doomslayer_1p/doomslayer_1p_right",
                        "arm_low_rt_base"));
    CHECK(surfaceNameIs("arm_low_rt_base", "arm_low_rt_base$anything"));
    CHECK_FALSE(surfaceNameIs("arm_low_lf_base", "arm_low_rt_base"));
    CHECK_FALSE(surfaceNameIs("arm_low_rt_base_extra", "arm_low_rt_base"));
    CHECK_FALSE(surfaceNameIs("arm_low_rt", "arm_low_rt_base"));
    CHECK_FALSE(surfaceNameIs("", "arm_low_rt_base"));
    CHECK_FALSE(surfaceNameIs("$arm_low_rt_base", "arm_low_rt_base"));
}

TEST_CASE("kit names compare whole, ignoring case") {
    CHECK(equalsIgnoringCase("ArmRight", "armright"));
    CHECK_FALSE(equalsIgnoringCase("ArmRight", "ArmRigh"));
    CHECK_FALSE(equalsIgnoringCase("ArmLeft", "ArmRight"));
}

TEST_CASE("a surface's bit: dword index >> 5, bit index & 31, 128 surfaces") {
    REQUIRE(surfaceBit(0));
    CHECK(surfaceBit(0)->word == 0);
    CHECK(surfaceBit(0)->mask == 1u);
    REQUIRE(surfaceBit(1));
    CHECK(surfaceBit(1)->mask == 2u);
    REQUIRE(surfaceBit(31));
    CHECK(surfaceBit(31)->word == 0);
    CHECK(surfaceBit(31)->mask == 0x80000000u);
    REQUIRE(surfaceBit(32));
    CHECK(surfaceBit(32)->word == 1);
    CHECK(surfaceBit(32)->mask == 1u);
    REQUIRE(surfaceBit(kMaxSurfaces - 1));
    CHECK(surfaceBit(kMaxSurfaces - 1)->word == 3);
    CHECK_FALSE(surfaceBit(kMaxSurfaces));
    CHECK_FALSE(surfaceBit(-1));
}

namespace {

// One surface over a run of ticks: the layer's step on the bit, the game's kit changing it in between.
struct Model {
    bool visible = false;
    bool ours = false;
    int shows = 0;
    int hides = 0;

    void tick(bool posed) {
        const SurfacePlan plan = planSurface(posed, visible, ours);
        if (plan.step == SurfaceStep::Show) {
            visible = true;
            ++shows;
        } else if (plan.step == SurfaceStep::Hide) {
            visible = false;
            ++hides;
        }
        ours = plan.ours;
    }
};

} // namespace

TEST_CASE("posed: a surface the kit hides is shown once and is the layer's") {
    Model m; // ArmLeft: the right arm hidden
    m.tick(true);
    CHECK(m.visible);
    CHECK(m.ours);
    m.tick(true);
    m.tick(true);
    CHECK(m.shows == 1);
    CHECK(m.hides == 0);
}

TEST_CASE("handed back: only what the layer showed is hidden again, once") {
    Model m;
    m.tick(true);
    m.tick(false);
    CHECK_FALSE(m.visible);
    CHECK_FALSE(m.ours);
    m.tick(false);
    CHECK(m.hides == 1);
}

TEST_CASE("a surface the game's kit shows is never touched") {
    Model m;
    m.visible = true; // fists, chainsaw, melee (BaseSimple): both arms
    m.tick(true);
    m.tick(false);
    m.tick(true);
    CHECK(m.visible);
    CHECK_FALSE(m.ours);
    CHECK(m.shows == 0);
    CHECK(m.hides == 0);
}

TEST_CASE("a kit applied while posed hides the arm again: shown again on the next tick") {
    Model m;
    m.tick(true);
    m.visible = false; // a weapon switch re-applies ArmLeft
    m.tick(true);
    CHECK(m.visible);
    CHECK(m.ours);
    CHECK(m.shows == 2);
}

TEST_CASE("a kit that hid the arm before the hand-back leaves nothing to hide") {
    Model m;
    m.tick(true);
    m.visible = false; // the game hid it itself
    m.tick(false);
    CHECK_FALSE(m.ours);
    CHECK(m.hides == 0);
}

TEST_CASE("the plan's table") {
    CHECK(planSurface(true, false, false).step == SurfaceStep::Show);
    CHECK(planSurface(true, false, false).ours);
    CHECK(planSurface(true, true, false).step == SurfaceStep::None);
    CHECK_FALSE(planSurface(true, true, false).ours);
    CHECK(planSurface(true, true, true).ours);
    CHECK(planSurface(false, true, true).step == SurfaceStep::Hide);
    CHECK_FALSE(planSurface(false, true, true).ours);
    CHECK(planSurface(false, false, true).step == SurfaceStep::None);
    CHECK(planSurface(false, true, false).step == SurfaceStep::None);
    CHECK(planSurface(false, false, false).step == SurfaceStep::None);
}
