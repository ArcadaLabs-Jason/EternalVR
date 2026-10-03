#include "features/input/action_aim.hpp"

#include <cmath>
#include <initializer_list>

namespace evr::input {

namespace {

game::GameActionSet actionsOf(AimedAction action) {
    game::GameActionSet set;
    if (action == AimedAction::Melee) {
        game::add(set, game::GameAction::Melee);
    } else if (action == AimedAction::Equipment) {
        game::add(set, game::GameAction::Equipment);
        game::add(set, game::GameAction::FlameBelch);
    }
    return set;
}

} // namespace

const char* actionAimSourceName(ActionAimSource source) {
    switch (source) {
    case ActionAimSource::Same:
        return "weapon hand";
    case ActionAimSource::Head:
        return "head";
    case ActionAimSource::OffHand:
        return "off hand";
    }
    return "weapon hand";
}

const char* aimedActionName(AimedAction action) {
    switch (action) {
    case AimedAction::None:
        return "none";
    case AimedAction::Melee:
        return "melee";
    case AimedAction::Equipment:
        return "equipment";
    }
    return "none";
}

ActionAim::ActionAim(ActionAimSettings settings) : settings_(settings) {}

ActionAimSource ActionAim::sourceFor(AimedAction action) const {
    switch (action) {
    case AimedAction::Melee:
        return settings_.melee;
    case AimedAction::Equipment:
        return settings_.equipment;
    case AimedAction::None:
        break;
    }
    return ActionAimSource::Same;
}

void ActionAim::setTarget(ActionAimSource target, AimedAction action) {
    if (target != target_) {
        target_ = target;
        ++generation_;
    }
    action_ = target == ActionAimSource::Same ? AimedAction::None : action;
}

ActionAimOutput ActionAim::update(const ActionAimInput& input) {
    const float dt = std::isfinite(input.dtSeconds) && input.dtSeconds > 0.0f ? input.dtSeconds : 0.0f;
    ActionAimOutput out;
    out.actions = input.actions;
    const game::GameActionSet pressed = input.actions & ~previous_;
    previous_ = input.actions;
    if (!settings_.any()) {
        return out;
    }

    // A new press: melee wins when both go down on one command.
    AimedAction pressedAction = AimedAction::None;
    bool holdStarted = false;
    if ((pressed & actionsOf(AimedAction::Equipment)).any()) {
        pressedAction = AimedAction::Equipment;
    }
    if (game::contains(pressed, game::GameAction::Melee)) {
        pressedAction = AimedAction::Melee;
    }
    if (pressedAction != AimedAction::None) {
        const ActionAimSource want = sourceFor(pressedAction);
        const bool retarget = want != target_;
        if (retarget && heldBack_.any()) {
            // A press still waiting for the previous target goes out now, not with the next one.
            out.actions |= heldBack_;
            out.released = true;
            out.overtaken = true;
            out.pressed = heldAction_;
            out.waitedSeconds = waitedSeconds_ + dt;
            out.waitedCommands = waitedCommands_;
            heldBack_.reset();
        }
        if (retarget) {
            setTarget(want, pressedAction);
        } else if (want != ActionAimSource::Same) {
            action_ = pressedAction;
        }
        // Held back while the game's angles do not have its target yet: a new one, or one not written yet.
        if (input.retargetable && (retarget || heldBack_.any() || input.targetWritten < generation_)) {
            if (heldBack_.none()) {
                waitedSeconds_ = 0.0f;
                waitedCommands_ = 0;
                holdStarted = true;
            }
            heldBack_ |= pressed & actionsOf(pressedAction);
            heldAction_ = pressedAction;
        }
        keepSeconds_ = 0.0f;
    }

    // The target stays while an action that aims with it is held, the game forces the view or a press waits.
    if (action_ != AimedAction::None) {
        bool held = false;
        for (const AimedAction a : {AimedAction::Melee, AimedAction::Equipment}) {
            held = held || (sourceFor(a) == target_ && (input.actions & actionsOf(a)).any());
        }
        if (held || input.forcedView || heldBack_.any()) {
            keepSeconds_ = 0.0f;
        } else {
            keepSeconds_ += dt;
            if (keepSeconds_ >= kKeepSeconds) {
                setTarget(ActionAimSource::Same, AimedAction::None);
                out.ended = true;
            }
        }
    }

    if (heldBack_.any()) {
        if (!holdStarted) {
            waitedSeconds_ += dt;
        }
        const bool written = input.targetWritten >= generation_;
        const bool timedOut = waitedSeconds_ >= kMaxHoldBackSeconds;
        if (written || timedOut || !input.retargetable) {
            out.actions |= heldBack_; // sent once even if let go meanwhile; ActionHold keeps it long enough
            out.released = true;
            out.pressed = heldAction_;
            out.timedOut = !written && timedOut;
            out.waitedSeconds = waitedSeconds_;
            out.waitedCommands = waitedCommands_;
            heldBack_.reset();
        } else {
            out.actions &= ~heldBack_;
            ++waitedCommands_;
        }
    }

    out.target = target_;
    out.generation = generation_;
    out.action = action_;
    out.holdingBack = heldBack_.any();
    return out;
}

void ActionAim::reset() {
    setTarget(ActionAimSource::Same, AimedAction::None);
    previous_.reset();
    heldBack_.reset();
    heldAction_ = AimedAction::None;
    keepSeconds_ = 0.0f;
    waitedSeconds_ = 0.0f;
    waitedCommands_ = 0;
}

} // namespace evr::input
