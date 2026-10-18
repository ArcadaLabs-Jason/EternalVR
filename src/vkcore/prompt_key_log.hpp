#pragma once

// The log of the game's raw key texts the KeyNameText detour answers (prompt_hooks.cpp): each key once per
// kind of caller.

#include "features/input/prompt_keys.hpp"

#include <string>
#include <string_view>

namespace evr::vkcore::controllers {

// The first time a raw key text is answered: "prompts: a tab list names Q: LG", "prompts: a hold button names
// ENTER: Right Trigger", "prompts: the Rewards tab's arrows name A: LS". Any thread.
void logKeyName(input::KeyNameAnswer answer, std::string_view name, const std::string& text);

} // namespace evr::vkcore::controllers
