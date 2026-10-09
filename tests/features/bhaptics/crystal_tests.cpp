#include "features/bhaptics/body_haptics.hpp"
#include "features/bhaptics/crystal.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

using evr::bhaptics::BodyHaptics;
using evr::bhaptics::BodySignals;
using evr::bhaptics::crystalDistance;
using evr::bhaptics::crystalWaveDelay;
using evr::bhaptics::crystalWaveShare;
using evr::bhaptics::Device;
using evr::bhaptics::Effect;
using evr::bhaptics::Frame;
using evr::bhaptics::kCrystalDelaySeconds;
using evr::bhaptics::kCrystalSleeveDistance;
using evr::bhaptics::kCrystalWaveSeconds;
using evr::bhaptics::kRuneDelaySeconds;
using evr::bhaptics::kTokenDelaySeconds;
using evr::bhaptics::kVestColumns;
using evr::bhaptics::SyncKind;

namespace {

BodySignals playing(double seconds) {
    BodySignals s;
    s.seconds = seconds;
    s.gameplay = true;
    s.health = 100.0f;
    s.armor = 50.0f;
    return s;
}

BodySignals crystalSync(double seconds, SyncKind kind = SyncKind::Crystal) {
    BodySignals s = playing(seconds);
    s.sync = true;
    s.syncKind = kind;
    return s;
}

std::vector<const Frame*> of(const std::vector<Frame>& frames, Effect effect) {
    std::vector<const Frame*> out;
    for (const Frame& f : frames) {
        if (f.effect == effect) {
            out.push_back(&f);
        }
    }
    return out;
}

const Frame* on(const std::vector<Frame>& frames, Device device) {
    for (const Frame& f : frames) {
        if (f.device == device) {
            return &f;
        }
    }
    return nullptr;
}

float distanceOf(int index) {
    return crystalDistance(index / kVestColumns, index % kVestColumns);
}

// The frame's dots' distance from the centre, weighted by their intensity.
float meanDistance(const Frame& frame) {
    float sum = 0.0f;
    float weight = 0.0f;
    for (const auto& d : frame.dots) {
        sum += distanceOf(d.index) * d.intensity;
        weight += d.intensity;
    }
    return weight > 0.0f ? sum / weight : 0.0f;
}

bool hasCorner(const Frame& frame) {
    return std::any_of(frame.dots.begin(), frame.dots.end(), [](const auto& d) {
        return d.index == 0 || d.index == 3 || d.index == 16 || d.index == 19;
    });
}

bool hasCentre(const Frame& frame) {
    return std::any_of(frame.dots.begin(), frame.dots.end(),
                       [](const auto& d) { return d.index == 9 || d.index == 10; });
}

// When the share at `distance` is highest, sampled every 10 ms; also checks it rises and then only falls.
double peakAt(float distance) {
    double best = -1.0;
    float bestShare = 0.0f;
    float last = 0.0f;
    bool falling = false;
    for (double t = 0.0; t < kCrystalWaveSeconds; t += 0.01) {
        const float share = crystalWaveShare(t, distance);
        CHECK(share >= 0.0f);
        CHECK(share <= 1.0f);
        if (share < last) {
            falling = true;
        } else if (falling) {
            CHECK(share <= last); // fades behind the ring and does not come back
        }
        if (share > bestShare) {
            bestShare = share;
            best = t;
        }
        last = share;
    }
    return best;
}

} // namespace

TEST_CASE(
    "the crystal's ring starts on the vest's two middle motors and reaches the corners, then the sleeves") {
    CHECK(crystalDistance(2, 1) == doctest::Approx(0.5f));
    CHECK(crystalDistance(2, 2) == doctest::Approx(0.5f));
    CHECK(crystalDistance(0, 0) == doctest::Approx(2.5f));
    CHECK(crystalDistance(4, 3) == doctest::Approx(2.5f));
    CHECK(crystalDistance(1, 0) == doctest::Approx(crystalDistance(3, 3)));
    CHECK(kCrystalSleeveDistance > crystalDistance(0, 0));
    // At the start only the middle is felt, at full strength.
    CHECK(crystalWaveShare(0.0, crystalDistance(2, 1)) == doctest::Approx(1.0f));
    CHECK(crystalWaveShare(0.0, crystalDistance(1, 1)) == 0.0f);
    CHECK(crystalWaveShare(0.0, crystalDistance(0, 0)) == 0.0f);
    CHECK(crystalWaveShare(0.0, kCrystalSleeveDistance) == 0.0f);
    // Further out is reached later.
    const double middle = peakAt(crystalDistance(2, 1));
    const double inner = peakAt(crystalDistance(1, 1));
    const double side = peakAt(crystalDistance(2, 0));
    const double corner = peakAt(crystalDistance(0, 0));
    const double sleeve = peakAt(kCrystalSleeveDistance);
    CHECK(middle < inner);
    CHECK(inner < side);
    CHECK(side < corner);
    CHECK(corner < sleeve);
    CHECK(sleeve > kCrystalWaveSeconds / 2.0);
    CHECK(sleeve < kCrystalWaveSeconds);
    // The middle has faded by the time the ring is at the corners.
    CHECK(crystalWaveShare(corner, crystalDistance(2, 1)) == 0.0f);
    // Nothing outside the wave.
    CHECK(crystalWaveShare(-0.01, crystalDistance(2, 1)) == 0.0f);
    CHECK(crystalWaveShare(kCrystalWaveSeconds, kCrystalSleeveDistance) == 0.0f);
    CHECK(crystalWaveShare(std::numeric_limits<double>::quiet_NaN(), 1.0f) == 0.0f);
    CHECK(crystalWaveShare(0.5, std::numeric_limits<float>::infinity()) == 0.0f);
}

