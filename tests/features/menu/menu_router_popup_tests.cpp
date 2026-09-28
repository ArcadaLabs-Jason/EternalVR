#include "features/menu/menu_router.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string_view>
#include <utility>
#include <vector>

using evr::game::GameAction;
using evr::game::GameActionSet;
using evr::menu::CursorPixel;
using evr::menu::kKeyContinue;
using evr::menu::MenuRouter;
using evr::menu::popupActionKey;
using evr::menu::RouterEvent;
using evr::menu::RouterInput;
using evr::menu::RouterOutput;

namespace {

using Kind = RouterEvent::Kind;

constexpr std::size_t kLeft = 0;
constexpr std::size_t kRight = 1;

// A tutorial popup (or, with `popup` false, an ordinary menu) on a 1000 x 500 GUI, the cursor where the
// right hand points, with `held` the actions the controllers hold.
RouterInput popupFrame(double t, std::vector<GameAction> held = {}, bool popup = true) {
    RouterInput in;
    in.seconds = t;
    in.menuActive = true;
    in.popup = popup;
    in.width = 1000;
    in.height = 500;
    in.gameCursor = CursorPixel{500, 250};
    evr::menu::PanelHit hit;
    hit.u = 0.5f;
    hit.v = 0.5f;
    hit.distance = 1.5f;
    in.hands[kRight].hit = hit;
    for (const GameAction action : held) {
        evr::game::add(in.actions, action);
    }
    return in;
}

int count(const RouterOutput& out, Kind kind, std::uint8_t key) {
    int n = 0;
    for (const RouterEvent& e : out.events) {
        n += e.kind == kind && e.key == key ? 1 : 0;
    }
    return n;
}

bool anyKey(const RouterOutput& out) {
    for (const RouterEvent& e : out.events) {
        if (e.kind == Kind::KeyDown || e.kind == Kind::KeyUp) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("router: popup: the Flame Belch presses its key R down, and up when it is let go") {
    MenuRouter router;
    router.update(popupFrame(1.0));
    const RouterOutput down = router.update(popupFrame(1.1, {GameAction::FlameBelch}));
    CHECK(count(down, Kind::KeyDown, 'R') == 1);
    REQUIRE(down.popupKeys.size() == 1);
    CHECK(down.popupKeys[0].action == GameAction::FlameBelch);
    CHECK(down.popupKeys[0].key == 'R');
    // Held: nothing more.
    const RouterOutput held = router.update(popupFrame(1.3, {GameAction::FlameBelch}));
    CHECK_FALSE(anyKey(held));
    const RouterOutput up = router.update(popupFrame(1.4));
    CHECK(count(up, Kind::KeyUp, 'R') == 1);
    CHECK(up.suppressGameplay);
}

TEST_CASE("router: popup: switch weapon mod presses F, a tap is held at least minHold") {
    MenuRouter router;
    router.update(popupFrame(1.0));
    const RouterOutput down = router.update(popupFrame(1.01, {GameAction::SwitchWeaponMod}));
    CHECK(count(down, Kind::KeyDown, 'F') == 1);
    // Let go the next frame: the key stays down until minHold (0.06 s) has passed.
    CHECK(count(router.update(popupFrame(1.02)), Kind::KeyUp, 'F') == 0);
    CHECK(count(router.update(popupFrame(1.08)), Kind::KeyUp, 'F') == 1);
}

TEST_CASE("router: popup: the other gameplay actions press their default keys") {
    const std::vector<std::pair<GameAction, std::uint8_t>> expected{
        {GameAction::Dash, 0xA0},           {GameAction::Chainsaw, 'C'},    {GameAction::Equipment, 0xA2},
        {GameAction::SwitchEquipment, 'G'}, {GameAction::QuickSwitch, 'Q'}, {GameAction::WeaponWheel, 'Q'},
        {GameAction::WeaponSlot3, '3'},     {GameAction::Crucible, 'V'},
    };
    for (const auto& [action, key] : expected) {
        CAPTURE(evr::game::gameActionName(action));
        MenuRouter router;
        router.update(popupFrame(1.0));
        const RouterOutput out = router.update(popupFrame(1.1, {action}));
        CHECK(count(out, Kind::KeyDown, key) == 1);
    }
}

TEST_CASE("router: popup: the same action outside a popup sends nothing through the router") {
    SUBCASE("in gameplay") {
        MenuRouter router;
        RouterInput in;
        in.seconds = 1.0;
        router.update(in);
        evr::game::add(in.actions, GameAction::FlameBelch);
        in.seconds = 1.1;
        const RouterOutput out = router.update(in);
        CHECK(out.events.empty());
        CHECK(out.popupKeys.empty());
    }
    SUBCASE("in a menu the controllers asked for") {
        MenuRouter router;
        router.update(popupFrame(1.0, {}, false));
        const RouterOutput out = router.update(popupFrame(1.1, {GameAction::FlameBelch}, false));
        CHECK_FALSE(anyKey(out));
        CHECK(out.popupKeys.empty());
    }
}

TEST_CASE("router: popup: a key an action holds goes up when the popup closes") {
    MenuRouter router;
    router.update(popupFrame(1.0));
    router.update(popupFrame(1.1, {GameAction::Chainsaw}));
    RouterInput closed;
    closed.seconds = 1.2;
    evr::game::add(closed.actions, GameAction::Chainsaw); // still held
    const RouterOutput out = router.update(closed);
    CHECK(count(out, Kind::KeyUp, 'C') == 1);
    // Nothing more once it is let go.
    closed.actions.reset();
    closed.seconds = 1.3;
    CHECK_FALSE(anyKey(router.update(closed)));
}

TEST_CASE("router: popup: an action held when the popup comes up waits for a new press") {
    MenuRouter router;
    RouterInput gameplay;
    gameplay.seconds = 1.0;
    evr::game::add(gameplay.actions, GameAction::Equipment);
    router.update(gameplay);
    CHECK_FALSE(anyKey(router.update(popupFrame(1.1, {GameAction::Equipment}))));
    router.update(popupFrame(1.2));
    CHECK(count(router.update(popupFrame(1.3, {GameAction::Equipment})), Kind::KeyDown, 0xA2) == 1);
}

TEST_CASE("router: popup: the Dossier button presses TAB for the tutorial that asks for it") {
    const std::optional<std::uint8_t> key = popupActionKey(GameAction::Dossier);
    REQUIRE(key.has_value());
    CHECK(*key == 0x09);
}

TEST_CASE("router: popup: movement, fire, aim and the menu actions are not forwarded") {
    CHECK_FALSE(popupActionKey(GameAction::Fire).has_value());
    CHECK_FALSE(popupActionKey(GameAction::WeaponMod).has_value());
    CHECK_FALSE(popupActionKey(GameAction::NextWeapon).has_value());
    CHECK_FALSE(popupActionKey(GameAction::PreviousWeapon).has_value());
    CHECK_FALSE(popupActionKey(GameAction::Pause).has_value());
    CHECK_FALSE(popupActionKey(GameAction::Automap).has_value());
    CHECK_FALSE(popupActionKey(GameAction::Recenter).has_value());

    MenuRouter router;
    router.update(popupFrame(1.0));
    // The move stick pushed forward and the fire and weapon mod actions held: no W, no keys at all (the
    // sticks' own scrolling in a popup is the wheel, not a key).
    RouterInput in = popupFrame(1.1, {GameAction::Fire, GameAction::WeaponMod, GameAction::NextWeapon});
    in.hands[kLeft].stick = {0.0f, 1.0f};
    const RouterOutput out = router.update(in);
    CHECK_FALSE(anyKey(out));
    CHECK(count(out, Kind::KeyDown, 'W') == 0);
    CHECK(out.popupKeys.empty());
}

TEST_CASE("router: popup: jump and melee keep their buttons' keys, sent once") {
    CHECK_FALSE(popupActionKey(GameAction::Jump).has_value());        // Space: A / X
    CHECK_FALSE(popupActionKey(GameAction::Melee).has_value());       // E: a stick click
    CHECK_FALSE(popupActionKey(GameAction::MissionInfo).has_value()); // Left Alt: Y

    // A pressed: its Space, and the jump action it maps to adds nothing.
    MenuRouter router;
    router.update(popupFrame(1.0));
    RouterInput in = popupFrame(1.1, {GameAction::Jump});
    in.hands[kRight].primary = true;
    const RouterOutput out = router.update(in);
    CHECK(count(out, Kind::KeyDown, kKeyContinue) == 1);
    CHECK(out.popupKeys.empty());
}

TEST_CASE("router: popup: a key already down is not pressed again") {
    // The left grip taps Q (previous tab) while quick switch, also Q, is pressed.
    MenuRouter router;
    router.update(popupFrame(1.0));
    RouterInput in = popupFrame(1.1, {GameAction::QuickSwitch});
    in.hands[kLeft].grip = 1.0f;
    const RouterOutput out = router.update(in);
    CHECK(count(out, Kind::KeyDown, 'Q') == 1);
}

TEST_CASE("router: popup: each action is logged once per popup") {
    MenuRouter router;
    router.update(popupFrame(1.0));
    CHECK(router.update(popupFrame(1.1, {GameAction::FlameBelch})).popupKeys.size() == 1);
    router.update(popupFrame(1.2));
    const RouterOutput again = router.update(popupFrame(1.3, {GameAction::FlameBelch}));
    CHECK(count(again, Kind::KeyDown, 'R') == 1);
    CHECK(again.popupKeys.empty());
    // A new popup logs it again.
    RouterInput closed;
    closed.seconds = 1.4;
    router.update(closed);
    router.update(popupFrame(1.5));
    CHECK(router.update(popupFrame(1.6, {GameAction::FlameBelch})).popupKeys.size() == 1);
}
