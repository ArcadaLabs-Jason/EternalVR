#include "features/arm/frame_pin.hpp"

#include "features/arm/drawn_wrist.hpp"
#include "xr_math/head_aim.hpp"
#include "xr_math/offhand_pose.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using evr::Vec3;
using evr::arm::checkPin;
using evr::arm::drawnWrist;
using evr::arm::FramePin;
using evr::arm::FramePlacement;
using evr::arm::PinCheck;
using evr::arm::PinInputs;
using evr::arm::PinTally;
using evr::arm::placesAhead;
using evr::arm::PosedWrist;
using evr::arm::ReadWrist;
using evr::arm::samePlacement;
using evr::xr_math::axisFromAngles;
using evr::xr_math::EyeRelativePose;
using evr::xr_math::inModelSpace;

namespace {

constexpr float kUpm = 39.37f;
const Vec3 kEye{800.0f, 120.0f, 64.0f};
const int kHands = 0;
const int kShotgun = 1;
const int kCannon = 2;
constexpr std::int32_t kFrames = 41; // ends a frame after a step, so both frames of the last one count

// The arms model as the viewmodel hook places it at the weapon hand in frame f for the held item: the weapon
// hand steps 15 degrees and 2 cm in one frame every 4 frames (a fast flick's frame), there and back; the
// cannon's row sits 5 cm further ahead.
EyeRelativePose placementOf(std::int32_t f, const void* item = &kShotgun) {
    const bool turned = (f / 4) % 2 == 1;
    const float ahead = item == &kCannon ? 0.05f : 0.0f;
    return {Vec3{0.30f + ahead, -0.25f + (turned ? 0.02f : 0.0f), -0.30f} * kUpm,
            axisFromAngles({0.0f, turned ? 15.0f : 0.0f, 0.0f})};
}

// The off hand's controller, held still: its wrist target relative to the eye.
const EyeRelativePose kOffHand{Vec3{0.35f, 0.20f, -0.25f} * kUpm, axisFromAngles({0.0f, 0.0f, 0.0f})};

struct Hooks {
    bool offHandFirst = true; // the off-hand hook runs before the viewmodel hook in a frame (the rig's order)
    bool pinning = true; // the off hand poses under the frame's placement (false: under the last written)
    std::int32_t switchAt = 0; // the held item changes to the cannon in this frame (0: never)
};

struct Run {
    int off = 0; // frames whose glove is drawn more than 1 cm from its still controller
    std::vector<PinCheck> checks;
    bool gunOwn = true; // the viewmodel hook wrote its own placement in every frame
};

// Frames 1..kFrames as the game runs them (rig, docs/rig-findings/hands-rig.md section 26): the hooks in
// their order; the viewmodel hook reads the held item and places the model itself every frame, checking the
// pin; the off-hand hook knows the held item as the viewmodel hook read it last (the frame before); the off
// hand posed with its wrist where the controller puts it under the placement it posed under, written exactly,
// and drawn in the next frame (the skeleton a frame draws carries the frame before's writes) under that
// frame's placement.
Run run(const Hooks& hooks) {
    const auto itemOf = [&](std::int32_t f) -> const void* {
        return hooks.switchAt != 0 && f >= hooks.switchAt ? &kCannon : &kShotgun;
    };
    std::int32_t placedIn = 0; // idHands +0xFC0: the frame the hands were last placed in
    const void* itemRead = itemOf(0);
    EyeRelativePose lastWritten = placementOf(0, itemRead);
    FramePin<std::pair<PinInputs, EyeRelativePose>> pin;
    std::vector<FramePlacement> written;
    std::optional<PosedWrist> last;
    Run out;
    for (std::int32_t f = 1; f <= kFrames; ++f) {
        std::optional<PosedWrist> posed;
        const auto viewmodel = [&] {
            placedIn = f;
            itemRead = itemOf(f);
            const EyeRelativePose own = placementOf(f, itemRead);
            if (const auto pinned = pin.take(&kHands, f)) {
                out.checks.push_back(checkPin(pinned->first, pinned->second, PinInputs{itemRead}, own));
            }
            lastWritten = own;
            out.gunOwn = out.gunOwn && samePlacement(own, placementOf(f, itemOf(f)));
            written.push_back({f, kEye, lastWritten});
        };
        const auto offHand = [&] {
            EyeRelativePose under = lastWritten;
            if (hooks.pinning && placesAhead(f, placedIn)) {
                under = placementOf(f, itemRead);
                pin.pin(&kHands, f, {PinInputs{itemRead}, under});
            } else {
                pin.clear();
            }
            posed = PosedWrist{f, kEye, under, inModelSpace(under, kOffHand).position, 0.0, 0.0};
        };
        if (hooks.offHandFirst) {
            offHand();
            viewmodel();
        } else {
            viewmodel();
            offHand();
        }
        // This frame draws the arm written in the frame before (read here as written there).
        if (last) {
            const auto d = drawnWrist(*last, written, ReadWrist{f, last->wrist}, kUpm);
            REQUIRE(d.has_value());
            const Vec3 controller = (kEye + kOffHand.offset) * (1.0f / kUpm);
            out.off += length(d->drawn - controller) > 0.01f ? 1 : 0;
        }
        last = posed;
    }
    return out;
}

// Steps of the weapon hand within frames 2..kFrames (the placement changes from one frame to the next).
int steps() {
    int n = 0;
    for (std::int32_t f = 2; f <= kFrames; ++f) {
        n += (f / 4) != ((f - 1) / 4) ? 1 : 0;
    }
    return n;
}

int countOf(const std::vector<PinCheck>& checks, PinCheck check) {
    int n = 0;
    for (const PinCheck c : checks) {
        n += c == check ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("the off hand poses ahead only in a frame its hands have not been placed in yet") {
    CHECK(placesAhead(41, 40));
    CHECK_FALSE(placesAhead(41, 41));
    CHECK_FALSE(placesAhead(std::nullopt, 40));
    CHECK_FALSE(placesAhead(41, std::nullopt));
}

TEST_CASE("a pinned placement is taken once, in its own frame, for its own hands") {
    FramePin<int> pin;
    const int hands = 0;
    const int other = 0;
    pin.pin(&hands, 7, 42);
    CHECK_FALSE(pin.take(&hands, 8).has_value()); // the next frame: not this one's, and gone
    CHECK_FALSE(pin.take(&hands, 7).has_value());
    pin.pin(&hands, 7, 42);
    CHECK_FALSE(pin.take(&other, 7).has_value());
    pin.pin(&hands, 7, 42);
    CHECK_FALSE(pin.take(&hands, std::nullopt).has_value());
    pin.pin(&hands, 7, 42);
    REQUIRE(pin.take(&hands, 7).has_value());
    CHECK_FALSE(pin.take(&hands, 7).has_value());
    pin.pin(&hands, 7, 42);
    pin.clear();
    CHECK_FALSE(pin.take(&hands, 7).has_value());
}

TEST_CASE(
    "with the off-hand hook first in a frame, a step of the gun moves the still glove one frame, not two") {
    // Posed under the last frame's placement (dev main to 0.1.87): the glove is drawn off for two frames at
    // every step of the weapon hand, as the rig's captures showed.
    CHECK(run({true, false}).off == 2 * steps());
    // Posed under the frame's own placement: one frame, the frame after the step (the game draws the arm a
    // frame after it is written). Every pin the same as the gun's own placement.
    const Run pinned = run({true, true});
    CHECK(pinned.off == steps());
    CHECK(pinned.gunOwn);
    CHECK(countOf(pinned.checks, PinCheck::Same) == kFrames);
}

TEST_CASE("with the viewmodel hook first in a frame, its placement is the frame's: nothing is pinned") {
    CHECK(run({false, false}).off == steps());
    const Run first = run({false, true});
    CHECK(first.off == steps());
    CHECK(first.checks.empty());
}

TEST_CASE("on a switch the gun takes its own placement and the frame the pin missed is counted") {
    // The held item changes in frame 22 (no step near it): the off-hand hook still knows the shotgun there,
    // the viewmodel hook places the cannon's row. The gun is its own in every frame; that frame's pin differs
    // by the item. The glove is drawn off in the frame of the change (the arm posed for the frame before, as
    // at any change of the placement) and, for the missed pin, in the one after it: two frames, as on dev
    // main.
    const Run r = run({true, true, 22});
    CHECK(r.gunOwn);
    CHECK(countOf(r.checks, PinCheck::Item) == 1);
    CHECK(countOf(r.checks, PinCheck::Same) == kFrames - 1);
    CHECK(r.off == steps() + 2);
}

TEST_CASE("a pin is checked by its placement, and a differing one says which input changed") {
    const EyeRelativePose p{{1.0f, 2.0f, 3.0f}, axisFromAngles({0.0f, 20.0f, 0.0f})};
    EyeRelativePose q = p;
    q.offset.x += 1e-6f; // under the tolerance
    const PinInputs in{&kShotgun, 100, nullptr, 0.0f, {}};
    CHECK(checkPin(in, p, in, q) == PinCheck::Same);
    q.offset.x += 0.01f;
    CHECK(checkPin(in, p, PinInputs{&kCannon, 100, nullptr, 0.0f, {}}, q) == PinCheck::Item);
    CHECK(checkPin(in, p, PinInputs{&kShotgun, 101, nullptr, 0.0f, {}}, q) == PinCheck::Hands);
    CHECK(checkPin(in, p, PinInputs{&kShotgun, 100, &kShotgun, 1.0f, {}}, q) == PinCheck::LeftGlove);
    CHECK(checkPin(in, p, PinInputs{&kShotgun, 100, nullptr, 0.0f, {0.0f, 0.01f, 0.0f}}, q) ==
          PinCheck::LeftGlove);
    CHECK(checkPin(in, p, in, q) == PinCheck::Placement);
    EyeRelativePose turned = p;
    turned.axis = axisFromAngles({0.0f, 20.01f, 0.0f});
    CHECK_FALSE(samePlacement(p, turned));
}

TEST_CASE("the 10 s line counts each check") {
    PinTally t;
    CHECK(t.empty());
    t.note(PinCheck::Same);
    t.note(PinCheck::Same);
    t.note(PinCheck::Item);
    t.note(PinCheck::NotPlaced);
    const std::string line = t.line();
    CHECK(line.find("4 frame(s)") != std::string::npos);
    CHECK(line.find("the same 2") != std::string::npos);
    CHECK(line.find("another held item 1") != std::string::npos);
    CHECK(line.find("not placed 1") != std::string::npos);
    t.reset();
    CHECK(t.empty());
}
