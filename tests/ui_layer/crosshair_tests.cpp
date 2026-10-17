#include "ui_layer/crosshair.hpp"
#include "ui_layer/ui_settings.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using evr::ui_layer::centreMasked;
using evr::ui_layer::CrosshairMode;
using evr::ui_layer::crosshairModeName;
using evr::ui_layer::CrosshairState;
using evr::ui_layer::holdConfirmed;
using evr::ui_layer::holdRequested;
using evr::ui_layer::kHoldConfirmPresents;
using evr::ui_layer::kHoldStaleMs;
using evr::ui_layer::kHoldStalePresents;
using evr::ui_layer::nextHoldStreak;
using evr::ui_layer::nextRequestPresents;
using evr::ui_layer::parseCrosshairMode;
using evr::ui_layer::wantsReticleHold;

namespace {

evr::ui_layer::EnvLookup envOf(std::map<std::wstring, std::wstring> values) {
    return [values](std::wstring_view name) -> std::optional<std::wstring> {
        const auto it = values.find(std::wstring(name));
        if (it == values.end()) {
            return std::nullopt;
        }
        return it->second;
    };
}

CrosshairState state(CrosshairMode mode, bool handAim, bool confirmed) {
    CrosshairState s;
    s.mode = mode;
    s.handAim = handAim;
    s.holdConfirmed = confirmed;
    return s;
}

// The run after `presents` presents that each hold and read hidden.
std::uint32_t runOf(std::uint32_t presents) {
    std::uint32_t streak = 0;
    for (std::uint32_t i = 0; i < presents; ++i) {
        streak = nextHoldStreak(streak, true, true);
    }
    return streak;
}

} // namespace

TEST_CASE("crosshair: the mode parses hold and mask, nothing else") {
    CHECK(parseCrosshairMode(L"hold") == CrosshairMode::Hold);
    CHECK(parseCrosshairMode(L" MASK ") == CrosshairMode::Mask);
    CHECK(parseCrosshairMode(L"Hold") == CrosshairMode::Hold);
    CHECK_FALSE(parseCrosshairMode(L""));
    CHECK_FALSE(parseCrosshairMode(L"1"));
    CHECK_FALSE(parseCrosshairMode(L"hide"));
    CHECK(std::string(crosshairModeName(CrosshairMode::Hold)) == "hold");
    CHECK(std::string(crosshairModeName(CrosshairMode::Mask)) == "mask");
}

TEST_CASE("crosshair: ETERNALVR_UI_CROSSHAIR in the UI settings, hold by default") {
    std::vector<std::string> warnings;
    CHECK(evr::ui_layer::readUiSettings(envOf({}), warnings).crosshair == CrosshairMode::Hold);
    CHECK(evr::ui_layer::readUiSettings(envOf({{L"ETERNALVR_UI_CROSSHAIR", L"mask"}}), warnings).crosshair ==
          CrosshairMode::Mask);
    CHECK(warnings.empty());
    const auto bad = evr::ui_layer::readUiSettings(envOf({{L"ETERNALVR_UI_CROSSHAIR", L"off"}}), warnings);
    CHECK(bad.crosshair == CrosshairMode::Hold);
    CHECK(warnings.size() == 1);
}

TEST_CASE("crosshair: positive control, mask mode masks every hand-aim copy as before") {
    // Today's rule: hand aim and no menu or backdrop. A confirmed hold changes nothing in mask mode.
    CHECK(centreMasked(state(CrosshairMode::Mask, true, false)));
    CHECK(centreMasked(state(CrosshairMode::Mask, true, true)));
    CHECK_FALSE(centreMasked(state(CrosshairMode::Mask, false, false)));
    CrosshairState menu = state(CrosshairMode::Mask, true, false);
    menu.menu = true;
    CHECK_FALSE(centreMasked(menu));
    CrosshairState backdrop = state(CrosshairMode::Mask, true, false);
    backdrop.backdrop = true;
    CHECK_FALSE(centreMasked(backdrop));
}

