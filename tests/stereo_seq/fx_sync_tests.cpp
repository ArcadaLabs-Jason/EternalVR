#include "stereo_seq/fx_sync.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>
#include <optional>

using evr::stereo_seq::Eye;
using evr::stereo_seq::FxAction;
using evr::stereo_seq::fxActionFor;
using evr::stereo_seq::fxGenerationFor;
using evr::stereo_seq::fxGpuStages;
using evr::stereo_seq::fxPoolResetFor;
using evr::stereo_seq::fxPreviousSlot;
using evr::stereo_seq::FxSyncMode;
using evr::stereo_seq::fxSyncMode;

TEST_CASE("fx sync: the switch") {
    CHECK(fxSyncMode("1") == FxSyncMode::On);
    CHECK(fxSyncMode("on") == FxSyncMode::On);
    CHECK(fxSyncMode(" On ") == FxSyncMode::On);
    CHECK(fxSyncMode("TRUE") == FxSyncMode::On);
    CHECK(fxSyncMode("yes") == FxSyncMode::On);
    CHECK(fxSyncMode("Count") == FxSyncMode::Count);
    CHECK(fxSyncMode("\tcount\n") == FxSyncMode::Count);
    // Unset and anything else is off.
    CHECK(fxSyncMode("") == FxSyncMode::Off);
    CHECK(fxSyncMode("0") == FxSyncMode::Off);
    CHECK(fxSyncMode("off") == FxSyncMode::Off);
    CHECK(fxSyncMode("2") == FxSyncMode::Off);
    CHECK(fxSyncMode("o n") == FxSyncMode::Off);
    CHECK(fxSyncMode("counting") == FxSyncMode::Off);
}

TEST_CASE("fx sync: only eye R leaves the ring and the generation to eye L") {
    CHECK(fxActionFor(FxSyncMode::On, Eye::Right, true) == FxAction::UseEyeL);
    // Eye L of a pair and every render outside one (the engine's own chain).
    CHECK(fxActionFor(FxSyncMode::On, Eye::Left, true) == FxAction::Run);
}

TEST_CASE("fx sync: the multiplayer guard keeps the engine's code") {
    CHECK(fxActionFor(FxSyncMode::On, Eye::Right, false) == FxAction::Run);
    CHECK(fxActionFor(FxSyncMode::Count, Eye::Right, false) == FxAction::Run);
}

TEST_CASE("fx sync: off changes nothing, counting counts eye R only") {
    CHECK(fxActionFor(FxSyncMode::Off, Eye::Left, true) == FxAction::Run);
    CHECK(fxActionFor(FxSyncMode::Off, Eye::Right, true) == FxAction::Run);
    CHECK(fxActionFor(FxSyncMode::Count, Eye::Right, true) == FxAction::RunCounted);
    CHECK(fxActionFor(FxSyncMode::Count, Eye::Left, true) == FxAction::Run);
}

TEST_CASE("fx sync: the light pool's reset follows the ring in the same prepare") {
    CHECK(fxPoolResetFor(FxAction::UseEyeL) == FxAction::UseEyeL); // eye R keeps eye L's count
    CHECK(fxPoolResetFor(FxAction::RunCounted) == FxAction::RunCounted);
    CHECK(fxPoolResetFor(FxAction::Run) == FxAction::Run);
    // No ring hook acted in this prepare (no ring, or the hooks went live between the two sites): the reset.
    CHECK(fxPoolResetFor(std::nullopt) == FxAction::Run);
}

TEST_CASE("fx sync: eye R reuses only what eye L generated in this tick") {
    const std::uint32_t frame = 500;
    CHECK(fxGenerationFor(FxAction::UseEyeL, frame, frame, false) == FxAction::UseEyeL);
    // Eye L did not have the model: generated the render before (eye R binds it, then generates), long ago,
    // or never.
    CHECK(fxGenerationFor(FxAction::UseEyeL, frame - 1, frame, false) == FxAction::Run);
    CHECK(fxGenerationFor(FxAction::UseEyeL, frame - 40, frame, false) == FxAction::Run);
    CHECK(fxGenerationFor(FxAction::UseEyeL, 0, frame, false) == FxAction::Run);
    // The ring advanced in this eye R render (the hooks went live after its prepare): no stamp is the frame.
    CHECK(fxGenerationFor(FxAction::UseEyeL, frame, frame + 1, false) == FxAction::Run);
}

TEST_CASE("fx sync: eye R binds the GPU stages of what eye L generated and generates it itself") {
    const std::uint32_t frame = 500;
    CHECK(fxGenerationFor(FxAction::UseEyeL, frame, frame, true) == FxAction::BindGpuThenRun);
    // A model eye L did not generate in this tick runs the engine's code, its whole bind included.
    CHECK(fxGenerationFor(FxAction::UseEyeL, frame - 1, frame, true) == FxAction::Run);
    CHECK(fxGenerationFor(FxAction::UseEyeL, 0, frame, true) == FxAction::Run);
    CHECK(fxGenerationFor(FxAction::UseEyeL, frame, frame + 1, true) == FxAction::Run);
    // Counting changes nothing: it is counted like any other model eye L generated.
    CHECK(fxGenerationFor(FxAction::RunCounted, frame - 1, frame, true) == FxAction::RunCounted);
    CHECK(fxGenerationFor(FxAction::RunCounted, frame, frame, true) == FxAction::Run);
    // The engine's renders run every generation.
    CHECK(fxGenerationFor(FxAction::Run, frame, frame, true) == FxAction::Run);
}

TEST_CASE("fx sync: the GPU stage switch") {
    CHECK(fxGpuStages(""));
    CHECK(fxGpuStages("1"));
    CHECK(fxGpuStages(" on "));
    CHECK(fxGpuStages("2"));
    CHECK_FALSE(fxGpuStages("0"));
    CHECK_FALSE(fxGpuStages(" Off"));
    CHECK_FALSE(fxGpuStages("FALSE"));
    CHECK_FALSE(fxGpuStages("no\n"));
}

TEST_CASE("fx sync: counting, eye R's render advanced the ring") {
    const std::uint32_t frame = 501;
    CHECK(fxGenerationFor(FxAction::RunCounted, frame - 1, frame, false) == FxAction::RunCounted);
    CHECK(fxGenerationFor(FxAction::RunCounted, frame, frame, false) == FxAction::Run);
    CHECK(fxGenerationFor(FxAction::RunCounted, frame - 2, frame, false) == FxAction::Run);
    // The frame counter wraps.
    CHECK(fxGenerationFor(FxAction::RunCounted, 0xFFFFFFFFu, 0, false) == FxAction::RunCounted);
}

TEST_CASE("fx sync: the engine's renders run every generation") {
    for (const std::uint32_t stamp : {0u, 499u, 500u}) {
        CHECK(fxGenerationFor(FxAction::Run, stamp, 500, false) == FxAction::Run);
    }
}

TEST_CASE("fx sync: the previous slot is the one before the current, as the engine computes it") {
    CHECK(fxPreviousSlot(0) == 2);
    CHECK(fxPreviousSlot(1) == 0);
    CHECK(fxPreviousSlot(2) == 1);
}
