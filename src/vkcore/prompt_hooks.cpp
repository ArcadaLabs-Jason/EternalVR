// Motion controllers, the game's button prompts (controllers.hpp, docs/VR_CONTROLLERS.md): tutorials, hints
// and HUD prompts name the player's own VR buttons ("Press [Left Grip] to use the Flame Belch") instead of
// keys.
//
// Two lookups build every prompt (build 25216728; the Game Pass build has the same code):
// - GetKeysForAction (RVA 0x17E1EC0): an action's bind name ("_bfg") to the names of the keys bound to it, as
//   three idList<idStr>. The tooltip text pass (0x1835410: tutorial popups, tips, hints, Dossier notices)
//   asks for localized names and shows each as "[NAME]". Three HUD helpers (0xED03C0: blood punch, dash;
//   0xF0BB50: the demon's ability list, weapon info, tutorial objectives; 0xFA1130: the reticle) ask for key
//   names, turn the first into a key number and ask LocalizedKeyName for its text.
// - LocalizedKeyName (0x17E2820): a key number to its text.
//
// The detour on GetKeysForAction runs the game's function and then, only when one of those four called it for
// the local player, replaces the first name with our text: the label itself for the text pass, or for the
// helpers the name of a VR key the game knows but nothing binds (STEAMVR_PRIMARY_*, STEAMVR_SECONDARY_*),
// whose text the detour on LocalizedKeyName then gives as the label (in short for the weapon info row,
// prompt_weapon_info.hpp). Every other caller gets the game's
// result: the weapon wheel and tutorial dismissal match real key presses with it, and the bindings menu
// lists the real binds. The action is found through the game's action table ("_altfire" is usercmd button
// 2), then our action that presses that button, or while piloting a demon the action that presses the
// demon's own binding. The game is held on its keyboard prompts (swf_platformOverride 2, runtime_cvars.hpp),
// so a gamepad or Steam Input cannot switch it to pad glyphs that name the pad's binds.
//
// Fixed keys go through the text pass too: any SWF text marked for it has its "K_<key>" tokens turned into
// "[<LocalizedKeyName>]" while the game is on keyboard prompts. Tutorial popups write "[K_SPACE] TO DISMISS";
// the menus' hint bars (idSWFWidget_Button_CommandBar, 0x159F6B0) write each button's PC key that way
// ("[ESC] BACK", "[ENTER] SELECT"). The LocalizedKeyName detour names the button that sends such a key
// (input::kLayerKeyNames). The tab lists (idSWFWidget_TabList) write their Q / E as plain text from
// KeyNameText (0x17E2680), and so do the hint bars for a hold button ("ENTER HOLD TO CONTINUE",
// prompt_command_bar.hpp); a detour there answers only TabList's two calls (input::kTabKeyNames), the
// command bar's two (input::kLayerKeyNames) and the two of the mission-complete Rewards tab's page arrows
// (input::kPageKeyNames, prompt_page_keys.hpp).
//
// Everything is found by signature and checked at install; if any part is missing nothing is installed and
// the prompts stay the game's. The tab lists' and the hold buttons' parts are each optional.

#include "vkcore/controllers_impl.hpp"

#include "features/input/button_labels.hpp"
#include "features/input/prompt_keys.hpp"
#include "game/eternal/usercmd_buttons.hpp"
#include "vkcore/demon_view.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/prompt_command_bar.hpp"
#include "vkcore/prompt_key_log.hpp"
#include "vkcore/prompt_load_tips.hpp"
#include "vkcore/prompt_page_keys.hpp"
#include "vkcore/prompt_weapon_info.hpp"

#include <windows.h>

#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "prompts";

constexpr const char* kGetKeysSignature = "4C 8B DC 55 53 56 41 55 41 56 41 57 49 8D 6B A9 48 81 EC E8 00 00 "
                                          "00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 "
                                          "45 07 45 33 F6 C7 41 10 00 00 05 00";
