#include "features/input/button_labels.hpp"

#include "features/input/binding_keys.hpp"
#include "features/input/controller_bindings.hpp"
#include "features/input/interaction_profiles.hpp"
#include "features/input/xr_action_set.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <utility>

namespace evr::input {

namespace {

constexpr std::array<std::pair<LabelInput, std::string_view>, kLabelInputCount> kInputNames{{
    {LabelInput::Trigger, "trigger"},
    {LabelInput::Grip, "grip"},
    {LabelInput::StickClick, "stick_click"},
    {LabelInput::Primary, "primary"},
    {LabelInput::Secondary, "secondary"},
    {LabelInput::Face3, "face3"},
    {LabelInput::Face4, "face4"},
    {LabelInput::Shoulder, "shoulder"},
    {LabelInput::Menu, "menu"},
    {LabelInput::Stick, "stick"},
}};

constexpr std::array<Hand, 2> kHands{Hand::Left, Hand::Right};

std::size_t handIndex(Hand hand) {
    return hand == Hand::Left ? 0 : 1;
}

std::size_t inputIndex(LabelInput input) {
    return static_cast<std::size_t>(input);
}

// The gameplay action whose binding path names the input.
XrActionId gameplayAction(LabelInput input) {
    switch (input) {
    case LabelInput::Trigger:
        return XrActionId::Trigger;
    case LabelInput::Grip:
        return XrActionId::Grip;
    case LabelInput::StickClick:
        return XrActionId::ThumbstickClick;
    case LabelInput::Primary:
        return XrActionId::Primary;
    case LabelInput::Secondary:
        return XrActionId::Secondary;
    case LabelInput::Face3:
        return XrActionId::Face3;
    case LabelInput::Face4:
        return XrActionId::Face4;
    case LabelInput::Shoulder:
        return XrActionId::Shoulder;
    case LabelInput::Menu:
        return XrActionId::Menu;
    case LabelInput::Stick:
    case LabelInput::Count:
        break;
    }
    return XrActionId::Thumbstick;
}

LabelInput labelInput(ButtonInput input) {
    switch (input) {
    case ButtonInput::Trigger:
        return LabelInput::Trigger;
    case ButtonInput::Grip:
        return LabelInput::Grip;
    case ButtonInput::StickClick:
        return LabelInput::StickClick;
    case ButtonInput::Primary:
        return LabelInput::Primary;
    case ButtonInput::Secondary:
        return LabelInput::Secondary;
    case ButtonInput::Face3:
        return LabelInput::Face3;
    case ButtonInput::Face4:
        return LabelInput::Face4;
    case ButtonInput::Shoulder:
        return LabelInput::Shoulder;
    case ButtonInput::Menu:
    case ButtonInput::Count:
        break;
    }
    return LabelInput::Menu;
}

std::string capitalized(std::string_view word) {
    std::string out(word);
    if (!out.empty()) {
        out.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(out.front())));
    }
    return out;
}

constexpr std::string_view kInput = "/input/";

// Controls whose name is not their identifier in title case.
constexpr std::array<std::pair<std::string_view, std::string_view>, 8> kControlNames{{
    {"squeeze", "Grip"},
    {"thumbstick", "Stick"},
    {"shoulder", "Bumper"},
    {"bumper", "Bumper"},
    {"dpad_up", "D-pad Up"},
    {"dpad_down", "D-pad Down"},
    {"dpad_left", "D-pad Left"},
    {"dpad_right", "D-pad Right"},
}};

// "dpad_up" is "Dpad Up", "a" is "A".
std::string titleCase(std::string_view identifier) {
    std::string out;
    bool wordStart = true;
    for (const char c : identifier) {
        if (c == '_') {
            out += ' ';
            wordStart = true;
            continue;
        }
        out += wordStart ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        wordStart = false;
    }
    return out;
}

// The part of a binding path from "/input/" on: "/user/hand/left/input/x/click" is "/input/x/click".
std::string_view inputLeaf(std::string_view path) {
    const std::size_t at = path.find(kInput);
    return at == std::string_view::npos ? std::string_view{} : path.substr(at);
}

std::string handPrefixed(Hand hand, std::string_view name) {
    return capitalized(handName(hand)) + " " + std::string(name);
}

} // namespace

std::optional<LabelKey> parseLabelKey(std::string_view text) {
    const std::size_t dot = text.find('.');
    if (dot == std::string_view::npos) {
        return std::nullopt;
    }
    const std::optional<Hand> hand = parseHand(text.substr(0, dot));
    const std::string_view inputName = text.substr(dot + 1);
    const auto it =
        std::ranges::find(kInputNames, inputName, &std::pair<LabelInput, std::string_view>::second);
    if (!hand || it == kInputNames.end()) {
        return std::nullopt;
    }
    return LabelKey{*hand, it->first};
}

std::string formatLabelKey(LabelKey key) {
    return std::string(handName(key.hand)) + "." + std::string(kInputNames[inputIndex(key.input)].second);
}

const std::string& ButtonLabels::name(Hand hand, LabelInput input) const {
    return names_[handIndex(hand)][inputIndex(input)];
}

void ButtonLabels::setName(Hand hand, LabelInput input, std::string name) {
    names_[handIndex(hand)][inputIndex(input)] = std::move(name);
}

