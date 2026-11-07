#pragma once

// What the game's prompts name for a bind (vkcore/prompt_hooks.cpp). The game's texts name an action by its
// bind (the mods loading tip names _reload), and the game asks for the keys bound to it; the layer answers
// with the button of the action that presses the bind's user command button (_reload is button 7, switch
// weapon mod). The move binds are no action of ours: the move stick moves. The tooltip text (tutorial popups,
// loading screen tips, hints) gets the move stick's direction for them, so the wall-climbing tutorial, which
// names the four move binds, shows Left Stick Up, Left, Down and Right on Touch where it showed W, A, S and
// D. The HUD's prompt helpers ask for actions only (they show one VR key each, PromptLabelSet::distinct), so
// they get no move text.
//
// Pure, so every bind the game's texts name is tested without the game.

#include "features/input/button_labels.hpp"
#include "game/eternal/game_action.hpp"

#include <cstddef>
#include <optional>
#include <string>

namespace evr::input {

// The game's user command buttons for moving (usercmdButton_t INPUT_MOVELEFT .. INPUT_MOVEBACK, build
// 25216728, docs/rig-findings/input-aim.md section 1.2); the action table maps "_moveLeft" to 49,
// "_moveRight" to 50, "_moveForward" and "_forward" to 51, "_moveBack" and "_back" to 52.
inline constexpr int kMoveLeftButton = 49;
inline constexpr int kMoveRightButton = 50;
inline constexpr int kMoveForwardButton = 51;
inline constexpr int kMoveBackButton = 52;

// The index into PromptLabelSet::moveText (forward, left, back, right) of a move button, or nothing.
std::optional<std::size_t> moveTextIndex(int usercmdButton);

struct BindAnswer {
    const std::string* text = nullptr; // into the label set; nullptr: the game's keys stay
    int slot = -1;                     // the text's PromptLabelSet::distinct slot; -1 for a move text
};

// The answer for a bind whose user command button is `button` (nothing: not in the game's action table) and
// whose action is `action` (nothing: no action of ours presses it), for the tooltip text (`textPass`) or a
// HUD helper. An action's text when it has one; for the tooltip text, a move button's direction.
BindAnswer bindAnswer(std::optional<game::GameAction> action,
                      std::optional<int> button,
                      bool textPass,
                      const PromptLabelSet& labels);

} // namespace evr::input
