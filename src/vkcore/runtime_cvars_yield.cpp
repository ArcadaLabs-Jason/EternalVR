// Holds that yield to the player's choice in the game's menu (runtime_cvars_impl.hpp, yieldTurn): the HUD
// Scale (stereo_seq::stereoHudScaleCvar). The layer holds it at Large until the game shows a value the layer
// did not write while the hold stood confirmed (mp_policy::YieldingHold); a profile load sets the player's
// stored value, which is not a choice (profileLoading). A choice is seen at the next present (yieldTurn) or
// at the next profile save, whichever comes first (playerChose), so the save that follows the menu writes it.

#include "platform/mp_policy/profile_cvars.hpp"
#include "platform/mp_policy/saved_cvars.hpp"
#include "vkcore/log.hpp"
#include "vkcore/profile_hooks.hpp"
#include "vkcore/runtime_cvars.hpp"
#include "vkcore/runtime_cvars_impl.hpp"

#include <string>
#include <string_view>

namespace evr::vkcore::runtime_cvars {

namespace {

Held* yieldingNamed(std::string_view name) {
    for (Held& h : heldList()) {
        if (h.yields && h.object && mp_policy::sameCvarName(h.name, name)) {
            return &h;
        }
    }
    return nullptr;
}

void logYield(const Held& h, const std::string& live, const char* when) {
    EVR_LOG("%s: %s %s, not written by the layer (held %s; %s): the player's choice in the game's menu, no "
            "longer held",
            kTag, h.name.c_str(), live.c_str(), h.value.c_str(), when);
}

} // namespace

bool yieldTurn(Held& h) {
    const std::string live = std::to_string(readValue(h.object));
    switch (h.yield.step(live, h.value)) {
    case mp_policy::YieldingHold::Step::Hold:
        return false;
    case mp_policy::YieldingHold::Step::Keep:
        return true;
    case mp_policy::YieldingHold::Step::Yield:
        logYield(h, live, "seen at a present");
        profilePlayerChoice(h.name, live);
        return true;
    }
    return true;
}

void profileLoading(std::string_view name) {
    if (Held* h = yieldingNamed(name)) {
        h->yield.profileLoaded();
    }
}

bool playerChose(std::string_view name, std::string_view live) {
    Held* h = yieldingNamed(name);
    if (!h || h->yield.step(live, h->value) != mp_policy::YieldingHold::Step::Yield) {
        return false;
    }
    logYield(*h, std::string(live), "seen at a profile save");
    return true;
}

} // namespace evr::vkcore::runtime_cvars