constexpr const char* kKeyTextSignature =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 8B F1 48 "
    "8B FA 48 8B CA E8 ?? ?? ?? ?? 33 ED 48 8D 1D ?? ?? ?? ??";
constexpr const char* kStrAssignSignature =
    "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 30 4C 8B "
    "F2 48 8B D9 48 85 D2 0F 85 ?? ?? ?? ?? 8B 51 14 8B CA 8B C2";
constexpr const char* kKeyNameSignature = "48 83 3D ?? ?? ?? ?? 00 48 8D 05 ?? ?? ?? ?? 74 0F 3B 08";
constexpr const char* kCacheKeySignature =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 "
    "83 EC 20 41 0F B6 B8 2A 01 00 00 48 63 D2 49 63 D9";

struct Caller {
    const char* name;
    const char* signature;
};

// The prompt code that calls GetKeysForAction. A and C differ by one check before their last bytes.
constexpr std::array<Caller, 4> kCallers{{
    {"tooltip text", "40 55 53 41 55 41 56 41 57 48 8D AC 24 ?? ?? ?? ?? B8 00 42 00 00 E8 ?? ?? ?? ?? 48 2B "
                     "E0 48 8B 05 ?? ?? ?? "
                     "?? 48 33 C4 48 89 85 ?? ?? ?? ??"},
    {"HUD prompt (blood punch, dash)", "48 85 D2 0F 84 ?? ?? ?? ?? 4C 8B DC 55 53 56 41 57 48 8D 6C 24 98 48 "
                                       "81 EC 68 01 00 00 48 8B 05 ?? ?? ?? ?? "
                                       "48 33 C4 48 89 45 10 44 8B BD B0 00 00 00 49 8B F0 48 8B DA 4D 85 C0 "
                                       "0F 84 ?? ?? ?? ?? 41 80 38 00 0F 84 "
                                       "?? ?? ?? ?? 80 BD C0 00 00 00 00 0F 85 ?? ?? ?? ?? 45 84 C9"},
    {"HUD prompt (demon abilities, weapon info, objectives)", "48 85 C9 0F 84 ?? ?? ?? ?? 4C 8B DC 55 53 56"},
    {"HUD prompt (reticle)", "48 85 D2 0F 84 ?? ?? ?? ?? 4C 8B DC 55 53 56 41 57 48 8D 6C 24 98 48 81 EC 68 "
                             "01 00 00 48 8B 05 ?? ?? ?? ?? "
                             "48 33 C4 48 89 45 10 44 8B BD B0 00 00 00 49 8B F0 48 8B DA 4D 85 C0 0F 84 ?? "
                             "?? ?? ?? 41 80 38 00 0F 84 "
                             "?? ?? ?? ?? 45 84 C9"},
}};
constexpr std::size_t kTextCaller = 0;
constexpr std::size_t kHudHelperCaller = 2; // the helper the weapon info row's labels come from
// How far into each caller its one call to GetKeysForAction may lie (the text pass has it at +0x654).
constexpr std::size_t kCallerSpan = 0x1000;

// The VR keys the helpers are handed: STEAMVR_PRIMARY_* 0x131..0x13D, STEAMVR_SECONDARY_* 0x141..0x14D.
constexpr std::array<std::pair<int, int>, 2> kVirtualKeyRanges{{{0x131, 0x13D}, {0x141, 0x14D}}};

constexpr int kLastKeyboardKey = 0xFF;

// The tab lists (settings, the Dossier's pages) name their previous and next tab keys without a key token:
// in keyboard mode TabList's update (0x1592670) asks KeyNameText (0x17E2680, a key number to its text in a
// static buffer) once for each.
constexpr const char* kKeyNameTextSignature =
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 8B F9 33 D2 "
    "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 58 65 48 8B 04 25 58 00 00 00";