TEST_CASE("the Sentinel Crystal's pickup plays a wave from the centre of the vest out, after its delay") {
    BodyHaptics body;
    body.update(playing(1.0));
    auto s = crystalSync(1.1);
    CHECK(body.update(s).empty()); // not a glory kill, and the wave waits for its delay
    const double start = 1.1 + kCrystalDelaySeconds;
    struct Step {
        double seconds;
        std::vector<Frame> frames;
    };
    std::vector<Step> steps;
    for (double t = 1.12; t < start + kCrystalWaveSeconds + 0.5; t += 0.02) {
        s.seconds = t;
        const auto frames = body.update(s);
        CHECK(of(frames, Effect::GloryKill).empty());
        std::vector<Frame> wave;
        for (const Frame* f : of(frames, Effect::Crystal)) {
            wave.push_back(*f);
        }
        if (!wave.empty()) {
            CHECK(t >= start - 1e-9);
            CHECK(t < start + kCrystalWaveSeconds);
            steps.push_back({t, std::move(wave)});
        }
    }
    REQUIRE(steps.size() >= 22);
    CHECK(steps.size() <= 26);
    // The whole wave is over about kCrystalWaveSeconds after its start.
    for (const Frame& f : steps.back().frames) {
        CHECK(steps.back().seconds + f.durationMillis / 1000.0 <= start + kCrystalWaveSeconds + 0.1);
    }
    std::vector<const Frame*> fronts;
    double firstCorner = -1.0;
    double firstSleeve = -1.0;
    int strongest = 0;
    for (const Step& step : steps) {
        for (const Frame& f : step.frames) {
            for (const auto& d : f.dots) {
                strongest = std::max<int>(strongest, d.intensity);
            }
        }
        const Frame* front = on(step.frames, Device::VestFront);
        if (front != nullptr) {
            fronts.push_back(front);
            if (firstCorner < 0.0 && hasCorner(*front)) {
                firstCorner = step.seconds;
            }
        }
        if (firstSleeve < 0.0 && on(step.frames, Device::ForearmL) != nullptr) {
            firstSleeve = step.seconds;
            CHECK(on(step.frames, Device::ForearmR) != nullptr);
        }
    }
    CHECK(strongest >= 70);
    CHECK(strongest <= 85);
    // The first step: the middle of the chest and back, strongest in the two middle motors, no sleeves.
    const std::vector<Frame>& first = steps.front().frames;
    REQUIRE(on(first, Device::VestFront) != nullptr);
    REQUIRE(on(first, Device::VestBack) != nullptr);
    CHECK(on(first, Device::ForearmL) == nullptr);
    for (const Frame& f : first) {
        int centre = 0;
        int rest = 0;
        for (const auto& d : f.dots) {
            CHECK(distanceOf(d.index) < 1.2f);
            int& level = distanceOf(d.index) < 0.6f ? centre : rest;
            level = std::max<int>(level, d.intensity);
        }
        CHECK(centre > rest);
    }
    // Then out to the corners, the middle left behind, and the sleeves after the corners, at the end.
    REQUIRE(fronts.size() >= 3);
    CHECK(meanDistance(*fronts.front()) < meanDistance(*fronts[fronts.size() / 2]));
    CHECK(meanDistance(*fronts[fronts.size() / 2]) < meanDistance(*fronts.back()));
    CHECK(meanDistance(*fronts.back()) > 2.0f);
    CHECK_FALSE(hasCentre(*fronts.back()));
    REQUIRE(firstCorner > 0.0);
    REQUIRE(firstSleeve > 0.0);
    CHECK(firstCorner > start + 0.5);
    CHECK(firstSleeve >= firstCorner);
    CHECK(firstSleeve > start + kCrystalWaveSeconds / 2.0);
    CHECK(on(steps.back().frames, Device::ForearmL) != nullptr);
}