TEST_CASE("crosshair: hold mode masks until the hold is confirmed") {
    CHECK(centreMasked(state(CrosshairMode::Hold, true, false)));
    CHECK_FALSE(centreMasked(state(CrosshairMode::Hold, true, true)));
    // Head aim (or the demon piloted under head aim): the game's crosshair stays, no mask either way.
    CHECK_FALSE(centreMasked(state(CrosshairMode::Hold, false, false)));
    CHECK_FALSE(centreMasked(state(CrosshairMode::Hold, false, true)));
    // A menu or a backdrop never gets the square, confirmed or not.
    CrosshairState menu = state(CrosshairMode::Hold, true, false);
    menu.menu = true;
    CHECK_FALSE(centreMasked(menu));
    CrosshairState backdrop = state(CrosshairMode::Hold, true, false);
    backdrop.backdrop = true;
    CHECK_FALSE(centreMasked(backdrop));
}

TEST_CASE("crosshair: the hold is asked for only in hold mode under hand aim") {
    CHECK(wantsReticleHold(CrosshairMode::Hold, true));
    CHECK_FALSE(wantsReticleHold(CrosshairMode::Hold, false));
    CHECK_FALSE(wantsReticleHold(CrosshairMode::Mask, true));
    CHECK_FALSE(wantsReticleHold(CrosshairMode::Mask, false));
}

TEST_CASE("crosshair: a scoped zoom asks for the hold under head aim too, in hold mode only") {
    CHECK(wantsReticleHold(CrosshairMode::Hold, false, true));
    CHECK(wantsReticleHold(CrosshairMode::Hold, true, true));
    CHECK_FALSE(wantsReticleHold(CrosshairMode::Hold, false, false));
    CHECK_FALSE(wantsReticleHold(CrosshairMode::Mask, false, true));
}

TEST_CASE("crosshair: confirmed after a run of presents that read hidden") {
    const std::uint64_t now = 10'000;
    CHECK_FALSE(holdConfirmed(runOf(kHoldConfirmPresents - 1), now, now));
    CHECK(holdConfirmed(runOf(kHoldConfirmPresents), now, now));
    // The run stops growing at the threshold and stays confirmed.
    CHECK(runOf(kHoldConfirmPresents * 5) == kHoldConfirmPresents);
    CHECK(holdConfirmed(runOf(kHoldConfirmPresents * 5), now, now));
}

TEST_CASE("crosshair: one present that is not hidden, or not held, starts the run again") {
    const std::uint32_t full = runOf(kHoldConfirmPresents);
    // The game put another style back (a cheat cvar reset, a console write), or the guard tripped.
    CHECK(nextHoldStreak(full, true, false) == 0);
    // Hand aim ended: the layer gives the cvar back, the next hand-aim stretch must confirm again.
    CHECK(nextHoldStreak(full, false, true) == 0);
    CHECK(nextHoldStreak(full, false, false) == 0);
    CHECK_FALSE(holdConfirmed(nextHoldStreak(full, true, false), 0, 0));
}

TEST_CASE("crosshair: a confirmation no present has renewed goes stale") {
    const std::uint32_t full = runOf(kHoldConfirmPresents);
    CHECK(holdConfirmed(full, 1'000, 1'000 + kHoldStaleMs));
    CHECK_FALSE(holdConfirmed(full, 1'000, 1'000 + kHoldStaleMs + 1));
    // Never checked (a session without run-time holds: mono with the UI layer): never confirmed.
    CHECK_FALSE(holdConfirmed(0, 0, 0));
    // A reading stamped after the clock read (another thread) still counts.
    CHECK(holdConfirmed(full, 2'000, 1'000));
}

TEST_CASE("crosshair: the presenter's tally counts kept copies and each change once") {
    evr::ui_layer::HoldTally tally;
    CHECK_FALSE(tally.note(true, false, true)); // starts not confirmed: no change
    CHECK(tally.note(true, true, true));        // confirmed
    CHECK_FALSE(tally.note(true, true, true));
    CHECK_FALSE(tally.note(true, true, false));  // a menu or backdrop copy: whole anyway, not the hold's
    CHECK_FALSE(tally.note(false, false, true)); // head aim or mask mode: nothing changes
    CHECK(tally.confirmed);
    CHECK(tally.note(true, false, true)); // the mask is back
    CHECK(tally.changes == 2);
    CHECK(tally.kept == 2);
    CHECK_FALSE(tally.confirmed);
}

namespace {

// The request as runtime_cvars_reticle.cpp keeps it: each present reads it first and counts itself
// (runtime_cvars::apply, with the same nextRequestPresents), a GUI copy after that makes it again
// (wantReticleHidden).
struct RequestModel {
    std::uint64_t ms = 0;
    std::uint32_t presents = 0;

