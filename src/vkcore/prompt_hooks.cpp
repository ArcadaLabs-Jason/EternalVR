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
// whose text the detour on LocalizedKeyName then gives as the label. Every other caller gets the game's
// result: the weapon wheel and tutorial dismissal match real key presses with it, and the bindings menu
// lists the real binds. The action is found through the game's action table ("_altfire" is usercmd button
// 2), then our action that presses that button, or while piloting a demon the action that presses the
// demon's own binding. The game is held on its keyboard prompts (swf_platformOverride 2, runtime_cvars.hpp),
// so a gamepad or Steam Input cannot switch it to pad glyphs that name the pad's binds.
//
// Everything is found by signature and checked at install; if any part is missing nothing is installed and
// the prompts stay the game's.

#include "vkcore/controllers_impl.hpp"

#include "features/input/button_labels.hpp"
#include "game/eternal/usercmd_buttons.hpp"
#include "vkcore/demon_view.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"

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
// How far into each caller its one call to GetKeysForAction may lie (the text pass has it at +0x654).
constexpr std::size_t kCallerSpan = 0x1000;

// The VR keys the helpers are handed: STEAMVR_PRIMARY_* 0x131..0x13D, STEAMVR_SECONDARY_* 0x141..0x14D.
constexpr std::array<std::pair<int, int>, 2> kVirtualKeyRanges{{{0x131, 0x13D}, {0x141, 0x14D}}};

// Keys the layer presses for an action where the game reads keys, not commands (usercmd_hook.cpp: tutorial
// and lore popups take Space and E, the Dossier button presses Tab). A popup's fixed key token ("[SPACE] TO
// DISMISS") then names the button that sends the key.
constexpr std::array<std::pair<std::string_view, game::GameAction>, 3> kLayerKeys{{
    {"SPACE", game::GameAction::Jump},
    {"E", game::GameAction::Melee},
    {"TAB", game::GameAction::Dossier},
}};
constexpr int kLastKeyboardKey = 0xFF;

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

GetKeysFn g_getKeys = nullptr;
KeyTextFn g_keyText = nullptr;
StrAssignFn g_strAssign = nullptr;

std::array<std::uintptr_t, kCallers.size()> g_callerReturns{};
std::vector<std::pair<std::string, int>> g_actionTable;    // lower-case bind name, usercmd button index
std::vector<std::pair<int, std::string>> g_virtualKeys;    // key number and its name, in slot order
std::vector<std::pair<int, game::GameAction>> g_layerKeys; // key number and the action whose button sends it
std::uint64_t* g_bindHash = nullptr;
std::atomic<bool> g_installed{false};

std::atomic<std::shared_ptr<const input::PromptLabelSet>> g_labels;

std::array<std::atomic<int>, kCallers.size()> g_logged{};
constexpr int kLogPerCaller = 6;

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
                g_strAssign(out, labels->distinct[slot].c_str());
                return;
            }
        }
        for (const auto& [key, action] : g_layerKeys) {
            const std::string& text = labels->text[static_cast<std::size_t>(action)];
            if (key == keyNum && !text.empty()) {
                g_strAssign(out, text.c_str());
                return;
            }
        }
    }
    g_keyText(keyNum, out);
}

// The return address of the single `call target` within kCallerSpan bytes of `function`, or 0.
std::uintptr_t callReturn(const GameImage& image, const std::byte* function, const std::byte* target) {
    std::uintptr_t found = 0;
    int count = 0;
    for (std::size_t i = 0; i + 5 <= kCallerSpan && image.inText(function + i, 5); ++i) {
        const std::byte* at = function + i;
        if (at[0] == std::byte{0xE8} && at + 5 + readI32(at + 1) == target) {
            found = reinterpret_cast<std::uintptr_t>(at + 5);
            ++count;
        }
    }
    return count == 1 ? found : 0;
}

// The .data section of the module.
std::pair<const std::byte*, std::size_t> dataSection(const GameImage& image) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::memcmp(section->Name, ".data\0\0\0", 8) == 0 &&
            image.contains(image.base + section->VirtualAddress, section->Misc.VirtualSize)) {
            return {image.base + section->VirtualAddress, section->Misc.VirtualSize};
        }
    }
    return {nullptr, 0};
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
        for (const auto& [keyText, action] : kLayerKeys) {
            if (name != nullptr && keyText == name) {
                g_layerKeys.emplace_back(key, action);
            }
        }
    }
    EVR_LOG("%s: %zu of the %zu keys the layer presses found", kTag, g_layerKeys.size(), kLayerKeys.size());
    return true;
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
    for (std::size_t i = 0; i < kCallers.size(); ++i) {
        const std::byte* caller = findUnique(image, kTag, kCallers[i].name, kCallers[i].signature);
        g_callerReturns[i] = caller != nullptr ? callReturn(image, caller, getKeys) : 0;
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
    g_installed.store(true, std::memory_order_release);
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
    if (labels->distinct.size() > g_virtualKeys.size()) {
        EVR_LOG("%s: %zu texts for %zu VR keys; the HUD prompts past them keep the game's keys", kTag,
                labels->distinct.size(), g_virtualKeys.size());
    }
    g_labels.store(std::move(labels), std::memory_order_release);
    bumpBindGeneration();
}

} // namespace evr::vkcore::controllers
