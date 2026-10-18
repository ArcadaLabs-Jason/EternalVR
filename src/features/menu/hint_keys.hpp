#pragma once

// A menu's hint bar (the command bar under it: "[B] BACK", "[F] CUSTOMIZE WEAPON") puts each shortcut in a
// slot, which is also the pad button that presses it on a pad (slot n is the game's JOY n+1: 0 A, 1 B, 2 X,
// 3 Y, 4 and 5 the shoulders, 7 the right stick's click), and on the PC names its keyboard key. The Dossier's
// Arsenal has three shortcuts no controller sent in VR, and the bar does not take clicks: F customize weapon
// (slot 2), X equip or unequip mod (slot 3) and T customize weapon wheel (slot 7). While the bar shows one of
// them, the controller button in its pad button's place sends its key: the left controller's primary (X on
// Touch) F, its secondary (Y on Touch) X, the right stick's click T; the bar names that button
// (vkcore/prompt_hint_keys.hpp). Elsewhere the same key in the same slot gets the same button: the weapon
// skins' F clear skin, the weapon wheel screen's F change size.
//
// Those buttons do something else in the menus (A / X clicks, B / Y goes back, a stick click sends the
// Arsenal weapon row's previous or next, menu_router.hpp): while a shortcut shows, its button is taken from
// the router (the trigger still clicks, B still goes back, the left stick's click and a stick pushed left or
// right still turn the weapon row), and so is a press that sent a key until it is let go. A key goes down on
// the button's press and up on its release, held at least `minHold`.
//
// A shortcut counts as shown while a visible bar button in its slot with its key last updated within
// `shownFresh` (the bar's buttons update every frame), a menu (not a popup) is up and the game may be
// touched.
//
// Pure; the presenter runs it once per XR frame, before the router.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace evr::menu {

// The controller buttons, in the shortcuts' order.
enum class HintButton : std::uint8_t { LeftPrimary, LeftSecondary, RightStickClick };

struct HintShortcut {
    std::int32_t slot = -1;      // the bar's slot
    std::int32_t gameKey = 0;    // the game's key number the bar's button is on
    std::uint8_t virtualKey = 0; // the same key as a Windows virtual-key code
    HintButton button = HintButton::LeftPrimary;
};

inline constexpr std::array<HintShortcut, 3> kHintShortcuts{{
    {2, 0x21, 'F', HintButton::LeftPrimary},
    {3, 0x2D, 'X', HintButton::LeftSecondary},
    {7, 0x14, 'T', HintButton::RightStickClick},
}};

// The shortcut a bar button in `slot` on the game's key `gameKey` is (an index into kHintShortcuts), or
// nothing.
std::optional<std::size_t> hintShortcut(std::int32_t slot, std::int32_t gameKey);

struct HintKeysTuning {
    double shownFresh = 0.25; // a bar button's last update counts this long (about 20 game frames at 90 fps)
    double minHold = 0.06;    // a key is held at least this long (as the router's)
};

struct HintKeysInput {
    double seconds = 0.0; // a steady clock
    // Per shortcut: seconds since a visible bar button showed it; negative: never, or the controller has no
    // name for its button (then it is neither named nor sent).
    std::array<double, kHintShortcuts.size()> sinceShown{-1.0, -1.0, -1.0};
    bool menuUp = false;                            // the router's menu is up, not a popup
    bool allowed = true;                            // the game may be touched (the multiplayer guard)
    std::array<bool, kHintShortcuts.size()> held{}; // each shortcut's button is held now
};

struct HintKey {
    std::uint8_t key = 0; // a virtual-key code
    bool down = false;
};

struct HintKeysOutput {
    std::vector<HintKey> keys;                       // to send this frame, in order
    std::array<bool, kHintShortcuts.size()> taken{}; // the router must not see this shortcut's button now
};

class HintKeys {
public:
    explicit HintKeys(HintKeysTuning tuning = {}) : tuning_(tuning) {}

    HintKeysOutput update(const HintKeysInput& in);

    // Every key down goes up now (the XR frames stop, VR ends); a button still held must be let go first.
    std::vector<HintKey> release();

    [[nodiscard]] bool shown(std::size_t shortcut, const HintKeysInput& in) const;

private:
    struct Button {
        bool held = false;    // last frame
        bool pressed = false; // a press that sent the key, until it is let go
        bool keyDown = false;
        bool releaseWanted = false;
        double downAt = 0.0;
    };

    HintKeysTuning tuning_;
    std::array<Button, kHintShortcuts.size()> buttons_{};
};

} // namespace evr::menu
