#pragma once

// The game's fixed keys that its texts name, and the button text the layer gives for each (vkcore/
// prompt_hooks.cpp): where the game reads keys, not commands, the button that sends the key. Pure, so the
// mapping is tested without the game.

#include "features/input/button_labels.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace evr::input {

// Keys the layer presses where the game reads keys, by the game's key name (KeyNumToString). Tutorial and
// lore popups take Space and E for actions (vkcore/usercmd_hook.cpp), the Dossier button presses Tab; the
// menus' hint bars (the command bar: "[ESC] BACK", "[ENTER] SELECT", "ENTER HOLD TO CONTINUE") name
// Escape, which B sends in every menu (features/menu/menu_router.hpp), and Enter, whose select the
// trigger's click does.
inline constexpr std::array<std::string_view, 5> kLayerKeyNames{"SPACE", "E", "TAB", "ESCAPE", "ENTER"};

// The tab lists' previous and next tab keys (settings, the Dossier's pages), which the grips send in every
// menu.
inline constexpr std::array<std::string_view, 2> kTabKeyNames{"Q", "E"};

// The index of the game's key name in kLayerKeyNames / kTabKeyNames, or nothing.
std::optional<std::size_t> layerKeyIndex(std::string_view gameKeyName);
std::optional<std::size_t> tabKeyIndex(std::string_view gameKeyName);

// The text for kLayerKeyNames[index] / kTabKeyNames[index] under `labels`: Space the jump button, E melee's,
// Tab the Dossier's, Escape the menus' back, Enter their select; Q and E the previous and next tab. A
// reference into `labels`; empty when the controller has no name for it (the game's key then stays).
const std::string& layerKeyLabel(std::size_t index, const PromptLabelSet& labels);
const std::string& tabKeyLabel(std::size_t index, const PromptLabelSet& labels);

// The game's raw key text (KeyNameText, a key number to its name with no key token) is answered only for the
// callers that show it as a prompt, told apart by their return address: TabList's two calls get the tab
// keys' text, the command bar's two hold-button calls (GetKeyString's and the bar update's) the layer keys'
// text, every other caller the game's own text. The loading screen's tips bar, which writes its Q and E as
// key tokens ("K_Q PREVIOUS"), is answered the same way at its two text formats: as tab keys, since the grips
// send its keys (features/menu/load_tips_keys.hpp).
enum class KeyNameAnswer { Game, TabKey, LayerKey };

// The callers' return addresses, as found at install; 0 for one not found (no return address is 0).
struct KeyNameCallers {
    std::array<std::uintptr_t, 2> tabList{};  // the previous tab's key, then the next tab's
    std::uintptr_t getKeyString = 0;          // idSWFWidget_Button_CommandBar::GetKeyString()
    std::uintptr_t commandBarUpdate = 0;      // the command bar's update ("custom" layout)
    std::array<std::uintptr_t, 2> loadTips{}; // the tips bar's previous and next text formats
};

KeyNameAnswer keyNameAnswer(std::uintptr_t returnAddress, const KeyNameCallers& callers);

// The text for the game's key name under `answer`: a reference into `labels`, or nullptr for the game's own
// text (KeyNameAnswer::Game, or a key the answer's table does not hold). Empty text also keeps the game's.
const std::string*
keyNameLabel(KeyNameAnswer answer, std::string_view gameKeyName, const PromptLabelSet& labels);

} // namespace evr::input
