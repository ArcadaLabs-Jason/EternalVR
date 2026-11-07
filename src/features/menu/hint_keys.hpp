#pragma once

// A menu's hint bar (the command bar under it: "[B] BACK", "[F] CUSTOMIZE WEAPON") puts each shortcut in a
// slot, which is also the pad button that presses it on a pad (slot n is the game's JOY n+1: 0 A, 1 B, 2 X,
// 3 Y, 4 and 5 the shoulders, 7 the right stick's click), and on the PC names its keyboard key. The Dossier's
// Arsenal has three shortcuts no controller sent in VR, and the bar does not take clicks: F customize weapon
// (slot 2), X equip or unequip mod (slot 3) and T customize weapon wheel (slot 7); the custom weapon wheel
// screen it opens has F change size (slot 2) and R reset to default (slot 3). While the bar shows one of
// them, the controller button in its pad button's place sends its key: the left controller's primary (X on
// Touch) F, its secondary (Y on Touch) X or R, the right stick's click T; the bar names that button
// (vkcore/prompt_hint_keys.hpp). A controller without a left primary or secondary button has the left stick's
// click and the left Menu button in those places (input::PromptLabelSet::menuHintControls). Elsewhere the
// same key in the same slot gets the same button: the weapon skins' F clear skin.
//
// Those buttons do something else in the menus (A / X clicks, B / Y goes back, a stick click sends the
// Arsenal weapon row's previous or next, menu_router.hpp; the Menu button pauses or goes back): while a
// shortcut shows, its button is taken from the router (the trigger still clicks, B still goes back, the left
// stick's click and a stick pushed left or right still turn the weapon row), and so is a press that sent a
// key until it is let go. A key goes down on the button's press and up on its release, held at least
// `minHold`.
//
// A shortcut counts as shown while a visible bar button in its slot with its key last updated within
// `shownFresh` (the bar's buttons update every frame), a menu (not a popup) is up and the game may be
// touched. Two shortcuts in one place (X and R, slot 3) are never shown together: going from the Arsenal to
// the wheel screen, the one whose button updated last is, and a press latches it until let go, so one press
// sends one key.
//
// R in slot 3 is not only the wheel's: the game's code adds it there for two other screens too (show map
// groups, and an unequip), so R counts only on a bar button whose label is the wheel's reset
// (HintShortcut::label, the text id the button holds); Y keeps its own job everywhere else.
//
// Pure; the presenter runs it once per XR frame, before the router.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace evr::menu {

// The pad buttons' places the shortcuts sit in, named for the controller buttons there on Touch (the index
// into input::PromptLabelSet::menuHints).
enum class HintButton : std::uint8_t { LeftPrimary, LeftSecondary, RightStickClick };

struct HintShortcut {
    std::int32_t slot = -1;      // the bar's slot
    std::int32_t gameKey = 0;    // the game's key number the bar's button is on
    std::uint8_t virtualKey = 0; // the same key as a Windows virtual-key code
    HintButton button = HintButton::LeftPrimary;
    std::string_view label; // the text id the bar button must hold; empty: any
};

// The custom weapon wheel screen's reset to default, the one R in slot 3 that is sent (its update, 0xFB2080
// in build 25216728, adds it with this label).
inline constexpr std::string_view kWheelResetLabel = "#str_cmd_weapon_wheel_reset";

inline constexpr std::array<HintShortcut, 4> kHintShortcuts{{
    {2, 0x21, 'F', HintButton::LeftPrimary, {}},
    {3, 0x2D, 'X', HintButton::LeftSecondary, {}},
    {3, 0x13, 'R', HintButton::LeftSecondary, kWheelResetLabel},
    {7, 0x14, 'T', HintButton::RightStickClick, {}},
}};

// The shortcut a bar button in `slot` on the game's key `gameKey` with the text id `label` is (an index into
// kHintShortcuts), or nothing. An empty `label`: the button's could not be read.
std::optional<std::size_t> hintShortcut(std::int32_t slot, std::int32_t gameKey, std::string_view label);

// True when a shortcut on `slot` and `gameKey` needs the bar button's text id (HintShortcut::label).
bool hintShortcutWantsLabel(std::int32_t slot, std::int32_t gameKey);

// Seconds since a bar button's last update from two microsecond stamps (window_timing): negative for never
// (`at` 0), 0 for a stamp newer than `now` (another thread stamped it after `now` was read).
double hintSinceShown(std::uint64_t now, std::uint64_t at);

struct HintKeysTuning {
    double shownFresh = 0.25; // a bar button's last update counts this long (about 20 game frames at 90 fps)
    double minHold = 0.06;    // a key is held at least this long (as the router's)
};

struct HintKeysInput {
    double seconds = 0.0; // a steady clock
    // Per shortcut: seconds since a visible bar button showed it; negative: never, or the controller has no
    // name for its button (then it is neither named nor sent).
    std::array<double, kHintShortcuts.size()> sinceShown{-1.0, -1.0, -1.0, -1.0};
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

    [[nodiscard]] bool latched(std::size_t shortcut) const {
        return buttons_[shortcut].pressed || buttons_[shortcut].keyDown;
    }

    HintKeysTuning tuning_;
    std::array<Button, kHintShortcuts.size()> buttons_{};
};

// The game's pause held back while a shortcut has the left Menu button (a controller without a left secondary
// button): the mapper sends the pause on the button's tap, on its release, so the hold lasts while a shortcut
// has the button, through a press begun then, and `afterRelease` past its release. A shortcut that stops
// showing without a press ends it at once. Pure; the presenter runs it once per XR frame.
class MenuPauseHold {
public:
    explicit MenuPauseHold(double afterRelease = 0.5) : afterRelease_(afterRelease) {}

    // `taken`: a shortcut on the left Menu button has it (HintKeysOutput::taken); `held`: the button is down.
    // True while the pause is held back.
    bool update(double seconds, bool taken, bool held);

private:
    double afterRelease_;
    bool press_ = false;       // a press begun while a shortcut had the button, until let go
    double releasedAt_ = -1.0; // when such a press was let go; negative: none
};

} // namespace evr::menu
