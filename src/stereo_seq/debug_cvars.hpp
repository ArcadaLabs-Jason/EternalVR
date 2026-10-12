#pragma once

// ETERNALVR_DEBUG_CVARS="name=value;name=value" (rig experiments; vkcore/runtime_cvars.hpp writes its entries
// at run time): an entry wins over the layer's own holds of the same cvar. runtime_cvars.cpp puts it in place
// of any entry of its held list for that cvar (replaceSameName), and per-eye TAA (vkcore/taa_hooks.cpp),
// which writes its own cvars on every stereo tick, leaves the cvars the list holds alone (debugCvarNames),
// r_SSR's hold included (debugCvarsTakeSsr).

#include "stereo_seq/seq_settings.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace evr::stereo_seq {

// The cvars the list holds (parseCvarList): each item the layer writes, a plain value or a cap "<=N" whose N
// parses (parseCvarCap). A "name=?" item only logs the value, so it takes nothing; nor does a cap that does
// not parse (left alone).
std::vector<std::string> debugCvarNames(std::string_view list);

// Where `name` is in `names` (compared as sameCvarName); nullopt when it is not.
std::optional<std::size_t> findCvarName(const std::vector<std::string>& names, std::string_view name);

// The names hold r_SSR or r_raytracedReflectionsTemporalUpscaleQuality, which tells per-eye TAA's r_SSR hold
// when the game's Reflections setting ran (setting_follow.hpp, SsrHold): r_SSR is then not held, and the hold
// never reports that it follows the game's setting (the status file's ssr_follow stays 0).
bool debugCvarsTakeSsr(const std::vector<std::string>& names);

// Puts `entry` in `held` in place of the first entry for the same cvar (its `name`, compared as sameCvarName)
// and drops any later one, so only `entry` writes that cvar; appended when there is none.
template <class Entry>
void replaceSameName(std::vector<Entry>& held, Entry entry) {
    const std::string name = entry.name; // entry is moved into place below
    bool placed = false;
    for (std::size_t i = 0; i < held.size();) {
        if (!sameCvarName(held[i].name, name)) {
            ++i;
        } else if (!placed) {
            held[i] = std::move(entry);
            placed = true;
            ++i;
        } else {
            held.erase(held.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
    if (!placed) {
        held.push_back(std::move(entry));
    }
}

} // namespace evr::stereo_seq
