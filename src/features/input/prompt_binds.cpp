#include "features/input/prompt_binds.hpp"

namespace evr::input {

std::optional<std::size_t> moveTextIndex(int usercmdButton) {
    switch (usercmdButton) {
    case kMoveForwardButton:
        return 0;
    case kMoveLeftButton:
        return 1;
    case kMoveBackButton:
        return 2;
    case kMoveRightButton:
        return 3;
    default:
        return std::nullopt;
    }
}

BindAnswer bindAnswer(std::optional<game::GameAction> action,
                      std::optional<int> button,
                      bool textPass,
                      const PromptLabelSet& labels) {
    if (action) {
        const auto index = static_cast<std::size_t>(*action);
        if (index < labels.text.size() && labels.slot[index] >= 0 && !labels.text[index].empty()) {
            return {&labels.text[index], labels.slot[index]};
        }
        return {};
    }
    const std::optional<std::size_t> move = button ? moveTextIndex(*button) : std::nullopt;
    if (textPass && move && !labels.moveText[*move].empty()) {
        return {&labels.moveText[*move], -1};
    }
    return {};
}

} // namespace evr::input
