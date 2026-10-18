#include "features/input/prompt_keys.hpp"

#include "game/eternal/game_action.hpp"

#include <algorithm>
#include <vector>

namespace evr::input {

namespace {

const std::string& actionText(game::GameAction action, const PromptLabelSet& labels) {
    return labels.text[static_cast<std::size_t>(action)];
}

template <std::size_t N>
std::optional<std::size_t> indexOf(const std::array<std::string_view, N>& names, std::string_view name) {
    for (std::size_t i = 0; i < N; ++i) {
        if (names[i] == name) {
            return i;
        }
    }
    return std::nullopt;
}

const std::string kNone;

} // namespace

std::optional<std::size_t> layerKeyIndex(std::string_view gameKeyName) {
    return indexOf(kLayerKeyNames, gameKeyName);
}

std::optional<std::size_t> tabKeyIndex(std::string_view gameKeyName) {
    return indexOf(kTabKeyNames, gameKeyName);
}

std::optional<std::size_t> pageKeyIndex(std::string_view gameKeyName) {
    return indexOf(kPageKeyNames, gameKeyName);
}

std::optional<std::string_view> answeredKeyName(std::string_view gameKeyName) {
    if (const auto i = layerKeyIndex(gameKeyName)) {
        return kLayerKeyNames[*i];
    }
    if (const auto i = tabKeyIndex(gameKeyName)) {
        return kTabKeyNames[*i];
    }
    if (const auto i = pageKeyIndex(gameKeyName)) {
        return kPageKeyNames[*i];
    }
    return std::nullopt;
}

const std::string& layerKeyLabel(std::size_t index, const PromptLabelSet& labels) {
    const std::string_view name = index < kLayerKeyNames.size() ? kLayerKeyNames[index] : std::string_view{};
    if (name == "SPACE") {
        return actionText(game::GameAction::Jump, labels);
    }
    if (name == "E") {
        return actionText(game::GameAction::Melee, labels);
    }
    if (name == "TAB") {
        return actionText(game::GameAction::Dossier, labels);
    }
    if (name == "ESCAPE") {
        return labels.menuBack;
    }
    if (name == "ENTER") {
        return labels.menuSelect;
    }
    return kNone;
}

const std::string& tabKeyLabel(std::size_t index, const PromptLabelSet& labels) {
    const std::string_view name = index < kTabKeyNames.size() ? kTabKeyNames[index] : std::string_view{};
    if (name == "Q") {
        return labels.menuPreviousTab;
    }
    if (name == "E") {
        return labels.menuNextTab;
    }
    return kNone;
}

const std::string& pageKeyLabel(std::size_t index, const PromptLabelSet& labels) {
    const std::string_view name = index < kPageKeyNames.size() ? kPageKeyNames[index] : std::string_view{};
    if (name == "A") {
        return labels.menuPreviousPage;
    }
    if (name == "D") {
        return labels.menuNextPage;
    }
    return kNone;
}

KeyNameAnswer keyNameAnswer(std::uintptr_t returnAddress, const KeyNameCallers& callers) {
    if (returnAddress == 0) {
        return KeyNameAnswer::Game;
    }
    if (returnAddress == callers.tabList[0] || returnAddress == callers.tabList[1]) {
        return callers.tabListPages ? KeyNameAnswer::TabKey : KeyNameAnswer::TabKeyOnly;
    }
    if (returnAddress == callers.loadTips[0] || returnAddress == callers.loadTips[1] ||
        returnAddress == callers.viewer[0] || returnAddress == callers.viewer[1]) {
        return KeyNameAnswer::TabKey;
    }
    if (returnAddress == callers.getKeyString || returnAddress == callers.commandBarUpdate) {
        return KeyNameAnswer::LayerKey;
    }
    if (returnAddress == callers.rewards[0] || returnAddress == callers.rewards[1]) {
        return KeyNameAnswer::PageKey;
    }
    return KeyNameAnswer::Game;
}

const std::string*
keyNameLabel(KeyNameAnswer answer, std::string_view gameKeyName, const PromptLabelSet& labels) {
    if (answer == KeyNameAnswer::TabKey || answer == KeyNameAnswer::TabKeyOnly) {
        if (const std::optional<std::size_t> index = tabKeyIndex(gameKeyName)) {
            return &tabKeyLabel(*index, labels);
        }
        if (answer == KeyNameAnswer::TabKeyOnly) {
            return nullptr;
        }
        answer = KeyNameAnswer::PageKey; // a tab list on A / D
    }
    if (answer == KeyNameAnswer::LayerKey) {
        const std::optional<std::size_t> index = layerKeyIndex(gameKeyName);
        return index ? &layerKeyLabel(*index, labels) : nullptr;
    }
    if (answer == KeyNameAnswer::PageKey) {
        const std::optional<std::size_t> index = pageKeyIndex(gameKeyName);
        return index ? &pageKeyLabel(*index, labels) : nullptr;
    }
    return nullptr;
}

HudKeyForm
hudKeyForm(std::uintptr_t returnAddress, std::uintptr_t callerReturn, const HudKeyCallers& callers) {
    if (returnAddress == 0 || callerReturn == 0 || returnAddress != callers.helperKeyText) {
        return HudKeyForm::Full;
    }
    return std::ranges::find(callers.weaponInfo, callerReturn) != callers.weaponInfo.end() ? HudKeyForm::Short
                                                                                           : HudKeyForm::Full;
}

const std::string* hudKeyText(std::size_t slot, HudKeyForm form, const PromptLabelSet& labels) {
    const std::vector<std::string>& texts =
        form == HudKeyForm::Short ? labels.distinctShort : labels.distinct;
    return slot < texts.size() ? &texts[slot] : nullptr;
}

} // namespace evr::input
