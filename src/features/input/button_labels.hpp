#pragma once

// The names of a controller family's buttons, and the text a game prompt shows for an action.
//
// The game's tutorials, hints and ability lists name keyboard keys or gamepad buttons. We replace
// those with the button the player's own control map binds to the action, named the way the
// controller names it: "X" and "Hold Y" on Touch controllers, "Left A" on Index controllers, where
// both hands have an A.
//
// Names come from the family's controller data (controller_bindings.hpp). A [labels] section names
// inputs directly:
//
//   [labels]
//   "<hand>.<input>" = "<name>"      e.g. "left.primary" = "X", "right.stick" = "Right Thumbstick"
//
//   <input>   trigger, grip, stick_click, primary, secondary, face3, face4, shoulder, menu, stick
//
// A name given there is used as written. An input without one is named from the path its gameplay
// action is bound to ("/input/x/click" is "X", "/input/squeeze/value" is "Grip"), with the hand in
// front unless the name is a letter only this hand has. So a family needs no [labels] section unless
// its buttons are named differently from their paths, and a family with more buttons needs no code
// here, only data.

#include "features/input/binding_profile.hpp"
#include "features/input/controller_state.hpp"
#include "game/eternal/game_action.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evr::input {

struct ControllerData;

// A physical control a prompt can name: one of the bindable buttons, or a whole stick.
enum class LabelInput : std::uint8_t {
    Trigger,
    Grip,
    StickClick,
    Primary,
    Secondary,
    Face3,
    Face4,
    Shoulder,
    Menu,
    Stick,
    Count,
};

inline constexpr std::size_t kLabelInputCount = static_cast<std::size_t>(LabelInput::Count);

struct LabelKey {
    Hand hand = Hand::Left;
    LabelInput input = LabelInput::Trigger;
    friend bool operator==(const LabelKey&, const LabelKey&) = default;
};

// "<hand>.<input>", the key a name is written under in [labels].
std::optional<LabelKey> parseLabelKey(std::string_view text);
std::string formatLabelKey(LabelKey key);

class ButtonLabels {
public:
    // Empty when the controller has no such input.
    [[nodiscard]] const std::string& name(Hand hand, LabelInput input) const;
    void setName(Hand hand, LabelInput input, std::string name);

private:
    std::array<std::array<std::string, kLabelInputCount>, 2> names_;
};

// The names for a family: its [labels] entries (checked by parseControllerData), and names from its
// gameplay bindings' paths for the rest.
ButtonLabels buttonLabelsFor(const ControllerData& data);

// The name an input path gives its control, without the hand: "/user/hand/left/input/x/click" is "X",
// "/input/thumbstick/click" is "Stick Click". Empty for a path that is not an input.
std::string nameFromInputPath(std::string_view path);

// How a prompt asks for its control.
enum class PromptPress : std::uint8_t {
    Press,         // Press the button (or tap it; a tap binding fires on the release).
    Hold,          // Hold the button.
    StickUp,       // Push the stick up.
    StickDown,     // Push the stick down and let go.
    StickDownHold, // Push the stick down and keep it there.
};

struct PromptControl {
    Hand hand = Hand::Right;
    LabelInput input = LabelInput::Trigger;
    PromptPress press = PromptPress::Press;
    friend bool operator==(const PromptControl&, const PromptControl&) = default;
};

// The controls the profile binds to `action`: presses and taps first, then holds, then turn-stick
// gestures, each in the profile's order. Empty when the action is unbound.
std::vector<PromptControl> promptControls(game::GameAction action, const BindingProfile& profile);

// "X", "Hold Y", "Right Trigger", "Right Stick Up", "Hold Right Stick Down". Empty when the
// controller has no name for the input.
std::string promptText(const PromptControl& control, const ButtonLabels& labels);

// The text for the first control bound to `action`, or empty when none has a name.
std::string
actionPromptText(game::GameAction action, const BindingProfile& profile, const ButtonLabels& labels);

// Every action's prompt text under one control map, for the game's prompts (vkcore/prompt_hooks.cpp). Texts
// are printable ASCII (other characters become '?'), since the game's prompt fonts may lack them. Each
// distinct text has a slot, for game code that asks for a key number per text.
struct PromptLabelSet {
    std::array<std::string, game::kGameActionCount> text; // empty when unbound or unnamed
    std::vector<std::string> distinct;                    // each text once, in action order
    std::array<int, game::kGameActionCount> slot{};       // index into `distinct`, -1 when there is no text

    // The menus' own controls (features/menu/menu_router.hpp), which no control map changes, for the keys
    // the menus' hints name: back (Escape) is the right secondary button (B on Touch) in every menu, or the
    // left one where the right hand has none; select (Enter) is the weapon hand's trigger, which clicks what
    // the pointer is on; the previous and next tab (Q, E) are the left and right grips, in short (LG, RG),
    // since the tab lists cut longer text. Empty when the controller has no name for the control.
    std::string menuBack;
    std::string menuSelect;
    std::string menuPreviousTab;
    std::string menuNextTab;

    friend bool operator==(const PromptLabelSet&, const PromptLabelSet&) = default;
};

PromptLabelSet promptLabelSet(const BindingProfile& profile, const ButtonLabels& labels);

} // namespace evr::input