constexpr const char* kTabListSignature = "40 53 B8 B0 40 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 8B 05 ?? ?? ?? ?? "
                                          "48 33 C4 48 89 84 24 90 40 00 00 48 83 79 18 00 48 8B D9 0F 84 "
                                          "?? ?? ?? ?? 48 89 AC 24 C8 40 00 00 48 89 B4 24 D0 40 00 00 48 89 "
                                          "BC 24 D8 40 00 00 4C 89 B4 24 A8 40 00 00 4C 89 BC 24 A0 40 00 00 "
                                          "E8 ?? ?? ?? ?? 83 BB A8 00 00 00 01";
constexpr std::size_t kTabListCalls = 2; // the previous tab's key, then the next tab's

// TooltipCacheKey adds two 64-bit hashes with `add rcx, [rip+disp32]`: the tooltip image generation's
// (+0x117), then the bind generation's (+0x135). Only the second is ours to change; the bytes around the
// first are live pointers.
constexpr std::size_t kCacheKeyScan = 0x200;
constexpr std::size_t kCacheKeyAdds = 2;

constexpr std::size_t kIdStrSize = 0x30;

struct IdList {
    std::byte* data;
    int num;
    int size;
    std::int64_t granularity;
};
static_assert(sizeof(IdList) == 0x18);

using GetKeysFn = void (*)(IdList* lists, int device, int bindset, const char* action, bool localized);
using KeyTextFn = void (*)(int keyNum, void* out);
using StrAssignFn = void (*)(void* str, const char* text);
using KeyNameFn = const char* (*)(int keyNum);
using KeyNameTextFn = const char* (*)(int keyNum);

GetKeysFn g_getKeys = nullptr;
KeyTextFn g_keyText = nullptr;
StrAssignFn g_strAssign = nullptr;
KeyNameTextFn g_keyNameText = nullptr;

std::array<std::uintptr_t, kCallers.size()> g_callerReturns{};
// The KeyNameText callers answered (input::keyNameAnswer): TabList's and the command bar's hold buttons'.
input::KeyNameCallers g_keyNameCallers;
input::HudKeyCallers g_hudKeyCallers; // the weapon info row's labels (input::hudKeyForm)
static_assert(std::tuple_size_v<decltype(input::KeyNameCallers::tabList)> == kTabListCalls);
std::vector<std::pair<std::string, int>> g_actionTable;   // lower-case bind name, usercmd button index
std::vector<std::pair<int, std::string>> g_virtualKeys;   // key number and its name, in slot order
std::vector<std::pair<int, std::size_t>> g_layerKeys;     // key number and its kLayerKeyNames index
std::vector<std::pair<int, std::size_t>> g_tabKeys;       // key number and its kTabKeyNames index
std::vector<std::pair<int, std::string_view>> g_keyNames; // key number and its name, for the keys of both
std::uint64_t* g_bindHash = nullptr;
std::atomic<bool> g_installed{false};

std::atomic<std::shared_ptr<const input::PromptLabelSet>> g_labels;
// Every set published before the current one. KeyNameText hands the game a pointer into a set's text, which
// the caller copies after the detour has returned, so no set is ever freed (one per control map change).
std::vector<std::shared_ptr<const input::PromptLabelSet>> g_retiredLabels;

std::array<std::atomic<int>, kCallers.size()> g_logged{};
constexpr int kLogPerCaller = 6;
std::array<std::atomic<bool>, input::kLayerKeyNames.size()> g_layerKeyLogged{};

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return (x | 0x20) == (y | 0x20); });
}

// Our action for a bind name under the current state, or nothing.
std::optional<game::GameAction> actionForBind(const char* bind) {
    const std::string_view name(bind);
    for (const auto& [entry, index] : g_actionTable) {
        if (!equalsIgnoreCase(entry, name)) {
            continue;
        }
        if (auto demon = pilotedDemonAction(std::uint64_t{1} << index)) {
            return demon;
        }
        return game::actionForUsercmdButton(index);
    }
    return std::nullopt;
}

