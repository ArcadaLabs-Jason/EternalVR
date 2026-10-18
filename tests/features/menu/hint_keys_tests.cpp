#include "features/menu/hint_keys.hpp"
#include "features/menu/menu_router.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

using evr::menu::HintButton;
using evr::menu::HintKey;
using evr::menu::HintKeys;
using evr::menu::HintKeysInput;
using evr::menu::HintKeysOutput;
using evr::menu::hintShortcut;
using evr::menu::kHintShortcuts;
using evr::menu::kKeyEscape;
using evr::menu::MenuRouter;
using evr::menu::RouterEvent;
using evr::menu::RouterInput;
using evr::menu::RouterOutput;

namespace {

constexpr std::size_t kF = 0; // the shortcuts' indices: F customize weapon (slot 2)
constexpr std::size_t kX = 1; // X equip / unequip mod (slot 3)
constexpr std::size_t kT = 2; // T customize weapon wheel (slot 7)

// The Dossier's Arsenal as the bar button hook saw it: every shortcut shown `since` seconds ago, or only one.
HintKeysInput arsenal(double t, double since = 0.01) {
    HintKeysInput in;
    in.seconds = t;
    in.menuUp = true;
    in.sinceShown = {since, since, since};
    return in;
}

HintKeysInput holding(HintKeysInput in, std::size_t shortcut) {
    in.held[shortcut] = true;
    return in;
}

int count(const std::vector<HintKey>& keys, std::uint8_t key, bool down) {
    int n = 0;
    for (const HintKey& k : keys) {
        n += k.key == key && k.down == down ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("hint keys: the Arsenal's shortcuts by slot and key, in the pad's places") {
    // The rig's Arsenal bar (2026-10-18): slot 0 MOUSE1 select, 1 Escape back, 2 F, 7 T, 3 X.
    CHECK(hintShortcut(2, 0x21) == kF);
    CHECK(hintShortcut(3, 0x2D) == kX);
    CHECK(hintShortcut(7, 0x14) == kT);
    CHECK(kHintShortcuts[kF].virtualKey == 'F');
    CHECK(kHintShortcuts[kX].virtualKey == 'X');
    CHECK(kHintShortcuts[kT].virtualKey == 'T');
    // Pad X (JOY3), Y (JOY4) and the right stick's click (JOY8): Touch's own X, Y and right stick click.
    CHECK(kHintShortcuts[kF].button == HintButton::LeftPrimary);
    CHECK(kHintShortcuts[kX].button == HintButton::LeftSecondary);
    CHECK(kHintShortcuts[kT].button == HintButton::RightStickClick);
    // The Dossier map's C (centre) is in slot 7 too; the sticks' clicks already send it there. Select, back
    // and a key in another slot are not shortcuts.
    CHECK_FALSE(hintShortcut(7, 0x2E).has_value());
    CHECK_FALSE(hintShortcut(0, 0x11E).has_value());
    CHECK_FALSE(hintShortcut(1, 0x01).has_value());
    CHECK_FALSE(hintShortcut(4, 0x21).has_value());
}

TEST_CASE(
    "hint keys: a shortcut's button sends its key while the bar shows it, and the router does not see it") {
    HintKeys keys;
    HintKeysOutput out = keys.update(arsenal(0.0));
    CHECK(out.keys.empty());
    CHECK(out.taken[kF]);
    CHECK(out.taken[kX]);
    CHECK(out.taken[kT]);
    out = keys.update(holding(arsenal(0.011), kX));
    CHECK(count(out.keys, 'X', true) == 1);
    out = keys.update(holding(arsenal(0.022), kX));
    CHECK(out.keys.empty());
    // Let go before the minimum hold: the key goes up once it has been down 0.06 s.
    out = keys.update(arsenal(0.033));
    CHECK(out.keys.empty());
    CHECK(out.taken[kX]);
    out = keys.update(arsenal(0.1));
    CHECK(count(out.keys, 'X', false) == 1);
    // The left primary sends F, the right stick's click T.
    out = keys.update(holding(holding(arsenal(0.2), kF), kT));
    CHECK(count(out.keys, 'F', true) == 1);
    CHECK(count(out.keys, 'T', true) == 1);
    out = keys.update(arsenal(0.3));
    CHECK(count(out.keys, 'F', false) == 1);
    CHECK(count(out.keys, 'T', false) == 1);
}

TEST_CASE("hint keys: nothing when the bar has not shown the shortcut lately, or no menu is up") {
    HintKeys keys;
    // The bar button's last update 0.3 s ago: the bar is gone (another page, the Dossier closed).
    HintKeysOutput out = keys.update(holding(arsenal(0.0, 0.3), kF));
    CHECK(out.keys.empty());
    CHECK_FALSE(out.taken[kF]);
    // Never shown, or the controller has no name for the button (the vkcore side passes -1).
    out = keys.update(holding(arsenal(0.1, -1.0), kX));
    CHECK(out.keys.empty());
    CHECK_FALSE(out.taken[kX]);
    // A popup, or the game may not be touched.
    HintKeysInput popup = holding(arsenal(0.2), kT);
    popup.menuUp = false;
    out = keys.update(popup);
    CHECK(out.keys.empty());
    CHECK_FALSE(out.taken[kT]);
    HintKeysInput guarded = holding(arsenal(0.3), kT);
    guarded.allowed = false;
    CHECK(keys.update(guarded).keys.empty());
}

TEST_CASE("hint keys: a press begun before the bar showed counts only once let go") {
    HintKeys keys;
    // Y held from before (the Dossier opened with it down): no X when the bar comes up.
    HintKeysOutput out = keys.update(holding(arsenal(0.0, -1.0), kX));
    CHECK(out.keys.empty());
    out = keys.update(holding(arsenal(0.1), kX));
    CHECK(out.keys.empty());
    CHECK(out.taken[kX]);
    out = keys.update(arsenal(0.2));
    out = keys.update(holding(arsenal(0.3), kX));
    CHECK(count(out.keys, 'X', true) == 1);
}

TEST_CASE("hint keys: a key down stays down until its button is let go, the button kept from the router") {
    HintKeys keys;
    keys.update(holding(arsenal(0.0), kF));
    // F opened the weapon skins, whose bar shows F (clear skin) again; then the bar is gone for a while.
    HintKeysOutput out = keys.update(holding(arsenal(0.5, 0.4), kF));
    CHECK(out.keys.empty());
    CHECK(out.taken[kF]);
    out = keys.update(arsenal(0.6, 0.5));
    CHECK(count(out.keys, 'F', false) == 1);
    CHECK_FALSE(out.taken[kF]);
    // A multiplayer guard trip lets the key go at once (after the minimum hold).
    keys.update(holding(arsenal(1.0), kT));
    HintKeysInput guarded = holding(arsenal(1.1), kT);
    guarded.allowed = false;
    out = keys.update(guarded);
    CHECK(count(out.keys, 'T', false) == 1);
    // release(): every key up; the held button sends nothing until let go.
    keys.update(holding(arsenal(2.0), kX));
    const std::vector<HintKey> up = keys.release();
    CHECK(count(up, 'X', false) == 1);
    CHECK(keys.update(holding(arsenal(2.1), kX)).keys.empty());
    keys.update(arsenal(2.2));
    CHECK(count(keys.update(holding(arsenal(2.3), kX)).keys, 'X', true) == 1);
}

namespace {

// The presenter's order: the hint keys first, their taken buttons cleared, then the router.
struct Frame {
    std::vector<HintKey> keys;
    RouterOutput router;
};

Frame step(HintKeys& keys, MenuRouter& router, RouterInput in, double since) {
    HintKeysInput hin = arsenal(in.seconds, since);
    hin.menuUp = in.menuActive && !in.popup;
    bool* buttons[] = {&in.hands[0].primary, &in.hands[0].secondary, &in.hands[1].stickClick};
    for (std::size_t i = 0; i < kHintShortcuts.size(); ++i) {
        hin.held[i] = *buttons[i];
    }
    const HintKeysOutput out = keys.update(hin);
    for (std::size_t i = 0; i < kHintShortcuts.size(); ++i) {
        if (out.taken[i]) {
            *buttons[i] = false;
        }
    }
    return {out.keys, router.update(in)};
}

// The Dossier on a 1000 x 500 GUI, no hand on the panel.
RouterInput dossier(double t) {
    RouterInput in;
    in.seconds = t;
    in.menuActive = true;
    in.dossier = true;
    in.width = 1000;
    in.height = 500;
    in.gameCursor = evr::menu::CursorPixel{500, 250};
    in.pageKeys = evr::menu::kPageArrowKeys; // the Arsenal's weapon row is on A / D
    return in;
}

int routerKeys(const RouterOutput& out, std::uint8_t key) {
    int n = 0;
    for (const RouterEvent& e : out.events) {
        n += e.kind == RouterEvent::Kind::KeyDown && e.key == key ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("hint keys: on the Arsenal Y equips the mod instead of going back, the right stick's click opens "
          "the weapon wheel instead of the next weapon") {
    HintKeys keys;
    MenuRouter router;
    step(keys, router, dossier(0.0), 0.01);
    // The router counts the Dossier's pages from the map; put it on the Arsenal with the right grip.
    RouterInput grip = dossier(0.1);
    grip.hands[1].grip = 1.0f;
    step(keys, router, grip, 0.01);
    step(keys, router, dossier(0.3), 0.01);
    RouterInput y = dossier(1.0);
    y.hands[0].secondary = true;
    Frame f = step(keys, router, y, 0.01);
    CHECK(count(f.keys, 'X', true) == 1);
    CHECK(routerKeys(f.router, kKeyEscape) == 0);
    step(keys, router, dossier(1.2), 0.01);
    RouterInput click = dossier(2.0);
    click.hands[1].stickClick = true;
    f = step(keys, router, click, 0.01);
    CHECK(count(f.keys, 'T', true) == 1);
    CHECK(routerKeys(f.router, 'D') == 0);
    step(keys, router, dossier(2.2), 0.01);
    // Without the Arsenal's bar (Codex, Challenges) Y goes back and the stick's click is the next tab.
    RouterInput codexClick = dossier(3.0);
    codexClick.hands[1].stickClick = true;
    f = step(keys, router, codexClick, -1.0);
    CHECK(f.keys.empty());
    CHECK(routerKeys(f.router, 'D') == 1);
    step(keys, router, dossier(3.2), -1.0);
    RouterInput back = dossier(4.0);
    back.hands[0].secondary = true;
    f = step(keys, router, back, -1.0);
    CHECK(routerKeys(f.router, kKeyEscape) == 1);
}
