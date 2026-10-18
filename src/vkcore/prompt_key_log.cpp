#include "vkcore/prompt_key_log.hpp"

#include "vkcore/log.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

namespace evr::vkcore::controllers {

namespace {

std::array<std::atomic<bool>, input::kTabKeyNames.size()> g_tabKeyLogged{};
std::array<std::atomic<bool>, input::kLayerKeyNames.size()> g_holdKeyLogged{};
std::array<std::atomic<bool>, input::kPageKeyNames.size()> g_pageKeyLogged{};
std::array<std::atomic<bool>, input::kPageKeyNames.size()> g_tabPageKeyLogged{};

} // namespace

void logKeyName(input::KeyNameAnswer answer, std::string_view name, const std::string& text) {
    std::optional<std::size_t> index;
    std::atomic<bool>* logged = nullptr;
    const char* who = "";
    if (answer == input::KeyNameAnswer::TabKey || answer == input::KeyNameAnswer::TabKeyOnly) {
        index = input::tabKeyIndex(name);
        logged = index ? &g_tabKeyLogged[*index] : nullptr;
        if (!index && answer == input::KeyNameAnswer::TabKey) {
            index = input::pageKeyIndex(name); // a tab list on A / D
            logged = index ? &g_tabPageKeyLogged[*index] : nullptr;
        }
        who = "a tab list names";
    } else if (answer == input::KeyNameAnswer::LayerKey) {
        index = input::layerKeyIndex(name);
        logged = index ? &g_holdKeyLogged[*index] : nullptr;
        who = "a hold button names";
    } else if (answer == input::KeyNameAnswer::PageKey) {
        index = input::pageKeyIndex(name);
        logged = index ? &g_pageKeyLogged[*index] : nullptr;
        who = "the Rewards tab's arrows name";
    }
    if (logged != nullptr && !logged->exchange(true, std::memory_order_relaxed)) {
        EVR_LOG("prompts: %s %.*s: %s", who, static_cast<int>(name.size()), name.data(), text.c_str());
    }
}

} // namespace evr::vkcore::controllers
