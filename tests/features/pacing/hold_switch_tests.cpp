#include "features/pacing/pace_policy.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <functional>
#include <vector>

using namespace evr::pacing;

namespace {

constexpr double kPeriod = 1.0 / 90.0;

// 90 Hz headset frames; the game hands over images at a rate that can depend on whether it is held and how
// long since the switch last changed.
struct Feeder {
    double t = 1.0;
    std::uint64_t handOvers = 0;
    double debt = 0.0;
    double changedAt = 1.0;
    std::vector<double> backOffsAtLetGo; // the back-off each time the game was let go

    // Runs `seconds`; `rate(held, sinceChange)` is the game's images a second. Returns the changes.
    int run(HoldSwitch& sw,
            double seconds,
            const std::function<double(bool, double)>& rate,
            bool inPlay = true,
            double frameHz = 90.0) {
        int changes = 0;
        const int frames = static_cast<int>(seconds * frameHz + 0.5);
        for (int i = 0; i < frames; ++i) {
            t += 1.0 / frameHz;
            debt += rate(sw.held(), t - changedAt) / frameHz;
            while (debt >= 1.0) {
                ++handOvers;
                debt -= 1.0;
            }
            if (sw.frame(t, handOvers, kPeriod, inPlay)) {
                ++changes;
                changedAt = t;
                if (!sw.held()) {
                    backOffsAtLetGo.push_back(sw.backOffSeconds());
                }
            }
        }
        return changes;
    }
};

double steady(double hz, bool, double) {
    return hz;
}

} // namespace

TEST_CASE(
    "hold switch: one bad window (a hitch above the refresh) does not let the game go; two in a row do") {
    HoldSwitch sw;
    Feeder f;
    // 90 images a second, but every 3 s a 100 ms hitch with no image: 9 headset frames repeat, at most one
    // window over 5%.
    const auto hitchy = [&f](bool, double) {
        const double phase = f.t - static_cast<double>(static_cast<long long>(f.t / 3.0)) * 3.0;
        return phase < 0.1 ? 0.0 : 90.0;
    };
    CHECK(f.run(sw, 30.0, hitchy) == 0);
    CHECK(sw.held());
    CHECK(sw.lastChange() == HoldSwitch::Change::None);
    // 85 images a second: every window repeats about 5.6%; the second such window lets it go.
    CHECK(f.run(sw, 2.5, [](bool h, double s) { return steady(85.0, h, s); }) == 1);
    CHECK_FALSE(sw.held());
    CHECK(sw.lastChange() == HoldSwitch::Change::FellBehind);
    CHECK(sw.lastRepeatShare() > HoldSwitch::kLetGoRepeatShare);
    CHECK(sw.lastRate() == doctest::Approx(85.0).epsilon(0.03));
}

TEST_CASE(
    "hold switch: holds that end within 10 s double the back-off 1, 2, 4 ... 32; a lasting hold resets it") {
    HoldSwitch sw;
    Feeder f;
    // Let go the game hands over 105 images a second (held again after the back-off); held it keeps up for
    // 2.5 s, then repeats 9% of the frames: each hold ends after about 4.5 s, past the old 2 s test.
    const auto failing = [](bool held, double since) {
        return held ? (since < 2.5 ? 90.0 : 82.0) : 105.0;
    };
    f.run(sw, 200.0, failing);
    REQUIRE(f.backOffsAtLetGo.size() >= 7);
    CHECK(f.backOffsAtLetGo[0] == doctest::Approx(1.0)); // the first let-go judges no hold
    CHECK(f.backOffsAtLetGo[1] == doctest::Approx(2.0));
    CHECK(f.backOffsAtLetGo[2] == doctest::Approx(4.0));
    CHECK(f.backOffsAtLetGo[3] == doctest::Approx(8.0));
    CHECK(f.backOffsAtLetGo[4] == doctest::Approx(16.0));
    CHECK(f.backOffsAtLetGo[5] == doctest::Approx(32.0));
    CHECK(f.backOffsAtLetGo[6] == doctest::Approx(32.0));
    // Wait for the next hold, keep it for 12 s, then fall behind: the back-off starts over.
    f.backOffsAtLetGo.clear();
    f.run(sw, 40.0, [](bool held, double) { return held ? 90.0 : 105.0; });
    REQUIRE(sw.held());
    f.run(sw, 12.0, [](bool, double) { return 90.0; });
    CHECK(f.run(sw, 3.0, [](bool held, double) { return held ? 82.0 : 82.0; }) == 1);
    REQUIRE(f.backOffsAtLetGo.size() == 1);
    CHECK(f.backOffsAtLetGo[0] == doctest::Approx(HoldSwitch::kWindowSeconds));
}

TEST_CASE("hold switch: a gap in the headset's frames decides nothing and breaks a run of bad windows") {
    HoldSwitch sw;
    Feeder f;
    f.run(sw, 1.5, [](bool, double) { return 85.0; }); // one bad window
    f.t += 5.0;                                        // the headset slept: no frames for 5 s
    CHECK(f.run(sw, 1.5, [](bool, double) { return 85.0; }) == 0);
    CHECK(sw.held());
    // A window with too few headset frames (20 a second, all repeating) decides nothing either.
    HoldSwitch slow;
    Feeder g;
    CHECK(g.run(slow, 3.0, [](bool, double) { return 0.0; }, true, 20.0) == 0);
    CHECK(slow.held());
}

TEST_CASE("hold switch: menus and loading screens hold the game and decide nothing") {
    HoldSwitch sw;
    Feeder f;
    REQUIRE(f.run(sw, 2.5, [](bool, double) { return 60.0; }) == 1); // let go below the refresh
    REQUIRE_FALSE(sw.held());
    // A loading screen: held at once, and its repeats let nothing go.
    CHECK(f.run(sw, 5.0, [](bool, double) { return 30.0; }, false) == 1);
    CHECK(sw.held());
    CHECK(sw.lastChange() == HoldSwitch::Change::LeftPlay);
    // Back in play below the refresh: let go after two bad windows, the back-off not doubled for the menu's
    // hold.
    CHECK(f.run(sw, 2.5, [](bool, double) { return 60.0; }) == 1);
    CHECK_FALSE(sw.held());
    CHECK(sw.backOffSeconds() == doctest::Approx(HoldSwitch::kWindowSeconds));
}
