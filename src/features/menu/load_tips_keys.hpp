#pragma once

// The loading screen's tips bar ("[Q] PREVIOUS | [E] NEXT", docs/VR_MENUS.md) is no menu: the game shows no
// cursor there and polls Q and E (and a pad's shoulder buttons) itself on every frame of the loading screen.
// On the rig (2026-10-17) the layer's own Q and E presses moved the tips back and forth, and neither stick
// click nor grip did anything. While the bar polls its keys and no menu is up, the grips send them as they
// change tabs in the menus: the left grip Q (the previous tip), the right grip E (the next). The bar names
// the grips (vkcore/prompt_load_tips.cpp).
//
// A grip already held when the bar comes up (a weapon mod held through the load) counts only once it has been
// let go. A key stays down at least `minHold`: the loading screen runs at about 11 frames a second on the rig
// and reads the key's state once a frame, so a quicker tap could fall between two reads.
//
// The bar counts as up only while the loading screen itself is: it polled its keys within `pollFresh` (about
// three of its frames), no menu is up, and play has not begun: the game has rendered no head-tracked view
// since that poll (no loading screen does) and the headset shows none. So a grip pulled as play starts sends
// nothing (Q and E are gameplay keys there).
//
// Pure; the presenter runs it once per XR frame.

#include <array>
#include <cstdint>
#include <vector>

namespace evr::menu {

struct LoadTipsTuning {
    float pressThreshold = 0.55f;   // a grip counts as pressed above this (as in the menus)
    float releaseThreshold = 0.35f; // and as released again below this
    double minHold = 0.2;           // seconds a key stays down at least
    double pollFresh = 0.25;        // the bar's last poll counts this long (about three of its frames)
};

struct LoadTipsInput {
    double seconds = 0.0;  // a steady clock
    double sincePoll = -1; // seconds since the bar last polled its keys; negative: never
    bool menuUp = false;   // a menu's cursor, panel or held backdrop is up
    bool gameView = false; // play: a head-tracked game view since the bar's last poll, or on the headset
    bool allowed = true;   // the game may be touched (the multiplayer guard)
    float leftGrip = 0.0f; // the grips' analog values, 0 to 1
    float rightGrip = 0.0f;
};

// The tips bar is up: polled within pollFresh, no menu, no game view, allowed.
bool tipsBarShowing(const LoadTipsInput& in, const LoadTipsTuning& tuning = {});

struct LoadTipsKey {
    std::uint8_t key = 0; // a virtual-key code: 'Q' or 'E'
    bool down = false;
};

class LoadTipsKeys {
public:
    explicit LoadTipsKeys(LoadTipsTuning tuning = {}) : tuning_(tuning) {}

    // The key presses and releases to send this frame; every key down goes up once the bar is gone.
    std::vector<LoadTipsKey> update(const LoadTipsInput& in);

    // Every key down goes up now (the XR frames stop, VR ends); the grips must be let go before the next
    // press.
    std::vector<LoadTipsKey> release();

    [[nodiscard]] bool holdsKey() const { return grips_[0].keyDown || grips_[1].keyDown; }

private:
    struct Grip {
        bool held = false;    // pressed, with hysteresis
        bool armed = false;   // let go while the bar showed: the next press sends the key
        bool keyDown = false; // the key is down
        double downAt = 0.0;
    };

    LoadTipsTuning tuning_;
    std::array<Grip, 2> grips_{}; // left (Q), right (E)
};

} // namespace evr::menu