std::string nameFromInputPath(std::string_view path) {
    const std::string_view leaf = inputLeaf(path);
    if (leaf.empty()) {
        return {};
    }
    const std::string_view rest = leaf.substr(kInput.size());
    const std::size_t slash = rest.find('/');
    const std::string_view control = rest.substr(0, slash);
    const std::string_view component =
        slash == std::string_view::npos ? std::string_view{} : rest.substr(slash + 1);
    if (control.empty()) {
        return {};
    }
    const auto known =
        std::ranges::find(kControlNames, control, &std::pair<std::string_view, std::string_view>::first);
    std::string name = known != kControlNames.end() ? std::string(known->second) : titleCase(control);
    // A stick or trackpad is named for moving it, so pressing it down says so.
    if (control == "thumbstick" || control == "trackpad") {
        if (component == "click") {
            name += " Click";
        } else if (component == "force") {
            name += " Press";
        }
    }
    return name;
}

ButtonLabels buttonLabelsFor(const ControllerData& data) {
    const InteractionProfileInfo* profile = findInteractionProfile(data.profilePath);
    std::array<std::array<std::string, kLabelInputCount>, 2> leaves;
    for (const Hand hand : kHands) {
        for (const auto& [input, inputName] : kInputNames) {
            if (const SuggestedBinding* binding = data.find(gameplayAction(input), hand)) {
                leaves[handIndex(hand)][inputIndex(input)] = std::string(inputLeaf(binding->path));
            }
        }
    }
    // A name is given its hand when the other controller has the same control: "Left Trigger", but
    // "X" on Touch controllers, where only the left one has an X.
    const auto otherHandHas = [&](Hand hand, const std::string& leaf) {
        const Hand other = otherHand(hand);
        if (profile != nullptr) {
            return profileHasPath(*profile, other, leaf);
        }
        const auto& otherLeaves = leaves[handIndex(other)];
        return std::ranges::any_of(otherLeaves, [&leaf](const std::string& otherLeaf) {
            return nameFromInputPath(otherLeaf) == nameFromInputPath(leaf);
        });
    };
    ButtonLabels labels;
    for (const Hand hand : kHands) {
        for (const auto& [input, inputName] : kInputNames) {
            const auto given = data.labels.find(formatLabelKey({hand, input}));
            if (given != data.labels.end()) {
                labels.setName(hand, input, given->second);
                continue;
            }
            const std::string& leaf = leaves[handIndex(hand)][inputIndex(input)];
            const std::string name = nameFromInputPath(leaf);
            if (name.empty()) {
                continue;
            }
            labels.setName(hand, input, otherHandHas(hand, leaf) ? handPrefixed(hand, name) : name);
        }
    }
    return labels;
}

std::vector<PromptControl> promptControls(game::GameAction action, const BindingProfile& profile) {
    std::vector<PromptControl> controls;
    for (const PressKind kind : {PressKind::WhileDown, PressKind::Tap, PressKind::Hold}) {
        for (const ButtonBinding& binding : profile.buttons) {
            if (binding.action == action && binding.kind == kind) {
                controls.push_back({binding.hand, labelInput(binding.input),
                                    kind == PressKind::Hold ? PromptPress::Hold : PromptPress::Press});
            }
        }
    }
    if (profile.turnStick) {
        for (const StickGestureBinding& binding : profile.stickGestures) {
            if (binding.action != action) {
                continue;
            }
            PromptPress press = PromptPress::StickUp;
            if (binding.gesture == StickGesture::DownTap) {
                press = PromptPress::StickDown;
            } else if (binding.gesture == StickGesture::DownHold) {
                press = PromptPress::StickDownHold;
            }
            controls.push_back({*profile.turnStick, LabelInput::Stick, press});
        }
    }
    return controls;
}

std::string promptText(const PromptControl& control, const ButtonLabels& labels) {
    const std::string& name = labels.name(control.hand, control.input);
    if (name.empty()) {
        return {};
    }
    switch (control.press) {
    case PromptPress::Press:
        return name;
    case PromptPress::Hold:
        return "Hold " + name;
    case PromptPress::StickUp:
        return name + " Up";
    case PromptPress::StickDown:
        return name + " Down";
    case PromptPress::StickDownHold:
        return "Hold " + name + " Down";
    }
    return name;
}

std::string
actionPromptText(game::GameAction action, const BindingProfile& profile, const ButtonLabels& labels) {
    for (const PromptControl& control : promptControls(action, profile)) {
        std::string text = promptText(control, labels);
        if (!text.empty()) {
            return text;
        }
    }
    return {};
}

PromptLabelSet promptLabelSet(const BindingProfile& profile, const ButtonLabels& labels) {
    PromptLabelSet set;
    set.slot.fill(-1);
    for (std::size_t i = 0; i < game::kGameActionCount; ++i) {
        std::string text = actionPromptText(static_cast<game::GameAction>(i), profile, labels);
        for (char& c : text) {
            if (c < 0x20 || c > 0x7E) {
                c = '?';
            }
        }
        if (text.empty()) {
            continue;
        }
        auto it = std::ranges::find(set.distinct, text);
        if (it == set.distinct.end()) {
            set.distinct.push_back(text);
            it = std::prev(set.distinct.end());
        }
        set.slot[i] = static_cast<int>(it - set.distinct.begin());
        set.text[i] = std::move(text);
    }
    return set;
}

} // namespace evr::input