TEST_CASE("leaving play cancels a crystal's wave that has not played out") {
    BodyHaptics body;
    body.update(playing(5.0));
    auto c = crystalSync(5.1);
    body.update(c);
    c.seconds = 5.1 + kCrystalDelaySeconds + 0.1;
    CHECK_FALSE(of(body.update(c), Effect::Crystal).empty());
    auto menu = c;
    menu.seconds = 5.1 + kCrystalDelaySeconds + 0.2;
    menu.gameplay = false;
    body.update(menu);
    for (double t = menu.seconds + 0.02; t < 5.1 + kCrystalDelaySeconds + kCrystalWaveSeconds + 0.5;
         t += 0.02) {
        c.seconds = t;
        CHECK(of(body.update(c), Effect::Crystal).empty());
    }
}

TEST_CASE("the crystal's wave comes after its upgrade menu, timed from the sync's start") {
    // The upgrade menu is up (no gameplay), its sync starts as it closes, while the menu's held-back controls
    // still keep gameplay off, and play comes back a little later with the sync running.
    struct Run {
        std::vector<double> times;
        std::vector<std::vector<Frame>> steps;
    };
    const auto run = [](double back) {
        BodyHaptics body;
        body.update(playing(1.0));
        auto menu = playing(1.1);
        menu.gameplay = false;
        body.update(menu);
        auto s = crystalSync(4.0);
        s.gameplay = false;
        CHECK(body.update(s).empty());
        s.gameplay = true;
        Run r;
        for (double t = back; t < 4.0 + kCrystalDelaySeconds + kCrystalWaveSeconds + 0.5; t += 0.02) {
            s.seconds = t;
            // Kept for the loop: the pointers of() returns point into it.
            const std::vector<Frame> frames = body.update(s);
            std::vector<Frame> wave;
            for (const Frame* f : of(frames, Effect::Crystal)) {
                wave.push_back(*f);
            }
            if (!wave.empty()) {
                r.times.push_back(t);
                r.steps.push_back(std::move(wave));
            }
        }
        return r;
    };
    const Run sameFrame = run(4.0);
    REQUIRE(sameFrame.times.size() >= 22);
    CHECK(sameFrame.times.front() >= 4.0 + kCrystalDelaySeconds - 1e-9);
    CHECK(sameFrame.times.front() < 4.0 + kCrystalDelaySeconds + 0.03);
    CHECK(sameFrame.times.back() < 4.0 + kCrystalDelaySeconds + kCrystalWaveSeconds);
    const Run later = run(4.5);
    REQUIRE(!later.times.empty());
    CHECK(later.times.front() < 4.0 + kCrystalDelaySeconds + 0.03);
    // Back during the wave: the ring where it is by now, the middle already behind it.
    const Run during = run(4.0 + kCrystalDelaySeconds + 1.0);
    REQUIRE(!during.times.empty());
    CHECK(during.times.front() < 4.0 + kCrystalDelaySeconds + 1.03);
    for (const Frame& f : during.steps.front()) {
        if (f.device == Device::VestFront || f.device == Device::VestBack) {
            CHECK_FALSE(hasCentre(f));
        }
    }
    // Back after the wave would have ended: nothing late.
    CHECK(run(4.0 + kCrystalDelaySeconds + kCrystalWaveSeconds + 0.1).times.empty());
}

TEST_CASE(
    "the crystal's wave plays for a Sentinel Crystal, a Praetor token and a rune, each with its delay") {
    REQUIRE(crystalWaveDelay(SyncKind::Crystal).has_value());
    CHECK(*crystalWaveDelay(SyncKind::Crystal) == kCrystalDelaySeconds);
    REQUIRE(crystalWaveDelay(SyncKind::Token).has_value());
    CHECK(*crystalWaveDelay(SyncKind::Token) == kTokenDelaySeconds);
    REQUIRE(crystalWaveDelay(SyncKind::Rune).has_value());
    CHECK(*crystalWaveDelay(SyncKind::Rune) == kRuneDelaySeconds);
    // The tester feels for it right after the perk is picked; the rune's animation runs about 7.6 s.
    CHECK(kRuneDelaySeconds > 0.0);
    CHECK(kRuneDelaySeconds + kCrystalWaveSeconds < 7.6);
    CHECK_FALSE(crystalWaveDelay(SyncKind::Pickup).has_value());
    CHECK_FALSE(crystalWaveDelay(SyncKind::GloryKill).has_value());
    // The hands close on the coin right after Use; the crystal is taken much later.
    CHECK(kTokenDelaySeconds >= 0.0);
    CHECK(kTokenDelaySeconds < 0.3);
    CHECK(kTokenDelaySeconds < kCrystalDelaySeconds);
}

