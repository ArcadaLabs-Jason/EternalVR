#include "features/menu/load_tips_keys.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

using evr::menu::LoadTipsInput;
using evr::menu::LoadTipsKey;
using evr::menu::LoadTipsKeys;
using evr::menu::tipsBarShowing;

namespace {

// `showing`: the bar polled its keys 0.05 s ago (one of its frames), no menu, no game view.
LoadTipsInput frame(double t, bool showing, float left, float right) {
    LoadTipsInput in;
    in.seconds = t;
    in.sincePoll = showing ? 0.05 : -1.0;
    in.leftGrip = left;
    in.rightGrip = right;
    return in;
}

bool has(const std::vector<LoadTipsKey>& keys, char key, bool down) {
    for (const LoadTipsKey& k : keys) {
        if (k.key == static_cast<std::uint8_t>(key) && k.down == down) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("on the tips bar the left grip sends Q and the right grip E") {
    LoadTipsKeys keys;
    CHECK(keys.update(frame(0.0, true, 0.0f, 0.0f)).empty());
    const std::vector<LoadTipsKey> left = keys.update(frame(0.1, true, 1.0f, 0.0f));
    REQUIRE(left.size() == 1);
    CHECK(has(left, 'Q', true));
    CHECK(keys.holdsKey());
    // Let go at once: the key stays down until minHold, so the bar's next read still sees it.
    CHECK(keys.update(frame(0.15, true, 0.0f, 0.0f)).empty());
    CHECK(has(keys.update(frame(0.31, true, 0.0f, 0.0f)), 'Q', false));
    CHECK_FALSE(keys.holdsKey());
    const std::vector<LoadTipsKey> right = keys.update(frame(0.5, true, 0.0f, 0.9f));
    REQUIRE(right.size() == 1);
    CHECK(has(right, 'E', true));
    // Held on: one press, no repeat; it goes up with the grip.
    CHECK(keys.update(frame(1.5, true, 0.0f, 0.9f)).empty());
    CHECK(has(keys.update(frame(1.6, true, 0.0f, 0.1f)), 'E', false));
}

TEST_CASE("without the tips bar the grips send nothing") {
    LoadTipsKeys keys;
    for (double t = 0.0; t < 1.0; t += 0.1) {
        CHECK(keys.update(frame(t, false, t < 0.5 ? 1.0f : 0.0f, t < 0.5 ? 0.0f : 1.0f)).empty());
    }
    CHECK_FALSE(keys.holdsKey());
}

TEST_CASE("a grip held when the bar comes up is not a press until it is let go") {
    LoadTipsKeys keys;
    CHECK(keys.update(frame(0.0, false, 1.0f, 0.0f)).empty());
    CHECK(keys.update(frame(0.1, true, 1.0f, 0.0f)).empty());
    CHECK(keys.update(frame(0.5, true, 1.0f, 0.0f)).empty());
    CHECK(keys.update(frame(0.6, true, 0.0f, 0.0f)).empty());
    CHECK(has(keys.update(frame(0.7, true, 1.0f, 0.0f)), 'Q', true));
}

TEST_CASE("a key down goes up when the bar goes") {
    LoadTipsKeys keys;
    keys.update(frame(0.0, true, 0.0f, 0.0f));
    CHECK(has(keys.update(frame(0.1, true, 0.0f, 1.0f)), 'E', true));
    const std::vector<LoadTipsKey> gone = keys.update(frame(0.12, false, 0.0f, 1.0f));
    REQUIRE(gone.size() == 1);
    CHECK(has(gone, 'E', false));
    CHECK_FALSE(keys.holdsKey());
    // Still held when the next loading screen's bar comes up: not a press.
    CHECK(keys.update(frame(5.0, true, 0.0f, 1.0f)).empty());
}

TEST_CASE("the grips' presses follow the menus' thresholds") {
    LoadTipsKeys keys;
    keys.update(frame(0.0, true, 0.0f, 0.0f));
    CHECK(keys.update(frame(0.1, true, 0.5f, 0.0f)).empty()); // under the press threshold
    CHECK(has(keys.update(frame(0.2, true, 0.6f, 0.0f)), 'Q', true));
    // 0.4 is still held (above the release threshold); 0.3 lets go.
    CHECK(keys.update(frame(0.5, true, 0.4f, 0.0f)).empty());
    CHECK(has(keys.update(frame(0.6, true, 0.3f, 0.0f)), 'Q', false));
}

TEST_CASE("a grip pulled as play starts sends nothing") {
    LoadTipsKeys keys;
    keys.update(frame(0.0, true, 0.0f, 0.0f));
    // The bar polled 0.1 s ago, but the headset already shows the game's view: play has begun.
    LoadTipsInput play = frame(0.1, true, 0.0f, 1.0f);
    play.sincePoll = 0.1;
    play.gameView = true;
    CHECK_FALSE(tipsBarShowing(play));
    CHECK(keys.update(play).empty());
    CHECK_FALSE(keys.holdsKey());
    // A key down when the game's view comes back goes up at once.
    LoadTipsKeys held;
    held.update(frame(0.0, true, 0.0f, 0.0f));
    CHECK(has(held.update(frame(0.1, true, 1.0f, 0.0f)), 'Q', true));
    LoadTipsInput back = frame(0.12, true, 1.0f, 0.0f);
    back.gameView = true;
    CHECK(has(held.update(back), 'Q', false));
}

TEST_CASE("the bar's last poll counts for about three of its frames") {
    LoadTipsInput in = frame(0.0, true, 0.0f, 0.0f);
    in.sincePoll = 0.2;
    CHECK(tipsBarShowing(in));
    in.sincePoll = 0.27;
    CHECK_FALSE(tipsBarShowing(in));
    in.sincePoll = -1.0;
    CHECK_FALSE(tipsBarShowing(in));
    in = frame(0.0, true, 0.0f, 0.0f);
    in.menuUp = true;
    CHECK_FALSE(tipsBarShowing(in));
    in = frame(0.0, true, 0.0f, 0.0f);
    in.allowed = false;
    CHECK_FALSE(tipsBarShowing(in));
}

TEST_CASE("release lets every key up and needs the grip let go again") {
    LoadTipsKeys keys;
    keys.update(frame(0.0, true, 0.0f, 0.0f));
    CHECK(has(keys.update(frame(0.1, true, 0.0f, 1.0f)), 'E', true));
    const std::vector<LoadTipsKey> up = keys.release();
    REQUIRE(up.size() == 1);
    CHECK(has(up, 'E', false));
    CHECK_FALSE(keys.holdsKey());
    CHECK(keys.release().empty());
    CHECK(keys.update(frame(0.2, true, 0.0f, 1.0f)).empty());
    CHECK(keys.update(frame(0.3, true, 0.0f, 0.0f)).empty());
    CHECK(has(keys.update(frame(0.4, true, 0.0f, 1.0f)), 'E', true));
}
