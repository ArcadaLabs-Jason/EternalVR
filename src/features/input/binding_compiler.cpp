#include "features/input/binding_compiler.hpp"

#include "features/input/binding_keys.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>

namespace evr::input {

namespace {

constexpr std::size_t handIndex(Hand hand) {
    return static_cast<std::size_t>(hand);
}

struct PendingGesture {
    std::string key;
    Hand hand = Hand::Right;
    StickGestureBinding binding;
};

struct PendingButton {
    std::string key;
    ButtonBinding binding;
};

// Everything read from the entries, before cross-entry checks.
struct Collected {
    std::optional<Hand> weaponHand;
    std::array<StickRole, 2> roles{StickRole::None, StickRole::None};
    std::array<std::string, 2> roleKeys;
    std::vector<PendingButton> buttons;
    std::vector<PendingGesture> gestures;
};

std::string quoted(std::string_view text) {
    return "'" + std::string(text) + "'";
}

class Compiler {
public:
    BindingBuildResult run(const BindingMap& entries) {
        for (const auto& [key, value] : entries) {
            collect(key, value);
        }
        BindingBuildResult result;
        result.profile.weaponHand = collected_.weaponHand.value_or(Hand::Right);
        assignSticks(result.profile);
        addButtons(result.profile);
        addGestures(result.profile);
        result.issues = std::move(issues_);
        return result;
    }

private:
    void report(BindingIssueKind kind, const std::string& key, std::string message) {
        issues_.push_back(issueOf(kind, key, 0, std::move(message)));
    }

    // A conflict between `side` (left out of the profile) and `kept`. The message names both keys and
    // what each binds, so the player can see which two actions on which two inputs clash.
    struct Side {
        std::string key;
        std::string value;
    };

    void reportConflict(BindingConflict conflict, const Side& side, const Side& kept, std::string_view why) {
        BindingIssue issue;
        issue.kind = BindingIssueKind::Conflict;
        issue.key = side.key;
        issue.conflict = conflict;
        issue.value = side.value;
        issue.otherKey = kept.key;
        issue.otherValue = kept.value;
        issue.message = quoted(side.key) + " (" + side.value + ") conflicts with " + quoted(kept.key) + " (" +
                        kept.value + "): " + std::string(why) + "; " + quoted(side.key) + " is ignored";
        issues_.push_back(std::move(issue));
    }

    void collect(const std::string& key, const std::string& value) {
        const auto parsed = parseBindingKey(key);
        if (!parsed) {
            report(BindingIssueKind::UnknownKey, key, quoted(key) + " is not a binding key");
            return;
        }
        if (std::holds_alternative<WeaponHandKey>(*parsed)) {
            collectWeaponHand(key, value);
        } else if (const auto* role = std::get_if<StickRoleKey>(&*parsed)) {
            collectRole(key, value, role->hand);
        } else if (value != kUnboundValue) {
            collectAction(key, value, *parsed);
        }
    }

    void collectWeaponHand(const std::string& key, const std::string& value) {
        collected_.weaponHand = parseHand(value);
        if (!collected_.weaponHand) {
            report(BindingIssueKind::UnknownValue, key,
                   quoted(value) + " is not a hand; use 'left' or 'right'");
        }
    }

    void collectRole(const std::string& key, const std::string& value, Hand hand) {
        const auto role = parseStickRole(value);
        if (!role) {
            report(BindingIssueKind::UnknownValue, key,
                   quoted(value) + " is not a stick role; use 'move', 'turn' or 'none'");
            return;
        }
        collected_.roles[handIndex(hand)] = *role;
        collected_.roleKeys[handIndex(hand)] = key;
    }

    void collectAction(const std::string& key, const std::string& value, const BindingKey& parsed) {
        const auto action = game::parseGameAction(value);
        if (!action) {
            report(BindingIssueKind::UnknownValue, key, quoted(value) + " is not a game action");
            return;
        }
        if (const auto* button = std::get_if<ButtonKey>(&parsed)) {
            collected_.buttons.push_back({key, {button->hand, button->input, button->kind, *action}});
        } else if (const auto* gesture = std::get_if<GestureKey>(&parsed)) {
            collected_.gestures.push_back({key, gesture->hand, {gesture->gesture, *action}});
        }
    }