// The first list with an entry, moved to the front, cut to that one entry; false when all three are empty.
bool keepOneEntry(IdList* lists) {
    for (int i = 0; i < 3; ++i) {
        if (lists[i].data != nullptr && lists[i].num > 0 && lists[i].num <= lists[i].size) {
            if (i != 0) {
                std::swap(lists[0], lists[i]);
            }
            lists[0].num = 1;
            lists[1].num = 0;
            return true;
        }
    }
    return false;
}

void onGetKeys(IdList* lists, int device, int bindset, const char* action, bool localized) {
    g_getKeys(lists, device, bindset, action, localized);
    const auto returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto caller = std::ranges::find(g_callerReturns, returnAddress);
    if (caller == g_callerReturns.end() || device != 0 || lists == nullptr || action == nullptr ||
        !mp_guard::allowsGameTouch()) {
        return;
    }
    const std::size_t callerIndex = static_cast<std::size_t>(caller - g_callerReturns.begin());
    const std::shared_ptr<const input::PromptLabelSet> labels = g_labels.load(std::memory_order_acquire);
    const std::optional<game::GameAction> mapped = actionForBind(action);
    const std::size_t index = mapped ? static_cast<std::size_t>(*mapped) : 0;
    const bool named = labels && mapped && labels->slot[index] >= 0;
    if (g_logged[callerIndex].fetch_add(1, std::memory_order_relaxed) < kLogPerCaller) {
        EVR_LOG("%s: %s asks for %s (bindset %d): %s", kTag, kCallers[callerIndex].name, action, bindset,
                named ? labels->text[index].c_str()
                      : (mapped ? "no button for it" : "not one of our actions; the game's keys"));
    }
    if (!named) {
        return;
    }
    const auto slot = static_cast<std::size_t>(labels->slot[index]);
    const bool text = localized && callerIndex == kTextCaller;
    if (!text && slot >= g_virtualKeys.size()) {
        return;
    }
    if (keepOneEntry(lists)) {
        g_strAssign(lists[0].data, text ? labels->text[index].c_str() : g_virtualKeys[slot].second.c_str());
    }
}

void onKeyText(int keyNum, void* out) {
    const std::shared_ptr<const input::PromptLabelSet> labels = g_labels.load(std::memory_order_acquire);
    if (labels && out != nullptr && mp_guard::allowsGameTouch()) {
        for (std::size_t slot = 0; slot < g_virtualKeys.size() && slot < labels->distinct.size(); ++slot) {
            if (g_virtualKeys[slot].first == keyNum) {
                const auto at = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
                g_strAssign(out, hudKeyAnswer(slot, at, g_hudKeyCallers, *labels, kTag).c_str());
                return;
            }
        }
        for (const auto& [key, index] : g_layerKeys) {
            const std::string& text = input::layerKeyLabel(index, *labels);
            if (key == keyNum && !text.empty()) {
                if (!g_layerKeyLogged[index].exchange(true, std::memory_order_relaxed)) {
                    EVR_LOG("%s: the game's text names %.*s: %s", kTag,
                            static_cast<int>(input::kLayerKeyNames[index].size()),
                            input::kLayerKeyNames[index].data(), text.c_str());
                }
                g_strAssign(out, text.c_str());
                return;
            }
        }
    }
    g_keyText(keyNum, out);
}

// The set stays alive (g_retiredLabels) after it is replaced, so the text handed out outlives the call.
const char* onKeyNameText(int keyNum) {
    const auto returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const input::KeyNameAnswer answer = mp_guard::allowsGameTouch()
                                            ? input::keyNameAnswer(returnAddress, g_keyNameCallers)
                                            : input::KeyNameAnswer::Game;
    if (answer != input::KeyNameAnswer::Game) {
        const auto key = std::ranges::find(g_keyNames, keyNum, &std::pair<int, std::string_view>::first);
        const std::shared_ptr<const input::PromptLabelSet> labels = g_labels.load(std::memory_order_acquire);
        const std::string* text =
            key != g_keyNames.end() && labels ? input::keyNameLabel(answer, key->second, *labels) : nullptr;
        if (text != nullptr && !text->empty()) {
            logKeyName(answer, key->second, *text);
            return text->c_str();
        }
    }
    return g_keyNameText(keyNum);
}

