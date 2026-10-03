#include "features/menu/menu_router.hpp"

#include "game/eternal/usercmd_buttons.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace evr::menu {

namespace {

std::size_t index(input::Hand hand) {
    return static_cast<std::size_t>(hand);
}

// An analog control with hysteresis: pressed above `press`, released again below `release`.
bool analogHeld(bool wasHeld, float value, float press, float release) {
    if (!std::isfinite(value)) {
        return false;
    }
    return wasHeld ? value > release : value > press;
}

// The direction a stick is held along one axis: -1, 0 or +1.
int direction(float value, float threshold) {
    if (!std::isfinite(value)) {
        return 0;
    }
    return value > threshold ? 1 : (value < -threshold ? -1 : 0);
}

RouterEvent event(RouterEvent::Kind kind) {
    RouterEvent e;
    e.kind = kind;
    return e;
}

RouterEvent keyEvent(RouterEvent::Kind kind, std::uint8_t key) {
    RouterEvent e = event(kind);
    e.key = key;
    return e;
}

RouterEvent mapEvent(const MapEvent& m) {
    switch (m.kind) {
    case MapEvent::Kind::Button: {
        const bool left = m.button == DragButton::Left;
        if (m.down) {
            return event(left ? RouterEvent::Kind::ButtonDown : RouterEvent::Kind::RightButtonDown);
        }
        return event(left ? RouterEvent::Kind::ButtonUp : RouterEvent::Kind::RightButtonUp);
    }
    case MapEvent::Kind::Key: {
        // The pan keys change many times a second while a stick is held part of the way.
        RouterEvent e = keyEvent(m.down ? RouterEvent::Kind::KeyDown : RouterEvent::Kind::KeyUp, m.key);
        e.quiet = true;
        return e;
    }
    case MapEvent::Kind::Move:
        break;
    }
    RouterEvent move = event(RouterEvent::Kind::Move);
    move.dx = m.dx;
    move.dy = m.dy;
    return move;
}

void sendMapEvents(const std::vector<MapEvent>& events, RouterOutput& out) {
    for (const MapEvent& m : events) {
        out.events.push_back(mapEvent(m));
    }
}

} // namespace

std::optional<std::uint8_t> popupActionKey(game::GameAction action) {
    // Pause stays on B and the Menu button. The Dossier's key is forwarded like the others: a tutorial asks
    // for TAB to open the Dossier, and only the Dossier button (X held) can answer it.
    if (action == game::GameAction::Pause) {
        return std::nullopt;
    }
    const std::optional<std::uint8_t> key = game::defaultKey(action);
    if (!key || *key == kKeyContinue || *key == kKeyUse || *key == kKeyObjectives) {
        return std::nullopt;
    }
    return key;
}

MenuRouter::MenuRouter(input::Hand dominant, input::MapSticks mapSticks, RouterTuning tuning)
    : dominant_(dominant), mapSticks_(mapSticks), tuning_(tuning), pointer_(dominant), drag_(tuning.map),
      cursorHide_(tuning.mapCursor) {}

bool MenuRouter::repeat(Repeater& r, int dir, double now, double delay, double interval) {
    if (dir == 0) {
        r.direction = 0;
        return false;
    }
    if (dir != r.direction) {
        r.direction = dir;
        r.next = now + delay;
        return true;
    }
    if (now >= r.next) {
        r.next = now + interval;
        return true;
    }
    return false;
}

void MenuRouter::tapKey(std::uint8_t key, double now, RouterOutput& out) {
    if (keyHeld(key)) {
        return; // still down from the last tap (or held by an action); it goes up first
    }
    out.events.push_back(keyEvent(RouterEvent::Kind::KeyDown, key));
    keys_.push_back({key, now + tuning_.minHold});
}

bool MenuRouter::keyHeld(std::uint8_t key) const {
    if (altDown_ && key == kKeyObjectives) {
        return true;
    }
    for (const HeldKey& held : keys_) {
        if (held.key == key) {
            return true;
        }
    }
    for (std::size_t i = 0; i < game::kGameActionCount; ++i) {
        if (actionKeys_.test(i) && popupActionKey(static_cast<game::GameAction>(i)) == key) {
            return true;
        }
    }
    return false;
}

