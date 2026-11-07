// Roll wrist on real and modelled motion (DECISIONS T-202). The owner's deliberate flicks are replayed from
// his headset session (wrist_flick_recorded.hpp) and still switch once. The rolls that the one-stroke flick
// up to 0.1.87 took for a flick in players' sessions are modelled from what was measured in them (roll rate
// and size, the roll back, the aim's swing, the pitch): no player's motion is stored here. Each of them
// switched the weapon mod with the one-stroke flick, and switches nothing now.

#include "features/input/wrist_flick.hpp"

#include "features/input/input_frames.hpp"
#include "features/input/input_mapper.hpp"
#include "features/input/wrist_flick_recorded.hpp"
#include "features/input/wrist_flick_rig.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <span>
#include <vector>

using evr::Quat;
using evr::game::contains;
using evr::game::GameAction;
using evr::input::FlickOutput;
using evr::input::Hand;
using evr::input::InputFrame;
using evr::input::InputMapper;
using evr::input::kFlickBackDegrees;
using evr::input::kFlickOverDegrees;
using evr::input::WristFlick;
using evr::test::RecordedSample;
using evr::test::restingFrame;
using evr::test::wrist::flickOn;
using evr::test::wrist::Rig;

namespace {

// The weapon hand at a recorded sample, the poses' own clock starting at 10 s; the hand still.
InputFrame frameAt(const RecordedSample& sample) {
    InputFrame frame = restingFrame();
    frame.right.aimPose.orientation = Quat{sample.x, sample.y, sample.z, sample.w};
    frame.sampleSeconds = 10.0 + static_cast<double>(sample.seconds);
    return frame;
}

// The recording through Roll wrist with the default settings: every frame that flicked.
std::vector<FlickOutput> flicks(std::span<const RecordedSample> samples) {
    WristFlick flick;
    std::vector<FlickOutput> fired;
    float previous = samples.front().seconds;
    for (const RecordedSample& sample : samples) {
        const float dt = sample.seconds > previous ? sample.seconds - previous : 1.0f / 90.0f;
        previous = sample.seconds;
        const FlickOutput out = flick.update(frameAt(sample), Hand::Right, dt);
        if (out.flicked) {
            fired.push_back(out);
        }
    }
    return fired;
}

} // namespace

TEST_CASE("modelled: a forearm turning one way as the player turns in the room is no flick") {
    // A player's 0.1.84 session with Roll wrist on, where it switched the weapon mod: the forearm turned
    // about 130 degrees one way over 0.8 s, fastest at about 460 degrees per second smoothed, the aim
    // swinging at about 195 as the player turned, the weapon level; back only about 25.
    Rig rig(flickOn(), -5.0f);
    int count = rig.rollBy(24.0f, 60.0f, -190.0f);
    count += rig.rollBy(90.0f, 600.0f, -190.0f);
    count += rig.rollBy(18.0f, 90.0f, -190.0f);
    count += rig.rollBy(-26.0f, 300.0f, -190.0f);
    count += rig.run(0.0f, 0.5f, 0.0f, rig.pitch);
    CHECK(count == 0);
}

TEST_CASE("modelled: the gun hand rolling with a fast turn round in the room is no flick") {
    // A player turning round with the head at about 170 degrees per second, the gun hand rolling about 35
    // degrees with the turn at up to 560 and back.
    Rig rig;
    int count = rig.rollBy(-35.0f, 700.0f, 170.0f);
    count += rig.rollBy(35.0f, 700.0f, 170.0f);
    count += rig.run(0.0f, 0.5f, 0.0f, rig.pitch);
    CHECK(count == 0);
}

TEST_CASE("modelled: a hand rolling one way while aiming up is no flick") {
    // A player aiming 67 degrees up, the hand rolling about 135 degrees one way over 0.9 s (fastest about 450
    // degrees per second, the aim swinging about 55), back about 17.
    Rig rig(flickOn(), 67.0f);
    int count = rig.rollBy(-61.0f, 100.0f, 60.0f);
    count += rig.rollBy(-75.0f, 550.0f, 60.0f);
    count += rig.rollBy(17.0f, 100.0f, 60.0f);
    count += rig.run(0.0f, 0.5f, 0.0f, rig.pitch);
    CHECK(count == 0);
}

TEST_CASE("modelled: a fast roll short of turning the hand over, and back, is no flick") {
    // A player aiming 33 degrees down while turning (head about 50 degrees per second, the aim swinging about
    // 400 at the roll): the hand rolled about 65 degrees one way in 0.07 s and 110 back.
    Rig rig(flickOn(), -33.0f);
    int count = rig.rollBy(-66.0f, 950.0f, 200.0f);
    count += rig.rollBy(109.0f, 950.0f, 200.0f);
    count += rig.run(0.0f, 0.5f, 0.0f, rig.pitch);
    CHECK(count == 0);
}

TEST_CASE("modelled: a hand turned over and back slowly is no flick") {
    // A player's hand rolling about 95 degrees one way (about 500 degrees per second, the aim swinging about
    // 240), then back 30 degrees with the aim swinging faster than it rolled, and the rest of the way slowly.
    Rig rig;
    int count = rig.rollBy(96.0f, 600.0f, 200.0f);
    count += rig.rollBy(-30.0f, 500.0f, 700.0f);
    count += rig.rollBy(-45.0f, 60.0f);
    count += rig.run(0.0f, 0.5f, 0.0f, rig.pitch);
    CHECK(count == 0);
}

TEST_CASE("recorded: deliberate flicks in a headset each switch the weapon mod once") {
    const std::span<const RecordedSample> deliberate[] = {
        evr::test::kDeliberateFlickA, evr::test::kDeliberateFlickB, evr::test::kDeliberateFlickC};
    for (std::size_t i = 0; i < std::size(deliberate); ++i) {
        CAPTURE(i);
        const std::vector<FlickOutput> fired = flicks(deliberate[i]);
        REQUIRE(fired.size() == 1);
        CHECK(fired.front().overDegrees >= kFlickOverDegrees);
        CHECK(fired.front().backDegrees >= kFlickBackDegrees);
    }
}

TEST_CASE("recorded: through the mapper, a deliberate flick presses the weapon mod switch once") {
    InputMapper mapper(evr::test::questTouchProfile());
    int presses = 0;
    const std::span<const RecordedSample> samples = evr::test::kDeliberateFlickB;
    float previous = samples.front().seconds;
    for (const RecordedSample& sample : samples) {
        const float dt = sample.seconds > previous ? sample.seconds - previous : 1.0f / 90.0f;
        previous = sample.seconds;
        presses +=
            contains(mapper.update(frameAt(sample), {}, dt).pressed, GameAction::SwitchWeaponMod) ? 1 : 0;
    }
    CHECK(presses == 1);
}