// The return address of the single `call target` within kCallerSpan bytes of `function`, or 0.
std::uintptr_t callReturn(const GameImage& image, const std::byte* function, const std::byte* target) {
    const std::vector<std::uintptr_t> found = callReturns(image, function, target, kCallerSpan);
    return found.size() == 1 ? found.front() : 0;
}

// The bind name of a table row, or empty when the row's pointer is not a "_name" string in the module.
std::string_view rowName(const GameImage& image, const std::byte* row) {
    std::uintptr_t pointer = 0;
    std::memcpy(&pointer, row, sizeof(pointer));
    const auto* p = reinterpret_cast<const std::byte*>(pointer);
    if (!image.contains(p)) {
        return {};
    }
    const std::string_view name = stringAt(image, p, 48);
    return name.size() > 1 && name.front() == '_' ? name : std::string_view{};
}

// The game's action table: 16-byte rows of { const char* bind name, usercmd button index }, found by its
// "_attack1" row (index 0).
bool readActionTable(const GameImage& image) {
    const std::byte* attack = findUniqueString(image, "_attack1");
    const auto [data, size] = dataSection(image);
    if (attack == nullptr || data == nullptr) {
        EVR_LOG("%s: no _attack1 string or no .data section", kTag);
        return false;
    }
    const auto wanted = reinterpret_cast<std::uintptr_t>(attack);
    const std::byte* row = nullptr;
    for (std::size_t offset = 0; offset + 16 <= size; offset += 8) {
        std::uintptr_t value = 0;
        std::uint64_t index = 1;
        std::memcpy(&value, data + offset, sizeof(value));
        std::memcpy(&index, data + offset + 8, sizeof(index));
        if (value == wanted && index == 0) {
            if (row != nullptr) {
                EVR_LOG("%s: two rows name _attack1", kTag);
                return false;
            }
            row = data + offset;
        }
    }
    if (row == nullptr) {
        EVR_LOG("%s: no action table row for _attack1", kTag);
        return false;
    }
    while (row - 16 >= data && !rowName(image, row - 16).empty()) {
        row -= 16;
    }
    for (; row + 16 <= data + size; row += 16) {
        const std::string_view name = rowName(image, row);
        std::uint64_t index = 0;
        std::memcpy(&index, row + 8, sizeof(index));
        if (name.empty() || index >= 64) {
            break;
        }
        std::string lower(name);
        std::ranges::transform(lower, lower.begin(), [](char c) { return static_cast<char>(c | 0x20); });
        g_actionTable.emplace_back(std::move(lower), static_cast<int>(index));
    }
    EVR_LOG("%s: the game's action table has %zu binds", kTag, g_actionTable.size());
    return g_actionTable.size() >= 40;
}

bool readVirtualKeys(const GameImage& image) {
    const auto keyName = reinterpret_cast<KeyNameFn>(
        const_cast<std::byte*>(findUnique(image, kTag, "KeyNumToString", kKeyNameSignature)));
    if (keyName == nullptr) {
        return false;
    }
    for (const auto& [first, last] : kVirtualKeyRanges) {
        for (int key = first; key <= last; ++key) {
            const char* name = keyName(key);
            if (name == nullptr || std::string_view(name).rfind("STEAMVR_", 0) != 0) {
                EVR_LOG("%s: key 0x%X is \"%s\", not a VR key", kTag, key, name ? name : "");
                return false;
            }
            g_virtualKeys.emplace_back(key, name);
        }
    }
    for (int key = 1; key <= kLastKeyboardKey; ++key) {
        const char* name = keyName(key);
        if (const auto i = name != nullptr ? input::layerKeyIndex(name) : std::nullopt) {
            g_layerKeys.emplace_back(key, *i);
        }
        if (const auto answered = name != nullptr ? input::answeredKeyName(name) : std::nullopt) {
            g_keyNames.emplace_back(key, *answered);
        }
        if (const auto i = name != nullptr ? input::tabKeyIndex(name) : std::nullopt) {
            g_tabKeys.emplace_back(key, *i);
        }
    }
    EVR_LOG("%s: %zu of the %zu keys the layer presses found, %zu of the %zu tab keys", kTag,
            g_layerKeys.size(), input::kLayerKeyNames.size(), g_tabKeys.size(), input::kTabKeyNames.size());
    return true;
}

