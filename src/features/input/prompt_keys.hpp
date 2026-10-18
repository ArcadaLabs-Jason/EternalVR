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

// The keys of a screen's own pages (features/menu/page_keys.hpp), previous then next: the mission-complete
// Rewards tab's arrows and the Dossier pages' inner tab lists, A and D, which a stick pushed left or right
// sends there.
inline constexpr std::array<std::string_view, 2> kPageKeyNames{"A", "D"};

// The index of the game's key name in kLayerKeyNames / kTabKeyNames / kPageKeyNames, or nothing.
std::optional<std::size_t> layerKeyIndex(std::string_view gameKeyName);
std::optional<std::size_t> tabKeyIndex(std::string_view gameKeyName);
std::optional<std::size_t> pageKeyIndex(std::string_view gameKeyName);

// The game's key name as the table that holds it spells it (any of the three), or nothing: the keys whose
// text the layer may give.
std::optional<std::string_view> answeredKeyName(std::string_view gameKeyName);

// The text for kLayerKeyNames[index] / kTabKeyNames[index] under `labels`: Space the jump button, E melee's,
// Tab the Dossier's, Escape the menus' back, Enter their select; Q and E the previous and next tab. A
// reference into `labels`; empty when the controller has no name for it (the game's key then stays).
const std::string& layerKeyLabel(std::size_t index, const PromptLabelSet& labels);
const std::string& tabKeyLabel(std::size_t index, const PromptLabelSet& labels);
// The text for kPageKeyNames[index]: the left stick (the previous page) or the right (the next), in short
// ("LS", "RS").
const std::string& pageKeyLabel(std::size_t index, const PromptLabelSet& labels);

// The game's raw key text (KeyNameText, a key number to its name with no key token) is answered only for the
// callers that show it as a prompt, told apart by their return address: TabList's two calls get the tab
// keys' text, the command bar's two hold-button calls (GetKeyString's and the bar update's) the layer keys'
// text, every other caller the game's own text. The loading screen's tips bar, which writes its Q and E as
// key tokens ("K_Q PREVIOUS"), is answered the same way at its two text formats: as tab keys, since the grips
// send its keys (features/menu/load_tips_keys.hpp). A tab list on A and D (the Dossier pages' inner tabs) and
// the Rewards tab's page arrows ("A 1/2 D") get the page keys' text, the stick that sends them there
// (features/menu/page_keys.hpp). The Fortress's collectible and music viewers' Q and E are tab keys: the
// grips send them there, as in every menu (vkcore/prompt_viewer_keys.hpp). TabKeyOnly: a tab list while no
// stick sends its A / D (the tab lists' page-key hook is not in), so only its Q and E are answered.
enum class KeyNameAnswer { Game, TabKey, TabKeyOnly, LayerKey, PageKey };

// The callers' return addresses, as found at install; 0 for one not found (no return address is 0).
struct KeyNameCallers {
    std::array<std::uintptr_t, 2> tabList{};  // the previous tab's key, then the next tab's
    std::uintptr_t getKeyString = 0;          // idSWFWidget_Button_CommandBar::GetKeyString()
    std::uintptr_t commandBarUpdate = 0;      // the command bar's update ("custom" layout)
    std::array<std::uintptr_t, 2> loadTips{}; // the tips bar's previous and next text formats
    std::array<std::uintptr_t, 2> rewards{};  // the Rewards tab's previous and next page arrows
    bool tabListPages = false;                // a stick sends a tab list's A / D (its hook is in)
    std::array<std::uintptr_t, 2> viewer{};   // the Fortress viewers' previous and next (Q, E)
};

KeyNameAnswer keyNameAnswer(std::uintptr_t returnAddress, const KeyNameCallers& callers);

// The text for the game's key name under `answer`: a reference into `labels`, or nullptr for the game's own
// text (KeyNameAnswer::Game, or a key the answer's table does not hold). Empty text also keeps the game's.
const std::string*
keyNameLabel(KeyNameAnswer answer, std::string_view gameKeyName, const PromptLabelSet& labels);

// The HUD's key labels from the helper that the demon's ability list, the tutorial objectives and the weapon
// info row share (0xF0BB50 in build 25216728): it asks LocalizedKeyName for the text of the VR key the
// GetKeysForAction detour handed it (vkcore/prompt_hooks.cpp), one text per slot of PromptLabelSet::distinct.
// The weapon info row (equipment, Flame Belch, chainsaw, the Crucible, switch equipment) sizes each label's
// box to its text above icons about 60 px apart, so three full names overlap; its calls get the short form.
// The call is told apart by two return addresses: the helper's LocalizedKeyName call's, and the one a frame
// above it, which is one of the weapon info update's calls to the helper.
enum class HudKeyForm { Full, Short };

// The return addresses found at install; 0 for one not found (no return address is 0).
struct HudKeyCallers {
    std::uintptr_t helperKeyText = 0;           // the helper's LocalizedKeyName call
    std::array<std::uintptr_t, 8> weaponInfo{}; // the weapon info update's calls to the helper
};

// `returnAddress` is LocalizedKeyName's, `callerReturn` the one a frame above it (0 when unknown).
HudKeyForm
hudKeyForm(std::uintptr_t returnAddress, std::uintptr_t callerReturn, const HudKeyCallers& callers);

// The text of the VR key in `slot` in `form`, or nullptr for a slot with no text.
const std::string* hudKeyText(std::size_t slot, HudKeyForm form, const PromptLabelSet& labels);

} // namespace evr::input
