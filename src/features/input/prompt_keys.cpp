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

KeyNameAnswer keyNameAnswer(std::uintptr_t returnAddress, const KeyNameCallers& callers) {
    if (returnAddress == 0) {
        return KeyNameAnswer::Game;
    }
    if (returnAddress == callers.tabList[0] || returnAddress == callers.tabList[1] ||
        returnAddress == callers.loadTips[0] || returnAddress == callers.loadTips[1]) {
        return KeyNameAnswer::TabKey;
    }
    if (returnAddress == callers.getKeyString || returnAddress == callers.commandBarUpdate) {
        return KeyNameAnswer::LayerKey;
    }
    return KeyNameAnswer::Game;
}

const std::string*
keyNameLabel(KeyNameAnswer answer, std::string_view gameKeyName, const PromptLabelSet& labels) {
    if (answer == KeyNameAnswer::TabKey) {
        const std::optional<std::size_t> index = tabKeyIndex(gameKeyName);
        return index ? &tabKeyLabel(*index, labels) : nullptr;
    }
    if (answer == KeyNameAnswer::LayerKey) {
        const std::optional<std::size_t> index = layerKeyIndex(gameKeyName);
        return index ? &layerKeyLabel(*index, labels) : nullptr;
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