void MenuRouter::popupActions(const RouterInput& in,
                              const game::GameActionSet& pressed,
                              double now,
                              RouterOutput& out) {
    if (!in.popup) {
        releaseActionKeys(out);
        return;
    }
    // A gameplay action pressed in the popup presses its default key and holds it while the action is held
    // (at least minHold, as a tap does). One already held when the popup came up has to be pressed again,
    // as the buttons do; a key already down from another press is not sent twice.
    for (std::size_t i = 0; i < game::kGameActionCount; ++i) {
        const auto action = static_cast<game::GameAction>(i);
        const std::optional<std::uint8_t> key = popupActionKey(action);
        const bool held = in.actions.test(i);
        if (!key) {
            continue;
        }
        if (actionKeys_.test(i)) {
            if (!held && now - actionKeyAt_[i] >= tuning_.minHold) {
                out.events.push_back(keyEvent(RouterEvent::Kind::KeyUp, *key));
                actionKeys_.reset(i);
            }
            continue;
        }
        if (!pressed.test(i) || keyHeld(*key)) {
            continue;
        }
        out.events.push_back(keyEvent(RouterEvent::Kind::KeyDown, *key));
        actionKeys_.set(i);
        actionKeyAt_[i] = now;
        if (!actionsLogged_.test(i)) {
            actionsLogged_.set(i);
            out.popupKeys.push_back({action, *key});
        }
    }
}

void MenuRouter::releaseActionKeys(RouterOutput& out) {
    for (std::size_t i = 0; i < game::kGameActionCount; ++i) {
        if (actionKeys_.test(i)) {
            if (const auto key = popupActionKey(static_cast<game::GameAction>(i))) {
                out.events.push_back(keyEvent(RouterEvent::Kind::KeyUp, *key));
            }
        }
    }
    actionKeys_.reset();
}

void MenuRouter::notePage(std::uint8_t key) {
    // The Dossier does not say which page is up; the router counts the tab keys it sends. Past either end
    // it no longer knows (the game may wrap or stop there), which only turns the map's sticks off.
    if (page_ < 0) {
        return;
    }
    if (key == kKeyNextTab) {
        page_ = page_ + 1 < kDossierPages ? page_ + 1 : -1;
    } else if (key == kKeyPreviousTab) {
        page_ = page_ > 0 ? page_ - 1 : -1;
    }
}

void MenuRouter::finishKeys(double now, RouterOutput& out) {
    for (auto it = keys_.begin(); it != keys_.end();) {
        if (now >= it->upAt) {
            out.events.push_back(keyEvent(RouterEvent::Kind::KeyUp, it->key));
            it = keys_.erase(it);
        } else {
            ++it;
        }
    }
}

void MenuRouter::releaseAll(double /*now*/, RouterOutput& out) {
    sendMapEvents(drag_.reset(), out);
    dragTarget_.reset();
    if (buttonDown_) {
        out.events.push_back(event(RouterEvent::Kind::ButtonUp));
    }
    buttonDown_ = false;
    clickWanted_ = false;
    releaseWanted_ = false;
    for (const HeldKey& held : keys_) {
        out.events.push_back(keyEvent(RouterEvent::Kind::KeyUp, held.key));
    }
    keys_.clear();
    if (altDown_) {
        out.events.push_back(keyEvent(RouterEvent::Kind::KeyUp, kKeyObjectives));
        altDown_ = false;
    }
    releaseActionKeys(out);
    actionsLogged_.reset();
    movePending_ = false;
    landedAt_ = -1.0;
    scroll_ = {};
    tabs_ = {};
    page_ = -1;
}

RouterOutput MenuRouter::releaseHeld(double now) {
    RouterOutput out;
    releaseAll(now, out);
    return out;
}

bool MenuRouter::holdsInput() const {
    return buttonDown_ || !keys_.empty() || altDown_ || actionKeys_.any() || drag_.active();
}