// The key text of the tab lists (input::kTabKeyNames) and of the command bar's hold buttons
// (prompt_command_bar.hpp), each optional: a part not found keeps the game's keys there.
void installKeyNameTextHook(const GameImage& image) {
    const std::byte* keyNameText = findUnique(image, kTag, "KeyNameText", kKeyNameTextSignature);
    if (keyNameText == nullptr) {
        EVR_LOG("%s: tab lists and hold buttons keep the game's keys (no KeyNameText)", kTag);
        return;
    }
    const std::byte* tabList = findUnique(image, kTag, "TabList update", kTabListSignature);
    const std::vector<std::uintptr_t> returns = tabList != nullptr
                                                    ? callReturns(image, tabList, keyNameText, kCallerSpan)
                                                    : std::vector<std::uintptr_t>{};
    if (tabList == nullptr) {
        EVR_LOG("%s: no TabList update; tab lists keep the game's keys", kTag);
    } else if (returns.size() != kTabListCalls) {
        EVR_LOG("%s: TabList update calls KeyNameText %zu times, not %zu; tab lists keep the game's keys",
                kTag, returns.size(), kTabListCalls);
    } else if (g_tabKeys.empty()) {
        EVR_LOG("%s: the game has no Q / E key; tab lists keep the game's keys", kTag);
    } else {
        std::ranges::copy(returns, g_keyNameCallers.tabList.begin());
    }
    if (!g_layerKeys.empty()) {
        const CommandBarCalls bar = findCommandBarKeyNameCalls(image, keyNameText, kTag);
        g_keyNameCallers.getKeyString = bar.getKeyString;
        g_keyNameCallers.commandBarUpdate = bar.update;
    }
    const PageKeyHooks pageKeys = installPageKeyHooks(image, keyNameText, tabList, kTag);
    g_keyNameCallers.rewards = pageKeys.rewards;
    g_keyNameCallers.tabListPages = pageKeys.tabLists;
    const auto rva = [&image](std::uintptr_t at) {
        return at != 0 ? image.rva(reinterpret_cast<const std::byte*>(at)) : 0u;
    };
    if (g_keyNameCallers.tabList[0] == 0 && g_keyNameCallers.getKeyString == 0 &&
        g_keyNameCallers.commandBarUpdate == 0 && g_keyNameCallers.rewards[0] == 0) {
        return;
    }
    std::string error;
    if (!installInlineHook(const_cast<std::byte*>(keyNameText), reinterpret_cast<void*>(&onKeyNameText),
                           reinterpret_cast<void**>(&g_keyNameText), error)) {
        g_keyNameCallers = {};
        EVR_LOG(
            "%s: KeyNameText hook at RVA 0x%X failed: %s; tab lists and hold buttons keep the game's keys",
            kTag, image.rva(keyNameText), error.c_str());
        return;
    }
    EVR_LOG("%s: KeyNameText hook at RVA 0x%X; TabList calls at 0x%X, 0x%X; hold buttons' calls at 0x%X "
            "(GetKeyString), 0x%X (the command bar's update); 0 keeps the game's keys",
            kTag, image.rva(keyNameText), rva(g_keyNameCallers.tabList[0]), rva(g_keyNameCallers.tabList[1]),
            rva(g_keyNameCallers.getKeyString), rva(g_keyNameCallers.commandBarUpdate));
}