TEST_CASE("a Praetor token's pickup plays the crystal's wave as the hands close on the coin") {
    // No menu before it: the sync starts in play, and the wave follows kTokenDelaySeconds later.
    BodyHaptics body;
    body.update(playing(1.0));
    auto s = crystalSync(1.1, SyncKind::Token);
    const auto atStart = body.update(s);
    CHECK(of(atStart, Effect::GloryKill).empty());
    const double start = 1.1 + kTokenDelaySeconds;
    std::vector<double> times;
    std::vector<Frame> first;
    // The animation runs about 3.1 s; the wave plays out past its end.
    for (double t = 1.12; t < start + kCrystalWaveSeconds + 0.5; t += 0.02) {
        s.seconds = t;
        s.sync = t < 1.1 + 3.1;
        const auto frames = body.update(s);
        CHECK(of(frames, Effect::GloryKill).empty());
        const auto wave = of(frames, Effect::Crystal);
        if (!wave.empty()) {
            if (times.empty()) {
                for (const Frame* f : wave) {
                    first.push_back(*f);
                }
            }
            times.push_back(t);
        }
    }
    REQUIRE(times.size() >= 22);
    CHECK(times.size() <= 26);
    CHECK(times.front() >= start - 1e-9);
    CHECK(times.front() < start + 0.03);
    CHECK(times.back() < start + kCrystalWaveSeconds);
    // It is the crystal's wave: the middle of the chest and back first, no sleeves yet.
    REQUIRE(on(first, Device::VestFront) != nullptr);
    REQUIRE(on(first, Device::VestBack) != nullptr);
    CHECK(on(first, Device::ForearmL) == nullptr);
    CHECK(hasCentre(*on(first, Device::VestFront)));
    CHECK_FALSE(hasCorner(*on(first, Device::VestFront)));
}

TEST_CASE("a Praetor token's wave plays once per pickup, and a crystal right after keeps its own delay") {
    BodyHaptics body;
    body.update(playing(1.0));
    auto token = crystalSync(1.1, SyncKind::Token);
    std::size_t tokenSteps = 0;
    for (double t = 1.1; t < 1.1 + 3.1; t += 0.02) {
        token.seconds = t;
        tokenSteps += of(body.update(token), Effect::Crystal).empty() ? 0 : 1;
    }
    CHECK(tokenSteps >= 22);
    for (double t = 4.2; t < 6.0; t += 0.02) {
        CHECK(of(body.update(playing(t)), Effect::Crystal).empty());
    }
    auto crystal = crystalSync(6.0);
    double firstCrystal = -1.0;
    for (double t = 6.0; t < 6.0 + kCrystalDelaySeconds + kCrystalWaveSeconds + 0.5; t += 0.02) {
        crystal.seconds = t;
        if (firstCrystal < 0.0 && !of(body.update(crystal), Effect::Crystal).empty()) {
            firstCrystal = t;
        }
    }
    CHECK(firstCrystal >= 6.0 + kCrystalDelaySeconds - 1e-9);
    CHECK(firstCrystal < 6.0 + kCrystalDelaySeconds + 0.03);
}

TEST_CASE("a rune's pickup plays the crystal's wave after its menu, timed from the sync's start") {
    // As in a player's log (public issue #25): the rune's menu is up, its sync starts as the menu closes and
    // play is back a frame or so later.
    BodyHaptics body;
    body.update(playing(1.0));
    auto menu = playing(1.1);
    menu.gameplay = false;
    body.update(menu);
    auto s = crystalSync(4.0, SyncKind::Rune);
    s.gameplay = false;
    CHECK(body.update(s).empty());
    s.gameplay = true;
    const double start = 4.0 + kRuneDelaySeconds;
    std::vector<double> times;
    std::vector<Frame> first;
    // The animation runs about 7.6 s; nothing after the wave.
    for (double t = 4.1; t < 4.0 + 7.6; t += 0.02) {
        s.seconds = t;
        const auto frames = body.update(s);
        CHECK(of(frames, Effect::GloryKill).empty());
        const auto wave = of(frames, Effect::Crystal);
        if (!wave.empty()) {
            if (times.empty()) {
                for (const Frame* f : wave) {
                    first.push_back(*f);
                }
            }
            times.push_back(t);
        }
    }
    REQUIRE(times.size() >= 22);
    CHECK(times.size() <= 26);
    CHECK(times.front() >= start - 1e-9);
    CHECK(times.front() < start + 0.03);
    CHECK(times.back() < start + kCrystalWaveSeconds);
    REQUIRE(on(first, Device::VestFront) != nullptr);
    REQUIRE(on(first, Device::VestBack) != nullptr);
    CHECK(hasCentre(*on(first, Device::VestFront)));
}