    void copy(std::uint64_t now) {
        ms = now;
        presents = 0;
    }
    bool present(std::uint64_t now) {
        const std::uint32_t read = presents;
        presents = nextRequestPresents(presents);
        return holdRequested(true, ms, now, read);
    }
};

} // namespace

TEST_CASE("crosshair: a request lapses only when it is old and enough presents read it") {
    CHECK(holdRequested(true, 1'000, 1'000, 0));
    CHECK(holdRequested(true, 1'000, 1'000 + kHoldStaleMs, kHoldStalePresents));
    CHECK_FALSE(holdRequested(true, 1'000, 1'000 + kHoldStaleMs + 1, kHoldStalePresents));
    CHECK(holdRequested(true, 1'000, 1'000 + kHoldStaleMs + 1, kHoldStalePresents - 1));
    CHECK_FALSE(holdRequested(false, 1'000, 1'000, 0));
    // A request stamped after the clock was read (another thread) stands.
    CHECK(holdRequested(true, 2'000, 1'000, kHoldStalePresents));
}

TEST_CASE("crosshair: one frame longer than the stale time keeps the request") {
    // Route S at 90 Hz: a present every 11 ms, a copy after every second one; then a 600 ms frame.
    RequestModel r;
    std::uint64_t now = 1'000;
    for (int i = 0; i < 20; ++i, now += 11) {
        CHECK(r.present(now));
        if (i % 2 == 0) {
            r.copy(now);
        }
    }
    now += 600;
    CHECK(r.present(now)); // the first present after the frame: the last copy's request is 0.6 s old
    r.copy(now);
    now += 11;
    CHECK(r.present(now));
    // Positive control: the same 0.6 s old request read by kHoldStalePresents presents lapses.
    CHECK_FALSE(holdRequested(true, now - 611, now, kHoldStalePresents));
    // Copies under 2 Hz (a present every 0.6 s, each second one copied) keep it too.
    for (int i = 0; i < 10; ++i) {
        now += 600;
        CHECK(r.present(now));
        if (i % 2 == 1) {
            r.copy(now);
        }
    }
}