bool MenuRouter::allReleased(const RouterInput& in) const {
    for (const RouterHand& h : in.hands) {
        if ((std::isfinite(h.trigger) && h.trigger > tuning_.releaseThreshold) ||
            (std::isfinite(h.grip) && h.grip > tuning_.releaseThreshold) || h.primary || h.secondary ||
            h.stickClick) {
            return false;
        }
    }
    return true;
}

void MenuRouter::moveCursor(const RouterInput& in,
                            std::optional<CursorPixel> target,
                            double now,
                            RouterOutput& out) {
    if (movePending_ || !target || !in.gameCursor || *target == *in.gameCursor) {
        return;
    }
    RouterEvent move = event(RouterEvent::Kind::Move);
    move.dx = target->x - in.gameCursor->x;
    move.dy = target->y - in.gameCursor->y;
    out.events.push_back(move);
    expected_ = *target;
    movePending_ = true;
    movedAt_ = now;
}

void MenuRouter::mapSticks(const RouterInput& in, double now, RouterOutput& out) {
    // One stick pans; the other zooms (up / down, the wheel) and rotates (left / right). The weapon hand's
    // stick pans unless the player chose the other one (ETERNALVR_MAP_STICKS=other).
    const input::Hand pan =
        mapSticks_ == input::MapSticks::OtherPans ? input::otherHand(dominant_) : dominant_;
    const std::size_t panStick = index(pan);
    const std::size_t turnStick = index(input::otherHand(pan));
    const input::Axis2 turn = in.hands[turnStick].stick;
    // The sticks move the map only while the left button is not held by a click; a drag going on ends.
    const bool free = !buttonDown_ && !clickWanted_;
    if (free || drag_.active()) {
        MapDragInput drag;
        drag.seconds = now;
        if (free) {
            drag.pan = in.hands[panStick].stick;
            drag.turn = turn;
        }
        drag.cursor = in.gameCursor;
        drag.cursorIdle = !movePending_;
        drag.width = in.width;
        drag.height = in.height;
        const MapDragOutput step = drag_.update(drag);
        dragTarget_ = step.target;
        sendMapEvents(step.events, out);
        // The drag's own motion moves the game's cursor: the closed loop starts again from where the game
        // shows it.
        if (std::any_of(step.events.begin(), step.events.end(),
                        [](const MapEvent& e) { return e.kind == MapEvent::Kind::Move; })) {
            movePending_ = false;
        }
    }
    int zoom = 0;
    if (std::isfinite(turn.x) && std::isfinite(turn.y) && std::fabs(turn.y) >= std::fabs(turn.x)) {
        zoom = direction(turn.y, tuning_.stickThreshold);
    }
    if (repeat(scroll_[turnStick], zoom, now, tuning_.zoomInterval, tuning_.zoomInterval)) {
        RouterEvent wheel = event(RouterEvent::Kind::Wheel);
        wheel.wheel = static_cast<std::int16_t>(zoom * kWheelNotch);
        out.events.push_back(wheel);
    }
    scroll_[panStick] = {};
    tabs_ = {};
}

void MenuRouter::menuSticks(const RouterInput& in, double now, RouterOutput& out) {
    // Sticks, either hand: up and down scroll (the wheel), left and right change tabs. Only the stronger
    // axis of a stick counts.
    for (std::size_t i = 0; i < 2; ++i) {
        const input::Axis2 stick = in.hands[i].stick;
        int vertical = 0;
        int horizontal = 0;
        if (std::fabs(stick.y) >= std::fabs(stick.x)) {
            vertical = direction(stick.y, tuning_.stickThreshold);
        } else {
            horizontal = direction(stick.x, tuning_.stickThreshold);
        }
        if (repeat(scroll_[i], vertical, now, tuning_.repeatDelay, tuning_.repeatInterval)) {
            RouterEvent wheel = event(RouterEvent::Kind::Wheel);
            wheel.wheel = static_cast<std::int16_t>(vertical * kWheelNotch);
            out.events.push_back(wheel);
        }
        if (repeat(tabs_[i], horizontal, now, tuning_.repeatDelay, tuning_.repeatInterval)) {
            const std::uint8_t key = horizontal > 0 ? kKeyNextTab : kKeyPreviousTab;
            tapKey(key, now, out);
            notePage(key);
        }
    }
}