bool findBindGeneration(const GameImage& image) {
    const std::byte* cacheKey = findUnique(image, kTag, "TooltipCacheKey", kCacheKeySignature);
    if (cacheKey == nullptr) {
        return false;
    }
    std::vector<const std::byte*> hashes;
    for (std::size_t i = 0; i + 7 <= kCacheKeyScan && image.inText(cacheKey + i, 7); ++i) {
        const std::byte* at = cacheKey + i;
        if (at[0] == std::byte{0x48} && at[1] == std::byte{0x03} && at[2] == std::byte{0x0D}) {
            hashes.push_back(ripTarget(image, at + 3, at + 7));
        }
    }
    if (hashes.size() != kCacheKeyAdds || hashes.back() == nullptr || !image.contains(hashes.back(), 8)) {
        EVR_LOG("%s: TooltipCacheKey adds %zu hashes, not the image and bind generations", kTag,
                hashes.size());
        return false;
    }
    g_bindHash = reinterpret_cast<std::uint64_t*>(const_cast<std::byte*>(hashes.back()));
    EVR_LOG("%s: bind generation hash at RVA 0x%X", kTag, image.rva(hashes.back()));
    return true;
}

// The game re-reads the prompts it has cached when the bind generation's hash changes.
void bumpBindGeneration() {
    if (g_bindHash != nullptr && mp_guard::allowsGameTouch()) {
        std::atomic_ref<std::uint64_t>(*g_bindHash).fetch_add(0x9E3779B97F4A7C15ull);
    }
}

// The trip listener (any thread, once): the detours give the game's own key names from the trip on, and the
// bump makes the game drop the prompts it cached with the VR buttons' names. The one write after a trip: it
// undoes the layer's own change, so nothing is written when no labels were ever given out. An atomic add, as
// above.
void bumpBindGenerationOnTrip() {
    const bool named = g_labels.load(std::memory_order_acquire) != nullptr;
    if (named) {
        std::atomic_ref<std::uint64_t>(*g_bindHash).fetch_add(0x9E3779B97F4A7C15ull);
    }
    EVR_LOG("%s: the multiplayer guard tripped: %s", kTag,
            named ? "bind generation bumped, so the game rebuilds its cached prompts with its own key names"
                  : "no VR button names were given out; the prompts are the game's");
}

} // namespace

bool installPromptHooks() {
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return false;
    }
    const std::byte* getKeys = findUnique(image, kTag, "GetKeysForAction", kGetKeysSignature);
    const std::byte* keyText = findUnique(image, kTag, "LocalizedKeyName", kKeyTextSignature);
    g_strAssign = reinterpret_cast<StrAssignFn>(
        const_cast<std::byte*>(findUnique(image, kTag, "idStr::operator=", kStrAssignSignature)));
    if (getKeys == nullptr || keyText == nullptr || g_strAssign == nullptr) {
        return false;
    }
    const std::byte* hudHelper = nullptr;
    for (std::size_t i = 0; i < kCallers.size(); ++i) {
        const std::byte* caller = findUnique(image, kTag, kCallers[i].name, kCallers[i].signature);
        g_callerReturns[i] = caller != nullptr ? callReturn(image, caller, getKeys) : 0;
        hudHelper = i == kHudHelperCaller ? caller : hudHelper;
        if (g_callerReturns[i] == 0) {
            EVR_LOG("%s: %s: no single call to GetKeysForAction; prompts left as the game's", kTag,
                    kCallers[i].name);
            return false;
        }
    }
    if (!readActionTable(image) || !readVirtualKeys(image)) {
        return false;
    }
    const bool generation = findBindGeneration(image);
    g_hudKeyCallers = findWeaponInfoKeyCalls(image, hudHelper, keyText, kTag);
    std::string error;
    if (!installInlineHook(const_cast<std::byte*>(getKeys), reinterpret_cast<void*>(&onGetKeys),
                           reinterpret_cast<void**>(&g_getKeys), error)) {
        EVR_LOG("%s: GetKeysForAction hook at RVA 0x%X failed: %s", kTag, image.rva(getKeys), error.c_str());
        return false;
    }
    if (!installInlineHook(const_cast<std::byte*>(keyText), reinterpret_cast<void*>(&onKeyText),
                           reinterpret_cast<void**>(&g_keyText), error)) {
        // The first detour still names buttons in the tooltip text; the HUD helpers show the VR key's name.
        EVR_LOG("%s: LocalizedKeyName hook at RVA 0x%X failed: %s", kTag, image.rva(keyText), error.c_str());
    }
    installKeyNameTextHook(image);
    installLoadTipsHooks(image, kTag);
    g_installed.store(true, std::memory_order_release);
    if (generation && !mp_guard::addTripListener(&bumpBindGenerationOnTrip)) {
        EVR_LOG("%s: no room in the multiplayer guard's trip listeners; cached prompts keep the VR buttons' "
                "names after a trip",
                kTag);
    }
    EVR_LOG(
        "%s: hooks at RVA 0x%X and 0x%X; callers at 0x%X, 0x%X, 0x%X, 0x%X; %zu VR keys; bind generation %s",
        kTag, image.rva(getKeys), image.rva(keyText),
        image.rva(reinterpret_cast<const std::byte*>(g_callerReturns[0])),
        image.rva(reinterpret_cast<const std::byte*>(g_callerReturns[1])),
        image.rva(reinterpret_cast<const std::byte*>(g_callerReturns[2])),
        image.rva(reinterpret_cast<const std::byte*>(g_callerReturns[3])), g_virtualKeys.size(),
        generation ? "found" : "not found (cached prompts keep their old text)");
    return true;
}