    void assignSticks(BindingProfile& profile) {
        for (const Hand hand : {Hand::Left, Hand::Right}) {
            std::optional<Hand>* slot = nullptr;
            const StickRole role = collected_.roles[handIndex(hand)];
            if (role == StickRole::Move) {
                slot = &profile.moveStick;
            } else if (role == StickRole::Turn) {
                slot = &profile.turnStick;
            }
            if (slot == nullptr) {
                continue;
            }
            if (slot->has_value()) {
                const std::string roleName(stickRoleName(role));
                reportConflict(
                    BindingConflict::SharedStickRole, {collected_.roleKeys[handIndex(hand)], roleName},
                    {collected_.roleKeys[handIndex(**slot)], roleName},
                    "both sticks are set to " + quoted(roleName) + " and only the left one is used");
                continue;
            }
            *slot = hand;
        }
    }

    [[nodiscard]] const PendingButton* pressBinding(Hand hand, ButtonInput input) const {
        const auto it = std::ranges::find_if(collected_.buttons, [hand, input](const PendingButton& pending) {
            return pending.binding.hand == hand && pending.binding.input == input &&
                   pending.binding.kind == PressKind::WhileDown;
        });
        return it == collected_.buttons.end() ? nullptr : &*it;
    }

    static Side sideOf(const PendingButton& pending) {
        return {pending.key, std::string(game::gameActionName(pending.binding.action))};
    }

    void addButtons(BindingProfile& profile) {
        for (const PendingButton& pending : collected_.buttons) {
            const ButtonBinding& binding = pending.binding;
            const PendingButton* press =
                binding.kind == PressKind::WhileDown ? nullptr : pressBinding(binding.hand, binding.input);
            if (press != nullptr) {
                reportConflict(BindingConflict::PressWithTapOrHold, sideOf(pending), sideOf(*press),
                               "a press binding fires on every tap and hold");
                continue;
            }
            profile.buttons.push_back(binding);
        }
    }

    void addGestures(BindingProfile& profile) {
        for (const PendingGesture& pending : collected_.gestures) {
            if (profile.turnStick != pending.hand) {
                const std::size_t index = handIndex(pending.hand);
                const std::string roleKey = collected_.roleKeys[index].empty()
                                                ? formatBindingKey(StickRoleKey{pending.hand})
                                                : collected_.roleKeys[index];
                reportConflict(
                    BindingConflict::GestureOffTurnStick,
                    {pending.key, std::string(game::gameActionName(pending.binding.action))},
                    {roleKey, std::string(stickRoleName(collected_.roles[index]))},
                    "the " + std::string(handName(pending.hand)) +
                        " stick is not the turn stick, and stick gestures only work on the turn stick");
                continue;
            }
            profile.stickGestures.push_back(pending.binding);
        }
    }

    Collected collected_;
    std::vector<BindingIssue> issues_;
};

} // namespace

BindingBuildResult buildBindingProfile(const BindingMap& entries) {
    return Compiler{}.run(entries);
}

BindingMap toBindingMap(const BindingProfile& profile) {
    BindingMap entries;
    entries[formatBindingKey(WeaponHandKey{})] = std::string(handName(profile.weaponHand));
    if (profile.moveStick) {
        entries[formatBindingKey(StickRoleKey{*profile.moveStick})] =
            std::string(stickRoleName(StickRole::Move));
    }
    if (profile.turnStick) {
        entries[formatBindingKey(StickRoleKey{*profile.turnStick})] =
            std::string(stickRoleName(StickRole::Turn));
        for (const StickGestureBinding& gesture : profile.stickGestures) {
            entries[formatBindingKey(GestureKey{*profile.turnStick, gesture.gesture})] =
                std::string(game::gameActionName(gesture.action));
        }
    }
    for (const ButtonBinding& button : profile.buttons) {
        entries[formatBindingKey(ButtonKey{button.hand, button.input, button.kind})] =
            std::string(game::gameActionName(button.action));
    }
    return entries;
}

} // namespace evr::input