RouterOutput MenuRouter::update(const RouterInput& in) {
    RouterOutput out;
    const double now = in.seconds;

    // Held states with hysteresis, and which of them started this frame. They are followed even while no
    // menu is up, so a control already held when a menu opens does not count as a press in it.
    std::array<bool, 2> triggerEdge{};
    std::array<bool, 2> gripEdge{};
    std::array<bool, 2> primaryEdge{};
    std::array<bool, 2> secondaryEdge{};
    std::array<bool, 2> stickClickEdge{};
    for (std::size_t i = 0; i < 2; ++i) {
        const RouterHand& h = in.hands[i];
        const bool trigger =
            analogHeld(trigger_[i], h.trigger, tuning_.pressThreshold, tuning_.releaseThreshold);
        const bool grip = analogHeld(grip_[i], h.grip, tuning_.pressThreshold, tuning_.releaseThreshold);
        triggerEdge[i] = trigger && !trigger_[i];
        gripEdge[i] = grip && !grip_[i];
        primaryEdge[i] = h.primary && !primary_[i];
        secondaryEdge[i] = h.secondary && !secondary_[i];
        stickClickEdge[i] = h.stickClick && !stickClick_[i];
        trigger_[i] = trigger;
        grip_[i] = grip;
        primary_[i] = h.primary;
        secondary_[i] = h.secondary;
        stickClick_[i] = h.stickClick;
    }
    const game::GameActionSet actionsPressed = in.actions & ~actions_;
    actions_ = in.actions;

    if (!in.menuActive) {
        cursorHide_.reset();
        if (wasActive_) {
            wasActive_ = false;
            latch_ = true;
            releaseAll(now, out);
        }
        finishKeys(now, out);
        if (latch_ && allReleased(in)) {
            latch_ = false;
        }
        out.suppressGameplay = latch_;
        out.pointerHand = pointer_;
        out.pointerVisible = false;
        return out;
    }
    if (!wasActive_) {
        wasActive_ = true;
        pointer_ = dominant_;
        movePending_ = false;
        landedAt_ = -1.0;
        page_ = in.dossier && !in.popup ? 0 : -1;
        actionsLogged_.reset();
    }
    latch_ = true;

    // The pointer follows the hand that last pulled its trigger (or pressed A / X) while pointing at the
    // panel.
    // In a popup A / X is its continue key, not a click.
    const bool primaryClicks = !in.popup;
    const input::Hand other = input::otherHand(pointer_);
    const std::size_t o = index(other);
    if ((triggerEdge[o] || (primaryClicks && primaryEdge[o])) && in.hands[o].hit) {
        pointer_ = other;
    }
    const std::size_t p = index(pointer_);
    const RouterHand& hand = in.hands[p];

    // The cursor: one move in flight at a time, the next once the game shows the last one.
    if (movePending_) {
        if (in.gameCursor && *in.gameCursor == expected_) {
            movePending_ = false;
            landedAt_ = now;
        } else if (now - movedAt_ > tuning_.moveTimeout) {
            movePending_ = false;
            landedAt_ = now;
        }
    }

    // The sticks: on the map page they move the map (a drag owns the cursor while it lasts), elsewhere they
    // scroll and change tabs.
    const bool mapPage = page_ == 0 && !in.popup;
    if (mapPage) {
        mapSticks(in, now, out);
    } else {
        sendMapEvents(drag_.reset(), out);
        dragTarget_.reset();
        menuSticks(in, now, out);
    }
    const bool dragging = drag_.active();
    std::optional<CursorPixel> target = dragTarget_;
    if (!dragging && hand.hit && in.width > 0 && in.height > 0) {
        target = cursorPixel(hand.hit->u, hand.hit->v, in.width, in.height);
    }
    moveCursor(in, target, now, out);
    DragCursorHideInput hide;
    hide.seconds = now;
    hide.menuActive = true;
    hide.dragOwnsCursor = dragging;
    hide.cursorSettled = !movePending_;
    out.hideCursor = cursorHide_.update(hide);

    // The left button: pressed once the cursor has settled where the ray points, held at least minHold. Not
    // while a stick drags the map (the drag has the button then).
    const bool clickHeld = trigger_[p] || (primaryClicks && primary_[p]);
    if ((triggerEdge[p] || (primaryClicks && primaryEdge[p])) && !buttonDown_ && !clickWanted_ && !dragging) {
        clickWanted_ = true;
        clickWantedAt_ = now;
        if (hand.onTabStrip) {
            page_ = -1; // a tab clicked: which page it opens is not known
        }
    }
    if (clickWanted_) {
        const bool settled = !movePending_ && (landedAt_ < 0.0 || now - landedAt_ >= tuning_.settle);
        if (settled || now - clickWantedAt_ > tuning_.clickTimeout) {
            out.events.push_back(event(RouterEvent::Kind::ButtonDown));
            buttonDown_ = true;
            buttonDownAt_ = now;
            clickWanted_ = false;
        }
    }
    if (buttonDown_ && !clickHeld) {
        releaseWanted_ = true;
    }
    if (releaseWanted_ && buttonDown_ && now - buttonDownAt_ >= tuning_.minHold) {
        out.events.push_back(event(RouterEvent::Kind::ButtonUp));
        buttonDown_ = false;
        releaseWanted_ = false;
    }

    // Back: B or Y, either hand. In a popup Y holds Left Alt instead, for as long as it is held (the
    // objectives key some tutorial popups wait for).
    const std::size_t left = index(input::Hand::Left);
    const std::size_t right = index(input::Hand::Right);
    // A map drag lets go before Escape, so the game never resumes with its button or W A S D still down.
    const auto back = [&] {
        sendMapEvents(drag_.reset(), out);
        dragTarget_.reset();
        tapKey(kKeyEscape, now, out);
    };
    if (in.popup && secondaryEdge[left] && !altDown_) {
        out.events.push_back(keyEvent(RouterEvent::Kind::KeyDown, kKeyObjectives));
        altDown_ = true;
        altDownAt_ = now;
    } else if (secondaryEdge[left]) {
        back();
    }
    if (altDown_ && !secondary_[left] && now - altDownAt_ >= tuning_.minHold) {
        out.events.push_back(keyEvent(RouterEvent::Kind::KeyUp, kKeyObjectives));
        altDown_ = false;
    }
    if (secondaryEdge[right]) {
        back();
    }

    // Tabs: the left grip goes to the previous tab (Q), the right grip to the next (E).
    if (gripEdge[left]) {
        tapKey(kKeyPreviousTab, now, out);
        notePage(kKeyPreviousTab);
    }
    if (gripEdge[right]) {
        tapKey(kKeyNextTab, now, out);
        notePage(kKeyNextTab);
    }

    // A stick click, either hand: C, the Dossier map's centre key; in a popup E, its use key. A click while
    // the other stick is already pressed is the recenter chord (both sticks held), not a key. A / X, either
    // hand, in a popup: Space, its continue key.
    for (std::size_t i = 0; i < 2; ++i) {
        if (stickClickEdge[i] && !in.hands[1 - i].stickClick) {
            tapKey(in.popup ? kKeyUse : kKeyCentre, now, out);
        }
    }
    if (in.popup && (primaryEdge[0] || primaryEdge[1])) {
        tapKey(kKeyContinue, now, out);
    }
    // In a popup the gameplay actions press their keys too: a tutorial waits for the key of the mechanic it
    // introduces.
    popupActions(in, actionsPressed, now, out);
    finishKeys(now, out);

    out.suppressGameplay = true;
    out.pointerHand = pointer_;
    out.pointerVisible = true;
    out.mapPage = page_ == 0 && !in.popup;
    out.mapPan = drag_.panBy();
    out.mapRotate = drag_.rotating();
    return out;
}

} // namespace evr::menu