std::shared_ptr<const input::PromptLabelSet> currentPromptLabels() {
    return g_labels.load(std::memory_order_acquire);
}

void publishPromptLabels(const input::BindingProfile& profile, game::Controller controller) {
    if (!g_installed.load(std::memory_order_acquire)) {
        return;
    }
    const input::ControllerData& data = state().controllerData[static_cast<std::size_t>(controller)];
    auto labels = std::make_shared<const input::PromptLabelSet>(
        input::promptLabelSet(profile, input::buttonLabelsFor(data)));
    const std::shared_ptr<const input::PromptLabelSet> previous = g_labels.load(std::memory_order_acquire);
    if (previous && *previous == *labels) {
        return;
    }
    const auto text = [&labels](game::GameAction action) {
        const std::string& t = labels->text[static_cast<std::size_t>(action)];
        return t.empty() ? "none" : t.c_str();
    };
    EVR_LOG("%s: %s buttons: fire %s, weapon mod %s, jump %s, dash %s, Dossier %s, chainsaw %s (%zu texts)",
            kTag, std::string(game::controllerName(controller)).c_str(), text(game::GameAction::Fire),
            text(game::GameAction::WeaponMod), text(game::GameAction::Jump), text(game::GameAction::Dash),
            text(game::GameAction::Dossier), text(game::GameAction::Chainsaw), labels->distinct.size());
    const auto menuText = [](const std::string& t) {
        return t.empty() ? "none (the game's key)" : t.c_str();
    };
    EVR_LOG("%s: menu hints: back (ESC) %s, select (ENTER) %s, tabs (Q / E) %s / %s, pages (A / D) %s / %s",
            kTag, menuText(labels->menuBack), menuText(labels->menuSelect), menuText(labels->menuPreviousTab),
            menuText(labels->menuNextTab), menuText(labels->menuPreviousPage),
            menuText(labels->menuNextPage));
    if (labels->distinct.size() > g_virtualKeys.size()) {
        EVR_LOG("%s: %zu texts for %zu VR keys; the HUD prompts past them keep the game's keys", kTag,
                labels->distinct.size(), g_virtualKeys.size());
    }
    if (previous) {
        g_retiredLabels.push_back(previous);
    }
    g_labels.store(std::move(labels), std::memory_order_release);
    bumpBindGeneration();
}

} // namespace evr::vkcore::controllers