TEST_CASE("crosshair: a request the copies stopped renewing lapses after the stale time") {
    // Positive control for the hitch case: the copies stop (VR off for the game, a failed target check) while
    // the presents go on.
    RequestModel r;
    std::uint64_t now = 1'000;
    r.copy(now);
    int presents = 0;
    while (r.present(now += 11)) {
        ++presents;
        REQUIRE(presents < 1'000);
    }
    CHECK(now - 1'000 > kHoldStaleMs);
    CHECK(now - 1'000 <= kHoldStaleMs + 11);
    CHECK(presents >= static_cast<int>(kHoldStalePresents));
}

TEST_CASE("crosshair: hand, head, hand: hidden, given back, hidden again") {
    using evr::ui_layer::ReticleWrite;
    evr::ui_layer::ReticleKeep keep;
    // Head aim before any hold: nothing to give back.
    CHECK(evr::ui_layer::reticleWrite(false, -1, keep) == ReticleWrite::None);
    CHECK_FALSE(keep.kept);
    // Hand aim: hidden, the game's -1 kept; the next presents read 0 and write nothing.
    CHECK(evr::ui_layer::reticleWrite(true, -1, keep) == ReticleWrite::Hide);
    CHECK(keep.kept);
    CHECK(keep.before == -1);
    CHECK(evr::ui_layer::reticleWrite(true, 0, keep) == ReticleWrite::None);
    // Head aim (or a lapsed request): the kept value goes back once; then it reads -1 and nothing more is
    // written.
    CHECK(evr::ui_layer::reticleWrite(false, 0, keep) == ReticleWrite::Restore);
    CHECK(keep.before == -1);
    CHECK_FALSE(keep.kept);
    CHECK(evr::ui_layer::reticleWrite(false, -1, keep) == ReticleWrite::None);
    // Hand aim again: hidden again, the value it reads then kept.
    CHECK(evr::ui_layer::reticleWrite(true, -1, keep) == ReticleWrite::Hide);
    CHECK(keep.kept);
    CHECK(keep.before == -1);
}

TEST_CASE("crosshair: a console style set between holds is the player's") {
    using evr::ui_layer::ReticleWrite;
    evr::ui_layer::ReticleKeep keep;
    CHECK(evr::ui_layer::reticleWrite(true, -1, keep) == ReticleWrite::Hide);
    CHECK(evr::ui_layer::reticleWrite(false, 0, keep) == ReticleWrite::Restore);
    // Under head aim the player sets style 0 at the console: it is not written back.
    CHECK(evr::ui_layer::reticleWrite(false, 0, keep) == ReticleWrite::None);
    CHECK(evr::ui_layer::reticleWrite(false, 0, keep) == ReticleWrite::None);
    // ... or style 5: the next hold keeps it and gives it back.
    CHECK(evr::ui_layer::reticleWrite(true, 5, keep) == ReticleWrite::Hide);
    CHECK(keep.before == 5);
    CHECK(evr::ui_layer::reticleWrite(false, 0, keep) == ReticleWrite::Restore);
    CHECK(keep.before == 5);
}

TEST_CASE("crosshair: a cheat reset while held is written over without replacing the kept value") {
    using evr::ui_layer::ReticleWrite;
    evr::ui_layer::ReticleKeep keep;
    // A console style 5 before the hold.
    CHECK(evr::ui_layer::reticleWrite(true, 5, keep) == ReticleWrite::Hide);
    CHECK(evr::ui_layer::reticleWrite(true, 0, keep) == ReticleWrite::None);
    // "Resetting cheat cvar": the game puts the default -1 back while the hold stands.
    CHECK(evr::ui_layer::reticleWrite(true, -1, keep) == ReticleWrite::Hide);
    CHECK(keep.before == 5);
    CHECK(evr::ui_layer::reticleWrite(false, 0, keep) == ReticleWrite::Restore);
    CHECK(keep.before == 5);
}

TEST_CASE("crosshair: a cvar already at 0 before the hold is never written or given back") {
    using evr::ui_layer::ReticleWrite;
    evr::ui_layer::ReticleKeep keep;
    CHECK(evr::ui_layer::reticleWrite(true, 0, keep) == ReticleWrite::None);
    CHECK_FALSE(keep.kept);
    CHECK(evr::ui_layer::reticleWrite(false, 0, keep) == ReticleWrite::None);
}

TEST_CASE("crosshair: a hold that ends on a value the game put back keeps nothing") {
    using evr::ui_layer::ReticleWrite;
    evr::ui_layer::ReticleKeep keep;
    CHECK(evr::ui_layer::reticleWrite(true, 5, keep) == ReticleWrite::Hide);
    CHECK(evr::ui_layer::reticleWrite(true, 0, keep) == ReticleWrite::None);
    // A cheat reset or a checkpoint reload puts -1 back, and the hold ends before the next present hides it
    // again: nothing written, nothing kept.
    CHECK(evr::ui_layer::reticleWrite(false, -1, keep) == ReticleWrite::None);
    CHECK_FALSE(keep.kept);
    // A console style 0 under head aim is the player's: not written back.
    CHECK(evr::ui_layer::reticleWrite(false, 0, keep) == ReticleWrite::None);
    // A console style 3 set before the next hold is what that hold keeps and gives back.
    CHECK(evr::ui_layer::reticleWrite(false, 3, keep) == ReticleWrite::None);
    CHECK(evr::ui_layer::reticleWrite(true, 3, keep) == ReticleWrite::Hide);
    CHECK(keep.before == 3);
    CHECK(evr::ui_layer::reticleWrite(false, 0, keep) == ReticleWrite::Restore);
    CHECK(keep.before == 3);
}

TEST_CASE("crosshair: a give-back the write refused is tried again on the next present") {
    using evr::ui_layer::ReticleWrite;
    evr::ui_layer::ReticleKeep keep;
    CHECK(evr::ui_layer::reticleWrite(true, -1, keep) == ReticleWrite::Hide);
    CHECK(evr::ui_layer::reticleWrite(false, 0, keep) == ReticleWrite::Restore);
    keep.kept = true; // what runtime_cvars_reticle.cpp does when cvar_book::write refuses the give-back
    CHECK(evr::ui_layer::reticleWrite(false, 0, keep) == ReticleWrite::Restore);
    CHECK(keep.before == -1);
    CHECK_FALSE(keep.kept);
}

TEST_CASE("crosshair: the presents that read a request count up to the stale count and stop") {
    CHECK(nextRequestPresents(0) == 1);
    CHECK(nextRequestPresents(kHoldStalePresents - 1) == kHoldStalePresents);
    CHECK(nextRequestPresents(kHoldStalePresents) == kHoldStalePresents);
}
